#pragma once

#include <hlclient/assets/model_asset_types.hpp>
#include <hlclient/entity_visual/entity_visual_asset_library.hpp>

#include "../delta_test_fixture.hpp"
#include "../local_resource_readiness_test_fixture.hpp"
#include "../local_resource_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace hlclient::tests::entity_visual_fixture {

namespace asset = hlclient::assets;
namespace entity = hlclient::entity_visual;
namespace goldsrc = hlclient::goldsrc;
namespace delta_fixture = hlclient::test::delta_fixture;
namespace readiness_fixture = hlclient::tests::readiness_fixture;
namespace resource_fixture = resource_list_test_fixture;

inline constexpr delta_fixture::Field kFieldNameSimilarityIsNotEvidence[]{
    {"modelindex", 0x0000'0001U, 0U, 8U},
};

[[nodiscard]] inline goldsrc::DeltaSchema synthetic_schema()
{
    const auto encoded = delta_fixture::schema(
        "entity_state_t", kFieldNameSimilarityIsNotEvidence);
    auto parsed = goldsrc::DeltaDescriptionParser{}.parse(encoded, 0U);
    INFO((parsed.error ? parsed.error->context : std::string{}));
    INFO((parsed.error
            ? static_cast<std::uint32_t>(parsed.error->code)
            : 0U));
    REQUIRE(parsed);
    REQUIRE(parsed.schema);
    return std::move(*parsed.schema);
}

[[nodiscard]] inline goldsrc::DeltaObjectState synthetic_object(
    const goldsrc::DeltaSchema& schema,
    const std::uint32_t value)
{
    const std::vector<goldsrc::DeltaScalarValue> values{value};
    auto built = goldsrc::DeltaObjectBuilder{
        {}, goldsrc::DeltaValueCompatibilityProfile::synthetic_neutral_v1}
                     .build(schema, values);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.state);
    return std::move(*built.state);
}

[[nodiscard]] inline goldsrc::EntitySnapshotState synthetic_snapshot(
    const std::span<const std::uint32_t> entity_numbers,
    const std::uint32_t reference = 1U)
{
    auto schema = synthetic_schema();
    auto object = synthetic_object(schema, 7U);
    goldsrc::DeltaSchemaRegistryBuilder schema_builder;
    REQUIRE(schema_builder.insert(schema));
    auto schemas = std::move(schema_builder).publish();
    goldsrc::EntityBaselineRegistryBuilder baseline_builder{
        schemas,
        {},
        goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1};
    for (const auto entity_number : entity_numbers) {
        REQUIRE(baseline_builder.insert(
            goldsrc::EntityBaselineKey::for_entity(entity_number),
            goldsrc::EntitySchemaCategory::ordinary_entity,
            object));
    }
    auto baselines = std::move(baseline_builder).publish();
    REQUIRE(baselines);
    REQUIRE(baselines.state);
    std::vector<goldsrc::EntitySnapshotEntityInput> inputs;
    inputs.reserve(entity_numbers.size());
    for (const auto entity_number : entity_numbers) {
        inputs.push_back(goldsrc::EntitySnapshotEntityInput::from_baseline(
            entity_number,
            goldsrc::EntityBaselineKey::for_entity(entity_number)));
    }
    auto built = goldsrc::EntityFullSnapshotBuilder{
                     {},
                     goldsrc::EntitySnapshotCompatibilityProfile::
                         synthetic_neutral_v1}
                     .build(
                         goldsrc::EntitySnapshotReference::synthetic(reference),
                         goldsrc::EntityServerTime::synthetic_raw(reference),
                         *baselines.state,
                         inputs);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.state);
    return std::move(*built.state);
}

[[nodiscard]] inline std::vector<entity::EntityVisualProjectionState> project(
    const goldsrc::EntitySnapshotState& snapshot,
    std::vector<entity::SyntheticEntityVisualInput> inputs)
{
    auto provider =
        entity::SyntheticEntityVisualProjectionProvider::create(
            std::move(inputs));
    INFO(provider.context);
    REQUIRE(provider);
    std::vector<entity::EntityVisualProjectionState> projections;
    projections.reserve(snapshot.entity_count());
    for (const auto& snapshot_entity : snapshot.entities()) {
        auto result = provider.provider->project(snapshot, snapshot_entity);
        INFO(result.context);
        REQUIRE(result);
        REQUIRE(result.state);
        projections.push_back(std::move(*result.state));
    }
    return projections;
}

struct ManifestFixture {
    std::unique_ptr<local_resources::LocalResourceEnvironment> environment;
    goldsrc::PrecacheManifestState manifest;
};

[[nodiscard]] inline ManifestFixture manifest(
    const ScopedLocalResourceTestRoot& root,
    const std::span<const resource_fixture::EntrySpec> entries)
{
    auto environment = readiness_fixture::make_environment(root);
    auto list = readiness_fixture::parse_resource_list(entries);
    auto inventory = readiness_fixture::build_inventory(list, *environment);
    const auto server =
        readiness_fixture::parse_server_info("maps/test_map.bsp");
    auto built = readiness_fixture::build_manifest(
        list, inventory, server, *environment);
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.state);
    return ManifestFixture{
        std::move(environment), std::move(*built.state)};
}

[[nodiscard]] inline ManifestFixture manifest(
    const ScopedLocalResourceTestRoot& root,
    const std::initializer_list<resource_fixture::EntrySpec> entries)
{
    return manifest(
        root,
        std::span<const resource_fixture::EntrySpec>{
            entries.begin(), entries.size()});
}

[[nodiscard]] inline std::shared_ptr<const asset::ModelAsset> studio_asset(
    const std::string& name = "synthetic-studio")
{
    auto skeletal = std::make_shared<asset::SkeletalModelAssetData>();
    asset::ModelSubmodel submodel;
    submodel.vertices.resize(3U);
    submodel.indices = {0U, 1U, 2U};
    skeletal->submodels.push_back(std::move(submodel));
    asset::ModelTextureAsset texture;
    texture.width = 1U;
    texture.height = 1U;
    texture.rgba8_level_zero.resize(4U, std::byte{0xffU});
    skeletal->textures.push_back(std::move(texture));
    auto model = std::make_shared<asset::ModelAsset>();
    model->identity.source_name = name;
    model->skeletal_data = std::move(skeletal);
    return model;
}

[[nodiscard]] inline std::shared_ptr<const asset::SpriteAsset> sprite_asset(
    const std::string& name = "synthetic-sprite")
{
    auto sprite = std::make_shared<asset::SpriteAsset>();
    sprite->identity.source_name = name;
    asset::ImageAsset image;
    image.identity.source_name = name;
    image.width = 1U;
    image.height = 1U;
    image.pixels.resize(4U, std::byte{0xffU});
    sprite->frames.push_back(asset::SpriteFrame{std::move(image), 0.1F});
    asset::SpriteSourceAssetData source;
    asset::SpriteIndexedFrame indexed;
    indexed.width = 1U;
    indexed.height = 1U;
    indexed.indexed_pixels.push_back(std::byte{0U});
    indexed.derived_rgba8.resize(4U, std::byte{0xffU});
    source.indexed_frames.push_back(std::move(indexed));
    sprite->source_data.emplace(std::move(source));
    return sprite;
}

[[nodiscard]] inline entity::EntityVisualAssetImportCompletion
studio_completion(
    const entity::EntityVisualAssetImportRequest& request,
    std::vector<asset::AssetSourceFingerprint> fingerprints = {
        {0x1111U, 0x2222U}})
{
    auto candidate = entity::EntityVisualImportedAssetCandidate::studio_model(
        request.source_key(),
        studio_asset(),
        "model:synthetic-studio",
        request.source_key().main_source_byte_count(),
        std::move(fingerprints));
    return entity::EntityVisualAssetImportCompletion{
        request.request_index(),
        entity::EntityVisualAssetImportCompletionStatus::imported,
        std::optional<entity::EntityVisualImportedAssetCandidate>{
            std::move(candidate)}};
}

[[nodiscard]] inline entity::EntityVisualAssetImportCompletion
sprite_completion(
    const entity::EntityVisualAssetImportRequest& request,
    std::vector<asset::AssetSourceFingerprint> fingerprints = {
        {0x3333U, 0x4444U}})
{
    auto candidate = entity::EntityVisualImportedAssetCandidate::sprite(
        request.source_key(),
        sprite_asset(),
        "sprite:synthetic-sprite",
        request.source_key().main_source_byte_count(),
        std::move(fingerprints));
    return entity::EntityVisualAssetImportCompletion{
        request.request_index(),
        entity::EntityVisualAssetImportCompletionStatus::imported,
        std::optional<entity::EntityVisualImportedAssetCandidate>{
            std::move(candidate)}};
}

} // namespace hlclient::tests::entity_visual_fixture
