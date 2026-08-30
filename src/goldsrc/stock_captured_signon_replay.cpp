#include <hlclient/goldsrc/stock_captured_signon_replay.hpp>

#include <hlclient/goldsrc/client_message.hpp>
#include <hlclient/goldsrc/delta_description.hpp>
#include <hlclient/goldsrc/move_vars.hpp>
#include <hlclient/goldsrc/resource_list.hpp>
#include <hlclient/goldsrc/resource_transition_control.hpp>
#include <hlclient/goldsrc/resource_transition_request.hpp>
#include <hlclient/goldsrc/service_message_stream.hpp>
#include <hlclient/goldsrc/user_info_update.hpp>

#include <algorithm>
#include <limits>
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
               return value == std::byte{0U};
           });
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
            return value == std::byte{0U};
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
    const bool known_signon_validated) noexcept
    : boundary_{std::move(boundary)}, cursor_{cursor},
      observed_client_request_count_{observed_client_request_count},
      decoded_server_signon_payload_count_{decoded_server_signon_payload_count},
      known_signon_validated_{known_signon_validated}
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
    if (!initial_bytes || !parse_initial_signon_request(*initial_bytes)) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::initial_request_not_observed,
            *initial_request_index,
            "observed initial request does not match the existing exact codec");
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
    if (!sendres || !sendres.request ||
        sendres.bytes_consumed != payloads[*sendres_index].bytes().size()) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::resource_transition_request_not_observed,
            *sendres_index,
            "observed C-to-S payload is not one exact sendres request");
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

    const auto response_index = next_semantic(NetchanDirection::client_to_server);
    if (!response_index) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::resource_response_not_observed,
            cursor, "observed opcode-5 resource response is absent or out of order");
    }
    const auto& response_payload = payloads[*response_index];
    Opcode5ResourceResponseParser response_parser;
    const auto response = response_parser.parse(
        response_payload.bytes(),
        Opcode5ResourceResponseSourceGeometry{
            0U, response_payload.bytes().size(), response_payload.bytes().size()},
        Opcode5ResourceResponseSourceProfile::captured_reliable_semantic_fragment);
    if (!response || !response.response ||
        response.bytes_consumed != response_payload.bytes().size()) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::resource_response_invalid,
            *response_index,
            "observed resource response failed the existing exact 41-byte parser");
    }

    const auto post_response_index = next_semantic(NetchanDirection::server_to_client);
    if (!post_response_index) {
        return signon_failure(
            StockCapturedSignonReplayErrorCode::post_resource_cursor_unavailable,
            cursor,
            "first post-response server payload is absent or out of order");
    }
    return reconstruct_boundary(
        transport, *post_response_index, 3U, 3U, true);
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
    const bool known_signon_validated) const
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
            known_signon_validated},
        std::nullopt,
    };
}

} // namespace hlclient::goldsrc
