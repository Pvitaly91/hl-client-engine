#include <hlclient/goldsrc/service_payload_envelope.hpp>

#include <bzlib.h>

#include <algorithm>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] ServicePayloadEnvelopeDecodeResult failure(
    const ServicePayloadEnvelopeErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return ServicePayloadEnvelopeDecodeResult{
        std::nullopt,
        ServicePayloadEnvelopeError{code, byte_offset, std::move(context)},
    };
}

class ScopedBzip2Decompressor final {
public:
    ScopedBzip2Decompressor() = default;
    ~ScopedBzip2Decompressor()
    {
        if (initialized_) {
            static_cast<void>(BZ2_bzDecompressEnd(&stream_));
        }
    }

    ScopedBzip2Decompressor(const ScopedBzip2Decompressor&) = delete;
    ScopedBzip2Decompressor& operator=(const ScopedBzip2Decompressor&) = delete;
    ScopedBzip2Decompressor(ScopedBzip2Decompressor&&) = delete;
    ScopedBzip2Decompressor& operator=(ScopedBzip2Decompressor&&) = delete;

    [[nodiscard]] int initialize() noexcept
    {
        const auto result = BZ2_bzDecompressInit(&stream_, 0, 0);
        initialized_ = result == BZ_OK;
        return result;
    }

    [[nodiscard]] bz_stream& stream() noexcept
    {
        return stream_;
    }

private:
    bz_stream stream_{};
    bool initialized_{false};
};

[[nodiscard]] bool bzip2_header(
    const std::span<const std::byte> compressed) noexcept
{
    return compressed.size() >= 4U && compressed[0] == std::byte{'B'} &&
           compressed[1] == std::byte{'Z'} && compressed[2] == std::byte{'h'} &&
           compressed[3] >= std::byte{'1'} && compressed[3] <= std::byte{'9'};
}

} // namespace

bool valid_service_payload_envelope_limits(
    const ServicePayloadEnvelopeLimits& limits) noexcept
{
    return limits.maximum_decompressed_payload_size > 0U &&
           limits.maximum_decompressed_payload_size <=
               kMaximumDecompressedServicePayloadSize;
}

ServicePayloadEnvelopeDecoder::ServicePayloadEnvelopeDecoder(
    ServicePayloadEnvelopeLimits limits) noexcept
    : limits_{limits}
{
}

bool ServicePayloadEnvelopeDecoder::valid_configuration() const noexcept
{
    return valid_service_payload_envelope_limits(limits_);
}

const ServicePayloadEnvelopeLimits& ServicePayloadEnvelopeDecoder::limits() const noexcept
{
    return limits_;
}

ServicePayloadEnvelopeDecodeResult ServicePayloadEnvelopeDecoder::decode(
    OwnedNetchanPayload payload) const
{
    return decode(make_owned_service_payload(std::move(payload)));
}

ServicePayloadEnvelopeDecodeResult ServicePayloadEnvelopeDecoder::decode(
    OwnedServicePayload payload) const
{
    if (!valid_configuration()) {
        return failure(
            ServicePayloadEnvelopeErrorCode::invalid_configuration,
            0U,
            "Service payload envelope limits are outside project hard caps");
    }
    if (payload.bytes.size() < kServicePayloadEnvelopeHeaderSize) {
        return failure(
            ServicePayloadEnvelopeErrorCode::payload_too_short,
            payload.bytes.size(),
            "Service payload is shorter than the captured BZ2 envelope magic");
    }
    if (payload.bytes.size() > kMaximumCompressedServiceEnvelopeSize) {
        return failure(
            ServicePayloadEnvelopeErrorCode::compressed_payload_too_large,
            kMaximumCompressedServiceEnvelopeSize,
            "Compressed service payload exceeds the project hard cap");
    }
    if (!std::ranges::equal(
            std::span<const std::byte>{payload.bytes}.first(
                kServicePayloadEnvelopeHeaderSize),
            kBzip2ServicePayloadEnvelopeMagic)) {
        return failure(
            ServicePayloadEnvelopeErrorCode::invalid_envelope_magic,
            0U,
            "Service payload does not begin with the captured BZ2-NUL envelope");
    }

    const auto compressed = std::span<std::byte>{payload.bytes}.subspan(
        kServicePayloadEnvelopeHeaderSize);
    if (compressed.empty()) {
        return failure(
            ServicePayloadEnvelopeErrorCode::missing_compressed_stream,
            kServicePayloadEnvelopeHeaderSize,
            "BZ2 service envelope has no compressed stream");
    }
    if (compressed.size() < 4U) {
        return failure(
            ServicePayloadEnvelopeErrorCode::truncated_compressed_stream,
            payload.bytes.size(),
            "BZ2 service envelope contains a truncated bzip2 header");
    }
    if (!bzip2_header(compressed)) {
        return failure(
            ServicePayloadEnvelopeErrorCode::invalid_compressed_header,
            kServicePayloadEnvelopeHeaderSize,
            "BZ2 service envelope is not followed by a standard BZh1-BZh9 stream");
    }
    if (compressed.size() > (std::numeric_limits<unsigned int>::max)()) {
        return failure(
            ServicePayloadEnvelopeErrorCode::size_overflow,
            kServicePayloadEnvelopeHeaderSize,
            "Compressed service stream cannot be represented by the bzip2 API");
    }
    if (limits_.maximum_decompressed_payload_size ==
        (std::numeric_limits<std::size_t>::max)()) {
        return failure(
            ServicePayloadEnvelopeErrorCode::size_overflow,
            0U,
            "Decompressed service payload allocation size overflowed");
    }

    const auto candidate_size = limits_.maximum_decompressed_payload_size + 1U;
    if (candidate_size > (std::numeric_limits<unsigned int>::max)()) {
        return failure(
            ServicePayloadEnvelopeErrorCode::size_overflow,
            0U,
            "Decompressed service payload cannot be represented by the bzip2 API");
    }
    std::vector<std::byte> candidate(candidate_size);

    ScopedBzip2Decompressor decompressor;
    const auto initialized = decompressor.initialize();
    if (initialized != BZ_OK) {
        return failure(
            ServicePayloadEnvelopeErrorCode::decompressor_initialization_failed,
            kServicePayloadEnvelopeHeaderSize,
            "In-memory bzip2 decompressor initialization failed");
    }

    auto& stream = decompressor.stream();
    stream.next_in = reinterpret_cast<char*>(compressed.data());
    stream.avail_in = static_cast<unsigned int>(compressed.size());
    stream.next_out = reinterpret_cast<char*>(candidate.data());
    stream.avail_out = static_cast<unsigned int>(candidate.size());

    int result = BZ_OK;
    while (result == BZ_OK) {
        const auto input_before = stream.avail_in;
        const auto output_before = stream.avail_out;
        result = BZ2_bzDecompress(&stream);
        if (result == BZ_STREAM_END) {
            break;
        }
        if (result != BZ_OK) {
            const auto code = result == BZ_DATA_ERROR || result == BZ_DATA_ERROR_MAGIC
                                  ? ServicePayloadEnvelopeErrorCode::corrupt_compressed_stream
                                  : ServicePayloadEnvelopeErrorCode::decompressor_failed;
            return failure(
                code,
                kServicePayloadEnvelopeHeaderSize +
                    (compressed.size() - stream.avail_in),
                "In-memory bzip2 decompression rejected the service stream");
        }
        if (stream.avail_out == 0U) {
            return failure(
                ServicePayloadEnvelopeErrorCode::decompressed_payload_too_large,
                kServicePayloadEnvelopeHeaderSize +
                    (compressed.size() - stream.avail_in),
                "Decompressed service payload exceeds the configured bound");
        }
        if (stream.avail_in == 0U) {
            return failure(
                ServicePayloadEnvelopeErrorCode::truncated_compressed_stream,
                payload.bytes.size(),
                "Compressed service stream ended before BZ_STREAM_END");
        }
        if (stream.avail_in == input_before && stream.avail_out == output_before) {
            return failure(
                ServicePayloadEnvelopeErrorCode::decompressor_failed,
                kServicePayloadEnvelopeHeaderSize +
                    (compressed.size() - stream.avail_in),
                "In-memory bzip2 decompressor made no progress");
        }
    }

    const auto decompressed_size = candidate.size() - stream.avail_out;
    if (decompressed_size > limits_.maximum_decompressed_payload_size) {
        return failure(
            ServicePayloadEnvelopeErrorCode::decompressed_payload_too_large,
            kServicePayloadEnvelopeHeaderSize +
                (compressed.size() - stream.avail_in),
            "Decompressed service payload exceeds the configured bound");
    }
    if (stream.avail_in != 0U) {
        return failure(
            ServicePayloadEnvelopeErrorCode::unexpected_trailing_data,
            kServicePayloadEnvelopeHeaderSize +
                (compressed.size() - stream.avail_in),
            "Bytes follow the single bzip2 stream in the service envelope");
    }

    candidate.resize(decompressed_size);
    const auto compressed_size = compressed.size();
    payload.bytes = std::move(candidate);
    payload.decompressed = true;
    return ServicePayloadEnvelopeDecodeResult{
        DecodedServicePayloadEnvelope{
            std::move(payload),
            compressed_size,
            decompressed_size,
        },
        std::nullopt,
    };
}

} // namespace hlclient::goldsrc
