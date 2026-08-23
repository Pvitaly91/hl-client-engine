#pragma once

#include <hlclient/assets/asset_source.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::local_assets {

inline constexpr std::uint64_t kDefaultMaximumLocalAssetSourceBytes =
    local_resources::kDefaultMaximumLocalResourceFileSize;
inline constexpr std::uint64_t kHardMaximumLocalAssetSourceBytes =
    local_resources::kHardMaximumLocalResourceFileSize;
inline constexpr std::size_t kDefaultLocalAssetSourceReadChunkBytes =
    local_resources::kDefaultLocalResourceReadChunkSize;
inline constexpr std::size_t kHardMaximumLocalAssetSourceReadChunkBytes =
    local_resources::kHardMaximumLocalResourceReadChunkSize;
inline constexpr std::size_t kDefaultMaximumLocalAssetSourceChunksPerUpdate =
    1U;
inline constexpr std::size_t kHardMaximumLocalAssetSourceChunksPerUpdate =
    64U;
inline constexpr std::size_t kMaximumSimultaneouslyOpenLocalAssetSources = 1U;
inline constexpr std::chrono::milliseconds kHardMaximumLocalAssetSourceTimeout{
    60'000};
inline constexpr std::size_t kLocalAssetSourceDiagnosticTextLimit = 256U;

using LocalAssetSourceOpenTimePoint = std::chrono::steady_clock::time_point;

struct LocalAssetSourceOpenLimits {
    std::uint64_t maximum_source_bytes{
        kDefaultMaximumLocalAssetSourceBytes};
    std::size_t read_chunk_bytes{kDefaultLocalAssetSourceReadChunkBytes};
    std::size_t maximum_chunks_per_update{
        kDefaultMaximumLocalAssetSourceChunksPerUpdate};
    std::size_t maximum_open_sources{
        kMaximumSimultaneouslyOpenLocalAssetSources};
    // The timeout is measured from the operation's first update. This keeps
    // the operation deterministic for composition roots that provide time.
    std::optional<std::chrono::milliseconds> timeout;
};

[[nodiscard]] bool valid_local_asset_source_open_limits(
    const LocalAssetSourceOpenLimits& limits) noexcept;

// Owns bytes and approved metadata only. It deliberately exposes no locator,
// environment, native path, handle, or mutable source state.
class LocalAssetSource final {
public:
    ~LocalAssetSource() = default;
    LocalAssetSource(LocalAssetSource&&) noexcept = default;
    LocalAssetSource& operator=(LocalAssetSource&&) noexcept = default;
    LocalAssetSource(const LocalAssetSource&) = delete;
    LocalAssetSource& operator=(const LocalAssetSource&) = delete;

    [[nodiscard]] const assets::AssetSource& source() const noexcept;
    [[nodiscard]] local_resources::LocalResourceRootId root_id()
        const noexcept;
    [[nodiscard]] local_resources::LocalVirtualResourceId virtual_resource_id()
        const noexcept;
    [[nodiscard]] local_resources::LocalStableFileIdentity expected_identity()
        const noexcept;
    [[nodiscard]] std::uint64_t byte_count() const noexcept;
    [[nodiscard]] local_resources::LocalResourceLocatorCompatibilityProfile
    locator_compatibility_profile() const noexcept;

private:
    friend class LocalAssetSourceOpenOperation;

    LocalAssetSource(
        assets::AssetSource source,
        local_resources::LocalResourceRootId root_id,
        local_resources::LocalVirtualResourceId virtual_resource_id,
        local_resources::LocalStableFileIdentity expected_identity,
        std::uint64_t byte_count,
        local_resources::LocalResourceLocatorCompatibilityProfile
            locator_compatibility_profile) noexcept;

    assets::AssetSource source_;
    local_resources::LocalResourceRootId root_id_;
    local_resources::LocalVirtualResourceId virtual_resource_id_;
    local_resources::LocalStableFileIdentity expected_identity_;
    std::uint64_t byte_count_{0U};
    local_resources::LocalResourceLocatorCompatibilityProfile
        locator_compatibility_profile_{
            local_resources::LocalResourceLocatorCompatibilityProfile::
                validated_fixed_local_volume_v1};
};

enum class LocalAssetSourceOpenState {
    idle,
    opening,
    reading,
    validating,
    source_ready,
    cancelled,
    timed_out,
    failed,
};

enum class LocalAssetSourceOpenErrorCode {
    invalid_configuration,
    open_source_limit_reached,
    locator_invalid,
    locator_environment_mismatch,
    locator_target_missing,
    stale_locator,
    source_too_large,
    source_read_failed,
    source_changed_during_read,
    source_creation_failed,
    cancelled,
    timed_out,
};

struct LocalAssetSourceOpenError {
    LocalAssetSourceOpenErrorCode code{
        LocalAssetSourceOpenErrorCode::invalid_configuration};
    std::optional<local_resources::LocalResourceLocatorReopenErrorCode>
        locator_reopen_code;
    std::optional<local_resources::LocalReadOnlyFileErrorCode> read_code;
    // Bounded and path-free. Raw virtual names and native paths are excluded.
    std::string context;
};

namespace detail {
struct LocalAssetSourceOpenGate;
class LocalAssetSourceOpenOperationTestAccess;
} // namespace detail

class LocalAssetSourceOpener;

// A single-threaded, caller-driven operation. Each update performs at most the
// configured number of bounded file reads and never sleeps or starts a thread.
class LocalAssetSourceOpenOperation final {
public:
    ~LocalAssetSourceOpenOperation();
    LocalAssetSourceOpenOperation(LocalAssetSourceOpenOperation&&) noexcept;
    LocalAssetSourceOpenOperation& operator=(
        LocalAssetSourceOpenOperation&&) noexcept;
    LocalAssetSourceOpenOperation(const LocalAssetSourceOpenOperation&) =
        delete;
    LocalAssetSourceOpenOperation& operator=(
        const LocalAssetSourceOpenOperation&) = delete;

    void update(LocalAssetSourceOpenTimePoint now) noexcept;
    void cancel() noexcept;

    [[nodiscard]] LocalAssetSourceOpenState state() const noexcept;
    [[nodiscard]] std::uint64_t progress_bytes() const noexcept;
    [[nodiscard]] const LocalAssetSource* result() const noexcept;
    [[nodiscard]] const LocalAssetSourceOpenError* error() const noexcept;
    [[nodiscard]] std::optional<LocalAssetSource> take_result() noexcept;

private:
    friend class LocalAssetSourceOpener;
    friend class detail::LocalAssetSourceOpenOperationTestAccess;

    LocalAssetSourceOpenOperation(
        std::unique_ptr<local_resources::LocalResourceLocator> locator,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        LocalAssetSourceOpenLimits limits,
        std::shared_ptr<detail::LocalAssetSourceOpenGate> gate) noexcept;

    void release_lease() noexcept;
    void close_and_discard_partial_source() noexcept;
    void fail(LocalAssetSourceOpenError error) noexcept;
    void fail_without_context(LocalAssetSourceOpenErrorCode code) noexcept;
    void time_out() noexcept;
    void update_opening();
    void update_reading();
    void update_validating();

    std::unique_ptr<local_resources::LocalResourceLocator> locator_;
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment_;
    LocalAssetSourceOpenLimits limits_;
    std::shared_ptr<detail::LocalAssetSourceOpenGate> gate_;
    bool owns_lease_{true};
    LocalAssetSourceOpenState state_{LocalAssetSourceOpenState::opening};
    std::optional<LocalAssetSourceOpenTimePoint> started_at_;
    std::optional<LocalAssetSourceOpenTimePoint> last_update_at_;
    std::optional<local_resources::LocalReadOnlyFile> file_;
    local_resources::LocalFileMetadataSnapshot initial_snapshot_;
    bool initial_snapshot_valid_{false};
    std::vector<std::byte> source_bytes_;
    std::uint64_t progress_bytes_{0U};
    std::optional<LocalAssetSource> result_;
    std::optional<LocalAssetSourceOpenError> error_;
};

struct LocalAssetSourceOpenBeginResult {
    std::optional<LocalAssetSourceOpenOperation> operation;
    std::optional<LocalAssetSourceOpenError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return operation.has_value();
    }
};

// The per-instance gate prevents more than one outstanding operation. The
// gate is retained by an operation if the opener itself is destroyed.
class LocalAssetSourceOpener final {
public:
    LocalAssetSourceOpener();
    ~LocalAssetSourceOpener() = default;
    LocalAssetSourceOpener(LocalAssetSourceOpener&&) noexcept = default;
    LocalAssetSourceOpener& operator=(LocalAssetSourceOpener&&) noexcept =
        default;
    LocalAssetSourceOpener(const LocalAssetSourceOpener&) = delete;
    LocalAssetSourceOpener& operator=(const LocalAssetSourceOpener&) = delete;

    [[nodiscard]] LocalAssetSourceOpenBeginResult begin(
        const local_resources::LocalResourceLocator& locator,
        std::shared_ptr<const local_resources::LocalResourceEnvironment>
            environment,
        LocalAssetSourceOpenLimits limits = {});

private:
    std::shared_ptr<detail::LocalAssetSourceOpenGate> gate_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const LocalAssetSourceOpenState state) noexcept
{
    switch (state) {
    case LocalAssetSourceOpenState::idle: return "idle";
    case LocalAssetSourceOpenState::opening: return "opening";
    case LocalAssetSourceOpenState::reading: return "reading";
    case LocalAssetSourceOpenState::validating: return "validating";
    case LocalAssetSourceOpenState::source_ready: return "source_ready";
    case LocalAssetSourceOpenState::cancelled: return "cancelled";
    case LocalAssetSourceOpenState::timed_out: return "timed_out";
    case LocalAssetSourceOpenState::failed: return "failed";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const LocalAssetSourceOpenErrorCode code) noexcept
{
    switch (code) {
    case LocalAssetSourceOpenErrorCode::invalid_configuration:
        return "invalid_configuration";
    case LocalAssetSourceOpenErrorCode::open_source_limit_reached:
        return "open_source_limit_reached";
    case LocalAssetSourceOpenErrorCode::locator_invalid:
        return "locator_invalid";
    case LocalAssetSourceOpenErrorCode::locator_environment_mismatch:
        return "locator_environment_mismatch";
    case LocalAssetSourceOpenErrorCode::locator_target_missing:
        return "locator_target_missing";
    case LocalAssetSourceOpenErrorCode::stale_locator:
        return "stale_locator";
    case LocalAssetSourceOpenErrorCode::source_too_large:
        return "source_too_large";
    case LocalAssetSourceOpenErrorCode::source_read_failed:
        return "source_read_failed";
    case LocalAssetSourceOpenErrorCode::source_changed_during_read:
        return "source_changed_during_read";
    case LocalAssetSourceOpenErrorCode::source_creation_failed:
        return "source_creation_failed";
    case LocalAssetSourceOpenErrorCode::cancelled: return "cancelled";
    case LocalAssetSourceOpenErrorCode::timed_out: return "timed_out";
    }
    return "unknown";
}

} // namespace hlclient::local_assets
