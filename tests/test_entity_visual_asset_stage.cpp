#include <hlclient/entity_visual/entity_visual_asset_stage.hpp>

#include "entity_visual/entity_pipeline_stage_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

namespace entity = hlclient::entity_visual;
namespace fixture = hlclient::tests::entity_pipeline_stage_fixture;
using hlclient::tests::ScopedLocalResourceTestRoot;

template <typename Stage>
concept HasNetworkTransmissionSurface = requires(Stage& stage) {
    stage.transmit_packet();
};

TEST_CASE("Entity visual asset stage publishes one immutable owning result",
    "[entity-visual][pipeline-stage][asset][ready]")
{
    ScopedLocalResourceTestRoot root;
    const auto inputs = fixture::make_visual_pipeline_inputs(root);
    entity::EntityVisualAssetStage stage;
    CHECK(stage.state() == entity::EntityVisualAssetStageState::idle);

    stage.begin(fixture::kStartTime);
    CHECK(stage.state() ==
        entity::EntityVisualAssetStageState::waiting_for_snapshot_history);
    REQUIRE(stage.provide_snapshot_history(
        inputs.history, inputs.environment, inputs.manifest));
    CHECK(stage.state() ==
        entity::EntityVisualAssetStageState::collecting_visual_references);
    REQUIRE(stage.visual_references_collected());
    CHECK(stage.state() ==
        entity::EntityVisualAssetStageState::resolving_model_slots);
    REQUIRE(stage.model_slots_resolved());
    CHECK(stage.state() ==
        entity::EntityVisualAssetStageState::importing_visual_assets);
    CHECK_FALSE(stage.result());
    REQUIRE(stage.publish_library(inputs.library, {}));
    CHECK(stage.state() ==
        entity::EntityVisualAssetStageState::visual_asset_library_ready);
    CHECK(stage.terminal());
    CHECK_FALSE(stage.error());
    REQUIRE(stage.result());
    CHECK(stage.result()->snapshot_history() == inputs.history);
    CHECK(stage.result()->environment() == inputs.environment);
    CHECK(stage.result()->manifest() == inputs.manifest);
    CHECK(stage.result()->library() == inputs.library);
    CHECK(stage.result()->bindings().empty());

    const auto published = stage.result();
    stage.cancel();
    CHECK(stage.state() ==
        entity::EntityVisualAssetStageState::visual_asset_library_ready);
    CHECK(stage.result() == published);
}

TEST_CASE("Entity visual asset stage has exact named contract states",
    "[entity-visual][pipeline-stage][asset][states]")
{
    using State = entity::EntityVisualAssetStageState;
    constexpr std::array expected{
        std::pair{State::idle, std::string_view{"idle"}},
        std::pair{State::waiting_for_snapshot_history,
            std::string_view{"waiting_for_snapshot_history"}},
        std::pair{State::collecting_visual_references,
            std::string_view{"collecting_visual_references"}},
        std::pair{State::resolving_model_slots,
            std::string_view{"resolving_model_slots"}},
        std::pair{State::importing_visual_assets,
            std::string_view{"importing_visual_assets"}},
        std::pair{State::visual_asset_library_ready,
            std::string_view{"visual_asset_library_ready"}},
        std::pair{State::projection_evidence_pending,
            std::string_view{"projection_evidence_pending"}},
        std::pair{State::missing_asset, std::string_view{"missing_asset"}},
        std::pair{
            State::unsupported_asset, std::string_view{"unsupported_asset"}},
        std::pair{State::import_failed, std::string_view{"import_failed"}},
        std::pair{State::cancelled, std::string_view{"cancelled"}},
        std::pair{State::timed_out, std::string_view{"timed_out"}},
        std::pair{State::backpressure, std::string_view{"backpressure"}},
        std::pair{State::failed, std::string_view{"failed"}},
    };
    for (const auto& [state, name] : expected) {
        CHECK(entity::to_string(state) == name);
    }
}

TEST_CASE("Entity visual asset stage bounds cancellation timeout and pressure",
    "[entity-visual][pipeline-stage][asset][control]")
{
    using namespace std::chrono_literals;

    SECTION("explicit backpressure is resumable")
    {
        entity::EntityVisualAssetStage stage;
        stage.begin(fixture::kStartTime);
        stage.signal_backpressure();
        CHECK(stage.state() ==
            entity::EntityVisualAssetStageState::backpressure);
        REQUIRE(stage.resume_from_backpressure());
        CHECK(stage.state() == entity::EntityVisualAssetStageState::
            waiting_for_snapshot_history);
        stage.cancel();
        CHECK(stage.state() == entity::EntityVisualAssetStageState::cancelled);
        CHECK(stage.terminal());
        CHECK_FALSE(stage.result());
    }

    SECTION("timeout is terminal")
    {
        entity::EntityVisualAssetStage stage{{64U, 5ms}};
        stage.begin(fixture::kStartTime);
        stage.update(fixture::kStartTime + 5ms);
        CHECK(stage.state() == entity::EntityVisualAssetStageState::timed_out);
        CHECK(stage.terminal());
        REQUIRE(stage.error());
    }

    SECTION("transition cap applies deterministic non-resumable pressure")
    {
        ScopedLocalResourceTestRoot root;
        const auto inputs = fixture::make_visual_pipeline_inputs(root);
        entity::EntityVisualAssetStage stage{{1U, std::nullopt}};
        stage.begin(fixture::kStartTime);
        CHECK_FALSE(stage.provide_snapshot_history(
            inputs.history, inputs.environment, inputs.manifest));
        CHECK(stage.state() ==
            entity::EntityVisualAssetStageState::backpressure);
        CHECK_FALSE(stage.resume_from_backpressure());
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
            entity::EntityPipelineStageErrorCode::transition_limit_reached);
    }
}

TEST_CASE("Entity visual asset stage preserves typed failures",
    "[entity-visual][pipeline-stage][asset][failure]")
{
    const auto begin = [](entity::EntityVisualAssetStage& stage) {
        stage.begin(fixture::kStartTime);
    };

    entity::EntityVisualAssetStage projection;
    begin(projection);
    projection.finish_projection_evidence_pending();
    CHECK(projection.state() == entity::EntityVisualAssetStageState::
        projection_evidence_pending);

    entity::EntityVisualAssetStage missing;
    begin(missing);
    missing.finish_missing_asset();
    CHECK(missing.state() == entity::EntityVisualAssetStageState::missing_asset);

    entity::EntityVisualAssetStage unsupported;
    begin(unsupported);
    unsupported.finish_unsupported_asset();
    CHECK(unsupported.state() ==
        entity::EntityVisualAssetStageState::unsupported_asset);

    entity::EntityVisualAssetStage import;
    begin(import);
    import.finish_import_failed();
    CHECK(import.state() == entity::EntityVisualAssetStageState::import_failed);

    entity::EntityVisualAssetStage failed;
    begin(failed);
    const std::string long_context(300U, 'x');
    failed.fail(long_context);
    CHECK(failed.state() == entity::EntityVisualAssetStageState::failed);
    REQUIRE(failed.error());
    CHECK(failed.error()->context.size() ==
        entity::kEntityPipelineStageDiagnosticTextLimit);

    entity::EntityVisualAssetStage invalid{{0U, std::nullopt}};
    invalid.begin(fixture::kStartTime);
    CHECK(invalid.state() == entity::EntityVisualAssetStageState::failed);
    REQUIRE(invalid.error());
    CHECK(invalid.error()->code ==
        entity::EntityPipelineStageErrorCode::invalid_configuration);
}

using AssetResultPointer = std::remove_cvref_t<decltype(
    std::declval<const entity::EntityVisualAssetStage&>().result())>;
static_assert(std::is_const_v<typename AssetResultPointer::element_type>);
static_assert(!std::is_copy_assignable_v<entity::EntityVisualAssetStageResult>);
static_assert(!HasNetworkTransmissionSurface<entity::EntityVisualAssetStage>);

} // namespace
