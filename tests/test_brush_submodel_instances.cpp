#include <hlclient/goldsrc/brush_models/brush_submodel_instances.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace brush = hlclient::goldsrc::brush_models;
namespace bsp = hlclient::goldsrc::bsp;
namespace spatial = hlclient::world_spatial;

[[nodiscard]] std::vector<std::byte> bytes_of(const std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const auto character : text) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    return bytes;
}

[[nodiscard]] bsp::GoldSrcEntityDocumentParseResult parse_entities(
    const std::string_view text)
{
    return bsp::GoldSrcEntityDocumentParser::parse(bytes_of(text));
}

enum class SpatialFixtureProfile {
    normal,
    only_leaf_zero,
    cycle,
    invalid_root,
};

[[nodiscard]] spatial::WorldSpatialPackage make_spatial_package(
    const SpatialFixtureProfile profile = SpatialFixtureProfile::normal)
{
    std::vector<spatial::WorldSpatialPlane> planes{
        spatial::WorldSpatialPlane{{1.0F, 0.0F, 0.0F}, 0.0F, 0},
    };
    std::vector<spatial::WorldSpatialNode> nodes{
        spatial::WorldSpatialNode{
            0U,
            {
                spatial::WorldSpatialNodeChild{
                    spatial::WorldSpatialNodeChildKind::leaf, 1U},
                spatial::WorldSpatialNodeChild{
                    spatial::WorldSpatialNodeChildKind::leaf, 2U},
            },
            {{-128.0F, -128.0F, -128.0F}, {128.0F, 128.0F, 128.0F}},
            std::nullopt,
            std::nullopt,
        },
    };
    if (profile == SpatialFixtureProfile::only_leaf_zero) {
        nodes[0U].children = {
            spatial::WorldSpatialNodeChild{
                spatial::WorldSpatialNodeChildKind::leaf, 0U},
            spatial::WorldSpatialNodeChild{
                spatial::WorldSpatialNodeChildKind::leaf, 0U},
        };
    } else if (profile == SpatialFixtureProfile::cycle) {
        nodes[0U].children[0U] = spatial::WorldSpatialNodeChild{
            spatial::WorldSpatialNodeChildKind::node, 0U};
    }

    std::vector<spatial::WorldSpatialLeaf> leaves{
        spatial::WorldSpatialLeaf{
            0U,
            -2,
            {{-128.0F, -128.0F, -128.0F}, {128.0F, 128.0F, 128.0F}},
            std::nullopt,
            spatial::WorldLeafSurfaceMembership{0U, 0U, {}},
            false,
            true,
        },
        spatial::WorldSpatialLeaf{
            1U,
            -1,
            {{0.0F, -128.0F, -128.0F}, {128.0F, 128.0F, 128.0F}},
            0U,
            spatial::WorldLeafSurfaceMembership{1U, 0U, {}},
            true,
            false,
        },
        spatial::WorldSpatialLeaf{
            2U,
            -1,
            {{-128.0F, -128.0F, -128.0F}, {0.0F, 128.0F, 128.0F}},
            0U,
            spatial::WorldLeafSurfaceMembership{2U, 0U, {}},
            true,
            false,
        },
    };
    spatial::WorldPvsTable pvs{
        1U,
        2U,
        {{std::byte{0x03U}}},
        {std::nullopt, 0U, 0U},
        0U,
    };
    return spatial::WorldSpatialPackage{
        std::move(planes),
        std::move(nodes),
        std::move(leaves),
        std::move(pvs),
        spatial::WorldSpatialModelMetadata{
            profile == SpatialFixtureProfile::invalid_root ? 9U : 0U,
            2U,
            {{-128.0F, -128.0F, -128.0F}, {128.0F, 128.0F, 128.0F}},
        },
        {},
        spatial::WorldSpatialCompatibilityProfile::
            goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records,
    };
}

[[nodiscard]] brush::BrushSubmodelModelMetadata model_one(
    const bool geometry_present = true)
{
    return brush::BrushSubmodelModelMetadata{
        1U,
        {},
        {{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}},
        geometry_present,
    };
}

TEST_CASE("One static opaque brush entity builds a renderable instance",
    "[goldsrc-brush][instances]")
{
    const auto document = parse_entities(
        R"({"classname" "worldspawn"}{"classname" "func_door" "model" "*1" "origin" "10 2 3"})");
    REQUIRE(document);
    const std::vector models{model_one()};
    const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
        *document.document, models, 2U, make_spatial_package());
    REQUIRE(built);
    REQUIRE(built.instance_set->size() == 1U);
    const auto& instance = built.instance_set->instances()[0U];
    CHECK(instance.source_entity_ordinal == 1U);
    CHECK(instance.source_model_index == 1U);
    CHECK(instance.classname_category ==
        brush::GoldSrcBrushClassnameCategory::function_entity);
    CHECK(instance.status ==
        brush::BrushSubmodelInstanceStatus::supported_static_opaque);
    CHECK(instance.renderable());
    REQUIRE(instance.transform);
    REQUIRE(instance.transformed_bounds);
    CHECK(instance.transformed_bounds->minimum.x == Catch::Approx(9.0F));
    CHECK(instance.transformed_bounds->maximum.x == Catch::Approx(11.0F));
    REQUIRE(instance.touched_world_leaves.size() == 1U);
    CHECK(instance.touched_world_leaves[0U] == 1U);
    CHECK(built.instance_set->statistics().source_entity_count == 2U);
    CHECK(built.instance_set->statistics().supported_static_opaque_count == 1U);
}

TEST_CASE("Two brush instances share one ordered model metadata record",
    "[goldsrc-brush][instances][reuse]")
{
    const auto document = parse_entities(
        R"({"model" "*1" "origin" "10 0 0"}{"model" "*1" "origin" "-10 0 0"})");
    REQUIRE(document);
    const std::vector models{model_one()};
    const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
        *document.document, models, 2U, make_spatial_package());
    REQUIRE(built);
    REQUIRE(built.instance_set->size() == 2U);
    CHECK(built.instance_set->instances()[0U].source_model_index == 1U);
    CHECK(built.instance_set->instances()[1U].source_model_index == 1U);
    CHECK(built.instance_set->instances()[0U].touched_world_leaves ==
        std::vector<std::uint32_t>{1U});
    CHECK(built.instance_set->instances()[1U].touched_world_leaves ==
        std::vector<std::uint32_t>{2U});
}

TEST_CASE("Brush instance builder retains every typed unsupported outcome",
    "[goldsrc-brush][instances][status]")
{
    SECTION("invalid model reference")
    {
        const auto document = parse_entities(R"({"model" "*2"})");
        REQUIRE(document);
        const std::vector models{model_one()};
        const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document, models, 2U, make_spatial_package());
        REQUIRE(built);
        CHECK(built.instance_set->instances()[0U].status ==
            brush::BrushSubmodelInstanceStatus::invalid_model_reference);
        CHECK_FALSE(built.instance_set->instances()[0U].renderable());
    }
    SECTION("missing model geometry")
    {
        const auto document = parse_entities(R"({"model" "*1"})");
        REQUIRE(document);
        const std::vector models{model_one(false)};
        const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document, models, 2U, make_spatial_package());
        REQUIRE(built);
        CHECK(built.instance_set->instances()[0U].status ==
            brush::BrushSubmodelInstanceStatus::missing_model_geometry);
    }
    SECTION("unsupported rendermode retains transform but no leaf membership")
    {
        const auto document = parse_entities(
            R"({"model" "*1" "origin" "10 0 0" "rendermode" "5"})");
        REQUIRE(document);
        const std::vector models{model_one()};
        const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document, models, 2U, make_spatial_package());
        REQUIRE(built);
        const auto& instance = built.instance_set->instances()[0U];
        CHECK(instance.status ==
            brush::BrushSubmodelInstanceStatus::unsupported_rendermode);
        CHECK(instance.transform.has_value());
        CHECK(instance.transformed_bounds.has_value());
        CHECK(instance.touched_world_leaves.empty());
        CHECK_FALSE(instance.renderable());
    }
    SECTION("nonzero source model origin is an unsupported transform")
    {
        const auto document = parse_entities(R"({"model" "*1"})");
        REQUIRE(document);
        auto metadata = model_one();
        metadata.source_model_origin = {1.0F, 0.0F, 0.0F};
        const std::vector models{metadata};
        const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document, models, 2U, make_spatial_package());
        REQUIRE(built);
        CHECK(built.instance_set->instances()[0U].status ==
            brush::BrushSubmodelInstanceStatus::unsupported_transform);
    }
    SECTION("ambiguous interpreted keys are invalid entity metadata")
    {
        const auto document = parse_entities(
            R"({"model" "*1" "origin" "0 0 0" "Origin" "1 2 3"})");
        REQUIRE(document);
        const std::vector models{model_one()};
        const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document, models, 2U, make_spatial_package());
        REQUIRE(built);
        CHECK(built.instance_set->instances()[0U].status ==
            brush::BrushSubmodelInstanceStatus::invalid_entity_metadata);
    }
}

TEST_CASE("Missing origin is deterministic zero and straddling leaves deduplicate",
    "[goldsrc-brush][instances][membership]")
{
    const auto document = parse_entities(R"({"model" "*1"})");
    REQUIRE(document);
    const std::vector models{model_one()};
    const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
        *document.document, models, 2U, make_spatial_package());
    REQUIRE(built);
    const auto& instance = built.instance_set->instances()[0U];
    REQUIRE(instance.transform);
    CHECK(instance.transform->translation.x == 0.0F);
    CHECK(instance.touched_world_leaves ==
        std::vector<std::uint32_t>{1U, 2U});
    CHECK(built.instance_set->statistics().touched_leaf_link_count == 2U);
}

TEST_CASE("Spatial query failures and unusable leaves remain typed per instance",
    "[goldsrc-brush][instances][spatial]")
{
    const auto document = parse_entities(
        R"({"model" "*1" "origin" "10 0 0"})");
    REQUIRE(document);
    const std::vector models{model_one()};

    SECTION("only leaf zero")
    {
        const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document,
            models,
            2U,
            make_spatial_package(SpatialFixtureProfile::only_leaf_zero));
        REQUIRE(built);
        CHECK(built.instance_set->instances()[0U].status ==
            brush::BrushSubmodelInstanceStatus::no_visible_leaf_membership);
        CHECK(built.instance_set->instances()[0U].touched_world_leaves.empty());
    }
    SECTION("cycle")
    {
        const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document,
            models,
            2U,
            make_spatial_package(SpatialFixtureProfile::cycle));
        REQUIRE(built);
        const auto& instance = built.instance_set->instances()[0U];
        CHECK(instance.status ==
            brush::BrushSubmodelInstanceStatus::outside_world_spatial_tree);
        CHECK(instance.spatial_query_error ==
            spatial::WorldSpatialQueryErrorCode::cycle_detected);
    }
    SECTION("invalid root")
    {
        const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document,
            models,
            2U,
            make_spatial_package(SpatialFixtureProfile::invalid_root));
        REQUIRE(built);
        CHECK(built.instance_set->instances()[0U].status ==
            brush::BrushSubmodelInstanceStatus::outside_world_spatial_tree);
        CHECK(built.instance_set->instances()[0U].spatial_query_error ==
            spatial::WorldSpatialQueryErrorCode::invalid_package);
    }
}

TEST_CASE("Instance and touched-leaf allocations enforce exact limits",
    "[goldsrc-brush][instances][limits]")
{
    const std::vector models{model_one()};
    SECTION("instance exact and plus one")
    {
        auto limits = brush::BrushSubmodelInstanceBuildLimits{};
        limits.maximum_instances = 1U;
        auto document = parse_entities(R"({"model" "*1"})");
        REQUIRE(document);
        REQUIRE(brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document, models, 2U, make_spatial_package(), limits));

        document = parse_entities(R"({"model" "*1"}{"model" "*1"})");
        REQUIRE(document);
        const auto over = brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document, models, 2U, make_spatial_package(), limits);
        REQUIRE_FALSE(over);
        CHECK(over.error->code ==
            brush::BrushSubmodelInstanceBuildErrorCode::instance_limit_exceeded);
    }
    SECTION("touched link exact and plus one")
    {
        auto limits = brush::BrushSubmodelInstanceBuildLimits{};
        limits.maximum_touched_leaf_links = 1U;
        auto document = parse_entities(
            R"({"model" "*1" "origin" "10 0 0"})");
        REQUIRE(document);
        REQUIRE(brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document, models, 2U, make_spatial_package(), limits));

        document = parse_entities(R"({"model" "*1"})");
        REQUIRE(document);
        const auto over = brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document, models, 2U, make_spatial_package(), limits);
        REQUIRE_FALSE(over);
        CHECK(over.error->code ==
            brush::BrushSubmodelInstanceBuildErrorCode::
                touched_leaf_limit_exceeded);
    }
}

TEST_CASE("Ordered model metadata is validated transactionally",
    "[goldsrc-brush][instances][model-metadata]")
{
    const auto document = parse_entities(R"({"model" "*1"})");
    REQUIRE(document);
    auto first = model_one();
    auto second = model_one();
    second.source_model_index = 2U;

    SECTION("unordered records")
    {
        const std::vector models{second, first};
        const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document, models, 3U, make_spatial_package());
        REQUIRE_FALSE(built);
        CHECK(built.error->code ==
            brush::BrushSubmodelInstanceBuildErrorCode::invalid_model_metadata);
    }
    SECTION("duplicate records")
    {
        const std::vector models{first, first};
        const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document, models, 3U, make_spatial_package());
        REQUIRE_FALSE(built);
        CHECK(built.error->code ==
            brush::BrushSubmodelInstanceBuildErrorCode::invalid_model_metadata);
    }
    SECTION("missing ordered record is a per-instance outcome")
    {
        const std::vector<decltype(first)> models;
        const auto built = brush::BrushSubmodelInstanceSetBuilder::build(
            *document.document, models, 2U, make_spatial_package());
        REQUIRE(built);
        CHECK(built.instance_set->instances()[0U].status ==
            brush::BrushSubmodelInstanceStatus::missing_model_geometry);
    }
}

} // namespace
