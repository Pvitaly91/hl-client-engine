#include <algorithm>
#include <bit>
#include <cmath>
#include <hlclient/games/halflife/multiplayer.hpp>
#include <hlclient/games/halflife/sprite_hud.hpp>
#include <numeric>

namespace hlclient::games::halflife {
namespace {
std::string display(std::string_view value, std::size_t maximum = 31U) {
  std::string result;
  for (const unsigned char c : value) {
    if (result.size() == maximum)
      break;
    result.push_back(c >= 32U && c <= 126U ? static_cast<char>(c) : '?');
  }
  return result;
}
std::string team_key(std::string_view value) {
  auto result = display(value);
  for (auto &c : result)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return result;
}
bool newer(const client::RuntimeObservationSource &source,
           const std::optional<client::RuntimeObservationSource> &high_water) {
  return !high_water || source.record_ordinal > high_water->record_ordinal ||
         (source.record_ordinal == high_water->record_ordinal &&
          source.end_bit_offset > high_water->end_bit_offset);
}
std::string player_name(const game_api::MultiplayerHudState &state,
                        std::uint32_t slot) {
  if (slot && slot <= state.maximum_clients &&
      !state.players[slot - 1U].name.empty())
    return state.players[slot - 1U].name;
  return "Player " + std::to_string(slot);
}
bool ready_notice_icon(const game_api::HudResourceEntry* entry) noexcept {
  return entry && entry->status==game_api::HudResourceStatus::ready &&
      entry->resource_id && entry->resource_revision && entry->source.width && entry->source.height &&
      entry->source.x<=entry->frame_width && entry->source.width<=entry->frame_width-entry->source.x &&
      entry->source.y<=entry->frame_height && entry->source.height<=entry->frame_height-entry->source.y;
}
const game_api::HudResourceEntry* notice_role(const game_api::HudResourceDescriptor& descriptor,
    std::string_view role,std::uint32_t tier) noexcept {
  const auto found=std::find_if(descriptor.entries.begin(),descriptor.entries.end(),[&](const auto& entry){
    return entry.role==role && entry.resolution==tier;
  });
  return found==descriptor.entries.end() ? nullptr : &*found;
}
const game_api::HudResourceEntry* notice_icon(const game_api::HudResourceDescriptor& descriptor,
    std::string_view weapon,std::uint32_t tier) {
  // A death token is inert role metadata, never a path/open request. Exact
  // case is important for the original d_9mmAR role.
  const auto* icon=notice_role(descriptor,"d_"+std::string(weapon),tier);
  if(ready_notice_icon(icon))return icon;
  const auto* skull=notice_role(descriptor,"d_skull",tier);
  return ready_notice_icon(skull) ? skull : nullptr;
}
std::array<float,4U> notice_name_color(std::optional<std::int16_t> team) noexcept {
  switch(team.value_or(0)) {
  case 0:case 3:return {1.0F,.7F,0.0F,1.0F};
  case 1:return {.6F,.8F,1.0F,1.0F};
  case 2:return {1.0F,.25F,.25F,1.0F};
  case 4:return {.6F,1.0F,.6F,1.0F};
  default:return {.8F,.8F,.8F,1.0F};
  }
}
std::string notice_elide(std::string_view value,std::size_t maximum) {
  if(value.size()<=maximum)return std::string(value);
  if(maximum<2U)return std::string(value.substr(0,maximum));
  // The existing project glyph set supports '-', not '.'. Keep the bound
  // and visibly mark truncation without introducing a second font path.
  return std::string(value.substr(0,maximum-1U))+"-";
}
bool compose_sprite_notices(const game_api::MultiplayerHudState& state,game_api::HudState& hud,
    game_api::HudPresentationContext context,const game_api::HudResourceDescriptor* descriptor,std::uint32_t tier) {
  if(!descriptor || descriptor->status!=game_api::HudResourceStatus::ready || !tier)return false;
  const float width=static_cast<float>(context.viewport_width),height=static_cast<float>(context.viewport_height);
  // The pinned death.cpp measures its independent console font; sprite tier
  // is not a font size. Our existing5x7 bitmap font stays14px at1080p (21px
  // beyond1440p). User scale is applied once, isotropically, not by icon size.
  const float user=halflife_hud_user_scale(context);
  const float pixels=(context.viewport_height>1440U ? 3.0F : 2.0F)*user;
  const float icon_scale=halflife_hud_automatic_scale(context,descriptor,tier)*user;
  const float advance=pixels*6.0F,line_height=pixels*8.0F;
  const float margin=4.0F*user;
  const float maximum_row=std::min(width-2.0F*margin,640.0F*user);
  hud.killfeed_font_pixel_size=pixels;hud.killfeed_icon_scale=icon_scale;
  hud.killfeed_maximum_row_width=maximum_row;
  // The stock console-font offset is four pixels. Our neutral bitmap font's
  // whole line box must fit before the next icon even when row widths differ.
  float row_gap=4.0F*user+line_height;
  if(const auto* skull=notice_role(*descriptor,"d_skull",tier);ready_notice_icon(skull))
    row_gap=std::max(row_gap,static_cast<float>(skull->source.height)*icon_scale);
  for(const auto& notice:state.notices)if(notice)
    if(const auto* icon=notice_icon(*descriptor,notice->weapon,tier))
      row_gap=std::max(row_gap,static_cast<float>(icon->source.height)*icon_scale);
  std::size_t row=0U;
  for(const auto& notice:state.notices)if(notice) {
    const float y=34.0F*user+static_cast<float>(row++)*(row_gap+2.0F*user);
    if(y+row_gap>height-margin)break; // retained notice state/expiry is unaffected
    const auto* icon=notice_icon(*descriptor,notice->weapon,tier);
    const float icon_width=icon ? static_cast<float>(icon->source.width)*icon_scale : 0.0F;
    const float icon_height=icon ? static_cast<float>(icon->source.height)*icon_scale : 0.0F;
    const float text_y=y+std::max(0.0F,(row_gap-pixels*7.0F)*.5F);
    const float icon_y=y+std::max(0.0F,(row_gap-icon_height)*.5F);
    const bool killer_visible=notice->killer!=0U && notice->kind!=game_api::DeathNoticeKind::suicide;
    const bool victim_visible=notice->kind!=game_api::DeathNoticeKind::non_player;
    const float gap=killer_visible ? 5.0F*user : 0.0F;
    const auto maximum=static_cast<std::size_t>(std::max(0.0F,std::floor((maximum_row-icon_width-gap)/advance)));
    std::size_t killer_budget=0U,victim_budget=0U;
    if(killer_visible && victim_visible) {
      victim_budget=std::min(notice->victim_name.size(),maximum/2U);
      killer_budget=std::min(notice->killer_name.size(),maximum-victim_budget);
      const auto spare=maximum-killer_budget-victim_budget;
      victim_budget+=std::min(spare,notice->victim_name.size()-victim_budget);
    } else if(killer_visible)killer_budget=maximum;
    else if(victim_visible)victim_budget=maximum;
    const auto killer=killer_visible ? notice_elide(notice->killer_name,killer_budget) : std::string{};
    const auto victim=victim_visible ? notice_elide(notice->victim_name,victim_budget) : std::string{};
    float x=std::max(margin,width-margin-icon_width-gap-static_cast<float>(killer.size()+victim.size())*advance);
    if(!killer.empty()) {
      hud.draw.texts.push_back({killer,x,text_y,pixels,advance,line_height,notice_name_color(notice->killer_team),100U});
      x+=static_cast<float>(killer.size())*advance+gap;
    }
    if(icon && hud.draw.sprites.size()<game_api::kMaximumHudSprites) {
      const std::array<float,4U> tint=notice->kind==game_api::DeathNoticeKind::teammate
          ? std::array<float,4U>{10.0F/255.0F,240.0F/255.0F,10.0F/255.0F,1.0F}
          : std::array<float,4U>{1.0F,80.0F/255.0F,0.0F,1.0F};
      hud.draw.sprites.push_back({icon->resource_id,icon->resource_revision,icon->frame,icon->source,
          x,icon_y,icon_width,icon_height,tint,icon->blend,
          game_api::HudRectangle{0,0,width,height,{0,0,0,0}},100U});
      x+=icon_width;
    }
    if(!victim.empty())hud.draw.texts.push_back({victim,x,text_y,pixels,advance,line_height,
        notice_name_color(notice->victim_team),100U});
  }
  return true;
}
void identity(game_api::MultiplayerHudState &state,
              const game_api::PlayerDisplayUpdate &update) {
  auto &player = state.players[update.slot - 1U];
  const auto epoch=player.occupant_epoch+
      static_cast<std::uint64_t>(!player.identity_observed || update.new_occupant || player.present!=update.present);
  if (!update.present || update.new_occupant)
    player = {};
  player.identity_observed=true;player.occupant_epoch=epoch;
  if (update.present) {
    player.present = true;
    if(player.appearance != update.appearance) {
      player.appearance=update.appearance;
      ++player.appearance_revision;
    }
    if (player.name != update.name) {
      player.name = update.name;
      ++player.name_revision;
    }
  }
}
std::int16_t short_le(std::span<const std::byte> body, std::size_t offset) {
  const auto value = static_cast<std::uint16_t>(
      std::to_integer<std::uint16_t>(body[offset]) |
      static_cast<std::uint16_t>(
          std::to_integer<std::uint16_t>(body[offset + 1U]) << 8U));
  return std::bit_cast<std::int16_t>(value);
}
std::optional<std::string> exact_string(std::span<const std::byte> body,
                                        std::size_t begin, std::size_t tail) {
  if (body.size() < begin + tail + 1U || body.size() > begin + tail + 32U)
    return {};
  const auto end = body.size() - tail - 1U;
  if (body[end] != std::byte{0} ||
      std::find(body.begin() + static_cast<std::ptrdiff_t>(begin),
                body.begin() + static_cast<std::ptrdiff_t>(end),
                std::byte{0}) !=
          body.begin() + static_cast<std::ptrdiff_t>(end))
    return {};
  return display(
      {reinterpret_cast<const char *>(body.data() + begin), end - begin});
}
} // namespace

std::optional<game_api::GameMessageFailure>
stage_multiplayer(game_api::GameRecordState &record,
                  const game_api::GameRecordInput &input) {
  auto &state = record.multiplayer;
  state.maximum_clients = input.maximum_clients;
  state.local_slot = input.receiving_entity;
  if (!input.previous)
    for (const auto &update : input.initial_players) {
      if (update.slot == 0U || update.slot > state.maximum_clients ||
          update.name != display(update.name))
        return game_api::GameMessageFailure{
            game_api::GameMessageErrorCode::invalid_identifier,
            {},
            {},
            {},
            {},
            0U,
            "Initial display identity violates its bounds",
            {}};
      identity(state, update);
    }
  for (const auto &message : input.messages) {
    const auto name = message.name;
    const bool player =
        message.kind == game_api::GameMessageKind::player_display;
    if (!player &&
        (message.kind != game_api::GameMessageKind::user_message ||
         (name != "ScoreInfo" && name != "DeathMsg" && name != "GameMode" &&
          name != "TeamInfo" && name != "TeamScore")))
      continue;
    const auto reject = [&](game_api::GameMessageErrorCode code,
                            std::string context,
                            std::optional<std::size_t> expected = {}) {
      return game_api::GameMessageFailure{code,
                                          std::string{name},
                                          message.registration_id,
                                          message.source,
                                          expected,
                                          message.body.size(),
                                          std::move(context),
                                          {}};
    };
    const auto body = message.body;
    const auto valid_slot = [&](std::uint32_t slot) {
      return slot > 0U && slot <= state.maximum_clients;
    };
    // Validate even stale messages. Only a complete owning record publishes.
    if (player) {
      if (!message.player_display ||
          !valid_slot(message.player_display->slot) ||
          message.player_display->name != display(message.player_display->name))
        return reject(game_api::GameMessageErrorCode::invalid_identifier,
                      "Player display update is invalid");
    } else if (name == "ScoreInfo") {
      if (body.size() != 9U)
        return reject(game_api::GameMessageErrorCode::invalid_body,
                      "ScoreInfo requires nine exact bytes", 9U);
      if (!valid_slot(std::to_integer<unsigned>(body[0])))
        return reject(game_api::GameMessageErrorCode::invalid_identifier,
                      "ScoreInfo slot is outside negotiated clients");
    } else if (name == "GameMode") {
      if (body.size() != 1U)
        return reject(game_api::GameMessageErrorCode::invalid_body,
                      "GameMode requires one byte", 1U);
      if (std::to_integer<unsigned>(body[0]) > 1U)
        return reject(game_api::GameMessageErrorCode::invalid_value,
                      "Unsupported base Half-Life GameMode");
    } else if (name == "TeamInfo") {
      if (!exact_string(body, 1U, 0U))
        return reject(game_api::GameMessageErrorCode::invalid_body,
                      "TeamInfo requires slot and bounded exact string");
      if (!valid_slot(std::to_integer<unsigned>(body[0])))
        return reject(game_api::GameMessageErrorCode::invalid_identifier,
                      "TeamInfo slot is outside negotiated clients");
    } else if (name == "TeamScore") {
      if (!exact_string(body, 0U, 4U))
        return reject(
            game_api::GameMessageErrorCode::invalid_body,
            "TeamScore requires bounded exact string and two signed shorts");
    } else if (name == "DeathMsg") {
      // Reuse the existing strict decoder; no second DeathMsg grammar.
      const auto event = std::find_if(
          record.life_events.begin(), record.life_events.end(),
          [&](const auto &value) {
            return value.kind == client::RuntimeLifeEventKind::death_notice &&
                   value.source == message.source;
          });
      if (event == record.life_events.end())
        return reject(game_api::GameMessageErrorCode::invalid_body,
                      "DeathMsg has no decoded event");
      if ((!valid_slot(event->killer_entity) && event->killer_entity != 0U) ||
          (!valid_slot(event->victim_entity) && event->victim_entity != 255U))
        return reject(
            game_api::GameMessageErrorCode::invalid_identifier,
            "DeathMsg entity index is outside negotiated players/sentinel");
      for (const unsigned char c : event->weapon_text)
        if (c < 32U || c > 126U)
          return reject(game_api::GameMessageErrorCode::invalid_token,
                        "DeathMsg weapon is not an inert display token");
    }
    if (!newer(message.source, state.high_water))
      continue;
    state.high_water = message.source;
    if (player)
      identity(state, *message.player_display);
    else if (name == "ScoreInfo") {
      auto &entry = state.players[std::to_integer<unsigned>(body[0]) - 1U];
      const auto frags = short_le(body, 1U), deaths = short_le(body, 3U),
                 player_class = short_le(body, 5U),
                 team_number = short_le(body, 7U);
      if (!entry.has_score || entry.frags != frags || entry.deaths != deaths ||
          entry.player_class != player_class ||
          entry.team_number != team_number)
        ++entry.score_revision;
      entry.has_score = true;
      entry.frags = frags;
      entry.deaths = deaths;
      entry.player_class = player_class;
      entry.team_number = team_number;
    } else if (name == "GameMode")
      state.teamplay = body[0] != std::byte{0};
    else if (name == "TeamInfo")
      state.players[std::to_integer<unsigned>(body[0]) - 1U].team =
          team_key(*exact_string(body, 1U, 0U));
    else if (name == "TeamScore") {
      const auto team = team_key(*exact_string(body, 0U, 4U));
      if (team.empty())
        continue;
      auto entry =
          std::find_if(state.teams.begin(), state.teams.end(),
                       [&](const auto &value) { return value.name == team; });
      if (entry == state.teams.end())
        entry =
            std::find_if(state.teams.begin(), state.teams.end(),
                         [](const auto &value) { return value.name.empty(); });
      if (entry == state.teams.end())
        return reject(game_api::GameMessageErrorCode::capacity_exceeded,
                      "Team directory capacity exceeded");
      *entry = {team, short_le(body, body.size() - 4U),
                short_le(body, body.size() - 2U)};
    } else if (name == "DeathMsg") {
      const auto &event = *std::find_if(
          record.life_events.begin(), record.life_events.end(),
          [&](const auto &value) {
            return value.kind == client::RuntimeLifeEventKind::death_notice &&
                   value.source == message.source;
          });
      game_api::DeathNoticeState notice;
      notice.source = event.source;
      notice.killer = event.killer_entity;
      notice.victim = event.victim_entity;
      notice.created_seconds = input.presentation_seconds;
      notice.weapon = event.weapon_text;
      if(valid_slot(event.killer_entity) && state.players[event.killer_entity-1U].has_score)
        notice.killer_team=state.players[event.killer_entity-1U].team_number;
      if(valid_slot(event.victim_entity) && state.players[event.victim_entity-1U].has_score)
        notice.victim_team=state.players[event.victim_entity-1U].team_number;
      notice.kind =
          event.victim_entity == 255U ? game_api::DeathNoticeKind::non_player
          : event.killer_entity == 0U ? game_api::DeathNoticeKind::world
          : event.killer_entity == event.victim_entity
              ? game_api::DeathNoticeKind::suicide
          : event.weapon_text == "teammate"
              ? game_api::DeathNoticeKind::teammate
              : game_api::DeathNoticeKind::normal;
      if (event.killer_entity)
        notice.killer_name = player_name(state, event.killer_entity);
      notice.victim_name = event.victim_entity == 255U
                               ? display(event.weapon_text)
                               : player_name(state, event.victim_entity);
      auto empty =
          std::find(state.notices.begin(), state.notices.end(), std::nullopt);
      if (empty == state.notices.end()) {
        std::move(state.notices.begin() + 1, state.notices.end(),
                  state.notices.begin());
        empty = state.notices.end() - 1;
      }
      *empty = std::move(notice);
    }
  }
  return {};
}

void compose_multiplayer_hud(game_api::MultiplayerHudState &state,
                             game_api::HudState &hud,
                             game_api::HudPresentationContext context,
                             double now,const game_api::HudResourceDescriptor* base_hud,std::uint32_t hud_tier) {
  if (!std::isfinite(now) || now < 0.0 || context.viewport_width < 160U ||
      context.viewport_height < 120U)
    return;
  // Live receipts use the host's existing project presentation origin.
  // Clockless offline fixtures initialize at first sample. No wall clock.
  // A committed receipt can be newer than the frame time cached before the
  // receive pump (or differ by epoch-double rounding). Display it immediately;
  // an earlier render sample is not evidence that the committed event is stale.
  // Keep its original receipt-based deadline, including while unfocused.
  for (auto &notice : state.notices)
    if (notice) {
      if (!notice->created_seconds)
        notice->created_seconds = now;
      if (now >= *notice->created_seconds + 6.0)
        notice.reset();
    }
  std::size_t retained = 0U;
  for (std::size_t i = 0U; i < state.notices.size(); ++i)
    if (state.notices[i]) {
      if (retained != i) {
        state.notices[retained] = std::move(state.notices[i]);
        state.notices[i].reset();
      }
      ++retained;
    }
  const float physical_width = static_cast<float>(context.viewport_width),
              physical_height = static_cast<float>(context.viewport_height);
  const float user_scale=halflife_hud_user_scale(context);
  const std::array<float, 4U> yellow{0.95F, 0.80F, 0.30F, 1.0F};
  const float fallback_pixel=(context.viewport_height>1440U ? 3.0F : 2.0F)*user_scale;
  const float fallback_advance=fallback_pixel*6.0F,fallback_row=fallback_pixel*9.0F;
  const float feed_y = std::max(0.0F,std::min(34.0F*user_scale,
      physical_height - 4.0F * fallback_row - 4.0F*user_scale));
  std::size_t feed_row = 0U;
  const bool sprite_feed=context.hud_draw && compose_sprite_notices(state,hud,context,base_hud,hud_tier);
  for (const auto &notice : state.notices)
    if (context.hud_draw && !sprite_feed && notice) {
      auto text = notice->killer == 0U
                      ? notice->victim_name + " [" + notice->weapon + "] WORLD"
                  : notice->kind == game_api::DeathNoticeKind::suicide
                      ? notice->victim_name + " [" + notice->weapon + "] SELF"
                      : notice->killer_name + " [" + notice->weapon + "] " +
                            notice->victim_name;
      const auto available = static_cast<std::size_t>(std::max(0.0F,
          std::floor(std::min(physical_width-8.0F*user_scale,640.0F*user_scale)/fallback_advance)));
      text=notice_elide(text,available);
      hud.draw.texts.push_back(
          {text,
           std::max(4.0F*user_scale,
                    physical_width - 4.0F*user_scale - static_cast<float>(text.size()) * fallback_advance),
           feed_y + static_cast<float>(feed_row++) * fallback_row, fallback_pixel, fallback_advance, fallback_row,
           yellow});
    }
  if (!context.scoreboard_held)
    return;
  const float width=physical_width/user_scale,height=physical_height/user_scale;
  const auto rectangle_begin=hud.draw.rectangles.size(),text_begin=hud.draw.texts.size();
  struct Row {
    std::string name;
    std::int32_t frags{}, deaths{};
    std::uint32_t slot{};
    bool score{}, team{};
  };
  std::array<Row, 64U> rows{};
  std::size_t row_count = 0U;
  std::array<std::uint32_t, 32U> order{};
  std::iota(order.begin(), order.end(), 0U);
  const auto better = [&](std::uint32_t a, std::uint32_t b) {
    const auto &x = state.players[a];
    const auto &y = state.players[b];
    if (x.frags != y.frags)
      return x.frags > y.frags;
    if (x.deaths != y.deaths)
      return x.deaths < y.deaths;
    return a < b;
  };
  std::sort(order.begin(), order.end(), better);
  struct Team {
    std::string name;
    std::int32_t frags{}, deaths{};
  };
  std::array<Team, 32U> teams{};
  std::size_t team_count = 0U;
  if (state.teamplay) {
    for (const auto slot : order)
      if (slot < state.maximum_clients && state.players[slot].present) {
        const auto &p = state.players[slot];
        const auto name = p.team.empty() ? std::string{"unassigned"} : p.team;
        auto found = std::find_if(
            teams.begin(),
            teams.begin() + static_cast<std::ptrdiff_t>(team_count),
            [&](const auto &t) { return t.name == name; });
        if (found == teams.begin() + static_cast<std::ptrdiff_t>(team_count)) {
          found->name = name;
          ++team_count;
        }
        found->frags += p.frags;
        found->deaths += p.deaths;
      }
    for (auto &team : teams)
      for (const auto &explicit_score : state.teams)
        if (!team.name.empty() && team.name == explicit_score.name) {
          team.frags = explicit_score.frags;
          team.deaths = explicit_score.deaths;
        }
    std::sort(teams.begin(),
              teams.begin() + static_cast<std::ptrdiff_t>(team_count),
              [](const auto &a, const auto &b) {
                if (a.frags != b.frags)
                  return a.frags > b.frags;
                if (a.deaths != b.deaths)
                  return a.deaths < b.deaths;
                return a.name < b.name;
              });
  }
  const auto add_player = [&](std::uint32_t index) {
    const auto &p = state.players[index];
    rows[row_count++] = {player_name(state, index + 1U),
                         p.frags,
                         p.deaths,
                         index + 1U,
                         p.has_score,
                         false};
  };
  if (state.teamplay)
    for (std::size_t t = 0U; t < team_count; ++t) {
      rows[row_count++] = {"TEAM " + teams[t].name,
                           teams[t].frags,
                           teams[t].deaths,
                           0U,
                           true,
                           true};
      for (const auto slot : order)
        if (slot < state.maximum_clients && state.players[slot].present &&
            (state.players[slot].team.empty()
                 ? "unassigned"
                 : state.players[slot].team) == teams[t].name)
          add_player(slot);
    }
  else
    for (const auto slot : order)
      if (slot < state.maximum_clients && state.players[slot].present)
        add_player(slot);
  const float table_width = std::min(760.0F, width - 16.0F);
  const float x = (width - table_width) * 0.5F,
              y = std::min(156.0F, height * 0.22F);
  const float row_height = std::min(
      18.0F, (height - y - 32.0F) /
                 static_cast<float>(std::max<std::size_t>(1U, row_count)));
  const float pixel = std::min(1.5F, row_height / 9.0F), advance = pixel * 6.0F;
  hud.draw.rectangles.push_back(
      {x,
       y,
       table_width,
       28.0F + row_height * static_cast<float>(row_count),
       {0.02F, 0.02F, 0.02F, 0.88F}});
  hud.draw.texts.push_back(
      {"PLAYER", x + 8.0F, y + 6.0F, 1.2F, 7.2F, 14.0F, yellow});
  hud.draw.texts.push_back({"KILLS  DEATHS", x + table_width - 110.0F, y + 6.0F,
                            1.2F, 7.2F, 14.0F, yellow});
  for (std::size_t i = 0U; i < row_count; ++i) {
    const auto &row = rows[i];
    const auto row_y = y + 24.0F + static_cast<float>(i) * row_height;
    if (row.slot && row.slot == state.local_slot)
      hud.draw.rectangles.push_back({x + 3.0F,
                                     row_y - 1.0F,
                                     table_width - 6.0F,
                                     row_height,
                                     {0.18F, 0.36F, 0.12F, 0.95F}});
    auto name = row.name;
    const auto max_name = static_cast<std::size_t>(
        std::max(1.0F, (table_width - 138.0F) / advance));
    if (name.size() > max_name)
      name.resize(max_name);
    hud.draw.texts.push_back(
        {std::move(name), x + 8.0F, row_y, pixel, advance, row_height,
         row.team ? yellow : std::array<float, 4U>{0.84F, 0.92F, 0.80F, 1.0F}});
    const auto frags = row.score ? std::to_string(row.frags) : "?";
    const auto deaths = row.score ? std::to_string(row.deaths) : "?";
    // Team sums can exceed i16. Preserve the value, compact only its glyphs
    // into the existing numeric column, never clamp authoritative counters.
    const float score_pixel =
        std::min(pixel, 48.0F / (6.0F * static_cast<float>(std::max(
                                            frags.size(), deaths.size()))));
    hud.draw.texts.push_back({frags, x + table_width - 104.0F, row_y,
                              score_pixel, score_pixel * 6.0F, row_height,
                              yellow});
    hud.draw.texts.push_back({deaths, x + table_width - 50.0F, row_y,
                              score_pixel, score_pixel * 6.0F, row_height,
                              yellow});
  }
  for(std::size_t i=rectangle_begin;i<hud.draw.rectangles.size();++i) {
    auto& item=hud.draw.rectangles[i];item.x*=user_scale;item.y*=user_scale;
    item.width*=user_scale;item.height*=user_scale;
  }
  for(std::size_t i=text_begin;i<hud.draw.texts.size();++i) {
    auto& item=hud.draw.texts[i];item.x*=user_scale;item.y*=user_scale;
    item.pixel_size*=user_scale;item.advance*=user_scale;item.line_height*=user_scale;
  }
}
} // namespace hlclient::games::halflife
