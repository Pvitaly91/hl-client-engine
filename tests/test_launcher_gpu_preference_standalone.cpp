// Standalone Win32 tests. Only the fresh, owned fixture key below is written;
// no real UserGpuPreferences value, game, renderer or adapter is activated.
#include "../apps/hlclient_launcher/gpu_preference.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace hlclient::launcher;
namespace detail = hlclient::launcher::gpu_preference_detail;
std::size_t assertions{};
void require(bool condition, const char* message) {
    ++assertions;
    if (!condition) throw std::runtime_error{message};
}

void parser_tests() {
    require(detail::parse(L"").status == GpuPreferenceStatus::ok, "empty value");
    require(!detail::parse(L"").preference_present, "empty has no GPU field");
    for (const auto [text, expected] : {
            std::pair{L"GpuPreference=0;", GpuPreference::automatic},
            std::pair{L"GpuPreference=1;", GpuPreference::power_saving},
            std::pair{L"GpuPreference=2", GpuPreference::high_performance}}) {
        const auto value = detail::parse(text);
        require(value.status == GpuPreferenceStatus::ok && value.preference_present && value.preference == expected,
            "read canonical GPU setting");
    }
    require(detail::rewrite(L"Other=opaque value;GpuPreference=2;AutoHDREnable=0;", GpuPreference::power_saving).value ==
        L"Other=opaque value;GpuPreference=1;AutoHDREnable=0;", "unknown fields survive replacement");
    require(detail::rewrite(L"Other=opaque value;GpuPreference=2;AutoHDREnable=0;", GpuPreference::automatic).value ==
        L"Other=opaque value;AutoHDREnable=0;", "Auto removes only GPU field");
    require(detail::rewrite(L"Other=1", GpuPreference::high_performance).value == L"Other=1;GpuPreference=2;",
        "append missing delimiter without losing existing value");
    require(detail::rewrite(L"GpuPreference=0;", GpuPreference::automatic).value.empty(), "Auto removes explicit zero");
    require(detail::rewrite(L"Other=1;", GpuPreference::automatic).value == L"Other=1;", "Auto keeps unknown-only value");
    require(detail::rewrite(L"gpupreference=1;", GpuPreference::high_performance).value == L"GpuPreference=2;",
        "case-insensitive known key remains unique");
    for (const auto invalid : {L";", L"GpuPreference", L"GpuPreference=", L"=2;", L"GpuPreference=3;",
            L"GpuPreference=01;", L"GpuPreference= 2;", L"GpuPreference=2;;", L"Other=1;other=2;",
            L"GpuPreference=1;gpupreference=2;", L"Other=x=y;", L"Bad key=1;", L"Other=a\nb;"}) {
        require(detail::parse(invalid).status == GpuPreferenceStatus::malformed_value, "reject malformed or duplicate token");
        require(detail::rewrite(invalid, GpuPreference::automatic).status == GpuPreferenceStatus::malformed_value,
            "Auto must not erase malformed values");
    }
    require(detail::parse(std::wstring_view{L"Other=a\0b;", 10U}).status == GpuPreferenceStatus::malformed_value,
        "embedded NUL rejected");
    require(detail::parse(std::wstring(detail::maximum_value_characters + 1U, L'x')).status == GpuPreferenceStatus::value_too_large,
        "bounded parser");
    std::wstring many;
    for (unsigned i = 0U; i < 65U; ++i) many += L"K" + std::to_wstring(i) + L"=1;";
    require(detail::parse(many).status == GpuPreferenceStatus::malformed_value, "bounded token count");
    require(detail::rewrite(L"", static_cast<GpuPreference>(99)).status == GpuPreferenceStatus::invalid_preference,
        "invalid enum rejected");
    require(detail::executable_value_name(L"C:\\Test Folder\\hlclient.EXE").has_value(), "absolute EXE path");
    require(detail::executable_value_name(L"\\\\server\\share\\hlclient.exe").has_value(), "UNC absolute EXE path");
    for (const auto invalid : {L"relative.exe", L"C:relative.exe", L"C:\\test\\..\\hlclient.exe",
            L"C:\\test\\client.dll", L"\\\\?\\C:\\test\\client.exe", L"C:\\test\\\"client.exe",
            L"C:\\test\\*.exe", L"C:\\test\\stream:client.exe", L"C:\\test\\bad\nclient.exe"}) {
        require(!detail::executable_value_name(invalid).has_value(), "reject unsafe path");
        require(!set_gpu_preference(invalid, GpuPreference::high_performance).succeeded(),
            "public invalid path rejected before any registry access");
    }
}

class Fixture final {
public:
    const std::wstring name{L"C:\\HLClient-GpuPreference-Test\\not-an-installed-app.exe"};
    std::wstring path;
    HKEY key{};
    Fixture() {
        path = L"Software\\HLClientEngine\\GpuPreferenceTests\\standalone-" +
            std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(::GetTickCount64());
        DWORD disposition{};
        const auto error = ::RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0U, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, &disposition);
        if (error != ERROR_SUCCESS || disposition != REG_CREATED_NEW_KEY) {
            if (key) { ::RegCloseKey(key); key = nullptr; }
            throw std::runtime_error{"Unable to create a new isolated test key; no reused key modified; win32_error=" +
                std::to_string(error) + " disposition=" + std::to_string(disposition)};
        }
    }
    ~Fixture() {
        if (key) {
            ::RegDeleteValueW(key, name.c_str());
            ::RegCloseKey(key);
            // Nonrecursive deletion of exactly the freshly created fixture.
            ::RegDeleteKeyW(HKEY_CURRENT_USER, path.c_str());
        }
    }
    void put(std::wstring_view value) const {
        std::wstring terminated{value};
        require(::RegSetValueExW(key, name.c_str(), 0U, REG_SZ,
            reinterpret_cast<const BYTE*>(terminated.c_str()), static_cast<DWORD>((terminated.size() + 1U) * sizeof(wchar_t))) == ERROR_SUCCESS,
            "write fixture string");
    }
    void erase() const {
        const auto result = ::RegDeleteValueW(key, name.c_str());
        require(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND, "erase owned fixture value");
    }
};

struct RawValue {
    DWORD type{};
    std::vector<BYTE> bytes;
    friend bool operator==(const RawValue&, const RawValue&) = default;
};
RawValue raw_value(const Fixture& fixture) {
    RawValue result;
    DWORD count{};
    require(::RegQueryValueExW(fixture.key, fixture.name.c_str(), nullptr, &result.type, nullptr, &count) == ERROR_SUCCESS,
        "query raw fixture length");
    result.bytes.resize(count);
    require(::RegQueryValueExW(fixture.key, fixture.name.c_str(), nullptr, &result.type, result.bytes.data(), &count) == ERROR_SUCCESS,
        "read raw fixture bytes");
    return result;
}
void require_rejected_unchanged(Fixture& fixture, GpuPreferenceStatus expected) {
    const auto before = raw_value(fixture);
    const auto read = detail::read_at_key(fixture.key, fixture.name);
    if (read.status != expected) {
        throw std::runtime_error{"invalid registry value read: expected=" + std::to_string(static_cast<int>(expected)) +
            " actual=" + std::to_string(static_cast<int>(read.status)) + " type=" + std::to_string(before.type) +
            " bytes=" + std::to_string(before.bytes.size()) + " after_assertion=" + std::to_string(assertions)};
    }
    require(read.status == expected, "invalid registry value read fails");
    for (const auto preference : {GpuPreference::automatic, GpuPreference::power_saving, GpuPreference::high_performance}) {
        const auto result = detail::set_at_key(fixture.key, fixture.name, preference);
        require(result.status == expected && !result.changed && !result.value_deleted, "invalid registry value write fails");
        require(raw_value(fixture) == before, "rejected operation preserved complete original bytes/type");
    }
}
void registry_tests() {
    Fixture fixture;
    auto read = detail::read_at_key(fixture.key, fixture.name);
    require(read.succeeded() && !read.app_value_exists && !read.preference_present &&
        read.preference == GpuPreference::automatic, "missing value is Auto");
    auto written = detail::set_at_key(fixture.key, fixture.name, GpuPreference::automatic);
    require(written.succeeded() && !written.changed, "Auto does not create a missing app value");
    written = detail::set_at_key(fixture.key, fixture.name, GpuPreference::power_saving);
    require(written.succeeded() && written.changed && detail::read_at_key(fixture.key, fixture.name).preference_present,
        "create exact app value in fixture");
    fixture.erase();
    fixture.put(L"Other=opaque value;AutoHDREnable=0;");
    written = detail::set_at_key(fixture.key, fixture.name, GpuPreference::high_performance);
    require(written.succeeded() && written.changed && !written.value_deleted, "set high performance in fixture");
    require(detail::snapshot(fixture.key, fixture.name).value == L"Other=opaque value;AutoHDREnable=0;GpuPreference=2;",
        "registry preserves unknown fields");
    read = detail::read_at_key(fixture.key, fixture.name);
    require(read.succeeded() && read.preference_present && read.preference == GpuPreference::high_performance, "registry read high performance");
    require(!detail::set_at_key(fixture.key, fixture.name, GpuPreference::high_performance).changed, "same preference is no-op");
    require(detail::set_at_key(fixture.key, fixture.name, GpuPreference::power_saving).succeeded(), "set power saving");
    require(detail::read_at_key(fixture.key, fixture.name).preference == GpuPreference::power_saving, "read power saving");
    written = detail::set_at_key(fixture.key, fixture.name, GpuPreference::automatic);
    require(written.succeeded() && written.changed && !written.value_deleted, "Auto preserves other fields/value");
    require(detail::snapshot(fixture.key, fixture.name).value == L"Other=opaque value;AutoHDREnable=0;", "Auto exact remaining bytes");
    fixture.put(L"GpuPreference=1;");
    written = detail::set_at_key(fixture.key, fixture.name, GpuPreference::automatic);
    require(written.succeeded() && written.changed && written.value_deleted, "Auto deletes only empty app value");
    require(!detail::read_at_key(fixture.key, fixture.name).app_value_exists, "value removed, fixture key retained");
    require(::RegQueryInfoKeyW(fixture.key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS, "helper never deletes registry key");
    fixture.put(L"");
    written = detail::set_at_key(fixture.key, fixture.name, GpuPreference::automatic);
    require(written.succeeded() && written.value_deleted, "Auto may delete an already empty app value");
    fixture.put(L"GpuPreference=1;GpuPreference=2;");
    require_rejected_unchanged(fixture, GpuPreferenceStatus::malformed_value);
    fixture.put(L"Other=1;;GpuPreference=2;");
    require_rejected_unchanged(fixture, GpuPreferenceStatus::malformed_value);
    const DWORD integer = 2U;
    require(::RegSetValueExW(fixture.key, fixture.name.c_str(), 0U, REG_DWORD,
        reinterpret_cast<const BYTE*>(&integer), sizeof(integer)) == ERROR_SUCCESS, "write unsupported fixture type");
    require_rejected_unchanged(fixture, GpuPreferenceStatus::unsupported_value_type);
    const std::wstring no_terminator{L"GpuPreference=2;"};
    require(::RegSetValueExW(fixture.key, fixture.name.c_str(), 0U, REG_SZ,
        reinterpret_cast<const BYTE*>(no_terminator.data()), static_cast<DWORD>(no_terminator.size() * sizeof(wchar_t))) == ERROR_SUCCESS,
        "write unterminated fixture string");
    const auto stored_unterminated = raw_value(fixture);
    // Some Windows versions append the missing terminator during RegSetValueExW.
    // Judge the actual stored bytes, not the malformed input we attempted to write.
    if (stored_unterminated.bytes.size() == no_terminator.size() * sizeof(wchar_t)) {
        require_rejected_unchanged(fixture, GpuPreferenceStatus::malformed_value);
    } else {
        require(stored_unterminated.bytes.size() == (no_terminator.size() + 1U) * sizeof(wchar_t) &&
            std::equal(stored_unterminated.bytes.begin(), stored_unterminated.bytes.end(),
                reinterpret_cast<const BYTE*>(no_terminator.c_str())), "OS-normalized fixture has exactly one appended NUL");
        const auto normalized = detail::read_at_key(fixture.key, fixture.name);
        require(normalized.succeeded() && normalized.preference_present &&
            normalized.preference == GpuPreference::high_performance, "actual normalized REG_SZ is valid");
        require(raw_value(fixture) == stored_unterminated, "read does not rewrite normalized registry bytes");
    }
    fixture.put(std::wstring{L"Other=1;\0GpuPreference=2;", 24U});
    require_rejected_unchanged(fixture, GpuPreferenceStatus::malformed_value);
    fixture.put(std::wstring(detail::maximum_value_characters + 1U, L'x'));
    require_rejected_unchanged(fixture, GpuPreferenceStatus::value_too_large);
    fixture.put(L"Other=1;");
    const auto before = raw_value(fixture);
    require(detail::set_at_key(fixture.key, fixture.name, static_cast<GpuPreference>(99)).status ==
        GpuPreferenceStatus::invalid_preference, "invalid enum does not change registry");
    require(raw_value(fixture) == before, "invalid enum preserved bytes");
    detail::RegistryKey readonly;
    require(::RegOpenKeyExW(HKEY_CURRENT_USER, fixture.path.c_str(), 0U, KEY_QUERY_VALUE, &readonly.value) == ERROR_SUCCESS,
        "open read-only fixture handle");
    written = detail::set_at_key(readonly.value, fixture.name, GpuPreference::high_performance);
    require(written.status == GpuPreferenceStatus::write_failed && written.win32_error == ERROR_ACCESS_DENIED && !written.changed,
        "access denied is reported without success claim");
    require(raw_value(fixture) == before, "access-denied write preserved original fields");
    fixture.put(L"GpuPreference=2;");
    const auto before_delete = raw_value(fixture);
    written = detail::set_at_key(readonly.value, fixture.name, GpuPreference::automatic);
    require(written.status == GpuPreferenceStatus::write_failed && written.win32_error == ERROR_ACCESS_DENIED && !written.changed,
        "Auto deletion reports access denied");
    require(raw_value(fixture) == before_delete, "access-denied Auto preserved original value");
    fixture.erase();
}
} // namespace

int wmain() {
    try {
        parser_tests(); registry_tests();
        std::cout << "PASS launcher_gpu_preference assertions=" << assertions
                  << " registry_scope=fresh_isolated_HKCU_fixture real_app_preferences_written=0 game_launches=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL launcher_gpu_preference: " << error.what() << '\n';
        return 1;
    }
}
