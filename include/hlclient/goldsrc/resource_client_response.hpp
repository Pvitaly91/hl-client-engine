#pragma once

#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/resource_consistency/provider.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::goldsrc {

inline constexpr std::uint8_t kOpcode5ResourceResponseOpcode = 5U;
inline constexpr std::uint16_t kOpcode5ResourceResponseEntryCount = 1U;
inline constexpr std::size_t kOpcode5ResourceResponseWireNameSize = 13U;
inline constexpr std::uint8_t kOpcode5ResourceResponseFieldType = 3U;
inline constexpr std::uint16_t kOpcode5ResourceResponseFieldIndex = 0U;
inline constexpr std::uint8_t kOpcode5ResourceResponseFieldFlags = 4U;
inline constexpr std::size_t kOpcode5ResourceResponseOpaqueSize = 16U;
inline constexpr std::size_t kOpcode5ResourceResponseSemanticSize = 41U;
inline constexpr std::size_t kOpcode5ResourceResponseFieldCount = 8U;
inline constexpr std::size_t kResourceResponseTailSha256Size = 32U;

// Project safety policy. These are bounded implementation limits, not claims
// about an engine-wide Protocol 48 maximum.
inline constexpr std::size_t kDefaultMaximumResourceResponseSize = 41U;
inline constexpr std::size_t kMaximumResourceResponseSize = 4'096U;
inline constexpr std::size_t kDefaultMaximumResponseFieldCount = 16U;
inline constexpr std::size_t kMaximumResponseFieldCount = 256U;
inline constexpr std::size_t kDefaultMaximumResponseOpaqueBytes = 16U;
inline constexpr std::size_t kMaximumResponseOpaqueBytes = 4'096U;
inline constexpr std::size_t kDefaultMaximumConcurrentTailSize = 64U;
inline constexpr std::size_t kMaximumConcurrentTailSize = 4'096U;
inline constexpr std::size_t kDefaultMaximumPreAckServerPayloads = 1U;
inline constexpr std::size_t kMaximumPreAckServerPayloads = 1U;
inline constexpr std::size_t kDefaultMaximumResponseStageEvents = 64U;
inline constexpr std::size_t kMaximumResponseStageEvents = 256U;
inline constexpr std::size_t kDefaultMaximumPostResponsePayloadSize =
    65'536U;
inline constexpr std::size_t kMaximumPostResponsePayloadSize = 1'048'576U;
inline constexpr std::size_t kResourceClientResponseDiagnosticTextLimit = 256U;

struct ResourceClientResponseLimits {
    std::size_t maximum_resource_response_size{
        kDefaultMaximumResourceResponseSize};
    std::size_t maximum_response_field_count{
        kDefaultMaximumResponseFieldCount};
    std::size_t maximum_response_opaque_bytes{
        kDefaultMaximumResponseOpaqueBytes};
    std::size_t maximum_concurrent_tail_size{
        kDefaultMaximumConcurrentTailSize};
    std::size_t maximum_pre_ack_server_payloads{
        kDefaultMaximumPreAckServerPayloads};
    std::size_t maximum_response_stage_events{
        kDefaultMaximumResponseStageEvents};
    std::size_t maximum_post_response_payload_size{
        kDefaultMaximumPostResponsePayloadSize};
};

[[nodiscard]] bool valid_resource_client_response_limits(
    const ResourceClientResponseLimits& limits) noexcept;

enum class ResourceClientResponseCompatibilityProfile {
    stock_protocol_48_build_10210_opcode5_single_entry,
};

enum class ResourceClientResponseEvidenceProfile {
    controlled_stock_exact_41_byte_layout_semantics_pending,
};

enum class Opcode5ResourceResponseSourceProfile {
    captured_reliable_semantic_fragment,
    independently_authored_synthetic_fixture,
    canonical_builder_output,
};

// The semantic parser receives only the semantic bytes. This explicit
// metadata retains where those bytes came from without retaining their source
// carrier or exposing a receive-buffer view.
struct Opcode5ResourceResponseSourceGeometry {
    std::size_t semantic_byte_offset{0U};
    std::size_t semantic_byte_count{0U};
    std::size_t source_body_byte_count{0U};
};

class ResourceClientResponseInput final {
public:
    ResourceClientResponseInput(
        std::string wire_name,
        std::uint8_t field_type,
        std::uint16_t field_index,
        std::uint8_t field_flags,
        resource_consistency::ResourceConsistencyMaterial material) noexcept;
    ~ResourceClientResponseInput() = default;

    ResourceClientResponseInput(ResourceClientResponseInput&&) noexcept =
        default;
    ResourceClientResponseInput& operator=(
        ResourceClientResponseInput&&) noexcept = default;
    ResourceClientResponseInput(const ResourceClientResponseInput&) = delete;
    ResourceClientResponseInput& operator=(
        const ResourceClientResponseInput&) = delete;

    [[nodiscard]] std::string_view wire_name() const noexcept;
    [[nodiscard]] std::uint8_t field_type() const noexcept;
    [[nodiscard]] std::uint16_t field_index() const noexcept;
    [[nodiscard]] std::uint32_t byte_count() const noexcept;
    [[nodiscard]] std::uint8_t field_flags() const noexcept;
    [[nodiscard]] std::size_t opaque_byte_count() const noexcept;

private:
    friend class Opcode5ResourceResponseBuilder;

    std::string wire_name_;
    std::uint8_t field_type_{0U};
    std::uint16_t field_index_{0U};
    std::uint8_t field_flags_{0U};
    resource_consistency::ResourceConsistencyMaterial material_;
};

class Opcode5ResourceResponse final {
public:
    Opcode5ResourceResponse(const Opcode5ResourceResponse&) = default;
    Opcode5ResourceResponse& operator=(const Opcode5ResourceResponse&) = delete;
    Opcode5ResourceResponse(Opcode5ResourceResponse&&) noexcept = default;
    Opcode5ResourceResponse& operator=(Opcode5ResourceResponse&&) = delete;
    ~Opcode5ResourceResponse() = default;

    [[nodiscard]] std::uint8_t opcode() const noexcept;
    [[nodiscard]] std::uint16_t entry_count() const noexcept;
    [[nodiscard]] std::string_view wire_name() const noexcept;
    [[nodiscard]] std::uint8_t field_type() const noexcept;
    [[nodiscard]] std::uint16_t field_index() const noexcept;
    [[nodiscard]] std::uint32_t byte_count() const noexcept;
    [[nodiscard]] std::uint8_t field_flags() const noexcept;
    [[nodiscard]] std::size_t opaque_byte_count() const noexcept;
    [[nodiscard]] std::size_t bytes_consumed() const noexcept;
    [[nodiscard]] const Opcode5ResourceResponseSourceGeometry& source_geometry()
        const noexcept;
    [[nodiscard]] Opcode5ResourceResponseSourceProfile source_profile()
        const noexcept;
    [[nodiscard]] ResourceClientResponseCompatibilityProfile
    compatibility_profile() const noexcept;
    [[nodiscard]] ResourceClientResponseEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class Opcode5ResourceResponseParser;
    friend class Opcode5ResourceResponseBuilder;

    Opcode5ResourceResponse(
        std::string wire_name,
        std::uint16_t field_index,
        std::uint32_t byte_count,
        std::array<std::byte, kOpcode5ResourceResponseOpaqueSize> opaque_bytes,
        Opcode5ResourceResponseSourceGeometry source_geometry,
        Opcode5ResourceResponseSourceProfile source_profile) noexcept;

    std::string wire_name_;
    std::uint16_t field_index_{0U};
    std::uint32_t byte_count_{0U};
    // Fixed provider/evidence bytes are intentionally private. The response
    // state has no raw-message or opaque-byte getter.
    std::array<std::byte, kOpcode5ResourceResponseOpaqueSize> opaque_bytes_{};
    Opcode5ResourceResponseSourceGeometry source_geometry_{};
    Opcode5ResourceResponseSourceProfile source_profile_{
        Opcode5ResourceResponseSourceProfile::
            captured_reliable_semantic_fragment};
};

enum class Opcode5ResourceResponseErrorCode {
    invalid_configuration,
    invalid_source_geometry,
    response_too_large,
    truncated_response,
    unexpected_trailing_bytes,
    wrong_opcode,
    unsupported_entry_count,
    unterminated_wire_name,
    unsupported_wire_name_size,
    unsupported_field_type,
    unsupported_field_index,
    unsupported_field_flags,
    opaque_size_mismatch,
    internal_encoding_error,
};

struct Opcode5ResourceResponseError {
    Opcode5ResourceResponseErrorCode code{
        Opcode5ResourceResponseErrorCode::truncated_response};
    std::size_t byte_offset{0U};
    std::string context;
};

struct Opcode5ResourceResponseParseResult {
    std::optional<Opcode5ResourceResponse> response;
    std::optional<Opcode5ResourceResponseError> error;
    std::size_t bytes_consumed{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return response.has_value();
    }
};

class Opcode5ResourceResponseParser final {
public:
    explicit Opcode5ResourceResponseParser(
        ResourceClientResponseLimits limits = {},
        ResourceClientResponseCompatibilityProfile profile =
            ResourceClientResponseCompatibilityProfile::
                stock_protocol_48_build_10210_opcode5_single_entry) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const ResourceClientResponseLimits& limits() const noexcept;
    [[nodiscard]] Opcode5ResourceResponseParseResult parse(
        std::span<const std::byte> semantic_fragment,
        Opcode5ResourceResponseSourceGeometry source_geometry,
        Opcode5ResourceResponseSourceProfile source_profile) const;

private:
    ResourceClientResponseLimits limits_;
    ResourceClientResponseCompatibilityProfile profile_;
};

// Builder output is a distinct bounded transport object. The typed response
// itself deliberately has no raw getter; only this canonical semantic encoding
// can be queued on the existing reliable driver.
class EncodedOpcode5ResourceResponse final {
public:
    EncodedOpcode5ResourceResponse(const EncodedOpcode5ResourceResponse&) =
        default;
    EncodedOpcode5ResourceResponse& operator=(
        const EncodedOpcode5ResourceResponse&) = delete;
    EncodedOpcode5ResourceResponse(EncodedOpcode5ResourceResponse&&) noexcept =
        default;
    EncodedOpcode5ResourceResponse& operator=(
        EncodedOpcode5ResourceResponse&&) = delete;
    ~EncodedOpcode5ResourceResponse() = default;

    [[nodiscard]] const Opcode5ResourceResponse& response() const noexcept;
    [[nodiscard]] std::span<const std::byte> semantic_bytes() const noexcept;

private:
    friend class Opcode5ResourceResponseBuilder;

    EncodedOpcode5ResourceResponse(
        Opcode5ResourceResponse response,
        std::array<std::byte, kOpcode5ResourceResponseSemanticSize>
            semantic_bytes) noexcept;

    Opcode5ResourceResponse response_;
    std::array<std::byte, kOpcode5ResourceResponseSemanticSize> semantic_bytes_{};
};

struct Opcode5ResourceResponseBuildResult {
    std::optional<EncodedOpcode5ResourceResponse> encoding;
    std::optional<Opcode5ResourceResponseError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return encoding.has_value();
    }
};

class Opcode5ResourceResponseBuilder final {
public:
    explicit Opcode5ResourceResponseBuilder(
        ResourceClientResponseLimits limits = {},
        ResourceClientResponseCompatibilityProfile profile =
            ResourceClientResponseCompatibilityProfile::
                stock_protocol_48_build_10210_opcode5_single_entry) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const ResourceClientResponseLimits& limits() const noexcept;
    [[nodiscard]] Opcode5ResourceResponseBuildResult build(
        ResourceClientResponseInput input) const;

private:
    ResourceClientResponseLimits limits_;
    ResourceClientResponseCompatibilityProfile profile_;
};

class ResourceResponseByteRange final {
public:
    [[nodiscard]] std::size_t byte_offset() const noexcept;
    [[nodiscard]] std::size_t byte_count() const noexcept;
    [[nodiscard]] std::size_t end_byte_offset() const noexcept;

private:
    friend class Opcode5ResourceResponseCarrierParser;

    ResourceResponseByteRange(
        std::size_t byte_offset,
        std::size_t byte_count) noexcept;

    std::size_t byte_offset_{0U};
    std::size_t byte_count_{0U};
};

class ResourceResponseCarrierGeometry final {
public:
    [[nodiscard]] const ResourceResponseByteRange& descriptor_range()
        const noexcept;
    [[nodiscard]] const ResourceResponseByteRange& semantic_reliable_range()
        const noexcept;
    [[nodiscard]] const ResourceResponseByteRange& tail_range() const noexcept;
    [[nodiscard]] std::size_t full_decoded_body_size() const noexcept;
    [[nodiscard]] std::uint32_t source_sequence() const noexcept;
    [[nodiscard]] std::uint64_t reliable_generation() const noexcept;

private:
    friend class Opcode5ResourceResponseCarrierParser;

    ResourceResponseCarrierGeometry(
        ResourceResponseByteRange descriptor_range,
        ResourceResponseByteRange semantic_reliable_range,
        ResourceResponseByteRange tail_range,
        std::size_t full_decoded_body_size,
        std::uint32_t source_sequence,
        std::uint64_t reliable_generation) noexcept;

    ResourceResponseByteRange descriptor_range_;
    ResourceResponseByteRange semantic_reliable_range_;
    ResourceResponseByteRange tail_range_;
    std::size_t full_decoded_body_size_{0U};
    std::uint32_t source_sequence_{0U};
    std::uint64_t reliable_generation_{0U};
};

enum class ResourceResponseConcurrentTailProfile {
    stock_opaque_length_11,
    stock_coalesced_opaque_length_13,
    stock_coalesced_opaque_length_15,
    stock_coalesced_opaque_length_17,
};

enum class ResourceResponseConcurrentTailEvidenceProfile {
    controlled_stock_bounded_metadata_only,
};

class ResourceResponseConcurrentTail final {
public:
    [[nodiscard]] std::size_t byte_count() const noexcept;
    [[nodiscard]] const std::array<std::byte, kResourceResponseTailSha256Size>&
    sha256() const noexcept;
    [[nodiscard]] ResourceResponseConcurrentTailProfile profile() const noexcept;
    [[nodiscard]] ResourceResponseConcurrentTailEvidenceProfile
    evidence_profile() const noexcept;

private:
    friend class Opcode5ResourceResponseCarrierParser;

    ResourceResponseConcurrentTail(
        std::size_t byte_count,
        std::array<std::byte, kResourceResponseTailSha256Size> sha256,
        ResourceResponseConcurrentTailProfile profile) noexcept;

    std::size_t byte_count_{0U};
    std::array<std::byte, kResourceResponseTailSha256Size> sha256_{};
    ResourceResponseConcurrentTailProfile profile_{
        ResourceResponseConcurrentTailProfile::stock_opaque_length_11};
};

class Opcode5ResourceResponseCarrierState final {
public:
    Opcode5ResourceResponseCarrierState(
        const Opcode5ResourceResponseCarrierState&) = default;
    Opcode5ResourceResponseCarrierState& operator=(
        const Opcode5ResourceResponseCarrierState&) = delete;
    Opcode5ResourceResponseCarrierState(
        Opcode5ResourceResponseCarrierState&&) noexcept = default;
    Opcode5ResourceResponseCarrierState& operator=(
        Opcode5ResourceResponseCarrierState&&) = delete;
    ~Opcode5ResourceResponseCarrierState() = default;

    [[nodiscard]] const Opcode5ResourceResponse& response() const noexcept;
    [[nodiscard]] const ResourceResponseCarrierGeometry& geometry()
        const noexcept;
    [[nodiscard]] const ResourceResponseConcurrentTail& concurrent_tail()
        const noexcept;

private:
    friend class Opcode5ResourceResponseCarrierParser;

    Opcode5ResourceResponseCarrierState(
        Opcode5ResourceResponse response,
        ResourceResponseCarrierGeometry geometry,
        ResourceResponseConcurrentTail concurrent_tail) noexcept;

    Opcode5ResourceResponse response_;
    ResourceResponseCarrierGeometry geometry_;
    ResourceResponseConcurrentTail concurrent_tail_;
};

enum class ResourceResponseCarrierErrorCode {
    invalid_configuration,
    invalid_reliable_generation,
    carrier_too_large,
    transport_decode_failed,
    reliable_fragment_required,
    unsupported_fragment_stream,
    unsupported_fragment_geometry,
    semantic_range_mismatch,
    concurrent_tail_too_large,
    unsupported_concurrent_tail_profile,
    semantic_response_invalid,
};

struct ResourceResponseCarrierError {
    ResourceResponseCarrierErrorCode code{
        ResourceResponseCarrierErrorCode::transport_decode_failed};
    std::size_t byte_offset{0U};
    std::string context;
    std::optional<NetchanPacketErrorCode> transport_error;
    std::optional<Opcode5ResourceResponseErrorCode> response_error;
};

struct Opcode5ResourceResponseCarrierParseResult {
    std::optional<Opcode5ResourceResponseCarrierState> state;
    std::optional<ResourceResponseCarrierError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class Opcode5ResourceResponseCarrierParser final {
public:
    explicit Opcode5ResourceResponseCarrierParser(
        ResourceClientResponseLimits limits = {},
        ResourceClientResponseCompatibilityProfile profile =
            ResourceClientResponseCompatibilityProfile::
                stock_protocol_48_build_10210_opcode5_single_entry) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] Opcode5ResourceResponseCarrierParseResult parse(
        const NetchanHeader& header,
        std::span<const std::byte> decoded_body,
        std::uint64_t reliable_generation) const;

private:
    ResourceClientResponseLimits limits_;
    ResourceClientResponseCompatibilityProfile profile_;
};

enum class PostResourceResponseBoundaryKind {
    opcode_at_payload_start,
    exact_end_of_payload,
};

enum class PostResourceResponseEvidenceProfile {
    first_complete_server_payload_no_opcode_scanning,
};

struct PostResourceResponseSourcePayloadMetadata {
    NetchanDirection direction{NetchanDirection::server_to_client};
    std::uint32_t source_sequence{0U};
    bool reliable{false};
    bool reassembled{false};
    bool decompressed{false};
    std::size_t decoded_payload_byte_count{0U};
};

class PostResourceResponseBoundary final {
public:
    [[nodiscard]] PostResourceResponseBoundaryKind kind() const noexcept;
    [[nodiscard]] const std::optional<std::uint8_t>& opcode() const noexcept;
    [[nodiscard]] std::size_t byte_offset() const noexcept;
    [[nodiscard]] std::size_t bit_offset() const noexcept;
    [[nodiscard]] std::size_t remaining_byte_count() const noexcept;
    [[nodiscard]] const PostResourceResponseSourcePayloadMetadata&
    source_payload() const noexcept;
    [[nodiscard]] PostResourceResponseEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class PostResourceResponseBoundaryParser;

    PostResourceResponseBoundary(
        PostResourceResponseBoundaryKind kind,
        std::optional<std::uint8_t> opcode,
        std::size_t remaining_byte_count,
        PostResourceResponseSourcePayloadMetadata source_payload) noexcept;

    PostResourceResponseBoundaryKind kind_{
        PostResourceResponseBoundaryKind::exact_end_of_payload};
    std::optional<std::uint8_t> opcode_;
    std::size_t remaining_byte_count_{0U};
    PostResourceResponseSourcePayloadMetadata source_payload_{};
};

enum class PostResourceResponseBoundaryErrorCode {
    invalid_configuration,
    invalid_source_metadata,
    payload_too_large,
};

struct PostResourceResponseBoundaryError {
    PostResourceResponseBoundaryErrorCode code{
        PostResourceResponseBoundaryErrorCode::invalid_source_metadata};
    std::size_t byte_offset{0U};
    std::string context;
};

struct PostResourceResponseBoundaryParseResult {
    std::optional<PostResourceResponseBoundary> boundary;
    std::optional<PostResourceResponseBoundaryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return boundary.has_value();
    }
};

class PostResourceResponseBoundaryParser final {
public:
    explicit PostResourceResponseBoundaryParser(
        ResourceClientResponseLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] PostResourceResponseBoundaryParseResult parse(
        std::span<const std::byte> complete_decoded_payload,
        PostResourceResponseSourcePayloadMetadata source_payload) const;

private:
    ResourceClientResponseLimits limits_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const Opcode5ResourceResponseErrorCode code) noexcept
{
    switch (code) {
    case Opcode5ResourceResponseErrorCode::invalid_configuration:
        return "invalid_configuration";
    case Opcode5ResourceResponseErrorCode::invalid_source_geometry:
        return "invalid_source_geometry";
    case Opcode5ResourceResponseErrorCode::response_too_large:
        return "response_too_large";
    case Opcode5ResourceResponseErrorCode::truncated_response:
        return "truncated_response";
    case Opcode5ResourceResponseErrorCode::unexpected_trailing_bytes:
        return "unexpected_trailing_bytes";
    case Opcode5ResourceResponseErrorCode::wrong_opcode: return "wrong_opcode";
    case Opcode5ResourceResponseErrorCode::unsupported_entry_count:
        return "unsupported_entry_count";
    case Opcode5ResourceResponseErrorCode::unterminated_wire_name:
        return "unterminated_wire_name";
    case Opcode5ResourceResponseErrorCode::unsupported_wire_name_size:
        return "unsupported_wire_name_size";
    case Opcode5ResourceResponseErrorCode::unsupported_field_type:
        return "unsupported_field_type";
    case Opcode5ResourceResponseErrorCode::unsupported_field_index:
        return "unsupported_field_index";
    case Opcode5ResourceResponseErrorCode::unsupported_field_flags:
        return "unsupported_field_flags";
    case Opcode5ResourceResponseErrorCode::opaque_size_mismatch:
        return "opaque_size_mismatch";
    case Opcode5ResourceResponseErrorCode::internal_encoding_error:
        return "internal_encoding_error";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const ResourceResponseCarrierErrorCode code) noexcept
{
    switch (code) {
    case ResourceResponseCarrierErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ResourceResponseCarrierErrorCode::invalid_reliable_generation:
        return "invalid_reliable_generation";
    case ResourceResponseCarrierErrorCode::carrier_too_large:
        return "carrier_too_large";
    case ResourceResponseCarrierErrorCode::transport_decode_failed:
        return "transport_decode_failed";
    case ResourceResponseCarrierErrorCode::reliable_fragment_required:
        return "reliable_fragment_required";
    case ResourceResponseCarrierErrorCode::unsupported_fragment_stream:
        return "unsupported_fragment_stream";
    case ResourceResponseCarrierErrorCode::unsupported_fragment_geometry:
        return "unsupported_fragment_geometry";
    case ResourceResponseCarrierErrorCode::semantic_range_mismatch:
        return "semantic_range_mismatch";
    case ResourceResponseCarrierErrorCode::concurrent_tail_too_large:
        return "concurrent_tail_too_large";
    case ResourceResponseCarrierErrorCode::unsupported_concurrent_tail_profile:
        return "unsupported_concurrent_tail_profile";
    case ResourceResponseCarrierErrorCode::semantic_response_invalid:
        return "semantic_response_invalid";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const PostResourceResponseBoundaryErrorCode code) noexcept
{
    switch (code) {
    case PostResourceResponseBoundaryErrorCode::invalid_configuration:
        return "invalid_configuration";
    case PostResourceResponseBoundaryErrorCode::invalid_source_metadata:
        return "invalid_source_metadata";
    case PostResourceResponseBoundaryErrorCode::payload_too_large:
        return "payload_too_large";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
