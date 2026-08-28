#include <hlclient/collision/collision_contents.hpp>

namespace hlclient::collision {

std::string_view to_string(const CollisionContentsCategory category) noexcept
{
    switch (category) {
    case CollisionContentsCategory::empty: return "empty";
    case CollisionContentsCategory::solid: return "solid";
    case CollisionContentsCategory::water: return "water";
    case CollisionContentsCategory::slime: return "slime";
    case CollisionContentsCategory::lava: return "lava";
    case CollisionContentsCategory::sky: return "sky";
    case CollisionContentsCategory::origin: return "origin";
    case CollisionContentsCategory::clip: return "clip";
    case CollisionContentsCategory::current_0: return "current_0";
    case CollisionContentsCategory::current_90: return "current_90";
    case CollisionContentsCategory::current_180: return "current_180";
    case CollisionContentsCategory::current_270: return "current_270";
    case CollisionContentsCategory::current_up: return "current_up";
    case CollisionContentsCategory::current_down: return "current_down";
    case CollisionContentsCategory::translucent: return "translucent";
    }
    return "unknown";
}

bool supported_goldsrc_contents_code(const GoldSrcContentsCode code) noexcept
{
    return code.raw >= kGoldSrcMinimumContentsCode &&
        code.raw <= kGoldSrcMaximumContentsCode;
}

std::optional<CollisionContents> decode_goldsrc_contents(
    const GoldSrcContentsCode code) noexcept
{
    switch (code.raw) {
    case -1: return CollisionContents{code, CollisionContentsCategory::empty};
    case -2: return CollisionContents{code, CollisionContentsCategory::solid};
    case -3: return CollisionContents{code, CollisionContentsCategory::water};
    case -4: return CollisionContents{code, CollisionContentsCategory::slime};
    case -5: return CollisionContents{code, CollisionContentsCategory::lava};
    case -6: return CollisionContents{code, CollisionContentsCategory::sky};
    case -7: return CollisionContents{code, CollisionContentsCategory::origin};
    case -8: return CollisionContents{code, CollisionContentsCategory::clip};
    case -9:
        return CollisionContents{code, CollisionContentsCategory::current_0};
    case -10:
        return CollisionContents{code, CollisionContentsCategory::current_90};
    case -11:
        return CollisionContents{code, CollisionContentsCategory::current_180};
    case -12:
        return CollisionContents{code, CollisionContentsCategory::current_270};
    case -13:
        return CollisionContents{code, CollisionContentsCategory::current_up};
    case -14:
        return CollisionContents{code, CollisionContentsCategory::current_down};
    case -15:
        return CollisionContents{code, CollisionContentsCategory::translucent};
    default: return std::nullopt;
    }
}

bool is_solid_geometry(const CollisionContentsCategory category) noexcept
{
    return category == CollisionContentsCategory::solid;
}

bool is_liquid(const CollisionContentsCategory category) noexcept
{
    switch (category) {
    case CollisionContentsCategory::water:
    case CollisionContentsCategory::slime:
    case CollisionContentsCategory::lava:
    case CollisionContentsCategory::current_0:
    case CollisionContentsCategory::current_90:
    case CollisionContentsCategory::current_180:
    case CollisionContentsCategory::current_270:
    case CollisionContentsCategory::current_up:
    case CollisionContentsCategory::current_down: return true;
    default: return false;
    }
}

bool is_current(const CollisionContentsCategory category) noexcept
{
    switch (category) {
    case CollisionContentsCategory::current_0:
    case CollisionContentsCategory::current_90:
    case CollisionContentsCategory::current_180:
    case CollisionContentsCategory::current_270:
    case CollisionContentsCategory::current_up:
    case CollisionContentsCategory::current_down: return true;
    default: return false;
    }
}

bool is_open_space(const CollisionContentsCategory category) noexcept
{
    return category == CollisionContentsCategory::empty;
}

bool is_special(const CollisionContentsCategory category) noexcept
{
    switch (category) {
    case CollisionContentsCategory::sky:
    case CollisionContentsCategory::origin:
    case CollisionContentsCategory::clip:
    case CollisionContentsCategory::translucent: return true;
    default: return false;
    }
}

std::string_view to_string(const CollisionContentsPolicy policy) noexcept
{
    switch (policy) {
    case CollisionContentsPolicy::project_solid_only_v1:
        return "project_solid_only_v1";
    case CollisionContentsPolicy::stock_player_trace_contents_policy_pending:
        return "stock_player_trace_contents_policy_pending";
    }
    return "unknown";
}

bool supported_collision_contents_policy(
    const CollisionContentsPolicy policy) noexcept
{
    return policy == CollisionContentsPolicy::project_solid_only_v1;
}

bool blocks(
    const CollisionContentsPolicy policy,
    const CollisionContentsCategory category) noexcept
{
    return supported_collision_contents_policy(policy) &&
        is_solid_geometry(category);
}

} // namespace hlclient::collision
