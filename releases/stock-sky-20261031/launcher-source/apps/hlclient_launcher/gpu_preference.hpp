#pragma once

// Launcher-only preference, not an OpenGL adapter-binding guarantee. The
// production entry points touch one exact EXE value under the current user's
// Windows graphics preferences. They never delete a key or change a global
// driver setting. Apply before creating the child; existing contexts do not move.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::launcher {

enum class GpuPreference { automatic = 0, power_saving = 1, high_performance = 2 };
enum class GpuPreferenceStatus {
    ok, invalid_executable_path, invalid_preference, malformed_value,
    value_too_large, unsupported_value_type, read_failed, write_failed,
    concurrent_change, busy, unavailable
};

struct GpuPreferenceRead {
    GpuPreferenceStatus status{GpuPreferenceStatus::ok};
    DWORD win32_error{};
    GpuPreference preference{GpuPreference::automatic};
    bool preference_present{};
    bool app_value_exists{};
    [[nodiscard]] bool succeeded() const noexcept { return status == GpuPreferenceStatus::ok; }
};
struct GpuPreferenceWrite {
    GpuPreferenceStatus status{GpuPreferenceStatus::ok};
    DWORD win32_error{};
    bool changed{};
    bool value_deleted{};
    [[nodiscard]] bool succeeded() const noexcept { return status == GpuPreferenceStatus::ok; }
};

[[nodiscard]] inline std::wstring_view gpu_preference_status_text(GpuPreferenceStatus status) noexcept {
    switch (status) {
    case GpuPreferenceStatus::ok: return L"ok";
    case GpuPreferenceStatus::invalid_executable_path: return L"invalid_executable_path";
    case GpuPreferenceStatus::invalid_preference: return L"invalid_preference";
    case GpuPreferenceStatus::malformed_value: return L"malformed_existing_value";
    case GpuPreferenceStatus::value_too_large: return L"existing_value_too_large";
    case GpuPreferenceStatus::unsupported_value_type: return L"unsupported_existing_value_type";
    case GpuPreferenceStatus::read_failed: return L"registry_read_failed";
    case GpuPreferenceStatus::write_failed: return L"registry_write_failed";
    case GpuPreferenceStatus::concurrent_change: return L"preference_changed_concurrently";
    case GpuPreferenceStatus::busy: return L"preference_busy";
    case GpuPreferenceStatus::unavailable: return L"preference_unavailable";
    }
    return L"preference_unavailable";
}

namespace gpu_preference_detail {
inline constexpr wchar_t key_path[] = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
inline constexpr std::size_t maximum_value_characters = 4096U;
inline constexpr std::size_t maximum_tokens = 64U;

[[nodiscard]] inline bool valid_preference(GpuPreference preference) noexcept {
    return preference == GpuPreference::automatic || preference == GpuPreference::power_saving ||
        preference == GpuPreference::high_performance;
}
[[nodiscard]] inline wchar_t folded(wchar_t c) noexcept {
    return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
}
[[nodiscard]] inline bool key_equal(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0U; i < a.size(); ++i) if (folded(a[i]) != folded(b[i])) return false;
    return true;
}

struct Token { std::size_t begin{}, end{}, key_end{}; bool delimiter{}, gpu{}; };
struct ParsedValue {
    GpuPreferenceStatus status{GpuPreferenceStatus::ok};
    GpuPreference preference{GpuPreference::automatic};
    bool preference_present{};
    std::vector<Token> tokens;
};

// Unknown key/value tokens are retained verbatim. Unsupported grammar is not
// repaired: refusing the write is safer than losing a future Windows option.
[[nodiscard]] inline ParsedValue parse(std::wstring_view value) {
    ParsedValue parsed;
    if (value.size() > maximum_value_characters) {
        parsed.status = GpuPreferenceStatus::value_too_large; return parsed;
    }
    std::size_t begin{};
    while (begin < value.size()) {
        if (parsed.tokens.size() == maximum_tokens) {
            parsed.status = GpuPreferenceStatus::malformed_value; return parsed;
        }
        const auto delimiter = value.find(L';', begin);
        const auto end = delimiter == std::wstring_view::npos ? value.size() : delimiter;
        const auto equals = value.find(L'=', begin);
        if (end == begin || equals == std::wstring_view::npos || equals == begin || equals >= end - 1U ||
            value.find(L'=', equals + 1U) < end) {
            parsed.status = GpuPreferenceStatus::malformed_value; return parsed;
        }
        const auto key = value.substr(begin, equals - begin);
        for (const auto c : key) {
            if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                  (c >= L'0' && c <= L'9') || c == L'_')) {
                parsed.status = GpuPreferenceStatus::malformed_value; return parsed;
            }
        }
        for (std::size_t i = equals + 1U; i < end; ++i) {
            if (value[i] < L' ' || value[i] == 0x7f) {
                parsed.status = GpuPreferenceStatus::malformed_value; return parsed;
            }
        }
        for (const auto& old : parsed.tokens) {
            if (key_equal(key, value.substr(old.begin, old.key_end - old.begin))) {
                parsed.status = GpuPreferenceStatus::malformed_value; return parsed;
            }
        }
        const bool gpu = key_equal(key, L"GpuPreference");
        if (gpu) {
            const auto setting = value.substr(equals + 1U, end - equals - 1U);
            if (setting.size() != 1U || setting[0U] < L'0' || setting[0U] > L'2') {
                parsed.status = GpuPreferenceStatus::malformed_value; return parsed;
            }
            parsed.preference = static_cast<GpuPreference>(setting[0U] - L'0');
            parsed.preference_present = true;
        }
        parsed.tokens.push_back({begin, end, equals, delimiter != std::wstring_view::npos, gpu});
        begin = end + (delimiter != std::wstring_view::npos ? 1U : 0U);
    }
    return parsed;
}

struct RewrittenValue { GpuPreferenceStatus status{GpuPreferenceStatus::ok}; std::wstring value; };
[[nodiscard]] inline RewrittenValue rewrite(std::wstring_view old, GpuPreference preference) {
    if (!valid_preference(preference)) return {GpuPreferenceStatus::invalid_preference, {}};
    const auto parsed = parse(old);
    if (parsed.status != GpuPreferenceStatus::ok) return {parsed.status, {}};
    RewrittenValue result;
    for (const auto& token : parsed.tokens) {
        if (!token.gpu) result.value.append(old.substr(token.begin, token.end - token.begin + (token.delimiter ? 1U : 0U)));
        else if (preference != GpuPreference::automatic) {
            result.value += L"GpuPreference=";
            result.value += preference == GpuPreference::power_saving ? L'1' : L'2';
            if (token.delimiter) result.value += L';';
        }
    }
    if (!parsed.preference_present && preference != GpuPreference::automatic) {
        if (!result.value.empty() && result.value.back() != L';') result.value += L';';
        result.value += preference == GpuPreference::power_saving ? L"GpuPreference=1;" : L"GpuPreference=2;";
    }
    if (result.value.size() > maximum_value_characters) return {GpuPreferenceStatus::value_too_large, {}};
    return result;
}

[[nodiscard]] inline std::optional<std::wstring> executable_value_name(const std::filesystem::path& path) {
    if (!path.is_absolute() || path.native().empty() || path.native().size() > 16'383U) return {};
    const auto text = path.native();
    if (text.find(L'\0') != std::wstring::npos || text.find(L'"') != std::wstring::npos ||
        text.starts_with(L"\\\\?\\") || text.starts_with(L"\\\\.\\")) return {};
    for (std::size_t i = 0U; i < text.size(); ++i) {
        const auto c = text[i];
        if (c < L' ' || c == L'<' || c == L'>' || c == L'|' || c == L'*' || c == L'?' ||
            (c == L':' && i != 1U)) return {};
    }
    for (const auto& component : path) if (component == L"." || component == L"..") return {};
    if (!key_equal(path.extension().native(), L".exe")) return {};
    auto preferred = path;
    preferred.make_preferred();
    return preferred.native(); // no symlink resolution, search, environment expansion or alternate EXE
}

class RegistryKey final {
public:
    HKEY value{};
    ~RegistryKey() { if (value) ::RegCloseKey(value); }
    RegistryKey() = default;
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
};
struct Snapshot {
    GpuPreferenceStatus status{GpuPreferenceStatus::ok};
    DWORD error{};
    bool exists{};
    std::wstring value;
};
[[nodiscard]] inline Snapshot snapshot(HKEY key, const std::wstring& name) {
    Snapshot result;
    DWORD type{}, bytes{};
    auto error = ::RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &bytes);
    if (error == ERROR_FILE_NOT_FOUND) return result;
    if (error != ERROR_SUCCESS) return {GpuPreferenceStatus::read_failed, static_cast<DWORD>(error), false, {}};
    if (type != REG_SZ) return {GpuPreferenceStatus::unsupported_value_type, ERROR_DATATYPE_MISMATCH, true, {}};
    if (bytes > (maximum_value_characters + 1U) * sizeof(wchar_t))
        return {GpuPreferenceStatus::value_too_large, ERROR_MORE_DATA, true, {}};
    if (bytes < sizeof(wchar_t) || bytes % sizeof(wchar_t) != 0U)
        return {GpuPreferenceStatus::malformed_value, ERROR_INVALID_DATA, true, {}};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    const DWORD allocated = bytes;
    error = ::RegQueryValueExW(key, name.c_str(), nullptr, &type,
        reinterpret_cast<BYTE*>(value.data()), &bytes);
    if (error != ERROR_SUCCESS || bytes != allocated || type != REG_SZ)
        return {GpuPreferenceStatus::concurrent_change, static_cast<DWORD>(error), true, {}};
    if (value.back() != L'\0' || value.find(L'\0') != value.size() - 1U)
        return {GpuPreferenceStatus::malformed_value, ERROR_INVALID_DATA, true, {}};
    value.pop_back();
    result.exists = true;
    result.value = std::move(value);
    return result;
}

// Existing-key seams allow offline tests to use a freshly owned HKCU fixture,
// never the production UserGpuPreferences key or a real application's value.
[[nodiscard]] inline GpuPreferenceRead read_at_key(HKEY key, const std::wstring& name) {
    const auto current = snapshot(key, name);
    if (current.status != GpuPreferenceStatus::ok) return {current.status, current.error};
    const auto parsed = parse(current.value);
    return {parsed.status, parsed.status == GpuPreferenceStatus::ok ? 0U : ERROR_INVALID_DATA,
        parsed.preference, parsed.preference_present, current.exists};
}
[[nodiscard]] inline GpuPreferenceWrite set_at_key(HKEY key, const std::wstring& name, GpuPreference preference) {
    if (!valid_preference(preference)) return {GpuPreferenceStatus::invalid_preference, ERROR_INVALID_PARAMETER};
    const auto before = snapshot(key, name);
    if (before.status != GpuPreferenceStatus::ok) return {before.status, before.error};
    const auto next = rewrite(before.value, preference);
    if (next.status != GpuPreferenceStatus::ok) return {next.status, ERROR_INVALID_DATA};
    if (!before.exists && next.value.empty()) return {};
    if (before.exists && !next.value.empty() && next.value == before.value) return {};
    // Catch an external edit observed before writing. There is no registry CAS;
    // Windows Settings does not participate in our launcher mutex below.
    const auto confirm = snapshot(key, name);
    if (confirm.status != GpuPreferenceStatus::ok || confirm.exists != before.exists || confirm.value != before.value)
        return {GpuPreferenceStatus::concurrent_change, confirm.error};
    if (next.value.empty()) {
        const auto error = ::RegDeleteValueW(key, name.c_str());
        if (error != ERROR_SUCCESS) return {GpuPreferenceStatus::write_failed, static_cast<DWORD>(error)};
        return {GpuPreferenceStatus::ok, 0U, true, true};
    }
    const auto bytes = static_cast<DWORD>((next.value.size() + 1U) * sizeof(wchar_t));
    const auto error = ::RegSetValueExW(key, name.c_str(), 0U, REG_SZ,
        reinterpret_cast<const BYTE*>(next.value.c_str()), bytes);
    if (error != ERROR_SUCCESS) return {GpuPreferenceStatus::write_failed, static_cast<DWORD>(error)};
    return {GpuPreferenceStatus::ok, 0U, true, false};
}

class PreferenceLock final {
public:
    PreferenceLock() {
        handle_ = ::CreateMutexW(nullptr, FALSE, L"Local\\HLClientEngine.ExactExeGpuPreference.v1");
        if (!handle_) return;
        const auto wait = ::WaitForSingleObject(handle_, 500U);
        owned_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
    }
    ~PreferenceLock() { if (owned_) ::ReleaseMutex(handle_); if (handle_) ::CloseHandle(handle_); }
    [[nodiscard]] bool owned() const noexcept { return owned_; }
    PreferenceLock(const PreferenceLock&) = delete;
    PreferenceLock& operator=(const PreferenceLock&) = delete;
private:
    HANDLE handle_{}; bool owned_{};
};
} // namespace gpu_preference_detail

[[nodiscard]] inline GpuPreferenceRead read_gpu_preference(const std::filesystem::path& executable) noexcept {
    try {
        const auto name = gpu_preference_detail::executable_value_name(executable);
        if (!name) return {GpuPreferenceStatus::invalid_executable_path, ERROR_INVALID_NAME};
        gpu_preference_detail::RegistryKey key;
        const auto error = ::RegOpenKeyExW(HKEY_CURRENT_USER, gpu_preference_detail::key_path, 0U, KEY_QUERY_VALUE, &key.value);
        if (error == ERROR_FILE_NOT_FOUND) return {};
        if (error != ERROR_SUCCESS) return {GpuPreferenceStatus::read_failed, static_cast<DWORD>(error)};
        return gpu_preference_detail::read_at_key(key.value, *name);
    } catch (...) { return {GpuPreferenceStatus::unavailable, ERROR_NOT_ENOUGH_MEMORY}; }
}

[[nodiscard]] inline GpuPreferenceWrite set_gpu_preference(
    const std::filesystem::path& executable, GpuPreference preference) noexcept {
    try {
        const auto name = gpu_preference_detail::executable_value_name(executable);
        if (!name) return {GpuPreferenceStatus::invalid_executable_path, ERROR_INVALID_NAME};
        if (!gpu_preference_detail::valid_preference(preference))
            return {GpuPreferenceStatus::invalid_preference, ERROR_INVALID_PARAMETER};
        gpu_preference_detail::PreferenceLock lock;
        if (!lock.owned()) return {GpuPreferenceStatus::busy, ERROR_BUSY};
        gpu_preference_detail::RegistryKey key;
        auto error = ::RegOpenKeyExW(HKEY_CURRENT_USER, gpu_preference_detail::key_path, 0U,
            KEY_QUERY_VALUE | KEY_SET_VALUE, &key.value);
        if (error == ERROR_FILE_NOT_FOUND) {
            if (preference == GpuPreference::automatic) return {};
            error = ::RegCreateKeyExW(HKEY_CURRENT_USER, gpu_preference_detail::key_path, 0U,
                nullptr, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key.value, nullptr);
        }
        if (error != ERROR_SUCCESS) return {GpuPreferenceStatus::write_failed, static_cast<DWORD>(error)};
        return gpu_preference_detail::set_at_key(key.value, *name, preference);
    } catch (...) { return {GpuPreferenceStatus::unavailable, ERROR_NOT_ENOUGH_MEMORY}; }
}

struct GpuAdapterLabels {
    std::wstring power_saving{L"Енергоощадний GPU"};
    std::wstring high_performance{L"Продуктивний GPU"};
    bool power_saving_observed{}, high_performance_observed{}, available{};
};
namespace gpu_preference_detail {
template<class Interface> class ComOwner final {
public:
    Interface* value{};
    ~ComOwner() { if (value) value->Release(); }
    ComOwner() = default;
    ComOwner(const ComOwner&) = delete;
    ComOwner& operator=(const ComOwner&) = delete;
};
class SystemDxgi final {
public:
    HMODULE value{::LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    ~SystemDxgi() { if (value) ::FreeLibrary(value); }
    SystemDxgi() = default;
    SystemDxgi(const SystemDxgi&) = delete;
    SystemDxgi& operator=(const SystemDxgi&) = delete;
};
[[nodiscard]] inline std::optional<std::wstring> adapter_label(IDXGIFactory6& factory, DXGI_GPU_PREFERENCE preference) {
    for (UINT index = 0U; index < 16U; ++index) {
        ComOwner<IDXGIAdapter1> adapter;
        const auto result = factory.EnumAdapterByGpuPreference(index, preference,
            __uuidof(IDXGIAdapter1), reinterpret_cast<void**>(&adapter.value));
        if (FAILED(result)) return {};
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter.value->GetDesc1(&description))) continue;
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0U) continue;
        constexpr std::size_t capacity = sizeof(description.Description) / sizeof(wchar_t);
        const auto end = std::find(description.Description, description.Description + capacity, L'\0');
        if (end == description.Description || end == description.Description + capacity) continue;
        return std::wstring{description.Description, end};
    }
    return {};
}
} // namespace gpu_preference_detail

// This is read-only DXGI ranking metadata, not evidence of the child's actual
// OpenGL adapter. The UI must display the child's startup renderer separately.
[[nodiscard]] inline GpuAdapterLabels read_gpu_adapter_labels() {
    GpuAdapterLabels labels;
    try {
        gpu_preference_detail::SystemDxgi library;
        if (!library.value) return labels;
        using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
        const auto create = reinterpret_cast<CreateFactory>(::GetProcAddress(library.value, "CreateDXGIFactory1"));
        if (!create) return labels;
        gpu_preference_detail::ComOwner<IDXGIFactory1> initial;
        if (FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&initial.value)))) return labels;
        gpu_preference_detail::ComOwner<IDXGIFactory6> factory;
        if (FAILED(initial.value->QueryInterface(__uuidof(IDXGIFactory6), reinterpret_cast<void**>(&factory.value)))) return labels;
        if (const auto value = gpu_preference_detail::adapter_label(*factory.value, DXGI_GPU_PREFERENCE_MINIMUM_POWER)) {
            labels.power_saving = *value; labels.power_saving_observed = true;
        }
        if (const auto value = gpu_preference_detail::adapter_label(*factory.value, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE)) {
            labels.high_performance = *value; labels.high_performance_observed = true;
        }
        labels.available = labels.power_saving_observed && labels.high_performance_observed;
    } catch (...) { /* The generic labels remain usable; no setting was changed. */ }
    return labels;
}
} // namespace hlclient::launcher


