#include "entity_render/entity_opengl_test_support.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <catch2/catch_test_macros.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/goldsrc/user_info_update.hpp>
#include <hlclient/goldsrc/usercmd_input_adapter.hpp>
#include <hlclient/input/input_state_tracker.hpp>
#include <limits>

namespace {
namespace api = hlclient::game_api;
namespace client = hlclient::client;
namespace gs = hlclient::goldsrc;
using Bytes = std::vector<std::byte>;
Bytes text_bytes(std::string_view text) {
  Bytes result;
  for (const unsigned char c : text)
    result.push_back(static_cast<std::byte>(c));
  result.push_back(std::byte{0});
  return result;
}
Bytes score(std::uint8_t slot, std::int16_t frags, std::int16_t deaths,
            std::int16_t player_class = 0, std::int16_t team = 0) {
  Bytes result{static_cast<std::byte>(slot)};
  for (const auto value : {frags, deaths, player_class, team}) {
    const auto wire = std::bit_cast<std::uint16_t>(value);
    result.push_back(static_cast<std::byte>(wire & 255U));
    result.push_back(static_cast<std::byte>(wire >> 8U));
  }
  return result;
}
Bytes death(std::uint8_t killer, std::uint8_t victim,
            std::string_view token = "9mmhandgun") {
  Bytes result{static_cast<std::byte>(killer), static_cast<std::byte>(victim)};
  const auto tail = text_bytes(token);
  result.insert(result.end(), tail.begin(), tail.end());
  return result;
}
struct Message {
  std::string name;
  Bytes body;
  std::optional<api::PlayerDisplayUpdate> identity;
};
Message player(std::uint32_t slot, std::string name, bool new_occupant = true,
               bool present = true) {
  return {
      {},
      {},
      api::PlayerDisplayUpdate{slot, present, new_occupant, std::move(name)}};
}
struct Fixture {
  api::GameClientHost host{
      hlclient::games::halflife::make_half_life_client_module()};
  client::RuntimeClientObservationState observation;
  std::uint32_t max_clients{4U};
  std::uint64_t ordinal{1U};
  bool committed{};
  Fixture() {
    observation.generation = 1U;
    host.reset({1U, 1U, 2U});
  }
  api::GameRecordResult stage(std::initializer_list<Message> messages,
                             std::optional<double> presentation_seconds = {}) {
    std::vector<api::GameMessageView> views;
    std::size_t index = 0U;
    for (const auto &message : messages) {
      views.push_back({message.identity ? api::GameMessageKind::player_display
                                        : api::GameMessageKind::user_message,
                       message.name,
                       message.body,
                       {ordinal, static_cast<std::size_t>(ordinal),
                        static_cast<std::uint32_t>(ordinal), index * 512U,
                        (index + 1U) * 512U},
                       message.identity
                           ? std::nullopt
                           : std::optional<std::uint8_t>{std::uint8_t{90}},
                       message.identity});
      ++index;
    }
    ++ordinal;
    return host.stage_record({committed ? &observation : nullptr, observation,
                              views, 2U, max_clients,{},presentation_seconds});
  }
  api::GameRecordState apply(std::initializer_list<Message> messages,
                            std::optional<double> presentation_seconds = {}) {
    auto result = stage(messages,presentation_seconds);
    INFO(result.error.value_or(""));
    REQUIRE(result);
    auto state = *result.state;
    observation.weapon_hud = state.weapon_hud;
    observation.lifecycle = state.lifecycle;
    host.commit_record(std::move(*result.state));
    committed = true;
    return state;
  }
  api::HudState hud(bool visible = true, double time = 0.0,
                    std::uint32_t width = 1280U, std::uint32_t height = 720U) {
    host.set_hud_context({width, height, visible});
    return host.hud(observation, time);
  }
};
std::size_t count_notices(const api::MultiplayerHudState &state) {
  return static_cast<std::size_t>(
      std::count_if(state.notices.begin(), state.notices.end(),
                    [](const auto &n) { return n.has_value(); }));
}
bool contains(const api::HudState &hud, std::string_view text) {
  return std::any_of(
      hud.draw.texts.begin(), hud.draw.texts.end(),
      [&](const auto &t) { return t.text.find(text) != std::string::npos; });
}
api::HudResourceSet killfeed_resources() {
  api::HudResourceSet resources;resources.revision=1;
  api::HudResourceDescriptor descriptor;descriptor.name="sprites/hud.txt";
  descriptor.status=api::HudResourceStatus::ready;
  const auto add=[&](std::string name,std::uint64_t id) {
    api::HudResourceEntry entry;entry.role=std::move(name);entry.resolution=640;
    entry.resource_id=id;entry.resource_revision=2;entry.source={4,7,32,16};
    entry.frame_width=entry.frame_height=128;entry.status=api::HudResourceStatus::ready;
    entry.blend=api::HudSpriteBlend::additive;entry.sprite_name="sprites/project_owned_death.spr";
    descriptor.entries.push_back(std::move(entry));
  };
  add("d_egon",801);add("d_skull",802);add("d_9mmAR",803);
  resources.descriptors.push_back(std::move(descriptor));return resources;
}
} // namespace

TEST_CASE("E29.1 ready original death icon replaces debug weapon text in the normal module path",
          "[e291][e14][hud][killfeed][game-module]") {
  Fixture f;REQUIRE(f.host.bind_hud_resources(killfeed_resources()));
  const auto initial=f.apply({player(1,"HLC_E9_B"),player(2,"HLC_E9_A"),
      {"ScoreInfo",score(1,0,0),{}},{"ScoreInfo",score(2,0,0),{}}});
  const auto published=f.apply({{"DeathMsg",death(1,2,"egon"),{}}});
  CHECK(published.multiplayer.players[0].frags==initial.multiplayer.players[0].frags);
  CHECK(published.multiplayer.players[1].deaths==initial.multiplayer.players[1].deaths);
  const auto hud=f.hud(false,1);
  REQUIRE(hud.draw.sprites.size()==1);const auto& icon=hud.draw.sprites[0];
  CHECK(icon.resource_id==801);CHECK(icon.resource_revision==2);CHECK(icon.source==api::HudSourceRectangle{4,7,32,16});
  CHECK(icon.y==36);CHECK(icon.width==32);CHECK(icon.height==16);CHECK(icon.draw_order==100);
  REQUIRE(icon.clip);CHECK(icon.clip->width==1280);CHECK(icon.clip->height==720);
  REQUIRE(hud.draw.texts.size()==2);
  CHECK(hud.draw.texts[0].text=="HLC_E9_B");CHECK(hud.draw.texts[1].text=="HLC_E9_A");
  CHECK(hud.draw.texts[0].pixel_size==2);CHECK(hud.draw.texts[0].advance==12);
  // E31.1 independently measured14px font and16px icon share a centerline.
  CHECK(hud.draw.texts[0].y==37);CHECK(hud.draw.texts[1].y==37);
  CHECK(hud.draw.texts[1].x+static_cast<float>(hud.draw.texts[1].text.size())*12==1276);
  CHECK_FALSE(contains(hud,"[egon]"));CHECK(api::valid_hud_draw_commands(hud.draw));
}
TEST_CASE("E29.1 death sprite selection preserves exact names skull fallback suicide and snapshotted team colors",
          "[e291][e14][hud][killfeed][game-module]") {
  Fixture f;REQUIRE(f.host.bind_hud_resources(killfeed_resources()));
  f.apply({player(1,"RedKiller"),player(2,"BlueVictim"),{"ScoreInfo",score(1,0,0,0,2),{}},
      {"ScoreInfo",score(2,0,0,0,1),{}}});
  auto notice=f.apply({{"DeathMsg",death(1,2,"9mmAR"),{}}});
  REQUIRE(notice.multiplayer.notices[0]);CHECK(notice.multiplayer.notices[0]->killer_team==2);
  CHECK(notice.multiplayer.notices[0]->victim_team==1);
  f.apply({player(1,"ReusedGreenKiller"),{"ScoreInfo",score(1,0,0,0,4),{}}});
  const auto hud=f.hud(false,1);REQUIRE(hud.draw.sprites.size()==1);CHECK(hud.draw.sprites[0].resource_id==803);
  REQUIRE(hud.draw.texts.size()==2);CHECK(hud.draw.texts[0].text=="RedKiller");
  CHECK(hud.draw.texts[0].color==std::array<float,4>{1,.25F,.25F,1});
  CHECK(hud.draw.texts[1].color==std::array<float,4>{.6F,.8F,1,1});
  Fixture unknown;REQUIRE(unknown.host.bind_hud_resources(killfeed_resources()));
  unknown.apply({player(1,"Alice"),player(2,"Bob"),{"DeathMsg",death(1,2,"unknown;not_a_command"),{}}});
  const auto degraded=unknown.hud(false,1);REQUIRE(degraded.draw.sprites.size()==1);
  CHECK(degraded.draw.sprites[0].resource_id==802);CHECK_FALSE(contains(degraded,"unknown"));
  Fixture self;REQUIRE(self.host.bind_hud_resources(killfeed_resources()));
  self.apply({player(1,"Alice"),{"DeathMsg",death(1,1,"egon"),{}}});
  const auto suicide=self.hud(false,1);REQUIRE(suicide.draw.texts.size()==1);
  CHECK(suicide.draw.texts[0].text=="Alice");CHECK(suicide.draw.sprites[0].resource_id==801);
}
TEST_CASE("E29.1 readable death rows stay bounded expire and coexist with unchanged scoreboard",
          "[e291][e14][hud][killfeed][game-module]") {
  Fixture f;REQUIRE(f.host.bind_hud_resources(killfeed_resources()));
  f.apply({player(1,std::string(31,'A')),player(2,std::string(31,'B')),
      {"DeathMsg",death(1,2,"egon"),{}},{"DeathMsg",death(1,2,"egon"),{}},
      {"DeathMsg",death(1,2,"egon"),{}},{"DeathMsg",death(1,2,"egon"),{}}});
  const auto small=f.hud(false,1,320,240);CHECK(small.draw.sprites.size()==4);
  CHECK(small.draw.texts.size()==8);CHECK(api::valid_hud_draw_commands(small.draw));
  for(const auto& text:small.draw.texts) {
    CHECK(text.x>=4);CHECK(text.x+static_cast<float>(text.text.size())*text.advance<=316);
    CHECK(text.text.size()<31);CHECK(text.text.back()=='-');
  }
  CHECK(small.draw.sprites[1].y==58);CHECK(small.draw.sprites[2].y==80);CHECK(small.draw.sprites[3].y==102);
  CHECK(contains(f.hud(true,1),"PLAYER"));CHECK_FALSE(contains(f.hud(false,1),"PLAYER"));
  CHECK(f.hud(false,7).draw.sprites.empty());CHECK(f.hud(false,7).draw.texts.empty());
}
TEST_CASE("E29.1 unequal death notice widths cannot overlap preceding name glyph rows",
          "[e291][e14][hud][killfeed][game-module]") {
  Fixture f;auto resources=killfeed_resources();
  resources.descriptors[0].entries[2].source.width=16;
  REQUIRE(f.host.bind_hud_resources(resources));
  f.apply({player(1,"KillerAA"),player(2,"VictimBB"),{"DeathMsg",death(1,2,"egon"),{}}});
  f.apply({player(2,"VI"),{"DeathMsg",death(1,2,"9mmAR"),{}}});
  const auto hud=f.hud(false,1,1280,720);
  REQUIRE(hud.draw.sprites.size()==2);REQUIRE(hud.draw.texts.size()==4);
  const auto& following=hud.draw.sprites[1];const auto& previous=hud.draw.texts[1];
  // The shorter victim and narrower icon move the new icon into the old
  // victim's horizontal span: adequate vertical spacing must prevent overlap.
  REQUIRE(following.x<previous.x+static_cast<float>(previous.text.size())*previous.advance);
  REQUIRE(following.x+following.width>previous.x);
  CHECK(following.y>=previous.y+7.0F*previous.pixel_size);
  CHECK(following.y>=previous.y+previous.line_height);
}

TEST_CASE("E29.2 committed killfeed survives the actual host mixed-epoch rounding boundary",
          "[e292][e14][hud][killfeed][clock][game-module]") {
  using Clock=std::chrono::steady_clock;
  const Clock::time_point origin{std::chrono::duration_cast<Clock::duration>(
      std::chrono::nanoseconds{1'000'000'000'000'000LL})};
  const auto received_at=origin+std::chrono::milliseconds{20};
  Fixture f;REQUIRE(f.host.bind_hud_resources(killfeed_resources()));
  f.host.set_movement_audio_time_origin(std::chrono::duration<double>(origin.time_since_epoch()).count());
  const auto committed_time=f.host.presentation_seconds(
      std::chrono::duration<double>(received_at.time_since_epoch()).count());
  REQUIRE(committed_time);
  const auto render_time=std::chrono::duration<double>(received_at-origin).count();
  REQUIRE(*committed_time>render_time); // Exact representative production conversion mismatch, not a guessed epsilon.
  f.apply({player(1,"A_Killer"),player(2,"B_Victim"),{"ScoreInfo",score(1,0,0),{}},
      {"ScoreInfo",score(2,0,0),{}}});
  const auto committed=f.apply({{"DeathMsg",death(1,2,"egon"),{}}},committed_time);
  REQUIRE(committed.multiplayer.notices[0]);CHECK(committed.multiplayer.notices[0]->created_seconds==committed_time);
  const auto first=f.hud(false,render_time);REQUIRE(first.draw.sprites.size()==1);
  CHECK(first.draw.sprites[0].resource_id==801);CHECK(contains(first,"A_Killer"));CHECK(contains(first,"B_Victim"));
  CHECK(committed.multiplayer.players[0].frags==0);CHECK(committed.multiplayer.players[1].deaths==0);
  CHECK(f.hud(false,*committed_time+5.999).draw.sprites.size()==1);
  CHECK(f.hud(false,*committed_time+6.0).draw.sprites.empty()); // Lifetime is still six seconds from the committed receipt.
  CHECK(f.hud(false,*committed_time+1.0).draw.sprites.empty()); // A later backwards sample cannot resurrect expired state.
}
TEST_CASE("E29.2 committed receipt after a cached render sample remains visible without extending expiry",
          "[e292][e14][hud][killfeed][clock][game-module]") {
  Fixture f;REQUIRE(f.host.bind_hud_resources(killfeed_resources()));
  f.apply({player(1,"Alice"),player(2,"Bob")});
  constexpr double cached_frame=10.0,received=10.025;
  const auto committed=f.apply({{"DeathMsg",death(1,2,"egon"),{}}},received);
  REQUIRE(committed.multiplayer.notices[0]);CHECK(committed.multiplayer.notices[0]->created_seconds==received);
  const auto first=f.hud(false,cached_frame);REQUIRE(first.draw.sprites.size()==1);
  CHECK(first.draw.texts.size()==2);CHECK(api::valid_hud_draw_commands(first.draw));
  CHECK(f.hud(false,received+5.999).draw.sprites.size()==1);
  CHECK(f.hud(false,received+6.0).draw.sprites.empty());
}
TEST_CASE("E29.2 A to B to A focus hooks retain resources and fresh kills still render after old expiry",
          "[e292][e14][hud][killfeed][focus][game-module]") {
  Fixture a,b;REQUIRE(a.host.bind_hud_resources(killfeed_resources()));
  REQUIRE(b.host.bind_hud_resources(killfeed_resources()));
  a.host.set_movement_audio_time_origin(1'000'000.0);b.host.set_movement_audio_time_origin(2'000'000.0);
  a.apply({player(1,"A_Killer"),player(2,"B_Victim")});
  b.apply({player(1,"A_Killer"),player(2,"B_Victim")});
  a.apply({{"DeathMsg",death(1,2,"egon"),{}}},1.0);
  REQUIRE(a.hud(false,1.0).draw.sprites.size()==1);CHECK(b.hud(false,1.0).draw.sprites.empty());
  const auto a_reset=a.host.reset_serial(),b_reset=b.host.reset_serial();
  a.host.cancel_uncommitted(); // Exact normal application's focus-lost hook; not teardown/reset.
  const auto a_unfocused=a.hud(false,2.0);REQUIRE(a_unfocused.draw.sprites.size()==1);
  CHECK(a_unfocused.draw.sprites[0].resource_id==801);CHECK(a_unfocused.draw.sprites[0].resource_revision==2);
  b.apply({{"DeathMsg",death(2,1,"9mmAR"),{}}},2.0);CHECK(b.hud(false,2.0).draw.sprites.size()==1);
  b.host.cancel_uncommitted();const auto a_refocused=a.hud(false,2.5);
  CHECK(a_refocused.draw.sprites.size()==1);CHECK(contains(a_refocused,"A_Killer"));
  CHECK(a.host.reset_serial()==a_reset);CHECK(b.host.reset_serial()==b_reset);
  CHECK(a.hud(false,8.0).draw.sprites.empty());CHECK(b.hud(false,8.0).draw.sprites.empty());
  // A new authoritative kill after switching back is independent of the old
  // six-second notice. Its receipt may follow this frame's cached timestamp.
  const auto new_time=a.host.presentation_seconds(1'000'009.025);REQUIRE(new_time);
  const auto fresh=a.apply({{"DeathMsg",death(1,2,"egon"),{}}},new_time);
  REQUIRE(fresh.multiplayer.notices[0]);CHECK(count_notices(fresh.multiplayer)==1);
  const auto new_draw=a.hud(false,9.0);REQUIRE(new_draw.draw.sprites.size()==1);
  CHECK(new_draw.draw.sprites[0].resource_id==801);CHECK(contains(new_draw,"A_Killer"));
  CHECK(a.hud(false,*new_time+5.999).draw.sprites.size()==1);
  CHECK(a.hud(false,*new_time+6.0).draw.sprites.empty());
}
TEST_CASE("E29.2 future cached samples preserve the bounded notice FIFO and real session reset",
          "[e292][e14][hud][killfeed][clock][lifecycle][game-module]") {
  Fixture f;REQUIRE(f.host.bind_hud_resources(killfeed_resources()));
  f.apply({player(1,"Killer"),player(2,"Victim")});
  api::GameRecordState retained;
  for(int i=0;i<5;++i)retained=f.apply({player(2,"Victim"+std::to_string(i),false),
      {"DeathMsg",death(1,2,"egon"),{}}},10.0+static_cast<double>(i+1)*.01);
  REQUIRE(count_notices(retained.multiplayer)==4);
  CHECK(retained.multiplayer.notices[0]->victim_name=="Victim1");
  CHECK(retained.multiplayer.notices[3]->victim_name=="Victim4");
  const auto cached=f.hud(false,10.0);REQUIRE(cached.draw.sprites.size()==4);
  CHECK(cached.draw.texts.size()==8);CHECK(api::valid_hud_draw_commands(cached.draw));
  f.host.reset({2,2,2});f.observation.generation=2;f.committed=false;
  CHECK(f.hud(false,10.1).draw.sprites.empty());CHECK(f.hud(false,10.1).draw.texts.empty());
  REQUIRE(f.host.bind_hud_resources(killfeed_resources()));
  const auto fresh=f.apply({player(1,"FreshKiller"),player(2,"FreshVictim"),
      {"DeathMsg",death(1,2,"egon"),{}}},11.02);
  CHECK(count_notices(fresh.multiplayer)==1);CHECK(f.hud(false,11.0).draw.sprites.size()==1);
}
TEST_CASE("E29.2 missing-descriptor text fallback shares the same committed clock and focus lifetime",
          "[e292][e14][hud][killfeed][clock][focus][fallback][game-module]") {
  Fixture f;f.apply({player(1,"Alice"),player(2,"Bob")});
  constexpr double received=1.01;
  f.apply({{"DeathMsg",death(1,2,"egon"),{}}},received);
  f.host.cancel_uncommitted();const auto first=f.hud(false,1.0);
  CHECK(first.draw.sprites.empty());REQUIRE(contains(first,"Alice [egon] Bob"));
  CHECK(contains(f.hud(false,received+5.999),"Alice [egon] Bob"));
  CHECK(f.hud(false,received+6.0).draw.texts.empty());
  f.apply({{"DeathMsg",death(1,2,"egon"),{}}},10.02);
  CHECK(contains(f.hud(false,9.99),"Alice [egon] Bob"));
}

TEST_CASE("E15 MP5 server death notice never predicts score", "[e15][e14][game-module]") {
  Fixture f;
  auto state=f.apply({player(1,"Shooter"),player(2,"Victim"),{"ScoreInfo",score(1,0,0),{}},
      {"ScoreInfo",score(2,0,0),{}}});
  state=f.apply({{"DeathMsg",death(1,2,"9mmAR"),{}}});
  REQUIRE(state.multiplayer.notices[0]);CHECK(state.multiplayer.notices[0]->weapon=="9mmAR");
  CHECK(state.multiplayer.players[0].frags==0);CHECK(state.multiplayer.players[1].deaths==0);
  state=f.apply({{"ScoreInfo",score(1,1,0),{}},{"ScoreInfo",score(2,0,1),{}}});
  CHECK(state.multiplayer.players[0].frags==1);CHECK(state.multiplayer.players[1].deaths==1);
}
TEST_CASE("E14 exact signed ScoreInfo and record rollback",
          "[e14][game-module][scoreinfo]") {
  Fixture f;
  const auto first =
      f.apply({player(1U, "Alice"),
               player(2U, "Bob"),
               {"ScoreInfo", score(1U, 5, 2), {}},
               {"ScoreInfo", score(2U, -32768, 32767, -1, 2), {}}});
  CHECK(first.multiplayer.players[1].frags == -32768);
  CHECK(first.multiplayer.players[1].deaths == 32767);
  CHECK(first.multiplayer.players[1].player_class == -1);
  CHECK(first.multiplayer.players[1].team_number == 2);
  for (std::size_t length = 0U; length < 9U; ++length) {
    auto body = score(1U, 42, 12);
    body.resize(length);
    auto result =
        f.stage({{"ScoreInfo", score(2U, 3, 4), {}}, {"ScoreInfo", body, {}}});
    REQUIRE_FALSE(result);
    REQUIRE(result.message_failure);
    CHECK(result.message_failure->code ==
          api::GameMessageErrorCode::invalid_body);
    CHECK(contains(f.hud(), "Alice"));
    const auto unchanged = f.apply({});
    CHECK(unchanged.multiplayer.players[1].frags == -32768);
  }
  for (const auto slot : {0U, 5U, 255U})
    CHECK_FALSE(f.stage(
        {{"ScoreInfo", score(static_cast<std::uint8_t>(slot), 0, 0), {}}}));
  auto extra = score(1U, 0, 0);
  extra.push_back(std::byte{0});
  CHECK_FALSE(f.stage({{"ScoreInfo", extra, {}}}));
  const auto max =
      f.apply({player(4U, "Max"), {"ScoreInfo", score(4U, 32767, -32768), {}}});
  CHECK(max.multiplayer.players[3].frags == 32767);
  CHECK(max.multiplayer.players[3].deaths == -32768);
}

TEST_CASE("E14 DeathMsg is notice-only with strict validation and stable name "
          "snapshots",
          "[e14][game-module][deathmsg]") {
  Fixture f;
  auto before = f.apply({player(1U, "Alice"),
                         player(2U, "Bob"),
                         {"ScoreInfo", score(1U, 0, 0), {}},
                         {"ScoreInfo", score(2U, 0, 0), {}}});
  const auto life = before.lifecycle;
  auto killed = f.apply({{"DeathMsg", death(1U, 2U), {}}});
  CHECK(killed.multiplayer.players[0].frags == 0);
  CHECK(killed.multiplayer.players[1].deaths == 0);
  REQUIRE(killed.multiplayer.notices[0]);
  CHECK(killed.multiplayer.notices[0]->victim_name == "Bob");
  CHECK(killed.lifecycle.state == life.state);
  CHECK(killed.lifecycle.life_epoch == life.life_epoch);
  CHECK(killed.lifecycle.deaths == life.deaths);
  CHECK(contains(f.hud(false), "Alice [9mmhandgun] Bob"));
  auto renamed = f.apply({player(2U, "NewBob", false),
                          {"ScoreInfo", score(1U, 1, 0), {}},
                          {"ScoreInfo", score(2U, 0, 1), {}}});
  CHECK(renamed.multiplayer.players[0].frags == 1);
  CHECK(renamed.multiplayer.players[1].deaths == 1);
  CHECK(renamed.multiplayer.notices[0]->victim_name == "Bob");
  auto next = f.apply({{"DeathMsg", death(1U, 2U), {}}});
  CHECK(next.multiplayer.notices[1]->victim_name == "NewBob");
  for (const auto body :
       {death(0U, 0U), death(5U, 2U), death(1U, 5U),
        death(1U, 2U, "bad\ntoken"), death(1U, 2U, std::string(64U, 'x'))})
    CHECK_FALSE(f.stage({{"DeathMsg", body, {}}}));
  auto truncated = death(1U, 2U);
  truncated.pop_back();
  CHECK_FALSE(f.stage({{"DeathMsg", truncated, {}}}));
  auto trailing = death(1U, 2U);
  trailing.push_back(std::byte{1});
  CHECK_FALSE(f.stage({{"DeathMsg", trailing, {}}}));
  const auto legal = f.apply({{"DeathMsg", death(0U, 1U, "world"), {}},
                              {"DeathMsg", death(2U, 2U, ""), {}},
                              {"DeathMsg", death(1U, 255U, "sentrygun"), {}}});
  REQUIRE(legal.multiplayer.notices[3]);
  CHECK(legal.multiplayer.notices[3]->kind == api::DeathNoticeKind::non_player);
  CHECK(legal.multiplayer.notices[3]->victim_name == "sentrygun");
  const auto maximum =
      f.apply({{"DeathMsg", death(1U, 2U, std::string(63U, 'x')), {}}});
  REQUIRE(maximum.multiplayer.notices[3]);
  CHECK(maximum.multiplayer.notices[3]->weapon.size() == 63U);
  f.apply({{"DeathMsg", death(0U, 255U, "sentrygun"), {}}});
  CHECK(contains(f.hud(false), "sentrygun [sentrygun] WORLD"));
}

TEST_CASE("E14 sorting local highlight visibility and slot reuse",
          "[e14][game-module][hud]") {
  Fixture f;
  auto state = f.apply({player(1U, "Alice"),
                        player(2U, "Bob"),
                        player(3U, "Carol"),
                        player(4U, "Dave"),
                        {"ScoreInfo", score(1U, 5, 2), {}},
                        {"ScoreInfo", score(2U, 10, 8), {}},
                        {"ScoreInfo", score(3U, 10, 3), {}},
                        {"ScoreInfo", score(4U, -1, 0), {}}});
  api::HudPresentationContext diagnostic_context{1280U,720U,true};
  diagnostic_context.diagnostic_overlay=true; // This legacy assertion includes the explicit basic diagnostic panel.
  f.host.set_hud_context(diagnostic_context);
  auto hud = f.host.hud(f.observation,0.0);
  REQUIRE_FALSE(hud.draw_rejected);
  std::vector<std::string> names;
  for (const auto &text : hud.draw.texts)
    if (text.text == "Carol" || text.text == "Bob" || text.text == "Alice" ||
        text.text == "Dave")
      names.push_back(text.text);
  CHECK(names == std::vector<std::string>{"Carol", "Bob", "Alice", "Dave"});
  REQUIRE(hud.draw.rectangles.size() ==
          3U); // Basic HUD, table, exactly one local highlight.
  const auto bob = std::find_if(hud.draw.texts.begin(), hud.draw.texts.end(),
                                [](const auto &t) { return t.text == "Bob"; });
  REQUIRE(bob != hud.draw.texts.end());
  CHECK(hud.draw.rectangles.back().y == bob->y - 1.0F);
  CHECK_FALSE(contains(f.hud(false), "PLAYER"));
  CHECK(contains(f.hud(true), "PLAYER"));
  f.apply({{"DeathMsg", death(1U, 2U), {}}});
  state = f.apply({player(2U, "", false, false)});
  CHECK_FALSE(state.multiplayer.players[1].present);
  const auto absent = f.hud();
  CHECK(std::none_of(absent.draw.texts.begin(), absent.draw.texts.end(),
                     [](const auto &t) { return t.text == "Bob"; }));
  state = f.apply({player(2U, "Eve"), {"ScoreInfo", score(2U, -1, 4), {}}});
  CHECK(state.multiplayer.players[1].name == "Eve");
  CHECK(state.multiplayer.players[1].team.empty());
  CHECK(state.multiplayer.players[1].frags == -1);
  CHECK(state.multiplayer.notices[0]->victim_name == "Bob");
  f.apply({player(2U, "Replacement")});
  state = f.apply({});
  CHECK_FALSE(state.multiplayer.players[1].has_score);
  CHECK(state.multiplayer.players[1].frags == 0);
  f.host.reset({2U, 2U, 2U});
  f.committed = false;
  CHECK_FALSE(contains(f.hud(), "Eve"));
  CHECK_FALSE(contains(f.hud(false), "9mmhandgun"));
  f.host.teardown();
  CHECK(f.hud().draw.texts.empty());
}

TEST_CASE("E14 bounded notices lifetime duplicate and malformed suffix",
          "[e14][game-module][hud]") {
  Fixture f;
  f.apply({player(1U, "Alice"), player(2U, "Bob")});
  const auto action = f.ordinal;
  f.apply({{"DeathMsg", death(1U, 2U), {}}});
  f.ordinal = action;
  auto duplicate = f.apply({{"DeathMsg", death(1U, 2U), {}}});
  CHECK(count_notices(duplicate.multiplayer) == 1U);
  for (int i = 0; i < 4; ++i)
    f.apply({{"DeathMsg", death(1U, 2U), {}}});
  auto state = f.apply({});
  CHECK(count_notices(state.multiplayer) == 4U);
  const auto hud = f.hud(false, 1.0);
  CHECK(contains(hud, "9mmhandgun"));
  CHECK(contains(f.hud(false, 6.99), "9mmhandgun"));
  CHECK_FALSE(contains(f.hud(false, 7.0), "9mmhandgun"));
  auto invalid = death(1U, 2U);
  invalid.pop_back();
  CHECK_FALSE(f.stage(
      {{"ScoreInfo", score(1U, 50, 2), {}}, {"DeathMsg", invalid, {}}}));
  state = f.apply({});
  CHECK_FALSE(state.multiplayer.players[0].has_score);
  CHECK(count_notices(state.multiplayer) == 0U);
}

TEST_CASE("E14 team state strict bodies and authoritative aggregate",
          "[e14][game-module][teamplay]") {
  Fixture f;
  auto red = text_bytes("Red");
  red.insert(red.begin(), std::byte{1});
  auto blue = text_bytes("Blue");
  blue.insert(blue.begin(), std::byte{2});
  auto aggregate = text_bytes("RED");
  aggregate.insert(aggregate.end(),
                   {std::byte{40}, std::byte{0}, std::byte{2}, std::byte{0}});
  const auto state = f.apply({player(1U, "Alice"),
                              player(2U, "Bob"),
                              {"GameMode", {std::byte{1}}, {}},
                              {"TeamInfo", red, {}},
                              {"TeamInfo", blue, {}},
                              {"ScoreInfo", score(1U, 1, 4), {}},
                              {"ScoreInfo", score(2U, 10, 2), {}},
                              {"TeamScore", aggregate, {}}});
  CHECK(state.multiplayer.teamplay);
  CHECK(state.multiplayer.players[0].team == "red");
  auto hud = f.hud();
  CHECK(contains(hud, "TEAM red"));
  CHECK(contains(hud, "40"));
  CHECK_FALSE(f.stage({{"GameMode", {}, {}}}));
  CHECK_FALSE(f.stage({{"GameMode", {std::byte{2}}, {}}}));
  red.push_back(std::byte{1});
  CHECK_FALSE(f.stage({{"TeamInfo", red, {}}}));
  aggregate.pop_back();
  CHECK_FALSE(f.stage({{"TeamScore", aggregate, {}}}));
  Fixture g;
  g.max_clients = 32U;
  for (std::uint32_t slot = 1U; slot <= 32U; ++slot) {
    auto team = text_bytes("Red");
    team.insert(team.begin(), static_cast<std::byte>(slot));
    g.apply({player(slot, "Player" + std::to_string(slot)),
             {"ScoreInfo",
              score(static_cast<std::uint8_t>(slot), -32768, 32767),
              {}},
             {"TeamInfo", team, {}}});
  }
  g.apply({{"GameMode", {std::byte{1}}, {}}});
  for (const auto width : {160U, 320U, 1280U}) {
    const auto sums = g.hud(true, 0.0, width, 480U);
    REQUIRE_FALSE(sums.draw_rejected);
    CHECK(contains(sums, "-1048576"));
    CHECK(contains(sums, "1048544"));
    for (const auto &t : sums.draw.texts)
      if (!t.text.empty() &&
          (t.text[0] == '-' || (t.text[0] >= '0' && t.text[0] <= '9'))) {
        CHECK(static_cast<float>(t.text.size()) * t.advance <= 48.001F);
        CHECK(t.x + static_cast<float>(t.text.size()) * t.advance <=
              static_cast<float>(width) - 8.0F);
      }
  }
}

TEST_CASE("E14 worst bounded HUD and viewport sizes",
          "[e14][game-module][hud-bounds]") {
  Fixture f;
  f.max_clients = 32U;
  for (std::uint32_t slot = 1U; slot <= 32U; ++slot) {
    f.apply({player(slot, std::string(31U, 'A')),
             {"ScoreInfo", score(static_cast<std::uint8_t>(slot), 1, 0), {}}});
    auto team = text_bytes("Team" + std::to_string(slot));
    team.insert(team.begin(), static_cast<std::byte>(slot));
    f.apply({{"TeamInfo", team, {}}});
  }
  f.apply({{"GameMode", {std::byte{1}}, {}},
           {"DeathMsg", death(1U, 2U), {}},
           {"DeathMsg", death(3U, 4U), {}},
           {"DeathMsg", death(5U, 6U), {}},
           {"DeathMsg", death(7U, 8U), {}}});
  for (const auto width : {160U, 320U, 640U, 1280U})
    for (const auto height : {120U, 240U, 480U, 720U}) {
      const auto hud = f.hud(true, 0.0, width, height);
      CHECK_FALSE(hud.draw_rejected);
      REQUIRE(api::valid_hud_draw_commands(hud.draw));
      CHECK(hud.draw.texts.size() <= api::kMaximumHudTexts);
      CHECK(contains(hud, "PLAYER"));
      for (const auto &t : hud.draw.texts) {
        CHECK(t.x >= 0.0F);
        CHECK(t.y >= 0.0F);
        CHECK(t.x < static_cast<float>(width));
        CHECK(t.y + 7.0F * t.pixel_size <= static_cast<float>(height));
      }
    }
  CHECK_FALSE(f.stage({player(1U, std::string(32U, 'A'))}));
  auto bad = f.hud().draw;
  bad.texts.resize(api::kMaximumHudTexts + 1U);
  CHECK_FALSE(api::valid_hud_draw_commands(bad));
  bad = f.hud().draw;
  bad.texts[0].x = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(api::valid_hud_draw_commands(bad));
}

TEST_CASE("E14 safe userinfo projection retains privacy and slot mapping",
          "[e14][userinfo]") {
  const auto payload = [](std::string_view info, std::uint8_t index = 0U,
                          std::uint32_t uid = 123U) {
    Bytes bytes{std::byte{13}, static_cast<std::byte>(index)};
    for (unsigned i = 0; i < 4U; ++i)
      bytes.push_back(static_cast<std::byte>((uid >> (8U * i)) & 255U));
    auto tail = text_bytes(info);
    bytes.insert(bytes.end(), tail.begin(), tail.end());
    bytes.resize(bytes.size() + 16U, std::byte{0xA5});
    return bytes;
  };
  const auto a = gs::UserInfoUpdateParser{}.parse(
      payload("\\name\\Alice\\private\\secret"));
  REQUIRE(a);
  CHECK(a.state->player_display_name() == "Alice");
  CHECK(a.state->has_private_user_id());
  const auto rename =
      gs::UserInfoUpdateParser{}.parse(payload("\\name\\Rename"));
  REQUIRE(rename);
  CHECK(rename.state->same_occupant(*a.state));
  const auto replacement =
      gs::UserInfoUpdateParser{}.parse(payload("\\name\\Alice", 0U, 124U));
  REQUIRE(replacement);
  CHECK_FALSE(replacement.state->same_occupant(*a.state));
  const auto odd = gs::UserInfoUpdateParser{}.parse(payload(
      std::string("\\name\\A\nB") + std::bit_cast<char>(std::uint8_t{255U})));
  REQUIRE(odd);
  CHECK(odd.state->player_display_name() == "A?B?");
  const auto max = gs::UserInfoUpdateParser{}.parse(
      payload("\\name\\" + std::string(80U, 'x')));
  REQUIRE(max);
  CHECK(max.state->player_display_name().size() == 31U);
  CHECK_FALSE(gs::UserInfoUpdateParser{}.parse(payload("\\name\\A\\name\\B")));
  CHECK_FALSE(gs::UserInfoUpdateParser{}.parse(payload("\\name\\")));
  CHECK(gs::player_slot_from_client_index(0U, 32U) == 1U);
  CHECK(gs::player_slot_from_client_index(31U, 32U) == 32U);
  CHECK_FALSE(gs::player_slot_from_client_index(31U, 8U));
  CHECK_FALSE(gs::player_slot_from_client_index(0U, 0U));
}

TEST_CASE("E14 normal production HUD OpenGL scoreboard notice score A B",
          "[e14][opengl][actual-context]") {
  auto context = hlclient::tests::entity_opengl_fixture::try_context(640, 480);
  if (!context)
    SKIP("OpenGL 3.3 Core context unavailable");
  context->initialize_renderer();
  Fixture f;
  f.apply({player(1U, "Alice"),
           player(2U, "Bob"),
           {"ScoreInfo", score(1U, 5, 2), {}},
           {"ScoreInfo", score(2U, 0, 0), {}}});
  const auto render = [&](bool held, double time = 0.0) {
    hlclient::renderer::RenderScene scene;
    scene.basic_hud = f.hud(held, time, 640U, 480U).draw;
    context->renderer().render(scene, {640, 480});
    auto pixels = context->renderer().observe_framebuffer(
        {640, 480}, scene.clear_color, true);
    REQUIRE(pixels);
    return pixels.rgba8;
  };
  const auto region = [](const auto &a, const auto &b, int x0, int y0, int x1,
                         int y1) {
    std::size_t changed = 0U;
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        const auto offset = (static_cast<std::size_t>(479 - y) * 640U +
                             static_cast<std::size_t>(x)) *
                            4U;
        if (!std::equal(a.begin() + static_cast<std::ptrdiff_t>(offset),
                        a.begin() + static_cast<std::ptrdiff_t>(offset + 3U),
                        b.begin() + static_cast<std::ptrdiff_t>(offset)))
          ++changed;
      }
    return changed;
  };
  const auto hidden = render(false), shown = render(true);
  CHECK(region(hidden, shown, 8, 100, 632, 220) > 500U);
  f.apply({{"ScoreInfo", score(1U, 6, 2), {}}});
  const auto updated = render(true);
  CHECK(region(shown, updated, 8, 100, 632, 220) > 0U);
  CHECK(region(shown, updated, 0, 240, 640, 480) == 0U);
  f.apply({{"DeathMsg", death(1U, 2U), {}}});
  const auto notice_hud = f.hud(true, 1.0, 640U, 480U).draw;
  const auto notice_text = std::find_if(
      notice_hud.texts.begin(), notice_hud.texts.end(),
      [](const auto &t) { return t.text == "Alice [9mmhandgun] Bob"; });
  REQUIRE(notice_text != notice_hud.texts.end());
  // E31 keeps the fallback kill feed at the same compact top-right anchor
  // as the sprite feed, independently of the scoreboard below it. A 5x7
  // glyph at pixel size 2 occupies this exact 262x14 ink rectangle.
  CHECK(notice_text->text.size() == 22U);
  CHECK(notice_text->x == 372.0F);
  CHECK(notice_text->y == 34.0F);
  CHECK(notice_text->pixel_size == 2.0F);
  CHECK(notice_text->advance == 12.0F);
  CHECK(notice_text->line_height == 18.0F);
  const auto notice = render(true, 1.0);
  const auto notice_changed = region(updated, notice, 372, 34, 634, 48);
  CHECK(notice_changed > 0U);
  CHECK(region(updated, notice, 0, 0, 640, 480) == notice_changed);
  // The normal font must render the chosen neutral weapon delimiters, not
  // substitute its unknown-glyph question mark for both brackets.
  hlclient::renderer::RenderScene unknown_glyph_scene;
  unknown_glyph_scene.basic_hud = notice_hud;
  for (auto &t : unknown_glyph_scene.basic_hud->texts)
    for (auto &c : t.text)
      if (c == '[' || c == ']')
        c = '?';
  context->renderer().render(unknown_glyph_scene, {640, 480});
  const auto substituted = context->renderer().observe_framebuffer(
      {640, 480}, unknown_glyph_scene.clear_color, true);
  REQUIRE(substituted);
  const auto delimiters_changed =
      region(notice, substituted.rgba8, 372, 34, 634, 48);
  CHECK(delimiters_changed > 0U);
  CHECK(region(notice, substituted.rgba8, 0, 0, 640, 480) ==
        delimiters_changed);
  const auto expired = render(true, 7.0);
  CHECK(expired == updated);
  CHECK(render(false, 7.0) == hidden);
}

TEST_CASE(
    "E14 TAB down held up remains local while movement look reload continue",
    "[e14][gameplay-input]") {
  namespace input = hlclient::input;
  namespace gameplay = hlclient::gameplay_input;
  Fixture f;
  f.apply({player(1U, "Alice"), player(2U, "Bob")});
  auto bindings = gameplay::GameplayInputBindings::project_default_v1();
  REQUIRE(bindings);
  input::InputStateTracker tracker;
  tracker.begin_frame();
  tracker.apply_event(input::InputEvent::focus_gained());
  tracker.apply_event(input::InputEvent::capture_acquired());
  tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::w));
  tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::d));
  tracker.apply_event(input::InputEvent::key_pressed(input::PhysicalKey::r));
  const auto baseline = gameplay::GameplayInputIntentBuilder{}.build(
      tracker.publish_snapshot(), *bindings.bindings, {}, 0.02);
  REQUIRE(baseline);
  tracker.end_frame();
  const auto wire = gs::GoldSrcUserCmdButtonMapping::synthetic_explicit_v1(
                        baseline.intent->held_buttons())
                        .wire_buttons;
  for (const bool held : {true, true, false}) {
    tracker.begin_frame();
    tracker.apply_event(input::InputEvent::mouse_motion(12, -6));
    tracker.apply_event(
        held ? input::InputEvent::key_pressed(input::PhysicalKey::tab)
             : input::InputEvent::key_released(input::PhysicalKey::tab));
    const auto built = gameplay::GameplayInputIntentBuilder{}.build(
        tracker.publish_snapshot(), *bindings.bindings, {}, 0.02);
    REQUIRE(built);
    const auto &intent = *built.intent;
    CHECK(intent.forward_axis() == baseline.intent->forward_axis());
    CHECK(intent.side_axis() == baseline.intent->side_axis());
    CHECK(intent.look_delta_yaw_degrees() != 0.0);
    CHECK(intent.look_delta_pitch_degrees() != 0.0);
    CHECK(gameplay::gameplay_button_is_set(intent.held_buttons(),
                                           gameplay::GameplayButton::reload));
    CHECK(gs::GoldSrcUserCmdButtonMapping::synthetic_explicit_v1(
              intent.held_buttons())
              .wire_buttons == wire);
    const bool show = gameplay::gameplay_button_is_set(
        intent.held_buttons(), gameplay::GameplayButton::scoreboard);
    CHECK(show == held);
    CHECK(contains(f.hud(show), "PLAYER") == held);
    tracker.end_frame();
  }
}

TEST_CASE("E14 score values are idempotent stale ordering and receipt expiry "
          "are deterministic",
          "[e14][game-module]") {
  Fixture f;
  auto state = f.apply({player(1U, "Alice"),
                        player(2U, "Bob"),
                        {"ScoreInfo", score(1U, 3, 1), {}},
                        {"ScoreInfo", score(2U, 3, 1), {}}});
  const auto revision = state.multiplayer.players[0].score_revision;
  state = f.apply({{"ScoreInfo", score(1U, 3, 1), {}}});
  CHECK(state.multiplayer.players[0].score_revision == revision);
  f.ordinal = 1U;
  state = f.apply({{"ScoreInfo", score(1U, 99, 99), {}}});
  CHECK(state.multiplayer.players[0].frags == 3);
  std::vector<std::string> names;
  for (const auto &t : f.hud().draw.texts)
    if (t.text == "Alice" || t.text == "Bob")
      names.push_back(t.text);
  CHECK(names == std::vector<std::string>{"Alice", "Bob"});
  api::GameMessageView event{
      api::GameMessageKind::user_message, "DeathMsg",       {},
      {20U, 20U, 20U, 0U, 128U},          std::uint8_t{91}, {}};
  const auto body = death(1U, 2U, "teammate");
  event.body = body;
  const std::array messages{event};
  auto staged = f.host.stage_record(
      {&f.observation, f.observation, messages, 2U, 4U, {}, 1.0});
  REQUIRE(staged);
  REQUIRE(staged.state->multiplayer.notices[0]);
  CHECK(staged.state->multiplayer.notices[0]->kind ==
        api::DeathNoticeKind::teammate);
  f.host.commit_record(std::move(*staged.state));
  CHECK_FALSE(contains(f.hud(false, 7.0), "teammate"));
  CHECK_FALSE(contains(
      f.hud(false, 7.01),
      "teammate")); // Delayed first render cannot resurrect an expired receipt.
}
