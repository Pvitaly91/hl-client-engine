#pragma once

#include <hlclient/prediction/prediction_types.hpp>

#include <cstdint>
#include <optional>

namespace hlclient::prediction {

struct AuthoritativePlayerUpdateIdentityCreateInfo {
    PredictionSessionIdentity session;
    std::uint64_t update_ordinal{0U};
    AuthoritativeCommandAcknowledgement acknowledgement{
        AuthoritativeCommandAcknowledgement::none()};
    std::int64_t synthetic_authority_time_nanoseconds{0};
    AuthoritativePlayerDiscontinuity discontinuity{
        AuthoritativePlayerDiscontinuity::normal};
};

class AuthoritativePlayerUpdateIdentity final {
public:
    struct CreationResult;

    AuthoritativePlayerUpdateIdentity(
        const AuthoritativePlayerUpdateIdentity&) = default;
    AuthoritativePlayerUpdateIdentity(
        AuthoritativePlayerUpdateIdentity&&) noexcept = default;
    AuthoritativePlayerUpdateIdentity& operator=(
        const AuthoritativePlayerUpdateIdentity&) = delete;
    AuthoritativePlayerUpdateIdentity& operator=(
        AuthoritativePlayerUpdateIdentity&&) = delete;
    ~AuthoritativePlayerUpdateIdentity() = default;

    [[nodiscard]] static CreationResult create(
        const AuthoritativePlayerUpdateIdentityCreateInfo& create_info) noexcept;

    [[nodiscard]] const PredictionSessionIdentity& session() const noexcept;
    [[nodiscard]] std::uint64_t update_ordinal() const noexcept;
    [[nodiscard]] const AuthoritativeCommandAcknowledgement& acknowledgement()
        const noexcept;
    [[nodiscard]] std::int64_t synthetic_authority_time_nanoseconds()
        const noexcept;
    [[nodiscard]] AuthoritativePlayerDiscontinuity discontinuity()
        const noexcept;

private:
    explicit AuthoritativePlayerUpdateIdentity(
        const AuthoritativePlayerUpdateIdentityCreateInfo& create_info) noexcept;

    PredictionSessionIdentity session_{};
    std::uint64_t update_ordinal_{0U};
    AuthoritativeCommandAcknowledgement acknowledgement_{
        AuthoritativeCommandAcknowledgement::none()};
    std::int64_t synthetic_authority_time_nanoseconds_{0};
    AuthoritativePlayerDiscontinuity discontinuity_{
        AuthoritativePlayerDiscontinuity::normal};
};

struct AuthoritativePlayerUpdateIdentity::CreationResult {
    std::optional<AuthoritativePlayerUpdateIdentity> identity;
    std::optional<PredictionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return identity.has_value() && !error.has_value();
    }
};

class AuthoritativePlayerState final {
public:
    struct CreationResult;

    AuthoritativePlayerState(const AuthoritativePlayerState&) = default;
    AuthoritativePlayerState(AuthoritativePlayerState&&) noexcept = default;
    AuthoritativePlayerState& operator=(const AuthoritativePlayerState&) = delete;
    AuthoritativePlayerState& operator=(AuthoritativePlayerState&&) = delete;
    ~AuthoritativePlayerState() = default;

    [[nodiscard]] static CreationResult from_synthetic_complete_state(
        AuthoritativePlayerUpdateIdentity update_identity,
        movement::LocalPlayerMovementState complete_state) noexcept;

    [[nodiscard]] const AuthoritativePlayerUpdateIdentity& update_identity()
        const noexcept;
    [[nodiscard]] const movement::LocalPlayerMovementState& movement_state()
        const noexcept;
    [[nodiscard]] const AuthoritativeCommandAcknowledgement& acknowledgement()
        const noexcept;
    [[nodiscard]] std::uint64_t state_signature() const noexcept;
    [[nodiscard]] PredictionCompatibilityProfile compatibility_profile()
        const noexcept;
    [[nodiscard]] PredictionEvidenceProfile evidence_profile() const noexcept;

private:
    AuthoritativePlayerState(
        AuthoritativePlayerUpdateIdentity update_identity,
        movement::LocalPlayerMovementState complete_state,
        std::uint64_t state_signature) noexcept;

    AuthoritativePlayerUpdateIdentity update_identity_;
    movement::LocalPlayerMovementState movement_state_;
    std::uint64_t state_signature_{0U};
    PredictionCompatibilityProfile compatibility_profile_{
        PredictionCompatibilityProfile::
            synthetic_authoritative_reconciliation_v1};
    PredictionEvidenceProfile evidence_profile_{
        PredictionEvidenceProfile::
            project_typed_authoritative_states_and_independent_fixtures};
};

struct AuthoritativePlayerState::CreationResult {
    std::optional<AuthoritativePlayerState> state;
    std::optional<PredictionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value() && !error.has_value();
    }
};

struct AuthoritativePlayerStatePollResult {
    std::optional<AuthoritativePlayerState> state;
    std::optional<PredictionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

class IAuthoritativePlayerStateSource {
public:
    virtual ~IAuthoritativePlayerStateSource() = default;
    [[nodiscard]] virtual AuthoritativePlayerStatePollResult poll_next() = 0;
};

class StockAuthoritativePlayerStateSourceEvidencePending final
    : public IAuthoritativePlayerStateSource {
public:
    [[nodiscard]] AuthoritativePlayerStatePollResult poll_next() override;
};

} // namespace hlclient::prediction
