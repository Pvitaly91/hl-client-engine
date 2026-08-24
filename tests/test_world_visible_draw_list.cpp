#include <hlclient/world_visibility/world_visible_draw_list.hpp>
#include <hlclient/world_visibility/world_visibility_resolver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace renderer = hlclient::renderer;
namespace visibility = hlclient::world_visibility;
namespace world_render = hlclient::world_render;

template <typename Type>
concept HasOpenGlObject = requires(Type value) {
    value.vao;
    value.vbo;
    value.opengl_texture;
};

[[nodiscard]] renderer::RenderCamera camera()
{
    renderer::RenderCamera result;
    result.position = {0.0F, 0.0F, 0.0F};
    result.target = {0.0F, 0.0F, -1.0F};
    result.up = {0.0F, 1.0F, 0.0F};
    result.near_plane = 0.1F;
    result.far_plane = 100.0F;
    return result;
}

[[nodiscard]] std::vector<visibility::WorldVisibleSurfaceInput> world_surfaces()
{
    return {
        {0U, 0U, 3U, 1U,
            {{-1.0F, -1.0F, -3.0F}, {0.0F, 0.0F, -2.0F}},
            assets::WorldTextureAlphaMode::masked_index_255,
            world_render::WorldRenderLightmapMode::unlit_white,
            std::nullopt},
        {1U, 3U, 3U, 1U,
            {{0.0F, 0.0F, -3.0F}, {1.0F, 1.0F, -2.0F}}},
        {2U, 6U, 3U, 0U,
            {{-0.5F, -0.5F, -4.0F}, {0.5F, 0.5F, -3.0F}}},
    };
}

[[nodiscard]] visibility::WorldVisibilitySet resolve_all(
    const std::span<const visibility::WorldVisibleSurfaceInput> surfaces,
    const std::span<const visibility::WorldVisibilityBrushInstanceInput> brushes = {})
{
    visibility::WorldVisibilityResolveInput input;
    input.world_surfaces = surfaces;
    input.brush_instances = brushes;
    input.camera = camera();
    input.mode = visibility::WorldVisibilityMode::all;
    input.revision = 9U;
    const visibility::WorldVisibilityResolver resolver;
    auto resolved = resolver.resolve(input);
    if (!resolved.visibility) {
        throw std::runtime_error{"Unable to construct draw-list visibility fixture"};
    }
    return std::move(*resolved.visibility);
}

TEST_CASE("Visible draw list emits exact deterministic opaque and masked groups",
          "[world-visibility][draw-list]")
{
    STATIC_REQUIRE_FALSE(HasOpenGlObject<visibility::WorldVisibleDrawCommand>);
    STATIC_REQUIRE_FALSE(HasOpenGlObject<visibility::WorldVisibleDrawList>);

    const auto surfaces = world_surfaces();
    const std::array<std::uint32_t, 1U> touched{1U};
    const std::array visibility_brushes{
        visibility::WorldVisibilityBrushInstanceInput{
            7U,
            {{-1.0F, -1.0F, -3.0F}, {1.0F, 1.0F, -2.0F}},
            touched,
            true,
        },
    };
    const auto selected = resolve_all(surfaces, visibility_brushes);

    std::array brush_surfaces{
        visibility::WorldVisibleSurfaceInput{
            4U,
            0U,
            3U,
            1U,
            {{-1.0F, -1.0F, -1.0F}, {0.0F, 0.0F, 0.0F}},
            assets::WorldTextureAlphaMode::masked_index_255,
            world_render::WorldRenderLightmapMode::unlit_white,
            std::nullopt,
        },
        visibility::WorldVisibleSurfaceInput{
            3U,
            3U,
            3U,
            0U,
            {{0.0F, 0.0F, -1.0F}, {1.0F, 1.0F, 0.0F}},
        },
    };
    const std::array brush_models{
        visibility::WorldVisibleBrushModelInput{1U, 6U, 2U, brush_surfaces},
    };
    renderer::RenderMatrix4 transform;
    transform.values[12U] = 2.0F;
    const std::array brush_draw_instances{
        visibility::WorldVisibleBrushInstanceDrawInput{7U, 1U, transform},
    };

    visibility::WorldVisibleDrawListBuildInput input;
    input.visibility = &selected;
    input.world_surfaces = surfaces;
    input.world_index_buffer_index_count = 9U;
    input.world_render_material_count = 2U;
    input.brush_models = brush_models;
    input.brush_instances = brush_draw_instances;
    const visibility::WorldVisibleDrawListBuilder builder;
    const auto built = builder.build(input);
    REQUIRE(built.draw_list);
    const auto commands = built.draw_list->commands();
    REQUIRE(commands.size() == 5U);

    CHECK(commands[0U].object_kind == visibility::WorldVisibleObjectKind::world_surface);
    CHECK(commands[0U].source_surface_index == 2U);
    CHECK(commands[1U].source_surface_index == 1U);
    CHECK(commands[2U].source_surface_index == 0U);
    CHECK(commands[2U].alpha_mode ==
        assets::WorldTextureAlphaMode::masked_index_255);
    CHECK(commands[3U].object_kind ==
        visibility::WorldVisibleObjectKind::brush_instance_surface);
    CHECK(commands[3U].source_surface_index == 3U);
    CHECK(commands[3U].source_instance_index == 7U);
    CHECK(commands[3U].model_transform.values[12U] == 2.0F);
    CHECK(commands[4U].source_surface_index == 4U);
    CHECK(commands[4U].alpha_mode ==
        assets::WorldTextureAlphaMode::masked_index_255);
    CHECK(built.draw_list->visibility_revision() == 9U);
    CHECK(static_cast<bool>(selected.result_signature()));
    CHECK(built.draw_list->result_signature() == selected.result_signature());
    CHECK(built.draw_list->statistics().world_command_count == 3U);
    CHECK(built.draw_list->statistics().brush_command_count == 2U);
    CHECK(built.draw_list->statistics().triangle_count == 5U);
}

TEST_CASE("Visible draw list excludes hidden surfaces and includes a visible range once",
          "[world-visibility][draw-list]")
{
    auto surfaces = world_surfaces();
    surfaces[1U].bounds = {{100.0F, 100.0F, -3.0F},
        {101.0F, 101.0F, -2.0F}};
    visibility::WorldVisibilityResolveInput resolve_input;
    resolve_input.world_surfaces = surfaces;
    resolve_input.camera = camera();
    resolve_input.extent = {100, 100};
    resolve_input.mode = visibility::WorldVisibilityMode::frustum_only;
    const visibility::WorldVisibilityResolver resolver;
    auto resolved = resolver.resolve(resolve_input);
    REQUIRE(resolved.visibility);

    visibility::WorldVisibleDrawListBuildInput input;
    input.visibility = &*resolved.visibility;
    input.world_surfaces = surfaces;
    input.world_index_buffer_index_count = 9U;
    input.world_render_material_count = 2U;
    const visibility::WorldVisibleDrawListBuilder builder;
    const auto built = builder.build(input);
    REQUIRE(built.draw_list);
    REQUIRE(built.draw_list->commands().size() == 2U);
    CHECK(built.draw_list->commands()[0U].source_surface_index == 2U);
    CHECK(built.draw_list->commands()[1U].source_surface_index == 0U);
}

TEST_CASE("Visible draw list rejects malformed ranges and overlapping triangles",
          "[world-visibility][draw-list]")
{
    auto surfaces = world_surfaces();
    auto selected = resolve_all(surfaces);
    visibility::WorldVisibleDrawListBuildInput input;
    input.visibility = &selected;
    input.world_surfaces = surfaces;
    input.world_index_buffer_index_count = 9U;
    input.world_render_material_count = 2U;
    const visibility::WorldVisibleDrawListBuilder builder;

    surfaces[0U].index_count = 4U;
    const auto malformed_range = builder.build(input);
    REQUIRE_FALSE(malformed_range);
    REQUIRE(malformed_range.error);
    CHECK(malformed_range.error->code ==
        visibility::WorldVisibleDrawListErrorCode::invalid_index_range);

    surfaces = world_surfaces();
    input.world_surfaces = surfaces;
    surfaces[1U].first_index = 2U;
    const auto overlapping_range = builder.build(input);
    REQUIRE_FALSE(overlapping_range);
    REQUIRE(overlapping_range.error);
    CHECK(overlapping_range.error->code ==
        visibility::WorldVisibleDrawListErrorCode::invalid_index_range);

    surfaces = world_surfaces();
    input.world_surfaces = surfaces;
    surfaces[2U].render_material_index = 2U;
    const auto invalid_material = builder.build(input);
    REQUIRE_FALSE(invalid_material);
    REQUIRE(invalid_material.error);
    CHECK(invalid_material.error->code ==
        visibility::WorldVisibleDrawListErrorCode::invalid_material_reference);
}

TEST_CASE("Visible draw list enforces the exact command limit transactionally",
          "[world-visibility][draw-list]")
{
    const auto surfaces = world_surfaces();
    const auto selected = resolve_all(surfaces);
    visibility::WorldVisibleDrawListBuildInput input;
    input.visibility = &selected;
    input.world_surfaces = surfaces;
    input.world_index_buffer_index_count = 9U;
    input.world_render_material_count = 2U;
    const visibility::WorldVisibleDrawListBuilder builder;

    visibility::WorldVisibleDrawListLimits exact;
    exact.maximum_draw_commands = 3U;
    REQUIRE(builder.build(input, exact));
    exact.maximum_draw_commands = 2U;
    const auto rejected = builder.build(input, exact);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        visibility::WorldVisibleDrawListErrorCode::command_limit_exceeded);
    CHECK_FALSE(rejected.draw_list);
}

TEST_CASE("Visible draw list remains deterministic at the configured surface maximum",
          "[world-visibility][draw-list][high-cardinality]")
{
    constexpr std::size_t surface_count = 65'535U;
    std::vector<visibility::WorldVisibleSurfaceInput> surfaces;
    surfaces.reserve(surface_count);
    for (std::size_t ordinal = surface_count; ordinal > 0U; --ordinal) {
        const auto source_index = static_cast<std::uint32_t>(ordinal - 1U);
        surfaces.push_back({
            source_index,
            source_index * 3U,
            3U,
            0U,
            {{-1.0F, -1.0F, -3.0F}, {1.0F, 1.0F, -2.0F}},
        });
    }

    const auto selected = resolve_all(surfaces);
    REQUIRE(selected.visible_world_surface_indices().size() == surface_count);

    visibility::WorldVisibleDrawListBuildInput input;
    input.visibility = &selected;
    input.world_surfaces = surfaces;
    input.world_index_buffer_index_count = surface_count * 3U;
    input.world_render_material_count = 1U;
    const visibility::WorldVisibleDrawListBuilder builder;
    const auto first = builder.build(input);
    const auto second = builder.build(input);
    REQUIRE(first.draw_list);
    REQUIRE(second.draw_list);

    const auto first_commands = first.draw_list->commands();
    const auto second_commands = second.draw_list->commands();
    REQUIRE(first_commands.size() == surface_count);
    REQUIRE(second_commands.size() == surface_count);
    CHECK(first.draw_list->statistics().triangle_count == surface_count);
    CHECK(first_commands.front().source_surface_index == 0U);
    CHECK(first_commands.back().source_surface_index == surface_count - 1U);
    CHECK(std::ranges::equal(first_commands, second_commands,
        [](const auto& left, const auto& right) {
            return left.source_surface_index == right.source_surface_index &&
                left.first_index == right.first_index &&
                left.index_count == right.index_count &&
                left.render_material_index == right.render_material_index;
        }));
}

} // namespace
