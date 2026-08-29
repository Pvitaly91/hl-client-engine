#include <hlclient/prediction/stock_authoritative_player_state_adapter.hpp>

namespace hlclient::prediction {
namespace {

[[nodiscard]] bool liquid_or_special(
    const movement::PlayerMovementContents contents) noexcept
{
    switch (contents) {
    case movement::PlayerMovementContents::water:
    case movement::PlayerMovementContents::slime:
    case movement::PlayerMovementContents::lava:
    case movement::PlayerMovementContents::current:
    case movement::PlayerMovementContents::special:
        return true;
    case movement::PlayerMovementContents::empty:
    case movement::PlayerMovementContents::solid:
    case movement::PlayerMovementContents::sky:
        return false;
    }
    return true;
}

[[nodiscard]] StockAuthoritativeProjectionResult result(
    const StockAuthoritativeProjectionStatus status,
    std::shared_ptr<const goldsrc::StockAuthoritativeMovementObservation>
        observation,
    std::optional<StockAuthoritativeCollisionValidation> collision_validation,
    const std::string_view context) noexcept
{
    return {status, std::move(observation), std::move(collision_validation),
        std::nullopt, context};
}

} // namespace

StockAuthoritativeProjectionResult
StockAuthoritativePlayerStateAdapter::project(
    const goldsrc::StockRuntimeFrameState& frame,
    const StockAuthoritativeAdapterContext& adapter_context,
    const goldsrc::movement::GoldSrcMovementEnvironment& environment,
    const goldsrc::movement::ILocalMovementCollision& collision,
    collision::CollisionQueryScratch& collision_scratch,
    const goldsrc::movement::GoldSrcLocalMovementConfig& movement_config)
    noexcept
{
    const auto observation = frame.authoritative_observation();
    if (adapter_context.runtime_generation == 0U ||
        !adapter_context.collision_session.valid() ||
        adapter_context.movement_environment_signature == 0U ||
        adapter_context.movement_config_signature == 0U ||
        !goldsrc::movement::valid_goldsrc_local_movement_config(
            movement_config) ||
        frame.runtime_generation() != adapter_context.runtime_generation ||
        prediction_movement_environment_signature(environment) !=
            adapter_context.movement_environment_signature ||
        prediction_movement_config_signature(movement_config) !=
            adapter_context.movement_config_signature) {
        return result(StockAuthoritativeProjectionStatus::profile_mismatch,
            observation, std::nullopt,
            "runtime, movement environment, or movement config mismatch");
    }
    if (frame.compatibility_profile() !=
            goldsrc::StockRuntimeCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending ||
        frame.evidence_profile() !=
            goldsrc::StockRuntimeEvidenceProfile::
                controlled_signed_stock_transcript_pending) {
        return result(StockAuthoritativeProjectionStatus::profile_mismatch,
            observation, std::nullopt,
            "stock runtime frame profile is unsupported");
    }
    if (!collision.valid() || !collision.session_identity().has_value() ||
        *collision.session_identity() != adapter_context.collision_session) {
        StockAuthoritativeCollisionValidation validation;
        validation.status =
            StockAuthoritativeCollisionValidationStatus::session_mismatch;
        return result(
            StockAuthoritativeProjectionStatus::collision_validation_failed,
            observation, validation,
            "collision provider does not match the immutable session");
    }
    if (!observation) {
        return result(
            StockAuthoritativeProjectionStatus::required_field_missing,
            observation, std::nullopt,
            "runtime frame has no authoritative movement observation");
    }
    if (frame.status() == goldsrc::StockRuntimeFrameStatus::component_conflict ||
        observation->status() ==
            goldsrc::StockAuthoritativeMovementObservationStatus::
                field_conflict) {
        return result(StockAuthoritativeProjectionStatus::field_conflict,
            observation, std::nullopt,
            "runtime frame contains conflicting authoritative evidence");
    }

    std::optional<StockAuthoritativeCollisionValidation> validation;
    const auto& values = observation->values();
    if (values.origin && values.hull) {
        validation.emplace();
        const auto position = collision.test_position(
            *values.origin, *values.hull, collision_scratch,
            movement_config.collision_query);
        if (!position) {
            validation->status =
                StockAuthoritativeCollisionValidationStatus::query_failed;
            if (position.error) {
                validation->collision_error = position.error->code;
            }
            return result(StockAuthoritativeProjectionStatus::
                              collision_validation_failed,
                observation, validation,
                "candidate hull position query failed");
        }
        validation->contents = position.result->contents.category;
        if (position.result->status ==
            goldsrc::movement::LocalMovementPositionStatus::blocking) {
            validation->status =
                StockAuthoritativeCollisionValidationStatus::blocking;
            return result(StockAuthoritativeProjectionStatus::
                              collision_validation_failed,
                observation, validation,
                "candidate origin is blocking; authoritative state was not nudged");
        }

        const auto contents = collision.point_contents(
            *values.origin, collision_scratch,
            movement_config.collision_query);
        if (!contents) {
            validation->status =
                StockAuthoritativeCollisionValidationStatus::query_failed;
            if (contents.error) {
                validation->collision_error = contents.error->code;
            }
            return result(StockAuthoritativeProjectionStatus::
                              collision_validation_failed,
                observation, validation,
                "candidate origin contents query failed");
        }
        validation->contents = contents.result->contents.category;
        if (liquid_or_special(contents.result->contents.category)) {
            validation->status =
                StockAuthoritativeCollisionValidationStatus::liquid;
            return result(StockAuthoritativeProjectionStatus::
                              unsupported_liquid_or_ladder,
                observation, validation,
                "liquid or special movement remains unsupported");
        }

        auto probe_end = *values.origin;
        probe_end.z -= static_cast<float>(movement_config.ground_probe_distance);
        const auto ground = collision.trace_hull(*values.origin, probe_end,
            *values.hull, collision_scratch,
            movement_config.collision_query);
        if (!ground) {
            validation->status =
                StockAuthoritativeCollisionValidationStatus::query_failed;
            if (ground.error) {
                validation->collision_error = ground.error->code;
            }
            return result(StockAuthoritativeProjectionStatus::
                              collision_validation_failed,
                observation, validation,
                "candidate ground trace failed");
        }
        if (ground.result->start_solid || ground.result->all_solid) {
            validation->status =
                StockAuthoritativeCollisionValidationStatus::blocking;
            return result(StockAuthoritativeProjectionStatus::
                              collision_validation_failed,
                observation, validation,
                "candidate ground trace begins in blocking contents");
        }
        const bool walkable = ground.result->collision_plane.has_value() &&
            ground.result->collision_plane->normal.z >=
                movement_config.minimum_walkable_normal_z;
        const bool vertical_velocity_allows_ground = !values.velocity ||
            values.velocity->z <=
                movement_config.maximum_ground_snap_upward_velocity;
        validation->walkable = walkable;
        validation->grounded = ground.result->fraction < 1.0 && walkable &&
            vertical_velocity_allows_ground;
        validation->ground_profile =
            StockAuthoritativeDerivedGroundProfile::
                collision_derived_ground_v1;
        validation->status =
            StockAuthoritativeCollisionValidationStatus::validated_free;
    }

    if (!frame.local_player_identity() ||
        !frame.local_player_identity()->confirmed_entity_number().has_value()) {
        return result(
            StockAuthoritativeProjectionStatus::local_player_identity_pending,
            observation, validation,
            "local-player identity is not confirmed for the stock profile");
    }
    if (!frame.server_time() ||
        frame.server_time()->confidence !=
            goldsrc::StockRuntimeCandidateConfidence::confirmed_for_profile) {
        return result(
            StockAuthoritativeProjectionStatus::server_time_pending,
            observation, validation,
            "server-time encoding remains evidence-pending");
    }
    if (!frame.command_acknowledgement_evidence() ||
        !frame.command_acknowledgement_evidence()
             ->exact_usercmd_sequence_available()) {
        return result(StockAuthoritativeProjectionStatus::
                          command_acknowledgement_pending,
            observation, validation,
            "exact stock usercmd acknowledgement remains evidence-pending");
    }
    if (!observation->complete_candidate_fields()) {
        return result(
            StockAuthoritativeProjectionStatus::required_field_missing,
            observation, validation,
            "one or more required authoritative movement fields are absent");
    }

    // The pending runtime/ACK profiles intentionally have no conversion route
    // to AuthoritativePlayerState. This is the final fail-closed gate for this
    // milestone and prevents partial stock authority from activating replay.
    return result(StockAuthoritativeProjectionStatus::stock_evidence_pending,
        observation, validation,
        "stock authoritative state construction awaits confirmed wire evidence");
}

std::string_view to_string(
    const StockAuthoritativeProjectionStatus status) noexcept
{
    switch (status) {
    case StockAuthoritativeProjectionStatus::complete_authoritative_state:
        return "complete_authoritative_state";
    case StockAuthoritativeProjectionStatus::command_acknowledgement_pending:
        return "command_acknowledgement_pending";
    case StockAuthoritativeProjectionStatus::local_player_identity_pending:
        return "local_player_identity_pending";
    case StockAuthoritativeProjectionStatus::server_time_pending:
        return "server_time_pending";
    case StockAuthoritativeProjectionStatus::required_field_missing:
        return "required_field_missing";
    case StockAuthoritativeProjectionStatus::field_conflict:
        return "field_conflict";
    case StockAuthoritativeProjectionStatus::collision_validation_failed:
        return "collision_validation_failed";
    case StockAuthoritativeProjectionStatus::unsupported_liquid_or_ladder:
        return "unsupported_liquid_or_ladder";
    case StockAuthoritativeProjectionStatus::profile_mismatch:
        return "profile_mismatch";
    case StockAuthoritativeProjectionStatus::stock_evidence_pending:
        return "stock_evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(
    const StockAuthoritativeCollisionValidationStatus status) noexcept
{
    switch (status) {
    case StockAuthoritativeCollisionValidationStatus::not_attempted:
        return "not_attempted";
    case StockAuthoritativeCollisionValidationStatus::validated_free:
        return "validated_free";
    case StockAuthoritativeCollisionValidationStatus::blocking:
        return "blocking";
    case StockAuthoritativeCollisionValidationStatus::liquid: return "liquid";
    case StockAuthoritativeCollisionValidationStatus::query_failed:
        return "query_failed";
    case StockAuthoritativeCollisionValidationStatus::session_mismatch:
        return "session_mismatch";
    }
    return "unknown";
}

} // namespace hlclient::prediction
