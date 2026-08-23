#pragma once

#include <hlclient/local_resources/local_resource_identity.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/local_resources/local_virtual_resource_name.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::local_resources {

inline constexpr std::size_t kDefaultLocalResourceReadChunkSize =
    64U * 1024U;
inline constexpr std::size_t kHardMaximumLocalResourceReadChunkSize =
    1U * 1024U * 1024U;

namespace detail {
struct LocalReadOnlyFileStorage;
class LocalReadOnlyFileTestAccess;
}

class LocalResourceResolver;

struct LocalFileMetadataSnapshot {
    std::uint64_t file_size{0U};
    std::int64_t last_write_time{0};
    std::int64_t change_time{0};
    LocalStableFileIdentity identity;

    friend bool operator==(
        const LocalFileMetadataSnapshot&,
        const LocalFileMetadataSnapshot&) noexcept = default;
};

enum class LocalReadOnlyFileErrorCode {
    invalid_state,
    io_error,
    short_read,
    state_changed,
};

struct LocalReadOnlyFileError {
    LocalReadOnlyFileErrorCode code{LocalReadOnlyFileErrorCode::io_error};
    std::string context;
};

struct LocalReadOnlyFileReadResult {
    std::size_t bytes_read{0U};
    bool end_of_file{false};
    std::optional<LocalReadOnlyFileError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !error.has_value();
    }
};

struct LocalFileMetadataSnapshotResult {
    std::optional<LocalFileMetadataSnapshot> snapshot;
    std::optional<LocalReadOnlyFileError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return snapshot.has_value();
    }
};

// A sequential, read-only, move-only handle. The native handle and every
// absolute/final path stay private to the local-resource implementation.
class LocalReadOnlyFile final {
public:
    ~LocalReadOnlyFile();
    LocalReadOnlyFile(LocalReadOnlyFile&&) noexcept;
    LocalReadOnlyFile& operator=(LocalReadOnlyFile&&) noexcept;
    LocalReadOnlyFile(const LocalReadOnlyFile&) = delete;
    LocalReadOnlyFile& operator=(const LocalReadOnlyFile&) = delete;

    [[nodiscard]] LocalVirtualResourceId virtual_resource_id() const noexcept;
    [[nodiscard]] LocalResourceRootId root_id() const noexcept;
    [[nodiscard]] std::uint64_t file_size() const noexcept;
    [[nodiscard]] LocalStableFileIdentity identity() const noexcept;
    [[nodiscard]] bool is_regular_file() const noexcept;
    [[nodiscard]] std::uint64_t bytes_consumed() const noexcept;
    [[nodiscard]] bool is_open() const noexcept;

    [[nodiscard]] LocalFileMetadataSnapshot initial_snapshot() const noexcept;
    [[nodiscard]] LocalFileMetadataSnapshotResult metadata_snapshot() const;
    [[nodiscard]] LocalReadOnlyFileReadResult read_next(
        std::span<std::byte> destination);
    void close() noexcept;

private:
    friend class LocalResourceResolver;
    friend class detail::LocalReadOnlyFileTestAccess;
    explicit LocalReadOnlyFile(
        std::unique_ptr<detail::LocalReadOnlyFileStorage> storage) noexcept;

    std::unique_ptr<detail::LocalReadOnlyFileStorage> storage_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const LocalReadOnlyFileErrorCode code) noexcept
{
    switch (code) {
    case LocalReadOnlyFileErrorCode::invalid_state: return "invalid_state";
    case LocalReadOnlyFileErrorCode::io_error: return "io_error";
    case LocalReadOnlyFileErrorCode::short_read: return "short_read";
    case LocalReadOnlyFileErrorCode::state_changed: return "state_changed";
    }
    return "unknown";
}

} // namespace hlclient::local_resources
