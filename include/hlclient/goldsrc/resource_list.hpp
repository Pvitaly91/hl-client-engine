#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::uint8_t kResourceListOpcode = 43U;
inline constexpr std::size_t kResourceListCountBitWidth = 12U;
inline constexpr std::size_t kResourceTypeBitWidth = 4U;
inline constexpr std::size_t kResourceIndexBitWidth = 12U;
inline constexpr std::size_t kResourceDeclaredSizeBitWidth = 24U;
inline constexpr std::size_t kResourceFlagsBitWidth = 4U;
inline constexpr std::uint32_t kMaximumResourceIndexWireValue = 0x0fffU;
inline constexpr std::uint32_t kMaximumResourceDeclaredSizeWireValue =
    0x00ff'ffffU;
inline constexpr std::uint8_t kMaximumSupportedStandardResourceFlagsMask =
    0x01U;

// Project safety limits, not claims about stock engine maxima.
inline constexpr std::size_t kDefaultMaximumResourceMessageBits = 1'048'576U;
inline constexpr std::size_t kMaximumResourceMessageBits = 8'388'608U;
inline constexpr std::size_t kDefaultMaximumResourceMessageBytes = 131'072U;
inline constexpr std::size_t kMaximumResourceMessageBytes = 1'048'576U;
inline constexpr std::size_t kDefaultMaximumResourceCount = 1'024U;
inline constexpr std::size_t kMaximumResourceCount = 4'095U;
inline constexpr std::size_t kDefaultMaximumResourceNameLength = 255U;
inline constexpr std::size_t kMaximumResourceNameLength = 4'096U;
inline constexpr std::size_t kDefaultMaximumResourceTotalNameBytes = 65'536U;
inline constexpr std::size_t kMaximumResourceTotalNameBytes = 1'048'576U;
inline constexpr std::uint32_t kDefaultMaximumResourceDeclaredSize =
    kMaximumResourceDeclaredSizeWireValue;
inline constexpr std::uint32_t kMaximumResourceDeclaredSize =
    kMaximumResourceDeclaredSizeWireValue;
inline constexpr std::uint64_t kDefaultMaximumResourceTotalDeclaredSize =
    8ULL * 1'024ULL * 1'024ULL * 1'024ULL;
inline constexpr std::uint64_t kMaximumResourceTotalDeclaredSize =
    64ULL * 1'024ULL * 1'024ULL * 1'024ULL;
inline constexpr std::uint8_t kDefaultMaximumResourceFlags =
    kMaximumSupportedStandardResourceFlagsMask;
inline constexpr std::uint8_t kMaximumResourceFlags = 0x0fU;
inline constexpr std::size_t kDefaultMaximumResourceListEvents = 2'048U;
inline constexpr std::size_t kMaximumResourceListEvents = 8'192U;
inline constexpr std::size_t kResourceListDiagnosticTextLimit = 256U;

struct ResourceListLimits {
    std::size_t maximum_resource_message_bits{
        kDefaultMaximumResourceMessageBits};
    std::size_t maximum_resource_message_bytes{
        kDefaultMaximumResourceMessageBytes};
    std::size_t maximum_resource_count{kDefaultMaximumResourceCount};
    std::size_t maximum_resource_name_length{
        kDefaultMaximumResourceNameLength};
    std::size_t maximum_resource_total_name_bytes{
        kDefaultMaximumResourceTotalNameBytes};
    // These two bounds constrain the opaque 24-bit size-code field and its
    // checked raw-code sum. They do not establish file-size semantics.
    std::uint32_t maximum_resource_declared_size{
        kDefaultMaximumResourceDeclaredSize};
    std::uint64_t maximum_resource_total_declared_size{
        kDefaultMaximumResourceTotalDeclaredSize};
    std::uint8_t maximum_resource_flags{kDefaultMaximumResourceFlags};
};

[[nodiscard]] bool valid_resource_list_limits(
    const ResourceListLimits& limits) noexcept;

enum class ResourceListCompatibilityProfile {
    stock_protocol_48_build_10210_standard,
};

enum class ResourceListEvidenceProfile {
    repeated_signed_stock_standard_resource_lists,
};

enum class ResourceType : std::uint8_t {
    sound = 0U,
    model = 2U,
    decal = 3U,
    generic = 4U,
    event_script = 5U,
};

class ResourceName final {
public:
    ResourceName(const ResourceName&) = default;
    ResourceName& operator=(const ResourceName&) = delete;
    ResourceName(ResourceName&&) noexcept = default;
    ResourceName& operator=(ResourceName&&) noexcept = delete;
    ~ResourceName() = default;

    [[nodiscard]] std::string_view bytes() const noexcept;
    [[nodiscard]] std::size_t byte_length() const noexcept;

private:
    friend class ResourceListParser;

    explicit ResourceName(std::string bytes) noexcept;

    std::string bytes_;
};

class ResourceIndex final {
public:
    [[nodiscard]] std::uint16_t value() const noexcept;

private:
    friend class ResourceListParser;

    explicit ResourceIndex(std::uint16_t value) noexcept;

    std::uint16_t value_{0U};
};

class ResourceByteSize final {
public:
    // This is the exact unsigned 24-bit wire code. In particular, 0xffffff
    // remains opaque metadata and is not exposed as a trusted byte count or
    // interpreted sentinel.
    [[nodiscard]] std::uint32_t raw_code() const noexcept;

private:
    friend class ResourceListParser;

    explicit ResourceByteSize(std::uint32_t value) noexcept;

    std::uint32_t value_{0U};
};

class ResourceFlags final {
public:
    [[nodiscard]] std::uint8_t wire_value() const noexcept;

private:
    friend class ResourceListParser;

    explicit ResourceFlags(std::uint8_t wire_value) noexcept;

    std::uint8_t wire_value_{0U};
};

class ResourceEntry final {
public:
    ResourceEntry(const ResourceEntry&) = default;
    ResourceEntry& operator=(const ResourceEntry&) = delete;
    ResourceEntry(ResourceEntry&&) noexcept = default;
    ResourceEntry& operator=(ResourceEntry&&) noexcept = delete;
    ~ResourceEntry() = default;

    [[nodiscard]] ResourceType type() const noexcept;
    [[nodiscard]] const ResourceName& name() const noexcept;
    [[nodiscard]] const ResourceIndex& index() const noexcept;
    [[nodiscard]] const ResourceByteSize& declared_size() const noexcept;
    [[nodiscard]] const ResourceFlags& flags() const noexcept;
    [[nodiscard]] std::size_t wire_ordinal() const noexcept;
    [[nodiscard]] std::size_t source_start_bit_offset() const noexcept;
    [[nodiscard]] std::size_t source_end_bit_offset() const noexcept;

private:
    friend class ResourceListParser;

    ResourceEntry(
        ResourceType type,
        ResourceName name,
        ResourceIndex index,
        ResourceByteSize declared_size,
        ResourceFlags flags,
        std::size_t wire_ordinal,
        std::size_t source_start_bit_offset,
        std::size_t source_end_bit_offset) noexcept;

    ResourceType type_{ResourceType::sound};
    ResourceName name_;
    ResourceIndex index_;
    ResourceByteSize declared_size_;
    ResourceFlags flags_;
    std::size_t wire_ordinal_{0U};
    std::size_t source_start_bit_offset_{0U};
    std::size_t source_end_bit_offset_{0U};
};

class ResourceTypeCount final {
public:
    ResourceTypeCount(const ResourceTypeCount&) = default;
    ResourceTypeCount& operator=(const ResourceTypeCount&) = delete;
    ResourceTypeCount(ResourceTypeCount&&) noexcept = default;
    ResourceTypeCount& operator=(ResourceTypeCount&&) noexcept = delete;
    ~ResourceTypeCount() = default;

    [[nodiscard]] ResourceType type() const noexcept;
    [[nodiscard]] std::size_t count() const noexcept;

private:
    friend class ResourceTypeSummary;

    ResourceTypeCount(ResourceType type, std::size_t count) noexcept;

    ResourceType type_{ResourceType::sound};
    std::size_t count_{0U};
};

class ResourceTypeSummary final {
public:
    ResourceTypeSummary(const ResourceTypeSummary&) = default;
    ResourceTypeSummary& operator=(const ResourceTypeSummary&) = delete;
    ResourceTypeSummary(ResourceTypeSummary&&) noexcept = default;
    ResourceTypeSummary& operator=(ResourceTypeSummary&&) noexcept = delete;
    ~ResourceTypeSummary() = default;

    // Counts are always exposed in confirmed numeric wire-type order:
    // sound(0), model(2), decal(3), generic(4), event_script(5).
    [[nodiscard]] std::span<const ResourceTypeCount> ordered_counts()
        const noexcept;
    [[nodiscard]] std::size_t count(ResourceType type) const noexcept;

private:
    friend class ResourceListParser;

    explicit ResourceTypeSummary(
        const std::array<std::size_t, 5U>& counts) noexcept;

    std::array<ResourceTypeCount, 5U> ordered_counts_;
};

class ResourceListState final {
public:
    ResourceListState(const ResourceListState&) = default;
    ResourceListState& operator=(const ResourceListState&) = delete;
    ResourceListState(ResourceListState&&) noexcept = default;
    ResourceListState& operator=(ResourceListState&&) noexcept = delete;
    ~ResourceListState() = default;

    [[nodiscard]] const std::vector<ResourceEntry>& entries() const noexcept;
    [[nodiscard]] std::size_t resource_count() const noexcept;
    [[nodiscard]] const ResourceTypeSummary& type_summary() const noexcept;
    // Checked arithmetic over opaque 24-bit wire codes. This value is not a
    // file-size claim and must not be used as an allocation request.
    [[nodiscard]] std::uint64_t total_size_code_sum() const noexcept;
    [[nodiscard]] std::size_t total_name_byte_count() const noexcept;
    [[nodiscard]] const ResourceEntry* find_exact(
        ResourceType type,
        std::uint16_t index) const noexcept;
    [[nodiscard]] std::size_t source_opcode_byte_offset() const noexcept;
    [[nodiscard]] std::size_t source_payload_bit_length() const noexcept;
    [[nodiscard]] std::size_t bits_consumed() const noexcept;
    [[nodiscard]] std::size_t bytes_consumed() const noexcept;
    [[nodiscard]] std::size_t next_byte_offset() const noexcept;
    [[nodiscard]] std::size_t next_bit_offset() const noexcept;
    [[nodiscard]] ResourceListCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] ResourceListEvidenceProfile evidence_profile() const noexcept;

private:
    friend class ResourceListParser;

    ResourceListState(
        std::vector<ResourceEntry> entries,
        ResourceTypeSummary type_summary,
        std::uint64_t total_size_code_sum,
        std::size_t total_name_byte_count,
        std::size_t source_opcode_byte_offset,
        std::size_t source_payload_bit_length,
        std::size_t bits_consumed,
        std::size_t bytes_consumed,
        std::size_t next_byte_offset,
        std::size_t next_bit_offset,
        ResourceListCompatibilityProfile profile) noexcept;

    std::vector<ResourceEntry> entries_;
    ResourceTypeSummary type_summary_;
    std::uint64_t total_size_code_sum_{0U};
    std::size_t total_name_byte_count_{0U};
    std::size_t source_opcode_byte_offset_{0U};
    std::size_t source_payload_bit_length_{0U};
    std::size_t bits_consumed_{0U};
    std::size_t bytes_consumed_{0U};
    std::size_t next_byte_offset_{0U};
    std::size_t next_bit_offset_{0U};
    ResourceListCompatibilityProfile profile_{
        ResourceListCompatibilityProfile::
            stock_protocol_48_build_10210_standard};
};

enum class ResourceListErrorCode {
    invalid_configuration,
    invalid_input_geometry,
    wrong_opcode,
    message_too_large,
    truncated_count,
    zero_resource_count,
    resource_count_limit_exceeded,
    truncated_entry,
    unsupported_resource_type,
    unterminated_resource_name,
    resource_name_too_long,
    total_name_bytes_limit_exceeded,
    duplicate_resource_identity,
    resource_declared_size_limit_exceeded,
    total_declared_size_limit_exceeded,
    unsupported_resource_flags,
    unsupported_resource_profile,
    truncated_padding,
    nonzero_padding,
    unexpected_trailing_data,
    size_overflow,
};

struct ResourceListError {
    ResourceListErrorCode code{ResourceListErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::size_t bit_offset{0U};
    std::optional<std::size_t> entry_index;
    std::string context;
};

struct ResourceListParseResult {
    std::optional<ResourceListState> state;
    std::optional<ResourceListError> error;
    std::size_t bits_consumed{0U};
    std::size_t bytes_consumed{0U};
    std::size_t next_byte_offset{0U};
    std::size_t next_bit_offset{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class ResourceListParser final {
public:
    explicit ResourceListParser(
        ResourceListLimits limits = {},
        ResourceListCompatibilityProfile profile =
            ResourceListCompatibilityProfile::
                stock_protocol_48_build_10210_standard) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const ResourceListLimits& limits() const noexcept;
    [[nodiscard]] ResourceListParseResult parse(
        std::span<const std::byte> service_payload,
        std::size_t opcode_byte_offset,
        std::size_t service_payload_bit_length =
            static_cast<std::size_t>(-1)) const;

private:
    ResourceListLimits limits_;
    ResourceListCompatibilityProfile profile_;
};

enum class PostResourceListBoundaryKind {
    exact_end_of_payload,
};

enum class PostResourceListEvidenceStatus {
    repeated_stock_exact_end_of_payload,
};

class PostResourceListBoundary final {
public:
    [[nodiscard]] PostResourceListBoundaryKind kind() const noexcept;
    [[nodiscard]] std::size_t byte_offset() const noexcept;
    [[nodiscard]] std::size_t bit_offset() const noexcept;
    [[nodiscard]] std::size_t remaining_byte_count() const noexcept;
    [[nodiscard]] std::size_t source_payload_bit_length() const noexcept;
    [[nodiscard]] std::size_t source_opcode_byte_offset() const noexcept;
    [[nodiscard]] PostResourceListEvidenceStatus evidence_status() const noexcept;

private:
    friend class PostResourceListStreamDecoder;

    PostResourceListBoundary(
        std::size_t byte_offset,
        std::size_t bit_offset,
        std::size_t source_payload_bit_length,
        std::size_t source_opcode_byte_offset) noexcept;

    std::size_t byte_offset_{0U};
    std::size_t bit_offset_{0U};
    std::size_t source_payload_bit_length_{0U};
    std::size_t source_opcode_byte_offset_{0U};
};

enum class ResourceClientResponseActionKind {
    stock_response_required_semantics_pending,
};

enum class ResourceClientResponseEvidenceStatus {
    stock_fragmented_reliable_opcode_five_semantics_pending,
};

inline constexpr std::uint8_t kResourceClientResponseOpcodeCandidate = 5U;

class ResourceClientResponseBoundary final {
public:
    [[nodiscard]] ResourceClientResponseActionKind action_kind() const noexcept;
    [[nodiscard]] std::uint8_t opcode_candidate() const noexcept;
    [[nodiscard]] std::size_t trigger_byte_offset() const noexcept;
    [[nodiscard]] std::size_t trigger_bit_offset() const noexcept;
    [[nodiscard]] ResourceClientResponseEvidenceStatus evidence_status()
        const noexcept;
    [[nodiscard]] constexpr bool response_builder_available() const noexcept
    {
        return false;
    }
    [[nodiscard]] constexpr bool response_queued() const noexcept
    {
        return false;
    }

private:
    friend class PostResourceListStreamDecoder;

    ResourceClientResponseBoundary(
        std::size_t trigger_byte_offset,
        std::size_t trigger_bit_offset) noexcept;

    std::size_t trigger_byte_offset_{0U};
    std::size_t trigger_bit_offset_{0U};
};

class PostResourceListStreamState final {
public:
    PostResourceListStreamState(const PostResourceListStreamState&) = default;
    PostResourceListStreamState& operator=(
        const PostResourceListStreamState&) = delete;
    PostResourceListStreamState(PostResourceListStreamState&&) noexcept = default;
    PostResourceListStreamState& operator=(
        PostResourceListStreamState&&) noexcept = delete;
    ~PostResourceListStreamState() = default;

    [[nodiscard]] const PostResourceListBoundary& boundary() const noexcept;
    [[nodiscard]] const ResourceClientResponseBoundary& client_response()
        const noexcept;

private:
    friend class PostResourceListStreamDecoder;

    PostResourceListStreamState(
        PostResourceListBoundary boundary,
        ResourceClientResponseBoundary client_response) noexcept;

    PostResourceListBoundary boundary_;
    ResourceClientResponseBoundary client_response_;
};

enum class PostResourceListStreamErrorCode {
    invalid_input_geometry,
    incompatible_resource_list_state,
    list_not_at_end_of_payload,
    size_overflow,
};

struct PostResourceListStreamError {
    PostResourceListStreamErrorCode code{
        PostResourceListStreamErrorCode::invalid_input_geometry};
    std::size_t byte_offset{0U};
    std::size_t bit_offset{0U};
    std::string context;
};

struct PostResourceListStreamDecodeResult {
    std::optional<PostResourceListStreamState> state;
    std::optional<PostResourceListStreamError> error;
    std::size_t required_event_count{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class PostResourceListStreamDecoder final {
public:
    [[nodiscard]] PostResourceListStreamDecodeResult decode(
        std::span<const std::byte> service_payload,
        const ResourceListState& resource_list,
        std::size_t service_payload_bit_length =
            static_cast<std::size_t>(-1)) const;
};

[[nodiscard]] constexpr std::string_view to_string(
    const ResourceType type) noexcept
{
    switch (type) {
    case ResourceType::sound: return "sound";
    case ResourceType::model: return "model";
    case ResourceType::decal: return "decal";
    case ResourceType::generic: return "generic";
    case ResourceType::event_script: return "event_script";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const ResourceListErrorCode code) noexcept
{
    switch (code) {
    case ResourceListErrorCode::invalid_configuration: return "invalid_configuration";
    case ResourceListErrorCode::invalid_input_geometry: return "invalid_input_geometry";
    case ResourceListErrorCode::wrong_opcode: return "wrong_opcode";
    case ResourceListErrorCode::message_too_large: return "message_too_large";
    case ResourceListErrorCode::truncated_count: return "truncated_count";
    case ResourceListErrorCode::zero_resource_count: return "zero_resource_count";
    case ResourceListErrorCode::resource_count_limit_exceeded:
        return "resource_count_limit_exceeded";
    case ResourceListErrorCode::truncated_entry: return "truncated_entry";
    case ResourceListErrorCode::unsupported_resource_type:
        return "unsupported_resource_type";
    case ResourceListErrorCode::unterminated_resource_name:
        return "unterminated_resource_name";
    case ResourceListErrorCode::resource_name_too_long:
        return "resource_name_too_long";
    case ResourceListErrorCode::total_name_bytes_limit_exceeded:
        return "total_name_bytes_limit_exceeded";
    case ResourceListErrorCode::duplicate_resource_identity:
        return "duplicate_resource_identity";
    case ResourceListErrorCode::resource_declared_size_limit_exceeded:
        return "resource_declared_size_limit_exceeded";
    case ResourceListErrorCode::total_declared_size_limit_exceeded:
        return "total_declared_size_limit_exceeded";
    case ResourceListErrorCode::unsupported_resource_flags:
        return "unsupported_resource_flags";
    case ResourceListErrorCode::unsupported_resource_profile:
        return "unsupported_resource_profile";
    case ResourceListErrorCode::truncated_padding: return "truncated_padding";
    case ResourceListErrorCode::nonzero_padding: return "nonzero_padding";
    case ResourceListErrorCode::unexpected_trailing_data:
        return "unexpected_trailing_data";
    case ResourceListErrorCode::size_overflow: return "size_overflow";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
