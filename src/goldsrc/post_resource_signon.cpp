#include <hlclient/goldsrc/post_resource_signon.hpp>

#include <algorithm>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] PostResourceSignonStreamDecodeResult failure(
    const PostResourceSignonStreamErrorCode code,
    const std::size_t byte_offset,
    const std::size_t bit_offset,
    const std::string_view context)
{
    PostResourceSignonStreamError error;
    error.code = code;
    error.byte_offset = byte_offset;
    error.bit_offset = bit_offset;
    const auto bounded = context.substr(
        0U, (std::min)(context.size(), kPostResourceDiagnosticTextLimit));
    error.context.assign(bounded.data(), bounded.size());
    return {std::nullopt, std::move(error)};
}

[[nodiscard]] PostResourceClientRequestBuildResult request_failure(
    const PostResourceClientRequestErrorCode code,
    const std::string_view context)
{
    PostResourceClientRequestError error;
    error.code = code;
    const auto bounded = context.substr(
        0U, (std::min)(context.size(), kPostResourceDiagnosticTextLimit));
    error.context.assign(bounded.data(), bounded.size());
    return {std::nullopt, std::move(error)};
}

} // namespace

bool valid_post_resource_signon_limits(
    const PostResourceSignonLimits& limits) noexcept
{
    return limits.maximum_post_resource_payload_bytes != 0U &&
           limits.maximum_post_resource_payload_bytes <=
               kMaximumPostResourcePayloadBytes &&
           limits.maximum_post_resource_messages != 0U &&
           limits.maximum_post_resource_messages <=
               kMaximumPostResourceMessages &&
           limits.maximum_client_signon_requests != 0U &&
           limits.maximum_client_signon_requests <=
               kMaximumClientSignonRequests;
}

bool valid_post_resource_signon_profile(
    const PostResourceSignonCompatibilityProfile profile) noexcept
{
    switch (profile) {
    case PostResourceSignonCompatibilityProfile::
        stock_protocol_48_build_10210_evidence_pending:
    case PostResourceSignonCompatibilityProfile::synthetic_neutral_v1:
        return true;
    }
    return false;
}

EncodedPostResourceClientRequest::EncodedPostResourceClientRequest(
    PostResourceClientRequestMetadata metadata,
    const std::array<std::byte, 3U> semantic_bytes) noexcept
    : metadata_{std::move(metadata)}, semantic_bytes_{semantic_bytes}
{
}

const PostResourceClientRequestMetadata&
EncodedPostResourceClientRequest::metadata() const noexcept
{
    return metadata_;
}

std::span<const std::byte>
EncodedPostResourceClientRequest::semantic_bytes() const noexcept
{
    return semantic_bytes_;
}

PostResourceClientRequestBuilder::PostResourceClientRequestBuilder(
    PostResourceSignonLimits limits,
    const PostResourceSignonCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool PostResourceClientRequestBuilder::valid_configuration() const noexcept
{
    return valid_post_resource_signon_limits(limits_) &&
           valid_post_resource_signon_profile(profile_);
}

PostResourceClientRequestBuildResult
PostResourceClientRequestBuilder::build_first() const
{
    if (!valid_configuration()) {
        return request_failure(
            PostResourceClientRequestErrorCode::invalid_configuration,
            "Post-resource request limits are outside project hard caps");
    }
    if (profile_ == PostResourceSignonCompatibilityProfile::
                        stock_protocol_48_build_10210_evidence_pending) {
        return request_failure(
            PostResourceClientRequestErrorCode::
                stock_request_layout_evidence_pending,
            "Stock continuation request layout requires an accepted capture");
    }
    if (limits_.maximum_client_signon_requests < 1U) {
        return request_failure(
            PostResourceClientRequestErrorCode::request_limit_exceeded,
            "No request slot is available");
    }
    return PostResourceClientRequestBuildResult{
        EncodedPostResourceClientRequest{
            PostResourceClientRequestMetadata{
                PostResourceClientRequestKind::synthetic_neutral_continue_v1,
                PostResourceSignonEvidenceStatus::
                    independently_authored_synthetic_fixture,
            kSyntheticPostResourceClientContinuation.size(),
                0U,
                true},
            kSyntheticPostResourceClientContinuation},
        std::nullopt};
}

ServerSignonCompletionState::ServerSignonCompletionState(
    const bool completed,
    const ServerSignonCompletionCondition condition,
    const PostResourceSignonEvidenceStatus evidence_status) noexcept
    : completed_{completed},
      condition_{condition},
      evidence_status_{evidence_status}
{
}

bool ServerSignonCompletionState::completed() const noexcept
{
    return completed_;
}

ServerSignonCompletionCondition
ServerSignonCompletionState::condition() const noexcept
{
    return condition_;
}

PostResourceSignonEvidenceStatus
ServerSignonCompletionState::evidence_status() const noexcept
{
    return evidence_status_;
}

ServerSignonCompletionState
ServerSignonCompletionState::evidence_pending() noexcept
{
    return {false,
            ServerSignonCompletionCondition::evidence_pending,
            PostResourceSignonEvidenceStatus::stock_capture_required};
}

ServerSignonCompletionState
ServerSignonCompletionState::synthetic_completed() noexcept
{
    return {true,
            ServerSignonCompletionCondition::synthetic_full_and_delta_published,
            PostResourceSignonEvidenceStatus::
                independently_authored_synthetic_fixture};
}

ServerSignonCompletionState
ServerSignonCompletionState::synthetic_pending() noexcept
{
    return {false,
            ServerSignonCompletionCondition::synthetic_sequence_in_progress,
            PostResourceSignonEvidenceStatus::
                independently_authored_synthetic_fixture};
}

PostResourceSignonProgressState::PostResourceSignonProgressState(
    const PostResourceSignonProgress progress,
    const std::uint32_t signon_generation,
    ServerSignonCompletionState completion) noexcept
    : progress_{progress},
      signon_generation_{signon_generation},
      completion_{std::move(completion)}
{
}

PostResourceSignonProgress
PostResourceSignonProgressState::progress() const noexcept
{
    return progress_;
}

std::uint32_t
PostResourceSignonProgressState::signon_generation() const noexcept
{
    return signon_generation_;
}

const ServerSignonCompletionState&
PostResourceSignonProgressState::completion() const noexcept
{
    return completion_;
}

PostResourceSignonTranscriptState::PostResourceSignonTranscriptState(
    std::vector<PostResourceMessageMetadata> server_messages,
    std::vector<PostResourceClientRequestMetadata> client_requests,
    const PostResourceSignonCompatibilityProfile profile) noexcept
    : server_messages_{std::move(server_messages)},
      client_requests_{std::move(client_requests)},
      profile_{profile}
{
}

const std::vector<PostResourceMessageMetadata>&
PostResourceSignonTranscriptState::server_messages() const noexcept
{
    return server_messages_;
}

const std::vector<PostResourceClientRequestMetadata>&
PostResourceSignonTranscriptState::client_requests() const noexcept
{
    return client_requests_;
}

PostResourceSignonCompatibilityProfile
PostResourceSignonTranscriptState::profile() const noexcept
{
    return profile_;
}

PostResourceSignonBoundaryState::PostResourceSignonBoundaryState(
    PostResourceSignonTranscriptState transcript,
    PostResourceSignonProgressState progress,
    const std::size_t next_byte_offset,
    const std::size_t next_bit_offset,
    const bool unsupported_boundary) noexcept
    : transcript_{std::move(transcript)},
      progress_{std::move(progress)},
      next_byte_offset_{next_byte_offset},
      next_bit_offset_{next_bit_offset},
      unsupported_boundary_{unsupported_boundary}
{
}

const PostResourceSignonTranscriptState&
PostResourceSignonBoundaryState::transcript() const noexcept
{
    return transcript_;
}

const PostResourceSignonProgressState&
PostResourceSignonBoundaryState::progress() const noexcept
{
    return progress_;
}

std::size_t PostResourceSignonBoundaryState::next_byte_offset() const noexcept
{
    return next_byte_offset_;
}

std::size_t PostResourceSignonBoundaryState::next_bit_offset() const noexcept
{
    return next_bit_offset_;
}

bool PostResourceSignonBoundaryState::unsupported_boundary() const noexcept
{
    return unsupported_boundary_;
}

std::optional<PostResourceSignonBoundaryState>
PostResourceSignonBoundaryState::with_transcript(
    const std::span<const PostResourceMessageMetadata> server_messages,
    const std::span<const PostResourceClientRequestMetadata> client_requests)
    const
{
    if (server_messages.empty() ||
        server_messages.size() > kMaximumPostResourceMessages ||
        client_requests.size() > kMaximumClientSignonRequests) {
        return std::nullopt;
    }
    return PostResourceSignonBoundaryState{
        PostResourceSignonTranscriptState{
            std::vector<PostResourceMessageMetadata>{
                server_messages.begin(), server_messages.end()},
            std::vector<PostResourceClientRequestMetadata>{
                client_requests.begin(), client_requests.end()},
            transcript_.profile()},
        progress_,
        next_byte_offset_,
        next_bit_offset_,
        unsupported_boundary_};
}

std::optional<PostResourceSignonBoundaryState>
PostResourceSignonBoundaryState::with_completed_synthetic_sequence() const
{
    if (unsupported_boundary_ ||
        transcript_.profile() !=
            PostResourceSignonCompatibilityProfile::synthetic_neutral_v1 ||
        progress_.progress() !=
            PostResourceSignonProgress::
                synthetic_delta_snapshot_publication_observed ||
        transcript_.server_messages().empty() ||
        transcript_.server_messages().back().category !=
            PostResourceServerMessageCategory::
                synthetic_delta_snapshot_publication) {
        return std::nullopt;
    }
    return PostResourceSignonBoundaryState{
        transcript_,
        PostResourceSignonProgressState{
            PostResourceSignonProgress::server_signon_completed,
            progress_.signon_generation(),
            ServerSignonCompletionState::synthetic_completed()},
        next_byte_offset_,
        next_bit_offset_,
        false};
}

std::optional<PostResourceSignonBoundaryState>
PostResourceSignonBoundaryState::with_applied_synthetic_publication(
    const PostResourceSignonProgress published_progress) const
{
    const auto observed = progress_.progress();
    if (transcript_.server_messages().empty()) {
        return std::nullopt;
    }
    const auto last_category =
        transcript_.server_messages().back().category;
    const bool valid_transition =
        (observed == PostResourceSignonProgress::
                         synthetic_baseline_publication_observed &&
         published_progress ==
             PostResourceSignonProgress::baseline_registry_ready &&
         last_category == PostResourceServerMessageCategory::
                              synthetic_baseline_publication) ||
        (observed == PostResourceSignonProgress::
                         synthetic_full_snapshot_publication_observed &&
         published_progress == PostResourceSignonProgress::full_snapshot_ready &&
         last_category == PostResourceServerMessageCategory::
                              synthetic_full_snapshot_publication);
    if (unsupported_boundary_ ||
        transcript_.profile() !=
            PostResourceSignonCompatibilityProfile::synthetic_neutral_v1 ||
        !valid_transition) {
        return std::nullopt;
    }
    return PostResourceSignonBoundaryState{
        transcript_,
        PostResourceSignonProgressState{
            published_progress,
            progress_.signon_generation(),
            ServerSignonCompletionState::synthetic_pending()},
        next_byte_offset_,
        next_bit_offset_,
        false};
}

PostResourceSignonStreamDecoder::PostResourceSignonStreamDecoder(
    PostResourceSignonLimits limits,
    const PostResourceSignonCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool PostResourceSignonStreamDecoder::valid_configuration() const noexcept
{
    return valid_post_resource_signon_limits(limits_) &&
           valid_post_resource_signon_profile(profile_);
}

PostResourceSignonStreamDecodeResult PostResourceSignonStreamDecoder::decode(
    const OwnedServicePayload& payload,
    const PostResourceResponseBoundary& boundary,
    const DeltaSchemaRegistryState& registry) const
{
    if (!valid_configuration()) {
        return failure(
            PostResourceSignonStreamErrorCode::invalid_configuration,
            0U,
            0U,
            "Post-resource stream limits are outside project hard caps");
    }
    if (payload.bytes.size() > limits_.maximum_post_resource_payload_bytes) {
        return failure(
            PostResourceSignonStreamErrorCode::payload_too_large,
            0U,
            0U,
            "Post-resource payload exceeds the configured bound");
    }
    if (registry.schema_count() == 0U) {
        return failure(
            PostResourceSignonStreamErrorCode::missing_delta_registry,
            0U,
            0U,
            "Post-resource decoding requires the existing delta registry");
    }
    const auto& source = boundary.source_payload();
    const bool source_matches =
        source.direction == payload.direction &&
        source.source_sequence == payload.source_sequence &&
        source.reliable == payload.source_reliable &&
        source.reassembled == payload.reassembled &&
        source.decompressed == payload.decompressed &&
        source.decoded_payload_byte_count == payload.bytes.size();
    const bool boundary_matches =
        boundary.byte_offset() == 0U && boundary.bit_offset() == 0U &&
        boundary.kind() == PostResourceResponseBoundaryKind::
                               opcode_at_payload_start &&
        boundary.opcode().has_value() && !payload.bytes.empty() &&
        *boundary.opcode() == std::to_integer<std::uint8_t>(payload.bytes[0]) &&
        boundary.remaining_byte_count() == payload.bytes.size() - 1U;
    if (!source_matches || !boundary_matches) {
        return failure(
            PostResourceSignonStreamErrorCode::invalid_boundary_geometry,
            boundary.byte_offset(),
            boundary.bit_offset(),
            "Post-resource boundary does not match its owning payload");
    }

    if (profile_ == PostResourceSignonCompatibilityProfile::
                        stock_protocol_48_build_10210_evidence_pending) {
        std::vector<PostResourceMessageMetadata> messages;
        messages.push_back(PostResourceMessageMetadata{
            PostResourceServerMessageCategory::unsupported_boundary,
            PostResourceSignonEvidenceStatus::stock_capture_required,
            *boundary.opcode(),
            0U,
            boundary.byte_offset(),
            boundary.bit_offset(),
            boundary.byte_offset(),
            boundary.bit_offset(),
            std::nullopt,
            boundary.remaining_byte_count(),
            payload.reassembled,
            payload.source_reliable,
            payload.direction,
            payload.source_sequence,
            0U,
            payload.decompressed,
            boundary.bit_offset() == 0U});
        return PostResourceSignonStreamDecodeResult{
            PostResourceSignonBoundaryState{
                PostResourceSignonTranscriptState{
                    std::move(messages), {}, profile_},
                PostResourceSignonProgressState{
                    PostResourceSignonProgress::unsupported_message,
                    0U,
                    ServerSignonCompletionState::evidence_pending()},
                boundary.byte_offset(),
                boundary.bit_offset(),
                true},
            std::nullopt};
    }

    const auto matches = [&payload](const auto& fixture) noexcept {
        return payload.bytes.size() == fixture.size() &&
               std::equal(
                   payload.bytes.begin(),
                   payload.bytes.end(),
                   fixture.begin());
    };
    const bool request_trigger =
        matches(kSyntheticPostResourceRequestTrigger);
    const bool baseline_publication =
        matches(kSyntheticPostResourceBaselinePublication);
    const bool full_snapshot_publication =
        matches(kSyntheticPostResourceFullSnapshotPublication);
    const bool delta_snapshot_publication =
        matches(kSyntheticPostResourceDeltaSnapshotPublication);
    if (!request_trigger && !baseline_publication &&
        !full_snapshot_publication && !delta_snapshot_publication) {
        return failure(
            PostResourceSignonStreamErrorCode::synthetic_fixture_mismatch,
            0U,
            0U,
            "Synthetic post-resource message does not match neutral v1");
    }
    const auto category = request_trigger
        ? PostResourceServerMessageCategory::synthetic_request_trigger
        : baseline_publication
            ? PostResourceServerMessageCategory::
                  synthetic_baseline_publication
            : full_snapshot_publication
                ? PostResourceServerMessageCategory::
                      synthetic_full_snapshot_publication
                : PostResourceServerMessageCategory::
                      synthetic_delta_snapshot_publication;
    const auto progress = request_trigger
        ? PostResourceSignonProgress::client_request_ready
        : baseline_publication
            ? PostResourceSignonProgress::
                  synthetic_baseline_publication_observed
            : full_snapshot_publication
                ? PostResourceSignonProgress::
                      synthetic_full_snapshot_publication_observed
                : PostResourceSignonProgress::
                      synthetic_delta_snapshot_publication_observed;
    constexpr std::uint32_t generation = 1U;
    std::vector<PostResourceMessageMetadata> messages;
    messages.push_back(PostResourceMessageMetadata{
        category,
        PostResourceSignonEvidenceStatus::
            independently_authored_synthetic_fixture,
        std::to_integer<std::uint8_t>(payload.bytes[0]),
        0U,
        0U,
        0U,
        payload.bytes.size(),
        0U,
        payload.bytes.size() - 1U,
        payload.bytes.size() - 1U,
        payload.reassembled,
        payload.source_reliable,
        payload.direction,
        payload.source_sequence,
        0U,
        payload.decompressed,
        true});
    std::vector<PostResourceClientRequestMetadata> requests;
    if (request_trigger) {
        auto request =
            PostResourceClientRequestBuilder{limits_, profile_}.build_first();
        if (!request || !request.encoding) {
            return failure(
                PostResourceSignonStreamErrorCode::invalid_configuration,
                0U,
                0U,
                "Synthetic request builder could not publish its fixed request");
        }
        requests.push_back(request.encoding->metadata());
    }
    return PostResourceSignonStreamDecodeResult{
        PostResourceSignonBoundaryState{
            PostResourceSignonTranscriptState{
                std::move(messages), std::move(requests), profile_},
            PostResourceSignonProgressState{
                progress,
                generation,
                ServerSignonCompletionState::synthetic_pending()},
            payload.bytes.size(),
            0U,
            false},
        std::nullopt};
}

} // namespace hlclient::goldsrc
