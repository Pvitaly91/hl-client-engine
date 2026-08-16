#include <hlclient/goldsrc/delta_description.hpp>

#include <hlclient/goldsrc/bit_reader.hpp>

#include <algorithm>
#include <limits>
#include <utility>
#include <variant>

namespace hlclient::goldsrc {
namespace {

constexpr std::uint8_t kRequiredDeltaPresenceMask = 0x7bU;
constexpr std::uint8_t kDeltaOffsetPresenceBit = 0x04U;
constexpr std::size_t kAccountedSchemaBytes = 32U;
constexpr std::size_t kAccountedFieldBytes = 32U;

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

[[nodiscard]] bool valid_wire_name(
    const std::string_view name) noexcept
{
    if (name.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(name.front());
    const bool valid_first =
        (first >= 'A' && first <= 'Z') ||
        (first >= 'a' && first <= 'z') || first == '_';
    if (!valid_first) {
        return false;
    }
    return std::all_of(
        name.begin() + 1,
        name.end(),
        [](const char character) noexcept {
            const auto value = static_cast<unsigned char>(character);
            return (value >= 'A' && value <= 'Z') ||
                   (value >= 'a' && value <= 'z') ||
                   (value >= '0' && value <= '9') || value == '_' ||
                   value == '[' || value == ']' || value == '.';
        });
}

[[nodiscard]] DeltaDescriptionParseResult parse_failure(
    const DeltaDescriptionErrorCode code,
    const std::size_t bit_offset,
    const std::optional<std::size_t> field_index,
    std::string context)
{
    return DeltaDescriptionParseResult{
        std::nullopt,
        DeltaDescriptionError{
            code,
            bit_offset / 8U,
            bit_offset,
            field_index,
            std::move(context),
        },
        0U,
        0U,
        0U,
        0U,
    };
}

[[nodiscard]] DeltaRegistryInsertResult registry_failure(
    const DeltaRegistryErrorCode code,
    const std::optional<std::size_t> schema_index,
    std::string context)
{
    return DeltaRegistryInsertResult{
        false,
        DeltaRegistryError{code, schema_index, std::move(context)},
    };
}

[[nodiscard]] DeltaDescriptionStreamDecodeResult stream_failure(
    const DeltaDescriptionStreamErrorCode code,
    const std::size_t byte_offset,
    const std::size_t bit_offset,
    const std::optional<DeltaDescriptionErrorCode> parser_code,
    const std::optional<DeltaRegistryErrorCode> registry_code,
    std::string context)
{
    return DeltaDescriptionStreamDecodeResult{
        std::nullopt,
        DeltaDescriptionStreamError{
            code,
            byte_offset,
            bit_offset,
            parser_code,
            registry_code,
            std::move(context),
        },
        0U,
    };
}

[[nodiscard]] std::optional<DeltaFieldBaseType> decode_base_type(
    const std::uint32_t wire_value) noexcept
{
    const auto base_value = wire_value & 0xffU;
    switch (base_value) {
    case static_cast<std::uint32_t>(DeltaFieldBaseType::byte_value):
        return DeltaFieldBaseType::byte_value;
    case static_cast<std::uint32_t>(DeltaFieldBaseType::short_value):
        return DeltaFieldBaseType::short_value;
    case static_cast<std::uint32_t>(DeltaFieldBaseType::float_value):
        return DeltaFieldBaseType::float_value;
    case static_cast<std::uint32_t>(DeltaFieldBaseType::integer_value):
        return DeltaFieldBaseType::integer_value;
    case static_cast<std::uint32_t>(DeltaFieldBaseType::angle):
        return DeltaFieldBaseType::angle;
    case static_cast<std::uint32_t>(DeltaFieldBaseType::time_window_8):
        return DeltaFieldBaseType::time_window_8;
    case static_cast<std::uint32_t>(DeltaFieldBaseType::time_window_big):
        return DeltaFieldBaseType::time_window_big;
    case static_cast<std::uint32_t>(DeltaFieldBaseType::string):
        return DeltaFieldBaseType::string;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool signed_modifier_allowed(
    const DeltaFieldBaseType base_type) noexcept
{
    return base_type == DeltaFieldBaseType::short_value ||
           base_type == DeltaFieldBaseType::float_value ||
           base_type == DeltaFieldBaseType::integer_value;
}

[[nodiscard]] std::uint8_t maximum_significant_bits(
    const DeltaFieldBaseType base_type) noexcept
{
    switch (base_type) {
    case DeltaFieldBaseType::byte_value:
        return 8U;
    case DeltaFieldBaseType::short_value:
        return 16U;
    case DeltaFieldBaseType::string:
        return 1U;
    case DeltaFieldBaseType::float_value:
    case DeltaFieldBaseType::integer_value:
    case DeltaFieldBaseType::angle:
    case DeltaFieldBaseType::time_window_8:
    case DeltaFieldBaseType::time_window_big:
        return 32U;
    }
    return 0U;
}

} // namespace

bool valid_delta_description_limits(
    const DeltaDescriptionLimits& limits) noexcept
{
    return limits.maximum_schema_name_length > 0U &&
           limits.maximum_schema_name_length <= kMaximumDeltaSchemaNameLength &&
           limits.maximum_field_name_length > 0U &&
           limits.maximum_field_name_length <= kMaximumDeltaFieldNameLength &&
           limits.maximum_schema_count > 0U &&
           limits.maximum_schema_count <= kMaximumDeltaSchemaCount &&
           limits.maximum_fields_per_schema > 0U &&
           limits.maximum_fields_per_schema <= kMaximumDeltaFieldsPerSchema &&
           limits.maximum_total_fields > 0U &&
           limits.maximum_total_fields <= kMaximumDeltaTotalFields &&
           limits.maximum_total_name_bytes > 0U &&
           limits.maximum_total_name_bytes <= kMaximumDeltaTotalNameBytes &&
           limits.maximum_message_bits > 0U &&
           limits.maximum_message_bits <= kMaximumDeltaMessageBits &&
           limits.maximum_registry_bytes > 0U &&
           limits.maximum_registry_bytes <= kMaximumDeltaRegistryBytes;
}

DeltaFieldTypeFlags::DeltaFieldTypeFlags(
    const DeltaFieldBaseType base_type,
    const bool signed_value,
    const std::uint32_t wire_value) noexcept
    : base_type_{base_type},
      signed_value_{signed_value},
      wire_value_{wire_value}
{
}

DeltaFieldBaseType DeltaFieldTypeFlags::base_type() const noexcept
{
    return base_type_;
}

bool DeltaFieldTypeFlags::signed_value() const noexcept
{
    return signed_value_;
}

std::uint32_t DeltaFieldTypeFlags::wire_value() const noexcept
{
    return wire_value_;
}

DeltaFieldDefinition::DeltaFieldDefinition(
    std::string name,
    DeltaFieldTypeFlags type_flags,
    const std::uint16_t offset,
    const std::uint8_t storage_size,
    const std::uint8_t significant_bits,
    const std::uint32_t premultiply_wire_value,
    const std::uint32_t postmultiply_wire_value,
    const std::size_t wire_index,
    const std::uint8_t presence_mask) noexcept
    : name_{std::move(name)},
      type_flags_{type_flags},
      offset_{offset},
      storage_size_{storage_size},
      significant_bits_{significant_bits},
      premultiply_wire_value_{premultiply_wire_value},
      postmultiply_wire_value_{postmultiply_wire_value},
      wire_index_{wire_index},
      presence_mask_{presence_mask}
{
}

std::string_view DeltaFieldDefinition::name() const noexcept { return name_; }
const DeltaFieldTypeFlags& DeltaFieldDefinition::type_flags() const noexcept { return type_flags_; }
std::uint16_t DeltaFieldDefinition::offset() const noexcept { return offset_; }
std::uint8_t DeltaFieldDefinition::storage_size() const noexcept { return storage_size_; }
std::uint8_t DeltaFieldDefinition::significant_bits() const noexcept { return significant_bits_; }
std::uint32_t DeltaFieldDefinition::premultiply_wire_value() const noexcept { return premultiply_wire_value_; }
std::uint32_t DeltaFieldDefinition::postmultiply_wire_value() const noexcept { return postmultiply_wire_value_; }
double DeltaFieldDefinition::premultiply() const noexcept
{
    return static_cast<double>(premultiply_wire_value_) /
           static_cast<double>(kDeltaMultiplierScale);
}
double DeltaFieldDefinition::postmultiply() const noexcept
{
    return static_cast<double>(postmultiply_wire_value_) /
           static_cast<double>(kDeltaMultiplierScale);
}
std::size_t DeltaFieldDefinition::wire_index() const noexcept { return wire_index_; }
std::uint8_t DeltaFieldDefinition::presence_mask() const noexcept { return presence_mask_; }

DeltaSchema::DeltaSchema(
    std::string name,
    std::vector<DeltaFieldDefinition> fields,
    const std::size_t source_message_offset,
    const std::size_t message_bits,
    const std::size_t message_bytes,
    const DeltaCompatibilityProfile profile) noexcept
    : name_{std::move(name)},
      fields_{std::move(fields)},
      source_message_offset_{source_message_offset},
      message_bits_{message_bits},
      message_bytes_{message_bytes},
      profile_{profile}
{
}

std::string_view DeltaSchema::name() const noexcept { return name_; }
const std::vector<DeltaFieldDefinition>& DeltaSchema::fields() const noexcept { return fields_; }
std::size_t DeltaSchema::field_count() const noexcept { return fields_.size(); }
std::size_t DeltaSchema::source_message_offset() const noexcept { return source_message_offset_; }
std::size_t DeltaSchema::message_bits() const noexcept { return message_bits_; }
std::size_t DeltaSchema::message_bytes() const noexcept { return message_bytes_; }
DeltaCompatibilityProfile DeltaSchema::profile() const noexcept { return profile_; }

DeltaDescriptionParser::DeltaDescriptionParser(
    DeltaDescriptionLimits limits,
    const DeltaCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool DeltaDescriptionParser::valid_configuration() const noexcept
{
    return valid_delta_description_limits(limits_);
}

const DeltaDescriptionLimits& DeltaDescriptionParser::limits() const noexcept
{
    return limits_;
}

DeltaDescriptionParseResult DeltaDescriptionParser::parse(
    const std::span<const std::byte> service_payload,
    const std::size_t opcode_byte_offset,
    const std::size_t service_payload_bit_length) const
{
    if (!valid_configuration()) {
        return parse_failure(
            DeltaDescriptionErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            "Delta-description limits are outside project hard caps");
    }
    if (service_payload.size() > std::numeric_limits<std::size_t>::max() / 8U) {
        return parse_failure(
            DeltaDescriptionErrorCode::invalid_input_geometry,
            0U,
            std::nullopt,
            "Delta-description opcode offset is outside the service payload");
    }
    const auto available_bits = service_payload.size() * 8U;
    const auto selected_bit_length =
        service_payload_bit_length == static_cast<std::size_t>(-1)
            ? available_bits
            : service_payload_bit_length;
    if (selected_bit_length > available_bits ||
        opcode_byte_offset >= service_payload.size() ||
        opcode_byte_offset > std::numeric_limits<std::size_t>::max() / 8U ||
        opcode_byte_offset * 8U > selected_bit_length ||
        selected_bit_length - opcode_byte_offset * 8U < 8U) {
        return parse_failure(
            DeltaDescriptionErrorCode::invalid_input_geometry,
            0U,
            std::nullopt,
            "Delta-description opcode offset or bit limit is outside the service payload");
    }
    if (std::to_integer<std::uint8_t>(service_payload[opcode_byte_offset]) !=
        kDeltaDescriptionOpcode) {
        return parse_failure(
            DeltaDescriptionErrorCode::wrong_opcode,
            opcode_byte_offset * 8U,
            std::nullopt,
            "Parser input does not begin with opcode 14");
    }
    if (opcode_byte_offset == std::numeric_limits<std::size_t>::max()) {
        return parse_failure(
            DeltaDescriptionErrorCode::size_overflow,
            opcode_byte_offset * 8U,
            std::nullopt,
            "Delta-description body offset overflowed");
    }

    const auto message_start_bit = opcode_byte_offset * 8U;
    const auto body_start_bit = (opcode_byte_offset + 1U) * 8U;
    BitReader reader{
        service_payload,
        body_start_bit,
        selected_bit_length - body_start_bit};
    if (!reader.valid()) {
        return parse_failure(
            DeltaDescriptionErrorCode::invalid_input_geometry,
            body_start_bit,
            std::nullopt,
            "Unable to construct a bounded delta-description reader");
    }

    auto read_name = [&reader](
                         const std::size_t maximum_length,
                         const DeltaDescriptionErrorCode truncated_code,
                         const DeltaDescriptionErrorCode too_long_code,
                         const std::optional<std::size_t> field_index)
        -> std::variant<std::string, DeltaDescriptionParseResult> {
        std::string value;
        value.reserve(std::min<std::size_t>(maximum_length, 64U));
        while (value.size() <= maximum_length) {
            const auto character = reader.read_bits(8U);
            if (!character) {
                return parse_failure(
                    truncated_code,
                    reader.bit_offset(),
                    field_index,
                    "Delta-description NUL string is truncated");
            }
            if (character.value == 0U) {
                return value;
            }
            if (value.size() == maximum_length) {
                return parse_failure(
                    too_long_code,
                    reader.bit_offset() - 8U,
                    field_index,
                    "Delta-description NUL string exceeds its configured bound");
            }
            value.push_back(static_cast<char>(character.value));
        }
        return parse_failure(
            too_long_code,
            reader.bit_offset(),
            field_index,
            "Delta-description NUL string exceeds its configured bound");
    };

    auto schema_name_result = read_name(
        limits_.maximum_schema_name_length,
        DeltaDescriptionErrorCode::truncated_schema_name,
        DeltaDescriptionErrorCode::schema_name_too_long,
        std::nullopt);
    if (std::holds_alternative<DeltaDescriptionParseResult>(schema_name_result)) {
        return std::get<DeltaDescriptionParseResult>(std::move(schema_name_result));
    }
    auto schema_name = std::get<std::string>(std::move(schema_name_result));
    if (!valid_wire_name(schema_name)) {
        return parse_failure(
            DeltaDescriptionErrorCode::invalid_schema_name,
            body_start_bit,
            std::nullopt,
            "Delta schema name is outside the confirmed terminal-safe grammar");
    }

    const auto field_count_read = reader.read_bits(16U);
    if (!field_count_read) {
        return parse_failure(
            DeltaDescriptionErrorCode::truncated_field,
            reader.bit_offset(),
            std::nullopt,
            "Delta schema field count is truncated");
    }
    const auto field_count = static_cast<std::size_t>(field_count_read.value);
    if (field_count == 0U) {
        return parse_failure(
            DeltaDescriptionErrorCode::zero_field_count,
            reader.bit_offset() - 16U,
            std::nullopt,
            "The captured stock profile has no empty delta schemas");
    }
    if (field_count > limits_.maximum_fields_per_schema) {
        return parse_failure(
            DeltaDescriptionErrorCode::field_count_limit_exceeded,
            reader.bit_offset() - 16U,
            std::nullopt,
            "Delta schema field count exceeds the configured bound");
    }

    std::vector<DeltaFieldDefinition> fields;
    fields.reserve(field_count);
    for (std::size_t field_index = 0U; field_index < field_count; ++field_index) {
        const auto field_start_bit = reader.bit_offset();
        const auto mask_byte_count = reader.read_bits(3U);
        const auto presence_mask_read = reader.read_bits(8U);
        if (!mask_byte_count || !presence_mask_read) {
            return parse_failure(
                DeltaDescriptionErrorCode::truncated_field,
                field_start_bit,
                field_index,
                "Delta field presence mask is truncated");
        }
        const auto presence_mask = static_cast<std::uint8_t>(presence_mask_read.value);
        if (mask_byte_count.value != 1U ||
            (presence_mask & kRequiredDeltaPresenceMask) != kRequiredDeltaPresenceMask ||
            (presence_mask & static_cast<std::uint8_t>(~(
                kRequiredDeltaPresenceMask | kDeltaOffsetPresenceBit))) != 0U) {
            return parse_failure(
                DeltaDescriptionErrorCode::invalid_presence_mask,
                field_start_bit,
                field_index,
                "Delta field presence mask is outside the confirmed one-byte profile");
        }

        const auto type_read = reader.read_bits(32U);
        if (!type_read) {
            return parse_failure(
                DeltaDescriptionErrorCode::truncated_field,
                reader.bit_offset(),
                field_index,
                "Delta field type flags are truncated");
        }
        const auto wire_type = type_read.value;
        if ((wire_type & ~(kDeltaSignedModifier | 0xffU)) != 0U) {
            return parse_failure(
                DeltaDescriptionErrorCode::reserved_type_flag,
                reader.bit_offset() - 32U,
                field_index,
                "Delta field type contains a reserved bit");
        }
        const auto base_type = decode_base_type(wire_type);
        if (!base_type) {
            const auto base_bits = wire_type & 0xffU;
            const bool conflicting = base_bits != 0U &&
                (base_bits & (base_bits - 1U)) != 0U;
            return parse_failure(
                conflicting
                    ? DeltaDescriptionErrorCode::conflicting_base_type_flags
                    : DeltaDescriptionErrorCode::invalid_type_flags,
                reader.bit_offset() - 32U,
                field_index,
                "Delta field must contain exactly one confirmed base type");
        }
        const bool signed_value = (wire_type & kDeltaSignedModifier) != 0U;
        if (signed_value && !signed_modifier_allowed(*base_type)) {
            return parse_failure(
                DeltaDescriptionErrorCode::invalid_type_flags,
                reader.bit_offset() - 32U,
                field_index,
                "Signed modifier is not valid for this captured base type");
        }

        auto field_name_result = read_name(
            limits_.maximum_field_name_length,
            DeltaDescriptionErrorCode::truncated_field,
            DeltaDescriptionErrorCode::field_name_too_long,
            field_index);
        if (std::holds_alternative<DeltaDescriptionParseResult>(field_name_result)) {
            return std::get<DeltaDescriptionParseResult>(std::move(field_name_result));
        }
        auto field_name = std::get<std::string>(std::move(field_name_result));
        if (field_name.empty()) {
            return parse_failure(
                DeltaDescriptionErrorCode::missing_field_name,
                reader.bit_offset(),
                field_index,
                "Delta field name is empty");
        }
        if (!valid_wire_name(field_name)) {
            return parse_failure(
                DeltaDescriptionErrorCode::invalid_field_name,
                reader.bit_offset(),
                field_index,
                "Delta field name is outside the confirmed terminal-safe grammar");
        }
        if (std::any_of(
                fields.begin(),
                fields.end(),
                [&field_name](const DeltaFieldDefinition& field) noexcept {
                    return field.name() == field_name;
                })) {
            return parse_failure(
                DeltaDescriptionErrorCode::duplicate_field_name,
                reader.bit_offset(),
                field_index,
                "Delta schema repeats an exact field name");
        }

        std::uint32_t offset_value = 0U;
        if ((presence_mask & kDeltaOffsetPresenceBit) != 0U) {
            const auto offset_read = reader.read_bits(16U);
            if (!offset_read) {
                return parse_failure(
                    DeltaDescriptionErrorCode::truncated_field,
                    reader.bit_offset(),
                    field_index,
                    "Delta field offset is truncated");
            }
            offset_value = offset_read.value;
        }
        const auto storage_size_read = reader.read_bits(8U);
        const auto significant_bits_read = reader.read_bits(8U);
        const auto premultiply_read = reader.read_bits(32U);
        const auto postmultiply_read = reader.read_bits(32U);
        if (!storage_size_read || !significant_bits_read ||
            !premultiply_read || !postmultiply_read) {
            return parse_failure(
                DeltaDescriptionErrorCode::truncated_field,
                reader.bit_offset(),
                field_index,
                "Delta field numeric metadata is truncated");
        }

        if (storage_size_read.value != 1U) {
            return parse_failure(
                DeltaDescriptionErrorCode::invalid_storage_size,
                reader.bit_offset(),
                field_index,
                "Captured delta field storage-size metadata must equal one");
        }
        if (((presence_mask & kDeltaOffsetPresenceBit) == 0U) !=
            (offset_value == 0U)) {
            return parse_failure(
                DeltaDescriptionErrorCode::invalid_presence_mask,
                field_start_bit,
                field_index,
                "Delta offset presence conflicts with the confirmed zero-offset encoding");
        }
        if (offset_value > std::numeric_limits<std::uint16_t>::max() ||
            offset_value + storage_size_read.value >
                static_cast<std::uint32_t>(
                    std::numeric_limits<std::uint16_t>::max()) + 1U) {
            return parse_failure(
                DeltaDescriptionErrorCode::offset_size_overflow,
                reader.bit_offset(),
                field_index,
                "Delta field offset plus storage size is outside the bounded range");
        }
        if (significant_bits_read.value == 0U ||
            significant_bits_read.value > maximum_significant_bits(*base_type)) {
            return parse_failure(
                DeltaDescriptionErrorCode::invalid_significant_bits,
                reader.bit_offset(),
                field_index,
                "Delta field significant-bit count conflicts with its base type");
        }
        if (premultiply_read.value == 0U || postmultiply_read.value == 0U) {
            return parse_failure(
                DeltaDescriptionErrorCode::invalid_multiplier,
                reader.bit_offset(),
                field_index,
                "Delta fixed-point multipliers must be non-zero");
        }

        fields.emplace_back(DeltaFieldDefinition{
            std::move(field_name),
            DeltaFieldTypeFlags{*base_type, signed_value, wire_type},
            static_cast<std::uint16_t>(offset_value),
            static_cast<std::uint8_t>(storage_size_read.value),
            static_cast<std::uint8_t>(significant_bits_read.value),
            premultiply_read.value,
            postmultiply_read.value,
            field_index,
            presence_mask,
        });

        const auto consumed = reader.bit_offset() - message_start_bit;
        if (consumed > limits_.maximum_message_bits) {
            return parse_failure(
                DeltaDescriptionErrorCode::message_too_large,
                reader.bit_offset(),
                field_index,
                "Delta message exceeds the configured bit bound");
        }
    }

    const auto padding_error = reader.align_to_byte_zero_padding();
    if (padding_error != BitReaderError::none) {
        return parse_failure(
            padding_error == BitReaderError::nonzero_padding
                ? DeltaDescriptionErrorCode::nonzero_padding
                : DeltaDescriptionErrorCode::truncated_field,
            reader.bit_offset(),
            std::nullopt,
            "Delta message has truncated or non-zero byte-alignment padding");
    }
    const auto message_bits = reader.bit_offset() - message_start_bit;
    if (message_bits > limits_.maximum_message_bits || (message_bits & 7U) != 0U) {
        return parse_failure(
            DeltaDescriptionErrorCode::message_too_large,
            reader.bit_offset(),
            std::nullopt,
            "Delta message exceeds the configured bit bound");
    }
    const auto message_bytes = message_bits / 8U;
    const auto next_byte_offset = reader.bit_offset() / 8U;

    return DeltaDescriptionParseResult{
        DeltaSchema{
            std::move(schema_name),
            std::move(fields),
            opcode_byte_offset,
            message_bits,
            message_bytes,
            profile_,
        },
        std::nullopt,
        message_bits,
        message_bytes,
        next_byte_offset,
        0U,
    };
}

DeltaSchemaRegistryState::DeltaSchemaRegistryState(
    std::vector<DeltaSchema> schemas,
    const std::size_t total_field_count,
    const std::size_t total_name_bytes,
    const std::size_t accounted_registry_bytes) noexcept
    : schemas_{std::move(schemas)},
      total_field_count_{total_field_count},
      total_name_bytes_{total_name_bytes},
      accounted_registry_bytes_{accounted_registry_bytes}
{
}

const std::vector<DeltaSchema>& DeltaSchemaRegistryState::schemas() const noexcept { return schemas_; }
const DeltaSchema* DeltaSchemaRegistryState::find_exact(const std::string_view name) const noexcept
{
    const auto found = std::find_if(
        schemas_.begin(),
        schemas_.end(),
        [name](const DeltaSchema& schema) noexcept { return schema.name() == name; });
    return found == schemas_.end() ? nullptr : &*found;
}
std::size_t DeltaSchemaRegistryState::schema_count() const noexcept { return schemas_.size(); }
std::size_t DeltaSchemaRegistryState::total_field_count() const noexcept { return total_field_count_; }
std::size_t DeltaSchemaRegistryState::total_name_bytes() const noexcept { return total_name_bytes_; }
std::size_t DeltaSchemaRegistryState::accounted_registry_bytes() const noexcept { return accounted_registry_bytes_; }

DeltaSchemaRegistryBuilder::DeltaSchemaRegistryBuilder(
    DeltaDescriptionLimits limits) noexcept
    : limits_{limits}
{
}

bool DeltaSchemaRegistryBuilder::valid_configuration() const noexcept
{
    return valid_delta_description_limits(limits_);
}

DeltaRegistryInsertResult DeltaSchemaRegistryBuilder::insert(
    const DeltaSchema& schema)
{
    if (!valid_configuration()) {
        return registry_failure(
            DeltaRegistryErrorCode::invalid_configuration,
            std::nullopt,
            "Delta registry limits are outside project hard caps");
    }
    if (schemas_.size() >= limits_.maximum_schema_count) {
        return registry_failure(
            DeltaRegistryErrorCode::schema_count_limit_exceeded,
            schemas_.size(),
            "Delta registry schema count exceeds the configured bound");
    }
    if (std::any_of(
            schemas_.begin(),
            schemas_.end(),
            [&schema](const DeltaSchema& existing) noexcept {
                return existing.name() == schema.name();
            })) {
        return registry_failure(
            DeltaRegistryErrorCode::duplicate_schema_name,
            schemas_.size(),
            "Delta registry repeats an exact schema name");
    }

    std::size_t candidate_fields = 0U;
    if (!checked_add(total_field_count_, schema.field_count(), candidate_fields)) {
        return registry_failure(
            DeltaRegistryErrorCode::size_overflow,
            schemas_.size(),
            "Delta registry field count overflowed");
    }
    if (candidate_fields > limits_.maximum_total_fields) {
        return registry_failure(
            DeltaRegistryErrorCode::total_field_limit_exceeded,
            schemas_.size(),
            "Delta registry total field count exceeds the configured bound");
    }

    std::size_t added_name_bytes = schema.name().size() + 1U;
    for (const auto& field : schema.fields()) {
        if (!checked_add(added_name_bytes, field.name().size() + 1U, added_name_bytes)) {
            return registry_failure(
                DeltaRegistryErrorCode::size_overflow,
                schemas_.size(),
                "Delta registry name-byte count overflowed");
        }
    }
    std::size_t candidate_name_bytes = 0U;
    if (!checked_add(total_name_bytes_, added_name_bytes, candidate_name_bytes)) {
        return registry_failure(
            DeltaRegistryErrorCode::size_overflow,
            schemas_.size(),
            "Delta registry name-byte count overflowed");
    }
    if (candidate_name_bytes > limits_.maximum_total_name_bytes) {
        return registry_failure(
            DeltaRegistryErrorCode::total_name_byte_limit_exceeded,
            schemas_.size(),
            "Delta registry total name bytes exceed the configured bound");
    }

    std::size_t added_registry_bytes = 0U;
    if (!checked_add(kAccountedSchemaBytes, added_name_bytes, added_registry_bytes)) {
        return registry_failure(
            DeltaRegistryErrorCode::size_overflow,
            schemas_.size(),
            "Delta registry accounting overflowed");
    }
    if (schema.field_count() >
        std::numeric_limits<std::size_t>::max() / kAccountedFieldBytes) {
        return registry_failure(
            DeltaRegistryErrorCode::size_overflow,
            schemas_.size(),
            "Delta registry field accounting overflowed");
    }
    if (!checked_add(
            added_registry_bytes,
            schema.field_count() * kAccountedFieldBytes,
            added_registry_bytes)) {
        return registry_failure(
            DeltaRegistryErrorCode::size_overflow,
            schemas_.size(),
            "Delta registry accounting overflowed");
    }
    std::size_t candidate_registry_bytes = 0U;
    if (!checked_add(
            accounted_registry_bytes_,
            added_registry_bytes,
            candidate_registry_bytes)) {
        return registry_failure(
            DeltaRegistryErrorCode::size_overflow,
            schemas_.size(),
            "Delta registry accounting overflowed");
    }
    if (candidate_registry_bytes > limits_.maximum_registry_bytes) {
        return registry_failure(
            DeltaRegistryErrorCode::registry_byte_limit_exceeded,
            schemas_.size(),
            "Delta registry accounted bytes exceed the configured bound");
    }

    auto candidate = schemas_;
    candidate.push_back(schema);
    schemas_.swap(candidate);
    total_field_count_ = candidate_fields;
    total_name_bytes_ = candidate_name_bytes;
    accounted_registry_bytes_ = candidate_registry_bytes;
    return {true, std::nullopt};
}

DeltaSchemaRegistryState DeltaSchemaRegistryBuilder::publish() && noexcept
{
    return DeltaSchemaRegistryState{
        std::move(schemas_),
        total_field_count_,
        total_name_bytes_,
        accounted_registry_bytes_,
    };
}

const std::vector<DeltaSchema>& DeltaSchemaRegistryBuilder::candidate_schemas() const noexcept
{
    return schemas_;
}

PostDeltaBoundary::PostDeltaBoundary(
    const std::uint8_t opcode,
    const std::size_t byte_offset,
    const std::size_t bit_offset,
    const std::size_t remaining_byte_count,
    const PostDeltaBoundaryCategory category,
    const PostDeltaBoundaryEvidenceStatus evidence_status) noexcept
    : opcode_{opcode},
      byte_offset_{byte_offset},
      bit_offset_{bit_offset},
      remaining_byte_count_{remaining_byte_count},
      category_{category},
      evidence_status_{evidence_status}
{
}

std::uint8_t PostDeltaBoundary::opcode() const noexcept { return opcode_; }
std::size_t PostDeltaBoundary::byte_offset() const noexcept { return byte_offset_; }
std::size_t PostDeltaBoundary::bit_offset() const noexcept { return bit_offset_; }
std::size_t PostDeltaBoundary::remaining_byte_count() const noexcept { return remaining_byte_count_; }
PostDeltaBoundaryCategory PostDeltaBoundary::category() const noexcept { return category_; }
PostDeltaBoundaryEvidenceStatus PostDeltaBoundary::evidence_status() const noexcept { return evidence_status_; }

DeltaDescriptionStreamDecoder::DeltaDescriptionStreamDecoder(
    DeltaDescriptionLimits limits,
    const DeltaCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool DeltaDescriptionStreamDecoder::valid_configuration() const noexcept
{
    return valid_delta_description_limits(limits_);
}

DeltaDescriptionStreamDecodeResult DeltaDescriptionStreamDecoder::decode(
    const std::span<const std::byte> service_payload,
    const ResourcePhaseBoundary& initial_boundary) const
{
    if (!valid_configuration()) {
        return stream_failure(
            DeltaDescriptionStreamErrorCode::invalid_configuration,
            0U,
            0U,
            std::nullopt,
            std::nullopt,
            "Delta stream limits are outside project hard caps");
    }
    if (initial_boundary.direction() != ResourcePhaseBoundaryDirection::server_message ||
        initial_boundary.opcode() != kDeltaDescriptionOpcode ||
        initial_boundary.byte_offset() >= service_payload.size()) {
        return stream_failure(
            initial_boundary.opcode() != kDeltaDescriptionOpcode
                ? DeltaDescriptionStreamErrorCode::wrong_initial_opcode
                : DeltaDescriptionStreamErrorCode::invalid_boundary_geometry,
            initial_boundary.byte_offset(),
            initial_boundary.byte_offset() * 8U,
            std::nullopt,
            std::nullopt,
            "Pre-resource continuation does not identify the exact opcode-14 boundary");
    }
    const auto expected_remaining =
        service_payload.size() - initial_boundary.byte_offset() - 1U;
    if (initial_boundary.remaining_byte_count() != expected_remaining) {
        return stream_failure(
            DeltaDescriptionStreamErrorCode::invalid_boundary_geometry,
            initial_boundary.byte_offset(),
            initial_boundary.byte_offset() * 8U,
            std::nullopt,
            std::nullopt,
            "Pre-resource remaining-byte count does not match the owning payload");
    }

    DeltaDescriptionParser parser{limits_, profile_};
    DeltaSchemaRegistryBuilder builder{limits_};
    auto cursor = initial_boundary.byte_offset();
    std::size_t message_count = 0U;
    while (true) {
        if (cursor >= service_payload.size()) {
            return stream_failure(
                DeltaDescriptionStreamErrorCode::missing_post_delta_boundary,
                cursor,
                cursor * 8U,
                std::nullopt,
                std::nullopt,
                "Delta schema stream ends without a following service opcode");
        }
        const auto opcode = std::to_integer<std::uint8_t>(service_payload[cursor]);
        if (opcode != kDeltaDescriptionOpcode) {
            break;
        }
        if (message_count >= limits_.maximum_schema_count) {
            return stream_failure(
                DeltaDescriptionStreamErrorCode::message_count_limit_exceeded,
                cursor,
                cursor * 8U,
                std::nullopt,
                std::nullopt,
                "Delta schema message count exceeds the configured bound");
        }

        auto parsed = parser.parse(service_payload, cursor);
        if (!parsed || !parsed.schema) {
            return stream_failure(
                DeltaDescriptionStreamErrorCode::parser_failure,
                parsed.error ? parsed.error->byte_offset : cursor,
                parsed.error ? parsed.error->bit_offset : cursor * 8U,
                parsed.error ? std::optional{parsed.error->code} : std::nullopt,
                std::nullopt,
                parsed.error ? parsed.error->context
                             : "Delta parser returned no candidate or diagnostic");
        }
        auto inserted = builder.insert(*parsed.schema);
        if (!inserted) {
            return stream_failure(
                DeltaDescriptionStreamErrorCode::registry_failure,
                cursor,
                cursor * 8U,
                std::nullopt,
                inserted.error ? std::optional{inserted.error->code} : std::nullopt,
                inserted.error ? inserted.error->context
                               : "Delta registry rejected a schema without a diagnostic");
        }
        if (parsed.next_bit_offset != 0U || parsed.next_byte_offset <= cursor) {
            return stream_failure(
                DeltaDescriptionStreamErrorCode::size_overflow,
                cursor,
                cursor * 8U,
                std::nullopt,
                std::nullopt,
                "Delta parser returned a non-advancing or unaligned cursor");
        }
        cursor = parsed.next_byte_offset;
        ++message_count;
    }

    if (message_count == 0U) {
        return stream_failure(
            DeltaDescriptionStreamErrorCode::wrong_initial_opcode,
            cursor,
            cursor * 8U,
            std::nullopt,
            std::nullopt,
            "Delta stream contains no opcode-14 schema message");
    }
    if (cursor >= service_payload.size() ||
        service_payload.size() - cursor < 2U) {
        const bool truncated_post_delta_boundary =
            cursor < service_payload.size() &&
            std::to_integer<std::uint8_t>(service_payload[cursor]) ==
                kStockPostDeltaBoundaryOpcode;
        return stream_failure(
            truncated_post_delta_boundary
                ? DeltaDescriptionStreamErrorCode::malformed_post_delta_boundary
                : DeltaDescriptionStreamErrorCode::missing_post_delta_boundary,
            cursor,
            cursor * 8U,
            std::nullopt,
            std::nullopt,
            "Post-delta boundary must retain at least one unconsumed body byte");
    }

    const auto boundary_opcode = std::to_integer<std::uint8_t>(service_payload[cursor]);
    const bool stock_boundary =
        boundary_opcode == kStockPostDeltaBoundaryOpcode;
    auto boundary = PostDeltaBoundary{
        boundary_opcode,
        cursor,
        0U,
        service_payload.size() - cursor - 1U,
        stock_boundary ? PostDeltaBoundaryCategory::stock_observed_opcode_44
                       : PostDeltaBoundaryCategory::neutral_message,
        stock_boundary
            ? PostDeltaBoundaryEvidenceStatus::
                  stock_confirmed_opcode_44_body_unconsumed
            : PostDeltaBoundaryEvidenceStatus::synthetic_neutral_boundary,
    };
    if (message_count > std::numeric_limits<std::size_t>::max() - 2U) {
        return stream_failure(
            DeltaDescriptionStreamErrorCode::size_overflow,
            cursor,
            cursor * 8U,
            std::nullopt,
            std::nullopt,
            "Delta event count overflowed");
    }
    const auto bytes_consumed = cursor - initial_boundary.byte_offset();
    if (bytes_consumed > std::numeric_limits<std::size_t>::max() / 8U) {
        return stream_failure(
            DeltaDescriptionStreamErrorCode::size_overflow,
            cursor,
            cursor * 8U,
            std::nullopt,
            std::nullopt,
            "Delta stream bit count overflowed");
    }

    return DeltaDescriptionStreamDecodeResult{
        DeltaDescriptionStreamState{
            std::move(builder).publish(),
            std::move(boundary),
            message_count,
            bytes_consumed * 8U,
            bytes_consumed,
        },
        std::nullopt,
        message_count + 2U,
    };
}

} // namespace hlclient::goldsrc
