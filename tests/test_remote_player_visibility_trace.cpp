#include <hlclient/app/remote_player_visibility_trace.hpp>
#include "entity_render/entity_render_test_fixture.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>

namespace {

struct VisibilityTraceFixture final {
  hlclient::client::RuntimeClientObservationState state;
  hlclient::app::ReplayLocalVisualSummary summary;
  hlclient::renderer::RenderCamera camera;
  std::shared_ptr<const hlclient::entity_render::EntitySceneRenderPackage> package;

  VisibilityTraceFixture() {
    using namespace hlclient;
    state.generation=1U; state.publication_revision=19U; state.server_time_seconds=2.5;
    state.entity_metadata.source=client::RuntimeObservationSource{900U,7U,17U,0U,8U};
    client::RuntimePacketEntityObservation source;
    source.entity_number=2U; source.model_index=7U;
    source.ordinary_visual_schema=true; source.player_movement_schema=true;
    source.origin={1.0,2.0,3.0}; source.effects=0U; source.render_mode=0U;
    state.packet_entities.push_back(source);
    summary.model_names.emplace(7U,"models/project-owned.mdl");
    summary.remote_player_count=1U;
    summary.remote_players[0U].entity=2U;
    summary.remote_players[0U].status=game_api::RemotePlayerPresentationStatus::ready;
    summary.remote_players[0U].pose_submitted=true;
    camera.position={0.0F,-128.0F,28.0F}; camera.target={0.0F,0.0F,28.0F};
    auto built=tests::entity_render_fixture::scene_package(
        tests::entity_render_fixture::render_assets(true,false));
    REQUIRE(built);
    package=std::make_shared<const entity_render::EntitySceneRenderPackage>(std::move(*built.package));
  }

  std::shared_ptr<const hlclient::entity_render::EntityRenderFrame> frame(
      const hlclient::entity_render::RuntimeEntityVisibilityStatus visibility=
          hlclient::entity_render::RuntimeEntityVisibilityStatus::visible,
      const std::optional<std::array<float,3U>> light={}) const {
    using namespace hlclient::entity_render;
    EntityRenderFrameBuildInput input;
    input.resource_id=0x8000U; input.resource_revision=3U;
    input.interpolation={0.5,0.0,1.0,0.5F,0x10U,0x11U,
        EntityRenderInterpolationProfile::synthetic_seconds_v1};
    input.studio_poses.push_back({package->studio_assets()[0U]->source_identity(),
        {{1.0F,0.0F,0.0F,0.0F,0.0F,1.0F,0.0F,0.0F,
          0.0F,0.0F,1.0F,0.0F,0.0F,0.0F,0.0F,1.0F}}});
    StudioEntityRenderInstance instance;
    instance.entity_number=2U; instance.studio_asset_index=0U; instance.pose_index=0U;
    instance.transform.origin={11.0F,12.0F,13.0F};
    instance.interpolated_bounds={{-5.0F,-4.0F,-23.0F},{27.0F,28.0F,49.0F}};
    instance.visibility_status=visibility; instance.static_light_rgb=light;
    input.studio_instances.push_back(instance);
    auto built=EntityRenderFrameBuilder{}.build(*package,std::move(input));
    INFO((built.error ? built.error->context : std::string{}));
    REQUIRE(built);
    return std::make_shared<const EntityRenderFrame>(std::move(*built.frame));
  }
};

} // namespace

TEST_CASE("Remote visibility evidence is owned and separates every CPU rejection stage",
          "[e10-near][visibility-trace][core-only]") {
  using namespace hlclient;
  using Stage=app::RemotePlayerVisibilityStage;
  static_assert(std::is_trivially_copyable_v<app::RemotePlayerVisibilitySample>);
  static_assert(sizeof(app::RemotePlayerVisibilitySample)<=512U);
  static_assert(sizeof(app::RemotePlayerVisibilityJournal)<=16U*1024U);
  VisibilityTraceFixture fixture;
  auto frame=fixture.frame({},std::array{0.7F,0.8F,0.9F});
  const auto sample=app::build_remote_player_visibility_sample(2U,fixture.state,fixture.summary,frame.get(),fixture.camera);
  REQUIRE(sample);
  CHECK(sample->stage==Stage::queued_visible);
  CHECK(sample->generation==1U); CHECK(sample->publication_revision==19U);
  CHECK(sample->source_identity==900U); CHECK(sample->source_ordinal==7U);
  CHECK(sample->server_seconds==2.5); CHECK(sample->effects==0U); CHECK(sample->render_mode==0U);
  REQUIRE(sample->entity_origin); REQUIRE(sample->posed_bounds); REQUIRE(sample->static_light_rgb);
  CHECK((*sample->entity_origin)[0U]==11.0); // presented origin, not raw source=1
  CHECK(sample->posed_bounds->minimum.z==-23.0F);
  CHECK((*sample->static_light_rgb)[2U]==0.9F);
  REQUIRE(sample->camera_distance);
  CHECK(*sample->camera_distance==Catch::Approx(std::sqrt(11.0*11.0+140.0*140.0+15.0*15.0)));
  CHECK(sample->near_plane==0.1F);
  auto stage=[&](const entity_render::EntityRenderFrame* current) {
    const auto result=app::build_remote_player_visibility_sample(2U,fixture.state,fixture.summary,current,fixture.camera);
    REQUIRE(result); return result->stage;
  };
  fixture.state.packet_entities[0U].ordinary_visual_schema=false;
  CHECK(stage(frame.get())==Stage::not_visual);
  fixture.state.packet_entities[0U].ordinary_visual_schema=true;
  fixture.summary.model_names.clear(); CHECK(stage(frame.get())==Stage::model_not_advertised);
  fixture.summary.model_names.emplace(7U,"models/project-owned.mdl");
  fixture.state.packet_entities[0U].effects=128U; CHECK(stage(frame.get())==Stage::server_hidden128);
  fixture.state.packet_entities[0U].effects=0U;
  fixture.state.packet_entities[0U].render_mode=1U; CHECK(stage(frame.get())==Stage::unsupported_render_mode);
  fixture.state.packet_entities[0U].render_mode=0U;
  fixture.summary.remote_players[0U].status=game_api::RemotePlayerPresentationStatus::unsupported_skeleton;
  CHECK(stage(frame.get())==Stage::game_not_ready);
  fixture.summary.remote_players[0U].status=game_api::RemotePlayerPresentationStatus::ready;
  fixture.summary.remote_players[0U].pose_submitted=false; CHECK(stage(frame.get())==Stage::pose_unavailable);
  fixture.summary.remote_players[0U].pose_submitted=true; CHECK(stage(nullptr)==Stage::frame_unavailable);
  const auto pvs=fixture.frame(entity_render::RuntimeEntityVisibilityStatus::culled_by_pvs);
  CHECK(stage(pvs.get())==Stage::pvs_culled);
  const auto frustum=fixture.frame(entity_render::RuntimeEntityVisibilityStatus::culled_by_frustum);
  CHECK(stage(frustum.get())==Stage::frustum_culled);
  fixture.state.packet_entities.clear(); CHECK(stage(frame.get())==Stage::source_absent);
  frame.reset(); fixture.summary={}; // evidence has no retained borrow into these owners
  CHECK((*sample->entity_origin)[0U]==11.0);
  CHECK(sample->posed_bounds->maximum.z==49.0F); CHECK((*sample->static_light_rgb)[2U]==0.9F);
}

TEST_CASE("Remote visibility separates unlit dim and legacy-light queueing without changing the frame",
          "[e10-near][visibility-trace][core-only]") {
  using namespace hlclient;
  using Stage=app::RemotePlayerVisibilityStage;
  VisibilityTraceFixture fixture;
  const std::array<std::pair<float,Stage>,4U> cases{{
      {0.01F,Stage::queued_unlit},{0.0101F,Stage::queued_dim},
      {0.119F,Stage::queued_dim},{0.12F,Stage::queued_visible}}};
  for (const auto& [maximum,expected] : cases) {
    const auto frame=fixture.frame({},std::array{0.0F,maximum,0.0F});
    const auto revision=frame->resource_revision();
    const auto sample=app::build_remote_player_visibility_sample(2U,fixture.state,fixture.summary,frame.get(),fixture.camera);
    REQUIRE(sample); CHECK(sample->stage==expected);
    CHECK(frame->resource_revision()==revision); CHECK(frame->draw_commands().size()>0U);
    CHECK(frame->studio_instances()[0U].visibility_status==entity_render::RuntimeEntityVisibilityStatus::visible);
  }
  const auto frame=fixture.frame();
  const auto sample=app::build_remote_player_visibility_sample(2U,fixture.state,fixture.summary,frame.get(),fixture.camera);
  REQUIRE(sample); CHECK(sample->stage==Stage::queued_visible); CHECK_FALSE(sample->static_light_rgb);
  fixture.camera.near_plane=std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(app::build_remote_player_visibility_sample(2U,fixture.state,fixture.summary,frame.get(),fixture.camera));
  fixture.camera.near_plane=0.1F;
  CHECK_FALSE(app::build_remote_player_visibility_sample(0U,fixture.state,fixture.summary,frame.get(),fixture.camera));
  CHECK_FALSE(app::build_remote_player_visibility_sample(33U,fixture.state,fixture.summary,frame.get(),fixture.camera));
  fixture.summary.remote_player_count=33U;
  CHECK_FALSE(app::build_remote_player_visibility_sample(2U,fixture.state,fixture.summary,frame.get(),fixture.camera));
}

TEST_CASE("Remote visibility journal records state and distance-band changes but not per-frame movement",
          "[e10-near][visibility-trace][core-only]") {
  using namespace hlclient;
  using Stage=app::RemotePlayerVisibilityStage;
  app::RemotePlayerVisibilityJournal journal;
  app::RemotePlayerVisibilitySample sample;
  sample.entity=2U; sample.generation=1U;
  CHECK_FALSE(journal.observe(sample)); CHECK(journal.events().empty());
  sample.stage=Stage::queued_visible; sample.model_slot=7U;
  sample.game_status=game_api::RemotePlayerPresentationStatus::ready; sample.camera_distance=128.0;
  REQUIRE(journal.observe(sample));
  sample.camera_position.x=8.0F; sample.source_ordinal=2U; sample.camera_distance=120.0;
  CHECK_FALSE(journal.observe(sample)); // movement within far band is deliberately silent
  sample.camera_distance=96.0; REQUIRE(journal.observe(sample));
  sample.camera_distance=49.0; CHECK_FALSE(journal.observe(sample));
  sample.camera_distance=48.0; REQUIRE(journal.observe(sample));
  sample.camera_distance=16.0; CHECK_FALSE(journal.observe(sample));
  sample.camera_distance=128.0; REQUIRE(journal.observe(sample)); // close -> far even still queued
  sample.stage=Stage::source_absent; sample.camera_distance.reset(); REQUIRE(journal.observe(sample));
  CHECK_FALSE(journal.observe(sample));
  sample.stage=Stage::queued_visible; REQUIRE(journal.observe(sample)); // reappearance
  sample.model_slot=8U; REQUIRE(journal.observe(sample));
  sample.game_status=game_api::RemotePlayerPresentationStatus::missing_fields; REQUIRE(journal.observe(sample));
  CHECK(journal.total()==8U); CHECK(journal.dropped()==0U);
  CHECK(journal.events().front().camera_position.x==0.0F);
  CHECK(journal.events().front().model_slot==7U); // retained records are numeric value copies
  CHECK(app::remote_player_visibility_distance_band(journal.events()[0U])==3U);
  CHECK(app::remote_player_visibility_distance_band(journal.events()[1U])==2U);
  CHECK(app::remote_player_visibility_distance_band(journal.events()[2U])==1U);
}

TEST_CASE("Remote visibility journal keeps last sixteen events and clears generations with bounded validation",
          "[e10-near][visibility-trace][core-only]") {
  using namespace hlclient;
  using Stage=app::RemotePlayerVisibilityStage;
  app::RemotePlayerVisibilityJournal journal;
  app::RemotePlayerVisibilitySample sample;
  sample.entity=2U; sample.generation=1U; sample.model_slot=7U;
  for (std::size_t index=0U;index<24U;++index) {
    sample.source_ordinal=index;
    sample.stage=index%2U ? Stage::pvs_culled : Stage::frustum_culled;
    REQUIRE(journal.observe(sample));
  }
  REQUIRE(journal.events().size()==16U); CHECK(journal.total()==24U); CHECK(journal.dropped()==8U);
  CHECK(journal.events().front().source_ordinal==8U); CHECK(journal.events().back().source_ordinal==23U);
  const auto before=journal.total();
  sample.entity=0U; CHECK_FALSE(journal.observe(sample));
  sample.entity=33U; CHECK_FALSE(journal.observe(sample));
  sample.entity=2U; sample.camera_distance=std::numeric_limits<double>::infinity();
  CHECK_FALSE(journal.observe(sample));
  sample.camera_distance.reset(); sample.posed_bounds=assets::WorldBounds{{1.0F,0.0F,0.0F},{0.0F,0.0F,0.0F}};
  CHECK_FALSE(journal.observe(sample));
  sample.posed_bounds.reset(); sample.static_light_rgb=std::array{-0.1F,0.0F,0.0F};
  CHECK_FALSE(journal.observe(sample)); CHECK(journal.total()==before);
  sample.static_light_rgb.reset(); sample.generation=2U; sample.stage=Stage::source_absent;
  CHECK_FALSE(journal.observe(sample)); CHECK(journal.events().empty()); CHECK(journal.total()==0U);
  sample.stage=Stage::queued_visible;
  for (std::uint32_t entity=1U;entity<=32U;++entity) {
    sample.entity=entity; REQUIRE(journal.observe(sample)); CHECK_FALSE(journal.observe(sample));
  }
  CHECK(journal.total()==32U); CHECK(journal.dropped()==16U);
  REQUIRE(journal.events().size()==16U); CHECK(journal.events().front().entity==17U);
  journal.reset(); CHECK(journal.events().empty()); CHECK(journal.total()==0U); CHECK(journal.dropped()==0U);
}
