#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/assets/model_asset_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::studio {

inline constexpr std::size_t kDefaultMaximumStudioPoseBones = 128U;
inline constexpr std::size_t kHardMaximumStudioPoseBones = 128U;
inline constexpr std::size_t kDefaultMaximumStudioPoseInstances = 4'096U;
inline constexpr std::size_t kHardMaximumStudioPoseInstances = 16'384U;
inline constexpr std::size_t kDefaultMaximumStudioPoseMatrices = 16'384U;
inline constexpr std::size_t kHardMaximumStudioPoseMatrices = 1'048'576U;
inline constexpr std::size_t kDefaultMaximumStudioBlendEvaluations = 4U;
inline constexpr std::size_t kHardMaximumStudioBlendEvaluations = 4U;
inline constexpr std::size_t kDefaultMaximumStudioChannelRunsPerSample =
    65'536U;
inline constexpr std::size_t kHardMaximumStudioChannelRunsPerSample =
    1'048'576U;
inline constexpr std::size_t kDefaultMaximumStudioPoseBytes =
    16U * 1'024U * 1'024U;
inline constexpr std::size_t kHardMaximumStudioPoseBytes =
    64U * 1'024U * 1'024U;
inline constexpr std::size_t kDefaultMaximumStudioPoseCacheEntries = 4'096U;
inline constexpr std::size_t kHardMaximumStudioPoseCacheEntries = 16'384U;
inline constexpr std::uint16_t kStudioPoseFrameFractionSteps = 4'096U;

enum class StudioPoseCompatibilityProfile {
    synthetic_explicit_v1,
    stock_entity_projection_evidence_pending,
};

struct StudioPoseModelIdentity {
    std::string resource_identity;
    assets::AssetSourceFingerprint revision{};

    [[nodiscard]] friend bool
    operator==(const StudioPoseModelIdentity&,
               const StudioPoseModelIdentity&) = default;
};

struct StudioPoseInput {
    std::uint32_t sequence_index{0U};
    double frame_coordinate{0.0};
    std::int32_t body_value{0};
    std::uint32_t skin_family_index{0U};
    std::array<std::uint8_t, 4U> controller_values{};
    std::array<std::uint8_t, 2U> blending_values{};
    std::uint8_t mouth_value{0U};
    assets::AssetVector3 entity_scale{1.0F, 1.0F, 1.0F};
    StudioPoseCompatibilityProfile compatibility_profile{
        StudioPoseCompatibilityProfile::synthetic_explicit_v1};
};

struct StudioPoseEvaluationLimits {
    std::size_t maximum_bones{kDefaultMaximumStudioPoseBones};
    std::size_t maximum_instances{kDefaultMaximumStudioPoseInstances};
    std::size_t maximum_pose_matrices{kDefaultMaximumStudioPoseMatrices};
    std::size_t maximum_blend_evaluations{
        kDefaultMaximumStudioBlendEvaluations};
    std::size_t maximum_channel_runs_per_sample{
        kDefaultMaximumStudioChannelRunsPerSample};
    std::size_t maximum_pose_bytes{kDefaultMaximumStudioPoseBytes};
    std::size_t maximum_pose_cache_entries{
        kDefaultMaximumStudioPoseCacheEntries};
    float maximum_entity_scale{1'024.0F};
};

[[nodiscard]] bool valid_studio_pose_evaluation_limits(
    const StudioPoseEvaluationLimits& limits) noexcept;

enum class StudioPoseErrorCode {
    invalid_configuration,
    evidence_pending,
    invalid_model_identity,
    invalid_model,
    invalid_sequence,
    invalid_frame,
    unsupported_blend_count,
    missing_sequence_group,
    invalid_animation_track,
    channel_run_limit_exceeded,
    channel_sample_failed,
    invalid_controller,
    unsupported_controller_type,
    invalid_hierarchy,
    invalid_body_value,
    invalid_skin_family,
    bone_limit_exceeded,
    instance_limit_exceeded,
    matrix_limit_exceeded,
    blend_evaluation_limit_exceeded,
    pose_byte_limit_exceeded,
    pose_cache_entry_limit_exceeded,
    non_finite_pose,
    unable_to_retain_pose,
};

struct StudioPoseError {
    StudioPoseErrorCode code{StudioPoseErrorCode::invalid_configuration};
    std::optional<std::uint32_t> sequence_index;
    std::optional<std::uint32_t> bone_index;
    std::string context;
};

struct StudioFractionalChannelSample {
    std::uint32_t frame_zero{0U};
    std::uint32_t frame_one{0U};
    float fraction{0.0F};
    std::int16_t quantized_zero{0};
    std::int16_t quantized_one{0};
    float scaled_zero{0.0F};
    float scaled_one{0.0F};
    float interpolated_value{0.0F};
    std::size_t runs_examined{0U};
};

struct StudioFractionalChannelSampleResult {
    std::optional<StudioFractionalChannelSample> sample;
    std::optional<StudioPoseError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return sample.has_value();
    }
};

class StudioFractionalAnimationChannelSampler final {
  public:
    [[nodiscard]] static StudioFractionalChannelSampleResult
    sample_fractional_frame(
        const assets::ModelAnimationChannel& channel, double frame_coordinate,
        std::size_t maximum_runs_per_sample =
            kDefaultMaximumStudioChannelRunsPerSample) noexcept;
};

struct StudioBoneControllerAdjustments {
    std::vector<float> values_by_controller_record;
};

struct StudioBoneControllerEvaluationResult {
    std::optional<StudioBoneControllerAdjustments> adjustments;
    std::optional<StudioPoseError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return adjustments.has_value();
    }
};

class StudioBoneControllerEvaluator final {
  public:
    [[nodiscard]] StudioBoneControllerEvaluationResult
    evaluate(const assets::SkeletalModelAssetData& model,
             const std::array<std::uint8_t, 4U>& controller_values,
             std::uint8_t mouth_value) const;
};

struct StudioBodyPartSelection {
    std::uint32_t bodypart_index{0U};
    std::uint32_t local_model_index{0U};
    std::uint32_t submodel_index{0U};
};

struct StudioBodySelection {
    std::vector<StudioBodyPartSelection> bodyparts;
};

struct StudioBodySelectionResult {
    std::optional<StudioBodySelection> selection;
    std::optional<StudioPoseError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return selection.has_value();
    }
};

class StudioBodyPartSelector final {
  public:
    [[nodiscard]] StudioBodySelectionResult
    select(const assets::SkeletalModelAssetData& model,
           std::int32_t body_value) const;
};

struct StudioSkinSelection {
    std::uint32_t family_index{0U};
    std::vector<std::uint16_t> texture_indices_by_skin_reference;

    [[nodiscard]] std::optional<std::uint32_t>
    texture_for_mesh(const assets::ModelMesh& mesh) const noexcept;
};

struct StudioSkinSelectionResult {
    std::optional<StudioSkinSelection> selection;
    std::optional<StudioPoseError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return selection.has_value();
    }
};

class StudioSkinSelector final {
  public:
    [[nodiscard]] StudioSkinSelectionResult
    select(const assets::SkeletalModelAssetData& model,
           std::uint32_t skin_family_index) const;
};

struct StudioQuaternion {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    float w{1.0F};
};

struct ModelBoneLocalPose {
    assets::AssetVector3 translation{};
    StudioQuaternion rotation{};
};

// Row-major affine 3x4 matrix for column-vector transforms.
struct StudioMatrix3x4 {
    std::array<float, 12U> values{
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
    };
};

struct ModelBoneWorldPose {
    StudioMatrix3x4 transform{};
    // Row-major orthonormal rotation/normal matrix. Source skeletons have no
    // per-bone scale.
    std::array<float, 9U> normal_rotation{
        1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
};

struct StudioPoseFrameSample {
    double source_frame_coordinate{0.0};
    double normalized_frame_coordinate{0.0};
    std::uint32_t frame_zero{0U};
    std::uint32_t frame_one{0U};
    std::uint16_t quantized_fraction{0U};
    float fraction{0.0F};
    bool looped{false};
    bool clamped{false};
};

struct StudioPoseStatistics {
    std::size_t bone_count{0U};
    std::size_t local_pose_count{0U};
    std::size_t world_matrix_count{0U};
    std::size_t blend_evaluation_count{0U};
    std::size_t channel_sample_count{0U};
    std::size_t channel_runs_examined{0U};
    std::size_t controller_count{0U};
    std::size_t bodypart_selection_count{0U};
    std::size_t skin_reference_count{0U};
    std::size_t suppressed_motion_axis_count{0U};
    std::size_t accounted_pose_bytes{0U};
};

class StudioPoseState final {
  public:
    StudioPoseState(const StudioPoseState&) = default;
    StudioPoseState(StudioPoseState&&) noexcept = default;
    StudioPoseState& operator=(const StudioPoseState&) = delete;
    StudioPoseState& operator=(StudioPoseState&&) noexcept = delete;
    ~StudioPoseState() = default;

    [[nodiscard]] const StudioPoseModelIdentity&
    model_identity() const noexcept;
    [[nodiscard]] std::uint32_t sequence_index() const noexcept;
    [[nodiscard]] const StudioPoseFrameSample& frame_sample() const noexcept;
    [[nodiscard]] const assets::AssetVector3& entity_scale() const noexcept;
    [[nodiscard]] std::span<const ModelBoneLocalPose>
    local_bones() const noexcept;
    [[nodiscard]] std::span<const ModelBoneWorldPose>
    world_bones() const noexcept;
    [[nodiscard]] const StudioBodySelection& body_selection() const noexcept;
    [[nodiscard]] const StudioSkinSelection& skin_selection() const noexcept;
    [[nodiscard]] const StudioPoseStatistics& statistics() const noexcept;
    [[nodiscard]] StudioPoseCompatibilityProfile
    compatibility_profile() const noexcept;

  private:
    friend class StudioPoseEvaluator;

    StudioPoseState(
        StudioPoseModelIdentity model_identity, std::uint32_t sequence_index,
        StudioPoseFrameSample frame_sample, assets::AssetVector3 entity_scale,
        std::vector<ModelBoneLocalPose> local_bones,
        std::vector<ModelBoneWorldPose> world_bones,
        StudioBodySelection body_selection, StudioSkinSelection skin_selection,
        StudioPoseStatistics statistics,
        StudioPoseCompatibilityProfile compatibility_profile) noexcept;

    StudioPoseModelIdentity model_identity_;
    std::uint32_t sequence_index_{0U};
    StudioPoseFrameSample frame_sample_;
    assets::AssetVector3 entity_scale_{1.0F, 1.0F, 1.0F};
    std::vector<ModelBoneLocalPose> local_bones_;
    std::vector<ModelBoneWorldPose> world_bones_;
    StudioBodySelection body_selection_;
    StudioSkinSelection skin_selection_;
    StudioPoseStatistics statistics_;
    StudioPoseCompatibilityProfile compatibility_profile_{
        StudioPoseCompatibilityProfile::synthetic_explicit_v1};
};

struct StudioPoseEvaluationResult {
    std::optional<StudioPoseState> pose;
    std::optional<StudioPoseError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return pose.has_value();
    }
};

class StudioPoseEvaluator final {
  public:
    [[nodiscard]] StudioPoseEvaluationResult
    evaluate(const StudioPoseModelIdentity& model_identity,
             const assets::SkeletalModelAssetData& model,
             const StudioPoseInput& input,
             const StudioPoseEvaluationLimits& limits = {}) const;
};

enum class StudioPoseCacheLookupStatus {
    inserted,
    shared_existing,
};

struct StudioPoseCacheStatistics {
    std::uint64_t frame_token{0U};
    std::size_t instance_request_count{0U};
    std::size_t entry_count{0U};
    std::size_t cache_hit_count{0U};
    std::size_t cache_miss_count{0U};
    std::size_t accounted_pose_bytes{0U};
};

struct StudioPoseCacheResult {
    std::shared_ptr<const StudioPoseState> pose;
    std::optional<StudioPoseError> error;
    StudioPoseCacheLookupStatus status{StudioPoseCacheLookupStatus::inserted};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(pose);
    }
};

class StudioPoseCache final {
  public:
    explicit StudioPoseCache(StudioPoseEvaluationLimits limits = {});
    StudioPoseCache(const StudioPoseCache&) = delete;
    StudioPoseCache(StudioPoseCache&&) = delete;
    StudioPoseCache& operator=(const StudioPoseCache&) = delete;
    StudioPoseCache& operator=(StudioPoseCache&&) = delete;
    ~StudioPoseCache();

    [[nodiscard]] bool valid_configuration() const noexcept;
    void reset_for_frame(std::uint64_t frame_token) noexcept;
    [[nodiscard]] StudioPoseCacheResult
    find_or_evaluate(const StudioPoseModelIdentity& model_identity,
                     const assets::SkeletalModelAssetData& model,
                     const StudioPoseInput& input);
    [[nodiscard]] const StudioPoseCacheStatistics& statistics() const noexcept;

  private:
    struct Entry;

    StudioPoseEvaluationLimits limits_;
    bool frame_started_{false};
    std::vector<Entry> entries_;
    StudioPoseCacheStatistics statistics_;
};

[[nodiscard]] constexpr std::string_view
to_string(StudioPoseErrorCode code) noexcept
{
    switch (code) {
    case StudioPoseErrorCode::invalid_configuration:
        return "invalid_configuration";
    case StudioPoseErrorCode::evidence_pending:
        return "evidence_pending";
    case StudioPoseErrorCode::invalid_model_identity:
        return "invalid_model_identity";
    case StudioPoseErrorCode::invalid_model:
        return "invalid_model";
    case StudioPoseErrorCode::invalid_sequence:
        return "invalid_sequence";
    case StudioPoseErrorCode::invalid_frame:
        return "invalid_frame";
    case StudioPoseErrorCode::unsupported_blend_count:
        return "unsupported_blend_count";
    case StudioPoseErrorCode::missing_sequence_group:
        return "missing_sequence_group";
    case StudioPoseErrorCode::invalid_animation_track:
        return "invalid_animation_track";
    case StudioPoseErrorCode::channel_run_limit_exceeded:
        return "channel_run_limit_exceeded";
    case StudioPoseErrorCode::channel_sample_failed:
        return "channel_sample_failed";
    case StudioPoseErrorCode::invalid_controller:
        return "invalid_controller";
    case StudioPoseErrorCode::unsupported_controller_type:
        return "unsupported_controller_type";
    case StudioPoseErrorCode::invalid_hierarchy:
        return "invalid_hierarchy";
    case StudioPoseErrorCode::invalid_body_value:
        return "invalid_body_value";
    case StudioPoseErrorCode::invalid_skin_family:
        return "invalid_skin_family";
    case StudioPoseErrorCode::bone_limit_exceeded:
        return "bone_limit_exceeded";
    case StudioPoseErrorCode::instance_limit_exceeded:
        return "instance_limit_exceeded";
    case StudioPoseErrorCode::matrix_limit_exceeded:
        return "matrix_limit_exceeded";
    case StudioPoseErrorCode::blend_evaluation_limit_exceeded:
        return "blend_evaluation_limit_exceeded";
    case StudioPoseErrorCode::pose_byte_limit_exceeded:
        return "pose_byte_limit_exceeded";
    case StudioPoseErrorCode::pose_cache_entry_limit_exceeded:
        return "pose_cache_entry_limit_exceeded";
    case StudioPoseErrorCode::non_finite_pose:
        return "non_finite_pose";
    case StudioPoseErrorCode::unable_to_retain_pose:
        return "unable_to_retain_pose";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc::studio
