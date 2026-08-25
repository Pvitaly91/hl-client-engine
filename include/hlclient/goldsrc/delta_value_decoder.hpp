#pragma once

#include <hlclient/goldsrc/delta_description.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hlclient::goldsrc {

// Project safety limits, not stock engine maxima.
inline constexpr std::size_t kDefaultMaximumDeltaValueFieldsPerObject = 256U;
inline constexpr std::size_t kMaximumDeltaValueFieldsPerObject = 1'024U;
inline constexpr std::size_t kDefaultMaximumDeltaValueMaskBytes = 32U;
inline constexpr std::size_t kMaximumDeltaValueMaskBytes = 128U;
inline constexpr std::size_t kDefaultMaximumDeltaValueStringBytes = 1'024U;
inline constexpr std::size_t kMaximumDeltaValueStringBytes = 65'535U;
inline constexpr std::size_t kDefaultMaximumDeltaValueTotalBytes = 65'536U;
inline constexpr std::size_t kMaximumDeltaValueTotalBytes = 1'048'576U;
inline constexpr std::size_t kDefaultMaximumDeltaObjectCountPerMessage = 2'048U;
inline constexpr std::size_t kMaximumDeltaObjectCountPerMessage = 8'192U;
inline constexpr std::size_t kDefaultMaximumDeltaValueBits = 1'048'576U;
inline constexpr std::size_t kMaximumDeltaValueBits = 8'388'608U;
inline constexpr double kDefaultMaximumDeltaNumericMagnitude = 1.0e9;
inline constexpr double kMaximumDeltaNumericMagnitude = 1.0e15;

struct GoldSrcDeltaValueLimits {
    std::size_t maximum_fields_per_object{
        kDefaultMaximumDeltaValueFieldsPerObject};
    std::size_t maximum_mask_bytes{kDefaultMaximumDeltaValueMaskBytes};
    std::size_t maximum_string_bytes{kDefaultMaximumDeltaValueStringBytes};
    std::size_t maximum_total_value_bytes{
        kDefaultMaximumDeltaValueTotalBytes};
    std::size_t maximum_object_count_per_message{
        kDefaultMaximumDeltaObjectCountPerMessage};
    std::size_t maximum_delta_bits{kDefaultMaximumDeltaValueBits};
    double maximum_numeric_magnitude{kDefaultMaximumDeltaNumericMagnitude};
};

[[nodiscard]] bool valid_goldsrc_delta_value_limits(
    const GoldSrcDeltaValueLimits& limits) noexcept;

// Stock runtime value grammar remains deliberately closed until isolated
// stock evidence confirms it. The synthetic profile is project-owned and may
// only be selected explicitly by tests and fake peers.
enum class DeltaValueCompatibilityProfile {
    stock_protocol_48_build_10210_evidence_pending,
    synthetic_neutral_v1,
};

using DeltaScalarValue =
    std::variant<std::uint32_t, std::int32_t, double, std::string>;

class DeltaFieldValue final {
public:
    DeltaFieldValue(const DeltaFieldValue&) = default;
    DeltaFieldValue& operator=(const DeltaFieldValue&) = delete;
    DeltaFieldValue(DeltaFieldValue&&) noexcept = default;
    DeltaFieldValue& operator=(DeltaFieldValue&&) noexcept = delete;
    ~DeltaFieldValue() = default;

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] DeltaFieldBaseType base_type() const noexcept;
    [[nodiscard]] std::size_t wire_index() const noexcept;
    [[nodiscard]] const DeltaScalarValue& value() const noexcept;

private:
    friend class DeltaObjectBuilder;
    friend class DeltaObjectState;
    friend class GoldSrcDeltaValueDecoder;

    DeltaFieldValue(
        DeltaFieldDefinition definition,
        DeltaScalarValue value) noexcept;

    DeltaFieldDefinition definition_;
    DeltaScalarValue value_{std::uint32_t{0U}};
};

class DeltaObjectState final {
public:
    DeltaObjectState(const DeltaObjectState&) = default;
    DeltaObjectState& operator=(const DeltaObjectState&) = delete;
    DeltaObjectState(DeltaObjectState&&) noexcept = default;
    DeltaObjectState& operator=(DeltaObjectState&&) noexcept = delete;
    ~DeltaObjectState() = default;

    [[nodiscard]] std::string_view schema_name() const noexcept;
    [[nodiscard]] DeltaCompatibilityProfile schema_profile() const noexcept;
    [[nodiscard]] DeltaValueCompatibilityProfile decode_profile() const noexcept;
    [[nodiscard]] const std::vector<DeltaFieldValue>& fields() const noexcept;
    [[nodiscard]] std::size_t field_count() const noexcept;
    [[nodiscard]] const DeltaFieldValue* find_exact(
        std::string_view field_name) const noexcept;
    [[nodiscard]] std::size_t accounted_value_bytes() const noexcept;
    [[nodiscard]] bool matches_schema(
        const DeltaSchema& schema) const noexcept;
    [[nodiscard]] bool has_same_schema_as(
        const DeltaObjectState& other) const noexcept;

private:
    friend class DeltaObjectBuilder;
    friend class GoldSrcDeltaValueDecoder;

    DeltaObjectState(
        std::string schema_name,
        DeltaCompatibilityProfile schema_profile,
        DeltaValueCompatibilityProfile decode_profile,
        std::vector<DeltaFieldValue> fields,
        std::size_t accounted_value_bytes) noexcept;

    std::string schema_name_;
    DeltaCompatibilityProfile schema_profile_{
        DeltaCompatibilityProfile::stock_protocol_48_build_10210};
    DeltaValueCompatibilityProfile decode_profile_{
        DeltaValueCompatibilityProfile::stock_protocol_48_build_10210_evidence_pending};
    std::vector<DeltaFieldValue> fields_;
    std::size_t accounted_value_bytes_{0U};
};

// This preserves a typed boundary without assigning an unconfirmed unit or
// scale. It is intentionally unused by the evidence-pending time codecs.
struct DeltaTimeReference {
    std::int64_t raw_value{0};
};

struct DeltaValueDecodeContext {
    std::span<const std::byte> bytes;
    std::size_t start_bit_offset{0U};
    std::size_t bit_length{static_cast<std::size_t>(-1)};
    std::optional<DeltaTimeReference> time_reference;
};

enum class DeltaValueErrorCode {
    invalid_configuration,
    evidence_pending,
    invalid_input_geometry,
    schema_not_found,
    invalid_schema,
    field_limit_exceeded,
    invalid_base,
    missing_required_base,
    truncated_mask,
    mask_byte_limit_exceeded,
    mask_length_exceeds_schema,
    mask_bit_out_of_range,
    truncated_value,
    invalid_significant_bits,
    invalid_multiplier,
    numeric_overflow,
    non_finite_result,
    numeric_magnitude_exceeded,
    string_limit_exceeded,
    total_value_bytes_exceeded,
    nonzero_padding,
    unexpected_trailing_bits,
    value_type_mismatch,
};

struct DeltaValueError {
    DeltaValueErrorCode code{DeltaValueErrorCode::invalid_configuration};
    std::size_t bit_offset{0U};
    std::optional<std::size_t> field_index;
    std::string context;
};

struct DeltaValueDecodeResult {
    std::optional<DeltaObjectState> state;
    std::optional<DeltaValueError> error;
    std::size_t bits_consumed{0U};
    std::size_t next_bit_offset{0U};
    std::size_t next_byte_offset{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

struct DeltaObjectBuildResult {
    std::optional<DeltaObjectState> state;
    std::optional<DeltaValueError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

// Publishes only fully specified states; it never invents no-base defaults.
class DeltaObjectBuilder final {
public:
    explicit DeltaObjectBuilder(
        GoldSrcDeltaValueLimits limits = {},
        DeltaValueCompatibilityProfile profile =
            DeltaValueCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const GoldSrcDeltaValueLimits& limits() const noexcept;
    [[nodiscard]] DeltaValueCompatibilityProfile profile() const noexcept;
    [[nodiscard]] DeltaObjectBuildResult build(
        const DeltaSchema& schema,
        std::span<const DeltaScalarValue> values) const;

private:
    GoldSrcDeltaValueLimits limits_;
    DeltaValueCompatibilityProfile profile_{
        DeltaValueCompatibilityProfile::stock_protocol_48_build_10210_evidence_pending};
};

class GoldSrcDeltaValueDecoder final {
public:
    explicit GoldSrcDeltaValueDecoder(
        GoldSrcDeltaValueLimits limits = {},
        DeltaValueCompatibilityProfile profile =
            DeltaValueCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const GoldSrcDeltaValueLimits& limits() const noexcept;
    [[nodiscard]] DeltaValueCompatibilityProfile profile() const noexcept;

    [[nodiscard]] DeltaValueDecodeResult decode_delta(
        const DeltaSchema& schema,
        const DeltaObjectState* base,
        const DeltaValueDecodeContext& context) const;
    [[nodiscard]] DeltaValueDecodeResult decode_delta(
        const DeltaSchemaRegistryState& registry,
        std::string_view schema_name,
        const DeltaObjectState* base,
        const DeltaValueDecodeContext& context) const;

private:
    GoldSrcDeltaValueLimits limits_;
    DeltaValueCompatibilityProfile profile_{
        DeltaValueCompatibilityProfile::stock_protocol_48_build_10210_evidence_pending};
};

[[nodiscard]] constexpr std::string_view to_string(
    const DeltaValueCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case DeltaValueCompatibilityProfile::
        stock_protocol_48_build_10210_evidence_pending:
        return "stock_protocol_48_build_10210_evidence_pending";
    case DeltaValueCompatibilityProfile::synthetic_neutral_v1:
        return "synthetic_neutral_v1";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const DeltaValueErrorCode code) noexcept
{
    switch (code) {
    case DeltaValueErrorCode::invalid_configuration: return "invalid_configuration";
    case DeltaValueErrorCode::evidence_pending: return "evidence_pending";
    case DeltaValueErrorCode::invalid_input_geometry: return "invalid_input_geometry";
    case DeltaValueErrorCode::schema_not_found: return "schema_not_found";
    case DeltaValueErrorCode::invalid_schema: return "invalid_schema";
    case DeltaValueErrorCode::field_limit_exceeded: return "field_limit_exceeded";
    case DeltaValueErrorCode::invalid_base: return "invalid_base";
    case DeltaValueErrorCode::missing_required_base: return "missing_required_base";
    case DeltaValueErrorCode::truncated_mask: return "truncated_mask";
    case DeltaValueErrorCode::mask_byte_limit_exceeded: return "mask_byte_limit_exceeded";
    case DeltaValueErrorCode::mask_length_exceeds_schema: return "mask_length_exceeds_schema";
    case DeltaValueErrorCode::mask_bit_out_of_range: return "mask_bit_out_of_range";
    case DeltaValueErrorCode::truncated_value: return "truncated_value";
    case DeltaValueErrorCode::invalid_significant_bits: return "invalid_significant_bits";
    case DeltaValueErrorCode::invalid_multiplier: return "invalid_multiplier";
    case DeltaValueErrorCode::numeric_overflow: return "numeric_overflow";
    case DeltaValueErrorCode::non_finite_result: return "non_finite_result";
    case DeltaValueErrorCode::numeric_magnitude_exceeded: return "numeric_magnitude_exceeded";
    case DeltaValueErrorCode::string_limit_exceeded: return "string_limit_exceeded";
    case DeltaValueErrorCode::total_value_bytes_exceeded: return "total_value_bytes_exceeded";
    case DeltaValueErrorCode::nonzero_padding: return "nonzero_padding";
    case DeltaValueErrorCode::unexpected_trailing_bits: return "unexpected_trailing_bits";
    case DeltaValueErrorCode::value_type_mismatch: return "value_type_mismatch";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
