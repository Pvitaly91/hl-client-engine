#pragma once

#include <hlclient/assets/asset_types.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc::bsp {

enum class GoldSrcEntityNumberErrorCode : std::uint8_t {
    missing_component,
    extra_component,
    invalid_decimal,
    non_finite,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcEntityNumberErrorCode code) noexcept;

struct GoldSrcEntityVectorResult {
    std::optional<assets::AssetVector3> value;
    std::optional<GoldSrcEntityNumberErrorCode> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value();
    }
};

// Parses exactly three finite, locale-independent decimal values separated by
// bounded ASCII whitespace. Commas, missing fields and suffixes are rejected.
[[nodiscard]] GoldSrcEntityVectorResult parse_entity_vector3(
    std::string_view value) noexcept;

enum class GoldSrcEntityAnglesSource : std::uint8_t {
    default_zero,
    angles_vector,
    angle_yaw,
    angle_up,
    angle_down,
};

struct GoldSrcEntityAnglesResult {
    std::optional<assets::AssetVector3> degrees;
    std::optional<GoldSrcEntityNumberErrorCode> error;
    GoldSrcEntityAnglesSource source{GoldSrcEntityAnglesSource::default_zero};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return degrees.has_value();
    }
};

// `angles` has priority. A normal `angle` is yaw. Pinned ANGLE_UP=-1 and
// ANGLE_DOWN=-2 map to pitch -90/+90 so the Valve AngleMatrix forward axis is
// +Z/-Z (utils/common/bspfile.h:224-225,
// utils/qrad/lightmap.c:965-990).
[[nodiscard]] GoldSrcEntityAnglesResult parse_entity_angles(
    std::optional<std::string_view> angles,
    std::optional<std::string_view> angle) noexcept;

} // namespace hlclient::goldsrc::bsp
