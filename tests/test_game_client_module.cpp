#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/goldsrc/runtime_replay_fixture.hpp>
#include <hlclient/goldsrc/client_message.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
namespace api = hlclient::game_api;
namespace client = hlclient::client;

struct TestModuleProbe {
    api::ScreenEffectsFrame screen_effects;
    double mouse_sensitivity_scale{1.0};
    bool persistent_beams{};
    bool sprite_hud_test{};
    bool surface_decal_test{};
    std::size_t hud_bindings{}, hud_menu_inputs{}, hud_handoffs{};
    api::HudResourceSet hud_resources;
    api::HudMenuInput last_hud_input;
    std::uint8_t hud_handoff_weapon{};
    bool hud_handoff_queued{};
    std::size_t resets{}, teardowns{}, commits{}, observations{}, commands{};
    api::GameSessionIdentity identity;
    std::size_t scripted_events{};
    double scripted_seconds{};
    std::size_t socket_samples{};
    api::RemoteWeaponPoseSample last_socket;
    std::size_t entity_observations{};
    std::size_t temporary_effects{};
    double temporary_seconds{};
    client::CommittedTemporaryEffect temporary_observation;
    std::vector<api::LocalWeaponModelMetadata> model_variants;
    std::size_t variant_bindings{};
    std::size_t sound_observations{},effect_bindings{};
    api::CommittedSoundObservation sound_observation;
    std::size_t palette_bindings{},effect_policies{};
    api::EffectPaletteMetadata palette;
    api::EffectPresentationPolicy effect_policy;
};

// Entirely project-owned alternate rules. No concrete game header/factory or
// asset installation is available to this test. It uses the production host.
class TestGameClientModule final : public api::IGameClientModule {
public:
    api::ScreenEffectsFrame screen_effects(double) const noexcept override {return probe_->screen_effects;}
    std::span<const api::DecalAssetRequest> surface_decal_preparation() const noexcept override {
        if(!probe_->surface_decal_test)return {};
        static constexpr auto requests=[] {
            std::array<api::DecalAssetRequest,1> result{};
            constexpr std::string_view archive="owned_marks.wad",image="own_mark";
            for(std::size_t i=0;i<archive.size();++i)result[0].wad_name[i]=archive[i];
            for(std::size_t i=0;i<image.size();++i)result[0].texture_name[i]=image[i];
            return result;
        }();
        return requests;
    }
    std::string_view effect_palette_preparation() const noexcept override {return "gfx/project_owned.lmp";}
    std::string_view temporary_effect_source_label(api::TemporaryEffectIdentity::Source) const noexcept override {return "project_owned_effect";}
    void bind_effect_palette(const api::EffectPaletteMetadata& palette) noexcept override {
        ++probe_->palette_bindings;probe_->palette=palette;
    }
    void configure_effect_policy(const api::EffectPresentationPolicy& policy) noexcept override {
        ++probe_->effect_policies;probe_->effect_policy=policy;
    }
    void bind_effect_resource(const api::EffectResourceBinding&) noexcept override {++probe_->effect_bindings;}
    void committed_sound(const api::CommittedSoundObservation& event) noexcept override {
        if(event.generation!=probe_->identity.network_generation)return;
        ++probe_->sound_observations;probe_->sound_observation=event;
    }
    std::optional<hlclient::assets::AssetVector3> command_movement_effect(const api::LocalWeaponSubmittedCommand& c) noexcept override {
        if(c.generation!=probe_->identity.network_generation)return {};return hlclient::assets::AssetVector3{7,11,13};
    }
    api::BeamEffectsFrame beam_effects(const api::IStaticEffectTrace&,double now,std::optional<hlclient::assets::AssetVector3>) noexcept override {
        api::BeamEffectsFrame out;if(!probe_->identity.network_generation)return out;out.count=1;out.sprites[0].action.generation=probe_->identity.network_generation;
        out.sprites[0].color={0,1,1};out.sprites[0].starts=now;out.sprites[0].expires=now+.125;
        if(probe_->persistent_beams){out.sprites[0].persistent=true;out.sprites[0].noise_amplitude=3;out.sprites[0].scroll_cycles_per_second=.25F;out.sprites[0].phase_seconds=now;}return out;
    }
    void committed_temporary_effect(const client::CommittedTemporaryEffect& event) noexcept override {
        if (event.generation != probe_->identity.network_generation) return;
        ++probe_->temporary_effects;
        probe_->temporary_seconds = event.received_at_seconds;
        probe_->temporary_observation=event;
        temporary_.count = 1;
        temporary_.effects[0] = {{event.generation, event.record, event.message_bit_offset, event.entry_ordinal},
            event.origin, event.received_at_seconds};
        // Independent alternate policy: a cyan light only, regardless of wire
        // kind. No Half-Life profile, sprite name, audio sample or fallback.
        temporary_.effects[0].light = api::TemporaryLightIntent{17.0F, .25F, {0, 1, 1}, .125};
        if(probe_->surface_decal_test && event.kind==client::TemporaryEffectKind::decal) {
            // Explicit alternate policy and fixture-owned slot. No concrete
            // game's decal catalogue or sprite profile is available here.
            temporary_.effects[0].light.reset();
            temporary_.effects[0].surface_decal=api::TemporarySurfaceDecalIntent{0,
                static_cast<std::uint16_t>(event.decal_index),static_cast<std::uint32_t>(event.decal_entity),
                event.decal_model_reference,event.decal_target_pose};
        }
    }
    api::TemporaryEffectsBatch drain_temporary_effects(double) noexcept override {
        const auto result = temporary_; temporary_ = {}; return result;
    }
    api::WorldEntityPresentationIntent world_entity(const api::WorldEntityPresentationContext& input) const noexcept override {
        if(input.entity.render_effect!=201U) return {};
        return {api::WorldEntityPresentationRole::player_proxy,27U,3U,true};
    }
    void remote_entities(std::span<const client::RuntimePacketEntityObservation> input,
        std::uint32_t,std::uint64_t) noexcept override { probe_->entity_observations+=input.size(); }
    api::RemoteWeaponSocketPlan remote_weapon_socket_plan(
        const api::RemoteWeaponModelContext&) noexcept override {
        api::RemoteWeaponSocketPlan result;
        result.source=api::RemoteSocketSource::validated_pmodel_socket;
        result.muzzle={0,{3,5,7}};result.shell={0,{-3,-5,7}};
        return result;
    }
    void remote_weapon_poses(std::span<const api::RemoteWeaponPoseSample> input) noexcept override {
        probe_->socket_samples=input.size();
        probe_->last_socket=input.empty() ? api::RemoteWeaponPoseSample{} : input.back();
    }
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
        probe_->model_variants.clear();
        audio_={};
        visual_.reset();
        remote_={};
        temporary_={};
        probe_->socket_samples=0;probe_->last_socket={};
        hud_resources_={};hud_menu_open_=false;hud_candidate_.reset();hud_pending_.reset();hud_queued_=false;
    }
    void teardown() noexcept override {
        probe_->model_variants.clear();
        ++probe_->teardowns;
        audio_={};
        visual_.reset();
        remote_={};
        temporary_={};
        probe_->socket_samples=0;probe_->last_socket={};
        committed_ = {};
        model_.reset();
        hud_resources_={};hud_menu_open_=false;hud_candidate_.reset();hud_pending_.reset();hud_queued_=false;
    }
    const api::GameMovementPolicy& movement_policy() const noexcept override { return policy_; }
    void enable_edge_test_policy() noexcept {
        policy_.live_buttons=hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::move_left);
        edge_enabled_=true;
    }
    hlclient::gameplay_input::GameplayButtonMask edge_triggered_buttons(
        const client::RuntimeClientObservationState&) const noexcept override {
        return edge_enabled_ ? policy_.live_buttons | 0x80000000U : 0U;
    }
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
    std::span<const std::string_view> local_model_preparation() const noexcept override {
        static constexpr std::array<std::string_view,2> names{"models/owned_a.mdl","models/owned_b.mdl"};return names;
    }
    void bind_model_variants(std::span<const api::LocalWeaponModelMetadata> models) override {
        probe_->model_variants.assign(models.begin(),models.end());++probe_->variant_bindings;
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
        if(probe_->sprite_hud_test) {
            result.weapon_menu_open=hud_menu_open_;result.weapon_menu_candidate=hud_candidate_;
            result.weapon_selection_request=hud_queued_ ? std::nullopt : hud_pending_;
            result.weapon_selection_pending=hud_queued_ ? hud_pending_ : std::nullopt;
            for(const auto& descriptor:hud_resources_.descriptors)for(const auto& entry:descriptor.entries)
                if(entry.status==api::HudResourceStatus::ready)
                    result.draw.sprites.push_back({entry.resource_id,entry.resource_revision,entry.frame,entry.source,
                        17.0F,19.0F,static_cast<float>(entry.source.width),static_cast<float>(entry.source.height),
                        {0,1,1,1},entry.blend,{},7});
        }
        return result;
    }
    std::span<const api::HudManifestRequest> hud_resource_preparation() const noexcept override {
        if(!probe_->sprite_hud_test)return {};
        static const std::array<api::HudManifestRequest,1> requests{
            api::HudManifestRequest{"sprites/project_owned_ui.txt",{}}};
        return requests;
    }
    void bind_hud_resources(const api::HudResourceSet& resources) override {
        if(!probe_->sprite_hud_test)return;
        ++probe_->hud_bindings;probe_->hud_resources=resources;hud_resources_=resources;
    }
    api::HudMenuResult hud_menu_input(const client::RuntimeClientObservationState& observation,
        const api::HudMenuInput& input) noexcept override {
        if(!probe_->sprite_hud_test || observation.generation!=probe_->identity.network_generation)return {};
        ++probe_->hud_menu_inputs;probe_->last_hud_input=input;
        api::HudMenuResult result;
        if(input.cancel && hud_menu_open_) {
            hud_menu_open_=false;hud_candidate_.reset();result.cancel_consumed=true;
        } else if(input.group_digit) {hud_menu_open_=true;hud_candidate_=std::uint8_t{7U};}
        if(hud_menu_open_ && (input.primary_pressed || input.primary_held)) {
            result.selection=hud_candidate_;result.consume_primary=true;
            hud_pending_=hud_candidate_;hud_queued_=false;hud_menu_open_=false;hud_candidate_.reset();
        }
        result.menu_open=hud_menu_open_;return result;
    }
    void hud_selection_result(std::uint8_t weapon,bool queued,double) noexcept override {
        if(!probe_->sprite_hud_test)return;
        ++probe_->hud_handoffs;probe_->hud_handoff_weapon=weapon;probe_->hud_handoff_queued=queued;
        if(hud_pending_!=weapon)return;
        if(queued)hud_queued_=true;
        else {hud_pending_.reset();hud_queued_=false;}
    }
    api::CameraIntent camera(const client::RuntimeClientObservationState&,
        double) const noexcept override {
        api::CameraIntent result;
        result.allow_predicted_translation = false;
        result.pitch_offset_degrees = 7.0;
        result.yaw_offset_degrees = 11.0;
        result.mouse_sensitivity_scale=probe_->mouse_sensitivity_scale;
        return result;
    }
    std::optional<api::GameCommandRequest> inventory_selection(
        const client::RuntimeClientObservationState&, std::uint8_t id) const override {
        if(probe_->sprite_hud_test && id==7U)
            return api::GameCommandRequest{api::GameCommandKind::inventory_selection,"weapon_project"};
        return {};
    }
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
    bool edge_enabled_{};
    bool remote_enabled_{};
    api::LocalAudioBatch audio_;
    api::RemoteWeaponEffectsBatch remote_;
    api::TemporaryEffectsBatch temporary_;
    std::optional<api::LocalMuzzleFlash> visual_;
    std::shared_ptr<TestModuleProbe> probe_;
    api::GameMovementPolicy policy_;
    api::GameRecordState committed_;
    std::uint64_t observed_generation_{}, observed_revision_{};
    std::optional<api::LocalWeaponSubmittedCommand> last_command_;
    api::LocalWeaponPresentationSnapshot presentation_;
    std::optional<api::LocalWeaponModelMetadata> model_;
    api::HudResourceSet hud_resources_;
    bool hud_menu_open_{}, hud_queued_{};
    std::optional<std::uint8_t> hud_candidate_,hud_pending_;
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
api::HudResourceSet project_owned_hud_resources() {
    api::HudResourceSet resources;resources.revision=3;
    api::HudResourceDescriptor descriptor;descriptor.name="sprites/project_owned_ui.txt";
    descriptor.status=api::HudResourceStatus::ready;
    api::HudResourceEntry entry;entry.role="project_pointer";entry.resolution=777;
    entry.sprite_name="sprites/project_owned_ui.spr";entry.resource_id=501;entry.resource_revision=3;
    entry.frame=2;entry.source={3,5,7,9};entry.frame_width=32;entry.frame_height=32;
    entry.blend=api::HudSpriteBlend::straight_alpha;entry.status=api::HudResourceStatus::ready;
    descriptor.entries.push_back(std::move(entry));resources.descriptors.push_back(std::move(descriptor));
    return resources;
}
} // namespace

TEST_CASE("Neutral screen effects are bounded by the core host without a concrete game",
    "[screen-effects][game-api][core-only]") {
    auto probe=std::make_shared<TestModuleProbe>();
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};host.reset({3,4,2});
    CHECK(host.screen_effects(1).translation==std::array<float,3>{});
    probe->screen_effects={{1,2,3},2,{0,.5F,1,.25F},true};
    CHECK(host.screen_effects(1).translation==probe->screen_effects.translation);
    CHECK(host.screen_effects(1).overlay_rgba==probe->screen_effects.overlay_rgba);
    CHECK(host.screen_effects(1).modulate);
    CHECK(host.screen_effects(-1).overlay_rgba[3]==0);
    for(float bad:{17.F,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        probe->screen_effects.translation[0]=bad;
        CHECK(host.screen_effects(1).overlay_rgba[3]==0);
    }
    probe->screen_effects.translation[0]=1;probe->screen_effects.roll_degrees=17;
    CHECK(host.screen_effects(1).overlay_rgba[3]==0);
    probe->screen_effects.roll_degrees=2;probe->screen_effects.overlay_rgba[3]=2;
    CHECK(host.screen_effects(1).overlay_rgba[3]==0);
    probe->screen_effects.overlay_rgba[3]=.5F;host.teardown();
    CHECK(host.screen_effects(1).overlay_rgba[3]==0);
}

TEST_CASE("E29 alternate module declares and binds its own cyan sprite through the same core host API",
    "[e29][hud][game-api][core-only]") {
    auto probe=std::make_shared<TestModuleProbe>();probe->sprite_hud_test=true;
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};host.reset({3,4,2});
    const auto preparation=host.hud_resource_preparation();REQUIRE(preparation.size()==1);
    CHECK(preparation[0].name=="sprites/project_owned_ui.txt");CHECK_FALSE(preparation[0].count_compatibility);
    CHECK(host.hud_sound_preparation().empty());
    auto resources=project_owned_hud_resources();REQUIRE(host.bind_hud_resources(resources));
    CHECK(probe->hud_bindings==1);CHECK(probe->hud_resources==resources);
    resources.descriptors[0].entries[0].resource_id=999; // neither host nor module retains borrowed metadata
    const auto state=test_observation();const auto before=client::runtime_observation_canonical_hash(state);
    const auto hud=host.hud(state,.1);REQUIRE(hud.draw.sprites.size()==1);
    const auto& sprite=hud.draw.sprites[0];CHECK(sprite.resource_id==501);CHECK(sprite.resource_revision==3);
    CHECK(sprite.frame==2);CHECK(sprite.source==api::HudSourceRectangle{3,5,7,9});
    CHECK(sprite.x==17);CHECK(sprite.y==19);CHECK(sprite.width==7);CHECK(sprite.height==9);
    CHECK(sprite.color==std::array<float,4>{0,1,1,1});CHECK(sprite.blend==api::HudSpriteBlend::straight_alpha);
    CHECK(api::valid_hud_draw_commands(hud.draw));CHECK(hud.crosshair_role.empty());CHECK(hud.crosshair_resource_id==0);
    CHECK(client::runtime_observation_canonical_hash(state)==before);
    resources=project_owned_hud_resources();resources.descriptors[0].entries[0].source.width=30;
    CHECK_FALSE(host.bind_hud_resources(resources));CHECK(probe->hud_bindings==1);
    CHECK(host.hud(state,.2).draw.sprites[0].source.width==7);
    resources=project_owned_hud_resources();resources.descriptors[0].entries[0].source.x=std::numeric_limits<std::uint32_t>::max();
    CHECK_FALSE(host.bind_hud_resources(resources));CHECK(probe->hud_bindings==1);
}
TEST_CASE("E29 alternate menu input request and actual handoff stay independent of Half-Life rules",
    "[e29][hud][game-api][core-only]") {
    auto probe=std::make_shared<TestModuleProbe>();probe->sprite_hud_test=true;
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};host.reset({3,4,2});
    const auto state=test_observation();
    auto opened=host.hud_menu_input(state,{.group_digit=4,.seconds=.1});CHECK(opened.menu_open);CHECK_FALSE(opened.selection);
    CHECK(probe->hud_menu_inputs==1);CHECK(probe->last_hud_input.group_digit==4);
    auto hud=host.hud(state,.11);CHECK(hud.weapon_menu_candidate==7);CHECK_FALSE(hud.weapon_selection_pending);
    const auto confirm=host.hud_menu_input(state,{.primary_held=true,.seconds=.2});
    CHECK(confirm.consume_primary);CHECK(confirm.selection==7);CHECK_FALSE(confirm.menu_open);
    const auto command=host.inventory_selection(state,7);REQUIRE(command);CHECK(command->token=="weapon_project");
    CHECK(command->kind==api::GameCommandKind::inventory_selection);
    CHECK(hlclient::goldsrc::encode_game_command(*command));
    CHECK_FALSE(host.inventory_selection(state,2));
    CHECK(host.hud(state,.21).weapon_selection_request==7);CHECK_FALSE(host.hud(state,.21).weapon_selection_pending);
    host.hud_selection_result(7,false,.22);CHECK(probe->hud_handoffs==1);CHECK_FALSE(probe->hud_handoff_queued);
    hud=host.hud(state,.23);CHECK_FALSE(hud.weapon_selection_request);CHECK_FALSE(hud.weapon_selection_pending);
    CHECK_FALSE(hud.weapon_menu_candidate);
    (void)host.hud_menu_input(state,{.group_digit=4,.seconds=.3});
    CHECK(host.hud_menu_input(state,{.primary_pressed=true,.seconds=.4}).selection==7);
    host.hud_selection_result(7,true,.41);CHECK(probe->hud_handoffs==2);CHECK(probe->hud_handoff_weapon==7);
    CHECK(probe->hud_handoff_queued);CHECK(host.hud(state,.42).weapon_selection_pending==7);
    (void)host.hud_menu_input(state,{.group_digit=4,.seconds=.5});
    const auto cancel=host.hud_menu_input(state,{.cancel=true,.seconds=.6});
    CHECK(cancel.cancel_consumed);CHECK_FALSE(cancel.menu_open);
    CHECK_FALSE(host.hud_menu_input(state,{.cancel=true,.seconds=.7}).cancel_consumed);
}
TEST_CASE("E29 core host rejects malformed HUD input before calling the opt-in alternate module",
    "[e29][hud][game-api][core-only]") {
    auto probe=std::make_shared<TestModuleProbe>();probe->sprite_hud_test=true;
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};host.reset({3,4,2});
    const auto state=test_observation();
    CHECK_FALSE(host.hud_menu_input(state,{.group_digit=0,.seconds=.1}).menu_open);
    CHECK_FALSE(host.hud_menu_input(state,{.group_digit=6,.seconds=.1}).menu_open);
    CHECK_FALSE(host.hud_menu_input(state,{.cycle_direction=2,.seconds=.1}).menu_open);
    CHECK_FALSE(host.hud_menu_input(state,{.seconds=-1}).menu_open);
    CHECK_FALSE(host.hud_menu_input(state,{.seconds=std::numeric_limits<double>::quiet_NaN()}).menu_open);
    CHECK(probe->hud_menu_inputs==0);
    host.hud_selection_result(0,true,.1);host.hud_selection_result(7,true,-1);
    host.hud_selection_result(7,true,std::numeric_limits<double>::infinity());CHECK(probe->hud_handoffs==0);
}
TEST_CASE("E29 alternate HUD resets tears down and defaults without concrete-game fallback",
    "[e29][hud][game-api][core-only]") {
    auto probe=std::make_shared<TestModuleProbe>();
    api::GameClientHost default_host{std::make_unique<TestGameClientModule>(probe)};default_host.reset({3,4,2});
    const auto state=test_observation();CHECK(default_host.hud_resource_preparation().empty());
    CHECK(default_host.hud(state,0).draw.sprites.empty());CHECK_FALSE(default_host.hud_menu_input(state,{.group_digit=2,.seconds=.1}).menu_open);
    CHECK_FALSE(default_host.inventory_selection(state,7));CHECK(probe->hud_menu_inputs==0);
    probe->sprite_hud_test=true;REQUIRE(default_host.bind_hud_resources(project_owned_hud_resources()));
    (void)default_host.hud_menu_input(state,{.group_digit=4,.seconds=.2});
    (void)default_host.hud_menu_input(state,{.primary_pressed=true,.seconds=.3});
    default_host.hud_selection_result(7,true,.31);CHECK(default_host.hud(state,.32).weapon_selection_pending==7);
    default_host.reset({5,6,2});auto reset_state=state;reset_state.generation=5;
    const auto fresh=default_host.hud(reset_state,.4);CHECK(fresh.draw.sprites.empty());
    CHECK_FALSE(fresh.weapon_menu_open);CHECK_FALSE(fresh.weapon_menu_candidate);CHECK_FALSE(fresh.weapon_selection_pending);
    CHECK(default_host.hud_menu_input(state,{.group_digit=4,.seconds=.5}).selection==std::nullopt);
    default_host.teardown();const auto old_calls=probe->hud_menu_inputs;
    CHECK(default_host.hud_resource_preparation().empty());CHECK(default_host.hud(reset_state,.6).draw.sprites.empty());
    CHECK(default_host.hud(reset_state,.6).draw.texts.empty());
    CHECK_FALSE(default_host.bind_hud_resources(project_owned_hud_resources()));
    CHECK_FALSE(default_host.hud_menu_input(reset_state,{.group_digit=4,.seconds=.7}).menu_open);
    default_host.hud_selection_result(7,true,.8);CHECK(probe->hud_menu_inputs==old_calls);
}

TEST_CASE("E25 alternate module receives bounded owning model variants through the same host without HL fallback", "[e25][core-only][game-api]") {
    auto probe=std::make_shared<TestModuleProbe>();api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};
    host.reset({3,4,2});REQUIRE(host.local_model_preparation().size()==2);
    std::array<api::LocalWeaponModelMetadata,2> models;
    for(std::size_t i=0;i<models.size();++i) {
        models[i].generation=3;models[i].model_index=static_cast<std::uint32_t>(17+i);models[i].resource_revision=2;
        models[i].resource_name=host.local_model_preparation()[i];
    }
    REQUIRE(host.bind_model_variants(models));CHECK(probe->variant_bindings==1);models[0].resource_name="changed";
    CHECK(probe->model_variants[0].resource_name=="models/owned_a.mdl");
    CHECK_FALSE(host.bind_model_variants(models));CHECK(probe->variant_bindings==1);
    models[0]=models[1];CHECK_FALSE(host.bind_model_variants(models));
    std::array<api::LocalWeaponModelMetadata,9> excessive{};CHECK_FALSE(host.bind_model_variants(excessive));
    host.reset({3,5,2});CHECK(probe->model_variants.empty());host.teardown();
    CHECK(host.local_model_preparation().empty());CHECK_FALSE(host.bind_model_variants({}));
}
TEST_CASE("E21 alternate module uses the same neutral beam and command impulse host without HL linkage", "[e21][core-only][game-api]") {
    struct EmptyWorld:api::IStaticEffectTrace {api::StaticEffectTrace trace(hlclient::assets::AssetVector3,hlclient::assets::AssetVector3 end) const noexcept override {return {true,false,false,false,false,end,{}};}} world;
    auto probe=std::make_shared<TestModuleProbe>();api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};
    host.reset({3,4,2});api::LocalWeaponSubmittedCommand command;command.generation=3;command.sequence=1;
    CHECK(host.command_movement_effect_phase()==api::CommandMovementEffectPhase::before_movement);
    const auto impulse=host.command_movement_effect(command);REQUIRE(impulse);CHECK(impulse->x==7);CHECK(impulse->z==13);
    const auto beam=host.beam_effects(world,.1,{});REQUIRE(beam.count==1);CHECK(beam.sprites[0].color[0]==0);
    CHECK(beam.sprites[0].expires==.225);CHECK(beam.sprites[0].action.generation==3);
    host.reset({5,6,2});CHECK_FALSE(host.command_movement_effect(command));host.teardown();CHECK(probe->teardowns>=1);CHECK(host.beam_effects(world,.2,{}).count==0);
    CHECK(host.command_movement_effect_phase()==api::CommandMovementEffectPhase::before_movement);
}
TEST_CASE("E22 alternate module current-state persistent beam uses the same core host","[e22][core-only][game-api]") {
    struct World:api::IStaticEffectTrace {api::StaticEffectTrace trace(hlclient::assets::AssetVector3,hlclient::assets::AssetVector3 end)const noexcept override{return {true,false,false,false,false,end,{}};}} world;
    auto probe=std::make_shared<TestModuleProbe>();probe->persistent_beams=true;api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};host.reset({3,4,2});
    const auto early=host.beam_effects(world,.1,{}),late=host.beam_effects(world,30,{});REQUIRE(early.count==1);REQUIRE(late.count==1);
    CHECK(late.sprites[0].persistent);CHECK(late.sprites[0].noise_amplitude==3);CHECK(late.sprites[0].phase_seconds==30);CHECK(late.sprites[0].color[0]==0);
    CHECK(late.sprites[0].ribbon_glow.opacity==0);
    CHECK(host.beam_ribbon_glow_profile("sprites/xbeam1.spr").opacity==0);
    CHECK(host.beam_ribbon_glow_profile("sprites/alternate_owned.spr").opacity==0);
    CHECK(late.sprites[0].scene_light.intensity==0);
    CHECK(host.beam_scene_light_profile("sprites/xbeam1.spr",true).intensity==0);
    CHECK(host.beam_scene_light_profile("sprites/alternate_owned.spr",false).intensity==0);
    host.teardown();CHECK(host.beam_effects(world,31,{}).count==0);
}
TEST_CASE("E20 generic follow outbox bypasses alternate gameplay semantics and clears on reset",
    "[e20][game-api][core-only]") {
    auto probe=std::make_shared<TestModuleProbe>();
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};
    host.reset({1,1,1});host.set_movement_audio_time_origin(10);
    client::CommittedTemporaryEffect e;e.generation=1;e.record=e.record_ordinal=1;
    e.received_at_seconds=10.1;e.kind=client::TemporaryEffectKind::beam_follow;
    e.entity_attachment=29;e.sprite_index=7;e.life_tenths=20;e.width_tenths=3;
    host.committed_temporary_effect(e);auto batch=host.drain_follow_events();REQUIRE(batch.count==1);
    CHECK(batch.events[0].entity_attachment==29);CHECK(batch.events[0].width_tenths==3);
    CHECK(probe->temporary_effects==0);CHECK(host.drain_temporary_effects(.1).count==0);
    CHECK(host.drain_follow_events().count==0);
    host.committed_temporary_effect(e);host.reset({2,2,1});CHECK(host.drain_follow_events().count==0);
    host.committed_temporary_effect(e);CHECK(host.drain_follow_events().count==0);
    e.generation=2;for(unsigned i=0;i<33;++i)host.committed_temporary_effect(e);
    batch=host.drain_follow_events();CHECK(batch.count==32);CHECK(batch.dropped==1);
    host.teardown();CHECK(host.drain_follow_events().count==0);
}

TEST_CASE("E20 malformed suffix cannot publish a staged generic follow event",
          "[e20][game-api][core-only][runtime-replay]") {
    auto probe=std::make_shared<TestModuleProbe>();
    auto host=std::make_shared<api::GameClientHost>(std::make_unique<TestGameClientModule>(probe));
    auto built=hlclient::goldsrc::make_runtime_replay_fixture({});REQUIRE(built);
    auto init=built.fixture->initialization;init.game_client=host;
    client::ClientWorldState world;auto session=hlclient::goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
    host->set_movement_audio_time_origin(0);
    auto record=built.fixture->records.front();
    record.payload.bytes={std::byte{23},std::byte{22},std::byte{29},std::byte{0},std::byte{7},std::byte{0},
        std::byte{40},std::byte{5},std::byte{224},std::byte{224},std::byte{255},std::byte{255},std::byte{255}};
    auto cursor=hlclient::goldsrc::StockRuntimeSourceCursor::create(0,0,record.payload.bytes.size());REQUIRE(cursor);
    record.initial_cursor=*cursor;const auto before=world.runtime_observation();const auto commits=probe->commits;
    CHECK_FALSE(session.session->apply_record(record));CHECK(world.runtime_observation()==before);
    CHECK(probe->commits==commits);CHECK(host->drain_follow_events().count==0);
    record.payload.bytes.pop_back();++record.record_identity;++record.record_ordinal;++record.payload.source_sequence;
    cursor=hlclient::goldsrc::StockRuntimeSourceCursor::create(0,0,record.payload.bytes.size());REQUIRE(cursor);record.initial_cursor=*cursor;
    REQUIRE(session.session->apply_record(record));CHECK(host->drain_follow_events().count==1);
    CHECK(host->drain_follow_events().count==0);CHECK(probe->temporary_effects==0);
}

TEST_CASE("E18 alternate module selects bounded edge input through the same generic host",
          "[e18][game-api][core-only][input-adapter]") {
    auto probe=std::make_shared<TestModuleProbe>();auto module=std::make_unique<TestGameClientModule>(probe);
    module->enable_edge_test_policy();api::GameClientHost host(std::move(module));host.reset({3,1,1});
    CHECK(host.edge_triggered_buttons(test_observation())==
        hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::move_left));
    host.teardown();CHECK(host.edge_triggered_buttons(test_observation())==0U);
    CHECK(probe->resets==1);CHECK(probe->teardowns==1);
}

TEST_CASE("E16 alternate module receives temporary effects through shared host clock and lifecycle",
          "[e16][game-module][game-api][core-only][temp-entity]") {
    const auto probe = std::make_shared<TestModuleProbe>();
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};
    host.reset({3, 5, 2});
    client::CommittedTemporaryEffect event;
    event.generation = 3; event.record = 23; event.record_ordinal = 7;
    event.message_bit_offset = 104; event.entry_ordinal = 0;
    event.received_at_seconds = 101.25; event.kind = client::TemporaryEffectKind::bubbles;
    event.origin = {2, 3, 4}; event.sprite_index = 29;
    host.committed_temporary_effect(event); // Explicit absent clock, no guessed timing.
    CHECK(probe->temporary_effects == 0U);
    host.set_movement_audio_time_origin(100.0);
    host.committed_temporary_effect(event);
    CHECK(probe->temporary_effects == 1U); CHECK(probe->temporary_seconds == 1.25);
    auto output = host.drain_temporary_effects(1.25);
    REQUIRE(output.count == 1U);
    CHECK(output.effects[0].identity.generation == 3U);
    CHECK(output.effects[0].identity.record == 23U);
    CHECK(output.effects[0].identity.message_bit_offset == 104U);
    CHECK(output.effects[0].origin.x == 2.0F); CHECK(output.effects[0].origin.y == 3.0F);
    CHECK(output.effects[0].origin.z == 4.0F); CHECK(output.effects[0].starts_at_seconds == 1.25);
    REQUIRE(output.effects[0].light);
    CHECK(output.effects[0].light->radius_units == 17.0F);
    CHECK(output.effects[0].light->color == std::array<float, 3>{0, 1, 1});
    CHECK_FALSE(output.effects[0].sprite); CHECK_FALSE(output.effects[0].particles);
    CHECK(host.drain_temporary_effects(1.25).count == 0U);
    host.committed_temporary_effect(event);
    host.reset({4, 6, 2});
    CHECK(host.drain_temporary_effects(1.25).count == 0U);
    host.committed_temporary_effect(event); // Alternate module owns generation policy too.
    CHECK(probe->temporary_effects == 2U);
    event.generation = 4; ++event.record;
    host.committed_temporary_effect(event);
    REQUIRE(host.drain_temporary_effects(1.25).count == 1U);
    host.committed_temporary_effect(event);
    host.teardown();
    CHECK(host.drain_temporary_effects(1.25).count == 0U);
    host.committed_temporary_effect(event);
    CHECK(probe->temporary_effects == 4U); CHECK(probe->teardowns == 1U);
}

TEST_CASE("Neutral committed sound seam is available to core only alternate module without fallback",
    "[game-module][game-api][core-only][sound-pop]") {
    auto probe=std::make_shared<TestModuleProbe>();
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};host.reset({3,5,2});
    host.bind_effect_resource({api::EffectResourceBinding::Kind::sound,7,"project/owned.wav"});
    CHECK(probe->effect_bindings==1);
    api::CommittedSoundObservation event;event.generation=3;event.record=23;event.record_ordinal=9;
    event.entity=41;event.sample_slot=7;event.channel=3;event.origin={2,3,4};event.received_at_seconds=101.25;
    host.committed_sound(event);CHECK(probe->sound_observations==0);
    host.set_movement_audio_time_origin(100);host.committed_sound(event);
    REQUIRE(probe->sound_observations==1);CHECK(probe->sound_observation.received_at_seconds==1.25);
    CHECK(probe->sound_observation.entity==41);CHECK(probe->sound_observation.origin.x==2);
    CHECK(probe->sound_observation.origin.y==3);CHECK(probe->sound_observation.origin.z==4);
    CHECK(host.drain_temporary_effects(1.25).count==0); // No concrete game fallback in core.
    CHECK(host.drain_audio().count==0);
    event.received_at_seconds=99;host.committed_sound(event);CHECK(probe->sound_observations==1);
    host.reset({4,6,2});event.received_at_seconds=102;host.committed_sound(event);CHECK(probe->sound_observations==1);
    host.teardown();host.committed_sound(event);host.bind_effect_resource({});
    CHECK(probe->sound_observations==1);CHECK(probe->effect_bindings==1);
}

TEST_CASE("Neutral verified effect palette and optional presentation policy cross core only host",
    "[e27-1][game-api][core-only][effect-palette]") {
    auto probe=std::make_shared<TestModuleProbe>();
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};host.reset({3,5,2});
    CHECK(host.effect_palette_preparation()=="gfx/project_owned.lmp");
    CHECK(host.temporary_effect_source_label(api::TemporaryEffectIdentity::Source::server_sound)=="project_owned_effect");
    api::EffectPaletteMetadata palette;palette.status=api::EffectPaletteStatus::ready;palette.colors[17]={7,11,13};
    host.bind_effect_palette(palette);REQUIRE(probe->palette_bindings==1);
    CHECK(probe->palette.colors[17]==std::array<std::uint8_t,3>{7,11,13});
    palette.colors[17]={0,0,0};CHECK(probe->palette.colors[17][0]==7); // Owning copy.
    host.configure_effect_policy({false});CHECK(probe->effect_policies==1);
    REQUIRE(probe->effect_policy.server_blood_sprites_enabled);CHECK_FALSE(*probe->effect_policy.server_blood_sprites_enabled);
    host.configure_effect_policy({});CHECK_FALSE(probe->effect_policy.server_blood_sprites_enabled);
    palette.status=static_cast<api::EffectPaletteStatus>(255);host.bind_effect_palette(palette);
    CHECK(probe->palette_bindings==1);CHECK(host.drain_temporary_effects(0).count==0);
    host.teardown();host.bind_effect_palette(palette);host.configure_effect_policy({true});
    CHECK(probe->effect_policies==2);CHECK(host.effect_palette_preparation().empty());
}

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
    independent_entity.render_effect=17;
    CHECK(host.world_entity({independent_entity}).role==api::WorldEntityPresentationRole::ordinary);
    independent_entity.render_effect=201;
    CHECK(host.world_entity({independent_entity}).model_index==27);
    input.server_owned_proxy=true;
    CHECK(host.remote_player(input).sample.sequence==13); // Same host, independent proxy pose rule.
    const std::array independent_entities{independent_entity};
    host.remote_entities(independent_entities,8,1);CHECK(probe->entity_observations==1);
    host.reset({4U,6U,2U});
    CHECK(probe->resets==2U);
    host.teardown();
    CHECK(host.remote_player(input).status==api::RemotePlayerPresentationStatus::silent);
    CHECK_FALSE(host.remote_player_policy().enabled);
    CHECK(host.world_entity({independent_entity}).role==api::WorldEntityPresentationRole::ordinary);
    host.remote_entities(independent_entities,8,2);CHECK(probe->entity_observations==1);
    CHECK(probe->teardowns==1U);
}

TEST_CASE("E27 core alternate temporary delivery diagnostics partition clock failures and reset without Half-Life linkage",
          "[e27][pop-diagnostics][game-module][game-api][core-only][temp-entity]") {
    const auto probe=std::make_shared<TestModuleProbe>();api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};
    host.reset({3,5,2});client::CommittedTemporaryEffect event;event.generation=3;event.record=1;
    event.received_at_seconds=101.25;event.origin={2,3,4};
    const std::array kinds{client::TemporaryEffectKind::explosion,client::TemporaryEffectKind::smoke,
        client::TemporaryEffectKind::sparks,client::TemporaryEffectKind::bubbles,client::TemporaryEffectKind::bubble_trail,
        client::TemporaryEffectKind::beam_follow,client::TemporaryEffectKind::blood_sprite};
    for(const auto kind:kinds) {event.kind=kind;host.committed_temporary_effect(event);const auto clock=host.temporary_delivery_statistics(kind);
        CHECK(clock.received==1);CHECK(clock.forwarded==0);CHECK(clock.clock_dropped==1);}
    CHECK(probe->temporary_effects==0);CHECK(host.drain_temporary_effects(1.25).count==0);CHECK(host.drain_follow_events().count==0);
    host.set_movement_audio_time_origin(100);
    for(const auto kind:kinds) {event.kind=kind;++event.record;host.committed_temporary_effect(event);
        const auto clock=host.temporary_delivery_statistics(kind);CHECK(clock.received==2);CHECK(clock.clock_dropped==1);
        if(kind==client::TemporaryEffectKind::beam_follow) {CHECK(clock.forwarded==0);CHECK(host.drain_follow_events().count==1);}
        else {CHECK(clock.forwarded==1);const auto result=host.drain_temporary_effects(1.25);REQUIRE(result.count==1);
            REQUIRE(result.effects[0].light);CHECK(result.effects[0].light->color==std::array<float,3>{0,1,1});
            CHECK(result.effects[0].starts_at_seconds==1.25);CHECK(result.effects[0].diagnostic_tag==0);
            CHECK_FALSE(result.effects[0].sprite);CHECK_FALSE(result.effects[0].secondary_sprite);CHECK_FALSE(result.effects[0].sound);}}
    CHECK(probe->temporary_effects==6);CHECK(probe->temporary_seconds==1.25);
    event.kind=client::TemporaryEffectKind::blood_sprite;
    for(const double time:{99.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        event.received_at_seconds=time;host.committed_temporary_effect(event);}
    const auto rejected=host.temporary_delivery_statistics(event.kind);
    CHECK(rejected.received==5);CHECK(rejected.forwarded==1);CHECK(rejected.clock_dropped==4);CHECK(probe->temporary_effects==6);
    CHECK(host.temporary_delivery_statistics(client::TemporaryEffectKind::explosion).received==2);
    event.kind=static_cast<client::TemporaryEffectKind>(255);host.committed_temporary_effect(event);
    const auto unknown=host.temporary_delivery_statistics(event.kind);CHECK(unknown.received==0);CHECK(unknown.forwarded==0);CHECK(unknown.clock_dropped==0);
    host.reset({4,6,2});
    for(const auto kind:kinds) {const auto clock=host.temporary_delivery_statistics(kind);CHECK(clock.received==0);CHECK(clock.forwarded==0);CHECK(clock.clock_dropped==0);}
    event.kind=client::TemporaryEffectKind::blood_sprite;event.generation=4;event.received_at_seconds=101.25;
    host.committed_temporary_effect(event);CHECK(probe->temporary_effects==7);CHECK(probe->temporary_seconds==1.25);
    CHECK(host.temporary_delivery_statistics(event.kind).forwarded==1);CHECK(host.temporary_delivery_statistics(event.kind).clock_dropped==0);
    REQUIRE(host.drain_temporary_effects(1.25).count==1);host.teardown();host.committed_temporary_effect(event);
    const auto stopped=host.temporary_delivery_statistics(event.kind);CHECK(stopped.received==2);CHECK(stopped.forwarded==1);CHECK(stopped.clock_dropped==1);
    CHECK(probe->temporary_effects==7);CHECK(host.drain_temporary_effects(1.25).count==0);
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
    CHECK(host.movement_policy().nonblocking_point_boxes.empty());
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
        CHECK(camera.mouse_sensitivity_scale == 1.0);
        probe->mouse_sensitivity_scale=.375;
        CHECK(host.camera(observation,0.0).mouse_sensitivity_scale == .375);
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
        CHECK(host.camera(observation,0.0).mouse_sensitivity_scale == 1.0);
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

TEST_CASE("E14 neutral HUD context does not create hidden Half-Life HUD in alternate module",
          "[e14][game-api][core-only]") {
    const auto probe=std::make_shared<TestModuleProbe>();
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};
    host.reset({3U,5U,2U});const auto observation=test_observation();
    host.set_hud_context({640U,480U,true});const auto shown=host.hud(observation,1.0);
    host.set_hud_context({640U,480U,false});const auto hidden=host.hud(observation,1.0);
    REQUIRE(shown.draw.texts.size()==1U);REQUIRE(hidden.draw.texts.size()==1U);
    CHECK(shown.draw.texts[0].text==hidden.draw.texts[0].text);CHECK(shown.draw.rectangles.empty());
    CHECK_FALSE(shown.draw_rejected);CHECK(api::valid_hud_draw_commands(shown.draw));
    auto bad=shown.draw;bad.texts.resize(api::kMaximumHudTexts+1U);CHECK_FALSE(api::valid_hud_draw_commands(bad));
    bad=shown.draw;bad.texts[0].text=std::string(161U,'a');CHECK_FALSE(api::valid_hud_draw_commands(bad));
    host.set_movement_audio_time_origin(1000.0);CHECK(host.presentation_seconds(1001.5)==1.5);
    CHECK_FALSE(host.presentation_seconds(999.0));host.teardown();CHECK(host.hud(observation,2.0).draw.texts.empty());
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

TEST_CASE("E12 core alternate module uses bounded owned socket handoff without Half-Life fallback",
          "[e12][game-api][core-only]") {
    const auto probe=std::make_shared<TestModuleProbe>();
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};
    hlclient::assets::SkeletalModelAssetData model;
    host.reset({4,9,2});
    const auto plan=host.remote_weapon_socket_plan({19,{7,11},"project_owned/alternate",model});
    CHECK(plan.source==api::RemoteSocketSource::validated_pmodel_socket);
    CHECK(plan.muzzle.local_origin.y==5);CHECK(plan.shell.local_origin.x==-3);
    std::array<api::RemoteWeaponPoseSample,40> input;
    for(std::size_t i=0;i<input.size();++i) {input[i].entity=static_cast<std::uint32_t>(i+1);input[i].muzzle.origin.x=31;}
    host.remote_weapon_poses(input);
    CHECK(probe->socket_samples==32);CHECK(probe->last_socket.entity==32);
    input[31].muzzle.origin.x=99;CHECK(probe->last_socket.muzzle.origin.x==31);
    host.reset({4,10,2});CHECK(probe->socket_samples==0);
    host.remote_weapon_poses(input);CHECK(probe->socket_samples==32);
    host.teardown();host.remote_weapon_poses(input);CHECK(probe->socket_samples==0);
    CHECK(host.remote_weapon_socket_plan({19,{7,11},"project_owned/alternate",model}).source==api::RemoteSocketSource::unavailable);
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

TEST_CASE("E30 neutral surface decal dependencies and material defaults contain no implicit concrete game",
          "[e30][surface-decal][game-api][core-only]") {
    api::DecalAssetRequest request;
    CHECK(request.material_mode==api::LocalDecalMaterialMode::lit_straight_alpha);
    CHECK(request.interpretation==api::DecalImageInterpretation::masked_palette_color);
    CHECK(std::ranges::all_of(request.wad_name,[](char c){return c=='\0';}));
    CHECK(std::ranges::all_of(request.texture_name,[](char c){return c=='\0';}));
    api::TemporarySurfaceDecalIntent intent;
    CHECK(intent.asset_slot==0);CHECK(intent.server_index==0);CHECK(intent.target_entity==0);
    CHECK_FALSE(intent.target_model_reference);CHECK_FALSE(intent.target_pose);
    const auto probe=std::make_shared<TestModuleProbe>();
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};
    host.reset({3,5,2});CHECK(host.surface_decal_preparation().empty());
    probe->surface_decal_test=true;const auto prepared=host.surface_decal_preparation();REQUIRE(prepared.size()==1);
    CHECK(std::string_view{prepared[0].wad_name.data()}=="owned_marks.wad");
    CHECK(std::string_view{prepared[0].texture_name.data()}=="own_mark");
    CHECK(prepared[0].material_mode==api::LocalDecalMaterialMode::lit_straight_alpha);
    const auto address=prepared.data();host.cancel_uncommitted();CHECK(host.surface_decal_preparation().data()==address);
    host.reset({4,6,2});CHECK(host.surface_decal_preparation().data()==address);
    host.teardown();CHECK(host.surface_decal_preparation().empty());
}
TEST_CASE("E30 alternate module owns explicit target pose and decal output through the production clock seam",
          "[e30][surface-decal][game-api][core-only][clock]") {
    const auto probe=std::make_shared<TestModuleProbe>();probe->surface_decal_test=true;
    api::GameClientHost host{std::make_unique<TestGameClientModule>(probe)};
    host.reset({3,5,2});host.set_movement_audio_time_origin(1000);
    client::CommittedTemporaryEffect event;event.generation=3;event.record=811;event.record_ordinal=7;
    event.kind=client::TemporaryEffectKind::decal;event.received_at_seconds=1001.25;
    event.message_bit_offset=56;event.entry_ordinal=3;event.origin={17,19,23};
    event.decal_index=4000;event.decal_entity=77;event.decal_model_reference=29;
    event.decal_target_pose=client::CommittedDecalTargetPose{29,{5,7,11},{13,17,19}};
    host.committed_temporary_effect(event);REQUIRE(probe->temporary_effects==1);
    CHECK(probe->temporary_seconds==1.25);CHECK(probe->temporary_observation.decal_index==4000);
    REQUIRE(probe->temporary_observation.decal_target_pose);CHECK(probe->temporary_observation.decal_target_pose->origin.y==7);
    event.origin={};event.decal_target_pose.reset();event.decal_model_reference.reset();
    host.cancel_uncommitted();const auto output=host.drain_temporary_effects(1.25);REQUIRE(output.count==1);
    const auto& result=output.effects[0];REQUIRE(result.surface_decal);CHECK_FALSE(result.light);
    CHECK(result.origin.x==17);CHECK(result.origin.y==19);CHECK(result.origin.z==23);CHECK(result.starts_at_seconds==1.25);
    CHECK(result.identity.record==811);CHECK(result.identity.message_bit_offset==56);CHECK(result.identity.entry_ordinal==3);
    CHECK(result.surface_decal->asset_slot==0);CHECK(result.surface_decal->server_index==4000);
    CHECK(result.surface_decal->target_entity==77);CHECK(result.surface_decal->target_model_reference==29);
    REQUIRE(result.surface_decal->target_pose);CHECK(result.surface_decal->target_pose->model_slot==29);
    CHECK(result.surface_decal->target_pose->origin.x==5);CHECK(result.surface_decal->target_pose->origin.y==7);
    CHECK(result.surface_decal->target_pose->angles_degrees.z==19);
    CHECK(host.drain_temporary_effects(1.25).count==0);CHECK(host.drain_audio().count==0);
    host.reset({4,6,2});host.committed_temporary_effect(event);CHECK(host.drain_temporary_effects(1.25).count==0);
    CHECK(probe->temporary_effects==1);event.generation=4;event.record=812;host.committed_temporary_effect(event);
    REQUIRE(host.drain_temporary_effects(1.25).count==1);CHECK(probe->temporary_seconds==1.25);
    host.committed_temporary_effect(event);host.reset({4,7,2});CHECK(host.drain_temporary_effects(1.25).count==0);
    host.teardown();host.committed_temporary_effect(event);CHECK(host.drain_temporary_effects(1.25).count==0);
}
TEST_CASE("E30 alternate module surface decal replay is transactional without a concrete game dependency",
          "[e30][surface-decal][game-api][core-only][runtime-replay]") {
    namespace goldsrc=hlclient::goldsrc;
    const auto probe=std::make_shared<TestModuleProbe>();probe->surface_decal_test=true;
    auto host=std::make_shared<api::GameClientHost>(std::make_unique<TestGameClientModule>(probe));
    host->set_movement_audio_time_origin(1000);auto fixture=goldsrc::make_runtime_replay_fixture({});REQUIRE(fixture);
    auto init=fixture.fixture->initialization;init.game_client=host;
    client::ClientWorldState world;auto session=goldsrc::RuntimeReplaySession::initialize(init,world);REQUIRE(session);
    auto incoming=fixture.fixture->records.front();incoming.payload.bytes={std::byte{23},std::byte{13},
        std::byte{8},std::byte{0},std::byte{16},std::byte{0},std::byte{24},std::byte{0},
        std::byte{160},std::byte{15},std::byte{0},std::byte{0}}; // Named-index4000 world placement, not enum.
    auto cursor=goldsrc::StockRuntimeSourceCursor::create(0,0,incoming.payload.bytes.size());REQUIRE(cursor);incoming.initial_cursor=*cursor;
    incoming.received_at=goldsrc::SoundTime{}+std::chrono::milliseconds{1001250};
    auto malformed=incoming;malformed.payload.bytes.push_back(std::byte{255});
    cursor=goldsrc::StockRuntimeSourceCursor::create(0,0,malformed.payload.bytes.size());REQUIRE(cursor);malformed.initial_cursor=*cursor;
    const auto previous=world.runtime_observation();CHECK_FALSE(session.session->apply_record(malformed));
    CHECK(world.runtime_observation()==previous);CHECK(probe->temporary_effects==0);CHECK(host->drain_temporary_effects(1.25).count==0);
    REQUIRE(session.session->apply_record(incoming));CHECK(probe->temporary_effects==1);const auto batch=host->drain_temporary_effects(1.25);
    REQUIRE(batch.count==1);REQUIRE(batch.effects[0].surface_decal);CHECK(batch.effects[0].surface_decal->server_index==4000);
    CHECK(batch.effects[0].surface_decal->target_entity==0);CHECK_FALSE(batch.effects[0].surface_decal->target_pose);
    CHECK(batch.effects[0].origin.x==1);CHECK(batch.effects[0].origin.y==2);CHECK(batch.effects[0].origin.z==3);
    CHECK(batch.effects[0].starts_at_seconds==1.25);CHECK_FALSE(session.session->apply_record(incoming));
    CHECK(probe->temporary_effects==1);CHECK(host->drain_temporary_effects(1.25).count==0);
    session.session->finish();
    // Finishing replay does not own the shared application host's teardown.
    // Verify both lifecycle boundaries, instead of assuming finish deactivates it.
    CHECK(host->active());CHECK(host->surface_decal_preparation().size()==1);
    host->teardown();CHECK_FALSE(host->active());CHECK(host->surface_decal_preparation().empty());
}
