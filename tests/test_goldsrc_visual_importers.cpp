#include <hlclient/assets/asset_importer_dispatcher.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/sprite/goldsrc_sprite_importer.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_model_importer.hpp>

#include "goldsrc_sprite_test_fixture.hpp"
#include "goldsrc_studio_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace bsp = hlclient::goldsrc::bsp;
namespace sprite = hlclient::goldsrc::sprite;
namespace studio = hlclient::goldsrc::studio;
namespace sprite_fixture = hlclient::tests::sprite_fixture;

struct SpriteImporterCallCounts {
    std::size_t probe_calls{0U};
    std::size_t import_calls{0U};
};

class CountingGoldSrcSpriteImporter final : public assets::ISpriteImporter {
public:
    CountingGoldSrcSpriteImporter(
        std::shared_ptr<SpriteImporterCallCounts> calls,
        sprite::GoldSrcSpriteImportLimits limits)
        : calls_{std::move(calls)}, delegate_{std::move(limits)}
    {
    }

    [[nodiscard]] std::string_view id() const noexcept override
    {
        return delegate_.id();
    }

    [[nodiscard]] assets::AssetProbeConfidence probe(
        const assets::AssetProbe& probe) const noexcept override
    {
        ++calls_->probe_calls;
        return delegate_.probe(probe);
    }

    [[nodiscard]] assets::SpriteAssetResult import(
        const assets::AssetSource& source) const override
    {
        ++calls_->import_calls;
        return delegate_.import(source);
    }

private:
    std::shared_ptr<SpriteImporterCallCounts> calls_;
    sprite::GoldSrcSpriteImporter delegate_;
};

struct CountedSpriteDispatch {
    assets::AssetDispatchResult result;
    std::size_t probe_calls{0U};
    std::size_t import_calls{0U};
};

[[nodiscard]] assets::AssetSource make_source(
    std::filesystem::path virtual_path,
    std::vector<std::byte> bytes)
{
    assets::AssetSourceMetadata metadata;
    metadata.content_size = bytes.size();
    auto created = assets::AssetSource::create(
        std::move(virtual_path), std::move(bytes), std::move(metadata));
    if (!created || !created.source) {
        throw std::runtime_error{"Unable to create synthetic visual source"};
    }
    return std::move(*created.source);
}

[[nodiscard]] std::vector<std::byte> literal_idsq_header()
{
    std::vector<std::byte> bytes(
        studio::kGoldSrcStudioSequenceHeaderWireSize, std::byte{0});
    bytes[0U] = std::byte{0x49};
    bytes[1U] = std::byte{0x44};
    bytes[2U] = std::byte{0x53};
    bytes[3U] = std::byte{0x51};
    hlclient::tests::studio_write_i32le(bytes, 4U, 10);
    hlclient::tests::studio_write_i32le(
        bytes, 72U, static_cast<std::int32_t>(bytes.size()));
    return bytes;
}

[[nodiscard]] CountedSpriteDispatch dispatch_counted_sprite(
    std::filesystem::path virtual_path,
    std::vector<std::byte> bytes,
    sprite::GoldSrcSpriteImportLimits limits = {})
{
    assets::AssetImporterRegistries registries;
    const auto model_registered = registries.models.register_importer(
        std::make_unique<studio::GoldSrcStudioModelImporter>(),
        studio::kGoldSrcStudioModelImporterPriority);
    if (!model_registered) {
        throw std::runtime_error{"Unable to register Studio test importer"};
    }

    const auto calls = std::make_shared<SpriteImporterCallCounts>();
    const auto sprite_registered = registries.sprites.register_importer(
        std::make_unique<CountingGoldSrcSpriteImporter>(calls, std::move(limits)),
        sprite::kGoldSrcSpriteImporterPriority);
    if (!sprite_registered) {
        throw std::runtime_error{"Unable to register sprite test importer"};
    }

    const assets::AssetImporterDispatcher dispatcher{registries};
    auto result = dispatcher.dispatch(
        make_source(std::move(virtual_path), std::move(bytes)),
        assets::AssetDispatchRole::model_or_sprite);
    return CountedSpriteDispatch{
        std::move(result), calls->probe_calls, calls->import_calls};
}

void check_single_sprite_failure(
    const CountedSpriteDispatch& dispatched,
    const assets::AssetProbeConfidence expected_confidence,
    const assets::AssetErrorCode expected_error_code =
        assets::AssetErrorCode::MalformedData)
{
    CHECK(dispatched.probe_calls == 1U);
    CHECK(dispatched.import_calls == 1U);
    CHECK_FALSE(dispatched.result.imported());
    CHECK(dispatched.result.state == assets::AssetDispatchState::import_failed);
    CHECK(dispatched.result.selected_category ==
          assets::AssetImporterCategory::sprite);
    CHECK(dispatched.result.selected_importer_id ==
          "sprite:goldsrc-sprite-v2");
    REQUIRE(dispatched.result.top_candidates.size() == 1U);
    CHECK(dispatched.result.top_candidates.front().category ==
          assets::AssetImporterCategory::sprite);
    CHECK(dispatched.result.top_candidates.front().importer_id ==
          "sprite:goldsrc-sprite-v2");
    CHECK(dispatched.result.top_candidates.front().confidence ==
          expected_confidence);
    REQUIRE(dispatched.result.error);
    CHECK(dispatched.result.error->code == expected_error_code);
}

TEST_CASE("Built-in registration installs exactly BSP Studio and sprite",
    "[goldsrc][visual][registration]")
{
    assets::AssetImporterRegistries registries;
    const auto registered = hlclient::goldsrc::register_builtin_asset_importers(registries);
    INFO((registered.error ? registered.error->context : std::string{}));
    REQUIRE(registered);
    CHECK(registries.worlds.size() == 1U);
    CHECK(registries.models.size() == 1U);
    CHECK(registries.sprites.size() == 1U);
    CHECK(registries.images.size() == 0U);
    CHECK(registries.audio.size() == 0U);

    const auto model = make_source(
        "models/synthetic.bin",
        hlclient::tests::literal_minimal_goldsrc_studio_v10());
    const auto model_probe = registries.models.probe(model);
    REQUIRE(model_probe.selected());
    REQUIRE(model_probe.top_candidates.size() == 1U);
    CHECK(model_probe.top_candidates.front().importer_id ==
          studio::kGoldSrcStudioModelImporterId);
    CHECK(model_probe.top_candidates.front().priority ==
          studio::kGoldSrcStudioModelImporterPriority);

    const auto sprite_source = make_source(
        "models/synthetic.bin", sprite_fixture::literal_single_sprite());
    const auto sprite_probe = registries.sprites.probe(sprite_source);
    REQUIRE(sprite_probe.selected());
    REQUIRE(sprite_probe.top_candidates.size() == 1U);
    CHECK(sprite_probe.top_candidates.front().importer_id ==
          sprite::kGoldSrcSpriteImporterId);
    CHECK(sprite_probe.top_candidates.front().priority ==
          sprite::kGoldSrcSpriteImporterPriority);

    const auto duplicate = hlclient::goldsrc::register_builtin_asset_importers(registries);
    REQUIRE_FALSE(duplicate);
    REQUIRE(duplicate.error);
    CHECK(duplicate.error->code ==
          assets::AssetImporterRegistrationErrorCode::DuplicateImporterId);
    CHECK(registries.worlds.size() == 1U);
    CHECK(registries.models.size() == 1U);
    CHECK(registries.sprites.size() == 1U);
}

TEST_CASE("GoldSrc model-or-sprite dispatch is signature-first",
    "[goldsrc][visual][dispatch]")
{
    assets::AssetImporterRegistries registries;
    REQUIRE(hlclient::goldsrc::register_builtin_asset_importers(registries));
    const assets::AssetImporterDispatcher dispatcher{registries};

    SECTION("IDST selects the model category without an extension")
    {
        const auto source = make_source(
            "models/visual.data",
            hlclient::tests::literal_minimal_goldsrc_studio_v10());
        const auto dispatched = dispatcher.dispatch(
            source, assets::AssetDispatchRole::model_or_sprite);
        REQUIRE(dispatched.imported());
        CHECK(dispatched.selected_category ==
              assets::AssetImporterCategory::model);
        CHECK(dispatched.selected_importer_id ==
              "model:goldsrc-studio-mdl-v10");
        const auto& model = std::get<assets::ModelAsset>(*dispatched.asset);
        REQUIRE(model.skeletal_data);
        CHECK(model.skeletal_data->statistics.emitted_triangle_count == 1U);
    }

    SECTION("IDSP selects the sprite category through the model resource role")
    {
        const auto source = make_source(
            "models/visual.data", sprite_fixture::literal_single_sprite());
        const auto dispatched = dispatcher.dispatch(
            source, assets::AssetDispatchRole::model_or_sprite);
        REQUIRE(dispatched.imported());
        CHECK(dispatched.selected_category ==
              assets::AssetImporterCategory::sprite);
        CHECK(dispatched.selected_importer_id == "sprite:goldsrc-sprite-v2");
        REQUIRE(dispatched.top_candidates.size() == 1U);
        CHECK(dispatched.top_candidates.front().confidence ==
              sprite::kGoldSrcSpriteHeaderProbeConfidence);
        const auto& imported = std::get<assets::SpriteAsset>(*dispatched.asset);
        REQUIRE(imported.source_data);
        CHECK(imported.source_data->statistics.flattened_frame_count == 1U);
    }

    SECTION("extensions never override random bytes")
    {
        const std::vector<std::byte> random{
            std::byte{0x52}, std::byte{0x41}, std::byte{0x4e},
            std::byte{0x44}, std::byte{0x4f}, std::byte{0x4d}};
        for (const auto* path : {"models/random.mdl", "sprites/random.spr"}) {
            const auto dispatched = dispatcher.dispatch(
                make_source(path, random),
                assets::AssetDispatchRole::model_or_sprite);
            CHECK_FALSE(dispatched.imported());
            CHECK(dispatched.state ==
                  assets::AssetDispatchState::importer_not_registered);
        }
    }

    SECTION("short wrong-ID and wrong-version sprite probes do not match")
    {
        std::vector<std::vector<std::byte>> non_matches;
        non_matches.push_back({
            std::byte{0x49}, std::byte{0x44}, std::byte{0x53}, std::byte{0x50},
            std::byte{0x02}, std::byte{0}, std::byte{0}});

        auto wrong_identifier = sprite_fixture::literal_single_sprite();
        sprite_fixture::write_u32_le(wrong_identifier, 0U, 0x51534449U);
        non_matches.push_back(std::move(wrong_identifier));

        auto wrong_version = sprite_fixture::literal_single_sprite();
        sprite_fixture::write_i32_le(wrong_version, 4U, 3);
        non_matches.push_back(std::move(wrong_version));

        for (auto& bytes : non_matches) {
            const auto dispatched = dispatcher.dispatch(
                make_source("sprites/non-match.spr", std::move(bytes)),
                assets::AssetDispatchRole::model_or_sprite);
            CHECK(dispatched.state ==
                  assets::AssetDispatchState::importer_not_registered);
            CHECK(dispatched.selected_category ==
                  assets::AssetImporterCategory::none);
            CHECK(dispatched.selected_importer_id.empty());
        }
    }

    SECTION("IDSQ never matches as a top-level model")
    {
        const auto source = make_source("models/group01.mdl", literal_idsq_header());
        CHECK(registries.models.probe(source).state ==
              assets::AssetImporterProbeState::no_match);
        const auto dispatched = dispatcher.dispatch(
            source, assets::AssetDispatchRole::model_or_sprite);
        CHECK(dispatched.state ==
              assets::AssetDispatchState::importer_not_registered);
    }

    SECTION("matching Studio signature selects then reports malformed data")
    {
        std::vector<std::byte> truncated{
            std::byte{0x49}, std::byte{0x44}, std::byte{0x53}, std::byte{0x54},
            std::byte{0x0a}, std::byte{0}, std::byte{0}, std::byte{0}};
        const auto dispatched = dispatcher.dispatch(
            make_source("models/truncated.mdl", std::move(truncated)),
            assets::AssetDispatchRole::model_or_sprite);
        CHECK_FALSE(dispatched.imported());
        CHECK(dispatched.state == assets::AssetDispatchState::import_failed);
        REQUIRE(dispatched.error);
        CHECK(dispatched.error->code == assets::AssetErrorCode::MalformedData);
    }

    SECTION("matching sprite signature selects then reports malformed data")
    {
        std::vector<std::byte> truncated{
            std::byte{0x49}, std::byte{0x44}, std::byte{0x53}, std::byte{0x50},
            std::byte{0x02}, std::byte{0}, std::byte{0}, std::byte{0}};
        const auto dispatched = dispatch_counted_sprite(
            "sprites/truncated.spr", std::move(truncated));
        check_single_sprite_failure(dispatched,
            static_cast<assets::AssetProbeConfidence>(
                sprite::kGoldSrcSpriteSignatureProbeConfidence +
                sprite::kGoldSrcSpriteExtensionHintBoost));
    }

    SECTION("oversize sprite preserves plausible-header confidence then fails typed")
    {
        auto bytes = sprite_fixture::literal_single_sprite();
        auto limits = sprite::GoldSrcSpriteImportLimits{};
        limits.maximum_source_bytes = bytes.size() - 1U;
        REQUIRE(sprite::valid_goldsrc_sprite_import_limits(limits));

        const auto dispatched = dispatch_counted_sprite(
            "sprites/oversize.spr", std::move(bytes), limits);
        check_single_sprite_failure(dispatched,
            static_cast<assets::AssetProbeConfidence>(
                sprite::kGoldSrcSpriteHeaderProbeConfidence +
                sprite::kGoldSrcSpriteExtensionHintBoost));
    }

    SECTION("malformed full sprite header selects then fails typed")
    {
        auto bytes = sprite_fixture::literal_single_sprite();
        sprite_fixture::write_i32_le(bytes, 8U, 5);
        const auto dispatched = dispatch_counted_sprite(
            "sprites/malformed.spr", std::move(bytes));
        check_single_sprite_failure(dispatched,
            static_cast<assets::AssetProbeConfidence>(
                sprite::kGoldSrcSpriteSignatureProbeConfidence +
                sprite::kGoldSrcSpriteExtensionHintBoost));
    }

    SECTION("invalid sprite caller configuration still selects exact signature")
    {
        auto limits = sprite::GoldSrcSpriteImportLimits{};
        limits.maximum_width = 0U;
        REQUIRE_FALSE(sprite::valid_goldsrc_sprite_import_limits(limits));
        const auto dispatched = dispatch_counted_sprite(
            "sprites/invalid-config.spr",
            sprite_fixture::literal_single_sprite(),
            limits);
        check_single_sprite_failure(dispatched,
            static_cast<assets::AssetProbeConfidence>(
                sprite::kGoldSrcSpriteSignatureProbeConfidence +
                sprite::kGoldSrcSpriteExtensionHintBoost),
            assets::AssetErrorCode::ImportFailed);
    }

    SECTION("validated split Studio source reports a typed dependency")
    {
        const auto dispatched = dispatcher.dispatch(
            make_source("models/split.mdl",
                hlclient::tests::synthetic_split_texture_main()),
            assets::AssetDispatchRole::model_or_sprite);
        CHECK_FALSE(dispatched.imported());
        CHECK(dispatched.state == assets::AssetDispatchState::import_failed);
        REQUIRE(dispatched.error);
        CHECK(dispatched.error->code ==
              assets::AssetErrorCode::ExternalDependencyRequired);
        CHECK(dispatched.selected_category == assets::AssetImporterCategory::model);
    }
}

TEST_CASE("Self-contained visual imports are deterministic across repetitions",
    "[goldsrc][visual][repeated]")
{
    const studio::GoldSrcStudioModelImporter model_importer;
    const sprite::GoldSrcSpriteImporter sprite_importer;
    const auto model_source = make_source(
        "models/repeated.mdl",
        hlclient::tests::literal_minimal_goldsrc_studio_v10());
    const auto sprite_source = make_source(
        "sprites/repeated.spr", sprite_fixture::literal_group_sprite());

    for (std::size_t repetition = 0U; repetition < 20U; ++repetition) {
        const auto model = model_importer.import(model_source);
        INFO(repetition);
        REQUIRE(model);
        REQUIRE(model.value().skeletal_data);
        CHECK(model.value().skeletal_data->statistics.bone_count == 1U);
        CHECK(model.value().skeletal_data->statistics.emitted_triangle_count == 1U);

        const auto imported_sprite = sprite_importer.import(sprite_source);
        REQUIRE(imported_sprite);
        REQUIRE(imported_sprite.value().source_data);
        CHECK(imported_sprite.value().source_data->statistics.group_count == 1U);
        CHECK(imported_sprite.value().source_data->statistics.flattened_frame_count == 2U);
    }
}

} // namespace
