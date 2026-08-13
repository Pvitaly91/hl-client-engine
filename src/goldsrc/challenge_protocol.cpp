#include <hlclient/goldsrc/challenge_protocol.hpp>

#include <charconv>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace hlclient::goldsrc {
namespace {

inline constexpr std::string_view kGetChallengePayload = "getchallenge steam\n";
inline constexpr std::string_view kChallengeResponsePrefix = "A00000000 ";
inline constexpr std::uint32_t kObservedProfileParameter1 = 3U;
inline constexpr std::uint64_t kObservedProfileParameter2 = 72'057'594'037'927'936ULL;
inline constexpr std::uint32_t kObservedProfileParameter3 = 0U;

[[nodiscard]] ChallengeResponseParseResult failure(
    const ChallengeProtocolErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return ChallengeResponseParseResult{
        std::nullopt,
        ChallengeProtocolError{code, byte_offset, std::move(context)},
    };
}

[[nodiscard]] std::string_view as_text(const std::span<const std::byte> bytes) noexcept
{
    return std::string_view{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size(),
    };
}

template<class Integer>
struct DecimalFieldResult {
    Integer value{};
    std::size_t next{0};
    bool present{false};
    bool valid{false};
    bool overflow{false};
};

template<class Integer>
[[nodiscard]] DecimalFieldResult<Integer> parse_decimal_field(
    const std::string_view payload,
    const std::size_t begin) noexcept
{
    DecimalFieldResult<Integer> result;
    result.next = begin;
    if (begin >= payload.size() || payload[begin] < '0' || payload[begin] > '9') {
        return result;
    }
    result.present = true;

    auto end = begin;
    while (end < payload.size() && payload[end] >= '0' && payload[end] <= '9') {
        ++end;
    }
    result.next = end;
    if (end - begin > 1U && payload[begin] == '0') {
        return result;
    }

    const auto parsed = std::from_chars(
        payload.data() + begin, payload.data() + end, result.value, 10);
    result.overflow = parsed.ec == std::errc::result_out_of_range;
    result.valid = parsed.ec == std::errc{} && parsed.ptr == payload.data() + end;
    return result;
}

} // namespace

GetChallengeRequestResult build_getchallenge_request()
{
    const auto payload = std::as_bytes(
        std::span{kGetChallengePayload.data(), kGetChallengePayload.size()});
    auto encoded = encode_connectionless_packet(payload);
    if (!encoded) {
        return GetChallengeRequestResult{
            std::nullopt,
            ChallengeProtocolError{
                ChallengeProtocolErrorCode::payload_too_large,
                0U,
                encoded.error ? encoded.error->context
                              : "Unable to encode getchallenge request",
            },
        };
    }
    return GetChallengeRequestResult{std::move(encoded.datagram), std::nullopt};
}

ChallengeResponseParseResult parse_challenge_response(
    const std::span<const std::byte> datagram)
{
    if (datagram.size() > kMaximumConnectionlessChallengeDatagramSize) {
        return failure(
            ChallengeProtocolErrorCode::payload_too_large,
            kMaximumConnectionlessChallengeDatagramSize,
            "Challenge response exceeds the 1024-byte profile bound");
    }
    if (datagram.size() < kConnectionlessPacketHeaderSize) {
        return failure(
            ChallengeProtocolErrorCode::packet_too_short,
            datagram.size(),
            "Challenge response is shorter than the connectionless header");
    }

    const auto envelope = parse_connectionless_packet(datagram);
    if (!envelope) {
        const auto envelope_code = envelope.error->code;
        if (envelope_code == ConnectionlessPacketErrorCode::invalid_header) {
            return failure(
                ChallengeProtocolErrorCode::invalid_header,
                envelope.error->byte_offset,
                envelope.error->context);
        }
        if (envelope_code == ConnectionlessPacketErrorCode::datagram_too_large ||
            envelope_code == ConnectionlessPacketErrorCode::payload_too_large) {
            return failure(
                ChallengeProtocolErrorCode::payload_too_large,
                envelope.error->byte_offset,
                envelope.error->context);
        }
        return failure(
            ChallengeProtocolErrorCode::packet_too_short,
            envelope.error->byte_offset,
            envelope.error->context);
    }

    const auto payload = as_text(envelope.packet->payload);
    if (payload.size() < kChallengeResponsePrefix.size()) {
        return failure(
            ChallengeProtocolErrorCode::packet_too_short,
            kConnectionlessPacketHeaderSize + payload.size(),
            "Challenge response prefix is truncated");
    }
    if (!payload.starts_with(kChallengeResponsePrefix)) {
        return failure(
            ChallengeProtocolErrorCode::unexpected_response_type,
            kConnectionlessPacketHeaderSize,
            "Connectionless response is not the Protocol 48 challenge variant");
    }

    auto offset = kChallengeResponsePrefix.size();
    const auto challenge = parse_decimal_field<ChallengeToken>(payload, offset);
    if (!challenge.present &&
        (offset >= payload.size() || payload[offset] == ' ' || payload[offset] == '\n' ||
         payload[offset] == '\0')) {
        return failure(
            ChallengeProtocolErrorCode::missing_challenge,
            kConnectionlessPacketHeaderSize + offset,
            "Challenge token is missing");
    }
    if (!challenge.present) {
        return failure(
            ChallengeProtocolErrorCode::invalid_challenge,
            kConnectionlessPacketHeaderSize + offset,
            "Challenge token is not a canonical unsigned decimal uint32");
    }
    if (challenge.overflow) {
        return failure(
            ChallengeProtocolErrorCode::challenge_overflow,
            kConnectionlessPacketHeaderSize + offset,
            "Challenge token exceeds the uint32 range");
    }
    if (!challenge.valid) {
        return failure(
            ChallengeProtocolErrorCode::invalid_challenge,
            kConnectionlessPacketHeaderSize + offset,
            "Challenge token is not a canonical unsigned decimal uint32");
    }
    offset = challenge.next;
    if (offset >= payload.size() || payload[offset] != ' ') {
        return failure(
            ChallengeProtocolErrorCode::invalid_challenge,
            kConnectionlessPacketHeaderSize + offset,
            "Challenge token must be followed by one ASCII space");
    }

    ++offset;
    const auto profile_parameter_1 = parse_decimal_field<std::uint32_t>(payload, offset);
    if (!profile_parameter_1.present || !profile_parameter_1.valid ||
        profile_parameter_1.overflow) {
        return failure(
            ChallengeProtocolErrorCode::unsupported_variant,
            kConnectionlessPacketHeaderSize + offset,
            "Profile parameter 1 is invalid or out of range");
    }
    if (profile_parameter_1.value != kObservedProfileParameter1) {
        return failure(
            ChallengeProtocolErrorCode::unsupported_variant,
            kConnectionlessPacketHeaderSize + offset,
            "Only observed Steam Half-Life profile parameter 1 value 3 is supported");
    }
    offset = profile_parameter_1.next;
    if (offset >= payload.size() || payload[offset] != ' ') {
        return failure(
            ChallengeProtocolErrorCode::unsupported_variant,
            kConnectionlessPacketHeaderSize + offset,
            "Profile parameter 1 must be followed by one ASCII space");
    }

    ++offset;
    const auto profile_parameter_2 = parse_decimal_field<std::uint64_t>(payload, offset);
    if (!profile_parameter_2.present || !profile_parameter_2.valid ||
        profile_parameter_2.overflow) {
        return failure(
            ChallengeProtocolErrorCode::unsupported_variant,
            kConnectionlessPacketHeaderSize + offset,
            "Profile parameter 2 is invalid or out of range");
    }
    if (profile_parameter_2.value != kObservedProfileParameter2) {
        return failure(
            ChallengeProtocolErrorCode::unsupported_variant,
            kConnectionlessPacketHeaderSize + offset,
            "Profile parameter 2 does not match the captured Steam Half-Life value");
    }
    offset = profile_parameter_2.next;
    if (offset >= payload.size() || payload[offset] != ' ') {
        return failure(
            ChallengeProtocolErrorCode::unsupported_variant,
            kConnectionlessPacketHeaderSize + offset,
            "Profile parameter 2 must be followed by one ASCII space");
    }

    ++offset;
    const auto profile_parameter_3 = parse_decimal_field<std::uint32_t>(payload, offset);
    if (!profile_parameter_3.present || !profile_parameter_3.valid ||
        profile_parameter_3.overflow) {
        return failure(
            ChallengeProtocolErrorCode::unsupported_variant,
            kConnectionlessPacketHeaderSize + offset,
            "Profile parameter 3 is invalid or out of range");
    }
    if (profile_parameter_3.value != kObservedProfileParameter3) {
        return failure(
            ChallengeProtocolErrorCode::unsupported_variant,
            kConnectionlessPacketHeaderSize + offset,
            "Profile parameter 3 does not match the captured Steam Half-Life value");
    }
    offset = profile_parameter_3.next;

    if (offset + 2U > payload.size() || payload[offset] != '\n' ||
        payload[offset + 1U] != '\0') {
        return failure(
            ChallengeProtocolErrorCode::invalid_terminator,
            kConnectionlessPacketHeaderSize + offset,
            "Challenge response must end with LF followed by NUL");
    }
    offset += 2U;
    if (offset != payload.size()) {
        return failure(
            ChallengeProtocolErrorCode::unexpected_trailing_data,
            kConnectionlessPacketHeaderSize + offset,
            "Challenge response contains bytes after its LF-NUL terminator");
    }

    return ChallengeResponseParseResult{
        ChallengeResponse{
            challenge.value,
            profile_parameter_1.value,
            profile_parameter_2.value,
            profile_parameter_3.value,
        },
        std::nullopt,
    };
}

} // namespace hlclient::goldsrc
