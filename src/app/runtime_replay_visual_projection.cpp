#include <hlclient/app/runtime_replay_visual_projection.hpp>

#include <hlclient/assets/model_asset_types.hpp>
#include <hlclient/entity_render/studio_model_render_asset.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::app {
namespace {

constexpr entity_render::EntityRenderResourceIdentity kDiagnosticModelIdentity{
    0x4756'4d4f'4445'4c01ULL, 1U};
constexpr std::uint64_t kDiagnosticSceneResourceId =
    0x4756'5343'454e'4501ULL;
constexpr std::uint64_t kDiagnosticFrameResourceId =
    0x4756'4652'414d'4501ULL;
constexpr float kDiagnosticModelScale = 1.5F;

[[nodiscard]] RuntimeReplayVisualProjectionError error(
    const RuntimeReplayVisualProjectionErrorCode code,
    std::string context,
    const std::optional<std::uint32_t> entity_number = std::nullopt,
    const std::optional<entity_render::EntityRenderFrameErrorCode> frame_error =
        std::nullopt)
{
    return {code, entity_number, frame_error, std::move(context)};
}

[[nodiscard]] RuntimeReplayVisualProjectionResult failure(
    RuntimeReplayVisualProjectionError value)
{
    return {false, false, std::move(value)};
}

[[nodiscard]] std::shared_ptr<const assets::ModelAsset> diagnostic_model()
{
    auto skeletal = std::make_shared<assets::SkeletalModelAssetData>();
    skeletal->source_clipping_bounds = {
        {-10.0F, -8.0F, -6.0F}, {18.0F, 8.0F, 10.0F}};
    skeletal->bones.push_back(assets::ModelBone{"diagnostic-root"});

    assets::ModelSubmodel submodel;
    submodel.name = "asymmetric-arrow";
    submodel.bounding_radius = 22.0F;
    submodel.bounds = skeletal->source_clipping_bounds;
    submodel.vertices = {
        {{18.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 3, 2, 0U, 0U},
        {{-10.0F, -8.0F, -6.0F}, {-1.0F, -1.0F, -1.0F}, 0, 0, 0U, 0U},
        {{-10.0F, 8.0F, -6.0F}, {-1.0F, 1.0F, -1.0F}, 0, 3, 0U, 0U},
        {{-10.0F, 0.0F, 10.0F}, {-1.0F, 0.0F, 1.0F}, 3, 0, 0U, 0U},
    };
    submodel.indices = {
        0U, 2U, 1U,
        0U, 3U, 2U,
        0U, 1U, 3U,
        1U, 2U, 3U,
    };
    submodel.meshes = {{0U, 12U, 0U, 0U, 4U, 1U, 0U}};
    skeletal->submodels.push_back(std::move(submodel));
    skeletal->bodyparts.push_back({"diagnostic-body", 1, {0U}});

    assets::ModelTextureAsset texture;
    texture.source_name = "project-generated-orange-cyan";
    texture.width = 4U;
    texture.height = 4U;
    texture.rgba8_level_zero.reserve(4U * 4U * 4U);
    for (std::uint32_t y = 0U; y < 4U; ++y) {
        for (std::uint32_t x = 0U; x < 4U; ++x) {
            const bool orange = ((x + y) & 1U) == 0U;
            texture.rgba8_level_zero.push_back(
                orange ? std::byte{0xf0U} : std::byte{0x10U});
            texture.rgba8_level_zero.push_back(
                orange ? std::byte{0x70U} : std::byte{0xd0U});
            texture.rgba8_level_zero.push_back(
                orange ? std::byte{0x10U} : std::byte{0xe0U});
            texture.rgba8_level_zero.push_back(std::byte{0xffU});
        }
    }
    skeletal->textures.push_back(std::move(texture));
    skeletal->skin_families.push_back({{0U}});

    auto model = std::make_shared<assets::ModelAsset>();
    model->identity.source_name =
        "project-generated/runtime-replay-diagnostic-arrow";
    model->skeletal_data = std::move(skeletal);
    return model;
}

[[nodiscard]] std::array<float, 16U> identity_matrix() noexcept
{
    return {1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F};
}

[[nodiscard]] bool finite_bounded(
    const double value,
    const double maximum_absolute) noexcept
{
    return std::isfinite(value) && std::abs(value) <= maximum_absolute &&
        value <= static_cast<double>((std::numeric_limits<float>::max)()) &&
        value >= -static_cast<double>((std::numeric_limits<float>::max)());
}

[[nodiscard]] assets::WorldBounds conservative_bounds(
    const entity_render::EntityRenderTransform& transform) noexcept
{
    constexpr float local_radius = 22.0F;
    const float radius = local_radius * transform.uniform_scale;
    return {
        {transform.origin.x - radius, transform.origin.y - radius,
            transform.origin.z - radius},
        {transform.origin.x + radius, transform.origin.y + radius,
            transform.origin.z + radius},
    };
}

} // namespace

bool valid_runtime_replay_visual_projection_limits(
    const RuntimeReplayVisualProjectionLimits& limits) noexcept
{
    return limits.maximum_entities != 0U &&
        limits.maximum_entities <=
            entity_render::kRuntimeEntityVisualHardLimits.maximum_entities &&
        std::isfinite(limits.maximum_absolute_coordinate) &&
        limits.maximum_absolute_coordinate > 0.0 &&
        limits.maximum_absolute_coordinate <= 1.0e9;
}

RuntimeReplayVisualProjection::RuntimeReplayVisualProjection(
    const RuntimeReplayVisualProjectionLimits limits,
    std::shared_ptr<const entity_render::EntitySceneRenderPackage> package)
    noexcept
    : limits_{limits}, package_{std::move(package)}
{
}

RuntimeReplayVisualProjectionCreateResult
RuntimeReplayVisualProjection::create(
    const RuntimeReplayVisualProjectionLimits limits)
{
    if (!valid_runtime_replay_visual_projection_limits(limits)) {
        return {{}, error(
            RuntimeReplayVisualProjectionErrorCode::invalid_configuration,
            "runtime replay visual projection limits are invalid")};
    }
    try {
        const auto model = diagnostic_model();
        auto render_asset =
            entity_render::StudioModelRenderAssetBuilder{}.build(
                *model, kDiagnosticModelIdentity);
        if (!render_asset || !render_asset.asset) {
            return {{}, error(
                RuntimeReplayVisualProjectionErrorCode::asset_build_failed,
                render_asset.error
                    ? render_asset.error->context
                    : "diagnostic Studio render asset build failed")};
        }

        entity_render::EntitySceneRenderPackageCreateInfo package_input;
        package_input.asset_source = entity_render::
            EntitySceneRenderAssetSource::project_generated_diagnostic;
        package_input.resource_id = kDiagnosticSceneResourceId;
        package_input.studio_assets.push_back(
            std::make_shared<const entity_render::StudioModelRenderAsset>(
                std::move(*render_asset.asset)));
        auto package = entity_render::EntitySceneRenderPackageBuilder{}.build(
            std::move(package_input));
        if (!package || !package.package) {
            return {{}, error(
                RuntimeReplayVisualProjectionErrorCode::package_build_failed,
                package.error
                    ? package.error->context
                    : "diagnostic entity render package build failed")};
        }
        auto shared_package = std::make_shared<
            const entity_render::EntitySceneRenderPackage>(
                std::move(*package.package));
        return {
            std::unique_ptr<RuntimeReplayVisualProjection>{
                new RuntimeReplayVisualProjection{limits,
                    std::move(shared_package)}},
            std::nullopt};
    } catch (const std::bad_alloc&) {
        return {{}, error(
            RuntimeReplayVisualProjectionErrorCode::unable_to_retain_candidate,
            "unable to retain project-generated diagnostic render resources")};
    }
}

RuntimeReplayVisualProjectionResult
RuntimeReplayVisualProjection::reset_generation(
    const client::RuntimeClientObservationState& observation,
    client::ClientWorldState& target)
{
    generation_ = 0U;
    presented_runtime_revision_ = 0U;
    visual_frame_revision_ = 0U;
    projected_frame_count_ = 0U;
    unchanged_record_count_ = 0U;
    if (!observation.packet_entities.empty()) {
        return failure(error(
            RuntimeReplayVisualProjectionErrorCode::invalid_configuration,
            "generation reset observation unexpectedly retained packet entities"));
    }
    generation_ = observation.generation;
    return project_entities(observation, target);
}

RuntimeReplayVisualProjectionResult
RuntimeReplayVisualProjection::project_entities(
    const client::RuntimeClientObservationState& observation,
    client::ClientWorldState& target)
{
    for (const auto& entity : observation.packet_entities) {
        if (!entity.origin.complete() || !entity.angles.complete()) {
            return failure(error(
                RuntimeReplayVisualProjectionErrorCode::incomplete_transform,
                "diagnostic binding requires complete decoded origin and angles",
                entity.entity_number));
        }
        const std::array values{
            *entity.origin.x, *entity.origin.y, *entity.origin.z,
            *entity.angles.x, *entity.angles.y, *entity.angles.z};
        if (!std::all_of(values.begin(), values.end(),
                [&](const double value) {
                    return finite_bounded(
                        value, limits_.maximum_absolute_coordinate);
                })) {
            return failure(error(
                RuntimeReplayVisualProjectionErrorCode::non_finite_transform,
                "decoded origin or angles are non-finite or outside the diagnostic bound",
                entity.entity_number));
        }
    }
    if (!client::valid_runtime_observation(observation)) {
        return failure(error(
            RuntimeReplayVisualProjectionErrorCode::observation_missing,
            "runtime visual projection requires one valid committed observation"));
    }
    if (generation_ == 0U) {
        generation_ = observation.generation;
    }
    if (observation.generation != generation_) {
        return failure(error(
            RuntimeReplayVisualProjectionErrorCode::generation_mismatch,
            "runtime visual projection observation belongs to another generation"));
    }
    if (observation.packet_entities.size() > limits_.maximum_entities) {
        return failure(error(
            RuntimeReplayVisualProjectionErrorCode::projection_limit_exceeded,
            "runtime visual entity count exceeds the diagnostic projection limit"));
    }
    if (visual_frame_revision_ ==
        (std::numeric_limits<std::uint64_t>::max)()) {
        return failure(error(
            RuntimeReplayVisualProjectionErrorCode::frame_revision_overflow,
            "runtime visual frame revision overflowed"));
    }

    try {
        entity_render::EntityRenderFrameBuildInput input;
        input.resource_id = kDiagnosticFrameResourceId;
        input.resource_revision = visual_frame_revision_ + 1U;
        const double sample_time = observation.server_time_seconds.value_or(0.0);
        input.interpolation = {
            sample_time,
            sample_time,
            sample_time,
            0.0F,
            observation.canonical_state_hash,
            observation.canonical_state_hash,
            entity_render::EntityRenderInterpolationProfile::
                decoded_discrete_runtime_replay_v1,
        };
        if (!observation.packet_entities.empty()) {
            input.studio_poses.push_back({
                kDiagnosticModelIdentity, {identity_matrix()}});
        }
        input.studio_instances.reserve(observation.packet_entities.size());
        for (const auto& entity : observation.packet_entities) {
            entity_render::EntityRenderTransform transform;
            transform.origin = {
                static_cast<float>(*entity.origin.x),
                static_cast<float>(*entity.origin.y),
                static_cast<float>(*entity.origin.z)};
            transform.rotation_degrees = {
                static_cast<float>(*entity.angles.x),
                static_cast<float>(*entity.angles.y),
                static_cast<float>(*entity.angles.z)};
            transform.uniform_scale = kDiagnosticModelScale;
            if (!entity_render::finite_entity_render_transform(transform)) {
                return failure(error(
                    RuntimeReplayVisualProjectionErrorCode::
                        non_finite_transform,
                    "decoded transform cannot be represented by the neutral render contract",
                    entity.entity_number));
            }
            entity_render::StudioEntityRenderInstance instance;
            instance.entity_number = entity.entity_number;
            instance.studio_asset_index = 0U;
            instance.pose_index = 0U;
            instance.transform = transform;
            instance.interpolated_bounds = conservative_bounds(transform);
            input.studio_instances.push_back(instance);
        }

        auto built = entity_render::EntityRenderFrameBuilder{}.build(
            *package_, std::move(input));
        if (!built || !built.frame) {
            return failure(error(
                RuntimeReplayVisualProjectionErrorCode::frame_build_failed,
                built.error ? built.error->context
                            : "diagnostic entity frame build failed",
                built.error ? built.error->entity_number : std::nullopt,
                built.error ? std::optional{built.error->code} : std::nullopt));
        }
        auto frame = std::make_shared<const entity_render::EntityRenderFrame>(
            std::move(*built.frame));
        if (!target.set_dynamic_entities(package_, std::move(frame))) {
            return failure(error(
                RuntimeReplayVisualProjectionErrorCode::publication_failed,
                "ClientWorldState rejected the complete diagnostic frame"));
        }
        ++visual_frame_revision_;
        ++projected_frame_count_;
        presented_runtime_revision_ = observation.publication_revision;
        return {true, true, std::nullopt};
    } catch (const std::bad_alloc&) {
        return failure(error(
            RuntimeReplayVisualProjectionErrorCode::unable_to_retain_candidate,
            "unable to retain a complete diagnostic visual candidate"));
    } catch (const std::length_error&) {
        return failure(error(
            RuntimeReplayVisualProjectionErrorCode::projection_limit_exceeded,
            "diagnostic visual candidate exceeds an owning container limit"));
    }
}

RuntimeReplayVisualProjectionResult
RuntimeReplayVisualProjection::acknowledge_unchanged(
    const client::RuntimeClientObservationState& observation) noexcept
{
    if (!client::valid_runtime_observation(observation)) {
        return failure(error(
            RuntimeReplayVisualProjectionErrorCode::observation_missing,
            "unchanged visual acknowledgement requires a valid observation"));
    }
    if (observation.generation != generation_) {
        return failure(error(
            RuntimeReplayVisualProjectionErrorCode::generation_mismatch,
            "unchanged visual acknowledgement belongs to another generation"));
    }
    presented_runtime_revision_ = observation.publication_revision;
    ++unchanged_record_count_;
    return {true, false, std::nullopt};
}

const std::shared_ptr<const entity_render::EntitySceneRenderPackage>&
RuntimeReplayVisualProjection::package() const noexcept
{
    return package_;
}

RuntimeReplayVisualProjectionSummary RuntimeReplayVisualProjection::summary()
    const noexcept
{
    return {
        RuntimeReplayVisualBinding::diagnostic_fixture,
        RuntimeReplayVisualAssetSource::project_generated,
        RuntimeReplayStockModelBinding::not_verified,
        generation_,
        presented_runtime_revision_,
        visual_frame_revision_,
        package_ ? package_->resource_id() : 0U,
        package_ ? package_->resource_revision() : 0U,
        projected_frame_count_,
        unchanged_record_count_,
        package_ ? 1U : 0U,
    };
}

std::string_view to_string(const RuntimeReplayVisualBinding binding) noexcept
{
    switch (binding) {
    case RuntimeReplayVisualBinding::diagnostic_fixture:
        return "diagnostic_fixture";
    }
    return "unknown";
}

std::string_view to_string(
    const RuntimeReplayVisualAssetSource source) noexcept
{
    switch (source) {
    case RuntimeReplayVisualAssetSource::project_generated:
        return "project_generated";
    }
    return "unknown";
}

std::string_view to_string(
    const RuntimeReplayStockModelBinding binding) noexcept
{
    switch (binding) {
    case RuntimeReplayStockModelBinding::not_verified:
        return "not_verified";
    }
    return "unknown";
}

std::string_view to_string(
    const RuntimeReplayVisualProjectionErrorCode code) noexcept
{
    switch (code) {
    case RuntimeReplayVisualProjectionErrorCode::invalid_configuration:
        return "invalid_configuration";
    case RuntimeReplayVisualProjectionErrorCode::asset_build_failed:
        return "asset_build_failed";
    case RuntimeReplayVisualProjectionErrorCode::package_build_failed:
        return "package_build_failed";
    case RuntimeReplayVisualProjectionErrorCode::observation_missing:
        return "observation_missing";
    case RuntimeReplayVisualProjectionErrorCode::generation_mismatch:
        return "generation_mismatch";
    case RuntimeReplayVisualProjectionErrorCode::projection_limit_exceeded:
        return "projection_limit_exceeded";
    case RuntimeReplayVisualProjectionErrorCode::incomplete_transform:
        return "incomplete_transform";
    case RuntimeReplayVisualProjectionErrorCode::non_finite_transform:
        return "non_finite_transform";
    case RuntimeReplayVisualProjectionErrorCode::frame_revision_overflow:
        return "frame_revision_overflow";
    case RuntimeReplayVisualProjectionErrorCode::frame_build_failed:
        return "frame_build_failed";
    case RuntimeReplayVisualProjectionErrorCode::publication_failed:
        return "publication_failed";
    case RuntimeReplayVisualProjectionErrorCode::unable_to_retain_candidate:
        return "unable_to_retain_candidate";
    }
    return "unknown";
}

} // namespace hlclient::app
