#include <algorithm>
#include <cmath>
#include <hlclient/app/runtime_replay_local_assets.hpp>
#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/entity_render/entity_render_frame_composer.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <hlclient/goldsrc/goldsrc_builtin_asset_importers.hpp>
#include <hlclient/goldsrc/lightmaps/goldsrc_world_lightmap_import.hpp>
#include <hlclient/goldsrc/local_resource_inventory.hpp>
#include <hlclient/goldsrc/precache_asset_dispatch.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>
#include <hlclient/goldsrc/visual_assets/goldsrc_visual_asset_import.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/world_render/world_render_package_builder.hpp>
#include <limits>
#include <stdexcept>

namespace hlclient::app {
namespace {
namespace g = goldsrc;
namespace v = entity_visual;
namespace r = entity_render;
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
    if (!g::register_builtin_asset_importers(registries)) {
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
    auto textures = g::WorldTextureImportOperation::begin(
        world, world_source.source().bytes(), environment);
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

    std::vector<v::EntityVisualModelReference> references;
    for (const auto &entry : manifest.state->entries()) {
      if (entry.resource_type() != g::ResourceType::model ||
          &entry == manifest.state->world_entry()) {
        continue;
      }
      const auto name =
          resources.entries()[entry.wire_ordinal()].name().bytes();
      if (entry.locator()) {
        summary.model_names.emplace(entry.resource_index(), name);
      }
      if (name.starts_with('*')) {
        // Inline references are a map submodel namespace, never paths.
        result->bindings_.emplace(
            entry.resource_index(),
            Binding{ReplayLocalVisualStatus::inline_brush_unsupported, {}, {}});
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
  generation_ = observation.generation;
  frame_revision_ = 0;
  camera_selected_ = false;
  target.set_static_world(world_);
  return project_entities(observation, target);
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
    auto next = summary_;
    next.coverage.clear();
    next.rendered_model_slots.clear();
    next.decoded_entities = observation.packet_entities.size();
    next.resolved_instances = 0;
    next.rendered_instances = 0;
    next.unsupported_instances = 0;
    std::optional<client::RenderCameraState> camera;
    const auto unavailable = [&](std::uint32_t number,
                                 ReplayLocalVisualStatus reason) {
      ++next.coverage[reason];
      ++next.unsupported_instances;
      input.unsupported_instances.push_back(
          {number,
           {},
           r::UnsupportedEntityVisualReason::unsupported_asset_kind,
           r::RuntimeEntityVisibilityStatus::unsupported_visual});
    };
    for (const auto &entity : observation.packet_entities) {
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
        auto pose = g::studio::StudioPoseEvaluator{}.evaluate(
            {std::to_string(record.resource_id()), record.source_fingerprint()},
            model, pose_input);
        if (!pose) {
          unavailable(entity.entity_number,
                      ReplayLocalVisualStatus::unsupported_pose);
          continue;
        }
        const auto &asset = *package_->studio_assets()[*binding.render_index];
        const auto material = r::studio_entity_material_support(
            asset, entity.body.value_or(0U), pose_input.skin_family_index);
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
        instance.body_value = entity.body.value_or(0U);
        instance.skin_family_index = pose_input.skin_family_index;
        instance.material_support_status = *material;
        instance.interpolated_bounds =
            bounds({asset.bounds().minimum, asset.bounds().maximum}, transform);
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
    if (!target.set_dynamic_entities(
            package_, std::make_shared<const r::EntityRenderFrame>(
                          std::move(*frame.frame)))) {
      return {false, false,
              visual_error("atomic local visual frame publication rejected")};
    }
    if (camera_policy_ == ReplayLocalCameraPolicy::offline_spectator &&
        camera) {
      target.set_camera(*camera);
      camera_selected_ = true;
    }
    ++frame_revision_;
    ++next.projected_frames;
    summary_ = std::move(next);
    return {true, true, {}};
  } catch (const std::exception &error) {
    return {false, false, visual_error(error.what())};
  }
}

std::string_view to_string(ReplayLocalVisualStatus status) noexcept {
  switch (status) {
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
