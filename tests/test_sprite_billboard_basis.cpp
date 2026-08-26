#include <hlclient/goldsrc/sprite/goldsrc_sprite_playback.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

namespace assets = hlclient::assets;
namespace sprite = hlclient::goldsrc::sprite;
using Catch::Approx;

TEST_CASE("Sprite billboard supports the three evidenced orientations",
          "[goldsrc-sprite][billboard][basis]")
{
    sprite::SpriteBillboardInput input;
    const auto parallel = sprite::make_sprite_billboard_basis(input);
    REQUIRE(parallel);
    CHECK(parallel.basis->right.y == Approx(-1.0F));
    CHECK(parallel.basis->up.z == Approx(1.0F));
    CHECK(parallel.basis->normal.x == Approx(-1.0F));

    input.orientation = assets::SpriteOrientation::view_parallel_upright;
    const auto upright = sprite::make_sprite_billboard_basis(input);
    REQUIRE(upright);
    CHECK(upright.basis->right.y == Approx(-1.0F));
    CHECK(upright.basis->up.z == Approx(1.0F));

    input.orientation = assets::SpriteOrientation::oriented;
    const auto oriented = sprite::make_sprite_billboard_basis(input);
    REQUIRE(oriented);
    CHECK(oriented.basis->normal.x == Approx(1.0F));
}

TEST_CASE("Sprite billboard rejects unsupported and degenerate modes",
          "[goldsrc-sprite][billboard][unsupported]")
{
    sprite::SpriteBillboardInput input;
    input.orientation = assets::SpriteOrientation::facing_upright;
    const auto facing = sprite::make_sprite_billboard_basis(input);
    REQUIRE_FALSE(facing);
    CHECK(facing.error->code ==
          sprite::SpritePlaybackErrorCode::unsupported_orientation);

    input.orientation = assets::SpriteOrientation::view_parallel_oriented;
    const auto rotated = sprite::make_sprite_billboard_basis(input);
    REQUIRE_FALSE(rotated);
    CHECK(rotated.error->code ==
          sprite::SpritePlaybackErrorCode::unsupported_orientation);

    input.orientation = assets::SpriteOrientation::view_parallel_upright;
    input.camera_forward = {0.0F, 0.0F, 1.0F};
    const auto degenerate = sprite::make_sprite_billboard_basis(input);
    REQUIRE_FALSE(degenerate);
    CHECK(degenerate.error->code ==
          sprite::SpritePlaybackErrorCode::degenerate_billboard_basis);
}

TEST_CASE("Sprite quad preserves exact source origin without centering",
          "[goldsrc-sprite][billboard][quad]")
{
    assets::SpriteIndexedFrame frame;
    frame.origin = {-2, 3};
    frame.width = 4U;
    frame.height = 5U;
    sprite::SpriteBillboardBasis basis;
    basis.right = {1.0F, 0.0F, 0.0F};
    basis.up = {0.0F, 1.0F, 0.0F};
    basis.normal = {0.0F, 0.0F, 1.0F};

    const auto quad =
        sprite::make_sprite_quad_geometry(frame, basis, {10.0F, 20.0F, 30.0F});
    REQUIRE(quad);
    CHECK(quad.geometry->vertices[0U].position.x == Approx(8.0F));
    CHECK(quad.geometry->vertices[0U].position.y == Approx(18.0F));
    CHECK(quad.geometry->vertices[2U].position.x == Approx(12.0F));
    CHECK(quad.geometry->vertices[2U].position.y == Approx(23.0F));
    CHECK(quad.geometry->vertices[2U].position.z == Approx(30.0F));
}

} // namespace
