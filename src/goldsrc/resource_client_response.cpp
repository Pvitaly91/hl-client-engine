#include <hlclient/goldsrc/resource_client_response.hpp>

#include <hlclient/goldsrc/byte_reader.hpp>
#include <hlclient/goldsrc/byte_writer.hpp>

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

inline constexpr std::size_t kMaximumDecodedDescriptorAreaSize =
    kNetchanFragmentSlotCount * kStockProtocol48PresentFragmentDescriptorSize;

[[nodiscard]] Opcode5ResourceResponseError response_error(
    const Opcode5ResourceResponseErrorCode code,
    const std::size_t byte_offset,
    const std::string_view context)
{
    const auto bounded = context.substr(
        0U,
        (std::min)(
            context.size(), kResourceClientResponseDiagnosticTextLimit));
    return Opcode5ResourceResponseError{
        code,
        byte_offset,
        std::string{bounded.data(), bounded.size()},
    };
}

[[nodiscard]] Opcode5ResourceResponseParseResult response_parse_failure(
    const Opcode5ResourceResponseErrorCode code,
    const std::size_t byte_offset,
    const std::string_view context)
{
    return Opcode5ResourceResponseParseResult{
        std::nullopt,
        response_error(code, byte_offset, context),
        0U,
    };
}

[[nodiscard]] Opcode5ResourceResponseBuildResult response_build_failure(
    const Opcode5ResourceResponseErrorCode code,
    const std::size_t byte_offset,
    const std::string_view context)
{
    return Opcode5ResourceResponseBuildResult{
        std::nullopt,
        response_error(code, byte_offset, context),
    };
}

[[nodiscard]] ResourceResponseCarrierError carrier_error(
    const ResourceResponseCarrierErrorCode code,
    const std::size_t byte_offset,
    const std::string_view context,
    const std::optional<NetchanPacketErrorCode> transport_error = std::nullopt,
    const std::optional<Opcode5ResourceResponseErrorCode> response_error_code =
        std::nullopt)
{
    const auto bounded = context.substr(
        0U,
        (std::min)(
            context.size(), kResourceClientResponseDiagnosticTextLimit));
    return ResourceResponseCarrierError{
        code,
        byte_offset,
        std::string{bounded.data(), bounded.size()},
        transport_error,
        response_error_code,
    };
}

[[nodiscard]] Opcode5ResourceResponseCarrierParseResult carrier_failure(
    const ResourceResponseCarrierErrorCode code,
    const std::size_t byte_offset,
    const std::string_view context,
    const std::optional<NetchanPacketErrorCode> transport_error = std::nullopt,
    const std::optional<Opcode5ResourceResponseErrorCode> response_error_code =
        std::nullopt)
{
    return Opcode5ResourceResponseCarrierParseResult{
        std::nullopt,
        carrier_error(
            code,
            byte_offset,
            context,
            transport_error,
            response_error_code),
    };
}

[[nodiscard]] PostResourceResponseBoundaryError boundary_error(
    const PostResourceResponseBoundaryErrorCode code,
    const std::size_t byte_offset,
    const std::string_view context)
{
    const auto bounded = context.substr(
        0U,
        (std::min)(
            context.size(), kResourceClientResponseDiagnosticTextLimit));
    return PostResourceResponseBoundaryError{
        code,
        byte_offset,
        std::string{bounded.data(), bounded.size()},
    };
}

[[nodiscard]] PostResourceResponseBoundaryParseResult boundary_failure(
    const PostResourceResponseBoundaryErrorCode code,
    const std::size_t byte_offset,
    const std::string_view context)
{
    return PostResourceResponseBoundaryParseResult{
        std::nullopt,
        boundary_error(code, byte_offset, context),
    };
}

[[nodiscard]] bool valid_source_profile(
    const Opcode5ResourceResponseSourceProfile profile) noexcept
{
    switch (profile) {
    case Opcode5ResourceResponseSourceProfile::
        captured_reliable_semantic_fragment:
    case Opcode5ResourceResponseSourceProfile::
        independently_authored_synthetic_fixture:
    case Opcode5ResourceResponseSourceProfile::canonical_builder_output:
        return true;
    }
    return false;
}

[[nodiscard]] std::optional<ResourceResponseConcurrentTailProfile>
tail_profile_for_size(const std::size_t byte_count) noexcept
{
    switch (byte_count) {
    case 11U:
        return ResourceResponseConcurrentTailProfile::stock_opaque_length_11;
    case 13U:
        return ResourceResponseConcurrentTailProfile::
            stock_coalesced_opaque_length_13;
    case 15U:
        return ResourceResponseConcurrentTailProfile::
            stock_coalesced_opaque_length_15;
    case 17U:
        return ResourceResponseConcurrentTailProfile::
            stock_coalesced_opaque_length_17;
    default: return std::nullopt;
    }
}

[[nodiscard]] constexpr std::uint32_t choose(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z) noexcept
{
    return (x & y) ^ (~x & z);
}

[[nodiscard]] constexpr std::uint32_t majority(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z) noexcept
{
    return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] constexpr std::uint32_t big_sigma_zero(
    const std::uint32_t value) noexcept
{
    return std::rotr(value, 2) ^ std::rotr(value, 13) ^ std::rotr(value, 22);
}

[[nodiscard]] constexpr std::uint32_t big_sigma_one(
    const std::uint32_t value) noexcept
{
    return std::rotr(value, 6) ^ std::rotr(value, 11) ^ std::rotr(value, 25);
}

[[nodiscard]] constexpr std::uint32_t small_sigma_zero(
    const std::uint32_t value) noexcept
{
    return std::rotr(value, 7) ^ std::rotr(value, 18) ^ (value >> 3U);
}

[[nodiscard]] constexpr std::uint32_t small_sigma_one(
    const std::uint32_t value) noexcept
{
    return std::rotr(value, 17) ^ std::rotr(value, 19) ^ (value >> 10U);
}

[[nodiscard]] std::array<std::byte, kResourceResponseTailSha256Size>
sha256(const std::span<const std::byte> bytes) noexcept
{
    static constexpr std::array<std::uint32_t, 64U> round_constants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };

    std::array<std::uint32_t, 8U> hash{
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    };

    const auto block_count = (bytes.size() + 9U + 63U) / 64U;
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;
    std::array<std::byte, 64U> block{};
    std::array<std::uint32_t, 64U> words{};

    for (std::size_t block_index = 0U; block_index < block_count;
         ++block_index) {
        block.fill(std::byte{0});
        const auto block_offset = block_index * block.size();
        for (std::size_t index = 0U; index < block.size(); ++index) {
            const auto source_offset = block_offset + index;
            if (source_offset < bytes.size()) {
                block[index] = bytes[source_offset];
            } else if (source_offset == bytes.size()) {
                block[index] = std::byte{0x80U};
            }
        }

        if (block_index + 1U == block_count) {
            for (std::size_t index = 0U; index < 8U; ++index) {
                const auto shift = static_cast<unsigned int>((7U - index) * 8U);
                block[56U + index] = std::byte{static_cast<std::uint8_t>(
                    (bit_length >> shift) & 0xffU)};
            }
        }

        for (std::size_t index = 0U; index < 16U; ++index) {
            const auto offset = index * 4U;
            words[index] =
                (static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(block[offset]))
                 << 24U) |
                (static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(block[offset + 1U]))
                 << 16U) |
                (static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(block[offset + 2U]))
                 << 8U) |
                static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(block[offset + 3U]));
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            words[index] = small_sigma_one(words[index - 2U]) +
                           words[index - 7U] +
                           small_sigma_zero(words[index - 15U]) +
                           words[index - 16U];
        }

        auto a = hash[0U];
        auto b = hash[1U];
        auto c = hash[2U];
        auto d = hash[3U];
        auto e = hash[4U];
        auto f = hash[5U];
        auto g = hash[6U];
        auto h = hash[7U];

        for (std::size_t index = 0U; index < words.size(); ++index) {
            const auto temporary_one = h + big_sigma_one(e) +
                                       choose(e, f, g) +
                                       round_constants[index] + words[index];
            const auto temporary_two = big_sigma_zero(a) + majority(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + temporary_one;
            d = c;
            c = b;
            b = a;
            a = temporary_one + temporary_two;
        }

        hash[0U] += a;
        hash[1U] += b;
        hash[2U] += c;
        hash[3U] += d;
        hash[4U] += e;
        hash[5U] += f;
        hash[6U] += g;
        hash[7U] += h;
    }

    std::array<std::byte, kResourceResponseTailSha256Size> result{};
    for (std::size_t index = 0U; index < hash.size(); ++index) {
        const auto offset = index * 4U;
        result[offset] = std::byte{
            static_cast<std::uint8_t>((hash[index] >> 24U) & 0xffU)};
        result[offset + 1U] = std::byte{
            static_cast<std::uint8_t>((hash[index] >> 16U) & 0xffU)};
        result[offset + 2U] = std::byte{
            static_cast<std::uint8_t>((hash[index] >> 8U) & 0xffU)};
        result[offset + 3U] =
            std::byte{static_cast<std::uint8_t>(hash[index] & 0xffU)};
    }
    return result;
}

} // namespace

bool valid_resource_client_response_limits(
    const ResourceClientResponseLimits& limits) noexcept
{
    return limits.maximum_resource_response_size >=
               kOpcode5ResourceResponseSemanticSize &&
           limits.maximum_resource_response_size <=
               kMaximumResourceResponseSize &&
           limits.maximum_response_field_count >=
               kOpcode5ResourceResponseFieldCount &&
           limits.maximum_response_field_count <= kMaximumResponseFieldCount &&
           limits.maximum_response_opaque_bytes >=
               kOpcode5ResourceResponseOpaqueSize &&
           limits.maximum_response_opaque_bytes <=
               kMaximumResponseOpaqueBytes &&
           limits.maximum_concurrent_tail_size > 0U &&
           limits.maximum_concurrent_tail_size <= kMaximumConcurrentTailSize &&
           limits.maximum_pre_ack_server_payloads ==
               kDefaultMaximumPreAckServerPayloads &&
           limits.maximum_response_stage_events > 0U &&
           limits.maximum_response_stage_events <= kMaximumResponseStageEvents &&
           limits.maximum_post_response_payload_size > 0U &&
           limits.maximum_post_response_payload_size <=
               kMaximumPostResponsePayloadSize;
}

ResourceClientResponseInput::ResourceClientResponseInput(
    std::string wire_name,
    const std::uint8_t field_type,
    const std::uint16_t field_index,
    const std::uint8_t field_flags,
    resource_consistency::ResourceConsistencyMaterial material) noexcept
    : wire_name_{std::move(wire_name)},
      field_type_{field_type},
      field_index_{field_index},
      field_flags_{field_flags},
      material_{std::move(material)}
{
}

std::string_view ResourceClientResponseInput::wire_name() const noexcept
{
    return wire_name_;
}

std::uint8_t ResourceClientResponseInput::field_type() const noexcept
{
    return field_type_;
}

std::uint16_t ResourceClientResponseInput::field_index() const noexcept
{
    return field_index_;
}

std::uint32_t ResourceClientResponseInput::byte_count() const noexcept
{
    return material_.byte_count();
}

std::uint8_t ResourceClientResponseInput::field_flags() const noexcept
{
    return field_flags_;
}

std::size_t ResourceClientResponseInput::opaque_byte_count() const noexcept
{
    return material_.opaque_byte_count();
}

Opcode5ResourceResponse::Opcode5ResourceResponse(
    std::string wire_name,
    const std::uint16_t field_index,
    const std::uint32_t byte_count,
    std::array<std::byte, kOpcode5ResourceResponseOpaqueSize> opaque_bytes,
    const Opcode5ResourceResponseSourceGeometry source_geometry,
    const Opcode5ResourceResponseSourceProfile source_profile) noexcept
    : wire_name_{std::move(wire_name)},
      field_index_{field_index},
      byte_count_{byte_count},
      opaque_bytes_{opaque_bytes},
      source_geometry_{source_geometry},
      source_profile_{source_profile}
{
}

std::uint8_t Opcode5ResourceResponse::opcode() const noexcept
{
    return kOpcode5ResourceResponseOpcode;
}

std::uint16_t Opcode5ResourceResponse::entry_count() const noexcept
{
    return kOpcode5ResourceResponseEntryCount;
}

std::string_view Opcode5ResourceResponse::wire_name() const noexcept
{
    return wire_name_;
}

std::uint8_t Opcode5ResourceResponse::field_type() const noexcept
{
    return kOpcode5ResourceResponseFieldType;
}

std::uint16_t Opcode5ResourceResponse::field_index() const noexcept
{
    return field_index_;
}

std::uint32_t Opcode5ResourceResponse::byte_count() const noexcept
{
    return byte_count_;
}

std::uint8_t Opcode5ResourceResponse::field_flags() const noexcept
{
    return kOpcode5ResourceResponseFieldFlags;
}

std::size_t Opcode5ResourceResponse::opaque_byte_count() const noexcept
{
    return opaque_bytes_.size();
}

std::size_t Opcode5ResourceResponse::bytes_consumed() const noexcept
{
    return kOpcode5ResourceResponseSemanticSize;
}

const Opcode5ResourceResponseSourceGeometry&
Opcode5ResourceResponse::source_geometry() const noexcept
{
    return source_geometry_;
}

Opcode5ResourceResponseSourceProfile
Opcode5ResourceResponse::source_profile() const noexcept
{
    return source_profile_;
}

ResourceClientResponseCompatibilityProfile
Opcode5ResourceResponse::compatibility_profile() const noexcept
{
    return ResourceClientResponseCompatibilityProfile::
        stock_protocol_48_build_10210_opcode5_single_entry;
}

ResourceClientResponseEvidenceProfile
Opcode5ResourceResponse::evidence_profile() const noexcept
{
    return ResourceClientResponseEvidenceProfile::
        controlled_stock_exact_41_byte_layout_semantics_pending;
}

Opcode5ResourceResponseParser::Opcode5ResourceResponseParser(
    const ResourceClientResponseLimits limits,
    const ResourceClientResponseCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool Opcode5ResourceResponseParser::valid_configuration() const noexcept
{
    return valid_resource_client_response_limits(limits_) &&
           profile_ == ResourceClientResponseCompatibilityProfile::
                           stock_protocol_48_build_10210_opcode5_single_entry;
}

const ResourceClientResponseLimits& Opcode5ResourceResponseParser::limits()
    const noexcept
{
    return limits_;
}

Opcode5ResourceResponseParseResult Opcode5ResourceResponseParser::parse(
    const std::span<const std::byte> semantic_fragment,
    const Opcode5ResourceResponseSourceGeometry source_geometry,
    const Opcode5ResourceResponseSourceProfile source_profile) const
{
    if (!valid_configuration()) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::invalid_configuration,
            0U,
            "Resource-response limits or compatibility profile are invalid");
    }
    if (!valid_source_profile(source_profile) ||
        source_geometry.semantic_byte_count != semantic_fragment.size() ||
        source_geometry.semantic_byte_offset >
            source_geometry.source_body_byte_count ||
        source_geometry.semantic_byte_count >
            source_geometry.source_body_byte_count -
                source_geometry.semantic_byte_offset) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::invalid_source_geometry,
            0U,
            "Semantic fragment does not match its explicit source geometry");
    }
    if (semantic_fragment.size() < kOpcode5ResourceResponseSemanticSize) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::truncated_response,
            semantic_fragment.size(),
            "Semantic resource response is shorter than the exact profile");
    }
    if (semantic_fragment.size() > kOpcode5ResourceResponseSemanticSize) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::unexpected_trailing_bytes,
            kOpcode5ResourceResponseSemanticSize,
            "Semantic resource response has unexpected trailing bytes");
    }
    if (semantic_fragment.size() > limits_.maximum_resource_response_size) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::response_too_large,
            limits_.maximum_resource_response_size,
            "Semantic resource response exceeds its configured bound");
    }

    ByteReader reader{semantic_fragment};
    const auto opcode = reader.read_uint8();
    if (!opcode || *opcode != kOpcode5ResourceResponseOpcode) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::wrong_opcode,
            0U,
            "Semantic resource response has the wrong opcode");
    }
    const auto entry_count = reader.read_uint16_le();
    if (!entry_count || *entry_count != kOpcode5ResourceResponseEntryCount) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::unsupported_entry_count,
            1U,
            "Semantic resource response has an unsupported entry count");
    }

    const auto wire_name_bytes =
        reader.read_bytes(kOpcode5ResourceResponseWireNameSize);
    const auto wire_name_terminator = reader.read_uint8();
    if (!wire_name_bytes || !wire_name_terminator ||
        *wire_name_terminator != 0U) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::unterminated_wire_name,
            3U + kOpcode5ResourceResponseWireNameSize,
            "Semantic resource-response wire name is not exactly terminated");
    }
    const auto embedded_zero = std::ranges::find(
        *wire_name_bytes,
        std::byte{0});
    if (embedded_zero != wire_name_bytes->end()) {
        const auto offset = static_cast<std::size_t>(
            embedded_zero - wire_name_bytes->begin());
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::unsupported_wire_name_size,
            3U + offset,
            "Semantic resource-response wire name is not exactly 13 bytes");
    }

    const auto field_type = reader.read_uint8();
    if (!field_type || *field_type != kOpcode5ResourceResponseFieldType) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::unsupported_field_type,
            17U,
            "Semantic resource response has an unsupported type field");
    }
    const auto field_index = reader.read_uint16_le();
    if (!field_index || *field_index != kOpcode5ResourceResponseFieldIndex) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::unsupported_field_index,
            18U,
            "Semantic resource response has an unsupported reserved index");
    }
    const auto byte_count = reader.read_uint32_le();
    if (!byte_count) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::truncated_response,
            reader.position(),
            "Semantic resource-response byte-count field is truncated");
    }
    const auto field_flags = reader.read_uint8();
    if (!field_flags || *field_flags != kOpcode5ResourceResponseFieldFlags) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::unsupported_field_flags,
            24U,
            "Semantic resource response has unsupported flags/reserved bits");
    }
    const auto opaque_bytes =
        reader.read_bytes(kOpcode5ResourceResponseOpaqueSize);
    if (!opaque_bytes ||
        opaque_bytes->size() != kOpcode5ResourceResponseOpaqueSize) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::opaque_size_mismatch,
            25U,
            "Semantic resource-response opaque field has the wrong width");
    }
    if (reader.remaining() != 0U) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::unexpected_trailing_bytes,
            reader.position(),
            "Semantic resource response was not consumed exactly");
    }

    std::string wire_name;
    try {
        const auto* data = reinterpret_cast<const char*>(wire_name_bytes->data());
        wire_name.assign(data, wire_name_bytes->size());
    } catch (...) {
        return response_parse_failure(
            Opcode5ResourceResponseErrorCode::response_too_large,
            3U,
            "Unable to retain bounded semantic response state");
    }

    std::array<std::byte, kOpcode5ResourceResponseOpaqueSize> owning_opaque{};
    std::ranges::copy(*opaque_bytes, owning_opaque.begin());
    return Opcode5ResourceResponseParseResult{
        Opcode5ResourceResponse{
            std::move(wire_name),
            *field_index,
            *byte_count,
            owning_opaque,
            source_geometry,
            source_profile},
        std::nullopt,
        kOpcode5ResourceResponseSemanticSize,
    };
}

EncodedOpcode5ResourceResponse::EncodedOpcode5ResourceResponse(
    Opcode5ResourceResponse response,
    std::array<std::byte, kOpcode5ResourceResponseSemanticSize> semantic_bytes)
    noexcept
    : response_{std::move(response)}, semantic_bytes_{semantic_bytes}
{
}

const Opcode5ResourceResponse& EncodedOpcode5ResourceResponse::response()
    const noexcept
{
    return response_;
}

std::span<const std::byte> EncodedOpcode5ResourceResponse::semantic_bytes()
    const noexcept
{
    return semantic_bytes_;
}

Opcode5ResourceResponseBuilder::Opcode5ResourceResponseBuilder(
    const ResourceClientResponseLimits limits,
    const ResourceClientResponseCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool Opcode5ResourceResponseBuilder::valid_configuration() const noexcept
{
    return valid_resource_client_response_limits(limits_) &&
           profile_ == ResourceClientResponseCompatibilityProfile::
                           stock_protocol_48_build_10210_opcode5_single_entry;
}

const ResourceClientResponseLimits& Opcode5ResourceResponseBuilder::limits()
    const noexcept
{
    return limits_;
}

Opcode5ResourceResponseBuildResult Opcode5ResourceResponseBuilder::build(
    ResourceClientResponseInput input) const
{
    if (!valid_configuration()) {
        return response_build_failure(
            Opcode5ResourceResponseErrorCode::invalid_configuration,
            0U,
            "Resource-response limits or compatibility profile are invalid");
    }
    if (input.wire_name_.size() != kOpcode5ResourceResponseWireNameSize ||
        input.wire_name_.find('\0') != std::string::npos) {
        return response_build_failure(
            Opcode5ResourceResponseErrorCode::unsupported_wire_name_size,
            3U,
            "Resource-response input requires exactly 13 non-NUL name bytes");
    }
    if (input.field_type_ != kOpcode5ResourceResponseFieldType) {
        return response_build_failure(
            Opcode5ResourceResponseErrorCode::unsupported_field_type,
            17U,
            "Resource-response input has an unsupported type field");
    }
    if (input.field_index_ != kOpcode5ResourceResponseFieldIndex) {
        return response_build_failure(
            Opcode5ResourceResponseErrorCode::unsupported_field_index,
            18U,
            "Resource-response input has an unsupported reserved index");
    }
    if (input.field_flags_ != kOpcode5ResourceResponseFieldFlags) {
        return response_build_failure(
            Opcode5ResourceResponseErrorCode::unsupported_field_flags,
            24U,
            "Resource-response input has unsupported flags/reserved bits");
    }
    if (input.material_.opaque_bytes_.size() !=
            kOpcode5ResourceResponseOpaqueSize ||
        input.material_.opaque_bytes_.size() >
            limits_.maximum_response_opaque_bytes) {
        return response_build_failure(
            Opcode5ResourceResponseErrorCode::opaque_size_mismatch,
            25U,
            "Resource-response input requires exactly 16 opaque material bytes");
    }

    std::array<std::byte, kOpcode5ResourceResponseSemanticSize> bytes{};
    ByteWriter writer{bytes};
    const auto name_bytes = std::as_bytes(
        std::span{input.wire_name_.data(), input.wire_name_.size()});
    if (!writer.write_uint8(kOpcode5ResourceResponseOpcode) ||
        !writer.write_uint16_le(kOpcode5ResourceResponseEntryCount) ||
        !writer.write_bytes(name_bytes) || !writer.write_uint8(0U) ||
        !writer.write_uint8(input.field_type_) ||
        !writer.write_uint16_le(input.field_index_) ||
        !writer.write_uint32_le(input.material_.byte_count_) ||
        !writer.write_uint8(input.field_flags_) ||
        !writer.write_bytes(input.material_.opaque_bytes_) ||
        writer.position() != bytes.size()) {
        return response_build_failure(
            Opcode5ResourceResponseErrorCode::internal_encoding_error,
            writer.position(),
            "Unable to encode the exact bounded semantic response");
    }

    std::array<std::byte, kOpcode5ResourceResponseOpaqueSize> owning_opaque{};
    std::ranges::copy(input.material_.opaque_bytes_, owning_opaque.begin());
    Opcode5ResourceResponse response{
        std::move(input.wire_name_),
        input.field_index_,
        input.material_.byte_count_,
        owning_opaque,
        Opcode5ResourceResponseSourceGeometry{
            0U,
            kOpcode5ResourceResponseSemanticSize,
            kOpcode5ResourceResponseSemanticSize},
        Opcode5ResourceResponseSourceProfile::canonical_builder_output};
    return Opcode5ResourceResponseBuildResult{
        EncodedOpcode5ResourceResponse{std::move(response), bytes},
        std::nullopt,
    };
}

ResourceResponseByteRange::ResourceResponseByteRange(
    const std::size_t byte_offset,
    const std::size_t byte_count) noexcept
    : byte_offset_{byte_offset}, byte_count_{byte_count}
{
}

std::size_t ResourceResponseByteRange::byte_offset() const noexcept
{
    return byte_offset_;
}

std::size_t ResourceResponseByteRange::byte_count() const noexcept
{
    return byte_count_;
}

std::size_t ResourceResponseByteRange::end_byte_offset() const noexcept
{
    return byte_offset_ + byte_count_;
}

ResourceResponseCarrierGeometry::ResourceResponseCarrierGeometry(
    const ResourceResponseByteRange descriptor_range,
    const ResourceResponseByteRange semantic_reliable_range,
    const ResourceResponseByteRange tail_range,
    const std::size_t full_decoded_body_size,
    const std::uint32_t source_sequence,
    const std::uint64_t reliable_generation) noexcept
    : descriptor_range_{descriptor_range},
      semantic_reliable_range_{semantic_reliable_range},
      tail_range_{tail_range},
      full_decoded_body_size_{full_decoded_body_size},
      source_sequence_{source_sequence},
      reliable_generation_{reliable_generation}
{
}

const ResourceResponseByteRange&
ResourceResponseCarrierGeometry::descriptor_range() const noexcept
{
    return descriptor_range_;
}

const ResourceResponseByteRange&
ResourceResponseCarrierGeometry::semantic_reliable_range() const noexcept
{
    return semantic_reliable_range_;
}

const ResourceResponseByteRange& ResourceResponseCarrierGeometry::tail_range()
    const noexcept
{
    return tail_range_;
}

std::size_t ResourceResponseCarrierGeometry::full_decoded_body_size()
    const noexcept
{
    return full_decoded_body_size_;
}

std::uint32_t ResourceResponseCarrierGeometry::source_sequence() const noexcept
{
    return source_sequence_;
}

std::uint64_t ResourceResponseCarrierGeometry::reliable_generation()
    const noexcept
{
    return reliable_generation_;
}

ResourceResponseConcurrentTail::ResourceResponseConcurrentTail(
    const std::size_t byte_count,
    std::array<std::byte, kResourceResponseTailSha256Size> sha256_value,
    const ResourceResponseConcurrentTailProfile profile) noexcept
    : byte_count_{byte_count}, sha256_{sha256_value}, profile_{profile}
{
}

std::size_t ResourceResponseConcurrentTail::byte_count() const noexcept
{
    return byte_count_;
}

const std::array<std::byte, kResourceResponseTailSha256Size>&
ResourceResponseConcurrentTail::sha256() const noexcept
{
    return sha256_;
}

ResourceResponseConcurrentTailProfile
ResourceResponseConcurrentTail::profile() const noexcept
{
    return profile_;
}

ResourceResponseConcurrentTailEvidenceProfile
ResourceResponseConcurrentTail::evidence_profile() const noexcept
{
    return ResourceResponseConcurrentTailEvidenceProfile::
        controlled_stock_bounded_metadata_only;
}

Opcode5ResourceResponseCarrierState::Opcode5ResourceResponseCarrierState(
    Opcode5ResourceResponse response,
    const ResourceResponseCarrierGeometry geometry,
    const ResourceResponseConcurrentTail concurrent_tail) noexcept
    : response_{std::move(response)},
      geometry_{geometry},
      concurrent_tail_{concurrent_tail}
{
}

const Opcode5ResourceResponse& Opcode5ResourceResponseCarrierState::response()
    const noexcept
{
    return response_;
}

const ResourceResponseCarrierGeometry&
Opcode5ResourceResponseCarrierState::geometry() const noexcept
{
    return geometry_;
}

const ResourceResponseConcurrentTail&
Opcode5ResourceResponseCarrierState::concurrent_tail() const noexcept
{
    return concurrent_tail_;
}

Opcode5ResourceResponseCarrierParser::Opcode5ResourceResponseCarrierParser(
    const ResourceClientResponseLimits limits,
    const ResourceClientResponseCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool Opcode5ResourceResponseCarrierParser::valid_configuration() const noexcept
{
    return valid_resource_client_response_limits(limits_) &&
           profile_ == ResourceClientResponseCompatibilityProfile::
                           stock_protocol_48_build_10210_opcode5_single_entry;
}

Opcode5ResourceResponseCarrierParseResult
Opcode5ResourceResponseCarrierParser::parse(
    const NetchanHeader& header,
    const std::span<const std::byte> decoded_body,
    const std::uint64_t reliable_generation) const
{
    if (!valid_configuration()) {
        return carrier_failure(
            ResourceResponseCarrierErrorCode::invalid_configuration,
            0U,
            "Resource-response carrier configuration is invalid");
    }
    if (reliable_generation == 0U) {
        return carrier_failure(
            ResourceResponseCarrierErrorCode::invalid_reliable_generation,
            0U,
            "Resource-response carrier requires a nonzero reliable generation");
    }

    const auto maximum_carrier_body_size =
        kMaximumDecodedDescriptorAreaSize +
        limits_.maximum_resource_response_size +
        limits_.maximum_concurrent_tail_size;
    if (decoded_body.size() > maximum_carrier_body_size) {
        return carrier_failure(
            ResourceResponseCarrierErrorCode::carrier_too_large,
            maximum_carrier_body_size,
            "Decoded resource-response carrier exceeds its configured bound");
    }
    if (!header.sequence.flags.fragmented ||
        !header.sequence.flags.reliable) {
        return carrier_failure(
            ResourceResponseCarrierErrorCode::reliable_fragment_required,
            0U,
            "Opcode-5 response evidence requires a reliable fragment carrier");
    }

    const auto netchan_limit =
        kNetchanHeaderSize + maximum_carrier_body_size;
    if (netchan_limit > kMaximumNetchanDatagramSize) {
        return carrier_failure(
            ResourceResponseCarrierErrorCode::invalid_configuration,
            0U,
            "Resource-response carrier limits exceed the netchan hard cap");
    }
    auto decoded = decode_netchan_fragment_body(
        NetchanDirection::client_to_server,
        header,
        decoded_body,
        NetchanPacketLimits{netchan_limit});
    if (!decoded) {
        const auto nested = decoded.error
                                ? std::optional{decoded.error->code}
                                : std::nullopt;
        const auto offset = decoded.error ? decoded.error->byte_offset : 0U;
        return carrier_failure(
            ResourceResponseCarrierErrorCode::transport_decode_failed,
            offset,
            "Strict netchan fragment descriptor decoding failed",
            nested);
    }

    const auto& packet = *decoded.packet;
    if (!packet.fragments[0U] || packet.fragments[1U]) {
        return carrier_failure(
            ResourceResponseCarrierErrorCode::unsupported_fragment_stream,
            0U,
            "Supported response carrier requires only normal fragment slot 0");
    }
    const auto& descriptor = *packet.fragments[0U];
    const auto packed_id = descriptor.packed_id();
    if (!packed_id || packed_id->fragment_index() != 1U ||
        packed_id->fragment_count() != 1U || descriptor.offset != 0U ||
        descriptor.payload_offset != 0U) {
        return carrier_failure(
            ResourceResponseCarrierErrorCode::unsupported_fragment_geometry,
            0U,
            "Response carrier descriptor is outside the exact 1-of-1 profile");
    }
    if (descriptor.length != kOpcode5ResourceResponseSemanticSize ||
        packet.fragment_payload_size !=
            kOpcode5ResourceResponseSemanticSize ||
        packet.payload.size() < packet.fragment_payload_size) {
        return carrier_failure(
            ResourceResponseCarrierErrorCode::semantic_range_mismatch,
            0U,
            "Descriptor-derived reliable semantic range is not exactly 41 bytes");
    }

    const auto descriptor_byte_count =
        decoded_body.size() - packet.payload.size();
    const auto semantic_byte_offset =
        descriptor_byte_count + descriptor.payload_offset;
    const auto tail_byte_offset =
        descriptor_byte_count + packet.fragment_payload_size;
    const auto tail_byte_count =
        packet.payload.size() - packet.fragment_payload_size;
    if (semantic_byte_offset > decoded_body.size() ||
        kOpcode5ResourceResponseSemanticSize >
            decoded_body.size() - semantic_byte_offset ||
        tail_byte_offset > decoded_body.size() ||
        tail_byte_count > decoded_body.size() - tail_byte_offset ||
        tail_byte_offset + tail_byte_count != decoded_body.size()) {
        return carrier_failure(
            ResourceResponseCarrierErrorCode::semantic_range_mismatch,
            semantic_byte_offset,
            "Descriptor-derived response ranges overrun the decoded carrier");
    }
    if (tail_byte_count > limits_.maximum_concurrent_tail_size) {
        return carrier_failure(
            ResourceResponseCarrierErrorCode::concurrent_tail_too_large,
            tail_byte_offset,
            "Concurrent response tail exceeds its configured metadata bound");
    }
    const auto tail_profile = tail_profile_for_size(tail_byte_count);
    if (!tail_profile) {
        return carrier_failure(
            ResourceResponseCarrierErrorCode::
                unsupported_concurrent_tail_profile,
            tail_byte_offset,
            "Concurrent response tail length is outside observed profiles");
    }

    const std::span<const std::byte> decoded_payload{packet.payload};
    const auto semantic_fragment = decoded_payload.first(
        kOpcode5ResourceResponseSemanticSize);
    Opcode5ResourceResponseParser parser{limits_, profile_};
    auto parsed = parser.parse(
        semantic_fragment,
        Opcode5ResourceResponseSourceGeometry{
            semantic_byte_offset,
            semantic_fragment.size(),
            decoded_body.size()},
        Opcode5ResourceResponseSourceProfile::
            captured_reliable_semantic_fragment);
    if (!parsed) {
        const auto nested = parsed.error
                                ? std::optional{parsed.error->code}
                                : std::nullopt;
        const auto offset = parsed.error
                                ? semantic_byte_offset +
                                      parsed.error->byte_offset
                                : semantic_byte_offset;
        return carrier_failure(
            ResourceResponseCarrierErrorCode::semantic_response_invalid,
            offset,
            "Descriptor-selected semantic response is invalid",
            std::nullopt,
            nested);
    }

    const auto tail_bytes =
        decoded_payload.subspan(packet.fragment_payload_size);
    ResourceResponseCarrierGeometry geometry{
        ResourceResponseByteRange{0U, descriptor_byte_count},
        ResourceResponseByteRange{
            semantic_byte_offset,
            kOpcode5ResourceResponseSemanticSize},
        ResourceResponseByteRange{tail_byte_offset, tail_byte_count},
        decoded_body.size(),
        header.sequence.sequence.value(),
        reliable_generation};
    ResourceResponseConcurrentTail concurrent_tail{
        tail_byte_count,
        sha256(tail_bytes),
        *tail_profile};
    return Opcode5ResourceResponseCarrierParseResult{
        Opcode5ResourceResponseCarrierState{
            std::move(*parsed.response),
            geometry,
            concurrent_tail},
        std::nullopt,
    };
}

PostResourceResponseBoundary::PostResourceResponseBoundary(
    const PostResourceResponseBoundaryKind kind,
    std::optional<std::uint8_t> opcode,
    const std::size_t remaining_byte_count,
    const PostResourceResponseSourcePayloadMetadata source_payload) noexcept
    : kind_{kind},
      opcode_{std::move(opcode)},
      remaining_byte_count_{remaining_byte_count},
      source_payload_{source_payload}
{
}

PostResourceResponseBoundaryKind PostResourceResponseBoundary::kind()
    const noexcept
{
    return kind_;
}

const std::optional<std::uint8_t>& PostResourceResponseBoundary::opcode()
    const noexcept
{
    return opcode_;
}

std::size_t PostResourceResponseBoundary::byte_offset() const noexcept
{
    return 0U;
}

std::size_t PostResourceResponseBoundary::bit_offset() const noexcept
{
    return 0U;
}

std::size_t PostResourceResponseBoundary::remaining_byte_count() const noexcept
{
    return remaining_byte_count_;
}

const PostResourceResponseSourcePayloadMetadata&
PostResourceResponseBoundary::source_payload() const noexcept
{
    return source_payload_;
}

PostResourceResponseEvidenceProfile
PostResourceResponseBoundary::evidence_profile() const noexcept
{
    return PostResourceResponseEvidenceProfile::
        first_complete_server_payload_no_opcode_scanning;
}

PostResourceResponseBoundaryParser::PostResourceResponseBoundaryParser(
    const ResourceClientResponseLimits limits) noexcept
    : limits_{limits}
{
}

bool PostResourceResponseBoundaryParser::valid_configuration() const noexcept
{
    return valid_resource_client_response_limits(limits_);
}

PostResourceResponseBoundaryParseResult
PostResourceResponseBoundaryParser::parse(
    const std::span<const std::byte> complete_decoded_payload,
    PostResourceResponseSourcePayloadMetadata source_payload) const
{
    if (!valid_configuration()) {
        return boundary_failure(
            PostResourceResponseBoundaryErrorCode::invalid_configuration,
            0U,
            "Post-response boundary configuration is invalid");
    }
    if (source_payload.direction != NetchanDirection::server_to_client ||
        source_payload.source_sequence > kNetchanSequenceMask ||
        source_payload.decoded_payload_byte_count !=
            complete_decoded_payload.size()) {
        return boundary_failure(
            PostResourceResponseBoundaryErrorCode::invalid_source_metadata,
            0U,
            "Complete server payload does not match its explicit source metadata");
    }
    if (complete_decoded_payload.size() >
        limits_.maximum_post_response_payload_size) {
        return boundary_failure(
            PostResourceResponseBoundaryErrorCode::payload_too_large,
            limits_.maximum_post_response_payload_size,
            "Post-response server payload exceeds its configured bound");
    }
    if (complete_decoded_payload.empty()) {
        return PostResourceResponseBoundaryParseResult{
            PostResourceResponseBoundary{
                PostResourceResponseBoundaryKind::exact_end_of_payload,
                std::nullopt,
                0U,
                source_payload},
            std::nullopt,
        };
    }

    // Deliberately read only byte zero. No scan or speculative parsing of the
    // following complex body occurs at this milestone boundary.
    const auto opcode =
        std::to_integer<std::uint8_t>(complete_decoded_payload.front());
    return PostResourceResponseBoundaryParseResult{
        PostResourceResponseBoundary{
            PostResourceResponseBoundaryKind::opcode_at_payload_start,
            opcode,
            complete_decoded_payload.size() - 1U,
            source_payload},
        std::nullopt,
    };
}

} // namespace hlclient::goldsrc
