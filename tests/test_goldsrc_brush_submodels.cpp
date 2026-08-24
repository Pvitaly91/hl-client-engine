#include <hlclient/assets/asset_source.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>

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
        // The brush face deliberately stores every edge backwards. Its four
        // negative surfedges must reconstruct the same counter-clockwise loop.
        fixture::SyntheticBspEdge{5U, 4U},
        fixture::SyntheticBspEdge{6U, 5U},
        fixture::SyntheticBspEdge{7U, 6U},
        fixture::SyntheticBspEdge{4U, 7U},
    };
    std::array<std::int32_t, 8U> surfedges{1, 2, 3, 4, -5, -6, -7, -8};
    if (malformed_brush_face) {
        surfedges[5U] = -7;
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
