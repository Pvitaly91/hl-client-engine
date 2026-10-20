#include <algorithm>
#include <bit>
#include <cmath>
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/app/remote_support_presentation.hpp>
#include <hlclient/goldsrc/reference_brush_collision.hpp>
#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>
#include <hlclient/entity_render/entity_render_frame_composer.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/byte_reader.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_worldspawn_wad_references.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_render_library.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_entity.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp>
#include <hlclient/entity_render/studio_socket.hpp>
#include <hlclient/entity_render/beam_ribbon.hpp>
#include <hlclient/goldsrc/spatial/goldsrc_spatial_package_builder.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>
#include <hlclient/goldsrc/local_resource_inventory.hpp>
#include <hlclient/goldsrc/precache_asset_dispatch.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>
#include <hlclient/goldsrc/sprite/goldsrc_sprite_importer.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_visual_asset_import.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import.hpp>
#include <hlclient/goldsrc/wad3/goldsrc_wad3_catalog.hpp>
#include <hlclient/goldsrc/wad3/goldsrc_wad3_texture.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_assets/local_asset_source.hpp>
#include <hlclient/world_render/world_render_package_builder.hpp>
#include <hlclient/world_spatial/world_spatial_query.hpp>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace hlclient::app {
namespace {
// Observation validity guarantees strictly ascending, unique entity numbers.
// Presented effect entities are a filtered copy preserving that same order.
[[nodiscard]] const client::RuntimePacketEntityObservation* find_sorted_packet_entity(
    const std::vector<client::RuntimePacketEntityObservation>& entities,
    std::uint32_t number) noexcept {
  const auto found=std::ranges::lower_bound(entities,number,{},
      &client::RuntimePacketEntityObservation::entity_number);
  return found!=entities.end() && found->entity_number==number ? &*found : nullptr;
}
bool rigid_effect_surface_transform(const renderer::RenderMatrix4& matrix) noexcept {
  if(!renderer::is_finite(matrix) || std::abs(matrix.values[3])>1e-6F ||
      std::abs(matrix.values[7])>1e-6F || std::abs(matrix.values[11])>1e-6F ||
      std::abs(matrix.values[15]-1)>1e-6F)return false;
  const assets::AssetVector3 x{matrix.values[0],matrix.values[1],matrix.values[2]},
      y{matrix.values[4],matrix.values[5],matrix.values[6]},
      z{matrix.values[8],matrix.values[9],matrix.values[10]};
  return std::abs(renderer::dot(x,x)-1)<1e-4F && std::abs(renderer::dot(y,y)-1)<1e-4F &&
      std::abs(renderer::dot(z,z)-1)<1e-4F && std::abs(renderer::dot(x,y))<1e-4F &&
      std::abs(renderer::dot(x,z))<1e-4F && std::abs(renderer::dot(y,z))<1e-4F &&
      renderer::dot(renderer::cross(x,y),z)>.9999F;
}
}
const goldsrc::PreparedDecalAssets& RuntimeReplayLocalAssets::prepare_surface_decals(
    std::span<const game_api::DecalAssetRequest> requests) noexcept {
  if(!surface_decal_assets_) {
    try {
      auto prepared=goldsrc::prepare_decal_assets(sound_resources_,requests);
      for(std::size_t i=0;i<std::min(requests.size(),surface_decal_modes_.size());++i)
        surface_decal_modes_[i]=requests[i].material_mode;
      surface_decal_assets_=std::make_shared<const goldsrc::PreparedDecalAssets>(std::move(prepared));
    } catch(...) {}
  }
  static const goldsrc::PreparedDecalAssets unavailable=[] {
    goldsrc::PreparedDecalAssets value;value.archive_status=goldsrc::DecalAssetStatus::capacity;return value;
  }();
  return surface_decal_assets_ ? *surface_decal_assets_ : unavailable;
}
ServerDecalMaterializationResult RuntimeReplayLocalAssets::materialize_server_decal(
    const game_api::TemporaryEffectIntent& intent,WorldImpactPresentation& owner,
    std::uint64_t resource_generation) const noexcept {
  ServerDecalMaterializationResult result;
  if(!intent.surface_decal || !surface_decal_assets_)return result;
  const auto& placement=*intent.surface_decal;
  if(placement.asset_slot>=surface_decal_assets_->count) {
    result.asset_status=goldsrc::DecalAssetStatus::not_authorized;return result;
  }
  const auto& asset=surface_decal_assets_->entries[placement.asset_slot];
  result.asset_status=asset.status;
  if(asset.status!=goldsrc::DecalAssetStatus::ready || !asset.texture)return result;
  WorldImpactPresentation::ServerDecalRequest request{intent.identity,intent.record_ordinal,
      resource_generation,intent.origin,placement.target_entity,asset.texture,surface_decal_modes_[placement.asset_slot]};
  if(!world_scene_) {result.placement.status=ServerDecalStatus::target_unavailable;return result;}
  if(placement.target_entity==0) {
    result.placement=owner.submit_server(request,world_scene_->world_package().get());return result;
  }
  // Birth placement uses the immutable committed target pose, not a delayed
  // render frame or the player's current camera. Current transforms are only
  // used later by frame_batches when the already-attached decal is drawn.
  if(!placement.target_pose) {result.placement.status=ServerDecalStatus::target_unavailable;return result;}
  const auto& pose=*placement.target_pose;
  const auto binding=bindings_.find(pose.model_slot);
  const auto life=entity_follow_.lifetime(placement.target_entity,intent.record_ordinal);
  if(!life || binding==bindings_.end() || !binding->second.brush_model ||
      (placement.target_model_reference && static_cast<std::uint32_t>(*placement.target_model_reference)!=pose.model_slot)) {
    result.placement.status=ServerDecalStatus::target_unavailable;return result;
  }
  const auto transform=goldsrc::brush_models::make_brush_submodel_transform(
      pose.origin,pose.angles_degrees,binding->second.brush_source_origin);
  const auto& library=world_scene_->brush_library();
  const auto model=std::ranges::find(library.models(),*binding->second.brush_model,
      &world_scene_render::BrushSubmodelRenderModel::source_model_index);
  if(!transform || model==library.models().end() || model->render_surface_indices().empty()) {
    result.placement.status=ServerDecalStatus::target_unavailable;return result;
  }
  const WorldImpactPresentation::ServerDecalTarget target{placement.target_entity,pose.model_slot,
      *binding->second.brush_model,life,transform.transform->model_matrix};
  result.placement=owner.submit_server(request,library.render_package().get(),model->render_surface_indices(),target);
  return result;
}
LocalImpactDecalStatus RuntimeReplayLocalAssets::prepare_impact_decal(
    const game_api::LocalImpactAssetProfile& profile, std::size_t slot) noexcept {
  if (slot>=impact_decal_statuses_.size()) return LocalImpactDecalStatus::rejected;
  auto& status=impact_decal_statuses_[slot];
  auto& texture_asset=impact_decal_textures_[slot];
  if (status!=LocalImpactDecalStatus::not_requested) return status;
  status=LocalImpactDecalStatus::rejected;
  try {
    const auto wad_end=std::find(profile.wad_name.begin(),profile.wad_name.end(),'\0');
    const auto texture_end=std::find(profile.texture_name.begin(),profile.texture_name.end(),'\0');
    if (wad_end==profile.wad_name.end() || texture_end==profile.texture_name.end()) return status;
    const std::string_view wad{profile.wad_name.data(),static_cast<std::size_t>(wad_end-profile.wad_name.begin())};
    const std::string_view texture{profile.texture_name.data(),static_cast<std::size_t>(texture_end-profile.texture_name.begin())};
    if (wad.size()<5 || !wad.ends_with(".wad") ||
        !std::all_of(wad.begin(),wad.end(),[](char c){return (c>='a'&&c<='z')||
            (c>='0'&&c<='9')||c=='_'||c=='.';}) || texture.empty()) return status;
    const auto& resources=sound_resources_;
    const auto* world=resources.manifest ? resources.manifest->world_entry() : nullptr;
    if (!world || !world->locator() || !resources.environment) {
      status=LocalImpactDecalStatus::absent; return status;
    }
    auto name=local_resources::LocalVirtualResourceName::create(std::string{wad});
    if (!name) return status;
    auto resolved=resources.environment->resolve_exact_root(*name.name,world->locator()->root_id());
    if (!resolved) {status=LocalImpactDecalStatus::absent;return status;}
    if (resolved.file->file_size()>4U*1024U*1024U) return status;
    auto locator=resources.environment->make_locator(resolved.file->root_id(),*name.name,
        resolved.file->identity(),resolved.file->file_size());
    resolved.file->close();
    if (!locator) return status;
    local_assets::LocalAssetSourceOpenLimits limits;
    limits.maximum_source_bytes=4U*1024U*1024U;
    limits.timeout=std::chrono::seconds{2};
    local_assets::LocalAssetSourceOpener opener;
    auto opened=opener.begin(*locator.locator,resources.environment,limits);
    if (!opened) return status;
    for (std::size_t i=0;i<65536U && !opened.operation->result() &&
        !opened.operation->error();++i)
      opened.operation->update(std::chrono::steady_clock::now());
    auto source=opened.operation->take_result();
    if (!source) return status;
    const auto bytes=source->source().bytes();
    auto catalog=goldsrc::wad3::GoldSrcWad3CatalogParser::parse(bytes,
        goldsrc::wad3::GoldSrcWad3CatalogLimits{4U*1024U*1024U,4096U});
    if (!catalog) return status;
    const auto* entry=catalog.catalog->find_miptex(texture);
    if (!entry) {status=LocalImpactDecalStatus::absent;return status;}
    goldsrc::wad3::GoldSrcWad3TextureRequest request;
    request.expected_texture_name=texture;
    auto decoded=goldsrc::wad3::GoldSrcWad3TextureDecoder::decode(bytes,*entry,request);
    if (!decoded || decoded.texture->alpha_mode!=assets::WorldTextureAlphaMode::masked_index_255 ||
        decoded.texture->width>256U || decoded.texture->height>256U) return status;
    texture_asset=std::make_shared<const assets::WorldTextureAsset>(std::move(*decoded.texture));
    impact_decal_modes_[slot]=profile.material_mode;
    status=LocalImpactDecalStatus::ready;
  } catch (...) { status=LocalImpactDecalStatus::rejected; }
  return status;
}
namespace {
[[nodiscard]] game_api::EffectPaletteMetadata read_effect_palette(
    std::string_view token,
    const std::shared_ptr<const local_resources::LocalResourceEnvironment>& environment,
    const goldsrc::PrecacheManifestEntry& world) {
  game_api::EffectPaletteMetadata result;
  if(token.empty())return result;
  result.status=game_api::EffectPaletteStatus::not_authorized;
  // One compiled module dependency: a leaf palette under gfx, exactly768 RGB
  // bytes. No arbitrary directory, unbounded source or alternate installation.
  if(!token.starts_with("gfx/") || !token.ends_with(".lmp") || token.size()>64 ||
      token.substr(4).find('/')!=token.npos || token.find("..")!=token.npos ||
      !std::ranges::all_of(token,[](char c){return (c>='a'&&c<='z') ||
          (c>='0'&&c<='9') || c=='/' || c=='_' || c=='.';}) || !environment || !world.locator())return result;
  const auto name=local_resources::LocalVirtualResourceName::create(std::string{token});
  if(!name)return result;
  auto resolved=environment->resolve_exact_root(*name.name,world.locator()->root_id());
  if(!resolved) {result.status=game_api::EffectPaletteStatus::absent;return result;}
  if(resolved.file->file_size()!=768) {result.status=game_api::EffectPaletteStatus::decode_failed;return result;}
  auto locator=environment->make_locator(resolved.file->root_id(),*name.name,
      resolved.file->identity(),resolved.file->file_size());
  resolved.file->close();if(!locator)return result;
  local_assets::LocalAssetSourceOpenLimits limits;limits.maximum_source_bytes=768;
  limits.timeout=std::chrono::seconds{2};
  local_assets::LocalAssetSourceOpener opener;
  auto opened=opener.begin(*locator.locator,environment,limits);
  if(!opened) {result.status=game_api::EffectPaletteStatus::open_failed;return result;}
  for(std::size_t i=0;i<32 && !opened.operation->result() && !opened.operation->error();++i)
    opened.operation->update(std::chrono::steady_clock::now());
  auto source=opened.operation->take_result();
  if(!source) {result.status=game_api::EffectPaletteStatus::open_failed;return result;}
  const auto bytes=source->source().bytes();
  if(bytes.size()!=768) {result.status=game_api::EffectPaletteStatus::decode_failed;return result;}
  for(std::size_t color=0;color<256;++color)for(std::size_t channel=0;channel<3;++channel)
    result.colors[color][channel]=std::to_integer<std::uint8_t>(bytes[color*3+channel]);
  result.status=game_api::EffectPaletteStatus::ready;return result;
}
[[nodiscard]] std::string read_movement_materials(
    const std::shared_ptr<const local_resources::LocalResourceEnvironment>& environment,
    const goldsrc::PrecacheManifestEntry& world) {
  if (!environment || !world.locator()) return {};
  const auto name=local_resources::LocalVirtualResourceName::create("sound/materials.txt");
  if (!name) return {};
  auto resolved=environment->resolve_exact_root(*name.name,world.locator()->root_id());
  if (!resolved || resolved.file->file_size()>128U*1024U) return {};
  auto locator=environment->make_locator(resolved.file->root_id(),*name.name,
      resolved.file->identity(),resolved.file->file_size());
  resolved.file->close();
  if (!locator) return {};
  local_assets::LocalAssetSourceOpenLimits limits;
  limits.maximum_source_bytes=128U*1024U;
  limits.timeout=std::chrono::seconds{2};
  local_assets::LocalAssetSourceOpener opener;
  auto opened=opener.begin(*locator.locator,environment,limits);
  if (!opened) return {};
  for (std::size_t i=0;i<32U && !opened.operation->result() &&
      !opened.operation->error();++i)
    opened.operation->update(std::chrono::steady_clock::now());
  auto source=opened.operation->take_result();
  if (!source) return {};
  const auto bytes=source->source().bytes();
  return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
} // namespace
void RuntimeReplayLocalAssets::sound_sources(const client::RuntimeClientObservationState& state,
    const goldsrc::collision::BrushCollisionScene* sampled,std::vector<goldsrc::SoundSource>& sources) const {
  sources.clear();
  for(const auto& entity:state.packet_entities) {
    if(sources.size()>=8192 || !entity.origin.complete()) continue;
    assets::AssetVector3 position{static_cast<float>(*entity.origin.x),static_cast<float>(*entity.origin.y),static_cast<float>(*entity.origin.z)};
    const auto binding=entity.model_index ? bindings_.find(*entity.model_index) : bindings_.end();
    if(entity.model_index) {
      const auto named=summary_->model_names.find(*entity.model_index);
      if(named!=summary_->model_names.end() && named->second.starts_with("*") &&
          (binding==bindings_.end() || !binding->second.brush_model)) continue;
    }
    if(binding!=bindings_.end() && binding->second.brush_model) {
      if(!entity.angles.complete() || !world_scene_) continue;
      const auto& b=binding->second;
      const auto transform=goldsrc::brush_models::make_brush_submodel_transform(position,
          {static_cast<float>(*entity.angles.x),static_cast<float>(*entity.angles.y),static_cast<float>(*entity.angles.z)},b.brush_source_origin);
      if(!transform) continue;
      const auto models=world_scene_->brush_library().models();
      const auto model=std::ranges::find_if(models,[&](const auto& m){return m.source_model_index()==*b.brush_model;});
      if(model==models.end()) continue;
      const auto bounds=goldsrc::brush_models::transform_brush_bounds(model->local_bounds(),*transform.transform);
      if(!bounds) continue;
      position={(bounds.bounds->minimum.x+bounds.bounds->maximum.x)*0.5F,
          (bounds.bounds->minimum.y+bounds.bounds->maximum.y)*0.5F,
          (bounds.bounds->minimum.z+bounds.bounds->maximum.z)*0.5F};
      if(sampled) for(const auto& p:sampled->instances())
        if(p.identity.source_entity_index==entity.entity_number && p.identity.source_model_index==*b.brush_model)
          position.z+=p.transform.translation.z-transform.transform->model_matrix.values[14];
    }
    sources.push_back({entity.entity_number,position,entity_follow_.lifetime(entity.entity_number)});
  }
}
std::shared_ptr<const world_scene_render::RuntimeBrushRenderFrame>
present_runtime_brushes(const world_scene_render::RuntimeBrushRenderFrame& committed,
    const goldsrc::collision::BrushCollisionScene& sampled, const std::uint64_t generation) {
  if (generation != committed.generation) return {};
  auto frame = std::make_shared<world_scene_render::RuntimeBrushRenderFrame>(committed);
  for (auto& instance : frame->instances) for (const auto& brush : sampled.instances()) {
    if (brush.identity.source_entity_index != instance.entity_number ||
        brush.identity.source_model_index != instance.source_model_index) continue;
    const auto dz = brush.transform.translation.z - instance.model_transform.values[14];
    instance.model_transform.values[14] += dz;
    instance.transformed_bounds.minimum.z += dz;
    instance.transformed_bounds.maximum.z += dz;
    if (dz != 0.0F) instance.touched_leaf_indices.clear(); // conservative frustum fallback
  }
  return frame;
}
namespace {
namespace g = goldsrc;
namespace v = entity_visual;
namespace r = entity_render;

// Existing StudioPoseCache deliberately coalesces normalized frame keys. This
// local cache instead retains the complete raw input (including source-frame
// metadata) and the exact sealed skeletal object as well as resource revision.
// It is only an optimization: failures and capacity pressure keep the ordinary
// evaluator path, and its storage dies with this one materialization call.
struct OrdinaryFramePose {
  const assets::SkeletalModelAssetData* model{};
  g::studio::StudioPoseModelIdentity identity;
  g::studio::StudioPoseInput input;
  std::shared_ptr<const g::studio::StudioPoseEvaluationResult> result;
};
static_assert(std::is_nothrow_move_constructible_v<g::studio::StudioPoseEvaluationResult>);
bool exact_ordinary_pose_input(const g::studio::StudioPoseInput& a,
                               const g::studio::StudioPoseInput& b) noexcept {
  return a.sequence_index==b.sequence_index &&
      std::bit_cast<std::uint64_t>(a.frame_coordinate)==std::bit_cast<std::uint64_t>(b.frame_coordinate) &&
      a.body_value==b.body_value && a.skin_family_index==b.skin_family_index &&
      a.controller_values==b.controller_values && a.blending_values==b.blending_values &&
      a.mouth_value==b.mouth_value && a.compatibility_profile==b.compatibility_profile &&
      std::bit_cast<std::uint32_t>(a.entity_scale.x)==std::bit_cast<std::uint32_t>(b.entity_scale.x) &&
      std::bit_cast<std::uint32_t>(a.entity_scale.y)==std::bit_cast<std::uint32_t>(b.entity_scale.y) &&
      std::bit_cast<std::uint32_t>(a.entity_scale.z)==std::bit_cast<std::uint32_t>(b.entity_scale.z);
}

// Neutral, bounded downward probe over the already validated world triangles.
// Interpolated atlas UVs use the same selected static style as world rendering.
// No BSP reparse, asset open, visibility dependency or game material names.
std::optional<std::array<float,3>> static_world_light(
    const world_render::WorldRenderPackage& world, assets::AssetVector3 origin) {
  double nearest = 2048.0;
  std::optional<std::array<float,3>> result;
  const auto vertices=world.vertices(); const auto indices=world.indices();
  std::size_t examined{};
  for (const auto& range:world.surface_ranges()) {
    if (origin.x<range.bounds.minimum.x || origin.x>range.bounds.maximum.x ||
        origin.y<range.bounds.minimum.y || origin.y>range.bounds.maximum.y ||
        range.bounds.minimum.z>origin.z || origin.z-range.bounds.maximum.z>nearest) continue;
    for (std::size_t i=range.first_index;i+2U<static_cast<std::size_t>(range.first_index)+range.index_count;i+=3U) {
      if (++examined>1'048'576U) return {};
      const auto& a=vertices[indices[i]]; const auto& b=vertices[indices[i+1U]];
      const auto& c=vertices[indices[i+2U]];
      const double determinant=(b.position.y-c.position.y)*(a.position.x-c.position.x)+
          (c.position.x-b.position.x)*(a.position.y-c.position.y);
      if (std::abs(determinant)<1e-8) continue;
      const double u=((b.position.y-c.position.y)*(origin.x-c.position.x)+
          (c.position.x-b.position.x)*(origin.y-c.position.y))/determinant;
      const double v=((c.position.y-a.position.y)*(origin.x-c.position.x)+
          (a.position.x-c.position.x)*(origin.y-c.position.y))/determinant;
      const double w=1.0-u-v;
      if (u<0.0 || v<0.0 || w<0.0) continue;
      const double distance=origin.z-(u*a.position.z+v*b.position.z+w*c.position.z);
      if (distance<0.0 || distance>nearest) continue;
      nearest=distance; result.reset(); // Never sample through a nearer unlit surface.
      const auto* binding=world.lightmaps().binding_for_surface(range.source_world_surface_index);
      if (!binding || !binding->atlas_page_index ||
          binding->status!=assets::WorldSurfaceLightmapBindingStatus::resolved) continue;
      const auto& page=world.lightmaps().pages()[*binding->atlas_page_index];
      const auto& image=page.style_slot_images[binding->selected_static_source_style_slot];
      const double s=(u*a.lightmap_atlas_coordinate.x+v*b.lightmap_atlas_coordinate.x+
          w*c.lightmap_atlas_coordinate.x)*page.width-0.5;
      const double t=(u*a.lightmap_atlas_coordinate.y+v*b.lightmap_atlas_coordinate.y+
          w*c.lightmap_atlas_coordinate.y)*page.height-0.5;
      const double x=std::clamp(s,0.0,static_cast<double>(page.width-1U));
      const double y=std::clamp(t,0.0,static_cast<double>(page.height-1U));
      const auto x0=static_cast<std::uint32_t>(x), y0=static_cast<std::uint32_t>(y);
      const auto x1=std::min(x0+1U,page.width-1U), y1=std::min(y0+1U,page.height-1U);
      const auto sample=[&](std::uint32_t px,std::uint32_t py,std::size_t channel) {
        return static_cast<double>(std::to_integer<unsigned char>(image.rgba_pixels[
            (static_cast<std::size_t>(py)*page.width+px)*4U+channel]))/255.0;
      };
      std::array<float,3> rgb{};
      for(std::size_t channel=0;channel<3U;++channel) {
        const double top=std::lerp(sample(x0,y0,channel),sample(x1,y0,channel),x-x0);
        const double bottom=std::lerp(sample(x0,y1,channel),sample(x1,y1,channel),x-x0);
        rgb[channel]=static_cast<float>(std::lerp(top,bottom,y-y0));
      }
      result=rgb;
    }
  }
  return result;
}
constexpr std::uint64_t library_id = 0x495245504c415901ULL;
constexpr std::uint64_t package_id = 0x495245504c415902ULL;
constexpr std::uint64_t frame_id = 0x495245504c415903ULL;

RuntimeReplayVisualProjectionError visual_error(std::string text) {
  return {RuntimeReplayVisualProjectionErrorCode::asset_build_failed,
          {},
          {},
          std::move(text)};
}

class AssetPreparationCancelled final : public std::exception {
public:
  const char* what() const noexcept override {return "asset_preparation_cancelled";}
};
void check_preparation(const std::shared_ptr<LoadingWork>& work) {
  if(work && work->cancelled.load())throw AssetPreparationCancelled{};
}
void preparation_progress(const std::shared_ptr<LoadingWork>& work,const LoadingStage stage,
    const std::size_t done=0U,const std::size_t total=0U) {
  if(done>(std::numeric_limits<std::uint32_t>::max)() ||
      total>(std::numeric_limits<std::uint32_t>::max)())
    throw std::runtime_error{"asset preparation progress count exceeds its bounded mailbox"};
  if(work && !work->progress(stage,static_cast<std::uint32_t>(done),static_cast<std::uint32_t>(total)))
    throw AssetPreparationCancelled{};
}

// ReHLDS engine/crc.cpp::CRC_MapFile and sv_main.cpp::SV_SpawnServer;
// Xash engine/common/mod_bmodel.c::CRC32_MapFile. The multiplayer checksum
// starts at all ones and has NO final complement. Only ordered BSP lumps
// 1..14 participate: entity patches, directory offsets and padding do not.
// This validates the bounded directory for checksum access; the canonical
// BSP importer still independently validates all geometry and references.
std::optional<std::uint32_t> live_hl_map_crc(
    const std::span<const std::byte> bytes,
    const std::shared_ptr<LoadingWork>& loading_work) {
  namespace bsp = g::bsp;
  if (bytes.size() < bsp::kGoldSrcBspHeaderWireSize ||
      bytes.size() > bsp::kGoldSrcBspDefaultMaximumSourceBytes) return {};
  g::ByteReader reader{bytes};
  if (reader.read_int32_le() != bsp::kGoldSrcBspVersion) return {};
  std::array<std::pair<std::size_t, std::size_t>, bsp::kGoldSrcBspLumpCount> ranges{};
  for (auto& [offset, length] : ranges) {
    const auto wire_offset = reader.read_int32_le();
    const auto wire_length = reader.read_int32_le();
    if (!wire_offset || !wire_length || *wire_offset < 0 || *wire_length < 0) return {};
    offset = static_cast<std::size_t>(*wire_offset);
    length = static_cast<std::size_t>(*wire_length);
    if (offset > bytes.size() || length > bytes.size() - offset ||
        (length != 0U && offset < bsp::kGoldSrcBspHeaderWireSize)) return {};
  }
  for (std::size_t left = 0U; left < ranges.size(); ++left) {
    if (ranges[left].second == 0U) continue;
    for (std::size_t right = left + 1U; right < ranges.size(); ++right) {
      if (ranges[right].second != 0U &&
          ranges[left].first < ranges[right].first + ranges[right].second &&
          ranges[right].first < ranges[left].first + ranges[left].second) return {};
    }
  }
  static constexpr auto table = [] {
    std::array<std::uint32_t, 256U> values{};
    for (std::uint32_t index = 0U; index < values.size(); ++index) {
      auto value = index;
      for (unsigned bit = 0U; bit < 8U; ++bit)
        value = (value >> 1U) ^ ((value & 1U) ? 0xedb88320U : 0U);
      values[index] = value;
    }
    return values;
  }();
  std::uint32_t crc = 0xffffffffU;
  for (std::size_t index = 1U; index < ranges.size(); ++index) {
    const auto [offset, length] = ranges[index];
    const auto lump = bytes.subspan(offset, length);
    for (std::size_t position = 0U; position < lump.size(); ++position) {
      if ((position & 0xffffU) == 0U) check_preparation(loading_work);
      crc = (crc >> 8U) ^ table[(crc ^ std::to_integer<std::uint8_t>(lump[position])) & 255U];
    }
  }
  return crc;
}

g::ApprovedAssetSource open_source(
    const g::PrecacheManifestState &manifest,
    const g::PrecacheManifestEntry &entry,
    const std::shared_ptr<const local_resources::LocalResourceEnvironment>
        &environment,const std::shared_ptr<LoadingWork>& loading_work) {
  check_preparation(loading_work);
  auto plan = g::AssetDispatchPlanBuilder{}.build(manifest, entry);
  if (!plan) {
    throw std::runtime_error{"asset dispatch plan rejected"};
  }
  g::ApprovedAssetSourceOpener opener;
  auto opened = opener.begin(*plan.plan, environment);
  if (!opened) {
    throw std::runtime_error{"approved source open rejected"};
  }
  for (std::size_t i = 0;
       i < 65536U && !opened.operation->result() && !opened.operation->error();
       ++i) {
    check_preparation(loading_work);
    opened.operation->update(std::chrono::steady_clock::now());
  }
  check_preparation(loading_work);
  auto result = opened.operation->take_result();
  if (!result) {
    throw std::runtime_error{opened.operation->error()
                                 ? opened.operation->error()->context
                                 : "bounded source read failed"};
  }
  return std::move(*result);
}

assets::WorldBounds bounds(const assets::WorldBounds &b,
                           const r::EntityRenderTransform &t) {
  const auto result = r::transform_entity_render_bounds(b, t);
  if (!result) {
    throw std::runtime_error{"nonfinite transformed entity bounds"};
  }
  return *result;
}

std::optional<client::RenderCameraState> spectator_camera(
    const std::shared_ptr<const collision::CollisionWorldPackage> &world,
    const assets::AssetVector3 &origin) {
  collision::CollisionWorldQuery query{world};
  collision::CollisionQueryScratch scratch;
  const assets::AssetVector3 target{origin.x, origin.y, origin.z + 8.0F};
  std::optional<client::RenderCameraState> camera;
  double best_fraction = 0.1;
  // One bounded initial placement around a decoded model, clipped through
  // the existing BSP query. This is not movement, prediction or player ID.
  constexpr std::array<assets::AssetVector3, 8> offsets{{{120, -120, 48},
                                                         {-120, -120, 48},
                                                         {120, 120, 48},
                                                         {-120, 120, 48},
                                                         {160, 0, 48},
                                                         {-160, 0, 48},
                                                         {0, 160, 48},
                                                         {0, -160, 48}}};
  for (const auto &offset : offsets) {
    collision::CollisionTraceRequest request;
    request.start = target;
    request.end = {target.x + offset.x, target.y + offset.y,
                   target.z + offset.z};
    const auto traced = query.trace_line(request, scratch);
    if (!traced || traced.result->start_solid || traced.result->all_solid) {
      continue;
    }
    const auto fraction = std::max(0.0, traced.result->fraction - 0.03);
    if (fraction <= best_fraction) {
      continue;
    }
    best_fraction = fraction;
    const assets::AssetVector3 position{
        target.x + offset.x * static_cast<float>(fraction),
        target.y + offset.y * static_cast<float>(fraction),
        target.z + offset.z * static_cast<float>(fraction)};
    camera = client::RenderCameraState{position,    target, {0, 0, 1},
                                       1.04719755F, 1.0F,   4096.0F};
  }
  return camera;
}
} // namespace

ReplayLocalAssetsCreateResult
RuntimeReplayLocalAssets::create(const g::RuntimeReplayCaptureState &capture,
                                 const std::filesystem::path &basedir,
                                 const std::string_view game,
                                 std::shared_ptr<LoadingWork> loading_work) {
  return create(capture.resources(), capture.server_info(), basedir, game,
                ReplayLocalCameraPolicy::offline_spectator,{},{},{},std::move(loading_work));
}

ReplayLocalAssetsCreateResult
RuntimeReplayLocalAssets::create(const g::ResourceListState &resources,
                                 const g::ServerInfoState &server_info,
                                 const std::filesystem::path &basedir,
                                 const std::string_view game,
                                 const ReplayLocalCameraPolicy camera_policy,
                                 const std::string_view effect_palette_dependency,
                                 const std::span<const std::string> effect_sprite_dependencies,
                                 const std::string_view sky_name,
                                 std::shared_ptr<LoadingWork> loading_work,
                                 const ReplayLocalResourcePolicy resource_policy) {
  std::unique_ptr<RuntimeReplayLocalAssets> result;
  try {
    preparation_progress(loading_work,LoadingStage::resources);
    if (resource_policy != ReplayLocalResourcePolicy::recorded_exact_size &&
        resource_policy != ReplayLocalResourcePolicy::live_standard_hl)
      throw std::runtime_error{"invalid local resource validation policy"};
    result.reset(new RuntimeReplayLocalAssets);
    result->camera_policy_ = camera_policy;
    auto roots =
        local_resources::LocalResourceSearchRoots::create(basedir, game);
    if (!roots) {
      throw std::runtime_error{"explicit local asset roots rejected"};
    }
    auto resolver_limits = local_resources::LocalResourceResolverLimits{};
    // Match the existing world-texture path: stock WAD archives may exceed
    // the generic 16 MiB file default. All rooted checks still apply.
    resolver_limits.maximum_file_size =
        local_resources::kHardMaximumLocalResourceFileSize;
    auto env = local_resources::LocalResourceEnvironment::create(
        std::move(*roots.roots), resolver_limits);
    if (!env) {
      throw std::runtime_error{"local asset environment rejected"};
    }
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment{std::move(env.environment)};
    g::GoldSrcResourceNameMapper mapper;
    auto inventory = g::LocalResourceInventoryBuilder{}.build(
        resources, mapper, environment->resolver());
    check_preparation(loading_work);
    if (!inventory) {
      throw std::runtime_error{"captured resource inventory rejected"};
    }
    auto manifest = g::PrecacheManifestBuilder{}.build(
        resources, *inventory.state, server_info, mapper, *environment);
    check_preparation(loading_work);
    if (!manifest || !manifest.state->world_geometry_ready() ||
        !manifest.state->world_entry()) {
      const auto safe_map=local_resources::LocalVirtualResourceName::create(server_info.map_file_path());
      throw std::runtime_error{"map_resource_not_ready: virtual=" +
          (safe_map ? std::string{safe_map.name->value()} : std::string{"rejected_virtual_name"}) +
          "; status=" + std::string{manifest ? g::to_string(manifest.state->completeness()) :
              manifest.error ? g::to_string(manifest.error->code) : std::string_view{"unavailable"}}};
    }
    auto &summary = *result->summary_;
    std::size_t checked_resources{};
    for(const auto& entry:inventory.state->entries()) {
      preparation_progress(loading_work,LoadingStage::resources,checked_resources++,inventory.state->entries().size());
      if(entry.status()==g::LocalResourceInventoryStatus::resolved) continue;
      const auto& wire=resources.entries()[entry.wire_ordinal()];
      if (!g::resource_has_bindable_index(wire.type(), entry.resource_index())) continue;
      const auto mapped=mapper.classify(wire.type(),wire.name().bytes());
      if(mapped.kind()==g::GoldSrcResourceNameClassificationKind::metadata_only ||
          wire.name().bytes().starts_with('*')) continue;
      if(summary.resource_issues.size()>=32U) {++summary.resource_issues_omitted;continue;}
      summary.resource_issues.push_back({mapped.safe_virtual_name() ?
          std::string{mapped.safe_virtual_name()->value()} : std::string{"rejected_virtual_name"},
          std::string{g::to_string(entry.status())},entry.resource_index(),
          static_cast<std::uint8_t>(entry.resource_type())});
    }
    preparation_progress(loading_work,LoadingStage::resources,checked_resources,checked_resources);
    result->maximum_clients_=server_info.maximum_clients().value();
    result->sound_resources_={std::make_shared<const g::PrecacheManifestState>(*manifest.state),environment};
    result->player_model_assets_=std::make_shared<g::ApprovedPlayerModelAssets>(result->sound_resources_);
    result->movement_materials_=read_movement_materials(environment,*manifest.state->world_entry());
    check_preparation(loading_work);
    result->effect_palette_=read_effect_palette(effect_palette_dependency,environment,*manifest.state->world_entry());
    check_preparation(loading_work);
    summary.map = server_info.map_file_path();
    summary.precache_completeness =
        g::to_string(manifest.state->completeness());
    summary.captured_resources = resources.entries().size();
    const auto check_size = [&](const g::PrecacheManifestEntry &entry) {
      const auto code =
          resources.entries()[entry.wire_ordinal()].declared_size().raw_code();
      // Standard resource lists can retain an unavailable -1 code.
      // Only nonnegative, positive file sizes establish a size comparison.
      if (code == 0U || code == 0xffffffU) {
        ++summary.size_unavailable;
        return;
      }
      if (!entry.local_file_size()) {
        throw std::runtime_error{"approved local resource size unavailable at model slot " +
            std::to_string(entry.resource_index())};
      }
      if (*entry.local_file_size() != code) {
        if (resource_policy == ReplayLocalResourcePolicy::live_standard_hl) {
          ++summary.size_mismatches;
          return;
        }
        throw std::runtime_error{
            "captured/local resource size mismatch at model slot " +
            std::to_string(entry.resource_index())};
      }
      ++summary.size_matches;
    };
    preparation_progress(loading_work,LoadingStage::map);
    check_size(*manifest.state->world_entry());
    assets::AssetImporterRegistries registries;
    if (!g::register_builtin_asset_importers(
            registries, {}, g::bsp::GoldSrcBspParseOptions{true})) {
      throw std::runtime_error{"builtin importer registration failed"};
    }
    auto world_source = open_source(
        *manifest.state, *manifest.state->world_entry(), environment,loading_work);
    if (resource_policy == ReplayLocalResourcePolicy::live_standard_hl) {
      const auto actual_crc = live_hl_map_crc(world_source.source().bytes(), loading_work);
      if (!actual_crc)
        throw std::runtime_error{"map_crc_invalid_bsp: approved map has an invalid bounded BSP30 directory"};
      const bool multiplayer = server_info.maximum_clients().value() > 1U;
      // Stock GoldSrc uses zero for singleplayer, not a map-content checksum.
      // Zero is not treated as an unavailable/wildcard checksum in multiplayer.
      const auto expected_crc = multiplayer ? *actual_crc : 0U;
      if (expected_crc != server_info.world_map_crc()) {
        const auto safe_map = local_resources::LocalVirtualResourceName::create(
            server_info.map_file_path());
        const auto name = safe_map ? safe_map.name->value()
                                   : std::string_view{"rejected_virtual_name"};
        // Bounded, escaped server virtual name only; never a native path.
        std::string diagnostic_name;
        std::size_t retained = 0U;
        for (const char value : name) {
          const auto needed = value == '"' ? 2U : 1U;
          if (diagnostic_name.size() + needed > 128U) break;
          if (value == '"') diagnostic_name.push_back('\\');
          diagnostic_name.push_back(value);
          ++retained;
        }
        throw std::runtime_error{"map_crc_mismatch: expected=" +
            std::to_string(server_info.world_map_crc()) + " actual=" +
            std::to_string(expected_crc) + " multiplayer=" +
            std::to_string(multiplayer ? 1U : 0U) + " virtual=\"" +
            diagnostic_name + "\" truncated=" +
            std::to_string(retained != name.size() ? 1U : 0U)};
      }
      summary.world_crc_verified = multiplayer;
    }
    auto world_plan = g::AssetDispatchPlanBuilder{}.build(
        *manifest.state, *manifest.state->world_entry());
    auto imported = g::ApprovedAssetImporterDispatcher{registries}.dispatch(
        world_source, *world_plan.plan);
    check_preparation(loading_work);
    if (!imported.imported() ||
        !std::holds_alternative<assets::WorldAsset>(*imported.asset)) {
      throw std::runtime_error{imported.error ? imported.error->context
                                              : "captured BSP import failed"};
    }
    auto world = std::get<assets::WorldAsset>(std::move(*imported.asset));
    const auto attachment = std::dynamic_pointer_cast<
        const g::bsp::GoldSrcBspCollisionImportAttachment>(imported.attachment);
    if (!attachment) {
      throw std::runtime_error{"BSP camera collision attachment absent"};
    }
    const auto collision = g::collision::GoldSrcCollisionWorldBuilder::build(
        attachment->collision_source());
    check_preparation(loading_work);
    if (!collision) {
      throw std::runtime_error{collision.error->context};
    }
    result->camera_collision_ = collision.package;
    // Recover names only from the already retained, validated BSP metadata.
    // The existing importer supplies the actual archive outcome/ordinal; this
    // does not probe, resolve or reopen any asset for diagnostic purposes.
    const auto wad_references = g::bsp::GoldSrcEntityLumpParser::
        parse_worldspawn_wad_references(attachment->document().entity_lump_bytes);
    const auto record_wad_issue = [&](std::uint32_t ordinal, std::string_view reason) {
      if (!wad_references || ordinal > (std::numeric_limits<std::uint16_t>::max)()) return;
      const auto references = wad_references.references->references();
      const auto reference = std::ranges::find_if(references, [ordinal](const auto& item) {
        return item.declaration_ordinal == ordinal;
      });
      if (reference == references.end()) return;
      if (std::ranges::any_of(summary.resource_issues, [&](const auto& issue) {
        return issue.resource_type == 255U && issue.virtual_name == reference->basename &&
            issue.reason == reason;
      })) return;
      if (summary.resource_issues.size() >= 32U) {++summary.resource_issues_omitted;return;}
      summary.resource_issues.push_back({reference->basename,std::string{reason},
          static_cast<std::uint16_t>(ordinal),255U});
    };
    const auto archive_issue_reason = [](assets::WorldTextureArchiveStatus status) -> std::string_view {
      switch(status) {
      case assets::WorldTextureArchiveStatus::missing: return "wad_missing";
      case assets::WorldTextureArchiveStatus::malformed: return "wad_malformed";
      case assets::WorldTextureArchiveStatus::unsupported_profile: return "wad_unsupported_profile";
      case assets::WorldTextureArchiveStatus::not_required:
      case assets::WorldTextureArchiveStatus::resolved: return {};
      }
      return {};
    };
    g::GoldSrcWorldTextureImportLimits texture_limits;
    preparation_progress(loading_work,LoadingStage::textures);
    texture_limits.missing_texture_policy = g::MissingWorldTexturePolicy::placeholder_for_absent_name;
    auto textures = g::WorldTextureImportOperation::begin(
        world, world_source.source().bytes(), environment, texture_limits);
    if (!textures) {
      throw std::runtime_error{"world texture import rejected"};
    }
    for (std::size_t i = 0; i < 100000U && !textures.operation->terminal();
         ++i) {
      check_preparation(loading_work);
      textures.operation->update(std::chrono::steady_clock::now());
    }
    check_preparation(loading_work);
    auto texture_set = textures.operation->take_result();
    if (!texture_set) {
      if (const auto* failure = textures.operation->error(); failure && failure->archive_ordinal) {
        auto reason = std::string{g::to_string(failure->code)};
        if (failure->resolution_code)
          reason = "wad_resolution_" + std::string{local_resources::to_string(*failure->resolution_code)};
        else if (failure->source_open_code)
          reason = "wad_open_" + std::string{local_assets::to_string(*failure->source_open_code)};
        record_wad_issue(*failure->archive_ordinal,reason);
      }
      throw std::runtime_error{textures.operation->error()
                                   ? textures.operation->error()->context
                                   : "world textures unavailable"};
    }
    for (const auto& archive : texture_set->archive_metadata()) {
      const auto reason = archive_issue_reason(archive.status);
      if (!reason.empty()) record_wad_issue(archive.declaration_ordinal,reason);
    }
    summary.missing_texture_placeholder_bindings = texture_set->statistics().placeholder_material_count;
    auto lightmaps = g::lightmaps::GoldSrcWorldLightmapImporter::import(
        world, world_source.source().bytes());
    check_preparation(loading_work);
    if (!lightmaps) {
      throw std::runtime_error{lightmaps.error->context};
    }
    auto world_package = world_render::WorldRenderPackageBuilder{}.build(
        {std::move(world), std::move(*texture_set)},
        std::move(*lightmaps.lightmap_set));
    check_preparation(loading_work);
    if (!world_package) {
      throw std::runtime_error{world_package.error->context};
    }
    result->world_ = std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*world_package.package));
    result->sky_assets_=g::prepare_sky_assets(result->sound_resources_,sky_name,
        attachment->document().entity_lump_bytes);
    check_preparation(loading_work);

    const auto& document = attachment->document();
    g::brush_models::GoldSrcBrushRenderLibraryLimits brush_limits;
    brush_limits.textures = texture_limits;
    auto brushes = g::brush_models::GoldSrcBrushRenderLibraryBuilder::build(
        document, world_source.source().bytes(), environment, brush_limits);
    check_preparation(loading_work);
    if (!brushes) {
      if (brushes.error && brushes.error->texture_incomplete_reason) {
        const auto& incomplete = *brushes.error->texture_incomplete_reason;
        if (incomplete.source_archive_ordinal && incomplete.archive_status) {
          const auto reason = archive_issue_reason(*incomplete.archive_status);
          if (!reason.empty()) record_wad_issue(*incomplete.source_archive_ordinal,reason);
        }
      }
      throw std::runtime_error{"brush geometry preparation: " +
          std::string{g::brush_models::to_string(brushes.error->code)}};
    }
    summary.missing_texture_placeholder_bindings +=
        static_cast<std::size_t>(brushes.statistics.placeholder_material_count);
    const auto& spatial = document.spatial_source;
    auto spatial_package = g::spatial::GoldSrcSpatialPackageBuilder::build({
        spatial.planes, spatial.nodes, spatial.leaves,
        spatial.marksurface_face_ordinals, spatial.visibility_bytes,
        spatial.world_model, spatial.source_face_count,
        document.world_asset.surfaces, spatial.submodel_face_ordinals});
    check_preparation(loading_work);
    if (!spatial_package) throw std::runtime_error{"brush spatial preparation failed"};
    auto scene = world_scene_render::WorldSceneRenderPackageBuilder{}.build(
        result->world_, std::move(*spatial_package.package),
        std::move(*brushes.library)); // No BSP metadata instances in the live scene.
    check_preparation(loading_work);
    if (!scene) throw std::runtime_error{scene.error->context};
    result->world_scene_ = std::make_shared<const world_scene_render::WorldSceneRenderPackage>(
        std::move(*scene.package));
    summary.prepared_brush_models = result->world_scene_->brush_library().models().size();

    preparation_progress(loading_work,LoadingStage::models);
    std::vector<v::EntityVisualModelReference> references;
    for (const auto &entry : manifest.state->entries()) {
      check_preparation(loading_work);
      if (entry.resource_type() != g::ResourceType::model ||
          &entry == manifest.state->world_entry()) {
        continue;
      }
      const auto name =
          resources.entries()[entry.wire_ordinal()].name().bytes();
      if (name == "models/shell.mdl")
        result->shell_resource_status_ = entry.locator()
            ? ReplayLocalVisualStatus::missing_asset
            : ReplayLocalVisualStatus::dependency_missing;
      if (entry.locator()) {
        summary.model_names.emplace(entry.resource_index(), name);
      }
      if (name.starts_with('*')) {
        // Inline references are a map submodel namespace, never paths.
        summary.model_names.emplace(entry.resource_index(), name);
        Binding binding;
        binding.status = ReplayLocalVisualStatus::invalid_brush_reference;
        const auto reference = g::brush_models::parse_brush_model_reference(
            name, static_cast<std::size_t>(document.geometry_statistics.source_model_count));
        if (reference) {
          binding.status = ReplayLocalVisualStatus::missing_brush_geometry;
          const auto models = result->world_scene_->brush_library().models();
          if (std::ranges::any_of(models, [&](const auto& model) {
                return model.source_model_index() == *reference.source_model_index;
              })) {
            binding.status = ReplayLocalVisualStatus::ready_brush;
            binding.brush_model = *reference.source_model_index;
            for (const auto& model : document.brush_submodels)
              if (model.source_model_index == *binding.brush_model)
                binding.brush_source_origin = model.source_model_origin;
          }
        }
        result->bindings_.emplace(entry.resource_index(), binding);
        continue;
      }
      if (entry.locator()) {
        check_size(entry);
      }
      references.push_back(
          v::EntityVisualModelReference::public_goldsrc48_model_slot(
              entry.resource_index()));
    }
    v::EntityVisualAssetLibraryLimits limits;
    // Offline preparation uses the existing per-operation bounded batches.
    constexpr std::size_t batch_size = 8U;
    preparation_progress(loading_work,LoadingStage::models,0U,references.size());
    for (std::size_t first = 0; first < references.size();
         first += batch_size) {
      check_preparation(loading_work);
      const auto batch =
          std::span<const v::EntityVisualModelReference>{references}.subspan(
              first, std::min(batch_size, references.size() - first));
      auto plan = v::EntityVisualAssetLibraryBuilder{}.plan_references(
          library_id, batch, *manifest.state, v::PublicGoldSrc48ModelResolver{},
          result->library_, limits);
      if (!plan) {
        throw std::runtime_error{plan.error->context};
      }
      std::vector<v::EntityVisualAssetImportCompletion> completions;
      for (const auto &request : plan.plan->requests()) {
        check_preparation(loading_work);
        auto source = open_source(
            *manifest.state,
            manifest.state->entries()[request.manifest_entry_offset()],
            environment,loading_work);
        auto operation =
            g::visual_assets::GoldSrcVisualAssetImportOperation::begin(
                source, environment, registries);
        if (!operation) {
          throw std::runtime_error{"visual importer begin failed"};
        }
        for (std::size_t i = 0; i < 65536U && !operation.operation->terminal();
             ++i) {
          check_preparation(loading_work);
          operation.operation->update(std::chrono::steady_clock::now());
        }
        check_preparation(loading_work);
        auto asset = operation.operation->take_result();
        if (!asset) {
          const auto &failure = operation.operation->error();
          if (!failure ||
              failure->code ==
                  g::visual_assets::GoldSrcVisualAssetImportErrorCode::
                      unable_to_retain_state ||
              failure->code ==
                  g::visual_assets::GoldSrcVisualAssetImportErrorCode::
                      timed_out) {
            throw std::runtime_error{"bounded visual import did not complete"};
          }
          auto status =
              v::EntityVisualAssetImportCompletionStatus::asset_import_failed;
          if (failure->code ==
              g::visual_assets::GoldSrcVisualAssetImportErrorCode::
                  dependency_missing) {
            status = v::EntityVisualAssetImportCompletionStatus::
                asset_dependency_missing;
          } else if (failure->asset_code ==
                         assets::AssetErrorCode::UnsupportedFormat ||
                     failure->code ==
                         g::visual_assets::GoldSrcVisualAssetImportErrorCode::
                             unsupported_external_dependency ||
                     failure->dispatch_state ==
                         assets::AssetDispatchState::importer_not_registered) {
            status = v::EntityVisualAssetImportCompletionStatus::
                unsupported_asset_format;
          } else if (failure->dispatch_state ==
                     assets::AssetDispatchState::ambiguous_importer) {
            status =
                v::EntityVisualAssetImportCompletionStatus::asset_ambiguous;
          }
          completions.push_back({request.request_index(), status, {}});
          continue;
        }
        std::vector<assets::AssetSourceFingerprint> fingerprints(
            asset->source_fingerprints().begin(),
            asset->source_fingerprints().end());
        const auto total = asset->dependency_statistics().total_source_bytes;
        if (const auto *model =
                std::get_if<assets::ModelAsset>(&asset->asset())) {
          completions.push_back(
              {request.request_index(),
               v::EntityVisualAssetImportCompletionStatus::imported,
               v::EntityVisualImportedAssetCandidate::studio_model(
                   request.source_key(),
                   std::make_shared<const assets::ModelAsset>(*model),
                   std::string{asset->selected_importer_id()},
                   std::max(total, source.byte_count()),
                   std::move(fingerprints))});
        } else if (const auto *sprite =
                       std::get_if<assets::SpriteAsset>(&asset->asset())) {
          completions.push_back(
              {request.request_index(),
               v::EntityVisualAssetImportCompletionStatus::imported,
               v::EntityVisualImportedAssetCandidate::sprite(
                   request.source_key(),
                   std::make_shared<const assets::SpriteAsset>(*sprite),
                   std::string{asset->selected_importer_id()},
                   source.byte_count(), std::move(fingerprints))});
        } else {
          throw std::runtime_error{"visual importer category mismatch"};
        }
      }
      auto published = v::EntityVisualAssetLibraryBuilder{}.publish(
          *plan.plan, completions, result->library_, limits);
      if (!published) {
        throw std::runtime_error{published.error->context};
      }
      result->library_ = published.library;
      for (const auto &entry : published.bindings) {
        check_preparation(loading_work);
        auto status = ReplayLocalVisualStatus::unsupported_asset;
        if (entry.selected_category() ==
            v::EntityVisualBindingCategory::missing) {
          status = ReplayLocalVisualStatus::missing_asset;
        }
        if (entry.selected_category() ==
            v::EntityVisualBindingCategory::unsafe) {
          status = ReplayLocalVisualStatus::unsafe_asset;
        }
        if (entry.selected_category() ==
            v::EntityVisualBindingCategory::ambiguous) {
          status = ReplayLocalVisualStatus::ambiguous_asset;
        }
        if (entry.status() ==
            v::EntityVisualBindingStatus::asset_import_failed) {
          status = ReplayLocalVisualStatus::import_failed;
        }
        if (entry.status() ==
            v::EntityVisualBindingStatus::asset_dependency_missing) {
          status = ReplayLocalVisualStatus::dependency_missing;
        }
        result->bindings_.try_emplace(entry.model_reference().value(),
                                      Binding{status, {}, {}});
      }
      preparation_progress(loading_work,LoadingStage::models,first+batch.size(),references.size());
    }
    if (!result->library_) {
      auto plan = v::EntityVisualAssetLibraryBuilder{}.plan_references(
          library_id, {}, *manifest.state, v::PublicGoldSrc48ModelResolver{});
      if (!plan) {
        throw std::runtime_error{plan.error->context};
      }
      auto published =
          v::EntityVisualAssetLibraryBuilder{}.publish(*plan.plan, {});
      if (!published) {
        throw std::runtime_error{published.error->context};
      }
      result->library_ = published.library;
    }
    preparation_progress(loading_work,LoadingStage::scene);
    r::EntitySceneRenderPackageCreateInfo package;
    package.asset_library = result->library_;
    package.asset_library_identity = {result->library_->resource_id(),
                                      result->library_->resource_revision()};
    package.resource_id = package_id;
    std::vector<Binding> record_bindings;
    for (std::size_t i = 0; i < result->library_->records().size(); ++i) {
      check_preparation(loading_work);
      const auto &record = result->library_->records()[i];
      const r::EntityRenderResourceIdentity id{record.resource_id(),
                                               record.resource_revision()};
      if (record.kind() == v::EntityVisualAssetKind::studio_model) {
        auto built =
            r::StudioModelRenderAssetBuilder{}.build(*record.model_asset(), id);
        if (!built) {
          throw std::runtime_error{built.error->context};
        }
        record_bindings.push_back({ReplayLocalVisualStatus::ready_studio, i,
                                   package.studio_assets.size()});
        package.studio_assets.push_back(
            std::make_shared<const r::StudioModelRenderAsset>(
                std::move(*built.asset)));
      } else {
        auto built =
            r::SpriteRenderAssetBuilder{}.build(*record.sprite_asset(), id);
        if (!built) {
          throw std::runtime_error{built.error->context};
        }
        record_bindings.push_back({ReplayLocalVisualStatus::ready_sprite, i,
                                   package.sprite_assets.size()});
        package.sprite_assets.push_back(
            std::make_shared<const r::SpriteRenderAsset>(
                std::move(*built.asset)));
      }
    }
    for (const auto &reference : result->library_->references()) {
      check_preparation(loading_work);
      result->bindings_[reference.reference.value()] =
          record_bindings.at(reference.asset_library_index);
    }
    // Compiled presentation dependencies are rooted in the selected map's
    // approved installation, never assigned fictitious server model slots.
    // Preload in this existing preparation worker: no first-shot file I/O.
    if(effect_sprite_dependencies.size()>8U) {
      ++result->muzzle_sprite_stats_.rejected;
    } else for(const auto& token:effect_sprite_dependencies) {
      check_preparation(loading_work);
      const auto reject=[&]{++result->muzzle_sprite_stats_.rejected;};
      if(token.size()>63 || !token.starts_with("sprites/") || !token.ends_with(".spr") ||
          token.substr(8).find('/')!=std::string::npos || token.find("..")!=std::string::npos ||
          !std::ranges::all_of(token,[](char c){return (c>='a'&&c<='z') || (c>='0'&&c<='9') ||
              c=='_' || c=='.' || c=='/';}) || result->effect_sprite_bindings_.contains(token)) {
        reject();continue;
      }
      const auto name=local_resources::LocalVirtualResourceName::create(token);
      if(!name || !manifest.state->world_entry()->locator()){reject();continue;}
      auto resolved=environment->resolve_exact_root(*name.name,manifest.state->world_entry()->locator()->root_id());
      if(!resolved){++result->muzzle_sprite_stats_.missing;continue;}
      constexpr std::size_t maximum_source=256U*1024U;
      if(resolved.file->file_size()>maximum_source){reject();continue;}
      auto locator=environment->make_locator(resolved.file->root_id(),*name.name,
          resolved.file->identity(),resolved.file->file_size());
      resolved.file->close();if(!locator){reject();continue;}
      local_assets::LocalAssetSourceOpenLimits open_limits;
      open_limits.maximum_source_bytes=maximum_source;open_limits.timeout=std::chrono::seconds{2};
      auto opened=local_assets::LocalAssetSourceOpener{}.begin(*locator.locator,environment,open_limits);
      if(!opened){reject();continue;}
      for(std::size_t i=0;i<65536 && !opened.operation->result() && !opened.operation->error();++i) {
        check_preparation(loading_work);
        opened.operation->update(std::chrono::steady_clock::now());
      }
      check_preparation(loading_work);
      auto source=opened.operation->take_result();if(!source){reject();continue;}
      g::sprite::GoldSrcSpriteImportLimits sprite_limits;
      sprite_limits.maximum_source_bytes=maximum_source;
      sprite_limits.maximum_width=sprite_limits.maximum_height=256;
      sprite_limits.maximum_top_level_entries=sprite_limits.maximum_flattened_frames=8;
      sprite_limits.maximum_group_frames=8;
      sprite_limits.maximum_total_indexed_bytes=256U*256U*8U;
      sprite_limits.maximum_total_rgba_bytes=256U*256U*8U*4U;
      sprite_limits.maximum_pixels_per_frame=256U*256U;
      auto imported_sprite=g::sprite::GoldSrcSpriteImporter{sprite_limits}.import(source->source());
      if(!imported_sprite){reject();continue;}
      auto sprite=std::make_shared<const assets::SpriteAsset>(std::move(imported_sprite.value()));
      const auto fingerprint=g::visual_assets::goldsrc_studio_source_fingerprint(source->source().bytes());
      const r::EntityRenderResourceIdentity identity{0xAE33000000000000ULL+package.presentation_sprite_sources.size()+1U,
          fingerprint.primary ? fingerprint.primary : fingerprint.secondary ? fingerprint.secondary : 1U};
      auto render=r::SpriteRenderAssetBuilder{}.build(*sprite,identity);
      if(!render || render.asset->orientation()!=assets::SpriteOrientation::view_parallel ||
          render.asset->render_profile()!=r::SpriteRenderTextureProfile::additive ||
          !render.asset->groups().empty() || render.asset->frames().empty()){reject();continue;}
      result->effect_sprite_bindings_.emplace(token,package.sprite_assets.size());
      package.sprite_assets.push_back(std::make_shared<const r::SpriteRenderAsset>(std::move(*render.asset)));
      package.presentation_sprite_sources.push_back({identity,std::move(sprite)});
      ++result->muzzle_sprite_stats_.ready;
    }
    summary.imported_studio = package.studio_assets.size();
    summary.imported_sprites = package.sprite_assets.size();
    auto built = r::EntitySceneRenderPackageBuilder{}.build(std::move(package));
    check_preparation(loading_work);
    if (!built) {
      throw std::runtime_error{built.error->context};
    }
    result->package_ = std::make_shared<const r::EntitySceneRenderPackage>(
        std::move(*built.package));
    for (const auto& [index,name] : summary.model_names) {
      check_preparation(loading_work);
      if (name != "models/shell.mdl") continue;
      const auto bound = result->bindings_.find(index);
      if (bound != result->bindings_.end()) {
        result->shell_resource_status_ = bound->second.status;
        if (bound->second.status == ReplayLocalVisualStatus::ready_studio)
          result->shell_model_index_ = index;
      }
      break;
    }
    check_preparation(loading_work);
    // Resource diagnostics are immutable for this local asset context. Keep a
    // second owning copy once, rather than duplicating their maps each frame.
    *result->staged_summary_ = summary;
    return {std::move(result), {}};
  } catch (const AssetPreparationCancelled&) {
    return {{},RuntimeReplayVisualProjectionError{
        RuntimeReplayVisualProjectionErrorCode::asset_preparation_cancelled,{},{},"asset_preparation_cancelled"}};
  } catch (const std::exception &error) {
    return {{}, visual_error(error.what()),
        result ? std::move(result->summary_->resource_issues) :
            std::vector<ReplayLocalVisualSummary::ResourceIssue>{},
        result ? result->summary_->resource_issues_omitted : 0U};
  }
}

RuntimeReplayVisualProjectionResult RuntimeReplayLocalAssets::reset_generation(
    const client::RuntimeClientObservationState &observation,
    client::ClientWorldState &target) {
  if (generation_ != 0U && generation_ != observation.generation) {
    // A new capture must rebuild the resource context; old slot bindings
    // cannot survive an unrelated generation.
    return {
        false, false,
        visual_error(
            "local asset context belongs to a different capture generation")};
  }
  if (!client::valid_runtime_observation(observation))
    return {false, false, visual_error("invalid local asset reset observation")};
  const auto previous_generation = generation_;
  const auto previous_revision = frame_revision_;
  const auto previous_camera = camera_selected_;
  generation_ = observation.generation;
  frame_revision_ = 0;
  camera_selected_ = false;

  auto candidate = target;
  candidate.set_world_scene(world_scene_);
  auto result = project_entities(observation, candidate);
  if (result) {
    viewmodel_pose_intent_.reset();
    viewmodel_pose_.reset();
    viewmodel_attachment_zero_.reset();
    impact_brush_scene_.reset();impact_brush_generation_=impact_brush_revision_=0;
    entity_follow_.reset(observation.generation);entity_follow_.observe(observation);
    target = std::move(candidate);
    previous_entities_.reset();clock_record_=0U; light_cache_.clear(); bone_merge_cache_.reset();
    ordinary_presentation_.reset();ordinary_presentation_.observe(current_entities_);
    ordinary_animation_.reset();remote_support_contexts_.clear();sampled_brush_scene_.reset();
    player_presentation_.reset();player_presentation_.observe(current_entities_);
    sampled_player_anchors_.clear();sampled_player_previous_.reset();sampled_player_current_.reset();
    sampled_motion_exceptions_.clear();
    remote_movement_.clear();remote_movement_valid_=false;
    beam_endpoint_bindings_.clear();beam_glow_profiles_.clear();beam_light_profiles_.clear();
    beam_light_count_=0;beam_light_frame_.reset();unavailable_effect_sources_.clear();placement_entities_.reset();
    remote_weapon_pose_count_=0;
    published_appearances_.clear();corpse_appearances_.clear();
    for(auto& [entity,lifetime]:player_lifetimes_) { (void)entity; lifetime.identity=++next_player_lifetime_; lifetime.continuity_floor=0U; }
  }
  else {
    generation_ = previous_generation;
    frame_revision_ = previous_revision;
    camera_selected_ = previous_camera;
  }
  return result;
}

void RuntimeReplayLocalAssets::player_slot_boundary(std::uint32_t entity) noexcept {
  published_appearances_.erase(entity);
  if (const auto found=player_lifetimes_.find(entity);found!=player_lifetimes_.end()) {
    found->second.identity=++next_player_lifetime_;
    found->second.continuity_floor=current_entities_ && current_entities_->entity_metadata.source ?
        current_entities_->entity_metadata.source->record_ordinal : 0U;
  }
  light_cache_.erase(entity);
  remote_weapon_pose_count_=0;
}

void RuntimeReplayLocalAssets::invalidate_player_continuity() noexcept {
  remote_movement_.clear();remote_movement_valid_=false;
  sampled_motion_exceptions_.clear();
  for (auto& [entity,lifetime]:player_lifetimes_) {
    (void)entity;
    lifetime.identity=++next_player_lifetime_;
    lifetime.continuity_floor=current_entities_ && current_entities_->entity_metadata.source ?
        current_entities_->entity_metadata.source->record_ordinal : 0U;
  }
  previous_entities_.reset();
  player_presentation_.reset();player_presentation_.observe(current_entities_);
  clock_record_=0U;
  light_cache_.clear();
  remote_weapon_pose_count_=0;
}

RuntimeReplayVisualProjectionResult RuntimeReplayLocalAssets::present_entities(
    game_api::GameClientHost& game, double application_seconds,
    std::optional<std::uint32_t> receiving_entity, client::ClientWorldState& target,
    renderer::RenderExtent extent, bool capture_remote_diagnostics,
    std::shared_ptr<const goldsrc::collision::BrushCollisionScene> rendered_brush_scene) {
  remote_movement_.clear();remote_movement_valid_=false;
  sampled_motion_exceptions_.clear();
  // Resolve game policy only through approved ready sprite slots. Retain the
  // owning numeric profile, never the module or borrowed resource name.
  if(current_entities_)for(const auto& entity:current_entities_->packet_entities)if(entity.custom_beam) {
    const auto slot=entity.custom_beam->sprite_model_index;
    if(beam_glow_profiles_.contains(slot))continue;
    const auto named=summary_->model_names.find(slot);
    const auto bound=bindings_.find(slot);
    if(named!=summary_->model_names.end() && bound!=bindings_.end() &&
        bound->second.status==ReplayLocalVisualStatus::ready_sprite) {
      beam_glow_profiles_.emplace(slot,game.beam_ribbon_glow_profile(named->second));
      beam_light_profiles_.emplace(slot,std::array{
          game.beam_scene_light_profile(named->second,false),game.beam_scene_light_profile(named->second,true)});
    }
  }
  const auto policy=game.remote_player_policy();
  if (!policy.enabled || !current_entities_ || !current_entities_->server_time_seconds)
    return {true,false,{}};
  if(!std::isfinite(policy.interpolation_delay_seconds) || policy.interpolation_delay_seconds<0.0 ||
      policy.interpolation_delay_seconds>policy.maximum_observation_gap_seconds ||
      !std::isfinite(policy.maximum_observation_gap_seconds) || policy.maximum_observation_gap_seconds<=0.0 ||
      !std::isfinite(policy.teleport_distance_units) || policy.teleport_distance_units<=0.0 ||
      policy.maximum_players==0U || policy.maximum_players>game_api::kMaximumRemotePlayers)
    return {false,false,visual_error("invalid selected remote player policy")};
  if (!std::isfinite(application_seconds))
    return {false,false,visual_error("invalid explicit application presentation clock")};
  if(clock_record_!=current_entities_->publication_revision) {
    clock_record_=current_entities_->publication_revision;clock_anchor_seconds_=application_seconds;
  }
  const auto& previous=previous_entities_ ? *previous_entities_ : *current_entities_;
  g::RuntimeEntityInterpolationOptions options;
  options.limits.maximum_snapshot_gap_seconds=policy.maximum_observation_gap_seconds;
  options.teleport_distance=policy.teleport_distance_units;
  options.no_interpolation_effect_mask=policy.no_interpolation_effect_mask;
  std::array<std::uint32_t,game_api::kMaximumRemotePlayers> discontinuous{};
  std::size_t discontinuous_count{};
  const auto previous_record=previous.entity_metadata.source ? previous.entity_metadata.source->record_ordinal : 0U;
  for(const auto& [entity,lifetime]:player_lifetimes_) {
    const auto* now=find_sorted_packet_entity(current_entities_->packet_entities,entity);
    const auto* before=find_sorted_packet_entity(previous.packet_entities,entity);
    const bool game_boundary=now && game.world_entity({*now,before,
        current_entities_->packet_entities,maximum_clients_}).discontinuity;
    if(((lifetime.continuity_floor && previous_record<=lifetime.continuity_floor) || game_boundary) && discontinuous_count<discontinuous.size())
      discontinuous[discontinuous_count++]=entity;
  }
  options.discontinuous_entities=std::span{discontinuous}.first(discontinuous_count);
  std::vector<std::uint32_t> players;
  std::vector<g::RuntimePlayerContinuityBoundary> boundaries;
  for(const auto number:player_entity_candidates_) {
    if(number==receiving_entity)continue;
    if(players.size()>=policy.maximum_players)break;
    const auto* candidate=find_sorted_packet_entity(current_entities_->packet_entities,number);
    if(!candidate)continue; // Candidate and source are committed together.
    const auto& entity=*candidate;
    players.push_back(entity.entity_number);
    std::size_t floor{};
    const client::RuntimePacketEntityObservation* before=nullptr;
    std::optional<double> before_seconds;
    for(const auto& source:player_presentation_.observations()) {
      const auto* row=find_sorted_packet_entity(source->packet_entities,entity.entity_number);
      if(row) {
        const auto approved=studio_model_data(row->model_index.value_or(0U));
        const auto intent=game.world_entity({*row,before,source->packet_entities,maximum_clients_,
            approved.get(),before_seconds,source->server_time_seconds});
        if(intent.discontinuity)
          floor=source->entity_metadata.source->record_ordinal;
        if(intent.animation_only_effect_mask && sampled_motion_exceptions_.size()<
            g::RuntimePlayerSnapshotPresentation::maximum_history*g::RuntimePlayerSnapshotPresentation::maximum_players)
          sampled_motion_exceptions_.push_back({entity.entity_number,source->entity_metadata.source->record_ordinal,
              intent.animation_only_effect_mask});
        before=row;
        before_seconds=source->server_time_seconds;
      } else {before=nullptr;before_seconds.reset();}
    }
    if(floor)boundaries.push_back({entity.entity_number,floor});
  }
  options.animation_only_effect_exceptions=sampled_motion_exceptions_;
  auto buffered=player_presentation_.sample(*current_entities_,application_seconds,options,
      policy.interpolation_delay_seconds,players,boundaries);
  if(!buffered)return {false,false,visual_error(buffered.presentation.error->context)};
  auto& sampled=buffered.presentation;
  options.ordinary_entities=ordinary_entity_candidates_;
  // Kinetic models can cover 200 units per 100 ms authoritative interval.
  // Do not misclassify them with a player teleport threshold. Keep a separate
  // finite displacement guard, the same bounded gap, and no extrapolation.
  options.ordinary_teleport_distance=g::RuntimeOrdinarySnapshotPresentation::default_continuity_distance_units;
  auto ordinary_sample=ordinary_presentation_.sample(*sampled.state,application_seconds,options,
      std::min(policy.interpolation_delay_seconds,g::RuntimeOrdinarySnapshotPresentation::maximum_delay_seconds));
  if (!ordinary_sample) return {false,false,visual_error(ordinary_sample.error->context)};
  sampled_game_=&game; sampled_receiving_entity_=receiving_entity; sampled_extent_=extent;
  sampled_application_seconds_=application_seconds;
  sampled_server_seconds_=sampled.sample_seconds;
  sampled_player_previous_=std::move(buffered.previous);sampled_player_current_=std::move(buffered.current);
  sampled_player_anchors_=std::move(buffered.players);
  sampled_previous_seconds_=sampled_player_previous_->server_time_seconds.value_or(sampled.sample_seconds);
  sampled_current_seconds_=*sampled_player_current_->server_time_seconds; sampled_alpha_=sampled.alpha;
  sampled_selection_status_=sampled.selection_status;
  if(sampled.selection_status==g::EntitySnapshotPairSelectionStatus::held_only) {
    sampled_previous_seconds_=sampled_current_seconds_; sampled_alpha_=0.0;
  }
  sampled_brush_scene_=rendered_brush_scene ? std::move(rendered_brush_scene) : impact_brush_scene();
  if(sampled_brush_scene_ && (remote_support_contexts_.empty() ||
      remote_support_contexts_.back().revision!=current_entities_->publication_revision ||
      !remote_support_contexts_.back().scene ||
      remote_support_contexts_.back().scene->model_library()!=sampled_brush_scene_->model_library())) {
    std::vector<g::ReferenceBrushCollisionContext> contexts;
    contexts.reserve(g::RuntimePlayerSnapshotPresentation::maximum_history+1U);
    const auto append=[&](const std::shared_ptr<const client::RuntimeClientObservationState>& source) {
      if(!source || (!contexts.empty() && contexts.back().revision==source->publication_revision))return;
      const auto cached=std::ranges::find(remote_support_contexts_,source->publication_revision,
          &g::ReferenceBrushCollisionContext::revision);
      if(cached!=remote_support_contexts_.end() && cached->generation==generation_ && cached->scene &&
          cached->scene->model_library()==sampled_brush_scene_->model_library())contexts.push_back(std::move(*cached));
      else contexts.push_back(g::build_reference_brush_collision(sampled_brush_scene_->model_library(),
          summary_->model_names,*source,generation_));
    };
    for(const auto& source:player_presentation_.observations())append(source);
    append(current_entities_);
    remote_support_contexts_=std::move(contexts);
  }
  auto result=project_entities(*ordinary_sample.state,target);
  if(result && capture_remote_diagnostics)capture_remote_movement(*ordinary_sample.state,target);
  sampled_game_=nullptr; sampled_extent_.reset(); sampled_receiving_entity_.reset();
  sampled_player_anchors_.clear();sampled_player_previous_.reset();sampled_player_current_.reset();
  sampled_brush_scene_.reset();
  return result;
}

void RuntimeReplayLocalAssets::capture_remote_movement(
    const client::RuntimeClientObservationState& sampled,
    const client::ClientWorldState& target) noexcept {
  // Best-effort capture cannot fail a presentation or allocate every frame.
  // The bounded vector is on the existing heap asset owner, not the Win32
  // main-loop stack. An RX coordinate is not proof of a submitted transform.
  try {
    remote_movement_.reserve(game_api::kMaximumRemotePlayers);
    const auto actor=[](const client::RuntimeClientObservationState* source,std::uint32_t number)
        -> const client::RuntimePacketEntityObservation* {
      if(!source)return nullptr;
      return find_sorted_packet_entity(source->packet_entities,number);
    };
    const auto origin=[](const client::RuntimePacketEntityObservation* row)
        -> std::optional<std::array<double,3>> {
      if(!row || !row->origin.complete())return {};
      return std::array<double,3>{*row->origin.x,*row->origin.y,*row->origin.z};
    };
    const auto source=[&](std::size_t ordinal) -> const client::RuntimeClientObservationState* {
      for(const auto& observation:player_presentation_.observations())
        if(observation->entity_metadata.source && observation->entity_metadata.source->record_ordinal==ordinal)
          return observation.get();
      return nullptr;
    };
    for(const auto& anchor:sampled_player_anchors_) {
      if(remote_movement_.size()==game_api::kMaximumRemotePlayers)break;
      RemoteMovementObservation row;
      row.entity_index=anchor.entity;row.presentation=anchor.diagnostics;
      if(const auto life=player_lifetimes_.find(anchor.entity);life!=player_lifetimes_.end())
        row.lifetime=life->second.identity;
      const auto* previous=source(anchor.diagnostics.position_previous_record_ordinal);
      const auto* current=source(anchor.diagnostics.position_current_record_ordinal);
      const auto* latest=actor(current_entities_.get(),anchor.entity);
      const auto* selected=actor(&sampled,anchor.entity);
      row.previous_raw_origin=origin(actor(previous,anchor.entity));
      row.current_raw_origin=origin(actor(current,anchor.entity));
      row.latest_raw_origin=origin(latest);row.sampled_origin=origin(selected);
      if(previous && previous->entity_metadata.source) {
        row.previous_source_ordinal=previous->entity_metadata.source->record_ordinal;
        row.previous_server_s=previous->server_time_seconds;
      }
      if(current && current->entity_metadata.source) {
        row.current_source_ordinal=current->entity_metadata.source->record_ordinal;
        row.current_server_s=current->server_time_seconds;
      }
      if(current_entities_->entity_metadata.source)
        row.latest_source_ordinal=current_entities_->entity_metadata.source->record_ordinal;
      row.latest_server_s=current_entities_->server_time_seconds;row.sample_server_s=anchor.sample_seconds;
      if(latest) {
        row.model_index=latest->model_index.value_or(0U);
        row.raw_main=latest->sequence;row.raw_gait=latest->gait_sequence;
        row.raw_weapon=latest->weapon_model_index;row.raw_effects=latest->effects;
        row.raw_render_mode=latest->render_mode;row.raw_render_amount=latest->render_amount;
        row.raw_render_effect=latest->render_effect;
      }
      if(selected) {
        row.selected_main=selected->sequence;row.selected_gait=selected->gait_sequence;
        row.selected_weapon=selected->weapon_model_index;
      }
      for(std::size_t i=0U;i<summary_->remote_player_count;++i) {
        const auto& diagnostic=summary_->remote_players[i];
        if(diagnostic.entity!=anchor.entity)continue;
        row.support_entity=diagnostic.support_entity;row.support_model=diagnostic.support_model;
        row.support_offset_z=diagnostic.support_offset_z;break;
      }
      row.pose_submitted=false;row.visible=false;
      if(const auto& frame=target.entity_frame();frame) {
        for(const auto& instance:frame->studio_instances()) {
          if(instance.entity_number!=anchor.entity || instance.subinstance_ordinal!=0U)continue;
          row.pose_submitted=true;
          row.visible=instance.visibility_status==r::RuntimeEntityVisibilityStatus::visible;
          row.rendered_opacity=instance.opacity;
          const auto& position=instance.transform.origin;
          row.rendered_origin=std::array<double,3>{position.x,position.y,position.z};
          break;
        }
      }
      remote_movement_.push_back(row);
    }
    remote_movement_valid_=true;
  } catch(...) {
    remote_movement_.clear();remote_movement_valid_=false;
  }
}

std::optional<std::size_t> RuntimeReplayLocalAssets::resolve_player_appearance(
    game_api::PlayerAppearanceSelection& selection,const assets::ModelAsset& fallback,
    r::EntityRenderResourceIdentity base_identity,assets::AssetSourceFingerprint fingerprint,
    const client::RuntimePacketEntityObservation& entity) {
  using Status=game_api::PlayerAppearanceStatus;
  if(!sampled_game_ || !package_ || !fallback.skeletal_data)return {};
  if(selection.model_name.empty() && !selection.fields.top_color && !selection.fields.bottom_color)return {};
  std::shared_ptr<const assets::ModelAsset> selected;
  const assets::ModelAsset* source=&fallback;
  if(!selection.model_name.empty()) {
    const auto prepared=player_model_assets_ ? player_model_assets_->request(selection.model_name) : g::PreparedPlayerModel{};
    selection.status=prepared.status;
    if(!prepared.model)return {};
    selected=prepared.model;fingerprint=prepared.revision;
    source=selected.get();
    const auto& data=*selected->skeletal_data;
    if(!sampled_game_->player_model_compatible(data) || entity.sequence.value_or(0U)>=data.sequences.size() ||
        entity.gait_sequence.value_or(0U)>=data.sequences.size() || entity.skin.value_or(0)<0 ||
        static_cast<std::size_t>(entity.skin.value_or(0))>=data.skin_families.size()) {
      selection.status=Status::pose_incompatible;return {};
    }
  }
  // Borrow the already owned immutable base through the cache lookup. Avoid
  // allocating/copying its wrapper and source-name string on every color-only
  // hit (or empty palette); Studio bundles already share their skeletal data.
  // A palette miss acquires its own owner below; no borrowed pointer escapes.
  const auto ranges=sampled_game_->player_palette(selection.fields,*source->skeletal_data);
  if(selection.model_name.empty() && ranges.empty())return {};
  std::string key=selection.model_name+":"+std::to_string(base_identity.resource_id)+":"+
      std::to_string(fingerprint.primary)+":"+std::to_string(fingerprint.secondary);
  for(const auto& range:ranges)key+=":"+std::to_string(range.texture)+","+std::to_string(range.first)+","+
      std::to_string(range.last)+","+std::to_string(range.hue_degrees);
  for(std::size_t i=0;i<appearance_variants_.size();++i)if(appearance_variants_[i].key==key)return i;
  if(appearance_variants_.size()>=64U) {selection.status=Status::resource_limit;return {};}
  // Every remap owns its cloned animation graph as well as its pixels. Count
  // all nested track/run vectors before copying; GPU source bytes are added below.
  const auto model_bytes=g::retained_player_model_bytes(*source);
  if(model_bytes>128U*1024U*1024U-appearance_variant_bytes_) {selection.status=Status::resource_limit;return {};}
  if(!ranges.empty())selected=g::remap_player_model_palette(*source,ranges);
  if(!selected){selection.status=Status::asset_import_failed;return {};}
  const std::uint64_t id=0xAD31000000000000ULL+appearance_variants_.size()+1U;
  const r::EntityRenderResourceIdentity identity{id,fingerprint.primary ? fingerprint.primary : fingerprint.secondary};
  auto render=r::StudioModelRenderAssetBuilder{}.build(*selected,identity);
  if(!render){selection.status=Status::asset_import_failed;return {};}
  const auto bytes=render.asset->statistics().total_gpu_source_bytes+model_bytes;
  if(bytes>128U*1024U*1024U-appearance_variant_bytes_){selection.status=Status::resource_limit;return {};}
  r::EntitySceneRenderPackageCreateInfo package;
  package.asset_library=package_->asset_library();package.asset_library_identity=package_->asset_library_identity();
  package.resource_id=package_->resource_id();package.world_scene_association=package_->world_scene_association();
  package.studio_assets.assign(package_->studio_assets().begin(),package_->studio_assets().end());
  package.sprite_assets.assign(package_->sprite_assets().begin(),package_->sprite_assets().end());
  package.presentation_studio_sources.assign(package_->presentation_studio_sources().begin(),package_->presentation_studio_sources().end());
  package.presentation_sprite_sources.assign(package_->presentation_sprite_sources().begin(),package_->presentation_sprite_sources().end());
  const auto render_index=package.studio_assets.size();
  package.studio_assets.push_back(std::make_shared<const r::StudioModelRenderAsset>(std::move(*render.asset)));
  package.presentation_studio_sources.push_back({identity,selected});
  auto built=r::EntitySceneRenderPackageBuilder{}.build(std::move(package));
  if(!built){selection.status=Status::resource_limit;return {};}
  auto next_package=std::make_shared<const r::EntitySceneRenderPackage>(std::move(*built.package));
  appearance_variants_.push_back({std::move(key),selection.model_name,selected,fingerprint,identity,render_index,!ranges.empty()});
  package_=std::move(next_package);appearance_variant_bytes_+=bytes;
  return appearance_variants_.size()-1U;
}

std::optional<std::array<float,3>> RuntimeReplayLocalAssets::studio_light(
    std::uint32_t key, assets::AssetVector3 origin) {
  if (!world_ || !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) return {};
  auto cached=light_cache_.find(key);
  if (cached==light_cache_.end() || std::abs(cached->second.first.x-origin.x)>8.0F ||
      std::abs(cached->second.first.y-origin.y)>8.0F || std::abs(cached->second.first.z-origin.z)>8.0F) {
    cached=light_cache_.insert_or_assign(key,std::pair{origin,static_world_light(*world_,origin)}).first;
  }
  return cached->second.second;
}

void RuntimeReplayLocalAssets::set_diagnostic_sink(
    core::MultiplayerDiagnosticSink* sink, std::uint64_t map_generation) noexcept {
  if(diagnostic_sink_!=sink || diagnostic_map_generation_!=map_generation) {
    ordinary_diagnostic_samples_={};ordinary_diagnostic_next_=0U;ordinary_diagnostic_evictions_=0U;
  }
  diagnostic_sink_=sink;diagnostic_map_generation_=map_generation;
}

void RuntimeReplayLocalAssets::observe_ordinary_studio(std::uint32_t entity,
    std::uint64_t model_resource, const client::RuntimePacketEntityObservation* raw,
    const client::RuntimeClientObservationState* source,
    const goldsrc::studio::StudioPoseInput& pose, assets::AssetVector3 origin,
    assets::AssetVector3 angles, bool cache_hit, std::optional<double> sample_seconds,
    std::optional<double> previous_source_seconds) noexcept {
  if(!diagnostic_sink_ || !diagnostic_sink_->enabled())return;
  const auto now=core::multiplayer_diagnostic_now_ns();
  const auto source_ordinal=source && source->entity_metadata.source ?
      source->entity_metadata.source->record_ordinal : 0U;
  const auto lifetime=entity_follow_.lifetime(entity,source_ordinal);
  auto found=std::ranges::find_if(ordinary_diagnostic_samples_,[&](const auto& value) {
    return value.used && value.entity==entity && value.generation==generation_ &&
        value.lifetime==lifetime && value.model_resource==model_resource;
  });
  const auto raw_sequence=raw ? raw->sequence : std::optional<std::uint32_t>{};
  if(found!=ordinary_diagnostic_samples_.end() && found->raw_sequence==raw_sequence &&
      found->selected_sequence==pose.sequence_index && found->cache_hit==cache_hit &&
      now>=found->observed_ns && now-found->observed_ns<125'000'000U) {
    if(found->suppressed!=std::numeric_limits<std::uint32_t>::max())++found->suppressed;
    return;
  }
  if(found==ordinary_diagnostic_samples_.end()) {
    found=std::ranges::find(ordinary_diagnostic_samples_,false,&OrdinaryDiagnosticSample::used);
    if(found==ordinary_diagnostic_samples_.end()) {
      found=ordinary_diagnostic_samples_.begin()+(ordinary_diagnostic_next_++%ordinary_diagnostic_samples_.size());
      if(ordinary_diagnostic_evictions_!=std::numeric_limits<std::uint32_t>::max())++ordinary_diagnostic_evictions_;
    }
    *found={};
  }
  core::MultiplayerDiagnosticEvent event;
  event.identity.producer=core::MultiplayerDiagnosticProducer::ordinary_studio;
  event.identity.producer_event_id=++ordinary_diagnostic_serial_;
  event.identity.network_generation=source ? source->generation : generation_;
  event.identity.map_generation=diagnostic_map_generation_;
  event.identity.actor_entity=entity;
  if(lifetime)event.identity.actor_visual_lifetime=lifetime;
  if(source && source->entity_metadata.source) {
    const auto& owner=*source->entity_metadata.source;
    event.identity.source_record=owner.record_identity;
    event.identity.source_record_ordinal=owner.record_ordinal;
    event.identity.source_sequence=owner.source_transport_sequence;
  }
  event.local_steady_ns=now;
  if(source)event.server_seconds=source->server_time_seconds;
  if(event.server_seconds)event.server_time_domain=core::MultiplayerTimeDomain::server_simulation_seconds;
  event.provenance=core::MultiplayerProvenance::derived_estimate;
  event.knowledge=core::MultiplayerKnowledge::cosmetic_feedback;
  event.edge=core::MultiplayerEdgeConfidence::exact;
  core::MultiplayerPresentationPayload payload;
  payload.phase=core::MultiplayerPresentationPhase::pose;
  payload.current_source_seconds=event.server_seconds;
  payload.previous_source_seconds=previous_source_seconds;
  payload.sample_seconds=payload.pose_seconds=sample_seconds;
  payload.sequence=pose.sequence_index;
  payload.animation_frame=static_cast<float>(pose.frame_coordinate);
  payload.cache_hit=cache_hit;
  payload.origin=std::array{origin.x,origin.y,origin.z};
  payload.angles=std::array{angles.x,angles.y,angles.z};
  payload.suppressed_repetitions=found->suppressed;
  payload.diagnostic_cache_evictions=ordinary_diagnostic_evictions_;
  if(raw) {
    payload.raw_animation_sequence=raw->sequence;
    payload.raw_animation_frame=raw->frame;
    payload.raw_animtime_seconds=raw->animation_time_seconds;
    payload.raw_framerate=raw->frame_rate;
    const auto observed_bytes=[]<std::size_t N>(const std::array<std::optional<std::uint32_t>,N>& values)
        ->std::optional<std::array<std::uint8_t,N>> {
      std::array<std::uint8_t,N> result{};
      for(std::size_t i=0;i<N;++i) {
        if(!values[i] || *values[i]>255U)return {};
        result[i]=static_cast<std::uint8_t>(*values[i]);
      }
      return result;
    };
    payload.raw_controllers=observed_bytes(raw->controllers);
    payload.raw_blending=observed_bytes(raw->blending);
  }
  event.payload=payload;
  // This is a pose-evaluation receipt, not a successful scene/swap receipt.
  // No renderer visibility, model revision or server outcome is inferred.
  (void)diagnostic_sink_->observe(event);
  *found={true,cache_hit,entity,pose.sequence_index,raw_sequence,generation_,lifetime,
      model_resource,now,0U};
}

RuntimeReplayLocalAssets::AuthoritativeObservationStage
RuntimeReplayLocalAssets::stage_authoritative_observation(
    const client::RuntimeClientObservationState& observation) {
  AuthoritativeObservationStage stage;
  stage.retain=observation.entity_metadata.source &&
      (!current_entities_ ||
       current_entities_->entity_metadata.source!=observation.entity_metadata.source);
  stage.lifetime_identity=next_player_lifetime_;
  if(stage.retain) {
    stage.retained=std::make_shared<const client::RuntimeClientObservationState>(
        observation);
    stage.ordinary=ordinary_presentation_;
    stage.ordinary->observe(stage.retained);
    stage.players=player_presentation_;
    stage.players->observe(stage.retained);
    stage.ordinary_entity_candidates.reserve(observation.packet_entities.size());
    stage.player_entity_candidates.reserve(std::min<std::size_t>(
        maximum_clients_,observation.packet_entities.size()));
    for(const auto& value:observation.packet_entities) {
      if(value.player_movement_schema && value.entity_number<=maximum_clients_) {
        // Keep every source candidate, including the local player. The
        // receiving slot and mutable module policy are evaluated each render.
        stage.player_entity_candidates.push_back(value.entity_number);
        const auto old=player_lifetimes_.find(value.entity_number);
        stage.lifetimes.emplace(value.entity_number,
            old!=player_lifetimes_.end() && old->second.model==value.model_index
                ? old->second
                : PlayerLifetime{++stage.lifetime_identity,value.model_index});
      }
      // Ready kinetic Studio slots and supported server-controlled glow
      // sprites opt in. MOVETYPE_NONE can still move a glow each snapshot.
      // Other static assets stay discrete; brush carry has its own owner.
      if(value.player_movement_schema || !value.model_index)continue;
      const auto binding=bindings_.find(*value.model_index);
      if(binding==bindings_.end())continue;
      const bool kinetic_studio=binding->second.status==ReplayLocalVisualStatus::ready_studio &&
          value.brush_move_type && (*value.brush_move_type==5U || *value.brush_move_type==6U ||
          *value.brush_move_type==9U || *value.brush_move_type==10U || *value.brush_move_type==11U);
      const bool glow_sprite=binding->second.status==ReplayLocalVisualStatus::ready_sprite &&
          value.render_mode==3U && (value.render_effect.value_or(0U)==0U || value.render_effect==14U);
      if(kinetic_studio || glow_sprite)
        stage.ordinary_entity_candidates.push_back(value.entity_number);
    }
  }
  // Keep the complete trail owner off the Win32 stack.  All allocation and
  // observation work finishes before either this state or a render target is
  // committed.
  stage.follow=std::make_unique<EntityFollowPresentation>(entity_follow_);
  stage.follow->observe(observation);
  stage.effect_entities=observation.packet_entities;
  stage.decal_lifetimes.reserve(observation.packet_entities.size());
  for(const auto& entity:observation.packet_entities) {
    const auto id=stage.follow->lifetime(entity.entity_number);
    if(!id) continue;
    const auto bound=entity.model_index ? bindings_.find(*entity.model_index)
                                        : bindings_.end();
    stage.decal_lifetimes.push_back({entity.entity_number,id,
        entity.model_index.value_or(0U),
        bound!=bindings_.end() ? bound->second.brush_model.value_or(0U) : 0U});
  }
  return stage;
}

void RuntimeReplayLocalAssets::commit_authoritative_observation(
    AuthoritativeObservationStage&& stage) noexcept {
  if(stage.retain) {
    previous_entities_=std::move(current_entities_);
    current_entities_=std::move(stage.retained);
    ordinary_entity_candidates_=std::move(stage.ordinary_entity_candidates);
    player_entity_candidates_=std::move(stage.player_entity_candidates);
    ordinary_animation_.retain_entities(current_entities_->packet_entities);
    // A committed removal is a lifetime boundary even if no render frame
    // samples that gap before this slot is reused by the next RX record.
    std::erase_if(beam_endpoint_bindings_,[&](const auto& entry) {
      const auto* entity=find_sorted_packet_entity(current_entities_->packet_entities,entry.first);
      return !entity || !entity->custom_beam;
    });
    ordinary_presentation_=std::move(*stage.ordinary);
    player_presentation_=std::move(*stage.players);
    next_player_lifetime_=stage.lifetime_identity;
    player_lifetimes_=std::move(stage.lifetimes);
    // A removal is an authoritative corpse lifetime boundary even when the
    // application skips the discrete render frame. Do not let a later reuse
    // of the same entity number inherit the removed body's frozen appearance.
    std::erase_if(corpse_appearances_,[&](const auto& item) {
      return !find_sorted_packet_entity(current_entities_->packet_entities,item.first);
    });
    std::erase_if(light_cache_,[&](const auto& value) {
      if(value.first==0U || value.first>=0xe0000000U) return false;
      return !find_sorted_packet_entity(current_entities_->packet_entities,value.first);
    });
  }
  server_decal_lifetimes_=std::move(stage.decal_lifetimes);
  entity_follow_=std::move(*stage.follow);
  presented_effect_entities_=std::move(stage.effect_entities);
  unavailable_effect_sources_.clear();
}

RuntimeReplayVisualProjectionResult RuntimeReplayLocalAssets::observe_entities(
    const client::RuntimeClientObservationState& observation) {
  if(!client::valid_runtime_observation(observation) ||
      observation.generation!=generation_)
    return {false,false,
        visual_error("invalid local asset observation generation")};
  try {
    auto staged=stage_authoritative_observation(observation);
    commit_authoritative_observation(std::move(staged));
    return {true,false,{}};
  } catch(const std::exception& error) {
    return {false,false,visual_error(error.what())};
  }
}

RuntimeReplayVisualProjectionResult RuntimeReplayLocalAssets::project_entities(
    const client::RuntimeClientObservationState &observation,
    client::ClientWorldState &target) {
  if (!client::valid_runtime_observation(observation) ||
      observation.generation != generation_) {
    return {false, false,
            visual_error("invalid local asset observation generation")};
  }
  try {
    r::EntityRenderFrameBuildInput input;
    input.resource_id = frame_id;
    input.resource_revision = frame_revision_ + 1U;
    const double time = observation.server_time_seconds.value_or(0.0);
    input.interpolation = {time,
                           time,
                           time,
                           0.0F,
                           observation.canonical_state_hash,
                           observation.canonical_state_hash,
                           r::EntityRenderInterpolationProfile::
                               decoded_discrete_runtime_replay_v1};
    std::optional<world_visibility::WorldViewFrustum> frustum;
    if (sampled_game_) {
      input.interpolation={sampled_server_seconds_,sampled_previous_seconds_,
          sampled_current_seconds_,static_cast<float>(sampled_alpha_),
          sampled_previous_seconds_!=sampled_current_seconds_ && sampled_player_previous_ ?
              (sampled_player_previous_->entity_metadata.source ? sampled_player_previous_->entity_metadata.source->record_identity :
                  client::runtime_observation_visual_hash_v3(*sampled_player_previous_)) :
              (sampled_player_current_->entity_metadata.source ? sampled_player_current_->entity_metadata.source->record_identity :
                  client::runtime_observation_visual_hash_v3(*sampled_player_current_)),
          sampled_player_current_->entity_metadata.source ? sampled_player_current_->entity_metadata.source->record_identity :
              client::runtime_observation_visual_hash_v3(*sampled_player_current_),
          r::EntityRenderInterpolationProfile::public_runtime_server_seconds_v1};
      const auto camera=client::build_render_scene(target).camera;
      const auto leaf=world_spatial::WorldSpatialQuery::locate_point(
          world_scene_->spatial_package(),camera.position);
      // Match the existing world-camera fallback: unavailable/solid camera PVS
      // cannot become a fabricated row or a fatal entity-publication error.
      // Frustum/depth still apply; exact usable camera PVS remains preferred.
      if (leaf && !leaf.result->solid_or_special && leaf.result->pvs_available) {
        input.spatial_package=&world_scene_->spatial_package();
        input.camera_leaf_index=leaf.result->leaf_index;
      }
      if (sampled_extent_) {
        auto built=world_visibility::WorldViewFrustum::from_camera(camera,*sampled_extent_);
        if (built) { frustum=std::move(built.frustum); input.view_frustum=&*frustum; }
      }
    }
    auto& next = *staged_summary_;
    // The public summary is committed only after the render target succeeds.
    // Its resource metadata is identical in both buffers. Remote entries
    // overwritten by this frame need no deep copy; preserve only the unused
    // tail at the publication boundary for exact historical summary output.
    next.remote_previous_seconds = summary_->remote_previous_seconds;
    next.remote_current_seconds = summary_->remote_current_seconds;
    next.remote_sample_seconds = summary_->remote_sample_seconds;
    next.remote_alpha = summary_->remote_alpha;
    next.remote_snapshot_mode = summary_->remote_snapshot_mode;
    next.projected_frames = summary_->projected_frames;
    next.unchanged_records = summary_->unchanged_records;
    next.brush_transform_changes = summary_->brush_transform_changes;
    next.last_changed_brush_entity = summary_->last_changed_brush_entity;
    next.last_changed_brush_model = summary_->last_changed_brush_model;
    constexpr auto status_count =
        static_cast<std::size_t>(ReplayLocalVisualStatus::import_failed) + 1U;
    std::array<std::size_t, status_count> coverage_counts{};
    std::vector<std::pair<std::uint32_t, std::size_t>> model_slot_counts;
    model_slot_counts.reserve(bindings_.size());
    for (const auto& [slot, binding] : bindings_) {
      (void)binding;
      model_slot_counts.emplace_back(slot, 0U);
    }
    const auto add_coverage = [&](ReplayLocalVisualStatus status) {
      ++coverage_counts[static_cast<std::size_t>(status)];
    };
    const auto add_rendered_slot = [&](std::uint32_t slot) {
      const auto found = std::lower_bound(model_slot_counts.begin(), model_slot_counts.end(), slot,
          [](const auto& item, std::uint32_t value) { return item.first < value; });
      if (found != model_slot_counts.end() && found->first == slot)
        ++found->second;
      else
        ++next.rendered_model_slots[slot]; // Preserve unbound diagnostic slots.
    };
    next.ordinary_pose_evaluations=next.ordinary_pose_reuses=0U;
    std::vector<OrdinaryFramePose> ordinary_poses;
    std::size_t ordinary_pose_bytes{};
    std::array<game_api::RemoteWeaponPoseSample,32> next_weapon_poses{};
    std::size_t next_weapon_pose_count=0;
    std::map<std::uint32_t,std::array<std::optional<assets::AssetVector3>,4>> next_attachments;
    next.remote_player_count=0U;
    if(sampled_game_) {
      next.remote_previous_seconds=sampled_previous_seconds_; next.remote_current_seconds=sampled_current_seconds_;
      next.remote_sample_seconds=sampled_server_seconds_; next.remote_alpha=sampled_alpha_;
      next.remote_snapshot_mode=sampled_selection_status_;
    }
    else next.remote_snapshot_mode.reset();
    next.coverage.clear();
    next.rendered_model_slots.clear();
    next.decoded_entities = observation.packet_entities.size();
    next.resolved_instances = 0;
    next.rendered_instances = 0;
    next.unsupported_instances = 0;
    next.brush_candidates = next.resolved_brushes = next.submitted_brushes = 0U;
    next.hidden_brushes = next.unsupported_brush_materials = 0U;
    next.first_brush_rejection.reset();
    world_scene_render::RuntimeBrushRenderFrame brush_frame;
    brush_frame.scene_identity = world_scene_->resource_identity();
    brush_frame.generation = generation_;
    brush_frame.revision = frame_revision_ + 1U;
    std::optional<client::RenderCameraState> camera;
    const auto unavailable = [&](std::uint32_t number,
                                 ReplayLocalVisualStatus reason) {
      add_coverage(reason);
      ++next.unsupported_instances;
      const auto* entity=find_sorted_packet_entity(observation.packet_entities,number);
      if (!next.first_brush_rejection && entity && entity->model_index) {
        const auto name = next.model_names.find(*entity->model_index);
        const auto binding = bindings_.find(*entity->model_index);
        if (name != next.model_names.end() && name->second.starts_with('*'))
          next.first_brush_rejection = ReplayLocalVisualSummary::Rejection{number, *entity->model_index,
              binding == bindings_.end() ? std::nullopt : binding->second.brush_model,
              reason, observation.publication_revision};
      }
      input.unsupported_instances.push_back(
          {number,
           {},
           r::UnsupportedEntityVisualReason::unsupported_asset_kind,
           r::RuntimeEntityVisibilityStatus::unsupported_visual});
    };
    for (const auto &entity : observation.packet_entities) {
      if(entity.custom_beam)continue; // typed persistent beams execute after owner poses
      if (sampled_game_ && entity.player_movement_schema && sampled_receiving_entity_ &&
          entity.entity_number==*sampled_receiving_entity_) continue;
      if (!entity.ordinary_visual_schema) {
        unavailable(entity.entity_number,
                    ReplayLocalVisualStatus::unsupported_schema);
        continue;
      }
      const client::RuntimePacketEntityObservation* selection_previous=nullptr;
      if(sampled_game_ && previous_entities_) {
        selection_previous=find_sorted_packet_entity(previous_entities_->packet_entities,
            entity.entity_number);
      }
      auto selection=sampled_game_ ? sampled_game_->world_entity(
          {entity,selection_previous,current_entities_->packet_entities,maximum_clients_}) : game_api::WorldEntityPresentationIntent{};
      // A body queue instance freezes the originating appearance. A later
      // player occupant or model change cannot repaint that retained corpse.
      // Compare only the already classified corpse anchor, never infer death.
      const auto same_corpse=[&](const CorpseAppearance& saved) {
        const auto& a=saved.anchor;
        // Body queues retain server MOVETYPE_TOSS/velocity (SDK world.cpp).
        // A changing position is motion, not a new corpse/appearance lifetime.
        return a.entity_number==entity.entity_number && a.render_effect==entity.render_effect &&
            a.render_amount==entity.render_amount && a.model_index==entity.model_index &&
            a.sequence==entity.sequence && a.animation_time_seconds==entity.animation_time_seconds;
      };
      auto corpse=corpse_appearances_.find(entity.entity_number);
      if(corpse!=corpse_appearances_.end() && !same_corpse(corpse->second)) {
        corpse_appearances_.erase(corpse);corpse=corpse_appearances_.end();
      }
      if(corpse!=corpse_appearances_.end())selection=corpse->second.selection;
      const bool proxy=selection.role==game_api::WorldEntityPresentationRole::player_proxy;
      if(selection.role==game_api::WorldEntityPresentationRole::unavailable) {
        if(next.remote_player_count<next.remote_players.size()) {
          auto& diagnostic=next.remote_players[next.remote_player_count++];diagnostic={};
          diagnostic.entity=entity.entity_number;diagnostic.render_effect=entity.render_effect;
          diagnostic.render_amount=entity.render_amount;diagnostic.classification=selection.reason;
          diagnostic.status=game_api::RemotePlayerPresentationStatus::missing_fields;
          diagnostic.network_generation=diagnostic.map_generation=generation_;
        }
        unavailable(entity.entity_number,ReplayLocalVisualStatus::unsupported_pose);continue;
      }
      const auto model_slot=proxy ? selection.model_index : entity.model_index;
      if (!model_slot || *model_slot == 0U) {
        unavailable(entity.entity_number,
                    ReplayLocalVisualStatus::absent_model);
        continue;
      }
      const auto found = bindings_.find(*model_slot);
      if (found == bindings_.end()) {
        unavailable(entity.entity_number,
                    ReplayLocalVisualStatus::unknown_model_slot);
        continue;
      }
      if (proxy && found->second.status != ReplayLocalVisualStatus::ready_studio) {
        unavailable(entity.entity_number, ReplayLocalVisualStatus::unsupported_pose);
        continue;
      }
      const auto &binding = found->second;
      const auto name = next.model_names.find(*entity.model_index);
      if (name != next.model_names.end() && name->second.starts_with('*')) ++next.brush_candidates;
      if (binding.brush_model) {
        ++next.resolved_brushes;
        ++next.resolved_instances;
        if ((entity.effects.value_or(0U) & 128U) != 0U) {
          ++next.hidden_brushes;
          unavailable(entity.entity_number, ReplayLocalVisualStatus::hidden_by_effects);
          continue;
        }
        // GoldSrc normal and trans-alpha are depth-writing textured passes.
        // Trans-alpha uses the texture's cutout; renderamt is not translucency.
        if (entity.render_mode.value_or(0U) != 0U && entity.render_mode != 4U) {
          ++next.unsupported_brush_materials;
          unavailable(entity.entity_number, ReplayLocalVisualStatus::unsupported_render_mode);
          continue;
        }
        if (!entity.origin.complete() || !entity.angles.complete()) {
          unavailable(entity.entity_number, ReplayLocalVisualStatus::incomplete_transform);
          continue;
        }
        const auto transform = g::brush_models::make_brush_submodel_transform(
            {static_cast<float>(*entity.origin.x), static_cast<float>(*entity.origin.y),
             static_cast<float>(*entity.origin.z)},
            {static_cast<float>(*entity.angles.x), static_cast<float>(*entity.angles.y),
             static_cast<float>(*entity.angles.z)}, binding.brush_source_origin);
        if (!transform) {
          unavailable(entity.entity_number, ReplayLocalVisualStatus::unsupported_brush_transform);
          continue;
        }
        const auto models = world_scene_->brush_library().models();
        const auto model = std::ranges::find_if(models, [&](const auto& value) {
          return value.source_model_index() == *binding.brush_model;
        });
        const auto transformed = g::brush_models::transform_brush_bounds(
            model->local_bounds(), *transform.transform);
        if (!transformed) return {false, false, visual_error("invalid brush bounds")};
        brush_frame.instances.push_back({entity.entity_number, *entity.model_index,
            *binding.brush_model, transform.transform->model_matrix, *transformed.bounds,
            entity.frame.value_or(0.0) != 0.0});
        const auto membership = world_spatial::WorldSpatialQuery::collect_intersecting_leaves(
            world_scene_->spatial_package(), *transformed.bounds, {65536U, 256U});
        if (membership)
          brush_frame.instances.back().touched_leaf_indices = membership.result->leaf_indices;
        ++next.submitted_brushes;
        ++next.rendered_instances;
        add_coverage(ReplayLocalVisualStatus::ready_brush);
        add_rendered_slot(*entity.model_index);
        continue;
      }
      if (!binding.library_index || !binding.render_index) {
        unavailable(entity.entity_number, binding.status);
        continue;
      }
      ++next.resolved_instances;
      if (!entity.origin.complete() || !entity.angles.complete()) {
        unavailable(entity.entity_number,
                    ReplayLocalVisualStatus::incomplete_transform);
        continue;
      }
      if (entity.render_mode.value_or(0U) != 0U &&
          !(binding.status==ReplayLocalVisualStatus::ready_studio && entity.render_mode.value_or(0U)<=5U) &&
          !(binding.status==ReplayLocalVisualStatus::ready_sprite && entity.render_mode==3U &&
            (entity.render_effect.value_or(0U)==0U || entity.render_effect==14U))) {
        unavailable(entity.entity_number,
                    ReplayLocalVisualStatus::unsupported_render_mode);
        continue;
      }
      if ((entity.effects.value_or(0U) & 128U) != 0U) {
        unavailable(entity.entity_number,
                    ReplayLocalVisualStatus::hidden_by_effects);
        continue;
      }
      r::EntityRenderTransform transform{{static_cast<float>(*entity.origin.x),
                                          static_cast<float>(*entity.origin.y),
                                          static_cast<float>(*entity.origin.z)},
                                         {static_cast<float>(*entity.angles.z),
                                          -static_cast<float>(*entity.angles.x),
                                          static_cast<float>(*entity.angles.y)},
                                         static_cast<float>(entity.visual_scale && *entity.visual_scale>0 ? *entity.visual_scale : 1.0)};
      if (!r::finite_entity_render_transform(transform)) {
        return {false, false, visual_error("nonfinite local entity transform")};
      }
      std::optional<RemoteSupportPresentation> rider_support;
      if(sampled_game_ && entity.player_movement_schema && sampled_brush_scene_) {
        const auto anchor=std::ranges::find(sampled_player_anchors_,entity.entity_number,
            &g::RuntimePlayerInterpolationAnchor::entity);
        if(anchor!=sampled_player_anchors_.end()) {
          rider_support=sample_remote_vertical_support(*anchor,entity,*current_entities_,
              remote_support_contexts_,sampled_brush_scene_,remote_support_scratch_);
          if(rider_support)transform.origin=rider_support->origin;
        }
      }
      const auto &record = library_->records()[*binding.library_index];
      if (binding.status == ReplayLocalVisualStatus::ready_studio) {
        game_api::PlayerAppearanceSelection appearance;
        std::optional<std::size_t> appearance_index;
        auto visual_identity=r::EntityRenderResourceIdentity{record.resource_id(),record.resource_revision()};
        auto visual_fingerprint=record.source_fingerprint();
        auto visual_render_index=*binding.render_index;
        auto visual_model=record.model_asset();
        if(sampled_game_ && (entity.player_movement_schema || proxy)) {
          const auto model_name=summary_->model_names.find(*model_slot);
          if(corpse!=corpse_appearances_.end())appearance=corpse->second.appearance;
          else appearance=sampled_game_->player_appearance(proxy ? selection.source_player.value_or(0U) : entity.entity_number,
              model_name!=summary_->model_names.end() ? model_name->second : std::string_view{});
          if(proxy && corpse==corpse_appearances_.end() && corpse_appearances_.size()<64U)
            corpse_appearances_.emplace(entity.entity_number,CorpseAppearance{entity,selection,appearance});
          appearance_index=resolve_player_appearance(appearance,*record.model_asset(),visual_identity,visual_fingerprint,entity);
          if(!proxy && entity.entity_number<=32U) {
            auto published=published_appearances_.find(entity.entity_number);
            if(published!=published_appearances_.end() && published->second.occupant_epoch!=appearance.occupant_epoch) {
              published_appearances_.erase(published);published=published_appearances_.end();
            }
            // A late update is published atomically once its owning variant is
            // ready; only the same occupant may retain the previous selection.
            if(!appearance_index && appearance.status==game_api::PlayerAppearanceStatus::asset_pending &&
                published!=published_appearances_.end())appearance_index=published->second.variant;
            else if(appearance_index)published_appearances_[entity.entity_number]={appearance.occupant_epoch,*appearance_index};
            else published_appearances_.erase(entity.entity_number);
          }
          if(appearance_index) {
            const auto& variant=appearance_variants_[*appearance_index];visual_model=variant.model;
            visual_identity=variant.identity;visual_fingerprint=variant.fingerprint;visual_render_index=variant.render_index;
          }
        }
        const auto &model = *visual_model->skeletal_data;
        g::studio::StudioPoseInput pose_input;
        pose_input.compatibility_profile =
            g::studio::StudioPoseCompatibilityProfile::
                public_goldsrc48_discrete_local_asset_v1;
        pose_input.sequence_index = entity.sequence.value_or(0U);
        pose_input.body_value =
            static_cast<std::int32_t>(entity.body.value_or(0U));
        pose_input.skin_family_index =
            static_cast<std::uint32_t>(entity.skin.value_or(0));
        if (pose_input.sequence_index >= model.sequences.size() ||
            entity.skin.value_or(0) < 0 ||
            model.sequences[pose_input.sequence_index].frame_count == 0U ||
            entity.body.value_or(0U) >
                static_cast<std::uint32_t>(
                    std::numeric_limits<std::int32_t>::max())) {
          unavailable(entity.entity_number,
                      ReplayLocalVisualStatus::unsupported_pose);
          continue;
        }
        pose_input.frame_coordinate =
            entity.frame.value_or(0.0) *
            static_cast<double>(
                model.sequences[pose_input.sequence_index].frame_count - 1U) /
            256.0;
        for (std::size_t i = 0; i < 4; ++i) {
          pose_input.controller_values[i] =
              static_cast<std::uint8_t>(entity.controllers[i].value_or(0U));
        }
        for (std::size_t i = 0; i < 2; ++i) {
          pose_input.blending_values[i] =
              static_cast<std::uint8_t>(entity.blending[i].value_or(0U));
        }
        const g::studio::StudioPoseModelIdentity pose_identity{
            std::to_string(visual_identity.resource_id),visual_fingerprint};
        std::optional<g::studio::StudioPoseEvaluationResult> pose_result;
        std::shared_ptr<const g::studio::StudioPoseEvaluationResult> owned_ordinary_pose;
        const g::studio::StudioPoseEvaluationResult* shared_ordinary_pose=nullptr;
        bool player_pose=false;
        bool ordinary_cache_hit=false;
        // Raw ownership is recorded only from the existing selected anchor;
        // a blended presentation entity is never relabelled as wire state.
        const client::RuntimePacketEntityObservation* ordinary_raw=sampled_game_ ? nullptr : &entity;
        const client::RuntimeClientObservationState* ordinary_source=sampled_game_ ? nullptr : &observation;
        std::optional<double> ordinary_pose_seconds,ordinary_previous_seconds;
        std::optional<std::uint32_t> attachment_slot;
        ReplayLocalVisualSummary::RemotePlayer* player_diagnostic=nullptr;
        game_api::RemoteWeaponPoseSample* weapon_sample=nullptr;
        if (sampled_game_ && (entity.player_movement_schema || proxy)) {
          const auto sampled_anchor=std::ranges::find(sampled_player_anchors_,entity.entity_number,
              &g::RuntimePlayerInterpolationAnchor::entity);
          const auto current_source=!proxy && sampled_anchor!=sampled_player_anchors_.end() ?
              sampled_anchor->current : current_entities_;
          auto previous_source=!proxy && sampled_anchor!=sampled_player_anchors_.end() ?
              sampled_anchor->previous : previous_entities_;
          if(current_source==current_entities_ && previous_source==current_source)
            previous_source=previous_entities_; // Preserve real held/gap raw anchors at the API.
          const auto* current=find_sorted_packet_entity(current_source->packet_entities,
              entity.entity_number);
          const client::RuntimePacketEntityObservation* previous=nullptr;
          if (previous_source && previous_source!=current_source) {
            previous=find_sorted_packet_entity(previous_source->packet_entities,entity.entity_number);
          }
          const auto lifetime=player_lifetimes_.find(entity.entity_number);
          if (!current || (!proxy && lifetime==player_lifetimes_.end())) {
            unavailable(entity.entity_number,ReplayLocalVisualStatus::unsupported_pose); continue;
          }
          const auto source_record=current_source->entity_metadata.source ? current_source->entity_metadata.source->record_ordinal : 0U;
          const auto latest_record=current_entities_->entity_metadata.source ? current_entities_->entity_metadata.source->record_ordinal : 0U;
          if(!proxy && lifetime->second.continuity_floor && latest_record<=lifetime->second.continuity_floor) {
            unavailable(entity.entity_number,ReplayLocalVisualStatus::unsupported_pose); continue;
          }
          if(!proxy && lifetime->second.continuity_floor && previous_source && previous_source->entity_metadata.source &&
              previous_source->entity_metadata.source->record_ordinal<=lifetime->second.continuity_floor) previous=nullptr;
          game_api::RemotePlayerPresentationContext context{generation_,generation_,proxy ? 0U : lifetime->second.identity,
              visual_identity.resource_id,visual_fingerprint,model,entity,*current,previous,
              maximum_clients_,sampled_receiving_entity_,
              current_source->entity_metadata.source ? current_source->entity_metadata.source->record_identity : 0U,
              previous_source && previous_source->entity_metadata.source ? previous_source->entity_metadata.source->record_identity : 0U,
              current_source->server_time_seconds.value_or(sampled_current_seconds_),previous_source ?
                  previous_source->server_time_seconds.value_or(sampled_previous_seconds_) : sampled_previous_seconds_,
              !proxy && sampled_anchor!=sampled_player_anchors_.end() ? sampled_anchor->sample_seconds :
                  proxy ? current_source->server_time_seconds.value_or(sampled_current_seconds_) : sampled_server_seconds_,false,
              source_record,previous_source && previous_source->entity_metadata.source ?
                  previous_source->entity_metadata.source->record_ordinal : 0U};
          const auto policy=sampled_game_->remote_player_policy();
          g::RuntimeEntityInterpolationOptions motion_options;
          motion_options.animation_only_effect_exceptions=sampled_motion_exceptions_;
          if(!proxy) {
            context.current_animation_only_effect_mask=g::runtime_animation_only_effect_mask(motion_options,entity.entity_number,source_record);
            context.previous_animation_only_effect_mask=g::runtime_animation_only_effect_mask(motion_options,entity.entity_number,
                static_cast<std::size_t>(context.previous_record_ordinal));
          }
          context.server_owned_proxy=proxy;
          context.discontinuity=proxy || (sampled_anchor!=sampled_player_anchors_.end() && sampled_anchor->discontinuity) || sampled_game_->world_entity({*current,previous,
              current_entities_->packet_entities,maximum_clients_}).discontinuity || !previous || previous->model_index!=current->model_index ||
              previous->player_movement_schema!=current->player_movement_schema ||
              (current->effects.value_or(0U)&policy.no_interpolation_effect_mask&~context.current_animation_only_effect_mask)!=0U ||
              context.current_server_seconds-context.previous_server_seconds>policy.maximum_observation_gap_seconds;
          if (previous && previous->origin.complete() && current->origin.complete()) {
            const double dx=*current->origin.x-*previous->origin.x,dy=*current->origin.y-*previous->origin.y,
                dz=*current->origin.z-*previous->origin.z;
            context.discontinuity=context.discontinuity ||
                dx*dx+dy*dy+dz*dz>policy.teleport_distance_units*policy.teleport_distance_units;
          }
          const auto intent=sampled_game_->remote_player(context);
          if(next.remote_player_count<next.remote_players.size()) {
            auto& diagnostic=next.remote_players[next.remote_player_count++];
            diagnostic={}; diagnostic.entity=entity.entity_number; diagnostic.model_slot=*model_slot;
            diagnostic.appearance=appearance;diagnostic.visual_model_resource_id=visual_identity.resource_id;
            const auto base_name=summary_->model_names.find(*model_slot);
            diagnostic.resolved_visual_model=appearance_index && !appearance_variants_[*appearance_index].virtual_name.empty()
                ? appearance_variants_[*appearance_index].virtual_name : base_name!=summary_->model_names.end() ? base_name->second : "";
            diagnostic.palette_remapped=appearance_index && appearance_variants_[*appearance_index].remapped;
            diagnostic.status=intent.status; diagnostic.intent=intent;
            diagnostic.server_owned_proxy=proxy;diagnostic.source_player=selection.source_player;
            diagnostic.classification=selection.reason;
            diagnostic.network_generation=diagnostic.map_generation=generation_;
            diagnostic.entity_lifetime=context.entity_lifetime;
            diagnostic.render_effect=entity.render_effect;diagnostic.render_amount=entity.render_amount;
            if(rider_support) {
              diagnostic.support_entity=rider_support->support.source_entity_index;
              diagnostic.support_model=rider_support->support.source_model_index;
              diagnostic.support_offset_z=rider_support->offset_z;
            }
            diagnostic.weapon_model_index=current->weapon_model_index;diagnostic.gait_sequence=current->gait_sequence;
            diagnostic.effects=current->effects.value_or(0U);diagnostic.no_interpolation=context.discontinuity;
            diagnostic.origin={*entity.origin.x,*entity.origin.y,*entity.origin.z};
            diagnostic.source_angles={*entity.angles.x,*entity.angles.y,*entity.angles.z};
            player_diagnostic=&diagnostic;
          }
          if (intent.status!=game_api::RemotePlayerPresentationStatus::ready) {
            unavailable(entity.entity_number,ReplayLocalVisualStatus::unsupported_pose); continue;
          }
          const auto* latest_socket_entity=find_sorted_packet_entity(current_entities_->packet_entities,
              entity.entity_number);
          const bool matching_socket_weapon=!intent.attachment_model_index ||
              (latest_socket_entity &&
               latest_socket_entity->weapon_model_index==intent.attachment_model_index &&
               current->weapon_model_index==intent.attachment_model_index);
          // A delayed old p-model cannot supply a socket for a new weapon's
          // live event. Omit only that numeric pose and retain the existing
          // server-event fallback; never weaken the effects' mismatch guard.
          if(!proxy && matching_socket_weapon && next_weapon_pose_count<next_weapon_poses.size()) {
            weapon_sample=&next_weapon_poses[next_weapon_pose_count++];
            weapon_sample->generation=generation_;weapon_sample->map_generation=generation_;
            weapon_sample->entity_lifetime=lifetime->second.identity;
            weapon_sample->entity=entity.entity_number;weapon_sample->model_slot=intent.attachment_model_index.value_or(0U);
            weapon_sample->current_model_slot=current->weapon_model_index;
            weapon_sample->source_record=context.current_record_identity;
            weapon_sample->source_ordinal=context.current_record_ordinal;
            weapon_sample->frame_identity=frame_revision_+1U;
            weapon_sample->observed_at_seconds=clock_anchor_seconds_;
            weapon_sample->presented_at_seconds=sampled_application_seconds_;
            weapon_sample->effects=current->effects.value_or(0U);
            weapon_sample->server_sequence=intent.sample.sequence;
          }
          const auto convert=[&](const game_api::RemotePlayerStudioSample& sample) {
            auto value=pose_input; value.sequence_index=sample.sequence;
            value.frame_coordinate=sample.frame_coordinate; value.body_value=sample.body;
            value.skin_family_index=sample.skin; value.controller_values=sample.controllers;
            value.blending_values=sample.blending; return value;
          };
          g::studio::StudioPoseCompositionInput composition;
          composition.main=convert(intent.sample);
          if(intent.previous_sample) composition.previous=convert(*intent.previous_sample);
          composition.previous_weight=static_cast<float>(intent.previous_weight);
          if(intent.gait_sample) {
            composition.layer=convert(*intent.gait_sample);
            for(std::size_t i=0;i<intent.bone_count && i<intent.gait_bone_mask.size();++i)
              if(intent.gait_bone_mask[i]) composition.lower_bone_indices.push_back(static_cast<std::uint32_t>(i));
          }
          pose_input=composition.main;
          attachment_slot=intent.attachment_model_index;
          transform.rotation_degrees={static_cast<float>(intent.transform_angles[2]),
              -static_cast<float>(intent.transform_angles[0]),static_cast<float>(intent.transform_angles[1])};
          pose_result.emplace(g::studio::StudioPoseEvaluator{}.compose(pose_identity,model,composition));
          player_pose=true;
        } else {
          std::optional<g::studio::StudioPoseCompositionInput> ordinary_composition;
          if(sampled_game_ && sampled_player_current_) {
            const auto find_raw=[&](const auto& source)->const client::RuntimePacketEntityObservation* {
              if(!source)return nullptr;
              return find_sorted_packet_entity(source->packet_entities,entity.entity_number);
            };
            auto raw_current=find_raw(sampled_player_current_);
            auto raw_previous=find_raw(sampled_player_previous_);
            const bool bracketed=raw_current && raw_current->model_index==entity.model_index;
            if(!bracketed)raw_current=find_raw(current_entities_);
            const auto& current_source=bracketed ? sampled_player_current_ : current_entities_;
            if(raw_current && raw_current->model_index==entity.model_index && current_source->server_time_seconds) {
              ordinary_raw=raw_current;ordinary_source=current_source.get();
              g::studio::StudioEntityAnimationContext context{model,*raw_current};
              context.previous=bracketed ? raw_previous : nullptr;context.base=pose_input;
              context.generation=generation_;context.model_resource_id=visual_identity.resource_id;
              context.model_revision=visual_fingerprint;
              context.record_ordinal=current_source->entity_metadata.source ?
                  current_source->entity_metadata.source->record_ordinal : 0U;
              context.current_server_seconds=*current_source->server_time_seconds;
              context.previous_server_seconds=sampled_player_previous_ ?
                  sampled_player_previous_->server_time_seconds.value_or(context.current_server_seconds) : context.current_server_seconds;
              context.sample_server_seconds=sampled_server_seconds_;
              context.maximum_gap_seconds=sampled_game_->remote_player_policy().maximum_observation_gap_seconds;
              context.discontinuity=!bracketed || selection.discontinuity;
              ordinary_composition=ordinary_animation_.sample(context);
              if(ordinary_composition) {
                pose_input=ordinary_composition->main;
                ordinary_pose_seconds=context.sample_server_seconds;
                if(context.previous && sampled_player_previous_)
                  ordinary_previous_seconds=sampled_player_previous_->server_time_seconds;
              }
            }
          }
          if(ordinary_composition && ordinary_composition->previous) {
            ++next.ordinary_pose_evaluations;
            pose_result.emplace(g::studio::StudioPoseEvaluator{}.compose(pose_identity,model,*ordinary_composition));
          } else {
          const auto cached=std::ranges::find_if(ordinary_poses,[&](const auto& candidate) {
            return candidate.model==&model && candidate.identity==pose_identity &&
                exact_ordinary_pose_input(candidate.input,pose_input);
          });
          if(cached!=ordinary_poses.end()) {
            shared_ordinary_pose=cached->result.get();++next.ordinary_pose_reuses;ordinary_cache_hit=true;
          } else {
            ++next.ordinary_pose_evaluations;
            pose_result.emplace(g::studio::StudioPoseEvaluator{}.evaluate(pose_identity,model,pose_input));
            constexpr std::size_t maximum_entries=64U,maximum_bytes=1U*1024U*1024U;
            if(*pose_result && ordinary_poses.size()<maximum_entries) {
              const auto bytes=pose_result->pose->statistics().accounted_pose_bytes;
              if(bytes<=maximum_bytes-ordinary_pose_bytes) {
                try {
                  // Allocation failure precedes the noexcept move. If entry
                  // insertion fails afterwards, this instance still owns and
                  // consumes the complete evaluated state through the pointer.
                  owned_ordinary_pose=std::make_shared<const g::studio::StudioPoseEvaluationResult>(std::move(*pose_result));
                  shared_ordinary_pose=owned_ordinary_pose.get();
                  ordinary_poses.push_back({&model,pose_identity,pose_input,
                      owned_ordinary_pose});
                  ordinary_pose_bytes+=bytes;
                } catch(const std::bad_alloc&) {
                  // A cache cannot turn an otherwise valid frame into failure.
                } catch(const std::length_error&) {
                }
              }
            }
          }
          }
        }
        const auto& pose=shared_ordinary_pose ? *shared_ordinary_pose : *pose_result;
        if (!pose) {
          unavailable(entity.entity_number,
                      ReplayLocalVisualStatus::unsupported_pose);
          continue;
        }
        if(!player_pose && diagnostic_sink_ && diagnostic_sink_->enabled())
          observe_ordinary_studio(entity.entity_number,visual_identity.resource_id,
              ordinary_raw,ordinary_source,pose_input,transform.origin,
              transform.rotation_degrees,ordinary_cache_hit,ordinary_pose_seconds,
              ordinary_previous_seconds);
        if(!model.attachments.empty()) {
          auto& attachments=next_attachments[entity.entity_number];
          for(std::uint32_t i=0;i<std::min<std::size_t>(4,model.attachments.size());++i) {
            const auto socket=r::evaluate_studio_attachment(pose_identity,model,*pose.pose,i,transform);
            if(socket)attachments[i]=socket.socket->origin;
          }
        }
        const auto &asset = *package_->studio_assets()[visual_render_index];
        const auto material = r::studio_entity_material_support(
            asset, static_cast<std::uint32_t>(pose_input.body_value), pose_input.skin_family_index);
        if (!material) {
          unavailable(entity.entity_number,
                      ReplayLocalVisualStatus::unsupported_material);
          continue;
        }
        r::StudioRenderPose render_pose{asset.source_identity(), {}};
        for (const auto &bone : pose.pose->world_bones()) {
          const auto &m = bone.transform.values;
          render_pose.bone_matrices.push_back({m[0], m[4], m[8], 0, m[1], m[5],
                                               m[9], 0, m[2], m[6], m[10], 0,
                                               m[3], m[7], m[11], 1});
        }
        r::StudioEntityRenderInstance instance;
        instance.entity_number = entity.entity_number;
        instance.studio_asset_index =
            static_cast<std::uint32_t>(visual_render_index);
        instance.pose_index =
            static_cast<std::uint32_t>(input.studio_poses.size());
        instance.transform = transform;
        instance.body_value = static_cast<std::uint32_t>(pose_input.body_value);
        instance.skin_family_index = pose_input.skin_family_index;
        instance.material_support_status = *material;
        if(entity.render_mode.value_or(0U)!=0U) {
          instance.blend_mode=entity.render_mode==5U ? r::StudioBlendMode::additive : r::StudioBlendMode::alpha_blended;
          instance.opacity=static_cast<float>(entity.render_amount.value_or(255U))/255.0F;
          if(entity.render_mode==1U)
            for(std::size_t channel=0U;channel<instance.tint.size();++channel)
              instance.tint[channel]=static_cast<float>(entity.render_color[channel].value_or(255U))/255.0F;
        }
        instance.interpolated_bounds =
            bounds({asset.bounds().minimum, asset.bounds().maximum}, transform);
        // World lighting is a Studio property, not a remote-player policy.
        // Ordinary pickups/projectiles must not fall back to constant brightness.
        instance.static_light_rgb=studio_light(entity.entity_number,transform.origin);
        if (player_pose) {
          const auto posed=g::studio::StudioPoseEvaluator{}.posed_bounds(pose_identity,model,*pose.pose);
          if (!posed) { unavailable(entity.entity_number,ReplayLocalVisualStatus::unsupported_pose); continue; }
          instance.interpolated_bounds=bounds({posed.bounds->minimum,posed.bounds->maximum},transform);
          if(player_diagnostic) {
            player_diagnostic->static_light=instance.static_light_rgb; player_diagnostic->pose_submitted=true;
          }
        }
        input.studio_poses.push_back(std::move(render_pose));
        input.studio_instances.push_back(instance);
        if(player_pose && attachment_slot) {
          // This is an exact committed type-local slot, not a filename-derived
          // substitution. Failure affects only the child, never its owner.
          const auto attach=[&]() -> ReplayAttachmentStatus {
            const auto found=bindings_.find(*attachment_slot);
            if(found==bindings_.end()) return ReplayAttachmentStatus::unknown_slot;
            const auto& child_binding=found->second;
            if(child_binding.status!=ReplayLocalVisualStatus::ready_studio ||
                !child_binding.library_index || !child_binding.render_index)
              return ReplayAttachmentStatus::resource_unavailable;
            const auto& child_record=library_->records()[*child_binding.library_index];
            const auto& child=*child_record.model_asset()->skeletal_data;
            const g::studio::StudioPoseModelIdentity child_identity{
                std::to_string(child_record.resource_id()),child_record.source_fingerprint()};
            const auto mapping=bone_merge_cache_.prepare(pose_identity,model,child_identity,child);
            if(!mapping) return ReplayAttachmentStatus::merge_rejected;
            if(player_diagnostic) {
              player_diagnostic->attachment_matched_bones=mapping.mapping->matched_bones;
              player_diagnostic->attachment_bone_count=child.bones.size();
            }
            auto child_input=pose_input;
            // Public Studio attachment semantics: child defaults for body/skin,
            // shared sequence when available, otherwise sequence zero. Matched
            // bones use the final composed player palette; unmatched bones use
            // this child's sequence/controller context and merged hierarchy.
            child_input.body_value=0; child_input.skin_family_index=0;
            if(child_input.sequence_index>=child.sequences.size()) child_input.sequence_index=0;
            if(child.sequences.empty() || child.sequences[child_input.sequence_index].frame_count==0U)
              return ReplayAttachmentStatus::pose_rejected;
            const auto& sequence=child.sequences[child_input.sequence_index];
            child_input.frame_coordinate=entity.frame.value_or(0.0)*double(sequence.frame_count-1U)/256.0;
            if(entity.animation_time_seconds && sampled_server_seconds_>*entity.animation_time_seconds)
              child_input.frame_coordinate+=(sampled_server_seconds_-*entity.animation_time_seconds)*
                  entity.frame_rate.value_or(1.0)*sequence.frames_per_second;
            const auto merged=g::studio::StudioPoseEvaluator{}.merge(*pose.pose,child,*mapping.mapping,child_input);
            if(!merged) return ReplayAttachmentStatus::pose_rejected;
            const auto posed=g::studio::StudioPoseEvaluator{}.posed_bounds(child_identity,child,*merged.pose);
            if(!posed) return ReplayAttachmentStatus::pose_rejected;
            const auto& child_asset=*package_->studio_assets()[*child_binding.render_index];
            const auto support=r::studio_entity_material_support(child_asset,0U,0U);
            if(!support || *support==r::StudioEntityMaterialSupportStatus::unsupported_material)
              return ReplayAttachmentStatus::material_rejected;
            if(weapon_sample) {
              const auto named=summary_->model_names.find(*attachment_slot);
              const auto plan=sampled_game_->remote_weapon_socket_plan({child_record.resource_id(),
                  child_record.source_fingerprint(),named==summary_->model_names.end() ? std::string_view{} : named->second,child});
              weapon_sample->semantic_known=true;weapon_sample->semantic_match=plan.semantic_match;
              weapon_sample->semantic_key=plan.semantic_key;
              weapon_sample->resource_id=child_record.resource_id();weapon_sample->revision=child_record.source_fingerprint();
              weapon_sample->muzzle_bone=plan.muzzle.bone_index;weapon_sample->shell_bone=plan.shell.bone_index;
              if(plan.source!=game_api::RemoteSocketSource::unavailable) {
                const auto muzzle=plan.muzzle_attachment ? r::evaluate_studio_attachment(child_identity,child,*merged.pose,*plan.muzzle_attachment,transform) :
                    r::evaluate_studio_socket(child_identity,child,*merged.pose,plan.muzzle,transform);
                const auto shell=plan.shell_attachment ? r::evaluate_studio_attachment(child_identity,child,*merged.pose,*plan.shell_attachment,transform) :
                    r::evaluate_studio_socket(child_identity,child,*merged.pose,plan.shell,transform);
                if(muzzle && shell) {
                  weapon_sample->source=plan.source;weapon_sample->muzzle=*muzzle.socket;weapon_sample->shell=*shell.socket;
                }
              }
            }
            r::StudioRenderPose child_pose{child_asset.source_identity(),{}};
            for(const auto& bone:merged.pose->world_bones()) {
              const auto& m=bone.transform.values;
              child_pose.bone_matrices.push_back({m[0],m[4],m[8],0,m[1],m[5],m[9],0,m[2],m[6],m[10],0,m[3],m[7],m[11],1});
            }
            auto child_instance=instance;
            child_instance.subinstance_ordinal=1U;
            child_instance.studio_asset_index=static_cast<std::uint32_t>(*child_binding.render_index);
            child_instance.pose_index=static_cast<std::uint32_t>(input.studio_poses.size());
            child_instance.body_value=0U;child_instance.skin_family_index=0U;
            child_instance.material_support_status=*support;
            child_instance.interpolated_bounds=bounds({posed.bounds->minimum,posed.bounds->maximum},transform);
            input.studio_poses.push_back(std::move(child_pose));
            input.studio_instances.push_back(std::move(child_instance));
            add_rendered_slot(*attachment_slot);
            return ReplayAttachmentStatus::submitted;
          };
          const auto status=attach();
          if(player_diagnostic) player_diagnostic->attachment_status=status;
        }
        if (camera_policy_ == ReplayLocalCameraPolicy::offline_spectator &&
            !camera_selected_ && !camera) {
          // One-time spectator view of the first supported model.
          // No player identity or captured first-person view is inferred.
          camera = spectator_camera(camera_collision_, transform.origin);
        }
      } else {
        const auto &asset = *package_->sprite_assets()[*binding.render_index];
        if (asset.render_profile() ==
                r::SpriteRenderTextureProfile::unsupported ||
            asset.orientation() == assets::SpriteOrientation::facing_upright ||
            asset.orientation() ==
                assets::SpriteOrientation::view_parallel_oriented) {
          unavailable(entity.entity_number,
                      ReplayLocalVisualStatus::unsupported_asset);
          continue;
        }
        // Grouped formats still require the existing selector. Ordinary frame
        // sprites (including server-owned endpoint effects) use server frame.
        if (asset.frames().empty() || (record.sprite_asset()->source_data && !record.sprite_asset()->source_data->groups.empty())) {
          unavailable(entity.entity_number,
                      ReplayLocalVisualStatus::unsupported_pose);
          continue;
        }
        r::SpriteEntityRenderInstance instance;
        instance.entity_number = entity.entity_number;
        instance.sprite_asset_index =
            static_cast<std::uint32_t>(*binding.render_index);
        instance.transform = transform;
        instance.orientation = asset.orientation();
        instance.texture_format_support = asset.texture_support_status();
        instance.selected_frame_index=static_cast<std::uint32_t>(std::min(static_cast<double>(asset.frames().size()-1),std::floor(entity.frame.value_or(0))));
        if(entity.render_mode==3U) {
          instance.blend_mode=r::SpriteBlendMode::glow;
          instance.opacity=entity.render_amount.value_or(255U)/255.0F;
        }
        const float radius = asset.bounding_radius();
        instance.bounds = bounds(
            {{-radius, -radius, -radius}, {radius, radius, radius}}, transform);
        input.sprite_instances.push_back(instance);
      }
      ++next.rendered_instances;
      add_coverage(binding.status);
      add_rendered_slot(*entity.model_index);
    }
    auto frame =
        r::EntityRenderFrameBuilder{}.build(*package_, std::move(input));
    if (!frame) {
      return {false, false, visual_error(frame.error->context)};
    }
    for(std::size_t i=0;i<next.remote_player_count;++i) {
      auto& diagnostic=next.remote_players[i];
      const auto instance=std::ranges::find(frame.frame->studio_instances(),diagnostic.entity,
          &r::StudioEntityRenderInstance::entity_number);
      diagnostic.visible=instance!=frame.frame->studio_instances().end() &&
          instance->visibility_status==r::RuntimeEntityVisibilityStatus::visible;
      if(diagnostic.attachment_status==ReplayAttachmentStatus::submitted) {
        const auto child=std::ranges::find_if(frame.frame->studio_instances(),[&](const auto& value) {
          return value.entity_number==diagnostic.entity && value.subinstance_ordinal==1U;
        });
        diagnostic.attachment_status=child!=frame.frame->studio_instances().end() &&
            child->visibility_status==r::RuntimeEntityVisibilityStatus::visible ?
            ReplayAttachmentStatus::visible : ReplayAttachmentStatus::culled;
      }
    }
    auto candidate = target;
    std::ranges::sort(brush_frame.instances, {}, &world_scene_render::RuntimeBrushRenderInstance::entity_number);
    if (const auto& old = candidate.runtime_brushes(); old) {
      for (const auto& current : brush_frame.instances) for (const auto& previous : old->instances)
        if (current.entity_number == previous.entity_number && current.model_slot == previous.model_slot &&
            current.model_transform != previous.model_transform) {
          ++next.brush_transform_changes;
          next.last_changed_brush_entity=current.entity_number;
          next.last_changed_brush_model=current.source_model_index;
        }
    }
    if (!candidate.set_runtime_brushes(
            std::make_shared<const world_scene_render::RuntimeBrushRenderFrame>(std::move(brush_frame))) ||
        !candidate.set_dynamic_entities(
            package_, std::make_shared<const r::EntityRenderFrame>(
                          std::move(*frame.frame)))) {
      return {false, false,
              visual_error("atomic local visual frame publication rejected")};
    }
    // Stage every allocation in the authoritative ownership transaction
    // before the render target is published. Sampled presentation owns only
    // its filtered effect view and attachments.
    std::optional<AuthoritativeObservationStage> authoritative;
    if(!sampled_game_)
      authoritative.emplace(stage_authoritative_observation(observation));
    std::vector<client::RuntimePacketEntityObservation> next_effect_entities;
    std::vector<std::uint32_t> next_unavailable_sources;
    if(sampled_game_) {
      next_effect_entities=observation.packet_entities;
      next_unavailable_sources.reserve(next_effect_entities.size());
      std::erase_if(next_effect_entities,[&](const auto& entity) {
      const client::RuntimePacketEntityObservation* previous=nullptr;
      if(previous_entities_) {
        previous=find_sorted_packet_entity(previous_entities_->packet_entities,entity.entity_number);
      }
      const auto& current=current_entities_ ? current_entities_->packet_entities : observation.packet_entities;
      // Copy the selected module's source-availability decision while its
      // synchronous view is bound. Later attachment passes retain no module
      // pointer and must not resurrect an unavailable retained actor. Canonical
      // packets and independent world entities/point effects remain untouched.
      const bool unavailable=sampled_game_->world_entity({entity,previous,current,maximum_clients_}).role==
          game_api::WorldEntityPresentationRole::unavailable;
      if(unavailable) next_unavailable_sources.push_back(entity.entity_number);
      return unavailable;
      });
    }
    for (std::size_t status=0; status<coverage_counts.size(); ++status)
      if (coverage_counts[status])
        next.coverage.emplace(static_cast<ReplayLocalVisualStatus>(status), coverage_counts[status]);
    for (const auto& [slot, count] : model_slot_counts)
      if (count) next.rendered_model_slots[slot] += count;
    for (std::size_t index=next.remote_player_count; index<next.remote_players.size(); ++index)
      next.remote_players[index]=summary_->remote_players[index];
    target = std::move(candidate);
    if (camera_policy_ == ReplayLocalCameraPolicy::offline_spectator &&
        camera) {
      target.set_camera(*camera);
      camera_selected_ = true;
    }
    ++frame_revision_;
    ++next.projected_frames;
    static_assert(std::is_nothrow_swappable_v<decltype(summary_)>);
    std::swap(summary_, staged_summary_);
    if(sampled_game_) {
      remote_weapon_poses_=next_weapon_poses;remote_weapon_pose_count_=next_weapon_pose_count;
      sampled_game_->remote_weapon_poses(remote_weapon_poses());
    }
    if(authoritative)
      commit_authoritative_observation(std::move(*authoritative));
    else {
      presented_effect_entities_=std::move(next_effect_entities);
      unavailable_effect_sources_=std::move(next_unavailable_sources);
    }
    presented_attachments_=std::move(next_attachments);
    return {true, true, {}};
  } catch (const std::exception &error) {
    return {false, false, visual_error(error.what())};
  }
}

std::shared_ptr<const assets::SkeletalModelAssetData>
RuntimeReplayLocalAssets::studio_model_data(const std::uint32_t index) const noexcept {
  if (!library_) return {};
  const auto bound=bindings_.find(index);
  if (bound==bindings_.end() || bound->second.status!=ReplayLocalVisualStatus::ready_studio ||
      !bound->second.library_index || *bound->second.library_index>=library_->records().size()) return {};
  const auto& asset=library_->records()[*bound->second.library_index].model_asset();
  return asset ? asset->skeletal_data : nullptr;
}

std::optional<game_api::LocalWeaponModelMetadata>
RuntimeReplayLocalAssets::presentation_model(
    const std::uint64_t generation, const std::uint32_t index) {
  if (!library_ || !package_ || generation != generation_) return {};
  const auto bound = bindings_.find(index);
  const auto named = summary_->model_names.find(index);
  if (bound == bindings_.end() || named == summary_->model_names.end() ||
      bound->second.status != ReplayLocalVisualStatus::ready_studio ||
      !bound->second.library_index || !bound->second.render_index) return {};
  if (auto cached = presentation_models_.find(index); cached != presentation_models_.end()) {
    cached->second.generation = generation_;
    return cached->second;
  }
  const auto& record = library_->records()[*bound->second.library_index];
  if (!record.model_asset() || !record.model_asset()->skeletal_data) return {};
  const auto& model = *record.model_asset()->skeletal_data;
  const auto& asset = *package_->studio_assets()[*bound->second.render_index];
  game_api::LocalWeaponModelMetadata result;
  result.generation = generation_;
  result.model_index = index;
  result.resource_revision = library_->resource_revision();
  result.resource_name = named->second;
  for (const auto& seq : model.sequences)
    result.sequences.push_back({seq.frames_per_second, seq.frame_count,
                                (seq.source_flags & 1U) != 0U, seq.events, seq.label});
  for (std::size_t body = 0U; body < result.supported_bodies.size(); ++body) {
    result.selectable_bodies[body] =
        static_cast<bool>(g::studio::StudioBodyPartSelector{}.select(model, static_cast<int>(body)));
    result.supported_bodies[body] =
        result.selectable_bodies[body] &&
        r::studio_entity_material_support(asset, static_cast<std::uint32_t>(body), 0U).has_value();
  }
  presentation_models_.emplace(index, result);
  return result;
}

std::optional<renderer::RenderDynamicEntities>
RuntimeReplayLocalAssets::materialize_viewmodel(const game_api::ViewmodelIntent& intent,
    std::optional<assets::AssetVector3> world_light_origin) {
  first_person_status_ = ReplayLocalVisualStatus::absent_model;
  viewmodel_attachment_zero_.reset();
  viewmodel_pose_intent_.reset();
  viewmodel_pose_.reset();
  if (intent.status == game_api::ViewmodelStatus::unsupported_asset) {
    first_person_status_ = ReplayLocalVisualStatus::unsupported_asset;
    const auto unavailable = bindings_.find(intent.model_index);
    if (unavailable == bindings_.end())
      first_person_status_ = ReplayLocalVisualStatus::unknown_model_slot;
    else if (unavailable->second.status != ReplayLocalVisualStatus::ready_studio)
      first_person_status_ = unavailable->second.status;
  }
  if (intent.status == game_api::ViewmodelStatus::unsupported_pose)
    first_person_status_ = ReplayLocalVisualStatus::unsupported_pose;
  if (intent.status != game_api::ViewmodelStatus::ready || !package_ || !library_ ||
      intent.generation != generation_ || !std::isfinite(intent.local_time_seconds) ||
      !std::isfinite(intent.frame_coordinate) || intent.frame_coordinate < 0.0) return {};
  if(!std::ranges::all_of(intent.camera_local_origin,[](float v){return std::isfinite(v) && std::abs(v)<=64.0F;})) {
    first_person_status_=ReplayLocalVisualStatus::incomplete_transform;return {};
  }
  const auto found = bindings_.find(intent.model_index);
  if (found == bindings_.end()) {
    first_person_status_ = ReplayLocalVisualStatus::unknown_model_slot; return {};
  }
  if (found->second.status != ReplayLocalVisualStatus::ready_studio ||
      !found->second.library_index || !found->second.render_index) {
    first_person_status_ = found->second.status; return {};
  }
  const auto& record = library_->records()[*found->second.library_index];
  const auto& asset = *package_->studio_assets()[*found->second.render_index];
  if (!record.model_asset() || !record.model_asset()->skeletal_data) {
    first_person_status_ = ReplayLocalVisualStatus::unsupported_pose; return {};
  }
  const auto& model = *record.model_asset()->skeletal_data;
  const auto local_time_seconds = intent.local_time_seconds;
  g::studio::StudioPoseInput pose_input;
  pose_input.compatibility_profile =
      g::studio::StudioPoseCompatibilityProfile::public_goldsrc48_discrete_local_asset_v1;
  pose_input.sequence_index = intent.sequence;
  pose_input.body_value = intent.body;
  pose_input.frame_coordinate = intent.frame_coordinate;
  auto pose = g::studio::StudioPoseEvaluator{}.evaluate(
      {std::to_string(record.resource_id()), record.source_fingerprint()},
      model, pose_input);
  if (!pose) {
    first_person_status_ = ReplayLocalVisualStatus::unsupported_pose;
    return std::nullopt;
  }
  std::optional<assets::AssetVector3> attachment_zero;
  if (!model.attachments.empty()) {
    const auto& attachment = model.attachments.front();
    if (attachment.bone_index < pose.pose->world_bones().size()) {
      const auto& m=pose.pose->world_bones()[attachment.bone_index].transform.values;
      const auto p=attachment.local_origin;
      const assets::AssetVector3 location{
          m[0]*p.x+m[1]*p.y+m[2]*p.z+m[3]+intent.camera_local_origin[0],
          m[4]*p.x+m[5]*p.y+m[6]*p.z+m[7]+intent.camera_local_origin[1],
          m[8]*p.x+m[9]*p.y+m[10]*p.z+m[11]+intent.camera_local_origin[2]};
      if (std::isfinite(location.x)&&std::isfinite(location.y)&&std::isfinite(location.z))
        attachment_zero=location;
    }
  }
  const auto material = r::studio_entity_material_support(
      asset, static_cast<std::uint32_t>(pose_input.body_value), 0U);
  if (!material) {
    first_person_status_ = ReplayLocalVisualStatus::unsupported_material;
    return std::nullopt;
  }
  // Studio geometry and bone pose are already in the model's own +X-forward,
  // Z-up space. The first-person renderer supplies a fixed camera-local view.
  // Apply only the module's neutral held-origin translation, never the
  // predicted world eye or view Euler angles. Bone/mesh source scale is one.
  const r::EntityRenderTransform transform{
      {intent.camera_local_origin[0],intent.camera_local_origin[1],intent.camera_local_origin[2]},
      {0.0F, 0.0F, 0.0F}, 1.0F};
  if (!r::finite_entity_render_transform(transform)) {
    first_person_status_ = ReplayLocalVisualStatus::incomplete_transform;
    return std::nullopt;
  }
  r::StudioRenderPose render_pose{asset.source_identity(), {}};
  for (const auto &bone : pose.pose->world_bones()) {
    const auto &m = bone.transform.values;
    render_pose.bone_matrices.push_back({m[0], m[4], m[8], 0, m[1], m[5],
                                         m[9], 0, m[2], m[6], m[10], 0,
                                         m[3], m[7], m[11], 1});
  }
  r::StudioEntityRenderInstance instance;
  instance.entity_number = 1U; // Separate first-person frame namespace.
  instance.studio_asset_index = static_cast<std::uint32_t>(*found->second.render_index);
  instance.pose_index = 0U;
  instance.transform = transform;
  instance.body_value = static_cast<std::uint32_t>(pose_input.body_value);
  instance.material_support_status = *material;
  instance.interpolated_bounds = bounds(
      {asset.bounds().minimum, asset.bounds().maximum}, transform);
  // Sampling uses the world eye, while geometry and animation remain strictly
  // camera-local. Omitted/invalid probes retain the diagnostic unlit fallback.
  if(world_light_origin) instance.static_light_rgb=studio_light(0U,*world_light_origin);
  r::EntityRenderFrameBuildInput input;
  input.resource_id = frame_id + 1U;
  input.resource_revision = ++viewmodel_frame_revision_;
  input.interpolation = {local_time_seconds, local_time_seconds,
                         local_time_seconds, 0.0F,
                         intent.publication_revision,
                         intent.publication_revision,
                         r::EntityRenderInterpolationProfile::decoded_discrete_runtime_replay_v1};
  input.studio_poses.push_back(std::move(render_pose));
  input.studio_instances.push_back(instance);
  auto frame = r::EntityRenderFrameBuilder{}.build(*package_, std::move(input));
  if (!frame) {
    first_person_status_ = ReplayLocalVisualStatus::unsupported_pose;
    return std::nullopt;
  }
  // Finish all allocations before publishing same-frame socket state. A
  // failed frame allocation must leave attachments unavailable as well.
  renderer::RenderDynamicEntities materialized{
      package_, std::make_shared<const r::EntityRenderFrame>(std::move(*frame.frame)),
      {1U, 1U, 1U, 0U, 0U}};
  viewmodel_pose_.emplace(std::move(*pose.pose));
  viewmodel_pose_intent_.emplace(intent);
  viewmodel_attachment_zero_=attachment_zero;
  first_person_status_ = ReplayLocalVisualStatus::ready_studio;
  return materialized;
}

std::optional<assets::AssetVector3>
RuntimeReplayLocalAssets::viewmodel_attachment_at_frame(
    const game_api::ViewmodelIntent& intent, std::uint32_t attachment_index,
    double frame_coordinate) const {
  if (intent.status != game_api::ViewmodelStatus::ready ||
      intent.generation != generation_ || !std::isfinite(frame_coordinate) ||
      frame_coordinate < 0.0 || !library_ || !package_ ||
      !std::ranges::all_of(intent.camera_local_origin,[](float v){return std::isfinite(v) && std::abs(v)<=64.0F;})) return {};
  const auto bound = bindings_.find(intent.model_index);
  if (bound == bindings_.end() ||
      bound->second.status != ReplayLocalVisualStatus::ready_studio ||
      !bound->second.library_index || !bound->second.render_index) return {};
  const auto& record = library_->records()[*bound->second.library_index];
  if (!record.model_asset() || !record.model_asset()->skeletal_data) return {};
  const auto& model = *record.model_asset()->skeletal_data;
  if (attachment_index >= model.attachments.size() ||
      intent.sequence >= model.sequences.size()) return {};
  const auto& attachment = model.attachments[attachment_index];
  const g::studio::StudioPoseState* pose_state=nullptr;
  std::optional<g::studio::StudioPoseEvaluationResult> historical;
  if (viewmodel_pose_intent_ && *viewmodel_pose_intent_ == intent &&
      frame_coordinate == intent.frame_coordinate && viewmodel_pose_) {
    pose_state=&*viewmodel_pose_;
  } else {
    g::studio::StudioPoseInput pose_input;
    pose_input.compatibility_profile =
        g::studio::StudioPoseCompatibilityProfile::public_goldsrc48_discrete_local_asset_v1;
    pose_input.sequence_index = intent.sequence;
    pose_input.body_value = intent.body;
    pose_input.frame_coordinate = frame_coordinate;
    historical.emplace(g::studio::StudioPoseEvaluator{}.evaluate(
        {std::to_string(record.resource_id()), record.source_fingerprint()},
        model, pose_input));
    if (!*historical) return {};
    pose_state=&*historical->pose;
  }
  if (attachment.bone_index >= pose_state->world_bones().size()) return {};
  const auto& m = pose_state->world_bones()[attachment.bone_index].transform.values;
  const auto p = attachment.local_origin;
  const assets::AssetVector3 location{
      m[0]*p.x+m[1]*p.y+m[2]*p.z+m[3]+intent.camera_local_origin[0],
      m[4]*p.x+m[5]*p.y+m[6]*p.z+m[7]+intent.camera_local_origin[1],
      m[8]*p.x+m[9]*p.y+m[10]*p.z+m[11]+intent.camera_local_origin[2]};
  if (!std::isfinite(location.x) || !std::isfinite(location.y) ||
      !std::isfinite(location.z)) return {};
  return location;
}

bool RuntimeReplayLocalAssets::has_shell_model(std::string_view name) const noexcept {
  const auto named=std::ranges::find_if(summary_->model_names,[&](const auto& entry) {return entry.second==name;});
  if(named==summary_->model_names.end()) return false;
  const auto found=bindings_.find(named->first);
  return found!=bindings_.end() && found->second.status==ReplayLocalVisualStatus::ready_studio &&
      found->second.library_index && found->second.render_index;
}

std::optional<renderer::RenderDynamicEntities>
RuntimeReplayLocalAssets::materialize_shells(
    std::span<const renderer::TransientShellState> shells, double now,
    std::span<const TemporaryStudioFrame> models) {
  last_shell_frame_status_=ReplayShellFrameStatus::invalid_input;
  if ((shells.empty() && models.empty()) || shells.size()>32U || models.size()>32U || !std::isfinite(now)) return {};
  last_shell_frame_status_=ReplayShellFrameStatus::binding_unavailable;
  if (!package_ || !library_) return {};
  r::EntityRenderFrameBuildInput input;
  input.resource_id=frame_id+2U;
  input.resource_revision=++shell_frame_revision_;
  // EntityRenderFrameBuilder requires a nonzero discrete state identity.
  // These are presentation-frame identities, not authoritative snapshots.
  input.interpolation={now,now,now,0.0F,input.resource_revision,input.resource_revision,
      r::EntityRenderInterpolationProfile::decoded_discrete_runtime_replay_v1};
  std::map<std::size_t,std::uint32_t> poses;
  bool had_ready_binding=false;
  for (std::size_t i=0;i<shells.size()+models.size();++i) {
    const bool is_shell=i<shells.size();
    const auto token=is_shell ? std::span<const char>{shells[i].model_name} :
        std::span<const char>{models[i-shells.size()].model.model_name};
    const auto end=std::find(token.begin(),token.end(),'\0');
    if(end==token.end()) continue;
    const std::string_view name{token.data(),static_cast<std::size_t>(end-token.begin())};
    if(!is_shell) {
        const auto& frame=models[i-shells.size()];
        if(frame.identity.generation!=generation_ || !std::isfinite(frame.age_seconds) ||
            frame.age_seconds<0 || frame.age_seconds>=frame.model.lifetime_seconds) continue;
    }
    auto index=shell_model_index_; // compatibility for existing unnamed neutral fixtures
    if(!name.empty()) {
      const auto named=std::ranges::find_if(summary_->model_names,[&](const auto& entry) {return entry.second==name;});
      index=named!=summary_->model_names.end() ? std::optional{named->first} : std::nullopt;
    }
    if(!index) continue;
    const auto found=bindings_.find(*index);
    if(found==bindings_.end() || found->second.status!=ReplayLocalVisualStatus::ready_studio ||
        !found->second.library_index || !found->second.render_index) continue;
    const auto& record=library_->records()[*found->second.library_index];
    if(!record.model_asset() || !record.model_asset()->skeletal_data) continue;
    had_ready_binding=true;
    const auto render_index=*found->second.render_index;
    const auto& asset=*package_->studio_assets()[render_index];
    const auto material=r::studio_entity_material_support(asset,0U,0U);
    if(!material) {last_shell_frame_status_=ReplayShellFrameStatus::material_rejected;continue;}
    if(!poses.contains(render_index)) {
      g::studio::StudioPoseInput pose_input;
      pose_input.compatibility_profile=g::studio::StudioPoseCompatibilityProfile::public_goldsrc48_discrete_local_asset_v1;
      auto pose=g::studio::StudioPoseEvaluator{}.evaluate(
          {std::to_string(record.resource_id()),record.source_fingerprint()},*record.model_asset()->skeletal_data,pose_input);
      if(!pose) {last_shell_frame_status_=ReplayShellFrameStatus::pose_rejected;continue;}
      r::StudioRenderPose render_pose{asset.source_identity(),{}};
      for(const auto& bone:pose.pose->world_bones()) {
        const auto& m=bone.transform.values;
        render_pose.bone_matrices.push_back({m[0],m[4],m[8],0,m[1],m[5],m[9],0,
            m[2],m[6],m[10],0,m[3],m[7],m[11],1});
      }
      poses.emplace(render_index,static_cast<std::uint32_t>(input.studio_poses.size()));
      input.studio_poses.push_back(std::move(render_pose));
    }
    const r::EntityRenderTransform transform{
        is_shell ? shells[i].position : models[i-shells.size()].origin,
        is_shell ? assets::AssetVector3{0.0F,shells[i].yaw_degrees,0.0F} :
            assets::AssetVector3{models[i-shells.size()].model.angles_degrees.z,
                -models[i-shells.size()].model.angles_degrees.x,
                models[i-shells.size()].model.angles_degrees.y},1.0F};
    if (!r::finite_entity_render_transform(transform)) continue;
    r::StudioEntityRenderInstance instance;
    instance.entity_number=0xe0000000U+static_cast<std::uint32_t>(i);
    instance.studio_asset_index=static_cast<std::uint32_t>(*found->second.render_index);
    instance.pose_index=poses.at(render_index);
    instance.transform=transform;
    instance.body_value=0U;
    instance.material_support_status=*material;
    instance.interpolated_bounds=bounds({asset.bounds().minimum,asset.bounds().maximum},transform);
    instance.static_light_rgb=studio_light(instance.entity_number,transform.origin);
    input.studio_instances.push_back(instance);
  }
  if (input.studio_instances.empty()) {
    // A missing/unready named or legacy binding is not a malformed instance.
    // Keep the original binding reason, and preserve any pose/material error.
    if(had_ready_binding && last_shell_frame_status_==ReplayShellFrameStatus::binding_unavailable)
      last_shell_frame_status_=ReplayShellFrameStatus::no_valid_instances;
    return {};
  }
  last_shell_frame_status_=ReplayShellFrameStatus::frame_rejected;
  auto frame=r::EntityRenderFrameBuilder{}.build(*package_,std::move(input));
  if (!frame) return {};
  last_shell_frame_status_=ReplayShellFrameStatus::submitted;
  const auto count=frame.frame->studio_instances().size();
  return renderer::RenderDynamicEntities{package_,
      std::make_shared<const r::EntityRenderFrame>(std::move(*frame.frame)),
      {count,count,count,0U,0U}};
}

std::string_view to_string(ReplayShellFrameStatus status) noexcept {
  switch(status) {
  case ReplayShellFrameStatus::not_attempted:return "not_attempted";
  case ReplayShellFrameStatus::invalid_input:return "invalid_input";
  case ReplayShellFrameStatus::binding_unavailable:return "binding_unavailable";
  case ReplayShellFrameStatus::pose_rejected:return "pose_rejected";
  case ReplayShellFrameStatus::material_rejected:return "material_rejected";
  case ReplayShellFrameStatus::no_valid_instances:return "no_valid_instances";
  case ReplayShellFrameStatus::frame_rejected:return "frame_rejected";
  case ReplayShellFrameStatus::submitted:return "submitted";
  }
  return "unknown";
}

std::optional<renderer::RenderDynamicEntities>
RuntimeReplayLocalAssets::materialize_muzzle_sprites(std::span<const MuzzleSpriteFrame> sprites,double now) {
  if(sprites.empty() || sprites.size()>32 || !std::isfinite(now) || !package_)return {};
  r::EntityRenderFrameBuildInput input;
  input.resource_id=frame_id+5U;input.resource_revision=++muzzle_sprite_frame_revision_;
  input.interpolation={now,now,now,0.0F,input.resource_revision,input.resource_revision,
      r::EntityRenderInterpolationProfile::decoded_discrete_runtime_replay_v1};
  for(std::size_t i=0;i<sprites.size();++i) {
    const auto& request=sprites[i];
    const auto end=std::find(request.sprite.virtual_name.begin(),request.sprite.virtual_name.end(),'\0');
    if(end==request.sprite.virtual_name.end() || !std::isfinite(request.sprite.scale) ||
        request.sprite.scale<=0 || request.sprite.scale>1) {++muzzle_sprite_stats_.unavailable;continue;}
    const std::string_view name{request.sprite.virtual_name.data(),static_cast<std::size_t>(end-request.sprite.virtual_name.begin())};
    const auto found=effect_sprite_bindings_.find(name);
    if(found==effect_sprite_bindings_.end() || found->second>=package_->sprite_assets().size()) {
      ++muzzle_sprite_stats_.unavailable;continue;
    }
    const auto& asset=*package_->sprite_assets()[found->second];
    r::SpriteEntityRenderInstance instance;
    instance.entity_number=0xc1000000U+static_cast<std::uint32_t>(i);
    instance.sprite_asset_index=static_cast<std::uint32_t>(found->second);
    instance.selected_frame_index=request.sprite.frame_seed%static_cast<std::uint32_t>(asset.frames().size());
    instance.transform={request.origin,{},request.sprite.scale};
    if(!r::finite_entity_render_transform(instance.transform)){++muzzle_sprite_stats_.unavailable;continue;}
    instance.orientation=asset.orientation();instance.texture_format_support=asset.texture_support_status();
    instance.bounds=bounds({asset.bounds().minimum,asset.bounds().maximum},instance.transform);
    instance.blend_mode=r::SpriteBlendMode::additive;
    // Original palette supplies emitted RGB. Do not tint or statically light
    // the sprite; its black texels are additive-neutral, not white-key alpha.
    input.sprite_instances.push_back(instance);
  }
  if(input.sprite_instances.empty())return {};
  auto frame=r::EntityRenderFrameBuilder{}.build(*package_,std::move(input));
  if(!frame){++muzzle_sprite_stats_.frame_rejected;return {};}
  const auto count=frame.frame->sprite_instances().size();muzzle_sprite_stats_.submitted+=count;
  return renderer::RenderDynamicEntities{package_,
      std::make_shared<const r::EntityRenderFrame>(std::move(*frame.frame)),{count,count,0U,count,0U}};
}

std::optional<renderer::RenderDynamicEntities>
RuntimeReplayLocalAssets::materialize_temporary_sprites(std::span<const TemporarySpriteFrame> sprites,double now) {
  if(sprites.empty() || sprites.size()>160 || !std::isfinite(now) || !package_) return {};
  r::EntityRenderFrameBuildInput input;
  input.resource_id=frame_id+3U;input.resource_revision=++temporary_sprite_frame_revision_;
  input.interpolation={now,now,now,0.0F,input.resource_revision,input.resource_revision,
      r::EntityRenderInterpolationProfile::decoded_discrete_runtime_replay_v1};
  std::array<std::uint64_t,4> tag_submissions{};
  for(std::size_t i=0;i<sprites.size();++i) {
    const auto& frame=sprites[i];const auto& request=frame.sprite;
    const auto tag=frame.diagnostic_tag<temporary_sprite_tag_stats_.size() ? frame.diagnostic_tag : 0U;
    const auto count_reason=[&](auto member){++(temporary_sprite_stats_.*member);++(temporary_sprite_tag_stats_[tag].*member);};
    if(frame.identity.generation!=generation_ || !std::isfinite(frame.age_seconds) || frame.age_seconds<0 ||
        !std::isfinite(request.scale) || request.scale<=0 || request.scale>25.5F ||
        request.animation>game_api::TemporarySpriteAnimation::fixed ||
        (request.animation==game_api::TemporarySpriteAnimation::elapsed && !request.frames_per_second) ||
        request.blend>game_api::TemporarySpriteBlend::alpha_blended ||
        !std::isfinite(request.opacity) || request.opacity<0 || request.opacity>1 ||
        !std::ranges::all_of(request.tint,[](float v){return std::isfinite(v) && v>=0 && v<=1;}) ||
        !std::isfinite(request.vertical_speed_units) || std::abs(request.vertical_speed_units)>128 ||
        !std::isfinite(request.gravity_units) || request.gravity_units<0 || request.gravity_units>1024 ||
        !std::isfinite(request.maximum_lifetime_seconds) || request.maximum_lifetime_seconds<=0 ||
        request.maximum_lifetime_seconds>4 ||
        !std::isfinite(frame.velocity.x) || !std::isfinite(frame.velocity.y) || !std::isfinite(frame.velocity.z) ||
        std::abs(frame.velocity.x)>512 || std::abs(frame.velocity.y)>512 || std::abs(frame.velocity.z)>512) {
      count_reason(&TemporarySpriteStatistics::invalid);continue;
    }
    const auto binding=bindings_.find(request.model_slot);
    if(binding==bindings_.end() || !binding->second.render_index) {count_reason(&TemporarySpriteStatistics::absent);continue;}
    if(binding->second.status!=ReplayLocalVisualStatus::ready_sprite) {count_reason(&TemporarySpriteStatistics::unsupported);continue;}
    const auto& asset=*package_->sprite_assets()[*binding->second.render_index];
    if(asset.frames().empty()) {count_reason(&TemporarySpriteStatistics::unsupported);continue;}
    const double index=request.animation==game_api::TemporarySpriteAnimation::fixed ?
        static_cast<double>(request.frame_choice%asset.frames().size()) :
        std::floor(frame.age_seconds*request.frames_per_second);
    if(frame.age_seconds>=request.maximum_lifetime_seconds ||
        index>=static_cast<double>(asset.frames().size())) {count_reason(&TemporarySpriteStatistics::expired);continue;}
    const auto selected=static_cast<std::uint32_t>(index);
    const auto& source=asset.frames()[selected];
    if(source.profile==r::SpriteRenderTextureProfile::unsupported) {count_reason(&TemporarySpriteStatistics::unsupported);continue;}
    auto position=frame.origin;
    position.x+=frame.velocity.x*static_cast<float>(frame.age_seconds);
    position.y+=frame.velocity.y*static_cast<float>(frame.age_seconds);
    position.z+=frame.velocity.z*static_cast<float>(frame.age_seconds);
    if(request.gravity_units==0)
      position.z+=request.vertical_speed_units*static_cast<float>(frame.age_seconds);
    else
      position.z+=static_cast<float>(request.vertical_speed_units*frame.age_seconds-
          .5*request.gravity_units*frame.age_seconds*frame.age_seconds);
    const r::EntityRenderTransform transform{position,{},request.scale};
    if(!r::finite_entity_render_transform(transform)) {count_reason(&TemporarySpriteStatistics::invalid);continue;}
    r::SpriteEntityRenderInstance instance;
    instance.entity_number=0xd0000000U+static_cast<std::uint32_t>(i);
    instance.sprite_asset_index=static_cast<std::uint32_t>(*binding->second.render_index);
    instance.selected_frame_index=selected;instance.transform=transform;
    instance.orientation=asset.orientation();instance.texture_format_support=asset.texture_support_status();
    instance.bounds=bounds({asset.bounds().minimum,asset.bounds().maximum},transform);
    instance.blend_mode=request.blend==game_api::TemporarySpriteBlend::opaque ? r::SpriteBlendMode::opaque :
        request.blend==game_api::TemporarySpriteBlend::additive ? r::SpriteBlendMode::additive : r::SpriteBlendMode::alpha_blended;
    // Blend coverage does not imply a smoke fade. The selected module can
    // preserve the imported masked animation's intensity until its expiry.
    instance.opacity=request.opacity;
    instance.tint=request.tint;
    if(request.blend==game_api::TemporarySpriteBlend::alpha_blended && request.fade_with_animation &&
        request.animation==game_api::TemporarySpriteAnimation::elapsed)
      instance.opacity*=static_cast<float>(std::max(0.0,1.0-frame.age_seconds*request.frames_per_second/asset.frames().size()));
    input.sprite_instances.push_back(instance);
    ++tag_submissions[tag];
  }
  if(input.sprite_instances.empty()) return {};
  auto frame=r::EntityRenderFrameBuilder{}.build(*package_,std::move(input));
  if(!frame) {++temporary_sprite_stats_.frame_rejected;
    for(std::size_t tag=0;tag<tag_submissions.size();++tag)if(tag_submissions[tag])++temporary_sprite_tag_stats_[tag].frame_rejected;
    return {};}
  const auto count=frame.frame->sprite_instances().size();temporary_sprite_stats_.submitted+=count;
  for(std::size_t tag=0;tag<tag_submissions.size();++tag)temporary_sprite_tag_stats_[tag].submitted+=tag_submissions[tag];
  return renderer::RenderDynamicEntities{package_,
      std::make_shared<const r::EntityRenderFrame>(std::move(*frame.frame)),{count,count,0U,count,0U}};
}

std::shared_ptr<const goldsrc::collision::BrushCollisionScene>
RuntimeReplayLocalAssets::impact_brush_scene() const noexcept {
  const auto& observed=current_entities_;
  if(!camera_collision_ || !observed || observed->generation!=generation_)return {};
  if(impact_brush_generation_==generation_ && impact_brush_revision_==observed->publication_revision)
    return impact_brush_scene_;
  impact_brush_scene_.reset();
  impact_brush_generation_=generation_;impact_brush_revision_=observed->publication_revision;
  try {
    if(!placement_collision_library_) {
      const auto built=goldsrc::collision::build_brush_collision_model_library(camera_collision_);
      if(!built)return {};
      placement_collision_library_=built.library;
    }
    // Defaults intentionally omit player/entity boxes. Only explicitly
    // committed inline BSP solidity participates in this presentation query.
    impact_brush_scene_=goldsrc::build_reference_brush_collision(placement_collision_library_,
        summary_->model_names,*observed,generation_).scene;
  } catch(...) {impact_brush_scene_.reset();}
  return impact_brush_scene_;
}

game_api::PlacementTraceResult RuntimeReplayLocalAssets::placement_trace(
    const game_api::PlacementTraceRequest& request) const noexcept {
  game_api::PlacementTraceResult out;
  const auto& observed=placement_entities_ ? placement_entities_ : current_entities_;
  if(!camera_collision_ || !observed || observed->generation!=generation_ || request.generation!=generation_) return out;
  const auto distance=std::hypot(request.end.x-request.start.x,request.end.y-request.start.y,
      request.end.z-request.start.z);
  if(!std::isfinite(distance) || distance<=0 || distance>512) return out;
  try {
    if(!placement_collision_library_) {
      const auto built=goldsrc::collision::build_brush_collision_model_library(camera_collision_);
      if(!built) return out;
      placement_collision_library_=built.library;
    }
    const auto context=goldsrc::build_reference_brush_collision(placement_collision_library_,
        summary_->model_names,*observed,generation_,request.server_box_solid_code,
        request.ignore_entity,false);
    if(!context.scene) return out;
    auto boxes=context.boxes;
    // Protocol player hulls, not model dimensions or game/classname guesses.
    for(const auto& entity:observed->packet_entities) {
      if(!entity.player_movement_schema || entity.entity_number==request.ignore_entity ||
          entity.spectator==true || !request.player_solid_code) continue;
      if(!entity.solid) return out;
      if(entity.solid!=request.player_solid_code) continue;
      if(!entity.use_hull || *entity.use_hull>1 || !entity.origin.complete() ||
          boxes.size()>=goldsrc::movement::kMaximumServerEntityCollisionBoxes) return out;
      const auto hull=collision::standard_collision_hull_profile(*entity.use_hull==0 ?
          collision::CollisionHullOrdinal::standing_32x32x72 : collision::CollisionHullOrdinal::duck_32x32x36);
      if(!hull)return out;
      const assets::AssetVector3 origin{static_cast<float>(*entity.origin.x),
          static_cast<float>(*entity.origin.y),static_cast<float>(*entity.origin.z)};
      boxes.push_back({entity.entity_number,{{origin.x+hull->clip_mins.x,origin.y+hull->clip_mins.y,origin.z+hull->clip_mins.z},
          {origin.x+hull->clip_maxs.x,origin.y+hull->clip_maxs.y,origin.z+hull->clip_maxs.z}}});
    }
    const auto trace=goldsrc::movement::BrushSceneMovementCollision{context.scene,boxes}
        .trace_point(request.start,request.end,effect_trace_scratch_);
    if(!trace || !trace.result) return out;
    const auto& t=*trace.result;out.valid=true;out.start_solid=t.start_solid;
    out.all_solid=t.all_solid;out.fraction=t.fraction;out.point=t.end_position;
    if(t.hit) out.hit_entity=t.hit->source_entity_index.value_or(0U);
    if(t.collision_plane) out.normal=t.collision_plane->normal;
  } catch(...) {} return out;
}
game_api::StaticEffectTrace RuntimeReplayLocalAssets::trace(assets::AssetVector3 start,assets::AssetVector3 end) const noexcept {
  game_api::StaticEffectTrace out;
  if(!camera_collision_ || !current_entities_)return out;
  try {
    collision::CollisionTraceResult t;
    bool unsupported_target=false;
    if(current_entities_) {
      // Beam contacts use the same committed solid inline BSP scene as local
      // bullet/melee impacts. A failed reconstruction is unavailable, not an
      // empty scene that permits a beam to continue through a closed door.
      const auto brushes=impact_brush_scene();if(!brushes)return out;
      goldsrc::collision::BrushCollisionSceneTraceRequest request;request.start=start;request.end=end;
      const auto traced=goldsrc::collision::BrushCollisionSceneQuery{brushes}
          .trace_hull(request,effect_trace_scratch_);
      if(!traced)return out;t=traced.result->trace;
      if(t.hit && t.hit->kind!=collision::CollisionTraceHitKind::world) {
        const auto* identity=traced.result->scene_hit && traced.result->scene_hit->brush_instance ?
            &*traced.result->scene_hit->brush_instance : nullptr;
        const auto instance=identity ? std::ranges::find(brushes->instances(),*identity,
            &goldsrc::collision::BrushCollisionSceneInstance::identity) : brushes->instances().end();
        if(instance!=brushes->instances().end() && identity->source_entity_index &&
            world_scene_ && t.collision_plane) {
          const auto entity=*identity->source_entity_index,source_model=identity->source_model_index;
          const auto life=std::ranges::find_if(server_decal_lifetimes_,[&](const auto& value) {
            return value.entity_number==entity && value.source_model_index==source_model &&
                value.lifetime!=0U && value.model_slot!=0U;
          });
          const auto& models=world_scene_->brush_library().models();
          const auto model=std::ranges::find(models,source_model,
              &world_scene_render::BrushSubmodelRenderModel::source_model_index);
          if(life!=server_decal_lifetimes_.end() && model!=models.end() &&
              !model->render_surface_indices().empty()) {
            const auto point=goldsrc::brush_models::brush_rigid_world_to_local_point(instance->transform,t.end_position);
            const auto normal=goldsrc::brush_models::brush_rigid_world_to_local_vector(instance->transform,t.collision_plane->normal);
            const auto finite=[](assets::AssetVector3 v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
            const auto length=std::hypot(normal.x,normal.y,normal.z);
            if(finite(point) && finite(normal) && length>=.99F && length<=1.01F)
              out.surface_attachment=game_api::EffectSurfaceAttachment{generation_,life->lifetime,
                  entity,life->model_slot,source_model,point,normal};
          }
        }
        // Keep the real closest contact, but never treat an unmapped brush as
        // a supported world hit or fall through it to a farther static wall.
        unsupported_target=!out.surface_attachment;
      }
    }
    out.valid=true;out.start_solid=t.start_solid;out.all_solid=t.all_solid;
    out.hit=t.hit.has_value();out.point=t.end_position;
    if(t.collision_plane)out.normal=t.collision_plane->normal;
    out.unsupported=unsupported_target || t.in_liquid || t.end_contents.category==collision::CollisionContentsCategory::sky ||
      (t.blocking_contents && t.blocking_contents->category==collision::CollisionContentsCategory::sky);
  } catch(...) {}return out;
}
void RuntimeReplayLocalAssets::consume_follow(const client::CommittedTemporaryEffect& event,double now,
    const game_api::GameClientHost* game) noexcept {
  std::optional<game_api::EntityTrailProfile> profile;
  if(game && current_entities_) {
    const auto* entity=find_sorted_packet_entity(current_entities_->packet_entities,
        event.entity_attachment&0xfffU);
    if(entity && entity->model_index) {
      const auto model=summary_->model_names.find(*entity->model_index);
      const auto sprite=summary_->model_names.find(event.sprite_index);
      if(model!=summary_->model_names.end() && sprite!=summary_->model_names.end())
        profile=game->entity_trail_profile(model->second,sprite->second);
    }
  }
  std::uint16_t head_slot=0;
  if(profile && profile->head_sprite[0]) {
    const auto end=std::find(profile->head_sprite.begin(),profile->head_sprite.end(),'\0');
    const std::string_view name{profile->head_sprite.data(),static_cast<std::size_t>(end-profile->head_sprite.begin())};
    const auto named=std::ranges::find_if(summary_->model_names,[&](const auto& item){return item.second==name;});
    if(named!=summary_->model_names.end() && named->first<=1023) head_slot=static_cast<std::uint16_t>(named->first);
    else ++follow_resource_stats_.absent; // Independent optional head, not a smoke rejection.
  }
  entity_follow_.consume(event,now,profile,head_slot);
}
void RuntimeReplayLocalAssets::present_entity_effects(renderer::RenderScene& scene,double now,std::span<const game_api::BeamSpriteFrame> beams,
    std::optional<std::uint32_t> receiving_entity,std::optional<assets::AssetVector3> receiving_muzzle) {
  beam_light_count_=0;beam_light_frame_.reset();
  if(!std::isfinite(now)) return;
  std::vector<game_api::BeamSpriteFrame> current_beams;
  current_beams.reserve(192);
  current_beams.insert(current_beams.end(),beams.begin(),beams.begin()+std::min<std::size_t>(128,beams.size()));
  const auto observed_point=[](const client::RuntimeVector3Observation& v)->std::optional<assets::AssetVector3>{
    if(!v.complete())return {};return assets::AssetVector3{static_cast<float>(*v.x),static_cast<float>(*v.y),static_cast<float>(*v.z)};};
  const auto resolve=[&](const client::RuntimeBeamEndpoint& ref)->std::optional<assets::AssetVector3> {
    const auto* owner=find_sorted_packet_entity(presented_effect_entities_,ref.entity);
    if(!ref.entity || !owner || (owner->effects.value_or(0)&128))return {};
    if(ref.attachment==0)return observed_point(owner->origin); // explicit attachment-zero semantics only
    if(ref.attachment>4)return {};
    if(receiving_entity==ref.entity && receiving_muzzle)return receiving_muzzle;
    const auto a=presented_attachments_.find(ref.entity);
    if(a!=presented_attachments_.end() && a->second[ref.attachment-1])return a->second[ref.attachment-1];
    const auto pose=std::ranges::find(remote_weapon_poses(),ref.entity,&game_api::RemoteWeaponPoseSample::entity);
    const auto lifetime=player_lifetimes_.find(ref.entity);
    if(pose!=remote_weapon_poses().end() && lifetime!=player_lifetimes_.end() && pose->entity_lifetime==lifetime->second.identity &&
        pose->generation==generation_ && pose->current_model_slot==owner->weapon_model_index &&
        pose->source==game_api::RemoteSocketSource::validated_pmodel_socket && pose->semantic_match)
        return pose->muzzle.origin;
    return {}; // no silent feet/camera/Glock fallback
  };
  std::size_t server_count=0;
  std::erase_if(beam_endpoint_bindings_,[&](const auto& entry) {
    const auto& live=current_entities_ ? current_entities_->packet_entities : presented_effect_entities_;
    const auto* entity=find_sorted_packet_entity(live,entry.first);
    return !entity || !entity->custom_beam;
  });
  for(const auto& e:presented_effect_entities_)if(e.custom_beam) {
    if(current_entities_) {
      const auto* live=find_sorted_packet_entity(current_entities_->packet_entities,e.entity_number);
      if(!live || !live->custom_beam)continue;
    }
    const auto& b=*e.custom_beam;
    if(!b.supported || (b.flags&0xe0) || server_count==64){++follow_resource_stats_.unsupported;continue;}
    auto binding=beam_endpoint_bindings_.find(e.entity_number);
    if(binding==beam_endpoint_bindings_.end()) {
      if(beam_endpoint_bindings_.size()==64) {++follow_resource_stats_.unsupported;continue;}
      binding=beam_endpoint_bindings_.emplace(e.entity_number,BeamEndpointBinding{}).first;
    }
    auto& lifetime=binding->second;
    if(lifetime.type!=b.type || lifetime.start!=b.start_reference || lifetime.end!=b.end_reference ||
        (lifetime.broken && (lifetime.point!=e.origin || lifetime.sprite!=b.sprite_model_index))) {
      lifetime={};lifetime.type=b.type;lifetime.start=b.start_reference;lifetime.end=b.end_reference;
    }
    lifetime.point=e.origin;lifetime.sprite=b.sprite_model_index;
    const auto endpoint_alive=[&](const auto& ref,bool& bound,std::optional<std::uint32_t>& model,std::uint64_t& player_life) {
      const auto* owner=find_sorted_packet_entity(presented_effect_entities_,ref.entity);
      if(!ref.entity || !owner || !owner->model_index || !*owner->model_index ||
          (owner->effects.value_or(0)&128)) {
        // A selected module's explicit rejection retires even an unbound
        // receipt. Merely missing initial source data still waits for loading.
        // Broken receipts cannot attach to a replacement occupant unless the
        // server supplies a new beam/reference/geometry occurrence above.
        if(bound || std::ranges::find(unavailable_effect_sources_,ref.entity)!=unavailable_effect_sources_.end())
          lifetime.broken=true;
        return false;
      }
      const auto player=player_lifetimes_.find(ref.entity);
      const auto epoch=player==player_lifetimes_.end() ? 0U : player->second.identity;
      if(bound && (model!=owner->model_index || player_life!=epoch)) {lifetime.broken=true;return false;}
      bound=true;model=owner->model_index;player_life=epoch;return true;
    };
    const bool start_alive=b.type!=client::RuntimeCustomBeamObservation::Type::entities ||
        endpoint_alive(b.start_reference,lifetime.start_bound,lifetime.start_model,lifetime.start_player_life);
    const bool end_alive=b.type==client::RuntimeCustomBeamObservation::Type::points ||
        endpoint_alive(b.end_reference,lifetime.end_bound,lifetime.end_model,lifetime.end_player_life);
    if(lifetime.broken || !start_alive || !end_alive) {++follow_resource_stats_.unsupported;continue;}
    auto a=b.type==client::RuntimeCustomBeamObservation::Type::entities ? resolve(b.start_reference) : observed_point(e.origin);
    auto end=b.type==client::RuntimeCustomBeamObservation::Type::points ? observed_point(e.angles) : resolve(b.end_reference);
    if(!a || !end){++follow_resource_stats_.unsupported;continue;}
    game_api::BeamSpriteFrame out;out.action.generation=generation_;out.server_entity=e.entity_number;out.sprite_model_slot=b.sprite_model_index;
    out.start=*a;out.end=*end;out.width=static_cast<float>(b.width_units);out.opacity=b.opacity/255.F;
    out.color={b.color[0]/255.F,b.color[1]/255.F,b.color[2]/255.F};out.starts=now;out.expires=now+.1;out.persistent=true;
    out.noise_amplitude=static_cast<float>(b.noise_raw*.1);out.sine_noise=(b.flags&16)!=0;
    out.scroll_cycles_per_second=static_cast<float>(b.scroll_rate*.01);out.phase_seconds=now;
    if(const auto glow=beam_glow_profiles_.find(b.sprite_model_index);glow!=beam_glow_profiles_.end())out.ribbon_glow=glow->second;
    if(const auto light=beam_light_profiles_.find(b.sprite_model_index);light!=beam_light_profiles_.end())
      out.scene_light=light->second[out.sine_noise ? 1U : 0U];
    out.sprite_frame=static_cast<std::uint32_t>(std::floor(b.frame));current_beams.push_back(out);++server_count;
  }
  beams=current_beams;
  entity_follow_.present(presented_effect_entities_,scene,now);
  scene.follow_trail_entities.reset();
  if(!package_ || (entity_follow_.segments().empty() && entity_follow_.heads().empty() && beams.empty())) return;
  r::EntityRenderFrameBuildInput input;
  input.resource_id=frame_id+4U;input.resource_revision=++follow_frame_revision_;
  input.interpolation={now,now,now,0,input.resource_revision,input.resource_revision,
      r::EntityRenderInterpolationProfile::decoded_discrete_runtime_replay_v1};
  for(const auto group:{entity_follow_.segments(),entity_follow_.heads()}) for(const auto& segment:group) {
    const auto binding=bindings_.find(segment.sprite_slot);
    if(binding==bindings_.end() || !binding->second.render_index) {++follow_resource_stats_.absent;continue;}
    if(binding->second.status!=ReplayLocalVisualStatus::ready_sprite) {++follow_resource_stats_.unsupported;continue;}
    const auto& asset=*package_->sprite_assets()[*binding->second.render_index];
    if(asset.frames().empty() || asset.frames()[0].profile==r::SpriteRenderTextureProfile::unsupported) {++follow_resource_stats_.unsupported;continue;}
    r::SpriteEntityRenderInstance instance;
    instance.entity_number=0xc0000000U+static_cast<std::uint32_t>(input.sprite_instances.size());
    instance.sprite_asset_index=static_cast<std::uint32_t>(*binding->second.render_index);
    instance.orientation=asset.orientation();instance.texture_format_support=asset.texture_support_status();
    instance.transform={segment.start,{},1};instance.ribbon_end=segment.end;instance.ribbon_width=segment.width;
    if(segment.puff) {
      instance.selected_frame_index=static_cast<std::uint32_t>(std::min(asset.frames().size()-1,
          static_cast<std::size_t>(segment.age_fraction*asset.frames().size())));
      const auto& frame=asset.frames()[instance.selected_frame_index];
      if(frame.profile==r::SpriteRenderTextureProfile::unsupported) {++follow_resource_stats_.unsupported;continue;}
      const auto extent=std::max(frame.geometry.width,frame.geometry.height);
      if(!extent) {++follow_resource_stats_.unsupported;continue;}
      instance.transform.uniform_scale=segment.width/static_cast<float>(extent);
      instance.ribbon_end.reset();instance.ribbon_width=0;
    }
    const auto& a=segment.start;const auto& b=segment.end;const auto w=segment.width;
    instance.bounds={{std::min(a.x,b.x)-w,std::min(a.y,b.y)-w,std::min(a.z,b.z)-w},
        {std::max(a.x,b.x)+w,std::max(a.y,b.y)+w,std::max(a.z,b.z)+w}};
    instance.blend_mode=r::SpriteBlendMode::additive;instance.opacity=segment.opacity;instance.tint=segment.color;
    input.sprite_instances.push_back(instance);
  }
  for(const auto& beam:beams.first(std::min<std::size_t>(192,beams.size()))) {
    const auto finite_point=[](assets::AssetVector3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    if(beam.action.generation!=generation_ || !std::isfinite(beam.starts) || !std::isfinite(beam.expires) ||
      beam.starts>now || beam.expires<=now || (!beam.persistent && beam.expires-beam.starts>6.001) ||
      !std::isfinite(beam.width) || beam.width<=0 || beam.width>25.5F || !std::isfinite(beam.scale) || beam.scale<=0 || beam.scale>25.5F ||
      !finite_point(beam.start) || !finite_point(beam.end) ||
      !game_api::valid_world_line_geometry(beam.geometry) ||
      !game_api::valid_beam_ribbon_glow(beam.ribbon_glow) ||
      (beam.ribbon_glow.opacity>0 && (!beam.ribbon || beam.geometry!=game_api::WorldLineGeometry::flat_billboard)) ||
      !game_api::valid_beam_scene_light(beam.scene_light) ||
      (beam.scene_light.intensity>0 && (!beam.ribbon || beam.geometry!=game_api::WorldLineGeometry::flat_billboard)) ||
      !std::isfinite(beam.opacity) || beam.opacity<0 || beam.opacity>1 ||
      !std::ranges::all_of(beam.color,[](float v){return std::isfinite(v)&&v>=0&&v<=1;})) {++follow_resource_stats_.unsupported;continue;}
    auto surface_point=beam.start;
    auto surface_normal=beam.surface_normal;
    if(beam.surface_attachment) {
      const auto& target=*beam.surface_attachment;
      const auto* frame=scene.static_world ? scene.static_world->runtime_brushes.get() : nullptr;
      if(!surface_normal || beam.ribbon || beam.server_entity ||
          beam.velocity.x!=0 || beam.velocity.y!=0 || beam.velocity.z!=0 ||
          target.generation!=generation_ || !target.entity_lifetime || !target.entity ||
          !target.model_slot || !target.source_model_index ||
          !finite_point(target.local_point) || !finite_point(target.local_normal) ||
          std::abs(std::hypot(target.local_normal.x,target.local_normal.y,target.local_normal.z)-1)>.01F ||
          !frame || !world_scene_ || frame->generation!=generation_ ||
          frame->scene_identity!=world_scene_->resource_identity() || frame->instances.size()>4096U ||
          std::ranges::none_of(server_decal_lifetimes_,[&](const auto& live) {
            return live.entity_number==target.entity && live.lifetime==target.entity_lifetime &&
                live.model_slot==target.model_slot && live.source_model_index==target.source_model_index;
          })) {++follow_resource_stats_.unsupported;continue;}
      const auto current=std::ranges::find_if(frame->instances,[&](const auto& instance) {
        return instance.entity_number==target.entity && instance.model_slot==target.model_slot &&
            instance.source_model_index==target.source_model_index;
      });
      if(current==frame->instances.end() || !rigid_effect_surface_transform(current->model_transform)) {
        ++follow_resource_stats_.unsupported;continue;
      }
      // Resolve the same presented rigid pose as the visible door. The owning
      // local contact never borrows the collision scene or follows a reused slot.
      const auto p=renderer::transform(current->model_transform,
          {target.local_point.x,target.local_point.y,target.local_point.z,1});
      const auto n=renderer::transform(current->model_transform,
          {target.local_normal.x,target.local_normal.y,target.local_normal.z,0});
      surface_point={p.x,p.y,p.z};surface_normal=assets::AssetVector3{n.x,n.y,n.z};
      if(!finite_point(surface_point) || !finite_point(*surface_normal) ||
          !std::isfinite(p.w) || std::abs(p.w-1)>1e-5F || !std::isfinite(n.w) || std::abs(n.w)>1e-5F) {
        ++follow_resource_stats_.unsupported;continue;
      }
    }
    if(beam.geometry==game_api::WorldLineGeometry::solid_3d) {
      const bool stationary=beam.travel_speed_units_per_second==0 && beam.segment_length_units==0;
      if(!beam.ribbon || beam.surface_normal || beam.width>6 ||
          !std::isfinite(beam.emission_gain) || beam.emission_gain<0 || beam.emission_gain>4 ||
          !std::isfinite(beam.head_taper_fraction) || beam.head_taper_fraction<0 || beam.head_taper_fraction>1 ||
          !std::isfinite(beam.taper_end_diameter_fraction) || beam.taper_end_diameter_fraction<0 || beam.taper_end_diameter_fraction>1 ||
          (beam.motion_starts && (!std::isfinite(*beam.motion_starts) || *beam.motion_starts<beam.starts || *beam.motion_starts>now || *beam.motion_starts>=beam.expires)) ||
          !game_api::valid_transient_glow(beam.head_glow) ||
          (!stationary && (!std::isfinite(beam.travel_speed_units_per_second) || beam.travel_speed_units_per_second<=0 ||
              beam.travel_speed_units_per_second>100000 || !std::isfinite(beam.segment_length_units) ||
              beam.segment_length_units<=0 || beam.segment_length_units>1024))) {++follow_resource_stats_.unsupported;continue;}
      const assets::AssetVector3 delta{beam.end.x-beam.start.x,beam.end.y-beam.start.y,beam.end.z-beam.start.z};
      const auto distance=std::hypot(delta.x,delta.y,delta.z);
      if(!std::isfinite(distance) || distance<.002F || distance>10000) {++follow_resource_stats_.unsupported;continue;}
      auto start=beam.start,end=beam.end;auto opacity=beam.opacity;bool head_clipped_to_endpoint=false;
      if(!stationary) {
        // Same immutable launch / moving-tail rule as the existing 3D streak.
        // Rendering never retraces, follows a later muzzle or creates impacts.
        const auto back=static_cast<double>(beam.travel_speed_units_per_second)*(now-beam.motion_starts.value_or(beam.starts));
        const auto front=std::min<double>(distance,back+beam.segment_length_units);
        if(back>=front) continue;
        head_clipped_to_endpoint=front>=distance;
        const auto point=[&](double along) {const auto t=static_cast<float>(along/distance);return assets::AssetVector3{
            beam.start.x+delta.x*t,beam.start.y+delta.y*t,beam.start.z+delta.z*t};};
        start=point(back);end=point(front);
        opacity*=static_cast<float>((beam.expires-now)/(beam.expires-beam.starts));
      }
      if(scene.world_line_count>=scene.world_lines.size()) {++follow_resource_stats_.unsupported;continue;}
      scene.world_lines[scene.world_line_count++]={start,end,beam.width,
          {beam.color[0],beam.color[1],beam.color[2],opacity},beam.head_taper_fraction,
          beam.geometry,beam.taper_end_diameter_fraction,beam.emission_gain};
      // At the traced endpoint, the surface-aligned contact sprite owns the
      // glow. A camera-facing head flash there would cut through the wall again.
      if(!head_clipped_to_endpoint && beam.head_glow.radius_units>0 && scene.world_flash_count<scene.world_flashes.size())
        scene.world_flashes[scene.world_flash_count++]={end,beam.head_glow.radius_units,
            {beam.color[0],beam.color[1],beam.color[2],opacity*.45F}};
      if(beam.head_glow.light_radius_units>0 && scene.world_effect_light_count<scene.world_effect_lights.size())
        scene.world_effect_lights[scene.world_effect_light_count++]={end,beam.head_glow.light_radius_units,
            beam.head_glow.light_intensity*opacity,beam.color};
      ++follow_resource_stats_.submitted;
      continue; // Solid geometry does not require a sprite/texture slot.
    }
    const auto end=std::find(beam.virtual_sprite.begin(),beam.virtual_sprite.end(),'\0');
    const std::string_view name{beam.virtual_sprite.data(),static_cast<std::size_t>(end-beam.virtual_sprite.begin())};
    const auto named=std::ranges::find_if(summary_->model_names,[&](const auto& pair){return pair.second==name;});
    if(!beam.sprite_model_slot && named==summary_->model_names.end()) {++follow_resource_stats_.absent;continue;}
    const auto binding=bindings_.find(beam.sprite_model_slot.value_or(named!=summary_->model_names.end() ? named->first : 0));
    if(binding==bindings_.end() || !binding->second.render_index || binding->second.status!=ReplayLocalVisualStatus::ready_sprite) {++follow_resource_stats_.absent;continue;}
    const auto& asset=*package_->sprite_assets()[*binding->second.render_index];
    if(asset.frames().empty() || asset.frames()[0].profile==r::SpriteRenderTextureProfile::unsupported) {++follow_resource_stats_.unsupported;continue;}
    r::SpriteEntityRenderInstance instance;instance.entity_number=0xc0001000U+static_cast<std::uint32_t>(input.sprite_instances.size());
    if(beam.persistent && !beam.server_entity) {
      // Renderer-local logical slots, never advertised/network entity IDs.
      // A persistent owner's phase must not change when preceding finite
      // effects expire or change their array positions.
      if(beam.action.emitter_entity>32 || beam.action.sub_identity>255){++follow_resource_stats_.unsupported;continue;}
      instance.entity_number=0xc1000000U|(beam.action.emitter_entity<<8)|beam.action.sub_identity;
    }
    if(beam.server_entity)instance.entity_number=*beam.server_entity;
    instance.sprite_asset_index=static_cast<std::uint32_t>(*binding->second.render_index);
    instance.orientation=asset.orientation();instance.texture_format_support=asset.texture_support_status();
    instance.transform={beam.start,{},beam.scale};instance.opacity=beam.opacity;instance.tint=beam.color;instance.blend_mode=r::SpriteBlendMode::additive;
    instance.selected_frame_index=static_cast<std::uint32_t>(beam.sprite_frame%asset.frames().size());
    if(surface_normal) {
      const auto normal=*surface_normal;
      const auto length=std::hypot(normal.x,normal.y,normal.z);
      if(beam.ribbon || !finite_point(normal) || length<.99F || length>1.01F ||
          beam.velocity.x!=0 || beam.velocity.y!=0 || beam.velocity.z!=0) {++follow_resource_stats_.unsupported;continue;}
      const assets::AssetVector3 n{normal.x/length,normal.y/length,normal.z/length};
      // A depth-tested glow belongs to the traced surface plane, not to a
      // camera-facing quad cutting through it. Preserve true wall occlusion.
      constexpr float degrees=57.29577951308232F;
      instance.orientation=assets::SpriteOrientation::oriented;
      instance.surface_aligned=true;
      instance.transform.rotation_degrees={0,std::acos(std::clamp(n.z,-1.0F,1.0F))*degrees,
          std::atan2(n.y,n.x)*degrees};
      instance.transform.origin={surface_point.x+n.x*.125F,surface_point.y+n.y*.125F,surface_point.z+n.z*.125F};
    }
    instance.bounds=bounds({asset.bounds().minimum,asset.bounds().maximum},instance.transform);
    if(beam.ribbon) {
      instance.ribbon_noise_amplitude=beam.noise_amplitude;instance.ribbon_scroll_cycles_per_second=beam.scroll_cycles_per_second;
      instance.ribbon_sine_noise=beam.sine_noise;instance.ribbon_phase_seconds=beam.phase_seconds;
      instance.ribbon_glow_width_scale=beam.ribbon_glow.width_scale;instance.ribbon_glow_opacity=beam.ribbon_glow.opacity;
      instance.ribbon_end=beam.end;instance.ribbon_width=beam.width;const auto a=beam.start,b=beam.end;
      const auto w=beam.width*(beam.ribbon_glow.opacity>0 ? beam.ribbon_glow.width_scale : 1.F)+
          r::beam_ribbon_radius(beam.noise_amplitude,beam.sine_noise);
      instance.bounds={{std::min(a.x,b.x)-w,std::min(a.y,b.y)-w,std::min(a.z,b.z)-w},{std::max(a.x,b.x)+w,std::max(a.y,b.y)+w,std::max(a.z,b.z)+w}};
    }
    if(!r::finite_entity_render_transform(instance.transform)) {++follow_resource_stats_.unsupported;continue;}
    input.sprite_instances.push_back(instance);
    if(beam.scene_light.intensity>0 && beam.opacity>0) {
      const auto& profile=beam.scene_light;
      const double dx=double(beam.end.x)-beam.start.x,dy=double(beam.end.y)-beam.start.y,dz=double(beam.end.z)-beam.start.z;
      const auto length=std::hypot(dx,dy,dz);
      if(std::isfinite(length) && length>0 && finite_point(scene.camera.position)) {
        const auto samples=static_cast<unsigned>(std::clamp(std::ceil(length/profile.spacing_units),1.,4.));
        const auto inset=std::min(length*.5,double(profile.radius_units)*.25);
        const auto distance_squared=[&](const auto& light) {
          const auto& eye=scene.camera.position;
          return std::pow(double(light.center.x)-eye.x,2)+std::pow(double(light.center.y)-eye.y,2)+std::pow(double(light.center.z)-eye.z,2);
        };
        for(unsigned sample=0;sample<samples;++sample) {
          // Symmetric: local beams run muzzle->contact, server beams may run
          // contact->muzzle. Keep light outside both ends of the struck wall.
          const auto along=samples==1 ? length*.5 : inset+(length-2*inset)*sample/(samples-1);
          const auto t=along/length;
          renderer::RenderPointLight light{{static_cast<float>(beam.start.x+dx*t),
              static_cast<float>(beam.start.y+dy*t),static_cast<float>(beam.start.z+dz*t)},
              profile.radius_units,profile.intensity*(profile.inherit_beam_opacity ? beam.opacity : 1.F),
              profile.color_override.value_or(beam.color)};
          if(!renderer::valid_render_point_light(light))continue;
          const auto active=std::span{beam_lights_}.first(beam_light_count_);
          if(std::ranges::any_of(active,[&](const auto& old) {
              return std::abs(old.center.x-light.center.x)<.01F && std::abs(old.center.y-light.center.y)<.01F &&
                  std::abs(old.center.z-light.center.z)<.01F && old.color==light.color &&
                  old.radius_units==light.radius_units && old.intensity==light.intensity;
          }))continue;
          if(beam_light_count_<beam_lights_.size())beam_lights_[beam_light_count_++]=light;
          else {
            const auto worst=std::ranges::max_element(beam_lights_,{},distance_squared);
            if(distance_squared(light)<distance_squared(*worst))*worst=light;
          }
        }
      }
    }
  }
  if(input.sprite_instances.empty()) return;
  auto frame=r::EntityRenderFrameBuilder{}.build(*package_,std::move(input));
  if(!frame) {beam_light_count_=0;++follow_resource_stats_.frame_rejected;return;}
  const auto count=frame.frame->sprite_instances().size();
  follow_resource_stats_.submitted+=count;
  scene.follow_trail_entities=renderer::RenderDynamicEntities{package_,
      std::make_shared<const r::EntityRenderFrame>(std::move(*frame.frame)),{count,count,0,count,0}};
  beam_light_frame_=scene.follow_trail_entities->frame;
}

void RuntimeReplayLocalAssets::append_beam_lights(renderer::RenderScene& scene) noexcept {
  const auto count=std::exchange(beam_light_count_,0U);
  const auto frame=beam_light_frame_.lock();beam_light_frame_.reset();
  if(!frame || !scene.follow_trail_entities || scene.follow_trail_entities->frame!=frame)return;
  auto lights=std::span{beam_lights_}.first(count);
  const auto& eye=scene.camera.position;
  if(!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z))return;
  std::ranges::sort(lights,{},[&](const auto& light) {
    const double x=double(light.center.x)-eye.x,y=double(light.center.y)-eye.y,z=double(light.center.z)-eye.z;
    return x*x+y*y+z*z;
  });
  for(const auto& light:lights) {
    if(scene.world_effect_light_count==scene.world_effect_lights.size())break;
    if(scene.world_effect_light_count>scene.world_effect_lights.size())return;
    scene.world_effect_lights[scene.world_effect_light_count++]=light;
  }
}

std::string_view to_string(ReplayLocalVisualStatus status) noexcept {
  switch (status) {
  case ReplayLocalVisualStatus::ready_brush: return "ready_brush";
  case ReplayLocalVisualStatus::invalid_brush_reference: return "invalid_brush_reference";
  case ReplayLocalVisualStatus::missing_brush_geometry: return "missing_brush_geometry";
  case ReplayLocalVisualStatus::unsupported_brush_transform: return "unsupported_brush_transform";
  case ReplayLocalVisualStatus::ready_studio:
    return "ready_studio";
  case ReplayLocalVisualStatus::ready_sprite:
    return "ready_sprite";
  case ReplayLocalVisualStatus::absent_model:
    return "absent_model";
  case ReplayLocalVisualStatus::unknown_model_slot:
    return "unknown_model_slot";
  case ReplayLocalVisualStatus::inline_brush_unsupported:
    return "inline_brush_unsupported";
  case ReplayLocalVisualStatus::missing_asset:
    return "missing_asset";
  case ReplayLocalVisualStatus::unsupported_asset:
    return "unsupported_asset";
  case ReplayLocalVisualStatus::unsupported_schema:
    return "unsupported_schema";
  case ReplayLocalVisualStatus::incomplete_transform:
    return "incomplete_transform";
  case ReplayLocalVisualStatus::unsupported_render_mode:
    return "unsupported_render_mode";
  case ReplayLocalVisualStatus::hidden_by_effects:
    return "hidden_by_effects";
  case ReplayLocalVisualStatus::unsupported_pose:
    return "unsupported_pose";
  case ReplayLocalVisualStatus::unsupported_material:
    return "unsupported_material";
  case ReplayLocalVisualStatus::unsafe_asset:
    return "unsafe_asset";
  case ReplayLocalVisualStatus::ambiguous_asset:
    return "ambiguous_asset";
  case ReplayLocalVisualStatus::dependency_missing:
    return "dependency_missing";
  case ReplayLocalVisualStatus::import_failed:
    return "import_failed";
  }
  return "unknown";
}
} // namespace hlclient::app
