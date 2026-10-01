#include <hlclient/game_api/game_client_host.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace hlclient::game_api {
GameClientHost::GameClientHost(std::unique_ptr<IGameClientModule> module)
    : module_(std::move(module)) {
    if (!module_) throw std::invalid_argument{"Game module is required"};
}
GameClientHost::~GameClientHost() { teardown(); }
void GameClientHost::reset(GameSessionIdentity identity) noexcept {
    ++audio_session_;
    // RuntimeReplaySession resets the game generation after the live loop has
    // established its steady-clock origin. That origin belongs to this host
    // lifetime, not to the network/map generation.
    module_->reset(identity); active_ = true;
}
void GameClientHost::teardown() noexcept {
    ++audio_session_;
    movement_audio_time_origin_.reset();
    if (active_) { module_->teardown(); active_ = false; }
}
const GameMovementPolicy& GameClientHost::movement_policy() const noexcept {
    return module_->movement_policy();
}
GameRecordResult GameClientHost::stage_record(const GameRecordInput& input) const {
    if (!active_) return {{}, std::string{"Game session is torn down"}};
    auto result = module_->stage_record(input);
    if (result.message_failure) {
        const auto& failure = *result.message_failure;
        if (failure.message_name.size() > 63U || failure.actual_body_size > 65'536U ||
            (failure.body_byte_offset && *failure.body_byte_offset >= failure.actual_body_size) ||
            std::none_of(input.messages.begin(), input.messages.end(),
                [&](const auto& message) {
                    return message.source == failure.source &&
                        message.registration_id == failure.registration_id &&
                        message.body.size() == failure.actual_body_size;
                }))
            return {{}, std::string{"Game failure metadata violates its input boundary"}};
    }
    if (!result) return result;
    if (result.state->pickups.size() > 64U)
        return {{}, std::string{"Game pickup event capacity exceeded"}};
    for (const auto& pickup : result.state->pickups) {
        if (pickup.kind > GameRecordState::Pickup::Kind::item ||
            pickup.item_token.size() > 63U ||
            pickup.source.record_identity == 0U ||
            pickup.source.end_bit_offset <= pickup.source.start_bit_offset ||
            std::none_of(input.messages.begin(), input.messages.end(),
                [&](const auto& message) { return message.source == pickup.source; }))
            return {{}, std::string{"Game pickup event has invalid bounds or provenance"}};
    }
    return result;
}
void GameClientHost::commit_record(GameRecordState&& state) noexcept {
    if (active_) module_->commit_record(std::move(state));
}
void GameClientHost::bind_model(std::optional<LocalWeaponModelMetadata> model) {
    if (active_) module_->bind_model(std::move(model));
}
void GameClientHost::observe(const client::RuntimeClientObservationState& state, double time) {
    if (active_) module_->observe(state, time);
}
void GameClientHost::submit(const LocalWeaponSubmittedCommand& command, double time) {
    if (active_) module_->submit(command, time);
}
void GameClientHost::cancel_uncommitted() noexcept {
    if (active_) module_->cancel_uncommitted();
}
LocalWeaponPresentationSnapshot GameClientHost::sample(double time) noexcept {
    return active_ ? module_->sample(time) : LocalWeaponPresentationSnapshot{};
}
LocalAudioBatch GameClientHost::drain_audio() noexcept {
    auto batch=active_ ? module_->drain_audio() : LocalAudioBatch{};
    batch.statistics.movement_observations=movement_audio_observations_;
    batch.statistics.movement_clock_dropped=movement_audio_clock_dropped_;
    batch.statistics.movement_invalid_time=movement_audio_invalid_time_;
    batch.session=audio_session_; return batch;
}
RemotePlayerPresentationPolicy GameClientHost::remote_player_policy() const noexcept {
    return active_ ? module_->remote_player_policy() : RemotePlayerPresentationPolicy{};
}
void GameClientHost::configure_scripted_events(std::span<const ScriptedEventBinding> bindings) noexcept {
    if(active_) module_->configure_scripted_events(bindings);
}
void GameClientHost::committed_scripted_event(const CommittedScriptedEvent& event) noexcept {
    if(!active_) return;
    auto relative=event;
    if(!movement_audio_time_origin_) relative.resolution=ScriptedEventResolution::missing_clock;
    else relative.received_at_seconds-=*movement_audio_time_origin_;
    if(!std::isfinite(relative.received_at_seconds) || relative.received_at_seconds<0)
        relative.resolution=ScriptedEventResolution::missing_clock;
    module_->committed_scripted_event(relative);
}
RemoteWeaponEffectsBatch GameClientHost::drain_remote_effects(double now) noexcept {
    return active_ ? module_->drain_remote_effects(now) : RemoteWeaponEffectsBatch{};
}
LocalAudioBatch GameClientHost::drain_remote_audio() noexcept {
    auto batch=active_ ? module_->drain_remote_audio() : LocalAudioBatch{};
    batch.session=audio_session_;
    return batch;
}
void GameClientHost::remote_world_impact(const LocalWeaponActionIdentity& action,
    assets::AssetVector3 point,double time,std::optional<LocalWorldSurfaceHit> surface) noexcept {
    if(active_) module_->remote_world_impact(action,point,time,std::move(surface));
}
void GameClientHost::remote_shell_contact(const LocalShellContact& contact) noexcept {
    if(active_) module_->remote_shell_contact(contact);
}
RemotePlayerPresentationIntent GameClientHost::remote_player(
    const RemotePlayerPresentationContext& input) noexcept {
    return active_ ? module_->remote_player(input) : RemotePlayerPresentationIntent{};
}
LocalVisualFrame GameClientHost::local_visuals(const LocalVisualContext& context) noexcept {
    return active_ ? module_->local_visuals(context) : LocalVisualFrame{};
}
std::optional<LocalImpactAssetProfile> GameClientHost::local_impact_assets() const noexcept {
    return active_ ? module_->local_impact_assets() : std::nullopt;
}
std::optional<LocalCrowbarImpactAssetProfile> GameClientHost::local_crowbar_impact_assets() const noexcept {
    return active_ ? module_->local_crowbar_impact_assets() : std::nullopt;
}
void GameClientHost::accepted_world_impact(const LocalWeaponActionIdentity& action,
    assets::AssetVector3 point, double time,std::optional<LocalWorldSurfaceHit> surface) noexcept {
    if (active_) module_->accepted_world_impact(action, point, time,std::move(surface));
}
bool GameClientHost::resolved_world_impact(const LocalWeaponActionIdentity& action,
    LocalWorldImpactOutcome outcome, std::optional<assets::AssetVector3> point,
    double time,std::optional<LocalWorldSurfaceHit> surface) noexcept {
    return active_ && module_->resolved_world_impact(action, outcome, point, time,std::move(surface));
}
std::optional<LocalWorldImpactDiagnostic>
GameClientHost::last_world_impact_diagnostic() const noexcept {
    return active_ ? module_->last_world_impact_diagnostic() : std::nullopt;
}
void GameClientHost::shell_contact(const LocalShellContact& contact) noexcept {
    if (active_) module_->shell_contact(contact);
}
void GameClientHost::configure_movement_materials(std::string_view text) noexcept {
    if (active_) module_->configure_movement_materials(text);
}
std::span<const LocalSoundReference> GameClientHost::movement_sound_preparation() const noexcept {
    return active_ ? module_->movement_sound_preparation() : std::span<const LocalSoundReference>{};
}
void GameClientHost::rewind_movement_audio(std::uint32_t boundary) noexcept {
    if (active_) module_->rewind_movement_audio(boundary);
}
void GameClientHost::observe_movement_audio(
    const MovementAudioObservation& observation, bool replay) noexcept {
    if (!active_) return;
    ++movement_audio_observations_;
    if (!movement_audio_time_origin_) {++movement_audio_clock_dropped_; return;}
    auto relative=observation;
    relative.scheduled_seconds-=*movement_audio_time_origin_;
    if (std::isfinite(relative.scheduled_seconds) && relative.scheduled_seconds>=0)
        module_->observe_movement_audio(relative,replay);
    else ++movement_audio_invalid_time_;
}
void GameClientHost::set_movement_audio_time_origin(double seconds) noexcept {
    if (std::isfinite(seconds) && seconds>=0) movement_audio_time_origin_=seconds;
}
ViewmodelIntent GameClientHost::viewmodel(const client::RuntimeClientObservationState& state,
    std::optional<LocalWeaponModelMetadata> model, double time,
    std::optional<LocalWeaponVisual> visual) {
    return active_ ? module_->viewmodel(state,std::move(model),time,visual) : ViewmodelIntent{};
}
HudState GameClientHost::hud(const client::RuntimeClientObservationState& state, double time) {
    return active_ ? module_->hud(state,time) : HudState{};
}
CameraIntent GameClientHost::camera(const client::RuntimeClientObservationState& state, double punch) const noexcept {
    return active_ ? module_->camera(state,punch) : CameraIntent{};
}
void GameClientHost::observe_action_evidence(const client::RuntimeClientObservationState& state,
    const std::optional<GameActionTraffic>& traffic) noexcept {
    if (active_) module_->observe_action_evidence(state,traffic);
}
GameActionEvidenceSnapshot GameClientHost::action_evidence() const noexcept {
    return active_ ? module_->action_evidence() : GameActionEvidenceSnapshot{};
}
std::optional<GameCommandRequest> GameClientHost::inventory_selection(
    const client::RuntimeClientObservationState& state, std::uint8_t id) const {
    return active_ ? module_->inventory_selection(state,id) : std::nullopt;
}
std::optional<std::uint8_t> GameClientHost::select_group(
    const client::RuntimeClientObservationState& state, std::uint8_t group,
    std::optional<std::uint8_t> current) const {
    return active_ ? module_->select_group(state,group,current) : std::nullopt;
}
std::optional<std::uint8_t> GameClientHost::cycle_inventory(
    const client::RuntimeClientObservationState& state,
    std::optional<std::uint8_t> current, int direction) const {
    return active_ ? module_->cycle_inventory(state,current,direction) : std::nullopt;
}
GameCommandRequest GameClientHost::self_kill_request() const {
    return active_ ? module_->self_kill_request() : GameCommandRequest{};
}
std::optional<std::uint8_t> GameClientHost::scenario_inventory_target(
    const client::RuntimeClientObservationState& state, std::size_t phase) const {
    return active_ ? module_->scenario_inventory_target(state,phase) : std::nullopt;
}
GameScenarioDirective GameClientHost::scenario_command(GameScenario mode,
    const client::RuntimeClientObservationState* state, std::size_t phase, double time, double phase_time) noexcept {
    return active_ ? module_->scenario_command(mode,state,phase,time,phase_time) : GameScenarioDirective{};
}
DamageRespawnScriptSnapshot GameClientHost::damage_respawn_snapshot() const noexcept {
    return module_->damage_respawn_snapshot();
}
}
