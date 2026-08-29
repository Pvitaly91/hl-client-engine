#include <hlclient/prediction/prediction_history.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace hlclient::prediction {
namespace {

[[nodiscard]] PredictionError failure(
    const PredictionErrorCode code,
    const std::string_view context,
    const std::optional<goldsrc::GoldSrcUserCmdSequence> sequence =
        std::nullopt) noexcept
{
    return PredictionError{code, sequence, context};
}

[[nodiscard]] bool checked_add(
    std::size_t& destination,
    const std::size_t value) noexcept
{
    if (destination > (std::numeric_limits<std::size_t>::max)() - value) {
        return false;
    }
    destination += value;
    return true;
}

[[nodiscard]] bool checked_add(
    std::uint64_t& destination,
    const std::uint64_t value) noexcept
{
    if (destination > UINT64_MAX - value) {
        return false;
    }
    destination += value;
    return true;
}

void hash_byte(std::uint64_t& hash, const std::uint8_t value) noexcept
{
    hash ^= value;
    hash *= 1'099'511'628'211ULL;
}

template<class Value>
void hash_value(std::uint64_t& hash, const Value value) noexcept
{
    static_assert(std::is_trivially_copyable_v<Value>);
    const auto bytes = std::bit_cast<std::array<std::byte, sizeof(Value)>>(value);
    for (const auto byte : bytes) {
        hash_byte(hash, std::to_integer<std::uint8_t>(byte));
    }
}

constexpr std::uint64_t kHistoryFirstOffset =
    14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kHistoryFirstPrime = 1'099'511'628'211ULL;
constexpr std::uint64_t kHistorySecondOffset =
    7'806'984'959'868'165'187ULL;
constexpr std::uint64_t kHistorySecondPrime =
    14'029'467'366'897'019'727ULL;

class HistorySignatureHasher final {
public:
    void add(const bool value) noexcept
    {
        add_byte(value ? 1U : 0U);
    }

    template<class Integer>
        requires std::is_integral_v<Integer> &&
            (!std::is_same_v<std::remove_cv_t<Integer>, bool>)
    void add(const Integer value) noexcept
    {
        using Unsigned = std::make_unsigned_t<Integer>;
        auto remaining = static_cast<Unsigned>(value);
        for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
            add_byte(static_cast<std::uint8_t>(remaining &
                static_cast<Unsigned>(0xFFU)));
            remaining >>= 8U;
        }
    }

    template<class Enumeration>
        requires std::is_enum_v<Enumeration>
    void add(const Enumeration value) noexcept
    {
        add(static_cast<std::underlying_type_t<Enumeration>>(value));
    }

    void add(const float value) noexcept
    {
        add(std::bit_cast<std::uint32_t>(value));
    }

    void add(const double value) noexcept
    {
        add(std::bit_cast<std::uint64_t>(value));
    }

    [[nodiscard]] std::uint64_t result() const noexcept
    {
        auto mixed = first_ ^ std::rotl(second_, 29);
        mixed ^= mixed >> 30U;
        mixed *= 0xBF58476D1CE4E5B9ULL;
        mixed ^= mixed >> 27U;
        mixed *= 0x94D049BB133111EBULL;
        mixed ^= mixed >> 31U;
        return mixed == 0U ? kHistoryFirstOffset : mixed;
    }

private:
    void add_byte(const std::uint8_t value) noexcept
    {
        first_ ^= value;
        first_ *= kHistoryFirstPrime;
        second_ ^= value;
        second_ *= kHistorySecondPrime;
    }

    std::uint64_t first_{kHistoryFirstOffset};
    std::uint64_t second_{kHistorySecondOffset};
};

void add_optional_sequence(HistorySignatureHasher& hash,
    const std::optional<goldsrc::GoldSrcUserCmdSequence>& sequence) noexcept
{
    hash.add(sequence.has_value());
    hash.add(sequence ? sequence->value() : 0U);
}

void add_optional_u64(HistorySignatureHasher& hash,
    const std::optional<std::uint64_t> value) noexcept
{
    hash.add(value.has_value());
    hash.add(value.value_or(0U));
}

void add_session(HistorySignatureHasher& hash,
    const PredictionSessionIdentity& session) noexcept
{
    hash.add(0x53455353U); // SESS
    hash.add(session.session_generation);
    hash.add(session.prediction_generation);
    hash.add(session.collision_world_primary);
    hash.add(session.collision_world_secondary);
    hash.add(session.collision_world_revision);
    hash.add(session.collision_scene_signature);
    hash.add(session.collision_profile);
    hash.add(session.movement_environment_signature);
    hash.add(session.movement_config_signature);
    hash.add(session.spawn_initial_state_signature);
    hash.add(session.command_profile);
    hash.add(session.prediction_profile);
    hash.add(session.acknowledgement_profile);
}

void add_authority_identity(HistorySignatureHasher& hash,
    const AuthoritativePlayerUpdateIdentity& identity) noexcept
{
    hash.add(0x41555448U); // AUTH
    add_session(hash, identity.session());
    hash.add(identity.update_ordinal());
    add_optional_sequence(hash, identity.acknowledgement().sequence());
    hash.add(identity.synthetic_authority_time_nanoseconds());
    hash.add(identity.discontinuity());
}

void add_command(HistorySignatureHasher& hash,
    const goldsrc::GoldSrcUserCmdState& command) noexcept
{
    hash.add(0x434D4431U); // CMD1
    hash.add(command.lerp_msec());
    hash.add(command.msec());
    hash.add(static_cast<std::uint64_t>(command.view_angles().size()));
    for (const auto value : command.view_angles()) {
        hash.add(value);
    }
    hash.add(command.forward_move());
    hash.add(command.side_move());
    hash.add(command.up_move());
    hash.add(command.light_level());
    hash.add(command.buttons());
    hash.add(command.impulse());
    hash.add(command.weapon_select());
    hash.add(command.impact_index());
    hash.add(static_cast<std::uint64_t>(command.impact_position().size()));
    for (const auto value : command.impact_position()) {
        hash.add(value);
    }
    hash.add(command.command_sequence().value());
    hash.add(command.compatibility_profile());
    hash.add(command.input_mapping_profile());
    hash.add(command.schema_binding_profile());
    hash.add(command.source_input_sequence());
    hash.add(command.sample_time_nanoseconds());
    hash.add(command.sample_duration_nanoseconds());
}

void add_movement_statistics(HistorySignatureHasher& hash,
    const movement::PlayerMovementStatistics& statistics) noexcept
{
    hash.add(0x53544154U); // STAT
    hash.add(statistics.command_count);
    hash.add(statistics.substep_count);
    hash.add(statistics.grounded_command_count);
    hash.add(statistics.airborne_command_count);
    hash.add(statistics.ground_probe_count);
    hash.add(statistics.trace_count);
    hash.add(statistics.collision_hit_count);
    hash.add(statistics.slide_bump_count);
    hash.add(statistics.clip_plane_count);
    hash.add(statistics.step_attempt_count);
    hash.add(statistics.step_success_count);
    hash.add(statistics.jump_count);
    hash.add(statistics.duck_enter_count);
    hash.add(statistics.duck_exit_count);
    hash.add(statistics.stand_blocked_count);
    hash.add(statistics.start_solid_count);
    hash.add(statistics.all_solid_count);
    hash.add(statistics.total_horizontal_distance);
    hash.add(statistics.total_vertical_distance);
}

void add_touch_summary(HistorySignatureHasher& hash,
    const PredictionTouchSummary& summary) noexcept
{
    hash.add(0x544F5543U); // TOUC
    hash.add(static_cast<std::uint64_t>(summary.touch_count));
    hash.add(summary.first_hit_kind.has_value());
    if (summary.first_hit_kind) {
        hash.add(*summary.first_hit_kind);
    }
    hash.add(summary.last_hit_kind.has_value());
    if (summary.last_hit_kind) {
        hash.add(*summary.last_hit_kind);
    }
    hash.add(summary.start_solid);
    hash.add(summary.all_solid);
    hash.add(summary.deterministic_signature);
    hash.add(static_cast<std::uint64_t>(summary.accounted_bytes));
}

void add_history_limits(HistorySignatureHasher& hash,
    const LocalPredictionHistoryLimits& limits) noexcept
{
    hash.add(0x4C494D54U); // LIMT
    hash.add(static_cast<std::uint64_t>(limits.maximum_entries));
    hash.add(static_cast<std::uint64_t>(limits.maximum_retained_state_bytes));
    hash.add(static_cast<std::uint64_t>(limits.maximum_retained_command_bytes));
    hash.add(static_cast<std::uint64_t>(
        limits.maximum_authority_delay_commands));
    hash.add(static_cast<std::uint64_t>(limits.maximum_replay_commands));
    hash.add(limits.maximum_history_revision);
    hash.add(static_cast<std::uint64_t>(limits.maximum_touch_summary_bytes));
}

void add_history_statistics(HistorySignatureHasher& hash,
    const LocalPredictionHistoryStatistics& statistics) noexcept
{
    hash.add(0x48535441U); // HSTA
    hash.add(statistics.total_appended_commands);
    hash.add(statistics.total_acknowledged_commands);
    hash.add(statistics.total_replayed_commands);
    hash.add(statistics.publication_count);
    hash.add(static_cast<std::uint64_t>(statistics.high_water_mark));
}

[[nodiscard]] std::uint32_t expected_next_sequence(
    const LocalPredictionHistoryState& history) noexcept
{
    if (const auto newest = history.newest_command_sequence()) {
        return newest->value() == UINT32_MAX ? 0U : newest->value() + 1U;
    }
    if (const auto anchor = history.anchor().acknowledgement().sequence()) {
        return anchor->value() == UINT32_MAX ? 0U : anchor->value() + 1U;
    }
    return 1U;
}

} // namespace

PredictionHistoryAnchor::PredictionHistoryAnchor(
    std::shared_ptr<const movement::LocalPlayerMovementState> movement_state,
    std::optional<AuthoritativePlayerUpdateIdentity> authority_update_identity,
    const std::optional<std::uint64_t> authority_state_signature,
    PredictionSessionIdentity session) noexcept
    : movement_state_{std::move(movement_state)},
      authority_update_identity_{std::move(authority_update_identity)},
      authority_state_signature_{authority_state_signature},
      state_signature_{movement_state_
              ? movement::local_player_movement_state_signature(*movement_state_)
              : 0U},
      session_{authority_update_identity_
              ? authority_update_identity_->session()
              : session}
{
}

const std::shared_ptr<const movement::LocalPlayerMovementState>&
PredictionHistoryAnchor::movement_state() const noexcept
{
    return movement_state_;
}

const AuthoritativeCommandAcknowledgement&
PredictionHistoryAnchor::acknowledgement() const noexcept
{
    static constexpr auto no_acknowledgement =
        AuthoritativeCommandAcknowledgement::none();
    return authority_update_identity_
        ? authority_update_identity_->acknowledgement()
        : no_acknowledgement;
}

const std::optional<AuthoritativePlayerUpdateIdentity>&
PredictionHistoryAnchor::authority_update_identity() const noexcept
{
    return authority_update_identity_;
}

std::optional<std::uint64_t>
PredictionHistoryAnchor::authority_update_ordinal() const noexcept
{
    return authority_update_identity_
        ? std::optional{authority_update_identity_->update_ordinal()}
        : std::nullopt;
}

std::optional<std::uint64_t>
PredictionHistoryAnchor::authority_state_signature() const noexcept
{
    return authority_state_signature_;
}

std::uint64_t PredictionHistoryAnchor::state_signature() const noexcept
{
    return state_signature_;
}

const PredictionSessionIdentity& PredictionHistoryAnchor::session()
    const noexcept
{
    return session_;
}

PredictedCommandEntry::PredictedCommandEntry(
    std::shared_ptr<const goldsrc::GoldSrcUserCmdState> command,
    std::shared_ptr<const movement::LocalPlayerMovementState> pre_command_state,
    std::shared_ptr<const movement::LocalPlayerMovementState> post_command_state,
    movement::PlayerMovementStatistics simulation_statistics,
    PredictionTouchSummary touch_summary,
    const std::uint64_t prediction_generation,
    const std::uint64_t entry_ordinal) noexcept
    : command_{std::move(command)},
      pre_command_state_{std::move(pre_command_state)},
      post_command_state_{std::move(post_command_state)},
      pre_state_signature_{pre_command_state_
              ? movement::local_player_movement_state_signature(
                    *pre_command_state_)
              : 0U},
      post_state_signature_{post_command_state_
              ? movement::local_player_movement_state_signature(
                    *post_command_state_)
              : 0U},
      simulation_statistics_{simulation_statistics},
      touch_summary_{touch_summary},
      prediction_generation_{prediction_generation},
      entry_ordinal_{entry_ordinal}
{
}

const std::shared_ptr<const goldsrc::GoldSrcUserCmdState>&
PredictedCommandEntry::command() const noexcept { return command_; }
goldsrc::GoldSrcUserCmdSequence PredictedCommandEntry::command_sequence()
    const noexcept { return command_->command_sequence(); }
const std::shared_ptr<const movement::LocalPlayerMovementState>&
PredictedCommandEntry::pre_command_state() const noexcept
{ return pre_command_state_; }
const std::shared_ptr<const movement::LocalPlayerMovementState>&
PredictedCommandEntry::post_command_state() const noexcept
{ return post_command_state_; }
std::uint64_t PredictedCommandEntry::pre_state_signature() const noexcept
{ return pre_state_signature_; }
std::uint64_t PredictedCommandEntry::post_state_signature() const noexcept
{ return post_state_signature_; }
const movement::PlayerMovementStatistics&
PredictedCommandEntry::simulation_statistics() const noexcept
{ return simulation_statistics_; }
const PredictionTouchSummary& PredictedCommandEntry::touch_summary()
    const noexcept { return touch_summary_; }
std::uint64_t PredictedCommandEntry::prediction_generation() const noexcept
{ return prediction_generation_; }
std::uint64_t PredictedCommandEntry::entry_ordinal() const noexcept
{ return entry_ordinal_; }

LocalPredictionHistoryState::LocalPredictionHistoryState(
    PredictionHistoryAnchor anchor,
    std::vector<PredictedCommandEntry> entries,
    std::shared_ptr<const movement::LocalPlayerMovementState> current_state,
    const std::uint64_t revision,
    const std::size_t accounted_state_bytes,
    const std::size_t accounted_command_bytes,
    const std::size_t accounted_touch_summary_bytes,
    LocalPredictionHistoryLimits limits,
    LocalPredictionHistoryStatistics statistics) noexcept
    : anchor_{std::move(anchor)},
      entries_{std::move(entries)},
      current_state_{std::move(current_state)},
      revision_{revision},
      accounted_state_bytes_{accounted_state_bytes},
      accounted_command_bytes_{accounted_command_bytes},
      accounted_touch_summary_bytes_{accounted_touch_summary_bytes},
      limits_{limits},
      statistics_{statistics}
{
}

LocalPredictionHistoryState::CreateResult
LocalPredictionHistoryState::create_initial(
    movement::LocalPlayerMovementState initial_state,
    PredictionSessionIdentity session,
    const LocalPredictionHistoryLimits& limits)
{
    if (!session.valid() || !valid_local_prediction_history_limits(limits)) {
        return {nullptr,
            failure(
                !session.valid() ? PredictionErrorCode::invalid_session_identity
                                 : PredictionErrorCode::invalid_configuration,
                !session.valid() ? "initial prediction session is invalid"
                                 : "prediction-history limits are invalid")};
    }
    if (initial_state.source_command_sequence() != 0U ||
        initial_state.command_profile() != session.command_profile ||
        movement::local_player_movement_state_signature(initial_state) !=
            session.spawn_initial_state_signature) {
        return {nullptr,
            failure(
                PredictionErrorCode::invalid_session_identity,
                "initial movement state does not match the prediction session")};
    }
    try {
        auto state = std::make_shared<const movement::LocalPlayerMovementState>(
            std::move(initial_state));
        PredictionHistoryAnchor anchor{state, std::nullopt, std::nullopt,
            session};
        LocalPredictionHistoryStatistics statistics;
        statistics.publication_count = 1U;
        auto history = std::shared_ptr<const LocalPredictionHistoryState>{
            new LocalPredictionHistoryState{std::move(anchor), {}, state, 1U,
                sizeof(movement::LocalPlayerMovementState), 0U, 0U, limits,
                statistics}};
        return {std::move(history), std::nullopt};
    } catch (const std::bad_alloc&) {
        return {nullptr,
            failure(
                PredictionErrorCode::allocation_failed,
                "initial prediction history allocation failed")};
    }
}

const PredictionHistoryAnchor& LocalPredictionHistoryState::anchor()
    const noexcept { return anchor_; }
std::span<const PredictedCommandEntry> LocalPredictionHistoryState::entries()
    const noexcept { return entries_; }
std::size_t LocalPredictionHistoryState::size() const noexcept
{ return entries_.size(); }

const PredictedCommandEntry* LocalPredictionHistoryState::find_exact(
    const goldsrc::GoldSrcUserCmdSequence sequence) const noexcept
{
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), sequence.value(),
        [](const PredictedCommandEntry& entry, const std::uint32_t value) {
            return entry.command_sequence().value() < value;
        });
    return found != entries_.end() &&
            found->command_sequence().value() == sequence.value()
        ? &*found
        : nullptr;
}

const std::shared_ptr<const movement::LocalPlayerMovementState>&
LocalPredictionHistoryState::current_predicted_state() const noexcept
{ return current_state_; }

std::optional<goldsrc::GoldSrcUserCmdSequence>
LocalPredictionHistoryState::oldest_command_sequence() const noexcept
{
    return entries_.empty()
        ? std::nullopt
        : std::optional{entries_.front().command_sequence()};
}

std::optional<goldsrc::GoldSrcUserCmdSequence>
LocalPredictionHistoryState::newest_command_sequence() const noexcept
{
    return entries_.empty()
        ? std::nullopt
        : std::optional{entries_.back().command_sequence()};
}

std::uint64_t LocalPredictionHistoryState::revision() const noexcept
{ return revision_; }
std::size_t LocalPredictionHistoryState::accounted_state_bytes() const noexcept
{ return accounted_state_bytes_; }
std::size_t LocalPredictionHistoryState::accounted_command_bytes() const noexcept
{ return accounted_command_bytes_; }
std::size_t LocalPredictionHistoryState::accounted_touch_summary_bytes()
    const noexcept { return accounted_touch_summary_bytes_; }
const LocalPredictionHistoryLimits& LocalPredictionHistoryState::limits()
    const noexcept { return limits_; }
const PredictionSessionIdentity& LocalPredictionHistoryState::session()
    const noexcept { return anchor_.session(); }
const LocalPredictionHistoryStatistics& LocalPredictionHistoryState::statistics()
    const noexcept { return statistics_; }

LocalPredictionAppendResult append_local_prediction_commands(
    const LocalPredictionHistoryState& history,
    const std::span<const PredictedCommandAppend> commands)
{
    LocalPredictionAppendResult result;
    result.final_predicted_state = history.current_predicted_state();
    result.history_size = history.size();
    result.prediction_revision = history.revision();
    if (commands.empty()) {
        try {
            result.history = std::shared_ptr<const LocalPredictionHistoryState>{
                new LocalPredictionHistoryState{history}};
        } catch (const std::bad_alloc&) {
            result.error = failure(
                PredictionErrorCode::allocation_failed,
                "prediction history publication failed");
        }
        return result;
    }
    if (history.size() > history.limits().maximum_entries -
            (std::min)(commands.size(), history.limits().maximum_entries)) {
        result.error = failure(
            PredictionErrorCode::prediction_history_backpressure,
            "unacknowledged prediction history reached capacity");
        return result;
    }
    if (history.size() + commands.size() >
        history.limits().maximum_authority_delay_commands) {
        result.error = failure(
            PredictionErrorCode::prediction_history_backpressure,
            "authority delay reached the configured command bound");
        return result;
    }
    if (history.revision() >= history.limits().maximum_history_revision) {
        result.error = failure(
            PredictionErrorCode::revision_exhausted,
            "prediction history revision is exhausted");
        return result;
    }

    try {
        auto entries = std::vector<PredictedCommandEntry>{};
        entries.reserve(history.size() + commands.size());
        for (const auto& entry : history.entries()) {
            entries.emplace_back(entry);
        }
        auto current_state = history.current_predicted_state();
        auto expected = expected_next_sequence(history);
        auto entry_ordinal = history.statistics().total_appended_commands;
        auto state_bytes = history.accounted_state_bytes();
        auto command_bytes = history.accounted_command_bytes();
        auto touch_bytes = history.accounted_touch_summary_bytes();
        for (const auto& append : commands) {
            if (!append.command || !append.pre_command_state ||
                !append.post_command_state || expected == 0U) {
                result.error = failure(
                    PredictionErrorCode::invalid_configuration,
                    "prediction append contains an invalid immutable value");
                return result;
            }
            const auto sequence = append.command->command_sequence();
            if (sequence.value() < expected) {
                result.error = failure(
                    sequence.value() + 1U == expected
                        ? PredictionErrorCode::duplicate_predicted_command
                        : PredictionErrorCode::out_of_order_predicted_command,
                    "prediction command is duplicate or out of order", sequence);
                return result;
            }
            if (sequence.value() > expected) {
                result.error = failure(
                    PredictionErrorCode::prediction_command_gap,
                    "prediction command sequence contains a gap", sequence);
                return result;
            }
            if (append.command->compatibility_profile() !=
                    goldsrc::GoldSrcUserCmdCompatibilityProfile::
                        synthetic_usercmd_v1 ||
                append.pre_command_state->command_profile() !=
                    history.session().command_profile ||
                append.post_command_state->command_profile() !=
                    history.session().command_profile ||
                append.pre_command_state->source_command_sequence() !=
                    current_state->source_command_sequence() ||
                movement::local_player_movement_state_signature(
                    *append.pre_command_state) !=
                    movement::local_player_movement_state_signature(
                        *current_state) ||
                append.post_command_state->source_command_sequence() !=
                    sequence.value()) {
                result.error = failure(
                    PredictionErrorCode::prediction_command_gap,
                    "prediction command pre/post state continuity is invalid",
                    sequence);
                return result;
            }
            if (!checked_add(state_bytes,
                    sizeof(movement::LocalPlayerMovementState) * 2U) ||
                !checked_add(command_bytes, sizeof(goldsrc::GoldSrcUserCmdState)) ||
                !checked_add(touch_bytes, append.touch_summary.accounted_bytes) ||
                state_bytes > history.limits().maximum_retained_state_bytes ||
                command_bytes > history.limits().maximum_retained_command_bytes ||
                touch_bytes > history.limits().maximum_touch_summary_bytes) {
                result.error = failure(
                    PredictionErrorCode::prediction_history_full,
                    "prediction history byte limit was reached", sequence);
                return result;
            }
            if (entry_ordinal == UINT64_MAX) {
                result.error = failure(
                    PredictionErrorCode::revision_exhausted,
                    "prediction entry ordinal is exhausted", sequence);
                return result;
            }
            ++entry_ordinal;
            entries.push_back(PredictedCommandEntry{
                append.command, append.pre_command_state,
                append.post_command_state, append.simulation_statistics,
                append.touch_summary, history.session().prediction_generation,
                entry_ordinal});
            current_state = append.post_command_state;
            expected = sequence.value() == UINT32_MAX
                ? 0U
                : sequence.value() + 1U;
        }
        auto statistics = history.statistics();
        if (!checked_add(
                statistics.total_appended_commands,
                static_cast<std::uint64_t>(commands.size())) ||
            statistics.publication_count == UINT64_MAX) {
            result.error = failure(
                PredictionErrorCode::revision_exhausted,
                "prediction statistics are exhausted");
            return result;
        }
        ++statistics.publication_count;
        statistics.high_water_mark =
            (std::max)(statistics.high_water_mark, entries.size());
        auto next = std::shared_ptr<const LocalPredictionHistoryState>{
            new LocalPredictionHistoryState{history.anchor(), std::move(entries),
                current_state, history.revision() + 1U, state_bytes,
                command_bytes, touch_bytes, history.limits(), statistics}};
        result.history = std::move(next);
        result.final_predicted_state = current_state;
        result.appended_command_count = commands.size();
        result.history_size = result.history->size();
        result.prediction_revision = result.history->revision();
        return result;
    } catch (const std::bad_alloc&) {
        result.error = failure(
            PredictionErrorCode::allocation_failed,
            "prediction history append allocation failed");
        return result;
    }
}

PredictionTouchSummary summarize_prediction_touches(
    const std::span<const movement::PlayerMovementTouch> touches,
    const bool start_solid,
    const bool all_solid) noexcept
{
    PredictionTouchSummary summary;
    summary.touch_count = touches.size();
    summary.start_solid = start_solid;
    summary.all_solid = all_solid;
    summary.accounted_bytes = sizeof(PredictionTouchSummary);
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    hash_value(hash, touches.size());
    hash_value(hash, start_solid);
    hash_value(hash, all_solid);
    if (!touches.empty()) {
        summary.first_hit_kind = touches.front().hit.kind;
        summary.last_hit_kind = touches.back().hit.kind;
        for (const auto& touch : touches) {
            hash_value(hash, touch.hit.kind);
            hash_value(hash, touch.hit.source_model_index);
            hash_value(hash, touch.fraction);
            hash_value(hash, touch.phase);
            hash_value(hash, touch.source_command_sequence);
        }
    }
    summary.deterministic_signature = hash;
    return summary;
}

std::uint64_t local_prediction_history_signature(
    const LocalPredictionHistoryState& history) noexcept
{
    HistorySignatureHasher hash;
    hash.add(0x48535432U); // HST2
    add_session(hash, history.session());

    const auto& anchor = history.anchor();
    hash.add(0x414E4348U); // ANCH
    hash.add(anchor.movement_state() != nullptr);
    hash.add(anchor.state_signature());
    hash.add(anchor.authority_update_identity().has_value());
    if (anchor.authority_update_identity()) {
        add_authority_identity(hash, *anchor.authority_update_identity());
    }
    add_optional_u64(hash, anchor.authority_state_signature());
    add_session(hash, anchor.session());

    hash.add(0x5055424CU); // PUBL
    hash.add(history.revision());
    hash.add(static_cast<std::uint64_t>(history.size()));
    hash.add(static_cast<std::uint64_t>(history.accounted_state_bytes()));
    hash.add(static_cast<std::uint64_t>(history.accounted_command_bytes()));
    hash.add(static_cast<std::uint64_t>(
        history.accounted_touch_summary_bytes()));
    add_history_limits(hash, history.limits());
    add_history_statistics(hash, history.statistics());
    hash.add(history.current_predicted_state() != nullptr);
    hash.add(history.current_predicted_state()
            ? movement::local_player_movement_state_signature(
                  *history.current_predicted_state())
            : 0U);
    add_optional_sequence(hash, history.oldest_command_sequence());
    add_optional_sequence(hash, history.newest_command_sequence());

    hash.add(0x454E5452U); // ENTR
    hash.add(static_cast<std::uint64_t>(history.entries().size()));
    for (const auto& entry : history.entries()) {
        hash.add(0x454E5431U); // ENT1
        hash.add(entry.entry_ordinal());
        hash.add(entry.prediction_generation());
        hash.add(entry.command() != nullptr);
        if (entry.command()) {
            add_command(hash, *entry.command());
        }
        hash.add(entry.pre_command_state() != nullptr);
        hash.add(entry.pre_state_signature());
        hash.add(entry.pre_command_state()
                ? movement::local_player_movement_state_signature(
                      *entry.pre_command_state())
                : 0U);
        hash.add(entry.post_command_state() != nullptr);
        hash.add(entry.post_state_signature());
        hash.add(entry.post_command_state()
                ? movement::local_player_movement_state_signature(
                      *entry.post_command_state())
                : 0U);
        add_movement_statistics(hash, entry.simulation_statistics());
        add_touch_summary(hash, entry.touch_summary());
    }
    return hash.result();
}

} // namespace hlclient::prediction
