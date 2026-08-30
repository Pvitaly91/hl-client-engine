#pragma once

#include <hlclient/goldsrc/stock_runtime_capture.hpp>
#include <hlclient/goldsrc/stock_runtime_transport_journal.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::string_view kStockRuntimeResearchRunSchema =
    "hlclient.stock-runtime-research-run.v1";
inline constexpr std::string_view kStockRuntimeVersionObservationSchema =
    "hlclient.stock-runtime-version-observation.v1";
inline constexpr std::string_view kStockRuntimeIsolationAttestationSchema =
    "hlclient.stock-runtime-isolation-attestation.v1";
inline constexpr std::string_view kStockRuntimeRestorationAttestationSchema =
    "hlclient.stock-runtime-restoration.v1";

enum class StockRuntimeCaptureCorpusLoadPolicy {
    prepublication,
    published,
};

enum class StockRuntimeCaptureCorpusPublicationState {
    ready_for_manifest_publication,
    published_incomplete,
    published_accepted,
};

struct StockRuntimeCaptureCorpusLimits final {
    StockRuntimeTransportJournalLimits journal{};
    std::size_t maximum_manifest_bytes{1U * 1'024U * 1'024U};
    std::size_t maximum_journal_bytes{64U * 1'024U * 1'024U};
    std::size_t maximum_log_files{16U};
    std::uint64_t maximum_total_log_bytes{16U * 1'024U * 1'024U};
};

enum class StockRuntimeCaptureCorpusErrorCode {
    invalid_configuration,
    unsafe_run_path,
    invalid_run_id,
    reparse_point,
    hardlink_detected,
    missing_directory,
    missing_manifest,
    unexpected_manifest,
    unexpected_file,
    invalid_filename,
    file_not_regular,
    file_too_large,
    open_failed,
    read_failed,
    invalid_json,
    duplicate_property,
    wrong_schema,
    wrong_run_id,
    invalid_capture_metadata,
    invalid_journal,
    missing_raw_file,
    unexpected_raw_file,
    raw_size_mismatch,
    raw_hash_mismatch,
    count_mismatch,
    byte_count_mismatch,
    publication_state_mismatch,
    structural_hash_failed,
};

struct StockRuntimeCaptureCorpusError final {
    StockRuntimeCaptureCorpusErrorCode code{
        StockRuntimeCaptureCorpusErrorCode::invalid_configuration};
    std::size_t ordinal{0U};
    std::string context;
    std::optional<StockRuntimeTransportJournalErrorCode> journal_code;
};

struct StockRuntimeCorpusDocument final {
    std::string schema;
    std::string structural_sha256;
};

class StockRuntimeCorpusObservedDatagram final {
public:
    [[nodiscard]] const StockRuntimeTransportJournalEntry& journal() const noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

private:
    friend class StockRuntimeCaptureCorpusLoader;

    StockRuntimeCorpusObservedDatagram(
        StockRuntimeTransportJournalEntry journal,
        std::shared_ptr<const std::vector<std::byte>> bytes) noexcept;

    StockRuntimeTransportJournalEntry journal_;
    std::shared_ptr<const std::vector<std::byte>> bytes_;
};

class StockRuntimeCorpusDeliveredDatagram final {
public:
    [[nodiscard]] std::size_t delivery_ordinal() const noexcept;
    [[nodiscard]] std::size_t observed_ordinal() const noexcept;
    [[nodiscard]] StockRuntimeCaptureDirection direction() const noexcept;
    [[nodiscard]] std::size_t direction_ordinal() const noexcept;
    [[nodiscard]] std::uint64_t observed_relative_timestamp_us() const noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

private:
    friend class StockRuntimeCaptureCorpusLoader;

    StockRuntimeCorpusDeliveredDatagram(
        std::size_t delivery_ordinal,
        const StockRuntimeTransportJournalEntry& journal,
        std::shared_ptr<const std::vector<std::byte>> bytes) noexcept;

    std::size_t delivery_ordinal_{0U};
    std::size_t observed_ordinal_{0U};
    StockRuntimeCaptureDirection direction_{
        StockRuntimeCaptureDirection::client_to_server};
    std::size_t direction_ordinal_{1U};
    std::uint64_t observed_relative_timestamp_us_{0U};
    std::shared_ptr<const std::vector<std::byte>> bytes_;
};

// Exact, sanitized claims from an accepted final run manifest.  The checker
// independently reconstructs the same value after offline replay and compares
// it as one typed object before it may report accepted-run=true.
struct StockRuntimeAcceptedManifestClaims final {
    std::size_t reassembled_payload_count{0U};
    std::size_t decompressed_payload_count{0U};
    std::size_t replay_payload_ordinal{0U};
    std::size_t corpus_observed_ordinal{0U};
    std::size_t delivery_ordinal{0U};
    std::size_t byte_offset{0U};
    std::size_t bit_offset{0U};
    std::uint64_t source_netchan_sequence{0U};
    std::size_t source_payload_byte_count{0U};
    std::size_t source_payload_bit_count{0U};
    std::size_t next_unconsumed_bit_count{0U};
    bool reassembled{false};
    bool decompressed{false};
    bool byte_aligned{false};
    std::string first_candidate;
    std::size_t candidate_bit_width{0U};
    std::size_t candidate_recurrence{0U};
    std::string candidate_stability;
    std::string replay_structural_sha256;

    [[nodiscard]] bool operator==(
        const StockRuntimeAcceptedManifestClaims&) const = default;
};

// Fully owning, immutable publication. It deliberately retains no native path.
// Raw bytes exist only in this research/checker state and are never exposed as
// text, diagnostics, structural hashes, or production runtime state.
class StockRuntimeCaptureCorpusState final {
public:
    StockRuntimeCaptureCorpusState(const StockRuntimeCaptureCorpusState&) = default;
    StockRuntimeCaptureCorpusState& operator=(
        const StockRuntimeCaptureCorpusState&) = delete;
    StockRuntimeCaptureCorpusState(StockRuntimeCaptureCorpusState&&) noexcept = default;
    StockRuntimeCaptureCorpusState& operator=(
        StockRuntimeCaptureCorpusState&&) noexcept = delete;
    ~StockRuntimeCaptureCorpusState() = default;

    [[nodiscard]] std::string_view run_id() const noexcept;
    [[nodiscard]] const StockRuntimeCaptureMetadata& capture_metadata() const noexcept;
    [[nodiscard]] StockRuntimeCaptureCorpusLoadPolicy load_policy() const noexcept;
    [[nodiscard]] StockRuntimeCaptureCorpusPublicationState publication_state() const noexcept;
    [[nodiscard]] bool accepted_evidence_run() const noexcept;
    [[nodiscard]] const std::vector<StockRuntimeCorpusObservedDatagram>&
    observed_datagrams() const noexcept;
    [[nodiscard]] const std::vector<StockRuntimeCorpusDeliveredDatagram>&
    delivered_datagrams() const noexcept;
    [[nodiscard]] const std::vector<StockRuntimeCorpusDeliveredDatagram>&
    delivered_client_to_server() const noexcept;
    [[nodiscard]] const std::vector<StockRuntimeCorpusDeliveredDatagram>&
    delivered_server_to_client() const noexcept;
    [[nodiscard]] const StockRuntimeCorpusDocument& version_observation() const noexcept;
    [[nodiscard]] const StockRuntimeCorpusDocument& isolation_attestation() const noexcept;
    [[nodiscard]] const StockRuntimeCorpusDocument& restoration_attestation() const noexcept;
    [[nodiscard]] const std::optional<StockRuntimeCorpusDocument>&
    research_run_metadata() const noexcept;
    [[nodiscard]] const std::optional<StockRuntimeAcceptedManifestClaims>&
    accepted_manifest_claims() const noexcept;
    // Geometry-only identity. It excludes raw bytes and per-datagram SHA-256,
    // because connectionless packets can contain opaque authentication bytes.
    // Local raw SHA-256 remains an internal corpus-integrity check only.
    [[nodiscard]] std::string_view structural_sha256() const noexcept;

private:
    friend class StockRuntimeCaptureCorpusLoader;

    StockRuntimeCaptureCorpusState(
        std::string run_id,
        StockRuntimeCaptureMetadata capture_metadata,
        StockRuntimeCaptureCorpusLoadPolicy load_policy,
        StockRuntimeCaptureCorpusPublicationState publication_state,
        std::vector<StockRuntimeCorpusObservedDatagram> observed_datagrams,
        std::vector<StockRuntimeCorpusDeliveredDatagram> delivered_datagrams,
        std::vector<StockRuntimeCorpusDeliveredDatagram>
            delivered_client_to_server,
        std::vector<StockRuntimeCorpusDeliveredDatagram>
            delivered_server_to_client,
        StockRuntimeCorpusDocument version_observation,
        StockRuntimeCorpusDocument isolation_attestation,
        StockRuntimeCorpusDocument restoration_attestation,
        std::optional<StockRuntimeCorpusDocument> research_run_metadata,
        std::optional<StockRuntimeAcceptedManifestClaims> accepted_manifest_claims,
        std::string structural_sha256) noexcept;

    std::string run_id_;
    StockRuntimeCaptureMetadata capture_metadata_;
    StockRuntimeCaptureCorpusLoadPolicy load_policy_{
        StockRuntimeCaptureCorpusLoadPolicy::prepublication};
    StockRuntimeCaptureCorpusPublicationState publication_state_{
        StockRuntimeCaptureCorpusPublicationState::ready_for_manifest_publication};
    std::vector<StockRuntimeCorpusObservedDatagram> observed_datagrams_;
    // The first vector is exact global peer-visible order. Direction-specific
    // views share their immutable byte storage and retain global ordinals.
    std::vector<StockRuntimeCorpusDeliveredDatagram> delivered_datagrams_;
    std::vector<StockRuntimeCorpusDeliveredDatagram>
        delivered_client_to_server_;
    std::vector<StockRuntimeCorpusDeliveredDatagram>
        delivered_server_to_client_;
    StockRuntimeCorpusDocument version_observation_;
    StockRuntimeCorpusDocument isolation_attestation_;
    StockRuntimeCorpusDocument restoration_attestation_;
    std::optional<StockRuntimeCorpusDocument> research_run_metadata_;
    std::optional<StockRuntimeAcceptedManifestClaims> accepted_manifest_claims_;
    std::string structural_sha256_;
};

struct StockRuntimeCaptureCorpusLoadResult final {
    std::optional<StockRuntimeCaptureCorpusState> state;
    std::optional<StockRuntimeCaptureCorpusError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class StockRuntimeCaptureCorpusLoader final {
public:
    explicit StockRuntimeCaptureCorpusLoader(
        StockRuntimeCaptureCorpusLimits limits = {}) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const StockRuntimeCaptureCorpusLimits& limits() const noexcept;
    [[nodiscard]] StockRuntimeCaptureCorpusLoadResult load(
        const std::filesystem::path& exact_run_directory,
        StockRuntimeCaptureCorpusLoadPolicy policy) const;

private:
    StockRuntimeCaptureCorpusLimits limits_;
};

[[nodiscard]] constexpr std::string_view to_string(
    StockRuntimeCaptureCorpusPublicationState state) noexcept
{
    switch (state) {
    case StockRuntimeCaptureCorpusPublicationState::ready_for_manifest_publication:
        return "ready_for_manifest_publication";
    case StockRuntimeCaptureCorpusPublicationState::published_incomplete:
        return "published_incomplete";
    case StockRuntimeCaptureCorpusPublicationState::published_accepted:
        return "published_accepted";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    StockRuntimeCaptureCorpusErrorCode code) noexcept
{
    switch (code) {
    case StockRuntimeCaptureCorpusErrorCode::invalid_configuration: return "invalid_configuration";
    case StockRuntimeCaptureCorpusErrorCode::unsafe_run_path: return "unsafe_run_path";
    case StockRuntimeCaptureCorpusErrorCode::invalid_run_id: return "invalid_run_id";
    case StockRuntimeCaptureCorpusErrorCode::reparse_point: return "reparse_point";
    case StockRuntimeCaptureCorpusErrorCode::hardlink_detected: return "hardlink_detected";
    case StockRuntimeCaptureCorpusErrorCode::missing_directory: return "missing_directory";
    case StockRuntimeCaptureCorpusErrorCode::missing_manifest: return "missing_manifest";
    case StockRuntimeCaptureCorpusErrorCode::unexpected_manifest: return "unexpected_manifest";
    case StockRuntimeCaptureCorpusErrorCode::unexpected_file: return "unexpected_file";
    case StockRuntimeCaptureCorpusErrorCode::invalid_filename: return "invalid_filename";
    case StockRuntimeCaptureCorpusErrorCode::file_not_regular: return "file_not_regular";
    case StockRuntimeCaptureCorpusErrorCode::file_too_large: return "file_too_large";
    case StockRuntimeCaptureCorpusErrorCode::open_failed: return "open_failed";
    case StockRuntimeCaptureCorpusErrorCode::read_failed: return "read_failed";
    case StockRuntimeCaptureCorpusErrorCode::invalid_json: return "invalid_json";
    case StockRuntimeCaptureCorpusErrorCode::duplicate_property: return "duplicate_property";
    case StockRuntimeCaptureCorpusErrorCode::wrong_schema: return "wrong_schema";
    case StockRuntimeCaptureCorpusErrorCode::wrong_run_id: return "wrong_run_id";
    case StockRuntimeCaptureCorpusErrorCode::invalid_capture_metadata: return "invalid_capture_metadata";
    case StockRuntimeCaptureCorpusErrorCode::invalid_journal: return "invalid_journal";
    case StockRuntimeCaptureCorpusErrorCode::missing_raw_file: return "missing_raw_file";
    case StockRuntimeCaptureCorpusErrorCode::unexpected_raw_file: return "unexpected_raw_file";
    case StockRuntimeCaptureCorpusErrorCode::raw_size_mismatch: return "raw_size_mismatch";
    case StockRuntimeCaptureCorpusErrorCode::raw_hash_mismatch: return "raw_hash_mismatch";
    case StockRuntimeCaptureCorpusErrorCode::count_mismatch: return "count_mismatch";
    case StockRuntimeCaptureCorpusErrorCode::byte_count_mismatch: return "byte_count_mismatch";
    case StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch: return "publication_state_mismatch";
    case StockRuntimeCaptureCorpusErrorCode::structural_hash_failed: return "structural_hash_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
