#include "user_info_test_fixture.hpp"
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/goldsrc/runtime_replay_fixture.hpp>
#include <hlclient/goldsrc/server_audio.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <hlclient/game_api/remote_player.hpp>
#include <string_view>
#include <vector>

namespace {
namespace gs = hlclient::goldsrc;
namespace api = hlclient::game_api;
namespace client = hlclient::client;
namespace info = hlclient::test::user_info_fixture;
using Bytes = std::vector<std::byte>;
void append(Bytes& target, const Bytes& source) { target.insert(target.end(), source.begin(), source.end()); }
Bytes say_text(std::string_view text = "- Candidate has left the game\n") {
    Bytes body{std::byte{2U}};
    for (const unsigned char c : text) body.push_back(static_cast<std::byte>(c));
    body.push_back(std::byte{0U});
    return body;
}
Bytes registered(std::uint8_t id, const Bytes& body) {
    REQUIRE(body.size() <= 255U);
    Bytes wire{static_cast<std::byte>(id), static_cast<std::byte>(body.size())};
    append(wire, body);
    return wire;
}
Bytes score(unsigned slot, unsigned frags) {
    return {std::byte{201U}, static_cast<std::byte>(slot), static_cast<std::byte>(frags),
        std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U}};
}
struct PeerReplay {
    std::shared_ptr<api::GameClientHost> host = std::make_shared<api::GameClientHost>(
        hlclient::games::halflife::make_half_life_client_module());
    std::shared_ptr<gs::CommittedSoundQueue> queue = std::make_shared<gs::CommittedSoundQueue>();
    client::ClientWorldState world;
    std::unique_ptr<gs::RuntimeReplaySession> session;
    std::size_t ordinal{};
    PeerReplay() {
        auto fixture = gs::make_runtime_replay_fixture({}); REQUIRE(fixture);
        auto init = fixture.fixture->initialization;
        init.max_clients = 2U; init.receiving_player_entity = 1U;
        init.game_client = host; init.sound_events = queue;
        init.user_message_definitions = {{201U, 9, "ScoreInfo"}, {202U, -1, "SayText"},
            {203U, 1, "Health"}, {204U, 2, "AmmoX"}, {205U, 3, "CurWeapon"}};
        auto initialized = gs::RuntimeReplaySession::initialize(std::move(init), world); REQUIRE(initialized);
        session = std::move(initialized.session);
    }
    gs::RuntimeReplayRecord record(Bytes wire) {
        ++ordinal;
        gs::OwnedServicePayload payload;
        payload.bytes = std::move(wire); payload.source_sequence = static_cast<std::uint32_t>(100U + ordinal);
        payload.source_acknowledgement = payload.source_sequence - 1U;
        payload.source_reliable = true; payload.decompressed = true;
        payload.direction = gs::NetchanDirection::server_to_client;
        const auto cursor = gs::StockRuntimeSourceCursor::create(0U, 0U, payload.bytes.size()); REQUIRE(cursor);
        gs::RuntimeReplayRecord result{1U, 9000U + ordinal, ordinal, std::move(payload), *cursor};
        result.received_at = std::chrono::steady_clock::time_point{} + std::chrono::seconds{ordinal};
        return result;
    }
    gs::RuntimeReplayApplyResult apply(Bytes wire) { return session->apply_record(record(std::move(wire))); }
    api::MultiplayerHudState directory() const {
        const auto& observation = *world.runtime_observation();
        auto staged = host->stage_record({&observation, observation, {}, 1U, 2U}); REQUIRE(staged);
        return staged.state->multiplayer;
    }
    void seed() {
        Bytes wire = info::make_message(0U, 11U, "\\name\\Observer");
        append(wire, info::make_message(1U, 12U, "\\name\\Candidate"));
        append(wire, score(1U, 5U)); append(wire, score(2U, 3U));
        wire.insert(wire.end(), {std::byte{203U}, std::byte{100U}, std::byte{204U}, std::byte{1U}, std::byte{36U},
            std::byte{205U}, std::byte{1U}, std::byte{2U}, std::byte{17U}});
        REQUIRE(apply(std::move(wire)));
        gs::CommittedSound discarded; while (queue->pop(discarded)) {}
    }
};
}

TEST_CASE("E28 independently authored departure commits and A continues without B",
          "[e28][peer-disconnect][runtime-replay][game-module]") {
    PeerReplay f; f.seed();
    const auto before = f.world.runtime_observation();
    // Exactly 54 public svc_nop bytes reproduce the known message-offset shape.
    // These bytes are NOT the unavailable historical record 2381 payload.
    Bytes wire(54U, std::byte{1U});
    append(wire, info::make_message(1U, 12U, ""));
    append(wire, registered(202U, say_text()));
    wire.insert(wire.end(), {std::byte{203U}, std::byte{98U}, std::byte{204U}, std::byte{1U}, std::byte{40U}});
    auto departure = f.record(std::move(wire));
    const auto accepted = f.session->apply_record(departure); REQUIRE(accepted);
    const auto after = f.world.runtime_observation();
    CHECK(after->generation == before->generation);
    CHECK(after->lifecycle.life_epoch == before->lifecycle.life_epoch);
    CHECK(after->weapon_hud.health == 98U);
    CHECK(after->weapon_hud.reserve_ammo[1] == 40U);
    CHECK(after->weapon_hud.active_weapon_id == 2U);
    CHECK(after->weapon_hud.clips[2] == 17);
    const auto players = f.directory();
    CHECK(players.players[0].present); CHECK(players.players[0].name == "Observer");
    CHECK(players.players[0].frags == 5); CHECK_FALSE(players.players[1].present);
    CHECK(players.players[1].name.empty()); CHECK_FALSE(players.players[1].has_score);
    gs::CommittedSound marker; REQUIRE(f.queue->pop(marker));
    CHECK(marker.opcode == gs::RuntimeControlOpcode::svc_updateuserinfo);
    CHECK(marker.sound.entity_reference == 2U); CHECK_FALSE(f.queue->pop(marker));
    CHECK_FALSE(f.session->apply_record(departure));
    CHECK(f.world.runtime_observation() == after); CHECK_FALSE(f.queue->pop(marker));
    REQUIRE(f.apply({std::byte{203U}, std::byte{97U}}));
    CHECK(f.world.runtime_observation()->weapon_hud.health == 97U);
    CHECK(f.directory().players[0].frags == 5);
    REQUIRE(f.apply(info::make_message(1U, 13U, "\\name\\Replacement")));
    const auto reused = f.directory(); CHECK(reused.players[1].present);
    CHECK(reused.players[1].name == "Replacement"); CHECK_FALSE(reused.players[1].has_score);
    CHECK(reused.players[0].frags == 5);
}

TEST_CASE("E28 absent directory slot cannot reappear from a fresh retained entity publication",
          "[e28][peer-disconnect][lifecycle][game-module]") {
    PeerReplay f;f.seed();
    client::RuntimePacketEntityObservation entity;entity.entity_number=2;entity.model_index=7;
    entity.ordinary_visual_schema=true;entity.player_movement_schema=true;entity.weapon_model_index=71;
    const std::array list{entity};
    CHECK(f.host->world_entity({entity,nullptr,list,2}).role==api::WorldEntityPresentationRole::ordinary);
    REQUIRE(f.apply(info::make_message(1U,12U,"")));
    f.host->remote_entities(list,2,100); // Not the publication at the removal floor.
    CHECK(f.host->world_entity({entity,nullptr,list,2}).role==api::WorldEntityPresentationRole::unavailable);
    CHECK_FALSE(f.directory().players[1].present);
    CHECK(f.directory().players[1].identity_observed);
    REQUIRE(f.apply(info::make_message(1U,13U,"\\name\\Replacement")));
    CHECK(f.host->world_entity({entity,nullptr,list,2}).role==api::WorldEntityPresentationRole::ordinary);
    CHECK(f.directory().players[1].occupant_epoch>=3);
}

TEST_CASE("E28 failure summary exports typed userinfo shape and never its private text",
          "[e28][peer-disconnect][privacy][diagnostics]") {
    PeerReplay f;f.seed();
    Bytes wire(54,std::byte{1});append(wire,info::make_message(1,12,"x"));
    const auto rejected=f.apply(wire);REQUIRE_FALSE(rejected);REQUIRE(rejected.error);
    const auto summary=gs::runtime_failure_summary(*rejected.error);
    CHECK(summary.find("user_info_error=invalid_info_string")!=summary.npos);
    CHECK(summary.find("user_info_string_error=missing_leading_separator")!=summary.npos);
    CHECK(summary.find("user_info_string_length=1")!=summary.npos);
    CHECK(summary.find("user_info_empty=0")!=summary.npos);
    CHECK(summary.find("user_info_private_id_present=1")!=summary.npos);
    CHECK(summary.find("message_start_byte=54")!=summary.npos);
    CHECK(summary.find("failure_byte=60")!=summary.npos);
    CHECK(summary.find("Candidate")==summary.npos);CHECK(summary.find("Observer")==summary.npos);
}

TEST_CASE("E28 actual userinfo failure producer supplies private-safe native roundtrip evidence",
          "[e28][peer-disconnect][privacy][runtime-diagnostics]") {
    PeerReplay f; f.seed();
    REQUIRE(f.apply({std::byte{203U},std::byte{99U}}));
    const auto before=f.world.runtime_observation();
    Bytes wire(54U,std::byte{1U});
    append(wire,info::make_message(1U,12U,"private_fixture"));
    const auto rejected=f.apply(wire);REQUIRE_FALSE(rejected);REQUIRE(rejected.error);
    const auto& error=*rejected.error;
    CHECK(f.world.runtime_observation()==before);
    CHECK(error.last_publication==3U);CHECK(error.committed_records==2U);
    const auto summary=gs::runtime_failure_summary(error);
    CHECK(summary.find("private_fixture")==std::string::npos);
    CHECK(summary.find("Observer")==std::string::npos);
    CHECK(summary.find("Candidate")==std::string::npos);
    CHECK(summary.find("user_info_string_length=15")!=std::string::npos);
    // Actual production dispatcher/error/formatter result, supplied to the
    // existing fake-child native JSON/stdout restoration roundtrip. No copy of
    // raw userinfo or a handwritten replacement for the typed values.
    std::cout << "live_application_outcome result=error primary_error=runtime_record_failed"
        << " runtime_error=" << gs::to_string(error.code)
        << " parser_error=" << (error.decoder_error ? gs::to_string(*error.decoder_error) : "unavailable")
        << " opcode=" << (error.decoder_wire_opcode ? std::to_string(*error.decoder_wire_opcode) : "unavailable")
        << " cursor=" << (error.decoder_cursor ? std::to_string(error.decoder_cursor->absolute_bit_offset()) : "unavailable")
        << " scripted_coverage=not_evaluated prediction_coverage=limited inventory_notifications=0 feedback_rows=0"
        << summary << '\n';
}

TEST_CASE("E28 departure followed by malformed framing cannot partially clear a peer",
          "[e28][peer-disconnect][runtime-replay][transaction]") {
    PeerReplay f; f.seed(); const auto before = f.world.runtime_observation();
    Bytes wire = info::make_message(1U, 12U, ""); wire.push_back(std::byte{255U});
    const auto rejected = f.apply(wire); REQUIRE_FALSE(rejected); REQUIRE(rejected.error);
    CHECK(rejected.error->decoder_wire_opcode == 255U);
    CHECK(f.world.runtime_observation() == before);
    CHECK(f.directory().players[1].present); CHECK(f.directory().players[1].frags == 3);
    gs::CommittedSound marker; CHECK_FALSE(f.queue->pop(marker));
}

TEST_CASE("E28 registered SayText is bounded inert byte plus exact terminated text",
          "[e28][peer-disconnect][saytext][runtime-replay][game-module]") {
    PeerReplay f; f.seed();
    // Source shape: pinned SDK ClientDisconnect writes entity byte then string.
    // Text is never executed, formatted, rendered or exported by this parser.
    REQUIRE(f.apply(registered(202U, say_text("%s%n;quit\n"))));
    auto server_text = say_text(""); server_text[0] = std::byte{0U};
    REQUIRE(f.apply(registered(202U, server_text)));
    REQUIRE(f.apply(registered(202U, say_text(std::string(253U, 'x')))));
    CHECK(f.directory().players[0].frags == 5);
    const auto before = f.world.runtime_observation();
    for (auto body : std::array{Bytes{}, Bytes{std::byte{2U}}, Bytes{std::byte{2U},std::byte{'x'}},
            Bytes{std::byte{2U},std::byte{'x'},std::byte{0U},std::byte{'y'},std::byte{0U}}}) {
        const auto rejected = f.apply(registered(202U, body)); REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error); REQUIRE(rejected.error->module_error);
        CHECK(rejected.error->module_error->message_name == "SayText");
        CHECK(rejected.error->module_error->code == api::GameMessageErrorCode::invalid_body);
        CHECK(f.world.runtime_observation() == before);
    }
    auto malformed = say_text(); malformed[0] = std::byte{3U}; // negotiated max is two, not always32
    const auto invalid_slot = f.apply(registered(202U, malformed)); REQUIRE_FALSE(invalid_slot);
    REQUIRE(invalid_slot.error->module_error);
    CHECK(invalid_slot.error->module_error->code == api::GameMessageErrorCode::invalid_identifier);
    CHECK(f.world.runtime_observation() == before);
}
