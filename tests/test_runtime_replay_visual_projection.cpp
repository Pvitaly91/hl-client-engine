#include <hlclient/app/runtime_replay_scene_source.hpp>
#include <hlclient/app/runtime_replay_visual_projection.hpp>

#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/client/runtime_observation.hpp>
#include <hlclient/entity_render/entity_render_types.hpp>
#include <hlclient/entity_render/entity_scene_render.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace {

namespace app = hlclient::app;
namespace client = hlclient::client;
namespace entity_render = hlclient::entity_render;
namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::RuntimeReplayFixture visual_fixture(
    const std::uint64_t generation = 1U,
    const std::uint64_t identity_base = 20'000U)
{
    auto built = goldsrc::make_runtime_replay_fixture({
        goldsrc::RuntimeReplayFixtureKind::visual_entities,
        generation,
        100U,
        identity_base,
    });
    REQUIRE(built);
    REQUIRE(built.fixture);
    return std::move(*built.fixture);
}

[[nodiscard]] std::unique_ptr<app::RuntimeReplaySceneSource> visual_source(
    goldsrc::RuntimeReplayFixture fixture,
    const app::RuntimeReplaySchedulingLimits limits = {1U, 1'024U})
{
    auto created = app::RuntimeReplaySceneSource::create(
        std::move(fixture), limits, true);
    INFO((created.error ? created.error->context : std::string{}));
    REQUIRE(created);
    REQUIRE(created.source);
    return std::move(created.source);
}

[[nodiscard]] const entity_render::StudioEntityRenderInstance* find_instance(
    const entity_render::EntityRenderFrame& frame,
    const std::uint32_t entity_number)
{
    const auto instances = frame.studio_instances();
    const auto found = std::find_if(
        instances.begin(), instances.end(),
        [entity_number](const auto& instance) {
            return instance.entity_number == entity_number;
        });
    return found == instances.end() ? nullptr : &*found;
}

[[nodiscard]] const client::RuntimePacketEntityObservation* find_entity(
    const client::RuntimeClientObservationState& observation,
    const std::uint32_t entity_number)
{
    const auto found = std::find_if(
        observation.packet_entities.begin(), observation.packet_entities.end(),
        [entity_number](const auto& entity) {
            return entity.entity_number == entity_number;
        });
    return found == observation.packet_entities.end() ? nullptr : &*found;
}

void require_transform_matches_observation(
    const entity_render::StudioEntityRenderInstance& instance,
    const client::RuntimePacketEntityObservation& observation)
{
    REQUIRE(observation.origin.complete());
    REQUIRE(observation.angles.complete());
    CHECK(instance.transform.origin.x ==
          Catch::Approx(static_cast<float>(*observation.origin.x)));
    CHECK(instance.transform.origin.y ==
          Catch::Approx(static_cast<float>(*observation.origin.y)));
    CHECK(instance.transform.origin.z ==
          Catch::Approx(static_cast<float>(*observation.origin.z)));
    CHECK(instance.transform.rotation_degrees.x ==
          Catch::Approx(static_cast<float>(*observation.angles.x)));
    CHECK(instance.transform.rotation_degrees.y ==
          Catch::Approx(static_cast<float>(*observation.angles.y)));
    CHECK(instance.transform.rotation_degrees.z ==
          Catch::Approx(static_cast<float>(*observation.angles.z)));
}

} // namespace

TEST_CASE("Literal replay entities project through the application-owned visual path",
    "[runtime-replay-visual][application][projection][null-renderer]")
{
    auto playback = visual_source(visual_fixture());
    REQUIRE(playback->start());
    REQUIRE(playback->diagnostic_visuals_enabled());
    REQUIRE(playback->world_state().entity_scene());
    REQUIRE(playback->world_state().entity_frame());
    const auto package = playback->world_state().entity_scene();
    CHECK(package->asset_source() ==
          entity_render::EntitySceneRenderAssetSource::
              project_generated_diagnostic);
    CHECK_FALSE(package->asset_library());
    REQUIRE(package->studio_assets().size() == 1U);
    CHECK(playback->world_state().entity_frame()->studio_instances().empty());
    CHECK(playback->world_state().entity_frame_revision() == 1U);

    REQUIRE(playback->update(std::chrono::duration<double>{0.0}));
    const auto initial_frame = playback->world_state().entity_frame();
    const auto initial_observation = playback->world_state().runtime_observation();
    REQUIRE(initial_frame);
    REQUIRE(initial_observation);
    REQUIRE(initial_frame->studio_instances().size() == 3U);
    for (const auto number : {2U, 10U, 20U}) {
        const auto* instance = find_instance(*initial_frame, number);
        const auto* entity = find_entity(*initial_observation, number);
        REQUIRE(instance);
        REQUIRE(entity);
        require_transform_matches_observation(*instance, *entity);
    }
    REQUIRE(find_instance(*initial_frame, 2U));
    CHECK(find_instance(*initial_frame, 2U)->transform.origin.x ==
          Catch::Approx(-32.0F));

    REQUIRE(playback->update(std::chrono::duration<double>{1.5}));
    const auto moved_frame = playback->world_state().entity_frame();
    REQUIRE(moved_frame);
    REQUIRE(moved_frame != initial_frame);
    REQUIRE(find_instance(*moved_frame, 2U));
    CHECK(find_instance(*moved_frame, 2U)->transform.origin.x ==
          Catch::Approx(-8.0F));
    REQUIRE(find_instance(*moved_frame, 10U));
    REQUIRE(find_instance(*initial_frame, 10U));
    CHECK(find_instance(*moved_frame, 10U)->transform.origin.x ==
          find_instance(*initial_frame, 10U)->transform.origin.x);

    REQUIRE(playback->update(std::chrono::duration<double>{1.5}));
    const auto rotated_frame = playback->world_state().entity_frame();
    REQUIRE(rotated_frame);
    const auto* rotated = find_instance(*rotated_frame, 10U);
    REQUIRE(rotated);
    CHECK(rotated->transform.rotation_degrees.z == Catch::Approx(90.0F));
    const auto model_matrix =
        entity_render::entity_render_model_matrix(rotated->transform);
    CHECK(model_matrix[0U] == Catch::Approx(0.0F).margin(0.0001F));
    CHECK(model_matrix[1U] == Catch::Approx(1.5F).margin(0.0001F));
    CHECK(model_matrix[4U] == Catch::Approx(-1.5F).margin(0.0001F));
    CHECK(model_matrix[5U] == Catch::Approx(0.0F).margin(0.0001F));
    CHECK(model_matrix[12U] == Catch::Approx(rotated->transform.origin.x));
    CHECK(model_matrix[13U] == Catch::Approx(rotated->transform.origin.y));
    CHECK(model_matrix[14U] == Catch::Approx(rotated->transform.origin.z));

    REQUIRE(playback->update(std::chrono::duration<double>{1.5}));
    const auto membership_frame = playback->world_state().entity_frame();
    REQUIRE(membership_frame);
    CHECK(find_instance(*membership_frame, 2U));
    CHECK(find_instance(*membership_frame, 10U));
    CHECK_FALSE(find_instance(*membership_frame, 20U));
    REQUIRE(find_instance(*membership_frame, 30U));
    CHECK(find_instance(*membership_frame, 30U)->transform.origin.y ==
          Catch::Approx(24.0F));
    CHECK(playback->world_state().entity_scene() == package);

    const auto frame_before_clientdata = playback->world_state().entity_frame();
    const auto signature_before_clientdata =
        frame_before_clientdata->frame_signature();
    REQUIRE(playback->update(std::chrono::duration<double>{1.5}));
    CHECK(playback->state() == app::RuntimeReplaySourceState::completed);
    CHECK(playback->world_state().entity_frame() == frame_before_clientdata);
    CHECK(playback->world_state().entity_frame()->frame_signature() ==
          signature_before_clientdata);
    REQUIRE(playback->world_state().runtime_observation()->receiving_client);
    REQUIRE(playback->world_state().runtime_observation()
                ->receiving_client->health);
    CHECK(*playback->world_state().runtime_observation()
               ->receiving_client->health == Catch::Approx(80.0));

    const auto summary = playback->summary();
    REQUIRE(summary.visual);
    CHECK(summary.applied_records == 5U);
    CHECK(summary.failed_records == 0U);
    CHECK(summary.visual_failed_records == 0U);
    CHECK(summary.pending_records == 0U);
    CHECK(summary.session_finish_count == 1U);
    CHECK(summary.visual->presented_runtime_revision ==
          playback->world_state().runtime_publication_revision());
    CHECK(summary.visual->visual_frame_revision == 5U);
    CHECK(summary.visual->projected_frame_count == 5U);
    CHECK(summary.visual->unchanged_record_count == 1U);
    CHECK(summary.visual->resource_build_count == 1U);

    const auto render_scene =
        client::build_render_scene(playback->world_state());
    REQUIRE(render_scene.dynamic_entities);
    CHECK(render_scene.dynamic_entities->package == package);
    CHECK(render_scene.dynamic_entities->frame == frame_before_clientdata);
    hlclient::renderer::null::NullRenderer renderer;
    renderer.initialize();
    renderer.render(render_scene, {640, 480});
    const auto renderer_statistics = renderer.statistics();
    CHECK(renderer_statistics.dynamic_entity_package_present);
    CHECK(renderer_statistics.studio_entity_instance_count == 3U);
    CHECK(renderer_statistics.visible_entity_count == 3U);
    CHECK(renderer_statistics.entity_scene_resource_id ==
          std::optional{package->resource_id()});
    CHECK(renderer_statistics.entity_frame_revision == std::optional{5U});
    renderer.shutdown();
    CHECK(renderer.statistics().shutdown);
}

TEST_CASE("Visual scheduling changes presentation only and restart clears instances",
    "[runtime-replay-visual][determinism][backlog][restart]")
{
    auto stepped = visual_source(visual_fixture(1U, 30'000U), {1U, 1'024U});
    auto batched = visual_source(visual_fixture(1U, 30'000U), {8U, 4'096U});
    REQUIRE(stepped->start());
    REQUIRE(batched->start());
    for (const auto seconds : {0.0, 1.5, 1.5, 1.5, 1.5}) {
        REQUIRE(stepped->update(std::chrono::duration<double>{seconds}));
    }
    REQUIRE(batched->update(std::chrono::duration<double>{6.0}));
    REQUIRE(stepped->terminal());
    REQUIRE(batched->terminal());
    REQUIRE(stepped->world_state().runtime_observation());
    REQUIRE(batched->world_state().runtime_observation());
    CHECK(*stepped->world_state().runtime_observation() ==
          *batched->world_state().runtime_observation());
    CHECK(stepped->world_state().runtime_observation()->canonical_state_hash ==
          batched->world_state().runtime_observation()->canonical_state_hash);
    REQUIRE(stepped->world_state().entity_frame());
    REQUIRE(batched->world_state().entity_frame());
    CHECK(stepped->world_state().entity_frame()->frame_signature() ==
          batched->world_state().entity_frame()->frame_signature());
    CHECK(stepped->world_state().entity_scene()->resource_revision() ==
          batched->world_state().entity_scene()->resource_revision());
    CHECK(batched->summary().application_updates == 1U);

    const auto resource_package = stepped->world_state().entity_scene();
    REQUIRE(stepped->restart(visual_fixture(2U, 40'000U)));
    CHECK(stepped->world_state().entity_scene() == resource_package);
    REQUIRE(stepped->world_state().entity_frame());
    CHECK(stepped->world_state().entity_frame()->studio_instances().empty());
    CHECK(stepped->world_state().entity_frame_revision() == 1U);
    REQUIRE(stepped->update(std::chrono::duration<double>{0.0}));
    CHECK(stepped->world_state().entity_frame()->studio_instances().size() == 3U);
    CHECK(stepped->summary().visual->generation == 2U);
}

TEST_CASE("Decoder and visual failures preserve the last complete visual frame",
    "[runtime-replay-visual][failure][transaction]")
{
    SECTION("decoder failure never reaches visual publication") {
        auto replay = visual_fixture();
        auto bad = goldsrc::make_runtime_replay_fixture({
            goldsrc::RuntimeReplayFixtureKind::missing_entity_base,
            1U,
            100U,
            50'000U,
        });
        REQUIRE(bad);
        REQUIRE(bad.fixture);
        REQUIRE(bad.fixture->records.size() >= 2U);
        replay.records[1U].payload = bad.fixture->records[1U].payload;
        auto playback = visual_source(std::move(replay));
        REQUIRE(playback->start());
        REQUIRE(playback->update(std::chrono::duration<double>{0.0}));
        const auto committed_observation =
            playback->world_state().runtime_observation();
        const auto committed_frame = playback->world_state().entity_frame();
        REQUIRE(committed_observation);
        REQUIRE(committed_frame);
        CHECK_FALSE(playback->update(std::chrono::duration<double>{1.5}));
        CHECK(playback->state() == app::RuntimeReplaySourceState::failed);
        CHECK(playback->world_state().runtime_observation() ==
              committed_observation);
        CHECK(playback->world_state().entity_frame() == committed_frame);
        CHECK(playback->summary().applied_records == 1U);
        CHECK(playback->summary().failed_records == 1U);
        CHECK(playback->summary().visual_failed_records == 0U);
        CHECK(playback->summary().session_finish_count == 1U);
    }

    SECTION("source stops after a committed record fails visual projection") {
        auto created = app::RuntimeReplaySceneSource::create(
            visual_fixture(1U, 60'000U),
            {1U, 1'024U},
            true,
            {2U, 65'536.0});
        REQUIRE(created);
        REQUIRE(created.source);
        auto playback = std::move(created.source);
        REQUIRE(playback->start());
        const auto last_valid_frame = playback->world_state().entity_frame();
        REQUIRE(last_valid_frame);
        CHECK(last_valid_frame->studio_instances().empty());

        CHECK_FALSE(playback->update(std::chrono::duration<double>{0.0}));
        CHECK(playback->state() == app::RuntimeReplaySourceState::failed);
        const auto summary = playback->summary();
        REQUIRE(summary.visual);
        REQUIRE(summary.terminal_error);
        CHECK(summary.terminal_error->code ==
              app::RuntimeReplaySourceErrorCode::visual_projection_failed);
        REQUIRE(summary.terminal_error->visual_error);
        CHECK(summary.terminal_error->visual_error->code ==
              app::RuntimeReplayVisualProjectionErrorCode::
                  projection_limit_exceeded);
        CHECK(summary.applied_records == 1U);
        CHECK(summary.failed_records == 0U);
        CHECK(summary.visual_failed_records == 1U);
        CHECK(summary.session_finish_count == 1U);
        CHECK(playback->world_state().runtime_publication_revision() >
              summary.visual->presented_runtime_revision);
        CHECK(playback->world_state().entity_frame() == last_valid_frame);
        CHECK(playback->world_state().entity_frame_revision() == 1U);
    }

    SECTION("projection rejects incomplete and non-finite transforms atomically") {
        auto decoded = visual_source(visual_fixture());
        REQUIRE(decoded->start());
        const auto initial_observation = decoded->world_state().runtime_observation();
        REQUIRE(initial_observation);
        REQUIRE(decoded->update(std::chrono::duration<double>{0.0}));
        const auto valid_observation = decoded->world_state().runtime_observation();
        REQUIRE(valid_observation);

        auto created = app::RuntimeReplayVisualProjection::create();
        REQUIRE(created);
        REQUIRE(created.projection);
        client::ClientWorldState target;
        REQUIRE(created.projection->reset_generation(*initial_observation, target));
        REQUIRE(created.projection->project_entities(*valid_observation, target));
        const auto last_valid_frame = target.entity_frame();
        const auto last_valid_revision = target.entity_frame_revision();
        REQUIRE(last_valid_frame);

        auto incomplete = *valid_observation;
        incomplete.packet_entities.front().origin.x.reset();
        incomplete.canonical_state_hash =
            client::runtime_observation_canonical_hash(incomplete);
        const auto missing = created.projection->project_entities(
            incomplete, target);
        CHECK_FALSE(missing);
        REQUIRE(missing.error);
        CHECK(missing.error->code == app::RuntimeReplayVisualProjectionErrorCode::
              incomplete_transform);
        CHECK(target.entity_frame() == last_valid_frame);
        CHECK(target.entity_frame_revision() == last_valid_revision);

        auto non_finite = *valid_observation;
        non_finite.packet_entities.front().angles.z =
            (std::numeric_limits<double>::infinity)();
        non_finite.canonical_state_hash =
            client::runtime_observation_canonical_hash(non_finite);
        const auto invalid = created.projection->project_entities(
            non_finite, target);
        CHECK_FALSE(invalid);
        REQUIRE(invalid.error);
        CHECK(invalid.error->code == app::RuntimeReplayVisualProjectionErrorCode::
              non_finite_transform);
        CHECK(target.entity_frame() == last_valid_frame);
        CHECK(target.entity_frame_revision() == last_valid_revision);
        CHECK(created.projection->summary().presented_runtime_revision ==
              valid_observation->publication_revision);
    }
}
