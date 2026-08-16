#pragma once

#include <hlclient/goldsrc/service_message_stream.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::goldsrc {

inline constexpr std::array<std::byte, 4U> kBzip2ServicePayloadEnvelopeMagic{
    std::byte{0x42U},
    std::byte{0x5aU},
    std::byte{0x32U},
    std::byte{0x00U},
};
inline constexpr std::size_t kServicePayloadEnvelopeHeaderSize =
    kBzip2ServicePayloadEnvelopeMagic.size();

// Project safety limits, not claims about stock engine maxima.
inline constexpr std::size_t kDefaultMaximumDecompressedServicePayloadSize =
    65'536U;
inline constexpr std::size_t kMaximumDecompressedServicePayloadSize = 1'048'576U;
inline constexpr std::size_t kMaximumCompressedServiceEnvelopeSize = 1'048'576U;
inline constexpr std::size_t kServicePayloadEnvelopeDiagnosticTextLimit = 256U;

struct ServicePayloadEnvelopeLimits {
    std::size_t maximum_decompressed_payload_size{
        kDefaultMaximumDecompressedServicePayloadSize};
};

[[nodiscard]] bool valid_service_payload_envelope_limits(
    const ServicePayloadEnvelopeLimits& limits) noexcept;

enum class ServicePayloadEnvelopeErrorCode {
    invalid_configuration,
    payload_too_short,
    compressed_payload_too_large,
    invalid_envelope_magic,
    missing_compressed_stream,
    invalid_compressed_header,
    decompressor_initialization_failed,
    truncated_compressed_stream,
    corrupt_compressed_stream,
    decompressed_payload_too_large,
    unexpected_trailing_data,
    decompressor_failed,
    size_overflow,
};

struct ServicePayloadEnvelopeError {
    ServicePayloadEnvelopeErrorCode code{
        ServicePayloadEnvelopeErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::string context;
};

struct DecodedServicePayloadEnvelope {
    OwnedServicePayload payload;
    std::size_t compressed_byte_count{0U};
    std::size_t decompressed_byte_count{0U};
};

struct ServicePayloadEnvelopeDecodeResult {
    std::optional<DecodedServicePayloadEnvelope> envelope;
    std::optional<ServicePayloadEnvelopeError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return envelope.has_value();
    }
};

class ServicePayloadEnvelopeDecoder final {
public:
    explicit ServicePayloadEnvelopeDecoder(
        ServicePayloadEnvelopeLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const ServicePayloadEnvelopeLimits& limits() const noexcept;
    [[nodiscard]] ServicePayloadEnvelopeDecodeResult decode(
        OwnedServicePayload payload) const;
    [[nodiscard]] ServicePayloadEnvelopeDecodeResult decode(
        OwnedNetchanPayload payload) const;

private:
    ServicePayloadEnvelopeLimits limits_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const ServicePayloadEnvelopeErrorCode code) noexcept
{
    switch (code) {
    case ServicePayloadEnvelopeErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ServicePayloadEnvelopeErrorCode::payload_too_short:
        return "payload_too_short";
    case ServicePayloadEnvelopeErrorCode::compressed_payload_too_large:
        return "compressed_payload_too_large";
    case ServicePayloadEnvelopeErrorCode::invalid_envelope_magic:
        return "invalid_envelope_magic";
    case ServicePayloadEnvelopeErrorCode::missing_compressed_stream:
        return "missing_compressed_stream";
    case ServicePayloadEnvelopeErrorCode::invalid_compressed_header:
        return "invalid_compressed_header";
    case ServicePayloadEnvelopeErrorCode::decompressor_initialization_failed:
        return "decompressor_initialization_failed";
    case ServicePayloadEnvelopeErrorCode::truncated_compressed_stream:
        return "truncated_compressed_stream";
    case ServicePayloadEnvelopeErrorCode::corrupt_compressed_stream:
        return "corrupt_compressed_stream";
    case ServicePayloadEnvelopeErrorCode::decompressed_payload_too_large:
        return "decompressed_payload_too_large";
    case ServicePayloadEnvelopeErrorCode::unexpected_trailing_data:
        return "unexpected_trailing_data";
    case ServicePayloadEnvelopeErrorCode::decompressor_failed:
        return "decompressor_failed";
    case ServicePayloadEnvelopeErrorCode::size_overflow:
        return "size_overflow";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
