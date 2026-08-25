#include <hlclient/goldsrc/delta_value_decoder.hpp>

#include <hlclient/goldsrc/bit_reader.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    output = left + right;
    return true;
}

[[nodiscard]] bool valid_profile(
    const DeltaValueCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case DeltaValueCompatibilityProfile::
        stock_protocol_48_build_10210_evidence_pending:
    case DeltaValueCompatibilityProfile::synthetic_neutral_v1:
        return true;
    }
    return false;
}

[[nodiscard]] DeltaValueDecodeResult decode_failure(
    const DeltaValueErrorCode code,
    const std::size_t initial_bit_offset,
    const std::size_t diagnostic_bit_offset,
    const std::optional<std::size_t> field_index,
    std::string context)
{
    return DeltaValueDecodeResult{
        std::nullopt,
        DeltaValueError{
            code,
            diagnostic_bit_offset,
            field_index,
            std::move(context),
        },
        0U,
        initial_bit_offset,
        initial_bit_offset / 8U,
    };
}

[[nodiscard]] DeltaObjectBuildResult build_failure(
    const DeltaValueErrorCode code,
    const std::optional<std::size_t> field_index,
    std::string context)
{
    return DeltaObjectBuildResult{
        std::nullopt,
        DeltaValueError{code, 0U, field_index, std::move(context)},
    };
}

[[nodiscard]] bool valid_significant_bits(
    const DeltaFieldDefinition& field) noexcept
{
    const auto width = field.significant_bits();
    if (width == 0U) {
        return false;
    }
    switch (field.type_flags().base_type()) {
    case DeltaFieldBaseType::byte_value:
        return width <= 8U;
    case DeltaFieldBaseType::short_value:
        return width <= 16U;
    case DeltaFieldBaseType::string:
        return width == 1U;
    case DeltaFieldBaseType::float_value:
    case DeltaFieldBaseType::integer_value:
    case DeltaFieldBaseType::angle:
    case DeltaFieldBaseType::time_window_8:
    case DeltaFieldBaseType::time_window_big:
        return width <= 32U;
    }
    return false;
}

[[nodiscard]] std::optional<DeltaValueError> validate_schema(
    const DeltaSchema& schema,
    const GoldSrcDeltaValueLimits& limits)
{
    if (schema.name().empty() || schema.field_count() == 0U) {
        return DeltaValueError{
            DeltaValueErrorCode::invalid_schema,
            0U,
            std::nullopt,
            "Runtime delta schema must have an exact name and fields",
        };
    }
    if (schema.field_count() > limits.maximum_fields_per_object) {
        return DeltaValueError{
            DeltaValueErrorCode::field_limit_exceeded,
            0U,
            std::nullopt,
            "Runtime delta schema exceeds the configured field bound",
        };
    }
    for (std::size_t index = 0U; index < schema.field_count(); ++index) {
        const auto& field = schema.fields()[index];
        if (field.name().empty() || field.wire_index() != index) {
            return DeltaValueError{
                DeltaValueErrorCode::invalid_schema,
                0U,
                index,
                "Runtime delta schema field order is not its wire order",
            };
        }
        if (!valid_significant_bits(field)) {
            return DeltaValueError{
                DeltaValueErrorCode::invalid_significant_bits,
                0U,
                index,
                "Runtime delta field has an invalid significant-bit width",
            };
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::size_t value_byte_count(
    const DeltaScalarValue& value) noexcept
{
    if (std::holds_alternative<std::uint32_t>(value) ||
        std::holds_alternative<std::int32_t>(value)) {
        return sizeof(std::uint32_t);
    }
    if (std::holds_alternative<double>(value)) {
        return sizeof(double);
    }
    return std::get<std::string>(value).size();
}

[[nodiscard]] bool magnitude_within_limit(
    const DeltaScalarValue& value,
    const double maximum) noexcept
{
    if (const auto* unsigned_value = std::get_if<std::uint32_t>(&value)) {
        return static_cast<long double>(*unsigned_value) <=
               static_cast<long double>(maximum);
    }
    if (const auto* signed_value = std::get_if<std::int32_t>(&value)) {
        const auto widened = static_cast<std::int64_t>(*signed_value);
        const auto magnitude = widened < 0 ? -widened : widened;
        return static_cast<long double>(magnitude) <=
               static_cast<long double>(maximum);
    }
    if (const auto* floating_value = std::get_if<double>(&value)) {
        return std::isfinite(*floating_value) &&
               std::abs(*floating_value) <= maximum;
    }
    return true;
}

[[nodiscard]] bool value_type_matches(
    const DeltaFieldDefinition& field,
    const DeltaScalarValue& value) noexcept
{
    switch (field.type_flags().base_type()) {
    case DeltaFieldBaseType::byte_value:
    case DeltaFieldBaseType::short_value:
    case DeltaFieldBaseType::integer_value:
        return field.type_flags().signed_value()
                   ? std::holds_alternative<std::int32_t>(value)
                   : std::holds_alternative<std::uint32_t>(value);
    case DeltaFieldBaseType::float_value:
    case DeltaFieldBaseType::angle:
        return std::holds_alternative<double>(value);
    case DeltaFieldBaseType::string:
        return std::holds_alternative<std::string>(value);
    case DeltaFieldBaseType::time_window_8:
    case DeltaFieldBaseType::time_window_big:
        return false;
    }
    return false;
}

[[nodiscard]] bool explicit_integer_fits(
    const DeltaFieldDefinition& field,
    const DeltaScalarValue& value) noexcept
{
    const auto width = field.significant_bits();
    if (field.type_flags().signed_value()) {
        const auto candidate = std::get<std::int32_t>(value);
        if (width == 32U) {
            return true;
        }
        const auto maximum =
            (std::int64_t{1} << (width - 1U)) - std::int64_t{1};
        const auto minimum = -(std::int64_t{1} << (width - 1U));
        return static_cast<std::int64_t>(candidate) >= minimum &&
               static_cast<std::int64_t>(candidate) <= maximum;
    }
    const auto candidate = std::get<std::uint32_t>(value);
    if (width == 32U) {
        return true;
    }
    const auto maximum = (std::uint32_t{1U} << width) - 1U;
    return candidate <= maximum;
}

[[nodiscard]] std::optional<DeltaValueError> validate_explicit_value(
    const DeltaFieldDefinition& field,
    const DeltaScalarValue& value,
    const GoldSrcDeltaValueLimits& limits,
    const std::size_t field_index)
{
    const auto base_type = field.type_flags().base_type();
    if (base_type == DeltaFieldBaseType::time_window_8 ||
        base_type == DeltaFieldBaseType::time_window_big) {
        return DeltaValueError{
            DeltaValueErrorCode::evidence_pending,
            0U,
            field_index,
            "Time-window values remain closed pending exact stock evidence",
        };
    }
    if (!value_type_matches(field, value)) {
        return DeltaValueError{
            DeltaValueErrorCode::value_type_mismatch,
            0U,
            field_index,
            "Explicit delta value does not match its schema field type",
        };
    }
    if ((base_type == DeltaFieldBaseType::byte_value ||
         base_type == DeltaFieldBaseType::short_value ||
         base_type == DeltaFieldBaseType::integer_value) &&
        !explicit_integer_fits(field, value)) {
        return DeltaValueError{
            DeltaValueErrorCode::numeric_overflow,
            0U,
            field_index,
            "Explicit integer does not fit its significant-bit width",
        };
    }
    if (const auto* string_value = std::get_if<std::string>(&value);
        string_value != nullptr &&
        string_value->size() > limits.maximum_string_bytes) {
        return DeltaValueError{
            DeltaValueErrorCode::string_limit_exceeded,
            0U,
            field_index,
            "Explicit string exceeds the configured byte bound",
        };
    }
    if (const auto* floating_value = std::get_if<double>(&value);
        floating_value != nullptr && !std::isfinite(*floating_value)) {
        return DeltaValueError{
            DeltaValueErrorCode::non_finite_result,
            0U,
            field_index,
            "Explicit floating value must be finite",
        };
    }
    if (!magnitude_within_limit(value, limits.maximum_numeric_magnitude)) {
        return DeltaValueError{
            DeltaValueErrorCode::numeric_magnitude_exceeded,
            0U,
            field_index,
            "Explicit numeric value exceeds the configured safety magnitude",
        };
    }
    return std::nullopt;
}

[[nodiscard]] std::int32_t sign_extend(
    const std::uint32_t raw,
    const std::uint8_t width) noexcept
{
    const auto sign_bit = std::uint32_t{1U} << (width - 1U);
    if ((raw & sign_bit) == 0U) {
        return static_cast<std::int32_t>(raw);
    }
    const auto modulus = std::int64_t{1} << width;
    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(raw) - modulus);
}

[[nodiscard]] bool same_field_definition(
    const DeltaFieldDefinition& left,
    const DeltaFieldDefinition& right) noexcept
{
    return left.name() == right.name() &&
           left.type_flags().base_type() ==
               right.type_flags().base_type() &&
           left.type_flags().signed_value() ==
               right.type_flags().signed_value() &&
           left.type_flags().wire_value() ==
               right.type_flags().wire_value() &&
           left.offset() == right.offset() &&
           left.storage_size() == right.storage_size() &&
           left.significant_bits() == right.significant_bits() &&
           left.premultiply_wire_value() ==
               right.premultiply_wire_value() &&
           left.postmultiply_wire_value() ==
               right.postmultiply_wire_value() &&
           left.wire_index() == right.wire_index() &&
           left.presence_mask() == right.presence_mask();
}

[[nodiscard]] bool validate_base(
    const DeltaObjectState& base,
    const DeltaSchema& schema,
    const DeltaValueCompatibilityProfile profile,
    const GoldSrcDeltaValueLimits& limits) noexcept
{
    if (!base.matches_schema(schema) ||
        base.decode_profile() != profile ||
        base.accounted_value_bytes() > limits.maximum_total_value_bytes) {
        return false;
    }
    for (std::size_t index = 0U; index < schema.field_count(); ++index) {
        const auto& field = schema.fields()[index];
        const auto& base_field = base.fields()[index];
        if (!value_type_matches(field, base_field.value())) {
            return false;
        }
        if (const auto* string_value =
                std::get_if<std::string>(&base_field.value());
            string_value != nullptr &&
            string_value->size() > limits.maximum_string_bytes) {
            return false;
        }
        if ((field.type_flags().base_type() ==
                 DeltaFieldBaseType::byte_value ||
             field.type_flags().base_type() ==
                 DeltaFieldBaseType::short_value ||
             field.type_flags().base_type() ==
                 DeltaFieldBaseType::integer_value) &&
            !explicit_integer_fits(field, base_field.value())) {
            return false;
        }
        if (!magnitude_within_limit(
                base_field.value(), limits.maximum_numeric_magnitude)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool valid_goldsrc_delta_value_limits(
    const GoldSrcDeltaValueLimits& limits) noexcept
{
    return limits.maximum_fields_per_object > 0U &&
           limits.maximum_fields_per_object <=
               kMaximumDeltaValueFieldsPerObject &&
           limits.maximum_mask_bytes > 0U &&
           limits.maximum_mask_bytes <= kMaximumDeltaValueMaskBytes &&
           limits.maximum_string_bytes > 0U &&
           limits.maximum_string_bytes <= kMaximumDeltaValueStringBytes &&
           limits.maximum_total_value_bytes > 0U &&
           limits.maximum_total_value_bytes <=
               kMaximumDeltaValueTotalBytes &&
           limits.maximum_object_count_per_message > 0U &&
           limits.maximum_object_count_per_message <=
               kMaximumDeltaObjectCountPerMessage &&
           limits.maximum_delta_bits > 0U &&
           limits.maximum_delta_bits <= kMaximumDeltaValueBits &&
           std::isfinite(limits.maximum_numeric_magnitude) &&
           limits.maximum_numeric_magnitude > 0.0 &&
           limits.maximum_numeric_magnitude <=
               kMaximumDeltaNumericMagnitude;
}

DeltaFieldValue::DeltaFieldValue(
    DeltaFieldDefinition definition,
    DeltaScalarValue value) noexcept
    : definition_{std::move(definition)},
      value_{std::move(value)}
{
}

std::string_view DeltaFieldValue::name() const noexcept
{
    return definition_.name();
}
DeltaFieldBaseType DeltaFieldValue::base_type() const noexcept
{
    return definition_.type_flags().base_type();
}
std::size_t DeltaFieldValue::wire_index() const noexcept
{
    return definition_.wire_index();
}
const DeltaScalarValue& DeltaFieldValue::value() const noexcept { return value_; }

DeltaObjectState::DeltaObjectState(
    std::string schema_name,
    const DeltaCompatibilityProfile schema_profile,
    const DeltaValueCompatibilityProfile decode_profile,
    std::vector<DeltaFieldValue> fields,
    const std::size_t accounted_value_bytes) noexcept
    : schema_name_{std::move(schema_name)},
      schema_profile_{schema_profile},
      decode_profile_{decode_profile},
      fields_{std::move(fields)},
      accounted_value_bytes_{accounted_value_bytes}
{
}

std::string_view DeltaObjectState::schema_name() const noexcept { return schema_name_; }
DeltaCompatibilityProfile DeltaObjectState::schema_profile() const noexcept { return schema_profile_; }
DeltaValueCompatibilityProfile DeltaObjectState::decode_profile() const noexcept { return decode_profile_; }
const std::vector<DeltaFieldValue>& DeltaObjectState::fields() const noexcept { return fields_; }
std::size_t DeltaObjectState::field_count() const noexcept { return fields_.size(); }
const DeltaFieldValue* DeltaObjectState::find_exact(
    const std::string_view field_name) const noexcept
{
    const auto found = std::find_if(
        fields_.begin(),
        fields_.end(),
        [field_name](const DeltaFieldValue& field) noexcept {
            return field.name() == field_name;
        });
    return found == fields_.end() ? nullptr : &*found;
}
std::size_t DeltaObjectState::accounted_value_bytes() const noexcept
{
    return accounted_value_bytes_;
}

bool DeltaObjectState::matches_schema(const DeltaSchema& schema) const noexcept
{
    if (schema_name_ != schema.name() ||
        schema_profile_ != schema.profile() ||
        fields_.size() != schema.field_count()) {
        return false;
    }
    for (std::size_t index = 0U; index < fields_.size(); ++index) {
        if (!same_field_definition(
                fields_[index].definition_, schema.fields()[index])) {
            return false;
        }
    }
    return true;
}

bool DeltaObjectState::has_same_schema_as(
    const DeltaObjectState& other) const noexcept
{
    if (schema_name_ != other.schema_name_ ||
        schema_profile_ != other.schema_profile_ ||
        fields_.size() != other.fields_.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < fields_.size(); ++index) {
        if (!same_field_definition(
                fields_[index].definition_,
                other.fields_[index].definition_)) {
            return false;
        }
    }
    return true;
}

DeltaObjectBuilder::DeltaObjectBuilder(
    GoldSrcDeltaValueLimits limits,
    const DeltaValueCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool DeltaObjectBuilder::valid_configuration() const noexcept
{
    return valid_goldsrc_delta_value_limits(limits_) && valid_profile(profile_);
}

const GoldSrcDeltaValueLimits& DeltaObjectBuilder::limits() const noexcept
{
    return limits_;
}

DeltaValueCompatibilityProfile DeltaObjectBuilder::profile() const noexcept
{
    return profile_;
}

DeltaObjectBuildResult DeltaObjectBuilder::build(
    const DeltaSchema& schema,
    const std::span<const DeltaScalarValue> values) const
{
    if (!valid_configuration()) {
        return build_failure(
            DeltaValueErrorCode::invalid_configuration,
            std::nullopt,
            "Delta object builder limits or profile are invalid");
    }
    if (profile_ == DeltaValueCompatibilityProfile::
                        stock_protocol_48_build_10210_evidence_pending) {
        return build_failure(
            DeltaValueErrorCode::evidence_pending,
            std::nullopt,
            "Stock runtime delta values remain closed pending exact evidence");
    }
    if (const auto schema_error = validate_schema(schema, limits_)) {
        return {std::nullopt, schema_error};
    }
    if (values.size() != schema.field_count()) {
        return build_failure(
            DeltaValueErrorCode::value_type_mismatch,
            std::nullopt,
            "Explicit delta object values must exactly match schema field count");
    }

    std::size_t accounted_bytes = 0U;
    std::vector<DeltaFieldValue> fields;
    fields.reserve(schema.field_count());
    for (std::size_t index = 0U; index < schema.field_count(); ++index) {
        const auto& definition = schema.fields()[index];
        if (const auto value_error =
                validate_explicit_value(definition, values[index], limits_, index)) {
            return {std::nullopt, value_error};
        }
        if (!checked_add(
                accounted_bytes,
                value_byte_count(values[index]),
                accounted_bytes) ||
            accounted_bytes > limits_.maximum_total_value_bytes) {
            return build_failure(
                DeltaValueErrorCode::total_value_bytes_exceeded,
                index,
                "Explicit delta object exceeds the configured value-byte bound");
        }
        fields.push_back(DeltaFieldValue{definition, values[index]});
    }

    return DeltaObjectBuildResult{
        DeltaObjectState{
            std::string{schema.name()},
            schema.profile(),
            profile_,
            std::move(fields),
            accounted_bytes,
        },
        std::nullopt,
    };
}

GoldSrcDeltaValueDecoder::GoldSrcDeltaValueDecoder(
    GoldSrcDeltaValueLimits limits,
    const DeltaValueCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool GoldSrcDeltaValueDecoder::valid_configuration() const noexcept
{
    return valid_goldsrc_delta_value_limits(limits_) && valid_profile(profile_);
}

const GoldSrcDeltaValueLimits& GoldSrcDeltaValueDecoder::limits() const noexcept
{
    return limits_;
}

DeltaValueCompatibilityProfile GoldSrcDeltaValueDecoder::profile() const noexcept
{
    return profile_;
}

DeltaValueDecodeResult GoldSrcDeltaValueDecoder::decode_delta(
    const DeltaSchemaRegistryState& registry,
    const std::string_view schema_name,
    const DeltaObjectState* const base,
    const DeltaValueDecodeContext& context) const
{
    if (!valid_configuration()) {
        return decode_failure(
            DeltaValueErrorCode::invalid_configuration,
            context.start_bit_offset,
            context.start_bit_offset,
            std::nullopt,
            "Delta value decoder limits or profile are invalid");
    }
    if (profile_ == DeltaValueCompatibilityProfile::
                        stock_protocol_48_build_10210_evidence_pending) {
        return decode_failure(
            DeltaValueErrorCode::evidence_pending,
            context.start_bit_offset,
            context.start_bit_offset,
            std::nullopt,
            "Stock runtime delta grammar remains closed pending exact evidence");
    }
    const auto* schema = registry.find_exact(schema_name);
    if (schema == nullptr) {
        return decode_failure(
            DeltaValueErrorCode::schema_not_found,
            context.start_bit_offset,
            context.start_bit_offset,
            std::nullopt,
            "Runtime delta schema name is absent from the published registry");
    }
    return decode_delta(*schema, base, context);
}

DeltaValueDecodeResult GoldSrcDeltaValueDecoder::decode_delta(
    const DeltaSchema& schema,
    const DeltaObjectState* const base,
    const DeltaValueDecodeContext& context) const
{
    const auto initial_bit_offset = context.start_bit_offset;
    if (!valid_configuration()) {
        return decode_failure(
            DeltaValueErrorCode::invalid_configuration,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Delta value decoder limits or profile are invalid");
    }
    // This gate intentionally precedes input/schema/base inspection and any
    // BitReader construction. Production cannot drift into synthetic grammar.
    if (profile_ == DeltaValueCompatibilityProfile::
                        stock_protocol_48_build_10210_evidence_pending) {
        return decode_failure(
            DeltaValueErrorCode::evidence_pending,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Stock runtime delta grammar remains closed pending exact evidence");
    }

    if (context.bytes.size() >
        std::numeric_limits<std::size_t>::max() / 8U) {
        return decode_failure(
            DeltaValueErrorCode::invalid_input_geometry,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Runtime delta input bit size overflowed");
    }
    const auto available_bits = context.bytes.size() * 8U;
    if (initial_bit_offset > available_bits) {
        return decode_failure(
            DeltaValueErrorCode::invalid_input_geometry,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Runtime delta start cursor is outside the owning bytes");
    }
    const auto remaining_bits = available_bits - initial_bit_offset;
    const auto selected_bits =
        context.bit_length == static_cast<std::size_t>(-1)
            ? remaining_bits
            : context.bit_length;
    if (selected_bits > remaining_bits ||
        selected_bits > limits_.maximum_delta_bits ||
        (initial_bit_offset & 7U) != 0U ||
        ((initial_bit_offset + selected_bits) & 7U) != 0U) {
        return decode_failure(
            DeltaValueErrorCode::invalid_input_geometry,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Synthetic runtime delta input must be bounded and byte-aligned");
    }
    if (const auto schema_error = validate_schema(schema, limits_)) {
        return decode_failure(
            schema_error->code,
            initial_bit_offset,
            initial_bit_offset,
            schema_error->field_index,
            schema_error->context);
    }
    if (base != nullptr &&
        !validate_base(*base, schema, profile_, limits_)) {
        return decode_failure(
            DeltaValueErrorCode::invalid_base,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Runtime delta base does not exactly match schema and profile");
    }

    BitReader reader{context.bytes, initial_bit_offset, selected_bits};
    if (!reader.valid()) {
        return decode_failure(
            DeltaValueErrorCode::invalid_input_geometry,
            initial_bit_offset,
            initial_bit_offset,
            std::nullopt,
            "Runtime delta bit-reader geometry is invalid");
    }
    const auto mask_count_read = reader.read_bits(8U);
    if (!mask_count_read) {
        return decode_failure(
            DeltaValueErrorCode::truncated_mask,
            initial_bit_offset,
            reader.bit_offset(),
            std::nullopt,
            "Runtime delta mask-byte count is truncated");
    }
    const auto mask_byte_count =
        static_cast<std::size_t>(mask_count_read.value);
    if (mask_byte_count > limits_.maximum_mask_bytes) {
        return decode_failure(
            DeltaValueErrorCode::mask_byte_limit_exceeded,
            initial_bit_offset,
            reader.bit_offset(),
            std::nullopt,
            "Runtime delta mask-byte count exceeds the configured bound");
    }
    const auto schema_mask_bytes = (schema.field_count() + 7U) / 8U;
    if (mask_byte_count > schema_mask_bytes) {
        return decode_failure(
            DeltaValueErrorCode::mask_length_exceeds_schema,
            initial_bit_offset,
            reader.bit_offset(),
            std::nullopt,
            "Runtime delta mask-byte count exceeds schema coverage");
    }

    std::vector<bool> changed(schema.field_count(), false);
    for (std::size_t mask_index = 0U;
         mask_index < mask_byte_count;
         ++mask_index) {
        const auto mask_read = reader.read_bits(8U);
        if (!mask_read) {
            return decode_failure(
                DeltaValueErrorCode::truncated_mask,
                initial_bit_offset,
                reader.bit_offset(),
                std::nullopt,
                "Runtime delta changed-field mask is truncated");
        }
        for (std::size_t bit = 0U; bit < 8U; ++bit) {
            if ((mask_read.value & (std::uint32_t{1U} << bit)) == 0U) {
                continue;
            }
            const auto field_index = mask_index * 8U + bit;
            if (field_index >= schema.field_count()) {
                return decode_failure(
                    DeltaValueErrorCode::mask_bit_out_of_range,
                    initial_bit_offset,
                    reader.bit_offset(),
                    field_index,
                    "Runtime delta mask selects a field outside the schema");
            }
            changed[field_index] = true;
        }
    }

    const auto all_fields_changed = std::all_of(
        changed.begin(), changed.end(), [](const bool value) noexcept {
            return value;
        });
    if (base == nullptr && !all_fields_changed) {
        return decode_failure(
            DeltaValueErrorCode::missing_required_base,
            initial_bit_offset,
            reader.bit_offset(),
            std::nullopt,
            "Synthetic runtime delta requires a base for every unchanged field");
    }

    std::vector<std::optional<DeltaScalarValue>> staged(schema.field_count());
    if (base != nullptr) {
        for (std::size_t index = 0U; index < schema.field_count(); ++index) {
            staged[index] = base->fields()[index].value();
        }
    }

    for (std::size_t index = 0U; index < schema.field_count(); ++index) {
        if (!changed[index]) {
            continue;
        }
        const auto& field = schema.fields()[index];
        if (!valid_significant_bits(field)) {
            return decode_failure(
                DeltaValueErrorCode::invalid_significant_bits,
                initial_bit_offset,
                reader.bit_offset(),
                index,
                "Runtime delta field significant-bit width is invalid");
        }
        const auto base_type = field.type_flags().base_type();
        if (base_type == DeltaFieldBaseType::time_window_8 ||
            base_type == DeltaFieldBaseType::time_window_big) {
            return decode_failure(
                DeltaValueErrorCode::evidence_pending,
                initial_bit_offset,
                reader.bit_offset(),
                index,
                "Time-window runtime decoding awaits stock reference-time evidence");
        }

        if (base_type == DeltaFieldBaseType::string) {
            const auto length_read = reader.read_bits(16U);
            if (!length_read) {
                return decode_failure(
                    DeltaValueErrorCode::truncated_value,
                    initial_bit_offset,
                    reader.bit_offset(),
                    index,
                    "Synthetic runtime string length is truncated");
            }
            const auto length = static_cast<std::size_t>(length_read.value);
            if (length > limits_.maximum_string_bytes) {
                return decode_failure(
                    DeltaValueErrorCode::string_limit_exceeded,
                    initial_bit_offset,
                    reader.bit_offset(),
                    index,
                    "Synthetic runtime string exceeds the configured byte bound");
            }
            std::string value;
            value.reserve(length);
            for (std::size_t byte_index = 0U; byte_index < length; ++byte_index) {
                const auto byte_read = reader.read_bits(8U);
                if (!byte_read) {
                    return decode_failure(
                        DeltaValueErrorCode::truncated_value,
                        initial_bit_offset,
                        reader.bit_offset(),
                        index,
                        "Synthetic runtime string bytes are truncated");
                }
                value.push_back(static_cast<char>(
                    static_cast<std::uint8_t>(byte_read.value)));
            }
            staged[index] = std::move(value);
            continue;
        }

        const auto raw_read = reader.read_bits(field.significant_bits());
        if (!raw_read) {
            return decode_failure(
                DeltaValueErrorCode::truncated_value,
                initial_bit_offset,
                reader.bit_offset(),
                index,
                "Synthetic runtime scalar bits are truncated");
        }
        const auto signed_quantized = field.type_flags().signed_value()
                                          ? sign_extend(
                                                raw_read.value,
                                                field.significant_bits())
                                          : std::int32_t{0};

        switch (base_type) {
        case DeltaFieldBaseType::byte_value:
        case DeltaFieldBaseType::short_value:
        case DeltaFieldBaseType::integer_value:
            if (field.type_flags().signed_value()) {
                staged[index] = signed_quantized;
            } else {
                staged[index] = raw_read.value;
            }
            break;
        case DeltaFieldBaseType::float_value: {
            const auto premultiply = field.premultiply_wire_value();
            const auto postmultiply = field.postmultiply_wire_value();
            if (premultiply == 0U || postmultiply == 0U) {
                return decode_failure(
                    DeltaValueErrorCode::invalid_multiplier,
                    initial_bit_offset,
                    reader.bit_offset(),
                    index,
                    "Runtime float multipliers must be non-zero");
            }
            const auto quantized = field.type_flags().signed_value()
                                       ? static_cast<long double>(signed_quantized)
                                       : static_cast<long double>(raw_read.value);
            const auto result =
                quantized * static_cast<long double>(postmultiply) /
                static_cast<long double>(premultiply);
            if (!std::isfinite(result) ||
                result > static_cast<long double>(
                             std::numeric_limits<double>::max()) ||
                result < -static_cast<long double>(
                              std::numeric_limits<double>::max())) {
                return decode_failure(
                    DeltaValueErrorCode::numeric_overflow,
                    initial_bit_offset,
                    reader.bit_offset(),
                    index,
                    "Runtime float rational scaling overflowed");
            }
            const auto value = static_cast<double>(result);
            if (!std::isfinite(value)) {
                return decode_failure(
                    DeltaValueErrorCode::non_finite_result,
                    initial_bit_offset,
                    reader.bit_offset(),
                    index,
                    "Runtime float rational scaling is non-finite");
            }
            staged[index] = value;
            break;
        }
        case DeltaFieldBaseType::angle: {
            const auto denominator =
                std::ldexp(1.0L, field.significant_bits());
            const auto value = static_cast<double>(
                static_cast<long double>(raw_read.value) * 360.0L /
                denominator);
            if (!std::isfinite(value)) {
                return decode_failure(
                    DeltaValueErrorCode::non_finite_result,
                    initial_bit_offset,
                    reader.bit_offset(),
                    index,
                    "Runtime angle normalization is non-finite");
            }
            staged[index] = value;
            break;
        }
        case DeltaFieldBaseType::string:
        case DeltaFieldBaseType::time_window_8:
        case DeltaFieldBaseType::time_window_big:
            return decode_failure(
                DeltaValueErrorCode::invalid_schema,
                initial_bit_offset,
                reader.bit_offset(),
                index,
                "Runtime delta reached an inconsistent scalar field type");
        }

        if (!magnitude_within_limit(
                *staged[index], limits_.maximum_numeric_magnitude)) {
            return decode_failure(
                DeltaValueErrorCode::numeric_magnitude_exceeded,
                initial_bit_offset,
                reader.bit_offset(),
                index,
                "Runtime numeric value exceeds the configured safety magnitude");
        }
    }

    const auto padding_error = reader.align_to_byte_zero_padding();
    if (padding_error != BitReaderError::none) {
        return decode_failure(
            padding_error == BitReaderError::nonzero_padding
                ? DeltaValueErrorCode::nonzero_padding
                : DeltaValueErrorCode::truncated_value,
            initial_bit_offset,
            reader.bit_offset(),
            std::nullopt,
            "Runtime delta has truncated or non-zero byte padding");
    }
    if (reader.remaining_bits() != 0U) {
        return decode_failure(
            DeltaValueErrorCode::unexpected_trailing_bits,
            initial_bit_offset,
            reader.bit_offset(),
            std::nullopt,
            "Runtime delta leaves bytes after its aligned object end");
    }

    std::size_t accounted_bytes = 0U;
    std::vector<DeltaFieldValue> fields;
    fields.reserve(schema.field_count());
    for (std::size_t index = 0U; index < schema.field_count(); ++index) {
        if (!staged[index]) {
            return decode_failure(
                DeltaValueErrorCode::missing_required_base,
                initial_bit_offset,
                reader.bit_offset(),
                index,
                "Runtime delta left a field without a decoded or base value");
        }
        const auto value_size = value_byte_count(*staged[index]);
        if (!checked_add(accounted_bytes, value_size, accounted_bytes) ||
            accounted_bytes > limits_.maximum_total_value_bytes) {
            return decode_failure(
                DeltaValueErrorCode::total_value_bytes_exceeded,
                initial_bit_offset,
                reader.bit_offset(),
                index,
                "Runtime delta object exceeds the configured value-byte bound");
        }
        const auto& definition = schema.fields()[index];
        fields.push_back(DeltaFieldValue{
            definition, std::move(*staged[index])});
    }

    const auto bits_consumed = reader.bit_offset() - initial_bit_offset;
    return DeltaValueDecodeResult{
        DeltaObjectState{
            std::string{schema.name()},
            schema.profile(),
            profile_,
            std::move(fields),
            accounted_bytes,
        },
        std::nullopt,
        bits_consumed,
        reader.bit_offset(),
        reader.bit_offset() / 8U,
    };
}

} // namespace hlclient::goldsrc
