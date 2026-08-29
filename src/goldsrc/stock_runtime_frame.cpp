#include <hlclient/goldsrc/stock_runtime_frame.hpp>

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace hlclient::goldsrc {
namespace {

template<class Result>
[[nodiscard]] Result failure(
    const StockRuntimeFrameErrorCode code,
    const std::string_view context) noexcept
{
    return {std::nullopt, StockRuntimeFrameError{code, context}};
}

[[nodiscard]] bool pending_profile_tuple(
    const StockRuntimeCompatibilityProfile compatibility,
    const StockRuntimeEvidenceProfile evidence) noexcept
{
    return compatibility == StockRuntimeCompatibilityProfile::
               stock_protocol_48_build_10210_evidence_pending &&
        evidence == StockRuntimeEvidenceProfile::
            controlled_signed_stock_transcript_pending;
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (left > std::numeric_limits<std::size_t>::max() - right) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool valid_direction(const NetchanDirection direction) noexcept
{
    return direction == NetchanDirection::client_to_server ||
        direction == NetchanDirection::server_to_client;
}

[[nodiscard]] bool valid_message_category(
    const StockRuntimeMessageCategory category) noexcept
{
    switch (category) {
    case StockRuntimeMessageCategory::runtime_control_candidate:
    case StockRuntimeMessageCategory::runtime_time_candidate:
    case StockRuntimeMessageCategory::baseline_candidate:
    case StockRuntimeMessageCategory::entity_full_candidate:
    case StockRuntimeMessageCategory::entity_delta_candidate:
    case StockRuntimeMessageCategory::client_local_data_candidate:
    case StockRuntimeMessageCategory::command_ack_candidate:
    case StockRuntimeMessageCategory::unsupported_runtime_message:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_component_kind(
    const StockRuntimeComponentKind kind) noexcept
{
    switch (kind) {
    case StockRuntimeComponentKind::client_local_data:
    case StockRuntimeComponentKind::weapon_data:
    case StockRuntimeComponentKind::visual_projection:
        return true;
    }
    return false;
}

[[nodiscard]] std::size_t component_entry_limit(
    const StockRuntimeComponentKind kind) noexcept
{
    const auto& limits = hard_stock_runtime_decode_limits();
    switch (kind) {
    case StockRuntimeComponentKind::client_local_data:
        return limits.maximum_clientdata_fields;
    case StockRuntimeComponentKind::weapon_data:
        return limits.maximum_weapon_entries;
    case StockRuntimeComponentKind::visual_projection:
        return limits.maximum_entities;
    }
    return 0U;
}

[[nodiscard]] bool component_category_matches(
    const StockRuntimeComponentKind kind,
    const StockRuntimeMessageCategory category) noexcept
{
    switch (kind) {
    case StockRuntimeComponentKind::client_local_data:
    case StockRuntimeComponentKind::weapon_data:
        return category ==
            StockRuntimeMessageCategory::client_local_data_candidate;
    case StockRuntimeComponentKind::visual_projection:
        return category == StockRuntimeMessageCategory::entity_full_candidate ||
            category == StockRuntimeMessageCategory::entity_delta_candidate;
    }
    return false;
}

} // namespace

bool valid_stock_runtime_frame_limits(
    const StockRuntimeFrameLimits& limits) noexcept
{
    return limits.maximum_source_records > 0U &&
        limits.maximum_source_records <=
            kHardMaximumStockRuntimeFrameSources &&
        limits.maximum_component_references > 0U &&
        limits.maximum_component_references <=
            kHardMaximumStockRuntimeFrameComponents &&
        limits.maximum_accounted_metadata_bytes > 0U &&
        limits.maximum_accounted_metadata_bytes <= 64U * 1024U * 1024U;
}

bool valid_stock_runtime_frame_history_limits(
    const StockRuntimeFrameHistoryLimits& limits) noexcept
{
    return limits.maximum_frames > 0U &&
        limits.maximum_frames <= kHardMaximumStockRuntimeFrameHistory &&
        limits.maximum_accounted_metadata_bytes > 0U &&
        limits.maximum_accounted_metadata_bytes <= 256U * 1024U * 1024U &&
        limits.maximum_history_revision > 0U;
}

StockRuntimeFrameState::StockRuntimeFrameState(
    StockRuntimeFrameCreateInfo create_info,
    const std::size_t accounted_metadata_bytes,
    const StockRuntimeFrameStatus status) noexcept
    : runtime_generation_{create_info.runtime_generation},
      frame_ordinal_{create_info.frame_ordinal},
      server_time_{std::move(create_info.server_time)},
      snapshot_reference_{std::move(create_info.snapshot_reference)},
      entity_snapshot_{std::move(create_info.entity_snapshot)},
      component_references_{std::move(create_info.component_references)},
      message_catalog_{std::move(create_info.message_catalog)},
      local_player_identity_{std::move(create_info.local_player_identity)},
      command_acknowledgement_evidence_{
          std::move(create_info.command_acknowledgement_evidence)},
      authoritative_observation_{
          std::move(create_info.authoritative_observation)},
      source_messages_{std::move(create_info.source_messages)},
      accounted_metadata_bytes_{accounted_metadata_bytes},
      status_{status},
      compatibility_profile_{create_info.compatibility_profile},
      evidence_profile_{create_info.evidence_profile}
{
}

StockRuntimeFrameState::CreationResult StockRuntimeFrameState::create(
    const StockRuntimeFrameCreateInfo& create_info,
    const StockRuntimeFrameLimits& limits) noexcept
{
    using Result = StockRuntimeFrameState::CreationResult;
    if (!valid_stock_runtime_frame_limits(limits)) {
        return failure<Result>(StockRuntimeFrameErrorCode::invalid_configuration,
            "invalid stock runtime frame safety limits");
    }
    if (create_info.runtime_generation == 0U) {
        return failure<Result>(
            StockRuntimeFrameErrorCode::invalid_runtime_generation,
            "runtime generation must be nonzero");
    }
    if (create_info.frame_ordinal == 0U) {
        return failure<Result>(StockRuntimeFrameErrorCode::invalid_frame_ordinal,
            "runtime frame ordinal must be nonzero");
    }
    if (!valid_stock_runtime_compatibility_profile(
            create_info.compatibility_profile) ||
        !valid_stock_runtime_evidence_profile(create_info.evidence_profile) ||
        stock_runtime_evidence_profile_for(create_info.compatibility_profile) !=
            create_info.evidence_profile) {
        return failure<Result>(StockRuntimeFrameErrorCode::profile_mismatch,
            "runtime frame profile tuple is inconsistent");
    }
    if (!pending_profile_tuple(create_info.compatibility_profile,
            create_info.evidence_profile)) {
        return failure<Result>(StockRuntimeFrameErrorCode::stock_evidence_pending,
            "confirmed stock runtime frame profile is not implemented");
    }
    if (create_info.source_messages.size() > limits.maximum_source_records) {
        return failure<Result>(StockRuntimeFrameErrorCode::source_limit_exceeded,
            "runtime frame source-record limit was exceeded");
    }
    if (create_info.component_references.size() >
        limits.maximum_component_references) {
        return failure<Result>(
            StockRuntimeFrameErrorCode::component_limit_exceeded,
            "runtime frame component-reference limit was exceeded");
    }
    for (const auto& source : create_info.source_messages) {
        if (!valid_direction(source.direction) ||
            source.decoded_payload_byte_count == 0U ||
            source.decoded_payload_byte_count >
                hard_stock_runtime_decode_limits().maximum_payload_bytes) {
            return failure<Result>(
                StockRuntimeFrameErrorCode::invalid_source_metadata,
                "runtime frame source metadata is outside its bounded domain");
        }
    }
    const auto source_geometry_matches = [&create_info](
                                             const std::size_t source_index,
                                             const StockRuntimeSourceCursor& start,
                                             const StockRuntimeSourceCursor& end) noexcept {
        if (source_index >= create_info.source_messages.size()) return false;
        const auto payload_bytes =
            create_info.source_messages[source_index].decoded_payload_byte_count;
        return valid_stock_runtime_source_cursor(start, payload_bytes) &&
            valid_stock_runtime_source_cursor(end, payload_bytes) &&
            create_info.source_messages[source_index].direction ==
                NetchanDirection::server_to_client &&
            end.absolute_bit_offset() >= start.absolute_bit_offset();
    };
    const auto catalog_binding_matches = [&create_info](
                                             const std::size_t source_index,
                                             const StockRuntimeMessageCategory category,
                                             const std::size_t message_ordinal,
                                             const StockRuntimeSourceCursor& start,
                                             const StockRuntimeSourceCursor& end) noexcept {
        if (!create_info.message_catalog ||
            source_index >= create_info.source_messages.size()) {
            return false;
        }
        const auto entries = create_info.message_catalog->entries();
        const auto found = std::find_if(entries.begin(), entries.end(),
            [message_ordinal](const auto& entry) noexcept {
                return entry.message_ordinal() == message_ordinal;
            });
        return found != entries.end() &&
            std::none_of(found + 1, entries.end(),
                [message_ordinal](const auto& entry) noexcept {
                    return entry.message_ordinal() == message_ordinal;
                }) &&
            found->category() == category &&
            found->source() == create_info.source_messages[source_index] &&
            start.absolute_bit_offset() >=
                found->start_cursor().absolute_bit_offset() &&
            end.absolute_bit_offset() <=
                found->end_cursor().absolute_bit_offset();
    };
    if (create_info.server_time &&
        (!valid_pending_stock_server_time_observation(
             *create_info.server_time, create_info.runtime_generation) ||
            !source_geometry_matches(create_info.server_time->source_record_index,
                create_info.server_time->source_start_cursor,
                create_info.server_time->source_end_cursor) ||
            !catalog_binding_matches(
                create_info.server_time->source_record_index,
                create_info.server_time->source_message_category,
                create_info.server_time->source_message_ordinal,
                create_info.server_time->source_start_cursor,
                create_info.server_time->source_end_cursor))) {
        return failure<Result>(
            StockRuntimeFrameErrorCode::stock_evidence_pending,
            "server-time candidate is malformed or exceeds pending evidence");
    }
    if (create_info.snapshot_reference &&
        (!valid_pending_stock_snapshot_reference_candidate(
             *create_info.snapshot_reference) ||
            !source_geometry_matches(
                create_info.snapshot_reference->source_record_index,
                create_info.snapshot_reference->source_start_cursor,
                create_info.snapshot_reference->source_end_cursor) ||
            !catalog_binding_matches(
                create_info.snapshot_reference->source_record_index,
                create_info.snapshot_reference->source_message_category,
                create_info.snapshot_reference->source_message_ordinal,
                create_info.snapshot_reference->source_start_cursor,
                create_info.snapshot_reference->source_end_cursor))) {
        return failure<Result>(
            StockRuntimeFrameErrorCode::stock_evidence_pending,
            "snapshot-reference candidate is malformed or exceeds pending evidence");
    }

    std::size_t metadata_bytes = create_info.source_messages.size() *
        sizeof(StockRuntimeSourceMetadata);
    for (auto current = create_info.component_references.begin();
         current != create_info.component_references.end(); ++current) {
        if (!valid_component_kind(current->kind) ||
            !valid_message_category(current->source_message_category) ||
            !component_category_matches(
                current->kind, current->source_message_category) ||
            current->entry_count == 0U ||
            current->entry_count > component_entry_limit(current->kind) ||
            !source_geometry_matches(current->source_record_index,
                current->source_start_cursor, current->source_end_cursor) ||
            !catalog_binding_matches(current->source_record_index,
                current->source_message_category,
                current->source_message_ordinal,
                current->source_start_cursor, current->source_end_cursor) ||
            current->confidence !=
                StockRuntimeCandidateConfidence::evidence_pending) {
            return failure<Result>(
                StockRuntimeFrameErrorCode::invalid_component_reference,
                "runtime component reference is malformed or overclaims evidence");
        }
        if (std::find(create_info.component_references.begin(), current,
                *current) != current) {
            return failure<Result>(
                StockRuntimeFrameErrorCode::duplicate_component_reference,
                "duplicate runtime component reference");
        }
        if (!checked_add(metadata_bytes, sizeof(StockRuntimeComponentReference),
                metadata_bytes) ||
            !checked_add(metadata_bytes, current->accounted_metadata_bytes,
                metadata_bytes) ||
            metadata_bytes > limits.maximum_accounted_metadata_bytes) {
            return failure<Result>(
                StockRuntimeFrameErrorCode::metadata_limit_exceeded,
                "runtime frame metadata safety limit was exceeded");
        }
    }

    const auto profile_matches = [&create_info](const auto& state) noexcept {
        return state->compatibility_profile() ==
                create_info.compatibility_profile &&
            state->evidence_profile() == create_info.evidence_profile;
    };
    if (create_info.message_catalog &&
        !profile_matches(create_info.message_catalog)) {
        return failure<Result>(StockRuntimeFrameErrorCode::profile_mismatch,
            "runtime message catalog profile differs from frame profile");
    }
    if (create_info.local_player_identity &&
        (!profile_matches(create_info.local_player_identity) ||
            create_info.local_player_identity->runtime_generation() !=
                create_info.runtime_generation)) {
        return failure<Result>(
            StockRuntimeFrameErrorCode::runtime_generation_mismatch,
            "local-player identity belongs to a different runtime/profile");
    }
    if (create_info.command_acknowledgement_evidence &&
        (!profile_matches(create_info.command_acknowledgement_evidence) ||
            create_info.command_acknowledgement_evidence->runtime_generation() !=
                create_info.runtime_generation)) {
        return failure<Result>(
            StockRuntimeFrameErrorCode::runtime_generation_mismatch,
            "acknowledgement evidence belongs to a different runtime/profile");
    }
    if (create_info.authoritative_observation &&
        (!profile_matches(create_info.authoritative_observation) ||
            create_info.authoritative_observation->runtime_generation() !=
                create_info.runtime_generation)) {
        return failure<Result>(
            StockRuntimeFrameErrorCode::runtime_generation_mismatch,
            "authoritative observation belongs to a different runtime/profile");
    }
    if (create_info.authoritative_observation &&
        create_info.authoritative_observation->values().server_time &&
        create_info.authoritative_observation->values().server_time !=
            create_info.server_time) {
        return failure<Result>(
            StockRuntimeFrameErrorCode::server_time_observation_mismatch,
            "authoritative and frame server-time observations differ");
    }
    if (create_info.authoritative_observation &&
        create_info.authoritative_observation->
                command_acknowledgement_evidence().get() !=
            create_info.command_acknowledgement_evidence.get()) {
        return failure<Result>(
            StockRuntimeFrameErrorCode::acknowledgement_evidence_mismatch,
            "frame and authoritative observation retain different ACK evidence");
    }
    if (create_info.local_player_identity &&
        create_info.authoritative_observation &&
        create_info.local_player_identity->candidate_entity_number() &&
        *create_info.local_player_identity->candidate_entity_number() !=
            create_info.authoritative_observation->
                local_player_candidate_entity_number()) {
        return failure<Result>(
            StockRuntimeFrameErrorCode::local_player_candidate_mismatch,
            "identity and authoritative observation name different candidates");
    }
    if (create_info.entity_snapshot &&
        (create_info.entity_snapshot->compatibility_profile() !=
                EntitySnapshotCompatibilityProfile::
                    stock_protocol_48_build_10210_evidence_pending ||
            create_info.entity_snapshot->evidence_profile() !=
                EntitySnapshotEvidenceProfile::
                    stock_runtime_grammar_evidence_pending)) {
        return failure<Result>(
            StockRuntimeFrameErrorCode::incompatible_entity_snapshot,
            "synthetic or caller-supplied entity snapshot cannot enter stock frame");
    }

    const bool conflict =
        (create_info.local_player_identity &&
            create_info.local_player_identity->status() ==
                StockLocalPlayerIdentityStatus::conflicting) ||
        (create_info.command_acknowledgement_evidence &&
            create_info.command_acknowledgement_evidence->status() ==
                StockCommandAcknowledgementEvidenceStatus::conflicting) ||
        (create_info.authoritative_observation &&
            create_info.authoritative_observation->status() ==
                StockAuthoritativeMovementObservationStatus::field_conflict);
    const bool has_component = create_info.server_time.has_value() ||
        create_info.snapshot_reference.has_value() ||
        static_cast<bool>(create_info.entity_snapshot) ||
        !create_info.component_references.empty() ||
        static_cast<bool>(create_info.message_catalog) ||
        static_cast<bool>(create_info.local_player_identity) ||
        static_cast<bool>(create_info.command_acknowledgement_evidence) ||
        static_cast<bool>(create_info.authoritative_observation) ||
        !create_info.source_messages.empty();
    const auto status = conflict
        ? StockRuntimeFrameStatus::component_conflict
        : (has_component ? StockRuntimeFrameStatus::partial_evidence_pending
                         : StockRuntimeFrameStatus::evidence_pending);

    try {
        auto retained = create_info;
        return {StockRuntimeFrameState{
                    std::move(retained), metadata_bytes, status},
            std::nullopt};
    } catch (const std::bad_alloc&) {
        return failure<Result>(StockRuntimeFrameErrorCode::allocation_failed,
            "unable to retain stock runtime frame");
    }
}

std::uint64_t StockRuntimeFrameState::runtime_generation() const noexcept
{
    return runtime_generation_;
}
std::uint64_t StockRuntimeFrameState::frame_ordinal() const noexcept
{
    return frame_ordinal_;
}
const std::optional<StockServerTimeObservation>&
StockRuntimeFrameState::server_time() const noexcept
{
    return server_time_;
}
const std::optional<StockRuntimeSnapshotReferenceCandidate>&
StockRuntimeFrameState::snapshot_reference() const noexcept
{
    return snapshot_reference_;
}
const std::shared_ptr<const EntitySnapshotState>&
StockRuntimeFrameState::entity_snapshot() const noexcept
{
    return entity_snapshot_;
}
std::span<const StockRuntimeComponentReference>
StockRuntimeFrameState::component_references() const noexcept
{
    return component_references_;
}
const std::shared_ptr<const StockRuntimeMessageCatalogState>&
StockRuntimeFrameState::message_catalog() const noexcept
{
    return message_catalog_;
}
const std::shared_ptr<const StockLocalPlayerIdentityState>&
StockRuntimeFrameState::local_player_identity() const noexcept
{
    return local_player_identity_;
}
const std::shared_ptr<const StockCommandAcknowledgementEvidenceState>&
StockRuntimeFrameState::command_acknowledgement_evidence() const noexcept
{
    return command_acknowledgement_evidence_;
}
const std::shared_ptr<const StockAuthoritativeMovementObservation>&
StockRuntimeFrameState::authoritative_observation() const noexcept
{
    return authoritative_observation_;
}
std::span<const StockRuntimeSourceMetadata>
StockRuntimeFrameState::source_messages() const noexcept
{
    return source_messages_;
}
std::size_t StockRuntimeFrameState::accounted_metadata_bytes() const noexcept
{
    return accounted_metadata_bytes_;
}
StockRuntimeFrameStatus StockRuntimeFrameState::status() const noexcept
{
    return status_;
}
StockRuntimeCompatibilityProfile
StockRuntimeFrameState::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}
StockRuntimeEvidenceProfile StockRuntimeFrameState::evidence_profile()
    const noexcept
{
    return evidence_profile_;
}

StockRuntimeFrameHistoryState::StockRuntimeFrameHistoryState(
    const std::uint64_t runtime_generation,
    const StockRuntimeFrameHistoryLimits limits,
    std::vector<StockRuntimeFrameState> frames,
    const std::size_t accounted_metadata_bytes,
    const std::uint64_t history_revision,
    const StockRuntimeCompatibilityProfile compatibility_profile,
    const StockRuntimeEvidenceProfile evidence_profile) noexcept
    : runtime_generation_{runtime_generation},
      limits_{limits},
      frames_{std::move(frames)},
      accounted_metadata_bytes_{accounted_metadata_bytes},
      history_revision_{history_revision},
      compatibility_profile_{compatibility_profile},
      evidence_profile_{evidence_profile}
{
}

StockRuntimeFrameHistoryState::CreationResult
StockRuntimeFrameHistoryState::create(
    const std::uint64_t runtime_generation,
    const StockRuntimeFrameHistoryLimits limits,
    const StockRuntimeCompatibilityProfile compatibility_profile,
    const StockRuntimeEvidenceProfile evidence_profile) noexcept
{
    using Result = StockRuntimeFrameHistoryState::CreationResult;
    if (runtime_generation == 0U ||
        !valid_stock_runtime_frame_history_limits(limits)) {
        return failure<Result>(StockRuntimeFrameErrorCode::invalid_configuration,
            "invalid stock runtime history configuration");
    }
    if (!valid_stock_runtime_compatibility_profile(compatibility_profile) ||
        !valid_stock_runtime_evidence_profile(evidence_profile) ||
        stock_runtime_evidence_profile_for(compatibility_profile) !=
            evidence_profile) {
        return failure<Result>(StockRuntimeFrameErrorCode::profile_mismatch,
            "runtime history profile tuple is inconsistent");
    }
    if (!pending_profile_tuple(compatibility_profile, evidence_profile)) {
        return failure<Result>(StockRuntimeFrameErrorCode::stock_evidence_pending,
            "confirmed stock runtime history profile is not implemented");
    }
    return {StockRuntimeFrameHistoryState{runtime_generation, limits, {}, 0U,
                1U, compatibility_profile, evidence_profile},
        std::nullopt};
}

StockRuntimeFrameHistoryState::AppendResult
StockRuntimeFrameHistoryState::append(
    const StockRuntimeFrameState& frame) const noexcept
{
    using Result = StockRuntimeFrameHistoryState::AppendResult;
    if (frame.runtime_generation() != runtime_generation_) {
        return failure<Result>(
            StockRuntimeFrameErrorCode::runtime_generation_mismatch,
            "runtime frame belongs to a different generation");
    }
    if (frame.compatibility_profile() != compatibility_profile_ ||
        frame.evidence_profile() != evidence_profile_) {
        return failure<Result>(StockRuntimeFrameErrorCode::profile_mismatch,
            "runtime frame profile differs from history profile");
    }
    if (!frames_.empty() &&
        frame.frame_ordinal() == frames_.back().frame_ordinal()) {
        return failure<Result>(StockRuntimeFrameErrorCode::duplicate_frame,
            "duplicate runtime frame ordinal");
    }
    if (!frames_.empty() &&
        frame.frame_ordinal() < frames_.back().frame_ordinal()) {
        return failure<Result>(StockRuntimeFrameErrorCode::out_of_order_frame,
            "runtime history does not reorder older frames implicitly");
    }
    if (frames_.size() >= limits_.maximum_frames) {
        return failure<Result>(StockRuntimeFrameErrorCode::history_backpressure,
            "runtime history is full; caller must publish a new retention window");
    }
    std::size_t next_bytes = 0U;
    if (!checked_add(accounted_metadata_bytes_,
            frame.accounted_metadata_bytes(), next_bytes) ||
        next_bytes > limits_.maximum_accounted_metadata_bytes) {
        return failure<Result>(StockRuntimeFrameErrorCode::history_backpressure,
            "runtime history metadata budget is exhausted");
    }
    if (history_revision_ >= limits_.maximum_history_revision) {
        return failure<Result>(
            StockRuntimeFrameErrorCode::history_revision_exhausted,
            "runtime history revision is exhausted");
    }
    try {
        auto next_frames = frames_;
        next_frames.push_back(frame);
        return {StockRuntimeFrameHistoryState{runtime_generation_, limits_,
                    std::move(next_frames), next_bytes, history_revision_ + 1U,
                    compatibility_profile_, evidence_profile_},
            std::nullopt};
    } catch (const std::bad_alloc&) {
        return failure<Result>(StockRuntimeFrameErrorCode::allocation_failed,
            "unable to retain runtime history append");
    }
}

std::span<const StockRuntimeFrameState> StockRuntimeFrameHistoryState::frames()
    const noexcept
{
    return frames_;
}
const StockRuntimeFrameState* StockRuntimeFrameHistoryState::find_exact(
    const std::uint64_t frame_ordinal) const noexcept
{
    const auto found = std::lower_bound(frames_.begin(), frames_.end(),
        frame_ordinal, [](const auto& frame, const auto ordinal) noexcept {
            return frame.frame_ordinal() < ordinal;
        });
    return found != frames_.end() && found->frame_ordinal() == frame_ordinal
        ? &*found
        : nullptr;
}
std::size_t StockRuntimeFrameHistoryState::frame_count() const noexcept
{
    return frames_.size();
}
std::size_t StockRuntimeFrameHistoryState::accounted_metadata_bytes()
    const noexcept
{
    return accounted_metadata_bytes_;
}
std::uint64_t StockRuntimeFrameHistoryState::history_revision() const noexcept
{
    return history_revision_;
}
std::uint64_t StockRuntimeFrameHistoryState::runtime_generation() const noexcept
{
    return runtime_generation_;
}
const StockRuntimeFrameHistoryLimits& StockRuntimeFrameHistoryState::limits()
    const noexcept
{
    return limits_;
}
StockRuntimeCompatibilityProfile
StockRuntimeFrameHistoryState::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}
StockRuntimeEvidenceProfile StockRuntimeFrameHistoryState::evidence_profile()
    const noexcept
{
    return evidence_profile_;
}

std::string_view to_string(const StockRuntimeFrameStatus status) noexcept
{
    switch (status) {
    case StockRuntimeFrameStatus::evidence_pending: return "evidence_pending";
    case StockRuntimeFrameStatus::partial_evidence_pending:
        return "partial_evidence_pending";
    case StockRuntimeFrameStatus::component_conflict:
        return "component_conflict";
    }
    return "unknown";
}

std::string_view to_string(const StockRuntimeFrameErrorCode code) noexcept
{
    switch (code) {
    case StockRuntimeFrameErrorCode::invalid_configuration:
        return "invalid_configuration";
    case StockRuntimeFrameErrorCode::invalid_runtime_generation:
        return "invalid_runtime_generation";
    case StockRuntimeFrameErrorCode::invalid_frame_ordinal:
        return "invalid_frame_ordinal";
    case StockRuntimeFrameErrorCode::invalid_source_metadata:
        return "invalid_source_metadata";
    case StockRuntimeFrameErrorCode::invalid_component_reference:
        return "invalid_component_reference";
    case StockRuntimeFrameErrorCode::source_limit_exceeded:
        return "source_limit_exceeded";
    case StockRuntimeFrameErrorCode::component_limit_exceeded:
        return "component_limit_exceeded";
    case StockRuntimeFrameErrorCode::metadata_limit_exceeded:
        return "metadata_limit_exceeded";
    case StockRuntimeFrameErrorCode::duplicate_component_reference:
        return "duplicate_component_reference";
    case StockRuntimeFrameErrorCode::profile_mismatch:
        return "profile_mismatch";
    case StockRuntimeFrameErrorCode::runtime_generation_mismatch:
        return "runtime_generation_mismatch";
    case StockRuntimeFrameErrorCode::local_player_candidate_mismatch:
        return "local_player_candidate_mismatch";
    case StockRuntimeFrameErrorCode::acknowledgement_evidence_mismatch:
        return "acknowledgement_evidence_mismatch";
    case StockRuntimeFrameErrorCode::server_time_observation_mismatch:
        return "server_time_observation_mismatch";
    case StockRuntimeFrameErrorCode::incompatible_entity_snapshot:
        return "incompatible_entity_snapshot";
    case StockRuntimeFrameErrorCode::stock_evidence_pending:
        return "stock_evidence_pending";
    case StockRuntimeFrameErrorCode::allocation_failed:
        return "allocation_failed";
    case StockRuntimeFrameErrorCode::duplicate_frame: return "duplicate_frame";
    case StockRuntimeFrameErrorCode::out_of_order_frame:
        return "out_of_order_frame";
    case StockRuntimeFrameErrorCode::history_limit_exceeded:
        return "history_limit_exceeded";
    case StockRuntimeFrameErrorCode::history_backpressure:
        return "history_backpressure";
    case StockRuntimeFrameErrorCode::history_revision_exhausted:
        return "history_revision_exhausted";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
