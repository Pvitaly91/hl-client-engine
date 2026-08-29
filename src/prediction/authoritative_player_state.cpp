#include <hlclient/prediction/authoritative_player_state.hpp>

#include <cmath>
#include <utility>

namespace hlclient::prediction {
namespace {

[[nodiscard]] PredictionError failure(
    const PredictionErrorCode code,
    const std::string_view context) noexcept
{
    return PredictionError{code, std::nullopt, context};
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool valid_discontinuity(
    const AuthoritativePlayerDiscontinuity discontinuity) noexcept
{
    switch (discontinuity) {
    case AuthoritativePlayerDiscontinuity::normal:
    case AuthoritativePlayerDiscontinuity::teleport:
    case AuthoritativePlayerDiscontinuity::respawn_or_hard_reset:
        return true;
    }
    return false;
}

} // namespace

AuthoritativePlayerUpdateIdentity::AuthoritativePlayerUpdateIdentity(
    const AuthoritativePlayerUpdateIdentityCreateInfo& create_info) noexcept
    : session_{create_info.session},
      update_ordinal_{create_info.update_ordinal},
      acknowledgement_{create_info.acknowledgement},
      synthetic_authority_time_nanoseconds_{
          create_info.synthetic_authority_time_nanoseconds},
      discontinuity_{create_info.discontinuity}
{
}

AuthoritativePlayerUpdateIdentity::CreationResult
AuthoritativePlayerUpdateIdentity::create(
    const AuthoritativePlayerUpdateIdentityCreateInfo& create_info) noexcept
{
    if (!create_info.session.valid()) {
        const auto stock = create_info.session.prediction_profile ==
                PredictionCompatibilityProfile::
                    stock_protocol_48_authoritative_reconciliation_evidence_pending ||
            create_info.session.acknowledgement_profile ==
                PredictionAcknowledgementProfile::
                    stock_usercmd_acknowledgement_evidence_pending;
        return {std::nullopt,
            failure(
                stock ? PredictionErrorCode::stock_evidence_pending
                      : PredictionErrorCode::invalid_session_identity,
                stock ? "stock authoritative acknowledgement evidence is pending"
                      : "prediction session identity is invalid")};
    }
    if (create_info.update_ordinal == 0U) {
        return {std::nullopt,
            failure(
                PredictionErrorCode::invalid_authority_update_ordinal,
                "authority update ordinal must be nonzero")};
    }
    if (create_info.synthetic_authority_time_nanoseconds < 0) {
        return {std::nullopt,
            failure(
                PredictionErrorCode::invalid_authoritative_state,
                "synthetic authority time must be non-negative")};
    }
    if (!valid_discontinuity(create_info.discontinuity)) {
        return {std::nullopt,
            failure(
                PredictionErrorCode::invalid_authoritative_state,
                "authority discontinuity is invalid")};
    }
    const auto hard_reset = create_info.discontinuity ==
        AuthoritativePlayerDiscontinuity::respawn_or_hard_reset;
    if (!hard_reset && !create_info.acknowledgement.has_sequence()) {
        return {std::nullopt,
            failure(
                PredictionErrorCode::authoritative_acknowledgement_missing,
                "normal and teleport updates require an exact command acknowledgement")};
    }
    if (hard_reset && create_info.acknowledgement.has_sequence()) {
        return {std::nullopt,
            failure(
                PredictionErrorCode::invalid_authoritative_state,
                "hard-reset authority must use the no-command anchor")};
    }
    return {AuthoritativePlayerUpdateIdentity{create_info}, std::nullopt};
}

const PredictionSessionIdentity& AuthoritativePlayerUpdateIdentity::session()
    const noexcept
{
    return session_;
}

std::uint64_t AuthoritativePlayerUpdateIdentity::update_ordinal() const noexcept
{
    return update_ordinal_;
}

const AuthoritativeCommandAcknowledgement&
AuthoritativePlayerUpdateIdentity::acknowledgement() const noexcept
{
    return acknowledgement_;
}

std::int64_t
AuthoritativePlayerUpdateIdentity::synthetic_authority_time_nanoseconds()
    const noexcept
{
    return synthetic_authority_time_nanoseconds_;
}

AuthoritativePlayerDiscontinuity
AuthoritativePlayerUpdateIdentity::discontinuity() const noexcept
{
    return discontinuity_;
}

AuthoritativePlayerState::AuthoritativePlayerState(
    AuthoritativePlayerUpdateIdentity update_identity,
    movement::LocalPlayerMovementState complete_state,
    const std::uint64_t state_signature) noexcept
    : update_identity_{std::move(update_identity)},
      movement_state_{std::move(complete_state)},
      state_signature_{state_signature}
{
}

AuthoritativePlayerState::CreationResult
AuthoritativePlayerState::from_synthetic_complete_state(
    AuthoritativePlayerUpdateIdentity update_identity,
    movement::LocalPlayerMovementState complete_state) noexcept
{
    const auto& acknowledgement = update_identity.acknowledgement();
    const auto expected_sequence = acknowledgement.sequence()
        ? acknowledgement.sequence()->value()
        : 0U;
    if (complete_state.source_command_sequence() != expected_sequence) {
        return {std::nullopt,
            failure(
                PredictionErrorCode::invalid_authoritative_state,
                "authoritative state sequence does not match acknowledgement")};
    }
    if (complete_state.state_revision() == 0U ||
        complete_state.command_profile() != update_identity.session().command_profile ||
        complete_state.compatibility_profile() !=
            movement::GoldSrcMovementCompatibilityProfile::
                public_valve_pm_shared_dry_walk_subset_v1 ||
        complete_state.evidence_profile() !=
            movement::GoldSrcMovementEvidenceProfile::
                public_valve_pm_shared_and_independent_fixtures ||
        complete_state.mode() == movement::PlayerMovementMode::
            unsupported_liquid ||
        complete_state.mode() == movement::PlayerMovementMode::
            unsupported_ladder ||
        complete_state.mode() == movement::PlayerMovementMode::
            invalid_or_stuck ||
        !finite_vector(complete_state.origin()) ||
        !finite_vector(complete_state.velocity()) ||
        !finite_vector(complete_state.view_angles()) ||
        !finite_vector(complete_state.view_offset())) {
        return {std::nullopt,
            failure(
                PredictionErrorCode::invalid_authoritative_state,
                "authoritative movement state violates the synthetic profile")};
    }
    if (update_identity.discontinuity() ==
            AuthoritativePlayerDiscontinuity::respawn_or_hard_reset &&
        complete_state.source_command_sequence() != 0U) {
        return {std::nullopt,
            failure(
                PredictionErrorCode::invalid_authoritative_state,
                "hard-reset state must be a command-zero anchor")};
    }
    const auto signature =
        movement::local_player_movement_state_signature(complete_state);
    if (signature == 0U) {
        return {std::nullopt,
            failure(
                PredictionErrorCode::invalid_authoritative_state,
                "authoritative state signature is invalid")};
    }
    return {AuthoritativePlayerState{
                std::move(update_identity), std::move(complete_state), signature},
        std::nullopt};
}

const AuthoritativePlayerUpdateIdentity&
AuthoritativePlayerState::update_identity() const noexcept
{
    return update_identity_;
}

const movement::LocalPlayerMovementState&
AuthoritativePlayerState::movement_state() const noexcept
{
    return movement_state_;
}

const AuthoritativeCommandAcknowledgement&
AuthoritativePlayerState::acknowledgement() const noexcept
{
    return update_identity_.acknowledgement();
}

std::uint64_t AuthoritativePlayerState::state_signature() const noexcept
{
    return state_signature_;
}

PredictionCompatibilityProfile
AuthoritativePlayerState::compatibility_profile() const noexcept
{
    return compatibility_profile_;
}

PredictionEvidenceProfile AuthoritativePlayerState::evidence_profile()
    const noexcept
{
    return evidence_profile_;
}

AuthoritativePlayerStatePollResult
StockAuthoritativePlayerStateSourceEvidencePending::poll_next()
{
    return {std::nullopt,
        failure(
            PredictionErrorCode::stock_evidence_pending,
            "stock authoritative player-state and acknowledgement evidence is pending")};
}

} // namespace hlclient::prediction
