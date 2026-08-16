#pragma once

#include <hlclient/goldsrc/service_message_stream.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::uint8_t kDeltaDescriptionOpcode = 14U;
inline constexpr std::uint8_t kStockPostDeltaBoundaryOpcode = 44U;
inline constexpr std::uint32_t kDeltaSignedModifier = 0x80000000U;
inline constexpr std::uint32_t kDeltaMultiplierScale = 4'000U;

// Project safety limits, not stock engine maxima.
inline constexpr std::size_t kDefaultMaximumDeltaSchemaNameLength = 64U;
inline constexpr std::size_t kMaximumDeltaSchemaNameLength = 256U;
inline constexpr std::size_t kDefaultMaximumDeltaFieldNameLength = 64U;
inline constexpr std::size_t kMaximumDeltaFieldNameLength = 256U;
inline constexpr std::size_t kDefaultMaximumDeltaSchemaCount = 32U;
inline constexpr std::size_t kMaximumDeltaSchemaCount = 256U;
inline constexpr std::size_t kDefaultMaximumDeltaFieldsPerSchema = 256U;
inline constexpr std::size_t kMaximumDeltaFieldsPerSchema = 1'024U;
inline constexpr std::size_t kDefaultMaximumDeltaTotalFields = 2'048U;
inline constexpr std::size_t kMaximumDeltaTotalFields = 8'192U;
inline constexpr std::size_t kDefaultMaximumDeltaTotalNameBytes = 65'536U;
inline constexpr std::size_t kMaximumDeltaTotalNameBytes = 1'048'576U;
inline constexpr std::size_t kDefaultMaximumDeltaMessageBits = 1'048'576U;
inline constexpr std::size_t kMaximumDeltaMessageBits = 8'388'608U;
inline constexpr std::size_t kDefaultMaximumDeltaRegistryBytes = 262'144U;
inline constexpr std::size_t kMaximumDeltaRegistryBytes = 2'097'152U;

struct DeltaDescriptionLimits {
    std::size_t maximum_schema_name_length{
        kDefaultMaximumDeltaSchemaNameLength};
    std::size_t maximum_field_name_length{
        kDefaultMaximumDeltaFieldNameLength};
    std::size_t maximum_schema_count{kDefaultMaximumDeltaSchemaCount};
    std::size_t maximum_fields_per_schema{
        kDefaultMaximumDeltaFieldsPerSchema};
    std::size_t maximum_total_fields{kDefaultMaximumDeltaTotalFields};
    std::size_t maximum_total_name_bytes{
        kDefaultMaximumDeltaTotalNameBytes};
    std::size_t maximum_message_bits{kDefaultMaximumDeltaMessageBits};
    std::size_t maximum_registry_bytes{kDefaultMaximumDeltaRegistryBytes};
};

[[nodiscard]] bool valid_delta_description_limits(
    const DeltaDescriptionLimits& limits) noexcept;

enum class DeltaCompatibilityProfile {
    stock_protocol_48_build_10210,
};

enum class DeltaFieldBaseType : std::uint32_t {
    byte_value = 0x01U,
    short_value = 0x02U,
    float_value = 0x04U,
    integer_value = 0x08U,
    angle = 0x10U,
    time_window_8 = 0x20U,
    time_window_big = 0x40U,
    string = 0x80U,
};

class DeltaFieldTypeFlags final {
public:
    [[nodiscard]] DeltaFieldBaseType base_type() const noexcept;
    [[nodiscard]] bool signed_value() const noexcept;
    [[nodiscard]] std::uint32_t wire_value() const noexcept;

private:
    friend class DeltaDescriptionParser;

    DeltaFieldTypeFlags(
        DeltaFieldBaseType base_type,
        bool signed_value,
        std::uint32_t wire_value) noexcept;

    DeltaFieldBaseType base_type_{DeltaFieldBaseType::byte_value};
    bool signed_value_{false};
    std::uint32_t wire_value_{0U};
};

class DeltaFieldDefinition final {
public:
    DeltaFieldDefinition(const DeltaFieldDefinition&) = default;
    DeltaFieldDefinition& operator=(const DeltaFieldDefinition&) = delete;
    DeltaFieldDefinition(DeltaFieldDefinition&&) noexcept = default;
    DeltaFieldDefinition& operator=(DeltaFieldDefinition&&) noexcept = delete;
    ~DeltaFieldDefinition() = default;

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] const DeltaFieldTypeFlags& type_flags() const noexcept;
    [[nodiscard]] std::uint16_t offset() const noexcept;
    [[nodiscard]] std::uint8_t storage_size() const noexcept;
    [[nodiscard]] std::uint8_t significant_bits() const noexcept;
    [[nodiscard]] std::uint32_t premultiply_wire_value() const noexcept;
    [[nodiscard]] std::uint32_t postmultiply_wire_value() const noexcept;
    [[nodiscard]] double premultiply() const noexcept;
    [[nodiscard]] double postmultiply() const noexcept;
    [[nodiscard]] std::size_t wire_index() const noexcept;
    [[nodiscard]] std::uint8_t presence_mask() const noexcept;

private:
    friend class DeltaDescriptionParser;

    DeltaFieldDefinition(
        std::string name,
        DeltaFieldTypeFlags type_flags,
        std::uint16_t offset,
        std::uint8_t storage_size,
        std::uint8_t significant_bits,
        std::uint32_t premultiply_wire_value,
        std::uint32_t postmultiply_wire_value,
        std::size_t wire_index,
        std::uint8_t presence_mask) noexcept;

    std::string name_;
    DeltaFieldTypeFlags type_flags_;
    std::uint16_t offset_{0U};
    std::uint8_t storage_size_{0U};
    std::uint8_t significant_bits_{0U};
    std::uint32_t premultiply_wire_value_{0U};
    std::uint32_t postmultiply_wire_value_{0U};
    std::size_t wire_index_{0U};
    std::uint8_t presence_mask_{0U};
};

class DeltaSchema final {
public:
    DeltaSchema(const DeltaSchema&) = default;
    DeltaSchema& operator=(const DeltaSchema&) = delete;
    DeltaSchema(DeltaSchema&&) noexcept = default;
    DeltaSchema& operator=(DeltaSchema&&) noexcept = delete;
    ~DeltaSchema() = default;

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] const std::vector<DeltaFieldDefinition>& fields() const noexcept;
    [[nodiscard]] std::size_t field_count() const noexcept;
    [[nodiscard]] std::size_t source_message_offset() const noexcept;
    [[nodiscard]] std::size_t message_bits() const noexcept;
    [[nodiscard]] std::size_t message_bytes() const noexcept;
    [[nodiscard]] DeltaCompatibilityProfile profile() const noexcept;

private:
    friend class DeltaDescriptionParser;

    DeltaSchema(
        std::string name,
        std::vector<DeltaFieldDefinition> fields,
        std::size_t source_message_offset,
        std::size_t message_bits,
        std::size_t message_bytes,
        DeltaCompatibilityProfile profile) noexcept;

    std::string name_;
    std::vector<DeltaFieldDefinition> fields_;
    std::size_t source_message_offset_{0U};
    std::size_t message_bits_{0U};
    std::size_t message_bytes_{0U};
    DeltaCompatibilityProfile profile_{
        DeltaCompatibilityProfile::stock_protocol_48_build_10210};
};

enum class DeltaDescriptionErrorCode {
    invalid_configuration,
    invalid_input_geometry,
    wrong_opcode,
    message_too_large,
    truncated_schema_name,
    schema_name_too_long,
    invalid_schema_name,
    zero_field_count,
    field_count_limit_exceeded,
    truncated_field,
    invalid_presence_mask,
    invalid_type_flags,
    conflicting_base_type_flags,
    reserved_type_flag,
    missing_field_name,
    field_name_too_long,
    invalid_field_name,
    duplicate_field_name,
    invalid_storage_size,
    offset_size_overflow,
    invalid_significant_bits,
    invalid_multiplier,
    nonzero_padding,
    size_overflow,
};

struct DeltaDescriptionError {
    DeltaDescriptionErrorCode code{
        DeltaDescriptionErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::size_t bit_offset{0U};
    std::optional<std::size_t> field_index;
    std::string context;
};

struct DeltaDescriptionParseResult {
    std::optional<DeltaSchema> schema;
    std::optional<DeltaDescriptionError> error;
    std::size_t bits_consumed{0U};
    std::size_t bytes_consumed{0U};
    std::size_t next_byte_offset{0U};
    std::size_t next_bit_offset{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return schema.has_value();
    }
};

class DeltaDescriptionParser final {
public:
    explicit DeltaDescriptionParser(
        DeltaDescriptionLimits limits = {},
        DeltaCompatibilityProfile profile =
            DeltaCompatibilityProfile::stock_protocol_48_build_10210) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const DeltaDescriptionLimits& limits() const noexcept;
    [[nodiscard]] DeltaDescriptionParseResult parse(
        std::span<const std::byte> service_payload,
        std::size_t opcode_byte_offset,
        std::size_t service_payload_bit_length =
            static_cast<std::size_t>(-1)) const;

private:
    DeltaDescriptionLimits limits_;
    DeltaCompatibilityProfile profile_;
};

enum class DeltaRegistryErrorCode {
    invalid_configuration,
    duplicate_schema_name,
    duplicate_field_name,
    schema_count_limit_exceeded,
    total_field_limit_exceeded,
    total_name_byte_limit_exceeded,
    registry_byte_limit_exceeded,
    size_overflow,
};

struct DeltaRegistryError {
    DeltaRegistryErrorCode code{
        DeltaRegistryErrorCode::invalid_configuration};
    std::optional<std::size_t> schema_index;
    std::string context;
};

struct DeltaRegistryInsertResult {
    bool inserted{false};
    std::optional<DeltaRegistryError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return inserted;
    }
};

class DeltaSchemaRegistryState final {
public:
    DeltaSchemaRegistryState(const DeltaSchemaRegistryState&) = default;
    DeltaSchemaRegistryState& operator=(const DeltaSchemaRegistryState&) = delete;
    DeltaSchemaRegistryState(DeltaSchemaRegistryState&&) noexcept = default;
    DeltaSchemaRegistryState& operator=(DeltaSchemaRegistryState&&) noexcept = delete;
    ~DeltaSchemaRegistryState() = default;

    [[nodiscard]] const std::vector<DeltaSchema>& schemas() const noexcept;
    [[nodiscard]] const DeltaSchema* find_exact(std::string_view name) const noexcept;
    [[nodiscard]] std::size_t schema_count() const noexcept;
    [[nodiscard]] std::size_t total_field_count() const noexcept;
    [[nodiscard]] std::size_t total_name_bytes() const noexcept;
    [[nodiscard]] std::size_t accounted_registry_bytes() const noexcept;

private:
    friend class DeltaSchemaRegistryBuilder;

    DeltaSchemaRegistryState(
        std::vector<DeltaSchema> schemas,
        std::size_t total_field_count,
        std::size_t total_name_bytes,
        std::size_t accounted_registry_bytes) noexcept;

    std::vector<DeltaSchema> schemas_;
    std::size_t total_field_count_{0U};
    std::size_t total_name_bytes_{0U};
    std::size_t accounted_registry_bytes_{0U};
};

class DeltaSchemaRegistryBuilder final {
public:
    explicit DeltaSchemaRegistryBuilder(
        DeltaDescriptionLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] DeltaRegistryInsertResult insert(const DeltaSchema& schema);
    [[nodiscard]] DeltaSchemaRegistryState publish() && noexcept;
    [[nodiscard]] const std::vector<DeltaSchema>& candidate_schemas() const noexcept;

private:
    DeltaDescriptionLimits limits_;
    std::vector<DeltaSchema> schemas_;
    std::size_t total_field_count_{0U};
    std::size_t total_name_bytes_{0U};
    std::size_t accounted_registry_bytes_{0U};
};

enum class PostDeltaBoundaryCategory {
    neutral_message,
    stock_observed_opcode_44,
};

enum class PostDeltaBoundaryEvidenceStatus {
    stock_confirmed_opcode_44_body_unconsumed,
    synthetic_neutral_boundary,
};

class PostDeltaBoundary final {
public:
    [[nodiscard]] std::uint8_t opcode() const noexcept;
    [[nodiscard]] std::size_t byte_offset() const noexcept;
    [[nodiscard]] std::size_t bit_offset() const noexcept;
    [[nodiscard]] std::size_t remaining_byte_count() const noexcept;
    [[nodiscard]] PostDeltaBoundaryCategory category() const noexcept;
    [[nodiscard]] PostDeltaBoundaryEvidenceStatus evidence_status() const noexcept;

private:
    friend class DeltaDescriptionStreamDecoder;

    PostDeltaBoundary(
        std::uint8_t opcode,
        std::size_t byte_offset,
        std::size_t bit_offset,
        std::size_t remaining_byte_count,
        PostDeltaBoundaryCategory category,
        PostDeltaBoundaryEvidenceStatus evidence_status) noexcept;

    std::uint8_t opcode_{0U};
    std::size_t byte_offset_{0U};
    std::size_t bit_offset_{0U};
    std::size_t remaining_byte_count_{0U};
    PostDeltaBoundaryCategory category_{PostDeltaBoundaryCategory::neutral_message};
    PostDeltaBoundaryEvidenceStatus evidence_status_{
        PostDeltaBoundaryEvidenceStatus::synthetic_neutral_boundary};
};

enum class DeltaDescriptionStreamErrorCode {
    invalid_configuration,
    invalid_boundary_geometry,
    wrong_initial_opcode,
    parser_failure,
    registry_failure,
    message_count_limit_exceeded,
    missing_post_delta_boundary,
    malformed_post_delta_boundary,
    size_overflow,
};

struct DeltaDescriptionStreamError {
    DeltaDescriptionStreamErrorCode code{
        DeltaDescriptionStreamErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::size_t bit_offset{0U};
    std::optional<DeltaDescriptionErrorCode> parser_code;
    std::optional<DeltaRegistryErrorCode> registry_code;
    std::string context;
};

struct DeltaDescriptionStreamState {
    DeltaSchemaRegistryState registry;
    PostDeltaBoundary boundary;
    std::size_t delta_message_count{0U};
    std::size_t bits_consumed{0U};
    std::size_t bytes_consumed{0U};
};

struct DeltaDescriptionStreamDecodeResult {
    std::optional<DeltaDescriptionStreamState> state;
    std::optional<DeltaDescriptionStreamError> error;
    std::size_t required_event_count{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class DeltaDescriptionStreamDecoder final {
public:
    explicit DeltaDescriptionStreamDecoder(
        DeltaDescriptionLimits limits = {},
        DeltaCompatibilityProfile profile =
            DeltaCompatibilityProfile::stock_protocol_48_build_10210) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] DeltaDescriptionStreamDecodeResult decode(
        std::span<const std::byte> service_payload,
        const ResourcePhaseBoundary& initial_boundary) const;

private:
    DeltaDescriptionLimits limits_;
    DeltaCompatibilityProfile profile_;
};

[[nodiscard]] constexpr std::string_view to_string(
    DeltaFieldBaseType type) noexcept
{
    switch (type) {
    case DeltaFieldBaseType::byte_value: return "byte";
    case DeltaFieldBaseType::short_value: return "short";
    case DeltaFieldBaseType::float_value: return "float";
    case DeltaFieldBaseType::integer_value: return "integer";
    case DeltaFieldBaseType::angle: return "angle";
    case DeltaFieldBaseType::time_window_8: return "time_window_8";
    case DeltaFieldBaseType::time_window_big: return "time_window_big";
    case DeltaFieldBaseType::string: return "string";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
