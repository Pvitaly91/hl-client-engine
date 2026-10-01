#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace assets = hlclient::assets;
namespace studio = hlclient::goldsrc::studio;
using Catch::Approx;

constexpr float kPi = 3.14159265358979323846F;

[[nodiscard]] assets::ModelAnimationChannel
channel(const assets::ModelAnimationChannelSemantic semantic,
        const std::uint32_t frame_count, const float source_default = 0.0F,
        const float source_scale = 0.0F, std::vector<std::int16_t> values = {})
{
    assets::ModelAnimationChannel result;
    result.semantic = semantic;
    result.frame_coverage = frame_count;
    result.source_default = source_default;
    result.source_scale = source_scale;
    if (!values.empty()) {
        REQUIRE(values.size() <= (std::numeric_limits<std::uint8_t>::max)());
        result.runs.push_back(assets::ModelAnimationRun{
            0U,
            static_cast<std::uint8_t>(values.size()),
            static_cast<std::uint8_t>(values.size()),
            std::move(values),
        });
    }
    return result;
}

[[nodiscard]] assets::ModelBoneAnimationTrack
track(const std::uint32_t bone, const std::uint32_t frame_count,
      const float translation_x, const float translation_y,
      const float rotation_z,
      std::vector<std::int16_t> animated_translation_x = {})
{
    assets::ModelBoneAnimationTrack result;
    result.bone_index = bone;
    for (std::size_t index = 0U; index < result.channels.size(); ++index) {
        result.channels[index] =
            channel(static_cast<assets::ModelAnimationChannelSemantic>(index),
                    frame_count);
    }
    result.channels[0U] = channel(
        assets::ModelAnimationChannelSemantic::translation_x, frame_count,
        translation_x, animated_translation_x.empty() ? 0.0F : 1.0F,
        std::move(animated_translation_x));
    result.channels[1U].source_default = translation_y;
    result.channels[5U].source_default = rotation_z;
    return result;
}

[[nodiscard]] assets::SkeletalModelAssetData
pose_model(const std::uint32_t blend_count = 1U, const bool looping = false,
           const bool animated = false)
{
    assets::SkeletalModelAssetData model;
    model.bones.resize(3U);
    model.bones[0U].name = "root";
    model.bones[0U].parent_index = -1;
    model.bones[1U].name = "child";
    model.bones[1U].parent_index = 0;
    model.bones[2U].name = "second_root";
    model.bones[2U].parent_index = -1;

    model.submodels.resize(3U);
    model.bodyparts = {
        {"body", 1, {0U, 1U}},
        {"gear", 2, {1U, 2U}},
    };
    model.textures.resize(2U);
    model.skin_families = {{{0U}}, {{1U}}};
    model.sequence_groups = {{"default", {}, 0U, false}};

    assets::ModelSequence sequence;
    sequence.label = "pose";
    sequence.frames_per_second = 30.0F;
    sequence.source_flags = looping ? 1U : 0U;
    sequence.frame_count = 3U;
    sequence.blend_count = blend_count;
    sequence.motion_bone = 0;
    sequence.sequence_group_index = 0U;
    for (std::uint32_t blend = 0U; blend < blend_count; ++blend) {
        assets::ModelAnimationBlend animation;
        animation.source_blend_ordinal = blend;
        const auto blend_translation = static_cast<float>(blend * 10U);
        animation.bone_tracks.push_back(
            track(0U, sequence.frame_count, blend_translation, 0.0F, kPi * 0.5F,
                  animated && blend == 0U ? std::vector<std::int16_t>{0, 10, 20}
                                          : std::vector<std::int16_t>{}));
        animation.bone_tracks.push_back(
            track(1U, sequence.frame_count, 1.0F, 0.0F, 0.0F));
        animation.bone_tracks.push_back(
            track(2U, sequence.frame_count, 0.0F, 5.0F, 0.0F));
        sequence.animation_blends.push_back(std::move(animation));
    }
    model.sequences.push_back(std::move(sequence));
    return model;
}

[[nodiscard]] studio::StudioPoseModelIdentity model_identity()
{
    return {"models/synthetic_pose.mdl", {0x1234U, 0x5678U}};
}

[[nodiscard]] studio::StudioPoseState
evaluate(const assets::SkeletalModelAssetData& model,
         const studio::StudioPoseInput& input = {})
{
    auto result =
        studio::StudioPoseEvaluator{}.evaluate(model_identity(), model, input);
    INFO((result.error ? result.error->context : std::string{}));
    REQUIRE(result);
    return std::move(*result.pose);
}

TEST_CASE(
    "Studio fractional channels sample defaults values and run boundaries",
    "[goldsrc-studio][pose][fractional]")
{
    const auto default_channel = channel(
        assets::ModelAnimationChannelSemantic::translation_x, 2U, 3.0F, 2.0F);
    const auto default_sample =
        studio::StudioFractionalAnimationChannelSampler::
            sample_fractional_frame(default_channel, 0.5);
    REQUIRE(default_sample);
    CHECK(default_sample.sample->quantized_zero == 0);
    CHECK(default_sample.sample->interpolated_value == Approx(3.0F));

    const auto translation =
        channel(assets::ModelAnimationChannelSemantic::translation_x, 2U, 0.0F,
                1.0F, {0, 10});
    const auto translated = studio::StudioFractionalAnimationChannelSampler::
        sample_fractional_frame(translation, 0.25);
    REQUIRE(translated);
    CHECK(translated.sample->scaled_zero == Approx(0.0F));
    CHECK(translated.sample->scaled_one == Approx(10.0F));
    CHECK(translated.sample->interpolated_value == Approx(2.5F));

    const auto rotation =
        channel(assets::ModelAnimationChannelSemantic::rotation_z, 2U, 0.0F,
                kPi * 0.5F, {0, 1});
    const auto rotated = studio::StudioFractionalAnimationChannelSampler::
        sample_fractional_frame(rotation, 0.5);
    REQUIRE(rotated);
    CHECK(rotated.sample->interpolated_value == Approx(kPi * 0.25F));

    auto boundary =
        channel(assets::ModelAnimationChannelSemantic::translation_x, 2U);
    boundary.source_scale = 1.0F;
    boundary.runs = {
        {0U, 1U, 1U, {2}},
        {1U, 1U, 1U, {6}},
    };
    const auto crossed = studio::StudioFractionalAnimationChannelSampler::
        sample_fractional_frame(boundary, 0.5, 3U);
    REQUIRE(crossed);
    CHECK(crossed.sample->interpolated_value == Approx(4.0F));
    CHECK(boundary.runs.size() == 2U);
    CHECK(boundary.runs[1U].quantized_values == std::vector<std::int16_t>{6});

    const auto invalid = studio::StudioFractionalAnimationChannelSampler::
        sample_fractional_frame(boundary, 2.0);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code == studio::StudioPoseErrorCode::invalid_frame);
}

TEST_CASE("Studio pose wraps or clamps frames and evaluates finite hierarchy",
          "[goldsrc-studio][pose][frame][hierarchy]")
{
    const auto looping = pose_model(1U, true, true);
    studio::StudioPoseInput input;
    input.frame_coordinate = 2.5;
    const auto looped = evaluate(looping, input);
    CHECK(looped.frame_sample().normalized_frame_coordinate ==
          Approx(0.5).margin(0.001));
    CHECK(looped.frame_sample().looped);
    CHECK(looped.local_bones()[0U].translation.x == Approx(5.0F).margin(0.01F));
    CHECK(looped.world_bones()[1U].transform.values[3U] ==
          Approx(5.0F).margin(0.01F));
    CHECK(looped.world_bones()[1U].transform.values[7U] ==
          Approx(1.0F).margin(0.01F));
    CHECK(looped.world_bones()[2U].transform.values[7U] == Approx(5.0F));
    for (const auto& bone : looped.world_bones()) {
        CHECK(std::all_of(
            bone.transform.values.begin(), bone.transform.values.end(),
            [](const float value) { return std::isfinite(value); }));
    }

    const auto nonlooping = pose_model(1U, false, true);
    input.frame_coordinate = 99.0;
    const auto clamped = evaluate(nonlooping, input);
    CHECK(clamped.frame_sample().normalized_frame_coordinate == Approx(2.0));
    CHECK(clamped.frame_sample().clamped);
    CHECK(clamped.local_bones()[0U].translation.x == Approx(20.0F));

    auto suppressed_model = nonlooping;
    suppressed_model.sequences[0U].motion_type = 1;
    input.frame_coordinate = 1.0;
    const auto suppressed = evaluate(suppressed_model, input);
    CHECK(suppressed.local_bones()[0U].translation.x == Approx(0.0F));
    CHECK(suppressed.statistics().suppressed_motion_axis_count == 1U);
    CHECK(nonlooping.sequences[0U]
              .animation_blends[0U]
              .bone_tracks[0U]
              .channels[0U]
              .runs[0U]
              .quantized_values == std::vector<std::int16_t>{0, 10, 20});
}

TEST_CASE("Studio fractional bone rotations take the shortest endpoint quaternion path",
          "[goldsrc-studio][pose][fractional][rotation-wrap]")
{
    // Equivalent orientations on opposite sides of an Euler wrap must stay
    // near 180 degrees. Scalar interpolation would turn the bone through zero.
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        auto model = pose_model();
        auto& root = model.sequences[0U].animation_blends[0U].bone_tracks[0U];
        root.channels[5U].source_default = 0.0F;
        root.channels[axis + 3U] = channel(
            static_cast<assets::ModelAnimationChannelSemantic>(axis + 3U),
            3U, 0.0F, kPi / 180.0F, {179, -179, 179});
        for (const double coordinate : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            studio::StudioPoseInput input;
            input.frame_coordinate = coordinate;
            const auto posed = evaluate(model, input);
            const auto fraction = posed.frame_sample().fraction;
            const auto degrees = coordinate == 1.0 ? 181.0F :
                179.0F + 2.0F * fraction;
            const auto half_angle = degrees * kPi / 360.0F;
            const auto& q = posed.local_bones()[0U].rotation;
            const std::array components{q.x, q.y, q.z};
            // Quaternion signs can differ without changing orientation. The
            // signed product also rejects the opposite side of the wrap.
            CHECK(std::fabs(components[axis]) ==
                  Approx(std::fabs(std::sin(half_angle))).margin(1e-5F));
            CHECK(std::fabs(q.w) ==
                  Approx(std::fabs(std::cos(half_angle))).margin(1e-5F));
            CHECK(components[axis] * q.w ==
                  Approx(std::sin(half_angle) * std::cos(half_angle)).margin(1e-5F));
            for (std::size_t other = 0U; other < components.size(); ++other)
                if (other != axis) CHECK(components[other] == Approx(0.0F).margin(1e-6F));
            for (const auto value : posed.world_bones()[1U].transform.values)
                CHECK(std::isfinite(value));
        }
    }
}

TEST_CASE("Studio fractional rotations preserve controller adjustments and coupled axes",
          "[goldsrc-studio][pose][fractional][rotation-wrap][controller]")
{
    auto model = pose_model();
    auto& root = model.sequences[0U].animation_blends[0U].bone_tracks[0U];
    root.channels[5U] = channel(assets::ModelAnimationChannelSemantic::rotation_z,
                              3U, 0.0F, kPi / 180.0F, {179, -179, 179});
    // The controller's fixed ten-degree adjustment belongs to BOTH endpoints.
    model.bone_controllers = {{0, 0x0020U, 10.0F, 10.0F, 0, 0, false}};
    model.bones[0U].controller_indices[5U] = 0;
    studio::StudioPoseInput input;
    input.frame_coordinate = 0.5;
    const auto adjusted = evaluate(model, input);
    const auto degrees = 189.0F + 2.0F * adjusted.frame_sample().fraction;
    const auto half_angle = degrees * kPi / 360.0F;
    const auto& q = adjusted.local_bones()[0U].rotation;
    CHECK(std::fabs(q.z) == Approx(std::fabs(std::sin(half_angle))).margin(1e-5F));
    CHECK(std::fabs(q.w) == Approx(std::fabs(std::cos(half_angle))).margin(1e-5F));
    CHECK(q.z * q.w == Approx(std::sin(half_angle) * std::cos(half_angle)).margin(1e-5F));
    CHECK(adjusted.statistics().channel_sample_count == 18U);

    model.bone_controllers.clear();
    model.bones[0U].controller_indices[5U] = -1;
    root.channels[3U] = channel(assets::ModelAnimationChannelSemantic::rotation_x,
                              3U, 0.0F, kPi / 180.0F, {90, 0, 90});
    root.channels[4U] = channel(assets::ModelAnimationChannelSemantic::rotation_y,
                              3U, 0.0F, kPi / 180.0F, {0, 90, 0});
    root.channels[5U] = channel(assets::ModelAnimationChannelSemantic::rotation_z,
                              3U, 0.0F);
    const auto coupled = evaluate(model, input);
    const auto& midpoint = coupled.local_bones()[0U].rotation;
    // At the midpoint between 90-degree X and Y orientations, the normalized
    // sum of their endpoint quaternions is (1,1,0,2) / sqrt(6). Account for the
    // existing 4096-step fractional clock using the known 60-degree arc.
    const auto fraction = coupled.frame_sample().fraction;
    const auto left = std::sin((1.0F - fraction) * kPi / 3.0F) /
                      std::sin(kPi / 3.0F);
    const auto right = std::sin(fraction * kPi / 3.0F) /
                       std::sin(kPi / 3.0F);
    CHECK(midpoint.x == Approx(std::sqrt(0.5F) * left).margin(1e-5F));
    CHECK(midpoint.y == Approx(std::sqrt(0.5F) * right).margin(1e-5F));
    CHECK(midpoint.z == Approx(0.0F).margin(1e-6F));
    CHECK(midpoint.w == Approx(std::sqrt(0.5F) * (left + right)).margin(1e-5F));
}

TEST_CASE("Studio controllers cover linear rotation wrap and mouth inputs",
          "[goldsrc-studio][pose][controller]")
{
    auto model = pose_model();
    model.bone_controllers = {
        {0, 0x0001U, 0.0F, 10.0F, 0, 0, false},
        {0, 0x0020U, 0.0F, 180.0F, 0, 1, false},
        {0, 0x0008U | 0x8000U, -180.0F, 180.0F, 0, 2, true},
        {1, 0x0002U, 0.0F, 8.0F, 0, 4, false},
    };
    const auto evaluated = studio::StudioBoneControllerEvaluator{}.evaluate(
        model, {128U, 255U, 64U, 0U}, 32U);
    REQUIRE(evaluated);
    const auto& values = evaluated.adjustments->values_by_controller_record;
    REQUIRE(values.size() == 4U);
    CHECK(values[0U] == Approx(10.0F * 128.0F / 255.0F));
    CHECK(values[1U] == Approx(kPi));
    CHECK(values[2U] == Approx(-kPi * 0.5F));
    CHECK(values[3U] == Approx(4.0F));

    model.bone_controllers[0U].controller_index = 5;
    const auto invalid =
        studio::StudioBoneControllerEvaluator{}.evaluate(model, {}, 0U);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error->code ==
          studio::StudioPoseErrorCode::invalid_controller);

    model.bone_controllers[0U].controller_index = 0;
    model.bone_controllers[0U].source_type = 0x0040U;
    const auto unsupported =
        studio::StudioBoneControllerEvaluator{}.evaluate(model, {}, 0U);
    REQUIRE_FALSE(unsupported);
    CHECK(unsupported.error->code ==
          studio::StudioPoseErrorCode::unsupported_controller_type);
}

TEST_CASE("Studio pose supports one two and four deterministic blends",
          "[goldsrc-studio][pose][blend]")
{
    const auto one = evaluate(pose_model(1U));
    CHECK(one.local_bones()[0U].translation.x == Approx(0.0F));
    CHECK(one.statistics().blend_evaluation_count == 1U);

    auto two_model = pose_model(2U);
    two_model.sequences[0U]
        .animation_blends[1U]
        .bone_tracks[0U]
        .channels[5U]
        .source_default = -kPi * 0.5F;
    studio::StudioPoseInput two_input;
    two_input.blending_values[0U] = 0U;
    const auto two_zero = evaluate(two_model, two_input);
    CHECK(two_zero.local_bones()[0U].translation.x == Approx(0.0F));
    two_input.blending_values[0U] = 128U;
    const auto two = evaluate(two_model, two_input);
    CHECK(two.local_bones()[0U].translation.x ==
          Approx(10.0F * 128.0F / 255.0F));
    CHECK(std::fabs(two.local_bones()[0U].rotation.z) < 0.01F);
    CHECK(two.local_bones()[0U].rotation.w > 0.99F);
    two_input.blending_values[0U] = 255U;
    const auto two_one = evaluate(two_model, two_input);
    CHECK(two_one.local_bones()[0U].translation.x == Approx(10.0F));

    studio::StudioPoseInput four_input;
    four_input.blending_values = {128U, 128U};
    const auto four = evaluate(pose_model(4U), four_input);
    const auto fraction = 128.0F / 255.0F;
    const auto expected = (10.0F * fraction) * (1.0F - fraction) +
                          (20.0F + 10.0F * fraction) * fraction;
    CHECK(four.local_bones()[0U].translation.x == Approx(expected));
    CHECK(four.statistics().blend_evaluation_count == 4U);

    auto unsupported_model = pose_model(3U);
    const auto unsupported = studio::StudioPoseEvaluator{}.evaluate(
        model_identity(), unsupported_model, {});
    REQUIRE_FALSE(unsupported);
    CHECK(unsupported.error->code ==
          studio::StudioPoseErrorCode::unsupported_blend_count);
}

TEST_CASE("Studio body and skin selectors are exact and never fall back",
          "[goldsrc-studio][pose][body][skin]")
{
    const auto model = pose_model();
    const auto body = studio::StudioBodyPartSelector{}.select(model, 3);
    REQUIRE(body);
    REQUIRE(body.selection->bodyparts.size() == 2U);
    CHECK(body.selection->bodyparts[0U].submodel_index == 1U);
    CHECK(body.selection->bodyparts[1U].submodel_index == 2U);

    const auto invalid_body =
        studio::StudioBodyPartSelector{}.select(model, -1);
    REQUIRE_FALSE(invalid_body);
    CHECK(invalid_body.error->code ==
          studio::StudioPoseErrorCode::invalid_body_value);

    const auto skin = studio::StudioSkinSelector{}.select(model, 1U);
    REQUIRE(skin);
    assets::ModelMesh mesh;
    mesh.skin_reference_slot = 0U;
    CHECK(skin.selection->texture_for_mesh(mesh) == 1U);
    CHECK(skin.selection->family_index == 1U);

    const auto invalid_skin = studio::StudioSkinSelector{}.select(model, 2U);
    REQUIRE_FALSE(invalid_skin);
    CHECK(invalid_skin.error->code ==
          studio::StudioPoseErrorCode::invalid_skin_family);
}

TEST_CASE("E10 composes transition and numeric masked layers in local bone space",
          "[e10][goldsrc-studio][pose][composition]")
{
    auto model = pose_model();
    model.sequences[0].animation_blends[0].bone_tracks[0].channels[5].source_default = 0.0F;
    auto previous = model.sequences[0];
    previous.label = "independent-previous";
    previous.animation_blends[0].bone_tracks[0].channels[0].source_default = 10.0F;
    previous.animation_blends[0].bone_tracks[0].channels[5].source_default = kPi * 0.5F;
    auto layer = model.sequences[0];
    layer.label = "independent-layer";
    layer.animation_blends[0].bone_tracks[0].channels[0].source_default = 20.0F;
    layer.animation_blends[0].bone_tracks[1].channels[0].source_default = 5.0F;
    model.sequences.push_back(std::move(previous));
    model.sequences.push_back(std::move(layer));
    studio::StudioPoseCompositionInput input;
    input.previous.emplace(); input.previous->sequence_index = 1U;
    const auto compose = [&]() {
        auto result = studio::StudioPoseEvaluator{}.compose(model_identity(), model, input);
        INFO((result.error ? result.error->context : std::string{}));
        REQUIRE(result);
        return std::move(*result.pose);
    };
    input.previous_weight = 0.0F;
    const auto current = compose();
    CHECK(current.world_bones()[0].transform.values[3] == Approx(0.0F));
    CHECK(current.world_bones()[1].transform.values[3] == Approx(1.0F));
    input.previous_weight = 1.0F;
    const auto old = compose();
    CHECK(old.world_bones()[0].transform.values[3] == Approx(10.0F));
    CHECK(old.world_bones()[1].transform.values[7] == Approx(1.0F));
    input.previous_weight = 0.5F;
    const auto mid = compose();
    CHECK(mid.world_bones()[0].transform.values[3] == Approx(5.0F));
    CHECK(mid.world_bones()[1].transform.values[3] == Approx(5.0F + std::sqrt(0.5F)));
    CHECK(mid.world_bones()[1].transform.values[7] == Approx(std::sqrt(0.5F)));
    CHECK(mid.statistics().composition_sample_count == 2U);
    input.layer.emplace(); input.layer->sequence_index = 2U;
    input.lower_bone_indices = {0U};
    const auto masked = compose();
    CHECK(masked.world_bones()[0].transform.values[3] == Approx(20.0F));
    // Child is rebuilt under the masked parent, but retains MAIN local pose.
    CHECK(masked.local_bones()[1].translation.x == Approx(1.0F));
    CHECK(masked.world_bones()[1].transform.values[3] == Approx(21.0F));
    CHECK(masked.statistics().composition_sample_count == 3U);
    for (auto& bone : model.bones) bone.name = "no-game-specific-bone-name";
    const auto renamed = compose();
    CHECK(renamed.world_bones()[1].transform.values == masked.world_bones()[1].transform.values);
}

TEST_CASE("E10 composition rejects invalid masks weights samples and finite inputs",
          "[e10][goldsrc-studio][pose][composition][bounds]")
{
    auto model = pose_model(4U);
    studio::StudioPoseCompositionInput input;
    input.layer.emplace(); input.lower_bone_indices = {0U};
    const auto rejected = [&](const studio::StudioPoseErrorCode code,
                              const studio::StudioPoseEvaluationLimits limits = {}) {
        auto result = studio::StudioPoseEvaluator{}.compose(model_identity(), model, input, limits);
        REQUIRE_FALSE(result); REQUIRE(result.error); CHECK(result.error->code == code);
    };
    input.lower_bone_indices = {0U,0U}; rejected(studio::StudioPoseErrorCode::invalid_composition);
    input.lower_bone_indices = {3U}; rejected(studio::StudioPoseErrorCode::invalid_composition);
    input.lower_bone_indices.clear(); rejected(studio::StudioPoseErrorCode::invalid_composition);
    input.lower_bone_indices = {0U};
    input.previous_weight = 0.5F; rejected(studio::StudioPoseErrorCode::invalid_composition);
    input.previous.emplace();
    input.previous_weight = std::numeric_limits<float>::quiet_NaN(); rejected(studio::StudioPoseErrorCode::invalid_composition);
    input.previous_weight = -0.1F; rejected(studio::StudioPoseErrorCode::invalid_composition);
    input.previous_weight = 1.1F; rejected(studio::StudioPoseErrorCode::invalid_composition);
    input.previous_weight = 0.5F;
    auto limits = studio::StudioPoseEvaluationLimits{};
    limits.maximum_composition_samples = 2U; rejected(studio::StudioPoseErrorCode::composition_limit_exceeded, limits);
    limits.maximum_composition_samples = 3U; limits.maximum_composition_blend_evaluations = 11U;
    rejected(studio::StudioPoseErrorCode::composition_limit_exceeded, limits);
    input.layer->sequence_index = 10U; rejected(studio::StudioPoseErrorCode::invalid_sequence);
    input.layer->sequence_index = 0U;
    input.layer->frame_coordinate = std::numeric_limits<double>::quiet_NaN(); rejected(studio::StudioPoseErrorCode::invalid_frame);
    input.layer->frame_coordinate = 0.0;
    input.layer->entity_scale.x = 2.0F; rejected(studio::StudioPoseErrorCode::invalid_composition);
    input.layer->entity_scale.x = 1.0F;
    limits = {}; limits.maximum_pose_matrices = 11U;
    rejected(studio::StudioPoseErrorCode::composition_limit_exceeded, limits);
    limits = {}; limits.maximum_pose_bytes = 1U;
    rejected(studio::StudioPoseErrorCode::pose_byte_limit_exceeded, limits);
}

TEST_CASE("E10 posed bounds follow actual selected vertices and exact model identity",
          "[e10][goldsrc-studio][pose][posed-bounds]")
{
    auto model = pose_model();
    model.submodels[0].vertices = {
        {{2.0F,0.0F,0.0F},{0.0F,0.0F,1.0F},0,0,0U,0U},
        {{0.0F,0.0F,0.0F},{0.0F,0.0F,1.0F},0,0,1U,1U},
    };
    model.submodels[1].vertices = {
        {{0.0F,0.0F,0.0F},{0.0F,0.0F,1.0F},0,0,2U,2U},
    };
    const auto pose = evaluate(model);
    studio::StudioPoseEvaluator evaluator;
    const auto bounds = evaluator.posed_bounds(model_identity(), model, pose);
    REQUIRE(bounds);
    CHECK(bounds.bounds->minimum.x == Approx(0.0F).margin(1e-5F));
    CHECK(bounds.bounds->maximum.y == Approx(5.0F));
    CHECK(bounds.bounds->minimum.y == Approx(1.0F));
    auto wrong_identity = model_identity(); wrong_identity.revision = {9U,10U};
    CHECK_FALSE(evaluator.posed_bounds(wrong_identity, model, pose));
    CHECK_FALSE(evaluator.posed_bounds(model_identity(), model, pose, 2U));
    model.submodels[0].vertices[0].position_bone_index = 99U;
    CHECK_FALSE(evaluator.posed_bounds(model_identity(), model, pose));
}

TEST_CASE("Studio pose cache is bounded per explicit playback frame",
          "[goldsrc-studio][pose][cache]")
{
    const auto model = pose_model();
    auto limits = studio::StudioPoseEvaluationLimits{};
    limits.maximum_pose_cache_entries = 3U;
    studio::StudioPoseCache cache{limits};
    cache.reset_for_frame(100U);

    studio::StudioPoseInput input;
    const auto first = cache.find_or_evaluate(model_identity(), model, input);
    REQUIRE(first);
    const auto shared = cache.find_or_evaluate(model_identity(), model, input);
    REQUIRE(shared);
    CHECK(shared.status ==
          studio::StudioPoseCacheLookupStatus::shared_existing);
    CHECK(first.pose == shared.pose);

    input.frame_coordinate = 1.0;
    const auto different_frame =
        cache.find_or_evaluate(model_identity(), model, input);
    REQUIRE(different_frame);
    CHECK(different_frame.pose != first.pose);

    input.frame_coordinate = 0.0;
    input.controller_values[0U] = 1U;
    const auto different_controller =
        cache.find_or_evaluate(model_identity(), model, input);
    REQUIRE(different_controller);
    CHECK(different_controller.pose != first.pose);

    input.frame_coordinate = 2.0;
    const auto bounded = cache.find_or_evaluate(model_identity(), model, input);
    REQUIRE_FALSE(bounded);
    CHECK(bounded.error->code ==
          studio::StudioPoseErrorCode::pose_cache_entry_limit_exceeded);
    CHECK(cache.statistics().entry_count == 3U);

    cache.reset_for_frame(101U);
    const auto after_reset =
        cache.find_or_evaluate(model_identity(), model, input);
    REQUIRE(after_reset);
    CHECK(cache.statistics().entry_count == 1U);
    CHECK(cache.statistics().frame_token == 101U);
}

static_assert(!std::is_copy_assignable_v<studio::StudioPoseState>);
static_assert(std::is_same_v<decltype(studio::StudioPoseCacheResult{}.pose),
                             std::shared_ptr<const studio::StudioPoseState>>);

} // namespace
