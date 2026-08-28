#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::collision {

inline constexpr std::int32_t kGoldSrcMinimumContentsCode = -15;
inline constexpr std::int32_t kGoldSrcMaximumContentsCode = -1;

// Exact signed terminal retained from the validated BSP source. Construction
// remains public so builder and defensive-query tests can represent malformed
// inputs; decode_goldsrc_contents is the validation boundary.
struct GoldSrcContentsCode {
    std::int32_t raw{-1};

    [[nodiscard]] friend bool operator==(
        const GoldSrcContentsCode&,
        const GoldSrcContentsCode&) = default;
};

enum class CollisionContentsCategory : std::uint8_t {
    empty,
    solid,
    water,
    slime,
    lava,
    sky,
    origin,
    clip,
    current_0,
    current_90,
    current_180,
    current_270,
    current_up,
    current_down,
    translucent,
};

[[nodiscard]] std::string_view to_string(
    CollisionContentsCategory category) noexcept;

struct CollisionContents {
    GoldSrcContentsCode source{};
    CollisionContentsCategory category{CollisionContentsCategory::empty};

    [[nodiscard]] friend bool operator==(
        const CollisionContents&,
        const CollisionContents&) = default;
};

[[nodiscard]] bool supported_goldsrc_contents_code(
    GoldSrcContentsCode code) noexcept;

[[nodiscard]] std::optional<CollisionContents> decode_goldsrc_contents(
    GoldSrcContentsCode code) noexcept;

[[nodiscard]] bool is_solid_geometry(
    CollisionContentsCategory category) noexcept;
[[nodiscard]] bool is_liquid(CollisionContentsCategory category) noexcept;
[[nodiscard]] bool is_current(CollisionContentsCategory category) noexcept;
[[nodiscard]] bool is_open_space(
    CollisionContentsCategory category) noexcept;
[[nodiscard]] bool is_special(CollisionContentsCategory category) noexcept;

enum class CollisionContentsPolicy : std::uint8_t {
    project_solid_only_v1,
    stock_player_trace_contents_policy_pending,
};

[[nodiscard]] std::string_view to_string(
    CollisionContentsPolicy policy) noexcept;
[[nodiscard]] bool supported_collision_contents_policy(
    CollisionContentsPolicy policy) noexcept;
[[nodiscard]] bool blocks(
    CollisionContentsPolicy policy,
    CollisionContentsCategory category) noexcept;

} // namespace hlclient::collision
