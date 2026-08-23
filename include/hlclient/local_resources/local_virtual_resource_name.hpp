#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::local_resources {

inline constexpr std::size_t kMaximumLocalVirtualResourceComponentBytes = 255U;
inline constexpr std::size_t kMaximumLocalVirtualResourcePathBytes = 1'024U;
inline constexpr std::size_t kMaximumLocalVirtualResourceComponents = 64U;

class LocalVirtualResourceName;
class LocalReadOnlyFile;

enum class LocalVirtualResourceNameErrorCode {
    unsafe_name,
    unsupported_name_encoding,
};

struct LocalVirtualResourceNameError {
    LocalVirtualResourceNameErrorCode code{
        LocalVirtualResourceNameErrorCode::unsafe_name};
    std::string context;
};

struct LocalVirtualResourceNameCreateResult;

class LocalVirtualResourceId final {
public:
    [[nodiscard]] std::uint64_t value() const noexcept { return value_; }

    friend bool operator==(
        const LocalVirtualResourceId&,
        const LocalVirtualResourceId&) noexcept = default;

private:
    friend class LocalVirtualResourceName;
    friend class LocalReadOnlyFile;
    explicit LocalVirtualResourceId(const std::uint64_t value) noexcept
        : value_{value}
    {
    }

    std::uint64_t value_{0U};
};

// Owning, byte-exact virtual name. The supported profile accepts printable
// ASCII and '/' separators only. No URL decoding, Unicode conversion,
// separator repair, or path normalization is performed.
class LocalVirtualResourceName final {
public:
    [[nodiscard]] static LocalVirtualResourceNameCreateResult create(
        std::string_view bytes);

    [[nodiscard]] std::string_view value() const noexcept { return value_; }
    [[nodiscard]] LocalVirtualResourceId id() const noexcept { return id_; }
    [[nodiscard]] std::size_t component_count() const noexcept
    {
        return component_count_;
    }

private:
    LocalVirtualResourceName(
        std::string value,
        LocalVirtualResourceId id,
        std::size_t component_count) noexcept;

    std::string value_;
    LocalVirtualResourceId id_{0U};
    std::size_t component_count_{0U};
};

struct LocalVirtualResourceNameCreateResult {
    std::optional<LocalVirtualResourceName> name;
    std::optional<LocalVirtualResourceNameError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return name.has_value();
    }
};

[[nodiscard]] constexpr std::string_view to_string(
    const LocalVirtualResourceNameErrorCode code) noexcept
{
    switch (code) {
    case LocalVirtualResourceNameErrorCode::unsafe_name: return "unsafe_name";
    case LocalVirtualResourceNameErrorCode::unsupported_name_encoding:
        return "unsupported_name_encoding";
    }
    return "unknown";
}

} // namespace hlclient::local_resources
