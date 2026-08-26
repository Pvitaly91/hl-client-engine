#include <hlclient/entity_visual/entity_visual_asset_library.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_studio_model_source_bundle.hpp>

#include "entity_visual/entity_visual_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

namespace entity = hlclient::entity_visual;
namespace fixture = hlclient::tests::entity_visual_fixture;
using hlclient::tests::ScopedLocalResourceTestRoot;

template <typename Type>
concept HasNativePathSurface =
    requires(const Type& value) { value.native_path; } ||
    requires(const Type& value) { value.filesystem_path; } ||
    requires(const Type& value) { value.absolute_path; };

[[nodiscard]] std::vector<entity::EntityVisualProjectionState> projections(
    const std::vector<std::uint32_t>& slots)
{
    std::vector<std::uint32_t> numbers;
    std::vector<entity::SyntheticEntityVisualInput> inputs(slots.size());
    numbers.reserve(slots.size());
    for (std::size_t index = 0U; index < slots.size(); ++index) {
        const auto number = static_cast<std::uint32_t>(index + 1U);
        numbers.push_back(number);
        inputs[index].entity_number = number;
        inputs[index].model_reference =
            entity::EntityVisualModelReference::synthetic_model_slot(
                slots[index]);
    }
    const auto snapshot = fixture::synthetic_snapshot(numbers);
    return fixture::project(snapshot, std::move(inputs));
}

[[nodiscard]] entity::EntityVisualAssetReuseEvidence reuse_evidence(
    const entity::EntityVisualAssetRecord& record,
    std::vector<hlclient::assets::AssetSourceFingerprint> fingerprints = {})
{
    if (fingerprints.empty()) {
        fingerprints.assign(record.source_fingerprints().begin(),
            record.source_fingerprints().end());
    }
    return {
        record.approved_source_key(),
        record.kind(),
        record.importer_category(),
        record.compatibility_profile(),
        std::string{record.importer_id()},
        record.total_source_bytes(),
        std::move(fingerprints),
    };
}

[[nodiscard]] hlclient::assets::AssetSourceFingerprint source_fingerprint(
    const std::string_view bytes) noexcept
{
    return hlclient::goldsrc::visual_assets::goldsrc_studio_source_fingerprint(
        std::as_bytes(std::span{bytes.data(), bytes.size()}));
}

TEST_CASE("Visual asset library loads unique referenced Studio and Sprite assets",
          "[entity-visual][asset-library][on-demand][dedup]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/test.mdl", "studio");
    root.write("valve", "sprites/test.spr", "sprite");
    root.write("valve", "models/unused.mdl", "unused");
    auto resources = fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/test.mdl", 1U, 6U, 0U},
        {2U, "sprites/test.spr", 2U, 6U, 0U},
        {2U, "models/unused.mdl", 3U, 6U, 0U},
    });
    const auto used = projections({1U, 1U, 2U});
    entity::EntityVisualAssetLibraryBuilder builder;
    entity::SyntheticModelSlotResolver resolver;
    auto planned = builder.plan(
        200U, {}, used, resources.manifest, resolver);
    INFO((planned.error ? planned.error->context : std::string{}));
    REQUIRE(planned);
    REQUIRE(planned.plan);
    CHECK(planned.plan->unique_reference_count() == 2U);
    CHECK(planned.plan->duplicate_reference_count() == 1U);
    REQUIRE(planned.plan->requests().size() == 2U);
    // Slot 3 exists and is ready, but it was not referenced and is not loaded.
    CHECK(planned.plan->requests()[0U].model_slot() != 3U);
    CHECK(planned.plan->requests()[1U].model_slot() != 3U);

    std::vector<entity::EntityVisualAssetImportCompletion> completions;
    for (const auto& request : planned.plan->requests()) {
        if (request.model_slot() == 1U) {
            completions.push_back(fixture::studio_completion(
                request,
                {{0x10U, 0x11U}, {0x20U, 0x21U}, {0x30U, 0x31U}}));
        } else {
            completions.push_back(fixture::sprite_completion(request));
        }
    }
    auto built = builder.publish(*planned.plan, completions);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.library);
    CHECK(built.library->resource_id() == 200U);
    CHECK(built.library->resource_revision() == 1U);
    REQUIRE(built.library->records().size() == 2U);
    CHECK(built.library->references().size() == 2U);
    CHECK(built.library->statistics().studio_model_count == 1U);
    CHECK(built.library->statistics().sprite_count == 1U);
    CHECK(built.library->statistics().cumulative_import_request_count == 2U);
    const auto* model = built.library->find_exact(
        entity::EntityVisualModelReference::synthetic_model_slot(1U));
    const auto* sprite = built.library->find_exact(
        entity::EntityVisualModelReference::synthetic_model_slot(2U));
    REQUIRE(model != nullptr);
    REQUIRE(sprite != nullptr);
    CHECK(model->kind() == entity::EntityVisualAssetKind::studio_model);
    CHECK(model->model_asset() != nullptr);
    CHECK(model->sprite_asset() == nullptr);
    CHECK(model->source_fingerprints().size() == 3U);
    CHECK(sprite->kind() == entity::EntityVisualAssetKind::sprite);
    CHECK(sprite->sprite_asset() != nullptr);
    CHECK_FALSE(built.library->find_exact(
        entity::EntityVisualModelReference::synthetic_model_slot(3U)));
}

TEST_CASE("One exact approved source produces one import and multiple references",
          "[entity-visual][asset-library][exact-source-dedup]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/shared.mdl", "studio");
    auto resources = fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/shared.mdl", 1U, 6U, 0U},
        {2U, "models/shared.mdl", 2U, 6U, 0U},
    });
    const auto used = projections({1U, 2U});
    entity::EntityVisualAssetLibraryBuilder builder;
    entity::SyntheticModelSlotResolver resolver;
    auto planned = builder.plan(
        201U, {}, used, resources.manifest, resolver);
    REQUIRE(planned);
    REQUIRE(planned.plan);
    REQUIRE(planned.plan->requests().size() == 1U);
    CHECK(planned.plan->requests().front().references().size() == 2U);
    std::vector completions{
        fixture::studio_completion(planned.plan->requests().front())};
    auto built = builder.publish(*planned.plan, completions);
    REQUIRE(built);
    REQUIRE(built.library);
    CHECK(built.library->records().size() == 1U);
    CHECK(built.library->references().size() == 2U);
    REQUIRE(built.library->find_exact(
        entity::EntityVisualModelReference::synthetic_model_slot(1U)));
    REQUIRE(built.library->find_exact(
        entity::EntityVisualModelReference::synthetic_model_slot(2U)));
    CHECK(built.library->find_exact_index(
              entity::EntityVisualModelReference::synthetic_model_slot(1U)) ==
          built.library->find_exact_index(
              entity::EntityVisualModelReference::synthetic_model_slot(2U)));
}

TEST_CASE("Incremental updates preserve shared assets and advance revision once",
          "[entity-visual][asset-library][immutable][revision]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/test.mdl", "studio");
    root.write("valve", "sprites/test.spr", "sprite");
    auto resources = fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/test.mdl", 1U, 6U, 0U},
        {2U, "sprites/test.spr", 2U, 6U, 0U},
    });
    entity::EntityVisualAssetLibraryBuilder builder;
    entity::SyntheticModelSlotResolver resolver;
    const auto first_used = projections({1U});
    auto first_plan = builder.plan(
        202U, {}, first_used, resources.manifest, resolver);
    REQUIRE(first_plan);
    REQUIRE(first_plan.plan);
    std::vector first_completion{
        fixture::studio_completion(first_plan.plan->requests().front())};
    auto first = builder.publish(*first_plan.plan, first_completion);
    REQUIRE(first);
    REQUIRE(first.library);
    const auto first_library = first.library;
    const auto first_model = first_library->records().front().model_asset();

    const auto second_used = projections({1U, 2U});
    const std::array first_reuse_evidence{
        reuse_evidence(first_library->records().front())};
    auto second_plan = builder.plan(
        202U,
        {},
        second_used,
        resources.manifest,
        resolver,
        first_library,
        {},
        first_reuse_evidence);
    REQUIRE(second_plan);
    REQUIRE(second_plan.plan);
    REQUIRE(second_plan.plan->requests().size() == 1U);
    CHECK(second_plan.plan->requests().front().model_slot() == 2U);
    std::vector second_completion{
        fixture::sprite_completion(second_plan.plan->requests().front())};
    auto second = builder.publish(
        *second_plan.plan, second_completion, first_library);
    REQUIRE(second);
    REQUIRE(second.library);
    CHECK(second.library != first_library);
    CHECK(first_library->resource_revision() == 1U);
    CHECK(second.library->resource_revision() == 2U);
    CHECK(first_library->records().size() == 1U);
    CHECK(second.library->records().size() == 2U);
    CHECK(second.library->records().front().model_asset() == first_model);
    CHECK(second.library->records().front().resource_revision() == 1U);
    CHECK(second.library->records()[1U].resource_revision() == 2U);

    std::vector<entity::EntityVisualAssetReuseEvidence>
        unchanged_reuse_evidence;
    unchanged_reuse_evidence.reserve(second.library->records().size());
    for (const auto& record : second.library->records()) {
        unchanged_reuse_evidence.push_back(reuse_evidence(record));
    }
    auto unchanged_plan = builder.plan(
        202U,
        {},
        second_used,
        resources.manifest,
        resolver,
        second.library,
        {},
        unchanged_reuse_evidence);
    REQUIRE(unchanged_plan);
    REQUIRE(unchanged_plan.plan);
    CHECK(unchanged_plan.plan->requests().empty());
    auto unchanged = builder.publish(
        *unchanged_plan.plan, {}, second.library);
    REQUIRE(unchanged);
    CHECK(unchanged.library == second.library);
    CHECK(unchanged.library->resource_revision() == 2U);
}

TEST_CASE("Changed approved source identity cannot reuse a reference-bound asset",
    "[entity-visual][asset-library][incremental][source-identity]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/test.mdl", "old");
    auto first_resources = fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/test.mdl", 1U, 3U, 0U},
    });
    const auto used = projections({1U});
    entity::EntityVisualAssetLibraryBuilder builder;
    entity::SyntheticModelSlotResolver resolver;

    auto first_plan = builder.plan(
        0x20A'0001U, {}, used, first_resources.manifest, resolver);
    REQUIRE(first_plan);
    REQUIRE(first_plan.plan);
    REQUIRE(first_plan.plan->requests().size() == 1U);
    std::vector first_completion{
        fixture::studio_completion(first_plan.plan->requests().front())};
    auto first = builder.publish(*first_plan.plan, first_completion);
    REQUIRE(first);
    REQUIRE(first.library);
    const auto reference =
        entity::EntityVisualModelReference::synthetic_model_slot(1U);
    const auto* first_record = first.library->find_exact(reference);
    REQUIRE(first_record != nullptr);
    const auto first_source_key = first_record->approved_source_key();

    root.write("valve", "models/test.mdl", "changed");
    auto changed_resources = fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/test.mdl", 1U, 7U, 0U},
    });
    auto changed_plan = builder.plan(0x20A'0001U,
        {},
        used,
        changed_resources.manifest,
        resolver,
        first.library);
    INFO((changed_plan.error ? changed_plan.error->context : std::string{}));
    REQUIRE(changed_plan);
    REQUIRE(changed_plan.plan);
    REQUIRE(changed_plan.plan->entries().size() == 1U);
    CHECK_FALSE(
        changed_plan.plan->entries().front().existing_asset_library_index);
    REQUIRE(changed_plan.plan->requests().size() == 1U);
    CHECK(changed_plan.plan->requests().front().source_key() !=
          first_source_key);

    std::vector changed_completion{fixture::studio_completion(
        changed_plan.plan->requests().front(), {{0x5555U, 0x6666U}})};
    auto changed = builder.publish(
        *changed_plan.plan, changed_completion, first.library);
    INFO((changed.error ? changed.error->context : std::string{}));
    REQUIRE(changed);
    REQUIRE(changed.library);
    CHECK(changed.library != first.library);
    CHECK(first.library->resource_revision() == 1U);
    CHECK(changed.library->resource_revision() == 2U);
    REQUIRE(changed.library->references().size() == 1U);
    const auto* changed_record = changed.library->find_exact(reference);
    REQUIRE(changed_record != nullptr);
    CHECK(changed_record->approved_source_key() ==
          changed_plan.plan->requests().front().source_key());
    CHECK(changed_record->approved_source_key() != first_source_key);
}

TEST_CASE("Current bundle evidence prevents stale Studio asset reuse",
    "[entity-visual][asset-library][incremental][bundle-identity]")
{
    constexpr std::string_view main_v1{"main"};
    constexpr std::string_view main_v2{"mAin"};
    constexpr std::string_view group_v1{"seqA"};
    constexpr std::string_view group_v2{"seqB"};
    static_assert(main_v1.size() == main_v2.size());
    static_assert(group_v1.size() == group_v2.size());

    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/test.mdl", main_v1);
    root.write("valve", "models/test01.mdl", group_v1);
    auto resources = fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/test.mdl", 1U, 4U, 0U},
    });
    const auto used = projections({1U});
    entity::EntityVisualAssetLibraryBuilder builder;
    entity::SyntheticModelSlotResolver resolver;
    auto first_plan = builder.plan(
        0x20B'0001U, {}, used, resources.manifest, resolver);
    REQUIRE(first_plan);
    REQUIRE(first_plan.plan);
    REQUIRE(first_plan.plan->requests().size() == 1U);

    const auto main_fingerprint_v1 = source_fingerprint(main_v1);
    const auto main_fingerprint_v2 = source_fingerprint(main_v2);
    const auto group_fingerprint_v1 = source_fingerprint(group_v1);
    const auto group_fingerprint_v2 = source_fingerprint(group_v2);
    constexpr std::uint64_t total_source_bytes =
        main_v1.size() + group_v1.size();
    auto first_candidate =
        entity::EntityVisualImportedAssetCandidate::studio_model(
            first_plan.plan->requests().front().source_key(),
            fixture::studio_asset(),
            "model:synthetic-studio-bundle",
            total_source_bytes,
            {main_fingerprint_v1, group_fingerprint_v1});
    const std::array first_completion{
        entity::EntityVisualAssetImportCompletion{
            0U,
            entity::EntityVisualAssetImportCompletionStatus::imported,
            std::move(first_candidate)}};
    auto first = builder.publish(*first_plan.plan, first_completion);
    REQUIRE(first);
    REQUIRE(first.library);
    REQUIRE(first.library->records().size() == 1U);
    const auto original_source_key =
        first.library->records().front().approved_source_key();

    const std::array unchanged_evidence{
        reuse_evidence(first.library->records().front())};
    auto unchanged_plan = builder.plan(0x20B'0001U,
        {},
        used,
        resources.manifest,
        resolver,
        first.library,
        {},
        unchanged_evidence);
    REQUIRE(unchanged_plan);
    REQUIRE(unchanged_plan.plan);
    CHECK(unchanged_plan.plan->requests().empty());
    auto unchanged = builder.publish(
        *unchanged_plan.plan, {}, first.library);
    REQUIRE(unchanged);
    CHECK(unchanged.library == first.library);

    root.write("valve", "models/test01.mdl", group_v2);
    const std::array changed_group_evidence{reuse_evidence(
        first.library->records().front(),
        {main_fingerprint_v1, group_fingerprint_v2})};
    auto changed_group_plan = builder.plan(0x20B'0001U,
        {},
        used,
        resources.manifest,
        resolver,
        first.library,
        {},
        changed_group_evidence);
    REQUIRE(changed_group_plan);
    REQUIRE(changed_group_plan.plan);
    REQUIRE(changed_group_plan.plan->requests().size() == 1U);
    CHECK(changed_group_plan.plan->requests().front().source_key() ==
          original_source_key);
    auto changed_group_candidate =
        entity::EntityVisualImportedAssetCandidate::studio_model(
            changed_group_plan.plan->requests().front().source_key(),
            fixture::studio_asset(),
            "model:synthetic-studio-bundle",
            total_source_bytes,
            {main_fingerprint_v1, group_fingerprint_v2});
    const std::array changed_group_completion{
        entity::EntityVisualAssetImportCompletion{
            0U,
            entity::EntityVisualAssetImportCompletionStatus::imported,
            std::move(changed_group_candidate)}};
    auto changed_group = builder.publish(*changed_group_plan.plan,
        changed_group_completion,
        first.library);
    REQUIRE(changed_group);
    REQUIRE(changed_group.library);
    CHECK(changed_group.library->resource_revision() == 2U);

    root.write("valve", "models/test.mdl", main_v2);
    const auto* current_record = changed_group.library->find_exact(
        entity::EntityVisualModelReference::synthetic_model_slot(1U));
    REQUIRE(current_record != nullptr);
    const std::array changed_main_evidence{reuse_evidence(*current_record,
        {main_fingerprint_v2, group_fingerprint_v2})};
    auto changed_main_plan = builder.plan(0x20B'0001U,
        {},
        used,
        resources.manifest,
        resolver,
        changed_group.library,
        {},
        changed_main_evidence);
    REQUIRE(changed_main_plan);
    REQUIRE(changed_main_plan.plan);
    REQUIRE(changed_main_plan.plan->requests().size() == 1U);
    CHECK(changed_main_plan.plan->requests().front().source_key() ==
          original_source_key);
    auto changed_main_candidate =
        entity::EntityVisualImportedAssetCandidate::studio_model(
            changed_main_plan.plan->requests().front().source_key(),
            fixture::studio_asset(),
            "model:synthetic-studio-bundle",
            total_source_bytes,
            {main_fingerprint_v2, group_fingerprint_v2});
    const std::array changed_main_completion{
        entity::EntityVisualAssetImportCompletion{
            0U,
            entity::EntityVisualAssetImportCompletionStatus::imported,
            std::move(changed_main_candidate)}};
    auto changed_main = builder.publish(*changed_main_plan.plan,
        changed_main_completion,
        changed_group.library);
    REQUIRE(changed_main);
    REQUIRE(changed_main.library);
    CHECK(changed_main.library->resource_revision() == 3U);
    const auto* final_record = changed_main.library->find_exact(
        entity::EntityVisualModelReference::synthetic_model_slot(1U));
    REQUIRE(final_record != nullptr);
    REQUIRE(final_record->source_fingerprints().size() == 2U);
    CHECK(final_record->source_fingerprints()[0U] == main_fingerprint_v2);
    CHECK(final_record->source_fingerprints()[1U] == group_fingerprint_v2);
}

TEST_CASE("Identical virtual names in independent roots are not deduplicated",
          "[entity-visual][asset-library][root-identity]")
{
    entity::EntityVisualAssetLibraryBuilder builder;
    entity::SyntheticModelSlotResolver resolver;
    ScopedLocalResourceTestRoot first_root;
    first_root.write("valve", "maps/test_map.bsp", "map");
    first_root.write("valve", "models/same.mdl", "first");
    auto first_resources = fixture::manifest(first_root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/same.mdl", 1U, 5U, 0U},
    });
    const auto first_used = projections({1U});
    auto first_plan = builder.plan(
        203U, {}, first_used, first_resources.manifest, resolver);
    REQUIRE(first_plan);
    REQUIRE(first_plan.plan);
    std::vector first_completion{
        fixture::studio_completion(first_plan.plan->requests().front())};
    auto first = builder.publish(*first_plan.plan, first_completion);
    REQUIRE(first);

    ScopedLocalResourceTestRoot second_root;
    second_root.write("valve", "maps/test_map.bsp", "map");
    second_root.write("valve", "models/same.mdl", "second");
    auto second_resources = fixture::manifest(second_root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/same.mdl", 2U, 6U, 0U},
    });
    const auto second_used = projections({2U});
    auto second_plan = builder.plan(
        203U,
        {},
        second_used,
        second_resources.manifest,
        resolver,
        first.library);
    REQUIRE(second_plan);
    REQUIRE(second_plan.plan);
    REQUIRE(second_plan.plan->requests().size() == 1U);
    CHECK(second_plan.plan->requests().front().source_key().root_id() !=
          first.library->records().front().approved_source_key().root_id());
    std::vector second_completion{
        fixture::studio_completion(second_plan.plan->requests().front())};
    auto second = builder.publish(
        *second_plan.plan, second_completion, first.library);
    REQUIRE(second);
    REQUIRE(second.library);
    CHECK(second.library->records().size() == 2U);
}

TEST_CASE("Asset library limits fail transactionally",
          "[entity-visual][asset-library][limits][transactional]")
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/one.mdl", "studio-one");
    root.write("valve", "models/two.mdl", "studio-two");
    auto resources = fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/one.mdl", 1U, 10U, 0U},
        {2U, "models/two.mdl", 2U, 10U, 0U},
    });
    const auto used = projections({1U, 2U});
    entity::EntityVisualAssetLibraryBuilder builder;
    entity::SyntheticModelSlotResolver resolver;

    SECTION("pending and per-update caps")
    {
        auto limits = entity::EntityVisualAssetLibraryLimits{};
        limits.maximum_pending_imports = 1U;
        auto pending = builder.plan(
            204U, {}, used, resources.manifest, resolver, {}, limits);
        CHECK_FALSE(pending);
        REQUIRE(pending.error);
        CHECK(pending.error->code == entity::EntityVisualAssetLibraryErrorCode::
                                         pending_import_limit_exceeded);

        limits.maximum_pending_imports = 2U;
        limits.maximum_imports_per_update = 1U;
        auto per_update = builder.plan(
            204U, {}, used, resources.manifest, resolver, {}, limits);
        CHECK_FALSE(per_update);
        REQUIRE(per_update.error);
        CHECK(per_update.error->code ==
              entity::EntityVisualAssetLibraryErrorCode::
                  imports_per_update_limit_exceeded);
    }
    SECTION("asset count cap publishes no partial library")
    {
        auto planned = builder.plan(
            205U, {}, used, resources.manifest, resolver);
        REQUIRE(planned);
        REQUIRE(planned.plan);
        std::vector<entity::EntityVisualAssetImportCompletion> completions;
        for (const auto& request : planned.plan->requests()) {
            completions.push_back(fixture::studio_completion(request));
        }
        auto limits = entity::EntityVisualAssetLibraryLimits{};
        limits.maximum_asset_count = 1U;
        auto built = builder.publish(
            *planned.plan, completions, {}, limits);
        CHECK_FALSE(built);
        CHECK_FALSE(built.library);
        REQUIRE(built.error);
        CHECK(built.error->code == entity::EntityVisualAssetLibraryErrorCode::
                                        asset_count_limit_exceeded);
    }
    SECTION("source texture and geometry byte caps")
    {
        const auto one = projections({1U});
        auto planned = builder.plan(
            206U, {}, one, resources.manifest, resolver);
        REQUIRE(planned);
        REQUIRE(planned.plan);
        std::vector completion{
            fixture::studio_completion(planned.plan->requests().front())};

        auto source_limits = entity::EntityVisualAssetLibraryLimits{};
        source_limits.maximum_total_model_source_bytes = 1U;
        auto source = builder.publish(
            *planned.plan, completion, {}, source_limits);
        CHECK_FALSE(source);
        REQUIRE(source.error);
        CHECK(source.error->code ==
              entity::EntityVisualAssetLibraryErrorCode::
                  model_source_byte_limit_exceeded);

        auto texture_limits = entity::EntityVisualAssetLibraryLimits{};
        texture_limits.maximum_total_texture_rgba_bytes = 1U;
        auto texture = builder.publish(
            *planned.plan, completion, {}, texture_limits);
        CHECK_FALSE(texture);
        REQUIRE(texture.error);
        CHECK(texture.error->code ==
              entity::EntityVisualAssetLibraryErrorCode::
                  texture_rgba_byte_limit_exceeded);

        auto geometry_limits = entity::EntityVisualAssetLibraryLimits{};
        geometry_limits.maximum_total_geometry_bytes = 1U;
        auto geometry = builder.publish(
            *planned.plan, completion, {}, geometry_limits);
        CHECK_FALSE(geometry);
        REQUIRE(geometry.error);
        CHECK(geometry.error->code ==
              entity::EntityVisualAssetLibraryErrorCode::
                  geometry_byte_limit_exceeded);
    }
}

TEST_CASE("Asset library owns published inputs and exposes no native paths",
    "[entity-visual][asset-library][ownership][boundary]")
{
    STATIC_REQUIRE_FALSE(
        HasNativePathSurface<entity::EntityVisualAssetRecord>);
    STATIC_REQUIRE_FALSE(
        HasNativePathSurface<entity::EntityVisualAssetLibraryState>);
    STATIC_REQUIRE_FALSE(
        HasNativePathSurface<entity::EntityVisualApprovedSourceKey>);
    STATIC_REQUIRE_FALSE(
        HasNativePathSurface<entity::EntityVisualAssetReuseEvidence>);

    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    root.write("valve", "models/owned.mdl", "studio");
    auto resources = fixture::manifest(root, {
        {2U, "maps/test_map.bsp", 9U, 3U, 0U},
        {2U, "models/owned.mdl", 1U, 6U, 0U},
    });
    entity::EntityVisualAssetLibraryBuilder builder;
    entity::SyntheticModelSlotResolver resolver;
    const auto used = projections({1U});
    auto planned = builder.plan(
        207U, {}, used, resources.manifest, resolver);
    REQUIRE(planned);
    REQUIRE(planned.plan);

    std::weak_ptr<const hlclient::assets::ModelAsset> weak_model;
    auto published = [&] {
        auto completion =
            fixture::studio_completion(planned.plan->requests().front());
        REQUIRE(completion.candidate);
        weak_model = completion.candidate->model_asset();
        const std::array completions{completion};
        return builder.publish(*planned.plan, completions);
    }();
    REQUIRE(published);
    REQUIRE(published.library);
    CHECK_FALSE(weak_model.expired());
    REQUIRE(published.library->records().size() == 1U);
    CHECK(published.library->records()[0U].model_asset() == weak_model.lock());
    published.library.reset();
    CHECK(weak_model.expired());
}

} // namespace
