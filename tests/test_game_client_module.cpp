#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/goldsrc/runtime_replay_fixture.hpp>
#include <hlclient/goldsrc/client_message.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
namespace api = hlclient::game_api;
namespace client = hlclient::client;

struct TestModuleProbe {
    std::size_t resets{}, teardowns{}, commits{}, observations{}, commands{};
    api::GameSessionIdentity identity;
    std::size_t scripted_events{};
    double scripted_seconds{};
};

// Entirely project-owned alternate rules. No concrete game header/factory or
// asset installation is available to this test. It uses the production host.
class TestGameClientModule final : public api::IGameClientModule {
public:
    void enable_remote_player_test_policy() noexcept { remote_enabled_=true; }
    api::RemotePlayerPresentationPolicy remote_player_policy() const noexcept override {
        return remote_enabled_ ? api::RemotePlayerPresentationPolicy{true,0.05,0.2,64.0,8U} :
            api::RemotePlayerPresentationPolicy{};
    }
    api::RemotePlayerPresentationIntent remote_player(
        const api::RemotePlayerPresentationContext& input) noexcept override {
        if(!remote_enabled_) return {};
        api::RemotePlayerPresentationIntent result;
        result.status=api::RemotePlayerPresentationStatus::ready;
        result.sample.sequence=13U;
        result.sample.body=2;
        result.sample.frame_coordinate=input.sample_server_seconds*2.0;
        result.transform_angles={7.0,11.0,13.0};
        return result;
    }
    api::LocalAudioBatch drain_audio() noexcept override { auto result=audio_; audio_.count=0; return result; }
    void committed_scripted_event(const api::CommittedScriptedEvent& event) noexcept override {
        ++probe_->scripted_events;probe_->scripted_seconds=event.received_at_seconds;
        if(event.resolution!=api::ScriptedEventResolution::ready || event.event_index!=231U) return;
        remote_.count=1;
        remote_.effects[0].flash=api::RemoteMuzzleFlash{event.origin,7.0F,{0.0F,0.0F,1.0F,1.0F},
            event.received_at_seconds,event.received_at_seconds+0.1};
    }
    api::RemoteWeaponEffectsBatch drain_remote_effects(double) noexcept override {
        const auto result=remote_;remote_={};return result;
    }
    api::LocalVisualFrame local_visuals(const api::LocalVisualContext&) noexcept override {
        api::LocalVisualFrame result;
        result.flash=visual_;
        visual_.reset();
        return result;
    }
    explicit TestGameClientModule(std::shared_ptr<TestModuleProbe> probe)
        : probe_(std::move(probe)) {
        using namespace hlclient::gameplay_input;
        const std::array bindings{InputBinding::key(
            GameplayInputAction::move_forward, hlclient::input::PhysicalKey::up)};
        auto built = GameplayInputBindingsBuilder{}.build(bindings);
        if (!built) throw std::logic_error{"invalid project test binding"};
        policy_.bindings = std::make_shared<GameplayInputBindings>(std::move(*built.bindings));
        policy_.movement_speeds = {17.0F, 19.0F, 23.0F};
        policy_.speed_key_multiplier = 0.25F;
        policy_.environment_profile = hlclient::goldsrc::movement::
            GoldSrcMovementEnvironmentProfile::movevars_dry_walk_subset_v1;
    }

    void reset(api::GameSessionIdentity identity) noexcept override {
        probe_->identity = identity;
        ++probe_->resets;
        committed_ = {};
        observed_generation_ = observed_revision_ = 0U;
        last_command_.reset();
        presentation_ = {};
        model_.reset();
        audio_={};
        visual_.reset();
        remote_={};
    }
    void teardown() noexcept override {
        ++probe_->teardowns;
        audio_={};
        visual_.reset();
        remote_={};
        committed_ = {};
        model_.reset();
    }
    const api::GameMovementPolicy& movement_policy() const noexcept override { return policy_; }
    api::GameRecordResult stage_record(const api::GameRecordInput& input) const override {
        auto staged = input.previous ? committed_ : api::GameRecordState{};
        for (const auto& message : input.messages) {
            if (message.name != "ProjectSignal") continue; // explicit unknown behavior
            if (message.body.size() < 2U || message.body.size() > 9U)
                return {{}, std::string{"ProjectSignal body is outside the project fixture bound"}};
            staged.weapon_hud.health = std::to_integer<std::uint8_t>(message.body[0]);
            staged.weapon_hud.health_source = message.source;
            client::RuntimeWeaponTypeObservation label;
            label.id = 7U;
            label.source = message.source;
            for (const auto byte : message.body.subspan(1U))
                label.command_name.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
            staged.weapon_hud.catalogue = {std::move(label)};
            ++staged.weapon_hud.revision;
        }
        return {std::move(staged), {}};
    }
    void commit_record(api::GameRecordState&& state) noexcept override {
        committed_ = std::move(state);
        ++probe_->commits;
    }
    void bind_model(std::optional<api::LocalWeaponModelMetadata> model) override {
        model_ = std::move(model);
    }
    void observe(const client::RuntimeClientObservationState& state, double) override {
        if (state.generation == observed_generation_ &&
            state.publication_revision == observed_revision_) return;
        observed_generation_ = state.generation;
        observed_revision_ = state.publication_revision;
        ++probe_->observations;
    }
    void submit(const api::LocalWeaponSubmittedCommand& command, double) override {
        if (last_command_ && last_command_->generation == command.generation &&
            last_command_->sequence == command.sequence) {
            ++presentation_.duplicate_submissions;
            return;
        }
        last_command_ = command;
        ++probe_->commands;
        ++presentation_.actions_started;
        audio_.scope=probe_->identity.map_generation;
        audio_.count=1; audio_.cues[0].serial=probe_->commands;
        audio_.cues[0].scope=audio_.scope; audio_.cues[0].scheduled_seconds=command.sample_end_seconds;
        constexpr std::string_view name="weapons/project_alt.wav";
        std::copy(name.begin(),name.end(),audio_.cues[0].reference.sample.begin());
        presentation_.visual = api::LocalWeaponVisual{
            13U, 2U, 10'000U + command.sequence, command.sample_end_seconds};
        visual_=api::LocalMuzzleFlash{{command.generation,7U,27U,1U,command.sequence,
            api::LocalWeaponAction::primary_fire,command.sample_end_seconds,0U},27U,
            13U,0U,0U,0U,1.0F,{0,1,1,1},command.sample_end_seconds,
            command.sample_end_seconds+0.1};
    }
    void cancel_uncommitted() noexcept override { presentation_.visual.reset(); audio_.count=0; ++audio_.scope; visual_.reset(); }
    api::LocalWeaponPresentationSnapshot sample(double time) noexcept override {
        presentation_.frame_coordinate = time * 2.0;
        return presentation_;
    }
    api::ViewmodelIntent viewmodel(const client::RuntimeClientObservationState& state,
        std::optional<api::LocalWeaponModelMetadata> model, double time,
        std::optional<api::LocalWeaponVisual>) override {
        if (!model) return {};
        return {api::ViewmodelStatus::ready, state.generation, state.publication_revision,
                model->model_index, 13U, 2U, time * 2.0, time};
    }
    api::HudState hud(const client::RuntimeClientObservationState&, double) override {
        api::HudState result;
        if (committed_.weapon_hud.health) result.health = *committed_.weapon_hud.health;
        if (!committed_.weapon_hud.catalogue.empty())
            result.weapon_name = committed_.weapon_hud.catalogue.front().command_name;
        result.draw.texts.push_back({"ALT " + result.weapon_name, 7.0F, 9.0F,
            1.0F, 6.0F, 8.0F, {0.0F, 1.0F, 1.0F, 1.0F}});
        return result;
    }
    api::CameraIntent camera(const client::RuntimeClientObservationState&,
        double) const noexcept override {
        api::CameraIntent result;
        result.allow_predicted_translation = false;
        result.pitch_offset_degrees = 7.0;
        result.yaw_offset_degrees = 11.0;
        return result;
    }
    std::optional<api::GameCommandRequest> inventory_selection(
        const client::RuntimeClientObservationState&, std::uint8_t) const override { return {}; }
    std::optional<std::uint8_t> select_group(const client::RuntimeClientObservationState&,
        std::uint8_t, std::optional<std::uint8_t>) const override { return {}; }
    std::optional<std::uint8_t> cycle_inventory(const client::RuntimeClientObservationState&,
        std::optional<std::uint8_t>, int) const override { return {}; }
    api::GameCommandRequest self_kill_request() const override { return {}; }
    std::optional<std::uint8_t> scenario_inventory_target(
        const client::RuntimeClientObservationState&, std::size_t) const override { return {}; }
    api::GameScenarioDirective scenario_command(api::GameScenario,
        const client::RuntimeClientObservationState*, std::size_t, double, double) noexcept override {
        return {};
    }
    api::DamageRespawnScriptSnapshot damage_respawn_snapshot() const noexcept override { return {}; }
    void observe_action_evidence(const client::RuntimeClientObservationState&,
        const std::optional<api::GameActionTraffic>&) noexcept override {}
    api::GameActionEvidenceSnapshot action_evidence() const noexcept override { return {}; }

private:
    bool remote_enabled_{};
    api::LocalAudioBatch audio_;
    api::RemoteWeaponEffectsBatch remote_;
    std::optional<api::LocalMuzzleFlash> visual_;
    std::shared_ptr<TestModuleProbe> probe_;
    api::GameMovementPolicy policy_;
    api::GameRecordState committed_;
    std::uint64_t observed_generation_{}, observed_revision_{};
    std::optional<api::LocalWeaponSubmittedCommand> last_command_;
    api::LocalWeaponPresentationSnapshot presentation_;
    std::optional<api::LocalWeaponModelMetadata> model_;
};

client::RuntimeClientObservationState test_observation() {
    client::RuntimeClientObservationState state;
    state.generation = 3U;
    state.publication_revision = 7U;
    return state;
}
client::RuntimeObservationSource test_source() {
    return {11U, 1U, 42U, 0U, 64U};
}
} // namespace

TEST_CASE("Independent remote player presentation crosses the same host without Half-Life fallback",
          "[e10][game-module][game-api][core-only]") {
    const auto probe=std::make_shared<TestModuleProbe>();
    auto alternate=std::make_unique<TestGameClientModule>(probe);
    alternate->enable_remote_player_test_policy();
    api::GameClientHost host{std::move(alternate)};
    host.reset({3U,5U,2U});
    hlclient::assets::SkeletalModelAssetData empty_project_model;
    client::RuntimePacketEntityObservation independent_entity;
    independent_entity.entity_number=3U;
    api::RemotePlayerPresentationContext input{3U,5U,7U,27U,{1U,2U},
        empty_project_model,independent_entity,independent_entity};
    input.sample_server_seconds=2.0;
    const auto result=host.remote_player(input);
    REQUIRE(result.status==api::RemotePlayerPresentationStatus::ready);
    CHECK(result.sample.sequence==13U);
    CHECK(result.sample.body==2);
    CHECK(result.sample.frame_coordinate==4.0);
    CHECK(result.transform_angles[0]==7.0);
    CHECK(result.transform_angles[1]==11.0);
    CHECK(result.transform_angles[2]==13.0);
    CHECK(host.remote_player_policy().interpolation_delay_seconds==0.05);
    CHECK_FALSE(result.gait_sample);
    CHECK(host.drain_audio().count==0U);
    host.reset({4U,6U,2U});
    CHECK(probe->resets==2U);
    host.teardown();
    CHECK(host.remote_player(input).status==api::RemotePlayerPresentationStatus::silent);
    CHECK_FALSE(host.remote_player_policy().enabled);
    CHECK(probe->teardowns==1U);
}

TEST_CASE("GameClientHost accepts an independent module with owning transactional output",
          "[game-module][game-api][core-only]") {
    const auto probe = std::make_shared<TestModuleProbe>();
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};
    host.reset({3U, 5U, 2U});
    CHECK(host.movement_sound_preparation().empty()); // No implicit HL resource fallback.
    auto observation = test_observation();
    std::vector<std::byte> bytes{std::byte{5}, std::byte{'s'}, std::byte{'p'},
                               std::byte{'a'}, std::byte{'r'}, std::byte{'k'}};
    std::string name{"ProjectSignal"};
    const std::array messages{api::GameMessageView{
        api::GameMessageKind::user_message, name, bytes, test_source()}};
    auto staged = host.stage_record({nullptr, observation, messages, 2U});
    REQUIRE(staged);
    CHECK(probe->commits == 0U);
    CHECK_FALSE(host.hud(observation, 0.0).health);
    // The owning staged object, then the module, survive the borrowed RX data.
    name.assign("overwritten");
    std::fill(bytes.begin(), bytes.end(), std::byte{0});
    REQUIRE(staged.state->weapon_hud.catalogue.size() == 1U);
    CHECK(staged.state->weapon_hud.catalogue[0].command_name == "spark");
    host.commit_record(std::move(*staged.state));
    CHECK(probe->commits == 1U);
    const auto hud = host.hud(observation, 0.0);
    CHECK(hud.health == 5);
    REQUIRE(hud.draw.texts.size() == 1U);
    CHECK(hud.draw.texts[0].text == "ALT spark");
    CHECK(hud.draw.texts[0].x == 7.0F);
    CHECK(hud.draw.texts[0].y == 9.0F);

    const std::array valid_prefix{std::byte{9}, std::byte{'x'}};
    const std::array invalid_suffix{std::byte{77}};
    const std::array malformed{
        api::GameMessageView{api::GameMessageKind::user_message, "ProjectSignal", valid_prefix, test_source()},
        api::GameMessageView{api::GameMessageKind::user_message, "ProjectSignal", invalid_suffix, test_source()}};
    const auto rejected = host.stage_record({&observation, observation, malformed, 2U});
    CHECK_FALSE(rejected);
    CHECK(probe->commits == 1U);
    CHECK(host.hud(observation, 0.1).health == 5);
    CHECK(host.hud(observation, 0.1).weapon_name == "spark");

    REQUIRE(api::valid_game_movement_policy(host.movement_policy()));
    CHECK_FALSE(host.movement_policy().reference_ladder.has_value());
    CHECK(host.movement_policy().movement_speeds.forward_speed == 17.0F);
    CHECK(host.movement_policy().movement_speeds.backward_speed == 19.0F);
    CHECK(host.movement_policy().movement_speeds.side_speed == 23.0F);
    CHECK(host.movement_policy().speed_key_multiplier == 0.25F);
    CHECK(host.movement_policy().bindings->actions_for_key(hlclient::input::PhysicalKey::w) == 0U);
    CHECK(host.movement_policy().bindings->actions_for_key(hlclient::input::PhysicalKey::up) != 0U);
}

TEST_CASE("GameClientHost routes unique command presentation and lifecycle to the selected module",
          "[game-module][game-api][core-only]") {
    const auto probe = std::make_shared<TestModuleProbe>();
    {
        api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};
        host.reset({3U, 5U, 2U});
        auto observation = test_observation();
        host.observe(observation, 1.0);
        host.observe(observation, 1.0); // render/replay cannot create a new game event
        CHECK(probe->observations == 1U);
        const api::LocalWeaponSubmittedCommand command{3U, 21U, 4U, 1.0};
        host.submit(command, 1.0);
        host.submit(command, 1.0);
        CHECK(probe->commands == 1U);
        const auto audio=host.drain_audio(); REQUIRE(audio.count==1);
        CHECK(audio.cues[0].reference.name()=="weapons/project_alt.wav");
        CHECK(host.drain_audio().count==0);
        const auto alternate_visual=host.local_visuals({});
        REQUIRE(alternate_visual.flash);
        CHECK(alternate_visual.flash->color[1]==1.0F);
        CHECK_FALSE(host.local_visuals({}).flash);
        const auto sample = host.sample(1.25);
        REQUIRE(sample.visual);
        CHECK(sample.visual->sequence == 13U);
        CHECK(sample.visual->body == 2U);
        CHECK(sample.visual->restart_identity == 10021U);
        CHECK(sample.frame_coordinate == 2.5);
        CHECK(sample.actions_started == 1U);
        CHECK(sample.duplicate_submissions == 1U);
        host.observe(observation, 1.25);
        CHECK(host.sample(1.25).actions_started == 1U);
        api::LocalWeaponModelMetadata model;
        model.generation = 3U;
        model.model_index = 27U;
        host.bind_model(model);
        const auto pose = host.viewmodel(observation, model, 1.25);
        CHECK(pose.status == api::ViewmodelStatus::ready);
        CHECK(pose.model_index == 27U);
        CHECK(pose.sequence == 13U);
        CHECK(pose.frame_coordinate == 2.5);
        const auto camera = host.camera(observation, 0.0);
        CHECK(camera.status == api::CameraIntentStatus::ready);
        CHECK_FALSE(camera.allow_predicted_translation);
        CHECK(camera.pitch_offset_degrees == 7.0);
        CHECK(camera.yaw_offset_degrees == 11.0);
        host.cancel_uncommitted();
        CHECK_FALSE(host.sample(1.25).visual);
        host.reset({3U, 6U, 2U}); // map identity changes without a network reconnect
        CHECK(probe->resets == 2U);
        CHECK(probe->identity.network_generation == 3U);
        CHECK(probe->identity.map_generation == 6U);
        CHECK_FALSE(host.hud(observation, 2.0).health);
        CHECK(host.sample(2.0).actions_started == 0U);
        host.teardown();
        host.teardown();
        CHECK(probe->teardowns == 1U);
        CHECK_FALSE(host.active());
        CHECK_FALSE(host.stage_record({nullptr, observation, {}, 2U}));
        host.submit(command, 3.0);
        host.observe(observation, 3.0);
        CHECK(probe->commands == 1U);
        CHECK(probe->observations == 1U);
        CHECK(host.hud(observation, 3.0).draw.texts.empty());
    }
    CHECK(probe->teardowns == 1U); // destructor honors explicit teardown
}

TEST_CASE("GameClientHost rejects an absent module without choosing a fallback",
          "[game-module][game-api][core-only]") {
    CHECK_THROWS_AS(api::GameClientHost{nullptr}, std::invalid_argument);
}

TEST_CASE("Core alternate module receives committed events through the same clock and reset seam",
          "[remote-effects][game-module][game-api][core-only]") {
    const auto probe=std::make_shared<TestModuleProbe>();
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};
    host.set_movement_audio_time_origin(400.0);
    host.reset({1,1,1});
    api::CommittedScriptedEvent event;
    event.generation=1;event.record=1;event.emitter_entity=3;event.event_index=231;
    event.origin={7,8,9};event.received_at_seconds=401.25;
    host.committed_scripted_event(event);
    CHECK(probe->scripted_events==1);CHECK(probe->scripted_seconds==1.25);
    const auto first=host.drain_remote_effects(1.25);REQUIRE(first.count==1);
    REQUIRE(first.effects[0].flash);CHECK(first.effects[0].flash->color[2]==1.0F);
    CHECK(first.effects[0].flash->color[0]==0.0F);CHECK(first.effects[0].flash->radius_units==7.0F);
    CHECK(first.effects[0].flash->origin.z==9.0F);
    CHECK_FALSE(first.effects[0].shell);CHECK_FALSE(first.effects[0].impact);
    CHECK(host.drain_remote_effects(1.25).count==0);
    host.reset({2,2,1});event.generation=2;event.received_at_seconds=402;
    host.committed_scripted_event(event);CHECK(probe->scripted_seconds==2);
    CHECK(host.drain_remote_effects(2).count==1);
    host.teardown();host.committed_scripted_event(event);
    CHECK(probe->scripted_events==2);CHECK(host.drain_remote_effects(2).count==0);
}

TEST_CASE("Typed game command encoder permits bounded inventory and self-kill wire grammar only",
          "[game-module][game-api][core-only]") {
    const auto inventory = hlclient::goldsrc::encode_game_command(
        {api::GameCommandKind::inventory_selection, "weapon_crowbar"});
    REQUIRE(inventory);
    REQUIRE(inventory.bytes);
    std::vector<std::byte> expected{std::byte{3}};
    for (const char character : std::string_view{"weapon_crowbar"})
        expected.push_back(std::byte{static_cast<unsigned char>(character)});
    expected.push_back(std::byte{0});
    CHECK(*inventory.bytes == expected);
    const auto self_kill = hlclient::goldsrc::encode_game_command(
        {api::GameCommandKind::self_kill, "kill"});
    REQUIRE(self_kill);
    CHECK(*self_kill.bytes == std::vector<std::byte>{
        std::byte{3},std::byte{'k'},std::byte{'i'},std::byte{'l'},std::byte{'l'},std::byte{0}});
    for (const auto& token : std::vector<std::string>{
            "quit", "disconnect", "weapon_", "weapon_crowbar\n",
            "weapon_crowbar;quit", std::string{"weapon_crowbar\0quit",19U},
            "weapon_" + std::string(57U,'x')}) {
        CAPTURE(token);
        CHECK_FALSE(hlclient::goldsrc::encode_game_command(
            {api::GameCommandKind::inventory_selection, token}));
    }
    CHECK_FALSE(hlclient::goldsrc::encode_game_command(
        {api::GameCommandKind::self_kill, "weapon_crowbar"}));
    CHECK_FALSE(hlclient::goldsrc::encode_game_command(
        {static_cast<api::GameCommandKind>(255U), "weapon_crowbar"}));
}

TEST_CASE("Production replay commits an alternate module atomically and resets generation without fallback",
          "[game-module][game-api][core-only][runtime-replay]") {
    namespace goldsrc = hlclient::goldsrc;
    const auto probe = std::make_shared<TestModuleProbe>();
    auto host = std::make_shared<api::GameClientHost>(
        std::make_unique<TestGameClientModule>(probe));
    auto built = goldsrc::make_runtime_replay_fixture({});
    REQUIRE(built);
    auto init = built.fixture->initialization;
    init.game_client = host;
    init.user_message_definitions = {{99U, -1, "ProjectSignal"}};
    client::ClientWorldState world;
    auto session = goldsrc::RuntimeReplaySession::initialize(init, world);
    REQUIRE(session);
    CHECK(probe->resets == 1U);
    CHECK(probe->commits == 1U);
    auto record = built.fixture->records.front();
    record.payload.bytes = {std::byte{99},std::byte{6},std::byte{5},
        std::byte{'s'},std::byte{'p'},std::byte{'a'},std::byte{'r'},std::byte{'k'}};
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(0U,0U,record.payload.bytes.size());
    REQUIRE(cursor);
    record.initial_cursor = *cursor;
    REQUIRE(session.session->apply_record(record));
    const auto committed = world.runtime_observation();
    CHECK(committed->weapon_hud.health == 5U);
    REQUIRE(committed->weapon_hud.catalogue.size() == 1U);
    CHECK(committed->weapon_hud.catalogue[0].command_name == "spark");
    CHECK(host->hud(*committed,0.0).draw.texts[0].text == "ALT spark");
    CHECK(probe->commits == 2U);
    CHECK_FALSE(session.session->apply_record(record));
    CHECK(probe->commits == 2U);
    CHECK(world.runtime_observation() == committed);
    auto malformed = record;
    ++malformed.record_identity;
    ++malformed.record_ordinal;
    ++malformed.payload.source_sequence;
    malformed.payload.bytes = {std::byte{99},std::byte{2},std::byte{8},std::byte{'x'},
        std::byte{99},std::byte{1},std::byte{77}};
    const auto bad_cursor = goldsrc::StockRuntimeSourceCursor::create(
        0U,0U,malformed.payload.bytes.size());
    REQUIRE(bad_cursor);
    malformed.initial_cursor = *bad_cursor;
    CHECK_FALSE(session.session->apply_record(malformed));
    CHECK(probe->commits == 2U);
    CHECK(world.runtime_observation() == committed);
    CHECK(host->hud(*committed,0.0).health == 5);
    auto next = goldsrc::make_runtime_replay_fixture(
        {goldsrc::RuntimeReplayFixtureKind::basic_mixed,2U,200U,2000U});
    REQUIRE(next);
    next.fixture->initialization.game_client = host;
    CHECK_FALSE(session.session->reset_generation(next.fixture->initialization));
    CHECK(probe->resets == 2U);
    CHECK(probe->commits == 3U);
    CHECK(world.runtime_observation()->generation == 2U);
    CHECK_FALSE(world.runtime_observation()->weapon_hud.health);
    CHECK_FALSE(host->hud(*world.runtime_observation(),1.0).health);
    CHECK_FALSE(session.session->apply_record(record));
    CHECK(probe->commits == 3U);
    session.session->finish();
    host->teardown();
    CHECK(probe->teardowns == 1U);
}
