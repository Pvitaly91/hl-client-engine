#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>

#include "synthetic_goldsrc_bsp_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

namespace bsp = hlclient::goldsrc::bsp;
namespace fixture = hlclient::tests;
using Catch::Approx;

[[nodiscard]] fixture::SyntheticBspBuilder make_world_and_submodel_builder(
    const bool duplicate_submodel_face_range = false)
{
    fixture::SyntheticBspBuilder builder;
    constexpr std::array vertices{
        fixture::SyntheticBspVector3{0.0F, 0.0F, 0.0F},
        fixture::SyntheticBspVector3{64.0F, 0.0F, 0.0F},
        fixture::SyntheticBspVector3{0.0F, 64.0F, 0.0F},
        fixture::SyntheticBspVector3{128.0F, 0.0F, 0.0F},
        fixture::SyntheticBspVector3{192.0F, 0.0F, 0.0F},
        fixture::SyntheticBspVector3{128.0F, 64.0F, 0.0F},
    };
    constexpr std::array edges{
        fixture::SyntheticBspEdge{0U, 0U},
        fixture::SyntheticBspEdge{0U, 1U},
        fixture::SyntheticBspEdge{1U, 2U},
        fixture::SyntheticBspEdge{2U, 0U},
        fixture::SyntheticBspEdge{3U, 4U},
        fixture::SyntheticBspEdge{4U, 5U},
        fixture::SyntheticBspEdge{5U, 3U},
    };
    constexpr std::array<std::int32_t, 6U> surfedges{
        -3, -2, -1, -6, -5, -4};
    std::array faces{fixture::SyntheticBspFace{}, fixture::SyntheticBspFace{}};
    faces[0].surfedge_count = 3;
    faces[1].first_surfedge = 3;
    faces[1].surfedge_count = 3;
    auto node = fixture::SyntheticBspNode{};
    node.face_count = 2U;
    std::array leaves{fixture::SyntheticBspLeaf{}, fixture::SyntheticBspLeaf{}};
    leaves[0].contents = -2;
    leaves[0].marksurface_count = 0U;
    leaves[1].marksurface_count = 2U;
    constexpr std::array<std::uint16_t, 2U> marksurfaces{0U, 1U};
    auto world_model = fixture::SyntheticBspModel{};
    world_model.face_count = 1;
    auto submodel = fixture::SyntheticBspModel{};
    submodel.minimum = {127.0F, -1.0F, -1.0F};
    submodel.maximum = {193.0F, 65.0F, 1.0F};
    submodel.first_face = 1;
    submodel.face_count = 1;
    std::vector models{world_model, submodel};
    if (duplicate_submodel_face_range) {
        models.push_back(submodel);
    }

    builder.set_vertices(vertices)
        .set_edges(edges)
        .set_surfedges(surfedges)
        .set_faces(faces)
        .set_nodes(std::span{&node, 1U})
        .set_leaves(leaves)
        .set_marksurfaces(marksurfaces)
        .set_models(models);
    return builder;
}

TEST_CASE("Model zero is the only emitted world model", "[goldsrc-bsp][models][world]")
{
    const auto result = bsp::GoldSrcBspParser::parse(
        fixture::literal_minimal_goldsrc_bsp_v30());
    REQUIRE(result);
    const auto& world = result.document->world_asset;
    CHECK(world.statistics.source_model_count == 1U);
    CHECK(world.statistics.world_model_source_face_count == 1U);
    CHECK(world.statistics.skipped_submodel_face_count == 0U);
    CHECK(world.surfaces.size() == 1U);
    REQUIRE(world.source_model_bounds.has_value());
    CHECK(world.source_model_bounds->minimum.x == Approx(-1.0F));
    CHECK(world.source_model_bounds->maximum.z == Approx(1.0F));
}

TEST_CASE("An empty model lump cannot publish a world", "[goldsrc-bsp][models][empty]")
{
    fixture::SyntheticBspBuilder builder;
    const auto result = bsp::GoldSrcBspParser::parse(
        builder.clear_lump(fixture::SyntheticBspLumpId::models).build());
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.document.has_value());
    REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_model_reference);
}

TEST_CASE("World model face ranges are signed and checked", "[goldsrc-bsp][models][range]")
{
    SECTION("valid range")
    {
        REQUIRE(bsp::GoldSrcBspParser::parse(
            fixture::literal_minimal_goldsrc_bsp_v30()));
    }

    SECTION("negative first face")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .write_i32(fixture::SyntheticBspLumpId::models, 56U, -1)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_model_reference);
    }

    SECTION("negative face count")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .write_i32(fixture::SyntheticBspLumpId::models, 60U, -1)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_model_reference);
    }

    SECTION("face range overflow")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .write_i32(fixture::SyntheticBspLumpId::models, 56U, 1)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_model_reference);
    }
}

TEST_CASE("Brush submodels are validated but their faces are not emitted",
    "[goldsrc-bsp][models][submodel]")
{
    const auto result = bsp::GoldSrcBspParser::parse(
        make_world_and_submodel_builder().build());
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    const auto& world = result.document->world_asset;
    CHECK(world.statistics.source_model_count == 2U);
    CHECK(world.statistics.source_face_count == 2U);
    CHECK(world.statistics.world_model_source_face_count == 1U);
    CHECK(world.statistics.skipped_submodel_face_count == 1U);
    CHECK(world.surfaces.size() == 1U);
    CHECK(world.vertices.size() == 3U);
    CHECK(world.bounds.maximum.x == Approx(64.0F));
}

TEST_CASE("Render model face ranges are pairwise non-overlapping",
    "[goldsrc-bsp][models][range][overlap]")
{
    SECTION("valid ordered adjacent ranges")
    {
        const auto result = bsp::GoldSrcBspParser::parse(
            make_world_and_submodel_builder().build());
        REQUIRE(result);
        REQUIRE(result.document->brush_submodels.size() == 1U);
        CHECK(result.document->world_asset.surfaces.size() == 1U);
        CHECK(result.document->brush_submodels[0U].geometry.surfaces.size() ==
            1U);
    }

    SECTION("world and submodel overlap")
    {
        auto builder = make_world_and_submodel_builder();
        auto& models = builder.lump(fixture::SyntheticBspLumpId::models);
        fixture::synthetic_write_i32le(models, 64U + 56U, 0);
        const auto bytes = builder.build();
        const auto model_lump_offset = static_cast<std::size_t>(
            fixture::synthetic_read_i32le(
                bytes,
                fixture::synthetic_lump_descriptor_offset(
                    fixture::SyntheticBspLumpId::models)));
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.document);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcBspErrorCode::invalid_model_reference);
        CHECK(result.error->lump_id == bsp::GoldSrcBspLumpId::models);
        CHECK(result.error->element_index == 1U);
        CHECK(result.error->byte_offset ==
            model_lump_offset + 64U + 56U);
    }

    SECTION("two submodels overlap")
    {
        auto builder = make_world_and_submodel_builder(true);
        const auto bytes = builder.build();
        const auto model_lump_offset = static_cast<std::size_t>(
            fixture::synthetic_read_i32le(
                bytes,
                fixture::synthetic_lump_descriptor_offset(
                    fixture::SyntheticBspLumpId::models)));
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(result.document);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            bsp::GoldSrcBspErrorCode::invalid_model_reference);
        CHECK(result.error->lump_id == bsp::GoldSrcBspLumpId::models);
        CHECK(result.error->element_index == 2U);
        CHECK(result.error->byte_offset ==
            model_lump_offset + (2U * 64U) + 56U);
    }
}

TEST_CASE("Every model, including a skipped submodel, is structurally validated",
    "[goldsrc-bsp][models][submodel][mutation]")
{
    auto builder = make_world_and_submodel_builder();
    auto& models = builder.lump(fixture::SyntheticBspLumpId::models);
    fixture::synthetic_write_i32le(models, 64U + 36U, 99);
    const auto result = bsp::GoldSrcBspParser::parse(builder.build());
    REQUIRE_FALSE(result);
    REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_model_reference);
    REQUIRE(result.error->element_index == 1U);
}

TEST_CASE("World and hull headnodes are checked against their distinct domains",
    "[goldsrc-bsp][models][headnode]")
{
    SECTION("world hull node cannot use leaf-child encoding")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .write_i32(fixture::SyntheticBspLumpId::models, 36U, -1)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_model_reference);
    }

    SECTION("world hull node out of range")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .write_i32(fixture::SyntheticBspLumpId::models, 36U, 1)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_model_reference);
    }

    SECTION("collision hull clipnode out of range")
    {
        const auto bytes = fixture::SyntheticBspCorruptor{
            fixture::literal_minimal_goldsrc_bsp_v30()}
                               .write_i32(fixture::SyntheticBspLumpId::models, 40U, 0)
                               .take();
        const auto result = bsp::GoldSrcBspParser::parse(bytes);
        REQUIRE_FALSE(result);
        REQUIRE(result.error->code == bsp::GoldSrcBspErrorCode::invalid_model_reference);
    }
}

TEST_CASE("Entity bytes do not instantiate or alter geometry", "[goldsrc-bsp][models][entities]")
{
    fixture::SyntheticBspBuilder builder;
    constexpr std::array entity_bytes{
        std::byte{0x7B}, std::byte{0x22}, std::byte{0x78}, std::byte{0x22}, std::byte{0x7D}};
    builder.lump(fixture::SyntheticBspLumpId::entities).assign(
        entity_bytes.begin(), entity_bytes.end());
    const auto result = bsp::GoldSrcBspParser::parse(builder.build());
    REQUIRE(result);
    CHECK(result.document->world_asset.surfaces.size() == 1U);
    CHECK(result.document->world_asset.vertices.size() == 4U);
    CHECK(result.document->lump_element_counts[bsp::goldsrc_bsp_lump_index(
              bsp::GoldSrcBspLumpId::entities)] == entity_bytes.size());
}

} // namespace
