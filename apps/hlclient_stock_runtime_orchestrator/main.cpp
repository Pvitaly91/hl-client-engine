#include <hlclient/goldsrc/stock_runtime_capture.hpp>
#include <hlclient/platform/windows/binary_identity.hpp>
#include <hlclient/platform/windows/network_isolation.hpp>
#include <hlclient/platform/windows/process_orchestrator.hpp>
#include <hlclient/platform/windows/secure_output.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace {

namespace fs = std::filesystem;
namespace goldsrc = hlclient::goldsrc;
namespace windows = hlclient::platform::windows;

constexpr std::wstring_view kConfirmationToken =
    L"HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1";
constexpr std::string_view kPrefix = "[stock-runtime-orchestrator] ";

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(const HANDLE handle) noexcept : handle_{handle} {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_{std::exchange(other.handle_, INVALID_HANDLE_VALUE)}
    {
    }
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    void reset() noexcept
    {
        if (*this) static_cast<void>(::CloseHandle(handle_));
        handle_ = INVALID_HANDLE_VALUE;
    }
private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

struct Options final {
    bool validate_config{false};
    bool validate_environment{false};
    bool confirmation_seen{false};
    HANDLE wrapper_capability_handle{INVALID_HANDLE_VALUE};
    HANDLE wrapper_cleanup_capability_handle{INVALID_HANDLE_VALUE};
    HANDLE wrapper_job_handle{INVALID_HANDLE_VALUE};
    HANDLE wrapper_guard_job_handle{INVALID_HANDLE_VALUE};
    HANDLE isolation_release_handle{INVALID_HANDLE_VALUE};
    std::uint32_t wrapper_process_id{0U};
    fs::path run_root;
    fs::path research_root;
    fs::path client;
    fs::path server;
    fs::path relay;
    fs::path isolation_guard;
    fs::path app_manifest;
    std::string game;
    std::string map;
    std::string scenario;
    std::uint16_t relay_port{0U};
    std::uint16_t server_port{0U};
    std::uint32_t maximum_duration_seconds{45U};
    goldsrc::StockRuntimeCaptureLimits limits{};
    goldsrc::StockRuntimeCapturePerturbation perturbation{};
};

template<typename Integer>
[[nodiscard]] bool parse_wide_decimal(
    const std::wstring_view value,
    Integer& output) noexcept
{
    if (value.empty()) return false;
    Integer parsed{};
    for (const wchar_t character : value) {
        if (character < L'0' || character > L'9') return false;
        const auto digit = static_cast<Integer>(character - L'0');
        if (parsed > ((std::numeric_limits<Integer>::max)() - digit) / 10) {
            return false;
        }
        parsed = static_cast<Integer>(parsed * 10 + digit);
    }
    output = parsed;
    return true;
}

[[nodiscard]] std::optional<std::string> narrow_safe_token(
    const std::wstring_view value,
    const std::size_t maximum = 64U)
{
    if (value.empty() || value.size() > maximum) return std::nullopt;
    std::string output;
    output.reserve(value.size());
    for (const wchar_t character : value) {
        if (!((character >= L'a' && character <= L'z') ||
              (character >= L'A' && character <= L'Z') ||
              (character >= L'0' && character <= L'9') || character == L'_' ||
              character == L'-')) {
            return std::nullopt;
        }
        output.push_back(static_cast<char>(character));
    }
    return output;
}

[[nodiscard]] std::optional<Options> parse_options(
    const int argc,
    wchar_t** argv)
{
    if (argc == 2 && std::wstring_view{argv[1]} == L"--validate-config") {
        Options options;
        options.validate_config = true;
        return options;
    }
    Options options;
    std::array<bool, 37U> seen{};
    const auto mark = [&seen](const std::size_t index) {
        if (seen[index]) return false;
        seen[index] = true;
        return true;
    };
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view name{argv[index]};
        if (name == L"--validate-environment") {
            if (!mark(0U)) return std::nullopt;
            options.validate_environment = true;
            continue;
        }
        if (index + 1 >= argc) return std::nullopt;
        const std::wstring_view value{argv[++index]};
        std::size_t option = 0U;
        if (name == L"--confirmation-token") option = 2U;
        else if (name == L"--run-root") option = 3U;
        else if (name == L"--research-root") option = 4U;
        else if (name == L"--client") option = 5U;
        else if (name == L"--server") option = 6U;
        else if (name == L"--relay") option = 7U;
        else if (name == L"--isolation-guard") option = 8U;
        else if (name == L"--app-manifest") option = 9U;
        else if (name == L"--game") option = 10U;
        else if (name == L"--map") option = 11U;
        else if (name == L"--scenario") option = 12U;
        else if (name == L"--relay-port") option = 13U;
        else if (name == L"--server-port") option = 14U;
        else if (name == L"--max-duration-seconds") option = 15U;
        else if (name == L"--max-datagrams") option = 16U;
        else if (name == L"--max-total-raw-bytes") option = 17U;
        else if (name == L"--max-payload-bytes") option = 18U;
        else if (name == L"--max-reassembled-bytes") option = 19U;
        else if (name == L"--max-decompressed-bytes") option = 20U;
        else if (name == L"--max-message-count") option = 21U;
        else if (name == L"--max-runtime-frames") option = 22U;
        else if (name == L"--max-client-packets") option = 23U;
        else if (name == L"--max-server-packets") option = 24U;
        else if (name == L"--mutation-after-client-packets") option = 25U;
        else if (name == L"--mutation-after-server-packets") option = 26U;
        else if (name == L"--wrapper-capability-handle") option = 27U;
        else if (name == L"--wrapper-process-id") option = 28U;
        else if (name == L"--wrapper-cleanup-capability-handle") option = 29U;
        else if (name == L"--wrapper-job-handle") option = 30U;
        else if (name == L"--wrapper-guard-job-handle") option = 31U;
        else if (name == L"--isolation-release-handle") option = 32U;
        else return std::nullopt;
        if (!mark(option)) return std::nullopt;

        switch (option) {
        case 2U:
            options.confirmation_seen = value == kConfirmationToken;
            if (!options.confirmation_seen) return std::nullopt;
            break;
        case 3U: options.run_root = value; break;
        case 4U: options.research_root = value; break;
        case 5U: options.client = value; break;
        case 6U: options.server = value; break;
        case 7U: options.relay = value; break;
        case 8U: options.isolation_guard = value; break;
        case 9U: options.app_manifest = value; break;
        case 10U: {
            const auto token = narrow_safe_token(value);
            if (!token) return std::nullopt;
            options.game = *token;
            break;
        }
        case 11U: {
            const auto token = narrow_safe_token(value);
            if (!token) return std::nullopt;
            options.map = *token;
            break;
        }
        case 12U: {
            const auto token = narrow_safe_token(value);
            if (!token) return std::nullopt;
            options.scenario = *token;
            break;
        }
        case 13U:
        case 14U: {
            unsigned int port = 0U;
            if (!parse_wide_decimal(value, port) || port < 1'024U ||
                port > 65'534U) return std::nullopt;
            if (option == 13U) options.relay_port = static_cast<std::uint16_t>(port);
            else options.server_port = static_cast<std::uint16_t>(port);
            break;
        }
        case 15U:
            if (!parse_wide_decimal(value, options.maximum_duration_seconds) ||
                options.maximum_duration_seconds == 0U ||
                options.maximum_duration_seconds > 300U) return std::nullopt;
            options.limits.maximum_duration = std::chrono::seconds{
                options.maximum_duration_seconds};
            break;
        case 27U: {
            std::uintptr_t handle = 0U;
            if (!parse_wide_decimal(value, handle) || handle == 0U) {
                return std::nullopt;
            }
            options.wrapper_capability_handle =
                reinterpret_cast<HANDLE>(handle);
            break;
        }
        case 28U:
            if (!parse_wide_decimal(value, options.wrapper_process_id) ||
                options.wrapper_process_id == 0U) {
                return std::nullopt;
            }
            break;
        case 29U: {
            std::uintptr_t handle = 0U;
            if (!parse_wide_decimal(value, handle) || handle == 0U) {
                return std::nullopt;
            }
            options.wrapper_cleanup_capability_handle =
                reinterpret_cast<HANDLE>(handle);
            break;
        }
        case 30U: {
            std::uintptr_t handle = 0U;
            if (!parse_wide_decimal(value, handle) || handle == 0U) {
                return std::nullopt;
            }
            options.wrapper_job_handle = reinterpret_cast<HANDLE>(handle);
            break;
        }
        case 31U:
        case 32U: {
            std::uintptr_t handle = 0U;
            if (!parse_wide_decimal(value, handle) || handle == 0U) {
                return std::nullopt;
            }
            if (option == 31U) {
                options.wrapper_guard_job_handle = reinterpret_cast<HANDLE>(handle);
            } else {
                options.isolation_release_handle = reinterpret_cast<HANDLE>(handle);
            }
            break;
        }
        case 17U:
            if (!parse_wide_decimal(value, options.limits.maximum_total_raw_bytes))
                return std::nullopt;
            break;
        default: {
            std::size_t parsed = 0U;
            if (!parse_wide_decimal(value, parsed)) return std::nullopt;
            if (option == 16U) options.limits.maximum_datagrams = parsed;
            else if (option == 18U) options.limits.maximum_payload_bytes = parsed;
            else if (option == 19U) options.limits.maximum_reassembled_bytes = parsed;
            else if (option == 20U) options.limits.maximum_decompressed_bytes = parsed;
            else if (option == 21U) options.limits.maximum_message_count = parsed;
            else if (option == 22U) options.limits.maximum_runtime_frames = parsed;
            else if (option == 23U) options.limits.maximum_client_packets = parsed;
            else if (option == 24U) options.limits.maximum_server_packets = parsed;
            else if (option == 25U) options.perturbation.client_packet_ordinal = parsed;
            else if (option == 26U) options.perturbation.server_packet_ordinal = parsed;
            break;
        }
        }
    }
    if (options.research_root.empty() || options.client.empty() ||
        options.server.empty() || options.relay.empty() ||
        options.isolation_guard.empty() || options.app_manifest.empty() ||
        !goldsrc::validate_stock_runtime_capture_limits(options.limits)) {
        return std::nullopt;
    }
    if (options.validate_environment) {
        if (options.confirmation_seen || !options.run_root.empty() ||
            !options.scenario.empty() ||
            options.wrapper_capability_handle != INVALID_HANDLE_VALUE ||
            options.wrapper_cleanup_capability_handle != INVALID_HANDLE_VALUE ||
            options.wrapper_job_handle != INVALID_HANDLE_VALUE ||
            options.wrapper_guard_job_handle != INVALID_HANDLE_VALUE ||
            options.isolation_release_handle != INVALID_HANDLE_VALUE ||
            options.wrapper_process_id != 0U ||
            (!options.game.empty() && options.game != "valve") ||
            options.relay_port == options.server_port) return std::nullopt;
    } else if (!options.confirmation_seen || options.run_root.empty() ||
               options.game != "valve" || options.map.empty() ||
               !goldsrc::parse_stock_runtime_capture_scenario(options.scenario) ||
               options.relay_port == 0U || options.server_port == 0U ||
               options.relay_port == options.server_port ||
               options.perturbation.client_packet_ordinal == 0U ||
               options.perturbation.server_packet_ordinal == 0U) {
        return std::nullopt;
    }
    return options;
}

[[nodiscard]] std::optional<std::uint32_t> current_parent_process_id() noexcept
{
    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0U);
    if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    std::optional<std::uint32_t> parent;
    if (::Process32FirstW(snapshot, &process)) {
        do {
            if (process.th32ProcessID == ::GetCurrentProcessId()) {
                parent = process.th32ParentProcessID;
                break;
            }
        } while (::Process32NextW(snapshot, &process));
    }
    static_cast<void>(::CloseHandle(snapshot));
    return parent;
}

[[nodiscard]] bool validate_wrapper_transaction_capability(
    const Options& options) noexcept
{
    const auto parent = current_parent_process_id();
    if (!parent || options.wrapper_process_id == 0U ||
        *parent != options.wrapper_process_id ||
        options.wrapper_process_id == ::GetCurrentProcessId() ||
        options.wrapper_capability_handle == nullptr ||
        options.wrapper_capability_handle == INVALID_HANDLE_VALUE ||
        options.wrapper_cleanup_capability_handle == nullptr ||
        options.wrapper_cleanup_capability_handle == INVALID_HANDLE_VALUE ||
        options.wrapper_job_handle == nullptr ||
        options.wrapper_job_handle == INVALID_HANDLE_VALUE ||
        options.wrapper_guard_job_handle == nullptr ||
        options.wrapper_guard_job_handle == INVALID_HANDLE_VALUE ||
        options.isolation_release_handle == nullptr ||
        options.isolation_release_handle == INVALID_HANDLE_VALUE ||
        options.wrapper_cleanup_capability_handle ==
            options.wrapper_capability_handle ||
        options.wrapper_guard_job_handle == options.wrapper_job_handle ||
        options.isolation_release_handle == options.wrapper_capability_handle ||
        options.isolation_release_handle ==
            options.wrapper_cleanup_capability_handle ||
        options.isolation_release_handle == options.wrapper_job_handle ||
        options.isolation_release_handle == options.wrapper_guard_job_handle ||
        options.wrapper_job_handle == options.wrapper_capability_handle ||
        options.wrapper_job_handle == options.wrapper_cleanup_capability_handle ||
        options.wrapper_guard_job_handle == options.wrapper_capability_handle ||
        options.wrapper_guard_job_handle ==
            options.wrapper_cleanup_capability_handle) {
        return false;
    }
    DWORD startup_flags = 0U;
    DWORD cleanup_flags = 0U;
    DWORD job_flags = 0U;
    DWORD guard_job_flags = 0U;
    DWORD release_flags = 0U;
    return ::GetHandleInformation(
               options.wrapper_capability_handle, &startup_flags) != FALSE &&
           (startup_flags & HANDLE_FLAG_INHERIT) != 0U &&
           ::GetHandleInformation(
               options.wrapper_cleanup_capability_handle, &cleanup_flags) != FALSE &&
           (cleanup_flags & HANDLE_FLAG_INHERIT) != 0U &&
           ::GetHandleInformation(options.wrapper_job_handle, &job_flags) != FALSE &&
           (job_flags & HANDLE_FLAG_INHERIT) != 0U &&
           ::GetHandleInformation(
               options.wrapper_guard_job_handle, &guard_job_flags) != FALSE &&
           (guard_job_flags & HANDLE_FLAG_INHERIT) != 0U &&
           ::GetHandleInformation(
               options.isolation_release_handle, &release_flags) != FALSE &&
           (release_flags & HANDLE_FLAG_INHERIT) != 0U &&
           ::WaitForSingleObject(options.wrapper_capability_handle, 0U) ==
               WAIT_TIMEOUT &&
           ::WaitForSingleObject(
               options.wrapper_cleanup_capability_handle, 0U) == WAIT_TIMEOUT &&
           ::WaitForSingleObject(options.isolation_release_handle, 0U) ==
               WAIT_TIMEOUT &&
           ::SetEvent(options.wrapper_capability_handle) != FALSE &&
           ::WaitForSingleObject(options.wrapper_capability_handle, 0U) ==
               WAIT_OBJECT_0;
}

[[nodiscard]] bool signal_wrapper_cleanup_capability(
    const Options& options) noexcept
{
    return options.wrapper_cleanup_capability_handle != nullptr &&
           options.wrapper_cleanup_capability_handle != INVALID_HANDLE_VALUE &&
           ::SetEvent(options.wrapper_cleanup_capability_handle) != FALSE &&
           ::WaitForSingleObject(
               options.wrapper_cleanup_capability_handle, 0U) == WAIT_OBJECT_0;
}

[[nodiscard]] bool signal_wrapper_empty_cleanup_capabilities(
    const Options& options) noexcept
{
    return options.isolation_release_handle != nullptr &&
           options.isolation_release_handle != INVALID_HANDLE_VALUE &&
           ::SetEvent(options.isolation_release_handle) != FALSE &&
           ::WaitForSingleObject(options.isolation_release_handle, 0U) ==
               WAIT_OBJECT_0 &&
           signal_wrapper_cleanup_capability(options);
}

[[nodiscard]] bool exact_marker_present(const fs::path& root)
{
    const auto marker = root / L".hlclient-research-isolated";
    std::ifstream input{marker, std::ios::binary};
    if (!input) return false;
    std::string content((std::istreambuf_iterator<char>{input}),
                        std::istreambuf_iterator<char>{});
    return content == "HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1" ||
           content == "HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1\r\n" ||
           content == "HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1\n";
}

[[nodiscard]] bool path_is_within(
    const fs::path& root,
    const fs::path& candidate)
{
    std::error_code error;
    auto canonical_root = fs::weakly_canonical(root, error).wstring();
    if (error) return false;
    auto canonical_candidate = fs::weakly_canonical(candidate, error).wstring();
    if (error || canonical_candidate.size() <= canonical_root.size()) return false;
    if (canonical_root.back() == L'\\') canonical_root.pop_back();
    if (::CompareStringOrdinal(
            canonical_root.data(), static_cast<int>(canonical_root.size()),
            canonical_candidate.data(), static_cast<int>(canonical_root.size()),
            TRUE) != CSTR_EQUAL) return false;
    return canonical_candidate[canonical_root.size()] == L'\\';
}

[[nodiscard]] bool paths_equal(const fs::path& left, const fs::path& right)
{
    std::error_code error;
    const auto canonical_left = fs::weakly_canonical(left, error).wstring();
    if (error) return false;
    const auto canonical_right = fs::weakly_canonical(right, error).wstring();
    if (error || canonical_left.size() != canonical_right.size()) return false;
    return ::CompareStringOrdinal(
               canonical_left.data(), static_cast<int>(canonical_left.size()),
               canonical_right.data(), static_cast<int>(canonical_right.size()),
               TRUE) == CSTR_EQUAL;
}

[[nodiscard]] fs::path sibling_executable(const wchar_t* filename)
{
    std::wstring module(32'768U, L'\0');
    const DWORD size = ::GetModuleFileNameW(
        nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (size == 0U || size >= module.size()) return {};
    module.resize(size);
    return fs::path{std::move(module)}.parent_path() / filename;
}

struct Environment final {
    windows::StockBinaryProfileObservation profile;
    windows::WindowsBinaryIdentity client;
    windows::WindowsBinaryIdentity server;
    windows::WindowsBinaryIdentity relay;
    windows::WindowsBinaryIdentity guard;
    windows::WindowsBinaryIdentity probe;
    windows::NetworkIsolationCanaryResult canary;
};

struct EnvironmentResult final {
    std::optional<Environment> environment;
    std::string failure;
};

[[nodiscard]] windows::WindowsBinaryIdentityResult observe_project_binary(
    const fs::path& path) noexcept
{
    return windows::observe_windows_binary_identity(
        path, windows::kMaximumObservedExecutableBytes,
        {windows::AuthenticodePolicy::not_required_for_project_owned_binary,
         false});
}

[[nodiscard]] EnvironmentResult validate_environment(const Options& options)
{
    if (!windows::windows_process_is_elevated()) {
        return {std::nullopt, "network-isolation-privilege-required"};
    }
    if (!options.research_root.is_absolute() ||
        !exact_marker_present(options.research_root) ||
        !path_is_within(options.research_root, options.client) ||
        !path_is_within(options.research_root, options.server)) {
        return {std::nullopt, "unsafe-research-root"};
    }
    const auto primary_steam_root =
        options.app_manifest.parent_path() / L"common" / L"Half-Life";
    if (paths_equal(options.research_root, primary_steam_root) ||
        path_is_within(primary_steam_root, options.research_root) ||
        path_is_within(options.research_root, primary_steam_root)) {
        return {std::nullopt, "primary-steam-root-forbidden"};
    }
    const auto profile = windows::observe_required_stock_binary_profile(
        options.client, options.server, options.app_manifest);
    if (!profile) {
        return {std::nullopt,
                "binary-profile-" + std::string{windows::to_string(profile.code)}};
    }
    const auto client = windows::observe_windows_binary_identity(options.client);
    const auto server = windows::observe_windows_binary_identity(options.server);
    const auto relay = observe_project_binary(options.relay);
    const auto guard = observe_project_binary(options.isolation_guard);
    const auto probe_path = sibling_executable(L"hlclient_network_isolation_probe.exe");
    const auto probe = observe_project_binary(probe_path);
    if (!client || !server || !relay || !guard || !probe) {
        return {std::nullopt, "executable-identity-invalid"};
    }
    windows::WindowsBinaryIdentityErrorCode binary_error{};
    windows::NetworkIsolationErrorCode isolation_error{};
    auto probe_application = windows::observe_network_isolation_application(
        probe_path, binary_error, isolation_error);
    if (!probe_application) {
        return {std::nullopt, "isolation-probe-identity-invalid"};
    }
    windows::NetworkIsolationPolicy policy;
    policy.applications.push_back(std::move(*probe_application));
    const auto canary = windows::run_network_isolation_canary(probe_path, policy);
    if (!canary) {
        return {std::nullopt,
                std::string{windows::to_string(canary.status)}};
    }
    return {Environment{*profile.observation, *client.identity, *server.identity,
                        *relay.identity, *guard.identity, *probe.identity, canary},
            {}};
}

[[nodiscard]] bool is_lower_hex_run_id(const std::wstring_view value) noexcept
{
    if (value.size() != 32U) return false;
    return std::ranges::all_of(value, [](const wchar_t character) {
        return (character >= L'0' && character <= L'9') ||
               (character >= L'a' && character <= L'f');
    });
}

[[nodiscard]] bool has_reparse_component(const fs::path& path) noexcept
{
    auto current = path.root_path();
    for (const auto& component : path.relative_path()) {
        current /= component;
        const DWORD attributes = ::GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = ::GetLastError();
            return error != ERROR_FILE_NOT_FOUND &&
                   error != ERROR_PATH_NOT_FOUND;
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) return true;
    }
    return false;
}

[[nodiscard]] bool validate_new_run_root(const fs::path& run_root)
{
    std::error_code error;
    const auto repository = fs::weakly_canonical(fs::current_path(), error);
    if (error) return false;
    const auto manual_root = fs::weakly_canonical(
        repository / L"manual-artifacts", error);
    if (error || !fs::is_directory(manual_root, error) || error ||
        has_reparse_component(manual_root)) return false;
    const auto parent = (manual_root / L"stock-runtime").lexically_normal();
    return !has_reparse_component(parent) && run_root.is_absolute() &&
           run_root.lexically_normal() == run_root &&
           run_root.parent_path().lexically_normal() == parent &&
           is_lower_hex_run_id(run_root.filename().wstring()) &&
           !fs::exists(run_root, error) && !error;
}

[[nodiscard]] bool secure_open_or_create_stock_runtime_parent(
    const fs::path& parent,
    UniqueHandle& held_directory) noexcept
{
    try {
        DWORD attributes = ::GetFileAttributesW(parent.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD absent_error = ::GetLastError();
            if ((absent_error != ERROR_FILE_NOT_FOUND &&
                 absent_error != ERROR_PATH_NOT_FOUND) ||
                ::CreateDirectoryW(parent.c_str(), nullptr) == FALSE) {
                return false;
            }
            attributes = ::GetFileAttributesW(parent.c_str());
        }
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
            has_reparse_component(parent)) {
            return false;
        }
        UniqueHandle directory{::CreateFileW(
            parent.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr)};
        if (!directory) return false;
        FILE_ATTRIBUTE_TAG_INFO tag{};
        if (!::GetFileInformationByHandleEx(
                directory.get(), FileAttributeTagInfo, &tag, sizeof(tag)) ||
            (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
            (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            return false;
        }
        std::wstring final_path(32'768U, L'\0');
        const DWORD final_size = ::GetFinalPathNameByHandleW(
            directory.get(), final_path.data(),
            static_cast<DWORD>(final_path.size()),
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (final_size == 0U || final_size >= final_path.size()) return false;
        final_path.resize(final_size);
        if (final_path.starts_with(LR"(\\?\)")) final_path.erase(0U, 4U);
        auto expected = fs::weakly_canonical(parent).wstring();
        if (expected.starts_with(LR"(\\?\)")) expected.erase(0U, 4U);
        if (final_path.size() != expected.size() ||
            final_path.size() > static_cast<std::size_t>(
                (std::numeric_limits<int>::max)()) ||
            ::CompareStringOrdinal(
                final_path.data(), static_cast<int>(final_path.size()),
                expected.data(), static_cast<int>(expected.size()), TRUE) !=
                CSTR_EQUAL) {
            return false;
        }
        held_directory = std::move(directory);
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool secure_create_and_hold_empty_run_root(
    const fs::path& run_root,
    UniqueHandle& held_directory) noexcept
{
    try {
        if (::CreateDirectoryW(run_root.c_str(), nullptr) == FALSE) return false;
        const DWORD attributes = ::GetFileAttributesW(run_root.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
            has_reparse_component(run_root)) {
            return false;
        }
        UniqueHandle directory{::CreateFileW(
            run_root.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr)};
        if (!directory) return false;
        FILE_ATTRIBUTE_TAG_INFO tag{};
        if (!::GetFileInformationByHandleEx(
                directory.get(), FileAttributeTagInfo, &tag, sizeof(tag)) ||
            (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
            (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            return false;
        }
        std::wstring final_path(32'768U, L'\0');
        const DWORD final_size = ::GetFinalPathNameByHandleW(
            directory.get(), final_path.data(),
            static_cast<DWORD>(final_path.size()),
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (final_size == 0U || final_size >= final_path.size()) return false;
        final_path.resize(final_size);
        if (final_path.starts_with(LR"(\\?\)")) final_path.erase(0U, 4U);
        auto expected = fs::weakly_canonical(run_root).wstring();
        if (expected.starts_with(LR"(\\?\)")) expected.erase(0U, 4U);
        if (final_path.size() != expected.size() ||
            final_path.size() > static_cast<std::size_t>(
                (std::numeric_limits<int>::max)()) ||
            ::CompareStringOrdinal(
                final_path.data(), static_cast<int>(final_path.size()),
                expected.data(), static_cast<int>(expected.size()), TRUE) !=
                CSTR_EQUAL) {
            return false;
        }
        WIN32_FIND_DATAW entry{};
        const auto wildcard = run_root / L"*";
        const HANDLE search = ::FindFirstFileW(wildcard.c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE) return false;
        bool empty = true;
        for (;;) {
            const std::wstring_view name{entry.cFileName};
            if (name != L"." && name != L"..") {
                empty = false;
                break;
            }
            if (::FindNextFileW(search, &entry) == FALSE) break;
        }
        const DWORD enumeration_error = ::GetLastError();
        static_cast<void>(::FindClose(search));
        if (!empty || enumeration_error != ERROR_NO_MORE_FILES) return false;
        held_directory = std::move(directory);
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::wstring to_wide_ascii(const std::string_view value)
{
    return std::wstring{value.begin(), value.end()};
}

[[nodiscard]] std::wstring handle_decimal(const HANDLE handle)
{
    return std::to_wstring(reinterpret_cast<std::uintptr_t>(handle));
}

[[nodiscard]] bool make_pipe(
    UniqueHandle& parent_read,
    UniqueHandle& child_write)
{
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    if (!::CreatePipe(&read, &write, &security, 4'096U)) return false;
    parent_read = UniqueHandle{read};
    child_write = UniqueHandle{write};
    return ::SetHandleInformation(parent_read.get(), HANDLE_FLAG_INHERIT, 0U) !=
        FALSE;
}

[[nodiscard]] bool make_reverse_pipe(
    UniqueHandle& child_read,
    UniqueHandle& parent_write)
{
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    if (!::CreatePipe(&read, &write, &security, 4'096U)) return false;
    child_read = UniqueHandle{read};
    parent_write = UniqueHandle{write};
    return ::SetHandleInformation(parent_write.get(), HANDLE_FLAG_INHERIT, 0U) !=
        FALSE;
}

[[nodiscard]] bool wait_for_pipe_line(
    const HANDLE read,
    windows::OwnedProcess& process,
    const std::string_view expected,
    const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string data;
    data.reserve(1'024U);
    while (std::chrono::steady_clock::now() < deadline && process.running()) {
        DWORD available = 0U;
        if (!::PeekNamedPipe(read, nullptr, 0U, nullptr, &available, nullptr)) {
            return false;
        }
        if (available != 0U) {
            std::array<char, 1'024U> buffer{};
            DWORD count = 0U;
            const DWORD request = (std::min)(available,
                static_cast<DWORD>(buffer.size()));
            if (!::ReadFile(read, buffer.data(), request, &count, nullptr) ||
                data.size() + count > 1'024U) return false;
            data.append(buffer.data(), count);
            if (data.find('\n') != std::string::npos) return data == expected;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
    return false;
}

[[nodiscard]] bool wait_for_log_marker(
    windows::BoundedProcessLogCapture& log,
    std::span<windows::OwnedProcess*> required_processes,
    const std::string_view marker,
    const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::ranges::any_of(required_processes, [](const auto* process) {
                return process == nullptr || !process->running();
            })) return false;
        const auto snapshot = log.snapshot();
        if (snapshot.capture_failed || snapshot.byte_truncated ||
            snapshot.line_count_truncated || snapshot.line_length_truncated) {
            return false;
        }
        if (snapshot.bytes.find(marker) != std::string::npos) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
    return false;
}

[[nodiscard]] bool write_bounded_file(
    const windows::SecureOutputDirectory& directory,
    const std::wstring_view leaf_name,
    const std::string_view bytes,
    const std::size_t maximum = 2U * 1'024U * 1'024U)
{
    if (bytes.size() > maximum) return false;
    const auto byte_view = std::as_bytes(
        std::span{bytes.data(), bytes.size()});
    return static_cast<bool>(windows::secure_atomic_write_new(
        directory, leaf_name, byte_view));
}

[[nodiscard]] std::string log_metadata_json(
    const windows::BoundedProcessLogSnapshot& log)
{
    std::ostringstream output;
    output << "{\n  \"schema\": \"hlclient.stock-runtime-process-log.v1\",\n"
           << "  \"observed_bytes\": " << log.observed_bytes << ",\n"
           << "  \"observed_lines\": " << log.observed_line_count << ",\n"
           << "  \"maximum_line_length\": "
           << log.maximum_observed_line_length << ",\n"
           << "  \"byte_truncated\": "
           << (log.byte_truncated ? "true" : "false") << ",\n"
           << "  \"line_count_truncated\": "
           << (log.line_count_truncated ? "true" : "false") << ",\n"
           << "  \"line_length_truncated\": "
           << (log.line_length_truncated ? "true" : "false") << ",\n"
           << "  \"capture_failed\": "
           << (log.capture_failed ? "true" : "false") << "\n}\n";
    return output.str();
}

struct ActiveSummary final {
    bool success{false};
    std::string failure{"unknown"};
    std::size_t processes_started{0U};
    bool relay_ready{false};
    bool server_ready{false};
    bool client_ready{false};
    bool bounded_transport_complete{false};
    bool cleanup_exact{false};
    std::uint64_t duration_ms{0U};
};

class OwnedJobExitBarrier final {
public:
    OwnedJobExitBarrier(
        windows::KillOnCloseProcessJob& campaign_job,
        windows::KillOnCloseProcessJob& guard_job,
        UniqueHandle& heartbeat_write,
        HANDLE isolation_release,
        std::optional<windows::OwnedJobCleanupResult>& campaign_result,
        std::optional<windows::OwnedJobCleanupResult>& guard_result) noexcept
        : campaign_job_{campaign_job}, guard_job_{guard_job},
          heartbeat_write_{heartbeat_write}, isolation_release_{isolation_release},
          campaign_result_{campaign_result}, guard_result_{guard_result}
    {
    }
    ~OwnedJobExitBarrier()
    {
        campaign_result_ = campaign_job_.terminate_and_wait(
            120U, std::chrono::seconds{10});
        if (!*campaign_result_) return;
        if (::SetEvent(isolation_release_) == FALSE) {
            guard_result_ = windows::OwnedJobCleanupResult{
                windows::OwnedJobCleanupErrorCode::terminate_failed,
                ::GetLastError(), 1U};
            return;
        }
        heartbeat_write_.reset();
        guard_result_ = guard_job_.terminate_and_wait(
            120U, std::chrono::seconds{10});
    }
    OwnedJobExitBarrier(const OwnedJobExitBarrier&) = delete;
    OwnedJobExitBarrier& operator=(const OwnedJobExitBarrier&) = delete;

private:
    windows::KillOnCloseProcessJob& campaign_job_;
    windows::KillOnCloseProcessJob& guard_job_;
    UniqueHandle& heartbeat_write_;
    HANDLE isolation_release_{INVALID_HANDLE_VALUE};
    std::optional<windows::OwnedJobCleanupResult>& campaign_result_;
    std::optional<windows::OwnedJobCleanupResult>& guard_result_;
};

[[nodiscard]] bool exact_process_snapshot_matches(
    const windows::WindowsBinaryIdentity& identity,
    const std::span<const std::uint32_t> expected_process_ids,
    std::string& failure)
{
    const auto scan =
        windows::find_processes_with_exact_image_identity(identity);
    if (!scan) {
        failure = "preexisting-process-scan-" +
            std::string{windows::to_string(scan.code)};
        return false;
    }
    std::vector<std::uint32_t> expected{
        expected_process_ids.begin(), expected_process_ids.end()};
    std::ranges::sort(expected);
    if (scan.process_ids != expected) {
        failure = expected.empty()
            ? "preexisting-research-process"
            : "owned-process-snapshot-mismatch";
        return false;
    }
    return true;
}

[[nodiscard]] ActiveSummary run_active(
    const Options& options,
    const Environment& environment)
{
    const auto started_at = std::chrono::steady_clock::now();
    ActiveSummary summary;
    windows::StockRuntimeStartupState startup;
    const auto finalize_duration = [&]() {
        summary.duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_at).count());
    };
    if (!validate_new_run_root(options.run_root)) {
        summary.failure = "unsafe-run-root";
        finalize_duration();
        return summary;
    }
    if (!exact_process_snapshot_matches(
            environment.client, std::span<const std::uint32_t>{},
            summary.failure) ||
        !exact_process_snapshot_matches(
            environment.server, std::span<const std::uint32_t>{},
            summary.failure)) {
        finalize_duration();
        return summary;
    }
    // Guard isolation has a distinct wrapper-owned Job. This lets the wrapper
    // terminate and prove the campaign Job is empty before allowing the WFP
    // guard to exit after an orchestrator timeout or forced termination.
    auto [campaign_job, campaign_job_result] =
        windows::KillOnCloseProcessJob::adopt_inherited(
            options.wrapper_job_handle, 4U);
    if (!campaign_job_result) {
        summary.failure = std::string{
            windows::to_string(campaign_job_result.code)};
        finalize_duration();
        return summary;
    }
    auto [guard_job, guard_job_result] =
        windows::KillOnCloseProcessJob::adopt_inherited(
            options.wrapper_guard_job_handle, 1U);
    if (!guard_job_result) {
        summary.failure = "guard-job-" + std::string{
            windows::to_string(guard_job_result.code)};
        finalize_duration();
        return summary;
    }

    std::optional<windows::OwnedJobCleanupResult> campaign_exit_barrier_result;
    std::optional<windows::OwnedJobCleanupResult> guard_exit_barrier_result;
    // This outer-scope owner outlives every lambda-local exit barrier and the
    // final retry boundary below. It cannot be destroyed by an early return
    // while either owned Job still lacks exact zero-process accounting.
    windows::DynamicNetworkIsolationSession redundant_isolation;
    const auto execute_owned = [&]() -> ActiveSummary {
    UniqueHandle readiness_read;
    UniqueHandle readiness_write;
    UniqueHandle heartbeat_read;
    UniqueHandle heartbeat_write;
    auto guard_log = windows::BoundedProcessLogCapture::create(
        {64U * 1'024U, 1'024U, 1'024U});
    if (!guard_log || !make_pipe(readiness_read, readiness_write) ||
        !make_reverse_pipe(heartbeat_read, heartbeat_write)) {
        summary.failure = "guard-pipe-failed";
        finalize_duration();
        return summary;
    }
    // Redundant WFP ownership closes the single-guard crash window. The
    // orchestrator session remains active if the guard dies; the guard session
    // remains active if this process dies. Declaring it before exit_barrier
    // guarantees campaign zero is proved before normal destruction closes it.
    // Declared after the parent heartbeat handle: on every later return this
    // barrier terminates and accounts the complete Job before heartbeat_write
    // can close and release the dynamic-WFP guard.
    OwnedJobExitBarrier exit_barrier{
        campaign_job, guard_job, heartbeat_write,
        options.isolation_release_handle,
        campaign_exit_barrier_result, guard_exit_barrier_result};

    windows::NetworkIsolationPolicy redundant_policy;
    const std::array<fs::path, 5U> redundant_applications{
        environment.client.canonical_path,
        environment.server.canonical_path,
        environment.relay.canonical_path,
        environment.probe.canonical_path,
        environment.guard.canonical_path};
    for (const auto& executable : redundant_applications) {
        windows::WindowsBinaryIdentityErrorCode binary_error{};
        windows::NetworkIsolationErrorCode isolation_error{};
        auto application = windows::observe_network_isolation_application(
            executable, binary_error, isolation_error);
        if (!application) {
            summary.failure = "redundant-isolation-" + std::string{
                isolation_error != windows::NetworkIsolationErrorCode::none
                    ? windows::to_string(isolation_error)
                    : windows::to_string(binary_error)};
            finalize_duration();
            return summary;
        }
        redundant_policy.applications.push_back(std::move(*application));
    }
    auto [redundant_candidate, redundant_started] =
        windows::DynamicNetworkIsolationSession::start(redundant_policy);
    if (!redundant_started) {
        summary.failure = "redundant-isolation-" +
            std::string{windows::to_string(redundant_started.code)};
        finalize_duration();
        return summary;
    }
    if (!redundant_started.attestation ||
        !redundant_started.attestation->dynamic_session ||
        !redundant_started.attestation->ipv4_loopback_allowed ||
        !redundant_started.attestation->ipv6_loopback_allowed ||
        !redundant_started.attestation->non_loopback_outbound_blocked ||
        !redundant_started.attestation->non_loopback_inbound_accept_blocked ||
        redundant_started.attestation->application_count !=
            redundant_applications.size() ||
        redundant_started.attestation->persistent_rule_count != 0U) {
        summary.failure = "redundant-isolation-attestation-invalid";
        finalize_duration();
        return summary;
    }
    redundant_isolation = std::move(redundant_candidate);

    windows::OwnedProcessLaunchSpec guard_spec;
    guard_spec.executable = environment.guard.canonical_path;
    guard_spec.working_directory = environment.guard.canonical_path.parent_path();
    guard_spec.expected_identity = environment.guard;
    guard_spec.stdout_handle = guard_log->inherited_write_handle();
    guard_spec.stderr_handle = guard_log->inherited_write_handle();
    guard_spec.additional_inherited_handles = {
        readiness_write.get(), heartbeat_read.get(),
        options.isolation_release_handle, options.wrapper_job_handle,
        options.wrapper_guard_job_handle};
    guard_spec.arguments = {
        L"--readiness-handle", handle_decimal(readiness_write.get()),
        L"--heartbeat-handle", handle_decimal(heartbeat_read.get()),
        L"--isolation-release-handle",
        handle_decimal(options.isolation_release_handle),
        L"--campaign-job-handle",
        handle_decimal(options.wrapper_job_handle),
        L"--guard-job-handle",
        handle_decimal(options.wrapper_guard_job_handle),
        L"--application", environment.client.canonical_path.wstring(),
        L"--application", environment.server.canonical_path.wstring(),
        L"--application", environment.relay.canonical_path.wstring(),
        L"--application", environment.probe.canonical_path.wstring(),
        L"--application", environment.guard.canonical_path.wstring(),
    };
    // Repeat the fail-closed snapshot under the wrapper capability at the
    // last boundary before any owned process is created.
    if (!exact_process_snapshot_matches(
            environment.client, std::span<const std::uint32_t>{},
            summary.failure) ||
        !exact_process_snapshot_matches(
            environment.server, std::span<const std::uint32_t>{},
            summary.failure)) {
        finalize_duration();
        return summary;
    }
    auto [guard, guard_result] = guard_job.launch(guard_spec);
    if (!guard_result) {
        summary.failure = "isolation-guard-" +
            std::string{windows::to_string(guard_result.code)};
        finalize_duration();
        return summary;
    }
    ++summary.processes_started;
    if (!windows::apply_stock_runtime_startup_event(
            startup, windows::StockRuntimeStartupEvent::guard_started)) {
        summary.failure = "startup-state-" +
            std::string{windows::to_string(startup.failure)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    guard_log->close_parent_write_handle();
    readiness_write.reset();
    heartbeat_read.reset();
    constexpr std::string_view guard_ready =
        "network-isolation=ready;session=dynamic;ipv4-loopback=allowed;"
        "ipv6-loopback=allowed;persistent-rules=0\n";
    if (!wait_for_pipe_line(readiness_read.get(), guard, guard_ready,
                            std::chrono::seconds{5})) {
        if (!guard.running()) {
            static_cast<void>(windows::apply_stock_runtime_startup_event(
                startup, windows::StockRuntimeStartupEvent::guard_early_exit));
        }
        summary.failure = "isolation-guard-not-ready";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    readiness_read.reset();
    if (!windows::apply_stock_runtime_startup_event(
            startup,
            windows::StockRuntimeStartupEvent::guard_readiness_observed)) {
        summary.failure = "startup-state-" +
            std::string{windows::to_string(startup.failure)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }

    const auto active_canary =
        windows::run_network_isolation_canary_under_existing_guard(
            campaign_job, environment.probe.canonical_path);
    summary.processes_started += active_canary.processes_started;
    if (!active_canary) {
        summary.failure = "active-guard-" +
            std::string{windows::to_string(active_canary.status)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    if (!windows::apply_stock_runtime_startup_event(
            startup,
            windows::StockRuntimeStartupEvent::active_canary_succeeded)) {
        summary.failure = "startup-state-" +
            std::string{windows::to_string(startup.failure)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    UniqueHandle stock_runtime_parent;
    if (!secure_open_or_create_stock_runtime_parent(
            options.run_root.parent_path(), stock_runtime_parent)) {
        summary.failure = "stock-runtime-parent-unsafe";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    if (fs::exists(options.run_root)) {
        summary.failure = "run-root-created-before-active-canary";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    UniqueHandle run_root_guard;
    if (!secure_create_and_hold_empty_run_root(
            options.run_root, run_root_guard)) {
        summary.failure = "run-root-create-or-hold-failed";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    if (!windows::apply_stock_runtime_startup_event(
            startup, windows::StockRuntimeStartupEvent::run_root_created)) {
        summary.failure = "startup-state-" +
            std::string{windows::to_string(startup.failure)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }

    auto relay_log = windows::BoundedProcessLogCapture::create({});
    if (!relay_log) {
        summary.failure = "relay-log-capture-failed";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    SECURITY_ATTRIBUTES relay_stop_security{};
    relay_stop_security.nLength = sizeof(relay_stop_security);
    relay_stop_security.bInheritHandle = TRUE;
    UniqueHandle relay_stop{::CreateEventW(
        &relay_stop_security, TRUE, FALSE, nullptr)};
    UniqueHandle relay_capability{::CreateEventW(
        &relay_stop_security, TRUE, FALSE, nullptr)};
    if (!relay_stop || !relay_capability) {
        summary.failure = "relay-capability-event-failed";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    windows::OwnedProcessLaunchSpec relay_spec;
    relay_spec.executable = environment.relay.canonical_path;
    relay_spec.working_directory = fs::current_path();
    relay_spec.expected_identity = environment.relay;
    relay_spec.stdout_handle = relay_log->inherited_write_handle();
    relay_spec.stderr_handle = relay_log->inherited_write_handle();
    relay_spec.additional_inherited_handles = {
        relay_stop.get(), relay_capability.get()};
    relay_spec.arguments = {
        L"--listen-port", std::to_wstring(options.relay_port),
        L"--server-port", std::to_wstring(options.server_port),
        L"--output-run-root", options.run_root.wstring(),
        L"--precreated-empty-run-root",
        L"--scenario", to_wide_ascii(options.scenario),
        L"--private-ipv4-loopback-only", L"--one-upstream-socket",
        L"--byte-preserving", L"--no-payload-rewrite",
        L"--max-duration-ms",
        std::to_wstring(options.limits.maximum_duration.count()),
        L"--max-datagrams", std::to_wstring(options.limits.maximum_datagrams),
        L"--max-total-raw-bytes",
        std::to_wstring(options.limits.maximum_total_raw_bytes),
        L"--max-payload-bytes",
        std::to_wstring(options.limits.maximum_payload_bytes),
        L"--max-reassembled-bytes",
        std::to_wstring(options.limits.maximum_reassembled_bytes),
        L"--max-decompressed-bytes",
        std::to_wstring(options.limits.maximum_decompressed_bytes),
        L"--max-message-count",
        std::to_wstring(options.limits.maximum_message_count),
        L"--max-runtime-frames",
        std::to_wstring(options.limits.maximum_runtime_frames),
        L"--max-client-packets",
        std::to_wstring(options.limits.maximum_client_packets),
        L"--max-server-packets",
        std::to_wstring(options.limits.maximum_server_packets),
        L"--mutation-after-client-packets",
        std::to_wstring(options.perturbation.client_packet_ordinal),
        L"--mutation-after-server-packets",
        std::to_wstring(options.perturbation.server_packet_ordinal),
        L"--stop-handle", handle_decimal(relay_stop.get()),
        L"--orchestrator-capability-handle",
        handle_decimal(relay_capability.get()),
        L"--orchestrator-process-id", std::to_wstring(::GetCurrentProcessId()),
    };
    const auto relay_started_at = std::chrono::steady_clock::now();
    auto [relay, relay_result] = campaign_job.launch(relay_spec);
    if (!relay_result) {
        summary.failure = "relay-" +
            std::string{windows::to_string(relay_result.code)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    ++summary.processes_started;
    if (::WaitForSingleObject(relay_capability.get(), 5'000U) != WAIT_OBJECT_0) {
        summary.failure = "relay-capability-not-attested";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    relay_capability.reset();
    if (!windows::apply_stock_runtime_startup_event(
            startup, windows::StockRuntimeStartupEvent::relay_started)) {
        summary.failure = "startup-state-" +
            std::string{windows::to_string(startup.failure)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    relay_log->close_parent_write_handle();
    std::array<windows::OwnedProcess*, 2U> relay_requirements{&relay, &guard};
    summary.relay_ready = wait_for_log_marker(
        *relay_log, relay_requirements,
        "[stock-runtime-capture] relay-ready=true", std::chrono::seconds{5});
    if (!summary.relay_ready || !fs::is_directory(options.run_root)) {
        static_cast<void>(windows::apply_stock_runtime_startup_event(
            startup, relay.running()
                ? windows::StockRuntimeStartupEvent::relay_readiness_timeout
                : windows::StockRuntimeStartupEvent::relay_early_exit));
        summary.failure = "relay-not-ready";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    if (!windows::apply_stock_runtime_startup_event(
            startup,
            windows::StockRuntimeStartupEvent::relay_readiness_observed)) {
        summary.failure = "startup-state-" +
            std::string{windows::to_string(startup.failure)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }

    auto server_log = windows::BoundedProcessLogCapture::create({});
    if (!server_log) {
        summary.failure = "server-log-capture-failed";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    windows::OwnedProcessLaunchSpec server_spec;
    server_spec.executable = environment.server.canonical_path;
    server_spec.working_directory = options.research_root;
    server_spec.expected_identity = environment.server;
    server_spec.stdout_handle = server_log->inherited_write_handle();
    server_spec.stderr_handle = server_log->inherited_write_handle();
    server_spec.arguments = {
        L"-console", L"-game", L"valve", L"-port",
        std::to_wstring(options.server_port), L"+ip", L"127.0.0.1",
        L"+map", to_wide_ascii(options.map), L"+maxplayers", L"8",
        L"+sv_lan", L"1", L"-nomaster", L"+status"};
    if (!exact_process_snapshot_matches(
            environment.client, std::span<const std::uint32_t>{},
            summary.failure) ||
        !exact_process_snapshot_matches(
            environment.server, std::span<const std::uint32_t>{},
            summary.failure)) {
        finalize_duration();
        return summary;
    }
    auto [server, server_result] = campaign_job.launch(server_spec);
    if (!server_result) {
        summary.failure = "server-" +
            std::string{windows::to_string(server_result.code)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    ++summary.processes_started;
    const std::array<std::uint32_t, 1U> expected_server{
        server.process_id()};
    if (!exact_process_snapshot_matches(
            environment.server, expected_server, summary.failure)) {
        finalize_duration();
        return summary;
    }
    if (!windows::apply_stock_runtime_startup_event(
            startup, windows::StockRuntimeStartupEvent::server_started)) {
        summary.failure = "startup-state-" +
            std::string{windows::to_string(startup.failure)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    server_log->close_parent_write_handle();
    windows::HldsBannerParseResult server_profile;
    const auto server_deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds{15};
    while (std::chrono::steady_clock::now() < server_deadline && server.running() &&
           relay.running() && guard.running()) {
        const auto snapshot = server_log->snapshot();
        if (snapshot.capture_failed || snapshot.byte_truncated ||
            snapshot.line_count_truncated || snapshot.line_length_truncated) break;
        server_profile = windows::parse_required_hlds_runtime_banner(
            snapshot.bytes, options.map, options.server_port);
        if (server_profile) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    summary.server_ready = static_cast<bool>(server_profile);
    if (!summary.server_ready) {
        const auto event = !server.running()
            ? windows::StockRuntimeStartupEvent::server_early_exit
            : server_profile.code == windows::HldsBannerParseErrorCode::profile_mismatch ||
                    server_profile.code == windows::HldsBannerParseErrorCode::malformed ||
                    server_profile.code == windows::HldsBannerParseErrorCode::duplicate_field
                ? windows::StockRuntimeStartupEvent::server_banner_mismatch
                : windows::StockRuntimeStartupEvent::server_readiness_timeout;
        static_cast<void>(windows::apply_stock_runtime_startup_event(
            startup, event));
        summary.failure = "server-profile-" +
            std::string{windows::to_string(server_profile.code)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    if (!windows::apply_stock_runtime_startup_event(
            startup,
            windows::StockRuntimeStartupEvent::server_readiness_observed)) {
        summary.failure = "startup-state-" +
            std::string{windows::to_string(startup.failure)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }

    auto client_log = windows::BoundedProcessLogCapture::create({});
    if (!client_log) {
        summary.failure = "client-log-capture-failed";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    windows::OwnedProcessLaunchSpec client_spec;
    client_spec.executable = environment.client.canonical_path;
    client_spec.working_directory = options.research_root;
    client_spec.expected_identity = environment.client;
    client_spec.stdout_handle = client_log->inherited_write_handle();
    client_spec.stderr_handle = client_log->inherited_write_handle();
    client_spec.create_no_window = false;
    client_spec.arguments = {
        L"-game", L"valve", L"-windowed", L"-w", L"800", L"-h", L"600",
        L"+name", L"HLCLIENT_A", L"+connect",
        L"127.0.0.1:" + std::to_wstring(options.relay_port), L"-nojoy"};
    if (!exact_process_snapshot_matches(
            environment.client, std::span<const std::uint32_t>{},
            summary.failure) ||
        !exact_process_snapshot_matches(
            environment.server, expected_server, summary.failure)) {
        finalize_duration();
        return summary;
    }
    auto [client, client_result] = campaign_job.launch(client_spec);
    if (!client_result) {
        summary.failure = "client-" +
            std::string{windows::to_string(client_result.code)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    ++summary.processes_started;
    const std::array<std::uint32_t, 1U> expected_client{
        client.process_id()};
    if (!exact_process_snapshot_matches(
            environment.client, expected_client, summary.failure) ||
        !exact_process_snapshot_matches(
            environment.server, expected_server, summary.failure)) {
        finalize_duration();
        return summary;
    }
    if (!windows::apply_stock_runtime_startup_event(
            startup, windows::StockRuntimeStartupEvent::client_started)) {
        summary.failure = "startup-state-" +
            std::string{windows::to_string(startup.failure)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    client_log->close_parent_write_handle();
    const auto client_deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds{20};
    while (std::chrono::steady_clock::now() < client_deadline && client.running() &&
           server.running() && relay.running() && guard.running()) {
        const auto server_snapshot = server_log->snapshot();
        const auto relay_snapshot = relay_log->snapshot();
        const bool named_client =
            server_snapshot.bytes.find("HLCLIENT_A") != std::string::npos &&
            (server_snapshot.bytes.find("entered the game") != std::string::npos ||
             server_snapshot.bytes.find(" connected") != std::string::npos);
        const bool bidirectional = relay_snapshot.bytes.find(
            "[stock-runtime-capture] bidirectional-traffic=true") !=
            std::string::npos;
        if (named_client && bidirectional) {
            summary.client_ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    if (!summary.client_ready) {
        static_cast<void>(windows::apply_stock_runtime_startup_event(
            startup, client.running()
                ? windows::StockRuntimeStartupEvent::client_readiness_timeout
                : windows::StockRuntimeStartupEvent::client_early_exit));
        summary.failure = "client-ready-not-observed";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    if (!windows::apply_stock_runtime_startup_event(
            startup,
            windows::StockRuntimeStartupEvent::client_readiness_observed)) {
        summary.failure = "startup-state-" +
            std::string{windows::to_string(startup.failure)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }

    const auto relay_deadline = relay_started_at +
        options.limits.maximum_duration - std::chrono::milliseconds{250};
    while (std::chrono::steady_clock::now() < relay_deadline &&
           client.running() && server.running() && relay.running() &&
           guard.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    if (!client.running() || !server.running() || !relay.running() ||
        !guard.running()) {
        const auto event = !guard.running()
            ? windows::StockRuntimeStartupEvent::guard_early_exit
            : !relay.running()
                ? windows::StockRuntimeStartupEvent::relay_early_exit
                : !server.running()
                    ? windows::StockRuntimeStartupEvent::server_early_exit
                    : windows::StockRuntimeStartupEvent::client_early_exit;
        static_cast<void>(windows::apply_stock_runtime_startup_event(
            startup, event));
        summary.failure = "bounded-session-incomplete";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    if (!exact_process_snapshot_matches(
            environment.client, expected_client, summary.failure) ||
        !exact_process_snapshot_matches(
            environment.server, expected_server, summary.failure)) {
        finalize_duration();
        return summary;
    }

    if (!windows::apply_stock_runtime_startup_event(
            startup,
            windows::StockRuntimeStartupEvent::cancellation_requested)) {
        summary.failure = "startup-state-" +
            std::string{windows::to_string(startup.failure)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    client.terminate(0U);
    const auto client_exit = client.wait(std::chrono::seconds{5});
    server.terminate(0U);
    const auto server_exit = server.wait(std::chrono::seconds{5});
    if (!windows::stock_runtime_stock_process_shutdown_confirmed(
            client_exit, server_exit)) {
        summary.failure = "stock-process-finalization-failed";
        finalize_duration();
        return summary;
    }
    if (::SetEvent(relay_stop.get()) == FALSE) {
        summary.failure = "relay-stop-signal-failed";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    const auto relay_exit = relay.wait(std::chrono::seconds{10});
    if (!relay_exit || *relay_exit != 0U) {
        summary.failure = "relay-finalization-failed";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    const auto campaign_process_count = campaign_job.active_process_count();
    if (!campaign_process_count || *campaign_process_count != 0U) {
        summary.failure = "campaign-job-not-empty-before-isolation-release";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    if (::SetEvent(options.isolation_release_handle) == FALSE) {
        summary.failure = "isolation-release-signal-failed";
        finalize_duration();
        return summary;
    }
    relay_stop.reset();
    heartbeat_write.reset();
    auto guard_exit = guard.wait(std::chrono::seconds{5});
    if (!guard_exit || *guard_exit != 0U) {
        guard.terminate(0U);
        guard_exit = guard.wait(std::chrono::seconds{2});
    }
    const auto relay_snapshot = relay_log->finish();
    const auto server_snapshot = server_log->finish();
    const auto client_snapshot = client_log->finish();
    const auto guard_snapshot = guard_log->finish();
    const auto guard_process_count = guard_job.active_process_count();
    const std::optional<std::size_t> total_process_count =
        campaign_process_count && guard_process_count
        ? std::optional<std::size_t>{
              *campaign_process_count + *guard_process_count}
        : std::nullopt;
    summary.cleanup_exact = windows::stock_runtime_graceful_cleanup_is_exact(
        client_exit, server_exit, relay_exit, guard_exit,
        total_process_count);
    if (summary.cleanup_exact) {
        summary.cleanup_exact = windows::apply_stock_runtime_startup_event(
            startup, windows::StockRuntimeStartupEvent::cleanup_completed);
    } else {
        static_cast<void>(windows::apply_stock_runtime_startup_event(
            startup, windows::StockRuntimeStartupEvent::cleanup_inexact));
    }
    if (!summary.cleanup_exact) {
        summary.failure = "job-cleanup-inexact";
        finalize_duration();
        return summary;
    }
    if (!windows::bounded_process_log_snapshot_complete(relay_snapshot) ||
        !windows::bounded_process_log_snapshot_complete(server_snapshot) ||
        !windows::bounded_process_log_snapshot_complete(client_snapshot) ||
        !windows::bounded_process_log_snapshot_complete(guard_snapshot)) {
        summary.failure = "process-log-limit-exceeded";
        finalize_duration();
        return summary;
    }
    summary.bounded_transport_complete = true;

    const auto logs_root = options.run_root / L"logs";
    const bool logs_created = ::CreateDirectoryW(logs_root.c_str(), nullptr) != FALSE;
    const DWORD log_attributes = ::GetFileAttributesW(logs_root.c_str());
    if (!logs_created || log_attributes == INVALID_FILE_ATTRIBUTES ||
        (log_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        summary.failure = "bounded-log-directory-failed";
        finalize_duration();
        return summary;
    }
    auto run_output = windows::open_secure_output_directory(options.run_root);
    auto logs_output = windows::open_secure_output_directory(logs_root);
    if (!run_output || !run_output.directory ||
        !logs_output || !logs_output.directory ||
        !write_bounded_file(*logs_output.directory, L"relay.log",
                            relay_snapshot.bytes) ||
        !write_bounded_file(*logs_output.directory, L"server.log",
                            server_snapshot.bytes) ||
        !write_bounded_file(*logs_output.directory, L"client.log",
                            client_snapshot.bytes) ||
        !write_bounded_file(*logs_output.directory, L"guard.log",
                            guard_snapshot.bytes) ||
        !write_bounded_file(*logs_output.directory, L"relay-metadata.json",
                            log_metadata_json(relay_snapshot)) ||
        !write_bounded_file(*logs_output.directory, L"server-metadata.json",
                            log_metadata_json(server_snapshot)) ||
        !write_bounded_file(*logs_output.directory, L"client-metadata.json",
                            log_metadata_json(client_snapshot)) ||
        !write_bounded_file(*logs_output.directory, L"guard-metadata.json",
                            log_metadata_json(guard_snapshot))) {
        summary.failure = "bounded-log-write-failed";
        finalize_duration();
        return summary;
    }

    std::ostringstream version;
    version << "{\n"
            << "  \"schema\": \"hlclient.stock-runtime-version-observation.v1\",\n"
            << "  \"map_category\": \"" << options.map << "\",\n"
            << "  \"client_file_version\": \""
            << windows::to_string(environment.profile.client_file_version) << "\",\n"
            << "  \"client_pe_machine\": \"x86\",\n"
            << "  \"client_signature\": \"valid\",\n"
            << "  \"client_profile_fingerprint\": \""
            << environment.profile.client_profile_fingerprint << "\",\n"
            << "  \"server_launcher_version\": \""
            << windows::to_string(environment.profile.server_launcher_version)
            << "\",\n  \"server_pe_machine\": \"x86\",\n"
            << "  \"server_signature\": \"valid\",\n"
            << "  \"server_profile_fingerprint\": \""
            << environment.profile.server_profile_fingerprint << "\",\n"
            << "  \"steam_app_id\": 70,\n"
            << "  \"steam_build_id\": 15961492,\n"
            << "  \"server_engine_version\": \"1.1.2.2\",\n"
            << "  \"protocol\": 48,\n"
            << "  \"server_build\": 10210,\n"
            << "  \"evidence_status\": \"observed\"\n}\n";
    std::ostringstream isolation;
    isolation << "{\n"
              << "  \"schema\": \"hlclient.stock-runtime-isolation-attestation.v1\",\n"
              << "  \"session_type\": \"dynamic\",\n"
              << "  \"persistent_rule_count\": 0,\n"
              << "  \"ipv4_loopback\": \"allowed\",\n"
              << "  \"ipv6_loopback\": \""
              << (environment.canary.ipv6_loopback_allowed
                      ? "allowed" : "capability_unavailable") << "\",\n"
              << "  \"non_loopback_canary\": \"denied_os_classified\",\n"
              << "  \"cleanup_status\": \"exact\",\n"
              << "  \"evidence_status\": \"observed\"\n}\n";
    if (!write_bounded_file(*run_output.directory,
                            L"version-observation.staged.json", version.str()) ||
        !write_bounded_file(*run_output.directory,
                            L"isolation-attestation.staged.json",
                            isolation.str())) {
        summary.failure = "attestation-write-failed";
        finalize_duration();
        return summary;
    }
    summary.success = true;
    summary.failure = "none";
    finalize_duration();
    return summary;
    };

    try {
        summary = execute_owned();
    } catch (...) {
        summary.success = false;
        summary.bounded_transport_complete = false;
        summary.failure = "orchestrator-exception";
        finalize_duration();
    }
    // Every post-launch return above converges here.  PowerShell may not begin
    // restoration until the Job accounting query has proved that no owned
    // process remains; closing a kill-on-close handle alone is asynchronous.
    auto final_campaign_cleanup = campaign_exit_barrier_result
        ? *campaign_exit_barrier_result
        : campaign_job.terminate_and_wait(120U, std::chrono::seconds{10});
    std::optional<windows::OwnedJobCleanupResult> final_guard_cleanup =
        guard_exit_barrier_result;
    const auto cleanup_guard_once = [&]() {
        if (::SetEvent(options.isolation_release_handle) != FALSE) {
            return guard_job.terminate_and_wait(
                120U, std::chrono::seconds{10});
        }
        return windows::OwnedJobCleanupResult{
            windows::OwnedJobCleanupErrorCode::terminate_failed,
            ::GetLastError(), 1U};
    };
    if (final_campaign_cleanup && !final_guard_cleanup) {
        final_guard_cleanup = cleanup_guard_once();
    }
    // If redundant WFP is active, a bounded cleanup failure must not unwind
    // and destroy it. Retry exact Job cleanup until it succeeds or the wrapper
    // terminates this orchestrator; in the latter case the independent guard
    // still owns the identical dynamic policy and the wrapper owns both Jobs.
    while (redundant_isolation.active() &&
           !windows::stock_runtime_owned_jobs_allow_isolation_release(
               final_campaign_cleanup, final_guard_cleanup)) {
        if (!final_campaign_cleanup) {
            final_campaign_cleanup = campaign_job.terminate_and_wait(
                120U, std::chrono::seconds{10});
        } else {
            final_guard_cleanup = cleanup_guard_once();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
    summary.cleanup_exact =
        windows::stock_runtime_owned_jobs_allow_isolation_release(
            final_campaign_cleanup, final_guard_cleanup);
    if (!final_campaign_cleanup) {
        summary.success = false;
        summary.bounded_transport_complete = false;
        summary.failure = "campaign-job-cleanup-" +
            std::string{windows::to_string(final_campaign_cleanup.code)};
    } else if (!final_guard_cleanup || !*final_guard_cleanup) {
        summary.success = false;
        summary.bounded_transport_complete = false;
        summary.failure = "guard-job-cleanup-" +
            std::string{windows::to_string(final_guard_cleanup
                ? final_guard_cleanup->code
                : windows::OwnedJobCleanupErrorCode::timeout)};
    }
    if (summary.cleanup_exact) redundant_isolation.close();
    campaign_job.close();
    guard_job.close();
    finalize_duration();
    return summary;
}

void print_key_value(const std::string_view key, const std::string_view value)
{
    std::cout << kPrefix << key << '=' << value << '\n';
}

} // namespace

int wmain(const int argc, wchar_t** argv)
{
    const auto options = parse_options(argc, argv);
    if (!options) {
        std::cerr << "Usage: hlclient_stock_runtime_orchestrator "
                     "--validate-environment <static paths> OR "
                     "--confirmation-token HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1 "
                     "<active options>\n";
        return 2;
    }
    if (options->validate_config) {
        print_key_value("configuration", "valid");
        print_key_value("stock-processes-started", "0");
        print_key_value("capture-files-written", "0");
        print_key_value("result", "success");
        return 0;
    }
    if (!options->validate_environment &&
        !validate_wrapper_transaction_capability(*options)) {
        print_key_value("orchestrator", "failed");
        print_key_value(
            "failure-category", "wrapper_transaction_capability_required");
        print_key_value("processes-started", "0");
        print_key_value("capture-files-written", "0");
        print_key_value("job-cleanup", "exact");
        print_key_value("result", "failed");
        return 3;
    }
    const auto environment = validate_environment(*options);
    if (!environment.environment) {
        const bool cleanup_signalled = options->validate_environment ||
            signal_wrapper_empty_cleanup_capabilities(*options);
        print_key_value("active-environment", "invalid");
        print_key_value("failure-category", cleanup_signalled
            ? environment.failure : "wrapper-cleanup-signal-failed");
        print_key_value("stock-processes-started", "0");
        print_key_value("capture-files-written", "0");
        if (!options->validate_environment) {
            print_key_value("job-cleanup", "exact");
        }
        print_key_value("result", "failed");
        return 1;
    }
    if (options->validate_environment) {
        print_key_value("active-environment", "valid");
        print_key_value("isolation-canary", "success");
        print_key_value("binary-profile", "valid");
        print_key_value("stock-processes-started", "0");
        print_key_value("capture-files-written", "0");
        print_key_value("result", "success");
        return 0;
    }
    auto summary = run_active(*options, *environment.environment);
    if (summary.cleanup_exact &&
        !signal_wrapper_cleanup_capability(*options)) {
        summary.success = false;
        summary.bounded_transport_complete = false;
        summary.failure = "wrapper-cleanup-signal-failed";
    }
    print_key_value("orchestrator", summary.success ? "success" : "failed");
    print_key_value("failure-category", summary.failure);
    print_key_value("duration-ms", std::to_string(summary.duration_ms));
    print_key_value("processes-started", std::to_string(summary.processes_started));
    print_key_value("relay-ready", summary.relay_ready ? "true" : "false");
    print_key_value("server-ready", summary.server_ready ? "true" : "false");
    print_key_value("client-ready", summary.client_ready ? "true" : "false");
    print_key_value("bounded-transport-complete",
                    summary.bounded_transport_complete ? "true" : "false");
    print_key_value("job-cleanup", summary.cleanup_exact ? "exact" : "incomplete");
    print_key_value("result", summary.success ? "success" : "failed");
    return summary.success ? 0 : 1;
}
