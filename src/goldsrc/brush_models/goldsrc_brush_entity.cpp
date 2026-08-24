#include <hlclient/goldsrc/brush_models/goldsrc_brush_entity.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace hlclient::goldsrc::brush_models {
namespace {

[[nodiscard]] bool ascii_whitespace(const char value) noexcept
{
    const auto byte = static_cast<unsigned char>(value);
    return byte == 0x20U || (byte >= 0x09U && byte <= 0x0DU);
}

[[nodiscard]] char lowercase_ascii(const char value) noexcept
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

[[nodiscard]] std::string_view trim_ascii(std::string_view value) noexcept
{
    while (!value.empty() && ascii_whitespace(value.front())) {
        value.remove_prefix(1U);
    }
    while (!value.empty() && ascii_whitespace(value.back())) {
        value.remove_suffix(1U);
    }
    return value;
}

struct FloatResult {
    std::optional<float> value;
    GoldSrcEntityNumberErrorCode error{
        GoldSrcEntityNumberErrorCode::invalid_decimal};
};

[[nodiscard]] FloatResult parse_float_token(std::string_view token) noexcept
{
    if (token.empty()) {
        return {std::nullopt, GoldSrcEntityNumberErrorCode::missing_component};
    }
    if (token.front() == '+') {
        token.remove_prefix(1U);
        if (token.empty()) {
            return {std::nullopt, GoldSrcEntityNumberErrorCode::invalid_decimal};
        }
    }
    float parsed = 0.0F;
    const auto result = std::from_chars(
        token.data(), token.data() + token.size(), parsed, std::chars_format::general);
    if (result.ec == std::errc::result_out_of_range) {
        return {std::nullopt, GoldSrcEntityNumberErrorCode::non_finite};
    }
    if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
        return {std::nullopt, GoldSrcEntityNumberErrorCode::invalid_decimal};
    }
    if (!std::isfinite(parsed)) {
        return {std::nullopt, GoldSrcEntityNumberErrorCode::non_finite};
    }
    return {parsed, GoldSrcEntityNumberErrorCode::invalid_decimal};
}

[[nodiscard]] FloatResult parse_single_float(std::string_view value) noexcept
{
    value = trim_ascii(value);
    if (value.empty()) {
        return {std::nullopt, GoldSrcEntityNumberErrorCode::missing_component};
    }
    for (const auto character : value) {
        if (ascii_whitespace(character)) {
            return {std::nullopt, GoldSrcEntityNumberErrorCode::extra_component};
        }
    }
    return parse_float_token(value);
}

[[nodiscard]] std::optional<std::int32_t> parse_int32(
    std::string_view value) noexcept
{
    value = trim_ascii(value);
    if (value.empty()) {
        return std::nullopt;
    }
    bool positive_sign = false;
    if (value.front() == '+') {
        positive_sign = true;
        value.remove_prefix(1U);
        if (value.empty()) {
            return std::nullopt;
        }
    }
    std::int64_t parsed = 0;
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed, 10);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        parsed < static_cast<std::int64_t>(
            std::numeric_limits<std::int32_t>::min()) ||
        parsed > static_cast<std::int64_t>(
            std::numeric_limits<std::int32_t>::max())) {
        return std::nullopt;
    }
    if (positive_sign && parsed < 0) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(parsed);
}

[[nodiscard]] bool begins_func_prefix(const std::string_view value) noexcept
{
    constexpr std::string_view prefix{"func_"};
    if (value.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < prefix.size(); ++index) {
        if (lowercase_ascii(value[index]) != prefix[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ambiguous(
    const bsp::GoldSrcInterpretedKeyLookup& lookup) noexcept
{
    return lookup.status == bsp::GoldSrcInterpretedKeyStatus::exact_duplicate ||
        lookup.status == bsp::GoldSrcInterpretedKeyStatus::ascii_case_collision;
}

} // namespace

std::string_view to_string(const BrushSubmodelInstanceStatus status) noexcept
{
    switch (status) {
    case BrushSubmodelInstanceStatus::supported_static_opaque:
        return "supported_static_opaque";
    case BrushSubmodelInstanceStatus::unsupported_transform:
        return "unsupported_transform";
    case BrushSubmodelInstanceStatus::unsupported_rendermode:
        return "unsupported_rendermode";
    case BrushSubmodelInstanceStatus::invalid_model_reference:
        return "invalid_model_reference";
    case BrushSubmodelInstanceStatus::missing_model_geometry:
        return "missing_model_geometry";
    case BrushSubmodelInstanceStatus::invalid_entity_metadata:
        return "invalid_entity_metadata";
    case BrushSubmodelInstanceStatus::outside_world_spatial_tree:
        return "outside_world_spatial_tree";
    case BrushSubmodelInstanceStatus::no_visible_leaf_membership:
        return "no_visible_leaf_membership";
    }
    return "unknown";
}

std::string_view to_string(
    const GoldSrcBrushModelReferenceErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcBrushModelReferenceErrorCode::not_brush_reference:
        return "not_brush_reference";
    case GoldSrcBrushModelReferenceErrorCode::world_model_reference:
        return "world_model_reference";
    case GoldSrcBrushModelReferenceErrorCode::invalid_syntax:
        return "invalid_syntax";
    case GoldSrcBrushModelReferenceErrorCode::index_overflow:
        return "index_overflow";
    case GoldSrcBrushModelReferenceErrorCode::index_out_of_range:
        return "index_out_of_range";
    }
    return "unknown";
}

GoldSrcBrushModelReferenceResult parse_brush_model_reference(
    const std::string_view value,
    const std::size_t source_model_count) noexcept
{
    if (value.empty() || value.front() != '*') {
        return {
            std::nullopt,
            GoldSrcBrushModelReferenceErrorCode::not_brush_reference,
        };
    }
    if (value.size() == 1U) {
        return {
            std::nullopt,
            GoldSrcBrushModelReferenceErrorCode::invalid_syntax,
        };
    }
    std::uint32_t index = 0U;
    for (std::size_t position = 1U; position < value.size(); ++position) {
        const auto character = value[position];
        if (character < '0' || character > '9') {
            return {
                std::nullopt,
                GoldSrcBrushModelReferenceErrorCode::invalid_syntax,
            };
        }
        const auto digit = static_cast<std::uint32_t>(character - '0');
        if (index > (std::numeric_limits<std::uint32_t>::max() - digit) / 10U) {
            return {
                std::nullopt,
                GoldSrcBrushModelReferenceErrorCode::index_overflow,
            };
        }
        index = index * 10U + digit;
    }
    if (index == 0U) {
        return {
            std::nullopt,
            GoldSrcBrushModelReferenceErrorCode::world_model_reference,
        };
    }
    if (static_cast<std::size_t>(index) >= source_model_count) {
        return {
            std::nullopt,
            GoldSrcBrushModelReferenceErrorCode::index_out_of_range,
        };
    }
    return {index, std::nullopt};
}

std::string_view to_string(const GoldSrcEntityNumberErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcEntityNumberErrorCode::missing_component:
        return "missing_component";
    case GoldSrcEntityNumberErrorCode::extra_component:
        return "extra_component";
    case GoldSrcEntityNumberErrorCode::invalid_decimal:
        return "invalid_decimal";
    case GoldSrcEntityNumberErrorCode::non_finite: return "non_finite";
    }
    return "unknown";
}

GoldSrcEntityVectorResult parse_entity_vector3(std::string_view value) noexcept
{
    std::array<float, 3U> components{};
    std::size_t cursor = 0U;
    for (std::size_t component = 0U; component < components.size(); ++component) {
        while (cursor < value.size() && ascii_whitespace(value[cursor])) {
            ++cursor;
        }
        if (cursor == value.size()) {
            return {
                std::nullopt,
                GoldSrcEntityNumberErrorCode::missing_component,
            };
        }
        const auto begin = cursor;
        while (cursor < value.size() && !ascii_whitespace(value[cursor])) {
            ++cursor;
        }
        const auto parsed = parse_float_token(value.substr(begin, cursor - begin));
        if (!parsed.value) {
            return {std::nullopt, parsed.error};
        }
        components[component] = *parsed.value;
    }
    while (cursor < value.size() && ascii_whitespace(value[cursor])) {
        ++cursor;
    }
    if (cursor != value.size()) {
        return {
            std::nullopt,
            GoldSrcEntityNumberErrorCode::extra_component,
        };
    }
    return {
        assets::AssetVector3{components[0U], components[1U], components[2U]},
        std::nullopt,
    };
}

GoldSrcEntityAnglesResult parse_entity_angles(
    const std::optional<std::string_view> angles,
    const std::optional<std::string_view> angle) noexcept
{
    if (angles) {
        const auto parsed = parse_entity_vector3(*angles);
        if (!parsed) {
            return {std::nullopt, parsed.error, GoldSrcEntityAnglesSource::angles_vector};
        }
        return {*parsed.value, std::nullopt, GoldSrcEntityAnglesSource::angles_vector};
    }
    if (!angle) {
        return {
            assets::AssetVector3{},
            std::nullopt,
            GoldSrcEntityAnglesSource::default_zero,
        };
    }
    const auto parsed = parse_single_float(*angle);
    if (!parsed.value) {
        return {std::nullopt, parsed.error, GoldSrcEntityAnglesSource::angle_yaw};
    }
    if (*parsed.value == -1.0F) {
        return {
            assets::AssetVector3{-90.0F, 0.0F, 0.0F},
            std::nullopt,
            GoldSrcEntityAnglesSource::angle_up,
        };
    }
    if (*parsed.value == -2.0F) {
        return {
            assets::AssetVector3{90.0F, 0.0F, 0.0F},
            std::nullopt,
            GoldSrcEntityAnglesSource::angle_down,
        };
    }
    return {
        assets::AssetVector3{0.0F, *parsed.value, 0.0F},
        std::nullopt,
        GoldSrcEntityAnglesSource::angle_yaw,
    };
}

GoldSrcBrushEntityInterpretation interpret_brush_entity(
    const bsp::GoldSrcEntityRecord& entity,
    const std::size_t source_entity_ordinal,
    const std::size_t source_model_count) noexcept
{
    using Key = bsp::GoldSrcInterpretedEntityKey;
    const auto classname = bsp::find_interpreted_key(entity, Key::classname);
    const auto model = bsp::find_interpreted_key(entity, Key::model);
    const auto origin = bsp::find_interpreted_key(entity, Key::origin);
    const auto angles = bsp::find_interpreted_key(entity, Key::angles);
    const auto angle = bsp::find_interpreted_key(entity, Key::angle);
    const auto rendermode = bsp::find_interpreted_key(entity, Key::rendermode);
    const auto renderamt = bsp::find_interpreted_key(entity, Key::renderamt);

    if (model.status == bsp::GoldSrcInterpretedKeyStatus::absent) {
        return {};
    }

    GoldSrcBrushEntityMetadata metadata;
    metadata.source_entity_ordinal = source_entity_ordinal;
    if (ambiguous(model)) {
        metadata.status = BrushSubmodelInstanceStatus::invalid_entity_metadata;
        return {metadata};
    }
    const auto* model_pair = model.unique_pair(entity);
    if (model_pair == nullptr) {
        metadata.status = BrushSubmodelInstanceStatus::invalid_entity_metadata;
        return {metadata};
    }
    if (model_pair->value.empty() || model_pair->value.front() != '*') {
        return {};
    }

    const auto model_reference =
        parse_brush_model_reference(model_pair->value, source_model_count);
    if (!model_reference) {
        metadata.status = BrushSubmodelInstanceStatus::invalid_model_reference;
        return {metadata};
    }
    metadata.source_model_index = model_reference.source_model_index;

    const std::array lookups{
        classname,
        origin,
        angles,
        angle,
        rendermode,
        renderamt,
    };
    for (const auto& lookup : lookups) {
        if (ambiguous(lookup)) {
            metadata.status = BrushSubmodelInstanceStatus::invalid_entity_metadata;
            return {metadata};
        }
    }

    if (const auto* pair = classname.unique_pair(entity); pair != nullptr) {
        metadata.classname_category = begins_func_prefix(pair->value)
            ? GoldSrcBrushClassnameCategory::function_entity
            : GoldSrcBrushClassnameCategory::other;
    }

    if (const auto* pair = origin.unique_pair(entity); pair != nullptr) {
        const auto parsed_origin = parse_entity_vector3(pair->value);
        if (!parsed_origin) {
            metadata.status = BrushSubmodelInstanceStatus::unsupported_transform;
            return {metadata};
        }
        metadata.origin = *parsed_origin.value;
    }

    std::optional<std::string_view> angles_value;
    std::optional<std::string_view> angle_value;
    if (const auto* pair = angles.unique_pair(entity); pair != nullptr) {
        angles_value = pair->value;
    }
    if (const auto* pair = angle.unique_pair(entity); pair != nullptr) {
        angle_value = pair->value;
    }
    const auto parsed_angles = parse_entity_angles(angles_value, angle_value);
    if (!parsed_angles) {
        metadata.status = BrushSubmodelInstanceStatus::unsupported_transform;
        return {metadata};
    }
    metadata.angles_degrees = *parsed_angles.degrees;
    metadata.angles_source = parsed_angles.source;

    if (const auto* pair = rendermode.unique_pair(entity); pair != nullptr) {
        const auto parsed_rendermode = parse_int32(pair->value);
        if (!parsed_rendermode) {
            metadata.status = BrushSubmodelInstanceStatus::invalid_entity_metadata;
            return {metadata};
        }
        metadata.rendermode = *parsed_rendermode;
    }
    if (const auto* pair = renderamt.unique_pair(entity); pair != nullptr) {
        const auto parsed_render_amount = parse_single_float(pair->value);
        if (!parsed_render_amount.value) {
            metadata.status = BrushSubmodelInstanceStatus::invalid_entity_metadata;
            return {metadata};
        }
        metadata.render_amount = *parsed_render_amount.value;
    }
    metadata.status = metadata.rendermode == 0
        ? BrushSubmodelInstanceStatus::supported_static_opaque
        : BrushSubmodelInstanceStatus::unsupported_rendermode;
    return {metadata};
}

} // namespace hlclient::goldsrc::brush_models
