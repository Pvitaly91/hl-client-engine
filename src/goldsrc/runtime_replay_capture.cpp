#include <hlclient/goldsrc/runtime_replay_capture.hpp>

#include <hlclient/goldsrc/entity_baseline_decoder.hpp>
#include <hlclient/goldsrc/runtime_control_decoder.hpp>
#include <hlclient/goldsrc/stock_runtime_transport_replay.hpp>

#include <algorithm>
#include <memory>
#include <new>
#include <ranges>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] RuntimeReplayCaptureLoadResult failure(
    RuntimeReplayCaptureError error)
{
    return {std::nullopt, std::move(error)};
}

[[nodiscard]] OwnedServicePayload owning_runtime_payload(
    const StockRuntimeReplayedPayload& payload)
{
    OwnedServicePayload result;
    result.bytes.assign(payload.bytes().begin(), payload.bytes().end());
    result.source_sequence = payload.source_sequence();
    result.source_acknowledgement = payload.source_acknowledgement();
    result.source_reliable = payload.reliable();
    result.reassembled = payload.reassembled();
    // StockRuntimeTransportReplay has already performed the optional bounded
    // envelope transform. The runtime decoder's flag means that this owning
    // buffer is ready for semantic decoding, including the identity transform
    // for an originally uncompressed payload. The transport summary retains
    // the count of payloads that actually required decompression.
    result.decompressed = true;
    result.acknowledgement_reliable = payload.acknowledgement_reliable();
    result.direction = payload.direction();
    return result;
}

[[nodiscard]] bool transport_padding_only(
    const StockRuntimeReplayedPayload& payload) noexcept
{
    return payload.bytes().size() <= 8U &&
        std::ranges::all_of(payload.bytes(), [](const std::byte value) {
            return value == std::byte{0U};
        });
}

[[nodiscard]] std::optional<std::uint8_t> opcode_at(
    const OwnedServicePayload& payload,
    const StockRuntimeSourceCursor& cursor) noexcept
{
    if (!cursor.byte_aligned() || cursor.byte_offset() >= payload.bytes.size()) {
        return std::nullopt;
    }
    return std::to_integer<std::uint8_t>(payload.bytes[cursor.byte_offset()]);
}

} // namespace

RuntimeReplayCaptureState::RuntimeReplayCaptureState(
    RuntimeReplayInitialization initialization,
    std::vector<RuntimeReplayRecord> records,
    const std::size_t baseline_consumed_bits,
    RuntimeReplayCaptureSummary summary,
    std::shared_ptr<const ServerInfoState> server_info,
    std::shared_ptr<const ResourceListState> resources,
    std::vector<double> presentation_offsets) noexcept
    : initialization_{std::move(initialization)}, records_{std::move(records)},
      baseline_consumed_bits_{baseline_consumed_bits},
      summary_{std::move(summary)}, server_info_{std::move(server_info)},
      resources_{std::move(resources)}, presentation_offsets_{std::move(presentation_offsets)}
{
}

const RuntimeReplayInitialization&
RuntimeReplayCaptureState::initialization() const noexcept
{
    return initialization_;
}

const std::vector<RuntimeReplayRecord>&
RuntimeReplayCaptureState::records() const noexcept
{
    return records_;
}

std::size_t RuntimeReplayCaptureState::baseline_consumed_bits() const noexcept
{
    return baseline_consumed_bits_;
}

const RuntimeReplayCaptureSummary&
RuntimeReplayCaptureState::summary() const noexcept
{
    return summary_;
}

RuntimeReplayCaptureLoadResult RuntimeReplayCaptureLoader::load(
    const std::filesystem::path& exact_functional_run_directory) const
{
    const auto corpus = StockRuntimeCaptureCorpusLoader{}.load(
        exact_functional_run_directory,
        StockRuntimeCaptureCorpusLoadPolicy::functional_capture);
    if (!corpus || !corpus.state) {
        RuntimeReplayCaptureError error;
        error.code = RuntimeReplayCaptureErrorCode::corpus_load_failed;
        if (corpus.error) {
            error.corpus_error = corpus.error->code;
            error.context = corpus.error->context;
        } else {
            error.context = "functional capture corpus publication is absent";
        }
        return failure(std::move(error));
    }

    const auto transport = StockRuntimeTransportReplay{}.replay(*corpus.state);
    if (!transport || !transport.state) {
        RuntimeReplayCaptureError error;
        error.code = RuntimeReplayCaptureErrorCode::transport_replay_failed;
        if (transport.error) {
            error.transport_error = transport.error->code;
            error.replay_payload_ordinal = transport.error->delivery_ordinal;
            error.context = transport.error->context;
        } else {
            error.context = "offline transport replay state is absent";
        }
        return failure(std::move(error));
    }

    const auto signon = StockCapturedSignonReplay{}.replay(*transport.state);
    if (!signon || !signon.state) {
        RuntimeReplayCaptureError error;
        error.code = RuntimeReplayCaptureErrorCode::signon_replay_failed;
        if (signon.error) {
            error.signon_error = signon.error->code;
            error.replay_payload_ordinal = signon.error->replay_payload_ordinal;
            error.context = signon.error->context;
        } else {
            error.context = "captured sign-on replay state is absent";
        }
        return failure(std::move(error));
    }
    if (!signon.state->known_signon_validated() ||
        !signon.state->server_info() || !signon.state->delta_registry() ||
        !signon.state->resources()) {
        return failure(RuntimeReplayCaptureError{
            RuntimeReplayCaptureErrorCode::initialization_context_missing,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, std::nullopt,
            signon.state->cursor().replay_payload_ordinal, {},
            "sign-on boundary did not retain its decoded server/schema context"});
    }

    constexpr std::uint64_t generation = 1U;
    const auto& payloads = transport.state->payloads();
    std::optional<EntityBaselineDecodeResult> decoded_baseline;
    std::size_t baseline_payload_ordinal = 0U;
    std::vector<RuntimeReplayRecord> records;
    std::vector<double> presentation_offsets;
    std::optional<std::uint64_t> first_observed_us;
    std::uint64_t available_us = 0U;
    std::size_t delivered_offset = 0U;
    try {
        records.reserve(payloads.size());
        const auto first_payload = signon.state->cursor().replay_payload_ordinal;
        for (std::size_t payload_ordinal = first_payload;
             payload_ordinal < payloads.size(); ++payload_ordinal) {
            const auto& replayed = payloads[payload_ordinal];
            // Replay provenance names the datagram which completed reassembly.
            // Prefix maximum also handles reordered delivery without pretending
            // the capture's observation clock is an actual delivery clock.
            const auto& delivered = corpus.state->delivered_datagrams();
            while (delivered_offset < delivered.size() &&
                   delivered[delivered_offset].delivery_ordinal() <= replayed.delivery_ordinal()) {
                available_us = std::max(available_us,
                    delivered[delivered_offset++].observed_relative_timestamp_us());
            }
            if (replayed.direction() != NetchanDirection::server_to_client ||
                transport_padding_only(replayed)) {
                continue;
            }
            auto payload = owning_runtime_payload(replayed);
            auto cursor = StockRuntimeSourceCursor::create(
                payload_ordinal == first_payload
                    ? signon.state->cursor().byte_offset
                    : 0U,
                payload_ordinal == first_payload
                    ? signon.state->cursor().bit_offset
                    : 0U,
                payload.bytes.size());
            if (!cursor) {
                return failure(RuntimeReplayCaptureError{
                    RuntimeReplayCaptureErrorCode::initialization_context_missing,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                    std::nullopt, std::nullopt, payload_ordinal, {},
                    "post-resource cursor is outside its owning payload"});
            }

            if (!decoded_baseline) {
                std::size_t control_ordinal = 0U;
                while (cursor->absolute_bit_offset() < payload.bytes.size() * 8U) {
                    const auto opcode = opcode_at(payload, *cursor);
                    if (!opcode) {
                        return failure(RuntimeReplayCaptureError{
                            RuntimeReplayCaptureErrorCode::
                                unsupported_initialization_message,
                            std::nullopt, std::nullopt, std::nullopt,
                            std::nullopt, std::nullopt, std::nullopt,
                            payload_ordinal, *cursor,
                            "non-byte-aligned initialization message has no supported framing"});
                    }
                    if (*opcode == kGoldSrcSvcSpawnBaselineOpcode) {
                        const auto body_cursor = StockRuntimeSourceCursor::create(
                            cursor->byte_offset() + 1U, 0U,
                            payload.bytes.size());
                        if (!body_cursor) {
                            return failure(RuntimeReplayCaptureError{
                                RuntimeReplayCaptureErrorCode::baseline_decode_failed,
                                std::nullopt, std::nullopt, std::nullopt,
                                EntityBaselineDecodeErrorCode::invalid_cursor,
                                std::nullopt, *opcode, payload_ordinal, *cursor,
                                "svc_spawnbaseline body cursor is invalid"});
                        }
                        auto baseline = GoldSrcEntityBaselineDecoder{}.decode(
                            EntityBaselineDecodeInput{
                                &payload, *body_cursor, payload_ordinal,
                                generation,
                                signon.state->server_info()
                                    ->maximum_clients().value()},
                            *signon.state->delta_registry());
                        if (!baseline || !baseline.registry) {
                            RuntimeReplayCaptureError error;
                            error.code = RuntimeReplayCaptureErrorCode::
                                baseline_decode_failed;
                            error.replay_payload_ordinal = payload_ordinal;
                            error.cursor = *body_cursor;
                            error.unsupported_opcode = *opcode;
                            if (baseline.error) {
                                error.baseline_error = baseline.error->code;
                                error.context = baseline.error->context;
                            } else {
                                error.context = "baseline decoder state is absent";
                            }
                            return failure(std::move(error));
                        }
                        baseline_payload_ordinal = payload_ordinal;
                        cursor = baseline.end_cursor;
                        decoded_baseline.emplace(std::move(baseline));
                        break;
                    }

                    const auto control = RuntimeControlDecoder{}.decode_one(
                        RuntimeControlDecodeInput{
                            payload, *cursor, generation, payload_ordinal},
                        control_ordinal++);
                    if (!control || !control.event) {
                        RuntimeReplayCaptureError error;
                        error.code = RuntimeReplayCaptureErrorCode::
                            unsupported_initialization_message;
                        error.replay_payload_ordinal = payload_ordinal;
                        error.cursor = *cursor;
                        error.unsupported_opcode = *opcode;
                        if (control.error) {
                            error.control_error = control.error->code;
                            error.context = control.error->context;
                            error.context += ";payload-bytes=" +
                                std::to_string(payload.bytes.size());
                            if (*opcode == 23U &&
                                cursor->byte_offset() + 1U < payload.bytes.size()) {
                                error.context += ";temp-entity-type=" +
                                    std::to_string(std::to_integer<std::uint8_t>(
                                        payload.bytes[cursor->byte_offset() + 1U]));
                            }
                        } else {
                            error.context = "initialization control decoder state is absent";
                        }
                        return failure(std::move(error));
                    }
                    cursor = control.event->provenance.end_cursor;
                }
                if (!decoded_baseline) {
                    continue;
                }
            }

            if (cursor->absolute_bit_offset() < payload.bytes.size() * 8U) {
                // RuntimeReplaySession requires a non-zero monotonically
                // increasing ordinal, but not a dense one. Retaining the
                // transport payload ordinal (+1 for the public non-zero
                // contract) keeps the canonical observation provenance tied
                // to the exact replayed payload without renumbering gaps that
                // belong to client-to-server or padding payloads.
                const auto ordinal = payload_ordinal + 1U;
                records.push_back(RuntimeReplayRecord{
                    generation, static_cast<std::uint64_t>(ordinal), ordinal,
                    std::move(payload), *cursor});
                if (!first_observed_us) { first_observed_us = available_us; }
                presentation_offsets.push_back(
                    static_cast<double>(available_us - *first_observed_us) / 1'000'000.0);
            }
        }
    } catch (const std::bad_alloc&) {
        return failure(RuntimeReplayCaptureError{
            RuntimeReplayCaptureErrorCode::allocation_failed, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, 0U, {},
            "unable to retain capture-backed runtime input"});
    }

    if (!decoded_baseline || !decoded_baseline->registry) {
        return failure(RuntimeReplayCaptureError{
            RuntimeReplayCaptureErrorCode::baseline_missing, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, signon.state->cursor().replay_payload_ordinal,
            StockRuntimeSourceCursor::create(
                signon.state->cursor().byte_offset,
                signon.state->cursor().bit_offset,
                signon.state->cursor().source_payload_byte_count).value_or(
                    StockRuntimeSourceCursor{}),
            "complete service-payload sequence contains no framed svc_spawnbaseline"});
    }
    if (records.empty()) {
        return failure(RuntimeReplayCaptureError{
            RuntimeReplayCaptureErrorCode::runtime_payload_missing,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, std::nullopt, baseline_payload_ordinal,
            decoded_baseline->end_cursor,
            "no server runtime payload remains after baseline initialization"});
    }

    RuntimeReplayInitialization initialization;
    initialization.generation = generation;
    initialization.max_clients =
        signon.state->server_info()->maximum_clients().value();
    initialization.schemas = signon.state->delta_registry();
    std::shared_ptr<const ServerInfoState> retained_server_info;
    try {
        retained_server_info=std::make_shared<const ServerInfoState>(*signon.state->server_info());
        initialization.user_message_definitions.assign(
            signon.state->user_message_definitions().begin(),
            signon.state->user_message_definitions().end());
        initialization.baselines =
            std::make_shared<const EntityBaselineRegistryState>(
                std::move(*decoded_baseline->registry));
    } catch (const std::bad_alloc&) {
        return failure(RuntimeReplayCaptureError{
            RuntimeReplayCaptureErrorCode::allocation_failed, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, baseline_payload_ordinal,
            decoded_baseline->end_cursor,
            "unable to retain decoded initialization context"});
    }

    RuntimeReplayCaptureSummary summary;
    summary.run_id = std::string{corpus.state->run_id()};
    summary.corpus_structural_sha256 =
        std::string{corpus.state->structural_sha256()};
    summary.delivered_datagram_count =
        corpus.state->delivered_datagrams().size();
    summary.replayed_payload_count = payloads.size();
    summary.reassembled_payload_count =
        transport.state->reassembled_payload_count();
    summary.decompressed_payload_count =
        transport.state->decompressed_payload_count();
    summary.decoded_server_signon_payload_count =
        signon.state->decoded_server_signon_payload_count();
    summary.schema_count = initialization.schemas->schema_count();
    summary.baseline_entity_count = decoded_baseline->entity_count;
    summary.baseline_instanced_count = decoded_baseline->instanced_count;
    summary.runtime_record_count = records.size();
    summary.signon_cursor = signon.state->cursor();
    summary.baseline_end_cursor = decoded_baseline->end_cursor;
    summary.baseline_payload_ordinal = baseline_payload_ordinal;
    summary.baseline_source_sequence =
        payloads[baseline_payload_ordinal].source_sequence();
    return RuntimeReplayCaptureLoadResult{
        RuntimeReplayCaptureState{
            std::move(initialization), std::move(records),
            decoded_baseline->bits_consumed, std::move(summary),
            std::move(retained_server_info),
            signon.state->resources(), std::move(presentation_offsets)},
        std::nullopt};
}

} // namespace hlclient::goldsrc
