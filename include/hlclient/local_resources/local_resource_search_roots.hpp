#pragma once

#include <hlclient/local_resources/local_resource_identity.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::local_resources {

inline constexpr std::size_t kMaximumLocalResourceSearchRoots = 8U;

namespace detail {
struct LocalResourceBaseStorage;
struct LocalResourceRootStorage;
}

class LocalResourceResolver;
class LocalReadOnlyFile;
struct LocalResourceSearchRootsCreateResult;

class LocalResourceRootId final {
public:
    [[nodiscard]] std::uint32_t value() const noexcept { return value_; }

    friend bool operator==(
        const LocalResourceRootId&,
        const LocalResourceRootId&) noexcept = default;

private:
    friend class LocalResourceSearchRoots;
    friend class LocalResourceResolver;
    friend class LocalReadOnlyFile;
    explicit LocalResourceRootId(const std::uint32_t value) noexcept
        : value_{value}
    {
    }

    std::uint32_t value_{0U};
};

enum class LocalResourceRootKind {
    game,
    valve_fallback,
};

struct LocalResourceSearchRootMetadata {
    LocalResourceRootId id;
    LocalResourceRootKind kind{LocalResourceRootKind::game};
    LocalStableFileIdentity identity;
};

enum class LocalResourceSearchRootsErrorCode {
    invalid_configuration,
    invalid_base_directory,
    missing_root,
    not_directory,
    final_reparse_point,
    escaped_base_directory,
    remote_volume_unsupported,
    io_error,
};

struct LocalResourceSearchRootsError {
    LocalResourceSearchRootsErrorCode code{
        LocalResourceSearchRootsErrorCode::invalid_configuration};
    // Sanitized metadata-only context. Native paths are never included.
    std::string context;
};

// Validated, ordered, owning search roots. Native/final paths and root handles
// remain private. Roots are opened explicitly; there is no CWD, registry,
// Steam-library, environment, or build-tree discovery fallback.
class LocalResourceSearchRoots final {
public:
    [[nodiscard]] static LocalResourceSearchRootsCreateResult create(
        const std::filesystem::path& base_directory,
        std::string_view game_directory);

    ~LocalResourceSearchRoots();
    LocalResourceSearchRoots(LocalResourceSearchRoots&&) noexcept;
    LocalResourceSearchRoots& operator=(LocalResourceSearchRoots&&) noexcept;
    LocalResourceSearchRoots(const LocalResourceSearchRoots&) = delete;
    LocalResourceSearchRoots& operator=(const LocalResourceSearchRoots&) = delete;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return size() == 0U; }
    [[nodiscard]] std::optional<LocalResourceSearchRootMetadata> metadata(
        std::size_t index) const noexcept;

private:
    friend class LocalResourceResolver;

    explicit LocalResourceSearchRoots(
        std::unique_ptr<detail::LocalResourceBaseStorage> base,
        std::vector<std::unique_ptr<detail::LocalResourceRootStorage>> roots)
        noexcept;

    std::unique_ptr<detail::LocalResourceBaseStorage> base_;
    std::vector<std::unique_ptr<detail::LocalResourceRootStorage>> roots_;
};

struct LocalResourceSearchRootsCreateResult {
    std::optional<LocalResourceSearchRoots> roots;
    std::optional<LocalResourceSearchRootsError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return roots.has_value();
    }
};

[[nodiscard]] constexpr std::string_view to_string(
    const LocalResourceSearchRootsErrorCode code) noexcept
{
    switch (code) {
    case LocalResourceSearchRootsErrorCode::invalid_configuration:
        return "invalid_configuration";
    case LocalResourceSearchRootsErrorCode::invalid_base_directory:
        return "invalid_base_directory";
    case LocalResourceSearchRootsErrorCode::missing_root: return "missing_root";
    case LocalResourceSearchRootsErrorCode::not_directory:
        return "not_directory";
    case LocalResourceSearchRootsErrorCode::final_reparse_point:
        return "final_reparse_point";
    case LocalResourceSearchRootsErrorCode::escaped_base_directory:
        return "escaped_base_directory";
    case LocalResourceSearchRootsErrorCode::remote_volume_unsupported:
        return "remote_volume_unsupported";
    case LocalResourceSearchRootsErrorCode::io_error: return "io_error";
    }
    return "unknown";
}

} // namespace hlclient::local_resources
