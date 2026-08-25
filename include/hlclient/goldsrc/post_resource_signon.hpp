#pragma once

#include <hlclient/goldsrc/delta_description.hpp>
#include <hlclient/goldsrc/resource_client_response.hpp>
#include <hlclient/goldsrc/service_message_stream.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

class PostResourceEntitySnapshotStage;

// Project safety limits. They are not claims about stock engine maxima.
inline constexpr std::size_t kDefaultMaximumPostResourcePayloadBytes = 65'536U;
inline constexpr std::size_t kMaximumPostResourcePayloadBytes = 1'048'576U;
inline constexpr std::size_t kDefaultMaximumPostResourceMessages = 64U;
inline constexpr std::size_t kMaximumPostResourceMessages = 512U;
inline constexpr std::size_t kDefaultMaximumClientSignonRequests = 8U;
inline constexpr std::size_t kMaximumClientSignonRequests = 32U;
inline constexpr std::size_t kPostResourceDiagnosticTextLimit = 256U;

// Sealed, independently authored protocol used only by the explicit neutral
// test/fake-peer profile. These bytes are not claims about stock GoldSrc.
inline constexpr std::array<std::byte, 3U>
    kSyntheticPostResourceRequestTrigger{
        std::byte{0xfeU}, std::byte{0x45U}, std::byte{0x01U}};
inline constexpr std::array<std::byte, 3U>
    kSyntheticPostResourceClientContinuation{
        std::byte{0xfdU}, std::byte{0x45U}, std::byte{0x01U}};
inline constexpr std::array<std::byte, 3U>
    kSyntheticPostResourceBaselinePublication{
        std::byte{0xfcU}, std::byte{0x45U}, std::byte{0x01U}};
inline constexpr std::array<std::byte, 3U>
    kSyntheticPostResourceFullSnapshotPublication{
        std::byte{0xfbU}, std::byte{0x45U}, std::byte{0x01U}};
inline constexpr std::array<std::byte, 3U>
    kSyntheticPostResourceDeltaSnapshotPublication{
        std::byte{0xfaU}, std::byte{0x45U}, std::byte{0x01U}};

struct PostResourceSignonLimits {
    std::size_t maximum_post_resource_payload_bytes{
        kDefaultMaximumPostResourcePayloadBytes};
    std::size_t maximum_post_resource_messages{
        kDefaultMaximumPostResourceMessages};
    std::size_t maximum_client_signon_requests{
        kDefaultMaximumClientSignonRequests};
};

[[nodiscard]] bool valid_post_resource_signon_limits(
    const PostResourceSignonLimits& limits) noexcept;

enum class PostResourceSignonCompatibilityProfile {
    stock_protocol_48_build_10210_evidence_pending,
    synthetic_neutral_v1,
};

enum class PostResourceSignonEvidenceStatus {
    stock_capture_required,
    independently_authored_synthetic_fixture,
};

[[nodiscard]] bool valid_post_resource_signon_profile(
    PostResourceSignonCompatibilityProfile profile) noexcept;

enum class PostResourceServerMessageCategory {
    post_resource_server_message_n,
    synthetic_request_trigger,
    synthetic_baseline_publication,
    synthetic_full_snapshot_publication,
    synthetic_delta_snapshot_publication,
    unsupported_boundary,
};

struct PostResourceMessageMetadata {
    PostResourceServerMessageCategory category{
        PostResourceServerMessageCategory::unsupported_boundary};
    PostResourceSignonEvidenceStatus evidence_status{
        PostResourceSignonEvidenceStatus::stock_capture_required};
    std::uint8_t opcode{0U};
    std::size_t ordinal{0U};
    std::size_t byte_start{0U};
    std::size_t bit_start{0U};
    std::size_t byte_end{0U};
    std::size_t bit_end{0U};
    std::optional<std::size_t> body_byte_count;
    std::size_t remaining_payload_byte_count{0U};
    bool fragmented{false};
    bool reliable{false};
    NetchanDirection direction{NetchanDirection::server_to_client};
    std::uint32_t source_sequence{0U};
    std::size_t decompressed_payload_ordinal{0U};
    bool decompressed{false};
    bool cursor_byte_aligned{true};
};

enum class PostResourceClientRequestKind {
    synthetic_neutral_continue_v1,
};

struct PostResourceClientRequestMetadata {
    PostResourceClientRequestKind kind{
        PostResourceClientRequestKind::synthetic_neutral_continue_v1};
    PostResourceSignonEvidenceStatus evidence_status{
        PostResourceSignonEvidenceStatus::independently_authored_synthetic_fixture};
    std::size_t semantic_byte_count{0U};
    std::size_t trigger_message_ordinal{0U};
    bool reliable_required{true};
};

class EncodedPostResourceClientRequest final {
public:
    EncodedPostResourceClientRequest(
        const EncodedPostResourceClientRequest&) = default;
    EncodedPostResourceClientRequest& operator=(
        const EncodedPostResourceClientRequest&) = delete;
    EncodedPostResourceClientRequest(
        EncodedPostResourceClientRequest&&) noexcept = default;
    EncodedPostResourceClientRequest& operator=(
        EncodedPostResourceClientRequest&&) noexcept = delete;
    ~EncodedPostResourceClientRequest() = default;

    [[nodiscard]] const PostResourceClientRequestMetadata& metadata()
        const noexcept;
    [[nodiscard]] std::span<const std::byte> semantic_bytes() const noexcept;

private:
    friend class PostResourceClientRequestBuilder;

    EncodedPostResourceClientRequest(
        PostResourceClientRequestMetadata metadata,
        std::array<std::byte, 3U> semantic_bytes) noexcept;

    PostResourceClientRequestMetadata metadata_;
    std::array<std::byte, 3U> semantic_bytes_{};
};

enum class PostResourceClientRequestErrorCode {
    invalid_configuration,
    stock_request_layout_evidence_pending,
    request_limit_exceeded,
};

struct PostResourceClientRequestError {
    PostResourceClientRequestErrorCode code{
        PostResourceClientRequestErrorCode::invalid_configuration};
    std::string context;
};

struct PostResourceClientRequestBuildResult {
    std::optional<EncodedPostResourceClientRequest> encoding;
    std::optional<PostResourceClientRequestError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return encoding.has_value();
    }
};

class PostResourceClientRequestBuilder final {
public:
    explicit PostResourceClientRequestBuilder(
        PostResourceSignonLimits limits = {},
        PostResourceSignonCompatibilityProfile profile =
            PostResourceSignonCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] PostResourceClientRequestBuildResult build_first() const;

private:
    PostResourceSignonLimits limits_;
    PostResourceSignonCompatibilityProfile profile_;
};

enum class ServerSignonCompletionCondition {
    evidence_pending,
    synthetic_sequence_in_progress,
    synthetic_full_and_delta_published,
};

class ServerSignonCompletionState final {
public:
    [[nodiscard]] bool completed() const noexcept;
    [[nodiscard]] ServerSignonCompletionCondition condition() const noexcept;
    [[nodiscard]] PostResourceSignonEvidenceStatus evidence_status()
        const noexcept;

    [[nodiscard]] static ServerSignonCompletionState evidence_pending()
        noexcept;
    [[nodiscard]] static ServerSignonCompletionState synthetic_pending()
        noexcept;
    [[nodiscard]] static ServerSignonCompletionState synthetic_completed()
        noexcept;

private:
    ServerSignonCompletionState(
        bool completed,
        ServerSignonCompletionCondition condition,
        PostResourceSignonEvidenceStatus evidence_status) noexcept;

    bool completed_{false};
    ServerSignonCompletionCondition condition_{
        ServerSignonCompletionCondition::evidence_pending};
    PostResourceSignonEvidenceStatus evidence_status_{
        PostResourceSignonEvidenceStatus::stock_capture_required};
};

enum class PostResourceSignonProgress {
    post_resource_boundary,
    client_request_ready,
    synthetic_baseline_publication_observed,
    synthetic_full_snapshot_publication_observed,
    synthetic_delta_snapshot_publication_observed,
    baseline_registry_ready,
    full_snapshot_ready,
    delta_snapshot_ready,
    server_signon_completed,
    unsupported_message,
};

class PostResourceSignonProgressState final {
public:
    [[nodiscard]] PostResourceSignonProgress progress() const noexcept;
    [[nodiscard]] std::uint32_t signon_generation() const noexcept;
    [[nodiscard]] const ServerSignonCompletionState& completion()
        const noexcept;

private:
    friend class PostResourceSignonStreamDecoder;
    friend class PostResourceSignonBoundaryState;

    PostResourceSignonProgressState(
        PostResourceSignonProgress progress,
        std::uint32_t signon_generation,
        ServerSignonCompletionState completion) noexcept;

    PostResourceSignonProgress progress_{
        PostResourceSignonProgress::post_resource_boundary};
    std::uint32_t signon_generation_{0U};
    ServerSignonCompletionState completion_{
        ServerSignonCompletionState::evidence_pending()};
};

class PostResourceSignonTranscriptState final {
public:
    [[nodiscard]] const std::vector<PostResourceMessageMetadata>&
    server_messages() const noexcept;
    [[nodiscard]] const std::vector<PostResourceClientRequestMetadata>&
    client_requests() const noexcept;
    [[nodiscard]] PostResourceSignonCompatibilityProfile profile()
        const noexcept;

private:
    friend class PostResourceSignonStreamDecoder;
    friend class PostResourceSignonBoundaryState;

    PostResourceSignonTranscriptState(
        std::vector<PostResourceMessageMetadata> server_messages,
        std::vector<PostResourceClientRequestMetadata> client_requests,
        PostResourceSignonCompatibilityProfile profile) noexcept;

    std::vector<PostResourceMessageMetadata> server_messages_;
    std::vector<PostResourceClientRequestMetadata> client_requests_;
    PostResourceSignonCompatibilityProfile profile_{
        PostResourceSignonCompatibilityProfile::
            stock_protocol_48_build_10210_evidence_pending};
};

class PostResourceSignonBoundaryState final {
public:
    [[nodiscard]] const PostResourceSignonTranscriptState& transcript()
        const noexcept;
    [[nodiscard]] const PostResourceSignonProgressState& progress()
        const noexcept;
    [[nodiscard]] std::size_t next_byte_offset() const noexcept;
    [[nodiscard]] std::size_t next_bit_offset() const noexcept;
    [[nodiscard]] bool unsupported_boundary() const noexcept;

private:
    friend class PostResourceSignonStreamDecoder;
    friend class PostResourceEntitySnapshotStage;

    [[nodiscard]] std::optional<PostResourceSignonBoundaryState>
    with_transcript(
        std::span<const PostResourceMessageMetadata> server_messages,
        std::span<const PostResourceClientRequestMetadata> client_requests)
        const;
    [[nodiscard]] std::optional<PostResourceSignonBoundaryState>
    with_completed_synthetic_sequence() const;
    [[nodiscard]] std::optional<PostResourceSignonBoundaryState>
    with_applied_synthetic_publication(
        PostResourceSignonProgress published_progress) const;

    PostResourceSignonBoundaryState(
        PostResourceSignonTranscriptState transcript,
        PostResourceSignonProgressState progress,
        std::size_t next_byte_offset,
        std::size_t next_bit_offset,
        bool unsupported_boundary) noexcept;

    PostResourceSignonTranscriptState transcript_;
    PostResourceSignonProgressState progress_;
    std::size_t next_byte_offset_{0U};
    std::size_t next_bit_offset_{0U};
    bool unsupported_boundary_{false};
};

enum class PostResourceSignonStreamErrorCode {
    invalid_configuration,
    invalid_boundary_geometry,
    payload_too_large,
    missing_delta_registry,
    message_limit_exceeded,
    synthetic_fixture_mismatch,
};

struct PostResourceSignonStreamError {
    PostResourceSignonStreamErrorCode code{
        PostResourceSignonStreamErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::size_t bit_offset{0U};
    std::string context;
};

struct PostResourceSignonStreamDecodeResult {
    std::optional<PostResourceSignonBoundaryState> state;
    std::optional<PostResourceSignonStreamError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

// Production stock mode deliberately stops at the first unconfirmed body and
// leaves its exact cursor unchanged. The synthetic profile is sealed and is
// accepted only for independently authored tests/fake peers.
class PostResourceSignonStreamDecoder final {
public:
    explicit PostResourceSignonStreamDecoder(
        PostResourceSignonLimits limits = {},
        PostResourceSignonCompatibilityProfile profile =
            PostResourceSignonCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] PostResourceSignonStreamDecodeResult decode(
        const OwnedServicePayload& payload,
        const PostResourceResponseBoundary& boundary,
        const DeltaSchemaRegistryState& registry) const;

private:
    PostResourceSignonLimits limits_;
    PostResourceSignonCompatibilityProfile profile_;
};

[[nodiscard]] constexpr std::string_view to_string(
    PostResourceSignonStreamErrorCode code) noexcept
{
    switch (code) {
    case PostResourceSignonStreamErrorCode::invalid_configuration:
        return "invalid_configuration";
    case PostResourceSignonStreamErrorCode::invalid_boundary_geometry:
        return "invalid_boundary_geometry";
    case PostResourceSignonStreamErrorCode::payload_too_large:
        return "payload_too_large";
    case PostResourceSignonStreamErrorCode::missing_delta_registry:
        return "missing_delta_registry";
    case PostResourceSignonStreamErrorCode::message_limit_exceeded:
        return "message_limit_exceeded";
    case PostResourceSignonStreamErrorCode::synthetic_fixture_mismatch:
        return "synthetic_fixture_mismatch";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
