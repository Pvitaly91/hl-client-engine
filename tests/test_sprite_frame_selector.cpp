#include <hlclient/goldsrc/sprite/goldsrc_sprite_playback.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace assets = hlclient::assets;
namespace sprite = hlclient::goldsrc::sprite;

[[nodiscard]] assets::SpriteAsset playback_sprite()
{
    assets::SpriteAsset asset;
    asset.identity.source_name = "sprites/playback.spr";
    assets::SpriteSourceAssetData source;
    source.indexed_frames.resize(3U);
    source.indexed_frames[0U].source_top_level_entry = 0U;
    source.indexed_frames[1U].source_top_level_entry = 1U;
    source.indexed_frames[1U].source_group_ordinal = 0U;
    source.indexed_frames[1U].source_group_frame_ordinal = 0U;
    source.indexed_frames[2U].source_top_level_entry = 1U;
    source.indexed_frames[2U].source_group_ordinal = 0U;
    source.indexed_frames[2U].source_group_frame_ordinal = 1U;
    source.top_level_entries = {
        {assets::SpriteTopLevelEntryKind::single, 0U, 0U, 1U, std::nullopt},
        {assets::SpriteTopLevelEntryKind::group, 1U, 1U, 2U, 0U},
    };
    source.groups = {{
        1U,
        0U,
        {0.25F, 1.0F},
        {0.25F, 0.75F},
        {1U, 2U},
    }};
    asset.source_data = std::move(source);
    return asset;
}

TEST_CASE("Sprite selector chooses singles and exact synchronized intervals",
          "[goldsrc-sprite][playback][selector]")
{
    const auto asset = playback_sprite();
    const sprite::SpriteFrameSelector selector;

    const auto single = selector.select(asset, {});
    REQUIRE(single);
    CHECK(single.selection->flattened_frame_index() == 0U);
    CHECK(single.selection->status() ==
          sprite::SpriteFrameSelectionStatus::single);

    sprite::SpritePlaybackInput grouped;
    grouped.top_level_entry_index = 1U;
    grouped.elapsed_seconds = 0.249;
    const auto first = selector.select(asset, grouped);
    REQUIRE(first);
    CHECK(first.selection->flattened_frame_index() == 1U);
    CHECK(first.selection->group_frame_ordinal() == 0U);

    grouped.elapsed_seconds = 0.25;
    const auto boundary = selector.select(asset, grouped);
    REQUIRE(boundary);
    CHECK(boundary.selection->flattened_frame_index() == 2U);
    CHECK(boundary.selection->group_frame_ordinal() == 1U);

    grouped.elapsed_seconds = 1.0;
    const auto wrapped = selector.select(asset, grouped);
    REQUIRE(wrapped);
    CHECK(wrapped.selection->flattened_frame_index() == 1U);
    CHECK(wrapped.selection->wrapped_elapsed_seconds() == 0.0);
}

TEST_CASE("Sprite selector keeps overrides explicit and random sync unguessed",
          "[goldsrc-sprite][playback][evidence]")
{
    auto asset = playback_sprite();
    asset.source_data->sync_type = assets::SpriteSyncType::random;
    sprite::SpritePlaybackInput input;
    input.top_level_entry_index = 1U;
    input.elapsed_seconds = 0.5;

    const auto pending = sprite::SpriteFrameSelector{}.select(asset, input);
    REQUIRE_FALSE(pending);
    REQUIRE(pending.error);
    CHECK(pending.error->code ==
          sprite::SpritePlaybackErrorCode::unsupported_random_sync);

    input.flattened_frame_override = 2U;
    const auto overridden = sprite::SpriteFrameSelector{}.select(asset, input);
    REQUIRE(overridden);
    CHECK(overridden.selection->flattened_frame_index() == 2U);
    CHECK(overridden.selection->status() ==
          sprite::SpriteFrameSelectionStatus::explicit_override);

    input.flattened_frame_override = 0U;
    const auto outside_entry =
        sprite::SpriteFrameSelector{}.select(asset, input);
    REQUIRE_FALSE(outside_entry);
    CHECK(outside_entry.error->code ==
          sprite::SpritePlaybackErrorCode::invalid_frame_override);
}

TEST_CASE("Sprite selector rejects invalid time and stock projection",
          "[goldsrc-sprite][playback][limits]")
{
    const auto asset = playback_sprite();
    sprite::SpritePlaybackInput input;
    input.elapsed_seconds = -1.0;
    const auto invalid = sprite::SpriteFrameSelector{}.select(asset, input);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code == sprite::SpritePlaybackErrorCode::invalid_time);

    input.elapsed_seconds = 0.0;
    input.compatibility_profile = sprite::SpritePlaybackCompatibilityProfile::
        stock_entity_projection_evidence_pending;
    const auto pending = sprite::SpriteFrameSelector{}.select(asset, input);
    REQUIRE_FALSE(pending);
    CHECK(pending.error->code ==
          sprite::SpritePlaybackErrorCode::evidence_pending);
}

} // namespace
