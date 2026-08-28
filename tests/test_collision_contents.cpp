#include <hlclient/collision/collision_contents.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

namespace {

namespace collision = hlclient::collision;

TEST_CASE("GoldSrc collision contents preserve every BSP v30 terminal",
    "[collision][contents]")
{
    constexpr std::array expected{
        collision::CollisionContentsCategory::empty,
        collision::CollisionContentsCategory::solid,
        collision::CollisionContentsCategory::water,
        collision::CollisionContentsCategory::slime,
        collision::CollisionContentsCategory::lava,
        collision::CollisionContentsCategory::sky,
        collision::CollisionContentsCategory::origin,
        collision::CollisionContentsCategory::clip,
        collision::CollisionContentsCategory::current_0,
        collision::CollisionContentsCategory::current_90,
        collision::CollisionContentsCategory::current_180,
        collision::CollisionContentsCategory::current_270,
        collision::CollisionContentsCategory::current_up,
        collision::CollisionContentsCategory::current_down,
        collision::CollisionContentsCategory::translucent,
    };

    for (std::size_t index = 0U; index < expected.size(); ++index) {
        const auto raw = -1 - static_cast<std::int32_t>(index);
        const auto decoded = collision::decode_goldsrc_contents({raw});
        REQUIRE(decoded);
        CHECK(decoded->source.raw == raw);
        CHECK(decoded->category == expected[index]);
        CHECK(collision::supported_goldsrc_contents_code(decoded->source));
        CHECK(collision::to_string(decoded->category) != "unknown");
    }

    CHECK_FALSE(collision::decode_goldsrc_contents({0}));
    CHECK_FALSE(collision::decode_goldsrc_contents({-16}));
    CHECK_FALSE(collision::supported_goldsrc_contents_code({0}));
    CHECK_FALSE(collision::supported_goldsrc_contents_code({-16}));
}

TEST_CASE("Collision contents helpers retain conservative category semantics",
    "[collision][contents][policy]")
{
    using Category = collision::CollisionContentsCategory;
    CHECK(collision::is_open_space(Category::empty));
    CHECK_FALSE(collision::is_open_space(Category::water));
    CHECK(collision::is_solid_geometry(Category::solid));
    CHECK_FALSE(collision::is_solid_geometry(Category::clip));

    CHECK(collision::is_liquid(Category::water));
    CHECK(collision::is_liquid(Category::slime));
    CHECK(collision::is_liquid(Category::lava));
    CHECK(collision::is_liquid(Category::current_down));
    CHECK(collision::is_current(Category::current_0));
    CHECK_FALSE(collision::is_current(Category::water));

    CHECK(collision::is_special(Category::sky));
    CHECK(collision::is_special(Category::origin));
    CHECK(collision::is_special(Category::clip));
    CHECK(collision::is_special(Category::translucent));
    CHECK_FALSE(collision::is_special(Category::solid));
}

TEST_CASE("Project solid-only policy never coerces liquids or special contents",
    "[collision][contents][blocking]")
{
    using Category = collision::CollisionContentsCategory;
    constexpr auto policy =
        collision::CollisionContentsPolicy::project_solid_only_v1;
    CHECK(collision::supported_collision_contents_policy(policy));
    CHECK(collision::blocks(policy, Category::solid));
    CHECK_FALSE(collision::blocks(policy, Category::empty));
    CHECK_FALSE(collision::blocks(policy, Category::water));
    CHECK_FALSE(collision::blocks(policy, Category::slime));
    CHECK_FALSE(collision::blocks(policy, Category::lava));
    CHECK_FALSE(collision::blocks(policy, Category::clip));
    CHECK_FALSE(collision::blocks(policy, Category::sky));
    CHECK_FALSE(collision::blocks(policy, Category::translucent));

    constexpr auto pending = collision::CollisionContentsPolicy::
        stock_player_trace_contents_policy_pending;
    CHECK_FALSE(collision::supported_collision_contents_policy(pending));
    CHECK_FALSE(collision::blocks(pending, Category::solid));
}

} // namespace
