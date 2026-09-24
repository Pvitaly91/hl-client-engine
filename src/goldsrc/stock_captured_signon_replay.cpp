#include <hlclient/goldsrc/stock_captured_signon_replay.hpp>

#include <hlclient/goldsrc/client_message.hpp>
#include <hlclient/goldsrc/delta_description.hpp>
#include <hlclient/goldsrc/move_vars.hpp>
#include <hlclient/goldsrc/netchan_session.hpp>
#include <hlclient/goldsrc/resource_list.hpp>
#include <hlclient/goldsrc/resource_transition_control.hpp>
#include <hlclient/goldsrc/resource_transition_request.hpp>
#include <hlclient/goldsrc/service_message_stream.hpp>
#include <hlclient/goldsrc/user_info_update.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <ranges>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] StockCapturedSignonReplayResult signon_failure(
    const StockCapturedSignonReplayErrorCode code,
    const std::size_t payload_ordinal,
    std::string context,
    const std::optional<StockRuntimeTransportReplayErrorCode> transport_code =
        std::nullopt)
{
    return {
        std::nullopt,
        StockCapturedSignonReplayError{
            code, payload_ordinal, std::move(context), transport_code},
    };
}

[[nodiscard]] bool valid_limits(const StockCapturedSignonReplayLimits& limits) noexcept
{
    return limits.maximum_replayed_payloads > 0U &&
           limits.maximum_replayed_payloads <= 131'072U &&
           limits.maximum_payload_bytes > 0U &&
           limits.maximum_payload_bytes <= kMaximumPostResponsePayloadSize &&
           limits.maximum_client_request_candidates > 0U &&
           limits.maximum_client_request_candidates <= 65'536U;
}

[[nodiscard]] bool transport_padding_only(
    const StockRuntimeReplayedPayload& payload) noexcept
{
    return payload.bytes().size() <= 8U &&
           std::ranges::all_of(payload.bytes(), [](const std::byte value) {
               return value == kStockProtocol48NetchanPaddingByte;
           });
}

[[nodiscard]] bool exact_zero_entry_resource_response(
    const std::span<const std::byte> bytes) noexcept
{
    constexpr std::size_t kOpcodeAndEntryCountSize = 3U;
    return bytes.size() == kOpcodeAndEntryCountSize &&
           std::to_integer<std::uint8_t>(bytes[0U]) ==
               kOpcode5ResourceResponseOpcode &&
           bytes[1U] == std::byte{0U} && bytes[2U] == std::byte{0U};
}

[[nodiscard]] std::optional<std::span<const std::byte>>
initial_request_semantic_bytes(const StockRuntimeReplayedPayload& payload) noexcept
{
    if (payload.bytes().size() < kInitialSignonRequestSize ||
        payload.bytes().size() > kInitialSignonRequestSize + 3U) {
        return std::nullopt;
    }
    const auto semantic = payload.bytes().first(kInitialSignonRequestSize);
    const auto padding = payload.bytes().subspan(kInitialSignonRequestSize);
    if (!std::ranges::all_of(padding, [](const std::byte value) {
            return value == kStockProtocol48NetchanPaddingByte;
        })) {
        return std::nullopt;
    }
    return semantic;
}

[[nodiscard]] OwnedServicePayload owning_service_payload(
    const StockRuntimeReplayedPayload& payload)
{
    return OwnedServicePayload{
        std::vector<std::byte>{payload.bytes().begin(), payload.bytes().end()},
        payload.source_sequence(),
        payload.source_acknowledgement(),
        payload.reliable(),
        payload.reassembled(),
        payload.decompressed(),
        false,
        payload.acknowledgement_reliable(),
        payload.direction(),
        NetchanDriverTimePoint{},
    };
}

[[nodiscard]] StockCapturedSignonReplayErrorCode map_transport_error(
    const StockRuntimeTransportReplayErrorCode code) noexcept
{
    switch (code) {
    case StockRuntimeTransportReplayErrorCode::connection_not_established:
    case StockRuntimeTransportReplayErrorCode::challenge_response_invalid:
    case StockRuntimeTransportReplayErrorCode::connect_request_invalid:
    case StockRuntimeTransportReplayErrorCode::challenge_mismatch:
    case StockRuntimeTransportReplayErrorCode::connect_response_invalid:
    case StockRuntimeTransportReplayErrorCode::connection_rejected:
        return StockCapturedSignonReplayErrorCode::connection_not_established;
    case StockRuntimeTransportReplayErrorCode::fragment_reassembly_failed:
        return StockCapturedSignonReplayErrorCode::fragment_reassembly_failed;
    case StockRuntimeTransportReplayErrorCode::decompression_failed:
        return StockCapturedSignonReplayErrorCode::decompression_failed;
    case StockRuntimeTransportReplayErrorCode::unsupported_secondary_stream:
        return StockCapturedSignonReplayErrorCode::unsupported_secondary_stream;
    case StockRuntimeTransportReplayErrorCode::capture_incomplete:
        return StockCapturedSignonReplayErrorCode::capture_incomplete;
    default:
        return StockCapturedSignonReplayErrorCode::netchan_replay_failed;
    }
}

} // namespace

StockCapturedSignonReplayState::StockCapturedSignonReplayState(
    PostResourceResponseBoundary boundary,
    StockPostResourceResponseCursor cursor,
    const std::size_t observed_client_request_count,
    const std::size_t decoded_server_signon_payload_count,
    const bool known_signon_validated,
    std::optional<ServerInfoState> server_info,
    std::shared_ptr<const DeltaSchemaRegistryState> delta_registry,
    std::vector<PostMoveVarsUserMessageDefinition>
        user_message_definitions,
    std::shared_ptr<const ResourceListState> resources) noexcept
    : boundary_{std::move(boundary)}, cursor_{cursor},
      observed_client_request_count_{observed_client_request_count},
      decoded_server_signon_payload_count_{decoded_server_signon_payload_count},
      known_signon_validated_{known_signon_validated},
      server_info_{std::move(server_info)},
      delta_registry_{std::move(delta_registry)},
      user_message_definitions_{std::move(user_message_definitions)},
      resources_{std::move(resources)}
{
}

const PostResourceResponseBoundary&
StockCapturedSignonReplayState::boundary() const noexcept { return boundary_; }
const StockPostResourceResponseCursor&
StockCapturedSignonReplayState::cursor() const noexcept { return cursor_; }
std::size_t StockCapturedSignonReplayState::observed_client_request_count() const noexcept
{
    return observed_client_request_count_;
}
std::size_t StockCapturedSignonReplayState::decoded_server_signon_payload_count() const noexcept
{
    return decoded_server_signon_payload_count_;
}
bool StockCapturedSignonReplayState::known_signon_validated() const noexcept
{
    return known_signon_validated_;
}
bool StockCapturedSignonReplayState::observed_initial_new() const noexcept
{
    return known_signon_validated_ && observed_client_request_count_ >= 1U;
}
bool StockCapturedSignonReplayState::observed_sendres() const noexcept
{
    return known_signon_validated_ && observed_client_request_count_ >= 2U;
}
bool StockCapturedSignonReplayState::observed_opcode5_resource_response() const noexcept
{
    return known_signon_validated_ && observed_client_request_count_ >= 3U;
}
const std::optional<ServerInfoState>&
StockCapturedSignonReplayState::server_info() const noexcept
{
    return server_info_;
}
const std::shared_ptr<const DeltaSchemaRegistryState>&
StockCapturedSignonReplayState::delta_registry() const noexcept
{
    return delta_registry_;
}

std::span<const PostMoveVarsUserMessageDefinition>
StockCapturedSignonReplayState::user_message_definitions() const noexcept
{
    return user_message_definitions_;
}

StockCapturedSignonReplay::StockCapturedSignonReplay(
    StockCapturedSignonReplayLimits limits) noexcept
    : limits_{std::move(limits)}
{
}

bool StockCapturedSignonReplay::valid_configuration() const noexcept
{
    return valid_limits(limits_);
}
const StockCapturedSignonReplayLimits&
StockCapturedSignonReplay::limits() const noexcept { return limits_; }

StockCapturedSignonReplayResult StockCapturedSignonReplay::replay(
    const StockRuntimeTransportReplayResult& transport) const
{
    if (!transport || !transport.state) {
        const auto transport_code = transport.error
                                        ? std::optional{transport.error->code}
                                        : std::nullopt;
        return signon_failure(
            transport_code ? map_transport_error(*transport_code)
                           : StockCapturedSignonReplayErrorCode::netchan_replay_failed,
            transport.error ? transport.error->delivery_ordinal : 0U,
            "offline transport replay did not publish a complete state",
            transport_code);
    }
    return replay(*transport.state);
}

StockCapturedSignonReplayResult StockCapturedSignonReplay::replay(
    const StockRuntimeTransportReplayState& transport) const
{
    if (!valid_configuration()) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::invalid_configuration, 0U,
            "captured sign-on replay limits are invalid");
    }
    const auto& payloads = transport.payloads();
    if (payloads.empty() || payloads.size() > limits_.maximum_replayed_payloads) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::signon_sequence_incomplete, 0U,
            "transport replay has no bounded semantic payload sequence");
    }
    if (std::ranges::any_of(payloads, [this](const auto& payload) {
            return payload.bytes().size() > limits_.maximum_payload_bytes;
        })) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::payload_limit_exceeded, 0U,
            "transport replay payload exceeds the sign-on bound");
    }

    std::size_t cursor = 0U;
    auto next_semantic = [&payloads, &cursor](
                             const NetchanDirection required_direction)
        -> std::optional<std::size_t> {
        while (cursor < payloads.size()) {
            const auto index = cursor++;
            if (transport_padding_only(payloads[index])) continue;
            if (payloads[index].direction() != required_direction) return std::nullopt;
            return index;
        }
        return std::nullopt;
    };

    const auto initial_request_index = next_semantic(NetchanDirection::client_to_server);
    if (!initial_request_index) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::initial_request_not_observed, cursor,
            "first semantic payload is not an observed C-to-S initial request");
    }
    const auto initial_bytes =
        initial_request_semantic_bytes(payloads[*initial_request_index]);
    if (!initial_bytes) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::initial_request_not_observed,
            *initial_request_index,
            payloads[*initial_request_index].bytes().size() <
                    kInitialSignonRequestSize ||
                payloads[*initial_request_index].bytes().size() >
                    kInitialSignonRequestSize + 3U
                ? "observed initial request is outside the bounded netchan "
                  "message-size envelope"
                : "observed initial request has noncanonical Protocol 48 "
                  "transport padding");
    }
    const auto parsed_initial_request =
        parse_initial_signon_request(*initial_bytes);
    if (!parsed_initial_request) {
        const auto code = parsed_initial_request.error
            ? to_string(parsed_initial_request.error->code)
            : std::string_view{"unavailable"};
        const auto byte_offset = parsed_initial_request.error
            ? parsed_initial_request.error->byte_offset
            : 0U;
        return signon_failure(
            StockCapturedSignonReplayErrorCode::initial_request_not_observed,
            *initial_request_index,
            "observed initial request codec failure=" + std::string{code} +
                ";byte-offset=" + std::to_string(byte_offset));
    }

    const auto first_server_index = next_semantic(NetchanDirection::server_to_client);
    if (!first_server_index) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::signon_sequence_incomplete, cursor,
            "initial server sign-on payload is absent or out of order");
    }
    const auto& first_server = payloads[*first_server_index];
    if (!first_server.decompressed()) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::decompression_failed,
            *first_server_index,
            "initial server sign-on payload was not BZip2-decoded offline");
    }

    ServiceMessageStreamDecoder service_decoder;
    auto initial_stream = service_decoder.decode(owning_service_payload(first_server));
    if (!initial_stream || !initial_stream.stream || !initial_stream.stream->boundary) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::initial_service_decode_failed,
            *first_server_index,
            "initial service batch failed the existing pure decoder");
    }
    auto pre_resource = service_decoder.continue_to_pre_resource(
        initial_stream.stream->payload, *initial_stream.stream->boundary);
    if (!pre_resource || !pre_resource.state) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::pre_resource_decode_failed,
            *first_server_index,
            "serverinfo/pre-resource continuation failed the existing decoder");
    }
    DeltaDescriptionStreamDecoder delta_decoder;
    auto delta = delta_decoder.decode(
        initial_stream.stream->payload.bytes, pre_resource.state->boundary());
    if (!delta || !delta.state) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::delta_description_decode_failed,
            *first_server_index,
            "delta-description continuation failed at its exact cursor");
    }
    MoveVarsStreamDecoder movevars_decoder;
    auto movevars = movevars_decoder.decode(
        initial_stream.stream->payload.bytes, delta.state->boundary);
    if (!movevars || !movevars.state) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::movevars_decode_failed,
            *first_server_index,
            "movevars continuation failed at its exact cursor");
    }
    UserInfoUpdateStreamDecoder user_info_decoder;
    auto user_info = user_info_decoder.decode(
        initial_stream.stream->payload.bytes, movevars.state->boundary());
    if (!user_info || !user_info.state ||
        user_info.state->completion().terminal_condition() !=
            UserInfoBatchTerminalCondition::exact_end_of_payload ||
        user_info.state->completion().final_byte_offset() !=
            initial_stream.stream->payload.bytes.size()) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::user_info_decode_failed,
            *first_server_index,
            "user-info first batch did not end at the owning payload boundary");
    }

    const auto sendres_index = next_semantic(NetchanDirection::client_to_server);
    if (!sendres_index) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::resource_transition_request_not_observed,
            cursor, "observed sendres request is absent or out of order");
    }
    ResourceTransitionRequestParser sendres_parser;
    const auto sendres = sendres_parser.parse(payloads[*sendres_index].bytes(), 0U);
    if (!sendres || !sendres.request) {
        const auto repeated_initial =
            initial_request_semantic_bytes(payloads[*sendres_index]);
        const bool exact_initial_retransmission = repeated_initial &&
            parse_initial_signon_request(*repeated_initial);
        const auto code = sendres.error
            ? to_string(sendres.error->code)
            : std::string_view{"unavailable"};
        const auto byte_offset = sendres.error
            ? sendres.error->byte_offset
            : 0U;
        return signon_failure(
            StockCapturedSignonReplayErrorCode::resource_transition_request_not_observed,
            *sendres_index,
            exact_initial_retransmission
                ? "exact initial request retransmission precedes sendres"
                : "sendres codec failure=" + std::string{code} +
                      ";byte-offset=" + std::to_string(byte_offset));
    }

    const auto second_server_index = next_semantic(NetchanDirection::server_to_client);
    if (!second_server_index) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::signon_sequence_incomplete, cursor,
            "resource-list server payload is absent or out of order");
    }
    const auto& second_server = payloads[*second_server_index];
    if (!second_server.decompressed()) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::decompression_failed,
            *second_server_index,
            "resource-list payload was not BZip2-decoded offline");
    }
    ResourceTransitionControlParser transition_parser;
    const auto transition = transition_parser.parse(second_server.bytes(), 0U);
    if (!transition || !transition.boundary) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::resource_transition_decode_failed,
            *second_server_index,
            "resource transition control failed the existing exact parser");
    }
    if (second_server.bytes().size() >
        (std::numeric_limits<std::size_t>::max)() / 8U) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::payload_limit_exceeded,
            *second_server_index, "resource-list bit geometry overflows");
    }
    const auto second_bits = second_server.bytes().size() * 8U;
    ResourceListParser resource_list_parser;
    const auto resource_list = resource_list_parser.parse(
        second_server.bytes(), transition.boundary->byte_offset(), second_bits);
    if (!resource_list || !resource_list.state) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::resource_list_decode_failed,
            *second_server_index,
            "resource list failed the existing exact bit parser");
    }
    PostResourceListStreamDecoder post_list_decoder;
    const auto post_list = post_list_decoder.decode(
        second_server.bytes(), *resource_list.state, second_bits);
    if (!post_list || !post_list.state) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::resource_list_decode_failed,
            *second_server_index,
            "resource list did not end at the exact owning payload boundary");
    }

    Opcode5ResourceResponseParser response_parser;
    std::optional<std::size_t> response_index;
    std::size_t client_candidates = 0U;
    while (cursor < payloads.size() &&
           client_candidates < limits_.maximum_client_request_candidates) {
        const auto candidate_index = cursor++;
        const auto& candidate = payloads[candidate_index];
        if (transport_padding_only(candidate) ||
            candidate.direction() != NetchanDirection::client_to_server) {
            continue;
        }
        ++client_candidates;
        if (candidate.kind() ==
                StockRuntimeReplayedPayloadKind::
                    contemporaneous_fragment_suffix ||
            candidate.bytes().empty() ||
            std::to_integer<std::uint8_t>(candidate.bytes().front()) !=
                kOpcode5ResourceResponseOpcode) {
            continue;
        }
        if (exact_zero_entry_resource_response(candidate.bytes())) {
            response_index = candidate_index;
            break;
        }
        const auto response = response_parser.parse(
            candidate.bytes(),
            Opcode5ResourceResponseSourceGeometry{
                0U, candidate.bytes().size(), candidate.bytes().size()},
            Opcode5ResourceResponseSourceProfile::
                captured_reliable_semantic_fragment);
        if (!response || !response.response ||
            response.bytes_consumed != candidate.bytes().size()) {
            const auto code = response.error
                ? to_string(response.error->code)
                : std::string_view{"unavailable"};
            const auto byte_offset = response.error
                ? response.error->byte_offset
                : response.bytes_consumed;
            return signon_failure(
                StockCapturedSignonReplayErrorCode::resource_response_invalid,
                candidate_index,
                "observed resource response codec failure=" + std::string{code} +
                    ";byte-offset=" + std::to_string(byte_offset) +
                    ";payload-bytes=" +
                    std::to_string(candidate.bytes().size()));
        }
        response_index = candidate_index;
        break;
    }
    if (!response_index) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::resource_response_not_observed,
            cursor,
            client_candidates >= limits_.maximum_client_request_candidates
                ? "bounded client request candidate limit reached before opcode 5"
                : "observed opcode-5 resource response is absent");
    }

    std::optional<std::size_t> post_response_index;
    while (cursor < payloads.size()) {
        const auto candidate_index = cursor++;
        if (!transport_padding_only(payloads[candidate_index]) &&
            payloads[candidate_index].direction() ==
                NetchanDirection::server_to_client) {
            post_response_index = candidate_index;
            break;
        }
    }
    if (!post_response_index) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::post_resource_cursor_unavailable,
            cursor,
            "first post-response server payload is absent or out of order");
    }
    std::shared_ptr<const DeltaSchemaRegistryState> retained_registry;
    std::optional<ServerInfoState> retained_server_info;
    std::vector<PostMoveVarsUserMessageDefinition> retained_user_messages;
    std::shared_ptr<const ResourceListState> retained_resources;
    try {
        retained_registry = std::make_shared<const DeltaSchemaRegistryState>(
            delta.state->registry);
        retained_server_info.emplace(pre_resource.state->server_info());
        retained_resources = std::make_shared<const ResourceListState>(*resource_list.state);
        for (const auto& control : movevars.state->controls()) {
            if (const auto* definition =
                    std::get_if<PostMoveVarsUserMessageDefinition>(
                        &control.body())) {
                retained_user_messages.push_back(*definition);
            }
        }
    } catch (const std::bad_alloc&) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::allocation_failed,
            *post_response_index,
            "unable to retain decoded sign-on initialization context");
    }
    return reconstruct_boundary(
        transport, *post_response_index, 3U, 3U, true,
        std::move(retained_server_info), std::move(retained_registry),
        std::move(retained_user_messages),
        std::move(retained_resources));
}

StockCapturedSignonReplayResult
StockCapturedSignonReplay::reconstruct_post_resource_boundary(
    const StockRuntimeTransportReplayState& transport,
    const std::size_t first_post_response_server_payload_ordinal,
    const std::size_t observed_client_request_count,
    const std::size_t decoded_server_signon_payload_count) const
{
    return reconstruct_boundary(
        transport, first_post_response_server_payload_ordinal,
        observed_client_request_count, decoded_server_signon_payload_count,
        false);
}

StockCapturedSignonReplayResult StockCapturedSignonReplay::reconstruct_boundary(
    const StockRuntimeTransportReplayState& transport,
    const std::size_t first_post_response_server_payload_ordinal,
    const std::size_t observed_client_request_count,
    const std::size_t decoded_server_signon_payload_count,
    const bool known_signon_validated,
    std::optional<ServerInfoState> server_info,
    std::shared_ptr<const DeltaSchemaRegistryState> delta_registry,
    std::vector<PostMoveVarsUserMessageDefinition>
        user_message_definitions,
    std::shared_ptr<const ResourceListState> resources) const
{
    if (!valid_configuration()) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::invalid_configuration, 0U,
            "captured sign-on replay limits are invalid");
    }
    if (first_post_response_server_payload_ordinal >= transport.payloads().size()) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::post_resource_cursor_unavailable,
            first_post_response_server_payload_ordinal,
            "post-resource payload ordinal is outside replay state");
    }
    const auto& payload =
        transport.payloads()[first_post_response_server_payload_ordinal];
    if (payload.direction() != NetchanDirection::server_to_client ||
        payload.bytes().size() > limits_.maximum_payload_bytes ||
        payload.bytes().size() > (std::numeric_limits<std::size_t>::max)() / 8U) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::post_resource_cursor_unavailable,
            first_post_response_server_payload_ordinal,
            "post-resource payload direction or geometry is invalid");
    }
    PostResourceResponseBoundaryParser parser;
    auto parsed = parser.parse(
        payload.bytes(),
        PostResourceResponseSourcePayloadMetadata{
            payload.direction(), payload.source_sequence(), payload.reliable(),
            payload.reassembled(), payload.decompressed(), payload.bytes().size()});
    if (!parsed || !parsed.boundary) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::post_resource_cursor_unavailable,
            first_post_response_server_payload_ordinal,
            "neutral post-response boundary parser rejected the exact payload");
    }
    const auto payload_bits = payload.bytes().size() * 8U;
    const auto boundary_byte_offset = parsed.boundary->byte_offset();
    const auto boundary_bit_offset = parsed.boundary->bit_offset();
    const auto cursor_bits = boundary_byte_offset * 8U + boundary_bit_offset;
    if (cursor_bits > payload_bits) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::post_resource_cursor_unavailable,
            first_post_response_server_payload_ordinal,
            "post-resource cursor exceeds its owning payload");
    }
    return StockCapturedSignonReplayResult{
        StockCapturedSignonReplayState{
            std::move(*parsed.boundary),
            StockPostResourceResponseCursor{
                first_post_response_server_payload_ordinal,
                payload.corpus_observed_ordinal(), payload.delivery_ordinal(),
                boundary_byte_offset, boundary_bit_offset,
                payload.source_sequence(), payload.bytes().size(), payload_bits,
                payload_bits - cursor_bits, payload.reassembled(),
                payload.decompressed()},
            observed_client_request_count, decoded_server_signon_payload_count,
            known_signon_validated, std::move(server_info),
            std::move(delta_registry), std::move(user_message_definitions),
            std::move(resources)},
        std::nullopt,
    };
}

} // namespace hlclient::goldsrc
