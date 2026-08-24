#include <hlclient/assets/world_texture_types.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;

[[nodiscard]] assets::WorldTextureAsset make_texture(
    const assets::WorldTextureSourceKind source_kind =
        assets::WorldTextureSourceKind::embedded_bsp,
    const assets::WorldTextureAlphaMode alpha_mode =
        assets::WorldTextureAlphaMode::opaque)
{
    assets::WorldTextureAsset texture;
    texture.name = "SYNTHETIC";
    texture.width = 16U;
    texture.height = 16U;
    texture.source_kind = source_kind;
    texture.alpha_mode = alpha_mode;
    for (std::size_t level = 0U; level < texture.mip_levels.size(); ++level) {
        auto& mip = texture.mip_levels[level];
        mip.width = texture.width >> level;
        mip.height = texture.height >> level;
        mip.rgba_pixels.resize(
            static_cast<std::size_t>(mip.width) * mip.height * 4U,
            std::byte{0x7f});
    }
    return texture;
}

[[nodiscard]] assets::WorldMaterialTextureBinding resolved_binding(
    const std::size_t material_index,
    const std::size_t texture_index,
    const assets::WorldMaterialTextureBindingStatus status =
        assets::WorldMaterialTextureBindingStatus::resolved_embedded)
{
    assets::WorldMaterialTextureBinding binding;
    binding.material_index = material_index;
    binding.status = status;
    binding.texture_asset_index = texture_index;
    binding.source_bsp_texture_index = 3U;
    return binding;
}

TEST_CASE("World texture set publishes exact owning material-order state",
    "[world-texture-set][assets]")
{
    auto embedded = make_texture();
    embedded.source_bsp_texture_index = 3U;
    auto wad = make_texture(
        assets::WorldTextureSourceKind::external_wad3,
        assets::WorldTextureAlphaMode::masked_index_255);
    wad.name = "{SYNTHETIC";
    wad.source_archive_ordinal = 1U;

    auto embedded_binding = resolved_binding(0U, 0U);
    auto wad_binding = resolved_binding(
        1U,
        1U,
        assets::WorldMaterialTextureBindingStatus::resolved_wad3);
    wad_binding.source_archive_ordinal = 1U;

    assets::WorldTextureArchiveMetadata archive;
    archive.declaration_ordinal = 1U;
    archive.basename_byte_count = 13U;
    archive.source_root_ordinal = 0U;
    archive.status = assets::WorldTextureArchiveStatus::resolved;
    archive.catalog_entry_count = 1U;
    archive.textures_supplied_count = 1U;
    archive.source_byte_count = 1'234U;

    std::vector textures{std::move(embedded), std::move(wad)};
    const auto expected_first_pixel = textures.front().mip_levels.front().rgba_pixels.front();
    auto result = assets::WorldTextureSet::create(
        std::move(textures),
        {embedded_binding, wad_binding},
        {archive},
        2U);
    REQUIRE(result);
    REQUIRE(result.texture_set.has_value());
    const auto& set = *result.texture_set;

    CHECK(set.texture_count() == 2U);
    CHECK(set.binding_count() == 2U);
    CHECK(set.complete_for_world_materials());
    REQUIRE(set.binding_for_material(0U) != nullptr);
    REQUIRE(set.binding_for_material(1U) != nullptr);
    CHECK(set.binding_for_material(0U)->texture_asset_index == 0U);
    CHECK(set.binding_for_material(1U)->texture_asset_index == 1U);
    CHECK(set.binding_for_material(2U) == nullptr);
    REQUIRE(set.textures().front().mip_levels.size() == 4U);
    CHECK(set.textures().front().mip_levels.front().rgba_pixels.front() ==
        expected_first_pixel);

    const auto& statistics = set.statistics();
    CHECK(statistics.material_binding_count == 2U);
    CHECK(statistics.decoded_texture_count == 2U);
    CHECK(statistics.embedded_texture_count == 1U);
    CHECK(statistics.wad3_texture_count == 1U);
    CHECK(statistics.masked_texture_count == 1U);
    CHECK(statistics.opaque_texture_count == 1U);
    CHECK(statistics.total_mip_level_count == 8U);
    CHECK(statistics.total_rgba_byte_count == 2U * 1'360U);
    CHECK(statistics.wad_declaration_count == 1U);
    CHECK(statistics.wad_archive_resolved_count == 1U);
    CHECK(statistics.unresolved_material_count == 0U);
}

TEST_CASE("World texture set retains typed incomplete bindings",
    "[world-texture-set][incomplete]")
{
    assets::WorldMaterialTextureBinding missing;
    missing.material_index = 0U;
    missing.status =
        assets::WorldMaterialTextureBindingStatus::missing_bsp_texture_reference;
    assets::WorldMaterialTextureBinding mismatch;
    mismatch.material_index = 1U;
    mismatch.status = assets::WorldMaterialTextureBindingStatus::
        external_texture_dimension_mismatch;
    assets::WorldTextureArchiveMetadata archive;
    archive.status = assets::WorldTextureArchiveStatus::missing;

    auto result = assets::WorldTextureSet::create(
        {}, {missing, mismatch}, {archive}, 2U);
    REQUIRE(result);
    CHECK_FALSE(result.texture_set->complete_for_world_materials());
    CHECK(result.texture_set->statistics().unresolved_material_count == 2U);
    CHECK(result.texture_set->statistics().missing_bsp_reference_count == 1U);
    CHECK(result.texture_set->statistics().dimension_mismatch_count == 1U);
    CHECK(result.texture_set->statistics().wad_archive_missing_count == 1U);
}

TEST_CASE("World texture set construction is bounded and transactional",
    "[world-texture-set][limits][invalid]")
{
    SECTION("binding cardinality and order are exact")
    {
        auto result = assets::WorldTextureSet::create({}, {}, {}, 1U);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            assets::WorldTextureSetErrorCode::binding_count_mismatch);

        auto binding = resolved_binding(1U, 0U);
        auto wrong_order = assets::WorldTextureSet::create(
            {make_texture()}, {binding}, {}, 1U);
        REQUIRE_FALSE(wrong_order);
        CHECK(wrong_order.error->code ==
            assets::WorldTextureSetErrorCode::invalid_material_binding);
    }

    SECTION("resolved status requires an in-range matching texture source")
    {
        auto binding = resolved_binding(0U, 1U);
        auto result = assets::WorldTextureSet::create(
            {make_texture()}, {binding}, {}, 1U);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            assets::WorldTextureSetErrorCode::invalid_material_binding);

        binding = resolved_binding(
            0U,
            0U,
            assets::WorldMaterialTextureBindingStatus::resolved_wad3);
        auto wrong_source = assets::WorldTextureSet::create(
            {make_texture()}, {binding}, {}, 1U);
        REQUIRE_FALSE(wrong_source);
        CHECK(wrong_source.error->code ==
            assets::WorldTextureSetErrorCode::invalid_material_binding);
    }

    SECTION("every owning mip byte count is exact")
    {
        auto texture = make_texture();
        texture.mip_levels[2U].rgba_pixels.pop_back();
        auto result = assets::WorldTextureSet::create(
            {std::move(texture)}, {resolved_binding(0U, 0U)}, {}, 1U);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            assets::WorldTextureSetErrorCode::invalid_texture_asset);
        CHECK_FALSE(result.texture_set.has_value());
    }

    SECTION("unknown texture alpha modes fail the immutable factory boundary")
    {
        auto texture = make_texture();
        texture.alpha_mode = static_cast<assets::WorldTextureAlphaMode>(0x7fU);
        auto result = assets::WorldTextureSet::create(
            {std::move(texture)}, {resolved_binding(0U, 0U)}, {}, 1U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            assets::WorldTextureSetErrorCode::invalid_texture_asset);
        CHECK_FALSE(result.texture_set.has_value());
    }

    SECTION("binding compatibility profiles are closed")
    {
        auto binding = resolved_binding(0U, 0U);
        binding.compatibility_profile =
            static_cast<assets::WorldTextureCompatibilityProfile>(0x7fU);
        auto result = assets::WorldTextureSet::create(
            {make_texture()}, {binding}, {}, 1U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            assets::WorldTextureSetErrorCode::invalid_material_binding);
        CHECK_FALSE(result.texture_set.has_value());
    }

    SECTION("retained texture assets require at least one resolved binding")
    {
        auto result = assets::WorldTextureSet::create(
            {make_texture(), make_texture()},
            {resolved_binding(0U, 0U)},
            {},
            1U);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
            assets::WorldTextureSetErrorCode::invalid_texture_asset);
        CHECK(result.error->element_index == 1U);
        CHECK_FALSE(result.texture_set.has_value());
    }

    SECTION("texture count accepts the exact limit and rejects limit plus one")
    {
        auto limits = assets::WorldTextureSetLimits{};
        limits.maximum_texture_count = 1U;
        auto exact = assets::WorldTextureSet::create(
            {make_texture()}, {resolved_binding(0U, 0U)}, {}, 1U, limits);
        REQUIRE(exact);

        auto too_many = assets::WorldTextureSet::create(
            {make_texture(), make_texture()},
            {resolved_binding(0U, 0U)},
            {},
            1U,
            limits);
        REQUIRE_FALSE(too_many);
        CHECK(too_many.error->code ==
            assets::WorldTextureSetErrorCode::texture_count_limit_exceeded);
    }

    SECTION("total decoded RGBA bytes accept the exact limit")
    {
        auto limits = assets::WorldTextureSetLimits{};
        limits.maximum_total_rgba_bytes = 1'360U;
        auto exact = assets::WorldTextureSet::create(
            {make_texture()}, {resolved_binding(0U, 0U)}, {}, 1U, limits);
        REQUIRE(exact);

        --limits.maximum_total_rgba_bytes;
        auto over = assets::WorldTextureSet::create(
            {make_texture()}, {resolved_binding(0U, 0U)}, {}, 1U, limits);
        REQUIRE_FALSE(over);
        CHECK(over.error->code == assets::WorldTextureSetErrorCode::
            total_decoded_bytes_limit_exceeded);
    }

    SECTION("zero hard limits are invalid")
    {
        auto limits = assets::WorldTextureSetLimits{};
        limits.maximum_texture_count = 0U;
        const auto result = assets::WorldTextureSet::create({}, {}, {}, 0U, limits);
        REQUIRE_FALSE(result);
        CHECK(result.error->code ==
            assets::WorldTextureSetErrorCode::invalid_configuration);
    }
}

} // namespace
