#include <hlclient/goldsrc/world_render/world_render_package_stage.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

namespace goldsrc = hlclient::goldsrc;

template <typename Type>
concept HasRendererHandle =
    requires(Type& value) { value.renderer_handle(); } ||
    requires(Type& value) { value.vao; } ||
    requires(Type& value) { value.opengl_texture; };

TEST_CASE("World spatial-scene stage remains an explicit CPU-only continuation",
    "[world-spatial-scene][stage][boundary]")
{
    STATIC_REQUIRE_FALSE(HasRendererHandle<goldsrc::WorldRenderPackageStage>);
    STATIC_REQUIRE_FALSE(HasRendererHandle<
        hlclient::world_scene_render::WorldSceneRenderPackage>);

    auto historical = goldsrc::WorldRenderPackageStageConfig{};
    REQUIRE(goldsrc::valid_world_render_package_stage_configuration(
        historical));
    CHECK_FALSE(historical.build_world_spatial_scene);

    auto spatial_scene = historical;
    spatial_scene.build_world_spatial_scene = true;
    spatial_scene.world_scene.brushes = hlclient::goldsrc::brush_models::
        GoldSrcWorldSceneBrushMode::static_initial;
    spatial_scene.world_scene.extract_spawn = true;
    CHECK(goldsrc::valid_world_render_package_stage_configuration(
        spatial_scene));
    CHECK(goldsrc::to_string(
              goldsrc::WorldRenderPackageStageErrorCode::
                  world_scene_bsp_parse_failed) ==
          "world_scene_bsp_parse_failed");
    CHECK(goldsrc::to_string(
              goldsrc::WorldRenderPackageStageErrorCode::
                  brush_render_library_build_failed) ==
          "brush_render_library_build_failed");
    CHECK(goldsrc::to_string(
              goldsrc::WorldRenderPackageStageErrorCode::
                  world_scene_build_failed) ==
          "world_scene_build_failed");
}

} // namespace
