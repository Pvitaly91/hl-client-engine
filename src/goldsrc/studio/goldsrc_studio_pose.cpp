#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>

#include <hlclient/goldsrc/studio/goldsrc_studio_format.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::goldsrc::studio {
namespace {

constexpr float kDegreesToRadians = 0.01745329251994329577F;
constexpr std::uint32_t kStudioSequenceLooping = 0x0001U;
constexpr std::uint32_t kStudioMotionX = 0x0001U;
constexpr std::uint32_t kStudioMotionY = 0x0002U;
constexpr std::uint32_t kStudioMotionZ = 0x0004U;
constexpr std::uint32_t kStudioControllerTranslationX = 0x0001U;
constexpr std::uint32_t kStudioControllerTranslationY = 0x0002U;
constexpr std::uint32_t kStudioControllerTranslationZ = 0x0004U;
constexpr std::uint32_t kStudioControllerRotationX = 0x0008U;
constexpr std::uint32_t kStudioControllerRotationY = 0x0010U;
constexpr std::uint32_t kStudioControllerRotationZ = 0x0020U;

[[nodiscard]] StudioPoseError
make_error(const StudioPoseErrorCode code, std::string context,
           const std::optional<std::uint32_t> sequence = std::nullopt,
           const std::optional<std::uint32_t> bone = std::nullopt)
{
    return StudioPoseError{code, sequence, bone, std::move(context)};
}

[[nodiscard]] StudioPoseEvaluationResult
fail_pose(const StudioPoseErrorCode code, std::string context,
          const std::optional<std::uint32_t> sequence = std::nullopt,
          const std::optional<std::uint32_t> bone = std::nullopt)
{
    return {std::nullopt, make_error(code, std::move(context), sequence, bone)};
}

[[nodiscard]] StudioFractionalChannelSampleResult
fail_sample(const StudioPoseErrorCode code, std::string context) noexcept
{
    try {
        return {std::nullopt, make_error(code, std::move(context))};
    } catch (...) {
        return {
            std::nullopt,
            StudioPoseError{code, std::nullopt, std::nullopt, {}},
        };
    }
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool finite_quaternion(const StudioQuaternion& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] bool finite_matrix(const StudioMatrix3x4& value) noexcept
{
    return std::all_of(value.values.begin(), value.values.end(),
                       [](const float v) { return std::isfinite(v); });
}

[[nodiscard]] bool checked_multiply(const std::size_t left,
                                    const std::size_t right,
                                    std::size_t& result) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool checked_add(const std::size_t left, const std::size_t right,
                               std::size_t& result) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

struct QuantizedLookup {
    std::optional<std::int16_t> value;
    std::size_t runs_examined{0U};
    bool run_limit_exceeded{false};
};

[[nodiscard]] QuantizedLookup
sample_quantized_bounded(const assets::ModelAnimationChannel& channel,
                         const std::uint32_t frame,
                         const std::size_t maximum_runs) noexcept
{
    if (frame >= channel.frame_coverage || maximum_runs == 0U) {
        return {};
    }
    if (channel.runs.empty()) {
        return {std::int16_t{0}, 0U, false};
    }
    std::size_t examined = 0U;
    for (const auto& run : channel.runs) {
        if (examined >= maximum_runs) {
            return {std::nullopt, examined, true};
        }
        ++examined;
        if (run.total_frame_count == 0U || run.valid_value_count == 0U ||
            run.valid_value_count > run.total_frame_count ||
            run.quantized_values.size() != run.valid_value_count) {
            return {std::nullopt, examined, false};
        }
        const auto end =
            static_cast<std::uint64_t>(run.first_frame) + run.total_frame_count;
        if (frame < run.first_frame ||
            static_cast<std::uint64_t>(frame) >= end) {
            continue;
        }
        const auto local = frame - run.first_frame;
        const auto value_index = std::min<std::size_t>(
            local, static_cast<std::size_t>(run.valid_value_count - 1U));
        return {run.quantized_values[value_index], examined, false};
    }
    return {std::nullopt, examined, false};
}

[[nodiscard]] std::optional<StudioPoseFrameSample>
normalize_frame(const assets::ModelSequence& sequence,
                const double source_frame) noexcept
{
    if (!std::isfinite(source_frame) || sequence.frame_count == 0U) {
        return std::nullopt;
    }

    StudioPoseFrameSample result;
    result.source_frame_coordinate = source_frame;
    const bool looping = (sequence.source_flags & kStudioSequenceLooping) != 0U;
    const auto final_frame = sequence.frame_count - 1U;
    double normalized = source_frame;
    if (looping) {
        const auto period = static_cast<double>(std::max(final_frame, 1U));
        normalized = std::fmod(source_frame, period);
        if (normalized < 0.0) {
            normalized += period;
        }
        result.looped = normalized != source_frame;
    } else {
        const auto maximum = static_cast<double>(final_frame);
        normalized = std::clamp(source_frame, 0.0, maximum);
        result.clamped = normalized != source_frame;
    }
    if (!std::isfinite(normalized) || normalized < 0.0 ||
        normalized > static_cast<double>(final_frame)) {
        return std::nullopt;
    }

    result.frame_zero = static_cast<std::uint32_t>(std::floor(normalized));
    result.frame_one = std::min(result.frame_zero + 1U, final_frame);
    const auto raw_fraction = normalized - result.frame_zero;
    constexpr auto denominator =
        static_cast<double>(kStudioPoseFrameFractionSteps - 1U);
    const auto quantized = static_cast<std::uint16_t>(std::clamp<long>(
        std::lround(raw_fraction * denominator), 0L,
        static_cast<long>(kStudioPoseFrameFractionSteps - 1U)));
    result.quantized_fraction = quantized;
    result.fraction =
        static_cast<float>(static_cast<double>(quantized) / denominator);
    result.normalized_frame_coordinate =
        static_cast<double>(result.frame_zero) + result.fraction;
    return result;
}

[[nodiscard]] StudioQuaternion
normalize_quaternion(StudioQuaternion value) noexcept
{
    const auto length_squared = static_cast<double>(value.x) * value.x +
                                static_cast<double>(value.y) * value.y +
                                static_cast<double>(value.z) * value.z +
                                static_cast<double>(value.w) * value.w;
    if (!std::isfinite(length_squared) || length_squared <= 0.0) {
        return StudioQuaternion{std::numeric_limits<float>::quiet_NaN(), 0.0F,
                                0.0F, 0.0F};
    }
    const auto inverse = static_cast<float>(1.0 / std::sqrt(length_squared));
    value.x *= inverse;
    value.y *= inverse;
    value.z *= inverse;
    value.w *= inverse;
    return value;
}

[[nodiscard]] StudioQuaternion
quaternion_from_euler(const assets::AssetVector3& radians) noexcept
{
    const auto half_x = radians.x * 0.5F;
    const auto half_y = radians.y * 0.5F;
    const auto half_z = radians.z * 0.5F;
    const auto sx = std::sin(half_x);
    const auto cx = std::cos(half_x);
    const auto sy = std::sin(half_y);
    const auto cy = std::cos(half_y);
    const auto sz = std::sin(half_z);
    const auto cz = std::cos(half_z);
    return normalize_quaternion({
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    });
}

[[nodiscard]] StudioQuaternion slerp(const StudioQuaternion& left,
                                     StudioQuaternion right,
                                     const float fraction) noexcept
{
    auto normalized_left = normalize_quaternion(left);
    right = normalize_quaternion(right);
    auto dot = normalized_left.x * right.x + normalized_left.y * right.y +
               normalized_left.z * right.z + normalized_left.w * right.w;
    if (dot < 0.0F) {
        dot = -dot;
        right.x = -right.x;
        right.y = -right.y;
        right.z = -right.z;
        right.w = -right.w;
    }
    dot = std::clamp(dot, -1.0F, 1.0F);
    if (dot > 0.9995F) {
        return normalize_quaternion({
            normalized_left.x + fraction * (right.x - normalized_left.x),
            normalized_left.y + fraction * (right.y - normalized_left.y),
            normalized_left.z + fraction * (right.z - normalized_left.z),
            normalized_left.w + fraction * (right.w - normalized_left.w),
        });
    }
    const auto angle = std::acos(dot);
    const auto sine = std::sin(angle);
    if (!std::isfinite(angle) || !std::isfinite(sine) || sine == 0.0F) {
        return normalized_left;
    }
    const auto left_weight = std::sin((1.0F - fraction) * angle) / sine;
    const auto right_weight = std::sin(fraction * angle) / sine;
    return normalize_quaternion({
        normalized_left.x * left_weight + right.x * right_weight,
        normalized_left.y * left_weight + right.y * right_weight,
        normalized_left.z * left_weight + right.z * right_weight,
        normalized_left.w * left_weight + right.w * right_weight,
    });
}

[[nodiscard]] assets::AssetVector3
lerp_vector(const assets::AssetVector3& left, const assets::AssetVector3& right,
            const float fraction) noexcept
{
    return {
        left.x + fraction * (right.x - left.x),
        left.y + fraction * (right.y - left.y),
        left.z + fraction * (right.z - left.z),
    };
}

[[nodiscard]] ModelBoneLocalPose
blend_local_pose(const ModelBoneLocalPose& left,
                 const ModelBoneLocalPose& right, const float fraction) noexcept
{
    return {
        lerp_vector(left.translation, right.translation, fraction),
        slerp(left.rotation, right.rotation, fraction),
    };
}

[[nodiscard]] StudioMatrix3x4
local_matrix(const ModelBoneLocalPose& pose) noexcept
{
    const auto q = normalize_quaternion(pose.rotation);
    const auto xx = q.x * q.x;
    const auto yy = q.y * q.y;
    const auto zz = q.z * q.z;
    const auto xy = q.x * q.y;
    const auto xz = q.x * q.z;
    const auto yz = q.y * q.z;
    const auto wx = q.w * q.x;
    const auto wy = q.w * q.y;
    const auto wz = q.w * q.z;
    return StudioMatrix3x4{{
        1.0F - 2.0F * (yy + zz),
        2.0F * (xy - wz),
        2.0F * (xz + wy),
        pose.translation.x,
        2.0F * (xy + wz),
        1.0F - 2.0F * (xx + zz),
        2.0F * (yz - wx),
        pose.translation.y,
        2.0F * (xz - wy),
        2.0F * (yz + wx),
        1.0F - 2.0F * (xx + yy),
        pose.translation.z,
    }};
}

[[nodiscard]] StudioMatrix3x4 concatenate(const StudioMatrix3x4& parent,
                                          const StudioMatrix3x4& local) noexcept
{
    StudioMatrix3x4 result;
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
            result.values[row * 4U + column] =
                parent.values[row * 4U] * local.values[column] +
                parent.values[row * 4U + 1U] * local.values[4U + column] +
                parent.values[row * 4U + 2U] * local.values[8U + column];
        }
        result.values[row * 4U + 3U] =
            parent.values[row * 4U] * local.values[3U] +
            parent.values[row * 4U + 1U] * local.values[7U] +
            parent.values[row * 4U + 2U] * local.values[11U] +
            parent.values[row * 4U + 3U];
    }
    return result;
}

[[nodiscard]] std::array<float, 9U>
matrix_rotation(const StudioMatrix3x4& matrix) noexcept
{
    return {
        matrix.values[0U], matrix.values[1U], matrix.values[2U],
        matrix.values[4U], matrix.values[5U], matrix.values[6U],
        matrix.values[8U], matrix.values[9U], matrix.values[10U],
    };
}

struct BlendPose {
    std::vector<ModelBoneLocalPose> bones;
    std::size_t channel_samples{0U};
    std::size_t runs_examined{0U};
};

[[nodiscard]] std::optional<StudioPoseError>
validate_track(const assets::ModelBoneAnimationTrack& track,
               const std::size_t bone_index,
               const assets::ModelSequence& sequence)
{
    if (track.bone_index != bone_index) {
        return make_error(
            StudioPoseErrorCode::invalid_animation_track,
            "Animation track bone ordinal does not match source order",
            std::nullopt, static_cast<std::uint32_t>(bone_index));
    }
    for (std::size_t channel_index = 0U; channel_index < 6U; ++channel_index) {
        const auto& channel = track.channels[channel_index];
        if (static_cast<std::size_t>(channel.semantic) != channel_index ||
            channel.frame_coverage != sequence.frame_count ||
            channel.source_sequence_group_ordinal !=
                sequence.sequence_group_index ||
            !std::isfinite(channel.source_default) ||
            !std::isfinite(channel.source_scale)) {
            return make_error(StudioPoseErrorCode::invalid_animation_track,
                              "Animation channel metadata disagrees with its "
                              "sequence/bone slot",
                              std::nullopt,
                              static_cast<std::uint32_t>(bone_index));
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<StudioPoseError>
evaluate_blend_pose(const assets::SkeletalModelAssetData& model,
                    const assets::ModelSequence& sequence,
                    const assets::ModelAnimationBlend& blend,
                    const StudioPoseFrameSample& frame,
                    const StudioBoneControllerAdjustments& controllers,
                    const StudioPoseEvaluationLimits& limits, BlendPose& output)
{
    if (blend.bone_tracks.size() != model.bones.size()) {
        return make_error(
            StudioPoseErrorCode::invalid_animation_track,
            "Animation blend does not contain one track per bone");
    }
    output.bones.reserve(model.bones.size());
    const auto sample_coordinate =
        static_cast<double>(frame.frame_zero) + frame.fraction;
    for (std::size_t bone_index = 0U; bone_index < model.bones.size();
         ++bone_index) {
        const auto& track = blend.bone_tracks[bone_index];
        if (const auto error = validate_track(track, bone_index, sequence)) {
            return error;
        }
        std::array<float, 6U> values{};
        for (std::size_t channel_index = 0U; channel_index < 6U;
             ++channel_index) {
            const auto sampled = StudioFractionalAnimationChannelSampler::
                sample_fractional_frame(track.channels[channel_index],
                                        sample_coordinate,
                                        limits.maximum_channel_runs_per_sample);
            if (!sampled || !sampled.sample) {
                auto error = sampled.error.value_or(
                    make_error(StudioPoseErrorCode::channel_sample_failed,
                               "Animation channel could not be sampled"));
                error.bone_index = static_cast<std::uint32_t>(bone_index);
                return error;
            }
            values[channel_index] = sampled.sample->interpolated_value;
            ++output.channel_samples;
            if (sampled.sample->runs_examined >
                std::numeric_limits<std::size_t>::max() -
                    output.runs_examined) {
                return make_error(
                    StudioPoseErrorCode::channel_run_limit_exceeded,
                    "Animation run accounting overflowed", std::nullopt,
                    static_cast<std::uint32_t>(bone_index));
            }
            output.runs_examined += sampled.sample->runs_examined;
        }

        const auto& bone = model.bones[bone_index];
        for (std::size_t channel_index = 0U; channel_index < 6U;
             ++channel_index) {
            const auto controller_index =
                bone.controller_indices[channel_index];
            if (controller_index < 0) {
                continue;
            }
            const auto index = static_cast<std::size_t>(controller_index);
            if (index >= controllers.values_by_controller_record.size()) {
                return make_error(StudioPoseErrorCode::invalid_controller,
                                  "Bone controller slot is outside the "
                                  "evaluated adjustment table",
                                  std::nullopt,
                                  static_cast<std::uint32_t>(bone_index));
            }
            values[channel_index] +=
                controllers.values_by_controller_record[index];
        }
        const assets::AssetVector3 translation{values[0U], values[1U],
                                               values[2U]};
        const assets::AssetVector3 rotation{values[3U], values[4U], values[5U]};
        const auto quaternion = quaternion_from_euler(rotation);
        if (!finite_vector(translation) || !finite_quaternion(quaternion)) {
            return make_error(StudioPoseErrorCode::non_finite_pose,
                              "Sampled local bone transform is non-finite",
                              std::nullopt,
                              static_cast<std::uint32_t>(bone_index));
        }
        output.bones.push_back(ModelBoneLocalPose{translation, quaternion});
    }
    return std::nullopt;
}

[[nodiscard]] bool
valid_model_identity(const StudioPoseModelIdentity& identity) noexcept
{
    return !identity.resource_identity.empty() &&
           identity.resource_identity.size() <= 1'024U &&
           identity.resource_identity.find('\\') == std::string::npos &&
           identity.resource_identity.find(':') == std::string::npos &&
           identity.resource_identity.front() != '/';
}

struct CacheKey {
    StudioPoseModelIdentity identity;
    std::uint32_t sequence_index{0U};
    std::uint32_t frame_zero{0U};
    std::uint16_t frame_fraction{0U};
    std::int32_t body_value{0};
    std::uint32_t skin_family_index{0U};
    std::array<std::uint8_t, 4U> controller_values{};
    std::array<std::uint8_t, 2U> blending_values{};
    std::uint8_t mouth_value{0U};
    std::array<std::uint32_t, 3U> entity_scale_bits{};
    StudioPoseCompatibilityProfile compatibility_profile{
        StudioPoseCompatibilityProfile::synthetic_explicit_v1};

    [[nodiscard]] friend bool operator==(const CacheKey&,
                                         const CacheKey&) = default;
};

[[nodiscard]] std::optional<CacheKey>
make_cache_key(const StudioPoseModelIdentity& identity,
               const assets::SkeletalModelAssetData& model,
               const StudioPoseInput& input)
{
    if (!valid_model_identity(identity) ||
        input.sequence_index >= model.sequences.size()) {
        return std::nullopt;
    }
    const auto frame = normalize_frame(model.sequences[input.sequence_index],
                                       input.frame_coordinate);
    if (!frame) {
        return std::nullopt;
    }
    return CacheKey{
        identity,
        input.sequence_index,
        frame->frame_zero,
        frame->quantized_fraction,
        input.body_value,
        input.skin_family_index,
        input.controller_values,
        input.blending_values,
        input.mouth_value,
        {
            std::bit_cast<std::uint32_t>(input.entity_scale.x),
            std::bit_cast<std::uint32_t>(input.entity_scale.y),
            std::bit_cast<std::uint32_t>(input.entity_scale.z),
        },
        input.compatibility_profile,
    };
}

} // namespace

bool valid_studio_pose_evaluation_limits(
    const StudioPoseEvaluationLimits& limits) noexcept
{
    return limits.maximum_bones > 0U &&
           limits.maximum_bones <= kHardMaximumStudioPoseBones &&
           limits.maximum_instances > 0U &&
           limits.maximum_instances <= kHardMaximumStudioPoseInstances &&
           limits.maximum_pose_matrices > 0U &&
           limits.maximum_pose_matrices <= kHardMaximumStudioPoseMatrices &&
           limits.maximum_blend_evaluations > 0U &&
           limits.maximum_blend_evaluations <=
               kHardMaximumStudioBlendEvaluations &&
           limits.maximum_channel_runs_per_sample > 0U &&
           limits.maximum_channel_runs_per_sample <=
               kHardMaximumStudioChannelRunsPerSample &&
           limits.maximum_pose_bytes > 0U &&
           limits.maximum_pose_bytes <= kHardMaximumStudioPoseBytes &&
           limits.maximum_pose_cache_entries > 0U &&
           limits.maximum_pose_cache_entries <=
               kHardMaximumStudioPoseCacheEntries &&
           std::isfinite(limits.maximum_entity_scale) &&
           limits.maximum_entity_scale > 0.0F &&
           limits.maximum_entity_scale <= 65'536.0F;
}

StudioFractionalChannelSampleResult
StudioFractionalAnimationChannelSampler::sample_fractional_frame(
    const assets::ModelAnimationChannel& channel, const double frame_coordinate,
    const std::size_t maximum_runs_per_sample) noexcept
{
    if (!std::isfinite(frame_coordinate) || frame_coordinate < 0.0 ||
        maximum_runs_per_sample == 0U || channel.frame_coverage == 0U ||
        frame_coordinate > static_cast<double>(channel.frame_coverage - 1U) ||
        !std::isfinite(channel.source_default) ||
        !std::isfinite(channel.source_scale)) {
        return fail_sample(StudioPoseErrorCode::invalid_frame,
                           "Fractional animation sample is outside its finite "
                           "channel coverage");
    }
    const auto frame_zero =
        static_cast<std::uint32_t>(std::floor(frame_coordinate));
    const auto frame_one =
        std::min(frame_zero + 1U, channel.frame_coverage - 1U);
    const auto fraction = static_cast<float>(frame_coordinate - frame_zero);
    const auto zero =
        sample_quantized_bounded(channel, frame_zero, maximum_runs_per_sample);
    const auto remaining =
        maximum_runs_per_sample -
        std::min(maximum_runs_per_sample, zero.runs_examined);
    const auto one =
        frame_one == frame_zero
            ? zero
            : sample_quantized_bounded(channel, frame_one, remaining);
    if (zero.run_limit_exceeded || one.run_limit_exceeded) {
        return fail_sample(
            StudioPoseErrorCode::channel_run_limit_exceeded,
            "Fractional animation sampling exceeded its run bound");
    }
    if (!zero.value || !one.value) {
        return fail_sample(
            StudioPoseErrorCode::channel_sample_failed,
            "Fractional animation channel metadata is inconsistent");
    }
    const auto scaled_zero =
        channel.source_default +
        static_cast<float>(*zero.value) * channel.source_scale;
    const auto scaled_one =
        channel.source_default +
        static_cast<float>(*one.value) * channel.source_scale;
    const auto interpolated =
        scaled_zero + fraction * (scaled_one - scaled_zero);
    if (!std::isfinite(scaled_zero) || !std::isfinite(scaled_one) ||
        !std::isfinite(interpolated)) {
        return fail_sample(
            StudioPoseErrorCode::non_finite_pose,
            "Fractional animation sample produced a non-finite value");
    }
    return {
        StudioFractionalChannelSample{
            frame_zero,
            frame_one,
            fraction,
            *zero.value,
            *one.value,
            scaled_zero,
            scaled_one,
            interpolated,
            zero.runs_examined +
                (frame_one == frame_zero ? 0U : one.runs_examined),
        },
        std::nullopt,
    };
}

StudioBoneControllerEvaluationResult StudioBoneControllerEvaluator::evaluate(
    const assets::SkeletalModelAssetData& model,
    const std::array<std::uint8_t, 4U>& controller_values,
    const std::uint8_t mouth_value) const
{
    try {
        StudioBoneControllerAdjustments result;
        result.values_by_controller_record.reserve(
            model.bone_controllers.size());
        for (std::size_t index = 0U; index < model.bone_controllers.size();
             ++index) {
            const auto& controller = model.bone_controllers[index];
            const auto axis =
                controller.source_type & kGoldSrcStudioControllerTypes;
            if ((controller.source_type & ~(kGoldSrcStudioControllerTypes |
                                            kGoldSrcStudioControllerWrap)) !=
                    0U ||
                axis == 0U || (axis & (axis - 1U)) != 0U) {
                return {
                    std::nullopt,
                    make_error(
                        StudioPoseErrorCode::unsupported_controller_type,
                        "Bone controller has unsupported source type bits"),
                };
            }
            if (controller.controller_index < 0 ||
                controller.controller_index > 4 ||
                !std::isfinite(controller.start) ||
                !std::isfinite(controller.end) || controller.bone_index < -1 ||
                (controller.bone_index >= 0 &&
                 static_cast<std::size_t>(controller.bone_index) >=
                     model.bones.size())) {
                return {
                    std::nullopt,
                    make_error(StudioPoseErrorCode::invalid_controller,
                               "Bone controller metadata is invalid"),
                };
            }

            float value = 0.0F;
            if (controller.controller_index <= 3) {
                const auto raw = controller_values[static_cast<std::size_t>(
                    controller.controller_index)];
                if (controller.wraps_shortest_distance) {
                    value = static_cast<float>(raw) * (360.0F / 256.0F) +
                            controller.start;
                } else {
                    const auto fraction = std::clamp(
                        static_cast<float>(raw) / 255.0F, 0.0F, 1.0F);
                    value = (1.0F - fraction) * controller.start +
                            fraction * controller.end;
                }
            } else {
                const auto fraction =
                    std::min(static_cast<float>(mouth_value) / 64.0F, 1.0F);
                value = (1.0F - fraction) * controller.start +
                        fraction * controller.end;
            }
            const bool rotation = axis == kStudioControllerRotationX ||
                                  axis == kStudioControllerRotationY ||
                                  axis == kStudioControllerRotationZ;
            const bool translation = axis == kStudioControllerTranslationX ||
                                     axis == kStudioControllerTranslationY ||
                                     axis == kStudioControllerTranslationZ;
            if (!rotation && !translation) {
                return {
                    std::nullopt,
                    make_error(StudioPoseErrorCode::unsupported_controller_type,
                               "Bone controller axis is unsupported"),
                };
            }
            if (rotation) {
                value *= kDegreesToRadians;
            }
            if (!std::isfinite(value)) {
                return {
                    std::nullopt,
                    make_error(
                        StudioPoseErrorCode::non_finite_pose,
                        "Bone controller produced a non-finite adjustment"),
                };
            }
            result.values_by_controller_record.push_back(value);
        }
        return {std::move(result), std::nullopt};
    } catch (const std::bad_alloc&) {
        return {
            std::nullopt,
            make_error(StudioPoseErrorCode::unable_to_retain_pose,
                       "Unable to retain controller adjustments"),
        };
    } catch (const std::length_error&) {
        return {
            std::nullopt,
            make_error(StudioPoseErrorCode::unable_to_retain_pose,
                       "Controller adjustment count is invalid"),
        };
    }
}

StudioBodySelectionResult
StudioBodyPartSelector::select(const assets::SkeletalModelAssetData& model,
                               const std::int32_t body_value) const
{
    if (body_value < 0) {
        return {
            std::nullopt,
            make_error(StudioPoseErrorCode::invalid_body_value,
                       "Studio body value must be nonnegative"),
        };
    }
    try {
        StudioBodySelection result;
        result.bodyparts.reserve(model.bodyparts.size());
        for (std::size_t bodypart_index = 0U;
             bodypart_index < model.bodyparts.size(); ++bodypart_index) {
            const auto& bodypart = model.bodyparts[bodypart_index];
            if (bodypart.base <= 0 || bodypart.submodel_indices.empty()) {
                return {
                    std::nullopt,
                    make_error(StudioPoseErrorCode::invalid_body_value,
                               "Studio bodypart has no positive base or "
                               "selectable models"),
                };
            }
            const auto quotient = body_value / bodypart.base;
            const auto local_index = static_cast<std::size_t>(quotient) %
                                     bodypart.submodel_indices.size();
            const auto submodel_index = bodypart.submodel_indices[local_index];
            if (submodel_index >= model.submodels.size()) {
                return {
                    std::nullopt,
                    make_error(StudioPoseErrorCode::invalid_body_value,
                               "Studio bodypart selects an invalid submodel"),
                };
            }
            result.bodyparts.push_back(StudioBodyPartSelection{
                static_cast<std::uint32_t>(bodypart_index),
                static_cast<std::uint32_t>(local_index),
                submodel_index,
            });
        }
        return {std::move(result), std::nullopt};
    } catch (const std::bad_alloc&) {
        return {
            std::nullopt,
            make_error(StudioPoseErrorCode::unable_to_retain_pose,
                       "Unable to retain bodypart selection"),
        };
    } catch (const std::length_error&) {
        return {
            std::nullopt,
            make_error(StudioPoseErrorCode::unable_to_retain_pose,
                       "Bodypart selection count is invalid"),
        };
    }
}

std::optional<std::uint32_t> StudioSkinSelection::texture_for_mesh(
    const assets::ModelMesh& mesh) const noexcept
{
    if (mesh.skin_reference_slot >= texture_indices_by_skin_reference.size()) {
        return std::nullopt;
    }
    return texture_indices_by_skin_reference[mesh.skin_reference_slot];
}

StudioSkinSelectionResult
StudioSkinSelector::select(const assets::SkeletalModelAssetData& model,
                           const std::uint32_t skin_family_index) const
{
    if (skin_family_index >= model.skin_families.size()) {
        return {
            std::nullopt,
            make_error(
                StudioPoseErrorCode::invalid_skin_family,
                "Studio skin-family index is outside the imported table"),
        };
    }
    try {
        StudioSkinSelection result;
        result.family_index = skin_family_index;
        result.texture_indices_by_skin_reference =
            model.skin_families[skin_family_index].texture_indices;
        for (const auto texture_index :
             result.texture_indices_by_skin_reference) {
            if (texture_index >= model.textures.size()) {
                return {
                    std::nullopt,
                    make_error(
                        StudioPoseErrorCode::invalid_skin_family,
                        "Selected Studio skin family contains an invalid "
                        "texture index"),
                };
            }
        }
        return {std::move(result), std::nullopt};
    } catch (const std::bad_alloc&) {
        return {
            std::nullopt,
            make_error(StudioPoseErrorCode::unable_to_retain_pose,
                       "Unable to retain skin-family selection"),
        };
    } catch (const std::length_error&) {
        return {
            std::nullopt,
            make_error(StudioPoseErrorCode::unable_to_retain_pose,
                       "Skin-family selection length is invalid"),
        };
    }
}

StudioPoseState::StudioPoseState(
    StudioPoseModelIdentity model_identity, const std::uint32_t sequence_index,
    const StudioPoseFrameSample frame_sample,
    const assets::AssetVector3 entity_scale,
    std::vector<ModelBoneLocalPose> local_bones,
    std::vector<ModelBoneWorldPose> world_bones,
    StudioBodySelection body_selection, StudioSkinSelection skin_selection,
    const StudioPoseStatistics statistics,
    const StudioPoseCompatibilityProfile compatibility_profile) noexcept
    : model_identity_{std::move(model_identity)},
      sequence_index_{sequence_index}, frame_sample_{frame_sample},
      entity_scale_{entity_scale}, local_bones_{std::move(local_bones)},
      world_bones_{std::move(world_bones)},
      body_selection_{std::move(body_selection)},
      skin_selection_{std::move(skin_selection)}, statistics_{statistics},
      compatibility_profile_{compatibility_profile}
{
}

const StudioPoseModelIdentity& StudioPoseState::model_identity() const noexcept
{
    return model_identity_;
}

std::uint32_t StudioPoseState::sequence_index() const noexcept
{
    return sequence_index_;
}

const StudioPoseFrameSample& StudioPoseState::frame_sample() const noexcept
{
    return frame_sample_;
}

const assets::AssetVector3& StudioPoseState::entity_scale() const noexcept
{
    return entity_scale_;
}

std::span<const ModelBoneLocalPose>
StudioPoseState::local_bones() const noexcept
{
    return local_bones_;
}

std::span<const ModelBoneWorldPose>
StudioPoseState::world_bones() const noexcept
{
    return world_bones_;
}

const StudioBodySelection& StudioPoseState::body_selection() const noexcept
{
    return body_selection_;
}

const StudioSkinSelection& StudioPoseState::skin_selection() const noexcept
{
    return skin_selection_;
}

const StudioPoseStatistics& StudioPoseState::statistics() const noexcept
{
    return statistics_;
}

StudioPoseCompatibilityProfile
StudioPoseState::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

StudioPoseEvaluationResult
StudioPoseEvaluator::evaluate(const StudioPoseModelIdentity& model_identity,
                              const assets::SkeletalModelAssetData& model,
                              const StudioPoseInput& input,
                              const StudioPoseEvaluationLimits& limits) const
{
    if (!valid_studio_pose_evaluation_limits(limits)) {
        return fail_pose(StudioPoseErrorCode::invalid_configuration,
                         "Studio pose limits are invalid");
    }
    if (input.compatibility_profile ==
        StudioPoseCompatibilityProfile::
            stock_entity_projection_evidence_pending) {
        return fail_pose(
            StudioPoseErrorCode::evidence_pending,
            "Stock entity-to-pose projection remains evidence-pending");
    }
    if (!valid_model_identity(model_identity)) {
        return fail_pose(
            StudioPoseErrorCode::invalid_model_identity,
            "Studio pose model identity must be a bounded virtual identity");
    }
    if (model.compatibility_profile !=
            assets::ModelSkeletalCompatibilityProfile::goldsrc_studio_v10 ||
        model.evidence_profile !=
            assets::ModelSkeletalEvidenceProfile::public_valve_wire_profile) {
        return fail_pose(
            StudioPoseErrorCode::invalid_model,
            "Studio pose evaluator requires the imported GoldSrc v10 profile");
    }
    if (model.bones.size() > limits.maximum_bones) {
        return fail_pose(StudioPoseErrorCode::bone_limit_exceeded,
                         "Studio pose bone count exceeds the configured limit");
    }
    if (model.bones.size() > limits.maximum_pose_matrices) {
        return fail_pose(
            StudioPoseErrorCode::matrix_limit_exceeded,
            "Studio pose world-matrix count exceeds the configured limit");
    }
    if (input.sequence_index >= model.sequences.size()) {
        return fail_pose(StudioPoseErrorCode::invalid_sequence,
                         "Studio sequence index is outside the imported model",
                         input.sequence_index);
    }
    if (!finite_vector(input.entity_scale) || input.entity_scale.x <= 0.0F ||
        input.entity_scale.y <= 0.0F || input.entity_scale.z <= 0.0F ||
        input.entity_scale.x > limits.maximum_entity_scale ||
        input.entity_scale.y > limits.maximum_entity_scale ||
        input.entity_scale.z > limits.maximum_entity_scale) {
        return fail_pose(
            StudioPoseErrorCode::invalid_configuration,
            "Explicit entity scale is non-finite, non-positive, or too large",
            input.sequence_index);
    }

    const auto& sequence = model.sequences[input.sequence_index];
    if (sequence.frame_count == 0U ||
        sequence.sequence_group_index >= model.sequence_groups.size() ||
        sequence.motion_bone < (model.bones.empty() ? -1 : 0) ||
        (sequence.motion_bone >= 0 &&
         static_cast<std::size_t>(sequence.motion_bone) >=
             model.bones.size())) {
        return fail_pose(
            StudioPoseErrorCode::invalid_sequence,
            "Studio sequence frame/group/motion-bone metadata is invalid",
            input.sequence_index);
    }
    if (model.sequence_groups[sequence.sequence_group_index]
            .source_group_ordinal != sequence.sequence_group_index) {
        return fail_pose(
            StudioPoseErrorCode::missing_sequence_group,
            "Required Studio sequence group is not present under its ordinal",
            input.sequence_index);
    }
    if (sequence.blend_count != 1U && sequence.blend_count != 2U &&
        sequence.blend_count != 4U) {
        return fail_pose(
            StudioPoseErrorCode::unsupported_blend_count,
            "Studio pose supports exactly one, two, or four sequence blends",
            input.sequence_index);
    }
    if (sequence.blend_count > limits.maximum_blend_evaluations) {
        return fail_pose(StudioPoseErrorCode::blend_evaluation_limit_exceeded,
                         "Studio sequence blend count exceeds the pose limit",
                         input.sequence_index);
    }
    if (sequence.animation_blends.size() != sequence.blend_count) {
        return fail_pose(
            StudioPoseErrorCode::invalid_animation_track,
            "Studio sequence does not retain its declared blend count",
            input.sequence_index);
    }
    const auto frame = normalize_frame(sequence, input.frame_coordinate);
    if (!frame) {
        return fail_pose(StudioPoseErrorCode::invalid_frame,
                         "Studio frame coordinate cannot be normalized safely",
                         input.sequence_index);
    }

    const auto controllers = StudioBoneControllerEvaluator{}.evaluate(
        model, input.controller_values, input.mouth_value);
    if (!controllers || !controllers.adjustments) {
        return {
            std::nullopt,
            controllers.error.value_or(
                make_error(StudioPoseErrorCode::invalid_controller,
                           "Studio bone controllers could not be evaluated",
                           input.sequence_index)),
        };
    }
    const auto body = StudioBodyPartSelector{}.select(model, input.body_value);
    if (!body || !body.selection) {
        return {
            std::nullopt,
            body.error.value_or(make_error(
                StudioPoseErrorCode::invalid_body_value,
                "Studio bodypart selection failed", input.sequence_index)),
        };
    }
    const auto skin =
        StudioSkinSelector{}.select(model, input.skin_family_index);
    if (!skin || !skin.selection) {
        return {
            std::nullopt,
            skin.error.value_or(make_error(
                StudioPoseErrorCode::invalid_skin_family,
                "Studio skin-family selection failed", input.sequence_index)),
        };
    }

    try {
        std::vector<BlendPose> blend_poses;
        blend_poses.reserve(sequence.blend_count);
        for (const auto& blend : sequence.animation_blends) {
            BlendPose pose;
            if (const auto error = evaluate_blend_pose(
                    model, sequence, blend, *frame, *controllers.adjustments,
                    limits, pose)) {
                auto retained_error = *error;
                retained_error.sequence_index = input.sequence_index;
                return {std::nullopt, std::move(retained_error)};
            }
            blend_poses.push_back(std::move(pose));
        }

        auto local_bones = std::move(blend_poses[0U].bones);
        const auto blend_x =
            static_cast<float>(input.blending_values[0U]) / 255.0F;
        const auto blend_y =
            static_cast<float>(input.blending_values[1U]) / 255.0F;
        if (sequence.blend_count >= 2U) {
            for (std::size_t bone = 0U; bone < local_bones.size(); ++bone) {
                local_bones[bone] = blend_local_pose(
                    local_bones[bone], blend_poses[1U].bones[bone], blend_x);
            }
        }
        if (sequence.blend_count == 4U) {
            for (std::size_t bone = 0U; bone < local_bones.size(); ++bone) {
                const auto second_row =
                    blend_local_pose(blend_poses[2U].bones[bone],
                                     blend_poses[3U].bones[bone], blend_x);
                local_bones[bone] =
                    blend_local_pose(local_bones[bone], second_row, blend_y);
            }
        }

        std::size_t suppressed_axes = 0U;
        if (sequence.motion_bone >= 0) {
            auto& translation =
                local_bones[static_cast<std::size_t>(sequence.motion_bone)]
                    .translation;
            const auto motion_type =
                std::bit_cast<std::uint32_t>(sequence.motion_type);
            if ((motion_type & kStudioMotionX) != 0U) {
                translation.x = 0.0F;
                ++suppressed_axes;
            }
            if ((motion_type & kStudioMotionY) != 0U) {
                translation.y = 0.0F;
                ++suppressed_axes;
            }
            if ((motion_type & kStudioMotionZ) != 0U) {
                translation.z = 0.0F;
                ++suppressed_axes;
            }
        }

        std::vector<ModelBoneWorldPose> world_bones(model.bones.size());
        std::vector<std::uint8_t> visit_state(model.bones.size(), 0U);
        std::vector<std::size_t> chain;
        chain.reserve(model.bones.size());
        for (std::size_t start = 0U; start < model.bones.size(); ++start) {
            if (visit_state[start] == 2U) {
                continue;
            }
            chain.clear();
            auto cursor = start;
            while (visit_state[cursor] != 2U) {
                if (visit_state[cursor] == 1U) {
                    return fail_pose(StudioPoseErrorCode::invalid_hierarchy,
                                     "Studio bone hierarchy contains a cycle",
                                     input.sequence_index,
                                     static_cast<std::uint32_t>(cursor));
                }
                visit_state[cursor] = 1U;
                chain.push_back(cursor);
                const auto parent = model.bones[cursor].parent_index;
                if (parent == -1) {
                    break;
                }
                if (parent < 0 ||
                    static_cast<std::size_t>(parent) >= model.bones.size()) {
                    return fail_pose(StudioPoseErrorCode::invalid_hierarchy,
                                     "Studio bone parent index is invalid",
                                     input.sequence_index,
                                     static_cast<std::uint32_t>(cursor));
                }
                cursor = static_cast<std::size_t>(parent);
            }
            for (auto iterator = chain.rbegin(); iterator != chain.rend();
                 ++iterator) {
                const auto bone_index = *iterator;
                const auto matrix = local_matrix(local_bones[bone_index]);
                const auto parent = model.bones[bone_index].parent_index;
                world_bones[bone_index].transform =
                    parent == -1
                        ? matrix
                        : concatenate(
                              world_bones[static_cast<std::size_t>(parent)]
                                  .transform,
                              matrix);
                if (!finite_matrix(world_bones[bone_index].transform)) {
                    return fail_pose(StudioPoseErrorCode::non_finite_pose,
                                     "Studio world bone matrix is non-finite",
                                     input.sequence_index,
                                     static_cast<std::uint32_t>(bone_index));
                }
                world_bones[bone_index].normal_rotation =
                    matrix_rotation(world_bones[bone_index].transform);
                visit_state[bone_index] = 2U;
            }
        }

        StudioPoseStatistics statistics;
        statistics.bone_count = model.bones.size();
        statistics.local_pose_count = local_bones.size();
        statistics.world_matrix_count = world_bones.size();
        statistics.blend_evaluation_count = blend_poses.size();
        statistics.controller_count = model.bone_controllers.size();
        statistics.bodypart_selection_count = body.selection->bodyparts.size();
        statistics.skin_reference_count =
            skin.selection->texture_indices_by_skin_reference.size();
        statistics.suppressed_motion_axis_count = suppressed_axes;
        for (const auto& pose : blend_poses) {
            statistics.channel_sample_count += pose.channel_samples;
            statistics.channel_runs_examined += pose.runs_examined;
        }

        std::size_t local_bytes = 0U;
        std::size_t world_bytes = 0U;
        std::size_t body_bytes = 0U;
        std::size_t skin_bytes = 0U;
        std::size_t total_bytes = model_identity.resource_identity.size();
        if (!checked_multiply(local_bones.size(), sizeof(ModelBoneLocalPose),
                              local_bytes) ||
            !checked_multiply(world_bones.size(), sizeof(ModelBoneWorldPose),
                              world_bytes) ||
            !checked_multiply(body.selection->bodyparts.size(),
                              sizeof(StudioBodyPartSelection), body_bytes) ||
            !checked_multiply(
                skin.selection->texture_indices_by_skin_reference.size(),
                sizeof(std::uint16_t), skin_bytes) ||
            !checked_add(total_bytes, local_bytes, total_bytes) ||
            !checked_add(total_bytes, world_bytes, total_bytes) ||
            !checked_add(total_bytes, body_bytes, total_bytes) ||
            !checked_add(total_bytes, skin_bytes, total_bytes) ||
            total_bytes > limits.maximum_pose_bytes) {
            return fail_pose(StudioPoseErrorCode::pose_byte_limit_exceeded,
                             "Studio pose exceeds its accounted byte limit",
                             input.sequence_index);
        }
        statistics.accounted_pose_bytes = total_bytes;
        return {
            StudioPoseState{
                model_identity,
                input.sequence_index,
                *frame,
                input.entity_scale,
                std::move(local_bones),
                std::move(world_bones),
                std::move(*body.selection),
                std::move(*skin.selection),
                statistics,
                input.compatibility_profile,
            },
            std::nullopt,
        };
    } catch (const std::bad_alloc&) {
        return fail_pose(StudioPoseErrorCode::unable_to_retain_pose,
                         "Unable to allocate Studio pose state",
                         input.sequence_index);
    } catch (const std::length_error&) {
        return fail_pose(StudioPoseErrorCode::unable_to_retain_pose,
                         "Studio pose container length is invalid",
                         input.sequence_index);
    }
}

struct StudioPoseCache::Entry {
    CacheKey key;
    std::shared_ptr<const StudioPoseState> pose;
};

StudioPoseCache::StudioPoseCache(StudioPoseEvaluationLimits limits)
    : limits_{std::move(limits)}
{
}

StudioPoseCache::~StudioPoseCache() = default;

bool StudioPoseCache::valid_configuration() const noexcept
{
    return valid_studio_pose_evaluation_limits(limits_);
}

void StudioPoseCache::reset_for_frame(const std::uint64_t frame_token) noexcept
{
    entries_.clear();
    frame_started_ = true;
    statistics_ = {};
    statistics_.frame_token = frame_token;
}

StudioPoseCacheResult
StudioPoseCache::find_or_evaluate(const StudioPoseModelIdentity& model_identity,
                                  const assets::SkeletalModelAssetData& model,
                                  const StudioPoseInput& input)
{
    if (!valid_configuration() || !frame_started_) {
        return {
            {},
            make_error(
                StudioPoseErrorCode::invalid_configuration,
                "Pose cache requires valid limits and an explicit frame reset"),
            StudioPoseCacheLookupStatus::inserted,
        };
    }
    if (statistics_.instance_request_count >= limits_.maximum_instances) {
        return {
            {},
            make_error(StudioPoseErrorCode::instance_limit_exceeded,
                       "Per-frame Studio pose instance limit exceeded"),
            StudioPoseCacheLookupStatus::inserted,
        };
    }
    ++statistics_.instance_request_count;
    std::optional<CacheKey> key;
    try {
        key = make_cache_key(model_identity, model, input);
    } catch (const std::bad_alloc&) {
        return {
            {},
            make_error(StudioPoseErrorCode::unable_to_retain_pose,
                       "Unable to retain Studio pose cache key"),
            StudioPoseCacheLookupStatus::inserted,
        };
    } catch (const std::length_error&) {
        return {
            {},
            make_error(StudioPoseErrorCode::unable_to_retain_pose,
                       "Studio pose cache key length is invalid"),
            StudioPoseCacheLookupStatus::inserted,
        };
    }
    if (!key) {
        return {
            {},
            make_error(StudioPoseErrorCode::invalid_configuration,
                       "Unable to derive a bounded Studio pose cache key"),
            StudioPoseCacheLookupStatus::inserted,
        };
    }
    const auto found =
        std::find_if(entries_.begin(), entries_.end(),
                     [&](const Entry& entry) { return entry.key == *key; });
    if (found != entries_.end()) {
        ++statistics_.cache_hit_count;
        return {
            found->pose,
            std::nullopt,
            StudioPoseCacheLookupStatus::shared_existing,
        };
    }
    if (entries_.size() >= limits_.maximum_pose_cache_entries) {
        return {
            {},
            make_error(StudioPoseErrorCode::pose_cache_entry_limit_exceeded,
                       "Per-frame Studio pose cache entry limit exceeded"),
            StudioPoseCacheLookupStatus::inserted,
        };
    }

    auto evaluated =
        StudioPoseEvaluator{}.evaluate(model_identity, model, input, limits_);
    if (!evaluated || !evaluated.pose) {
        return {
            {},
            evaluated.error.value_or(
                make_error(StudioPoseErrorCode::unable_to_retain_pose,
                           "Studio pose evaluation failed")),
            StudioPoseCacheLookupStatus::inserted,
        };
    }
    const auto pose_bytes = evaluated.pose->statistics().accounted_pose_bytes;
    if (pose_bytes >
        limits_.maximum_pose_bytes - std::min(statistics_.accounted_pose_bytes,
                                              limits_.maximum_pose_bytes)) {
        return {
            {},
            make_error(StudioPoseErrorCode::pose_byte_limit_exceeded,
                       "Per-frame Studio pose cache byte limit exceeded"),
            StudioPoseCacheLookupStatus::inserted,
        };
    }
    try {
        auto shared =
            std::make_shared<const StudioPoseState>(std::move(*evaluated.pose));
        entries_.push_back(Entry{*key, shared});
        ++statistics_.cache_miss_count;
        statistics_.entry_count = entries_.size();
        statistics_.accounted_pose_bytes += pose_bytes;
        return {
            std::move(shared),
            std::nullopt,
            StudioPoseCacheLookupStatus::inserted,
        };
    } catch (const std::bad_alloc&) {
        return {
            {},
            make_error(StudioPoseErrorCode::unable_to_retain_pose,
                       "Unable to retain Studio pose cache entry"),
            StudioPoseCacheLookupStatus::inserted,
        };
    } catch (const std::length_error&) {
        return {
            {},
            make_error(StudioPoseErrorCode::unable_to_retain_pose,
                       "Studio pose cache length is invalid"),
            StudioPoseCacheLookupStatus::inserted,
        };
    }
}

const StudioPoseCacheStatistics& StudioPoseCache::statistics() const noexcept
{
    return statistics_;
}

} // namespace hlclient::goldsrc::studio
