#include <hlclient/entity_visual/entity_visual_asset_library.hpp>

#include "entity_visual/entity_visual_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <vector>

namespace {

namespace entity = hlclient::entity_visual;
namespace fixture = hlclient::tests::entity_visual_fixture;
namespace goldsrc = hlclient::goldsrc;
using hlclient::tests::ScopedLocalResourceTestRoot;

[[nodiscard]] const entity::EntityVisualAssetImportRequest& request_for_slot(
    const entity::EntityVisualAssetLibraryPlan& plan,
    const std::uint16_t slot)
{
    const auto found = std::ranges::find_if(
        plan.requests(),
        [slot](const entity::EntityVisualAssetImportRequest& request) {
            return request.model_slot() == slot;
        });
    REQUIRE(found != plan.requests().end());
    return *found;
}

TEST_CASE("Synthetic references resolve only exact manifest model slots",
          "[entity-visual][binding][slot]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/exact.mdl", "model");
    auto resources = fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/exact.mdl", 7U, 5U, 0U},
        {2U, "models/missing.mdl", 8U, 5U, 0U},
    });
    entity::SyntheticModelSlotResolver resolver;

    const auto exact = resolver.resolve(
        entity::EntityVisualModelReference::synthetic_model_slot(7U),
        resources.manifest);
    REQUIRE(exact);
    REQUIRE(exact.model_slot);
    REQUIRE(exact.manifest_entry_offset);
    REQUIRE(exact.resource);
    CHECK(*exact.model_slot == 7U);
    CHECK(exact.resource->wire_ordinal == 1U);
    CHECK(exact.resource->resource_index == 7U);
    CHECK(exact.resource->readiness_status ==
          goldsrc::LocalResourceReadinessStatus::ready_local_file);

    const auto missing_slot = resolver.resolve(
        entity::EntityVisualModelReference::synthetic_model_slot(6U),
        resources.manifest);
    CHECK_FALSE(missing_slot);
    CHECK(missing_slot.status ==
          entity::EntityVisualModelResolutionStatus::missing_model_slot);

    const auto not_ready = resolver.resolve(
        entity::EntityVisualModelReference::synthetic_model_slot(8U),
        resources.manifest);
    CHECK_FALSE(not_ready);
    CHECK(not_ready.status == entity::EntityVisualModelResolutionStatus::
                                  manifest_entry_not_ready);
    REQUIRE(not_ready.resource);
    CHECK(not_ready.resource->readiness_status ==
          goldsrc::LocalResourceReadinessStatus::missing_local_file);
}

TEST_CASE("Stock modelindex mapping stays typed evidence pending",
          "[entity-visual][binding][stock][evidence]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/exact.mdl", "model");
    auto resources = fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/exact.mdl", 1U, 5U, 0U},
    });
    entity::EvidencePendingStockModelResolver resolver;
    const auto resolution = resolver.resolve(
        entity::EntityVisualModelReference::
            stock_modelindex_evidence_pending(1U),
        resources.manifest);
    CHECK_FALSE(resolution);
    CHECK(resolution.status == entity::EntityVisualModelResolutionStatus::
                                   stock_modelindex_mapping_evidence_pending);
    CHECK_FALSE(resolution.model_slot);
    CHECK_FALSE(resolution.manifest_entry_offset);
}

TEST_CASE("Binding categories come from typed dispatch completions not extensions",
          "[entity-visual][binding][category][no-extension-inference]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/studio-looking-like-sprite.spr", "model");
    root.write("valve", "sprites/sprite-looking-like-model.mdl", "sprite");
    auto resources = fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/studio-looking-like-sprite.spr", 1U, 5U, 0U},
        {2U, "sprites/sprite-looking-like-model.mdl", 2U, 6U, 0U},
    });
    const std::vector<std::uint32_t> numbers{1U, 2U};
    const auto snapshot = fixture::synthetic_snapshot(numbers);
    std::vector<entity::SyntheticEntityVisualInput> inputs(2U);
    inputs[0U].entity_number = 1U;
    inputs[0U].model_reference =
        entity::EntityVisualModelReference::synthetic_model_slot(1U);
    inputs[1U].entity_number = 2U;
    inputs[1U].model_reference =
        entity::EntityVisualModelReference::synthetic_model_slot(2U);
    const auto projections = fixture::project(snapshot, std::move(inputs));

    entity::EntityVisualAssetLibraryBuilder builder;
    entity::SyntheticModelSlotResolver resolver;
    auto planned = builder.plan(
        100U, {}, projections, resources.manifest, resolver);
    INFO((planned.error ? planned.error->context : std::string{}));
    REQUIRE(planned);
    REQUIRE(planned.plan);
    REQUIRE(planned.plan->requests().size() == 2U);
    std::vector<entity::EntityVisualAssetImportCompletion> completions;
    completions.push_back(fixture::studio_completion(
        request_for_slot(*planned.plan, 1U)));
    completions.push_back(fixture::sprite_completion(
        request_for_slot(*planned.plan, 2U)));

    auto built = builder.publish(*planned.plan, completions);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.library);
    REQUIRE(built.bindings.size() == 2U);
    CHECK(built.bindings[0U].status() ==
          entity::EntityVisualBindingStatus::resolved_studio_model);
    CHECK(built.bindings[0U].selected_category() ==
          entity::EntityVisualBindingCategory::studio_model);
    CHECK(built.bindings[1U].status() ==
          entity::EntityVisualBindingStatus::resolved_sprite);
    CHECK(built.bindings[1U].selected_category() ==
          entity::EntityVisualBindingCategory::sprite);
    CHECK(built.bindings[0U].resource_id() == 100U);
    CHECK(built.bindings[0U].resource_revision() == 1U);

    const auto& model_request = request_for_slot(*planned.plan, 1U);
    const auto& model_entry = resources.manifest.entries()[
        model_request.manifest_entry_offset()];
    REQUIRE(model_entry.locator());
    CHECK(model_request.source_key().root_id() ==
          model_entry.locator()->root_id());
    CHECK(model_request.source_key().virtual_resource_id() ==
          model_entry.locator()->virtual_name().id());
    CHECK(model_request.source_key().stable_identity() ==
          model_entry.locator()->expected_identity());
}

TEST_CASE("Import outcomes remain exact typed binding statuses",
          "[entity-visual][binding][failure-status]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    for (std::uint16_t slot = 1U; slot <= 5U; ++slot) {
        root.write(
            "valve",
            std::string{"models/failure"} + std::to_string(slot) + ".bin",
            "bytes");
    }
    auto resources = fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/failure1.bin", 1U, 5U, 0U},
        {2U, "models/failure2.bin", 2U, 5U, 0U},
        {2U, "models/failure3.bin", 3U, 5U, 0U},
        {2U, "models/failure4.bin", 4U, 5U, 0U},
        {2U, "models/failure5.bin", 5U, 5U, 0U},
    });
    const std::vector<std::uint32_t> numbers{1U, 2U, 3U, 4U, 5U};
    const auto snapshot = fixture::synthetic_snapshot(numbers);
    std::vector<entity::SyntheticEntityVisualInput> inputs(5U);
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
        inputs[index].entity_number = static_cast<std::uint32_t>(index + 1U);
        inputs[index].model_reference =
            entity::EntityVisualModelReference::synthetic_model_slot(
                static_cast<std::uint32_t>(index + 1U));
    }
    const auto projections = fixture::project(snapshot, std::move(inputs));
    entity::EntityVisualAssetLibraryBuilder builder;
    entity::SyntheticModelSlotResolver resolver;
    auto planned = builder.plan(
        101U, {}, projections, resources.manifest, resolver);
    REQUIRE(planned);
    REQUIRE(planned.plan);
    REQUIRE(planned.plan->requests().size() == 5U);

    constexpr entity::EntityVisualAssetImportCompletionStatus outcomes[]{
        entity::EntityVisualAssetImportCompletionStatus::
            unsupported_asset_format,
        entity::EntityVisualAssetImportCompletionStatus::asset_import_failed,
        entity::EntityVisualAssetImportCompletionStatus::
            asset_dependency_missing,
        entity::EntityVisualAssetImportCompletionStatus::asset_ambiguous,
        entity::EntityVisualAssetImportCompletionStatus::asset_limit_exceeded,
    };
    std::vector<entity::EntityVisualAssetImportCompletion> completions;
    for (std::size_t index = 0U; index < std::size(outcomes); ++index) {
        completions.push_back(entity::EntityVisualAssetImportCompletion{
            index, outcomes[index], std::nullopt});
    }
    auto built = builder.publish(*planned.plan, completions);
    REQUIRE(built);
    REQUIRE(built.library);
    REQUIRE(built.bindings.size() == 5U);
    CHECK(built.bindings[0U].status() ==
          entity::EntityVisualBindingStatus::unsupported_asset_format);
    CHECK(built.bindings[1U].status() ==
          entity::EntityVisualBindingStatus::asset_import_failed);
    CHECK(built.bindings[2U].status() ==
          entity::EntityVisualBindingStatus::asset_dependency_missing);
    CHECK(built.bindings[3U].status() ==
          entity::EntityVisualBindingStatus::asset_ambiguous);
    CHECK(built.bindings[4U].status() ==
          entity::EntityVisualBindingStatus::asset_limit_exceeded);
    CHECK(built.library->records().empty());
}

} // namespace
