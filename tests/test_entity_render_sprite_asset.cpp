#include <hlclient/entity_render/sprite_billboard_basis.hpp>
#include <hlclient/entity_render/sprite_frame_selector.hpp>

#include "entity_render/entity_render_test_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {

namespace assets = hlclient::assets;
namespace render = hlclient::entity_render;
namespace fixture = hlclient::tests::entity_render_fixture;

[[nodiscard]] render::SpriteRenderAsset make_asset(
    const assets::SpriteTextureFormat format =
        assets::SpriteTextureFormat::normal,
    const assets::SpriteSyncType sync = assets::SpriteSyncType::synchronized)
{
    const auto source = fixture::sprite_asset(format, sync);
    auto built = render::SpriteRenderAssetBuilder{}.build(
        *source, {0x300U, 0x400U});
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    return std::move(*built.asset);
}

TEST_CASE("Neutral Sprite asset retains exact origins and typed formats",
    "[entity-render][sprite][asset]")
{
    const auto normal = make_asset();
    REQUIRE(normal.frames().size() == 3U);
    const auto& geometry = normal.frames()[0U].geometry;
    CHECK(geometry.source_origin.x == -2);
    CHECK(geometry.source_origin.y == 3);
    CHECK(geometry.local_corners[0U].x == -2.0F);
    CHECK(geometry.local_corners[0U].y == -3.0F);
    CHECK(geometry.local_corners[1U].x == 2.0F);
    CHECK(geometry.local_corners[2U].y == 3.0F);
    CHECK(normal.render_profile() == render::SpriteRenderTextureProfile::opaque);

    const auto masked = make_asset(assets::SpriteTextureFormat::alpha_test);
    CHECK(masked.render_profile() ==
        render::SpriteRenderTextureProfile::alpha_test_masked);
    const auto additive = make_asset(assets::SpriteTextureFormat::additive);
    CHECK(additive.texture_support_status() ==
        render::SpriteRenderTextureSupportStatus::
            supported_additive);
    CHECK(additive.render_profile() ==
        render::SpriteRenderTextureProfile::additive);
    const auto index_alpha = make_asset(assets::SpriteTextureFormat::index_alpha);
    CHECK(index_alpha.texture_support_status() ==
        render::SpriteRenderTextureSupportStatus::
            supported_index_alpha_local_compatible);
    CHECK(index_alpha.render_profile() == render::SpriteRenderTextureProfile::index_alpha);
    CHECK(index_alpha.frames()[0U].rgba8.size() == 4U * 6U * 4U);
}

TEST_CASE("E16 neutral index-alpha asset derives explicit index coverage without mutating source",
    "[e16][entity-render][sprite][asset]")
{
    auto source = *fixture::sprite_asset(assets::SpriteTextureFormat::index_alpha);
    auto& metadata = *source.source_data;
    metadata.palette.colors[255U] = {16U,32U,64U};
    metadata.palette.colors[128U] = {255U,0U,0U};
    auto& frame = metadata.indexed_frames.front();
    frame.indexed_pixels[0U] = std::byte{0U};
    frame.indexed_pixels[1U] = std::byte{128U};
    frame.indexed_pixels[2U] = std::byte{255U};
    auto built = render::SpriteRenderAssetBuilder{}.build(source,{0x501U,0x502U});
    REQUIRE(built);
    const auto& pixels = built.asset->frames().front().rgba8;
    REQUIRE(pixels.size() == 96U);
    for (const auto offset : {0U,4U,8U}) {
        CHECK(pixels[offset] == std::byte{16U});
        CHECK(pixels[offset+1U] == std::byte{32U});
        CHECK(pixels[offset+2U] == std::byte{64U});
    }
    CHECK(pixels[3U] == std::byte{0U});
    CHECK(pixels[7U] == std::byte{128U});
    CHECK(pixels[11U] == std::byte{255U});
    CHECK(frame.derived_rgba8.empty());
    CHECK(frame.rgba_evidence == assets::SpriteRgbaEvidenceProfile::index_alpha_conversion_evidence_pending);
    CHECK(built.asset->statistics().renderable_frame_count == 3U);
    CHECK(built.asset->statistics().texture_rgba_bytes == (4U+5U+6U)*6U*4U);
    auto limits=render::RuntimeEntityVisualLimits{};
    limits.maximum_sprite_gpu_bytes=500U;
    const auto bounded=render::SpriteRenderAssetBuilder{}.build(source,{0x501U,0x502U},limits);
    REQUIRE_FALSE(bounded);
    CHECK(bounded.error->code==render::SpriteRenderAssetErrorCode::source_limit_exceeded);
    frame.indexed_pixels.pop_back();
    const auto invalid = render::SpriteRenderAssetBuilder{}.build(source,{0x501U,0x502U});
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code == render::SpriteRenderAssetErrorCode::invalid_frame);
}

TEST_CASE("Neutral Sprite selector uses cumulative exact interval boundaries",
    "[entity-render][sprite][selector]")
{
    const auto asset = make_asset();
    render::SpriteFrameSelector selector;
    auto selected = selector.select(asset, {0U, 10.0});
    REQUIRE(selected);
    CHECK(*selected.flattened_frame_index == 0U);
    selected = selector.select(asset, {1U, 0.249});
    REQUIRE(selected);
    CHECK(*selected.flattened_frame_index == 1U);
    selected = selector.select(asset, {1U, 0.25});
    REQUIRE(selected);
    CHECK(*selected.flattened_frame_index == 2U);
    selected = selector.select(asset, {1U, 1.0});
    REQUIRE(selected);
    CHECK(*selected.flattened_frame_index == 1U);
    selected = selector.select(asset, {1U, 0.0, 2U, 7U});
    REQUIRE(selected);
    CHECK(selected.status ==
        render::SpriteFrameSelectionStatus::selected_explicit_override);
    CHECK(selector.select(asset, {9U, 0.0}).status ==
        render::SpriteFrameSelectionStatus::invalid_top_level_entry);
    CHECK(selector
              .select(asset,
                  {1U, std::numeric_limits<double>::quiet_NaN()})
              .status ==
        render::SpriteFrameSelectionStatus::invalid_elapsed_time);

    const auto random = make_asset(
        assets::SpriteTextureFormat::normal, assets::SpriteSyncType::random);
    const auto pending = selector.select(random, {1U, 0.5, std::nullopt, 9U});
    REQUIRE_FALSE(pending);
    CHECK(pending.status ==
        render::SpriteFrameSelectionStatus::random_sync_evidence_pending);
}

TEST_CASE("Neutral Sprite billboard supports required bases and degeneracy",
    "[entity-render][sprite][billboard]")
{
    render::SpriteBillboardInput parallel;
    parallel.orientation = assets::SpriteOrientation::view_parallel;
    parallel.camera_right = {1.0F, 0.0F, 0.0F};
    parallel.camera_up = {0.0F, 1.0F, 0.0F};
    const auto parallel_basis =
        render::SpriteBillboardBasis::calculate(parallel);
    REQUIRE(parallel_basis);
    CHECK(parallel_basis.normal.z == Catch::Approx(1.0F));

    render::SpriteBillboardInput upright;
    upright.orientation = assets::SpriteOrientation::view_parallel_upright;
    upright.camera_forward = {1.0F, 0.0F, 0.0F};
    const auto upright_basis = render::SpriteBillboardBasis::calculate(upright);
    REQUIRE(upright_basis);
    CHECK(upright_basis.right.y == Catch::Approx(-1.0F));
    CHECK(upright_basis.up.z == Catch::Approx(1.0F));

    render::SpriteBillboardInput oriented;
    oriented.orientation = assets::SpriteOrientation::oriented;
    oriented.oriented_right = {1.0F, 0.0F, 0.0F};
    oriented.oriented_up = {0.0F, 1.0F, 0.0F};
    oriented.oriented_forward = {0.0F, 0.0F, 1.0F};
    REQUIRE(render::SpriteBillboardBasis::calculate(oriented));

    upright.camera_forward = {0.0F, 0.0F, 1.0F};
    CHECK(render::SpriteBillboardBasis::calculate(upright).status ==
        render::SpriteBillboardBasisStatus::degenerate_upright_basis);
    oriented.orientation = assets::SpriteOrientation::facing_upright;
    CHECK(render::SpriteBillboardBasis::calculate(oriented).status ==
        render::SpriteBillboardBasisStatus::
            unsupported_facing_upright_evidence_pending);
}

TEST_CASE("E16 camera-parallel oriented sprites retain finite roll and camera attachment",
    "[e16][entity-render][sprite][billboard]")
{
    render::SpriteBillboardInput input;
    input.orientation=assets::SpriteOrientation::view_parallel_oriented;
    input.camera_right={1,0,0};input.camera_up={0,0,1};
    auto result=render::SpriteBillboardBasis::calculate(input);
    REQUIRE(result);
    CHECK(result.right.x==Catch::Approx(1.0F));CHECK(result.up.z==Catch::Approx(1.0F));
    input.roll_degrees=90.0F;
    result=render::SpriteBillboardBasis::calculate(input);
    REQUIRE(result);
    CHECK(result.right.z==Catch::Approx(1.0F));CHECK(result.up.x==Catch::Approx(-1.0F));
    CHECK(result.normal.y==Catch::Approx(-1.0F));
    input.roll_degrees=std::numeric_limits<float>::infinity();
    CHECK(render::SpriteBillboardBasis::calculate(input).status==render::SpriteBillboardBasisStatus::non_finite_input);
    input.roll_degrees=0.0F;input.camera_up={1,0,0};
    CHECK(render::SpriteBillboardBasis::calculate(input).status==render::SpriteBillboardBasisStatus::degenerate_camera_basis);
}

} // namespace
