#include <hlclient/assets/asset_source.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace bsp = hlclient::goldsrc::bsp;
namespace fixture = hlclient::tests;
namespace lightmaps = hlclient::goldsrc::lightmaps;

[[nodiscard]] bool exact_float(const float left, const float right) noexcept
{
    return std::bit_cast<std::uint32_t>(left) ==
        std::bit_cast<std::uint32_t>(right);
}

[[nodiscard]] bool exact_vector(
    const assets::AssetVector3 left,
    const assets::AssetVector3 right) noexcept
{
    return exact_float(left.x, right.x) && exact_float(left.y, right.y) &&
        exact_float(left.z, right.z);
}

[[nodiscard]] bool exact_bounds(
    const assets::WorldBounds& left,
    const assets::WorldBounds& right) noexcept
{
    return exact_vector(left.minimum, right.minimum) &&
        exact_vector(left.maximum, right.maximum);
}

[[nodiscard]] bool exact_world_payload(
    const assets::WorldAsset& left,
    const assets::WorldAsset& right)
{
    if (left.identity.source_name != right.identity.source_name ||
        left.coordinate_space != right.coordinate_space ||
        left.texture_coordinate_space != right.texture_coordinate_space ||
        left.source_profile != right.source_profile ||
        !exact_bounds(left.bounds, right.bounds) ||
        left.source_model_bounds.has_value() != right.source_model_bounds.has_value() ||
        left.vertices.size() != right.vertices.size() ||
        left.indices != right.indices || left.surfaces.size() != right.surfaces.size() ||
        left.materials.size() != right.materials.size()) {
        return false;
    }
    if (left.source_model_bounds &&
        !exact_bounds(*left.source_model_bounds, *right.source_model_bounds)) {
        return false;
    }
    for (std::size_t index = 0U; index < left.vertices.size(); ++index) {
        const auto& a = left.vertices[index];
        const auto& b = right.vertices[index];
        if (!exact_vector(a.position, b.position) ||
            !exact_vector(a.normal, b.normal) ||
            !exact_float(a.texture_coordinate.x, b.texture_coordinate.x) ||
            !exact_float(a.texture_coordinate.y, b.texture_coordinate.y)) {
            return false;
        }
    }
    for (std::size_t index = 0U; index < left.surfaces.size(); ++index) {
        const auto& a = left.surfaces[index];
        const auto& b = right.surfaces[index];
        if (a.first_index != b.first_index || a.index_count != b.index_count ||
            a.material_index != b.material_index || !exact_bounds(a.bounds, b.bounds) ||
            a.source_surface_ordinal != b.source_surface_ordinal ||
            a.lightmap_offset != b.lightmap_offset ||
            a.light_styles != b.light_styles ||
            a.special_surface != b.special_surface ||
            a.first_vertex != b.first_vertex || a.vertex_count != b.vertex_count) {
            return false;
        }
    }
    for (std::size_t index = 0U; index < left.materials.size(); ++index) {
        const auto& a = left.materials[index];
        const auto& b = right.materials[index];
        if (a.texture_name != b.texture_name || a.width != b.width ||
            a.height != b.height || a.texture_storage != b.texture_storage ||
            a.source_texture_flags != b.source_texture_flags ||
            a.source_texinfo_index != b.source_texinfo_index ||
            a.source_texture_index != b.source_texture_index ||
            a.compatibility_profile != b.compatibility_profile ||
            a.evidence_profile != b.evidence_profile) {
            return false;
        }
    }
    return true;
}

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

[[nodiscard]] std::vector<std::byte> two_face_bsp(
    const bool publish_brush_model,
    const bool malformed_brush_face = false,
    const std::size_t brush_model_count = 1U)
{
    fixture::SyntheticBspBuilder builder;
    constexpr std::array vertices{
        fixture::SyntheticBspVector3{0.0F, 0.0F, 0.0F},
        fixture::SyntheticBspVector3{64.0F, 0.0F, 0.0F},
        fixture::SyntheticBspVector3{64.0F, 64.0F, 0.0F},
        fixture::SyntheticBspVector3{0.0F, 64.0F, 0.0F},
        fixture::SyntheticBspVector3{128.0F, 0.0F, 0.0F},
        fixture::SyntheticBspVector3{192.0F, 0.0F, 0.0F},
        fixture::SyntheticBspVector3{192.0F, 64.0F, 0.0F},
        fixture::SyntheticBspVector3{128.0F, 64.0F, 0.0F},
    };
    constexpr std::array edges{
        fixture::SyntheticBspEdge{0U, 0U},
        fixture::SyntheticBspEdge{0U, 1U},
        fixture::SyntheticBspEdge{1U, 2U},
        fixture::SyntheticBspEdge{2U, 3U},
        fixture::SyntheticBspEdge{3U, 0U},
        fixture::SyntheticBspEdge{4U, 5U},
        fixture::SyntheticBspEdge{5U, 6U},
        fixture::SyntheticBspEdge{6U, 7U},
        fixture::SyntheticBspEdge{7U, 4U},
    };
    // Valve QBSP writes face loops clockwise relative to the side-adjusted
    // normal. Negative surfedges preserve the first wire corner while both
    // quads canonicalize back to the renderer's counter-clockwise order.
    std::array<std::int32_t, 8U> surfedges{
        -4, -3, -2, -1, -8, -7, -6, -5};
    if (malformed_brush_face) {
        surfedges[5U] = -6;
    }

    std::vector faces{fixture::SyntheticBspFace{}};
    const auto retained_brush_face_count =
        std::max<std::size_t>(1U, brush_model_count);
    for (std::size_t index = 0U;
         index < retained_brush_face_count;
         ++index) {
        auto face = fixture::SyntheticBspFace{};
        face.first_surfedge = 4;
        face.texinfo_index = 1;
        face.light_offset = 3;
        face.light_styles = {0U, 0xFFU, 0xFFU, 0xFFU};
        faces.push_back(face);
    }

    std::array texinfo{fixture::SyntheticBspTexinfo{}, fixture::SyntheticBspTexinfo{}};
    texinfo[1U].miptex_index = 1;
    texinfo[1U].flags = 1;
    const std::array<std::optional<fixture::SyntheticBspMipTexture>, 2U> textures{
        fixture::synthetic_external_texture("WORLD_MAT"),
        fixture::synthetic_external_texture("BRUSH_MAT"),
    };

    std::vector<fixture::SyntheticBspModel> models;
    models.push_back(fixture::SyntheticBspModel{});
    if (publish_brush_model) {
        models.reserve(brush_model_count + 1U);
        for (std::size_t index = 0U; index < brush_model_count; ++index) {
            auto model = fixture::SyntheticBspModel{};
            model.minimum = {127.0F, -1.0F, -1.0F};
            model.maximum = {193.0F, 65.0F, 1.0F};
            model.origin = {
                128.0F + static_cast<float>(index),
                16.0F,
                8.0F,
            };
            model.visibility_leaf_count = 0;
            model.first_face = static_cast<std::int32_t>(index + 1U);
            model.face_count = 1;
            models.push_back(model);
        }
    }

    builder.set_vertices(vertices)
        .set_edges(edges)
        .set_surfedges(surfedges)
        .set_faces(faces)
        .set_texinfo(texinfo)
        .set_texture_directory(textures)
        .set_models(models);
    builder.lump(fixture::SyntheticBspLumpId::lighting).resize(64U);
    set_entity_lump(
        builder,
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n}\n\0");
    return builder.build();
}

TEST_CASE("Canonical BSP face reconstruction publishes ordered brush submodels",
    "[goldsrc-bsp][brush-submodels][geometry]")
{
    auto source = two_face_bsp(true);
    auto parsed = bsp::GoldSrcBspParser::parse(source);
    REQUIRE(parsed);
    REQUIRE(parsed.document);
    REQUIRE(parsed.document->brush_submodels.size() == 1U);

    const auto& submodel = parsed.document->brush_submodels[0U];
    CHECK(submodel.source_model_index == 1U);
    CHECK(exact_vector(
        submodel.source_model_origin,
        assets::AssetVector3{128.0F, 16.0F, 8.0F}));
    CHECK(exact_bounds(
        submodel.source_model_bounds,
        assets::WorldBounds{
            assets::AssetVector3{127.0F, -1.0F, -1.0F},
            assets::AssetVector3{193.0F, 65.0F, 1.0F}}));
    CHECK(submodel.render_headnode == 0);

    const auto& geometry = submodel.geometry;
    CHECK(geometry.coordinate_space ==
        assets::WorldCoordinateSpace::source_native_goldsrc_z_up);
    CHECK(geometry.texture_coordinate_space ==
        assets::WorldTextureCoordinateSpace::texel_units);
    CHECK(geometry.source_profile ==
        assets::WorldGeometrySourceProfile::goldsrc_bsp_v30);
    REQUIRE(geometry.vertices.size() == 4U);
    REQUIRE(geometry.indices.size() == 6U);
    CHECK(geometry.indices ==
        std::vector<std::uint32_t>{0U, 1U, 2U, 0U, 2U, 3U});
    CHECK(exact_vector(
        geometry.vertices[0U].position,
        assets::AssetVector3{128.0F, 0.0F, 0.0F}));
    CHECK(exact_vector(
        geometry.vertices[3U].position,
        assets::AssetVector3{128.0F, 64.0F, 0.0F}));
    for (const auto& vertex : geometry.vertices) {
        CHECK(exact_vector(
            vertex.normal,
            assets::AssetVector3{0.0F, 0.0F, 1.0F}));
    }
    CHECK(exact_bounds(
        geometry.bounds,
        assets::WorldBounds{
            assets::AssetVector3{128.0F, 0.0F, 0.0F},
            assets::AssetVector3{192.0F, 64.0F, 0.0F}}));

    REQUIRE(geometry.surfaces.size() == 1U);
    const auto& surface = geometry.surfaces[0U];
    CHECK(surface.source_surface_ordinal == 1U);
    CHECK(surface.first_vertex == 0U);
    CHECK(surface.vertex_count == 4U);
    CHECK(surface.first_index == 0U);
    CHECK(surface.index_count == 6U);
    CHECK(surface.lightmap_offset == 3U);
    CHECK(surface.light_styles ==
        std::array<std::uint8_t, 4U>{0U, 0xFFU, 0xFFU, 0xFFU});
    CHECK(surface.special_surface);
    REQUIRE(geometry.materials.size() == 1U);
    CHECK(geometry.materials[0U].texture_name == "BRUSH_MAT");
    CHECK(geometry.materials[0U].source_texture_index == 1U);
    CHECK(geometry.materials[0U].source_texinfo_index == 1U);

    const auto expected_entities = parsed.document->entity_lump_bytes;
    source.clear();
    REQUIRE_FALSE(expected_entities.empty());
    CHECK(parsed.document->entity_lump_bytes == expected_entities);
    CHECK(parsed.document->brush_submodels[0U].geometry.vertices.size() == 4U);
}

TEST_CASE("World and brush models share identical canonical face geometry",
    "[goldsrc-bsp][brush-submodels][shared-builder][face-orientation]")
{
    fixture::SyntheticBspBuilder builder;
    constexpr auto vertices = fixture::synthetic_quad_vertices();
    builder.set_convex_polygon(vertices);

    std::array faces{
        fixture::SyntheticBspFace{},
        fixture::SyntheticBspFace{},
    };
    for (auto& face : faces) {
        face.light_styles = {0U, 0xFFU, 0xFFU, 0xFFU};
        face.light_offset = 0;
    }
    std::array models{
        fixture::SyntheticBspModel{},
        fixture::SyntheticBspModel{},
    };
    models[1U].first_face = 1;
    models[1U].visibility_leaf_count = 0;
    builder.set_faces(faces).set_models(models);
    builder.lump(fixture::SyntheticBspLumpId::lighting).resize(75U);

    const auto parsed = bsp::GoldSrcBspParser::parse(builder.build());
    REQUIRE(parsed);
    REQUIRE(parsed.document);
    REQUIRE(parsed.document->brush_submodels.size() == 1U);
    const auto& world = parsed.document->world_asset;
    const auto& brush = parsed.document->brush_submodels[0U].geometry;

    REQUIRE(world.vertices.size() == 4U);
    REQUIRE(brush.vertices.size() == world.vertices.size());
    CHECK(world.indices ==
        std::vector<std::uint32_t>{0U, 1U, 2U, 0U, 2U, 3U});
    CHECK(brush.indices == world.indices);
    CHECK(exact_bounds(brush.bounds, world.bounds));
    for (std::size_t index = 0U; index < world.vertices.size(); ++index) {
        const auto& world_vertex = world.vertices[index];
        const auto& brush_vertex = brush.vertices[index];
        CHECK(exact_vector(brush_vertex.position, world_vertex.position));
        CHECK(exact_vector(brush_vertex.normal, world_vertex.normal));
        CHECK(exact_float(
            brush_vertex.texture_coordinate.x,
            world_vertex.texture_coordinate.x));
        CHECK(exact_float(
            brush_vertex.texture_coordinate.y,
            world_vertex.texture_coordinate.y));
        CHECK(exact_float(
            world_vertex.texture_coordinate.x, world_vertex.position.x));
        CHECK(exact_float(
            world_vertex.texture_coordinate.y, world_vertex.position.y));
    }

    REQUIRE(world.surfaces.size() == 1U);
    REQUIRE(brush.surfaces.size() == 1U);
    const auto& world_surface = world.surfaces[0U];
    const auto& brush_surface = brush.surfaces[0U];
    CHECK(world_surface.source_surface_ordinal == 0U);
    CHECK(brush_surface.source_surface_ordinal == 1U);
    CHECK(brush_surface.first_index == world_surface.first_index);
    CHECK(brush_surface.index_count == world_surface.index_count);
    CHECK(brush_surface.material_index == world_surface.material_index);
    CHECK(exact_bounds(brush_surface.bounds, world_surface.bounds));
    CHECK(brush_surface.lightmap_offset == world_surface.lightmap_offset);
    CHECK(brush_surface.light_styles == world_surface.light_styles);
    CHECK(brush_surface.special_surface == world_surface.special_surface);
    CHECK(brush_surface.first_vertex == world_surface.first_vertex);
    CHECK(brush_surface.vertex_count == world_surface.vertex_count);

    const auto winding_dot = [](const assets::WorldAsset& geometry,
                                 const std::size_t first_index) {
        const auto& first = geometry.vertices[geometry.indices[first_index]];
        const auto& second = geometry.vertices[geometry.indices[first_index + 1U]];
        const auto& third = geometry.vertices[geometry.indices[first_index + 2U]];
        const auto abx = static_cast<double>(second.position.x) - first.position.x;
        const auto aby = static_cast<double>(second.position.y) - first.position.y;
        const auto abz = static_cast<double>(second.position.z) - first.position.z;
        const auto acx = static_cast<double>(third.position.x) - first.position.x;
        const auto acy = static_cast<double>(third.position.y) - first.position.y;
        const auto acz = static_cast<double>(third.position.z) - first.position.z;
        const auto cross_x = aby * acz - abz * acy;
        const auto cross_y = abz * acx - abx * acz;
        const auto cross_z = abx * acy - aby * acx;
        return cross_x * first.normal.x + cross_y * first.normal.y +
            cross_z * first.normal.z;
    };
    for (std::size_t first_index = 0U;
         first_index < world.indices.size();
         first_index += 3U) {
        CHECK(winding_dot(world, first_index) > 0.0);
        CHECK(winding_dot(brush, first_index) == winding_dot(world, first_index));
    }

    const auto world_extents = lightmaps::calculate_goldsrc_lightmap_extents(
        world, world_surface);
    const auto brush_extents = lightmaps::calculate_goldsrc_lightmap_extents(
        brush, brush_surface);
    REQUIRE(world_extents);
    REQUIRE(brush_extents);
    CHECK(brush_extents.extents->texture_min_s ==
        world_extents.extents->texture_min_s);
    CHECK(brush_extents.extents->texture_min_t ==
        world_extents.extents->texture_min_t);
    CHECK(brush_extents.extents->extent_s == world_extents.extents->extent_s);
    CHECK(brush_extents.extents->extent_t == world_extents.extents->extent_t);
    CHECK(brush_extents.extents->sample_width ==
        world_extents.extents->sample_width);
    CHECK(brush_extents.extents->sample_height ==
        world_extents.extents->sample_height);
    CHECK(world_extents.extents->extent_s == 64U);
    CHECK(world_extents.extents->extent_t == 64U);
    CHECK(world_extents.extents->sample_width == 5U);
    CHECK(world_extents.extents->sample_height == 5U);
}

TEST_CASE("Brush extraction preserves the M4.1 world geometry payload",
    "[goldsrc-bsp][brush-submodels][world-regression]")
{
    const auto without_brush_record = bsp::GoldSrcBspParser::parse(
        two_face_bsp(false));
    const auto with_brush_record = bsp::GoldSrcBspParser::parse(
        two_face_bsp(true));
    REQUIRE(without_brush_record);
    REQUIRE(with_brush_record);
    CHECK(exact_world_payload(
        without_brush_record.document->world_asset,
        with_brush_record.document->world_asset));

    const auto& statistics = with_brush_record.document->world_asset.statistics;
    CHECK(statistics.source_model_count == 2U);
    CHECK(statistics.source_face_count == 2U);
    CHECK(statistics.world_model_source_face_count == 1U);
    CHECK(statistics.skipped_submodel_face_count == 1U);
    CHECK(statistics.emitted_surface_count == 1U);
    CHECK(statistics.emitted_vertex_count == 4U);
    CHECK(statistics.emitted_triangle_count == 2U);
}

TEST_CASE("Every BSP model after model zero receives one ordered owning asset",
    "[goldsrc-bsp][brush-submodels][ordering]")
{
    auto source = two_face_bsp(true, false, 2U);
    auto parsed = bsp::GoldSrcBspParser::parse(source);
    REQUIRE(parsed);
    REQUIRE(parsed.document->brush_submodels.size() == 2U);
    CHECK(parsed.document->brush_submodels[0U].source_model_index == 1U);
    CHECK(parsed.document->brush_submodels[1U].source_model_index == 2U);
    CHECK(parsed.document->brush_submodels[0U].geometry.indices ==
        parsed.document->brush_submodels[1U].geometry.indices);

    source.clear();
    CHECK(parsed.document->brush_submodels[1U].geometry.vertices.size() == 4U);
}

TEST_CASE("Malformed brush geometry fails the complete BSP document transaction",
    "[goldsrc-bsp][brush-submodels][malformed]")
{
    const auto malformed = bsp::GoldSrcBspParser::parse(
        two_face_bsp(true, true));
    REQUIRE_FALSE(malformed);
    CHECK_FALSE(malformed.document.has_value());
    REQUIRE(malformed.error);
    CHECK(malformed.error->code == bsp::GoldSrcBspErrorCode::broken_face_edge_loop);
    CHECK(malformed.error->element_index == 1U);
    CHECK(malformed.error->source_model_index == 1U);
}

TEST_CASE("Historical world import does not materialize malformed brush geometry",
    "[goldsrc-bsp][brush-submodels][historical][regression]")
{
    const auto source = two_face_bsp(true, true);
    const auto historical_parse = bsp::GoldSrcBspParser::parse(
        source,
        {},
        bsp::GoldSrcBspParseOptions{false});
    REQUIRE(historical_parse);
    REQUIRE(historical_parse.document);
    CHECK(historical_parse.document->brush_submodels.empty());
    CHECK(historical_parse.document->world_asset.surfaces.size() == 1U);
    REQUIRE(historical_parse.document->world_asset.source_content_fingerprint);
    CHECK(*historical_parse.document->world_asset.source_content_fingerprint ==
        bsp::goldsrc_bsp_source_fingerprint(source));

    auto created_source = assets::AssetSource::create(
        "maps/historical-malformed-brush.bsp",
        source);
    REQUIRE(created_source);
    const bsp::GoldSrcBspWorldImporter importer;
    const auto imported = importer.import(*created_source.source);
    REQUIRE(imported);
    CHECK(imported.value().surfaces.size() == 1U);
    CHECK(imported.value().source_content_fingerprint ==
        historical_parse.document->world_asset.source_content_fingerprint);
}

TEST_CASE("Aggregate brush geometry applies exact output limits",
    "[goldsrc-bsp][brush-submodels][limits]")
{
    const auto source = two_face_bsp(true);
    auto limits = bsp::GoldSrcBspImportLimits{};
    limits.maximum_output_vertices = 8U;
    REQUIRE(bsp::GoldSrcBspParser::parse(source, limits));

    limits.maximum_output_vertices = 7U;
    const auto over = bsp::GoldSrcBspParser::parse(source, limits);
    REQUIRE_FALSE(over);
    REQUIRE(over.error);
    CHECK(over.error->code == bsp::GoldSrcBspErrorCode::geometry_limit_exceeded);
    CHECK(over.error->element_index == 1U);
    CHECK(over.error->source_model_index == 1U);
}

} // namespace
