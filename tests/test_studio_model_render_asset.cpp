#include <hlclient/entity_render/studio_model_render_asset.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_format.hpp>

#include "entity_render/entity_render_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace {

namespace render = hlclient::entity_render;
namespace fixture = hlclient::tests::entity_render_fixture;
namespace studio = hlclient::goldsrc::studio;

template <typename Type>
concept HasOpenGlHandle = requires(const Type& value) {
    value.vao;
    value.vbo;
    value.ebo;
    value.gl_id;
    value.opengl_texture;
};

template <typename Type>
concept HasNativePath = requires(const Type& value) {
    value.native_path;
    value.filesystem_path;
};

TEST_CASE("Studio render asset retains exact bone-local aggregate geometry",
    "[entity-render][studio][geometry]")
{
    const auto model = fixture::model_asset({
        0U,
        studio::kGoldSrcStudioTextureMasked |
            studio::kGoldSrcStudioTextureNoMips,
        studio::kGoldSrcStudioTextureAdditive,
        studio::kGoldSrcStudioTextureChrome |
            studio::kGoldSrcStudioTextureAlpha,
    });
    auto built = render::StudioModelRenderAssetBuilder{}.build(
        *model, {0x100U, 0x200U});
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    REQUIRE(built.asset);
    const auto& asset = *built.asset;

    REQUIRE(asset.submodels().size() == 2U);
    CHECK(asset.submodels()[0U].first_vertex == 0U);
    CHECK(asset.submodels()[0U].vertex_count == 3U);
    CHECK(asset.submodels()[0U].first_index == 0U);
    CHECK(asset.submodels()[0U].index_count == 6U);
    CHECK(asset.submodels()[0U].first_mesh == 0U);
    CHECK(asset.submodels()[0U].mesh_count == 2U);
    CHECK(asset.submodels()[1U].first_vertex == 3U);
    CHECK(asset.submodels()[1U].first_index == 6U);
    CHECK(asset.submodels()[1U].first_mesh == 2U);
    REQUIRE(asset.vertices().size() == 6U);
    CHECK(asset.vertices()[0U].bone_local_position.x == 0.0F);
    CHECK(asset.vertices()[0U].bone_local_normal.z == 1.0F);
    CHECK(asset.vertices()[0U].raw_texture_s == -8);
    CHECK(asset.vertices()[0U].raw_texture_t == 16);
    CHECK(asset.vertices()[0U].position_bone_index == 0U);
    CHECK(asset.vertices()[0U].normal_bone_index == 0U);
    CHECK(asset.indices()[6U] == 3U);
    CHECK(asset.indices()[7U] == 4U);

    REQUIRE(asset.bodyparts().size() == 2U);
    const auto selection = asset.select_submodels(1U);
    REQUIRE(selection);
    REQUIRE(selection.submodel_indices.size() == 2U);
    CHECK(selection.submodel_indices[0U] == 1U);
    CHECK(selection.submodel_indices[1U] == 0U);

    REQUIRE(asset.materials().size() == 4U);
    CHECK(asset.materials()[0U].support_status ==
        render::StudioRenderMaterialSupportStatus::supported_opaque);
    CHECK(asset.materials()[1U].support_status ==
        render::StudioRenderMaterialSupportStatus::supported_masked);
    CHECK(asset.materials()[1U].no_mipmaps);
    CHECK(asset.materials()[2U].support_status ==
        render::StudioRenderMaterialSupportStatus::unsupported_additive);
    CHECK(asset.materials()[3U].support_status ==
        render::StudioRenderMaterialSupportStatus::
            unsupported_multiple_features);

    const auto selected_material = asset.select_material(1U, 0U);
    REQUIRE(selected_material);
    CHECK(*selected_material.material_index == 1U);
    CHECK(asset.materials()[*selected_material.material_index].width == 3U);
    // Raw S/T is deliberately unchanged; width-dependent normalization is a
    // draw-time concern after skin-family material selection.
    CHECK(asset.vertices()[0U].raw_texture_s == -8);
}

TEST_CASE("Studio render asset publication is stable owning and bounded",
    "[entity-render][studio][identity][limits]")
{
    const auto model = fixture::model_asset();
    auto first = render::StudioModelRenderAssetBuilder{}.build(
        *model, {0x101U, 0x201U});
    auto second = render::StudioModelRenderAssetBuilder{}.build(
        *model, {0x101U, 0x201U});
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.asset->resource_id() == 0x101U);
    CHECK(first.asset->source_identity() ==
        render::EntityRenderResourceIdentity{0x101U, 0x201U});
    CHECK(first.asset->resource_revision() ==
        second.asset->resource_revision());

    const auto owned = [] {
        const auto scoped_source = fixture::model_asset();
        return render::StudioModelRenderAssetBuilder{}.build(
            *scoped_source, {0x102U, 0x202U});
    }();
    REQUIRE(owned);
    CHECK(owned.asset->vertices().size() == 6U);
    CHECK(owned.asset->materials()[0U].rgba8_level_zero.size() == 16U);

    auto limits = render::RuntimeEntityVisualLimits{};
    REQUIRE(first.asset->statistics().total_gpu_source_bytes > 1U);
    limits.maximum_model_gpu_bytes =
        first.asset->statistics().total_gpu_source_bytes - 1U;
    const auto rejected = render::StudioModelRenderAssetBuilder{}.build(
        *model, {0x101U, 0x201U}, limits);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        render::StudioModelRenderAssetErrorCode::source_limit_exceeded);
}

TEST_CASE("Studio neutral API exposes neither GL IDs nor native paths",
    "[entity-render][studio][boundary]")
{
    STATIC_REQUIRE_FALSE(HasOpenGlHandle<render::StudioModelRenderAsset>);
    STATIC_REQUIRE_FALSE(HasOpenGlHandle<render::StudioRenderVertex>);
    STATIC_REQUIRE_FALSE(HasOpenGlHandle<render::StudioRenderMaterial>);
    STATIC_REQUIRE_FALSE(HasNativePath<render::StudioModelRenderAsset>);
    STATIC_REQUIRE_FALSE(HasNativePath<render::StudioRenderMaterial>);
}

} // namespace
