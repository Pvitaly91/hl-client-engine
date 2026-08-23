#include <hlclient/local_resources/local_resource_file_inspection.hpp>

#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace hlclient::local_resources {
namespace {

[[nodiscard]] LocalResourceFileInspectionResult failure(
    const LocalResourceFileInspectionErrorCode code,
    std::string context)
{
    return LocalResourceFileInspectionResult{
        std::nullopt,
        LocalResourceFileInspectionError{code, std::move(context)},
    };
}

} // namespace

bool valid_local_resource_file_inspection_limits(
    const LocalResourceFileInspectionLimits& limits) noexcept
{
    return limits.maximum_file_size > 0U &&
           limits.maximum_file_size <= kHardMaximumLocalResourceFileSize &&
           limits.read_chunk_size > 0U &&
           limits.read_chunk_size <= kHardMaximumLocalResourceReadChunkSize;
}

LocalResourceFileInspectionResult inspect_local_resource_file(
    LocalReadOnlyFile& file,
    const LocalResourceFileInspectionLimits limits)
{
    if (!valid_local_resource_file_inspection_limits(limits)) {
        return failure(
            LocalResourceFileInspectionErrorCode::invalid_configuration,
            "Local resource inspection limits are outside project hard caps");
    }
    if (!file.is_open() || file.bytes_consumed() != 0U) {
        return failure(
            LocalResourceFileInspectionErrorCode::invalid_state,
            "Local resource inspection requires a fresh open handle");
    }

    const auto initial = file.initial_snapshot();
    if (limits.require_non_empty && initial.file_size == 0U) {
        return failure(
            LocalResourceFileInspectionErrorCode::empty_file,
            "The current consistency profile requires a non-empty local resource");
    }
    if (initial.file_size > limits.maximum_file_size ||
        initial.file_size > (std::numeric_limits<std::uint32_t>::max)()) {
        return failure(
            LocalResourceFileInspectionErrorCode::too_large,
            "Local resource exceeds the configured inspection size bound");
    }

    std::vector<std::byte> chunk;
    try {
        chunk.resize(limits.read_chunk_size);
    } catch (...) {
        return failure(
            LocalResourceFileInspectionErrorCode::read_failed,
            "Unable to allocate the bounded local resource read chunk");
    }

    hash::Md5Hasher hasher;
    std::uint64_t total = 0U;
    while (total < initial.file_size) {
        const auto result = file.read_next(chunk);
        if (!result) {
            return failure(
                result.error->code == LocalReadOnlyFileErrorCode::state_changed
                    ? LocalResourceFileInspectionErrorCode::state_changed
                    : LocalResourceFileInspectionErrorCode::read_failed,
                result.error->context);
        }
        if (result.bytes_read == 0U ||
            total > initial.file_size - result.bytes_read) {
            return failure(
                LocalResourceFileInspectionErrorCode::read_failed,
                "Local resource produced an invalid bounded read");
        }
        if (!hasher.update(std::span<const std::byte>{chunk}.first(
                result.bytes_read))) {
            return failure(
                LocalResourceFileInspectionErrorCode::hash_failed,
                "Unable to update compatibility MD5 state");
        }
        total += result.bytes_read;
    }

    std::array<std::byte, 1U> end_probe{};
    const auto end = file.read_next(end_probe);
    if (!end) {
        return failure(
            end.error->code == LocalReadOnlyFileErrorCode::state_changed
                ? LocalResourceFileInspectionErrorCode::state_changed
                : LocalResourceFileInspectionErrorCode::read_failed,
            end.error->context);
    }
    if (!end.end_of_file || end.bytes_read != 0U) {
        return failure(
            LocalResourceFileInspectionErrorCode::state_changed,
            "Local resource grew during its bounded read");
    }

    const auto final = file.metadata_snapshot();
    if (!final) {
        return failure(
            LocalResourceFileInspectionErrorCode::read_failed,
            final.error->context);
    }
    if (*final.snapshot != initial) {
        return failure(
            LocalResourceFileInspectionErrorCode::state_changed,
            "Local resource metadata changed during its bounded read");
    }

    auto digest = hasher.finalize();
    if (!digest) {
        return failure(
            LocalResourceFileInspectionErrorCode::hash_failed,
            "Unable to finalize compatibility MD5 state");
    }
    return LocalResourceFileInspectionResult{
        LocalResourceFileInspection{
            static_cast<std::uint32_t>(total),
            *digest,
            limits.read_chunk_size,
        },
        std::nullopt,
    };
}

} // namespace hlclient::local_resources
