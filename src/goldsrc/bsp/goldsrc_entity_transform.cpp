#include <hlclient/goldsrc/bsp/goldsrc_entity_transform.hpp>

#include <array>
#include <charconv>
#include <cmath>

namespace hlclient::goldsrc::bsp {
namespace {

[[nodiscard]] bool ascii_whitespace(const char value) noexcept
{
    const auto byte = static_cast<unsigned char>(value);
    return byte == 0x20U || (byte >= 0x09U && byte <= 0x0DU);
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
            return {std::nullopt,
                GoldSrcEntityNumberErrorCode::invalid_decimal};
        }
    }
    float parsed = 0.0F;
    const auto result = std::from_chars(
        token.data(), token.data() + token.size(), parsed,
        std::chars_format::general);
    if (result.ec == std::errc::result_out_of_range) {
        return {std::nullopt, GoldSrcEntityNumberErrorCode::non_finite};
    }
    if (result.ec != std::errc{} ||
        result.ptr != token.data() + token.size()) {
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
            return {std::nullopt,
                GoldSrcEntityNumberErrorCode::extra_component};
        }
    }
    return parse_float_token(value);
}

} // namespace

std::string_view to_string(const GoldSrcEntityNumberErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcEntityNumberErrorCode::missing_component:
        return "missing_component";
    case GoldSrcEntityNumberErrorCode::extra_component:
        return "extra_component";
    case GoldSrcEntityNumberErrorCode::invalid_decimal:
        return "invalid_decimal";
    case GoldSrcEntityNumberErrorCode::non_finite:
        return "non_finite";
    }
    return "unknown";
}

GoldSrcEntityVectorResult parse_entity_vector3(
    const std::string_view value) noexcept
{
    std::array<float, 3U> components{};
    std::size_t cursor = 0U;
    for (std::size_t component = 0U;
         component < components.size(); ++component) {
        while (cursor < value.size() && ascii_whitespace(value[cursor])) {
            ++cursor;
        }
        if (cursor == value.size()) {
            return {std::nullopt,
                GoldSrcEntityNumberErrorCode::missing_component};
        }
        const auto begin = cursor;
        while (cursor < value.size() && !ascii_whitespace(value[cursor])) {
            ++cursor;
        }
        const auto parsed =
            parse_float_token(value.substr(begin, cursor - begin));
        if (!parsed.value) {
            return {std::nullopt, parsed.error};
        }
        components[component] = *parsed.value;
    }
    while (cursor < value.size() && ascii_whitespace(value[cursor])) {
        ++cursor;
    }
    if (cursor != value.size()) {
        return {std::nullopt,
            GoldSrcEntityNumberErrorCode::extra_component};
    }
    return {assets::AssetVector3{
                components[0U], components[1U], components[2U]},
        std::nullopt};
}

GoldSrcEntityAnglesResult parse_entity_angles(
    const std::optional<std::string_view> angles,
    const std::optional<std::string_view> angle) noexcept
{
    if (angles) {
        const auto parsed = parse_entity_vector3(*angles);
        if (!parsed) {
            return {std::nullopt, parsed.error,
                GoldSrcEntityAnglesSource::angles_vector};
        }
        return {*parsed.value, std::nullopt,
            GoldSrcEntityAnglesSource::angles_vector};
    }
    if (!angle) {
        return {assets::AssetVector3{}, std::nullopt,
            GoldSrcEntityAnglesSource::default_zero};
    }
    const auto parsed = parse_single_float(*angle);
    if (!parsed.value) {
        return {std::nullopt, parsed.error,
            GoldSrcEntityAnglesSource::angle_yaw};
    }
    if (*parsed.value == -1.0F) {
        return {assets::AssetVector3{-90.0F, 0.0F, 0.0F}, std::nullopt,
            GoldSrcEntityAnglesSource::angle_up};
    }
    if (*parsed.value == -2.0F) {
        return {assets::AssetVector3{90.0F, 0.0F, 0.0F}, std::nullopt,
            GoldSrcEntityAnglesSource::angle_down};
    }
    return {assets::AssetVector3{0.0F, *parsed.value, 0.0F}, std::nullopt,
        GoldSrcEntityAnglesSource::angle_yaw};
}

} // namespace hlclient::goldsrc::bsp
