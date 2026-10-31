#pragma once

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hlclient::platform::windows {
namespace executable_launch_path_detail {
[[nodiscard]] inline wchar_t upper_ascii(wchar_t c) noexcept {
    return c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - (L'a' - L'A')) : c;
}
[[nodiscard]] inline bool equal_ascii(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (upper_ascii(a[i]) != upper_ascii(b[i])) return false;
    return true;
}
[[nodiscard]] inline bool reserved_component(std::wstring_view component) noexcept {
    auto base = component.substr(0, component.find(L'.'));
    while (!base.empty() && base.back() == L' ') base.remove_suffix(1);
    for (const auto reserved : {L"CON", L"PRN", L"AUX", L"NUL", L"CLOCK$", L"CONIN$", L"CONOUT$"})
        if (equal_ascii(base, reserved)) return true;
    if (base.size() == 4 && (equal_ascii(base.substr(0, 3), L"COM") || equal_ascii(base.substr(0, 3), L"LPT"))) {
        const auto digit = base[3];
        if ((digit >= L'0' && digit <= L'9') || digit == L'\u00b9' || digit == L'\u00b2' || digit == L'\u00b3') return true;
    }
    return false;
}
} // namespace executable_launch_path_detail

// Launch spelling only, after observing a WindowsBinaryIdentity. Windows GPU
// preferences are keyed by an ordinary EXE path; an extended-length spelling
// can bypass that preference. Never change the expected canonical identity:
// the suspended process must still pass the original file/hash verification.
// No filesystem lookup, path search, symlink resolution or general normalization.
// Ambiguous DOS spellings and paths beyond the ordinary MAX_PATH bound fail
// closed instead of silently reverting to a GPU-preference-incompatible path.
[[nodiscard]] inline std::optional<std::filesystem::path> ordinary_executable_launch_path(
    const std::filesystem::path& canonical) {
    const auto& source = canonical.native();
    std::wstring ordinary;
    if (source.starts_with(L"\\\\?\\UNC\\")) ordinary = L"\\\\" + source.substr(8);
    else if (source.starts_with(L"\\\\?\\")) {
        if (source.size() < 7 || source[5] != L':' || source[6] != L'\\') return {};
        ordinary = source.substr(4);
    } else ordinary = source;
    if (ordinary.empty() || ordinary.size() >= 260) return {};

    std::size_t begin{};
    std::size_t minimum_components{};
    if (ordinary.size() >= 3 &&
        ((ordinary[0] >= L'A' && ordinary[0] <= L'Z') || (ordinary[0] >= L'a' && ordinary[0] <= L'z')) &&
        ordinary[1] == L':' && ordinary[2] == L'\\') {
        begin = 3; minimum_components = 1;
    } else if (ordinary.starts_with(L"\\\\")) {
        begin = 2; minimum_components = 3; // server, share, executable
    } else return {};

    std::size_t count{};
    while (begin < ordinary.size()) {
        const auto slash = ordinary.find(L'\\', begin);
        const auto end = slash == std::wstring::npos ? ordinary.size() : slash;
        const auto component = std::wstring_view{ordinary}.substr(begin, end - begin);
        if (component.empty() || component.size() > 255 || component.back() == L'.' || component.back() == L' ' ||
            executable_launch_path_detail::reserved_component(component)) return {};
        for (const auto c : component) {
            if (c < L' ' || c == 0x7f || c == L'/' || c == L':' || c == L'<' || c == L'>' ||
                c == L'"' || c == L'|' || c == L'?' || c == L'*') return {};
        }
        ++count;
        if (slash == std::wstring::npos) break;
        begin = slash + 1;
        if (begin == ordinary.size()) return {};
    }
    if (count < minimum_components) return {};
    return std::filesystem::path{std::move(ordinary)};
}
} // namespace hlclient::platform::windows


