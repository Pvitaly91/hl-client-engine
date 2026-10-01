#include <algorithm>
#include <cmath>
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>
#include <hlclient/world_visibility/world_view_frustum.hpp>
#include <hlclient/entity_render/entity_render_frame_composer.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_render_library.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_entity.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp>
#include <hlclient/goldsrc/spatial/goldsrc_spatial_package_builder.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>
#include <hlclient/goldsrc/local_resource_inventory.hpp>
#include <hlclient/goldsrc/precache_asset_dispatch.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>
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

namespace hlclient::app {
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
    status=LocalImpactDecalStatus::ready;
  } catch (...) { status=LocalImpactDecalStatus::rejected; }
  return status;
}
namespace {
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
      const auto named=summary_.model_names.find(*entity.model_index);
      if(named!=summary_.model_names.end() && named->second.starts_with("*") &&
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
    sources.push_back({entity.entity_number,position});
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

g::ApprovedAssetSource open_source(
    const g::PrecacheManifestState &manifest,
    const g::PrecacheManifestEntry &entry,
    const std::shared_ptr<const local_resources::LocalResourceEnvironment>
        &environment) {
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
    opened.operation->update(std::chrono::steady_clock::now());
  }
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
                                 const std::string_view game) {
  return create(capture.resources(), capture.server_info(), basedir, game,
                ReplayLocalCameraPolicy::offline_spectator);
}

ReplayLocalAssetsCreateResult
RuntimeReplayLocalAssets::create(const g::ResourceListState &resources,
                                 const g::ServerInfoState &server_info,
                                 const std::filesystem::path &basedir,
                                 const std::string_view game,
                                 const ReplayLocalCameraPolicy camera_policy) {
  try {
    auto result =
        std::unique_ptr<RuntimeReplayLocalAssets>{new RuntimeReplayLocalAssets};
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
    if (!inventory) {
      throw std::runtime_error{"captured resource inventory rejected"};
    }
    auto manifest = g::PrecacheManifestBuilder{}.build(
        resources, *inventory.state, server_info, mapper, *environment);
    if (!manifest || !manifest.state->world_geometry_ready() ||
        !manifest.state->world_entry()) {
      throw std::runtime_error{"captured map resource is not locally ready"};
    }
    auto &summary = result->summary_;
    result->maximum_clients_=server_info.maximum_clients().value();
    result->sound_resources_={std::make_shared<const g::PrecacheManifestState>(*manifest.state),environment};
    result->movement_materials_=read_movement_materials(environment,*manifest.state->world_entry());
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
      if (!entry.local_file_size() || *entry.local_file_size() != code) {
        throw std::runtime_error{
            "captured/local resource size mismatch at model slot " +
            std::to_string(entry.resource_index())};
      }
      ++summary.size_matches;
    };
    check_size(*manifest.state->world_entry());
    assets::AssetImporterRegistries registries;
    if (!g::register_builtin_asset_importers(
            registries, {}, g::bsp::GoldSrcBspParseOptions{true})) {
      throw std::runtime_error{"builtin importer registration failed"};
    }
    auto world_source = open_source(
        *manifest.state, *manifest.state->world_entry(), environment);
    auto world_plan = g::AssetDispatchPlanBuilder{}.build(
        *manifest.state, *manifest.state->world_entry());
    auto imported = g::ApprovedAssetImporterDispatcher{registries}.dispatch(
        world_source, *world_plan.plan);
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
    if (!collision) {
      throw std::runtime_error{collision.error->context};
    }
    result->camera_collision_ = collision.package;
    g::GoldSrcWorldTextureImportLimits texture_limits;
    texture_limits.missing_texture_policy = g::MissingWorldTexturePolicy::placeholder_for_absent_name;
    auto textures = g::WorldTextureImportOperation::begin(
        world, world_source.source().bytes(), environment, texture_limits);
    if (!textures) {
      throw std::runtime_error{"world texture import rejected"};
    }
    for (std::size_t i = 0; i < 100000U && !textures.operation->terminal();
         ++i) {
      textures.operation->update(std::chrono::steady_clock::now());
    }
    auto texture_set = textures.operation->take_result();
    if (!texture_set) {
      throw std::runtime_error{textures.operation->error()
                                   ? textures.operation->error()->context
                                   : "world textures unavailable"};
    }
    summary.missing_texture_placeholder_bindings = texture_set->statistics().placeholder_material_count;
    auto lightmaps = g::lightmaps::GoldSrcWorldLightmapImporter::import(
        world, world_source.source().bytes());
    if (!lightmaps) {
      throw std::runtime_error{lightmaps.error->context};
    }
    auto world_package = world_render::WorldRenderPackageBuilder{}.build(
        {std::move(world), std::move(*texture_set)},
        std::move(*lightmaps.lightmap_set));
    if (!world_package) {
      throw std::runtime_error{world_package.error->context};
    }
    result->world_ = std::make_shared<const world_render::WorldRenderPackage>(
        std::move(*world_package.package));

    const auto& document = attachment->document();
    g::brush_models::GoldSrcBrushRenderLibraryLimits brush_limits;
    brush_limits.textures = texture_limits;
    auto brushes = g::brush_models::GoldSrcBrushRenderLibraryBuilder::build(
        document, world_source.source().bytes(), environment, brush_limits);
    if (!brushes) throw std::runtime_error{"brush geometry preparation: " +
        std::string{g::brush_models::to_string(brushes.error->code)}};
    summary.missing_texture_placeholder_bindings +=
        static_cast<std::size_t>(brushes.statistics.placeholder_material_count);
    const auto& spatial = document.spatial_source;
    auto spatial_package = g::spatial::GoldSrcSpatialPackageBuilder::build({
        spatial.planes, spatial.nodes, spatial.leaves,
        spatial.marksurface_face_ordinals, spatial.visibility_bytes,
        spatial.world_model, spatial.source_face_count,
        document.world_asset.surfaces, spatial.submodel_face_ordinals});
    if (!spatial_package) throw std::runtime_error{"brush spatial preparation failed"};
    auto scene = world_scene_render::WorldSceneRenderPackageBuilder{}.build(
        result->world_, std::move(*spatial_package.package),
        std::move(*brushes.library)); // No BSP metadata instances in the live scene.
    if (!scene) throw std::runtime_error{scene.error->context};
    result->world_scene_ = std::make_shared<const world_scene_render::WorldSceneRenderPackage>(
        std::move(*scene.package));
    summary.prepared_brush_models = result->world_scene_->brush_library().models().size();

    std::vector<v::EntityVisualModelReference> references;
    for (const auto &entry : manifest.state->entries()) {
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
    for (std::size_t first = 0; first < references.size();
         first += batch_size) {
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
        auto source = open_source(
            *manifest.state,
            manifest.state->entries()[request.manifest_entry_offset()],
            environment);
        auto operation =
            g::visual_assets::GoldSrcVisualAssetImportOperation::begin(
                source, environment, registries);
        if (!operation) {
          throw std::runtime_error{"visual importer begin failed"};
        }
        for (std::size_t i = 0; i < 65536U && !operation.operation->terminal();
             ++i) {
          operation.operation->update(std::chrono::steady_clock::now());
        }
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
    r::EntitySceneRenderPackageCreateInfo package;
    package.asset_library = result->library_;
    package.asset_library_identity = {result->library_->resource_id(),
                                      result->library_->resource_revision()};
    package.resource_id = package_id;
    std::vector<Binding> record_bindings;
    for (std::size_t i = 0; i < result->library_->records().size(); ++i) {
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
      result->bindings_[reference.reference.value()] =
          record_bindings.at(reference.asset_library_index);
    }
    summary.imported_studio = package.studio_assets.size();
    summary.imported_sprites = package.sprite_assets.size();
    auto built = r::EntitySceneRenderPackageBuilder{}.build(std::move(package));
    if (!built) {
      throw std::runtime_error{built.error->context};
    }
    result->package_ = std::make_shared<const r::EntitySceneRenderPackage>(
        std::move(*built.package));
    for (const auto& [index,name] : summary.model_names) {
      if (name != "models/shell.mdl") continue;
      const auto bound = result->bindings_.find(index);
      if (bound != result->bindings_.end()) {
        result->shell_resource_status_ = bound->second.status;
        if (bound->second.status == ReplayLocalVisualStatus::ready_studio)
          result->shell_model_index_ = index;
      }
      break;
    }
    return {std::move(result), {}};
  } catch (const std::exception &error) {
    return {{}, visual_error(error.what())};
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
    target = std::move(candidate);
    previous_entities_.reset(); clock_record_=0U; light_cache_.clear();
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
  if (const auto found=player_lifetimes_.find(entity);found!=player_lifetimes_.end()) {
    found->second.identity=++next_player_lifetime_;
    found->second.continuity_floor=current_entities_ && current_entities_->entity_metadata.source ?
        current_entities_->entity_metadata.source->record_ordinal : 0U;
  }
  light_cache_.erase(entity);
}

void RuntimeReplayLocalAssets::invalidate_player_continuity() noexcept {
  for (auto& [entity,lifetime]:player_lifetimes_) {
    (void)entity;
    lifetime.identity=++next_player_lifetime_;
    lifetime.continuity_floor=current_entities_ && current_entities_->entity_metadata.source ?
        current_entities_->entity_metadata.source->record_ordinal : 0U;
  }
  previous_entities_.reset();
  clock_record_=0U;
  light_cache_.clear();
}

RuntimeReplayVisualProjectionResult RuntimeReplayLocalAssets::present_entities(
    game_api::GameClientHost& game, double application_seconds,
    std::optional<std::uint32_t> receiving_entity, client::ClientWorldState& target,
    renderer::RenderExtent extent) {
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
  const auto record=current_entities_->publication_revision;
  if(clock_record_!=record) {clock_record_=record;clock_anchor_seconds_=application_seconds;}
  const auto& previous=previous_entities_ ? *previous_entities_ : *current_entities_;
  // A bounded two-observation owner cannot cover a fixed 100 ms delay at
  // high RX cadence. Adapt the delay to this actual pair, without extrapolation.
  const double pair_duration=std::max(0.0,*current_entities_->server_time_seconds-
      previous.server_time_seconds.value_or(*current_entities_->server_time_seconds));
  const double delay=std::min(policy.interpolation_delay_seconds,pair_duration);
  const double elapsed=std::clamp(application_seconds-clock_anchor_seconds_,0.0,
      delay);
  const double target_seconds=*current_entities_->server_time_seconds-
      delay+elapsed;
  g::RuntimeEntityInterpolationOptions options;
  options.limits.maximum_snapshot_gap_seconds=policy.maximum_observation_gap_seconds;
  options.teleport_distance=policy.teleport_distance_units;
  options.no_interpolation_effect_mask=policy.no_interpolation_effect_mask;
  std::array<std::uint32_t,game_api::kMaximumRemotePlayers> discontinuous{};
  std::size_t discontinuous_count{};
  const auto previous_record=previous.entity_metadata.source ? previous.entity_metadata.source->record_ordinal : 0U;
  for(const auto& [entity,lifetime]:player_lifetimes_) {
    if(lifetime.continuity_floor && previous_record<=lifetime.continuity_floor && discontinuous_count<discontinuous.size())
      discontinuous[discontinuous_count++]=entity;
  }
  options.discontinuous_entities=std::span{discontinuous}.first(discontinuous_count);
  auto sampled=g::EntitySnapshotInterpolator{}.interpolate_runtime(previous,
      *current_entities_,target_seconds,options);
  if (!sampled) return {false,false,visual_error(sampled.error->context)};
  sampled_game_=&game; sampled_receiving_entity_=receiving_entity; sampled_extent_=extent;
  sampled_server_seconds_=sampled.sample_seconds;
  sampled_previous_seconds_=previous.server_time_seconds.value_or(sampled.sample_seconds);
  sampled_current_seconds_=*current_entities_->server_time_seconds; sampled_alpha_=sampled.alpha;
  if(sampled.selection_status==g::EntitySnapshotPairSelectionStatus::held_only) {
    sampled_previous_seconds_=sampled_current_seconds_; sampled_alpha_=0.0;
  }
  auto result=project_entities(*sampled.state,target);
  sampled_game_=nullptr; sampled_extent_.reset(); sampled_receiving_entity_.reset();
  return result;
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
          sampled_previous_seconds_!=sampled_current_seconds_ && previous_entities_ ?
              (previous_entities_->entity_metadata.source ? previous_entities_->entity_metadata.source->record_identity :
                  client::runtime_observation_visual_hash_v2(*previous_entities_)) :
              (current_entities_->entity_metadata.source ? current_entities_->entity_metadata.source->record_identity :
                  client::runtime_observation_visual_hash_v2(*current_entities_)),
          current_entities_->entity_metadata.source ? current_entities_->entity_metadata.source->record_identity :
              client::runtime_observation_visual_hash_v2(*current_entities_),
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
    auto next = summary_;
    next.remote_player_count=0U;
    if(sampled_game_) {
      next.remote_previous_seconds=sampled_previous_seconds_; next.remote_current_seconds=sampled_current_seconds_;
      next.remote_sample_seconds=sampled_server_seconds_; next.remote_alpha=sampled_alpha_;
    }
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
      ++next.coverage[reason];
      ++next.unsupported_instances;
      const auto entity = std::ranges::find(observation.packet_entities, number,
          &client::RuntimePacketEntityObservation::entity_number);
      if (!next.first_brush_rejection && entity != observation.packet_entities.end() && entity->model_index) {
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
      if (sampled_game_ && entity.player_movement_schema && sampled_receiving_entity_ &&
          entity.entity_number==*sampled_receiving_entity_) continue;
      if (!entity.ordinary_visual_schema) {
        unavailable(entity.entity_number,
                    ReplayLocalVisualStatus::unsupported_schema);
        continue;
      }
      if (!entity.model_index || *entity.model_index == 0U) {
        unavailable(entity.entity_number,
                    ReplayLocalVisualStatus::absent_model);
        continue;
      }
      const auto found = bindings_.find(*entity.model_index);
      if (found == bindings_.end()) {
        unavailable(entity.entity_number,
                    ReplayLocalVisualStatus::unknown_model_slot);
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
        ++next.coverage[ReplayLocalVisualStatus::ready_brush];
        ++next.rendered_model_slots[*entity.model_index];
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
      if (entity.render_mode.value_or(0U) != 0U) {
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
                                         1.0F};
      if (!r::finite_entity_render_transform(transform)) {
        return {false, false, visual_error("nonfinite local entity transform")};
      }
      const auto &record = library_->records()[*binding.library_index];
      if (binding.status == ReplayLocalVisualStatus::ready_studio) {
        const auto &model = *record.model_asset()->skeletal_data;
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
            std::to_string(record.resource_id()),record.source_fingerprint()};
        std::optional<g::studio::StudioPoseEvaluationResult> pose_result;
        bool player_pose=false;
        if (sampled_game_ && entity.player_movement_schema) {
          const auto current=std::ranges::find(current_entities_->packet_entities,
              entity.entity_number,&client::RuntimePacketEntityObservation::entity_number);
          const client::RuntimePacketEntityObservation* previous=nullptr;
          if (previous_entities_) {
            const auto found_previous=std::ranges::find(previous_entities_->packet_entities,
                entity.entity_number,&client::RuntimePacketEntityObservation::entity_number);
            if (found_previous!=previous_entities_->packet_entities.end()) previous=&*found_previous;
          }
          const auto lifetime=player_lifetimes_.find(entity.entity_number);
          if (current==current_entities_->packet_entities.end() || lifetime==player_lifetimes_.end()) {
            unavailable(entity.entity_number,ReplayLocalVisualStatus::unsupported_pose); continue;
          }
          const auto source_record=current_entities_->entity_metadata.source ? current_entities_->entity_metadata.source->record_ordinal : 0U;
          if(lifetime->second.continuity_floor && source_record<=lifetime->second.continuity_floor) {
            unavailable(entity.entity_number,ReplayLocalVisualStatus::unsupported_pose); continue;
          }
          if(lifetime->second.continuity_floor && previous_entities_ && previous_entities_->entity_metadata.source &&
              previous_entities_->entity_metadata.source->record_ordinal<=lifetime->second.continuity_floor) previous=nullptr;
          game_api::RemotePlayerPresentationContext context{generation_,generation_,lifetime->second.identity,
              record.resource_id(),record.source_fingerprint(),model,entity,*current,previous,
              maximum_clients_,sampled_receiving_entity_,
              current_entities_->entity_metadata.source ? current_entities_->entity_metadata.source->record_identity : 0U,
              previous_entities_ && previous_entities_->entity_metadata.source ? previous_entities_->entity_metadata.source->record_identity : 0U,
              sampled_current_seconds_,previous_entities_ ?
                  previous_entities_->server_time_seconds.value_or(sampled_previous_seconds_) : sampled_previous_seconds_,
              sampled_server_seconds_,false,
              source_record,previous_entities_ && previous_entities_->entity_metadata.source ?
                  previous_entities_->entity_metadata.source->record_ordinal : 0U};
          const auto policy=sampled_game_->remote_player_policy();
          context.discontinuity=!previous || previous->model_index!=current->model_index ||
              previous->player_movement_schema!=current->player_movement_schema ||
              (current->effects.value_or(0U)&policy.no_interpolation_effect_mask)!=0U ||
              sampled_current_seconds_-sampled_previous_seconds_>policy.maximum_observation_gap_seconds;
          if (previous && previous->origin.complete() && current->origin.complete()) {
            const double dx=*current->origin.x-*previous->origin.x,dy=*current->origin.y-*previous->origin.y,
                dz=*current->origin.z-*previous->origin.z;
            context.discontinuity=context.discontinuity ||
                dx*dx+dy*dy+dz*dz>policy.teleport_distance_units*policy.teleport_distance_units;
          }
          const auto intent=sampled_game_->remote_player(context);
          if(next.remote_player_count<next.remote_players.size()) {
            auto& diagnostic=next.remote_players[next.remote_player_count++];
            diagnostic={}; diagnostic.entity=entity.entity_number; diagnostic.model_slot=*entity.model_index;
            diagnostic.status=intent.status; diagnostic.intent=intent;
            diagnostic.origin={*entity.origin.x,*entity.origin.y,*entity.origin.z};
            diagnostic.source_angles={*entity.angles.x,*entity.angles.y,*entity.angles.z};
          }
          if (intent.status!=game_api::RemotePlayerPresentationStatus::ready) {
            unavailable(entity.entity_number,ReplayLocalVisualStatus::unsupported_pose); continue;
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
          transform.rotation_degrees={static_cast<float>(intent.transform_angles[2]),
              -static_cast<float>(intent.transform_angles[0]),static_cast<float>(intent.transform_angles[1])};
          pose_result.emplace(g::studio::StudioPoseEvaluator{}.compose(pose_identity,model,composition));
          player_pose=true;
        } else pose_result.emplace(g::studio::StudioPoseEvaluator{}.evaluate(pose_identity,model,pose_input));
        const auto& pose=*pose_result;
        if (!pose) {
          unavailable(entity.entity_number,
                      ReplayLocalVisualStatus::unsupported_pose);
          continue;
        }
        const auto &asset = *package_->studio_assets()[*binding.render_index];
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
            static_cast<std::uint32_t>(*binding.render_index);
        instance.pose_index =
            static_cast<std::uint32_t>(input.studio_poses.size());
        instance.transform = transform;
        instance.body_value = static_cast<std::uint32_t>(pose_input.body_value);
        instance.skin_family_index = pose_input.skin_family_index;
        instance.material_support_status = *material;
        instance.interpolated_bounds =
            bounds({asset.bounds().minimum, asset.bounds().maximum}, transform);
        if (player_pose) {
          const auto posed=g::studio::StudioPoseEvaluator{}.posed_bounds(pose_identity,model,*pose.pose);
          if (!posed) { unavailable(entity.entity_number,ReplayLocalVisualStatus::unsupported_pose); continue; }
          instance.interpolated_bounds=bounds({posed.bounds->minimum,posed.bounds->maximum},transform);
          const auto cached=light_cache_.find(entity.entity_number);
          const auto origin=transform.origin;
          if(cached==light_cache_.end() || std::abs(cached->second.first.x-origin.x)>8.0F ||
              std::abs(cached->second.first.y-origin.y)>8.0F || std::abs(cached->second.first.z-origin.z)>8.0F)
            light_cache_[entity.entity_number]={origin,static_world_light(*world_,origin)};
          instance.static_light_rgb=light_cache_[entity.entity_number].second;
          if(next.remote_player_count) {
            auto& diagnostic=next.remote_players[next.remote_player_count-1U];
            diagnostic.static_light=instance.static_light_rgb; diagnostic.pose_submitted=true;
          }
        }
        input.studio_poses.push_back(std::move(render_pose));
        input.studio_instances.push_back(instance);
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
        // The first slice supports ordinary single-frame sprites only.
        if (record.sprite_asset()->frames.size() != 1U) {
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
        const float radius = asset.bounding_radius();
        instance.bounds = bounds(
            {{-radius, -radius, -radius}, {radius, radius, radius}}, transform);
        input.sprite_instances.push_back(instance);
      }
      ++next.rendered_instances;
      ++next.coverage[binding.status];
      ++next.rendered_model_slots[*entity.model_index];
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
    // All retained-source/lifetime allocations precede the atomic publication.
    const bool retain=!sampled_game_ && observation.entity_metadata.source &&
        (!current_entities_ || current_entities_->entity_metadata.source!=observation.entity_metadata.source);
    std::shared_ptr<const client::RuntimeClientObservationState> retained;
    std::map<std::uint32_t,PlayerLifetime> lifetimes;
    auto lifetime_identity=next_player_lifetime_;
    if(retain) {
      retained=std::make_shared<const client::RuntimeClientObservationState>(observation);
      for(const auto& value:observation.packet_entities) {
        if(!value.player_movement_schema || value.entity_number>maximum_clients_) continue;
        const auto old=player_lifetimes_.find(value.entity_number);
        lifetimes.emplace(value.entity_number,old!=player_lifetimes_.end() && old->second.model==value.model_index
            ? old->second : PlayerLifetime{++lifetime_identity,value.model_index});
      }
    }
    target = std::move(candidate);
    if (camera_policy_ == ReplayLocalCameraPolicy::offline_spectator &&
        camera) {
      target.set_camera(*camera);
      camera_selected_ = true;
    }
    ++frame_revision_;
    ++next.projected_frames;
    summary_ = std::move(next);
    if (retain) {
      previous_entities_=std::move(current_entities_); current_entities_=std::move(retained);
      next_player_lifetime_=lifetime_identity;
      player_lifetimes_=std::move(lifetimes);
      std::erase_if(light_cache_,[&](const auto& value){return !player_lifetimes_.contains(value.first);});
    }
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
  const auto named = summary_.model_names.find(index);
  if (bound == bindings_.end() || named == summary_.model_names.end() ||
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
                                (seq.source_flags & 1U) != 0U, seq.events});
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
RuntimeReplayLocalAssets::materialize_viewmodel(const game_api::ViewmodelIntent& intent) {
  first_person_status_ = ReplayLocalVisualStatus::absent_model;
  viewmodel_attachment_zero_.reset();
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
  if (!model.attachments.empty()) {
    const auto& attachment = model.attachments.front();
    if (attachment.bone_index < pose.pose->world_bones().size()) {
      const auto& m=pose.pose->world_bones()[attachment.bone_index].transform.values;
      const auto p=attachment.local_origin;
      const assets::AssetVector3 location{
          m[0]*p.x+m[1]*p.y+m[2]*p.z+m[3],
          m[4]*p.x+m[5]*p.y+m[6]*p.z+m[7],
          m[8]*p.x+m[9]*p.y+m[10]*p.z+m[11]};
      if (std::isfinite(location.x)&&std::isfinite(location.y)&&std::isfinite(location.z))
        viewmodel_attachment_zero_=location;
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
  // Do not inject the predicted world eye or view Euler angles here.
  const r::EntityRenderTransform transform{
      {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, 1.0F};
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
  first_person_status_ = ReplayLocalVisualStatus::ready_studio;
  return renderer::RenderDynamicEntities{
      package_, std::make_shared<const r::EntityRenderFrame>(std::move(*frame.frame)),
      {1U, 1U, 1U, 0U, 0U}};
}

std::optional<assets::AssetVector3>
RuntimeReplayLocalAssets::viewmodel_attachment_at_frame(
    const game_api::ViewmodelIntent& intent, std::uint32_t attachment_index,
    double frame_coordinate) const {
  if (intent.status != game_api::ViewmodelStatus::ready ||
      intent.generation != generation_ || !std::isfinite(frame_coordinate) ||
      frame_coordinate < 0.0 || !library_ || !package_) return {};
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
  g::studio::StudioPoseInput pose_input;
  pose_input.compatibility_profile =
      g::studio::StudioPoseCompatibilityProfile::public_goldsrc48_discrete_local_asset_v1;
  pose_input.sequence_index = intent.sequence;
  pose_input.body_value = intent.body;
  pose_input.frame_coordinate = frame_coordinate;
  const auto pose = g::studio::StudioPoseEvaluator{}.evaluate(
      {std::to_string(record.resource_id()), record.source_fingerprint()},
      model, pose_input);
  if (!pose || attachment.bone_index >= pose.pose->world_bones().size()) return {};
  const auto& m = pose.pose->world_bones()[attachment.bone_index].transform.values;
  const auto p = attachment.local_origin;
  const assets::AssetVector3 location{
      m[0]*p.x+m[1]*p.y+m[2]*p.z+m[3],
      m[4]*p.x+m[5]*p.y+m[6]*p.z+m[7],
      m[8]*p.x+m[9]*p.y+m[10]*p.z+m[11]};
  if (!std::isfinite(location.x) || !std::isfinite(location.y) ||
      !std::isfinite(location.z)) return {};
  return location;
}

std::optional<renderer::RenderDynamicEntities>
RuntimeReplayLocalAssets::materialize_shells(
    std::span<const renderer::TransientShellState> shells, double now) {
  last_shell_frame_status_=ReplayShellFrameStatus::invalid_input;
  if (shells.empty() || shells.size()>32U || !std::isfinite(now)) return {};
  last_shell_frame_status_=ReplayShellFrameStatus::binding_unavailable;
  if (!shell_model_index_ || !package_ || !library_) return {};
  const auto found=bindings_.find(*shell_model_index_);
  if (found==bindings_.end() || !found->second.library_index ||
      !found->second.render_index ||
      found->second.status!=ReplayLocalVisualStatus::ready_studio) return {};
  const auto& record=library_->records()[*found->second.library_index];
  if (!record.model_asset() || !record.model_asset()->skeletal_data) return {};
  const auto& model=*record.model_asset()->skeletal_data;
  const auto& asset=*package_->studio_assets()[*found->second.render_index];
  g::studio::StudioPoseInput pose_input;
  pose_input.compatibility_profile=
      g::studio::StudioPoseCompatibilityProfile::public_goldsrc48_discrete_local_asset_v1;
  pose_input.sequence_index=0U;
  pose_input.body_value=0U;
  pose_input.frame_coordinate=0.0;
  last_shell_frame_status_=ReplayShellFrameStatus::pose_rejected;
  auto pose=g::studio::StudioPoseEvaluator{}.evaluate(
      {std::to_string(record.resource_id()),record.source_fingerprint()},model,pose_input);
  if (!pose) return {};
  const auto material=r::studio_entity_material_support(asset,0U,0U);
  last_shell_frame_status_=ReplayShellFrameStatus::material_rejected;
  if (!material) return {};
  r::StudioRenderPose render_pose{asset.source_identity(),{}};
  for (const auto& bone:pose.pose->world_bones()) {
    const auto& m=bone.transform.values;
    render_pose.bone_matrices.push_back({m[0],m[4],m[8],0,m[1],m[5],m[9],0,
                                         m[2],m[6],m[10],0,m[3],m[7],m[11],1});
  }
  r::EntityRenderFrameBuildInput input;
  input.resource_id=frame_id+2U;
  input.resource_revision=++shell_frame_revision_;
  // EntityRenderFrameBuilder requires a nonzero discrete state identity.
  // These are presentation-frame identities, not authoritative snapshots.
  input.interpolation={now,now,now,0.0F,input.resource_revision,input.resource_revision,
      r::EntityRenderInterpolationProfile::decoded_discrete_runtime_replay_v1};
  input.studio_poses.push_back(std::move(render_pose));
  for (std::size_t i=0;i<shells.size();++i) {
    const auto& shell=shells[i];
    const r::EntityRenderTransform transform{shell.position,
        {0.0F,shell.yaw_degrees,0.0F},1.0F};
    if (!r::finite_entity_render_transform(transform)) continue;
    r::StudioEntityRenderInstance instance;
    instance.entity_number=0xe0000000U+static_cast<std::uint32_t>(i);
    instance.studio_asset_index=static_cast<std::uint32_t>(*found->second.render_index);
    instance.pose_index=0U;
    instance.transform=transform;
    instance.body_value=0U;
    instance.material_support_status=*material;
    instance.interpolated_bounds=bounds({asset.bounds().minimum,asset.bounds().maximum},transform);
    input.studio_instances.push_back(instance);
  }
  last_shell_frame_status_=ReplayShellFrameStatus::no_valid_instances;
  if (input.studio_instances.empty()) return {};
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
