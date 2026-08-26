#pragma once

#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/entity_visual/entity_visual_asset_library.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_format.hpp>

#include "../entity_visual/entity_visual_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace hlclient::tests::entity_render_fixture {

namespace assets = hlclient::assets;
namespace render = hlclient::entity_render;
namespace visual = hlclient::entity_visual;
namespace visual_fixture = hlclient::tests::entity_visual_fixture;
namespace studio_format = hlclient::goldsrc::studio;

[[nodiscard]] inline std::shared_ptr<const assets::ModelAsset> model_asset(
    std::vector<std::uint32_t> texture_flags = {
        0U, studio_format::kGoldSrcStudioTextureMasked},
    const bool include_second_skin_family = false)
{
    REQUIRE(texture_flags.size() >= 2U);
    auto skeletal = std::make_shared<assets::SkeletalModelAssetData>();
    skeletal->source_clipping_bounds = {
        {-2.0F, -3.0F, -4.0F}, {5.0F, 6.0F, 7.0F}};
    skeletal->bones.push_back(assets::ModelBone{});

    for (std::uint32_t ordinal = 0U; ordinal < 2U; ++ordinal) {
        assets::ModelSubmodel submodel;
        submodel.source_model_ordinal = ordinal;
        submodel.bounds = {
            {-1.0F + static_cast<float>(ordinal), -1.0F, -1.0F},
            {1.0F + static_cast<float>(ordinal), 1.0F, 1.0F}};
        submodel.vertices = {
            {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, -8, 16, 0U, 0U},
            {{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, 24, 16, 0U, 0U},
            {{0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, -8, 48, 0U, 0U},
        };
        submodel.indices = {0U, 1U, 2U, 0U, 2U, 1U};
        submodel.meshes = {
            {0U, 3U, 0U, 10U + ordinal, 1U, 0U, 0U},
            {3U, 3U, 1U, 20U + ordinal, 1U, 0U, 0U},
        };
        skeletal->submodels.push_back(std::move(submodel));
    }
    skeletal->bodyparts = {
        {"primary", 1, {0U, 1U}},
        {"secondary", 2, {0U}},
    };
    for (std::size_t index = 0U; index < texture_flags.size(); ++index) {
        assets::ModelTextureAsset texture;
        texture.source_name = "texture-" + std::to_string(index);
        texture.width = static_cast<std::uint32_t>(2U + index);
        texture.height = 2U;
        texture.source_flags = texture_flags[index];
        texture.alpha_mode =
            (texture.source_flags & studio_format::kGoldSrcStudioTextureMasked) !=
                0U
            ? assets::ModelTextureAlphaMode::masked_index_255
            : assets::ModelTextureAlphaMode::opaque;
        texture.rgba8_level_zero.resize(
            static_cast<std::size_t>(texture.width) * texture.height * 4U,
            std::byte{0x7fU});
        if (include_second_skin_family) {
            for (std::size_t pixel = 0U;
                 pixel < texture.rgba8_level_zero.size();
                 pixel += 4U) {
                texture.rgba8_level_zero[pixel] =
                    index == 0U ? std::byte{0xE0U} : std::byte{0x20U};
                texture.rgba8_level_zero[pixel + 1U] =
                    index == 0U ? std::byte{0x20U} : std::byte{0xE0U};
                texture.rgba8_level_zero[pixel + 2U] = std::byte{0x20U};
                texture.rgba8_level_zero[pixel + 3U] = std::byte{0xFFU};
            }
        }
        skeletal->textures.push_back(std::move(texture));
    }
    assets::ModelSkinFamily family;
    family.texture_indices = {0U, 1U};
    skeletal->skin_families.push_back(std::move(family));
    if (include_second_skin_family) {
        assets::ModelSkinFamily alternate_family;
        alternate_family.texture_indices = {1U, 0U};
        skeletal->skin_families.push_back(std::move(alternate_family));
    }

    auto model = std::make_shared<assets::ModelAsset>();
    model->identity.source_name = "fixture-studio";
    model->skeletal_data = std::move(skeletal);
    return model;
}

[[nodiscard]] inline std::shared_ptr<const assets::SpriteAsset> sprite_asset(
    const assets::SpriteTextureFormat format =
        assets::SpriteTextureFormat::normal,
    const assets::SpriteSyncType sync = assets::SpriteSyncType::synchronized,
    const assets::SpriteOrientation orientation =
        assets::SpriteOrientation::view_parallel)
{
    auto sprite = std::make_shared<assets::SpriteAsset>();
    sprite->identity.source_name = "fixture-sprite";
    assets::SpriteSourceAssetData source;
    source.orientation = orientation;
    source.texture_format = format;
    source.sync_type = sync;
    source.bounding_radius = 12.0F;
    source.maximum_width = 8U;
    source.maximum_height = 8U;

    for (std::uint32_t index = 0U; index < 3U; ++index) {
        assets::SpriteIndexedFrame frame;
        frame.origin = {-2 + static_cast<std::int32_t>(index), 3};
        frame.width = 4U + index;
        frame.height = 6U;
        frame.source_top_level_entry = index == 0U ? 0U : 1U;
        if (index != 0U) {
            frame.source_group_ordinal = 0U;
            frame.source_group_frame_ordinal = index - 1U;
        }
        frame.indexed_pixels.resize(
            static_cast<std::size_t>(frame.width) * frame.height,
            std::byte{0U});
        if (format != assets::SpriteTextureFormat::index_alpha) {
            frame.derived_rgba8.resize(
                static_cast<std::size_t>(frame.width) * frame.height * 4U,
                std::byte{0xffU});
            if (format == assets::SpriteTextureFormat::alpha_test) {
                for (std::size_t alpha = 3U;
                     alpha < frame.derived_rgba8.size();
                     alpha += 4U) {
                    frame.derived_rgba8[alpha] = std::byte{0U};
                }
            }
        } else {
            frame.rgba_evidence = assets::SpriteRgbaEvidenceProfile::
                index_alpha_conversion_evidence_pending;
        }
        assets::ImageAsset image;
        image.width = frame.width;
        image.height = frame.height;
        image.pixels.resize(
            static_cast<std::size_t>(frame.width) * frame.height * 4U,
            std::byte{0xffU});
        sprite->frames.push_back({std::move(image), 0.25F});
        source.indexed_frames.push_back(std::move(frame));
    }
    source.top_level_entries = {
        {assets::SpriteTopLevelEntryKind::single, 0U, 0U, 1U, std::nullopt},
        {assets::SpriteTopLevelEntryKind::group, 1U, 1U, 2U, 0U},
    };
    source.groups = {{1U, 0U, {0.25F, 1.0F}, {0.25F, 0.75F}, {1U, 2U}}};
    source.statistics = {2U, 3U, 1U, 0U, 0U};
    sprite->source_data = std::move(source);
    return sprite;
}

struct PublishedVisualAssets {
    std::shared_ptr<const assets::ModelAsset> model;
    std::shared_ptr<const assets::SpriteAsset> sprite;
    std::shared_ptr<const visual::EntityVisualAssetLibraryState> library;
};

[[nodiscard]] inline PublishedVisualAssets published_visual_assets(
    const bool include_studio = true,
    const bool include_sprite = true,
    const assets::SpriteTextureFormat sprite_format =
        assets::SpriteTextureFormat::normal,
    const bool include_second_skin_family = false,
    const assets::SpriteOrientation sprite_orientation =
        assets::SpriteOrientation::view_parallel)
{
    ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    std::vector<resource_list_test_fixture::EntrySpec> entries{
        {2U, "maps/test_map.bsp", 9U, 3U, 0U}};
    std::vector<std::uint32_t> entity_numbers;
    std::vector<visual::SyntheticEntityVisualInput> projection_inputs;
    std::shared_ptr<const assets::ModelAsset> model;
    std::shared_ptr<const assets::SpriteAsset> sprite;
    if (include_studio) {
        root.write("valve", "models/fixture.mdl", "model");
        entries.push_back({2U, "models/fixture.mdl", 1U, 5U, 0U});
        entity_numbers.push_back(1U);
        visual::SyntheticEntityVisualInput input;
        input.entity_number = 1U;
        input.model_reference =
            visual::EntityVisualModelReference::synthetic_model_slot(1U);
        projection_inputs.push_back(input);
        model = include_second_skin_family
            ? model_asset({0U, 0U}, true)
            : model_asset();
    }
    if (include_sprite) {
        root.write("valve", "sprites/fixture.spr", "sprite");
        entries.push_back({2U, "sprites/fixture.spr", 2U, 6U, 0U});
        entity_numbers.push_back(2U);
        visual::SyntheticEntityVisualInput input;
        input.entity_number = 2U;
        input.model_reference =
            visual::EntityVisualModelReference::synthetic_model_slot(2U);
        projection_inputs.push_back(input);
        sprite = sprite_asset(sprite_format,
            assets::SpriteSyncType::synchronized,
            sprite_orientation);
    }

    auto resources = visual_fixture::manifest(root, entries);
    const auto snapshot = visual_fixture::synthetic_snapshot(entity_numbers);
    const auto projections =
        visual_fixture::project(snapshot, std::move(projection_inputs));
    visual::SyntheticModelSlotResolver resolver;
    visual::EntityVisualAssetLibraryBuilder builder;
    auto planned = builder.plan(
        0x5000U, {}, projections, resources.manifest, resolver);
    INFO((planned.error ? planned.error->context : std::string{}));
    REQUIRE(planned);
    REQUIRE(planned.plan);

    std::vector<visual::EntityVisualAssetImportCompletion> completions;
    for (const auto& request : planned.plan->requests()) {
        if (request.model_slot() == 1U) {
            auto candidate = visual::EntityVisualImportedAssetCandidate::
                studio_model(request.source_key(),
                    model,
                    "model:entity-render-fixture",
                    request.source_key().main_source_byte_count(),
                    {{0x1111U, 0x2222U}});
            completions.push_back({request.request_index(),
                visual::EntityVisualAssetImportCompletionStatus::imported,
                std::move(candidate)});
        } else {
            auto candidate = visual::EntityVisualImportedAssetCandidate::sprite(
                request.source_key(),
                sprite,
                "sprite:entity-render-fixture",
                request.source_key().main_source_byte_count(),
                {{0x3333U, 0x4444U}});
            completions.push_back({request.request_index(),
                visual::EntityVisualAssetImportCompletionStatus::imported,
                std::move(candidate)});
        }
    }
    auto published = builder.publish(*planned.plan, completions);
    INFO((published.error ? published.error->context : std::string{}));
    REQUIRE(published);
    REQUIRE(published.library);
    return {std::move(model), std::move(sprite), std::move(published.library)};
}

struct RenderAssets {
    PublishedVisualAssets sources;
    std::shared_ptr<const render::StudioModelRenderAsset> studio;
    std::shared_ptr<const render::SpriteRenderAsset> sprite;
};

[[nodiscard]] inline RenderAssets render_assets(
    const bool include_studio = true,
    const bool include_sprite = true,
    const assets::SpriteTextureFormat sprite_format =
        assets::SpriteTextureFormat::normal,
    const bool include_second_skin_family = false,
    const assets::SpriteOrientation sprite_orientation =
        assets::SpriteOrientation::view_parallel)
{
    auto output = RenderAssets{
        published_visual_assets(
            include_studio,
            include_sprite,
            sprite_format,
            include_second_skin_family,
            sprite_orientation),
        {},
        {}};
    for (const auto& record : output.sources.library->records()) {
        const render::EntityRenderResourceIdentity identity{
            record.resource_id(), record.resource_revision()};
        if (record.kind() == visual::EntityVisualAssetKind::studio_model) {
            auto built = render::StudioModelRenderAssetBuilder{}.build(
                *record.model_asset(), identity);
            INFO((built.error ? built.error->context : std::string{}));
            REQUIRE(built);
            output.studio =
                std::make_shared<const render::StudioModelRenderAsset>(
                    std::move(*built.asset));
        } else {
            auto built = render::SpriteRenderAssetBuilder{}.build(
                *record.sprite_asset(), identity);
            INFO((built.error ? built.error->context : std::string{}));
            REQUIRE(built);
            output.sprite = std::make_shared<const render::SpriteRenderAsset>(
                std::move(*built.asset));
        }
    }
    return output;
}

[[nodiscard]] inline render::EntitySceneRenderPackageBuildResult scene_package(
    RenderAssets assets,
    const render::RuntimeEntityVisualLimits& limits = {},
    const std::optional<render::EntityRenderResourceIdentity>
        world_scene_association = std::nullopt)
{
    render::EntitySceneRenderPackageCreateInfo input;
    input.asset_library = assets.sources.library;
    input.asset_library_identity = {
        assets.sources.library->resource_id(),
        assets.sources.library->resource_revision()};
    input.resource_id = 0x7000U;
    input.world_scene_association = world_scene_association;
    if (assets.studio) {
        input.studio_assets.push_back(std::move(assets.studio));
    }
    if (assets.sprite) {
        input.sprite_assets.push_back(std::move(assets.sprite));
    }
    auto built = render::EntitySceneRenderPackageBuilder{}.build(
        std::move(input), limits);
    UNSCOPED_INFO((built.error ? built.error->context : std::string{}));
    return built;
}

} // namespace hlclient::tests::entity_render_fixture
