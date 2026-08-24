#include <hlclient/goldsrc/brush_models/goldsrc_brush_render_library.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_world_scene_builder.hpp>
#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/world_render/world_render_package_builder.hpp>

#include "local_resource_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace brush = hlclient::goldsrc::brush_models;
namespace bsp = hlclient::goldsrc::bsp;
namespace fixture = hlclient::tests;
namespace local = hlclient::local_resources;
namespace world_render = hlclient::world_render;
namespace world_scene = hlclient::world_scene_render;
using fixture::ScopedLocalResourceTestRoot;

constexpr std::size_t kMipPixelByteCount = 256U + 64U + 16U + 4U;
constexpr std::size_t kPaletteByteCount = 256U * 3U;
constexpr std::size_t kLightmapBytesPerQuad = 5U * 5U * 3U;
constexpr std::string_view kWorldspawnEntityLump =
    "{\n\"classname\" \"worldspawn\"\n}\n";

struct BrushRenderFixtureOptions {
    std::string_view entity_lump_text{kWorldspawnEntityLump};
    bool exaggerated_source_model_bounds{false};
    bool external_texture{false};
    std::string_view texture_name{"BRUSH_SHARED"};
    std::uint32_t texture_width{16U};
    std::uint32_t texture_height{16U};
    bool brush_only_external_texture{false};
    bool first_brush_face_unlit{false};
};

struct BrushRenderFixture {
    std::vector<std::byte> source;
    bsp::GoldSrcBspParsedDocument document;
};

void set_entity_lump(
    fixture::SyntheticBspBuilder& builder,
    const std::string_view text)
{
    auto& bytes = builder.lump(fixture::SyntheticBspLumpId::entities);
    bytes.clear();
    bytes.reserve(text.size());
    for (const auto character : text) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
}

void write_embedded_palettes(
    std::vector<std::byte>& source,
    const std::size_t texture_count,
    const bool malformed_first_palette)
{
    const auto texture_lump = static_cast<std::size_t>(
        fixture::synthetic_read_i32le(
            source,
            fixture::synthetic_lump_descriptor_offset(
                fixture::SyntheticBspLumpId::textures)));
    for (std::size_t texture_index = 0U;
         texture_index < texture_count;
         ++texture_index) {
        const auto record_relative = static_cast<std::size_t>(
            fixture::synthetic_read_i32le(
                source,
                texture_lump + 4U + (texture_index * 4U)));
        const auto record = texture_lump + record_relative;
        if (fixture::synthetic_read_u32le(source, record + 24U) == 0U) {
            continue;
        }
        const auto palette_count = record + 40U + kMipPixelByteCount;
        fixture::synthetic_write_u16le(
            source,
            palette_count,
            malformed_first_palette && texture_index == 0U ? 255U : 256U);
        for (std::size_t palette_index = 0U;
             palette_index < 256U;
             ++palette_index) {
            source[palette_count + 2U + (palette_index * 3U)] =
                static_cast<std::byte>(palette_index);
            source[palette_count + 2U + (palette_index * 3U) + 1U] =
                static_cast<std::byte>(255U - palette_index);
            source[palette_count + 2U + (palette_index * 3U) + 2U] =
                static_cast<std::byte>(palette_index ^ 0xA5U);
        }
    }
}

[[nodiscard]] BrushRenderFixture make_brush_render_fixture(
    const bool distinct_material_sources = false,
    const bool malformed_palette = false,
    const std::size_t lighting_byte_count = 2U * kLightmapBytesPerQuad,
    const BrushRenderFixtureOptions options = {})
{
    fixture::SyntheticBspBuilder builder;
    constexpr auto vertices = fixture::synthetic_quad_vertices();
    constexpr std::array edges{
        fixture::SyntheticBspEdge{0U, 0U},
        fixture::SyntheticBspEdge{0U, 1U},
        fixture::SyntheticBspEdge{1U, 2U},
        fixture::SyntheticBspEdge{2U, 3U},
        fixture::SyntheticBspEdge{3U, 0U},
    };
    constexpr std::array<std::int32_t, 4U> surfedges{1, 2, 3, 4};

    std::array faces{
        fixture::SyntheticBspFace{},
        fixture::SyntheticBspFace{},
        fixture::SyntheticBspFace{},
    };
    if (!options.first_brush_face_unlit) {
        faces[1U].light_styles = {0U, 0xFFU, 0xFFU, 0xFFU};
        faces[1U].light_offset = 0;
    }
    faces[2U].light_styles = {0U, 0xFFU, 0xFFU, 0xFFU};
    faces[2U].light_offset = static_cast<std::int32_t>(kLightmapBytesPerQuad);
    if (options.brush_only_external_texture) {
        faces[1U].texinfo_index = 1;
        faces[2U].texinfo_index = 1;
    } else {
        faces[2U].texinfo_index = distinct_material_sources ? 1 : 0;
    }

    std::vector<fixture::SyntheticBspTexinfo> texinfo(1U);
    if (distinct_material_sources || options.brush_only_external_texture) {
        texinfo.push_back(fixture::SyntheticBspTexinfo{});
        texinfo[1U].miptex_index = 1;
    }

    std::vector<std::optional<fixture::SyntheticBspMipTexture>> textures;
    if (options.brush_only_external_texture) {
        auto world_texture = fixture::synthetic_embedded_texture(
            "WORLD_ONLY",
            options.texture_width,
            options.texture_height);
        world_texture.trailing_byte_count += 2U + kPaletteByteCount;
        textures.push_back(world_texture);

        auto brush_texture =
            fixture::synthetic_external_texture(options.texture_name);
        brush_texture.width = options.texture_width;
        brush_texture.height = options.texture_height;
        textures.push_back(brush_texture);
    } else {
        auto texture = options.external_texture
            ? fixture::synthetic_external_texture(options.texture_name)
            : fixture::synthetic_embedded_texture(
                  options.texture_name,
                  options.texture_width,
                  options.texture_height);
        texture.width = options.texture_width;
        texture.height = options.texture_height;
        if (!options.external_texture) {
            texture.trailing_byte_count += 2U + kPaletteByteCount;
        }
        textures.push_back(texture);
        if (distinct_material_sources) {
            // Both source records decode to the same pixels. They remain
            // distinct because BSP source identity, not post-decode RGBA
            // equality, is the material/texture deduplication contract.
            textures.push_back(texture);
        }
    }

    std::array models{
        fixture::SyntheticBspModel{},
        fixture::SyntheticBspModel{},
        fixture::SyntheticBspModel{},
    };
    models[0U].first_face = 0;
    models[1U].first_face = 1;
    models[1U].visibility_leaf_count = 0;
    models[2U].first_face = 2;
    models[2U].visibility_leaf_count = 0;
    if (options.exaggerated_source_model_bounds) {
        models[1U].minimum = {-32.0F, -48.0F, -16.0F};
        models[1U].maximum = {96.0F, 112.0F, 24.0F};
    }

    builder.set_vertices(vertices)
        .set_edges(edges)
        .set_surfedges(surfedges)
        .set_faces(faces)
        .set_texinfo(texinfo)
        .set_texture_directory(textures)
        .set_models(models);
    auto& lighting = builder.lump(fixture::SyntheticBspLumpId::lighting);
    lighting.resize(lighting_byte_count);
    for (std::size_t index = 0U; index < lighting.size(); ++index) {
        lighting[index] = static_cast<std::byte>(index & 0xFFU);
    }
    set_entity_lump(
        builder,
        options.entity_lump_text);

    auto source = builder.build();
    write_embedded_palettes(
        source,
        textures.size(),
        malformed_palette);
    auto parsed = bsp::GoldSrcBspParser::parse(source);
    if (!parsed || !parsed.document) {
        throw std::runtime_error{
            "Unable to parse synthetic brush render-library BSP"};
    }
    return BrushRenderFixture{
        std::move(source),
        std::move(*parsed.document),
    };
}

[[nodiscard]] std::shared_ptr<const local::LocalResourceEnvironment>
make_environment(ScopedLocalResourceTestRoot& temporary)
{
    auto roots = local::LocalResourceSearchRoots::create(
        temporary.path(), "valve");
    if (!roots || !roots.roots) {
        throw std::runtime_error{
            "Unable to create brush render-library search roots"};
    }
    local::LocalResourceResolverLimits resolver_limits;
    resolver_limits.maximum_file_size =
        local::kHardMaximumLocalResourceFileSize;
    auto environment = local::LocalResourceEnvironment::create(
        std::move(*roots.roots), resolver_limits);
    if (!environment || environment.environment == nullptr) {
        throw std::runtime_error{
            "Unable to create brush render-library environment"};
    }
    return std::shared_ptr<const local::LocalResourceEnvironment>{
        std::move(environment.environment)};
}

[[nodiscard]] brush::GoldSrcBrushRenderLibraryBuildResult build_library(
    const BrushRenderFixture& source,
    std::shared_ptr<const local::LocalResourceEnvironment> environment,
    const brush::GoldSrcBrushRenderLibraryLimits& limits = {})
{
    return brush::GoldSrcBrushRenderLibraryBuilder::build(
        source.document,
        source.source,
        std::move(environment),
        limits);
}

[[nodiscard]] std::shared_ptr<const world_render::WorldRenderPackage>
build_world_package(
    const BrushRenderFixture& source,
    std::shared_ptr<const local::LocalResourceEnvironment> environment)
{
    auto texture_begin = hlclient::goldsrc::WorldTextureImportOperation::begin(
        source.document.world_asset,
        source.source,
        std::move(environment));
    if (!texture_begin || !texture_begin.operation) {
        throw std::runtime_error{
            "Unable to begin synthetic world texture import"};
    }

    auto& texture_operation = *texture_begin.operation;
    auto now = hlclient::goldsrc::WorldTextureImportTimePoint{};
    constexpr std::size_t kMaximumTextureImportUpdates = 1'024U;
    for (std::size_t update = 0U;
         update < kMaximumTextureImportUpdates &&
             !texture_operation.terminal();
         ++update) {
        texture_operation.update(now);
        now += std::chrono::milliseconds{1};
    }
    if (texture_operation.state() !=
        hlclient::goldsrc::WorldTextureImportState::textures_ready) {
        throw std::runtime_error{
            "Unable to complete synthetic world texture import"};
    }
    auto textures = texture_operation.take_result();
    if (!textures) {
        throw std::runtime_error{
            "Synthetic world texture result is unavailable"};
    }

    auto lightmaps =
        hlclient::goldsrc::lightmaps::GoldSrcWorldLightmapImporter::import(
            source.document.world_asset,
            source.source);
    if (!lightmaps || !lightmaps.lightmap_set) {
        throw std::runtime_error{
            "Unable to import synthetic world lightmaps"};
    }

    const world_render::WorldRenderPackageBuilder builder;
    auto built = builder.build(
        assets::TexturedWorldAsset{
            source.document.world_asset,
            std::move(*textures),
        },
        std::move(*lightmaps.lightmap_set));
    if (!built || !built.package) {
        throw std::runtime_error{
            "Unable to build synthetic world render package"};
    }
    return std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*built.package));
}

TEST_CASE("Brush render library aggregates ordered models with exact ranges",
    "[goldsrc-brush][render-library][aggregate]")
{
    STATIC_REQUIRE_FALSE(std::is_aggregate_v<
        world_scene::BrushSubmodelRenderModel>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<
        world_scene::BrushSubmodelRenderModel>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<
        world_scene::BrushSubmodelRenderModel>);
    STATIC_REQUIRE_FALSE(std::is_aggregate_v<
        world_scene::BrushSubmodelRenderLibrary>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<
        world_scene::BrushSubmodelRenderLibrary>);
    STATIC_REQUIRE_FALSE(std::is_move_assignable_v<
        world_scene::BrushSubmodelRenderLibrary>);
    STATIC_REQUIRE((std::is_same_v<
        decltype(std::declval<const world_scene::BrushSubmodelRenderLibrary&>()
                     .models()),
        std::span<const world_scene::BrushSubmodelRenderModel>>));

    ScopedLocalResourceTestRoot temporary;
    auto source = make_brush_render_fixture();
    auto built = build_library(source, make_environment(temporary));
    REQUIRE(built);
    REQUIRE_FALSE(built.error);
    CHECK(built.coordinate_profile ==
        brush::BrushSubmodelCoordinateProfile::qcsg_entity_origin_relative_v1);
    CHECK(built.compatibility_profile ==
        brush::GoldSrcBrushRenderCompatibilityProfile::
            aggregate_static_opaque_world_render_package_v1);
    CHECK(built.evidence_profile ==
        brush::GoldSrcBrushRenderEvidenceProfile::
            canonical_bsp_geometry_shared_texture_and_lightmap_codecs);

    REQUIRE(built.library);
    REQUIRE(built.library->render_package());
    REQUIRE(built.library->models().size() == 2U);
    const auto& package = *built.library->render_package();
    const auto& aggregate = package.textured_world().world;
    CHECK(aggregate.vertices.size() == 8U);
    CHECK(aggregate.indices == std::vector<std::uint32_t>{
        0U, 1U, 2U, 0U, 2U, 3U,
        4U, 5U, 6U, 4U, 6U, 7U});
    REQUIRE(aggregate.surfaces.size() == 2U);
    REQUIRE(aggregate.materials.size() == 1U);
    CHECK(aggregate.surfaces[0U].source_surface_ordinal == 1U);
    CHECK(aggregate.surfaces[1U].source_surface_ordinal == 2U);
    CHECK(aggregate.surfaces[0U].first_index == 0U);
    CHECK(aggregate.surfaces[1U].first_index == 6U);
    CHECK(aggregate.surfaces[0U].first_vertex == 0U);
    CHECK(aggregate.surfaces[1U].first_vertex == 4U);
    CHECK(aggregate.surfaces[0U].lightmap_offset == 0U);
    CHECK(aggregate.surfaces[1U].lightmap_offset == kLightmapBytesPerQuad);
    CHECK(aggregate.surfaces[0U].light_styles ==
        std::array<std::uint8_t, 4U>{0U, 0xFFU, 0xFFU, 0xFFU});
    CHECK(aggregate.surfaces[1U].light_styles ==
        std::array<std::uint8_t, 4U>{0U, 0xFFU, 0xFFU, 0xFFU});
    CHECK(aggregate.surfaces[0U].material_index == 0U);
    CHECK(aggregate.surfaces[1U].material_index == 0U);

    const auto ranges = package.surface_ranges();
    REQUIRE(ranges.size() == 2U);
    CHECK(ranges[0U].source_world_surface_index == 0U);
    CHECK(ranges[1U].source_world_surface_index == 1U);
    CHECK(ranges[0U].first_index == 0U);
    CHECK(ranges[1U].first_index == 6U);
    CHECK(ranges[0U].index_count == 6U);
    CHECK(ranges[1U].index_count == 6U);

    const auto& first_model = built.library->models()[0U];
    const auto& second_model = built.library->models()[1U];
    CHECK(first_model.source_model_index() == 1U);
    CHECK(second_model.source_model_index() == 2U);
    CHECK(std::ranges::equal(
        first_model.render_surface_indices(), std::array{0U}));
    CHECK(std::ranges::equal(
        second_model.render_surface_indices(), std::array{1U}));
    REQUIRE(first_model.surfaces().size() == 1U);
    REQUIRE(second_model.surfaces().size() == 1U);
    CHECK(first_model.surfaces()[0U].source_surface_index == 0U);
    CHECK(second_model.surfaces()[0U].source_surface_index == 1U);
    CHECK(first_model.local_bounds().minimum.x == 0.0F);
    CHECK(first_model.local_bounds().maximum.x == 64.0F);

    CHECK(package.textured_world().textures.texture_count() == 1U);
    CHECK(package.textured_world().textures.binding_count() == 1U);
    CHECK(package.lightmaps().binding_count() == 2U);
    CHECK(package.lightmaps().page_count() == 1U);
    REQUIRE(package.lightmaps().bindings().size() == 2U);
    CHECK(package.lightmaps().bindings()[0U].sample_width == 5U);
    CHECK(package.lightmaps().bindings()[0U].sample_height == 5U);
    CHECK(package.lightmaps().bindings()[0U].source_styles.style_count == 1U);
    CHECK(package.lightmaps().bindings()[1U].source_styles.style_ids[0U] == 0U);
    CHECK(built.statistics.source_model_count == 2U);
    CHECK(built.statistics.aggregate_vertex_count == 8U);
    CHECK(built.statistics.aggregate_index_count == 12U);
    CHECK(built.statistics.aggregate_surface_count == 2U);
    CHECK(built.statistics.input_material_reference_count == 2U);
    CHECK(built.statistics.unique_material_reference_count == 1U);
    CHECK(built.statistics.deduplicated_material_reference_count == 1U);
    CHECK(built.statistics.decoded_texture_count == 1U);
    CHECK(built.statistics.lightmap_atlas_page_count == 1U);
}

TEST_CASE("Brush render library retains a valid unlit brush face",
    "[goldsrc-brush][render-library][lightmaps][unlit]")
{
    ScopedLocalResourceTestRoot temporary;
    BrushRenderFixtureOptions options;
    options.first_brush_face_unlit = true;
    const auto source = make_brush_render_fixture(
        false,
        false,
        2U * kLightmapBytesPerQuad,
        options);

    REQUIRE(source.document.brush_submodels.size() == 2U);
    REQUIRE(source.document.brush_submodels[0U].geometry.surfaces.size() == 1U);
    const auto& source_surface =
        source.document.brush_submodels[0U].geometry.surfaces[0U];
    CHECK_FALSE(source_surface.lightmap_offset);
    CHECK(source_surface.light_styles ==
        std::array<std::uint8_t, 4U>{0xFFU, 0xFFU, 0xFFU, 0xFFU});

    auto built = build_library(source, make_environment(temporary));
    REQUIRE(built);
    REQUIRE_FALSE(built.error);
    REQUIRE(built.library);
    REQUIRE(built.library->render_package());
    REQUIRE(built.library->models().size() == 2U);
    const auto& package = *built.library->render_package();

    const auto bindings = package.lightmaps().bindings();
    REQUIRE(bindings.size() == 2U);
    CHECK(bindings[0U].surface_index == 0U);
    CHECK(bindings[0U].status ==
        assets::WorldSurfaceLightmapBindingStatus::unlit_no_lightmap);
    CHECK_FALSE(bindings[0U].atlas_page_index);
    CHECK(bindings[0U].sample_width == 5U);
    CHECK(bindings[0U].sample_height == 5U);
    CHECK(bindings[1U].surface_index == 1U);
    CHECK(bindings[1U].status ==
        assets::WorldSurfaceLightmapBindingStatus::resolved);
    CHECK(bindings[1U].atlas_page_index == 0U);
    CHECK(package.lightmaps().page_count() == 1U);
    CHECK(package.lightmaps().statistics().unlit_surface_count == 1U);
    CHECK(package.lightmaps().statistics().resolved_surface_count == 1U);

    const auto ranges = package.surface_ranges();
    const auto materials = package.materials();
    REQUIRE(ranges.size() == 2U);
    REQUIRE(materials.size() == 2U);
    CHECK(ranges[0U].source_world_surface_index == 0U);
    CHECK(ranges[0U].lightmap_mode ==
        world_render::WorldRenderLightmapMode::unlit_white);
    CHECK_FALSE(ranges[0U].lightmap_atlas_page_index);
    REQUIRE(ranges[0U].render_material_index < materials.size());
    const auto& unlit_material =
        materials[ranges[0U].render_material_index];
    CHECK(unlit_material.lightmap_mode ==
        world_render::WorldRenderLightmapMode::unlit_white);
    CHECK_FALSE(unlit_material.lightmap_atlas_page_index);

    CHECK(ranges[1U].source_world_surface_index == 1U);
    CHECK(ranges[1U].lightmap_mode ==
        world_render::WorldRenderLightmapMode::atlas);
    CHECK(ranges[1U].lightmap_atlas_page_index == 0U);
    CHECK(package.statistics().atlas_batch_count == 1U);
    CHECK(package.statistics().unlit_batch_count == 1U);
}

TEST_CASE("Real brush library composes into a scene with geometry bounds",
    "[goldsrc-brush][render-library][world-scene][regression]")
{
    constexpr std::string_view entity_lump = R"({
"classname" "worldspawn"
}
{
"classname" "func_wall"
"model" "*1"
"origin" "0 0 -1"
}
)";
    ScopedLocalResourceTestRoot temporary;
    auto environment = make_environment(temporary);
    auto source = make_brush_render_fixture(
        false,
        false,
        2U * kLightmapBytesPerQuad,
        BrushRenderFixtureOptions{entity_lump, true});

    REQUIRE(source.document.brush_submodels.size() == 2U);
    const auto& source_model = source.document.brush_submodels[0U];
    CHECK(source_model.source_model_index == 1U);
    CHECK(source_model.source_model_bounds.minimum.x == -32.0F);
    CHECK(source_model.source_model_bounds.minimum.y == -48.0F);
    CHECK(source_model.source_model_bounds.minimum.z == -16.0F);
    CHECK(source_model.source_model_bounds.maximum.x == 96.0F);
    CHECK(source_model.source_model_bounds.maximum.y == 112.0F);
    CHECK(source_model.source_model_bounds.maximum.z == 24.0F);
    CHECK(source_model.geometry.bounds.minimum.x == 0.0F);
    CHECK(source_model.geometry.bounds.minimum.y == 0.0F);
    CHECK(source_model.geometry.bounds.minimum.z == 0.0F);
    CHECK(source_model.geometry.bounds.maximum.x == 64.0F);
    CHECK(source_model.geometry.bounds.maximum.y == 64.0F);
    CHECK(source_model.geometry.bounds.maximum.z == 0.0F);

    auto library_result = build_library(source, environment);
    REQUIRE(library_result);
    REQUIRE(library_result.library);
    REQUIRE(library_result.library->render_package());
    REQUIRE(library_result.library->models().size() == 2U);
    const auto retained_brush_package =
        library_result.library->render_package();
    const auto source_indices =
        library_result.library->models()[0U].render_surface_indices();
    const std::vector<std::uint32_t> retained_first_surface_indices{
        source_indices.begin(), source_indices.end()};
    CHECK(library_result.library->models()[0U].local_bounds().minimum.x == 0.0F);
    CHECK(library_result.library->models()[0U].local_bounds().maximum.x == 64.0F);

    const auto world_package = build_world_package(source, environment);
    REQUIRE(world_package);
    const brush::GoldSrcWorldSceneBuildConfig config{
        brush::GoldSrcWorldSceneBrushMode::static_initial,
        false,
    };
    auto scene_result = brush::GoldSrcWorldSceneBuilder::build(
        source.document,
        world_package,
        std::move(library_result.library),
        config);
    REQUIRE(scene_result);
    REQUIRE_FALSE(scene_result.error);
    REQUIRE(scene_result.scene_package);

    const auto& scene = *scene_result.scene_package;
    CHECK(scene.brush_library().render_package() == retained_brush_package);
    REQUIRE(scene.brush_library().models().size() == 2U);
    const auto& retained_model = scene.brush_library().models()[0U];
    CHECK(retained_model.source_model_index() == 1U);
    CHECK(retained_model.local_bounds().minimum.x == 0.0F);
    CHECK(retained_model.local_bounds().minimum.y == 0.0F);
    CHECK(retained_model.local_bounds().minimum.z == 0.0F);
    CHECK(retained_model.local_bounds().maximum.x == 64.0F);
    CHECK(retained_model.local_bounds().maximum.y == 64.0F);
    CHECK(retained_model.local_bounds().maximum.z == 0.0F);
    CHECK(std::ranges::equal(
        retained_model.render_surface_indices(),
        retained_first_surface_indices));

    const auto instances = scene.brush_instances();
    REQUIRE(instances.size() == 1U);
    const auto& instance = instances[0U];
    CHECK(instance.source_entity_ordinal == 1U);
    CHECK(instance.source_model_index == 1U);
    CHECK(instance.support_status ==
        world_scene::BrushSubmodelRenderSupportStatus::
            supported_static_opaque);
    CHECK(instance.transformed_bounds.minimum.x == 0.0F);
    CHECK(instance.transformed_bounds.minimum.y == 0.0F);
    CHECK(instance.transformed_bounds.minimum.z == -1.0F);
    CHECK(instance.transformed_bounds.maximum.x == 64.0F);
    CHECK(instance.transformed_bounds.maximum.y == 64.0F);
    CHECK(instance.transformed_bounds.maximum.z == -1.0F);
    CHECK(scene_result.statistics.retained_brush_render_model_count == 2U);
    CHECK(scene_result.statistics.supported_brush_instance_count == 1U);
    CHECK(scene.statistics().brush_model_count == 2U);
    CHECK(scene.statistics().supported_brush_instance_count == 1U);
}

TEST_CASE("Brush material dedup uses source identity rather than RGBA equality",
    "[goldsrc-brush][render-library][material-identity]")
{
    ScopedLocalResourceTestRoot temporary;
    auto source = make_brush_render_fixture(true);
    auto built = build_library(source, make_environment(temporary));
    REQUIRE(built);
    REQUIRE(built.library->render_package());
    const auto& package = *built.library->render_package();
    CHECK(built.statistics.input_material_reference_count == 2U);
    CHECK(built.statistics.unique_material_reference_count == 2U);
    CHECK(built.statistics.deduplicated_material_reference_count == 0U);
    CHECK(package.textured_world().world.materials.size() == 2U);
    CHECK(package.textured_world().textures.texture_count() == 2U);
    REQUIRE(package.textured_world().textures.textures().size() == 2U);
    CHECK(package.textured_world().textures.textures()[0U]
        .mip_levels[0U].rgba_pixels ==
        package.textured_world().textures.textures()[1U]
            .mip_levels[0U].rgba_pixels);
}

TEST_CASE("Brush render library publication is deterministic and owning",
    "[goldsrc-brush][render-library][determinism][ownership]")
{
    ScopedLocalResourceTestRoot temporary;
    auto source = make_brush_render_fixture();
    auto environment = make_environment(temporary);
    auto first = build_library(source, environment);
    auto second = build_library(source, environment);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first.library->render_package());
    REQUIRE(second.library->render_package());
    CHECK(first.library->render_package()->resource_identity() ==
        second.library->render_package()->resource_identity());
    CHECK(std::ranges::equal(
        first.library->models()[0U].render_surface_indices(),
        second.library->models()[0U].render_surface_indices()));
    CHECK(first.statistics.texture_import_update_count ==
        second.statistics.texture_import_update_count);

    auto retained_package = first.library->render_package();
    source.source.clear();
    source.document.brush_submodels.clear();
    CHECK(retained_package->vertices().size() == 8U);
    CHECK(retained_package->surface_ranges().size() == 2U);
    CHECK(retained_package->textured_world().textures.texture_count() == 1U);
}

TEST_CASE("Brush render library enforces exact aggregate limits",
    "[goldsrc-brush][render-library][limits]")
{
    ScopedLocalResourceTestRoot temporary;
    const auto source = make_brush_render_fixture();
    auto limits = brush::GoldSrcBrushRenderLibraryLimits{};
    limits.maximum_vertices = 8U;
    REQUIRE(build_library(source, make_environment(temporary), limits));

    limits.maximum_vertices = 7U;
    auto over = build_library(source, make_environment(temporary), limits);
    REQUIRE_FALSE(over);
    REQUIRE(over.error);
    CHECK(over.error->code ==
        brush::GoldSrcBrushRenderLibraryErrorCode::aggregate_limit_exceeded);
    CHECK(over.error->source_model_table_index == 1U);
    CHECK(over.error->source_model_index == 2U);
    CHECK_FALSE(over.library);
}

TEST_CASE("Brush render library rejects noncanonical model order transactionally",
    "[goldsrc-brush][render-library][ordering]")
{
    ScopedLocalResourceTestRoot temporary;
    auto source = make_brush_render_fixture();
    std::swap(
        source.document.brush_submodels[0U],
        source.document.brush_submodels[1U]);
    auto built = build_library(source, make_environment(temporary));
    REQUIRE_FALSE(built);
    REQUIRE(built.error);
    CHECK(built.error->code ==
        brush::GoldSrcBrushRenderLibraryErrorCode::invalid_model_order);
    CHECK_FALSE(built.library);
}

TEST_CASE("Brush render library rejects a retained source from another BSP",
    "[goldsrc-brush][render-library][provenance]")
{
    ScopedLocalResourceTestRoot temporary;
    const auto source = make_brush_render_fixture();
    auto foreign_source = source.source;
    REQUIRE_FALSE(foreign_source.empty());
    foreign_source.back() ^= std::byte{0x01};

    auto built = brush::GoldSrcBrushRenderLibraryBuilder::build(
        source.document,
        foreign_source,
        make_environment(temporary));
    REQUIRE_FALSE(built);
    REQUIRE(built.error);
    CHECK(built.error->code == brush::GoldSrcBrushRenderLibraryErrorCode::
        source_document_mismatch);
    CHECK_FALSE(built.library);

    auto forged_document = source.document;
    REQUIRE_FALSE(forged_document.brush_submodels.empty());
    forged_document.brush_submodels[0U].geometry.source_content_fingerprint =
        assets::AssetSourceFingerprint{1U, 2U};
    const auto forged_built = brush::GoldSrcBrushRenderLibraryBuilder::build(
        forged_document,
        source.source,
        make_environment(temporary));
    REQUIRE_FALSE(forged_built);
    REQUIRE(forged_built.error);
    CHECK(forged_built.error->code == brush::GoldSrcBrushRenderLibraryErrorCode::
        source_document_mismatch);
    CHECK(forged_built.error->source_model_index == 1U);
}

TEST_CASE("Brush render library preserves incomplete WAD diagnostics",
    "[goldsrc-brush][render-library][textures-incomplete][wad3]")
{
    const auto make_external_source = [](const std::string_view entity_lump) {
        BrushRenderFixtureOptions options;
        options.entity_lump_text = entity_lump;
        options.external_texture = true;
        options.texture_name = "BRUSH_WAD";
        return make_brush_render_fixture(
            false,
            false,
            2U * kLightmapBytesPerQuad,
            options);
    };

    SECTION("missing WAD retains binding and archive reason")
    {
        constexpr std::string_view entities = R"({
"classname" "worldspawn"
"_wad" "C:\compiler\missing.wad;"
}
)";
        ScopedLocalResourceTestRoot temporary;
        const auto source = make_external_source(entities);
        auto built = build_library(source, make_environment(temporary));
        REQUIRE_FALSE(built);
        REQUIRE_FALSE(built.library);
        REQUIRE(built.error);
        CHECK(built.error->code ==
            brush::GoldSrcBrushRenderLibraryErrorCode::textures_incomplete);
        CHECK_FALSE(built.error->texture_error);
        REQUIRE(built.error->texture_incomplete_reason);
        const auto& reason = *built.error->texture_incomplete_reason;
        CHECK(reason.material_index == 0U);
        CHECK(reason.binding_status ==
            assets::WorldMaterialTextureBindingStatus::
                external_wad_archive_missing);
        CHECK(reason.source_bsp_texture_index == 0U);
        CHECK(reason.source_archive_ordinal == 0U);
        CHECK(reason.archive_status ==
            assets::WorldTextureArchiveStatus::missing);
    }

    SECTION("matching WAD remains a successful render library")
    {
        constexpr std::string_view entities = R"({
"classname" "worldspawn"
"_wad" "D:\compiler\brush.wad;"
}
)";
        ScopedLocalResourceTestRoot temporary;
        const auto wad = fixture::synthetic_valid_wad3("BRUSH_WAD");
        temporary.write("valve", "brush.wad", wad.bytes);
        const auto source = make_external_source(entities);
        auto built = build_library(source, make_environment(temporary));
        REQUIRE(built);
        REQUIRE_FALSE(built.error);
        REQUIRE(built.library);
        REQUIRE(built.library->render_package());
        const auto& textures =
            built.library->render_package()->textured_world().textures;
        REQUIRE(textures.texture_count() == 1U);
        REQUIRE(textures.binding_count() == 1U);
        CHECK(textures.bindings()[0U].status ==
            assets::WorldMaterialTextureBindingStatus::resolved_wad3);
        CHECK(textures.textures()[0U].source_kind ==
            assets::WorldTextureSourceKind::external_wad3);
        CHECK(textures.textures()[0U].width == 16U);
        CHECK(textures.textures()[0U].height == 16U);
        REQUIRE(textures.archive_metadata().size() == 1U);
        CHECK(textures.archive_metadata()[0U].status ==
            assets::WorldTextureArchiveStatus::resolved);
    }

    SECTION("WAD dimension mismatch retains its resolved archive reason")
    {
        constexpr std::string_view entities = R"({
"classname" "worldspawn"
"_wad" "E:\compiler\mismatch.wad;"
}
)";
        ScopedLocalResourceTestRoot temporary;
        fixture::SyntheticWad3Entry entry;
        entry.name = "BRUSH_WAD";
        entry.payload = fixture::synthetic_goldsrc_miptex(
            "BRUSH_WAD", 32U, 16U);
        const auto wad = fixture::synthetic_wad3({std::move(entry)});
        temporary.write("valve", "mismatch.wad", wad.bytes);
        const auto source = make_external_source(entities);
        auto built = build_library(source, make_environment(temporary));
        REQUIRE_FALSE(built);
        REQUIRE_FALSE(built.library);
        REQUIRE(built.error);
        CHECK(built.error->code ==
            brush::GoldSrcBrushRenderLibraryErrorCode::textures_incomplete);
        CHECK_FALSE(built.error->texture_error);
        REQUIRE(built.error->texture_incomplete_reason);
        const auto& reason = *built.error->texture_incomplete_reason;
        CHECK(reason.material_index == 0U);
        CHECK(reason.binding_status ==
            assets::WorldMaterialTextureBindingStatus::
                external_texture_dimension_mismatch);
        CHECK(reason.source_bsp_texture_index == 0U);
        CHECK(reason.source_archive_ordinal == 0U);
        CHECK(reason.archive_status ==
            assets::WorldTextureArchiveStatus::resolved);
    }
}

TEST_CASE("Brush render library resolves a true brush-only WAD material",
    "[goldsrc-brush][render-library][textures][wad3][brush-only]")
{
    constexpr std::string_view entities = R"({
"classname" "worldspawn"
"_wad" "D:\compiler\brush_only.wad;"
}
)";
    ScopedLocalResourceTestRoot temporary;
    const auto wad = fixture::synthetic_valid_wad3("BRUSH_WAD");
    temporary.write("valve", "brush_only.wad", wad.bytes);

    BrushRenderFixtureOptions options;
    options.entity_lump_text = entities;
    options.texture_name = "BRUSH_WAD";
    options.brush_only_external_texture = true;
    const auto source = make_brush_render_fixture(
        false,
        false,
        2U * kLightmapBytesPerQuad,
        options);

    REQUIRE(source.document.world_asset.materials.size() == 1U);
    const auto& world_material = source.document.world_asset.materials[0U];
    CHECK(world_material.texture_name == "WORLD_ONLY");
    CHECK(world_material.texture_storage ==
        assets::WorldTextureStorage::embedded);
    CHECK(world_material.source_texture_index == 0U);
    REQUIRE(source.document.brush_submodels.size() == 2U);
    for (const auto& source_model : source.document.brush_submodels) {
        REQUIRE(source_model.geometry.materials.size() == 1U);
        const auto& brush_material = source_model.geometry.materials[0U];
        CHECK(brush_material.texture_name == "BRUSH_WAD");
        CHECK(brush_material.texture_storage ==
            assets::WorldTextureStorage::external_reference);
        CHECK(brush_material.source_texture_index == 1U);
        CHECK(brush_material.source_texinfo_index == 1U);
    }

    auto built = build_library(source, make_environment(temporary));
    REQUIRE(built);
    REQUIRE_FALSE(built.error);
    REQUIRE(built.library);
    REQUIRE(built.library->render_package());
    REQUIRE(built.library->models().size() == 2U);
    const auto& package = *built.library->render_package();
    REQUIRE(package.textured_world().world.materials.size() == 1U);
    CHECK(package.textured_world().world.materials[0U].texture_name ==
        "BRUSH_WAD");
    CHECK(package.textured_world().world.materials[0U].texture_storage ==
        assets::WorldTextureStorage::external_reference);
    CHECK(package.textured_world().world.materials[0U].source_texture_index ==
        1U);

    const auto& textures = package.textured_world().textures;
    REQUIRE(textures.binding_count() == 1U);
    REQUIRE(textures.texture_count() == 1U);
    const auto bindings = textures.bindings();
    REQUIRE(bindings.size() == 1U);
    CHECK(bindings[0U].material_index == 0U);
    CHECK(bindings[0U].status ==
        assets::WorldMaterialTextureBindingStatus::resolved_wad3);
    CHECK(bindings[0U].texture_asset_index == 0U);
    CHECK(bindings[0U].source_bsp_texture_index == 1U);
    CHECK(bindings[0U].source_archive_ordinal == 0U);

    const auto texture_assets = textures.textures();
    REQUIRE(texture_assets.size() == 1U);
    CHECK(texture_assets[0U].name == "BRUSH_WAD");
    CHECK(texture_assets[0U].source_kind ==
        assets::WorldTextureSourceKind::external_wad3);
    CHECK(texture_assets[0U].source_bsp_texture_index == 1U);
    CHECK(texture_assets[0U].source_archive_ordinal == 0U);
    CHECK(textures.statistics().embedded_texture_count == 0U);
    CHECK(textures.statistics().wad3_texture_count == 1U);
    CHECK(textures.statistics().wad_archive_resolved_count == 1U);
    REQUIRE(textures.archive_metadata().size() == 1U);
    CHECK(textures.archive_metadata()[0U].status ==
        assets::WorldTextureArchiveStatus::resolved);
    CHECK(built.statistics.input_material_reference_count == 2U);
    CHECK(built.statistics.unique_material_reference_count == 1U);
    CHECK(built.statistics.deduplicated_material_reference_count == 1U);
    CHECK(built.statistics.decoded_texture_count == 1U);
}

TEST_CASE("Brush render library delegates malformed texture and lightmap data",
    "[goldsrc-brush][render-library][shared-codecs][malformed]")
{
    ScopedLocalResourceTestRoot temporary;
    SECTION("shared texture-import pipeline owns palette validation")
    {
        const auto source = make_brush_render_fixture(false, true);
        auto built = build_library(source, make_environment(temporary));
        REQUIRE_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
            brush::GoldSrcBrushRenderLibraryErrorCode::texture_import_failed);
        REQUIRE(built.error->texture_error);
        CHECK(static_cast<int>(*built.error->texture_error) ==
            static_cast<int>(hlclient::goldsrc::WorldTextureImportErrorCode::
                bsp_texture_source_parse_failed));
        CHECK_FALSE(built.library);
    }
    SECTION("shared lightmap importer owns lighting-range validation")
    {
        const auto source = make_brush_render_fixture(
            false,
            false,
            (2U * kLightmapBytesPerQuad) - 1U);
        auto built = build_library(source, make_environment(temporary));
        REQUIRE_FALSE(built);
        REQUIRE(built.error);
        CHECK(built.error->code ==
            brush::GoldSrcBrushRenderLibraryErrorCode::lightmap_import_failed);
        CHECK(built.error->lightmap_error ==
            hlclient::goldsrc::lightmaps::
                GoldSrcWorldLightmapImportErrorCode::
                    lightmap_range_out_of_bounds);
        CHECK_FALSE(built.library);
    }
}

TEST_CASE("Brush render library bounds its synchronous texture driver",
    "[goldsrc-brush][render-library][driver-limit]")
{
    ScopedLocalResourceTestRoot temporary;
    const auto source = make_brush_render_fixture();
    auto limits = brush::GoldSrcBrushRenderLibraryLimits{};
    limits.maximum_texture_import_updates = 1U;
    auto built = build_library(source, make_environment(temporary), limits);
    REQUIRE_FALSE(built);
    REQUIRE(built.error);
    CHECK(built.error->code ==
        brush::GoldSrcBrushRenderLibraryErrorCode::
            texture_import_update_limit_exceeded);
    CHECK(built.statistics.texture_import_update_count == 1U);
    CHECK_FALSE(built.library);
}

TEST_CASE("Empty brush source publishes an empty compatible library",
    "[goldsrc-brush][render-library][empty]")
{
    const bsp::GoldSrcBspParsedDocument document;
    auto built = brush::GoldSrcBrushRenderLibraryBuilder::build(
        document,
        {},
        nullptr);
    REQUIRE(built);
    REQUIRE(built.library);
    CHECK_FALSE(built.library->render_package());
    CHECK(built.library->models().empty());
    CHECK(built.statistics.source_model_count == 0U);
}

TEST_CASE("Brush render library error names are stable",
    "[goldsrc-brush][render-library][errors]")
{
    CHECK(brush::to_string(
        brush::GoldSrcBrushRenderLibraryErrorCode::invalid_model_order) ==
        "invalid_model_order");
    CHECK(brush::to_string(
        brush::GoldSrcBrushRenderLibraryErrorCode::surface_range_mismatch) ==
        "surface_range_mismatch");
    CHECK(brush::to_string(
        brush::GoldSrcBrushRenderLibraryErrorCode::textures_incomplete) ==
        "textures_incomplete");
    CHECK(brush::to_string(
        brush::GoldSrcBrushRenderLibraryErrorCode::source_document_mismatch) ==
        "source_document_mismatch");
    CHECK(brush::to_string(
        static_cast<brush::GoldSrcBrushRenderLibraryErrorCode>(255)) ==
        "unknown");
}

} // namespace
