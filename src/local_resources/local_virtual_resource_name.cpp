#include <hlclient/local_resources/local_virtual_resource_name.hpp>

#include <algorithm>
#include <array>
#include <utility>

namespace hlclient::local_resources {
namespace {

[[nodiscard]] LocalVirtualResourceNameCreateResult failure(
    const LocalVirtualResourceNameErrorCode code,
    std::string context)
{
    return LocalVirtualResourceNameCreateResult{
        std::nullopt,
        LocalVirtualResourceNameError{code, std::move(context)},
    };
}

[[nodiscard]] constexpr char ascii_lower(const char value) noexcept
{
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value + ('a' - 'A'))
               : value;
}

[[nodiscard]] bool ascii_equal_insensitive(
    const std::string_view left,
    const std::string_view right) noexcept
{
    return left.size() == right.size() &&
           std::equal(
               left.begin(),
               left.end(),
               right.begin(),
               [](const char lhs, const char rhs) {
                   return ascii_lower(lhs) == ascii_lower(rhs);
               });
}

[[nodiscard]] bool is_reserved_windows_component(
    const std::string_view component) noexcept
{
    const auto stem = component.substr(0U, component.find('.'));
    constexpr std::array<std::string_view, 4U> exact_names{
        "CON", "PRN", "AUX", "NUL"};
    if (std::ranges::any_of(exact_names, [&](const std::string_view name) {
            return ascii_equal_insensitive(stem, name);
        })) {
        return true;
    }
    if (stem.size() == 4U &&
        (ascii_equal_insensitive(stem.substr(0U, 3U), "COM") ||
         ascii_equal_insensitive(stem.substr(0U, 3U), "LPT")) &&
        stem[3U] >= '1' && stem[3U] <= '9') {
        return true;
    }
    return false;
}

[[nodiscard]] std::uint64_t virtual_name_id(
    const std::string_view bytes) noexcept
{
    // Stable FNV-1a identifier over the exact approved bytes. This is an
    // identifier only, not a trust or security digest.
    std::uint64_t value = 14'695'981'039'346'656'037ULL;
    for (const unsigned char byte : bytes) {
        value ^= byte;
        value *= 1'099'511'628'211ULL;
    }
    return value;
}

} // namespace

LocalVirtualResourceName::LocalVirtualResourceName(
    std::string value,
    const LocalVirtualResourceId id,
    const std::size_t component_count) noexcept
    : value_{std::move(value)}, id_{id}, component_count_{component_count}
{
}

LocalVirtualResourceNameCreateResult LocalVirtualResourceName::create(
    const std::string_view bytes)
{
    if (bytes.empty()) {
        return failure(
            LocalVirtualResourceNameErrorCode::unsafe_name,
            "Virtual resource name is empty");
    }
    if (bytes.size() > kMaximumLocalVirtualResourcePathBytes) {
        return failure(
            LocalVirtualResourceNameErrorCode::unsafe_name,
            "Virtual resource name exceeds the byte limit");
    }

    std::size_t component_begin = 0U;
    std::size_t component_count = 0U;
    for (std::size_t index = 0U; index <= bytes.size(); ++index) {
        if (index < bytes.size()) {
            const auto byte = static_cast<unsigned char>(bytes[index]);
            if (byte >= 0x80U) {
                return failure(
                    LocalVirtualResourceNameErrorCode::unsupported_name_encoding,
                    "Virtual resource name is not printable ASCII");
            }
            if (byte < 0x20U || byte == 0x7fU) {
                return failure(
                    LocalVirtualResourceNameErrorCode::unsafe_name,
                    "Virtual resource name contains a control byte");
            }
            if (bytes[index] == '\\' || bytes[index] == ':') {
                return failure(
                    LocalVirtualResourceNameErrorCode::unsafe_name,
                    "Virtual resource name contains a forbidden path byte");
            }
            if (bytes[index] != '/') {
                continue;
            }
        }

        const auto component =
            bytes.substr(component_begin, index - component_begin);
        if (component.empty()) {
            return failure(
                LocalVirtualResourceNameErrorCode::unsafe_name,
                "Virtual resource name contains an empty component");
        }
        if (component.size() > kMaximumLocalVirtualResourceComponentBytes) {
            return failure(
                LocalVirtualResourceNameErrorCode::unsafe_name,
                "Virtual resource component exceeds the byte limit");
        }
        if (component == "." || component == "..") {
            return failure(
                LocalVirtualResourceNameErrorCode::unsafe_name,
                "Virtual resource name contains a traversal component");
        }
        if (component.back() == '.' || component.back() == ' ') {
            return failure(
                LocalVirtualResourceNameErrorCode::unsafe_name,
                "Virtual resource component has a forbidden suffix");
        }
        if (is_reserved_windows_component(component)) {
            return failure(
                LocalVirtualResourceNameErrorCode::unsafe_name,
                "Virtual resource name contains a reserved device component");
        }
        ++component_count;
        if (component_count > kMaximumLocalVirtualResourceComponents) {
            return failure(
                LocalVirtualResourceNameErrorCode::unsafe_name,
                "Virtual resource name has too many components");
        }
        component_begin = index + 1U;
    }

    try {
        return LocalVirtualResourceNameCreateResult{
            LocalVirtualResourceName{
                std::string{bytes},
                LocalVirtualResourceId{virtual_name_id(bytes)},
                component_count},
            std::nullopt,
        };
    } catch (...) {
        return failure(
            LocalVirtualResourceNameErrorCode::unsafe_name,
            "Unable to retain the bounded virtual resource name");
    }
}

} // namespace hlclient::local_resources
