#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace hlclient::local_resources {

class LocalResourceSearchRoots;
class LocalResourceResolver;
class LocalReadOnlyFile;

// An equality-only token for a local filesystem object. The token deliberately
// exposes neither a native path nor its platform identity fields.
class LocalStableFileIdentity final {
public:
    LocalStableFileIdentity() noexcept = default;
    [[nodiscard]] bool valid() const noexcept { return valid_; }

    friend bool operator==(
        const LocalStableFileIdentity&,
        const LocalStableFileIdentity&) noexcept = default;

private:
    friend class LocalResourceSearchRoots;
    friend class LocalResourceResolver;
    friend class LocalReadOnlyFile;

    LocalStableFileIdentity(
        const std::uint64_t volume_identity,
        const std::array<std::byte, 16U> file_identity) noexcept
        : volume_identity_{volume_identity},
          file_identity_{file_identity},
          valid_{true}
    {
    }

    std::uint64_t volume_identity_{0U};
    std::array<std::byte, 16U> file_identity_{};
    bool valid_{false};
};

} // namespace hlclient::local_resources
