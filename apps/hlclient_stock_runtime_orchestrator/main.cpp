#include <hlclient/goldsrc/stock_runtime_capture.hpp>
#include <hlclient/goldsrc/stock_runtime_reconnect_lifecycle.hpp>
#include <hlclient/platform/windows/binary_identity.hpp>
#include <hlclient/platform/windows/network_isolation.hpp>
#include <hlclient/platform/windows/process_orchestrator.hpp>
#include <hlclient/platform/windows/secure_output.hpp>
#include <hlclient/platform/windows/stock_research_copy.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iterator>
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
constexpr std::wstring_view kPrivateDiagnosticToken =
    L"HLCLIENT_PRIVATE_HLDS_BANNER_DIAGNOSTIC_V1";
constexpr std::wstring_view kFunctionalSmokeToken =
    L"HLCLIENT_LOCAL_RESEARCH_COPY_SMOKE_V1";
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

enum class ProjectClientStop : std::uint8_t {
    delta_schemas,
    live_runtime_state,
    live_usercmd_check,
    live_visual_control,
};

enum class ProjectClientLiveInput : std::uint8_t {
    scripted_check,
    scripted_side_check,
    scripted_jump_duck_check,
    scripted_speed_check,
    keyboard_mouse,
};

[[nodiscard]] constexpr bool emits_jump_duck_native_status(
    const ProjectClientLiveInput source) noexcept {
    return source == ProjectClientLiveInput::scripted_jump_duck_check;
}

[[nodiscard]] constexpr bool emits_speed_native_status(
    const ProjectClientLiveInput source) noexcept {
    return source == ProjectClientLiveInput::scripted_speed_check;
}

[[nodiscard]] constexpr std::wstring_view project_client_stop_argument(
    const ProjectClientStop stop) noexcept
{
    switch (stop) {
    case ProjectClientStop::delta_schemas: return L"delta-schemas";
    case ProjectClientStop::live_runtime_state: return L"live-runtime-state";
    case ProjectClientStop::live_usercmd_check: return L"live-usercmd-check";
    case ProjectClientStop::live_visual_control: return L"live-visual-control";
    }
    return L"delta-schemas";
}

struct Options final {
    bool validate_config{false};
    bool validate_functional_log_observation{false};
    bool validate_functional_lifecycle{false};
    bool validate_functional_lifecycle_limit{false};
    bool validate_functional_lifecycle_writer_failure{false};
    std::optional<std::string>
        validate_functional_lifecycle_injected_failure;
    bool validate_environment{false};
    bool diagnose_server_profile{false};
    bool private_server_profile_diagnostic{false};
    bool functional_smoke{false};
    bool project_client_stock_signon{false};
    ProjectClientStop project_client_stop{ProjectClientStop::delta_schemas};
    ProjectClientLiveInput project_client_live_input{
        ProjectClientLiveInput::scripted_check};
    bool project_client_reference_prediction{false};
    bool validate_wrapper_startup{false};
    windows::HldsRuntimeProfile::Id server_profile_id{
        windows::HldsRuntimeProfile::Id::legacy_stdio_hlds_banner_v1};
    std::optional<goldsrc::StockRuntimeCaptureOutputRole> output_role;
    bool confirmation_seen{false};
    bool private_confirmation_seen{false};
    bool functional_confirmation_seen{false};
    HANDLE wrapper_capability_handle{INVALID_HANDLE_VALUE};
    HANDLE wrapper_cleanup_capability_handle{INVALID_HANDLE_VALUE};
    HANDLE wrapper_job_handle{INVALID_HANDLE_VALUE};
    HANDLE wrapper_guard_job_handle{INVALID_HANDLE_VALUE};
    HANDLE isolation_release_handle{INVALID_HANDLE_VALUE};
    HANDLE writer_trace_prelaunch_ready_handle{INVALID_HANDLE_VALUE};
    HANDLE writer_trace_launch_release_handle{INVALID_HANDLE_VALUE};
    HANDLE writer_trace_stock_stopped_handle{INVALID_HANDLE_VALUE};
    std::uint32_t wrapper_process_id{0U};
    fs::path run_root;
    fs::path research_root;
    fs::path client;
    fs::path server;
    fs::path relay;
    fs::path isolation_guard;
    fs::path app_manifest;
    fs::path steam_api_runtime;
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
    if (argc == 2 && std::wstring_view{argv[1]} ==
            L"--validate-functional-log-observation") {
        Options options;
        options.validate_functional_log_observation = true;
        return options;
    }
    Options options;
    std::array<bool, 52U> seen{};
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
        if (name == L"--diagnose-server-profile") {
            if (!mark(33U)) return std::nullopt;
            options.diagnose_server_profile = true;
            continue;
        }
        if (name == L"--private-diagnose-server-profile") {
            if (!mark(34U)) return std::nullopt;
            options.diagnose_server_profile = true;
            options.private_server_profile_diagnostic = true;
            continue;
        }
        if (name == L"--functional-smoke") {
            if (!mark(40U)) return std::nullopt;
            options.functional_smoke = true;
            continue;
        }
        if (name == L"--project-client-stock-signon") {
            if (!mark(47U)) return std::nullopt;
            options.project_client_stock_signon = true;
            continue;
        }
        if (name == L"--validate-wrapper-startup") {
            if (!mark(42U)) return std::nullopt;
            options.validate_wrapper_startup = true;
            continue;
        }
        if (name == L"--validate-functional-lifecycle") {
            if (!mark(43U)) return std::nullopt;
            options.validate_functional_lifecycle = true;
            continue;
        }
        if (name == L"--validate-functional-lifecycle-limit") {
            if (!mark(44U)) return std::nullopt;
            options.validate_functional_lifecycle_limit = true;
            continue;
        }
        if (name == L"--validate-functional-lifecycle-writer-failure") {
            if (!mark(45U)) return std::nullopt;
            options.validate_functional_lifecycle_writer_failure = true;
            continue;
        }
        if (index + 1 >= argc) return std::nullopt;
        const std::wstring_view value{argv[++index]};
        std::size_t option = 0U;
        if (name == L"--output-role") option = 1U;
        else if (name == L"--confirmation-token") option = 2U;
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
        else if (name == L"--private-diagnostic-token") option = 35U;
        else if (name == L"--server-profile-id")
            option = 36U;
        else if (name == L"--writer-trace-prelaunch-ready-handle") option = 37U;
        else if (name == L"--writer-trace-launch-release-handle") option = 38U;
        else if (name == L"--writer-trace-stock-stopped-handle") option = 39U;
        else if (name == L"--functional-confirmation-token") option = 41U;
        else if (name == L"--validate-functional-lifecycle-injected-failure")
            option = 46U;
        else if (name == L"--steam-api-runtime") option = 48U;
        else if (name == L"--project-client-stop") option = 49U;
        else if (name == L"--project-client-live-input") option = 50U;
        else if (name == L"--project-client-prediction") option = 51U;
        else return std::nullopt;
        if (!mark(option)) return std::nullopt;

        switch (option) {
        case 1U: {
            const auto token = narrow_safe_token(value);
            if (!token) return std::nullopt;
            options.output_role =
                goldsrc::parse_stock_runtime_capture_output_role(*token);
            if (!options.output_role) return std::nullopt;
            break;
        }
        case 2U:
            options.confirmation_seen = value == kConfirmationToken;
            if (!options.confirmation_seen) return std::nullopt;
            break;
        case 35U:
            options.private_confirmation_seen = value == kPrivateDiagnosticToken;
            if (!options.private_confirmation_seen) return std::nullopt;
            break;
        case 41U:
            options.functional_confirmation_seen = value == kFunctionalSmokeToken;
            if (!options.functional_confirmation_seen) return std::nullopt;
            break;
        case 46U: {
            const auto token = narrow_safe_token(value);
            if (!token || (*token != "receive" && *token != "send")) {
                return std::nullopt;
            }
            options.validate_functional_lifecycle_injected_failure = *token;
            break;
        }
        case 36U: {
            const auto token = narrow_safe_token(value);
            if (!token) return std::nullopt;
            if (*token == "legacy-stdio-hlds-banner-v1") {
                options.server_profile_id =
                    windows::HldsRuntimeProfile::Id::legacy_stdio_hlds_banner_v1;
            } else if (*token == "steam-hlds-10210-no-mode-banner-v1") {
                options.server_profile_id =
                    windows::HldsRuntimeProfile::Id::steam_hlds_10210_no_mode_banner_v1;
            } else {
                return std::nullopt;
            }
            break;
        }
        case 3U: options.run_root = value; break;
        case 4U: options.research_root = value; break;
        case 5U: options.client = value; break;
        case 6U: options.server = value; break;
        case 7U: options.relay = value; break;
        case 8U: options.isolation_guard = value; break;
        case 9U: options.app_manifest = value; break;
        case 48U: options.steam_api_runtime = value; break;
        case 49U:
            if (value == L"delta-schemas") {
                options.project_client_stop = ProjectClientStop::delta_schemas;
            } else if (value == L"live-runtime-state") {
                options.project_client_stop =
                    ProjectClientStop::live_runtime_state;
            } else if (value == L"live-usercmd-check") {
                options.project_client_stop =
                    ProjectClientStop::live_usercmd_check;
            } else if (value == L"live-visual-control") {
                options.project_client_stop =
                    ProjectClientStop::live_visual_control;
            } else {
                return std::nullopt;
            }
            break;
        case 50U:
            if (value == L"scripted-check") {
                options.project_client_live_input =
                    ProjectClientLiveInput::scripted_check;
            } else if (value == L"scripted-side-check") {
                options.project_client_live_input =
                    ProjectClientLiveInput::scripted_side_check;
            } else if (value == L"scripted-jump-duck-check") {
                options.project_client_live_input =
                    ProjectClientLiveInput::scripted_jump_duck_check;
            } else if (value == L"scripted-speed-check") {
                options.project_client_live_input =
                    ProjectClientLiveInput::scripted_speed_check;
            } else if (value == L"keyboard-mouse") {
                options.project_client_live_input =
                    ProjectClientLiveInput::keyboard_mouse;
            } else {
                return std::nullopt;
            }
            break;
        case 51U:
            if (value != L"reference") return std::nullopt;
            options.project_client_reference_prediction = true;
            break;
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
        case 32U:
        case 37U:
        case 38U:
        case 39U: {
            std::uintptr_t handle = 0U;
            if (!parse_wide_decimal(value, handle) || handle == 0U) {
                return std::nullopt;
            }
            if (option == 31U) {
                options.wrapper_guard_job_handle = reinterpret_cast<HANDLE>(handle);
            } else if (option == 32U) {
                options.isolation_release_handle = reinterpret_cast<HANDLE>(handle);
            } else if (option == 37U) {
                options.writer_trace_prelaunch_ready_handle =
                    reinterpret_cast<HANDLE>(handle);
            } else if (option == 38U) {
                options.writer_trace_launch_release_handle =
                    reinterpret_cast<HANDLE>(handle);
            } else {
                options.writer_trace_stock_stopped_handle =
                    reinterpret_cast<HANDLE>(handle);
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
    if (options.validate_functional_lifecycle) {
        if (options.run_root.empty() || options.client.empty() ||
            options.server.empty() || options.relay.empty() ||
            options.game != "valve" || options.map != "boot_camp" ||
            options.scenario != "idle-runtime" || options.relay_port == 0U ||
            options.server_port == 0U ||
            options.relay_port == options.server_port ||
            !options.output_role || *options.output_role != goldsrc::
                StockRuntimeCaptureOutputRole::functional_runtime_capture ||
            options.confirmation_seen || options.private_confirmation_seen ||
             options.functional_confirmation_seen ||
            options.project_client_stock_signon ||
            seen[49U] || seen[50U] || seen[51U] ||
            !options.steam_api_runtime.empty() ||
            options.validate_environment || options.diagnose_server_profile ||
            options.functional_smoke || options.validate_wrapper_startup ||
             options.maximum_duration_seconds != 90U ||
            (options.validate_functional_lifecycle_limit &&
             options.limits.maximum_client_packets != 4U) ||
            (options.validate_functional_lifecycle_limit &&
             options.validate_functional_lifecycle_writer_failure) ||
            ((options.validate_functional_lifecycle_limit ||
              options.validate_functional_lifecycle_writer_failure) &&
             options.validate_functional_lifecycle_injected_failure) ||
            !goldsrc::validate_stock_runtime_capture_limits(options.limits)) {
            return std::nullopt;
        }
        return options;
    }
    if (options.validate_functional_lifecycle_limit ||
        options.validate_functional_lifecycle_writer_failure ||
        options.validate_functional_lifecycle_injected_failure) {
        return std::nullopt;
    }
    if (options.research_root.empty() || options.client.empty() ||
        options.server.empty() || options.relay.empty() ||
        options.isolation_guard.empty() || options.app_manifest.empty() ||
        (options.project_client_stock_signon !=
         !options.steam_api_runtime.empty()) ||
        (seen[49U] && !options.project_client_stock_signon) ||
        (seen[50U] !=
         (options.project_client_stock_signon &&
          options.project_client_stop ==
              ProjectClientStop::live_visual_control)) ||
        (seen[51U] && (!options.project_client_stock_signon ||
          options.project_client_stop !=
              ProjectClientStop::live_visual_control)) ||
        !goldsrc::validate_stock_runtime_capture_limits(options.limits)) {
        return std::nullopt;
    }
    if (options.project_client_stock_signon &&
        !options.validate_environment && !options.functional_smoke) {
        return std::nullopt;
    }
    if (options.validate_environment) {
        if (options.diagnose_server_profile || options.functional_smoke ||
            options.validate_functional_lifecycle ||
            options.validate_functional_lifecycle_limit ||
            options.validate_functional_lifecycle_writer_failure ||
            options.validate_functional_lifecycle_injected_failure ||
            options.validate_wrapper_startup ||
            options.output_role ||
            options.confirmation_seen || options.private_confirmation_seen ||
            options.functional_confirmation_seen ||
            !options.run_root.empty() ||
            !options.scenario.empty() ||
            options.wrapper_capability_handle != INVALID_HANDLE_VALUE ||
            options.wrapper_cleanup_capability_handle != INVALID_HANDLE_VALUE ||
            options.wrapper_job_handle != INVALID_HANDLE_VALUE ||
            options.wrapper_guard_job_handle != INVALID_HANDLE_VALUE ||
            options.isolation_release_handle != INVALID_HANDLE_VALUE ||
            options.writer_trace_prelaunch_ready_handle != INVALID_HANDLE_VALUE ||
            options.writer_trace_launch_release_handle != INVALID_HANDLE_VALUE ||
            options.writer_trace_stock_stopped_handle != INVALID_HANDLE_VALUE ||
            options.wrapper_process_id != 0U ||
            options.server_profile_id !=
                windows::HldsRuntimeProfile::Id::legacy_stdio_hlds_banner_v1 ||
            (!options.game.empty() && options.game != "valve") ||
            options.relay_port == options.server_port) return std::nullopt;
    } else {
        if (options.run_root.empty() || options.game != "valve" ||
            options.map.empty() || options.relay_port == 0U ||
            options.server_port == 0U ||
            options.relay_port == options.server_port) {
            return std::nullopt;
        }
        if (options.functional_smoke) {
            if (!options.functional_confirmation_seen ||
                options.confirmation_seen || options.private_confirmation_seen ||
                options.diagnose_server_profile || options.output_role ||
                !options.scenario.empty() ||
                options.server_profile_id != windows::HldsRuntimeProfile::Id::
                    steam_hlds_10210_no_mode_banner_v1) {
                return std::nullopt;
            }
            if (options.project_client_stock_signon &&
                options.maximum_duration_seconds > 90U) {
                return std::nullopt;
            }
        } else if (options.diagnose_server_profile) {
            const auto expected_role = options.private_server_profile_diagnostic
                ? goldsrc::StockRuntimeCaptureOutputRole::
                      server_profile_private_diagnostic
                : goldsrc::StockRuntimeCaptureOutputRole::
                      server_profile_diagnostic;
            const bool exact_confirmation =
                options.private_server_profile_diagnostic
                    ? options.private_confirmation_seen &&
                          !options.confirmation_seen
                    : options.confirmation_seen &&
                          !options.private_confirmation_seen;
            if (!exact_confirmation || !options.output_role ||
                *options.output_role != expected_role ||
                !options.scenario.empty()) {
                return std::nullopt;
            }
        } else if (!options.output_role || !options.confirmation_seen ||
                   options.private_confirmation_seen ||
                   *options.output_role == goldsrc::
                       StockRuntimeCaptureOutputRole::server_profile_diagnostic ||
                   *options.output_role == goldsrc::StockRuntimeCaptureOutputRole::
                       server_profile_private_diagnostic ||
                   !goldsrc::parse_stock_runtime_capture_scenario(
                       options.scenario) ||
                   options.perturbation.client_packet_ordinal == 0U ||
                   options.perturbation.server_packet_ordinal == 0U) {
            return std::nullopt;
        }
    }
    const bool functional_capture_startup_probe =
        options.output_role &&
        *options.output_role == goldsrc::StockRuntimeCaptureOutputRole::
            functional_runtime_capture;
    if (options.validate_wrapper_startup && !options.functional_smoke &&
        !functional_capture_startup_probe) {
        return std::nullopt;
    }
    if (options.output_role && *options.output_role ==
            goldsrc::StockRuntimeCaptureOutputRole::pre_campaign_canary &&
        (options.map != "boot_camp" || options.scenario != "baseline")) {
        return std::nullopt;
    }
    const auto writer_trace_handle_count =
        static_cast<unsigned int>(
            options.writer_trace_prelaunch_ready_handle != INVALID_HANDLE_VALUE) +
        static_cast<unsigned int>(
            options.writer_trace_launch_release_handle != INVALID_HANDLE_VALUE) +
        static_cast<unsigned int>(
            options.writer_trace_stock_stopped_handle != INVALID_HANDLE_VALUE);
    if (writer_trace_handle_count != 0U &&
        (writer_trace_handle_count != 3U ||
         !options.private_server_profile_diagnostic)) {
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
    const bool writer_trace_enabled =
        options.writer_trace_prelaunch_ready_handle != INVALID_HANDLE_VALUE;
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
    const std::array<HANDLE, 8U> inherited_handles{
        options.wrapper_capability_handle,
        options.wrapper_cleanup_capability_handle,
        options.wrapper_job_handle,
        options.wrapper_guard_job_handle,
        options.isolation_release_handle,
        options.writer_trace_prelaunch_ready_handle,
        options.writer_trace_launch_release_handle,
        options.writer_trace_stock_stopped_handle};
    if (writer_trace_enabled) {
        for (std::size_t outer = 0U; outer < inherited_handles.size(); ++outer) {
            if (inherited_handles[outer] == nullptr ||
                inherited_handles[outer] == INVALID_HANDLE_VALUE) return false;
            for (std::size_t inner = outer + 1U;
                 inner < inherited_handles.size(); ++inner) {
                if (inherited_handles[outer] == inherited_handles[inner]) {
                    return false;
                }
            }
        }
    }
    DWORD startup_flags = 0U;
    DWORD cleanup_flags = 0U;
    DWORD job_flags = 0U;
    DWORD guard_job_flags = 0U;
    DWORD release_flags = 0U;
    const bool base_valid = ::GetHandleInformation(
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
    if (!base_valid || !writer_trace_enabled) return base_valid;
    for (const auto handle : std::array<HANDLE, 3U>{
             options.writer_trace_prelaunch_ready_handle,
             options.writer_trace_launch_release_handle,
             options.writer_trace_stock_stopped_handle}) {
        DWORD flags = 0U;
        if (::GetHandleInformation(handle, &flags) == FALSE ||
            (flags & HANDLE_FLAG_INHERIT) == 0U ||
            ::WaitForSingleObject(handle, 0U) != WAIT_TIMEOUT) {
            return false;
        }
    }
    return true;
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
        !windows::stock_research_isolation_marker_exact(
            options.research_root) ||
        (!options.project_client_stock_signon &&
         !path_is_within(options.research_root, options.client)) ||
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
    const auto stock_client = options.project_client_stock_signon
        ? options.research_root / L"hl.exe"
        : options.client;
    const auto profile = windows::observe_required_stock_binary_profile(
        stock_client, options.server, options.app_manifest);
    if (!profile) {
        return {std::nullopt,
                "binary-profile-" + std::string{windows::to_string(profile.code)}};
    }
    const auto client = options.project_client_stock_signon
        ? observe_project_binary(options.client)
        : windows::observe_windows_binary_identity(options.client);
    const auto server = windows::observe_windows_binary_identity(options.server);
    const auto relay = observe_project_binary(options.relay);
    const auto guard = observe_project_binary(options.isolation_guard);
    const auto probe_path = sibling_executable(L"hlclient_network_isolation_probe.exe");
    const auto probe = observe_project_binary(probe_path);
    if (!client || !server || !relay || !guard || !probe) {
        return {std::nullopt, "executable-identity-invalid"};
    }
    if (options.project_client_stock_signon) {
        if (!paths_equal(options.client, sibling_executable(L"hlclient.exe"))) {
            return {std::nullopt, "project-client-path-invalid"};
        }
        const auto expected_runtime = primary_steam_root / L"steam_api.dll";
        if (!options.steam_api_runtime.is_absolute() ||
            !paths_equal(options.steam_api_runtime, expected_runtime)) {
            return {std::nullopt, "steam-api-runtime-path-invalid"};
        }
        const auto steam_runtime = windows::observe_windows_binary_identity(
            options.steam_api_runtime);
        if (!steam_runtime || steam_runtime.identity->pe_machine !=
                windows::WindowsPeMachine::x86) {
            return {std::nullopt, "steam-api-runtime-identity-invalid"};
        }
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

[[nodiscard]] bool validate_new_run_root(
    const fs::path& run_root,
    const std::optional<goldsrc::StockRuntimeCaptureOutputRole> output_role,
    const bool functional_smoke)
{
    std::error_code error;
    const auto repository = fs::weakly_canonical(fs::current_path(), error);
    if (error) return false;
    const auto manual_root = fs::weakly_canonical(
        repository / L"manual-artifacts", error);
    if (error || !fs::is_directory(manual_root, error) || error ||
        has_reparse_component(manual_root)) return false;
    if (functional_smoke == output_role.has_value()) return false;
    const auto parent = (manual_root /
        (functional_smoke
            ? fs::path{L"research-copy-smoke"}
            : fs::path{std::string{
                  goldsrc::stock_runtime_capture_output_parent_directory(
                      *output_role)}})).lexically_normal();
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

[[nodiscard]] std::string server_profile_diagnostic_json(
    const windows::HldsRuntimeProfileDiagnostic& diagnostic,
                               const windows::HldsRuntimeProfile::Id profile_id,
                               const std::optional<windows::HldsLocalReadinessResult>& readiness)
{
    std::ostringstream output;
    output << "{\n"
           << "  \"schema\": "
              "\"hlclient.stock-runtime-server-profile-diagnostic-staged.v2\",\n"
           << "  \"profile_id\": \"" << windows::to_string(profile_id) << "\",\n"
           << "  \"parse_status\": \""
           << windows::to_string(diagnostic.parse_status) << "\",\n"
           << "  \"mismatch_field\": \""
           << windows::to_string(diagnostic.mismatch_field) << "\",\n"
           << "  \"engine_version_status\": \""
           << windows::to_string(diagnostic.engine_version.status) << "\",\n"
           << "  \"runtime_mode_status\": \""
           << windows::to_string(diagnostic.runtime_mode.status) << "\",\n"
           << "  \"runtime_mode_category\": \""
           << windows::to_string(diagnostic.runtime_mode_category) << "\",\n"
           << "  \"game_status\": \""
           << windows::to_string(diagnostic.game.status) << "\",\n"
           << "  \"protocol_status\": \""
           << windows::to_string(diagnostic.protocol.status) << "\",\n"
           << "  \"build_status\": \""
           << windows::to_string(diagnostic.build.status) << "\",\n"
           << "  \"endpoint_address_status\": \""
           << windows::to_string(diagnostic.endpoint_address.status) << "\",\n"
           << "  \"endpoint_address_category\": \""
           << windows::to_string(diagnostic.endpoint_address_category) << "\",\n"
           << "  \"endpoint_port_status\": \""
           << windows::to_string(diagnostic.endpoint_port.status) << "\",\n"
           << "  \"map_status\": \""
           << windows::to_string(diagnostic.map.status) << "\",\n"
           << "  \"duplicate_field_count\": "
           << diagnostic.duplicate_field_count << ",\n"
           << "  \"process_log_truncated\": "
           << (diagnostic.process_log_truncated ? "true" : "false") << ",\n"
           << "  \"observed_byte_count\": "
           << diagnostic.observed_byte_count << ",\n"
           << "  \"observed_line_count\": "
           << diagnostic.observed_line_count << ",\n"
           << "  \"readiness_status\": \""
           << (readiness ? windows::to_string(readiness->status)
                         : std::string_view{"not-applicable"})
           << "\",\n"
           << "  \"endpoint_proof_source\": \""
           << (readiness ? windows::to_string(readiness->endpoint_proof_source)
                         : std::string_view{"absent"})
           << "\",\n"
           << "  \"map_proof_source\": \""
           << (readiness ? windows::to_string(readiness->map_proof_source)
                         : std::string_view{"absent"})
           << '"';
    if (diagnostic.observed_engine_version) {
        output << ",\n  \"observed_engine_version\": \""
               << windows::to_string(*diagnostic.observed_engine_version)
               << '"';
    }
    if (diagnostic.observed_protocol) {
        output << ",\n  \"observed_protocol\": "
               << *diagnostic.observed_protocol;
    }
    if (diagnostic.observed_build) {
        output << ",\n  \"observed_build\": "
               << *diagnostic.observed_build;
    }
    output << "\n}\n";
    return output.str();
}

[[nodiscard]] std::string private_banner_shape_json(
    const windows::HldsPrivateBannerShape& shape)
{
    const auto stream_json = [](std::ostringstream& output,
                                const std::string_view name,
                                const windows::HldsPrivateStreamShape& value) {
        output << "  \"" << name << "\": {\n"
               << "    \"byte_count\": " << value.byte_count << ",\n"
               << "    \"complete_line_count\": "
               << value.complete_line_count << ",\n"
               << "    \"trailing_partial_line_count\": "
               << value.trailing_partial_line_count << ",\n"
               << "    \"nul_byte_count\": " << value.nul_byte_count << ",\n"
               << "    \"escape_byte_count\": "
               << value.escape_byte_count << ",\n"
               << "    \"backspace_byte_count\": "
               << value.backspace_byte_count << ",\n"
               << "    \"high_bit_byte_count\": "
               << value.high_bit_byte_count << ",\n"
               << "    \"utf16_like\": "
               << (value.utf16_like ? "true" : "false") << ",\n"
               << "    \"repeated_carriage_return\": "
               << (value.repeated_carriage_return ? "true" : "false")
               << "\n  }";
    };
    const auto field_json = [](std::ostringstream& output,
                               const std::string_view name,
                               const windows::HldsPrivateFieldShape& value) {
        output << "  \"" << name << "\": {\n"
               << "    \"stdout_candidate_count\": "
               << value.stdout_candidate_count << ",\n"
               << "    \"stderr_candidate_count\": "
               << value.stderr_candidate_count << ",\n"
               << "    \"incomplete_candidate_count\": "
               << value.incomplete_candidate_count << ",\n"
               << "    \"recognized_prefix\": "
               << (value.recognized_prefix ? "true" : "false") << ",\n"
               << "    \"control_contaminated\": "
               << (value.control_contaminated ? "true" : "false")
               << "\n  }";
    };
    std::ostringstream output;
    output << "{\n"
           << "  \"schema\": "
              "\"hlclient.stock-runtime-server-banner-shape-private.v1\",\n"
           << "  \"stream_attribution\": \""
           << windows::to_string(shape.attribution) << "\",\n";
    stream_json(output, "stdout", shape.stdout_shape);
    output << ",\n";
    stream_json(output, "stderr", shape.stderr_shape);
    output << ",\n";
    field_json(output, "engine", shape.engine);
    output << ",\n";
    field_json(output, "runtime_mode", shape.runtime_mode);
    output << ",\n";
    field_json(output, "game", shape.game);
    output << ",\n";
    field_json(output, "protocol", shape.protocol);
    output << ",\n";
    field_json(output, "build", shape.build);
    output << ",\n";
    field_json(output, "endpoint", shape.endpoint);
    output << ",\n";
    field_json(output, "map", shape.map);
    output << "\n}\n";
    return output.str();
}

struct ProjectProtocolProgress final {
    std::string steam_initialization{"not_reached"};
    std::string fresh_material{"not_reached"};
    std::string connect_transmission{"not_reached"};
    std::string accept{"not_reached"};
    std::string serverinfo{"not_reached"};
    std::string schema_registry{"not_reached"};
    std::string movevars{"not_reached"};
    std::string user_info{"not_reached"};
    std::string sendres_queued{"not_reached"};
    std::string sendres_transmitted{"not_reached"};
    std::string sendres_acknowledged{"not_reached"};
    std::string resource_transition{"not_reached"};
    std::string resource_list{"not_reached"};
    std::string resource_response_queued{"not_reached"};
    std::string resource_response_transmitted{"not_reached"};
    std::string resource_response_acknowledged{"not_reached"};
    std::string resource_response{"not_reached"};
    std::string spawn_queued{"not_reached"};
    std::string spawn_transmitted{"not_reached"};
    std::string spawn_acknowledged{"not_reached"};
    std::string baselines{"not_reached"};
    std::string runtime_publication{"not_reached"};
    std::string operational_interval{"not_reached"};
};

struct ProjectTransitionFailure final {
    bool present{false};
    std::optional<std::uint32_t> expected_opcode;
    std::optional<std::uint32_t> actual_opcode;
    std::optional<std::uint32_t> cursor_byte_value;
    std::optional<std::uint64_t> payload_ordinal;
    std::optional<std::uint32_t> source_sequence;
    std::optional<std::uint32_t> source_acknowledgement;
    std::optional<std::uint64_t> wire_size;
    std::optional<std::uint64_t> decoded_size;
    std::optional<std::uint64_t> request_reliable_generation;
    std::optional<std::uint32_t> request_transmit_sequence;
    std::optional<std::uint32_t> request_acknowledgement_sequence;
    std::optional<std::string> stage;
    std::optional<std::string> profile;
    std::optional<std::string> cursor;
    std::optional<std::string> boundary;
    std::optional<std::string> payload_ordinal_scope;
    std::optional<std::string> direction;
    std::optional<std::string> source_reliable;
    std::optional<std::string> reassembled;
    std::optional<std::string> encoding;
    std::optional<std::string> pending_suffix_start;
    std::optional<std::string> sendres_queued;
    std::optional<std::string> sendres_transmitted;
    std::optional<std::string> sendres_acknowledged;
    std::optional<std::string> last_category;
    std::optional<std::string> last_scope;
    std::optional<std::string> last_cursor;
    std::optional<std::string> parser_error;
    std::optional<std::string> primary_error;
};

struct ProjectResponsePayloadDiagnostic final {
    bool present{false};
    std::optional<std::string> classification;
    std::optional<std::string> rx_position;
    std::optional<std::uint64_t> payload_ordinal;
    std::optional<std::uint32_t> source_sequence;
    std::optional<std::uint32_t> source_acknowledgement;
    std::optional<std::string> encoding;
    std::optional<std::uint64_t> wire_size;
    std::optional<std::uint64_t> decoded_size;
    std::optional<std::string> cursor;
    std::optional<std::string> boundary;
    std::optional<std::uint32_t> actual_opcode;
    std::optional<std::string> response_queued;
    std::optional<std::string> response_transmitted;
    std::optional<std::string> response_acknowledged;
    std::optional<std::uint64_t> reliable_generation;
    std::optional<std::uint32_t> first_transmit_sequence;
    std::optional<std::uint64_t> controls_consumed;
    std::optional<std::uint64_t> pending_count;
    std::optional<std::uint64_t> pending_bytes;
    std::optional<std::uint64_t> last_handoff_cursor;
};

struct ActiveSummary final {
    bool success{false};
    std::string failure{"unknown"};
    std::size_t processes_started{0U};
    bool relay_ready{false};
    bool server_ready{false};
    bool client_ready{false};
    bool functional_client_name_observed{false};
    bool functional_client_process_created{false};
    bool functional_client_image_identity_verified{false};
    bool functional_client_resume_succeeded{false};
    bool functional_connect_requested{false};
    bool functional_server_connection_accepted{false};
    bool functional_connection_rejected{false};
    bool functional_connection_timeout_observed{false};
    bool functional_steam_authentication_error_observed{false};
    bool functional_client_entered_game_observed{false};
    bool functional_client_running_at_readiness_deadline{false};
    bool functional_diagnostic_published{false};
    bool project_steam_api_initialized{false};
    bool project_application_entry_observed{false};
    bool project_arguments_accepted{false};
    bool project_provider_begin_observed{false};
    bool project_steam_api_init_attempted{false};
    bool project_fresh_material_acquired{false};
    bool project_connect_sent{false};
    bool project_connection_accepted{false};
    bool project_serverinfo_received{false};
    bool project_schema_registry_received{false};
    bool project_resource_continuation_sent{false};
    bool project_spawn_request_transmitted{false};
    bool project_spawn_request_acknowledged{false};
    bool project_signon_reply_transmitted{false};
    bool project_signon_reply_acknowledged{false};
    bool project_live_service_payloads_received{false};
    bool project_client_world_state_published{false};
    bool project_usercmd_zero_observed{false};
    bool project_usercmd_transmitted{false};
    bool project_usercmd_movement_verified{false};
    bool project_live_visual_verified{false};
    std::optional<std::string> project_jump_duck_result;
    std::optional<std::string> project_speed_result;
    std::optional<std::string> project_prediction_result;
    std::optional<bool> project_jump_observed;
    std::optional<bool> project_descent_observed;
    std::optional<bool> project_duck_observed;
    std::optional<bool> project_release_response_observed;
    std::optional<std::size_t> project_jump_new_submitted;
    std::optional<std::size_t> project_duck_new_submitted;
    std::optional<std::size_t> project_usercmd_generated_count;
    std::optional<std::size_t> project_usercmd_new_count;
    std::optional<std::size_t> project_usercmd_backup_count;
    std::optional<std::size_t> project_usercmd_packet_count;
    std::optional<std::size_t> project_usercmd_server_sample_count;
    std::optional<std::size_t> project_rx_driver_updates_post_input;
    std::optional<std::size_t> project_rx_receive_polls_post_input;
    std::optional<std::size_t> project_rx_owning_datagrams_post_input;
    std::optional<std::size_t> project_rx_payloads_created_post_input;
    std::optional<std::size_t> project_rx_payloads_consumed_post_input;
    std::optional<std::size_t> project_rx_records_committed_post_input;
    std::optional<std::size_t> project_rx_clientdata_committed_post_input;
    std::optional<std::size_t> project_rx_samples_delivered_post_input;
    std::uint32_t functional_server_process_id{0U};
    std::uint32_t functional_client_process_id{0U};
    bool bounded_transport_complete{false};
    bool cleanup_exact{false};
    bool writer_trace_prelaunch_ready{false};
    bool writer_trace_launch_released{false};
    bool writer_trace_stock_processes_stopped{false};
    std::uint64_t writer_trace_clock_frequency{0U};
    std::uint64_t writer_trace_stock_process_created_ticks{0U};
    std::uint64_t writer_trace_stock_processes_stopped_ticks{0U};
    std::size_t connection_generations{1U};
    bool generation_distinct{false};
    std::uint64_t duration_ms{0U};
    std::uint64_t stable_duration_ms{0U};
    std::uint64_t client_map_entry_observed_ms{0U};
    std::uint64_t functional_interval_completed_ms{0U};
    std::uint64_t shutdown_requested_ms{0U};
    std::uint64_t relay_stop_requested_ms{0U};
    std::uint64_t relay_finalization_completed_ms{0U};
    std::string stop_reason{"not-reached"};
    std::string stock_shutdown_method{"not-started"};
    std::string relay_phase{"unavailable"};
    std::string relay_failed_operation{"unavailable"};
    std::string relay_native_error_domain{"unavailable"};
    std::string relay_native_error_code{"unavailable"};
    std::string relay_wait_result{"unavailable"};
    std::string relay_stop_observed{"unavailable"};
    std::string relay_journal_publication_state{"unavailable"};
    std::string relay_metadata_publication_state{"unavailable"};
    std::optional<std::uint32_t> relay_exit_code;
    std::optional<std::uint32_t> client_exit_code;
    std::string client_wait_result{"not-reached"};
    std::optional<std::uint32_t> client_wait_native_error;
    std::optional<std::uint32_t> project_serverinfo_protocol;
    std::optional<std::uint32_t> project_serverinfo_max_clients;
    std::optional<std::string> project_serverinfo_game;
    std::optional<std::string> project_serverinfo_map;
    std::optional<std::size_t> project_schema_count;
    std::optional<std::size_t> project_schema_field_count;
    std::optional<std::size_t> project_baseline_entity_count;
    std::optional<std::size_t> project_service_payload_count;
    std::optional<std::size_t> project_applied_runtime_record_count;
    std::optional<std::size_t> project_entity_count;
    std::optional<std::uint64_t> project_publication_revision;
    std::optional<std::uint64_t> project_canonical_state_hash;
    std::optional<std::uint64_t> project_stable_interval_ms;
    ProjectProtocolProgress project_protocol_progress;
    ProjectTransitionFailure project_transition_failure;
    ProjectResponsePayloadDiagnostic project_response_payload_diagnostic;
    std::optional<std::uint32_t> server_exit_code;
    std::optional<windows::HldsRuntimeProfileDiagnostic>
        server_profile_diagnostic;
    std::optional<windows::HldsLocalReadinessResult> server_readiness;
    windows::HldsRuntimeProfile::Id server_profile_id{
        windows::HldsRuntimeProfile::Id::legacy_stdio_hlds_banner_v1
};
};

[[nodiscard]] bool safe_relay_diagnostic_value(
    const std::string_view value) noexcept
{
    if (value.empty() || value.size() > 128U) return false;
    for (const unsigned char character : value) {
        if (!std::isalnum(character) && character != '-' && character != '_' &&
            character != '.' && character != ':' && character != '/' &&
            character != '+') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::string> relay_diagnostic_value(
    const std::string_view bytes,
    const std::string_view key)
{
    const std::string marker =
        "[stock-runtime-capture] " + std::string{key} + '=';
    std::optional<std::string> value;
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto newline = bytes.find('\n', offset);
        const auto end = newline == std::string_view::npos
            ? bytes.size() : newline;
        auto line = bytes.substr(offset, end - offset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
        if (line.starts_with(marker)) {
            const auto candidate = line.substr(marker.size());
            if (value || !safe_relay_diagnostic_value(candidate)) {
                return std::nullopt;
            }
            value = std::string{candidate};
        }
        if (newline == std::string_view::npos) break;
        offset = newline + 1U;
    }
    return value;
}

void retain_relay_terminal_diagnostic(
    ActiveSummary& summary,
    const windows::BoundedProcessLogSnapshot& snapshot)
{
    const auto retain = [&](const std::string_view key, std::string& target) {
        if (const auto value = relay_diagnostic_value(snapshot.bytes, key)) {
            target = *value;
        }
    };
    retain("relay-phase", summary.relay_phase);
    retain("failed-operation", summary.relay_failed_operation);
    retain("native-error-domain", summary.relay_native_error_domain);
    retain("native-error-code", summary.relay_native_error_code);
    retain("stop-requested", summary.relay_stop_observed);
    retain("journal-publication-state",
           summary.relay_journal_publication_state);
    retain("metadata-publication-state",
           summary.relay_metadata_publication_state);
}

[[nodiscard]] std::string relay_exit_code_hex(const std::uint32_t exit_code)
{
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setw(8)
           << std::setfill('0') << exit_code;
    return output.str();
}

void retain_relay_wait_result(
    ActiveSummary& summary,
    const windows::OwnedProcess::WaitResult& wait)
{
    using Status = windows::OwnedProcess::WaitStatus;
    switch (wait.status) {
    case Status::exited:
        summary.relay_wait_result = "exited";
        summary.relay_exit_code = wait.exit_code;
        break;
    case Status::timeout:
        summary.relay_wait_result = "timeout";
        break;
    case Status::wait_failed:
        summary.relay_wait_result = "wait-failed";
        if (wait.native_error) {
            summary.relay_native_error_domain = "Win32";
            summary.relay_native_error_code = std::to_string(*wait.native_error);
        }
        break;
    case Status::exit_query_failed:
        summary.relay_wait_result = "exit-query-failed";
        if (wait.native_error) {
            summary.relay_native_error_domain = "Win32";
            summary.relay_native_error_code = std::to_string(*wait.native_error);
        }
        break;
    case Status::invalid_process:
        summary.relay_wait_result = "invalid-process";
        if (wait.native_error) {
            summary.relay_native_error_domain = "Win32";
            summary.relay_native_error_code = std::to_string(*wait.native_error);
        }
        break;
    }
}

struct FunctionalClientLogObservation final {
    bool name_observed{false};
    bool connection_accepted{false};
    bool entered_game{false};
    bool connection_lost{false};
};

struct ProjectClientSignonObservation final {
    bool application_entry_observed{false};
    bool arguments_accepted{false};
    bool provider_begin_observed{false};
    bool steam_api_init_attempted{false};
    bool steam_api_initialized{false};
    bool fresh_material_acquired{false};
    bool connect_sent{false};
    bool connection_accepted{false};
    bool serverinfo_received{false};
    bool schema_registry_received{false};
    bool terminal_delta_schemas_ready{false};
    bool resource_continuation_sent{false};
    bool spawn_request_transmitted{false};
    bool spawn_request_acknowledged{false};
    bool signon_reply_transmitted{false};
    bool signon_reply_acknowledged{false};
    bool live_service_payloads_received{false};
    bool client_world_state_published{false};
    bool terminal_live_runtime_state_ready{false};
    bool terminal_live_usercmd_check_ready{false};
    bool live_usercmd_motion_verified{false};
    bool live_visual_verified{false};
    std::optional<ProjectClientLiveInput> live_visual_input;
    std::optional<std::string> jump_duck_result;
    std::optional<std::string> speed_result;
    std::optional<std::string> prediction_result;
    std::optional<bool> jump_observed;
    std::optional<bool> descent_observed;
    std::optional<bool> duck_observed;
    std::optional<bool> release_response_observed;
    std::optional<std::size_t> jump_new_submitted;
    std::optional<std::size_t> duck_new_submitted;
    bool authentication_failure{false};
    bool connection_rejected{false};
    bool timeout{false};
    std::optional<std::uint32_t> serverinfo_protocol;
    std::optional<std::uint32_t> serverinfo_max_clients;
    std::optional<std::string> serverinfo_game;
    std::optional<std::string> serverinfo_map;
    std::optional<std::size_t> schema_count;
    std::optional<std::size_t> schema_field_count;
    std::optional<std::size_t> baseline_entity_count;
    std::optional<std::size_t> service_payload_count;
    std::optional<std::size_t> applied_runtime_record_count;
    std::optional<std::size_t> entity_count;
    std::optional<std::uint64_t> publication_revision;
    std::optional<std::uint64_t> canonical_state_hash;
    std::optional<std::uint64_t> stable_interval_ms;
    bool usercmd_zero_observed{false};
    bool usercmd_transmitted{false};
    std::optional<std::size_t> generated_usercmd_count;
    std::optional<std::size_t> new_usercmd_count;
    std::optional<std::size_t> backup_usercmd_count;
    std::optional<std::size_t> transmitted_usercmd_packet_count;
    std::optional<std::size_t> server_sample_count;
    std::optional<std::size_t> rx_driver_updates_post_input;
    std::optional<std::size_t> rx_receive_polls_post_input;
    std::optional<std::size_t> rx_owning_datagrams_post_input;
    std::optional<std::size_t> rx_payloads_created_post_input;
    std::optional<std::size_t> rx_payloads_consumed_post_input;
    std::optional<std::size_t> rx_records_committed_post_input;
    std::optional<std::size_t> rx_clientdata_committed_post_input;
    std::optional<std::size_t> rx_samples_delivered_post_input;
    ProjectProtocolProgress protocol_progress;
    ProjectTransitionFailure transition_failure;
    ProjectResponsePayloadDiagnostic response_payload_diagnostic;

    [[nodiscard]] bool complete(
        const ProjectClientStop stop,
        const ProjectClientLiveInput selected_visual_input =
            ProjectClientLiveInput::scripted_check,
        const bool reference_prediction = false) const noexcept
    {
        const bool signon = steam_api_initialized && fresh_material_acquired &&
            connect_sent && connection_accepted && serverinfo_received &&
            schema_registry_received;
        if (stop == ProjectClientStop::delta_schemas) {
            return signon && terminal_delta_schemas_ready;
        }
        const bool live_network_handoff = signon && resource_continuation_sent &&
            spawn_request_transmitted && spawn_request_acknowledged &&
            signon_reply_transmitted && signon_reply_acknowledged &&
            live_service_payloads_received;
        const bool live_handoff = live_network_handoff &&
            client_world_state_published;
        if (stop == ProjectClientStop::live_runtime_state) {
            return live_handoff && terminal_live_runtime_state_ready &&
                usercmd_zero_observed && !usercmd_transmitted;
        }
        if (stop == ProjectClientStop::live_visual_control) {
            const bool jump_duck_selected = selected_visual_input ==
                ProjectClientLiveInput::scripted_jump_duck_check;
            const bool speed_selected = selected_visual_input ==
                ProjectClientLiveInput::scripted_speed_check;
            return live_network_handoff &&
                live_visual_input == selected_visual_input &&
                (selected_visual_input == ProjectClientLiveInput::keyboard_mouse ||
                 client_world_state_published) &&
                (!jump_duck_selected ||
                 (jump_duck_result == "verified" &&
                  jump_observed == true && descent_observed == true &&
                  duck_observed == true && release_response_observed == true &&
                  jump_new_submitted.value_or(0U) > 0U &&
                  duck_new_submitted.value_or(0U) > 0U)) &&
                (!speed_selected || reference_prediction ||
                 speed_result == "verified") &&
                (!reference_prediction || prediction_result ==
                    "live_local_prediction_and_reconciliation_verified") &&
                live_visual_verified && usercmd_transmitted &&
                !usercmd_zero_observed && new_usercmd_count.value_or(0U) > 0U &&
                transmitted_usercmd_packet_count.value_or(0U) > 0U &&
                server_sample_count.value_or(0U) > 0U;
        }
        return live_handoff && terminal_live_usercmd_check_ready &&
            live_usercmd_motion_verified && usercmd_transmitted &&
            !usercmd_zero_observed && new_usercmd_count.value_or(0U) > 0U &&
            transmitted_usercmd_packet_count.value_or(0U) > 0U &&
            server_sample_count.value_or(0U) > 0U;
    }
};

[[nodiscard]] ProjectClientSignonObservation
observe_project_client_signon_log(const std::string_view bytes)
{
    ProjectClientSignonObservation observation;
    observation.application_entry_observed =
        bytes.find("application_entry_observed=true") != std::string_view::npos;
    observation.arguments_accepted =
        bytes.find("arguments_accepted=true") != std::string_view::npos;
    observation.provider_begin_observed =
        bytes.find("provider_begin_observed=true") != std::string_view::npos;
    observation.steam_api_init_attempted =
        bytes.find("steam_api_init_attempted=true") != std::string_view::npos;
    observation.steam_api_initialized =
        bytes.find("steam_api_initialized=true") != std::string_view::npos;
    observation.fresh_material_acquired =
        bytes.find("fresh_material_acquired=true") != std::string_view::npos;
    observation.connect_sent =
        bytes.find("connect_sent=true") != std::string_view::npos;
    observation.connection_accepted =
        bytes.find("connection_accepted=true") != std::string_view::npos;
    observation.serverinfo_received =
        bytes.find("serverinfo_received=true") != std::string_view::npos;
    observation.schema_registry_received =
        bytes.find("schema_registry_received=true") != std::string_view::npos;
    observation.terminal_delta_schemas_ready =
        bytes.find("terminal_reason=delta_schemas_ready") !=
            std::string_view::npos;
    observation.resource_continuation_sent =
        bytes.find("resource_continuation_sent=true") !=
            std::string_view::npos;
    observation.spawn_request_transmitted =
        bytes.find("spawn_request_transmitted=true") !=
            std::string_view::npos;
    observation.spawn_request_acknowledged =
        bytes.find("spawn_request_acknowledged=true") !=
            std::string_view::npos;
    observation.signon_reply_transmitted =
        bytes.find("signon_reply=sendents transmitted=true") !=
            std::string_view::npos;
    observation.signon_reply_acknowledged =
        bytes.find("signon_reply=sendents acknowledged=true") !=
            std::string_view::npos;
    observation.live_service_payloads_received =
        bytes.find("live_service_payloads_received=true") !=
            std::string_view::npos;
    observation.client_world_state_published =
        bytes.find("client_world_state_published=true") !=
            std::string_view::npos;
    observation.terminal_live_runtime_state_ready =
        bytes.find("terminal_reason=live_runtime_state_ready") !=
            std::string_view::npos;
    observation.terminal_live_usercmd_check_ready =
        bytes.find("terminal_reason=live_usercmd_check_ready") !=
            std::string_view::npos;
    observation.live_usercmd_motion_verified =
        bytes.find(
            "result=fresh_project_client_usercmd_server_motion_verified") !=
            std::string_view::npos &&
        bytes.find("movement_verified=true") != std::string_view::npos;
    observation.usercmd_zero_observed =
        bytes.find("usercmd_transmitted=0") != std::string_view::npos;
    std::string lower;
    lower.reserve(bytes.size());
    for (const unsigned char character : bytes) {
        lower.push_back(static_cast<char>(std::tolower(character)));
    }
    const auto has_any = [&lower](
        const std::initializer_list<std::string_view> needles) {
        return std::ranges::any_of(needles, [&](const auto needle) {
            return lower.find(needle) != std::string::npos;
        });
    };
    observation.authentication_failure = has_any({
        "steam authentication failed", "failed to initialize authentication",
        "authentication operation timed out"});
    observation.connection_rejected = has_any({
        "goldsrc connection rejected", "connect-rejected"});
    observation.timeout = has_any({
        "connect-response wait timed out", "challenge wait timed out",
        "netchan bootstrap timed out", "sign-on timed out"});

    const auto token_after = [&bytes](const std::string_view marker,
                                      const std::size_t maximum) ->
        std::optional<std::string> {
        const auto found = bytes.rfind(marker);
        if (found == std::string_view::npos) return std::nullopt;
        const auto begin = found + marker.size();
        const auto end = bytes.find_first_of(" ,\r\n", begin);
        const auto value = bytes.substr(
            begin, (end == std::string_view::npos ? bytes.size() : end) - begin);
        if (value.empty() || value.size() > maximum ||
            !std::ranges::all_of(value, [](const unsigned char character) {
                return std::isalnum(character) || character == '_' ||
                    character == '-' || character == '.' || character == ':' ||
                    character == '/';
            })) {
            return std::nullopt;
        }
        return std::string{value};
    };
    const auto unsigned_after = [&token_after](const std::string_view marker) ->
        std::optional<std::uint32_t> {
        const auto token = token_after(marker, 10U);
        if (!token) return std::nullopt;
        std::uint32_t value = 0U;
        const auto parsed = std::from_chars(
            token->data(), token->data() + token->size(), value, 10);
        return parsed.ec == std::errc{} &&
                parsed.ptr == token->data() + token->size()
            ? std::optional<std::uint32_t>{value} : std::nullopt;
    };
    const auto uint64_after = [&token_after](const std::string_view marker) ->
        std::optional<std::uint64_t> {
        const auto token = token_after(marker, 20U);
        if (!token) return std::nullopt;
        std::uint64_t value = 0U;
        const auto parsed = std::from_chars(
            token->data(), token->data() + token->size(), value, 10);
        return parsed.ec == std::errc{} &&
                parsed.ptr == token->data() + token->size()
            ? std::optional<std::uint64_t>{value} : std::nullopt;
    };
    const auto progress_after = [&token_after](const std::string_view key) {
        const auto value = token_after(
            "[session-progress] " + std::string{key} + "=", 16U);
        if (value && (*value == "not_reached" || *value == "pending" ||
                      *value == "succeeded" || *value == "failed" ||
                      *value == "unknown")) {
            return *value;
        }
        return std::string{"not_reached"};
    };

    auto& progress = observation.protocol_progress;
    progress.steam_initialization = observation.steam_api_initialized
        ? "succeeded"
        : observation.steam_api_init_attempted
            ? observation.authentication_failure ? "failed" : "unknown"
            : "not_reached";
    progress.fresh_material = observation.fresh_material_acquired
        ? "succeeded"
        : observation.steam_api_initialized
            ? observation.authentication_failure ? "failed" : "unknown"
            : "not_reached";
    progress.connect_transmission = observation.connect_sent
        ? "succeeded"
        : observation.fresh_material_acquired ? "unknown" : "not_reached";
    progress.accept = observation.connection_accepted
        ? "succeeded"
        : observation.connection_rejected || observation.timeout
            ? "failed"
            : observation.connect_sent ? "unknown" : "not_reached";
    progress.serverinfo = progress_after("serverinfo");
    progress.schema_registry = progress_after("schema_registry");
    progress.movevars = progress_after("movevars");
    progress.user_info = progress_after("user_info");
    progress.sendres_queued = progress_after("sendres_queued");
    progress.sendres_transmitted = progress_after("sendres_transmitted");
    progress.sendres_acknowledged = progress_after("sendres_acknowledged");
    progress.resource_transition = progress_after("resource_transition");
    progress.resource_list = progress_after("resource_list");
    progress.resource_response_queued =
        progress_after("resource_response_queued");
    progress.resource_response_transmitted =
        progress_after("resource_response_transmitted");
    progress.resource_response_acknowledged =
        progress_after("resource_response_acknowledged");
    progress.resource_response = progress_after("resource_response");
    progress.spawn_queued = progress_after("spawn_queued");
    progress.spawn_transmitted = progress_after("spawn_transmitted");
    progress.spawn_acknowledged = progress_after("spawn_acknowledged");
    progress.baselines = progress_after("baselines");
    progress.runtime_publication = progress_after("runtime_publication");
    progress.operational_interval = progress_after("operational_interval");

    auto& transition = observation.transition_failure;
    transition.stage = token_after(
        "[transition-diagnostic] stage=", 48U);
    transition.present = transition.stage.has_value();
    if (transition.present) {
        transition.profile = token_after(" profile=", 64U);
        transition.expected_opcode = unsigned_after(" expected_opcode=");
        transition.actual_opcode = unsigned_after(" actual_opcode=");
        transition.cursor_byte_value = unsigned_after(" cursor_byte_value=");
        transition.cursor = token_after(" cursor=", 48U);
        transition.boundary = token_after(" boundary=", 64U);
        transition.payload_ordinal = uint64_after(" payload_ordinal=");
        transition.payload_ordinal_scope = token_after(
            " payload_ordinal_scope=", 96U);
        transition.direction = token_after(" direction=", 32U);
        transition.source_sequence = unsigned_after(" source_sequence=");
        transition.source_acknowledgement = unsigned_after(" source_ack=");
        transition.source_reliable = token_after(" source_reliable=", 16U);
        transition.reassembled = token_after(" reassembled=", 16U);
        transition.encoding = token_after(" encoding=", 32U);
        transition.wire_size = uint64_after(" wire_size=");
        transition.decoded_size = uint64_after(" decoded_size=");
        transition.pending_suffix_start = token_after(
            " pending_suffix_start=", 48U);
        transition.sendres_queued = token_after(" sendres_queued=", 16U);
        transition.sendres_transmitted = token_after(
            " sendres_transmitted=", 16U);
        transition.sendres_acknowledged = token_after(
            " sendres_acknowledged=", 16U);
        transition.request_reliable_generation = uint64_after(
            " request_reliable_generation=");
        transition.request_transmit_sequence = unsigned_after(
            " request_tx_sequence=");
        transition.request_acknowledgement_sequence = unsigned_after(
            " request_ack_sequence=");
        transition.last_category = token_after(" last_category=", 64U);
        transition.last_scope = token_after(" last_scope=", 64U);
        transition.last_cursor = token_after(" last_cursor=", 48U);
        transition.parser_error = token_after(" parser_error=", 64U);
        transition.primary_error = token_after(" primary_error=", 64U);
    }
    auto& response = observation.response_payload_diagnostic;
    constexpr std::string_view response_marker{
        "[resource-response-diagnostic] classification="};
    const auto response_begin = bytes.rfind(response_marker);
    const auto response_end = response_begin == std::string_view::npos
        ? std::string_view::npos
        : bytes.find_first_of("\r\n", response_begin);
    const auto response_line = response_begin == std::string_view::npos
        ? std::string_view{}
        : bytes.substr(
              response_begin,
              (response_end == std::string_view::npos
                   ? bytes.size() : response_end) - response_begin);
    const auto response_token_after = [&response_line](
        const std::string_view marker,
        const std::size_t maximum) -> std::optional<std::string> {
        const auto found = response_line.find(marker);
        if (found == std::string_view::npos) return std::nullopt;
        const auto begin = found + marker.size();
        const auto end = response_line.find_first_of(" ,", begin);
        const auto value = response_line.substr(
            begin,
            (end == std::string_view::npos ? response_line.size() : end) -
                begin);
        if (value.empty() || value.size() > maximum ||
            !std::ranges::all_of(value, [](const unsigned char character) {
                return std::isalnum(character) || character == '_' ||
                    character == '-' || character == '.' ||
                    character == ':' || character == '/';
            })) {
            return std::nullopt;
        }
        return std::string{value};
    };
    const auto response_uint64_after = [&response_token_after](
        const std::string_view marker) -> std::optional<std::uint64_t> {
        const auto token = response_token_after(marker, 20U);
        if (!token) return std::nullopt;
        std::uint64_t value = 0U;
        const auto parsed = std::from_chars(
            token->data(), token->data() + token->size(), value, 10);
        return parsed.ec == std::errc{} &&
                parsed.ptr == token->data() + token->size()
            ? std::optional<std::uint64_t>{value} : std::nullopt;
    };
    const auto response_unsigned_after = [&response_uint64_after](
        const std::string_view marker) -> std::optional<std::uint32_t> {
        const auto value = response_uint64_after(marker);
        return value && *value <= (std::numeric_limits<std::uint32_t>::max)()
            ? std::optional<std::uint32_t>{
                  static_cast<std::uint32_t>(*value)}
            : std::nullopt;
    };
    response.classification = response_token_after(response_marker, 64U);
    response.present = response.classification.has_value();
    if (response.present) {
        response.rx_position = response_token_after(" rx_position=", 64U);
        response.payload_ordinal = response_uint64_after(" payload_ordinal=");
        response.source_sequence = response_unsigned_after(" source_sequence=");
        response.source_acknowledgement = response_unsigned_after(" source_ack=");
        response.encoding = response_token_after(" encoding=", 32U);
        response.wire_size = response_uint64_after(" wire_size=");
        response.decoded_size = response_uint64_after(" decoded_size=");
        response.cursor = response_token_after(" cursor=", 48U);
        response.boundary = response_token_after(" boundary=", 64U);
        response.actual_opcode = response_unsigned_after(" actual_opcode=");
        response.response_queued = response_token_after(" response_queued=", 16U);
        response.response_transmitted = response_token_after(
            " response_transmitted=", 16U);
        response.response_acknowledged = response_token_after(
            " response_acknowledged=", 16U);
        response.reliable_generation = response_uint64_after(" generation=");
        response.first_transmit_sequence = response_unsigned_after(
            " first_tx_sequence=");
        response.controls_consumed = response_uint64_after(" controls_consumed=");
        response.pending_count = response_uint64_after(" pending_count=");
        response.pending_bytes = response_uint64_after(" pending_bytes=");
        response.last_handoff_cursor = response_uint64_after(
            " last_handoff_cursor=");
    }
    observation.serverinfo_protocol = unsigned_after(" protocol=");
    observation.serverinfo_max_clients = unsigned_after(" max-clients=");
    observation.serverinfo_game = token_after(" game=", 64U);
    observation.serverinfo_map = token_after(" map=", 128U);
    if (const auto schemas = unsigned_after("delta registry ready: schemas=")) {
        observation.schema_count = *schemas;
    }
    if (const auto fields = unsigned_after(", fields=")) {
        observation.schema_field_count = *fields;
    }
    observation.baseline_entity_count = unsigned_after("baseline_entities=");
    observation.service_payload_count = unsigned_after("service_payloads=");
    observation.applied_runtime_record_count = unsigned_after("applied_records=");
    observation.entity_count = unsigned_after(" entities=");
    observation.publication_revision = uint64_after("publication_revision=");
    observation.canonical_state_hash = uint64_after("canonical_hash=");
    observation.stable_interval_ms = uint64_after("stable_interval_ms=");
    constexpr std::string_view usercmd_counter_marker{
        "[live-usercmd] counters "};
    const auto usercmd_counter_begin = bytes.rfind(usercmd_counter_marker);
    const auto usercmd_counter_end = usercmd_counter_begin ==
            std::string_view::npos
        ? std::string_view::npos
        : bytes.find_first_of("\r\n", usercmd_counter_begin);
    const auto usercmd_counter_line = usercmd_counter_begin ==
            std::string_view::npos
        ? std::string_view{}
        : bytes.substr(
              usercmd_counter_begin,
              (usercmd_counter_end == std::string_view::npos
                   ? bytes.size() : usercmd_counter_end) -
                  usercmd_counter_begin);
    const auto usercmd_count_after = [&usercmd_counter_line](
        const std::string_view marker) -> std::optional<std::size_t> {
        const auto found = usercmd_counter_line.find(marker);
        if (found == std::string_view::npos) return std::nullopt;
        const auto begin = found + marker.size();
        const auto end = usercmd_counter_line.find_first_of(" ,", begin);
        const auto token = usercmd_counter_line.substr(
            begin,
            (end == std::string_view::npos ? usercmd_counter_line.size() : end) -
                begin);
        std::uint64_t value = 0U;
        const auto parsed = std::from_chars(
            token.data(), token.data() + token.size(), value, 10);
        if (token.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != token.data() + token.size() ||
            value > static_cast<std::uint64_t>(
                (std::numeric_limits<std::size_t>::max)())) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(value);
    };
    observation.generated_usercmd_count =
        usercmd_count_after("generated=");
    observation.new_usercmd_count = usercmd_count_after(" new=");
    observation.backup_usercmd_count = usercmd_count_after(" backup=");
    observation.transmitted_usercmd_packet_count =
        usercmd_count_after(" sent_packets=");
    observation.server_sample_count =
        usercmd_count_after(" server_samples=");
    observation.usercmd_transmitted =
        observation.new_usercmd_count.value_or(0U) > 0U &&
        observation.transmitted_usercmd_packet_count.value_or(0U) > 0U;
    constexpr std::string_view visual_marker{"live_visual_control result="};
    const auto visual_begin = bytes.rfind(visual_marker);
    const auto visual_end = visual_begin == std::string_view::npos
        ? std::string_view::npos
        : bytes.find_first_of("\r\n", visual_begin);
    const auto visual_line = visual_begin == std::string_view::npos
        ? std::string_view{}
        : bytes.substr(
              visual_begin,
              (visual_end == std::string_view::npos ? bytes.size() : visual_end) -
                  visual_begin);
    const auto visual_count_after = [&visual_line](
        const std::string_view marker) -> std::optional<std::size_t> {
        const auto found = visual_line.find(marker);
        if (found == std::string_view::npos) return std::nullopt;
        const auto begin = found + marker.size();
        const auto end = visual_line.find_first_of(" ,", begin);
        const auto token = visual_line.substr(
            begin,
            (end == std::string_view::npos ? visual_line.size() : end) - begin);
        std::uint64_t value = 0U;
        const auto parsed = std::from_chars(
            token.data(), token.data() + token.size(), value, 10);
        if (token.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != token.data() + token.size() ||
            value > static_cast<std::uint64_t>(
                (std::numeric_limits<std::size_t>::max)())) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(value);
    };
    if (!visual_line.empty()) {
        if (visual_line.find(" input=keyboard-mouse ") !=
            std::string_view::npos) {
            observation.live_visual_input = ProjectClientLiveInput::keyboard_mouse;
        } else if (visual_line.find(" input=scripted-side-check ") !=
                   std::string_view::npos) {
            observation.live_visual_input =
                ProjectClientLiveInput::scripted_side_check;
        } else if (visual_line.find(" input=scripted-jump-duck-check ") !=
                   std::string_view::npos) {
            observation.live_visual_input =
                ProjectClientLiveInput::scripted_jump_duck_check;
        } else if (visual_line.find(" input=scripted-speed-check ") !=
                   std::string_view::npos) {
            observation.live_visual_input =
                ProjectClientLiveInput::scripted_speed_check;
        } else if (visual_line.find(" input=scripted-check ") !=
                   std::string_view::npos) {
            observation.live_visual_input = ProjectClientLiveInput::scripted_check;
        }
        observation.client_world_state_published =
            observation.client_world_state_published ||
            visual_line.find(" client_world_state_published=true") !=
                std::string_view::npos ||
            visual_line.find(" client_world_state_published=1") !=
                std::string_view::npos;
        observation.live_usercmd_motion_verified =
            observation.live_usercmd_motion_verified ||
            visual_line.find(" movement_verified=true") !=
                std::string_view::npos ||
            visual_line.find(" movement_verified=1") !=
                std::string_view::npos;
        observation.live_visual_verified =
            (visual_line.find(
                "result=fresh_project_client_live_visual_control_integrated") !=
                std::string_view::npos ||
             visual_line.find(
                "result=fresh_project_client_jump_duck_server_verified") !=
                std::string_view::npos ||
             visual_line.find(
                "result=live_normal_speed_and_shift_walk_verified") !=
                std::string_view::npos ||
             visual_line.find(
                "result=live_local_prediction_and_reconciliation_verified") !=
                std::string_view::npos) &&
            visual_line.find(" view_status=ready") != std::string_view::npos &&
            visual_line.find(" gl_errors=0 cleanup=complete") !=
                std::string_view::npos &&
            visual_count_after(" world_draws=").value_or(0U) > 0U &&
            visual_count_after(" world_uploads=").value_or(0U) > 0U &&
            visual_count_after(" non_clear_framebuffers=").value_or(0U) > 0U;
        observation.generated_usercmd_count =
            visual_count_after(" generated=");
        observation.new_usercmd_count = visual_count_after(" new=");
        observation.backup_usercmd_count = visual_count_after(" backup=");
        observation.transmitted_usercmd_packet_count =
            visual_count_after(" sent_packets=");
        observation.server_sample_count =
            visual_count_after(" server_samples=");
        observation.usercmd_transmitted =
            observation.new_usercmd_count.value_or(0U) > 0U &&
            observation.transmitted_usercmd_packet_count.value_or(0U) > 0U;
    }
    constexpr std::string_view jump_marker{"live_jump_duck input="};
    const auto jump_begin = bytes.rfind(jump_marker);
    const auto jump_end = jump_begin == std::string_view::npos
        ? std::string_view::npos : bytes.find_first_of("\r\n", jump_begin);
    const auto jump_line = jump_begin == std::string_view::npos
        ? std::string_view{}
        : bytes.substr(jump_begin,
              (jump_end == std::string_view::npos ? bytes.size() : jump_end) -
                  jump_begin);
    const auto jump_value = [&jump_line](const std::string_view marker)
        -> std::optional<std::string_view> {
        const auto found = jump_line.find(marker);
        if (found == std::string_view::npos) return std::nullopt;
        const auto begin = found + marker.size();
        const auto end = jump_line.find_first_of(" \r\n", begin);
        if (begin == end) return std::nullopt;
        return jump_line.substr(begin,
            (end == std::string_view::npos ? jump_line.size() : end) - begin);
    };
    if (const auto value = jump_value(" result="); value &&
        (*value == "not_evaluated" || *value == "verified" ||
         *value == "jump_verified_duck_pending" ||
         *value == "buttons_transmitted_server_effect_unverified" ||
         *value == "observation_context_blocked" ||
         *value == "environment_limited"))
        observation.jump_duck_result = std::string{*value};
    const auto jump_boolean = [&jump_value](const std::string_view marker)
        -> std::optional<bool> {
        const auto value = jump_value(marker);
        if (value == "1" || value == "true") return true;
        if (value == "0" || value == "false") return false;
        return std::nullopt;
    };
    observation.jump_observed = jump_boolean(" jump_observed=");
    observation.descent_observed = jump_boolean(" descent_observed=");
    observation.duck_observed = jump_boolean(" duck_observed=");
    observation.release_response_observed =
        jump_boolean(" release_response_observed=");
    const auto jump_count = [&jump_value](const std::string_view marker)
        -> std::optional<std::size_t> {
        const auto value = jump_value(marker);
        if (!value) return std::nullopt;
        std::size_t count = 0U;
        const auto parsed = std::from_chars(value->data(),
            value->data() + value->size(), count, 10);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != value->data() + value->size()) return std::nullopt;
        return count;
    };
    observation.jump_new_submitted = jump_count(" jump_new_submitted=");
    observation.duck_new_submitted = jump_count(" duck_new_submitted=");
    constexpr std::string_view speed_marker{"live_speed input=scripted-speed-check result="};
    if (const auto found = bytes.rfind(speed_marker);
        found != std::string_view::npos) {
        const auto begin = found + speed_marker.size();
        const auto end = bytes.find_first_of(" \r\n", begin);
        const auto value = bytes.substr(begin, end - begin);
        if (value == "verified" || value == "server_motion_unverified" ||
            value == "observation_context_blocked" || value == "not_evaluated")
            observation.speed_result = std::string{value};
    }
    constexpr std::string_view prediction_marker{"live_prediction mode=reference result="};
    constexpr std::string_view rx_marker{"live_rx mode=reference "};
    if (const auto found = bytes.rfind(rx_marker);
        found != std::string_view::npos) {
        const auto end = bytes.find_first_of("\r\n", found);
        const auto line = bytes.substr(found,
            (end == std::string_view::npos ? bytes.size() : end) - found);
        const auto count = [line](const std::string_view key)
            -> std::optional<std::size_t> {
            const auto at = line.find(key);
            if (at == std::string_view::npos) return std::nullopt;
            const auto begin = at + key.size();
            const auto end = line.find(' ', begin);
            const auto token = line.substr(begin, end - begin);
            std::size_t value = 0U;
            const auto parsed = std::from_chars(
                token.data(), token.data() + token.size(), value, 10);
            return parsed.ec == std::errc{} &&
                    parsed.ptr == token.data() + token.size()
                ? std::optional<std::size_t>{value} : std::nullopt;
        };
        observation.rx_driver_updates_post_input = count(" driver_updates_post_input=");
        observation.rx_receive_polls_post_input = count(" receive_polls_post_input=");
        observation.rx_owning_datagrams_post_input = count(" owning_datagrams_post_input=");
        observation.rx_payloads_created_post_input = count(" payloads_created_post_input=");
        observation.rx_payloads_consumed_post_input = count(" payloads_consumed_post_input=");
        observation.rx_records_committed_post_input = count(" records_committed_post_input=");
        observation.rx_clientdata_committed_post_input = count(" clientdata_committed_post_input=");
        observation.rx_samples_delivered_post_input = count(" samples_delivered_post_input=");
    }
    if (const auto found = bytes.rfind(prediction_marker);
        found != std::string_view::npos) {
        const auto begin = found + prediction_marker.size();
        const auto end = bytes.find_first_of(" \r\n", begin);
        const auto value = bytes.substr(begin, end - begin);
        if (value == "live_local_prediction_and_reconciliation_verified" ||
            value == "live_prediction_active_accuracy_or_coverage_limited" ||
            value == "live_prediction_integrated_live_pending" ||
            value == "prediction_seed_or_anchor_contract_partial")
            observation.prediction_result = std::string{value};
    }
    return observation;
}

void apply_project_client_observation(
    ActiveSummary& summary,
    const ProjectClientSignonObservation& observation)
{
    summary.project_application_entry_observed =
        observation.application_entry_observed;
    summary.project_arguments_accepted = observation.arguments_accepted;
    summary.project_provider_begin_observed = observation.provider_begin_observed;
    summary.project_steam_api_init_attempted =
        observation.steam_api_init_attempted;
    summary.project_steam_api_initialized = observation.steam_api_initialized;
    summary.project_fresh_material_acquired = observation.fresh_material_acquired;
    summary.project_connect_sent = observation.connect_sent;
    summary.functional_connect_requested = observation.connect_sent;
    summary.project_connection_accepted = observation.connection_accepted;
    summary.project_serverinfo_received = observation.serverinfo_received;
    summary.project_schema_registry_received = observation.schema_registry_received;
    summary.project_resource_continuation_sent =
        observation.resource_continuation_sent;
    summary.project_spawn_request_transmitted =
        observation.spawn_request_transmitted;
    summary.project_spawn_request_acknowledged =
        observation.spawn_request_acknowledged;
    summary.project_signon_reply_transmitted =
        observation.signon_reply_transmitted;
    summary.project_signon_reply_acknowledged =
        observation.signon_reply_acknowledged;
    summary.project_live_service_payloads_received =
        observation.live_service_payloads_received;
    summary.project_client_world_state_published =
        observation.client_world_state_published;
    summary.project_usercmd_zero_observed = observation.usercmd_zero_observed;
    summary.project_usercmd_transmitted = observation.usercmd_transmitted;
    summary.project_usercmd_movement_verified =
        observation.live_usercmd_motion_verified;
    summary.project_live_visual_verified = observation.live_visual_verified;
    summary.project_jump_duck_result = observation.jump_duck_result;
    summary.project_speed_result = observation.speed_result;
    summary.project_prediction_result = observation.prediction_result;
    summary.project_jump_observed = observation.jump_observed;
    summary.project_descent_observed = observation.descent_observed;
    summary.project_duck_observed = observation.duck_observed;
    summary.project_release_response_observed =
        observation.release_response_observed;
    summary.project_jump_new_submitted = observation.jump_new_submitted;
    summary.project_duck_new_submitted = observation.duck_new_submitted;
    summary.project_usercmd_generated_count =
        observation.generated_usercmd_count;
    summary.project_usercmd_new_count = observation.new_usercmd_count;
    summary.project_usercmd_backup_count = observation.backup_usercmd_count;
    summary.project_usercmd_packet_count =
        observation.transmitted_usercmd_packet_count;
    summary.project_usercmd_server_sample_count =
        observation.server_sample_count;
    summary.project_rx_driver_updates_post_input = observation.rx_driver_updates_post_input;
    summary.project_rx_receive_polls_post_input = observation.rx_receive_polls_post_input;
    summary.project_rx_owning_datagrams_post_input = observation.rx_owning_datagrams_post_input;
    summary.project_rx_payloads_created_post_input = observation.rx_payloads_created_post_input;
    summary.project_rx_payloads_consumed_post_input = observation.rx_payloads_consumed_post_input;
    summary.project_rx_records_committed_post_input = observation.rx_records_committed_post_input;
    summary.project_rx_clientdata_committed_post_input = observation.rx_clientdata_committed_post_input;
    summary.project_rx_samples_delivered_post_input = observation.rx_samples_delivered_post_input;
    summary.functional_steam_authentication_error_observed =
        summary.functional_steam_authentication_error_observed ||
        observation.authentication_failure;
    summary.functional_connection_rejected =
        summary.functional_connection_rejected || observation.connection_rejected;
    summary.functional_connection_timeout_observed =
        summary.functional_connection_timeout_observed || observation.timeout;
    summary.project_serverinfo_protocol = observation.serverinfo_protocol;
    summary.project_serverinfo_max_clients = observation.serverinfo_max_clients;
    summary.project_serverinfo_game = observation.serverinfo_game;
    summary.project_serverinfo_map = observation.serverinfo_map;
    summary.project_schema_count = observation.schema_count;
    summary.project_schema_field_count = observation.schema_field_count;
    summary.project_baseline_entity_count = observation.baseline_entity_count;
    summary.project_service_payload_count = observation.service_payload_count;
    summary.project_applied_runtime_record_count =
        observation.applied_runtime_record_count;
    summary.project_entity_count = observation.entity_count;
    summary.project_publication_revision = observation.publication_revision;
    summary.project_canonical_state_hash = observation.canonical_state_hash;
    summary.project_stable_interval_ms = observation.stable_interval_ms;
    summary.project_protocol_progress = observation.protocol_progress;
    summary.project_transition_failure = observation.transition_failure;
    summary.project_response_payload_diagnostic =
        observation.response_payload_diagnostic;
}

// The first numeric descriptor field is the server-assigned userid used by
// the Valve game DLL's GETPLAYERUSERID logging path. It is not ENTINDEX and
// must not be promoted to an entity slot.
[[nodiscard]] std::optional<std::uint32_t> player_userid_from_identity(
    const std::string_view identity)
{
    if (identity.empty()) return std::nullopt;
    std::size_t cursor = identity.size();
    std::string_view userid;
    for (std::size_t group = 0U; group < 3U; ++group) {
        const auto close = identity.rfind('>', cursor - 1U);
        if (close == std::string_view::npos || close == 0U) {
            return std::nullopt;
        }
        const auto open = identity.rfind('<', close - 1U);
        if (open == std::string_view::npos) return std::nullopt;
        if (group == 2U) {
            userid = identity.substr(open + 1U, close - open - 1U);
        }
        cursor = open;
    }
    if (userid.empty() || userid.size() > 10U) return std::nullopt;
    std::uint64_t value = 0U;
    for (const unsigned char character : userid) {
        if (!std::isdigit(character)) return std::nullopt;
        value = value * 10U + static_cast<std::uint64_t>(character - '0');
        if (value > (std::numeric_limits<std::uint32_t>::max)()) {
            return std::nullopt;
        }
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::string ascii_lower(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result;
}

[[nodiscard]] FunctionalClientLogObservation
observe_functional_client_log(const std::string_view bytes)
{
    FunctionalClientLogObservation observation;
    observation.name_observed =
        bytes.find("HLC_SMOKE") != std::string_view::npos;
    std::optional<std::uint32_t> player_userid;
    std::size_t lifecycle_offset = 0U;
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto newline = bytes.find('\n', offset);
        const auto end = newline == std::string_view::npos
            ? bytes.size() : newline;
        const auto line = bytes.substr(offset, end - offset);
        constexpr std::string_view connected_event =
            " connected, address \"127.0.0.1:";
        const auto connected = line.find(connected_event);
        if (connected != std::string_view::npos && connected > 1U &&
            line[connected - 1U] == '"') {
            const auto identity_start = line.rfind('"', connected - 2U);
            if (identity_start != std::string_view::npos &&
                connected - identity_start <= 512U) {
                player_userid = player_userid_from_identity(line.substr(
                    identity_start, connected - identity_start));
                if (player_userid) {
                    observation.connection_accepted = true;
                    lifecycle_offset = newline == std::string_view::npos
                        ? bytes.size() : newline + 1U;
                    break;
                }
            }
        }
        if (newline == std::string_view::npos) break;
        offset = newline + 1U;
    }
    if (!player_userid) return observation;

    // Scan only the selected connection lifecycle within the owned server
    // instance's post-client-launch log suffix. Pre-connection/stale lines
    // cannot satisfy map entry, and a later connection cannot inherit the
    // first lifecycle's stability timer.
    offset = lifecycle_offset;
    while (offset < bytes.size()) {
        const auto newline = bytes.find('\n', offset);
        const auto end = newline == std::string_view::npos
            ? bytes.size() : newline;
        const auto line = bytes.substr(offset, end - offset);
        constexpr std::string_view connected_event =
            " connected, address \"127.0.0.1:";
        const auto later_connected = line.find(connected_event);
        if (later_connected != std::string_view::npos) {
            observation.connection_lost = true;
        }
        const auto entered = line.find(" entered the game");
        const auto rejected = ascii_lower(line);
        const bool possible_terminal =
            rejected.find("rejected") != std::string::npos ||
            rejected.find("kicked") != std::string::npos ||
            rejected.find("dropped") != std::string::npos ||
            rejected.find("disconnected") != std::string::npos;
        const auto event = entered != std::string_view::npos
            ? entered
            : possible_terminal ? line.size() : std::string_view::npos;
        if (event != std::string_view::npos && event > 1U) {
            const auto identity_end = line.rfind('"', event - 1U);
            const auto identity_start = identity_end == std::string_view::npos ||
                    identity_end == 0U
                ? std::string_view::npos
                : line.rfind('"', identity_end - 1U);
            const auto observed_userid = identity_start == std::string_view::npos
                ? std::optional<std::uint32_t>{}
                : player_userid_from_identity(line.substr(
                    identity_start, identity_end - identity_start + 1U));
            if (observed_userid != player_userid) {
                if (newline == std::string_view::npos) break;
                offset = newline + 1U;
                continue;
            }
            const auto suffix = line.substr(identity_end + 1U);
            if (suffix.find(" entered the game") != std::string_view::npos) {
                observation.entered_game = true;
            }
            const auto lower = ascii_lower(suffix);
            if (lower.find("rejected") != std::string::npos ||
                lower.find("kicked") != std::string::npos ||
                lower.find("dropped") != std::string::npos ||
                lower.find("disconnected") != std::string::npos) {
                observation.connection_lost = true;
            }
        }
        if (newline == std::string_view::npos) break;
        offset = newline + 1U;
    }
    return observation;
}

[[nodiscard]] bool contains_any(
    const std::string_view haystack,
    const std::initializer_list<std::string_view> needles)
{
    return std::ranges::any_of(needles, [&](const auto needle) {
        return haystack.find(needle) != std::string_view::npos;
    });
}

[[nodiscard]] std::string functional_diagnostic_excerpt(
    const std::string_view bytes)
{
    constexpr std::size_t maximum_lines = 64U;
    constexpr std::size_t maximum_line_bytes = 4U * 1'024U;
    constexpr std::size_t maximum_output_bytes = 16U * 1'024U;
    std::string output;
    std::size_t emitted_lines = 0U;
    const auto summary_view = [](std::string_view line) {
        constexpr std::string_view logger_prefix{"[info] "};
        if (line.starts_with(logger_prefix)) {
            line.remove_prefix(logger_prefix.size());
        }
        return line;
    };
    const auto high_priority_summary = [](const std::string_view line) {
        return line.starts_with("[live-usercmd] production_handoff=") ||
            line.starts_with("[live-usercmd] scenario ") ||
            line.starts_with("[live-usercmd] counters ") ||
            line.starts_with("[live-usercmd-phase] ") ||
            line.starts_with("[live-usercmd-phase-observation] ") ||
            line.starts_with("[live-usercmd] outcome=") ||
            line.starts_with("[live-usercmd] result=") ||
            line.starts_with("live_visual_timing ") ||
            line.starts_with("live_speed_timing ") ||
            line.starts_with("live_visual_scheduler ") ||
            line.starts_with("live_visual_phase ") ||
            line.starts_with("live_visual_server_sample ") ||
            line.starts_with("live_visual_framebuffer ") ||
            line.starts_with("live_jump_duck input=") ||
            line.starts_with("live_speed input=") ||
            line.starts_with("live_rx mode=") ||
            line.starts_with("live_prediction mode=") ||
            line.starts_with("live_visual_control result=");
    };
    const auto append_summary = [&](const std::string_view line) {
        const auto bounded = line.substr(
            0U, (std::min)(line.size(), maximum_line_bytes));
        if (emitted_lines >= maximum_lines ||
            output.size() + bounded.size() + 1U > maximum_output_bytes) {
            return;
        }
        output.append(bounded);
        output.push_back('\n');
        ++emitted_lines;
    };

    // Terminal F timing/phase/framebuffer summaries must survive a large
    // number of earlier camera/TX observations. Select those bounded lines
    // first, independent of the logger's optional "[info] " prefix.
    if (const auto prediction_at = bytes.rfind("live_prediction mode=");
        prediction_at != std::string_view::npos) {
        const auto end = bytes.find_first_of("\r\n", prediction_at);
        append_summary(bytes.substr(prediction_at,
            (end == std::string_view::npos ? bytes.size() : end) -
                prediction_at));
    }
    std::size_t priority_offset = 0U;
    while (priority_offset < bytes.size()) {
        const auto newline = bytes.find('\n', priority_offset);
        const auto end = newline == std::string_view::npos
            ? bytes.size() : newline;
        const auto line = bytes.substr(priority_offset, end - priority_offset);
        const auto summary_line = summary_view(line);
        if (high_priority_summary(summary_line) &&
            !summary_line.starts_with("live_prediction mode=")) {
            append_summary(summary_line);
        }
        if (newline == std::string_view::npos) break;
        priority_offset = newline + 1U;
    }

    std::size_t retained_camera_lines = 0U;
    std::size_t retained_tx_lines = 0U;
    std::size_t offset = 0U;
    while (offset < bytes.size() && emitted_lines < maximum_lines &&
           output.size() < maximum_output_bytes) {
        const auto newline = bytes.find('\n', offset);
        const auto end = newline == std::string_view::npos
            ? bytes.size() : newline;
        const auto line = bytes.substr(
            offset, (std::min)(end - offset, maximum_line_bytes));
        const auto summary_line = summary_view(line);
        const auto lower = ascii_lower(line);
        const bool camera_line =
            summary_line.starts_with("live_visual_camera_sample ");
        const bool tx_line = summary_line.starts_with("[live-usercmd-tx] ");
        const bool safe_live_usercmd_summary =
            !high_priority_summary(summary_line) &&
            ((camera_line && retained_camera_lines < 4U) ||
             (tx_line && retained_tx_lines < 4U));
        if (safe_live_usercmd_summary) {
            append_summary(summary_line);
            if (camera_line) ++retained_camera_lines;
            if (tx_line) ++retained_tx_lines;
        } else if (!high_priority_summary(summary_line) &&
                   contains_any(lower, {
                "error", "failed", "unable", "reject", "disconnect",
                "dropped", "kicked", "timeout", "connect", "entered the game",
                "steam", "map", "server logging"})) {
            if (lower.find("auth") != std::string::npos ||
                lower.find("ticket") != std::string::npos) {
                output += "[authentication-related diagnostic redacted]\n";
            } else {
                bool in_quoted_field = false;
                for (const unsigned char character : line) {
                    if (character == '"') {
                        if (!in_quoted_field) output += "<quoted-field-redacted>";
                        in_quoted_field = !in_quoted_field;
                        continue;
                    }
                    if (in_quoted_field) continue;
                    if (std::isdigit(character)) {
                        output.push_back('#');
                    } else if (character == '\t' || character == '\r') {
                        output.push_back(' ');
                    } else if (character >= 0x20U && character <= 0x7eU) {
                        output.push_back(static_cast<char>(character));
                    }
                }
                output.push_back('\n');
            }
            ++emitted_lines;
        }
        if (newline == std::string_view::npos) break;
        offset = newline + 1U;
    }
    if (output.size() > maximum_output_bytes) {
        output.resize(maximum_output_bytes);
    }
    return output;
}

[[nodiscard]] std::string_view functional_connection_status(
    const ActiveSummary& summary) noexcept
{
    if (summary.functional_connection_rejected) return "rejected";
    if (summary.functional_server_connection_accepted ||
        summary.project_connection_accepted) return "accepted";
    if (summary.functional_connection_timeout_observed) return "timeout";
    return "unknown";
}

[[nodiscard]] std::string_view functional_last_confirmed_stage(
    const ActiveSummary& summary) noexcept
{
    if (summary.project_live_visual_verified) {
        if (summary.project_prediction_result ==
            "live_local_prediction_and_reconciliation_verified")
            return "live_local_prediction_and_reconciliation_verified";
        if (summary.project_jump_duck_result == "verified")
            return "live_jump_duck_server_verified";
        if (summary.project_speed_result == "verified")
            return "live_normal_speed_and_shift_walk_verified";
        return "live_visual_control_verified";
    }
    if (summary.project_usercmd_movement_verified) {
        return "live_usercmd_server_motion_verified";
    }
    if (summary.project_client_world_state_published) {
        return "live_runtime_state_ready";
    }
    if (summary.project_live_service_payloads_received) {
        return "live_service_payloads_received";
    }
    if (summary.project_spawn_request_transmitted) {
        return "resource_continuation_sent";
    }
    if (summary.project_schema_registry_received) {
        return "delta_schemas_ready";
    }
    if (summary.project_serverinfo_received) return "serverinfo_received";
    if (summary.project_connection_accepted) return "server_accepted";
    if (summary.project_connect_sent) return "connect_sent";
    if (summary.project_fresh_material_acquired) {
        return "fresh_material_acquired";
    }
    if (summary.project_steam_api_initialized) {
        return "steam_api_initialized";
    }
    if (summary.client_ready && summary.stable_duration_ms >= 30'000U) {
        return "stable_session_completed";
    }
    if (summary.client_ready) return "client_entered_map";
    if (summary.functional_server_connection_accepted) return "server_accepted";
    if (summary.functional_connect_requested) return "connect_requested";
    if (summary.functional_client_process_created) {
        return "client_process_created";
    }
    if (summary.server_ready) return "server_ready";
    return "unknown";
}

[[nodiscard]] bool write_functional_diagnostics(
    const Options& options,
    const ActiveSummary& summary,
    const windows::BoundedProcessLogSnapshot& server_log,
    const windows::BoundedProcessLogSnapshot& client_log,
    const windows::BoundedProcessLogSnapshot& guard_log)
{
    std::error_code error;
    if (!fs::is_directory(options.run_root, error) || error) return false;
    const auto logs_root = options.run_root / L"logs";
    if (::CreateDirectoryW(logs_root.c_str(), nullptr) == FALSE &&
        ::GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    auto run_output = windows::open_secure_output_directory(options.run_root);
    auto logs_output = windows::open_secure_output_directory(logs_root);
    if (!run_output || !run_output.directory || !logs_output ||
        !logs_output.directory) {
        return false;
    }

    const bool logs_complete =
        windows::bounded_process_log_snapshot_complete(server_log) &&
        windows::bounded_process_log_snapshot_complete(client_log) &&
        windows::bounded_process_log_snapshot_complete(guard_log);
    bool supporting_files_written = true;
    const auto write_supporting = [&](const std::wstring_view leaf,
                                      const std::string& bytes) {
        if (!write_bounded_file(*logs_output.directory, leaf, bytes, 64U * 1'024U)) {
            supporting_files_written = false;
        }
    };
    write_supporting(L"server-diagnostic-redacted.log",
                     functional_diagnostic_excerpt(server_log.bytes));
    write_supporting(L"client-diagnostic-redacted.log",
                     functional_diagnostic_excerpt(client_log.bytes));
    write_supporting(L"guard-diagnostic-redacted.log",
                     functional_diagnostic_excerpt(guard_log.bytes));
    write_supporting(L"server-metadata.json", log_metadata_json(server_log));
    write_supporting(L"client-metadata.json", log_metadata_json(client_log));
    write_supporting(L"guard-metadata.json", log_metadata_json(guard_log));

    const auto stage = [](const bool observed) {
        return observed ? "observed" : "unknown";
    };
    const auto result_stage = [](const bool observed, const bool reached) {
        return observed ? "succeeded" : reached ? "failed" : "not_reached";
    };
    const bool project_mode = options.project_client_stock_signon;
    const bool live_project_mode = project_mode &&
        options.project_client_stop != ProjectClientStop::delta_schemas;
    const bool usercmd_project_mode = project_mode &&
        options.project_client_stop == ProjectClientStop::live_usercmd_check;
    const bool visual_project_mode = project_mode &&
        options.project_client_stop == ProjectClientStop::live_visual_control;
    const bool client_initialized =
        summary.functional_server_connection_accepted ||
        summary.project_steam_api_initialized || summary.client_ready;
    std::ostringstream functional;
    const auto write_optional_string = [&functional](
        const std::optional<std::string>& value) {
        if (value) functional << '"' << *value << '"';
        else functional << "null";
    };
    const auto write_optional_number = [&functional](const auto& value) {
        if (value) functional << *value;
        else functional << "null";
    };
    functional
        << "{\n"
        << "  \"schema\": \"hlclient.local-research-copy-smoke.v2\",\n"
        << "  \"mode\": \""
        << (visual_project_mode ? "project_client_live_visual_control_v1"
            : usercmd_project_mode ? "project_client_live_usercmd_check_v1"
            : live_project_mode ? "project_client_live_runtime_state_v1"
            : project_mode ? "project_client_stock_signon_v1"
                         : "local_research_copy_smoke_v1") << "\",\n"
        << "  \"purpose\": \""
        << (visual_project_mode
                ? "fresh_project_client_live_visual_control"
            : usercmd_project_mode
                ? "fresh_project_client_usercmd_server_motion"
            : live_project_mode ? "fresh_project_client_live_runtime_state"
            : project_mode ? "fresh_project_client_stock_signon"
                         : "functional_smoke") << "\",\n"
        << "  \"evidence_eligible\": "
        << (project_mode ? "true" : "false") << ",\n"
        << "  \"route\": \"direct_loopback\",\n"
        << "  \"game\": \"valve\",\n"
        << "  \"map\": \"" << options.map << "\",\n"
        << "  \"server_port\": " << options.server_port << ",\n"
        << "  \"server_launch_role\": \"research_root/hlds.exe\",\n"
        << "  \"server_working_directory_role\": \"research_root\",\n"
        << "  \"server_logging\": \"enabled_before_map\",\n"
        << "  \"client_launch_role\": \""
        << (project_mode ? "repository_build/hlclient.exe"
                         : "research_root/hl.exe") << "\",\n"
        << "  \"client_working_directory_role\": \""
        << (project_mode ? "repository_build_directory"
                         : "research_root") << "\",\n"
        << "  \"client_steam_argument\": \""
        << (project_mode ? "not_applicable" : "present") << "\",\n"
        << "  \"authentication_provider\": \""
        << (project_mode ? "steam_legacy_initiate_game_connection"
                         : "stock_client") << "\",\n"
        << "  \"client_connect_argument\": \"127.0.0.1:"
        << options.server_port << "\",\n"
        << "  \"actual_client_argv_profile\": \""
        << (visual_project_mode
                ? options.project_client_live_input ==
                          ProjectClientLiveInput::keyboard_mouse
                      ? "renderer=opengl;auth-provider=steam;stop-after=live-visual-control;live-input=keyboard-mouse;basedir=research-root;game=valve"
                      : options.project_client_live_input ==
                                ProjectClientLiveInput::scripted_jump_duck_check
                            ? "renderer=opengl;auth-provider=steam;stop-after=live-visual-control;live-input=scripted-jump-duck-check;basedir=research-root;game=valve"
                      : options.project_client_live_input ==
                                ProjectClientLiveInput::scripted_speed_check
                            ? "renderer=opengl;auth-provider=steam;stop-after=live-visual-control;live-input=scripted-speed-check;basedir=research-root;game=valve"
                      : options.project_client_live_input ==
                                ProjectClientLiveInput::scripted_side_check
                            ? "renderer=opengl;auth-provider=steam;stop-after=live-visual-control;live-input=scripted-side-check;basedir=research-root;game=valve"
                            : "renderer=opengl;auth-provider=steam;stop-after=live-visual-control;live-input=scripted-check;basedir=research-root;game=valve"
            : usercmd_project_mode
                ? "renderer=null;auth-provider=steam;stop-after=live-usercmd-check;resource-advertisement=empty"
            : live_project_mode
                ? "renderer=null;auth-provider=steam;stop-after=live-runtime-state;resource-advertisement=empty"
            : project_mode
                ? "renderer=null;auth-provider=steam;stop-after=delta-schemas"
                : "stock-steam-windowed-connect")
        << (options.project_client_reference_prediction
                ? ";prediction=reference" : "") << "\",\n"
        << "  \"server_process_created\": \""
        << stage(summary.functional_server_process_id != 0U) << "\",\n"
        << "  \"server_process_id\": "
        << summary.functional_server_process_id << ",\n"
        << "  \"server_ready\": "
        << (summary.server_ready ? "true" : "false") << ",\n"
        << "  \"client_process_created\": \""
        << stage(summary.functional_client_process_created) << "\",\n"
        << "  \"client_process_id\": "
        << summary.functional_client_process_id << ",\n"
        << "  \"image_identity_verified\": \""
        << result_stage(summary.functional_client_image_identity_verified,
                        summary.functional_client_process_created) << "\",\n"
        << "  \"resume_result\": \""
        << result_stage(summary.functional_client_resume_succeeded,
                        summary.functional_client_image_identity_verified)
        << "\",\n"
        << "  \"application_entry_observed\": \""
        << result_stage(summary.project_application_entry_observed,
                        summary.functional_client_resume_succeeded) << "\",\n"
        << "  \"arguments_accepted\": \""
        << result_stage(summary.project_arguments_accepted,
                        summary.project_application_entry_observed) << "\",\n"
        << "  \"provider_begin_observed\": \""
        << (summary.project_provider_begin_observed ? "succeeded" : "not_reached")
        << "\",\n"
        << "  \"steam_api_init_attempted\": \""
        << (summary.project_steam_api_init_attempted ? "succeeded" : "not_reached")
        << "\",\n"
        << "  \"client_initialized\": \""
        << stage(client_initialized) << "\",\n"
        << "  \"connect_requested\": \""
        << stage(summary.functional_connect_requested) << "\",\n"
        << "  \"connection_status\": \""
        << functional_connection_status(summary) << "\",\n"
        << "  \"client_map_entry\": \""
        << stage(summary.client_ready) << "\",\n"
        << "  \"map_entry_source\": \""
        << (summary.client_ready
                 ? visual_project_mode
                    ? "owned_project_client_live_visual_control"
                  : usercmd_project_mode
                    ? "owned_project_client_live_usercmd_check"
                  : live_project_mode
                    ? "owned_project_client_live_runtime_state"
                  : project_mode
                    ? "owned_project_client_serverinfo_and_delta_registry"
                    : "owned_server_log_correlated_connected_and_entered"
                : "unknown") << "\",\n"
        << "  \"stable_session\": \""
        << stage(summary.client_ready &&
                 summary.stable_duration_ms >= 30'000U) << "\",\n"
        << "  \"stable_duration_ms\": " << summary.stable_duration_ms
        << ",\n"
        << "  \"last_confirmed_stage\": \""
        << functional_last_confirmed_stage(summary) << "\",\n"
        << "  \"steam_authentication_error_observed\": "
        << (summary.functional_steam_authentication_error_observed
                ? "true" : "false") << ",\n"
        << "  \"steam_api_initialized\": "
        << (summary.project_steam_api_initialized ? "true" : "false")
        << ",\n  \"fresh_material_acquired\": "
        << (summary.project_fresh_material_acquired ? "true" : "false")
        << ",\n  \"connect_sent\": "
        << (summary.project_connect_sent ? "true" : "false")
        << ",\n  \"connection_accepted\": "
        << (summary.project_connection_accepted ? "true" : "false")
        << ",\n  \"serverinfo_received\": "
        << (summary.project_serverinfo_received ? "true" : "false")
        << ",\n  \"schema_registry_received\": "
        << (summary.project_schema_registry_received ? "true" : "false")
        << ",\n  \"resource_continuation_sent\": "
        << (summary.project_resource_continuation_sent ? "true" : "false")
        << ",\n  \"spawn_request_transmitted\": "
        << (summary.project_spawn_request_transmitted ? "true" : "false")
        << ",\n  \"spawn_request_acknowledged\": "
        << (summary.project_spawn_request_acknowledged ? "true" : "false")
        << ",\n  \"signon_reply_transmitted\": "
        << (summary.project_signon_reply_transmitted ? "true" : "false")
        << ",\n  \"signon_reply_acknowledged\": "
        << (summary.project_signon_reply_acknowledged ? "true" : "false")
        << ",\n  \"live_service_payloads_received\": "
        << (summary.project_live_service_payloads_received ? "true" : "false")
        << ",\n  \"client_world_state_published\": "
        << (summary.project_client_world_state_published ? "true" : "false")
        << ",\n  \"usercmd_transmitted\": "
        << (summary.project_usercmd_zero_observed
                ? "0" : summary.project_usercmd_transmitted ? "true" : "null")
        << ",\n  \"usercmd_movement_verified\": "
        << (summary.project_usercmd_movement_verified ? "true" : "false")
        << ",\n  \"live_visual_verified\": "
        << (summary.project_live_visual_verified ? "true" : "false")
        << ",\n  \"jump_duck_result\": "
        << (summary.project_jump_duck_result
                ? "\"" + *summary.project_jump_duck_result + "\""
                : "null")
        << ",\n  \"speed_result\": "
        << (summary.project_speed_result
                ? "\"" + *summary.project_speed_result + "\""
                : "null")
        << ",\n  \"prediction_result\": "
        << (summary.project_prediction_result
                ? "\"" + *summary.project_prediction_result + "\""
                : "null")
        << ",\n  \"jump_observed\": "
        << (summary.project_jump_observed
                ? (*summary.project_jump_observed ? "true" : "false")
                : "null")
        << ",\n  \"descent_observed\": "
        << (summary.project_descent_observed
                ? (*summary.project_descent_observed ? "true" : "false")
                : "null")
        << ",\n  \"duck_observed\": "
        << (summary.project_duck_observed
                ? (*summary.project_duck_observed ? "true" : "false")
                : "null")
        << ",\n  \"release_response_observed\": "
        << (summary.project_release_response_observed
                ? (*summary.project_release_response_observed ? "true" : "false")
                : "null")
        << ",\n  \"jump_new_submitted\": "
        << (summary.project_jump_new_submitted
                ? std::to_string(*summary.project_jump_new_submitted)
                : "null")
        << ",\n  \"duck_new_submitted\": "
        << (summary.project_duck_new_submitted
                ? std::to_string(*summary.project_duck_new_submitted)
                : "null")
        << ",\n  \"usercmd_generated\": "
        << (summary.project_usercmd_generated_count
                ? std::to_string(*summary.project_usercmd_generated_count)
                : "null")
        << ",\n  \"usercmd_new\": "
        << (summary.project_usercmd_new_count
                ? std::to_string(*summary.project_usercmd_new_count)
                : "null")
        << ",\n  \"usercmd_backup\": "
        << (summary.project_usercmd_backup_count
                ? std::to_string(*summary.project_usercmd_backup_count)
                : "null")
        << ",\n  \"usercmd_packets\": "
        << (summary.project_usercmd_packet_count
                ? std::to_string(*summary.project_usercmd_packet_count)
                : "null")
        << ",\n  \"usercmd_server_samples\": "
        << (summary.project_usercmd_server_sample_count
                ? std::to_string(*summary.project_usercmd_server_sample_count)
                : "null")
        << ",\n  \"rx_progress_post_input\": {\n"
        << "    \"driver_updates\": "
        << (summary.project_rx_driver_updates_post_input ? std::to_string(*summary.project_rx_driver_updates_post_input) : "null")
        << ",\n    \"receive_polls\": "
        << (summary.project_rx_receive_polls_post_input ? std::to_string(*summary.project_rx_receive_polls_post_input) : "null")
        << ",\n    \"owning_datagrams\": "
        << (summary.project_rx_owning_datagrams_post_input ? std::to_string(*summary.project_rx_owning_datagrams_post_input) : "null")
        << ",\n    \"payloads_created\": "
        << (summary.project_rx_payloads_created_post_input ? std::to_string(*summary.project_rx_payloads_created_post_input) : "null")
        << ",\n    \"payloads_consumed\": "
        << (summary.project_rx_payloads_consumed_post_input ? std::to_string(*summary.project_rx_payloads_consumed_post_input) : "null")
        << ",\n    \"records_committed\": "
        << (summary.project_rx_records_committed_post_input ? std::to_string(*summary.project_rx_records_committed_post_input) : "null")
        << ",\n    \"clientdata_committed\": "
        << (summary.project_rx_clientdata_committed_post_input ? std::to_string(*summary.project_rx_clientdata_committed_post_input) : "null")
        << ",\n    \"samples_delivered\": "
        << (summary.project_rx_samples_delivered_post_input ? std::to_string(*summary.project_rx_samples_delivered_post_input) : "null")
        << "\n  }"
        << ",\n  \"authentication_status\": \""
        << (summary.functional_steam_authentication_error_observed
                ? "failed" : summary.project_connection_accepted
                    ? "pending_or_unknown" : "not_reached") << "\",\n";
    const auto& progress = summary.project_protocol_progress;
    functional
        << "  \"protocol_progress\": {\n"
        << "    \"steam_initialization\": \"" << progress.steam_initialization << "\",\n"
        << "    \"fresh_material\": \"" << progress.fresh_material << "\",\n"
        << "    \"connect_transmission\": \"" << progress.connect_transmission << "\",\n"
        << "    \"accept\": \"" << progress.accept << "\",\n"
        << "    \"serverinfo\": \"" << progress.serverinfo << "\",\n"
        << "    \"schema_registry\": \"" << progress.schema_registry << "\",\n"
        << "    \"movevars\": \"" << progress.movevars << "\",\n"
        << "    \"user_info\": \"" << progress.user_info << "\",\n"
        << "    \"sendres_queued\": \"" << progress.sendres_queued << "\",\n"
        << "    \"sendres_transmitted\": \"" << progress.sendres_transmitted << "\",\n"
        << "    \"sendres_acknowledged\": \"" << progress.sendres_acknowledged << "\",\n"
        << "    \"resource_transition\": \"" << progress.resource_transition << "\",\n"
        << "    \"resource_list\": \"" << progress.resource_list << "\",\n"
        << "    \"resource_response_queued\": \"" << progress.resource_response_queued << "\",\n"
        << "    \"resource_response_transmitted\": \"" << progress.resource_response_transmitted << "\",\n"
        << "    \"resource_response_acknowledged\": \"" << progress.resource_response_acknowledged << "\",\n"
        << "    \"resource_response\": \"" << progress.resource_response << "\",\n"
        << "    \"spawn_queued\": \"" << progress.spawn_queued << "\",\n"
        << "    \"spawn_transmitted\": \"" << progress.spawn_transmitted << "\",\n"
        << "    \"spawn_acknowledged\": \"" << progress.spawn_acknowledged << "\",\n"
        << "    \"baselines\": \"" << progress.baselines << "\",\n"
        << "    \"runtime_publication\": \"" << progress.runtime_publication << "\",\n"
        << "    \"operational_interval\": \"" << progress.operational_interval << "\"\n"
        << "  },\n";
    const auto& transition = summary.project_transition_failure;
    functional << "  \"transition_failure\": {\n"
               << "    \"present\": "
               << (transition.present ? "true" : "false") << ",\n"
               << "    \"stage\": ";
    write_optional_string(transition.stage);
    functional << ",\n    \"profile\": ";
    write_optional_string(transition.profile);
    functional << ",\n    \"expected_opcode\": ";
    write_optional_number(transition.expected_opcode);
    functional << ",\n    \"actual_opcode\": ";
    write_optional_number(transition.actual_opcode);
    functional << ",\n    \"cursor_byte_value\": ";
    write_optional_number(transition.cursor_byte_value);
    functional << ",\n    \"cursor\": ";
    write_optional_string(transition.cursor);
    functional << ",\n    \"boundary\": ";
    write_optional_string(transition.boundary);
    functional << ",\n    \"payload_ordinal\": ";
    write_optional_number(transition.payload_ordinal);
    functional << ",\n    \"payload_ordinal_scope\": ";
    write_optional_string(transition.payload_ordinal_scope);
    functional << ",\n    \"direction\": ";
    write_optional_string(transition.direction);
    functional << ",\n    \"source_sequence\": ";
    write_optional_number(transition.source_sequence);
    functional << ",\n    \"source_acknowledgement\": ";
    write_optional_number(transition.source_acknowledgement);
    functional << ",\n    \"source_reliable\": ";
    write_optional_string(transition.source_reliable);
    functional << ",\n    \"reassembled\": ";
    write_optional_string(transition.reassembled);
    functional << ",\n    \"encoding\": ";
    write_optional_string(transition.encoding);
    functional << ",\n    \"wire_size\": ";
    write_optional_number(transition.wire_size);
    functional << ",\n    \"decoded_size\": ";
    write_optional_number(transition.decoded_size);
    functional << ",\n    \"pending_suffix_start\": ";
    write_optional_string(transition.pending_suffix_start);
    functional << ",\n    \"sendres_queued\": ";
    write_optional_string(transition.sendres_queued);
    functional << ",\n    \"sendres_transmitted\": ";
    write_optional_string(transition.sendres_transmitted);
    functional << ",\n    \"sendres_acknowledged\": ";
    write_optional_string(transition.sendres_acknowledged);
    functional << ",\n    \"request_reliable_generation\": ";
    write_optional_number(transition.request_reliable_generation);
    functional << ",\n    \"request_transmit_sequence\": ";
    write_optional_number(transition.request_transmit_sequence);
    functional << ",\n    \"request_acknowledgement_sequence\": ";
    write_optional_number(transition.request_acknowledgement_sequence);
    functional << ",\n    \"last_category\": ";
    write_optional_string(transition.last_category);
    functional << ",\n    \"last_scope\": ";
    write_optional_string(transition.last_scope);
    functional << ",\n    \"last_cursor\": ";
    write_optional_string(transition.last_cursor);
    functional << ",\n    \"parser_error\": ";
    write_optional_string(transition.parser_error);
    functional << ",\n    \"primary_error\": ";
    write_optional_string(transition.primary_error);
    functional << "\n  },\n";
    const auto& response = summary.project_response_payload_diagnostic;
    functional << "  \"response_payload_diagnostic\": {\n"
               << "    \"present\": "
               << (response.present ? "true" : "false") << ",\n"
               << "    \"classification\": ";
    write_optional_string(response.classification);
    functional << ",\n    \"rx_position\": ";
    write_optional_string(response.rx_position);
    functional << ",\n    \"payload_ordinal\": ";
    write_optional_number(response.payload_ordinal);
    functional << ",\n    \"source_sequence\": ";
    write_optional_number(response.source_sequence);
    functional << ",\n    \"source_acknowledgement\": ";
    write_optional_number(response.source_acknowledgement);
    functional << ",\n    \"encoding\": ";
    write_optional_string(response.encoding);
    functional << ",\n    \"wire_size\": ";
    write_optional_number(response.wire_size);
    functional << ",\n    \"decoded_size\": ";
    write_optional_number(response.decoded_size);
    functional << ",\n    \"cursor\": ";
    write_optional_string(response.cursor);
    functional << ",\n    \"boundary\": ";
    write_optional_string(response.boundary);
    functional << ",\n    \"actual_opcode\": ";
    write_optional_number(response.actual_opcode);
    functional << ",\n    \"response_queued\": ";
    write_optional_string(response.response_queued);
    functional << ",\n    \"response_transmitted\": ";
    write_optional_string(response.response_transmitted);
    functional << ",\n    \"response_acknowledged\": ";
    write_optional_string(response.response_acknowledged);
    functional << ",\n    \"reliable_generation\": ";
    write_optional_number(response.reliable_generation);
    functional << ",\n    \"first_transmit_sequence\": ";
    write_optional_number(response.first_transmit_sequence);
    functional << ",\n    \"controls_consumed\": ";
    write_optional_number(response.controls_consumed);
    functional << ",\n    \"pending_count\": ";
    write_optional_number(response.pending_count);
    functional << ",\n    \"pending_bytes\": ";
    write_optional_number(response.pending_bytes);
    functional << ",\n    \"last_handoff_cursor\": ";
    write_optional_number(response.last_handoff_cursor);
    functional << "\n  },\n"
        << "  \"client_exit_status\": \""
        << (summary.client_exit_code ? "observed"
            : summary.functional_client_process_created && summary.cleanup_exact
                ? "owned_cleanup_without_individual_exit_code" : "unknown")
        << "\",\n"
        << "  \"client_exit_code\": ";
    if (summary.client_exit_code) functional << *summary.client_exit_code;
    else functional << "null";
    functional
        << ",\n  \"client_exit_code_hex\": ";
    if (summary.client_exit_code) {
        functional << "\"" << relay_exit_code_hex(*summary.client_exit_code)
                   << "\"";
    } else {
        functional << "null";
    }
    functional
        << ",\n  \"child_wait_result\": \""
        << summary.client_wait_result << "\",\n"
        << "  \"child_wait_native_error\": ";
    if (summary.client_wait_native_error) {
        functional << *summary.client_wait_native_error;
    } else {
        functional << "null";
    }
    functional
        << ",\n  \"serverinfo_protocol\": ";
    if (summary.project_serverinfo_protocol) {
        functional << *summary.project_serverinfo_protocol;
    } else {
        functional << "null";
    }
    functional << ",\n  \"serverinfo_max_clients\": ";
    if (summary.project_serverinfo_max_clients) {
        functional << *summary.project_serverinfo_max_clients;
    } else {
        functional << "null";
    }
    functional << ",\n  \"serverinfo_game\": ";
    if (summary.project_serverinfo_game) {
        functional << "\"" << *summary.project_serverinfo_game << "\"";
    } else {
        functional << "null";
    }
    functional << ",\n  \"serverinfo_map\": ";
    if (summary.project_serverinfo_map) {
        functional << "\"" << *summary.project_serverinfo_map << "\"";
    } else {
        functional << "null";
    }
    functional << ",\n  \"schema_count\": ";
    if (summary.project_schema_count) functional << *summary.project_schema_count;
    else functional << "null";
    functional << ",\n  \"schema_field_count\": ";
    if (summary.project_schema_field_count) {
        functional << *summary.project_schema_field_count;
    } else {
        functional << "null";
    }
    functional << ",\n  \"baseline_entity_count\": ";
    if (summary.project_baseline_entity_count) {
        functional << *summary.project_baseline_entity_count;
    } else functional << "null";
    functional << ",\n  \"service_payload_count\": ";
    if (summary.project_service_payload_count) {
        functional << *summary.project_service_payload_count;
    } else functional << "null";
    functional << ",\n  \"applied_runtime_record_count\": ";
    if (summary.project_applied_runtime_record_count) {
        functional << *summary.project_applied_runtime_record_count;
    } else functional << "null";
    functional << ",\n  \"world_entity_count\": ";
    if (summary.project_entity_count) functional << *summary.project_entity_count;
    else functional << "null";
    functional << ",\n  \"publication_revision\": ";
    if (summary.project_publication_revision) {
        functional << *summary.project_publication_revision;
    } else functional << "null";
    functional << ",\n  \"canonical_state_hash\": ";
    if (summary.project_canonical_state_hash) {
        functional << *summary.project_canonical_state_hash;
    } else functional << "null";
    functional << ",\n  \"stable_runtime_interval_ms\": ";
    if (summary.project_stable_interval_ms) {
        functional << *summary.project_stable_interval_ms;
    } else functional << "null";
    functional
        << ",\n  \"server_exit_status\": \""
        << (summary.server_exit_code ? "observed"
            : summary.functional_server_process_id != 0U && summary.cleanup_exact
                ? "owned_cleanup_without_individual_exit_code" : "unknown")
        << "\",\n"
        << "  \"server_exit_code\": ";
    if (summary.server_exit_code) functional << *summary.server_exit_code;
    else functional << "null";
    functional
        << ",\n  \"primary_failure\": \""
        << (summary.project_transition_failure.primary_error
                ? *summary.project_transition_failure.primary_error
                : summary.failure)
        << "\",\n"
        << "  \"owned_process_cleanup\": \""
        << (summary.cleanup_exact ? "exact" : "incomplete") << "\",\n"
        << "  \"bounded_log_capture\": \""
        << (logs_complete ? "complete" : "incomplete") << "\",\n"
        << "  \"supporting_diagnostics\": \""
        << (supporting_files_written ? "complete" : "incomplete") << "\",\n"
        << "  \"restoration_status\": \"wrapper_pending\",\n"
        << "  \"publication_status\": \"staged_after_process_cleanup\"\n"
        << "}\n";
    const bool summary_written = write_bounded_file(
        *run_output.directory, L"functional-smoke.staged.json",
        functional.str(), 128U * 1'024U);
    return summary_written && supporting_files_written && logs_complete;
}

class OwnedJobExitBarrier final {
public:
    OwnedJobExitBarrier(
        windows::KillOnCloseProcessJob& campaign_job,
        windows::KillOnCloseProcessJob& guard_job,
        UniqueHandle& heartbeat_write,
        HANDLE isolation_release,
        HANDLE writer_trace_stock_stopped,
        bool& writer_trace_stock_processes_stopped,
        std::optional<windows::OwnedJobCleanupResult>& campaign_result,
        std::optional<windows::OwnedJobCleanupResult>& guard_result) noexcept
        : campaign_job_{campaign_job}, guard_job_{guard_job},
          heartbeat_write_{heartbeat_write}, isolation_release_{isolation_release},
          writer_trace_stock_stopped_{writer_trace_stock_stopped},
          writer_trace_stock_processes_stopped_{
              writer_trace_stock_processes_stopped},
          campaign_result_{campaign_result}, guard_result_{guard_result}
    {
    }
    ~OwnedJobExitBarrier()
    {
        campaign_result_ = campaign_job_.terminate_and_wait(
            120U, std::chrono::seconds{10});
        if (!*campaign_result_) return;
        if (writer_trace_stock_stopped_ != INVALID_HANDLE_VALUE) {
            const auto stopped =
                windows::signal_writer_trace_stock_processes_stopped(
                    writer_trace_stock_stopped_);
            if (!stopped) return;
            writer_trace_stock_processes_stopped_ = true;
        }
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
    HANDLE writer_trace_stock_stopped_{INVALID_HANDLE_VALUE};
    bool& writer_trace_stock_processes_stopped_;
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
    summary.server_profile_id = options.server_profile_id;
    const bool functional_runtime_capture =
        options.output_role ==
        goldsrc::StockRuntimeCaptureOutputRole::functional_runtime_capture;
    windows::StockRuntimeStartupState startup;
    const auto finalize_duration = [&]() {
        summary.duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_at).count());
    };
    const auto elapsed_ms = [&]() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_at).count());
    };
    if (!validate_new_run_root(
            options.run_root, options.output_role, options.functional_smoke)) {
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
    auto guard_log = windows::BoundedProcessLogCapture::create(
        {64U * 1'024U, 1'024U, 1'024U});
    auto functional_server_log = options.functional_smoke
        ? windows::BoundedProcessLogCapture::create({})
        : std::optional<windows::BoundedProcessLogCapture>{};
    auto functional_client_log = options.functional_smoke
        ? windows::BoundedProcessLogCapture::create({})
        : std::optional<windows::BoundedProcessLogCapture>{};
    // This outer-scope owner outlives every lambda-local exit barrier and the
    // final retry boundary below. It cannot be destroyed by an early return
    // while either owned Job still lacks exact zero-process accounting.
    windows::DynamicNetworkIsolationSession redundant_isolation;
    const auto execute_owned = [&]() -> ActiveSummary {
    UniqueHandle readiness_read;
    UniqueHandle readiness_write;
    UniqueHandle heartbeat_read;
    UniqueHandle heartbeat_write;
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
        options.writer_trace_stock_stopped_handle,
        summary.writer_trace_stock_processes_stopped,
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

    if (options.functional_smoke) {
        auto& server_log = functional_server_log;
        auto& client_log = functional_client_log;
        if (!server_log || !client_log) {
            summary.failure = "functional-log-capture-failed";
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
            L"+log", L"on", L"+map", to_wide_ascii(options.map),
            L"+maxplayers", L"8", L"+sv_lan", L"1", L"+status"};
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
        summary.functional_server_process_id = server.process_id();
        const std::array<std::uint32_t, 1U> expected_server{
            server.process_id()};
        server_log->close_parent_write_handle();
        if (!exact_process_snapshot_matches(
                environment.server, expected_server, summary.failure)) {
            finalize_duration();
            return summary;
        }

        windows::HldsBannerParseResult server_profile;
        const auto banner_deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds{15};
        while (std::chrono::steady_clock::now() < banner_deadline &&
               server.running() && guard.running()) {
            server_profile = windows::diagnose_observed_hlds_runtime_banner(
                server_log->snapshot());
            if (server_profile ||
                server_profile.code == windows::HldsBannerParseErrorCode::
                    profile_mismatch ||
                server_profile.code == windows::HldsBannerParseErrorCode::
                    malformed ||
                server_profile.code == windows::HldsBannerParseErrorCode::
                    duplicate_field ||
                server_profile.code == windows::HldsBannerParseErrorCode::
                    process_log_truncated) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        summary.server_profile_diagnostic = server_profile.diagnostic;
        if (!server_profile) {
            summary.failure = !server.running()
                ? "server-early-exit"
                : "server-profile-" +
                      std::string{windows::to_string(server_profile.code)};
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        summary.server_readiness = windows::observe_hlds_local_readiness(
            server, environment.server, options.server_port,
            options.relay_port, options.map,
            std::chrono::steady_clock::now() + std::chrono::seconds{10},
            [&]() { return guard.running(); });
        summary.server_ready = static_cast<bool>(*summary.server_readiness);
        if (!summary.server_ready) {
            summary.failure = "server-readiness-" + std::string{
                windows::to_string(summary.server_readiness->status)};
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }

        const auto server_log_offset_before_client_launch =
            server_log->snapshot().bytes.size();
        windows::OwnedProcessLaunchSpec client_spec;
        client_spec.executable = environment.client.canonical_path;
        client_spec.working_directory = options.project_client_stock_signon
            ? environment.client.canonical_path.parent_path()
            : options.research_root;
        client_spec.expected_identity = environment.client;
        client_spec.stdout_handle = client_log->inherited_write_handle();
        client_spec.stderr_handle = client_log->inherited_write_handle();
        // Keep the project client on the same constrained inherited console
        // path as every other child. CREATE_NO_WINDOW is incompatible with
        // PROCESS_CREATION_CHILD_PROCESS_RESTRICTED on affected Windows
        // builds and can terminate the image in loader startup (0xC0000142)
        // before application diagnostics become available.
        client_spec.create_no_window = false;
        if (options.project_client_stock_signon) {
            client_spec.arguments = {
                L"--renderer",
                options.project_client_stop ==
                        ProjectClientStop::live_visual_control
                    ? L"opengl"
                    : L"null",
                L"--connect",
                L"127.0.0.1:" + std::to_wstring(options.server_port),
                L"--stop-after",
                std::wstring{project_client_stop_argument(
                    options.project_client_stop)}, L"--auth-provider",
                L"steam", L"--steam-api-runtime",
                options.steam_api_runtime.wstring(), L"--net-trace", L"--name",
                 options.project_client_stop ==
                         ProjectClientStop::live_visual_control
                     ? options.project_client_reference_prediction
                           ? L"HLC_M472H2"
                       : options.project_client_live_input ==
                               ProjectClientLiveInput::scripted_jump_duck_check
                           ? L"HLC_M472G"
                           : options.project_client_live_input ==
                               ProjectClientLiveInput::scripted_speed_check
                             ? L"HLC_M472H1" : L"HLC_M472F"
                 : options.project_client_stop == ProjectClientStop::live_usercmd_check
                     ? L"HLC_M472E"
                 : options.project_client_stop ==
                         ProjectClientStop::live_runtime_state
                     ? L"HLC_M472D" : L"HLC_M472C"};
            if (options.project_client_stop ==
                ProjectClientStop::live_visual_control) {
                if (options.project_client_reference_prediction)
                    client_spec.arguments.insert(client_spec.arguments.end(),
                        {L"--prediction", L"reference"});
                client_spec.arguments.insert(
                    client_spec.arguments.end(),
                    {L"--live-input",
                     options.project_client_live_input ==
                             ProjectClientLiveInput::keyboard_mouse
                         ? L"keyboard-mouse"
                         : options.project_client_live_input ==
                                   ProjectClientLiveInput::scripted_jump_duck_check
                               ? L"scripted-jump-duck-check"
                         : options.project_client_live_input ==
                                   ProjectClientLiveInput::scripted_speed_check
                               ? L"scripted-speed-check"
                         : options.project_client_live_input ==
                                   ProjectClientLiveInput::scripted_side_check
                               ? L"scripted-side-check"
                               : L"scripted-check",
                     L"--basedir", options.research_root.wstring(), L"--game",
                     L"valve"});
                if (options.project_client_live_input ==
                    ProjectClientLiveInput::keyboard_mouse) {
                    const auto client_duration =
                        (std::min)(
                            options.maximum_duration_seconds > 10U
                                ? options.maximum_duration_seconds - 10U
                                : 1U,
                            45U);
                    client_spec.arguments.insert(
                        client_spec.arguments.end(),
                        {L"--live-session-seconds",
                         std::to_wstring(client_duration)});
                }
            }
        } else {
            client_spec.arguments = {
                L"-steam", L"-game", L"valve", L"-windowed", L"-w", L"800",
                L"-h", L"600", L"+name", L"HLC_SMOKE", L"+connect",
                L"127.0.0.1:" + std::to_wstring(options.server_port), L"-nojoy"};
        }
        auto [client, client_result] = campaign_job.launch(client_spec);
        summary.functional_client_process_created =
            client_result.process_id != 0U;
        summary.functional_client_process_id = client_result.process_id;
        summary.functional_client_image_identity_verified =
            client_result.code == windows::OwnedProcessErrorCode::none ||
            client_result.code == windows::OwnedProcessErrorCode::resume_failed;
        summary.functional_client_resume_succeeded =
            client_result.code == windows::OwnedProcessErrorCode::none;
        if (!client_result) {
            summary.failure = "client-" +
                std::string{windows::to_string(client_result.code)};
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        ++summary.processes_started;
        summary.functional_connect_requested =
            !options.project_client_stock_signon;
        const std::array<std::uint32_t, 1U> expected_client{
            client.process_id()};
        client_log->close_parent_write_handle();
        if ((!options.project_client_stock_signon &&
             !exact_process_snapshot_matches(
                 environment.client, expected_client, summary.failure)) ||
            !exact_process_snapshot_matches(
                environment.server, expected_server, summary.failure)) {
            finalize_duration();
            return summary;
        }

        const auto client_deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds{options.project_client_stock_signon ? 60 : 30};
        while (std::chrono::steady_clock::now() < client_deadline &&
               client.running() && server.running() && guard.running()) {
            const auto server_snapshot = server_log->snapshot();
            const auto client_snapshot = client_log->snapshot();
            const auto post_launch_server_log = std::string_view{
                server_snapshot.bytes}.substr((std::min)(
                    server_log_offset_before_client_launch,
                    server_snapshot.bytes.size()));
            const auto server_client = observe_functional_client_log(
                post_launch_server_log);
            const auto client_lower = ascii_lower(client_snapshot.bytes);
            const bool timed_out = contains_any(
                client_lower, {"connection timed out", "connect timeout"});
            const bool steam_authentication_error = contains_any(
                client_lower,
                {"failed to initialize authentication",
                 "steam authentication failed",
                 "steam validation rejected"});
            summary.functional_client_name_observed =
                server_client.name_observed;
            summary.functional_server_connection_accepted =
                server_client.connection_accepted;
            summary.functional_connection_rejected =
                server_client.connection_lost;
            summary.functional_connection_timeout_observed = timed_out;
            summary.functional_steam_authentication_error_observed =
                steam_authentication_error;
            summary.functional_client_entered_game_observed =
                server_client.entered_game;
            const auto project_client = options.project_client_stock_signon
                ? observe_project_client_signon_log(client_snapshot.bytes)
                : ProjectClientSignonObservation{};
            if (options.project_client_stock_signon) {
                apply_project_client_observation(summary, project_client);
            }
            if ((options.project_client_stock_signon &&
                 project_client.complete(options.project_client_stop,
                                         options.project_client_live_input,
                                         options.project_client_reference_prediction)) ||
                (!options.project_client_stock_signon &&
                 server_client.connection_accepted &&
                 server_client.entered_game &&
                 !server_client.connection_lost)) {
                summary.client_ready = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        if (options.project_client_stock_signon) {
            std::optional<std::uint32_t> observed_exit;
            if (!client.running()) {
                observed_exit = client.wait(std::chrono::seconds{1});
                std::this_thread::sleep_for(std::chrono::milliseconds{50});
            }
            const auto final_project = observe_project_client_signon_log(
                client_log->snapshot().bytes);
            apply_project_client_observation(summary, final_project);
            if (final_project.complete(options.project_client_stop,
                                       options.project_client_live_input,
                                       options.project_client_reference_prediction)) {
                summary.client_ready = true;
            }
            const auto waited = observed_exit
                ? windows::OwnedProcess::WaitResult{
                      windows::OwnedProcess::WaitStatus::exited,
                      observed_exit, std::nullopt}
                : client.wait_result(
                      summary.client_ready ? std::chrono::seconds{5}
                                           : std::chrono::milliseconds{100});
            summary.client_exit_code = waited.exit_code;
            summary.client_wait_native_error = waited.native_error;
            switch (waited.status) {
            case windows::OwnedProcess::WaitStatus::exited:
                summary.client_wait_result = "exited";
                break;
            case windows::OwnedProcess::WaitStatus::timeout:
                summary.client_wait_result = "timeout";
                break;
            case windows::OwnedProcess::WaitStatus::wait_failed:
                summary.client_wait_result = "wait-failed";
                break;
            case windows::OwnedProcess::WaitStatus::exit_query_failed:
                summary.client_wait_result = "exit-query-failed";
                break;
            case windows::OwnedProcess::WaitStatus::invalid_process:
                summary.client_wait_result = "invalid-process";
                break;
            }
            if (!server.running() || !guard.running()) {
                summary.client_ready = false;
                summary.failure = !guard.running()
                    ? "isolation-guard-exited-during-project-signon"
                    : "server-exited-during-project-signon";
            }
            if (waited.status == windows::OwnedProcess::WaitStatus::wait_failed ||
                waited.status ==
                    windows::OwnedProcess::WaitStatus::exit_query_failed ||
                waited.status == windows::OwnedProcess::WaitStatus::invalid_process) {
                summary.client_ready = false;
                summary.failure = "project-client-" + summary.client_wait_result;
            } else if (!summary.client_exit_code && summary.client_ready) {
                summary.client_ready = false;
                summary.failure = "project-client-did-not-exit-after-selected-stop";
            } else if (summary.client_exit_code &&
                       *summary.client_exit_code != 0U) {
                summary.client_ready = false;
                summary.failure = "project-client-nonzero-exit";
            }
        }
        if (!summary.client_ready) {
            summary.functional_client_running_at_readiness_deadline =
                client.running();
            if (!client.running()) {
                const auto waited = client.wait_result(
                    std::chrono::milliseconds{100});
                summary.client_exit_code = waited.exit_code;
                summary.client_wait_native_error = waited.native_error;
                if (waited.status == windows::OwnedProcess::WaitStatus::exited) {
                    summary.client_wait_result = "exited";
                }
            }
            if (summary.failure != "project-client-did-not-exit-after-selected-stop" &&
                summary.failure != "project-client-nonzero-exit" &&
                !summary.failure.starts_with("project-client-wait-") &&
                summary.failure != "project-client-exit-query-failed" &&
                summary.failure != "project-client-invalid-process" &&
                summary.failure != "isolation-guard-exited-during-project-signon" &&
                summary.failure != "server-exited-during-project-signon") {
                summary.failure =
                  summary.functional_steam_authentication_error_observed
                    ? "client-steam-authentication-error"
                : summary.functional_connection_rejected
                    ? "client-connection-rejected"
                : summary.functional_connection_timeout_observed
                    ? "client-connect-timeout"
                : summary.functional_server_connection_accepted ||
                      summary.project_connection_accepted
                    ? "client-map-entry-not-observed"
                : !client.running()
                    ? "client-early-exit"
                    : "client-connect-state-unknown";
            }
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }

        if (!options.project_client_stock_signon) {
        // The monotonic timer begins only after the selected server-instance
        // connection has an entered event. Repeated entered lines do not
        // replace this time point; a disconnect/reconnect ends this lifecycle.
        const auto stable_started_at = std::chrono::steady_clock::now();
        const auto stable_deadline = stable_started_at +
            std::chrono::seconds{30};
        while (std::chrono::steady_clock::now() < stable_deadline &&
               client.running() && server.running() && guard.running()) {
            const auto server_snapshot = server_log->snapshot();
            const auto post_launch_server_log = std::string_view{
                server_snapshot.bytes}.substr((std::min)(
                    server_log_offset_before_client_launch,
                    server_snapshot.bytes.size()));
            const auto server_client = observe_functional_client_log(
                post_launch_server_log);
            if (server_client.connection_lost) {
                summary.functional_connection_rejected = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        summary.stable_duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - stable_started_at).count());
        if (!client.running() || !server.running() || !guard.running() ||
            summary.functional_connection_rejected ||
            summary.stable_duration_ms < 30'000U) {
            summary.failure = !guard.running()
                ? "isolation-guard-exited-during-stability"
                : !server.running()
                    ? "server-exited-during-stability"
                : summary.functional_connection_rejected
                    ? "client-disconnected-during-stability"
                    : "client-exited-during-stability";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        const auto final_readiness = windows::observe_hlds_local_readiness(
            server, environment.server, options.server_port,
            options.relay_port, options.map,
            std::chrono::steady_clock::now() + std::chrono::seconds{5},
            [&]() { return client.running() && guard.running(); });
        if (!final_readiness) {
            summary.failure = "server-final-readiness-" + std::string{
                windows::to_string(final_readiness.status)};
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        }

        if (!options.project_client_stock_signon) {
            client.terminate(0U);
            summary.client_exit_code = client.wait(std::chrono::seconds{5});
        }
        server.terminate(0U);
        summary.server_exit_code = server.wait(std::chrono::seconds{5});
        if (!windows::stock_runtime_stock_process_shutdown_confirmed(
                summary.client_exit_code, summary.server_exit_code)) {
            summary.failure = "stock-process-finalization-failed";
            finalize_duration();
            return summary;
        }
        const auto campaign_process_count = campaign_job.active_process_count();
        if (!campaign_process_count || *campaign_process_count != 0U) {
            summary.failure = "campaign-job-not-empty-before-isolation-release";
            finalize_duration();
            return summary;
        }
        if (::SetEvent(options.isolation_release_handle) == FALSE) {
            summary.failure = "isolation-release-signal-failed";
            finalize_duration();
            return summary;
        }
        heartbeat_write.reset();
        auto guard_exit = guard.wait(std::chrono::seconds{5});
        if (!guard_exit || *guard_exit != 0U) {
            guard.terminate(0U);
            guard_exit = guard.wait(std::chrono::seconds{2});
        }
        const auto guard_process_count = guard_job.active_process_count();
        summary.cleanup_exact = summary.client_exit_code &&
            *summary.client_exit_code == 0U && summary.server_exit_code &&
            *summary.server_exit_code == 0U && guard_exit &&
            *guard_exit == 0U && campaign_process_count &&
            *campaign_process_count == 0U && guard_process_count &&
            *guard_process_count == 0U;
        if (!summary.cleanup_exact) {
            summary.failure = "job-cleanup-inexact";
            finalize_duration();
            return summary;
        }
        summary.success = true;
        summary.failure = "none";
        finalize_duration();
        return summary;
    }

    if (options.diagnose_server_profile) {
        auto server_log = windows::BoundedProcessLogCapture::create(
            {64U * 1'024U, 4'096U, 1'024U});
        auto server_error_log = options.private_server_profile_diagnostic
            ? windows::BoundedProcessLogCapture::create(
                  {64U * 1'024U, 4'096U, 1'024U})
            : std::optional<windows::BoundedProcessLogCapture>{};
        if (!server_log ||
            (options.private_server_profile_diagnostic && !server_error_log)) {
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
        server_spec.stderr_handle = options.private_server_profile_diagnostic
            ? server_error_log->inherited_write_handle()
            : server_log->inherited_write_handle();
        server_spec.arguments = {
            L"-console", L"-game", L"valve", L"-port",
            std::to_wstring(options.server_port), L"+ip", L"127.0.0.1",
            L"+map", to_wide_ascii(options.map), L"+maxplayers", L"8",
            L"+sv_lan", L"1"};
            if (options.server_profile_id ==
                windows::HldsRuntimeProfile::Id::legacy_stdio_hlds_banner_v1) {
                server_spec.arguments.push_back( L"-nomaster");
            }
            server_spec.arguments.push_back( L"+status");
        if (!exact_process_snapshot_matches(
                environment.client, std::span<const std::uint32_t>{},
                summary.failure) ||
            !exact_process_snapshot_matches(
                environment.server, std::span<const std::uint32_t>{},
                summary.failure)) {
            finalize_duration();
            return summary;
        }
        if (options.writer_trace_prelaunch_ready_handle !=
                INVALID_HANDLE_VALUE) {
            const auto handoff =
                windows::signal_writer_trace_prelaunch_and_wait(
                    options.writer_trace_prelaunch_ready_handle,
                    options.writer_trace_launch_release_handle,
                    std::chrono::seconds{5});
            summary.writer_trace_prelaunch_ready = handoff.prelaunch_ready;
            summary.writer_trace_launch_released = handoff.launch_released;
            if (!handoff) {
                summary.failure = "writer-trace-" +
                    std::string{windows::to_string(handoff.code)};
                finalize_duration();
                return summary;
            }
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
        if (options.writer_trace_prelaunch_ready_handle !=
                INVALID_HANDLE_VALUE) {
            LARGE_INTEGER frequency{};
            LARGE_INTEGER created{};
            if (::QueryPerformanceFrequency(&frequency) == FALSE ||
                ::QueryPerformanceCounter(&created) == FALSE ||
                frequency.QuadPart <= 0 || created.QuadPart <= 0) {
                summary.failure = "writer-trace-monotonic-clock-unavailable";
                campaign_job.terminate(120U);
                finalize_duration();
                return summary;
            }
            summary.writer_trace_clock_frequency =
                static_cast<std::uint64_t>(frequency.QuadPart);
            summary.writer_trace_stock_process_created_ticks =
                static_cast<std::uint64_t>(created.QuadPart);
        }
        const std::array<std::uint32_t, 1U> expected_server{
            server.process_id()};
        if (!exact_process_snapshot_matches(
                environment.client, std::span<const std::uint32_t>{},
                summary.failure) ||
            !exact_process_snapshot_matches(
                environment.server, expected_server, summary.failure)) {
            finalize_duration();
            return summary;
        }
        server_log->close_parent_write_handle();
        if (server_error_log) server_error_log->close_parent_write_handle();
        windows::HldsBannerParseResult observation;
        const auto diagnose_snapshot = [&](const auto& snapshot) {
                return options.server_profile_id ==
                               windows::HldsRuntimeProfile::Id::steam_hlds_10210_no_mode_banner_v1
                           ? windows::diagnose_observed_hlds_runtime_banner(snapshot)
                           : windows::diagnose_required_hlds_runtime_banner(snapshot, options.map,
                                                                            options.server_port);
            };
            const auto terminal = [](const windows::HldsBannerParseResult& value) {
            return value || value.diagnostic.parse_status ==
                                windows::HldsRuntimeProfileParseStatus::
                                    profile_mismatch ||
                   value.diagnostic.parse_status ==
                       windows::HldsRuntimeProfileParseStatus::malformed ||
                   value.diagnostic.parse_status ==
                       windows::HldsRuntimeProfileParseStatus::duplicate_field ||
                   value.diagnostic.parse_status == windows::
                       HldsRuntimeProfileParseStatus::process_log_truncated;
        };
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds{15};
        std::optional<std::chrono::steady_clock::time_point> terminal_at;
        while (std::chrono::steady_clock::now() < deadline &&
               server.running() && guard.running()) {
            const auto stdout_snapshot = server_log->snapshot();
            if (options.private_server_profile_diagnostic) {
                const auto stderr_snapshot = server_error_log->snapshot();
                const auto stdout_observation = diagnose_snapshot(
                        stdout_snapshot);
                const auto stderr_observation = diagnose_snapshot(
                        stderr_snapshot);
                const auto rank = [](const windows::HldsBannerParseResult& value) {
                    if (value) return 4;
                    switch (value.diagnostic.parse_status) {
                    case windows::HldsRuntimeProfileParseStatus::profile_mismatch:
                        return 3;
                    case windows::HldsRuntimeProfileParseStatus::malformed:
                    case windows::HldsRuntimeProfileParseStatus::duplicate_field:
                    case windows::HldsRuntimeProfileParseStatus::process_log_truncated:
                        return 2;
                    case windows::HldsRuntimeProfileParseStatus::incomplete:
                        return 1;
                    case windows::HldsRuntimeProfileParseStatus::valid:
                        return 4;
                    }
                    return 0;
                };
                observation = rank(stderr_observation) > rank(stdout_observation)
                    ? stderr_observation : stdout_observation;
                if (terminal(observation) && !terminal_at) {
                    terminal_at = std::chrono::steady_clock::now();
                }
                if (terminal_at && std::chrono::steady_clock::now() >=
                        *terminal_at + std::chrono::seconds{5}) {
                    break;
                }
            } else {
                observation = diagnose_snapshot(
                    stdout_snapshot);
                if (terminal(observation)) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        const auto server_output = server_log->snapshot();
        const auto server_error_output = server_error_log
            ? server_error_log->snapshot()
            : windows::BoundedProcessLogSnapshot{};
        std::optional<windows::HldsPrivateBannerShape> private_shape;
        if (options.private_server_profile_diagnostic) {
            private_shape = windows::analyze_hlds_private_banner_streams(
                server_output.bytes, server_error_output.bytes);
        }
        summary.server_profile_diagnostic = observation.diagnostic;
        summary.server_ready = static_cast<bool>(observation);
        if (!terminal(observation)) {
            summary.failure = !server.running()
                ? "server-early-exit" : "server-readiness-timeout";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
            bool readiness_failed = false;
            if (options.server_profile_id ==
                windows::HldsRuntimeProfile::Id::steam_hlds_10210_no_mode_banner_v1) {
                summary.server_readiness = windows::observe_hlds_local_readiness(
                    server, environment.server, options.server_port, options.relay_port,
                    options.map, std::chrono::steady_clock::now() + std::chrono::seconds{5},
                    [&]() { return guard.running(); });
                summary.server_ready = static_cast<bool>(*summary.server_readiness);
                if (!summary.server_ready) {
                    summary.failure =
                        "server-readiness-" +
                        std::string{windows::to_string(summary.server_readiness->status)};
                    readiness_failed = true;
                }
            }
        auto diagnostic_output =
            windows::open_secure_output_directory(options.run_root);
        if (!diagnostic_output || !diagnostic_output.directory ||
            (options.private_server_profile_diagnostic &&
             (!private_shape ||
              !write_bounded_file(
                  *diagnostic_output.directory, L"server-stdout.bin",
                  server_output.bytes, 64U * 1'024U) ||
              !write_bounded_file(
                  *diagnostic_output.directory, L"server-stderr.bin",
                  server_error_output.bytes, 64U * 1'024U) ||
              !write_bounded_file(
                  *diagnostic_output.directory,
                  L"server-banner-shape-private.json",
                  private_banner_shape_json(*private_shape), 64U * 1'024U))) ||
            !write_bounded_file(
                *diagnostic_output.directory,
                L"server-profile-diagnostic.staged.json",
                server_profile_diagnostic_json(observation.diagnostic,
                                                                   options.server_profile_id,
                                                                   summary.server_readiness))) {
            summary.failure = "server-profile-diagnostic-stage-failed";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        if (observation.code ==
                windows::HldsBannerParseErrorCode::process_log_truncated ||
            (options.private_server_profile_diagnostic &&
             !windows::bounded_process_log_snapshot_complete(
                 server_error_output))) {
            summary.failure = "server-profile-process-log-truncated";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
            if (readiness_failed) {
                campaign_job.terminate(120U);
                finalize_duration();
                return summary;
            }
        // A completed diagnostic is operationally successful even when the
        // strict expected profile is unsupported. The typed profile result is
        // published separately and the client-launch branch is unreachable.
        summary.success = true;
        summary.failure = "none";
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
    const bool reconnect = options.scenario == "reconnect";
    UniqueHandle reconnect_transition;
    UniqueHandle reconnect_transition_ack;
    if (reconnect) {
        reconnect_transition = UniqueHandle{::CreateEventW(
            &relay_stop_security, FALSE, FALSE, nullptr)};
        reconnect_transition_ack = UniqueHandle{::CreateEventW(
            &relay_stop_security, FALSE, FALSE, nullptr)};
    }
    if (!relay_stop || !relay_capability) {
        summary.failure = "relay-capability-event-failed";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    if (reconnect &&
        (!reconnect_transition || !reconnect_transition_ack)) {
        summary.failure = "reconnect-capability-event-failed";
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
    if (reconnect) {
        relay_spec.additional_inherited_handles.push_back(
            reconnect_transition.get());
        relay_spec.additional_inherited_handles.push_back(
            reconnect_transition_ack.get());
    }
    relay_spec.arguments = {
        L"--listen-port", std::to_wstring(options.relay_port),
        L"--server-port", std::to_wstring(options.server_port),
        L"--output-run-root", options.run_root.wstring(),
        L"--precreated-empty-run-root",
        L"--output-role", to_wide_ascii(goldsrc::to_string(*options.output_role)),
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
    if (reconnect) {
        relay_spec.arguments.push_back(L"--reconnect-transition-handle");
        relay_spec.arguments.push_back(
            handle_decimal(reconnect_transition.get()));
        relay_spec.arguments.push_back(L"--reconnect-transition-ack-handle");
        relay_spec.arguments.push_back(
            handle_decimal(reconnect_transition_ack.get()));
    }
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
    std::optional<windows::BoundedProcessLogSnapshot>
        relay_terminal_snapshot;
    std::array<windows::OwnedProcess*, 2U> relay_requirements{&relay, &guard};
    summary.relay_ready = wait_for_log_marker(
        *relay_log, relay_requirements,
        "[stock-runtime-capture] relay-ready=true", std::chrono::seconds{5});
    if (!summary.relay_ready || !fs::is_directory(options.run_root)) {
        static_cast<void>(windows::apply_stock_runtime_startup_event(
            startup, relay.running()
                ? windows::StockRuntimeStartupEvent::relay_readiness_timeout
                : windows::StockRuntimeStartupEvent::relay_early_exit));
        if (!relay.running()) {
            const auto wait = relay.wait_result(std::chrono::milliseconds{0});
            retain_relay_wait_result(summary, wait);
            relay_terminal_snapshot = relay_log->finish();
            retain_relay_terminal_diagnostic(
                summary, *relay_terminal_snapshot);
            summary.failure = wait.exit_code &&
                    ((*wait.exit_code & 0xC0000000U) == 0xC0000000U)
                ? "relay-child-exception" : "relay-early-nonzero-exit";
        } else {
            summary.failure = "relay-readiness-timeout";
        }
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
        L"+sv_lan", L"1"};
        if (functional_runtime_capture) {
            server_spec.arguments.push_back(L"+log");
            server_spec.arguments.push_back(L"on");
        }
        if (options.server_profile_id ==
            windows::HldsRuntimeProfile::Id::legacy_stdio_hlds_banner_v1) {
            server_spec.arguments.push_back( L"-nomaster");
        }
        server_spec.arguments.push_back( L"+status");
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
        server_profile =
                options.server_profile_id == windows::HldsRuntimeProfile::Id::steam_hlds_10210_no_mode_banner_v1
                    ? windows::diagnose_observed_hlds_runtime_banner(snapshot)
                    : windows::diagnose_required_hlds_runtime_banner(
            snapshot, options.map, options.server_port);
        if (server_profile ||
            server_profile.code ==
                windows::HldsBannerParseErrorCode::profile_mismatch ||
            server_profile.code == windows::HldsBannerParseErrorCode::malformed ||
            server_profile.code ==
                windows::HldsBannerParseErrorCode::duplicate_field ||
            server_profile.code ==
                windows::HldsBannerParseErrorCode::process_log_truncated) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    summary.server_ready = static_cast<bool>(server_profile);
    if (!summary.server_ready) {
        const auto event = !server.running()
            ? windows::StockRuntimeStartupEvent::server_early_exit
            : server_profile.code == windows::HldsBannerParseErrorCode::profile_mismatch ||
                    server_profile.code == windows::HldsBannerParseErrorCode::malformed ||
                    server_profile.code == windows::HldsBannerParseErrorCode::duplicate_field ||
                    server_profile.code == windows::HldsBannerParseErrorCode::process_log_truncated
                ? windows::StockRuntimeStartupEvent::server_banner_mismatch
                : windows::StockRuntimeStartupEvent::server_readiness_timeout;
        static_cast<void>(windows::apply_stock_runtime_startup_event(
            startup, event));
        summary.failure = server_profile.code ==
                windows::HldsBannerParseErrorCode::profile_mismatch
            ? "server-profile-" + std::string{windows::to_string(
                  server_profile.diagnostic.mismatch_field)} + "-mismatch"
            : "server-profile-" +
                  std::string{windows::to_string(server_profile.code)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
        summary.server_profile_diagnostic = server_profile.diagnostic;
    if (options.server_profile_id ==
            windows::HldsRuntimeProfile::Id::steam_hlds_10210_no_mode_banner_v1) {
            summary.server_readiness = windows::observe_hlds_local_readiness(
                server, environment.server, options.server_port, options.relay_port, options.map,
                std::chrono::steady_clock::now() + std::chrono::seconds{5},
                [&]() { return relay.running() && guard.running(); });
            summary.server_ready = static_cast<bool>(*summary.server_readiness);
            if (!summary.server_ready) {
                static_cast<void>(windows::apply_stock_runtime_startup_event(
            startup,
            windows::StockRuntimeStartupEvent::server_readiness_timeout));
                summary.failure = "server-readiness-" +
                                  std::string{windows::to_string(summary.server_readiness->status)};
                campaign_job.terminate(120U);
                finalize_duration();
                return summary;
            }
        }
        if (!windows::apply_stock_runtime_startup_event(
                startup, windows::StockRuntimeStartupEvent::server_readiness_observed)) {
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
    std::optional<windows::BoundedProcessLogCapture>
        reconnect_generation_b_log;
    std::optional<std::uint32_t> generation_a_exit;
    bool generation_a_tail_emitter_ready_before_shutdown = false;
    windows::OwnedProcessLaunchSpec client_spec;
    client_spec.executable = environment.client.canonical_path;
    client_spec.working_directory = options.research_root;
    client_spec.expected_identity = environment.client;
    client_spec.stdout_handle = client_log->inherited_write_handle();
    client_spec.stderr_handle = client_log->inherited_write_handle();
    client_spec.create_no_window = false;
    client_spec.arguments = {
        L"-game", L"valve", L"-windowed", L"-w", L"800", L"-h", L"600",
        L"+name", functional_runtime_capture ? L"HLC_FUNCTIONAL" : L"HLCLIENT_A",
        L"+connect",
        L"127.0.0.1:" + std::to_wstring(options.relay_port), L"-nojoy"};
    if (functional_runtime_capture) {
        client_spec.arguments.insert(client_spec.arguments.begin(), L"-steam");
    }
    if (!exact_process_snapshot_matches(
            environment.client, std::span<const std::uint32_t>{},
            summary.failure) ||
        !exact_process_snapshot_matches(
            environment.server, expected_server, summary.failure)) {
        finalize_duration();
        return summary;
    }
    const auto server_log_offset_before_client_launch =
        server_log->snapshot().bytes.size();
    auto [client, client_result] = campaign_job.launch(client_spec);
    if (!client_result) {
        summary.failure = "client-" +
            std::string{windows::to_string(client_result.code)};
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    ++summary.processes_started;
    std::array<std::uint32_t, 1U> expected_client{
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
        const auto post_launch_server_log = std::string_view{
            server_snapshot.bytes}.substr((std::min)(
                server_log_offset_before_client_launch,
                server_snapshot.bytes.size()));
        const auto functional_client = functional_runtime_capture
            ? observe_functional_client_log(post_launch_server_log)
            : FunctionalClientLogObservation{};
        const bool named_client = functional_runtime_capture
            ? functional_client.connection_accepted &&
                  functional_client.entered_game &&
                  !functional_client.connection_lost
            : server_snapshot.bytes.find("HLCLIENT_A") != std::string::npos &&
                  (server_snapshot.bytes.find("entered the game") !=
                       std::string::npos ||
                   server_snapshot.bytes.find(" connected") !=
                       std::string::npos);
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
    if (functional_runtime_capture) {
        summary.client_map_entry_observed_ms = elapsed_ms();
    }

    if (functional_runtime_capture) {
        const auto stable_started_at = std::chrono::steady_clock::now();
        auto decision = goldsrc::StockRuntimeCaptureLifecycleDecision::
            continue_capture;
        while (decision == goldsrc::StockRuntimeCaptureLifecycleDecision::
                               continue_capture) {
            const auto now = std::chrono::steady_clock::now();
            const bool sources_healthy = client.running() && server.running() &&
                relay.running() && guard.running();
            const auto snapshot = server_log->snapshot();
            const auto post_launch = std::string_view{snapshot.bytes}.substr(
                (std::min)(server_log_offset_before_client_launch,
                           snapshot.bytes.size()));
            const bool disconnected =
                observe_functional_client_log(post_launch).connection_lost;
            decision = goldsrc::stock_runtime_capture_lifecycle_decision(
                *options.output_role,
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - started_at),
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - stable_started_at),
                options.limits.maximum_duration,
                goldsrc::kFunctionalRuntimeCaptureRequiredInterval,
                sources_healthy && !disconnected);
            if (decision != goldsrc::StockRuntimeCaptureLifecycleDecision::
                                continue_capture) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        summary.stable_duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - stable_started_at).count());
        if (decision != goldsrc::StockRuntimeCaptureLifecycleDecision::
                            functional_interval_complete) {
            summary.failure = "functional-runtime-interval-incomplete";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        summary.functional_interval_completed_ms = elapsed_ms();
        summary.stop_reason = "functional-interval-complete";
    }

    if (reconnect) {
        // Keep generation A live for a bounded post-ready window so its exact
        // post-resource boundary and runtime prefix can be reconstructed from
        // the journal. No candidate body is inspected here.
        const auto generation_a_runtime_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (std::chrono::steady_clock::now() <
                   generation_a_runtime_deadline &&
               client.running() && server.running() && relay.running() &&
               guard.running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        if (!client.running() || !server.running() || !relay.running() ||
            !guard.running()) {
            summary.failure = !guard.running()
                ? "guard-lost-between-generations"
                : !server.running()
                    ? "server-exited-between-generations"
                    : !relay.running()
                        ? "relay-exited-between-generations"
                        : "reconnect-generation-a-exited-early";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }

        // Phase one: while A is provably still alive, require the relay to
        // create and bind its private send-only tail emitter, switch all later
        // A-directed server sends onto it, and ACK that readiness capability.
        if (::SetEvent(reconnect_transition.get()) == FALSE) {
            summary.failure = "reconnect-tail-emitter-prepare-signal-failed";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        const auto prepare_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{5};
        bool prepare_acknowledged = false;
        while (std::chrono::steady_clock::now() < prepare_deadline &&
               client.running() && server.running() && relay.running() &&
               guard.running()) {
            const DWORD prepare_state = ::WaitForSingleObject(
                reconnect_transition_ack.get(), 25U);
            if (prepare_state == WAIT_OBJECT_0) {
                prepare_acknowledged = true;
                break;
            }
            if (prepare_state == WAIT_FAILED) break;
        }
        if (!prepare_acknowledged || !client.running() || !server.running() ||
            !relay.running() || !guard.running()) {
            summary.failure = !guard.running()
                ? "guard-lost-between-generations"
                : !server.running()
                    ? "server-exited-between-generations"
                    : !relay.running()
                        ? "relay-exited-between-generations"
                        : !client.running()
                            ? "reconnect-generation-a-exited-before-tail-ready"
                            : "reconnect-tail-emitter-not-ready-before-shutdown";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        generation_a_tail_emitter_ready_before_shutdown = true;

        const auto generation_a_process_id = client.process_id();
        client.terminate(0U);
        generation_a_exit = client.wait(std::chrono::seconds{5});
        if (!generation_a_exit || *generation_a_exit != 0U) {
            summary.failure = "reconnect-generation-a-exit-failed";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        if (!exact_process_snapshot_matches(
                environment.client, std::span<const std::uint32_t>{},
                summary.failure) ||
            !exact_process_snapshot_matches(
                environment.server, expected_server, summary.failure)) {
            summary.failure = "reconnect-generation-a-socket-or-process-retained";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        if (!guard.running() || !server.running() || !relay.running()) {
            summary.failure = !guard.running()
                ? "guard-lost-between-generations"
                : !server.running()
                    ? "server-exited-between-generations"
                    : "relay-exited-between-generations";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        // Phase two: only after exact A-process absence is proved may the relay
        // start its source-quiet window. Its second ACK is the sole capability
        // that permits generation B to launch.
        if (::SetEvent(reconnect_transition.get()) == FALSE) {
            summary.failure = "reconnect-post-exit-quiet-signal-failed";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }

        const auto transition_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{15};
        bool transition_acknowledged = false;
        while (std::chrono::steady_clock::now() < transition_deadline &&
               server.running() && relay.running() && guard.running()) {
            const DWORD transition_state = ::WaitForSingleObject(
                reconnect_transition_ack.get(), 25U);
            if (transition_state == WAIT_OBJECT_0) {
                transition_acknowledged = true;
                break;
            }
            if (transition_state == WAIT_FAILED) break;
        }
        if (!transition_acknowledged || !server.running() || !relay.running() ||
            !guard.running()) {
            summary.failure = !guard.running()
                ? "guard-lost-between-generations"
                : !server.running()
                    ? "server-exited-between-generations"
                    : !relay.running()
                        ? "relay-exited-between-generations"
                        : "reconnect-post-exit-quiet-not-proven";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        reconnect_transition.reset();
        reconnect_transition_ack.reset();

        reconnect_generation_b_log =
            windows::BoundedProcessLogCapture::create({});
        if (!reconnect_generation_b_log) {
            summary.failure = "reconnect-generation-b-log-capture-failed";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        client_spec.stdout_handle =
            reconnect_generation_b_log->inherited_write_handle();
        client_spec.stderr_handle =
            reconnect_generation_b_log->inherited_write_handle();
        client_spec.arguments[8U] = L"HLCLIENT_B";
        if (!exact_process_snapshot_matches(
                environment.client, std::span<const std::uint32_t>{},
                summary.failure) ||
            !exact_process_snapshot_matches(
                environment.server, expected_server, summary.failure)) {
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        auto [generation_b_client, generation_b_result] =
            campaign_job.launch(client_spec);
        if (!generation_b_result ||
            generation_b_client.process_id() == generation_a_process_id) {
            summary.failure = "reconnect-generation-b-launch-failed";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        ++summary.processes_started;
        client = std::move(generation_b_client);
        expected_client[0] = client.process_id();
        reconnect_generation_b_log->close_parent_write_handle();
        if (!exact_process_snapshot_matches(
                environment.client, expected_client, summary.failure) ||
            !exact_process_snapshot_matches(
                environment.server, expected_server, summary.failure)) {
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }

        const auto generation_b_ready_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{20};
        bool generation_b_ready = false;
        while (std::chrono::steady_clock::now() <
                   generation_b_ready_deadline &&
               client.running() && server.running() && relay.running() &&
               guard.running()) {
            const auto server_snapshot = server_log->snapshot();
            const auto relay_snapshot = relay_log->snapshot();
            const bool named_client =
                server_snapshot.bytes.find("HLCLIENT_B") != std::string::npos &&
                (server_snapshot.bytes.find("entered the game") !=
                     std::string::npos ||
                 server_snapshot.bytes.find(" connected") !=
                     std::string::npos);
            const bool second_bidirectional = relay_snapshot.bytes.find(
                "[stock-runtime-capture] reconnect-generation=2;"
                "bidirectional-traffic=true") != std::string::npos;
            if (named_client && second_bidirectional) {
                generation_b_ready = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        if (!generation_b_ready) {
            summary.failure = !guard.running()
                ? "guard-lost-between-generations"
                : !server.running()
                    ? "server-exited-between-generations"
                    : !relay.running()
                        ? "relay-exited-between-generations"
                        : "reconnect-generation-b-not-ready";
            campaign_job.terminate(120U);
            finalize_duration();
            return summary;
        }
        summary.connection_generations = 2U;
        summary.generation_distinct = true;
    }

    if (goldsrc::stock_runtime_capture_waits_until_requested_deadline(
            *options.output_role)) {
        const auto relay_deadline = relay_started_at +
            options.limits.maximum_duration - std::chrono::milliseconds{250};
        while (std::chrono::steady_clock::now() < relay_deadline &&
               client.running() && server.running() && relay.running() &&
               guard.running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
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
    summary.shutdown_requested_ms = elapsed_ms();
    summary.stock_shutdown_method = "owned-process-terminate";
    // Freeze the relay's accepted-observation boundary while both peer UDP
    // endpoints are still alive. Closing the client first can make a later
    // server-to-client send raise a Windows UDP reset in the relay before it
    // sees the stop event, losing the otherwise finalizable journal.
    if (::SetEvent(relay_stop.get()) == FALSE) {
        summary.failure = "relay-stop-signal-failed";
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    summary.relay_stop_requested_ms = elapsed_ms();
    const auto relay_wait = relay.wait_result(std::chrono::seconds{10});
    retain_relay_wait_result(summary, relay_wait);
    if (relay_wait.status == windows::OwnedProcess::WaitStatus::exited) {
        relay_terminal_snapshot = relay_log->finish();
        retain_relay_terminal_diagnostic(
            summary, *relay_terminal_snapshot);
    } else {
        // Snapshot the bounded reader before terminating the exact Job, then
        // drain it after every inherited writer has closed. This preserves
        // any child primary diagnostic and also gives the parent a typed wait
        // result if the child never reached its terminal record.
        retain_relay_terminal_diagnostic(summary, relay_log->snapshot());
        campaign_exit_barrier_result = campaign_job.terminate_and_wait(
            120U, std::chrono::seconds{5});
        relay_terminal_snapshot = relay_log->finish();
        retain_relay_terminal_diagnostic(
            summary, *relay_terminal_snapshot);
    }
    if (!relay_wait || *relay_wait.exit_code != 0U) {
        if (relay_wait.status == windows::OwnedProcess::WaitStatus::timeout) {
            summary.failure = "relay-finalization-timeout";
            summary.relay_failed_operation = "wait-relay-finalization";
        } else if (relay_wait.status !=
                   windows::OwnedProcess::WaitStatus::exited) {
            summary.failure = "relay-wait-failed";
            summary.relay_failed_operation = "wait-relay-process";
        } else if (relay_wait.exit_code &&
                   ((*relay_wait.exit_code & 0xC0000000U) == 0xC0000000U)) {
            summary.failure = "relay-child-exception";
            if (summary.relay_failed_operation == "unavailable") {
                summary.relay_failed_operation = "relay-process-exception";
            }
        } else {
            summary.failure = "relay-nonzero-exit";
        }
        campaign_job.terminate(120U);
        finalize_duration();
        return summary;
    }
    const auto relay_exit = relay_wait.exit_code;
    summary.relay_finalization_completed_ms = elapsed_ms();
    client.terminate(0U);
    const auto client_exit = client.wait(std::chrono::seconds{5});
    if (!client_exit || *client_exit != 0U) {
        summary.failure = "client-process-finalization-failed";
        finalize_duration();
        return summary;
    }
    server.terminate(0U);
    const auto server_exit = server.wait(std::chrono::seconds{5});
    if (!windows::stock_runtime_stock_process_shutdown_confirmed(
            client_exit, server_exit)) {
        summary.failure = "stock-process-finalization-failed";
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
    const auto relay_snapshot = relay_terminal_snapshot
        ? std::move(*relay_terminal_snapshot)
        : relay_log->finish();
    const auto server_snapshot = server_log->finish();
    const auto client_snapshot = client_log->finish();
    std::optional<windows::BoundedProcessLogSnapshot>
        reconnect_generation_b_snapshot;
    if (reconnect && reconnect_generation_b_log) {
        reconnect_generation_b_snapshot = reconnect_generation_b_log->finish();
    }
    const auto guard_snapshot = guard_log->finish();
    const auto guard_process_count = guard_job.active_process_count();
    const std::optional<std::size_t> total_process_count =
        campaign_process_count && guard_process_count
        ? std::optional<std::size_t>{
              *campaign_process_count + *guard_process_count}
        : std::nullopt;
    summary.cleanup_exact = windows::stock_runtime_graceful_cleanup_is_exact(
        client_exit, server_exit, relay_exit, guard_exit,
        total_process_count) &&
        (!reconnect || (generation_a_exit && *generation_a_exit == 0U));
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
        (reconnect &&
         (!reconnect_generation_b_snapshot ||
          !windows::bounded_process_log_snapshot_complete(
              *reconnect_generation_b_snapshot))) ||
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
    bool logs_written = run_output && run_output.directory && logs_output &&
        logs_output.directory &&
        write_bounded_file(*logs_output.directory, L"relay.log",
                           relay_snapshot.bytes) &&
        write_bounded_file(*logs_output.directory, L"server.log",
                           server_snapshot.bytes) &&
        write_bounded_file(*logs_output.directory, L"guard.log",
                           guard_snapshot.bytes) &&
        write_bounded_file(*logs_output.directory, L"relay-metadata.json",
                           log_metadata_json(relay_snapshot)) &&
        write_bounded_file(*logs_output.directory, L"server-metadata.json",
                           log_metadata_json(server_snapshot)) &&
        write_bounded_file(*logs_output.directory, L"guard-metadata.json",
                           log_metadata_json(guard_snapshot));
    if (logs_written && reconnect) {
        logs_written = reconnect_generation_b_snapshot &&
            write_bounded_file(
                *logs_output.directory, L"client-generation-a.log",
                client_snapshot.bytes) &&
            write_bounded_file(
                *logs_output.directory, L"client-generation-a-metadata.json",
                log_metadata_json(client_snapshot)) &&
            write_bounded_file(
                *logs_output.directory, L"client-generation-b.log",
                reconnect_generation_b_snapshot->bytes) &&
            write_bounded_file(
                *logs_output.directory, L"client-generation-b-metadata.json",
                log_metadata_json(*reconnect_generation_b_snapshot));
    } else if (logs_written) {
        logs_written =
            write_bounded_file(*logs_output.directory, L"client.log",
                               client_snapshot.bytes) &&
            write_bounded_file(*logs_output.directory, L"client-metadata.json",
                               log_metadata_json(client_snapshot));
    }
    if (!logs_written) {
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
              << "  \"schema\": "
                     "\"hlclient.stock-runtime-isolation-attestation.v1\",\n"
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
    if (reconnect) {
        std::ostringstream reconnect_attestation;
        reconnect_attestation
            << "{\n"
            << "  \"schema\": \""
            << goldsrc::kStockRuntimeReconnectOrchestrationAttestationSchema
            << "\",\n"
            << "  \"connection_generation_count\": 2,\n"
            << "  \"generation_distinct\": true,\n"
            << "  \"generation_a_process_role_identity\": \""
            << goldsrc::kStockRuntimeGenerationAProcessRole << "\",\n"
            << "  \"generation_b_process_role_identity\": \""
            << goldsrc::kStockRuntimeGenerationBProcessRole << "\",\n"
            << "  \"generation_a_endpoint_role_identity\": \""
            << goldsrc::kStockRuntimeGenerationAEndpointRole << "\",\n"
            << "  \"generation_b_endpoint_role_identity\": \""
            << goldsrc::kStockRuntimeGenerationBEndpointRole << "\",\n"
            << "  \"generation_a_tail_emitter_ready_before_shutdown\": "
            << (generation_a_tail_emitter_ready_before_shutdown
                    ? "true" : "false") << ",\n"
            << "  \"generation_a_controlled_shutdown\": true,\n"
            << "  \"generation_a_endpoint_quiet\": true,\n"
            << "  \"generation_b_fresh_owned_process\": true,\n"
            << "  \"generation_b_fresh_connection_lifecycle\": "
               "\"observed_by_relay\",\n"
            << "  \"guard_continuity\": true,\n"
            << "  \"server_continuity\": true,\n"
            << "  \"relay_continuity\": true,\n"
            << "  \"cleanup_status\": \"exact\",\n"
            << "  \"restoration_status\": \"wrapper_pending\",\n"
            << "  \"post_resource_boundary_status\": \"evidence_pending\",\n"
            << "  \"candidate_status\": \"evidence_pending\",\n"
            << "  \"candidate_body_consumed\": false,\n"
            << "  \"candidate_semantic_category_assigned\": false,\n"
            << "  \"publication_status\": \"staged\"\n"
            << "}\n";
        if (!write_bounded_file(
                *run_output.directory,
                L"reconnect-orchestration.staged.json",
                reconnect_attestation.str())) {
            summary.failure = "reconnect-attestation-write-failed";
            summary.success = false;
            summary.bounded_transport_complete = false;
            finalize_duration();
            return summary;
        }
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
    if (options.writer_trace_stock_stopped_handle != INVALID_HANDLE_VALUE &&
        ::WaitForSingleObject(
            options.writer_trace_stock_stopped_handle, 0U) == WAIT_OBJECT_0) {
        summary.writer_trace_stock_processes_stopped = true;
        LARGE_INTEGER stopped{};
        if (::QueryPerformanceCounter(&stopped) != FALSE &&
            stopped.QuadPart > 0) {
            summary.writer_trace_stock_processes_stopped_ticks =
                static_cast<std::uint64_t>(stopped.QuadPart);
        }
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
    if (options.functional_smoke) {
        windows::BoundedProcessLogSnapshot server_snapshot;
        windows::BoundedProcessLogSnapshot client_snapshot;
        windows::BoundedProcessLogSnapshot guard_snapshot;
        if (functional_server_log) {
            server_snapshot = functional_server_log->finish();
        } else {
            server_snapshot.capture_failed = true;
            server_snapshot.native_error = ERROR_NOT_ENOUGH_MEMORY;
        }
        if (functional_client_log) {
            client_snapshot = functional_client_log->finish();
        } else {
            client_snapshot.capture_failed = true;
            client_snapshot.native_error = ERROR_NOT_ENOUGH_MEMORY;
        }
        if (guard_log) {
            guard_snapshot = guard_log->finish();
        } else {
            guard_snapshot.capture_failed = true;
            guard_snapshot.native_error = ERROR_NOT_ENOUGH_MEMORY;
        }
        summary.functional_diagnostic_published = write_functional_diagnostics(
            options, summary, server_snapshot, client_snapshot, guard_snapshot);
        if (!summary.functional_diagnostic_published && summary.success) {
            summary.success = false;
            summary.failure = "functional-diagnostic-publication-incomplete";
        }
    }
    if (summary.cleanup_exact) redundant_isolation.close();
    campaign_job.close();
    guard_job.close();
    finalize_duration();
    return summary;
}

[[nodiscard]] bool nonempty_regular_file(const fs::path& path)
{
    std::error_code error;
    return fs::is_regular_file(path, error) && !error &&
           fs::file_size(path, error) != 0U && !error;
}

[[nodiscard]] int run_functional_lifecycle_validation(const Options& options)
{
    const auto expected_client = sibling_executable(
        L"hlclient_stock_runtime_fake_client.exe");
    const auto expected_server = sibling_executable(
        L"hlclient_stock_runtime_fake_server.exe");
    const auto expected_relay = sibling_executable(
        L"hlclient_stock_runtime_capture.exe");
    if (!paths_equal(options.client, expected_client) ||
        !paths_equal(options.server, expected_server) ||
        !paths_equal(options.relay, expected_relay) ||
        !validate_new_run_root(options.run_root, options.output_role, false)) {
        std::cerr << "[functional-lifecycle-test] result=unsafe-input\n";
        return 3;
    }
    const auto client_identity = observe_project_binary(options.client);
    const auto server_identity = observe_project_binary(options.server);
    const auto relay_identity = observe_project_binary(options.relay);
    if (!client_identity || !server_identity || !relay_identity) {
        std::cerr << "[functional-lifecycle-test] result=identity-failed\n";
        return 4;
    }
    if (::CreateDirectoryW(options.run_root.c_str(), nullptr) == FALSE) {
        std::cerr << "[functional-lifecycle-test] result=run-root-create-failed\n";
        return 5;
    }

    auto [job, job_created] = windows::KillOnCloseProcessJob::create(3U);
    auto relay_log = windows::BoundedProcessLogCapture::create({});
    auto server_log = windows::BoundedProcessLogCapture::create({});
    auto client_log = windows::BoundedProcessLogCapture::create({});
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    UniqueHandle relay_stop{::CreateEventW(&security, TRUE, FALSE, nullptr)};
    UniqueHandle relay_capability{
        ::CreateEventW(&security, TRUE, FALSE, nullptr)};
    UniqueHandle test_injection;
    if (options.validate_functional_lifecycle_injected_failure) {
        test_injection = UniqueHandle{
            ::CreateEventW(&security, TRUE, TRUE, nullptr)};
    }
    if (!job_created || !relay_log || !server_log || !client_log ||
        !relay_stop || !relay_capability ||
        (options.validate_functional_lifecycle_injected_failure &&
         !test_injection)) {
        std::cerr << "[functional-lifecycle-test] result=setup-failed\n";
        return 5;
    }

    windows::OwnedProcessLaunchSpec relay_spec;
    relay_spec.executable = relay_identity.identity->canonical_path;
    relay_spec.working_directory = fs::current_path();
    relay_spec.expected_identity = *relay_identity.identity;
    relay_spec.stdout_handle = relay_log->inherited_write_handle();
    relay_spec.stderr_handle = relay_log->inherited_write_handle();
    relay_spec.additional_inherited_handles = {
        relay_stop.get(), relay_capability.get()};
    if (test_injection) {
        relay_spec.additional_inherited_handles.push_back(
            test_injection.get());
    }
    relay_spec.arguments = {
        L"--listen-port", std::to_wstring(options.relay_port),
        L"--server-port", std::to_wstring(options.server_port),
        L"--output-run-root", options.run_root.wstring(),
        L"--precreated-empty-run-root", L"--output-role",
        L"functional-runtime-capture", L"--scenario", L"idle-runtime",
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
        std::to_wstring(options.validate_functional_lifecycle_limit
                           ? 1U
                           : options.perturbation.client_packet_ordinal),
        L"--mutation-after-server-packets",
        std::to_wstring(options.validate_functional_lifecycle_limit
                           ? 1U
                           : options.perturbation.server_packet_ordinal),
        L"--stop-handle", handle_decimal(relay_stop.get()),
        L"--orchestrator-capability-handle",
        handle_decimal(relay_capability.get()), L"--orchestrator-process-id",
        std::to_wstring(::GetCurrentProcessId())};
    if (options.validate_functional_lifecycle_injected_failure) {
        relay_spec.arguments.push_back(
            L"--test-injected-terminal-failure");
        relay_spec.arguments.emplace_back(
            options.validate_functional_lifecycle_injected_failure->begin(),
            options.validate_functional_lifecycle_injected_failure->end());
        relay_spec.arguments.push_back(L"--test-injection-handle");
        relay_spec.arguments.push_back(handle_decimal(test_injection.get()));
    }
    auto [relay, relay_started] = job.launch(relay_spec);
    if (!relay_started ||
        ::WaitForSingleObject(relay_capability.get(), 5'000U) != WAIT_OBJECT_0) {
        static_cast<void>(job.terminate_and_wait(
            120U, std::chrono::seconds{3}));
        std::cerr << "[functional-lifecycle-test] result=relay-start-failed\n";
        return 6;
    }
    relay_capability.reset();
    relay_log->close_parent_write_handle();
    std::array<windows::OwnedProcess*, 1U> relay_required{&relay};
    if (!wait_for_log_marker(*relay_log, relay_required,
                             "[stock-runtime-capture] relay-ready=true",
                             std::chrono::seconds{3})) {
        static_cast<void>(job.terminate_and_wait(
            120U, std::chrono::seconds{3}));
        std::cerr << "[functional-lifecycle-test] result=relay-not-ready\n";
        return 6;
    }

    windows::OwnedProcessLaunchSpec server_spec;
    server_spec.executable = server_identity.identity->canonical_path;
    server_spec.working_directory = server_spec.executable.parent_path();
    server_spec.expected_identity = *server_identity.identity;
    server_spec.stdout_handle = server_log->inherited_write_handle();
    server_spec.stderr_handle = server_log->inherited_write_handle();
    server_spec.arguments = {
        L"--port", std::to_wstring(options.server_port), L"--duration-ms",
        L"5000", L"--repeat-responses"};
    auto [server, server_started] = job.launch(server_spec);
    server_log->close_parent_write_handle();
    if (!server_started) {
        static_cast<void>(job.terminate_and_wait(
            120U, std::chrono::seconds{3}));
        std::cerr << "[functional-lifecycle-test] result=server-start-failed\n";
        return 7;
    }
    std::array<windows::OwnedProcess*, 2U> server_required{&server, &relay};
    if (!wait_for_log_marker(*server_log, server_required,
                             "[hlclient-fake-server] ready=true",
                             std::chrono::seconds{3})) {
        static_cast<void>(job.terminate_and_wait(
            120U, std::chrono::seconds{3}));
        std::cerr << "[functional-lifecycle-test] result=server-not-ready\n";
        return 7;
    }

    windows::OwnedProcessLaunchSpec client_spec;
    client_spec.executable = client_identity.identity->canonical_path;
    client_spec.working_directory = client_spec.executable.parent_path();
    client_spec.expected_identity = *client_identity.identity;
    client_spec.stdout_handle = client_log->inherited_write_handle();
    client_spec.stderr_handle = client_log->inherited_write_handle();
    client_spec.arguments = {
        L"--port", std::to_wstring(options.relay_port), L"--timeout-ms",
        L"1000", L"--duration-ms", L"5000", L"--repeat-interval-ms",
        L"2", L"--exit-code", L"0"};
    if (!options.validate_functional_lifecycle_limit) {
        client_spec.arguments.push_back(L"--auxiliary-count");
        client_spec.arguments.push_back(L"2");
    }
    auto [client, client_started] = job.launch(client_spec);
    client_log->close_parent_write_handle();
    if (!client_started) {
        static_cast<void>(job.terminate_and_wait(
            120U, std::chrono::seconds{3}));
        std::cerr << "[functional-lifecycle-test] result=client-start-failed\n";
        return 8;
    }
    if (options.validate_functional_lifecycle_limit) {
        const auto relay_wait = relay.wait_result(std::chrono::seconds{5});
        const auto relay_exit = relay_wait.exit_code;
        client.terminate(0U);
        const auto client_exit = client.wait(std::chrono::seconds{3});
        server.terminate(0U);
        const auto server_exit = server.wait(std::chrono::seconds{3});
        const auto relay_snapshot = relay_log->finish();
        static_cast<void>(client_log->finish());
        static_cast<void>(server_log->finish());
        const auto cleanup = job.terminate_and_wait(
            120U, std::chrono::seconds{3});
        std::error_code manifest_error;
        const bool false_complete_manifest = fs::exists(
            options.run_root / "functional-runtime-capture.json",
            manifest_error);
        if (!relay_exit || *relay_exit != 7U || !client_exit ||
            *client_exit != 0U || !server_exit || *server_exit != 0U ||
            !cleanup || cleanup.active_process_count != 0U ||
            false_complete_manifest || manifest_error ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] stop-reason="
                "client-packet-limit") == std::string::npos ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] result="
                "capture-bound-exceeded-journal-preserved") ==
                std::string::npos ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] failed-operation="
                "enforce-capture-budget") == std::string::npos ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] native-error-domain="
                "project-validation") == std::string::npos ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] journal-publication-state="
                "published-incomplete") == std::string::npos ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] metadata-publication-state="
                "published-incomplete") == std::string::npos ||
            !nonempty_regular_file(
                options.run_root / "transport-journal.jsonl") ||
            !nonempty_regular_file(options.run_root / "capture-metadata.json")) {
            std::cerr << "[functional-lifecycle-limit-test] relay-exit="
                      << (relay_exit ? std::to_string(*relay_exit) : "missing")
                      << ";cleanup=" << (cleanup ? "exact" : "failed")
                      << ";relay-log=" << relay_snapshot.bytes << '\n';
            std::cerr << "[functional-lifecycle-limit-test] result=failed\n";
            return 12;
        }
        std::cout << "[functional-lifecycle-limit-test] stop-reason="
                     "client-packet-limit\n"
                  << "[functional-lifecycle-limit-test] journal=preserved\n"
                  << "[functional-lifecycle-limit-test] complete-manifest="
                     "absent\n"
                  << "[functional-lifecycle-limit-test] stock-launch=absent\n"
                  << "[functional-lifecycle-limit-test] result=success\n";
        return 0;
    }
    if (options.validate_functional_lifecycle_injected_failure) {
        const auto relay_wait = relay.wait_result(std::chrono::seconds{5});
        const auto relay_exit = relay_wait.exit_code;
        const bool peers_alive_through_relay_terminal =
            client.running() && server.running();
        client.terminate(0U);
        const auto client_exit = client.wait(std::chrono::seconds{3});
        server.terminate(0U);
        const auto server_exit = server.wait(std::chrono::seconds{3});
        const auto relay_snapshot = relay_log->finish();
        static_cast<void>(client_log->finish());
        static_cast<void>(server_log->finish());
        const auto cleanup = job.terminate_and_wait(
            120U, std::chrono::seconds{3});
        const auto expected_exit =
            *options.validate_functional_lifecycle_injected_failure == "receive"
            ? 6U : 8U;
        const auto expected_operation =
            *options.validate_functional_lifecycle_injected_failure == "receive"
            ? "recvfrom-client-facing" : "sendto-server";
        const auto expected_error =
            *options.validate_functional_lifecycle_injected_failure == "receive"
            ? std::to_string(WSAECONNRESET) : std::to_string(WSAENOBUFS);
        std::ifstream journal_file{
            options.run_root / "transport-journal.jsonl", std::ios::binary};
        std::string journal_bytes{
            std::istreambuf_iterator<char>{journal_file},
            std::istreambuf_iterator<char>{}};
        std::error_code manifest_error;
        const bool false_complete_manifest = fs::exists(
            options.run_root / "functional-runtime-capture.json",
            manifest_error);
        const bool send_record_unemitted =
            *options.validate_functional_lifecycle_injected_failure != "send" ||
            journal_bytes.find("\"emitted_ordinals\":[]") !=
                std::string::npos;
        if (!relay_exit || *relay_exit != expected_exit ||
            !peers_alive_through_relay_terminal || !client_exit ||
            *client_exit != 0U || !server_exit || *server_exit != 0U ||
            !cleanup || cleanup.active_process_count != 0U ||
            false_complete_manifest || manifest_error ||
            journal_bytes.empty() || journal_bytes.size() > 1U * 1'024U * 1'024U ||
            !send_record_unemitted ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] failed-operation=" +
                std::string{expected_operation}) == std::string::npos ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] native-error-domain=Winsock") ==
                std::string::npos ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] native-error-code=" +
                expected_error) == std::string::npos ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] journal-publication-state="
                "published-incomplete") == std::string::npos ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] metadata-publication-state="
                "published-incomplete") == std::string::npos ||
            !nonempty_regular_file(
                options.run_root / "capture-metadata.json")) {
            std::cerr << "[functional-lifecycle-injected-failure-test] "
                         "result=failed;variant="
                      << *options.validate_functional_lifecycle_injected_failure
                      << ";relay-exit="
                      << (relay_exit ? std::to_string(*relay_exit) : "missing")
                      << ";cleanup=" << (cleanup ? "exact" : "failed")
                      << ";relay-log=" << relay_snapshot.bytes << '\n';
            return 14;
        }
        std::cout << "[functional-lifecycle-injected-failure-test] variant="
                  << *options.validate_functional_lifecycle_injected_failure
                  << '\n'
                  << "[functional-lifecycle-injected-failure-test] "
                     "primary-error=retained\n"
                  << "[functional-lifecycle-injected-failure-test] "
                     "journal=published-incomplete\n"
                  << "[functional-lifecycle-injected-failure-test] "
                     "metadata=published-incomplete\n"
                  << "[functional-lifecycle-injected-failure-test] "
                     "complete-manifest=absent\n"
                  << "[functional-lifecycle-injected-failure-test] "
                     "peers-alive-through-relay-terminal=true\n"
                  << "[functional-lifecycle-injected-failure-test] "
                     "stock-launch=absent\n"
                  << "[functional-lifecycle-injected-failure-test] "
                     "result=success\n";
        return 0;
    }
    std::array<windows::OwnedProcess*, 3U> all_required{
        &client, &server, &relay};
    const bool client_ready = wait_for_log_marker(
        *client_log, all_required, "[hlclient-fake-client] ready=true",
        std::chrono::seconds{3});
    const bool bidirectional = wait_for_log_marker(
        *relay_log, all_required,
        "[stock-runtime-capture] bidirectional-traffic=true",
        std::chrono::seconds{3});
    if (!client_ready || !bidirectional) {
        static_cast<void>(job.terminate_and_wait(
            120U, std::chrono::seconds{3}));
        std::cerr << "[functional-lifecycle-test] result=map-entry-not-ready\n";
        return 8;
    }

    // The process-level fixture uses a scaled physical wait but advances the
    // same production decision with the configured 90 s/15 s logical clock.
    // Continuous peer traffic makes the old wait-to-maximum behavior exceed
    // the deliberately small directional bound.
    const auto before = goldsrc::stock_runtime_capture_lifecycle_decision(
        *options.output_role, std::chrono::seconds{30},
        std::chrono::milliseconds{14'999}, options.limits.maximum_duration);
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    const bool sources_healthy = client.running() && server.running() &&
        relay.running();
    const auto completed = goldsrc::stock_runtime_capture_lifecycle_decision(
        *options.output_role, std::chrono::seconds{30},
        goldsrc::kFunctionalRuntimeCaptureRequiredInterval,
        options.limits.maximum_duration,
        goldsrc::kFunctionalRuntimeCaptureRequiredInterval, sources_healthy);
    if (before != goldsrc::StockRuntimeCaptureLifecycleDecision::
                      continue_capture ||
        completed != goldsrc::StockRuntimeCaptureLifecycleDecision::
                         functional_interval_complete) {
        static_cast<void>(job.terminate_and_wait(
            120U, std::chrono::seconds{3}));
        std::cerr << "[functional-lifecycle-test] result=decision-failed\n";
        return 9;
    }

    if (options.validate_functional_lifecycle_writer_failure) {
        const auto blocked_journal =
            options.run_root / L"transport-journal.jsonl";
        if (::CreateDirectoryW(blocked_journal.c_str(), nullptr) == FALSE) {
            static_cast<void>(job.terminate_and_wait(
                120U, std::chrono::seconds{3}));
            std::cerr << "[functional-lifecycle-writer-failure-test] "
                         "result=setup-failed\n";
            return 13;
        }
    }

    if (::SetEvent(relay_stop.get()) == FALSE) {
        static_cast<void>(job.terminate_and_wait(
            120U, std::chrono::seconds{3}));
        std::cerr << "[functional-lifecycle-test] result=stop-handoff-failed\n";
        return 10;
    }
    const auto relay_wait = relay.wait_result(std::chrono::seconds{5});
    const auto relay_exit = relay_wait.exit_code;
    const bool peers_alive_through_relay_finalization =
        client.running() && server.running();
    client.terminate(0U);
    const auto client_exit = client.wait(std::chrono::seconds{3});
    server.terminate(0U);
    const auto server_exit = server.wait(std::chrono::seconds{3});
    const auto relay_snapshot = relay_log->finish();
    const auto cleanup = job.terminate_and_wait(
        120U, std::chrono::seconds{3});
    if (options.validate_functional_lifecycle_writer_failure) {
        std::error_code manifest_error;
        const bool false_complete_manifest = fs::exists(
            options.run_root / "functional-runtime-capture.json",
            manifest_error);
        if (!relay_exit || *relay_exit != 15U ||
            !peers_alive_through_relay_finalization || !client_exit ||
            *client_exit != 0U || !server_exit || *server_exit != 0U ||
            !cleanup || cleanup.active_process_count != 0U ||
            false_complete_manifest || manifest_error ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] failed-operation="
                "write-transport-journal") == std::string::npos ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] native-error-domain="
                "project-validation") == std::string::npos ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] journal-publication-state=failed") ==
                std::string::npos ||
            relay_snapshot.bytes.find(
                "[stock-runtime-capture] metadata-publication-state="
                "published-incomplete") == std::string::npos ||
            !nonempty_regular_file(
                options.run_root / "capture-metadata.json")) {
            std::cerr << "[functional-lifecycle-writer-failure-test] "
                         "relay-exit="
                      << (relay_exit ? std::to_string(*relay_exit) : "missing")
                      << ";cleanup=" << (cleanup ? "exact" : "failed")
                      << ";relay-log=" << relay_snapshot.bytes << '\n'
                      << "[functional-lifecycle-writer-failure-test] "
                         "result=failed\n";
            return 13;
        }
        std::cout << "[functional-lifecycle-writer-failure-test] "
                     "writer-error=distinct\n"
                  << "[functional-lifecycle-writer-failure-test] "
                     "metadata=published-incomplete\n"
                  << "[functional-lifecycle-writer-failure-test] "
                     "complete-manifest=absent\n"
                  << "[functional-lifecycle-writer-failure-test] "
                     "peer-shutdown-after-relay-finalization=true\n"
                  << "[functional-lifecycle-writer-failure-test] "
                     "stock-launch=absent\n"
                  << "[functional-lifecycle-writer-failure-test] "
                     "result=success\n";
        return 0;
    }
    if (!relay_exit || *relay_exit != 0U ||
        !peers_alive_through_relay_finalization || !server_exit ||
        *server_exit != 0U || !cleanup ||
        cleanup.active_process_count != 0U ||
        relay_snapshot.bytes.find(
            "[stock-runtime-capture] stop-reason="
            "functional-interval-complete") == std::string::npos ||
        relay_snapshot.bytes.find(
            "[stock-runtime-capture] result="
            "bounded-transport-complete-evidence-pending") ==
            std::string::npos ||
        relay_snapshot.bytes.find(
            "[stock-runtime-capture] relay-phase=complete") ==
            std::string::npos ||
        relay_snapshot.bytes.find(
            "[stock-runtime-capture] failed-operation=unavailable") ==
            std::string::npos ||
        relay_snapshot.bytes.find(
            "[stock-runtime-capture] native-error-code=unavailable") ==
            std::string::npos ||
        relay_snapshot.bytes.find(
            "[stock-runtime-capture] stop-requested=true") ==
            std::string::npos ||
        relay_snapshot.bytes.find(
            "[stock-runtime-capture] journal-publication-state="
            "published-complete") == std::string::npos ||
        relay_snapshot.bytes.find(
            "[stock-runtime-capture] metadata-publication-state="
            "published-complete") == std::string::npos ||
        !nonempty_regular_file(options.run_root / "transport-journal.jsonl") ||
        !nonempty_regular_file(options.run_root / "capture-metadata.json")) {
        std::cerr << "[functional-lifecycle-test] relay-exit="
                  << (relay_exit ? std::to_string(*relay_exit) : "missing")
                  << ";cleanup=" << (cleanup ? "exact" : "failed")
                  << ";relay-log=" << relay_snapshot.bytes << '\n';
        std::cerr << "[functional-lifecycle-test] result=finalization-failed\n";
        return 11;
    }

    std::cout << "[functional-lifecycle-test] lifecycle-clock="
                 "scaled-steady-clock\n"
              << "[functional-lifecycle-test] lifecycle-unit=milliseconds\n"
              << "[functional-lifecycle-test] requested-maximum-duration-ms="
              << options.limits.maximum_duration.count() << '\n'
              << "[functional-lifecycle-test] required-runtime-interval-ms="
              << goldsrc::kFunctionalRuntimeCaptureRequiredInterval.count()
              << '\n'
              << "[functional-lifecycle-test] client-map-entry-observed-ms="
                 "15000\n"
              << "[functional-lifecycle-test] functional-interval-completed-ms="
                 "30000\n"
              << "[functional-lifecycle-test] shutdown-requested-ms=30000\n"
              << "[functional-lifecycle-test] relay-stop-requested-ms=30001\n"
              << "[functional-lifecycle-test] relay-finalization-completed-ms="
                 "30002\n"
              << "[functional-lifecycle-test] stop-reason="
                 "functional-interval-complete\n"
              << "[functional-lifecycle-test] stock-shutdown-method="
                 "owned-process-terminate\n"
              << "[functional-lifecycle-test] peer-shutdown-after-relay-"
                 "finalization=true\n"
              << "[functional-lifecycle-test] stock-launch=absent\n"
              << "[functional-lifecycle-test] result=success\n";
    return 0;
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
                      "--functional-smoke --functional-confirmation-token "
                      "HLCLIENT_LOCAL_RESEARCH_COPY_SMOKE_V1 "
                      "[--validate-wrapper-startup] OR "
                      "--diagnose-server-profile "
                      "--confirmation-token HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1 "
                      "OR --private-diagnose-server-profile "
                      "--private-diagnostic-token "
                      "HLCLIENT_PRIVATE_HLDS_BANNER_DIAGNOSTIC_V1 "
                     "[--server-profile-id <legacy-stdio-hlds-banner-v1|"
                     "steam-hlds-10210-no-mode-banner-v1>] "
                      "--output-role <normal-campaign-run|pre-campaign-canary|"
                      "functional-runtime-capture|"
                      "server-profile-diagnostic|server-profile-private-diagnostic> "
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
    if (options->validate_functional_log_observation) {
        constexpr std::string_view accepted_fixture =
            "L synthetic: \"RuntimeName<17><STEAM_ID_PENDING><>\" connected, "
            "address \"127.0.0.1:27015\"\n"
            "L synthetic: \"RenamedAfterAuth<17><SYNTHETIC_ID><>\" entered the game\n"
            "L synthetic: \"RenamedAgain<17><SYNTHETIC_ID><>\" entered the game\n";
        constexpr std::string_view mismatched_fixture =
            "L synthetic: \"RuntimeName<1><STEAM_ID_PENDING><>\" connected, "
            "address \"127.0.0.1:27015\"\n"
            "L synthetic: \"OtherName<2><OTHER_ID><>\" entered the game\n";
        constexpr std::string_view reconnect_fixture =
            "L synthetic: \"RuntimeName<3><STEAM_ID_PENDING><>\" connected, "
            "address \"127.0.0.1:27015\"\n"
            "L synthetic: \"RuntimeName<3><SYNTHETIC_ID><>\" entered the game\n"
            "L synthetic: \"RuntimeName<3><SYNTHETIC_ID><>\" disconnected\n"
            "L synthetic: \"RuntimeName<3><SYNTHETIC_ID><>\" connected, "
            "address \"127.0.0.1:27015\"\n"
            "L synthetic: \"RuntimeName<3><SYNTHETIC_ID><>\" entered the game\n";
        constexpr std::string_view preconnection_fixture =
            "L synthetic: \"RuntimeName<4><SYNTHETIC_ID><>\" entered the game\n"
            "L synthetic: \"RuntimeName<4><STEAM_ID_PENDING><>\" connected, "
            "address \"127.0.0.1:27015\"\n";
        constexpr std::string_view project_complete_fixture =
            "[startup] application_entry_observed=true arguments_accepted=true\n"
            "[auth] provider=steam status=operation_started provider_begin_observed=true\n"
            "[auth] provider=steam status=api_initializing steam_api_init_attempted=true\n"
            "[auth] provider=steam status=api_initialized steam_api_initialized=true\n"
            "[auth] provider=steam status=material_acquired fresh_material_acquired=true\n"
            "[session] connect_sent=true\n"
            "[session] connection_accepted=true authentication_status=pending_or_unknown\n"
            "[signon] delta registry ready: schemas=7, fields=168\n"
            "[session] serverinfo_received=true schema_registry_received=true "
            "authentication_status=pending_or_unknown terminal_reason=delta_schemas_ready "
            "protocol=48 max-clients=8 game=valve map=maps/boot_camp.bsp\n"
            "[signon] terminal_reason=delta_schemas_ready\n";
        constexpr std::string_view project_incomplete_fixture =
            "[steam-auth] steam_api_initialized=true family=legacy\n"
            "[connect] connect_sent=true\n";
        constexpr std::string_view project_live_complete_fixture =
            "[startup] application_entry_observed=true arguments_accepted=true\n"
            "[auth] provider=steam status=operation_started provider_begin_observed=true\n"
            "[auth] provider=steam status=api_initializing steam_api_init_attempted=true\n"
            "[auth] provider=steam status=api_initialized steam_api_initialized=true\n"
            "[auth] provider=steam status=material_acquired fresh_material_acquired=true\n"
            "[session] connect_sent=true\n"
            "[session] connection_accepted=true\n"
            "[signon] protocol=48\n"
            "[signon] max-clients=8\n"
            "[signon] game=valve\n"
            "[signon] map=maps/boot_camp.bsp\n"
            "[signon] delta registry ready: schemas=7, fields=168\n"
            "[live-runtime] resource_continuation_sent=true "
            "spawn_request_transmitted=true spawn_request_acknowledged=true\n"
            "[live-runtime] signon_reply=sendents transmitted=true\n"
            "[live-runtime] signon_reply=sendents acknowledged=true\n"
            "[live-runtime] baseline_entities=64 instanced_baselines=0 "
            "service_payloads=4 applied_records=3\n"
            "[live-runtime] publication_revision=3 entities=12 "
            "canonical_hash=14031366596970435596 stable_interval_ms=2000 "
            "usercmd_transmitted=0\n"
            "[session] serverinfo_received=true schema_registry_received=true "
            "resource_continuation_sent=true live_service_payloads_received=true "
            "client_world_state_published=true\n"
            "[session] terminal_reason=live_runtime_state_ready\n";
        constexpr std::string_view project_usercmd_complete_fixture =
            "[startup] application_entry_observed=true arguments_accepted=true\n"
            "[auth] provider=steam status=operation_started provider_begin_observed=true\n"
            "[auth] provider=steam status=api_initializing steam_api_init_attempted=true\n"
            "[auth] provider=steam status=api_initialized steam_api_initialized=true\n"
            "[auth] provider=steam status=material_acquired fresh_material_acquired=true\n"
            "[session] connect_sent=true\n"
            "[session] connection_accepted=true\n"
            "[signon] protocol=48\n"
            "[signon] max-clients=8\n"
            "[signon] game=valve\n"
            "[signon] map=maps/boot_camp.bsp\n"
            "[signon] delta registry ready: schemas=7, fields=168\n"
            "[live-runtime] resource_continuation_sent=true spawn_request_transmitted=true spawn_request_acknowledged=true\n"
            "[live-runtime] signon_reply=sendents transmitted=true\n"
            "[live-runtime] signon_reply=sendents acknowledged=true\n"
            "[session] serverinfo_received=true schema_registry_received=true resource_continuation_sent=true live_service_payloads_received=true client_world_state_published=true\n"
            "[info] [live-usercmd-phase-observation] phase=forward complete_samples=10 origin_first=1.250000,2.000000,3.000000 origin_last=4.500000,2.000000,3.000000 velocity_first=10.000000,0.000000,0.000000 velocity_last=80.000000,0.000000,0.000000 projected_origin_min=1.250000 projected_origin_max=4.500000 projected_velocity_min=10.000000 projected_velocity_max=80.000000\n"
            "[info] [live-usercmd] counters generated=350 history=350 new=350 backup=72 sent_packets=175 server_samples=70\n"
            "[info] [live-usercmd-tx] netchan_sequence=400 first_new=350 last_new=350 new=1 backup=2\n"
            "[info] [live-usercmd] outcome=verified movement_verified=true\n"
            "[session] terminal_reason=live_usercmd_check_ready\n"
            "[info] [live-usercmd] result=fresh_project_client_usercmd_server_motion_verified\n";
        constexpr std::string_view project_visual_complete_fixture =
            "[startup] application_entry_observed=true arguments_accepted=true\n"
            "[auth] provider=steam status=operation_started provider_begin_observed=true\n"
            "[auth] provider=steam status=api_initializing steam_api_init_attempted=true\n"
            "[auth] provider=steam status=api_initialized steam_api_initialized=true\n"
            "[auth] provider=steam status=material_acquired fresh_material_acquired=true\n"
            "[session] connect_sent=true\n"
            "[session] connection_accepted=true\n"
            "[signon] protocol=48\n"
            "[signon] max-clients=8\n"
            "[signon] game=valve\n"
            "[signon] map=maps/boot_camp.bsp\n"
            "[signon] delta registry ready: schemas=7, fields=168\n"
            "[live-runtime] resource_continuation_sent=true spawn_request_transmitted=true spawn_request_acknowledged=true\n"
            "[live-runtime] signon_reply=sendents transmitted=true\n"
            "[live-runtime] signon_reply=sendents acknowledged=true\n"
            "[session] serverinfo_received=true schema_registry_received=true resource_continuation_sent=true live_service_payloads_received=true client_world_state_published=true\n"
            "[info] live_visual_camera_sample generation=1 publication_revision=40 source_sequence=80 server_time=4.0 origin=1,2,3 view_offset=0,0,28 eye=1,2,31 local_angles=0,0 server_angle_correction=false freshness=observed_in_record\n"
            "[info] live_visual_timing resource_preparation_start_ms=100 resource_preparation_end_ms=500 first_scene_draw_end_ms=700 first_scene_readback_end_ms=710 first_scene_present_end_ms=720 input_activation_ms=720 preactivation_commands=0\n"
            "live_visual_scheduler initialized=true active=true activation_time_ns=720000000 next_sample_time_ns=740000000 command_interval_ms=20 last_due_commands=1 catchup_cap=8 underlying_error=none\n"
            "live_visual_phase phase=forward generated=50 sent=49 fresh_server_samples=20\n"
            "live_visual_server_sample phase=forward ordinal=first generation=1 publication_revision=40 source_sequence=80 server_time=4 origin=1,2,3 velocity=10,0,0 view_offset=0,0,28 eye=1,2,31\n"
            "live_visual_framebuffer status=valid source=default_back read_framebuffer=0 read_buffer=1029 frame_revision=40 non_clear_pixels=100\n"
            "live_visual_control result=fresh_project_client_live_visual_control_integrated input=scripted-check map=maps/boot_camp.bsp view_policy=ordinary_receiving_client_vertical_offset_v1 generated=350 history=350 new=350 backup=72 sent_packets=175 server_samples=70 camera_samples=70 server_angle_corrections=0 camera_translation=true eye_span=3.25,0 first_eye=1,2,31 last_eye=4.25,2,31 canonical_revision=90 camera_revision=70 entity_frame_revision=60 static_resource_revision=1 decoded=36 resolved=20 submitted=18 unsupported=2 world_draws=25 world_uploads=3 studio_draws=16 sprite_draws=2 studio_uploads=4 sprite_uploads=1 framebuffer_observations=32 non_clear_framebuffers=32 changed_framebuffers=8 capture_acquired=0 capture_released=0 focus_losses=0 close=not_requested motion=verified client_world_state_published=true movement_verified=true view_status=ready gl_errors=0 cleanup=complete\n";
        constexpr std::string_view project_transition_failure_fixture =
            "[auth] provider=steam status=api_initialized steam_api_initialized=true\n"
            "[auth] provider=steam status=material_acquired fresh_material_acquired=true\n"
            "[session] connect_sent=true\n"
            "[session] connection_accepted=true authentication_status=pending_or_unknown\n"
            "[session] serverinfo_received=true authentication_status=pending_or_unknown\n"
            "[signon] delta registry ready: schemas=5, fields=87\n"
            "[session] schema_registry_received=true authentication_status=pending_or_unknown\n"
            "[session-progress] serverinfo=succeeded\n"
            "[session-progress] schema_registry=succeeded\n"
            "[session-progress] movevars=succeeded\n"
            "[session-progress] user_info=succeeded\n"
            "[session-progress] sendres_queued=succeeded\n"
            "[session-progress] sendres_transmitted=succeeded\n"
            "[session-progress] sendres_acknowledged=succeeded\n"
            "[session-progress] resource_transition=failed\n"
            "[transition-diagnostic] stage=resource_transition profile=stock_protocol_48_build_10210 expected_opcode=45 actual_opcode=unavailable cursor_byte_value=9 cursor=0:0 boundary=parser_buffer_start_unverified payload_ordinal=1 payload_ordinal_scope=resource_transition_nonempty_service_payload\n"
            "[transition-diagnostic] direction=server_to_client source_sequence=14 source_ack=8 source_reliable=true reassembled=false encoding=wire_uncompressed wire_size=23 decoded_size=23 pending_suffix_start=unavailable\n"
            "[transition-diagnostic] sendres_queued=true sendres_transmitted=true sendres_acknowledged=true request_reliable_generation=3 request_tx_sequence=8 request_ack_sequence=8 last_category=user_info_first_batch last_scope=initial_service_payload last_cursor=91:128 parser_error=wrong_opcode primary_error=transition_control_decode_failed\n";
        constexpr std::string_view project_transition_dispatch_failure_fixture =
            "[transition-diagnostic] stage=resource_transition profile=stock_protocol_48_build_10210 expected_opcode=45 actual_opcode=44 cursor_byte_value=44 cursor=3:0 boundary=validated_message_boundary payload_ordinal=2 payload_ordinal_scope=resource_transition_nonempty_service_payload\n"
            "[transition-diagnostic] direction=server_to_client source_sequence=16 source_ack=9 source_reliable=false reassembled=false encoding=bzip2 wire_size=31 decoded_size=19 pending_suffix_start=3:0\n"
            "[transition-diagnostic] sendres_queued=true sendres_transmitted=true sendres_acknowledged=true request_reliable_generation=4 request_tx_sequence=9 request_ack_sequence=9 last_category=runtime_control_nop last_scope=resource_transition_payload last_payload_ordinal=2 last_source_sequence=16 last_cursor=2:3 intermediate_message_count=3 intermediate_parser_error=unsupported_opcode parser_error=unsupported_opcode primary_error=intermediate_message_decode_failed\n";
        constexpr std::string_view project_response_ordering_fixture =
            "[resource-response-diagnostic] classification=pre_transmit_control rx_position=before_first_response_transmit payload_ordinal=1 source_sequence=17 source_ack=9 encoding=wire_uncompressed wire_size=1 decoded_size=1 cursor=0:0 boundary=validated_message_boundary actual_opcode=1 response_queued=1 response_transmitted=0 response_acknowledged=0 generation=unavailable first_tx_sequence=unavailable controls_consumed=1 pending_count=0 pending_bytes=0 last_handoff_cursor=1\n"
            "[resource] client response transmitted, generation=99 sequence=12\n";
        const auto accepted = observe_functional_client_log(accepted_fixture);
        const auto mismatched = observe_functional_client_log(
            mismatched_fixture);
        const auto reconnect = observe_functional_client_log(
            reconnect_fixture);
        const auto preconnection = observe_functional_client_log(
            preconnection_fixture);
        const auto project_complete = observe_project_client_signon_log(
            project_complete_fixture);
        const auto project_incomplete = observe_project_client_signon_log(
            project_incomplete_fixture);
        const auto project_live_complete = observe_project_client_signon_log(
            project_live_complete_fixture);
        const auto project_usercmd_complete = observe_project_client_signon_log(
            project_usercmd_complete_fixture);
        const auto project_usercmd_diagnostic =
            functional_diagnostic_excerpt(project_usercmd_complete_fixture);
        const auto project_visual_complete = observe_project_client_signon_log(
            project_visual_complete_fixture);
        std::string project_visual_terminal_only_fixture{
            project_visual_complete_fixture};
        constexpr std::string_view legacy_world_marker{
            " client_world_state_published=true"};
        if (const auto legacy_world = project_visual_terminal_only_fixture.find(
                legacy_world_marker);
            legacy_world != std::string::npos) {
            project_visual_terminal_only_fixture.erase(
                legacy_world, legacy_world_marker.size());
        }
        const auto project_visual_terminal_only =
            observe_project_client_signon_log(
                project_visual_terminal_only_fixture);
        std::string project_visual_numeric_bool_fixture{
            project_visual_terminal_only_fixture};
        for (const auto marker : {
                 std::string_view{" client_world_state_published=true"},
                 std::string_view{" movement_verified=true"}}) {
            if (const auto found = project_visual_numeric_bool_fixture.find(
                    marker);
                found != std::string::npos) {
                project_visual_numeric_bool_fixture.replace(
                    found, marker.size(),
                    marker.substr(0U, marker.rfind('=') + 1U));
                project_visual_numeric_bool_fixture.insert(
                    found + marker.rfind('=') + 1U, "1");
            }
        }
        const auto project_visual_numeric_bool =
            observe_project_client_signon_log(
                project_visual_numeric_bool_fixture);
        std::string project_visual_keyboard_fixture{
            project_visual_complete_fixture};
        const auto replace_all = [](std::string& target,
                                    const std::string_view before,
                                    const std::string_view after) {
            std::size_t position = 0U;
            while ((position = target.find(before, position)) !=
                   std::string::npos) {
                target.replace(position, before.size(), std::string{after});
                position += after.size();
            }
        };
        replace_all(project_visual_keyboard_fixture,
                    "input=scripted-check", "input=keyboard-mouse");
        replace_all(project_visual_keyboard_fixture,
                    "client_world_state_published=true",
                    "client_world_state_published=false");
        replace_all(project_visual_keyboard_fixture,
                    "movement_verified=true", "movement_verified=false");
        replace_all(project_visual_keyboard_fixture,
                    "close=not_requested", "close=timed_session_complete");
        const auto project_visual_keyboard =
            observe_project_client_signon_log(project_visual_keyboard_fixture);
        std::string project_visual_jump_duck_fixture{
            project_visual_complete_fixture};
        replace_all(project_visual_jump_duck_fixture,
                    "input=scripted-check", "input=scripted-jump-duck-check");
        replace_all(project_visual_jump_duck_fixture,
                    "result=fresh_project_client_live_visual_control_integrated",
                    "result=fresh_project_client_jump_duck_server_verified");
        project_visual_jump_duck_fixture +=
            "live_jump_duck input=scripted-jump-duck-check result=verified "
            "jump_observed=1 descent_observed=1 duck_observed=1 "
            "release_response_observed=1 jump_new_submitted=60 "
            "duck_new_submitted=75\n";
        const auto project_visual_jump_duck =
            observe_project_client_signon_log(project_visual_jump_duck_fixture);
        const auto project_visual_jump_duck_diagnostic =
            functional_diagnostic_excerpt(project_visual_jump_duck_fixture);
        auto project_visual_jump_only_fixture =
            project_visual_jump_duck_fixture;
        replace_all(project_visual_jump_only_fixture,
                    "result=fresh_project_client_jump_duck_server_verified",
                    "result=live_visual_scene_verified_input_partial");
        replace_all(project_visual_jump_only_fixture,
                    "result=verified jump_observed=1",
                    "result=jump_verified_duck_pending jump_observed=1");
        replace_all(project_visual_jump_only_fixture,
                    "duck_observed=1 release_response_observed=1",
                    "duck_observed=0 release_response_observed=0");
        const auto project_visual_jump_only =
            observe_project_client_signon_log(project_visual_jump_only_fixture);
        auto project_visual_jump_failed_fixture =
            project_visual_jump_duck_fixture;
        replace_all(project_visual_jump_failed_fixture,
                    "result=fresh_project_client_jump_duck_server_verified",
                    "result=live_visual_scene_verified_input_partial");
        project_visual_jump_failed_fixture.erase(
            project_visual_jump_failed_fixture.find("live_jump_duck input="));
        const auto project_visual_jump_failed =
            observe_project_client_signon_log(project_visual_jump_failed_fixture);
        std::string project_visual_speed_fixture{project_visual_complete_fixture};
        replace_all(project_visual_speed_fixture,
                    "input=scripted-check", "input=scripted-speed-check");
        replace_all(project_visual_speed_fixture,
                    "result=fresh_project_client_live_visual_control_integrated",
                    "result=live_normal_speed_and_shift_walk_verified");
        project_visual_speed_fixture +=
            "[info] live_speed_timing active_window_ms=5600 "
            "loading_presentations=18 active_presentations=320 "
            "frame_interval_median_ms=16 fresh_clientdata_samples=60\n"
            "[info] live_speed input=scripted-speed-check result=verified "
            "normal_requested=400 shift_multiplier=0.3\n";
        const auto project_visual_speed =
            observe_project_client_signon_log(project_visual_speed_fixture);
        const auto project_visual_speed_diagnostic =
            functional_diagnostic_excerpt(project_visual_speed_fixture);
        auto project_visual_speed_late_error_fixture =
            project_visual_speed_fixture;
        project_visual_speed_late_error_fixture +=
            "[error] later H1 terminal failed after retained samples\n";
        project_visual_speed_late_error_fixture += std::string(20'000U, 'x');
        const auto project_visual_speed_late_error_diagnostic =
            functional_diagnostic_excerpt(project_visual_speed_late_error_fixture);
        auto project_visual_speed_partial_fixture = project_visual_speed_fixture;
        replace_all(project_visual_speed_partial_fixture,
                    "result=live_normal_speed_and_shift_walk_verified",
                    "result=live_visual_scene_verified_input_partial");
        replace_all(project_visual_speed_partial_fixture,
                    "result=verified normal_requested=400",
                    "result=server_motion_unverified normal_requested=400");
        const auto project_visual_speed_partial =
            observe_project_client_signon_log(project_visual_speed_partial_fixture);
        auto project_visual_speed_failed_fixture = project_visual_speed_fixture;
        project_visual_speed_failed_fixture.erase(
            project_visual_speed_failed_fixture.find("live_speed input="));
        const auto project_visual_speed_failed =
            observe_project_client_signon_log(project_visual_speed_failed_fixture);
        auto project_visual_prediction_fixture = project_visual_speed_fixture;
        replace_all(project_visual_prediction_fixture,
                    "result=live_normal_speed_and_shift_walk_verified",
                    "result=live_local_prediction_and_reconciliation_verified");
        project_visual_prediction_fixture +=
            "[info] live_rx mode=reference driver_updates=200 "
            "driver_updates_post_input=140 receive_polls=190 "
            "receive_polls_post_input=130 owning_datagrams=50 "
            "owning_datagrams_post_input=38 payloads_created_post_input=24 "
            "payloads_consumed_post_input=24 records_committed_post_input=20 "
            "clientdata_committed_post_input=12 "
            "samples_delivered_post_input=12\n"
            "[info] live_prediction mode=reference "
            "result=live_local_prediction_and_reconciliation_verified "
            "state=active active_frames=250 accepted_corrections=40 "
            "replayed_commands=45 moving_presented_changes=80\n";
        const auto project_visual_prediction =
            observe_project_client_signon_log(project_visual_prediction_fixture);
        const auto project_visual_prediction_diagnostic =
            functional_diagnostic_excerpt(project_visual_prediction_fixture);
        auto project_visual_prediction_partial_fixture =
            project_visual_prediction_fixture;
        replace_all(project_visual_prediction_partial_fixture,
                    "result=live_local_prediction_and_reconciliation_verified",
                    "result=live_prediction_active_accuracy_or_coverage_limited");
        const auto project_visual_prediction_partial =
            observe_project_client_signon_log(
                project_visual_prediction_partial_fixture);
        auto project_visual_prediction_waiting_fixture =
            project_visual_prediction_fixture;
        replace_all(project_visual_prediction_waiting_fixture,
                    "result=live_local_prediction_and_reconciliation_verified",
                    "result=live_prediction_integrated_live_pending");
        const auto project_visual_prediction_waiting =
            observe_project_client_signon_log(
                project_visual_prediction_waiting_fixture);
        auto project_visual_prediction_failed_fixture =
            project_visual_prediction_fixture;
        project_visual_prediction_failed_fixture.erase(
            project_visual_prediction_failed_fixture.find("live_prediction mode="));
        const auto project_visual_prediction_failed =
            observe_project_client_signon_log(
                project_visual_prediction_failed_fixture);
        std::string project_visual_incomplete_fixture{
            project_visual_complete_fixture};
        const auto visual_draws = project_visual_incomplete_fixture.find(
            " world_draws=25");
        if (visual_draws != std::string::npos) {
            project_visual_incomplete_fixture.replace(
                visual_draws, std::string_view{" world_draws=25"}.size(),
                " world_draws=0");
        }
        const auto project_visual_incomplete =
            observe_project_client_signon_log(
                project_visual_incomplete_fixture);
        std::string project_visual_logging_fixture{
            project_visual_complete_fixture};
        if (const auto terminal = project_visual_logging_fixture.find(
                "[info] live_visual_timing ");
            terminal != std::string::npos) {
            std::string camera_flood;
            for (std::size_t index = 0U; index < 100U; ++index) {
                camera_flood +=
                    "[info] live_visual_camera_sample generation=1 "
                    "publication_revision=41 source_sequence=81 "
                    "server_time=4.1 origin=1,2,3 view_offset=0,0,28 "
                    "eye=1,2,31 local_angles=0,0 "
                    "server_angle_correction=false "
                    "freshness=observed_in_record\n";
            }
            project_visual_logging_fixture.insert(terminal, camera_flood);
        }
        project_visual_logging_fixture +=
            "[error] later visual publication failed after retained summary\n";
        project_visual_logging_fixture += std::string(20'000U, 'x');
        project_visual_logging_fixture += " failed bounded tail\n";
        const auto project_visual_diagnostic =
            functional_diagnostic_excerpt(project_visual_logging_fixture);
        const auto project_transition_failure =
            observe_project_client_signon_log(
                project_transition_failure_fixture);
        const auto project_transition_dispatch_failure =
            observe_project_client_signon_log(
                project_transition_dispatch_failure_fixture);
        const auto project_response_ordering =
            observe_project_client_signon_log(
                project_response_ordering_fixture);
        const bool valid =
            emits_jump_duck_native_status(
                ProjectClientLiveInput::scripted_jump_duck_check) &&
            !emits_jump_duck_native_status(
                ProjectClientLiveInput::scripted_speed_check) &&
            !emits_jump_duck_native_status(
                ProjectClientLiveInput::keyboard_mouse) &&
            emits_speed_native_status(
                ProjectClientLiveInput::scripted_speed_check) &&
            !emits_speed_native_status(
                ProjectClientLiveInput::scripted_jump_duck_check) &&
            !emits_speed_native_status(
                ProjectClientLiveInput::scripted_check) &&
            accepted.connection_accepted &&
            accepted.entered_game && !accepted.name_observed &&
            !accepted.connection_lost && mismatched.connection_accepted &&
            !mismatched.entered_game && reconnect.connection_accepted &&
            reconnect.entered_game && reconnect.connection_lost &&
            preconnection.connection_accepted &&
            !preconnection.entered_game && project_complete.complete(
                ProjectClientStop::delta_schemas) &&
            project_complete.application_entry_observed &&
            project_complete.arguments_accepted &&
            project_complete.provider_begin_observed &&
            project_complete.steam_api_init_attempted &&
            project_complete.serverinfo_protocol == 48U &&
            project_complete.serverinfo_max_clients == 8U &&
            project_complete.serverinfo_game == "valve" &&
            project_complete.serverinfo_map == "maps/boot_camp.bsp" &&
            project_complete.schema_count == 7U &&
            project_complete.schema_field_count == 168U &&
            !project_complete.authentication_failure &&
            !project_complete.connection_rejected &&
            !project_complete.timeout &&
            !project_incomplete.complete(ProjectClientStop::delta_schemas) &&
            project_live_complete.complete(
                ProjectClientStop::live_runtime_state) &&
            project_live_complete.spawn_request_acknowledged &&
            project_live_complete.signon_reply_transmitted &&
            project_live_complete.signon_reply_acknowledged &&
            project_live_complete.baseline_entity_count == 64U &&
            project_live_complete.service_payload_count == 4U &&
            project_live_complete.applied_runtime_record_count == 3U &&
            project_live_complete.entity_count == 12U &&
            project_live_complete.publication_revision == 3U &&
            project_live_complete.canonical_state_hash ==
                14'031'366'596'970'435'596ULL &&
            project_live_complete.stable_interval_ms == 2'000U &&
            project_live_complete.usercmd_zero_observed &&
            !project_live_complete.usercmd_transmitted &&
            project_usercmd_complete.complete(
                ProjectClientStop::live_usercmd_check) &&
            project_usercmd_complete.generated_usercmd_count == 350U &&
            project_usercmd_complete.new_usercmd_count == 350U &&
            project_usercmd_complete.backup_usercmd_count == 72U &&
            project_usercmd_complete.transmitted_usercmd_packet_count == 175U &&
            project_usercmd_complete.server_sample_count == 70U &&
            project_usercmd_diagnostic.find(
                "phase=forward complete_samples=10 origin_first=1.250000,2.000000,3.000000") !=
                std::string::npos &&
            project_usercmd_diagnostic.find(
                "[live-usercmd] counters generated=350 history=350 new=350") !=
                std::string::npos &&
            project_usercmd_diagnostic.find(
                "[live-usercmd-tx] netchan_sequence=400 first_new=350 last_new=350") !=
                std::string::npos &&
            project_visual_complete.complete(
                ProjectClientStop::live_visual_control) &&
            project_visual_complete.live_visual_input ==
                ProjectClientLiveInput::scripted_check &&
            !project_visual_complete.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::keyboard_mouse) &&
            project_visual_keyboard.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::keyboard_mouse) &&
            !project_visual_keyboard.client_world_state_published &&
            project_visual_keyboard.live_visual_verified &&
            project_visual_jump_duck.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_jump_duck_check) &&
            project_visual_jump_duck.jump_new_submitted == 60U &&
            project_visual_jump_duck.duck_new_submitted == 75U &&
            project_visual_jump_duck_diagnostic.find(
                "live_jump_duck input=scripted-jump-duck-check result=verified") !=
                std::string::npos &&
            !project_visual_jump_only.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_jump_duck_check) &&
            project_visual_jump_only.jump_observed == true &&
            project_visual_jump_only.duck_observed == false &&
            !project_visual_jump_failed.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_jump_duck_check) &&
            !project_visual_jump_failed.jump_duck_result &&
            project_visual_speed.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_speed_check) &&
            project_visual_speed.speed_result == "verified" &&
            project_visual_prediction.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_speed_check, true) &&
            project_visual_prediction.prediction_result ==
                "live_local_prediction_and_reconciliation_verified" &&
            project_visual_prediction.rx_owning_datagrams_post_input == 38U &&
            project_visual_prediction.rx_payloads_created_post_input == 24U &&
            project_visual_prediction.rx_payloads_consumed_post_input == 24U &&
            project_visual_prediction.rx_clientdata_committed_post_input == 12U &&
            project_visual_prediction.rx_samples_delivered_post_input == 12U &&
            project_visual_prediction_diagnostic.find(
                "live_rx mode=reference driver_updates=200") !=
                std::string::npos &&
            project_visual_prediction_diagnostic.find(
                "live_prediction mode=reference result=") !=
                std::string::npos &&
            !project_visual_prediction_partial.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_speed_check, true) &&
            project_visual_prediction_waiting.prediction_result ==
                "live_prediction_integrated_live_pending" &&
            !project_visual_prediction_waiting.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_speed_check, true) &&
            !project_visual_prediction_failed.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_speed_check, true) &&
            project_visual_speed_diagnostic.find(
                "live_speed_timing active_window_ms=5600") !=
                std::string::npos &&
            project_visual_speed_diagnostic.find(
                "live_speed input=scripted-speed-check result=verified") !=
                std::string::npos &&
            project_visual_speed_late_error_diagnostic.find(
                "live_speed_timing active_window_ms=5600") !=
                std::string::npos &&
            project_visual_speed_late_error_diagnostic.find(
                "live_speed input=scripted-speed-check result=verified") !=
                std::string::npos &&
            !project_visual_speed_partial.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_speed_check) &&
            project_visual_speed_partial.speed_result ==
                "server_motion_unverified" &&
            !project_visual_speed_failed.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_speed_check) &&
            !project_visual_speed_failed.speed_result &&
            !project_visual_keyboard.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_check) &&
            project_visual_complete.live_visual_verified &&
            project_visual_complete.generated_usercmd_count == 350U &&
            project_visual_complete.new_usercmd_count == 350U &&
            project_visual_complete.backup_usercmd_count == 72U &&
            project_visual_complete.transmitted_usercmd_packet_count == 175U &&
            project_visual_complete.server_sample_count == 70U &&
            project_visual_terminal_only.complete(
                ProjectClientStop::live_visual_control) &&
            project_visual_terminal_only.client_world_state_published &&
            project_visual_terminal_only.live_usercmd_motion_verified &&
            project_visual_numeric_bool.complete(
                ProjectClientStop::live_visual_control) &&
            project_visual_numeric_bool.client_world_state_published &&
            project_visual_numeric_bool.live_usercmd_motion_verified &&
            !project_visual_incomplete.complete(
                ProjectClientStop::live_visual_control) &&
            project_visual_diagnostic.find(
                "live_visual_camera_sample generation=1") !=
                std::string::npos &&
            project_visual_diagnostic.find(
                "live_visual_control result=fresh_project_client_live_visual_control_integrated") !=
                std::string::npos &&
            project_visual_diagnostic.find(
                "live_visual_timing resource_preparation_start_ms=100") !=
                std::string::npos &&
            project_visual_diagnostic.find(
                "live_visual_scheduler initialized=true") !=
                std::string::npos &&
            project_visual_diagnostic.find(
                "live_visual_framebuffer status=valid") !=
                std::string::npos &&
            project_visual_diagnostic.find(
                "later visual publication failed") != std::string::npos &&
            project_visual_diagnostic.size() <= 16U * 1'024U &&
            project_transition_failure.serverinfo_received &&
            project_transition_failure.schema_registry_received &&
            project_transition_failure.schema_count == 5U &&
            project_transition_failure.schema_field_count == 87U &&
            project_transition_failure.protocol_progress.serverinfo ==
                "succeeded" &&
            project_transition_failure.protocol_progress.schema_registry ==
                "succeeded" &&
            project_transition_failure.protocol_progress.movevars ==
                "succeeded" &&
            project_transition_failure.protocol_progress.user_info ==
                "succeeded" &&
            project_transition_failure.protocol_progress.sendres_acknowledged ==
                "succeeded" &&
            project_transition_failure.protocol_progress.resource_transition ==
                "failed" &&
            project_transition_failure.protocol_progress.resource_list ==
                "not_reached" &&
            project_transition_failure.transition_failure.present &&
            project_transition_failure.transition_failure.expected_opcode ==
                45U &&
            !project_transition_failure.transition_failure.actual_opcode &&
            project_transition_failure.transition_failure.cursor_byte_value ==
                9U &&
            project_transition_failure.transition_failure.source_sequence ==
                14U &&
            project_transition_failure.transition_failure.encoding ==
                "wire_uncompressed" &&
            project_transition_failure.transition_failure.parser_error ==
                "wrong_opcode" &&
            project_transition_dispatch_failure.transition_failure.present &&
            project_transition_dispatch_failure.transition_failure.actual_opcode ==
                44U &&
            project_transition_dispatch_failure.transition_failure.cursor ==
                "3:0" &&
            project_transition_dispatch_failure.transition_failure.boundary ==
                "validated_message_boundary" &&
            project_transition_dispatch_failure.transition_failure.payload_ordinal ==
                2U &&
            project_transition_dispatch_failure.transition_failure.encoding ==
                "bzip2" &&
            project_transition_dispatch_failure.transition_failure
                    .pending_suffix_start == "3:0" &&
            project_transition_dispatch_failure.transition_failure.parser_error ==
                "unsupported_opcode" &&
            project_transition_dispatch_failure.transition_failure.primary_error ==
                "intermediate_message_decode_failed" &&
            project_response_ordering.response_payload_diagnostic.present &&
            project_response_ordering.response_payload_diagnostic.classification ==
                "pre_transmit_control" &&
            project_response_ordering.response_payload_diagnostic.rx_position ==
                "before_first_response_transmit" &&
            project_response_ordering.response_payload_diagnostic.actual_opcode ==
                1U &&
            project_response_ordering.response_payload_diagnostic
                    .controls_consumed == 1U &&
            !project_response_ordering.response_payload_diagnostic
                 .reliable_generation &&
            !project_response_ordering.response_payload_diagnostic
                 .first_transmit_sequence;
        print_key_value("functional-log-observation",
                        valid ? "valid" : "invalid");
        print_key_value("stock-processes-started", "0");
        print_key_value("capture-files-written", "0");
        print_key_value("result", valid ? "success" : "failed");
        return valid ? 0 : 5;
    }
    if (options->validate_functional_lifecycle) {
        return run_functional_lifecycle_validation(*options);
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
    if (options->validate_wrapper_startup) {
        const bool cleanup_signalled =
            signal_wrapper_empty_cleanup_capabilities(*options);
        const bool functional_capture = options->output_role &&
            *options->output_role == goldsrc::StockRuntimeCaptureOutputRole::
                functional_runtime_capture;
        print_key_value("orchestrator", cleanup_signalled ? "success" : "failed");
        print_key_value("failure-category", cleanup_signalled
            ? "none" : "wrapper-cleanup-signal-failed");
        print_key_value("startup-boundary", cleanup_signalled
            ? "acknowledged" : "cleanup-failed");
        print_key_value("mode", functional_capture
            ? "functional_runtime_capture_v1"
            : "local_research_copy_smoke_v1");
        print_key_value("purpose", functional_capture
            ? "functional_runtime_capture_startup_probe"
            : "functional_smoke_startup_probe");
        print_key_value("evidence-eligible", "false");
        print_key_value("processes-started", "0");
        print_key_value("capture-files-written", "0");
        print_key_value("job-cleanup", "exact");
        print_key_value("result", cleanup_signalled ? "success" : "failed");
        return cleanup_signalled ? 0 : 4;
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
        goldsrc::StockActiveCapturePreflightAttestation attestation;
        attestation.elevated = true;
        attestation.binary_profile_valid = true;
        attestation.app_manifest_valid = true;
        attestation.dynamic_wfp_session = true;
        attestation.ipv4_loopback_allowed =
            environment.environment->canary.ipv4_loopback_allowed;
        attestation.ipv6_loopback_allowed =
            environment.environment->canary.ipv6_loopback_allowed;
        attestation.ipv6_capability_available =
            environment.environment->canary.ipv6_loopback_allowed;
        attestation.non_loopback_denied_by_os =
            environment.environment->canary.non_loopback_os_denied;
        attestation.isolation_cleanup_exact = true;
        attestation.timestamp_category = "current-session";
        attestation.success = true;
        if (!goldsrc::validate_stock_active_capture_preflight_attestation(
                attestation)) {
            print_key_value("active-environment", "invalid");
            print_key_value("failure-category", "preflight-attestation-invalid");
            print_key_value("stock-processes-started", "0");
            print_key_value("capture-files-written", "0");
            print_key_value("result", "failed");
            return 1;
        }
        print_key_value("active-environment", "valid");
        print_key_value("preflight-schema", std::string{
            goldsrc::kStockActiveCapturePreflightAttestationSchema});
        print_key_value("elevation-status", "verified");
        print_key_value("isolation-canary", "success");
        print_key_value("binary-profile", "valid");
        print_key_value("app-manifest", "valid");
        print_key_value("wfp-session", "dynamic");
        print_key_value("ipv4-loopback", "allowed");
        print_key_value(
            "ipv6-loopback",
            environment.environment->canary.ipv6_loopback_allowed
                ? "allowed" : "capability-unavailable");
        print_key_value("non-loopback-canary", "denied-os-classified");
        print_key_value("isolation-cleanup", "exact");
        print_key_value("timestamp-category", "current-session");
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
    print_key_value("server-profile-id",
                    std::string{windows::to_string(summary.server_profile_id)});
    print_key_value("client-ready", summary.client_ready ? "true" : "false");
    if (options->functional_smoke) {
        const bool live_project_mode = options->project_client_stock_signon &&
            options->project_client_stop != ProjectClientStop::delta_schemas;
        const bool usercmd_project_mode = options->project_client_stock_signon &&
            options->project_client_stop ==
                ProjectClientStop::live_usercmd_check;
        const bool visual_project_mode = options->project_client_stock_signon &&
            options->project_client_stop ==
                ProjectClientStop::live_visual_control;
        print_key_value("mode", options->project_client_stock_signon
            ? visual_project_mode
                ? "project_client_live_visual_control_v1"
              : usercmd_project_mode
                ? "project_client_live_usercmd_check_v1"
              : live_project_mode
                ? "project_client_live_runtime_state_v1"
                : "project_client_stock_signon_v1"
            : "local_research_copy_smoke_v1");
        print_key_value("purpose", options->project_client_stock_signon
            ? visual_project_mode
                ? "fresh_project_client_live_visual_control"
              : usercmd_project_mode
                ? "fresh_project_client_usercmd_server_motion"
              : live_project_mode
                ? "fresh_project_client_live_runtime_state"
                : "fresh_project_client_stock_signon"
            : "functional_smoke");
        print_key_value("evidence-eligible",
                        options->project_client_stock_signon ? "true" : "false");
        print_key_value("route", "direct_loopback");
        print_key_value("stable-duration-ms",
                        std::to_string(summary.stable_duration_ms));
        print_key_value("server-process-created",
                        summary.functional_server_process_id != 0U
                            ? "observed" : "unknown");
        print_key_value("server-process-id",
                        std::to_string(summary.functional_server_process_id));
        print_key_value("client-process-created",
                        summary.functional_client_process_created
                            ? "observed" : "unknown");
        print_key_value("client-process-id",
                        std::to_string(summary.functional_client_process_id));
        print_key_value("client-image-identity",
                        summary.functional_client_image_identity_verified
                            ? "verified" : summary.functional_client_process_created
                                ? "mismatch-or-query-failed" : "not-reached");
        print_key_value("client-resume-result",
                        summary.functional_client_resume_succeeded
                            ? "succeeded" : summary.functional_client_image_identity_verified
                                ? "failed" : "not-reached");
        print_key_value("client-initialized",
                        summary.functional_server_connection_accepted ||
                                summary.client_ready
                            ? "observed" : "unknown");
        print_key_value("connect-requested",
                        summary.functional_connect_requested
                            ? "observed" : "unknown");
        print_key_value("connection-status",
                        functional_connection_status(summary));
        print_key_value("client-map-entry-status",
                        summary.client_ready ? "observed" : "unknown");
        print_key_value(
            "map-entry-source",
            summary.client_ready
                ? options->project_client_stock_signon
                    ? visual_project_mode
                        ? "owned-project-client-live-visual-control"
                      : usercmd_project_mode
                        ? "owned-project-client-live-usercmd-check"
                      : live_project_mode
                        ? "owned-project-client-live-runtime-state"
                        : "owned-project-client-serverinfo-and-delta-registry"
                    : "owned-server-log-correlated-connected-and-entered"
                : "unknown");
        print_key_value("last-confirmed-stage",
                        functional_last_confirmed_stage(summary));
        print_key_value("client-steam-argument",
                        options->project_client_stock_signon
                            ? "not-applicable" : "present");
        print_key_value("server-logging", "enabled-before-map");
        print_key_value("client-connect-port",
                        std::to_string(options->server_port));
        print_key_value("steam-authentication-error-observed",
                        summary.functional_steam_authentication_error_observed
                            ? "true" : "false");
        if (options->project_client_stock_signon) {
            print_key_value("application-entry-observed",
                            summary.project_application_entry_observed
                                ? "succeeded" : "not-reached");
            print_key_value("arguments-accepted",
                            summary.project_arguments_accepted
                                ? "succeeded" : "not-reached");
            print_key_value("provider-begin-observed",
                            summary.project_provider_begin_observed
                                ? "succeeded" : "not-reached");
            print_key_value("steam-api-init-attempted",
                            summary.project_steam_api_init_attempted
                                ? "succeeded" : "not-reached");
            print_key_value("steam-api-initialized",
                            summary.project_steam_api_initialized
                                ? "true" : "false");
            print_key_value("fresh-material-acquired",
                            summary.project_fresh_material_acquired
                                ? "true" : "false");
            print_key_value("connect-sent",
                            summary.project_connect_sent ? "true" : "false");
            print_key_value("connection-accepted",
                            summary.project_connection_accepted
                                ? "true" : "false");
            print_key_value("serverinfo-received",
                            summary.project_serverinfo_received
                                ? "true" : "false");
            print_key_value("schema-registry-received",
                            summary.project_schema_registry_received
                                ? "true" : "false");
            print_key_value("resource-continuation-sent",
                            summary.project_resource_continuation_sent
                                ? "true" : "false");
            print_key_value("spawn-request-transmitted",
                            summary.project_spawn_request_transmitted
                                ? "true" : "false");
            print_key_value("spawn-request-acknowledged",
                            summary.project_spawn_request_acknowledged
                                ? "true" : "false");
            print_key_value("signon-reply-transmitted",
                            summary.project_signon_reply_transmitted
                                ? "true" : "false");
            print_key_value("signon-reply-acknowledged",
                            summary.project_signon_reply_acknowledged
                                ? "true" : "false");
            print_key_value("live-service-payloads-received",
                            summary.project_live_service_payloads_received
                                ? "true" : "false");
            print_key_value("client-world-state-published",
                            summary.project_client_world_state_published
                                ? "true" : "false");
            print_key_value("usercmd-transmitted",
                            summary.project_usercmd_zero_observed
                                ? "0"
                                : summary.project_usercmd_transmitted
                                    ? "true" : "not-observed");
            print_key_value("usercmd-movement-verified",
                            summary.project_usercmd_movement_verified
                                ? "true" : "false");
            print_key_value("live-visual-verified",
                            summary.project_live_visual_verified
                                ? "true" : "false");
            if (emits_jump_duck_native_status(
                    options->project_client_live_input)) {
                print_key_value("jump-duck-result",
                                summary.project_jump_duck_result.value_or(
                                    "unavailable"));
                const auto optional_bool = [](const std::optional<bool> value)
                    -> std::string_view {
                    return value ? (*value ? "true" : "false") : "unavailable";
                };
                print_key_value("jump-observed",
                                optional_bool(summary.project_jump_observed));
                print_key_value("descent-observed",
                                optional_bool(summary.project_descent_observed));
                print_key_value("duck-observed",
                                optional_bool(summary.project_duck_observed));
                print_key_value("release-response-observed",
                                optional_bool(summary.project_release_response_observed));
                print_key_value("jump-new-submitted",
                                summary.project_jump_new_submitted
                                    ? std::to_string(*summary.project_jump_new_submitted)
                                    : "unavailable");
                print_key_value("duck-new-submitted",
                                summary.project_duck_new_submitted
                                    ? std::to_string(*summary.project_duck_new_submitted)
                                    : "unavailable");
            }
            if (emits_speed_native_status(
                    options->project_client_live_input))
                print_key_value("speed-result",
                                summary.project_speed_result.value_or(
                                    "unavailable"));
            if (options->project_client_reference_prediction)
                print_key_value("prediction-result",
                                summary.project_prediction_result.value_or(
                                    "unavailable"));
            print_key_value("usercmd-generated",
                            summary.project_usercmd_generated_count
                                ? std::to_string(
                                      *summary.project_usercmd_generated_count)
                                : "not-observed");
            print_key_value("usercmd-new",
                            summary.project_usercmd_new_count
                                ? std::to_string(*summary.project_usercmd_new_count)
                                : "not-observed");
            print_key_value("usercmd-backup",
                            summary.project_usercmd_backup_count
                                ? std::to_string(
                                      *summary.project_usercmd_backup_count)
                                : "not-observed");
            print_key_value("usercmd-packets",
                            summary.project_usercmd_packet_count
                                ? std::to_string(
                                      *summary.project_usercmd_packet_count)
                                : "not-observed");
            print_key_value("usercmd-server-samples",
                            summary.project_usercmd_server_sample_count
                                ? std::to_string(
                                      *summary.project_usercmd_server_sample_count)
                                : "not-observed");
            print_key_value("authentication-status",
                            summary.functional_steam_authentication_error_observed
                                ? "failed"
                                : summary.project_connection_accepted
                                     ? "pending-or-unknown" : "not-reached");
            print_key_value("serverinfo-protocol",
                            summary.project_serverinfo_protocol
                                ? std::to_string(*summary.project_serverinfo_protocol)
                                : "not-observed");
            print_key_value("serverinfo-max-clients",
                            summary.project_serverinfo_max_clients
                                ? std::to_string(*summary.project_serverinfo_max_clients)
                                : "not-observed");
            print_key_value("serverinfo-game",
                            summary.project_serverinfo_game
                                ? *summary.project_serverinfo_game : "not-observed");
            print_key_value("serverinfo-map",
                            summary.project_serverinfo_map
                                ? *summary.project_serverinfo_map : "not-observed");
            print_key_value("schema-count", summary.project_schema_count
                ? std::to_string(*summary.project_schema_count) : "not-observed");
            print_key_value("schema-field-count",
                            summary.project_schema_field_count
                                ? std::to_string(*summary.project_schema_field_count)
                                : "not-observed");
            print_key_value("baseline-entity-count",
                            summary.project_baseline_entity_count
                                ? std::to_string(
                                      *summary.project_baseline_entity_count)
                                : "not-observed");
            print_key_value("service-payload-count",
                            summary.project_service_payload_count
                                ? std::to_string(
                                      *summary.project_service_payload_count)
                                : "not-observed");
            print_key_value("applied-runtime-record-count",
                            summary.project_applied_runtime_record_count
                                ? std::to_string(
                                      *summary.project_applied_runtime_record_count)
                                : "not-observed");
            print_key_value("world-entity-count",
                            summary.project_entity_count
                                ? std::to_string(*summary.project_entity_count)
                                : "not-observed");
            print_key_value("publication-revision",
                            summary.project_publication_revision
                                ? std::to_string(
                                      *summary.project_publication_revision)
                                : "not-observed");
            print_key_value("canonical-state-hash",
                            summary.project_canonical_state_hash
                                ? std::to_string(
                                      *summary.project_canonical_state_hash)
                                : "not-observed");
            print_key_value("stable-runtime-interval-ms",
                            summary.project_stable_interval_ms
                                ? std::to_string(
                                      *summary.project_stable_interval_ms)
                                : "not-observed");
        }
        print_key_value("diagnostic-publication",
                        summary.functional_diagnostic_published
                            ? "complete" : "incomplete");
        print_key_value("client-name-observed",
                        summary.functional_client_name_observed
                            ? "true" : "false");
        print_key_value("client-entered-game-observed",
                        summary.functional_client_entered_game_observed
                            ? "true" : "false");
        print_key_value("client-running-at-readiness-deadline",
                        summary.functional_client_running_at_readiness_deadline
                            ? "true" : "false");
        print_key_value("client-exit-code", summary.client_exit_code
            ? std::to_string(*summary.client_exit_code) : "not-observed");
        print_key_value("client-exit-code-hex", summary.client_exit_code
            ? relay_exit_code_hex(*summary.client_exit_code) : "not-observed");
        print_key_value("client-wait-result", summary.client_wait_result);
        print_key_value("client-wait-native-error",
                        summary.client_wait_native_error
                            ? std::to_string(*summary.client_wait_native_error)
                            : "not-observed");
        print_key_value("server-exit-code", summary.server_exit_code
            ? std::to_string(*summary.server_exit_code) : "not-observed");
        print_key_value("external-steam-state",
                        options->project_client_stock_signon
                            ? "current_user_session_used" : "not_assessed");
        print_key_value("external-steam-state-policy",
                        options->project_client_stock_signon
                            ? "provider_runtime_only_no_credentials_or_material_retained"
                            : "functional_observation_only");
    }
    if (options->output_role && *options->output_role == goldsrc::
            StockRuntimeCaptureOutputRole::functional_runtime_capture) {
        print_key_value("lifecycle-clock", "steady-clock");
        print_key_value("lifecycle-unit", "milliseconds");
        print_key_value("requested-maximum-duration-ms",
                        std::to_string(options->limits.maximum_duration.count()));
        print_key_value("required-runtime-interval-ms", std::to_string(
            goldsrc::kFunctionalRuntimeCaptureRequiredInterval.count()));
        print_key_value("client-map-entry-observed-ms",
                        std::to_string(summary.client_map_entry_observed_ms));
        print_key_value("functional-interval-completed-ms", std::to_string(
            summary.functional_interval_completed_ms));
        print_key_value("shutdown-requested-ms",
                        std::to_string(summary.shutdown_requested_ms));
        print_key_value("relay-stop-requested-ms",
                        std::to_string(summary.relay_stop_requested_ms));
        print_key_value("relay-finalization-completed-ms", std::to_string(
            summary.relay_finalization_completed_ms));
        print_key_value("stop-reason", summary.stop_reason);
        print_key_value("stock-shutdown-method", summary.stock_shutdown_method);
        print_key_value("relay-phase", summary.relay_phase);
        print_key_value("failed-operation", summary.relay_failed_operation);
        print_key_value(
            "native-error-domain", summary.relay_native_error_domain);
        print_key_value("native-error-code", summary.relay_native_error_code);
        print_key_value("relay-exit-code", summary.relay_exit_code
            ? std::to_string(*summary.relay_exit_code) : "unavailable");
        print_key_value("relay-exit-code-hex", summary.relay_exit_code
            ? relay_exit_code_hex(*summary.relay_exit_code) : "unavailable");
        print_key_value("wait-result", summary.relay_wait_result);
        print_key_value("stop-requested", summary.relay_stop_observed);
        print_key_value("journal-publication-state",
                        summary.relay_journal_publication_state);
        print_key_value("metadata-publication-state",
                        summary.relay_metadata_publication_state);
    }
    print_key_value("bounded-transport-complete",
                    summary.bounded_transport_complete ? "true" : "false");
    print_key_value("connection-generations",
                    std::to_string(summary.connection_generations));
    print_key_value("generation-distinct",
                    summary.generation_distinct ? "true" : "false");
    print_key_value("candidate-conflict",
                    options->scenario == "reconnect"
                        ? "evidence-pending" : "not-applicable");
    print_key_value("job-cleanup", summary.cleanup_exact ? "exact" : "incomplete");
    print_key_value(
        "writer-trace-prelaunch-ready",
        summary.writer_trace_prelaunch_ready ? "true" : "false");
    print_key_value(
        "writer-trace-launch-released",
        summary.writer_trace_launch_released ? "true" : "false");
    print_key_value(
        "writer-trace-stock-processes-stopped",
        summary.writer_trace_stock_processes_stopped ? "true" : "false");
    print_key_value(
        "writer-trace-clock-frequency",
        std::to_string(summary.writer_trace_clock_frequency));
    print_key_value(
        "writer-trace-stock-process-created-ticks",
        std::to_string(summary.writer_trace_stock_process_created_ticks));
    print_key_value(
        "writer-trace-stock-processes-stopped-ticks",
        std::to_string(summary.writer_trace_stock_processes_stopped_ticks));
    if (summary.server_readiness) {
        const auto& readiness = *summary.server_readiness;
        print_key_value("server-readiness-status",
                        std::string{windows::to_string(readiness.status)});
        print_key_value("server-readiness-endpoint-proof",
                        std::string{windows::to_string(readiness.endpoint_proof_source)});
        print_key_value("server-readiness-map-proof",
                        std::string{windows::to_string(readiness.map_proof_source)});
        print_key_value("server-readiness-owned-process",
                        readiness.owned_process_running ? "true" : "false");
        print_key_value("server-readiness-process-identity",
                        readiness.process_identity_matches ? "match" : "mismatch");
        print_key_value("server-readiness-endpoint-owner",
                        readiness.endpoint_owner_matches ? "match" : "mismatch");
        print_key_value("server-readiness-endpoint-address",
                        readiness.endpoint_address_matches ? "match" : "mismatch");
        print_key_value("server-readiness-endpoint-port",
                        readiness.endpoint_port_matches ? "match" : "mismatch");
        print_key_value("server-readiness-response-source",
                        readiness.response_source_matches ? "match" : "mismatch");
        print_key_value("server-readiness-map", readiness.map_matches ? "match" : "mismatch");
        print_key_value("server-readiness-game", readiness.game_matches ? "match" : "mismatch");
        print_key_value("server-readiness-query-attempts",
                        std::to_string(readiness.request_attempt_count));
        print_key_value("server-readiness-response-bytes",
                        std::to_string(readiness.response_byte_count));
    }
    if (options->diagnose_server_profile &&
        summary.server_profile_diagnostic) {
        const auto& diagnostic = *summary.server_profile_diagnostic;
        print_key_value("server-profile-parse-status", std::string{
            windows::to_string(diagnostic.parse_status)});
        print_key_value("server-profile-mismatch-field", std::string{
            windows::to_string(diagnostic.mismatch_field)});
        print_key_value("server-profile-engine-version-status", std::string{
            windows::to_string(diagnostic.engine_version.status)});
        print_key_value("server-profile-runtime-mode-status", std::string{
            windows::to_string(diagnostic.runtime_mode.status)});
        print_key_value("server-profile-game-status", std::string{
            windows::to_string(diagnostic.game.status)});
        print_key_value("server-profile-protocol-status", std::string{
            windows::to_string(diagnostic.protocol.status)});
        print_key_value("server-profile-build-status", std::string{
            windows::to_string(diagnostic.build.status)});
        print_key_value("server-profile-endpoint-address-status", std::string{
            windows::to_string(diagnostic.endpoint_address.status)});
        print_key_value("server-profile-endpoint-port-status", std::string{
            windows::to_string(diagnostic.endpoint_port.status)});
        print_key_value("server-profile-map-status", std::string{
            windows::to_string(diagnostic.map.status)});
        print_key_value("server-profile-duplicate-fields",
                        std::to_string(diagnostic.duplicate_field_count));
        print_key_value("server-profile-process-log-truncated",
                        diagnostic.process_log_truncated ? "true" : "false");
        print_key_value("server-profile-endpoint-address-category", std::string{
            windows::to_string(diagnostic.endpoint_address_category)});
        print_key_value("server-profile-runtime-mode-category", std::string{
            windows::to_string(diagnostic.runtime_mode_category)});
        print_key_value("server-profile-observed-byte-count",
                        std::to_string(diagnostic.observed_byte_count));
        print_key_value("server-profile-observed-line-count",
                        std::to_string(diagnostic.observed_line_count));
        if (diagnostic.observed_engine_version) {
            print_key_value("server-profile-observed-engine-version",
                windows::to_string(*diagnostic.observed_engine_version));
        }
        if (diagnostic.observed_protocol) {
            print_key_value("server-profile-observed-protocol",
                            std::to_string(*diagnostic.observed_protocol));
        }
        if (diagnostic.observed_build) {
            print_key_value("server-profile-observed-build",
                            std::to_string(*diagnostic.observed_build));
        }
        const auto diagnostic_result = diagnostic.parse_status ==
                windows::HldsRuntimeProfileParseStatus::valid
            ? "supported"
            : diagnostic.parse_status == windows::
                    HldsRuntimeProfileParseStatus::profile_mismatch
                ? "stock_server_profile_not_supported"
                : windows::to_string(diagnostic.parse_status);
        print_key_value("server-profile-result", diagnostic_result);
    }
    print_key_value("result", summary.success ? "success" : "failed");
    return summary.success ? 0 : 1;
}
