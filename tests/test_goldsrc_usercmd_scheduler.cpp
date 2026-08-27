#include <hlclient/goldsrc/usercmd_scheduler.hpp>
#include <hlclient/input/input_state_tracker.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace camera = hlclient::gameplay_camera;
namespace gameplay_input = hlclient::gameplay_input;
namespace goldsrc = hlclient::goldsrc;
namespace input = hlclient::input;

[[nodiscard]] gameplay_input::GameplayInputIntent sample_intent(
    const bool pressed_button = false)
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::focus_gained());
    tracker.apply_event(input::InputEvent::capture_acquired());
    if (pressed_button) {
        tracker.apply_event(
            input::InputEvent::key_pressed(input::PhysicalKey::space));
    }
    const auto snapshot = tracker.publish_snapshot();
    auto bindings = gameplay_input::GameplayInputBindings::project_default_v1();
    REQUIRE(bindings);
    auto built = gameplay_input::GameplayInputIntentBuilder{}.build(
        snapshot,
        *bindings.bindings,
        gameplay_input::MouseLookConfig{},
        0.01);
    REQUIRE(built);
    return std::move(*built.intent);
}

[[nodiscard]] camera::GameplayCameraState sample_camera(
    const double yaw = 35.0,
    const double pitch = -12.0)
{
    camera::GameplayCameraStateCreateInfo info;
    info.yaw_degrees = yaw;
    info.pitch_degrees = pitch;
    auto created = camera::GameplayCameraState::create(info);
    REQUIRE(created);
    return std::move(*created.state);
}

void require_error(
    const goldsrc::GoldSrcUserCmdSchedulerUpdateResult& result,
    const goldsrc::GoldSrcUserCmdSchedulerErrorCode expected)
{
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
    CHECK(result.requests.empty());
}

void append_requests(
    std::vector<goldsrc::GoldSrcUserCmdSampleRequest>& destination,
    goldsrc::GoldSrcUserCmdSchedulerUpdateResult result)
{
    REQUIRE(result);
    destination.insert(
        destination.end(), result.requests.begin(), result.requests.end());
}

} // namespace

TEST_CASE("GoldSrc usercmd scheduler exposes fixed cadence independent of render polls",
          "[goldsrc][usercmd][scheduler][cadence][render-independent]")
{
    const auto intent = sample_intent(true);
    const auto camera_state = sample_camera();
    goldsrc::GoldSrcUserCmdScheduler dense;
    goldsrc::GoldSrcUserCmdScheduler sparse;

    REQUIRE(dense.update(0, intent, camera_state));
    REQUIRE(sparse.update(0, intent, camera_state));
    std::vector<goldsrc::GoldSrcUserCmdSampleRequest> dense_requests;
    for (const auto render_time : {
             2'000'000,
             5'000'000,
             10'000'000,
             11'000'000,
             20'000'000,
             25'000'000,
             30'000'000,
             31'000'000,
             40'000'000}) {
        append_requests(
            dense_requests,
            dense.update(render_time, intent, camera_state));
    }

    std::vector<goldsrc::GoldSrcUserCmdSampleRequest> sparse_requests;
    append_requests(
        sparse_requests, sparse.update(25'000'000, intent, camera_state));
    append_requests(
        sparse_requests, sparse.update(40'000'000, intent, camera_state));

    REQUIRE(dense_requests.size() == 4U);
    REQUIRE(sparse_requests.size() == dense_requests.size());
    for (std::size_t index = 0U; index < dense_requests.size(); ++index) {
        const auto& dense_request = dense_requests[index];
        const auto& sparse_request = sparse_requests[index];
        CHECK(dense_request.command_sequence == sparse_request.command_sequence);
        CHECK(dense_request.sample_time_nanoseconds ==
              sparse_request.sample_time_nanoseconds);
        CHECK(dense_request.sample_duration_nanoseconds == 10'000'000U);
        CHECK(dense_request.command_msec == 10U);
        CHECK(dense_request.sample_time_nanoseconds ==
              static_cast<std::int64_t>(index + 1U) * 10'000'000);
        CHECK(dense_request.source_input_sequence == intent.input_sequence());
        CHECK(dense_request.focused);
        CHECK(dense_request.camera_yaw_degrees == camera_state.yaw_degrees());
        CHECK(dense_request.camera_pitch_degrees == camera_state.pitch_degrees());
    }
    CHECK(dense_requests[0U].one_shot_eligible);
    CHECK(dense_requests[1U].one_shot_eligible);
    CHECK(sparse_requests[0U].one_shot_eligible);
    CHECK_FALSE(sparse_requests[1U].one_shot_eligible);
}

TEST_CASE("GoldSrc usercmd scheduler carries duration remainder across catch-up commands",
          "[goldsrc][usercmd][scheduler][duration][catchup]")
{
    const auto intent = sample_intent(true);
    const auto camera_state = sample_camera();
    goldsrc::GoldSrcUserCmdSchedulerConfig config;
    config.command_interval_nanoseconds = 16'666'667U;
    config.maximum_commands_per_update = 3U;
    goldsrc::GoldSrcUserCmdScheduler scheduler{config};

    const auto initialized = scheduler.update(0, intent, camera_state);
    REQUIRE(initialized);
    CHECK(initialized.requests.empty());
    CHECK(initialized.next_sample_time_nanoseconds == 16'666'667);

    const auto caught_up = scheduler.update(50'000'001, intent, camera_state);
    REQUIRE(caught_up);
    REQUIRE(caught_up.requests.size() == 3U);
    CHECK(caught_up.requests[0U].command_msec == 16U);
    CHECK(caught_up.requests[1U].command_msec == 17U);
    CHECK(caught_up.requests[2U].command_msec == 17U);
    CHECK(caught_up.requests[0U].command_sequence.value() == 1U);
    CHECK(caught_up.requests[1U].command_sequence.value() == 2U);
    CHECK(caught_up.requests[2U].command_sequence.value() == 3U);
    CHECK(caught_up.requests[0U].sample_time_nanoseconds == 16'666'667);
    CHECK(caught_up.requests[1U].sample_time_nanoseconds == 33'333'334);
    CHECK(caught_up.requests[2U].sample_time_nanoseconds == 50'000'001);
    CHECK(caught_up.requests[0U].one_shot_eligible);
    CHECK_FALSE(caught_up.requests[1U].one_shot_eligible);
    CHECK_FALSE(caught_up.requests[2U].one_shot_eligible);
    CHECK(caught_up.duration_remainder_nanoseconds == 1);
    CHECK(caught_up.next_sample_time_nanoseconds == 66'666'668);
}

TEST_CASE("GoldSrc scheduler lag failures leave cadence and sequences retryable",
          "[goldsrc][usercmd][scheduler][catchup][transactional]")
{
    const auto intent = sample_intent();
    const auto camera_state = sample_camera();
    goldsrc::GoldSrcUserCmdSchedulerConfig config;
    config.maximum_commands_per_update = 2U;
    goldsrc::GoldSrcUserCmdScheduler scheduler{config};
    REQUIRE(scheduler.update(0, intent, camera_state));

    const auto rejected = scheduler.update(30'000'000, intent, camera_state);
    require_error(
        rejected, goldsrc::GoldSrcUserCmdSchedulerErrorCode::lag_limit_exceeded);
    CHECK(rejected.next_sample_time_nanoseconds == 10'000'000);
    CHECK(rejected.duration_remainder_nanoseconds == 0);

    const auto retry = scheduler.update(20'000'000, intent, camera_state);
    REQUIRE(retry);
    REQUIRE(retry.requests.size() == 2U);
    CHECK(retry.requests[0U].command_sequence.value() == 1U);
    CHECK(retry.requests[1U].command_sequence.value() == 2U);
    CHECK(retry.requests[0U].sample_time_nanoseconds == 10'000'000);
    CHECK(retry.requests[1U].sample_time_nanoseconds == 20'000'000);

    const auto resumed = scheduler.update(30'000'000, intent, camera_state);
    REQUIRE(resumed);
    REQUIRE(resumed.requests.size() == 1U);
    CHECK(resumed.requests[0U].command_sequence.value() == 3U);
}

TEST_CASE("Copied GoldSrc schedulers form independent speculative timelines",
          "[goldsrc][usercmd][scheduler][copy][transactional]")
{
    static_assert(std::is_copy_constructible_v<goldsrc::GoldSrcUserCmdScheduler>);
    static_assert(std::is_copy_assignable_v<goldsrc::GoldSrcUserCmdScheduler>);

    const auto intent = sample_intent();
    const auto camera_state = sample_camera();
    goldsrc::GoldSrcUserCmdScheduler original;
    REQUIRE(original.update(0, intent, camera_state));
    auto speculative = original;

    const auto staged = speculative.update(20'000'000, intent, camera_state);
    REQUIRE(staged);
    REQUIRE(staged.requests.size() == 2U);
    CHECK(staged.requests[0U].command_sequence.value() == 1U);
    CHECK(staged.requests[1U].command_sequence.value() == 2U);

    const auto original_first = original.update(
        10'000'000, intent, camera_state);
    REQUIRE(original_first);
    REQUIRE(original_first.requests.size() == 1U);
    CHECK(original_first.requests[0U].command_sequence.value() == 1U);
    const auto original_second = original.update(
        20'000'000, intent, camera_state);
    REQUIRE(original_second);
    REQUIRE(original_second.requests.size() == 1U);
    CHECK(original_second.requests[0U].command_sequence.value() == 2U);
}

TEST_CASE("GoldSrc scheduler detects sequence exhaustion and reset restores identity one",
          "[goldsrc][usercmd][scheduler][sequence][overflow]")
{
    const auto intent = sample_intent();
    const auto camera_state = sample_camera();
    goldsrc::GoldSrcUserCmdSchedulerConfig config;
    config.maximum_commands_per_update = 2U;
    config.maximum_command_sequence = 2U;
    goldsrc::GoldSrcUserCmdScheduler scheduler{config};
    REQUIRE(scheduler.update(0, intent, camera_state));
    const auto complete_domain = scheduler.update(
        20'000'000, intent, camera_state);
    REQUIRE(complete_domain);
    REQUIRE(complete_domain.requests.size() == 2U);
    CHECK(complete_domain.requests.back().command_sequence.value() == 2U);

    const auto exhausted = scheduler.update(
        30'000'000, intent, camera_state);
    require_error(
        exhausted, goldsrc::GoldSrcUserCmdSchedulerErrorCode::sequence_exhausted);
    CHECK(exhausted.next_sample_time_nanoseconds == 30'000'000);
    require_error(
        scheduler.update(30'000'000, intent, camera_state),
        goldsrc::GoldSrcUserCmdSchedulerErrorCode::sequence_exhausted);

    scheduler.reset();
    REQUIRE(scheduler.update(100'000'000, intent, camera_state));
    const auto restarted = scheduler.update(
        110'000'000, intent, camera_state);
    REQUIRE(restarted);
    REQUIRE(restarted.requests.size() == 1U);
    CHECK(restarted.requests[0U].command_sequence.value() == 1U);
}

TEST_CASE("GoldSrc scheduler profile and time bounds fail without publication",
          "[goldsrc][usercmd][scheduler][limits][stock-pending]")
{
    const auto intent = sample_intent();
    const auto camera_state = sample_camera();

    auto invalid = goldsrc::GoldSrcUserCmdSchedulerConfig{};
    invalid.command_interval_nanoseconds = 0U;
    CHECK_FALSE(goldsrc::valid_goldsrc_usercmd_scheduler_config(invalid));
    goldsrc::GoldSrcUserCmdScheduler invalid_scheduler{invalid};
    require_error(
        invalid_scheduler.update(0, intent, camera_state),
        goldsrc::GoldSrcUserCmdSchedulerErrorCode::invalid_configuration);

    auto stock_config = goldsrc::GoldSrcUserCmdSchedulerConfig{};
    stock_config.profile =
        goldsrc::GoldSrcUserCmdSamplingProfile::
            stock_protocol_48_controlled_profile_v1;
    goldsrc::GoldSrcUserCmdScheduler stock{stock_config};
    require_error(
        stock.update(0, intent, camera_state),
        goldsrc::GoldSrcUserCmdSchedulerErrorCode::stock_evidence_pending);

    goldsrc::GoldSrcUserCmdScheduler backwards;
    REQUIRE(backwards.update(10'000'000, intent, camera_state));
    require_error(
        backwards.update(9'000'000, intent, camera_state),
        goldsrc::GoldSrcUserCmdSchedulerErrorCode::time_moved_backwards);
    const auto retry = backwards.update(20'000'000, intent, camera_state);
    REQUIRE(retry);
    REQUIRE(retry.requests.size() == 1U);
    CHECK(retry.requests[0U].command_sequence.value() == 1U);

    goldsrc::GoldSrcUserCmdScheduler overflow;
    require_error(
        overflow.update(
            std::numeric_limits<std::int64_t>::max() - 5,
            intent,
            camera_state),
        goldsrc::GoldSrcUserCmdSchedulerErrorCode::time_overflow);
    const auto initialized_after_failure = overflow.update(
        0, intent, camera_state);
    REQUIRE(initialized_after_failure);
    CHECK(initialized_after_failure.next_sample_time_nanoseconds == 10'000'000);

    goldsrc::GoldSrcUserCmdScheduler minimum_epoch;
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    REQUIRE(minimum_epoch.update(minimum, intent, camera_state));
    const auto minimum_first = minimum_epoch.update(
        minimum + 10'000'000, intent, camera_state);
    REQUIRE(minimum_first);
    REQUIRE(minimum_first.requests.size() == 1U);
    CHECK(minimum_first.requests[0U].sample_time_nanoseconds ==
        minimum + 10'000'000);
    require_error(
        minimum_epoch.update(
            std::numeric_limits<std::int64_t>::max(), intent, camera_state),
        goldsrc::GoldSrcUserCmdSchedulerErrorCode::lag_limit_exceeded);

    goldsrc::GoldSrcUserCmdScheduler deadline_overflow;
    REQUIRE(deadline_overflow.update(
        std::numeric_limits<std::int64_t>::max() - 10'000'000,
        intent,
        camera_state));
    require_error(
        deadline_overflow.update(
            std::numeric_limits<std::int64_t>::max(), intent, camera_state),
        goldsrc::GoldSrcUserCmdSchedulerErrorCode::time_overflow);
}
