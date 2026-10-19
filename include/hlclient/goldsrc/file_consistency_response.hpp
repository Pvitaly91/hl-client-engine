#pragma once

#include <hlclient/goldsrc/resource_list.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::uint8_t kFileConsistencyResponseOpcode = 7U;
inline constexpr std::uint64_t kMaximumFileConsistencyFileBytes = 16ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMaximumFileConsistencyTotalBytes = 256ULL * 1024ULL * 1024ULL;
inline constexpr std::size_t kMaximumFileConsistencyRequests = 4'095U;
inline constexpr std::uint32_t kFileConsistencyDeadlineSeconds = 10U;

enum class FileConsistencyReadPolicy {
    game_assets_only,
    // Requires explicit caller authorization for the selected server and pinned
    // local installation. It never expands reads outside that installation.
    approved_installation_files,
};

enum class FileConsistencyResponseErrorCode {
    invalid_request,
    unsupported_asset,
    unsafe_name,
    local_file_unavailable,
    local_file_too_large,
    total_bytes_exceeded,
    local_read_failed,
    local_file_changed,
    hash_failed,
    encoding_failed,
    cancelled,
    timed_out,
    allocation_failed,
};

struct FileConsistencyResponseError {
    FileConsistencyResponseErrorCode code{FileConsistencyResponseErrorCode::invalid_request};
    std::optional<std::uint16_t> ordinal;
    // Bounded numeric classifications and an escaped server-requested name;
    // never resolved local paths, file contents or digests.
    std::string context;
};

struct FileConsistencyResponseBuildResult {
    std::vector<std::byte> semantic_bytes;
    std::optional<FileConsistencyResponseError> error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error.has_value(); }
};

// Performs bounded local reads; call from a worker, not a transport update.
// Only known game-asset types/paths under the approved environment are read,
// plus the fixed game-root delta.lst baseline of a recognized ReChecker list.
// Explicit approved_installation_files policy additionally permits requested
// files within that pinned installation, only for recognized ReChecker rows.
// A successful empty result means that no consistency response was requested.
// Present files use actual stable MD5. For the recognized profile only, a
// verified missing allowed asset uses ReChecker's previous-response encoding;
// denied/unsafe/unreadable requests and a missing baseline remain failures.
[[nodiscard]] FileConsistencyResponseBuildResult build_file_consistency_response(
    const ResourceListState& resource_list,
    const local_resources::LocalResourceEnvironment& environment,
    std::uint32_t server_count,
    std::stop_token stop_token = {},
    FileConsistencyReadPolicy policy = FileConsistencyReadPolicy::game_assets_only);

[[nodiscard]] constexpr std::string_view to_string(
    const FileConsistencyResponseErrorCode code) noexcept
{
    switch (code) {
    case FileConsistencyResponseErrorCode::invalid_request: return "invalid_request";
    case FileConsistencyResponseErrorCode::unsupported_asset: return "unsupported_asset";
    case FileConsistencyResponseErrorCode::unsafe_name: return "unsafe_name";
    case FileConsistencyResponseErrorCode::local_file_unavailable: return "local_file_unavailable";
    case FileConsistencyResponseErrorCode::local_file_too_large: return "local_file_too_large";
    case FileConsistencyResponseErrorCode::total_bytes_exceeded: return "total_bytes_exceeded";
    case FileConsistencyResponseErrorCode::local_read_failed: return "local_read_failed";
    case FileConsistencyResponseErrorCode::local_file_changed: return "local_file_changed";
    case FileConsistencyResponseErrorCode::hash_failed: return "hash_failed";
    case FileConsistencyResponseErrorCode::encoding_failed: return "encoding_failed";
    case FileConsistencyResponseErrorCode::cancelled: return "cancelled";
    case FileConsistencyResponseErrorCode::timed_out: return "timed_out";
    case FileConsistencyResponseErrorCode::allocation_failed: return "allocation_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
