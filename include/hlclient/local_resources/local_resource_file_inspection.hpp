#pragma once

#include <hlclient/hash/md5.hpp>
#include <hlclient/local_resources/local_read_only_file.hpp>
#include <hlclient/local_resources/local_resource_resolver.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::local_resources {

struct LocalResourceFileInspectionLimits {
    std::uint64_t maximum_file_size{kDefaultMaximumLocalResourceFileSize};
    std::size_t read_chunk_size{kDefaultLocalResourceReadChunkSize};
    bool require_non_empty{true};
};

[[nodiscard]] bool valid_local_resource_file_inspection_limits(
    const LocalResourceFileInspectionLimits& limits) noexcept;

enum class LocalResourceFileInspectionErrorCode {
    invalid_configuration,
    invalid_state,
    empty_file,
    too_large,
    read_failed,
    state_changed,
    hash_failed,
};

struct LocalResourceFileInspectionError {
    LocalResourceFileInspectionErrorCode code{
        LocalResourceFileInspectionErrorCode::read_failed};
    std::string context;
};

struct LocalResourceFileInspection {
    std::uint32_t byte_count{0U};
    // Compatibility material for the local/provider layer only. Provider and
    // application APIs deliberately expose no raw-digest getter or log output.
    hash::Md5Digest compatibility_md5{};
    std::size_t read_chunk_size{0U};
};

struct LocalResourceFileInspectionResult {
    std::optional<LocalResourceFileInspection> inspection;
    std::optional<LocalResourceFileInspectionError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return inspection.has_value();
    }
};

// Streams the already-open handle from offset zero, then proves EOF and
// compares the final metadata/identity snapshot with the initial snapshot.
[[nodiscard]] LocalResourceFileInspectionResult inspect_local_resource_file(
    LocalReadOnlyFile& file,
    LocalResourceFileInspectionLimits limits = {});

[[nodiscard]] constexpr std::string_view to_string(
    const LocalResourceFileInspectionErrorCode code) noexcept
{
    switch (code) {
    case LocalResourceFileInspectionErrorCode::invalid_configuration:
        return "invalid_configuration";
    case LocalResourceFileInspectionErrorCode::invalid_state:
        return "invalid_state";
    case LocalResourceFileInspectionErrorCode::empty_file: return "empty_file";
    case LocalResourceFileInspectionErrorCode::too_large: return "too_large";
    case LocalResourceFileInspectionErrorCode::read_failed:
        return "read_failed";
    case LocalResourceFileInspectionErrorCode::state_changed:
        return "state_changed";
    case LocalResourceFileInspectionErrorCode::hash_failed:
        return "hash_failed";
    }
    return "unknown";
}

} // namespace hlclient::local_resources
