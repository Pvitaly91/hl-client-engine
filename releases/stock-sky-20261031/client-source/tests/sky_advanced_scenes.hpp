#pragma once
// Test-only, path-free owners using the accepted B6 public render interfaces.
// No Catch dependency, asset import, production settings, hidden candidate toggle,
// or old sky-order/BSP-selection experiment is used here.
#include "world_render_test_fixture.hpp"
#include <hlclient/assets/model_asset_types.hpp>
#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/entity_render/studio_model_render_asset.hpp>
#include <hlclient/renderer/render_scene.hpp>
#include <hlclient/world_scene_render/world_scene_render_types.hpp>
#include <hlclient/world_spatial/world_spatial_types.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sky_advanced {
namespace r=hlclient::renderer;
namespace a=hlclient::assets;
namespace er=hlclient::entity_render;
namespace wr=hlclient::world_render;
namespace ws=hlclient::world_scene_render;
namespace spatial=hlclient::world_spatial;
namespace fixture=hlclient::tests::world_render_fixture;
inline void require(bool value,const char* why) {if(!value)throw std::runtime_error(why);}

// A genuine Studio asset: one bone, one indexed textured quad, one material and
// one draw command. Local +X is the first-person view direction in B6.
inline std::shared_ptr<const er::EntitySceneRenderPackage> studio_package() {
    auto skeletal=std::make_shared<a::SkeletalModelAssetData>();
    skeletal->source_clipping_bounds={{1.99F,-.44F,-.44F},{2.01F,.44F,.44F}};
    skeletal->bones.push_back(a::ModelBone{});
    a::ModelSubmodel model;model.name="sky-test-visible-studio-quad";model.bounding_radius=3;
    model.bounds=skeletal->source_clipping_bounds;
    model.vertices={
        {{2,-.43F,-.43F},{-1,0,0},0,0,0,0},
        {{2,-.43F,.43F},{-1,0,0},0,4,0,0},
        {{2,.43F,.43F},{-1,0,0},4,4,0,0},
        {{2,.43F,-.43F},{-1,0,0},4,0,0,0}};
    model.indices={0,1,2,0,2,3};model.meshes={{0,6,0,0,2,1,0}};
    skeletal->submodels.push_back(std::move(model));
    skeletal->bodyparts={{"body",1,{0}}};
    a::ModelTextureAsset texture;texture.source_name="sky-test-studio-rgba";texture.width=texture.height=4;
    texture.rgba8_level_zero.resize(4*4*4);
    for(std::size_t y=0;y<4;++y)for(std::size_t x=0;x<4;++x) {
        const auto p=(y*4+x)*4;texture.rgba8_level_zero[p]=std::byte{static_cast<unsigned char>(70+x*45)};
        texture.rgba8_level_zero[p+1]=std::byte{static_cast<unsigned char>(30+y*45)};
        texture.rgba8_level_zero[p+2]=std::byte{210};texture.rgba8_level_zero[p+3]=std::byte{255};
    }
    skeletal->textures.push_back(std::move(texture));skeletal->skin_families={{{0}}};
    a::ModelAsset source;source.identity.source_name="synthetic-sky-studio";source.skeletal_data=std::move(skeletal);
    auto imported=er::StudioModelRenderAssetBuilder{}.build(source,{0x534b5354,1});
    require(bool(imported),"Synthetic Studio asset rejected");
    auto asset=std::make_shared<const er::StudioModelRenderAsset>(std::move(*imported.asset));
    er::EntitySceneRenderPackageCreateInfo input;input.asset_source=er::EntitySceneRenderAssetSource::project_generated_diagnostic;
    input.resource_id=0x534b5355;input.studio_assets.push_back(std::move(asset));
    auto package=er::EntitySceneRenderPackageBuilder{}.build(std::move(input));
    require(bool(package),"Synthetic Studio scene package rejected");
    return std::make_shared<const er::EntitySceneRenderPackage>(std::move(*package.package));
}

inline r::RenderDynamicEntities studio_entities(bool world_translucent) {
    auto package=studio_package();er::EntityRenderFrameBuildInput input;
    input.resource_id=0x534b5356;input.resource_revision=1;
    input.interpolation={.5,0,1,.5F,1,2,er::EntityRenderInterpolationProfile::synthetic_seconds_v1};
    constexpr std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    input.studio_poses.push_back({package->studio_assets()[0]->source_identity(),{identity}});
    er::StudioEntityRenderInstance instance;instance.entity_number=1;instance.studio_asset_index=0;instance.pose_index=0;
    instance.transform.uniform_scale=1;instance.static_light_rgb=std::array<float,3>{1,1,1};
    instance.interpolated_bounds={{1.99F,-.44F,-.44F},{2.01F,.44F,.44F}};
    if(world_translucent) {
        // Rotate the local +X-facing quad into the world's Y-facing plane;
        // y=-.35 lies between camera y=-2 and the primary opaque wall y=0.
        instance.transform.rotation_degrees={0,0,90};instance.transform.origin={.25F,-2.35F,0};
        instance.interpolated_bounds={{-.19F,-.36F,-.44F},{.69F,-.34F,.44F}};
        instance.blend_mode=er::StudioBlendMode::alpha_blended;instance.opacity=.45F;
    }
    input.studio_instances.push_back(instance);
    if(world_translucent) {
        auto additive=instance;additive.entity_number=2;additive.transform.origin.x=-.35F;
        additive.interpolated_bounds={{-.79F,-.36F,-.44F},{.09F,-.34F,.44F}};
        additive.blend_mode=er::StudioBlendMode::additive;additive.opacity=.35F;additive.tint={.25F,.65F,1};
        input.studio_instances.push_back(additive);
    }
    auto built=er::EntityRenderFrameBuilder{}.build(*package,std::move(input));require(bool(built),"Synthetic Studio frame rejected");
    auto frame=std::make_shared<const er::EntityRenderFrame>(std::move(*built.frame));
    const auto& stats=frame->statistics();require(stats.studio_instance_count==(world_translucent?2U:1U),"Studio fixture lost expected instances");
    require(stats.draw_count==(world_translucent?2U:1U),"Studio fixture lost expected draw commands");
    return {std::move(package),std::move(frame),{stats.candidate_count,stats.visible_count,stats.studio_instance_count,
        stats.sprite_instance_count,stats.unsupported_instance_count}};
}
inline r::RenderDynamicEntities first_person() {return studio_entities(false);}
inline void add_translucent_entities(r::RenderScene& scene) {scene.dynamic_entities=studio_entities(true);}

inline std::shared_ptr<const wr::WorldRenderPackage> quad_package(bool water) {
    auto world=fixture::make_world();
    world.bounds={{-.6F,0,-.6F},{.6F,0,.6F}};
    world.vertices={
        {{-.6F,0,-.6F},{0,-1,0},{0,0}},{{.6F,0,-.6F},{0,-1,0},{16,0}},
        {{.6F,0,.6F},{0,-1,0},{16,16}},{{-.6F,0,.6F},{0,-1,0},{0,16}}};
    world.surfaces[0].bounds=world.bounds;world.surfaces[0].special_surface=water;
    const auto name=water?"!water":"BRUSH";world.materials[0].texture_name=name;
    auto texture=fixture::make_texture(false);texture.name=name;
    for(auto& mip:texture.mip_levels)for(std::size_t p=0;p<mip.rgba_pixels.size();p+=4) {
        mip.rgba_pixels[p]=std::byte{water?static_cast<unsigned char>(35):static_cast<unsigned char>(210)};
        mip.rgba_pixels[p+1]=std::byte{water?static_cast<unsigned char>(125):static_cast<unsigned char>(65)};
        mip.rgba_pixels[p+2]=std::byte{180};mip.rgba_pixels[p+3]=std::byte{255};
    }
    a::WorldMaterialTextureBinding binding;binding.material_index=0;binding.texture_asset_index=0;binding.source_bsp_texture_index=0;
    binding.status=a::WorldMaterialTextureBindingStatus::resolved_embedded;
    auto textures=a::WorldTextureSet::create({std::move(texture)},{binding},{},1);require(bool(textures),"Quad texture rejected");
    fixture::FixtureOptions options;options.unlit=true;
    auto result=wr::WorldRenderPackageBuilder{}.build({std::move(world),std::move(*textures.texture_set)},fixture::make_lightmap_set(options));
    require(bool(result),"Quad world package rejected");
    auto package=std::make_shared<const wr::WorldRenderPackage>(std::move(*result.package));
    if(water)require(package->materials().size()==1 && package->materials()[0].special_surface && !package->materials()[0].sky_surface,
        "Special non-sky water material classification missing");
    return package;
}
// B6's supported material contract is a GoldSrc non-sky special surface named
// !water. This does not invent a refractive/animated water renderer absent in B6.
inline std::shared_ptr<const wr::WorldRenderPackage> water_package() {return quad_package(true);}

inline std::shared_ptr<const ws::WorldSceneRenderPackage> brush_scene(
    const std::shared_ptr<const wr::WorldRenderPackage>& world,
    const std::shared_ptr<const wr::WorldRenderPackage>& brush) {
    const a::WorldBounds bounds{{-32,-32,-32},{32,32,32}};
    spatial::WorldSpatialNode node;node.children={spatial::WorldSpatialNodeChild{spatial::WorldSpatialNodeChildKind::leaf,2},
        spatial::WorldSpatialNodeChild{spatial::WorldSpatialNodeChildKind::leaf,1}};node.bounds=bounds;
    std::vector<spatial::WorldSpatialLeaf> leaves(3);
    for(std::uint32_t i=0;i<3;++i) {
        auto& leaf=leaves[i];leaf.source_leaf_index=i;leaf.bounds=bounds;leaf.surface_membership.source_leaf_index=i;
        leaf.solid_or_special=i==0;if(!i)continue;leaf.pvs_row_index=0;leaf.pvs_bit_addressable=true;
        leaf.surface_membership.source_marksurface_count=static_cast<std::uint32_t>(world->surface_ranges().size());
        for(std::uint32_t surface=0;surface<world->surface_ranges().size();++surface)
            leaf.surface_membership.world_surface_indices.push_back(surface);
    }
    spatial::WorldSpatialPackage spatial_package{{spatial::WorldSpatialPlane{{1,0,0},.25F,0}},{node},std::move(leaves),
        spatial::WorldPvsTable{1,2,{{std::byte{3}}},{std::nullopt,0,0},0},{0,2,bounds},
        {1,1,3,2*world->surface_ranges().size(),2*world->surface_ranges().size(),1,1},
        spatial::WorldSpatialCompatibilityProfile::goldsrc_bsp_v30_leaf_one_is_pvs_bit_zero,
        spatial::WorldSpatialEvidenceProfile::canonical_validated_bsp_records};
    std::vector<std::uint32_t> surfaces;
    for(std::uint32_t i=0;i<brush->surface_ranges().size();++i)surfaces.push_back(i);
    std::vector<ws::BrushSubmodelRenderModel> models;models.emplace_back(1,brush->bounds(),std::move(surfaces));
    auto built=ws::WorldSceneRenderPackageBuilder{}.build(world,std::move(spatial_package),{brush,std::move(models)},{});
    require(bool(built),"Runtime brush scene rejected");
    return std::make_shared<const ws::WorldSceneRenderPackage>(std::move(*built.package));
}
inline void add_runtime_brush(r::RenderScene& scene) {
    require(scene.static_world && scene.static_world->package,"Runtime brush requires base world");
    auto brush=quad_package(false);auto map=brush_scene(scene.static_world->package,brush);
    auto frame=std::make_shared<ws::RuntimeBrushRenderFrame>();frame->scene_identity=map->resource_identity();frame->generation=frame->revision=1;
    for(unsigned i=0;i<2;++i) {
        const float x=i==0?-.45F:.45F,y=i==0?-.25F:.25F;
        ws::RuntimeBrushRenderInstance instance;instance.entity_number=100+i;instance.model_slot=10;instance.source_model_index=1;
        instance.model_transform.values[12]=x;instance.model_transform.values[13]=y;
        instance.transformed_bounds={{x-.6F,y,-.6F},{x+.6F,y,.6F}};
        frame->instances.push_back(instance);
    }
    require(ws::valid_runtime_brush_frame(*frame,*map),"Runtime brush frame identity/bounds rejected");
    scene.static_world->scene_package=std::move(map);scene.static_world->runtime_brushes=std::move(frame);
}
inline void add_decals(r::RenderScene& scene) {
    auto texture=fixture::make_texture(false);texture.alpha_mode=a::WorldTextureAlphaMode::straight_alpha;
    for(auto& mip:texture.mip_levels)for(std::size_t p=0;p<mip.rgba_pixels.size();p+=4) {
        mip.rgba_pixels[p]=std::byte{20};mip.rgba_pixels[p+1]=std::byte{230};mip.rgba_pixels[p+2]=std::byte{40};mip.rgba_pixels[p+3]=std::byte{145};
    }
    using V=r::RenderDecalVertex;
    auto vertices=std::make_shared<const std::vector<V>>(std::vector<V>{
        {{-.3F,-.01F,-.3F},{0,0}},{{.3F,-.01F,-.3F},{1,0}},{{.3F,-.01F,.3F},{1,1}},
        {{-.3F,-.01F,-.3F},{0,0}},{{.3F,-.01F,.3F},{1,1}},{{-.3F,-.01F,.3F},{0,1}}});
    scene.world_decals=r::RenderWorldDecals{std::make_shared<const a::WorldTextureAsset>(std::move(texture)),std::move(vertices),1};
}
} // namespace sky_advanced
