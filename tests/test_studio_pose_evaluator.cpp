#include <hlclient/goldsrc/studio/goldsrc_studio_pose.hpp>
#include <hlclient/entity_render/studio_socket.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
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

TEST_CASE("Studio single-frame looping pose stays frame zero for every finite render time",
          "[goldsrc-studio][pose][e22-single-frame][core-only]") {
    for(const bool looping:{false,true}) {
        auto model=pose_model(1U,looping,false);auto& sequence=model.sequences[0];sequence.frame_count=1;
        for(auto& blend:sequence.animation_blends)for(auto& bone:blend.bone_tracks)for(auto& c:bone.channels)c.frame_coverage=1;
        for(const double frame:{0.,.001,.25,.999,1.,1.25,7.1,4000.125}) {
            CAPTURE(looping,frame);studio::StudioPoseInput input;input.frame_coordinate=frame;
            const auto posed=studio::StudioPoseEvaluator{}.evaluate(model_identity(),model,input);REQUIRE(posed);
            CHECK(posed.pose->frame_sample().frame_zero==0);CHECK(posed.pose->frame_sample().frame_one==0);CHECK(posed.pose->frame_sample().fraction==0);
            CHECK(posed.pose->frame_sample().normalized_frame_coordinate==0);
            for(const auto& bone:posed.pose->world_bones())CHECK(std::ranges::all_of(bone.transform.values,[](float v){return std::isfinite(v);}));
        }
        for(const auto frame:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
            studio::StudioPoseInput input;input.frame_coordinate=frame;CHECK_FALSE(studio::StudioPoseEvaluator{}.evaluate(model_identity(),model,input));
        }
    }
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

TEST_CASE("Studio evaluated pose owns the exact selected body and skin",
          "[goldsrc-studio][pose][body][skin][selection-ownership]")
{
    const auto model = pose_model();
    for (std::int32_t body_value = 0; body_value < 4; ++body_value) {
        for (std::uint32_t skin_family = 0U; skin_family < 2U; ++skin_family) {
            const auto body = studio::StudioBodyPartSelector{}.select(model, body_value);
            const auto skin = studio::StudioSkinSelector{}.select(model, skin_family);
            REQUIRE(body);
            REQUIRE(skin);
            studio::StudioPoseInput input;
            input.body_value = body_value;
            input.skin_family_index = skin_family;
            const auto evaluated = studio::StudioPoseEvaluator{}.evaluate(
                model_identity(), model, input);
            REQUIRE(evaluated);
            const auto& selected = evaluated.pose->body_selection().bodyparts;
            REQUIRE(selected.size() == body.selection->bodyparts.size());
            for (std::size_t index = 0U; index < selected.size(); ++index) {
                CHECK(selected[index].bodypart_index == body.selection->bodyparts[index].bodypart_index);
                CHECK(selected[index].local_model_index == body.selection->bodyparts[index].local_model_index);
                CHECK(selected[index].submodel_index == body.selection->bodyparts[index].submodel_index);
            }
            CHECK(evaluated.pose->skin_selection().family_index == skin.selection->family_index);
            CHECK(evaluated.pose->skin_selection().texture_indices_by_skin_reference ==
                  skin.selection->texture_indices_by_skin_reference);
        }
    }
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
    CHECK(masked.statistics().world_build_calls_v2 == 1U);
    CHECK(masked.statistics().world_matrices_built_v2 == model.bones.size());
    for (auto& bone : model.bones) bone.name = "no-game-specific-bone-name";
    const auto renamed = compose();
    CHECK(renamed.world_bones()[1].transform.values == masked.world_bones()[1].transform.values);
}

TEST_CASE("Studio composition retains evaluated main pose and conservative byte budget",
          "[goldsrc-studio][pose][composition][local-evaluation]")
{
    const auto model = pose_model(4U, false, true);
    studio::StudioPoseCompositionInput input;
    input.main.frame_coordinate = 1.25;
    input.main.blending_values = {128U, 192U};
    input.main.body_value = 3;
    input.main.skin_family_index = 1U;
    const auto single = studio::StudioPoseEvaluator{}.evaluate(
        model_identity(), model, input.main);
    const auto composed = studio::StudioPoseEvaluator{}.compose(
        model_identity(), model, input);
    REQUIRE(single);
    REQUIRE(composed);
    REQUIRE(single.pose->local_bones().size() == composed.pose->local_bones().size());
    REQUIRE(single.pose->world_bones().size() == composed.pose->world_bones().size());
    for (std::size_t bone = 0U; bone < single.pose->local_bones().size(); ++bone) {
        const auto& left = single.pose->local_bones()[bone];
        const auto& right = composed.pose->local_bones()[bone];
        CHECK(left.translation.x == right.translation.x);
        CHECK(left.translation.y == right.translation.y);
        CHECK(left.translation.z == right.translation.z);
        CHECK(left.rotation.x == right.rotation.x);
        CHECK(left.rotation.y == right.rotation.y);
        CHECK(left.rotation.z == right.rotation.z);
        CHECK(left.rotation.w == right.rotation.w);
        CHECK(single.pose->world_bones()[bone].transform.values ==
              composed.pose->world_bones()[bone].transform.values);
        CHECK(single.pose->world_bones()[bone].normal_rotation ==
              composed.pose->world_bones()[bone].normal_rotation);
    }
    CHECK(single.pose->body_selection().bodyparts.size() ==
          composed.pose->body_selection().bodyparts.size());
    CHECK(single.pose->skin_selection().texture_indices_by_skin_reference ==
          composed.pose->skin_selection().texture_indices_by_skin_reference);
    CHECK(single.pose->statistics().world_matrix_count == model.bones.size());
    CHECK(composed.pose->statistics().world_matrix_count == model.bones.size());
    CHECK(single.pose->statistics().world_build_calls_v2 == 1U);
    CHECK(composed.pose->statistics().world_build_calls_v2 == 1U);
    CHECK(single.pose->statistics().world_matrices_built_v2 == model.bones.size());
    CHECK(composed.pose->statistics().world_matrices_built_v2 == model.bones.size());
    REQUIRE(single.pose->statistics().retained_pose_vector_capacity_bytes_v2);
    REQUIRE(composed.pose->statistics().retained_pose_vector_capacity_bytes_v2);
    const auto output_bytes = model.bones.size() *
        (sizeof(studio::ModelBoneLocalPose) + sizeof(studio::ModelBoneWorldPose));
    const auto minimum_retained_bytes = output_bytes +
        composed.pose->body_selection().bodyparts.size() *
            sizeof(studio::StudioBodyPartSelection) +
        composed.pose->skin_selection().texture_indices_by_skin_reference.size() *
            sizeof(std::uint16_t);
    CHECK(*composed.pose->statistics().retained_pose_vector_capacity_bytes_v2 >=
          minimum_retained_bytes);
    CHECK(composed.pose->statistics().accounted_pose_bytes ==
          single.pose->statistics().accounted_pose_bytes + output_bytes);
    CHECK(composed.pose->statistics().composition_sample_count == 1U);

    input.previous = input.main;
    input.previous_weight = 0.0F;
    const auto two_samples = studio::StudioPoseEvaluator{}.compose(
        model_identity(), model, input);
    REQUIRE(two_samples);
    CHECK(two_samples.pose->statistics().accounted_pose_bytes ==
          2U * single.pose->statistics().accounted_pose_bytes + output_bytes);
    CHECK(two_samples.pose->statistics().composition_sample_count == 2U);
    CHECK(two_samples.pose->statistics().world_build_calls_v2 == 1U);
    CHECK(two_samples.pose->statistics().world_matrices_built_v2 == model.bones.size());

    auto limits = studio::StudioPoseEvaluationLimits{};
    limits.maximum_pose_bytes = two_samples.pose->statistics().accounted_pose_bytes - 1U;
    const auto byte_rejected = studio::StudioPoseEvaluator{}.compose(
        model_identity(), model, input, limits);
    REQUIRE_FALSE(byte_rejected);
    REQUIRE(byte_rejected.error);
    CHECK(byte_rejected.error->code == studio::StudioPoseErrorCode::pose_byte_limit_exceeded);
    ++limits.maximum_pose_bytes;
    REQUIRE(studio::StudioPoseEvaluator{}.compose(model_identity(), model, input, limits));
    limits = studio::StudioPoseEvaluationLimits{};
    limits.maximum_pose_matrices = model.bones.size() * 3U - 1U;
    const auto matrix_rejected = studio::StudioPoseEvaluator{}.compose(
        model_identity(), model, input, limits);
    REQUIRE_FALSE(matrix_rejected);
    REQUIRE(matrix_rejected.error);
    CHECK(matrix_rejected.error->code == studio::StudioPoseErrorCode::composition_limit_exceeded);
    ++limits.maximum_pose_matrices;
    REQUIRE(studio::StudioPoseEvaluator{}.compose(model_identity(), model, input, limits));
}

TEST_CASE("Studio composition validates hidden extreme samples and hierarchy",
          "[goldsrc-studio][pose][composition][local-evaluation][rejection]")
{
    auto model = pose_model();
    auto extreme = model.sequences[0U];
    extreme.animation_blends[0U].bone_tracks[0U].channels[0U].source_default = 3.0e38F;
    extreme.animation_blends[0U].bone_tracks[0U].channels[5U].source_default = 0.0F;
    extreme.animation_blends[0U].bone_tracks[1U].channels[0U].source_default = 3.0e38F;
    model.sequences.push_back(std::move(extreme));
    studio::StudioPoseCompositionInput input;
    input.previous.emplace();
    input.previous->sequence_index = 1U;
    input.previous_weight = 0.0F;
    const auto rejected_previous = studio::StudioPoseEvaluator{}.compose(
        model_identity(), model, input);
    REQUIRE_FALSE(rejected_previous);
    REQUIRE(rejected_previous.error);
    CHECK(rejected_previous.error->code == studio::StudioPoseErrorCode::non_finite_pose);
    CHECK(rejected_previous.error->sequence_index == 1U);

    input.previous.reset();
    input.layer.emplace();
    input.layer->sequence_index = 1U;
    input.lower_bone_indices = {2U}; // The overflowing child is hidden by the mask.
    const auto rejected_layer = studio::StudioPoseEvaluator{}.compose(
        model_identity(), model, input);
    REQUIRE_FALSE(rejected_layer);
    REQUIRE(rejected_layer.error);
    CHECK(rejected_layer.error->code == studio::StudioPoseErrorCode::non_finite_pose);
    CHECK(rejected_layer.error->sequence_index == 1U);

    // A finite, unusual sample still takes the full-world validation path.
    // Count that real fallback separately from the final composed hierarchy.
    model.sequences[1U].animation_blends[0U].bone_tracks[0U].channels[0U].source_default = 20'000.0F;
    model.sequences[1U].animation_blends[0U].bone_tracks[1U].channels[0U].source_default = 20'000.0F;
    const auto guarded_layer = studio::StudioPoseEvaluator{}.compose(
        model_identity(), model, input);
    REQUIRE(guarded_layer);
    CHECK(guarded_layer.pose->statistics().world_matrix_count == model.bones.size());
    CHECK(guarded_layer.pose->statistics().world_build_calls_v2 == 2U);
    CHECK(guarded_layer.pose->statistics().world_matrices_built_v2 ==
          2U * model.bones.size());
    REQUIRE(guarded_layer.pose->statistics().retained_pose_vector_capacity_bytes_v2);

    model.bones[1U].parent_index = 1;
    input.layer.reset();
    input.lower_bone_indices.clear();
    const auto rejected_hierarchy = studio::StudioPoseEvaluator{}.compose(
        model_identity(), model, input);
    REQUIRE_FALSE(rejected_hierarchy);
    REQUIRE(rejected_hierarchy.error);
    CHECK(rejected_hierarchy.error->code == studio::StudioPoseErrorCode::invalid_hierarchy);
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

TEST_CASE("E11 bone merge matches case-folded names and evaluates unmatched child hierarchy",
          "[e11][pose][bone-merge]") {
    const auto parent=pose_model(1U,true,true);
    auto child=pose_model();
    child.bones[0].name="ROOT";child.bones[1].name="weapon_tip";
    studio::StudioBoneMergeCache cache;
    const studio::StudioPoseModelIdentity child_id{"synthetic_child",{77U,88U}};
    const auto mapping=cache.prepare(model_identity(),parent,child_id,child);
    REQUIRE(mapping);CHECK(mapping.mapping->matched_bones==2U);
    CHECK(mapping.mapping->parent_bones==std::vector<std::int32_t>{0,-1,2});
    studio::StudioPoseInput input;input.frame_coordinate=1.5;
    const auto parent_pose=evaluate(parent,input);
    const auto merged=studio::StudioPoseEvaluator{}.merge(parent_pose,child,*mapping.mapping,{});
    REQUIRE(merged);
    CHECK(merged.pose->world_bones()[0].transform.values==parent_pose.world_bones()[0].transform.values);
    CHECK(merged.pose->world_bones()[2].transform.values==parent_pose.world_bones()[2].transform.values);
    // Root rotates +90 degrees: the unmatched local +X tip inherits +Y.
    const auto& root=parent_pose.world_bones()[0].transform.values;
    CHECK(merged.pose->world_bones()[1].transform.values[3]==Approx(root[3]+root[0]));
    CHECK(merged.pose->world_bones()[1].transform.values[7]==Approx(root[7]+root[4]));
    const auto again=cache.prepare(model_identity(),parent,child_id,child);
    REQUIRE(again);CHECK(again.mapping.get()==mapping.mapping.get());CHECK(cache.hits()==1U);
    const auto changed=cache.prepare(model_identity(),parent,{"synthetic_child",{77U,89U}},child);
    REQUIRE(changed);CHECK(changed.mapping.get()!=mapping.mapping.get());
    cache.reset();CHECK(cache.hits()==0U);
    const auto after=cache.prepare(model_identity(),parent,child_id,child);
    REQUIRE(after);CHECK(after.mapping.get()!=mapping.mapping.get());
}
TEST_CASE("E11 invalid ambiguous cyclic and unrelated bone mappings fail typed without parent mutation",
          "[e11][pose][bone-merge]") {
    const auto parent=pose_model();const auto pose=evaluate(parent);
    studio::StudioBoneMergeCache cache;
    for(const unsigned failure:{0U,1U,2U,3U}) {
        auto child=pose_model();
        if(failure==0U) child.bones[1].name="ROOT";
        if(failure==1U) child.bones[0].parent_index=1;
        if(failure==2U) for(auto& bone:child.bones) bone.name="absent_"+bone.name;
        if(failure==3U) child.bones[0].name.clear();
        const auto rejected=cache.prepare(model_identity(),parent,{"invalid",{failure+1U,1U}},child);
        CHECK_FALSE(rejected);REQUIRE(rejected.error);
    }
    CHECK(pose.world_bones()[1].transform.values[7]==Approx(1.0F));
}
TEST_CASE("E11 merge cache stays bounded for repeated sixteen-player model pairs",
          "[e11][pose][bone-merge][bounds]") {
    const auto parent=pose_model(),child=pose_model();studio::StudioBoneMergeCache cache;
    for(unsigned frame=0;frame<30U;++frame) for(unsigned player=0;player<16U;++player) {
        const auto mapping=cache.prepare(model_identity(),parent,{"child",{player+1U,1U}},child);
        REQUIRE(mapping);
    }
    CHECK(cache.hits()==16U*29U);
    for(unsigned revision=0;revision<128U;++revision)
        REQUIRE(cache.prepare(model_identity(),parent,{"child",{revision+100U,2U}},child));
    cache.reset();CHECK(cache.hits()==0U);
}
TEST_CASE("E12 generic Studio socket uses the same bone and entity matrices as rendered vertices",
          "[e12][pose][studio-socket][core-api]") {
    namespace render=hlclient::entity_render;
    auto model=pose_model();model.attachments.push_back({"owned_socket",0,1U,{2,0,0},{}});
    const auto pose=evaluate(model);
    for(const float scale:{1.0F,2.0F}) for(const float yaw:{0.0F,90.0F}) {
        render::EntityRenderTransform transform{{10,20,30},{0,0,yaw},scale};
        const auto socket=render::evaluate_studio_attachment(model_identity(),model,pose,0U,transform);
        REQUIRE(socket);CHECK(socket.socket->origin.z==Approx(30));
        CHECK(socket.socket->origin.x==Approx(yaw==0 ? 10.0F : 10.0F-3.0F*scale).margin(0.0001));
        CHECK(socket.socket->origin.y==Approx(yaw==0 ? 20.0F+3.0F*scale : 20.0F).margin(0.0001));
        const auto& b=pose.world_bones()[1].transform.values;
        const auto matrix=render::entity_render_model_matrix(transform);
        const assets::AssetVector3 vertex{b[0]*2+b[3],b[4]*2+b[7],b[8]*2+b[11]};
        CHECK(socket.socket->origin.x==Approx(matrix[0]*vertex.x+matrix[4]*vertex.y+matrix[8]*vertex.z+matrix[12]));
        CHECK(socket.socket->origin.y==Approx(matrix[1]*vertex.x+matrix[5]*vertex.y+matrix[9]*vertex.z+matrix[13]));
        CHECK(socket.socket->origin.z==Approx(matrix[2]*vertex.x+matrix[6]*vertex.y+matrix[10]*vertex.z+matrix[14]));
    }
    auto definition=assets::ModelSocketDefinition{999,{}};
    CHECK(render::evaluate_studio_socket(model_identity(),model,pose,definition,{}).error==render::StudioSocketError::bone);
    definition={1,{std::numeric_limits<float>::quiet_NaN(),0,0}};
    CHECK(render::evaluate_studio_socket(model_identity(),model,pose,definition,{}).error==render::StudioSocketError::non_finite);
    definition={1,{2,0,0}};
    auto wrong=model_identity();++wrong.revision.primary;
    CHECK(render::evaluate_studio_socket(wrong,model,pose,definition,{}).error==render::StudioSocketError::identity);
    CHECK(render::evaluate_studio_socket(model_identity(),model,pose,definition,{{},{},0}).error==render::StudioSocketError::invalid_transform);
    CHECK(render::evaluate_studio_attachment(model_identity(),model,pose,10,{}).error==render::StudioSocketError::attachment);
    definition.right=definition.forward;
    CHECK(render::evaluate_studio_socket(model_identity(),model,pose,definition,{}).error==render::StudioSocketError::invalid_basis);
    model.attachments[0].local_vectors[0]={1,0,0};
    CHECK(render::evaluate_studio_attachment(model_identity(),model,pose,0,{}).error==render::StudioSocketError::invalid_basis);
}
TEST_CASE("E12 weapon socket follows final merged main gait and transition pose without mutating it",
          "[e12][pose][bone-merge][studio-socket][core-api]") {
    const auto parent=pose_model(1,true,true);auto child=pose_model();child.bones[1].name="weapon_tip";
    const studio::StudioPoseModelIdentity child_id{"owned_weapon_socket",{177,188}};
    studio::StudioBoneMergeCache cache;const auto mapping=cache.prepare(model_identity(),parent,child_id,child);REQUIRE(mapping);
    for(unsigned mode=0;mode<5;++mode) {
        studio::StudioPoseCompositionInput input;input.main.frame_coordinate=mode*0.25;
        if(mode==3U) {input.previous=input.main;input.previous->frame_coordinate=0;input.previous_weight=0.5F;}
        if(mode==4U) {input.layer=input.main;input.layer->frame_coordinate=0.75;input.lower_bone_indices={0};}
        const auto composed=studio::StudioPoseEvaluator{}.compose(model_identity(),parent,input);REQUIRE(composed);
        const auto before=composed.pose->world_bones()[0].transform.values;
        const auto merged=studio::StudioPoseEvaluator{}.merge(*composed.pose,child,*mapping.mapping,{});REQUIRE(merged);
        const assets::ModelSocketDefinition definition{1,{2,0,0}};
        const auto socket=hlclient::entity_render::evaluate_studio_socket(child_id,child,*merged.pose,definition,{{4,5,6},{},1});
        REQUIRE(socket);
        CHECK(socket.socket->origin.x==Approx(4+before[3]+before[0]*3).margin(0.0001));
        CHECK(socket.socket->origin.y==Approx(5+before[7]+before[4]*3).margin(0.0001));
        CHECK(socket.socket->origin.z==Approx(6+before[11]+before[8]*3).margin(0.0001));
        CHECK(composed.pose->world_bones()[0].transform.values==before);
    }
}

TEST_CASE("Opt-in deterministic Studio pose evaluation and composition benchmark",
          "[studio-pose-bench][.]")
{
    // Kept out of normal acceptance: timings are evidence, not pass/fail gates.
    // The same test source runs against the captured before and after binaries.
    bool enabled = false;
#ifdef _WIN32
    char* setting = nullptr;
    std::size_t setting_length = 0U;
    if (_dupenv_s(&setting, &setting_length, "HLCLIENT_STUDIO_POSE_BENCH") == 0) {
        enabled = setting != nullptr && std::string_view{setting} == "1";
    }
    std::free(setting);
#else
    const auto* setting = std::getenv("HLCLIENT_STUDIO_POSE_BENCH");
    enabled = setting != nullptr && std::string_view{setting} == "1";
#endif
    if (!enabled) {
        SKIP("Set HLCLIENT_STUDIO_POSE_BENCH=1 to run this offline benchmark");
    }

    auto model = pose_model(4U, true, true);
    constexpr std::uint32_t bone_count = 64U;
    for (std::uint32_t bone = 3U; bone < bone_count; ++bone) {
        auto record = model.bones[1U];
        record.name = "benchmark_bone_" + std::to_string(bone);
        record.parent_index = static_cast<std::int32_t>(bone - 1U);
        model.bones.push_back(std::move(record));
        for (auto& blend : model.sequences[0U].animation_blends) {
            auto animation = blend.bone_tracks[1U];
            animation.bone_index = bone;
            animation.channels[0U].source_default =
                0.125F + static_cast<float>(bone % 7U) * 0.01F;
            blend.bone_tracks.push_back(std::move(animation));
        }
    }
    auto previous_sequence = model.sequences[0U];
    previous_sequence.label = "benchmark_previous";
    for (auto& blend : previous_sequence.animation_blends) {
        blend.bone_tracks[0U].channels[0U].source_default += 1.75F;
    }
    model.sequences.push_back(std::move(previous_sequence));
    auto layer_sequence = model.sequences[0U];
    layer_sequence.label = "benchmark_layer";
    for (auto& blend : layer_sequence.animation_blends) {
        blend.bone_tracks[2U].channels[1U].source_default += 0.5F;
    }
    model.sequences.push_back(std::move(layer_sequence));

    studio::StudioPoseInput main_input;
    main_input.frame_coordinate = 1.25;
    main_input.body_value = 3;
    main_input.skin_family_index = 1U;
    main_input.blending_values = {97U, 161U};
    studio::StudioPoseCompositionInput main_only;
    main_only.main = main_input;
    auto combined = main_only;
    combined.previous = main_input;
    combined.previous->sequence_index = 1U;
    combined.previous->frame_coordinate = 0.25;
    combined.previous->blending_values = {5U, 220U};
    combined.previous_weight = 0.35F;
    combined.layer = main_input;
    combined.layer->sequence_index = 2U;
    combined.layer->frame_coordinate = 1.75;
    combined.layer->blending_values = {192U, 73U};
    for (std::uint32_t bone = 2U; bone < 32U; ++bone) {
        combined.lower_bone_indices.push_back(bone);
    }

    // FNV-1a over explicit numeric fields: independent of source/build paths,
    // pointer values, vector capacity, and diagnostic accounting changes.
    const auto fingerprint = [](const studio::StudioPoseState& pose) {
        std::uint64_t hash = 14695981039346656037ULL;
        const auto number = [&](std::uint64_t value, unsigned byte_count) {
            for (unsigned index = 0U; index < byte_count; ++index) {
                hash ^= (value >> (index * 8U)) & 0xffU;
                hash *= 1099511628211ULL;
            }
        };
        const auto scalar = [&](float value) {
            number(std::bit_cast<std::uint32_t>(value), 4U);
        };
        number(pose.sequence_index(), 4U);
        const auto& frame = pose.frame_sample();
        number(std::bit_cast<std::uint64_t>(frame.source_frame_coordinate), 8U);
        number(std::bit_cast<std::uint64_t>(frame.normalized_frame_coordinate), 8U);
        number(frame.frame_zero, 4U);
        number(frame.frame_one, 4U);
        number(frame.quantized_fraction, 2U);
        scalar(frame.fraction);
        number(frame.looped ? 1U : 0U, 1U);
        number(frame.clamped ? 1U : 0U, 1U);
        for (const auto& bone : pose.local_bones()) {
            scalar(bone.translation.x);
            scalar(bone.translation.y);
            scalar(bone.translation.z);
            scalar(bone.rotation.x);
            scalar(bone.rotation.y);
            scalar(bone.rotation.z);
            scalar(bone.rotation.w);
        }
        for (const auto& bone : pose.world_bones()) {
            for (const auto value : bone.transform.values) scalar(value);
            for (const auto value : bone.normal_rotation) scalar(value);
        }
        number(pose.body_selection().bodyparts.size(), 4U);
        for (const auto& part : pose.body_selection().bodyparts) {
            number(part.bodypart_index, 4U);
            number(part.local_model_index, 4U);
            number(part.submodel_index, 4U);
        }
        number(pose.skin_selection().family_index, 4U);
        number(pose.skin_selection().texture_indices_by_skin_reference.size(), 4U);
        for (const auto index : pose.skin_selection().texture_indices_by_skin_reference) {
            number(index, 2U);
        }
        return hash;
    };

    studio::StudioPoseEvaluator evaluator;
    const auto identity = model_identity();
    constexpr std::size_t warmup = 64U;
    constexpr std::size_t samples = 256U;
    const auto measure = [&](std::string_view name, auto&& evaluate_pose) {
        for (std::size_t index = 0U; index < warmup; ++index) {
            const auto result = evaluate_pose();
            INFO((result.error ? result.error->context : std::string{}));
            REQUIRE(result);
        }
        std::vector<double> microseconds;
        microseconds.reserve(samples);
        std::optional<std::uint64_t> expected_fingerprint;
        for (std::size_t index = 0U; index < samples; ++index) {
            const auto start = std::chrono::steady_clock::now();
            const auto result = evaluate_pose();
            const auto stop = std::chrono::steady_clock::now();
            INFO((result.error ? result.error->context : std::string{}));
            REQUIRE(result);
            const auto observed = fingerprint(*result.pose);
            if (!expected_fingerprint) expected_fingerprint = observed;
            REQUIRE(observed == *expected_fingerprint);
            microseconds.push_back(
                std::chrono::duration<double, std::micro>(stop - start).count());
        }
        std::sort(microseconds.begin(), microseconds.end());
        const auto quantile = [&](std::size_t numerator, std::size_t denominator) {
            return microseconds[(samples * numerator + denominator - 1U) /
                                denominator - 1U];
        };
        std::cout << std::setprecision(9)
                  << "[studio-pose-bench] {\"fixture\":\"synthetic_64bone_4blend_v1\","
                  << "\"case\":\"" << name << "\",\"warmup\":" << warmup
                  << ",\"n\":" << samples << ",\"unit\":\"us\","
                  << "\"p50\":" << quantile(1U, 2U)
                  << ",\"p95\":" << quantile(95U, 100U)
                  << ",\"p99\":" << quantile(99U, 100U)
                  << ",\"max\":" << microseconds.back()
                  << ",\"allocations_per_call\":null,\"pose_fingerprint\":\""
                  << std::hex << std::setw(16) << std::setfill('0')
                  << *expected_fingerprint << std::dec << std::setfill(' ')
                  << "\"}\n";
        return *expected_fingerprint;
    };
    const auto evaluated = measure("evaluate", [&] {
        return evaluator.evaluate(identity, model, main_input);
    });
    const auto composed_main = measure("compose_main", [&] {
        return evaluator.compose(identity, model, main_only);
    });
    CHECK(evaluated == composed_main);
    (void)measure("compose_combined", [&] {
        return evaluator.compose(identity, model, combined);
    });
}

static_assert(!std::is_copy_assignable_v<studio::StudioPoseState>);
static_assert(std::is_same_v<decltype(studio::StudioPoseCacheResult{}.pose),
                             std::shared_ptr<const studio::StudioPoseState>>);

} // namespace
