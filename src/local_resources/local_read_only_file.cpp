#include <hlclient/local_resources/local_read_only_file.hpp>

#include "win32_local_resource_detail.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace hlclient::local_resources {
namespace {

[[nodiscard]] LocalReadOnlyFileError error(
    const LocalReadOnlyFileErrorCode code,
    std::string context)
{
    return LocalReadOnlyFileError{code, std::move(context)};
}

} // namespace

LocalReadOnlyFile::LocalReadOnlyFile(
    std::unique_ptr<detail::LocalReadOnlyFileStorage> storage) noexcept
    : storage_{std::move(storage)}
{
}

LocalReadOnlyFile::~LocalReadOnlyFile() = default;
LocalReadOnlyFile::LocalReadOnlyFile(LocalReadOnlyFile&&) noexcept = default;
LocalReadOnlyFile& LocalReadOnlyFile::operator=(LocalReadOnlyFile&&) noexcept =
    default;

LocalVirtualResourceId LocalReadOnlyFile::virtual_resource_id() const noexcept
{
    return storage_ ? storage_->virtual_resource_id : LocalVirtualResourceId{0U};
}

LocalResourceRootId LocalReadOnlyFile::root_id() const noexcept
{
    return storage_ ? storage_->root_id : LocalResourceRootId{0U};
}

std::uint64_t LocalReadOnlyFile::file_size() const noexcept
{
    return storage_ ? storage_->initial_snapshot.size : 0U;
}

LocalStableFileIdentity LocalReadOnlyFile::identity() const noexcept
{
    return storage_
               ? LocalStableFileIdentity{
                     storage_->initial_snapshot.identity.volume,
                     storage_->initial_snapshot.identity.file}
               : LocalStableFileIdentity{};
}

bool LocalReadOnlyFile::is_regular_file() const noexcept
{
    return is_open();
}

std::uint64_t LocalReadOnlyFile::bytes_consumed() const noexcept
{
    return storage_ ? storage_->read_offset : 0U;
}

bool LocalReadOnlyFile::is_open() const noexcept
{
    return storage_ && static_cast<bool>(storage_->handle);
}

LocalFileMetadataSnapshot LocalReadOnlyFile::initial_snapshot() const noexcept
{
    if (!storage_) {
        return LocalFileMetadataSnapshot{
            0U, 0, 0, LocalStableFileIdentity{}};
    }
    const auto& snapshot = storage_->initial_snapshot;
    return LocalFileMetadataSnapshot{
        snapshot.size,
        snapshot.last_write_time,
        snapshot.change_time,
        LocalStableFileIdentity{
            snapshot.identity.volume, snapshot.identity.file},
    };
}

LocalFileMetadataSnapshotResult LocalReadOnlyFile::metadata_snapshot() const
{
    if (!is_open()) {
        return LocalFileMetadataSnapshotResult{
            std::nullopt,
            error(
                LocalReadOnlyFileErrorCode::invalid_state,
                "Local resource file handle is closed"),
        };
    }
    detail::NativeFileSnapshot snapshot{};
    if (!detail::query_snapshot(storage_->handle.get(), snapshot)) {
        return LocalFileMetadataSnapshotResult{
            std::nullopt,
            error(
                LocalReadOnlyFileErrorCode::io_error,
                "Unable to inspect the local resource file handle"),
        };
    }
    return LocalFileMetadataSnapshotResult{
        LocalFileMetadataSnapshot{
            snapshot.size,
            snapshot.last_write_time,
            snapshot.change_time,
            LocalStableFileIdentity{
                snapshot.identity.volume, snapshot.identity.file}},
        std::nullopt,
    };
}

LocalReadOnlyFileReadResult LocalReadOnlyFile::read_next(
    const std::span<std::byte> destination)
{
    if (!is_open()) {
        return LocalReadOnlyFileReadResult{
            0U,
            false,
            error(
                LocalReadOnlyFileErrorCode::invalid_state,
                "Local resource file handle is closed"),
        };
    }
    if (destination.size() > kHardMaximumLocalResourceReadChunkSize) {
        return LocalReadOnlyFileReadResult{
            0U,
            false,
            error(
                LocalReadOnlyFileErrorCode::invalid_state,
                "Local resource read exceeds the hard chunk bound"),
        };
    }
    if (destination.empty()) {
        return LocalReadOnlyFileReadResult{
            0U,
            storage_->read_offset == storage_->initial_snapshot.size,
            std::nullopt,
        };
    }

    if (storage_->read_offset == storage_->initial_snapshot.size) {
        std::byte extra{};
        DWORD bytes_read = 0U;
        if (!::ReadFile(storage_->handle.get(), &extra, 1U, &bytes_read, nullptr)) {
            return LocalReadOnlyFileReadResult{
                0U,
                false,
                error(
                    LocalReadOnlyFileErrorCode::io_error,
                    "Unable to establish the end of the local resource file"),
            };
        }
        if (bytes_read != 0U) {
            return LocalReadOnlyFileReadResult{
                0U,
                false,
                error(
                    LocalReadOnlyFileErrorCode::state_changed,
                    "Local resource file grew during its read"),
            };
        }
        return LocalReadOnlyFileReadResult{0U, true, std::nullopt};
    }
    if (storage_->read_offset > storage_->initial_snapshot.size) {
        return LocalReadOnlyFileReadResult{
            0U,
            false,
            error(
                LocalReadOnlyFileErrorCode::invalid_state,
                "Local resource read offset is invalid"),
        };
    }

    const auto remaining =
        storage_->initial_snapshot.size - storage_->read_offset;
    const auto requested = static_cast<DWORD>((std::min)(
        static_cast<std::uint64_t>(destination.size()), remaining));
    DWORD bytes_read = 0U;
    if (!::ReadFile(
            storage_->handle.get(),
            destination.data(),
            requested,
            &bytes_read,
            nullptr)) {
        return LocalReadOnlyFileReadResult{
            0U,
            false,
            error(
                LocalReadOnlyFileErrorCode::io_error,
                "Unable to read the local resource file handle"),
        };
    }
    if (bytes_read != requested) {
        detail::NativeFileSnapshot current{};
        const bool changed =
            detail::query_snapshot(storage_->handle.get(), current) &&
            current != storage_->initial_snapshot;
        return LocalReadOnlyFileReadResult{
            bytes_read,
            false,
            error(
                changed ? LocalReadOnlyFileErrorCode::state_changed
                        : LocalReadOnlyFileErrorCode::short_read,
                changed
                    ? "Local resource file changed during its read"
                    : "Local resource file produced a short read"),
        };
    }

    storage_->read_offset += bytes_read;
    return LocalReadOnlyFileReadResult{
        bytes_read,
        storage_->read_offset == storage_->initial_snapshot.size,
        std::nullopt,
    };
}

void LocalReadOnlyFile::close() noexcept
{
    storage_.reset();
}

} // namespace hlclient::local_resources
