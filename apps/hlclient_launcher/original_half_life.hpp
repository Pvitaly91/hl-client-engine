#pragma once

// Original Steam Half-Life only. This module never edits game files, config,
// registry settings or the parent's environment, and never terminates a game.
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>
#include <tlhelp32.h>

namespace hlclient::launcher::original_hl {
namespace fs = std::filesystem;
inline constexpr std::size_t maximum_metadata_bytes = 1024U * 1024U;

struct Request {
    fs::path asset_root;
    // Caller uses core::normalize_external_endpoint first. This second gate
    // accepts only its canonical literal IPv4:port output, never console text.
    std::string endpoint, player_name;
    int width{1280}, height{720};
    bool windowed{true};
    std::size_t display_index{};
};
struct Installation {
    fs::path root, executable;
    std::string source, error;
    [[nodiscard]] explicit operator bool() const noexcept { return error.empty() && !executable.empty(); }
};
struct ProcessSnapshot {
    bool complete{}, steam_running{}, half_life_running{};
    std::vector<DWORD> half_life_pids;
    std::string error;
};
struct Plan {
    Request request;
    Installation installation;
    fs::path executable, working_directory;
    std::vector<std::wstring> arguments;
    std::wstring command_line;
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return error.empty() && !command_line.empty(); }
};

[[nodiscard]] inline bool equal_ci(std::wstring_view a, std::wstring_view b) noexcept {
    return a.size() == b.size() && ::CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
        b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
[[nodiscard]] inline std::optional<std::wstring> widen(std::string_view value) {
    if (value.empty()) return std::wstring{};
    if (value.size() > 32767 || value.find('\0') != value.npos) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!n) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    if (!::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), out.data(), n)) return {};
    return out;
}
[[nodiscard]] inline bool safe_local_path(const fs::path& path) {
    const auto s = path.native();
    if (!path.is_absolute() || s.size() < 3 || s.size() > 4096 ||
        !((s[0] >= L'A' && s[0] <= L'Z') || (s[0] >= L'a' && s[0] <= L'z')) ||
        s[1] != L':' || (s[2] != L'\\' && s[2] != L'/')) return false;
    for (std::size_t i = 0; i < s.size(); ++i)
        if (s[i] < 32 || s[i] == L'"' || s[i] == L'|' || s[i] == L'<' || s[i] == L'>' ||
            s[i] == L'*' || s[i] == L'?' || (s[i] == L':' && i != 1)) return false;
    for (const auto& part : path) if (part == L"." || part == L"..") return false;
    return true;
}
[[nodiscard]] inline bool valid_endpoint(std::string_view value) noexcept {
    if (value.empty() || value.size() > 21) return false;
    std::array<unsigned, 5> parts{};
    std::size_t at = 0;
    for (std::size_t i = 0; i != parts.size(); ++i) {
        const auto end = i == 4 ? value.size() : value.find(i == 3 ? ':' : '.', at);
        if (end == value.npos || end == at || (end - at > 1 && value[at] == '0')) return false;
        const auto [p, ec] = std::from_chars(value.data() + at, value.data() + end, parts[i]);
        if (ec != std::errc{} || p != value.data() + end || (i < 4 && parts[i] > 255)) return false;
        at = end + 1;
    }
    return parts[0] != 0 && parts[0] != 127 && parts[0] < 224 && parts[3] != 255 && parts[4] > 0 && parts[4] <= 65535;
}
[[nodiscard]] inline bool valid_player_name(std::string_view value) noexcept {
    if (value.empty() || value.size() > 31 || value.front() == '-' || value.find(" -") != value.npos) return false;
    bool visible = false;
    for (const unsigned char c : value) {
        // '+' is additionally excluded: GoldSrc also interprets +commands.
        if (c < 32 || c > 126 || c == '"' || c == '\\' || c == ';' || c == '+') return false;
        visible |= c != ' ';
    }
    return visible;
}
[[nodiscard]] inline std::wstring quote_argument(std::wstring_view value) {
    std::wstring out{L"\""};
    std::size_t slashes = 0;
    for (const wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'"' ? 2U : 1U), L'\\');
        if (c == L'"') out.push_back(L'\\');
        out.push_back(c); slashes = 0;
    }
    out.append(slashes * 2U, L'\\'); out.push_back(L'"'); return out;
}

// Bounded Valve KeyValues subset used by Steam's local metadata. No execution,
// environment expansion, arbitrary recursive directory walk or regex guessing.
struct VdfNode { std::string key, value; bool object{}; std::vector<VdfNode> children; };
class VdfParser {
    std::string_view input_; std::size_t at_{}, tokens_{};
    void space() {
        for (;;) {
            while (at_ < input_.size() && (input_[at_] == ' ' || input_[at_] == '\t' || input_[at_] == '\r' || input_[at_] == '\n')) ++at_;
            if (input_.substr(at_, 2) != "//") break;
            while (at_ < input_.size() && input_[at_] != '\n') ++at_;
        }
    }
    std::optional<std::string> token() {
        space(); if (++tokens_ > 32768 || at_ == input_.size() || input_[at_++] != '"') return {};
        std::string out;
        while (at_ < input_.size()) {
            char c = input_[at_++];
            if (c == '"') return out;
            if (c == '\\') { if (at_ == input_.size()) return {}; c = input_[at_++]; if (c != '\\' && c != '"') return {}; }
            if (static_cast<unsigned char>(c) < 32 || out.size() >= 4096) return {};
            out.push_back(c);
        }
        return {};
    }
    bool object(std::vector<VdfNode>& nodes, unsigned depth, bool nested) {
        if (depth > 16) return false;
        for (;;) {
            space();
            if (at_ == input_.size()) return !nested;
            if (input_[at_] == '}') { ++at_; return nested; }
            auto key = token(); if (!key) return false;
            VdfNode node; node.key = std::move(*key); space();
            if (at_ < input_.size() && input_[at_] == '{') {
                ++at_; node.object = true; if (!object(node.children, depth + 1, true)) return false;
            } else { auto value = token(); if (!value) return false; node.value = std::move(*value); }
            nodes.push_back(std::move(node));
        }
    }
public:
    explicit VdfParser(std::string_view input) : input_(input) {}
    [[nodiscard]] std::optional<std::vector<VdfNode>> parse() {
        if (input_.empty() || input_.size() > maximum_metadata_bytes || input_.find('\0') != input_.npos) return {};
        if (input_.substr(0, 3) == "\xef\xbb\xbf") at_ = 3;
        std::vector<VdfNode> nodes; if (!object(nodes, 0, false) || at_ != input_.size()) return {}; return nodes;
    }
};
[[nodiscard]] inline const VdfNode* member(const std::vector<VdfNode>& nodes, std::string_view key) {
    const VdfNode* result = nullptr;
    for (const auto& n : nodes) if (n.key == key) { if (result) return nullptr; result = &n; }
    return result;
}
[[nodiscard]] inline bool app70_manifest(std::string_view text, std::wstring_view directory) {
    const auto parsed = VdfParser{text}.parse(); if (!parsed) return false;
    const auto* state = member(*parsed, "AppState"); if (!state || !state->object) return false;
    const auto* app = member(state->children, "appid"); const auto* dir = member(state->children, "installdir");
    if (!app || app->object || app->value != "70" || !dir || dir->object) return false;
    const auto wide = widen(dir->value);
    return wide && wide->find_first_of(L"\\/:\r\n") == wide->npos && equal_ci(*wide, directory);
}
[[nodiscard]] inline std::optional<std::vector<fs::path>> library_paths(std::string_view text) {
    const auto parsed = VdfParser{text}.parse(); if (!parsed) return {};
    const auto* root = member(*parsed, "libraryfolders"); if (!root || !root->object) return {};
    std::vector<fs::path> out;
    for (const auto& entry : root->children) {
        if (entry.key.empty() || !std::all_of(entry.key.begin(), entry.key.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        const auto* value = entry.object ? member(entry.children, "path") : &entry;
        if (!value || value->object) return {};
        const auto wide = widen(value->value); if (!wide || !safe_local_path(fs::path{*wide}) || out.size() >= 64) return {};
        out.emplace_back(*wide);
    }
    return out;
}
[[nodiscard]] inline std::optional<std::string> read_metadata(const fs::path& file) {
    std::error_code ec; const auto size = fs::file_size(file, ec);
    if (ec || size == 0 || size > maximum_metadata_bytes) return {};
    std::ifstream stream(file, std::ios::binary); if (!stream) return {};
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!stream.read(text.data(), static_cast<std::streamsize>(text.size())) || stream.peek() != std::char_traits<char>::eof()) return {};
    return text;
}
[[nodiscard]] inline std::optional<fs::path> registry_path(HKEY hive, const wchar_t* key, const wchar_t* value, REGSAM view) {
    HKEY opened{};
    if (::RegOpenKeyExW(hive, key, 0, KEY_QUERY_VALUE | view, &opened) != ERROR_SUCCESS) return {};
    std::array<wchar_t, 4097> data{}; DWORD type{}, size = static_cast<DWORD>(sizeof(data));
    const auto status = ::RegQueryValueExW(opened, value, nullptr, &type, reinterpret_cast<BYTE*>(data.data()), &size);
    ::RegCloseKey(opened);
    if (status != ERROR_SUCCESS || type != REG_SZ || size < sizeof(wchar_t) || size % sizeof(wchar_t) || size > sizeof(data) || data[size / sizeof(wchar_t) - 1] != L'\0') return {};
    const std::wstring text(data.data(), size / sizeof(wchar_t) - 1);
    if (text.find(L'\0') != text.npos || !safe_local_path(fs::path{text})) return {};
    return fs::path{text};
}
inline void add_unique(std::vector<fs::path>& paths, const fs::path& path) {
    if (std::none_of(paths.begin(), paths.end(), [&](const auto& p) { return equal_ci(p.lexically_normal().native(), path.lexically_normal().native()); })) paths.push_back(path);
}
[[nodiscard]] inline std::vector<fs::path> registered_steam_roots() {
    std::vector<fs::path> roots;
    for (const auto hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) for (const REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY})
        for (const auto value : {L"SteamPath", L"InstallPath"})
            if (const auto p = registry_path(hive, L"Software\\Valve\\Steam", value, view)) add_unique(roots, *p);
    return roots;
}
[[nodiscard]] inline std::vector<fs::path> registered_libraries() {
    auto roots = registered_steam_roots(); std::vector<fs::path> libraries = roots;
    for (const auto& root : roots) if (const auto text = read_metadata(root / L"steamapps" / L"libraryfolders.vdf"))
        if (const auto paths = library_paths(*text)) for (const auto& p : *paths) add_unique(libraries, p);
    return libraries;
}
[[nodiscard]] inline Installation inspect_installation(const fs::path& root, std::string source) {
    Installation out; out.source = std::move(source);
    if (!safe_local_path(root)) { out.error = "invalid_original_game_root"; return out; }
    std::error_code ec; out.root = fs::canonical(root, ec);
    if (ec || !safe_local_path(out.root)) { out.error = "original_game_root_missing"; return out; }
    const auto drive = out.root.root_path().native();
    if (::GetDriveTypeW(drive.c_str()) != DRIVE_FIXED) { out.error = "original_game_requires_local_fixed_drive"; return out; }
    out.executable = out.root / L"hl.exe";
    for (const auto& relative : {L"hl.exe", L"hw.dll", L"steam_api.dll", L"valve\\liblist.gam"}) {
        const auto file = out.root / relative;
        const auto attributes = ::GetFileAttributesW(file.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || !fs::is_regular_file(file, ec) || ec) {
            out.error = "original_game_files_missing_or_redirected"; return out;
        }
    }
    // A resource copy is not silently treated as an installed Steam game.
    const auto common = out.root.parent_path(), steamapps = common.parent_path();
    if (!equal_ci(common.filename().native(), L"common") || !equal_ci(steamapps.filename().native(), L"steamapps")) {
        out.error = "original_game_not_steam_installation"; return out;
    }
    const auto manifest = read_metadata(steamapps / L"appmanifest_70.acf");
    if (!manifest || !app70_manifest(*manifest, out.root.filename().native())) out.error = "original_game_app70_manifest_invalid";
    return out;
}
// Pure candidate selection is independently testable with fake inspections.
[[nodiscard]] inline Installation select_installation(const Installation& explicit_candidate, bool explicit_executable_exists,
    const std::vector<Installation>& discovered) {
    // A copied resource directory may also contain hl.exe. Never execute an
    // invalid copy, but still permit the unique registered Steam installation.
    if (explicit_executable_exists && explicit_candidate) return explicit_candidate;
    std::vector<Installation> valid;
    for (const auto& candidate : discovered) if (candidate && std::none_of(valid.begin(), valid.end(), [&](const auto& item) { return equal_ci(item.root.native(), candidate.root.native()); })) valid.push_back(candidate);
    if (valid.size() == 1) return valid.front();
    Installation failure; failure.error = valid.empty() ? "original_game_not_found" : "original_game_multiple_installations_choose_asset_root"; return failure;
}
[[nodiscard]] inline Installation resolve_installation(const fs::path& asset_root) {
    if (!safe_local_path(asset_root)) { Installation out; out.error = "invalid_original_game_root"; return out; }
    std::error_code ec; const bool exists = fs::exists(asset_root / L"hl.exe", ec);
    if (ec) { Installation out; out.error = "original_game_root_unreadable"; return out; }
    const auto explicit_candidate = exists ? inspect_installation(asset_root, "selected_asset_root") : Installation{};
    if (explicit_candidate) return explicit_candidate;
    std::vector<Installation> candidates;
    for (const auto& library : registered_libraries()) candidates.push_back(inspect_installation(library / L"steamapps" / L"common" / L"Half-Life", "registered_steam_library"));
    return select_installation(explicit_candidate, exists, candidates);
}
[[nodiscard]] inline ProcessSnapshot query_processes() {
    ProcessSnapshot out;
    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { out.error = "process_inventory_failed"; return out; }
    PROCESSENTRY32W item{}; item.dwSize = sizeof(item);
    BOOL more = ::Process32FirstW(snapshot, &item); unsigned count = 0;
    if (!more) { ::CloseHandle(snapshot); out.error = "process_inventory_failed"; return out; }
    do {
        if (++count > 65536) { ::CloseHandle(snapshot); out.error = "process_inventory_limit"; return out; }
        if (equal_ci(item.szExeFile, L"hl.exe")) { out.half_life_running = true; out.half_life_pids.push_back(item.th32ProcessID); }
        if (equal_ci(item.szExeFile, L"steam.exe")) out.steam_running = true;
        more = ::Process32NextW(snapshot, &item);
    } while (more);
    const auto error = ::GetLastError(); ::CloseHandle(snapshot);
    out.complete = error == ERROR_NO_MORE_FILES;
    if (!out.complete) out.error = "process_inventory_incomplete";
    return out;
}
[[nodiscard]] inline Plan make_plan(const Request& request, const Installation& installation, const ProcessSnapshot& processes) {
    Plan out; out.request = request; out.installation = installation;
    const auto fail = [&](std::string error) { out.error = std::move(error); return out; };
    if (!installation) return fail(installation.error.empty() ? "original_game_not_found" : installation.error);
    if (!safe_local_path(installation.root) || !safe_local_path(installation.executable) ||
        installation.executable.parent_path() != installation.root || !equal_ci(installation.executable.filename().native(), L"hl.exe")) return fail("invalid_original_executable");
    if (!valid_endpoint(request.endpoint)) return fail("invalid_endpoint");
    if (!valid_player_name(request.player_name)) return fail("invalid_player_name");
    if (request.width < 640 || request.width > 7680 || request.height < 480 || request.height > 4320) return fail("invalid_video_dimensions");
    // No verified Windows GoldSrc -displayindex contract; do not invent it.
    if (request.display_index != 0) return fail("stock_display_selection_unsupported");
    if (!processes.complete) return fail(processes.error.empty() ? "process_inventory_incomplete" : processes.error);
    if (processes.half_life_running) return fail("original_half_life_already_running_close_first");
    if (!processes.steam_running) return fail("steam_not_running");
    out.executable = installation.executable; out.working_directory = installation.root;
    // Valve Steam Support: https://help.steampowered.com/en/faqs/view/2542-790F-14F8-D66A
    // Width/height, -gl and window/full switches are requests, not observations.
    out.arguments = {L"-steam", L"-game", L"valve", L"-gl", L"-w", std::to_wstring(request.width),
        L"-h", std::to_wstring(request.height), request.windowed ? L"-windowed" : L"-full",
        L"+name", *widen(request.player_name), L"+connect", *widen(request.endpoint)};
    out.command_line = quote_argument(out.executable.native());
    // GoldSrc parses the raw WinMain tail, not CommandLineToArgvW. Quoting
    // switches leaves the quotes in COM_CheckParm / +command tokens, so -gl,
    // dimensions and +connect are silently ignored. Only a VALUE containing
    // spaces needs quotes (the validated name); keep all option tokens bare.
    // The EXE path still uses Windows quoting and CreateProcess lpApplicationName.
    for (const auto& argument : out.arguments) {
        out.command_line += L' ';
        out.command_line += argument.find_first_of(L" \t\"") == argument.npos ? argument : quote_argument(argument);
    }
    if (out.command_line.size() >= 32767) return fail("original_command_line_too_long");
    return out;
}

[[nodiscard]] inline std::optional<std::vector<wchar_t>> child_environment() {
    const auto raw = ::GetEnvironmentStringsW(); if (!raw) return {};
    std::vector<std::wstring> entries; std::size_t total = 0;
    for (const wchar_t* p = raw; *p;) {
        const std::wstring entry{p}; p += entry.size() + 1; total += entry.size() + 1;
        if (total > 1024U * 1024U) { ::FreeEnvironmentStringsW(raw); return {}; }
        const auto split = entry.find(L'='); const auto key = std::wstring_view{entry}.substr(0, split);
        if (!equal_ci(key, L"SteamAppId") && !equal_ci(key, L"SteamGameId")) entries.push_back(entry);
    }
    ::FreeEnvironmentStringsW(raw); entries.emplace_back(L"SteamAppId=70"); entries.emplace_back(L"SteamGameId=70");
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return ::CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN; });
    std::vector<wchar_t> result;
    for (const auto& item : entries) { result.insert(result.end(), item.begin(), item.end()); result.push_back(L'\0'); }
    result.push_back(L'\0'); return result;
}
struct LaunchResult { bool started{}; std::string error; DWORD system_error{}; [[nodiscard]] explicit operator bool() const noexcept { return started; } };
struct Status { bool running{}; std::optional<DWORD> exit_code; std::string error; };
class Process {
    HANDLE process_{}; DWORD pid_{};
public:
    Process() = default;
    Process(const Process&) = delete; Process& operator=(const Process&) = delete;
    ~Process() { if (process_) ::CloseHandle(process_); }
    [[nodiscard]] DWORD pid() const noexcept { return pid_; }
    [[nodiscard]] Status poll() const {
        if (!process_) return {false, {}, "original_process_not_started"};
        const DWORD wait = ::WaitForSingleObject(process_, 0);
        if (wait == WAIT_TIMEOUT) return {true, {}, {}};
        DWORD code{};
        if (wait != WAIT_OBJECT_0 || !::GetExitCodeProcess(process_, &code)) return {false, {}, "original_process_status_unavailable"};
        return {false, code, {}};
    }
    [[nodiscard]] LaunchResult launch(const Plan& requested) {
        if (process_) return {false, "original_process_already_owned", 0};
        if (!requested) return {false, requested.error.empty() ? "original_plan_invalid" : requested.error, 0};
        const HANDLE mutex = ::CreateMutexW(nullptr, FALSE, L"Local\\HLClientEngine.OriginalHalfLife.Launch");
        if (!mutex) return {false, "original_launch_lock_failed", ::GetLastError()};
        const DWORD lock = ::WaitForSingleObject(mutex, 0);
        if (lock != WAIT_OBJECT_0 && lock != WAIT_ABANDONED) { ::CloseHandle(mutex); return {false, "original_launch_in_progress", 0}; }
        struct Unlock { HANDLE value; ~Unlock() { ::ReleaseMutex(value); ::CloseHandle(value); } } unlock{mutex};
        // Fresh path/process validation immediately before launch. No caller
        // supplied arbitrary command line or arguments can bypass make_plan.
        const auto installation = inspect_installation(requested.installation.root, requested.installation.source);
        const auto plan = make_plan(requested.request, installation, query_processes());
        if (!plan) return {false, plan.error, 0};
        const HANDLE executable = ::CreateFileW(plan.executable.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (executable == INVALID_HANDLE_VALUE) return {false, "original_executable_unreadable", ::GetLastError()};
        struct Close { HANDLE value; ~Close() { ::CloseHandle(value); } } close{executable};
        auto environment = child_environment(); if (!environment) return {false, "original_child_environment_failed", 0};
        std::wstring command = plan.command_line; STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION info{};
        if (!::CreateProcessW(plan.executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT,
            environment->data(), plan.working_directory.c_str(), &startup, &info)) return {false, "original_create_process_failed", ::GetLastError()};
        ::CloseHandle(info.hThread); process_ = info.hProcess; pid_ = info.dwProcessId; return {true, {}, 0};
    }
    [[nodiscard]] bool request_close() const {
        if (!process_ || ::WaitForSingleObject(process_, 0) != WAIT_TIMEOUT) return false;
        struct State { HANDLE process; DWORD pid; bool posted{}; } state{process_, pid_, false};
        ::EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
            auto& s = *reinterpret_cast<State*>(parameter);
            if (::WaitForSingleObject(s.process, 0) != WAIT_TIMEOUT) return FALSE;
            DWORD owner{}; ::GetWindowThreadProcessId(window, &owner);
            if (owner == s.pid && ::PostMessageW(window, WM_CLOSE, 0, 0)) s.posted = true;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&state));
        return state.posted;
    }
};
} // namespace hlclient::launcher::original_hl
