#include <algorithm>
#include <array>
#include <filesystem>
#include <hlclient/goldsrc/reference_client_move.hpp>
#include <hlclient/goldsrc/stock_captured_signon_replay.hpp>
#include <iostream>
#include <string_view>
#include "transport_self_test.hpp"

namespace g = hlclient::goldsrc;
int main(int argc, char **argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--self-test-transport") {
        const auto r = hlclient::tools::reference_move_transport_self_test();
        std::cout << "reference_move_transport result=" << (r.passed ? "reference_client_move_transport_integrated" : "reference_move_planner_integrated_transport_pending")
            << " queued=" << r.queued << " new_submitted=" << r.new_submitted << " backup_submitted=" << r.backup_submitted
            << " packets_sent=" << r.packets_sent << " packets_received=" << r.packets_received
            << " checksum_matched=" << r.checksum_matched << " expected_commands=" << r.expected_commands
            << " dropped=" << r.dropped << " would_block=" << r.would_block << " retries=" << r.retries
            << " stale_contexts=" << r.stale_contexts << " history_revision=" << r.history_revision
            << " cleanup=" << r.cleanup << " stock_server_acceptance=not_tested failure=" << r.failure << '\n';
        return r.passed ? 0 : 1;
    }
    if (argc != 3 || std::string_view{argv[1]} != "--capture") {
        std::cerr << "usage: hlclient_client_move_check --capture "
                     "<existing-functional-run> | --self-test-transport\n";
        return 2;
    }
    try {
        const auto corpus = g::StockRuntimeCaptureCorpusLoader{}.load(
            std::filesystem::path{argv[2]},
            g::StockRuntimeCaptureCorpusLoadPolicy::functional_capture);
        if (!corpus) {
            std::cerr << "client_move_check corpus_error="
                      << g::to_string(corpus.error->code) << '\n';
            return 1;
        }
        const auto transport = g::StockRuntimeTransportReplay{}.replay(*corpus.state);
        if (!transport) {
            std::cerr << "client_move_check transport_error="
                      << g::to_string(transport.error->code) << '\n';
            return 1;
        }
        const auto signon = g::StockCapturedSignonReplay{}.replay(*transport.state);
        if (!signon) {
            std::cerr << "client_move_check signon_error="
                      << g::to_string(signon.error->code) << '\n';
            return 1;
        }
        auto binding = g::bind_goldsrc_usercmd_schema(
            *signon.state->delta_registry(),
            g::GoldSrcUserCmdSchemaBindingProfile::public_goldsrc48_usercmd_schema_v1);
        if (!binding) {
            std::cerr << "client_move_check schema_binding_failed field="
                      << binding.error->field_index.value_or(999) << '\n';
            return 1;
        }
        const g::GoldSrcReferenceClientMoveCodec codec{std::move(*binding.binding), 1};
        std::size_t clients = 0, moves = 0, backups = 0, new_commands = 0,
                    unsupported = 0, checksum_failed = 0, failures = 0, semantic = 0,
                    exact = 0;
        std::size_t last_payload = 0, last_end = 0, zero_msec = 0, negative_move = 0,
                    attack = 0, jump = 0;
        std::size_t redundant_fields = 0, nonminimal_masks = 0,
                    nonexact_with_redundancy = 0;
        std::array<std::size_t, 15> nonzero{};
        std::array<std::size_t, 12> opcodes{};
        std::uint64_t hash = 14695981039346656037ULL;
        const auto mix = [&](std::uint64_t value) {
            for (unsigned b = 0; b < 8; ++b) {
                hash ^= (value >> (8 * b)) & 255U;
                hash *= 1099511628211ULL;
            }
        };
        const auto &payloads = transport.state->payloads();
        for (std::size_t p = 0; p < payloads.size(); ++p) {
            const auto &payload = payloads[p];
            if (payload.direction() != g::NetchanDirection::client_to_server)
                continue;
            ++clients;
            g::ReferenceMoveSource source{1,
                                          p + 1,
                                          payload.delivery_ordinal(),
                                          payload.corpus_observed_ordinal(),
                                          payload.source_sequence(),
                                          payload.reliable(),
                                          payload.fragmented(),
                                          payload.reassembled(),
                                          payload.source_fragment_count(),
                                          payload.kind()};
            auto decoded =
                g::decode_reference_client_payload(codec, payload.bytes(), source);
            for (std::size_t i = 0; i < opcodes.size(); ++i)
                opcodes[i] += decoded.opcode_counts[i];
            last_payload = p + 1;
            last_end = decoded.end_byte;
            if (decoded.error) {
                if (decoded.error->code ==
                    g::ReferenceMoveErrorCode::unsupported_boundary)
                    ++unsupported;
                else
                    ++failures;
                if (decoded.error->code == g::ReferenceMoveErrorCode::checksum_mismatch)
                    ++checksum_failed;
                if (unsupported + failures <= 16)
                    std::cout << "client_move_boundary payload=" << p + 1
                              << " delivery=" << source.delivery_ordinal
                              << " sequence=" << source.sequence
                              << " bit=" << decoded.error->bit_offset << " opcode="
                              << static_cast<unsigned>(
                                     decoded.error->opcode.value_or(2))
                              << " error=" << g::to_string(decoded.error->code) << '\n';
                continue;
            }
            for (const auto &move : decoded.moves) {
                bool has_redundancy = false;
                ++moves;
                backups += move.backup_count;
                new_commands += move.new_count;
                mix(p + 1);
                mix(source.delivery_ordinal);
                mix(source.sequence);
                mix(move.start_byte);
                mix(move.end_byte);
                mix(move.checksum);
                std::vector<g::GoldSrcWireUserCmd> commands;
                for (const auto &record : move.commands) {
                    redundant_fields += record.redundant_fields;
                    const auto minimal = record.field_mask > 255 ? 2
                                         : record.field_mask     ? 1
                                                                 : 0;
                    if (record.mask_bytes != minimal)
                        ++nonminimal_masks;
                    has_redundancy = has_redundancy || record.redundant_fields != 0 ||
                                     record.mask_bytes != minimal;
                    const auto &c = record.value;
                    commands.push_back(c);
                    const std::array<std::int32_t, 15> fields{c.lerp_msec,
                                                              c.msec,
                                                              c.angle_turns[1],
                                                              c.angle_turns[0],
                                                              c.buttons,
                                                              c.forward,
                                                              c.light_level,
                                                              c.side,
                                                              c.up,
                                                              c.impulse,
                                                              c.angle_turns[2],
                                                              c.impact_index,
                                                              c.impact_eighths[0],
                                                              c.impact_eighths[1],
                                                              c.impact_eighths[2]};
                    for (std::size_t i = 0; i < fields.size(); ++i) {
                        if (fields[i] != 0)
                            ++nonzero[i];
                        mix(static_cast<std::uint32_t>(fields[i]));
                    }
                    if (c.msec == 0)
                        ++zero_msec;
                    if (c.forward < 0 || c.side < 0 || c.up < 0)
                        ++negative_move;
                    if ((c.buttons & 1U) != 0)
                        ++attack;
                    if ((c.buttons & 2U) != 0)
                        ++jump;
                }
                const auto encoded = codec.encode(commands, move.backup_count,
                                                  move.loss_metadata, source);
                if (!encoded) {
                    ++failures;
                    continue;
                }
                if (encoded.message->bytes == move.bytes)
                    ++exact;
                else if (has_redundancy)
                    ++nonexact_with_redundancy;
                const auto again = codec.decode(encoded.message->bytes, 0, source);
                if (again && again.message->commands.size() == commands.size() &&
                    std::equal(
                        commands.begin(), commands.end(),
                        again.message->commands.begin(),
                        [](const auto &c, const auto &r) { return c == r.value; }))
                    ++semantic;
                else
                    ++failures;
            }
        }
        const auto result = failures || unsupported
                                ? "client_move_codec_implemented_capture_partial"
                            : moves ? "client_move_codec_capture_verified"
                                    : "client_move_codec_implemented_no_move_observed";
        std::cout << "client_move_check result=" << result
                  << " profile=public_goldsrc48_client_move_v1 generation=1 run_id="
                  << corpus.state->run_id()
                  << " corpus_hash=" << corpus.state->structural_sha256()
                  << " delivered=" << corpus.state->delivered_datagrams().size()
                  << " transport_payloads=" << payloads.size()
                  << " client_payloads=" << clients << " moves=" << moves
                  << " checksum_matched=" << moves
                  << " checksum_failed=" << checksum_failed << " backups=" << backups
                  << " new_commands=" << new_commands
                  << " semantic_roundtrips=" << semantic << " byte_exact=" << exact
                  << " unsupported=" << unsupported << " failures=" << failures
                  << " last_payload=" << last_payload << " end_cursor=" << last_end
                  << ":0 hash=" << hash << " redundant_fields=" << redundant_fields
                  << " nonminimal_masks=" << nonminimal_masks
                  << " nonexact_with_redundancy=" << nonexact_with_redundancy
                  << " zero_msec=" << zero_msec
                  << " negative_movement=" << negative_move << " attack_bit=" << attack
                  << " jump_bit=" << jump;
        for (const auto &field : g::goldsrc_usercmd_schema_binding_entries())
            std::cout << " nonzero_" << field.exact_name << '='
                      << nonzero[field.wire_index];
        for (std::size_t i = 0; i < opcodes.size(); ++i)
            if (opcodes[i])
                std::cout << " opcode_" << i << '=' << opcodes[i];
        std::cout << " network_submissions=0 weaponselect=not_in_schema\n";
        return failures || unsupported ? 1 : 0;
    } catch (const std::exception &) {
        std::cerr << "client_move_check bounded_operation_failed\n";
        return 1;
    }
}
