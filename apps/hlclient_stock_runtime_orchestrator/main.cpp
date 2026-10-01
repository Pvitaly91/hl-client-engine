#include <hlclient/goldsrc/stock_runtime_capture.hpp>
#include <hlclient/core/remote_audio_peer_plan.hpp>
#include <hlclient/core/manual_session_timing.hpp>
#include <hlclient/goldsrc/stock_runtime_reconnect_lifecycle.hpp>
#include <hlclient/platform/windows/binary_identity.hpp>
#include <hlclient/platform/windows/network_isolation.hpp>
#include <hlclient/platform/windows/process_orchestrator.hpp>
#include <hlclient/platform/windows/secure_output.hpp>
#include <hlclient/platform/windows/stock_research_copy.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
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
    scripted_weapon_check,
    scripted_fire_reload_check,
    scripted_fire_reload_presentation_check,
    scripted_damage_respawn_check,
    keyboard_mouse,
};

constexpr std::array<std::string_view, 7U> kWeaponPredictionFlags{
    "server_fire_verified", "server_reload_verified", "glock_fire_animation_presented",
    "glock_reload_animation_presented", "glock_recoil_presented",
    "crowbar_swing_presented", "hud_server_state_updated"};
constexpr std::array<std::string_view, 131U> kApplicationMetrics{
    "result", "primary_error", "runtime_error", "parser_error", "opcode",
    "cursor", "record", "source_sequence", "scripted_coverage",
    "prediction_coverage", "inventory_notifications", "feedback_rows",
    "error_domain",
    "failure_stage",
    "replay_error",
    "control_error",
    "clientdata_error",
    "delta_error",
    "module_error",
    "generation",
    "life_epoch",
    "life_state",
    "record_identity",
    "carrier_ack",
    "reliable",
    "reassembled",
    "encoding",
    "payload_size",
    "record_start_byte",
    "record_start_bit",
    "message_start_byte",
    "message_start_bit",
    "failure_byte",
    "failure_bit",
    "user_message_name",
    "user_message_id",
    "expected_body",
    "actual_body",
    "last_publication",
    "attempted_records",
    "committed_records", "use_press", "use_release", "use_generated", "use_transmitted",
    "use_clear_transmitted", "use_sent", "use_health_before", "use_health_after",
    "use_armor_before", "use_armor_after", "use_server_effect", "use_reason",
    "use_prediction_state", "use_prediction_reason",
    "brush_candidates", "brush_resolved", "brush_prepared", "brush_hidden",
    "brush_material_unsupported", "brush_submitted", "brush_culled", "brush_uploads",
    "texture_uploads", "brush_reject_entity", "brush_reject_slot", "brush_reject_submodel",
    "brush_reject_reason", "brush_reject_revision",
    "collision_revision", "collision_brushes", "ground_entity", "ground_model",
    "ground_normal_x", "ground_normal_y", "ground_normal_z", "grounded_server", "grounded_local",
    "movement_steps", "brush_server_changes", "brush_render_changes", "base_velocity", "support_policy",
    "prediction_fallbacks", "prediction_raw_error", "prediction_camera_jump", "prediction_ground_status", "prediction_reason", "prediction_last_fallback",
    "brush_server_last_entity", "brush_server_last_model", "brush_render_last_entity", "brush_render_last_model",
    "audio_backend", "audio_error", "audio_start_messages", "audio_stop_messages", "audio_static_messages", "audio_change_messages",
    "audio_started", "audio_stopped", "audio_updated", "audio_duplicates", "audio_unsupported", "audio_missing",
    "audio_expired", "audio_limits", "audio_loads", "audio_queue_drops", "audio_output_frames", "audio_queued_frames", "audio_underruns", "audio_sentences", "audio_formats",
    "weapon_audio_actions", "weapon_audio_fire", "weapon_audio_reload", "weapon_audio_deploy", "weapon_audio_swing",
    "weapon_audio_markers", "weapon_audio_duplicates", "weapon_audio_late", "weapon_audio_cancelled", "weapon_audio_missing",
    "weapon_audio_submitted", "weapon_audio_started", "weapon_audio_invalid", "weapon_audio_muted",
    "weapon_audio_marker_duplicates", "weapon_audio_delivery_duplicates", "weapon_audio_timeline_corrections"};
using ApplicationEvidence = std::array<std::optional<std::string>, kApplicationMetrics.size()>;
bool valid_application_metric(std::string_view name, std::string_view value) {
    if (value.empty() || value.size()>64U) return false;
    const bool decimal=name=="ground_normal_x" || name=="ground_normal_y" ||
        name=="ground_normal_z" || name=="prediction_raw_error" || name=="prediction_camera_jump";
    if (decimal && value!="unavailable") {
        std::size_t i=value.front()=='-' ? 1U : 0U;
        const auto begin=i;
        while (i<value.size() && value[i]>='0' && value[i]<='9') ++i;
        if (i==begin) return false;
        if (i==value.size()) return true;
        if (value[i++]!='.') return false;
        const auto fraction=i;
        while (i<value.size() && value[i]>='0' && value[i]<='9') ++i;
        return i>fraction && i==value.size();
    }
    return std::all_of(value.begin(),value.end(),[](unsigned char c) {
        return (c>='a' && c<='z') || (c>='A' && c<='Z') ||
            (c>='0' && c<='9') || c=='_' || c=='-';
    });
}
// Values passed the bounded inert-token parser; no network text is emitted.
void write_application_evidence(std::ostream& out, const ApplicationEvidence& values) {
    out << "{";
    for (std::size_t i = 0U; i < kApplicationMetrics.size(); ++i) {
        out << (i == 0U ? "\n    " : ",\n    ") << "\"" << kApplicationMetrics[i] << "\": ";
        if (values[i]) out << "\"" << *values[i] << "\"";
        else out << "null";
    }
    out << "\n  }";
}
struct WeaponPredictionEvidence final {
    std::optional<std::string> result;
    std::array<std::optional<bool>, 7U> flags{};
    std::optional<std::string> hit_status;
    [[nodiscard]] bool verified() const noexcept {
        return result == "live_client_predicted_weapon_presentation_verified" &&
            hit_status == "unavailable" &&
            std::all_of(flags.begin(), flags.end(), [](const auto& value) { return value == true; });
    }
};
constexpr std::array<std::string_view, 6U> kLifeFlags{
    "respawn_input_submitted", "server_alive", "same_session",
    "glock_bound", "crowbar_bound", "feature_verified"};
constexpr std::array<std::string_view, 41U> kLifeMetrics{
    "application_runtime_result", "phase", "blocker", "generation", "life_epoch",
    "damage_events", "damage_live", "health_before", "health_after",
    "armor_before", "armor_after", "local_deaths", "dead_flag",
    "post_respawn_commands", "post_respawn_samples", "pre_model", "post_model",
    "prediction_reason", "prediction_anchor", "prediction_history_depth",
    "manual_validation", "post_respawn_frames", "damage_source", "death_source",
    "health_before_source", "health_after_source", "armor_before_source", "armor_after_source",
    "pre_weapon", "post_weapon", "hud_health", "hud_armor", "hud_weapon",
    "prediction_state", "prediction_history_end", "prediction_replayed_commands", "prediction_reseed",
    "pre_hud_health", "pre_hud_armor", "pre_hud_health_source", "pre_hud_armor_source"};
struct LifeEvidence final {
    std::optional<std::string> result;
    std::array<std::optional<bool>, kLifeFlags.size()> flags{};
    std::array<std::optional<std::string>, kLifeMetrics.size()> metrics{};
    [[nodiscard]] bool verified() const noexcept {
        const auto positive = [&](std::size_t index, std::size_t minimum) {
            if (!metrics[index]) return false;
            std::size_t value{};
            const auto& text = *metrics[index];
            const auto parsed = std::from_chars(text.data(), text.data()+text.size(), value);
            return parsed.ec == std::errc{} && parsed.ptr == text.data()+text.size() && value >= minimum;
        };
        return result == "live_death_respawn_verified_damage_pending" &&
            metrics[0] == "completed" &&
            metrics[1] == "complete" && positive(3,1) && positive(4,2) &&
            positive(11,1) && positive(13,1) && positive(14,2) && positive(21,1) &&
            std::all_of(flags.begin(), flags.end(), [](const auto& v) { return v == true; });
    }
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
    bool validate_test_start_health_contract{false};
    bool validate_remote_audio_peer_contract{false};
    bool remote_audio_peer{false};
    std::optional<std::uint32_t> manual_duration_seconds;
    bool manual_no_time_limit{false};
    bool validate_config{false};
    bool validate_functional_log_observation{false};
    std::optional<std::filesystem::path> runtime_failure_fixture;
    bool runtime_failure_status_fixture{false};
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
    bool project_client_mute_glock_fire_sound{false};
    bool test_start_health{false};
    bool validate_wrapper_startup{false};
    windows::HldsRuntimeProfile::Id server_profile_id{
        windows::HldsRuntimeProfile::Id::legacy_stdio_hlds_banner_v1};
    std::optional<goldsrc::StockRuntimeCaptureOutputRole> output_role;
    bool fast_manual{false};
    bool manual_validation{false};
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

void manual_phase(const Options& options, const std::string_view phase) {
    if (!options.manual_validation) return;
    static const auto start = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "[stock-runtime-orchestrator] phase-" << phase << "-ms=" << elapsed << std::endl;
}

struct TestStartHealthLaunch final {
    std::wstring player_name;
    std::vector<std::wstring> server_arguments;
};
[[nodiscard]] bool test_start_health_server_ready(std::string_view log) {
    return log.find("[hlclient-test-health-stage] stage=attached") != std::string_view::npos &&
        log.find("[hlclient-test-health-stage] stage=server_activated configured=true") != std::string_view::npos;
}
[[nodiscard]] std::optional<std::string_view> test_start_health_configuration_reason(std::string_view log) {
    constexpr std::string_view prefix="[hlclient-test-health-config] reason=";
    const auto at=log.rfind(prefix);
    if (at==std::string_view::npos || (at!=0U && log[at-1U]!='\n')) return std::nullopt;
    const auto begin=at+prefix.size();
    const auto end=log.find_first_of("\r\n",begin);
    const auto value=log.substr(begin,end==std::string_view::npos ? end : end-begin);
    for (const auto reason:{std::string_view{"ready"},std::string_view{"run_missing"},
            std::string_view{"run_invalid"},std::string_view{"profile_missing"},
            std::string_view{"profile_mismatch"},std::string_view{"globals_unavailable"},
            std::string_view{"deathmatch_unavailable"},std::string_view{"client_limit_invalid"},
            std::string_view{"map_mismatch"}})
        if (value==reason) return reason;
    return std::nullopt;
}
[[nodiscard]] std::optional<TestStartHealthLaunch> test_start_health_launch(const fs::path& run_root,
    const bool fast_manual = false) {
    const auto id=run_root.filename().wstring();
    if (id.size()!=32U || !std::all_of(id.begin(),id.end(),[](wchar_t c) {
        return (c>=L'0' && c<=L'9') || (c>=L'a' && c<=L'f'); })) return std::nullopt;
    if (fast_manual) {
        // The wrapper's scoped liblist.gam selects the verified prepared DLL.
        // Do not repeat its absolute path in -dll: stock HLDS CheckParm uses
        // substring matching, so HLC-steamcfg in a path activates -steam and
        // GUI/AdminServer even when -console is present. Quoting cannot help.
        return TestStartHealthLaunch{L"HLC50_"+id.substr(0U,24U),
            {L"+localinfo",L"hlc_test_run",id,L"+localinfo",L"hlc_test_profile",L"test_server_assisted"}};
    }
    return TestStartHealthLaunch{L"HLC50_"+id.substr(0U,24U),
        {L"-dll",L"addons/hlclient_test50/"+id+L"/metamod.dll",
         L"+localinfo",L"mm_configfile",L"addons/hlclient_test50/"+id+L"/config.ini",
         // Stock stuffcmds splits on +/- characters inside values as well.
         // Keep this internal sentinel distinct from the human profile label.
         L"+localinfo",L"hlc_test_run",id,L"+localinfo",L"hlc_test_profile",L"test_server_assisted"}};
}
[[nodiscard]] bool test_start_health_server_arguments(std::vector<std::wstring>& args,
    const TestStartHealthLaunch& launch) {
    if (std::find(args.begin(), args.end(), L"+map") == args.end() ||
        std::find(args.begin(), args.end(), L"+status") == args.end()) return false;
    args.insert(std::find(args.begin(), args.end(), L"+map"),
                launch.server_arguments.begin(), launch.server_arguments.end());
    // Fixed owned command, not arbitrary user-supplied console execution.
    args.insert(std::find(args.begin(), args.end(), L"+status"), {L"+meta", L"require", L"HLC50"});
    return true;
}
[[nodiscard]] bool test_start_health_client_arguments(std::vector<std::wstring>& args,
    const std::wstring& name) {
    const auto at=std::find(args.begin(),args.end(),L"--name");
    if(at==args.end() || at+1==args.end()) return false;
    *(at+1)=name; args.insert(args.end(),{L"--test-start-health",L"50"}); return true;
}

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
    if (argc == 2 && std::wstring_view{argv[1]} == L"--validate-test-start-health-contract") {
        Options options; options.validate_test_start_health_contract=true; return options;
    }
    if (argc == 2 && std::wstring_view{argv[1]} == L"--validate-remote-audio-peer-contract") {
        Options options; options.validate_remote_audio_peer_contract=true; return options;
    }
    if (argc == 2 && std::wstring_view{argv[1]} == L"--validate-config") {
        Options options;
        options.validate_config = true;
        return options;
    }
    if ((argc == 3 || (argc == 4 && std::wstring_view{argv[3]} == L"--status")) &&
        std::wstring_view{argv[1]} == L"--validate-runtime-failure-roundtrip") {
        Options options;
        options.validate_functional_log_observation = true;
        options.runtime_failure_fixture = std::filesystem::path{argv[2]};
        options.runtime_failure_status_fixture = argc == 4;
        return options;
    }
    if (argc == 2 && std::wstring_view{argv[1]} ==
            L"--validate-functional-log-observation") {
        Options options;
        options.validate_functional_log_observation = true;
        return options;
    }
    Options options;
    std::array<bool, 58U> seen{};
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
        if (name == L"--remote-audio-peer") {
            if (!mark(55U)) return std::nullopt;
            options.remote_audio_peer=true;
            continue;
        }
        if (name == L"--manual-no-time-limit") {
            if (!mark(56U)) return std::nullopt;
            options.manual_no_time_limit = true;
            continue;
        }
        if (name == L"--project-client-stock-signon") {
            if (!mark(47U)) return std::nullopt;
            options.project_client_stock_signon = true;
            continue;
        }
        if (name == L"--project-client-mute-glock-fire-sound") {
            if (!mark(54U)) return std::nullopt;
            options.project_client_mute_glock_fire_sound = true;
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
        else if (name == L"--manual-duration-seconds") option = 57U;
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
        else if (name == L"--test-start-health") option = 52U;
        else if (name == L"--validation-mode") option = 53U;
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
            } else if (value == L"scripted-weapon-check") {
                options.project_client_live_input =
                    ProjectClientLiveInput::scripted_weapon_check;
            } else if (value == L"scripted-fire-reload-check") {
                options.project_client_live_input =
                    ProjectClientLiveInput::scripted_fire_reload_check;
            } else if (value == L"scripted-fire-reload-presentation-check") {
                options.project_client_live_input =
                    ProjectClientLiveInput::scripted_fire_reload_presentation_check;
            } else if (value == L"scripted-damage-respawn-check") {
                options.project_client_live_input =
                    ProjectClientLiveInput::scripted_damage_respawn_check;
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
        case 52U:
            if (value != L"50") return std::nullopt;
            options.test_start_health = true;
            break;
        case 57U: {
            std::uint32_t seconds{};
            if (!parse_wide_decimal(value, seconds) || seconds == 0U ||
                seconds > hlclient::core::kMaximumManualSessionSeconds) return std::nullopt;
            options.manual_duration_seconds = seconds;
            break;
        }
        case 53U:
            if (value != L"fast" && value != L"strict") return std::nullopt;
            options.manual_validation = true;
            options.fast_manual = value == L"fast";
            break;
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
    if (options.manual_validation && (!options.functional_smoke || !options.project_client_stock_signon ||
        options.project_client_stop != ProjectClientStop::live_visual_control ||
        (options.fast_manual && options.project_client_live_input != ProjectClientLiveInput::keyboard_mouse &&
         options.project_client_live_input != ProjectClientLiveInput::scripted_damage_respawn_check))) return std::nullopt;
    if ((options.manual_no_time_limit || options.manual_duration_seconds) &&
        (!options.manual_validation || !options.functional_smoke || !options.project_client_stock_signon ||
         options.project_client_stop != ProjectClientStop::live_visual_control ||
         options.project_client_live_input != ProjectClientLiveInput::keyboard_mouse ||
         (options.manual_no_time_limit && options.manual_duration_seconds))) return std::nullopt;
    if (options.project_client_mute_glock_fire_sound &&
        (!options.manual_validation || !options.project_client_stock_signon ||
         options.project_client_stop != ProjectClientStop::live_visual_control ||
         options.project_client_live_input != ProjectClientLiveInput::keyboard_mouse))
        return std::nullopt;
    if (options.remote_audio_peer && (!options.manual_validation || !options.functional_smoke ||
        !options.project_client_stock_signon || options.project_client_stop!=ProjectClientStop::live_visual_control ||
        options.project_client_live_input!=ProjectClientLiveInput::keyboard_mouse || options.map!="crossfire" || options.test_start_health)) return std::nullopt;
    if (options.test_start_health &&
        (!options.functional_smoke || !options.project_client_stock_signon ||
         options.project_client_stop != ProjectClientStop::live_visual_control ||
         options.project_client_live_input != ProjectClientLiveInput::keyboard_mouse ||
         !options.project_client_reference_prediction || options.map != "crossfire"))
        return std::nullopt;
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
            seen[54U] ||
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
           << (log.capture_failed ? "true" : "false") << ",\n"
           << "  \"retained_window\": " << (log.retained_window ? "true" : "false") << "\n}\n";
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
    ApplicationEvidence project_application;
    std::optional<std::string> project_h4_result;
    std::optional<std::string> project_weapon_result;
    std::optional<std::string> project_fire_reload_result;
    WeaponPredictionEvidence project_weapon_prediction;
    LifeEvidence project_life;
    std::optional<std::size_t> project_server_confirmed_shots;
    std::optional<std::size_t> project_reload_completions;
    std::optional<std::size_t> project_weapon_selection_queued;
    std::optional<std::size_t> project_weapon_selection_confirmed;
    std::optional<bool> project_viewmodel_pixels_distinct;
    std::optional<bool> project_hud_pixels_distinct;
    std::optional<std::string> project_viewmodel_camera_result;
    std::optional<bool> project_viewmodel_camera_pixels_valid;
    std::optional<std::size_t> project_viewmodel_camera_pixel_count;
    std::array<std::optional<std::size_t>, 5U> project_h4_active_frames{};
    std::array<std::optional<std::size_t>, 5U> project_h4_fallback_frames{};
    std::array<std::optional<std::size_t>, 5U> project_h4_local_steps{};
    std::array<std::optional<std::size_t>, 5U> project_h4_corrections{};
    std::optional<std::size_t> project_prediction_interpolated_frames;
    std::optional<std::size_t> project_prediction_endpoint_frames;
    std::optional<std::size_t> project_prediction_collision_blocked_frames;
    std::optional<std::size_t> project_prediction_visual_correction_frames;
    std::optional<std::size_t> project_prediction_trace_queries;
    std::optional<std::size_t> project_prediction_scratch_growths;
    std::optional<std::size_t> project_prediction_long_stall_frames;
    std::optional<double> project_prediction_active_time_ms;
    std::optional<double> project_prediction_fallback_time_ms;
    std::optional<double> project_prediction_cpu_total_ms;
    std::optional<double> project_prediction_cpu_max_ms;
    std::optional<double> project_prediction_maximum_camera_jump;
    std::optional<std::string> project_prediction_correction_pair_window;
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
    std::uint32_t remote_audio_peer_process_id{};
    std::optional<std::uint32_t> remote_audio_peer_exit;
    bool remote_audio_peer_entered{};
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
    ApplicationEvidence application;
    std::optional<std::string> h4_result;
    std::optional<std::string> weapon_result;
    std::optional<std::string> fire_reload_result;
    WeaponPredictionEvidence weapon_prediction;
    LifeEvidence life;
    std::optional<std::size_t> server_confirmed_shots;
    std::optional<std::size_t> reload_completions;
    std::optional<std::size_t> weapon_selection_queued;
    std::optional<std::size_t> weapon_selection_confirmed;
    std::optional<bool> viewmodel_pixels_distinct;
    std::optional<bool> hud_pixels_distinct;
    std::optional<std::string> viewmodel_camera_result;
    std::optional<bool> viewmodel_camera_pixels_valid;
    std::optional<std::size_t> viewmodel_camera_pixel_count;
    std::array<std::optional<std::size_t>, 5U> h4_active_frames{};
    std::array<std::optional<std::size_t>, 5U> h4_fallback_frames{};
    std::array<std::optional<std::size_t>, 5U> h4_local_steps{};
    std::array<std::optional<std::size_t>, 5U> h4_corrections{};
    std::optional<std::size_t> prediction_interpolated_frames;
    std::optional<std::size_t> prediction_endpoint_frames;
    std::optional<std::size_t> prediction_collision_blocked_frames;
    std::optional<std::size_t> prediction_visual_correction_frames;
    std::optional<std::size_t> prediction_trace_queries;
    std::optional<std::size_t> prediction_scratch_growths;
    std::optional<std::size_t> prediction_long_stall_frames;
    std::optional<double> prediction_active_time_ms;
    std::optional<double> prediction_fallback_time_ms;
    std::optional<double> prediction_cpu_total_ms;
    std::optional<double> prediction_cpu_max_ms;
    std::optional<double> prediction_maximum_camera_jump;
    std::optional<std::string> prediction_correction_pair_window;
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
            if (selected_visual_input == ProjectClientLiveInput::scripted_damage_respawn_check)
                return live_network_handoff && live_visual_input == selected_visual_input &&
                    live_visual_verified && usercmd_transmitted && life.verified();
            const bool jump_duck_selected = selected_visual_input ==
                ProjectClientLiveInput::scripted_jump_duck_check;
            const bool speed_selected = selected_visual_input ==
                ProjectClientLiveInput::scripted_speed_check;
            const bool weapon_selected = selected_visual_input ==
                ProjectClientLiveInput::scripted_weapon_check;
            const bool fire_selected = selected_visual_input ==
                ProjectClientLiveInput::scripted_fire_reload_check;
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
                (!weapon_selected ||
                 ((weapon_result ==
                       "live_viewmodel_weapon_selection_and_basic_hud_verified" ||
                   weapon_result ==
                       "live_viewmodel_hud_verified_selection_pending") &&
                  viewmodel_pixels_distinct == true &&
                  hud_pixels_distinct == true &&
                  weapon_selection_confirmed.value_or(0U) <=
                      weapon_selection_queued.value_or(0U) &&
                  (weapon_result !=
                       "live_viewmodel_weapon_selection_and_basic_hud_verified" ||
                   weapon_selection_confirmed.value_or(0U) > 0U))) &&
                (!fire_selected ||
                 (fire_reload_result ==
                      "live_primary_fire_reload_and_weapon_animation_verified" &&
                  server_confirmed_shots.value_or(0U) > 0U &&
                  reload_completions.value_or(0U) > 0U)) &&
                (selected_visual_input != ProjectClientLiveInput::scripted_fire_reload_presentation_check ||
                 (weapon_prediction.verified() && server_confirmed_shots.value_or(0U) >= 2U &&
                  reload_completions.value_or(0U) > 0U)) &&
                (!reference_prediction || selected_visual_input == ProjectClientLiveInput::keyboard_mouse ||
                 prediction_result ==
                    "live_local_prediction_and_reconciliation_verified") &&
                (!(jump_duck_selected && reference_prediction) ||
                 h4_result == "live_jump_duck_crouchwalk_prediction_verified") &&
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
    // Inert typed diagnostics only. No free-form message body/path/auth text
    // enters the retained JSON. Optional for historical/other client modes.
    if (const auto at = bytes.find("live_application_outcome result=");
        at != std::string_view::npos) {
        const auto end = bytes.find_first_of("\r\n", at);
        const auto line = bytes.substr(at, (end == std::string_view::npos ? bytes.size() : end) - at);
        for (std::size_t i = 0U; i < kApplicationMetrics.size(); ++i) {
            const auto key = std::string{" "} + std::string{kApplicationMetrics[i]} + "=";
            const auto field = line.find(key);
            if (field == std::string_view::npos) continue;
            const auto begin = field + key.size();
            const auto stop = line.find(' ', begin);
            const auto value = line.substr(begin,
                (stop == std::string_view::npos ? line.size() : stop) - begin);
            if (valid_application_metric(kApplicationMetrics[i],value))
                observation.application[i] = std::string{value};
        }
    }
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
        } else if (visual_line.find(" input=scripted-weapon-check ") !=
                   std::string_view::npos) {
            observation.live_visual_input =
                ProjectClientLiveInput::scripted_weapon_check;
        } else if (visual_line.find(" input=scripted-damage-respawn-check ") != std::string_view::npos) {
            observation.live_visual_input = ProjectClientLiveInput::scripted_damage_respawn_check;
        } else if (visual_line.find(" input=scripted-fire-reload-presentation-check ") !=
                   std::string_view::npos) {
            observation.live_visual_input =
                ProjectClientLiveInput::scripted_fire_reload_presentation_check;
        } else if (visual_line.find(" input=scripted-fire-reload-check ") !=
                   std::string_view::npos) {
            observation.live_visual_input =
                ProjectClientLiveInput::scripted_fire_reload_check;
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
    constexpr std::string_view weapon_marker{"live_weapon_presentation result="};
    if (const auto at = bytes.rfind(weapon_marker);
        at != std::string_view::npos) {
        const auto end = bytes.find_first_of("\r\n", at);
        const auto line = bytes.substr(at,
            (end == std::string_view::npos ? bytes.size() : end) - at);
        const auto value = [line](const std::string_view key)
            -> std::optional<std::string_view> {
            const auto found = line.find(key);
            if (found == std::string_view::npos) return std::nullopt;
            const auto begin = found + key.size();
            const auto end = line.find(' ', begin);
            return line.substr(begin, end - begin);
        };
        if (const auto result = value(" result="); result &&
            (*result == "live_viewmodel_weapon_selection_and_basic_hud_verified" ||
             *result == "live_viewmodel_hud_verified_selection_pending" ||
             *result == "viewmodel_hud_implemented_live_pending"))
            observation.weapon_result = std::string{*result};
        const auto count = [&](const std::string_view key)
            -> std::optional<std::size_t> {
            const auto token = value(key);
            if (!token) return std::nullopt;
            std::size_t parsed_value = 0U;
            const auto parsed = std::from_chars(token->data(),
                token->data() + token->size(), parsed_value, 10);
            return parsed.ec == std::errc{} &&
                parsed.ptr == token->data() + token->size()
                ? std::optional<std::size_t>{parsed_value} : std::nullopt;
        };
        const auto boolean = [&](const std::string_view key)
            -> std::optional<bool> {
            const auto token = value(key);
            if (token == "1" || token == "true") return true;
            if (token == "0" || token == "false") return false;
            return std::nullopt;
        };
        observation.weapon_selection_queued = count(" selection_queued=");
        observation.weapon_selection_confirmed = count(" selection_confirmed=");
        observation.viewmodel_pixels_distinct =
            boolean(" viewmodel_pixels_distinct=");
        observation.hud_pixels_distinct = boolean(" hud_pixels_distinct=");
    }
    constexpr std::string_view fire_marker{"live_fire_reload result="};
    if (const auto at = bytes.rfind(fire_marker);
        at != std::string_view::npos) {
        const auto end = bytes.find_first_of("\r\n", at);
        const auto line = bytes.substr(at,
            (end == std::string_view::npos ? bytes.size() : end) - at);
        const auto token = [line](const std::string_view key)
            -> std::optional<std::string_view> {
            const auto found = line.find(key);
            if (found == std::string_view::npos) return std::nullopt;
            const auto begin = found + key.size();
            return line.substr(begin, line.find(' ', begin) - begin);
        };
        if (const auto result = token(" result="); result &&
            (*result == "live_primary_fire_reload_and_weapon_animation_verified" ||
             *result == "primary_fire_verified_reload_pending" ||
             *result == "primary_fire_reload_implemented_live_pending"))
            observation.fire_reload_result = std::string{*result};
        const auto count = [&](const std::string_view key)
            -> std::optional<std::size_t> {
            const auto value = token(key);
            if (!value) return std::nullopt;
            std::size_t parsed_value = 0U;
            const auto parsed = std::from_chars(value->data(),
                value->data() + value->size(), parsed_value, 10);
            return parsed.ec == std::errc{} &&
                parsed.ptr == value->data() + value->size()
                ? std::optional<std::size_t>{parsed_value} : std::nullopt;
        };
        observation.server_confirmed_shots = count(" server_confirmed_shots=");
        observation.reload_completions = count(" reload_completions=");
    }
    constexpr std::string_view prediction_weapon_marker{"live_weapon_prediction result="};
    if (const auto at = bytes.rfind(prediction_weapon_marker);
        at != std::string_view::npos && observation.live_visual_input ==
            ProjectClientLiveInput::scripted_fire_reload_presentation_check) {
        const auto end = bytes.find_first_of("\r\n", at);
        const auto line = bytes.substr(at, (end == std::string_view::npos ? bytes.size() : end) - at);
        const auto token = [line](std::string_view key) -> std::optional<std::string_view> {
            const auto at = line.find(key);
            if (at == std::string_view::npos) return {};
            const auto begin = at + key.size();
            return line.substr(begin, line.find(' ', begin) - begin);
        };
        if (const auto value = token(" result="); value &&
            (*value == "live_client_predicted_weapon_presentation_verified" ||
             *value == "client_weapon_presentation_implemented_live_pending"))
            observation.weapon_prediction.result = std::string{*value};
        for (std::size_t i = 0U; i < kWeaponPredictionFlags.size(); ++i) {
            const auto key = std::string{" "} + std::string{kWeaponPredictionFlags[i]} + "=";
            if (const auto value = token(key)) {
                if (*value == "1" || *value == "true") observation.weapon_prediction.flags[i] = true;
                else if (*value == "0" || *value == "false") observation.weapon_prediction.flags[i] = false;
            }
        }
        if (token(" crowbar_hit_status=") == "unavailable")
            observation.weapon_prediction.hit_status = "unavailable";
    }
    constexpr std::string_view life_marker{"live_damage_respawn result="};
    if (const auto at = bytes.rfind(life_marker); at != std::string_view::npos &&
        observation.live_visual_input == ProjectClientLiveInput::scripted_damage_respawn_check) {
        const auto end = bytes.find_first_of("\r\n", at);
        const auto line = bytes.substr(at, (end == std::string_view::npos ? bytes.size() : end) - at);
        const auto token = [line](std::string_view field) -> std::optional<std::string_view> {
            const auto key = std::string{" "} + std::string{field} + "=";
            const auto at = line.find(key);
            if (at == std::string_view::npos) return {};
            const auto begin = at + key.size();
            const auto value = line.substr(begin, line.find(' ', begin) - begin);
            if (value.empty() || value.size() > 128U ||
                !std::all_of(value.begin(), value.end(), [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
                })) return {};
            return value;
        };
        if (const auto v = token("result"); v &&
            (*v == "live_death_respawn_verified_damage_pending" ||
             *v == "damage_death_respawn_implemented_live_pending"))
            observation.life.result = std::string{*v};
        for (std::size_t i = 0; i < kLifeFlags.size(); ++i) {
            if (const auto v = token(kLifeFlags[i])) {
                if (*v == "1" || *v == "true") observation.life.flags[i] = true;
                else if (*v == "0" || *v == "false") observation.life.flags[i] = false;
            }
        }
        for (std::size_t i = 0; i < kLifeMetrics.size(); ++i)
            if (const auto v = token(kLifeMetrics[i]))
                observation.life.metrics[i] = std::string{*v};
    }
    constexpr std::string_view camera_marker{"live_viewmodel_camera result="};
    if (const auto at = bytes.rfind(camera_marker);
        at != std::string_view::npos) {
        const auto end = bytes.find_first_of("\r\n", at);
        const auto line = bytes.substr(at,
            (end == std::string_view::npos ? bytes.size() : end) - at);
        const auto value = [line](const std::string_view key)
            -> std::optional<std::string_view> {
            const auto found = line.find(key);
            if (found == std::string_view::npos) return std::nullopt;
            const auto begin = found + key.size();
            const auto end = line.find(' ', begin);
            return line.substr(begin, end - begin);
        };
        if (const auto result = value(" result="); result &&
            (*result == "live_viewmodel_camera_space_verified" ||
             *result == "viewmodel_camera_space_implemented_live_pending"))
            observation.viewmodel_camera_result = std::string{*result};
        if (const auto valid = value(" pixel_observation_valid="); valid) {
            if (*valid == "1" || *valid == "true")
                observation.viewmodel_camera_pixels_valid = true;
            else if (*valid == "0" || *valid == "false")
                observation.viewmodel_camera_pixels_valid = false;
        }
        if (const auto count = value(" pixel_count="); count) {
            std::size_t parsed_value = 0U;
            const auto parsed = std::from_chars(count->data(),
                count->data() + count->size(), parsed_value, 10);
            if (parsed.ec == std::errc{} &&
                parsed.ptr == count->data() + count->size())
                observation.viewmodel_camera_pixel_count = parsed_value;
        }
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
    constexpr std::string_view h4_marker{"live_h4_prediction result="};
    if (const auto found = bytes.rfind(h4_marker);
        found != std::string_view::npos) {
        const auto begin = found + h4_marker.size();
        const auto end = bytes.find_first_of(" \r\n", begin);
        const auto value = bytes.substr(begin, end - begin);
        if (value == "live_jump_duck_crouchwalk_prediction_verified" ||
            value == "jump_prediction_verified_duck_pending" ||
            value == "jump_duck_prediction_implemented_live_pending" ||
            value == "crouch_walk_prediction_context_blocked")
            observation.h4_result = std::string{value};
    }
    constexpr std::array<std::string_view, 5U> h4_phase_names{
        "settle", "jump_hold", "landing", "duck_crouch_walk",
        "unduck_standing_shift"};
    for (std::size_t index = 0U; index < h4_phase_names.size(); ++index) {
        const auto marker = std::string{"live_h4_phase phase="} +
            std::string{h4_phase_names[index]};
        const auto found = bytes.rfind(marker);
        if (found == std::string_view::npos) continue;
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
        observation.h4_active_frames[index] = count(" active_frames=");
        observation.h4_fallback_frames[index] = count(" fallback_frames=");
        observation.h4_local_steps[index] = count(" local_steps=");
        observation.h4_corrections[index] = count(" corrections=");
    }
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
        const auto line_end = bytes.find_first_of("\r\n", found);
        const auto line = bytes.substr(found,
            (line_end == std::string_view::npos ? bytes.size() : line_end) - found);
        const auto begin = found + prediction_marker.size();
        const auto end = bytes.find_first_of(" \r\n", begin);
        const auto value = bytes.substr(begin, end - begin);
        if (value == "live_local_prediction_and_reconciliation_verified" ||
            value == "live_prediction_active_accuracy_or_coverage_limited" ||
            value == "live_prediction_integrated_live_pending" ||
            value == "prediction_seed_or_anchor_contract_partial")
            observation.prediction_result = std::string{value};
        const auto token = [line](const std::string_view key)
            -> std::optional<std::string_view> {
            const auto at = line.find(key);
            if (at == std::string_view::npos) return std::nullopt;
            const auto start = at + key.size();
            const auto end = line.find(' ', start);
            return line.substr(start, end - start);
        };
        const auto count = [&](const std::string_view key)
            -> std::optional<std::size_t> {
            const auto raw = token(key);
            if (!raw) return std::nullopt;
            std::size_t parsed_value = 0U;
            const auto parsed = std::from_chars(raw->data(),
                raw->data() + raw->size(), parsed_value, 10);
            return parsed.ec == std::errc{} &&
                    parsed.ptr == raw->data() + raw->size()
                ? std::optional<std::size_t>{parsed_value} : std::nullopt;
        };
        observation.prediction_interpolated_frames = count(" interpolated_frames=");
        observation.prediction_endpoint_frames = count(" endpoint_frames=");
        observation.prediction_collision_blocked_frames = count(" collision_blocked_frames=");
        observation.prediction_visual_correction_frames = count(" visual_correction_frames=");
        observation.prediction_trace_queries = count(" presentation_trace_queries=");
        observation.prediction_scratch_growths = count(" presentation_scratch_growths=");
        observation.prediction_long_stall_frames = count(" long_stall_frames=");
        const auto nonnegative_real = [&](const std::string_view key)
            -> std::optional<double> {
            const auto raw = token(key);
            if (!raw) return std::nullopt;
            double parsed_value = 0.0;
            const auto parsed = std::from_chars(raw->data(),
                raw->data() + raw->size(), parsed_value);
            if (parsed.ec == std::errc{} &&
                parsed.ptr == raw->data() + raw->size() &&
                std::isfinite(parsed_value) && parsed_value >= 0.0)
                return parsed_value;
            return std::nullopt;
        };
        observation.prediction_active_time_ms =
            nonnegative_real(" active_time_ms=");
        observation.prediction_fallback_time_ms =
            nonnegative_real(" fallback_time_ms=");
        observation.prediction_cpu_total_ms =
            nonnegative_real(" presentation_cpu_total_ms=");
        observation.prediction_cpu_max_ms =
            nonnegative_real(" presentation_cpu_max_ms=");
        observation.prediction_maximum_camera_jump =
            nonnegative_real(" maximum_camera_correction_jump=");
        if (const auto raw = token(" correction_pair_window="); raw &&
            raw->size() <= 1024U && *raw != "unavailable" &&
            raw->find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyz_:.,-") ==
                std::string_view::npos)
            observation.prediction_correction_pair_window = std::string{*raw};
    }
    return observation;
}

// Peer evidence is independent of the listener's outcome. In particular an
// early listener failure must not turn a missing peer observation into a
// claimed peer startup failure, or replace the listener's primary error.
void apply_remote_audio_peer_observation(
    ActiveSummary& summary,
    const windows::BoundedProcessLogSnapshot& peer_log)
{
    const auto observed = observe_project_client_signon_log(peer_log.bytes);
    summary.remote_audio_peer_entered = summary.remote_audio_peer_entered ||
        (observed.connection_accepted && observed.client_world_state_published);
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
    summary.project_application = observation.application;
    summary.project_h4_result = observation.h4_result;
    summary.project_weapon_result = observation.weapon_result;
    summary.project_fire_reload_result = observation.fire_reload_result;
    summary.project_weapon_prediction = observation.weapon_prediction;
    summary.project_life = observation.life;
    summary.project_server_confirmed_shots = observation.server_confirmed_shots;
    summary.project_reload_completions = observation.reload_completions;
    summary.project_weapon_selection_queued =
        observation.weapon_selection_queued;
    summary.project_weapon_selection_confirmed =
        observation.weapon_selection_confirmed;
    summary.project_viewmodel_pixels_distinct =
        observation.viewmodel_pixels_distinct;
    summary.project_hud_pixels_distinct = observation.hud_pixels_distinct;
    summary.project_viewmodel_camera_result =
        observation.viewmodel_camera_result;
    summary.project_viewmodel_camera_pixels_valid =
        observation.viewmodel_camera_pixels_valid;
    summary.project_viewmodel_camera_pixel_count =
        observation.viewmodel_camera_pixel_count;
    summary.project_h4_active_frames = observation.h4_active_frames;
    summary.project_h4_fallback_frames = observation.h4_fallback_frames;
    summary.project_h4_local_steps = observation.h4_local_steps;
    summary.project_h4_corrections = observation.h4_corrections;
    summary.project_prediction_interpolated_frames = observation.prediction_interpolated_frames;
    summary.project_prediction_endpoint_frames = observation.prediction_endpoint_frames;
    summary.project_prediction_collision_blocked_frames = observation.prediction_collision_blocked_frames;
    summary.project_prediction_visual_correction_frames = observation.prediction_visual_correction_frames;
    summary.project_prediction_trace_queries = observation.prediction_trace_queries;
    summary.project_prediction_scratch_growths = observation.prediction_scratch_growths;
    summary.project_prediction_long_stall_frames = observation.prediction_long_stall_frames;
    summary.project_prediction_active_time_ms = observation.prediction_active_time_ms;
    summary.project_prediction_fallback_time_ms = observation.prediction_fallback_time_ms;
    summary.project_prediction_cpu_total_ms = observation.prediction_cpu_total_ms;
    summary.project_prediction_cpu_max_ms = observation.prediction_cpu_max_ms;
    summary.project_prediction_maximum_camera_jump = observation.prediction_maximum_camera_jump;
    summary.project_prediction_correction_pair_window = observation.prediction_correction_pair_window;
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

// This is a fixed, numeric-only application diagnostic grammar, not a raw-log
// allowlist. Keep its validation independent of the game module and producer.
// A bounded row cannot contain names, paths, tokens or borrowed resource data.
template <std::size_t Count>
[[nodiscard]] std::optional<std::array<std::string_view, Count>>
fixed_visibility_fields(const std::string_view line,
    const std::string_view marker,
    const std::array<std::string_view, Count>& keys,
    const std::size_t maximum_bytes) noexcept
{
    if (line.size() > maximum_bytes || !line.starts_with(marker)) return {};
    auto remaining = line.substr(marker.size());
    std::array<std::string_view, Count> values{};
    for (std::size_t index = 0U; index < Count; ++index) {
        const auto key = keys[index];
        if (!remaining.starts_with(key) || remaining.size() <= key.size() ||
            remaining[key.size()] != '=') return {};
        remaining.remove_prefix(key.size() + 1U);
        const auto separator = remaining.find(' ');
        values[index] = remaining.substr(0U, separator);
        if (values[index].empty()) return {};
        if (index + 1U == Count) {
            if (separator != std::string_view::npos) return {};
        } else {
            if (separator == std::string_view::npos) return {};
            remaining.remove_prefix(separator + 1U);
        }
    }
    return values;
}

[[nodiscard]] std::optional<std::uint64_t> visibility_unsigned(
    const std::string_view value, const std::uint64_t minimum,
    const std::uint64_t maximum) noexcept
{
    if (value.empty() || value.size() > 20U ||
        !std::ranges::all_of(value, [](const char character) {
            return character >= '0' && character <= '9';
        })) return {};
    std::uint64_t number{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        number < minimum || number > maximum) return {};
    return number;
}

[[nodiscard]] std::optional<double> visibility_number(
    const std::string_view value, const double minimum,
    const double maximum) noexcept
{
    if (value.empty() || value.size() > 48U ||
        !std::ranges::all_of(value, [](const char character) {
            return (character >= '0' && character <= '9') ||
                character == '-' || character == '+' || character == '.' ||
                character == 'e' || character == 'E';
        })) return {};
    double number{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        !std::isfinite(number) || number < minimum || number > maximum) return {};
    return number;
}

[[nodiscard]] std::optional<std::array<double, 3U>> visibility_vector(
    std::string_view value, const double minimum, const double maximum) noexcept
{
    std::array<double, 3U> result{};
    for (std::size_t index = 0U; index < result.size(); ++index) {
        const auto separator = value.find(',');
        const auto number = visibility_number(value.substr(0U, separator), minimum, maximum);
        if (!number) return {};
        result[index] = *number;
        if (index + 1U == result.size()) {
            if (separator != std::string_view::npos) return {};
        } else {
            if (separator == std::string_view::npos) return {};
            value.remove_prefix(separator + 1U);
        }
    }
    return result;
}

[[nodiscard]] bool valid_visibility_row(const std::string_view line) noexcept
{
    constexpr std::array keys{
        std::string_view{"generation"}, std::string_view{"source"},
        std::string_view{"ordinal"}, std::string_view{"publication"},
        std::string_view{"entity"}, std::string_view{"model"},
        std::string_view{"stage"}, std::string_view{"game"},
        std::string_view{"server-seconds"}, std::string_view{"distance"},
        std::string_view{"effects"}, std::string_view{"render-mode"},
        std::string_view{"camera"}, std::string_view{"target"},
        std::string_view{"near"}, std::string_view{"bounds"},
        std::string_view{"static-light"}};
    // 16 * 768 plus the summary leaves room for primary-error evidence in the
    // unchanged 16KiB excerpt. Oversized rows fail closed, never truncate.
    const auto fields = fixed_visibility_fields(
        line, "[remote-player-visibility] ", keys, 768U);
    if (!fields) return false;
    const auto& values = *fields;
    constexpr auto u64_max = (std::numeric_limits<std::uint64_t>::max)();
    constexpr auto u32_max = (std::numeric_limits<std::uint32_t>::max)();
    constexpr double coordinate_max = (std::numeric_limits<float>::max)();
    constexpr double number_max = (std::numeric_limits<double>::max)();
    constexpr std::array stages{
        std::string_view{"source_absent"}, std::string_view{"not_visual"},
        std::string_view{"model_not_advertised"}, std::string_view{"server_hidden128"},
        std::string_view{"unsupported_render_mode"}, std::string_view{"game_not_ready"},
        std::string_view{"pose_unavailable"}, std::string_view{"frame_unavailable"},
        std::string_view{"pvs_culled"}, std::string_view{"frustum_culled"},
        std::string_view{"queued_visible"}, std::string_view{"queued_unlit"},
        std::string_view{"queued_dim"}};
    if (!visibility_unsigned(values[0U], 1U, u64_max) ||
        !visibility_unsigned(values[1U], 0U, u64_max) ||
        !visibility_unsigned(values[2U], 0U, (std::numeric_limits<std::size_t>::max)()) ||
        !visibility_unsigned(values[3U], 0U, u64_max) ||
        !visibility_unsigned(values[4U], 1U, 32U) ||
        !visibility_unsigned(values[5U], 0U, 65'535U) ||
        std::ranges::find(stages, values[6U]) == stages.end() ||
        !visibility_unsigned(values[7U], 0U, 8U) ||
        !visibility_unsigned(values[10U], 0U, u32_max) ||
        !visibility_unsigned(values[11U], 0U, u32_max) ||
        !visibility_vector(values[12U], -coordinate_max, coordinate_max) ||
        !visibility_vector(values[13U], -coordinate_max, coordinate_max)) return false;
    for (const auto index : {8U, 9U}) {
        const auto number = visibility_number(values[index], -1.0, number_max);
        if (!number || (*number < 0.0 && *number != -1.0)) return false;
    }
    const auto near_plane = visibility_number(values[14U], 0.0, coordinate_max);
    if (!near_plane || *near_plane <= 0.0) return false;
    if (values[15U] != "unavailable") {
        const auto separator = values[15U].find(';');
        if (separator == std::string_view::npos) return false;
        const auto minimum = visibility_vector(values[15U].substr(0U, separator),
            -coordinate_max, coordinate_max);
        const auto maximum = visibility_vector(values[15U].substr(separator + 1U),
            -coordinate_max, coordinate_max);
        if (!minimum || !maximum) return false;
        for (std::size_t index = 0U; index < minimum->size(); ++index)
            if ((*minimum)[index] > (*maximum)[index]) return false;
    }
    return values[16U] == "fallback" || visibility_vector(values[16U], 0.0, 1.0).has_value();
}

[[nodiscard]] bool valid_visibility_summary(const std::string_view line) noexcept
{
    constexpr std::array keys{
        std::string_view{"changes"}, std::string_view{"retained"},
        std::string_view{"dropped"}, std::string_view{"evidence"}};
    const auto fields = fixed_visibility_fields(
        line, "[remote-player-visibility-summary] ", keys, 160U);
    if (!fields) return false;
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto changes = visibility_unsigned((*fields)[0U], 0U, maximum);
    const auto retained = visibility_unsigned((*fields)[1U], 0U, 16U);
    const auto dropped = visibility_unsigned((*fields)[2U], 0U, maximum);
    return changes && retained && dropped && *retained <= *changes &&
        *dropped == *changes - *retained && (*fields)[3U] == "cpu-frame-only";
}

[[nodiscard]] bool valid_remote_effects_summary(const std::string_view line) noexcept
{
    constexpr std::array keys{
        std::string_view{"received"}, std::string_view{"accepted"},
        std::string_view{"unresolved"}, std::string_view{"unsupported"},
        std::string_view{"local_echo"}, std::string_view{"late"},
        std::string_view{"invalid"}, std::string_view{"fire"},
        std::string_view{"swing"}, std::string_view{"audio_submitted"},
        std::string_view{"audio_late"}, std::string_view{"audio_missing"},
        std::string_view{"audio_rejected"}, std::string_view{"shells"},
        std::string_view{"impact_hits"}, std::string_view{"flash_submissions"}};
    const auto fields = fixed_visibility_fields(
        line, "[remote-effects-summary] ", keys, 511U);
    return fields && std::ranges::all_of(*fields, [](const auto value) {
        return visibility_unsigned(value, 0U,
            (std::numeric_limits<std::uint64_t>::max)()).has_value();
    });
}

[[nodiscard]] bool contains_fixed_numeric_marker(const std::string_view line) noexcept
{
    return line.find("[remote-player-visibility") != std::string_view::npos ||
        line.find("[remote-effects-summary") != std::string_view::npos;
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
        if (line.ends_with('\r')) line.remove_suffix(1U);
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
            line.starts_with("live_h4_phase ") ||
            line.starts_with("live_h4_prediction ") ||
            line.starts_with("live_weapon_presentation result=") ||
            line.starts_with("live_viewmodel_camera result=") ||
            line.starts_with("live_fire_reload result=") ||
            line.starts_with("live_damage_respawn result=") ||
            line.starts_with("live_weapon_prediction result=") ||
            line.starts_with("live_application_outcome result=") ||
            line.starts_with("live_visual_control result=");
    };
    const auto append_summary = [&](const std::string_view line) {
        const auto bounded = line.substr(
            0U, (std::min)(line.size(), maximum_line_bytes));
        if (emitted_lines >= maximum_lines ||
            output.size() + bounded.size() + 1U > maximum_output_bytes) {
            return false;
        }
        output.append(bounded);
        output.push_back('\n');
        ++emitted_lines;
        return true;
    };
    // Retain the newest bounded numeric journal BEFORE verbose terminal
    // summaries. This applies equally to both client roles; it does not widen
    // resource/native-log access or let malformed marker lookalikes through.
    std::array<std::string_view, 16U> visibility_rows{};
    std::size_t visibility_count = 0U;
    std::string_view visibility_summary;
    std::string_view remote_effects_summary;
    std::size_t visibility_offset = 0U;
    while (visibility_offset < bytes.size()) {
        const auto newline = bytes.find('\n', visibility_offset);
        const auto end = newline == std::string_view::npos ? bytes.size() : newline;
        const auto line = summary_view(bytes.substr(visibility_offset, end - visibility_offset));
        if (valid_remote_effects_summary(line)) remote_effects_summary = line;
        else if (valid_visibility_summary(line)) visibility_summary = line;
        else if (valid_visibility_row(line) &&
            std::ranges::find(visibility_rows, line) == visibility_rows.end()) {
            visibility_rows[visibility_count % visibility_rows.size()] = line;
            ++visibility_count;
        }
        if (newline == std::string_view::npos) break;
        visibility_offset = newline + 1U;
    }
    if (!remote_effects_summary.empty()) append_summary(remote_effects_summary);
    if (!visibility_summary.empty()) append_summary(visibility_summary);
    const auto first_visibility = visibility_count > visibility_rows.size()
        ? visibility_count - visibility_rows.size() : 0U;
    for (std::size_t index = first_visibility; index < visibility_count; ++index)
        append_summary(visibility_rows[index % visibility_rows.size()]);
    const auto safe_terminal_summary = [&](const std::string_view prefix,
                                            const bool newest) {
        std::string_view selected;
        std::size_t offset = 0U;
        while (offset < bytes.size()) {
            const auto newline = bytes.find('\n', offset);
            const auto end = newline == std::string_view::npos ? bytes.size() : newline;
            const auto line = summary_view(bytes.substr(offset, end - offset));
            if (line.starts_with(prefix) && !contains_fixed_numeric_marker(line)) {
                selected = line;
                if (!newest) break;
            }
            if (newline == std::string_view::npos) break;
            offset = newline + 1U;
        }
        return selected;
    };
    // Primary application evidence precedes competing health/weapon/timing
    // summaries. Preserve its bounded prefix even when the reserved journal
    // leaves slightly less than 4KiB. Structured outcome JSON is independent.
    bool application_summary_retained = false;
    const auto retained_application_line = safe_terminal_summary(
        "live_application_outcome result=", false);
    if (!retained_application_line.empty()) {
        const auto remaining = maximum_output_bytes - output.size();
        if (remaining > 1U)
            application_summary_retained = append_summary(retained_application_line.substr(
                0U, (std::min)(retained_application_line.size(), remaining - 1U)));
    }
    // Only the fixed test-helper evidence grammar survives redaction. The run
    // ID is wrapper-owned public metadata, not credentials/player data.
    constexpr std::string_view health_marker{"[hlclient-test-health] run="};
    constexpr std::array health_suffixes{
        std::string_view{" requested_start_health=50 server_setup_applied=true max_health=100 max_health_source=server_entvars"},
        std::string_view{" requested_start_health=50 server_setup_applied=identity_rejected max_health=unavailable max_health_source=unavailable"},
        std::string_view{" requested_start_health=50 server_setup_applied=slot_rejected max_health=unavailable max_health_source=unavailable"},
        std::string_view{" requested_start_health=50 server_setup_applied=ambiguous_identity max_health=unavailable max_health_source=unavailable"},
        std::string_view{" requested_start_health=50 server_setup_applied=spawn_validation_failed max_health=unavailable max_health_source=unavailable"},
        std::string_view{" requested_start_health=50 server_setup_applied=target_not_matched max_health=unavailable max_health_source=unavailable"},
        std::string_view{" requested_start_health=50 server_setup_applied=connection_binding_missing max_health=unavailable max_health_source=unavailable"}};
    for (std::size_t at = bytes.find(health_marker); at != std::string_view::npos;
         at = bytes.find(health_marker, at + health_marker.size())) {
        const auto id_at = at + health_marker.size();
        if (id_at + 32U <= bytes.size()) {
            const auto id = bytes.substr(id_at, 32U);
            if (std::all_of(id.begin(), id.end(), [](char c) {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) &&
                id.size() == 32U) {
                for (const auto suffix : health_suffixes) {
                    const auto end = id_at + 32U + suffix.size();
                    if (bytes.substr(id_at + 32U, suffix.size()) == suffix &&
                        (end == bytes.size() || bytes[end] == '\r' || bytes[end] == '\n'))
                        append_summary(bytes.substr(at, end-at));
                }
            }
        }
    }
    constexpr std::string_view health_ready{"[test-start-health] requested_start_health=50 client_observed_health=50 source=fresh_clientdata result=ready"};
    if (bytes.find(health_ready) != std::string_view::npos) append_summary(health_ready);
    if (const auto reason=test_start_health_configuration_reason(bytes))
        append_summary(std::string{"[hlclient-test-health-config] reason="}+std::string{*reason});
    for (const auto marker : {
            std::string_view{"[hlclient-test-health-stage] stage=attached"},
            std::string_view{"[hlclient-test-health-stage] stage=server_activated configured=true"},
            std::string_view{"[hlclient-test-health-stage] stage=server_activated configured=false"},
            std::string_view{"[test-start-health] result=failed reason=fresh_server_50_not_observed client_observed_health=100"},
            std::string_view{"[test-start-health] requested_start_health=50 client_observed_health=100 source=fresh_clientdata result=pending"},
            std::string_view{"[test-start-health] result=failed reason=fresh_server_50_not_observed client_observed_health=unavailable"}}) {
        if (bytes.find(marker) != std::string_view::npos) append_summary(marker);
    }

    // Only fixed numeric/status fields are emitted by this app marker. Keep
    // the latest bounded observations so a server-confirmed switch can be
    // inspected after exact restoration without retaining private raw logs.
    std::array<std::string_view, 6U> recent_weapon_lines{};
    std::size_t weapon_line_count = 0U;
    std::size_t weapon_offset = 0U;
    while (weapon_offset < bytes.size()) {
        const auto newline = bytes.find('\n', weapon_offset);
        const auto end = newline == std::string_view::npos
            ? bytes.size() : newline;
        const auto line = summary_view(bytes.substr(weapon_offset,
            end - weapon_offset));
        if (line.starts_with("live_weapon_observation generation=") &&
            !contains_fixed_numeric_marker(line)) {
            recent_weapon_lines[weapon_line_count % recent_weapon_lines.size()] = line;
            ++weapon_line_count;
        }
        if (newline == std::string_view::npos) break;
        weapon_offset = newline + 1U;
    }
    const auto first_weapon = weapon_line_count > recent_weapon_lines.size()
        ? weapon_line_count - recent_weapon_lines.size() : 0U;
    for (std::size_t index = first_weapon; index < weapon_line_count; ++index)
        append_summary(recent_weapon_lines[index % recent_weapon_lines.size()]);

    // Terminal F timing/phase/framebuffer summaries must survive a large
    // number of earlier camera/TX observations. Select those bounded lines
    // first, independent of the logger's optional "[info] " prefix.
    for (const auto prefix : {"live_prediction mode=", "live_weapon_prediction result=",
             "live_fire_reload result=", "live_damage_respawn result="}) {
        const auto line = safe_terminal_summary(prefix, true);
        if (!line.empty()) append_summary(line);
    }
    std::size_t priority_offset = 0U;
    while (priority_offset < bytes.size()) {
        const auto newline = bytes.find('\n', priority_offset);
        const auto end = newline == std::string_view::npos
            ? bytes.size() : newline;
        const auto line = bytes.substr(priority_offset, end - priority_offset);
        const auto summary_line = summary_view(line);
        if (high_priority_summary(summary_line) &&
            !contains_fixed_numeric_marker(summary_line) &&
            !summary_line.starts_with("live_prediction mode=") &&
            !summary_line.starts_with("live_damage_respawn result=") &&
            !summary_line.starts_with("live_weapon_prediction result=") &&
            !summary_line.starts_with("live_fire_reload result=") &&
            !(application_summary_retained &&
                summary_line == retained_application_line)) {
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
        const auto original_line = bytes.substr(offset, end - offset);
        const auto line = original_line.substr(
            0U, (std::min)(original_line.size(), maximum_line_bytes));
        const auto summary_line = summary_view(line);
        const auto lower = ascii_lower(line);
        const bool camera_line =
            summary_line.starts_with("live_visual_camera_sample ");
        const bool tx_line = summary_line.starts_with("[live-usercmd-tx] ");
        const bool safe_live_usercmd_summary =
            !high_priority_summary(summary_line) &&
            ((camera_line && retained_camera_lines < 4U) ||
             (tx_line && retained_tx_lines < 4U));
        if (contains_fixed_numeric_marker(original_line)) {
            // Valid rows were selected above; invalid/duplicate/oversized
            // rows must not survive the broad error/connect redaction path.
        } else if (safe_live_usercmd_summary) {
            append_summary(summary_line);
            if (camera_line) ++retained_camera_lines;
            if (tx_line) ++retained_tx_lines;
        } else if (!high_priority_summary(summary_line) &&
                   contains_any(lower, {
                "error", "failed", "unable", "reject", "disconnect",
                "dropped", "kicked", "timeout", "connect", "entered the game",
                "steam", "map", "server logging", "metamod", "plugin"})) {
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
        if (summary.project_life.verified())
            return "live_death_respawn_verified_damage_pending";
        if (summary.project_weapon_prediction.verified())
            return "live_client_predicted_weapon_presentation_verified";
        if (summary.project_fire_reload_result ==
            "live_primary_fire_reload_and_weapon_animation_verified")
            return "live_primary_fire_reload_and_weapon_animation_verified";
        if (summary.project_weapon_result ==
            "live_viewmodel_weapon_selection_and_basic_hud_verified")
            return "live_viewmodel_weapon_selection_and_basic_hud_verified";
        if (summary.project_weapon_result ==
            "live_viewmodel_hud_verified_selection_pending")
            return "live_viewmodel_hud_verified_selection_pending";
        if (summary.project_h4_result ==
            "live_jump_duck_crouchwalk_prediction_verified")
            return "live_jump_duck_crouchwalk_prediction_verified";
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
    const windows::BoundedProcessLogSnapshot& guard_log,
    const std::optional<windows::BoundedProcessLogSnapshot>& peer_log)
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
        windows::bounded_process_log_snapshot_complete(guard_log) &&
        (!peer_log || windows::bounded_process_log_snapshot_complete(*peer_log));
    const bool manual_timing = options.manual_no_time_limit || options.manual_duration_seconds;
    const auto diagnostic_usable = [&](const windows::BoundedProcessLogSnapshot& log) {
        return windows::bounded_process_log_snapshot_complete(log) ||
            (manual_timing && windows::bounded_process_log_diagnostic_window_usable(log));
    };
    const bool diagnostic_windows_usable = diagnostic_usable(server_log) &&
        diagnostic_usable(client_log) && diagnostic_usable(guard_log) &&
        (!peer_log || diagnostic_usable(*peer_log));
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
    if (peer_log) {
        write_supporting(L"peer-diagnostic-redacted.log",
                         functional_diagnostic_excerpt(peer_log->bytes));
        write_supporting(L"peer-metadata.json", log_metadata_json(*peer_log));
    }

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
    const auto write_optional_bool = [&functional](
        const std::optional<bool>& value) {
        if (value) functional << (*value ? "true" : "false");
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
        << (project_mode && !options.test_start_health && !options.fast_manual && !options.remote_audio_peer && !manual_timing ? "true" : "false") << ",\n"
        << "  \"validation_mode\": \"" << (options.fast_manual ? "fast" : "strict") << "\",\n"
        << (options.test_start_health
                ? "  \"server_environment_profile\": \"test-server-assisted\",\n"
                : "")
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
                                ProjectClientLiveInput::scripted_weapon_check
                            ? "renderer=opengl;auth-provider=steam;stop-after=live-visual-control;live-input=scripted-weapon-check;basedir=research-root;game=valve"
                      : options.project_client_live_input ==
                                ProjectClientLiveInput::scripted_damage_respawn_check
                            ? "renderer=opengl;auth-provider=steam;stop-after=live-visual-control;live-input=scripted-damage-respawn-check;basedir=research-root;game=valve"
                      : options.project_client_live_input ==
                                ProjectClientLiveInput::scripted_fire_reload_presentation_check
                            ? "renderer=opengl;auth-provider=steam;stop-after=live-visual-control;live-input=scripted-fire-reload-presentation-check;basedir=research-root;game=valve"
                      : options.project_client_live_input ==
                                ProjectClientLiveInput::scripted_fire_reload_check
                            ? "renderer=opengl;auth-provider=steam;stop-after=live-visual-control;live-input=scripted-fire-reload-check;basedir=research-root;game=valve"
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
                ? ";prediction=reference" : "")
        << (options.project_client_mute_glock_fire_sound
                ? ";glock-fire-sound=muted" : "") << "\",\n"
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
        << "  \"remote_audio_peer_enabled\": " << (options.remote_audio_peer ? "true" : "false") << ",\n"
        << "  \"remote_audio_peer_process_id\": " << summary.remote_audio_peer_process_id << ",\n"
        << "  \"remote_audio_peer_runtime_published\": " << (summary.remote_audio_peer_entered ? "true" : "false") << ",\n"
        << "  \"remote_audio_peer_exit_code\": " << (summary.remote_audio_peer_exit ? std::to_string(*summary.remote_audio_peer_exit) : "null") << ",\n"
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
        << ",\n  \"h4_result\": "
        << (summary.project_h4_result
                ? "\"" + *summary.project_h4_result + "\"" : "null")
        << ",\n  \"weapon_result\": "
        << (summary.project_weapon_result
                ? "\"" + *summary.project_weapon_result + "\"" : "null")
        << ",\n  \"fire_reload_result\": "
        << (summary.project_fire_reload_result
                ? "\"" + *summary.project_fire_reload_result + "\"" : "null")
        << ",\n  \"server_confirmed_shots\": ";
    write_optional_number(summary.project_server_confirmed_shots);
    functional << ",\n  \"reload_completions\": ";
    write_optional_number(summary.project_reload_completions);
    functional << ",\n  \"application_outcome\": ";
    write_application_evidence(functional, summary.project_application);
    if (options.project_client_live_input == ProjectClientLiveInput::scripted_damage_respawn_check) {
        functional << ",\n  \"damage_respawn\": {\n    \"result\": "
            << (summary.project_life.result ? "\"" + *summary.project_life.result + "\""
                : "\"damage_death_respawn_implemented_live_pending\"");
        for (std::size_t i = 0; i < kLifeFlags.size(); ++i) {
            functional << ",\n    \"" << kLifeFlags[i] << "\": ";
            write_optional_bool(summary.project_life.flags[i]);
        }
        for (std::size_t i = 0; i < kLifeMetrics.size(); ++i)
            functional << ",\n    \"" << kLifeMetrics[i] << "\": "
                << (summary.project_life.metrics[i] ? "\"" + *summary.project_life.metrics[i] + "\"" : "null");
        functional << "\n  }";
    }
    if (options.project_client_live_input == ProjectClientLiveInput::scripted_fire_reload_presentation_check) {
        functional << ",\n  \"weapon_prediction\": {\n    \"result\": "
            << (summary.project_weapon_prediction.result ? "\"" + *summary.project_weapon_prediction.result + "\"" : "null");
        for (std::size_t i = 0U; i < kWeaponPredictionFlags.size(); ++i) {
            functional << ",\n    \"" << kWeaponPredictionFlags[i] << "\": ";
            write_optional_bool(summary.project_weapon_prediction.flags[i]);
        }
        functional << ",\n    \"crowbar_hit_status\": "
            << (summary.project_weapon_prediction.hit_status ? "\"unavailable\"" : "null") << "\n  }";
    }
    functional << ",\n  \"weapon_selection_queued\": ";
    write_optional_number(summary.project_weapon_selection_queued);
    functional << ",\n  \"weapon_selection_confirmed\": ";
    write_optional_number(summary.project_weapon_selection_confirmed);
    functional << ",\n  \"viewmodel_pixels_distinct\": ";
    write_optional_bool(summary.project_viewmodel_pixels_distinct);
    functional << ",\n  \"hud_pixels_distinct\": ";
    write_optional_bool(summary.project_hud_pixels_distinct);
    functional << ",\n  \"viewmodel_camera_result\": "
        << (summary.project_viewmodel_camera_result
                ? "\"" + *summary.project_viewmodel_camera_result + "\""
                : "null");
    functional << ",\n  \"viewmodel_camera_pixels_valid\": ";
    write_optional_bool(summary.project_viewmodel_camera_pixels_valid);
    functional << ",\n  \"viewmodel_camera_pixel_count\": ";
    write_optional_number(summary.project_viewmodel_camera_pixel_count);
    functional
        << ",\n  \"h4_phases\": [";
    for (std::size_t index = 0U; index < 5U; ++index) {
        if (index != 0U) functional << ',';
        functional << "{\"active_frames\":";
        write_optional_number(summary.project_h4_active_frames[index]);
        functional << ",\"fallback_frames\":";
        write_optional_number(summary.project_h4_fallback_frames[index]);
        functional << ",\"local_steps\":";
        write_optional_number(summary.project_h4_local_steps[index]);
        functional << ",\"corrections\":";
        write_optional_number(summary.project_h4_corrections[index]);
        functional << '}';
    }
    functional << ']'
        << ",\n  \"prediction_presentation\": {\n"
        << "    \"interpolated_frames\": "
        << (summary.project_prediction_interpolated_frames
                ? std::to_string(*summary.project_prediction_interpolated_frames) : "null")
        << ",\n    \"endpoint_frames\": "
        << (summary.project_prediction_endpoint_frames
                ? std::to_string(*summary.project_prediction_endpoint_frames) : "null")
        << ",\n    \"collision_blocked_frames\": "
        << (summary.project_prediction_collision_blocked_frames
                ? std::to_string(*summary.project_prediction_collision_blocked_frames) : "null")
        << ",\n    \"visual_correction_frames\": "
        << (summary.project_prediction_visual_correction_frames
                ? std::to_string(*summary.project_prediction_visual_correction_frames) : "null")
        << ",\n    \"trace_queries\": "
        << (summary.project_prediction_trace_queries
                ? std::to_string(*summary.project_prediction_trace_queries) : "null")
        << ",\n    \"scratch_growths\": "
        << (summary.project_prediction_scratch_growths
                ? std::to_string(*summary.project_prediction_scratch_growths) : "null")
        << ",\n    \"long_stall_frames\": "
        << (summary.project_prediction_long_stall_frames
                ? std::to_string(*summary.project_prediction_long_stall_frames) : "null")
        << ",\n    \"active_time_ms\": "
        << (summary.project_prediction_active_time_ms
                ? std::to_string(*summary.project_prediction_active_time_ms) : "null")
        << ",\n    \"fallback_time_ms\": "
        << (summary.project_prediction_fallback_time_ms
                ? std::to_string(*summary.project_prediction_fallback_time_ms) : "null")
        << ",\n    \"cpu_total_ms\": "
        << (summary.project_prediction_cpu_total_ms
                ? std::to_string(*summary.project_prediction_cpu_total_ms) : "null")
        << ",\n    \"cpu_max_ms\": "
        << (summary.project_prediction_cpu_max_ms
                ? std::to_string(*summary.project_prediction_cpu_max_ms) : "null")
        << ",\n    \"maximum_camera_correction_jump\": "
        << (summary.project_prediction_maximum_camera_jump
                ? std::to_string(*summary.project_prediction_maximum_camera_jump) : "null")
        << ",\n    \"correction_pair_window\": "
        << (summary.project_prediction_correction_pair_window
                ? "\"" + *summary.project_prediction_correction_pair_window + "\""
                : "null")
        << "\n  }"
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
        << "  \"diagnostic_window_status\": \"" << (diagnostic_windows_usable ? "ready" : "incomplete") << "\",\n"
        << "  \"game_time_limit_mode\": \"" << (options.manual_no_time_limit ? "unlimited" : options.manual_duration_seconds ? "timed" : "legacy") << "\",\n"
        << "  \"game_duration_seconds\": " << (options.manual_duration_seconds ? std::to_string(*options.manual_duration_seconds) : "null") << ",\n"
        << "  \"supporting_diagnostics\": \""
        << (supporting_files_written ? "complete" : "incomplete") << "\",\n"
        << "  \"restoration_status\": \"wrapper_pending\",\n"
        << "  \"publication_status\": \"staged_after_process_cleanup\"\n"
        << "}\n";
    const bool summary_written = write_bounded_file(
        *run_output.directory, L"functional-smoke.staged.json",
        functional.str(), 128U * 1'024U);
    return summary_written && supporting_files_written && diagnostic_windows_usable;
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
    windows::BoundedProcessLogLimits manual_log_limits;
    if (options.manual_no_time_limit || options.manual_duration_seconds) {
        manual_log_limits.retained_prefix_bytes = 64U * 1'024U;
        manual_log_limits.maximum_line_length = 16U * 1'024U;
    }
    auto functional_server_log = options.functional_smoke
        ? windows::BoundedProcessLogCapture::create(manual_log_limits)
        : std::optional<windows::BoundedProcessLogCapture>{};
    auto functional_client_log = options.functional_smoke
        ? windows::BoundedProcessLogCapture::create(manual_log_limits)
        : std::optional<windows::BoundedProcessLogCapture>{};
    auto peer_log=options.remote_audio_peer ? windows::BoundedProcessLogCapture::create(manual_log_limits) : std::optional<windows::BoundedProcessLogCapture>{};
    // Retain the exact launched peer handle through every execute_owned early
    // return. Final diagnostics sample it only after owned Job cleanup.
    windows::OwnedProcess peer;
    bool cleanup_phase_reported = false;
    const auto begin_manual_cleanup = [&]() {
        if (!cleanup_phase_reported) {
            manual_phase(options, "owned-cleanup-started");
            cleanup_phase_reported = true;
        }
    };
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
        std::wstring test_player_name;
        if (options.test_start_health) {
            const auto launch = test_start_health_launch(options.run_root, options.fast_manual);
            if (!launch) {
                summary.failure = "test-start-health-run-identity-invalid";
                finalize_duration(); return summary;
            }
            if (!test_start_health_server_arguments(server_spec.arguments, *launch)) {
                summary.failure = "test-start-health-server-arguments-invalid";
                finalize_duration(); return summary;
            }
            test_player_name = launch->player_name;
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
        manual_phase(options, "server-startup");
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

        manual_phase(options, "server-ready");
        const auto server_log_offset_before_client_launch =
            server_log->snapshot().bytes.size();
        if (options.test_start_health) {
            // Readiness can race the pipe reader by a few milliseconds.
            const auto helper_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (std::chrono::steady_clock::now() < helper_deadline &&
                   server.running() && guard.running() &&
                   !test_start_health_server_ready(server_log->snapshot().bytes))
                std::this_thread::sleep_for(std::chrono::milliseconds{50});
            if (!test_start_health_server_ready(server_log->snapshot().bytes)) {
                summary.failure = "test-start-health-helper-startup-not-confirmed";
                if (const auto reason=test_start_health_configuration_reason(server_log->snapshot().bytes);
                    reason && *reason!="ready") summary.failure+="-"+std::string{*reason};
                campaign_job.terminate(120U);
                finalize_duration();
                return summary;
            }
        }
        manual_phase(options, "client-startup");
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
                     ? options.project_client_live_input ==
                               ProjectClientLiveInput::scripted_weapon_check
                           ? L"HLC_M473A"
                     : options.project_client_reference_prediction
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
                if (options.project_client_mute_glock_fire_sound)
                    client_spec.arguments.push_back(L"--mute-glock-fire-sound");
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
                                   ProjectClientLiveInput::scripted_weapon_check
                               ? L"scripted-weapon-check"
                         : options.project_client_live_input ==
                                   ProjectClientLiveInput::scripted_damage_respawn_check
                               ? L"scripted-damage-respawn-check"
                         : options.project_client_live_input ==
                                   ProjectClientLiveInput::scripted_fire_reload_presentation_check
                               ? L"scripted-fire-reload-presentation-check"
                         : options.project_client_live_input ==
                                   ProjectClientLiveInput::scripted_fire_reload_check
                               ? L"scripted-fire-reload-check"
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
                    if (options.manual_no_time_limit) {
                        client_spec.arguments.push_back(L"--live-session-unlimited");
                    } else {
                        client_spec.arguments.insert(
                            client_spec.arguments.end(),
                            {L"--live-session-seconds",
                             std::to_wstring(options.manual_duration_seconds.value_or(client_duration))});
                    }
                }
            }
        } else {
            client_spec.arguments = {
                L"-steam", L"-game", L"valve", L"-windowed", L"-w", L"800",
                L"-h", L"600", L"+name", L"HLC_SMOKE", L"+connect",
                L"127.0.0.1:" + std::to_wstring(options.server_port), L"-nojoy"};
        }
        if (options.test_start_health) {
            if (!test_start_health_client_arguments(client_spec.arguments, test_player_name)) {
                summary.failure = "test-start-health-client-name-missing";
                finalize_duration(); return summary;
            }
        }
        std::optional<hlclient::core::RemoteAudioPeerPlan> peer_plan;
        if(options.remote_audio_peer) {
            peer_plan=hlclient::core::remote_audio_peer_plan(client_spec.arguments);
            if(!peer_plan || !peer_log) {summary.failure="remote-audio-peer-plan-or-log-failed"; finalize_duration(); return summary;}
            client_spec.arguments=peer_plan->listener;
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
        if(peer_plan) {
            auto peer_spec=client_spec;
            peer_spec.arguments=peer_plan->mover;
            peer_spec.stdout_handle=peer_log->inherited_write_handle();
            peer_spec.stderr_handle=peer_log->inherited_write_handle();
            auto launched=campaign_job.launch(peer_spec);
            peer=std::move(launched.first);
            summary.remote_audio_peer_process_id=launched.second.process_id;
            peer_log->close_parent_write_handle();
            if(!launched.second) {summary.failure="remote-audio-peer-launch-failed"; campaign_job.terminate(120U); finalize_duration(); return summary;}
            ++summary.processes_started;
            const std::array<std::uint32_t,2> expected{client.process_id(),peer.process_id()};
            if(!exact_process_snapshot_matches(environment.client,expected,summary.failure)) {campaign_job.terminate(120U); finalize_duration(); return summary;}
        }
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

        bool runtime_phase_reported = false;
        const auto client_wait_started = std::chrono::steady_clock::now();
        const auto client_deadline = client_wait_started +
            std::chrono::seconds{options.project_client_stock_signon ? 60 : 30};
        const bool manual_timing = options.manual_no_time_limit || options.manual_duration_seconds;
        const auto client_wait_allowed = [&]() {
            if (!manual_timing) return std::chrono::steady_clock::now() < client_deadline;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - client_wait_started).count();
            return !hlclient::core::manual_client_wait_expired(
                static_cast<std::uint64_t>(elapsed), runtime_phase_reported, options.manual_duration_seconds);
        };
        while (client_wait_allowed() &&
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
                if (summary.project_live_service_payloads_received && !runtime_phase_reported) {
                    manual_phase(options, "runtime-observed");
                    runtime_phase_reported = true;
                }
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
        begin_manual_cleanup();
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
        if(options.remote_audio_peer) {
            if(peer.running()) peer.terminate(0U);
            summary.remote_audio_peer_exit=peer.wait(std::chrono::seconds{5});
            if(!summary.remote_audio_peer_exit || *summary.remote_audio_peer_exit!=0U) {
                summary.failure="remote-audio-peer-finalization-failed"; finalize_duration(); return summary;
            }
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
    begin_manual_cleanup();
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
        std::optional<windows::BoundedProcessLogSnapshot> peer_snapshot;
        if (options.remote_audio_peer &&
            summary.remote_audio_peer_process_id != 0U) {
            if (summary.cleanup_exact && !summary.remote_audio_peer_exit &&
                peer.valid()) {
                // The Job already proved zero children; this is observation,
                // not another termination or a different outcome decision.
                summary.remote_audio_peer_exit =
                    peer.wait(std::chrono::milliseconds{100});
            }
            if (peer_log) {
                peer_snapshot = peer_log->finish();
                apply_remote_audio_peer_observation(summary, *peer_snapshot);
            } else {
                peer_snapshot.emplace();
                peer_snapshot->capture_failed = true;
                peer_snapshot->native_error = ERROR_NOT_ENOUGH_MEMORY;
            }
        } else if (peer_log) {
            // No peer inherited the writer; close it without publishing an
            // empty log as evidence of a launched client.
            (void)peer_log->finish();
        }
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
            options, summary, server_snapshot, client_snapshot, guard_snapshot,
            peer_snapshot);
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

void print_application_evidence(const ApplicationEvidence& values) {
    for (std::size_t i = 0U; i < values.size(); ++i) {
        std::string key{"application-"};
        key += kApplicationMetrics[i];
        std::replace(key.begin(), key.end(), '_', '-');
        print_key_value(key, values[i].value_or("unavailable"));
    }
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
    if (options->validate_remote_audio_peer_contract) {
        const std::vector<std::wstring> args{L"--renderer",L"opengl",L"--connect",L"127.0.0.1:27243",
            L"--stop-after",L"live-visual-control",L"--live-input",L"keyboard-mouse",L"--game",L"valve",
            L"--name",L"fixture",L"--auth-provider",L"steam",L"--steam-api-runtime",L"D:/fixture/steam_api.dll",
            L"--basedir",L"D:/fixture",L"--live-session-seconds",L"45"};
        const auto plan=hlclient::core::remote_audio_peer_plan(args);
        bool ok=plan.has_value();
        if (plan) for (const auto& client : {plan->listener, plan->mover}) {
            ok = ok && client.size() == args.size() &&
                std::find(client.begin(), client.end(), L"--audio-on-focus-loss") == client.end() &&
                std::find(client.begin(), client.end(), L"--audio-volume") == client.end();
        }
        auto unlimited_args = args;
        unlimited_args.resize(unlimited_args.size() - 2U);
        unlimited_args.push_back(L"--live-session-unlimited");
        ok = ok && hlclient::core::remote_audio_peer_plan(unlimited_args).has_value();
        std::vector<std::wstring> timing_fixture{L"fixture", L"--functional-smoke", L"--project-client-stock-signon",
            L"--functional-confirmation-token", std::wstring{kFunctionalSmokeToken},
            L"--research-root",L"D:/fixture/research",L"--client",L"D:/fixture/hlclient.exe",
            L"--server",L"D:/fixture/hlds.exe",L"--relay",L"D:/fixture/relay.exe",
            L"--isolation-guard",L"D:/fixture/guard.exe",L"--app-manifest",L"D:/fixture/appmanifest_70.acf",
            L"--steam-api-runtime",L"D:/fixture/api.dll",L"--game",L"valve",L"--map",L"crossfire",
            L"--run-root",L"D:/fixture/run",L"--relay-port",L"27242",L"--server-port",L"27243",
            L"--server-profile-id",L"steam-hlds-10210-no-mode-banner-v1",
            L"--project-client-stop",L"live-visual-control",L"--project-client-live-input",L"keyboard-mouse",
            L"--validation-mode",L"fast", L"--manual-duration-seconds", L"300"};
        const auto parse_timing = [&]() {
            std::vector<wchar_t*> pointers;
            for (auto& part : timing_fixture) pointers.push_back(part.data());
            return parse_options(static_cast<int>(pointers.size()), pointers.data());
        };
        for (const auto seconds : {L"1", L"45", L"300", L"86400"}) {
            timing_fixture.back() = seconds;
            const auto timing = parse_timing();
            ok = ok && timing && timing->manual_duration_seconds && !timing->manual_no_time_limit;
        }
        for (const auto seconds : {L"0", L"-1", L"86401", L"999999999999999999999", L"1.5"}) {
            timing_fixture.back() = seconds; ok = ok && !parse_timing();
        }
        timing_fixture.back() = L"300";
        timing_fixture.push_back(L"--manual-no-time-limit"); ok = ok && !parse_timing();
        timing_fixture.erase(timing_fixture.end() - 3, timing_fixture.end() - 1);
        auto unlimited_timing = parse_timing();
        ok = ok && unlimited_timing && unlimited_timing->manual_no_time_limit && !unlimited_timing->manual_duration_seconds;
        const auto input_at = std::find(timing_fixture.begin(), timing_fixture.end(), L"--project-client-live-input");
        *std::next(input_at) = L"scripted-damage-respawn-check"; ok = ok && !parse_timing();
        *std::next(input_at) = L"keyboard-mouse";
        const auto validation_at = std::find(timing_fixture.begin(), timing_fixture.end(), L"--validation-mode");
        timing_fixture.erase(validation_at, std::next(validation_at, 2)); ok = ok && !parse_timing();
        ActiveSummary failed_listener;
        failed_listener.failure = "project-client-nonzero-exit";
        failed_listener.client_exit_code = 2U;
        windows::BoundedProcessLogSnapshot peer_fixture;
        peer_fixture.bytes =
            "connection_accepted=true client_world_state_published=true\n"
            "live_application_outcome result=error primary_error=runtime_record_failed "
            "runtime_error=decoder_failed parser_error=unsupported_opcode opcode=3\n"
            "L fixture: \"private-player\" connected, address \"private-address\"\n";
        apply_remote_audio_peer_observation(failed_listener, peer_fixture);
        const auto peer_excerpt = functional_diagnostic_excerpt(peer_fixture.bytes);
        ok = ok && failed_listener.remote_audio_peer_entered &&
            failed_listener.failure == "project-client-nonzero-exit" &&
            failed_listener.client_exit_code == 2U &&
            peer_excerpt.find("primary_error=runtime_record_failed") != std::string::npos &&
            peer_excerpt.find("private-player") == std::string::npos &&
            peer_excerpt.find("private-address") == std::string::npos;
        windows::BoundedProcessLogSnapshot no_peer_evidence;
        ActiveSummary unobserved_peer;
        apply_remote_audio_peer_observation(unobserved_peer, no_peer_evidence);
        ok = ok && !unobserved_peer.remote_audio_peer_entered;
        print_key_value("remote-audio-peer-contract",ok ? "passed" : "failed");
        print_key_value("process-launches","0");
        return ok ? 0 : 2;
    }
    if (options->validate_test_start_health_contract) {
        std::vector<std::wstring> fixture{L"fixture", L"--functional-smoke", L"--project-client-stock-signon",
            L"--functional-confirmation-token", std::wstring{kFunctionalSmokeToken},
            L"--research-root",L"D:/fixture/research",L"--client",L"D:/fixture/hlclient.exe",
            L"--server",L"D:/fixture/hlds.exe",L"--relay",L"D:/fixture/relay.exe",
            L"--isolation-guard",L"D:/fixture/guard.exe",L"--app-manifest",L"D:/fixture/appmanifest_70.acf",
            L"--steam-api-runtime",L"D:/fixture/steam_api.dll",L"--game",L"valve",L"--map",L"crossfire",
            L"--run-root",L"D:/fixture/0123456789abcdef0123456789abcdef",L"--relay-port",L"27242",
            L"--server-port",L"27243",L"--server-profile-id",L"steam-hlds-10210-no-mode-banner-v1",
            L"--project-client-stop",L"live-visual-control",L"--project-client-live-input",L"keyboard-mouse",
            L"--project-client-prediction",L"reference",L"--test-start-health",L"50"};
        const auto parse_fixture=[&]() {
            std::vector<wchar_t*> pointers; for(auto& part:fixture) pointers.push_back(part.data());
            return parse_options(static_cast<int>(pointers.size()),pointers.data());
        };
        const auto parsed_fixture=parse_fixture();
        bool parser_good=parsed_fixture && parsed_fixture->test_start_health;
        const auto change=[&](std::wstring_view flag,std::wstring replacement) {
            const auto at=std::find(fixture.begin(),fixture.end(),flag); *(at+1)=std::move(replacement);
        };
        change(L"--test-start-health",L"100"); parser_good=parser_good && !parse_fixture();
        change(L"--test-start-health",L"50"); change(L"--map",L"boot_camp"); parser_good=parser_good && !parse_fixture();
        change(L"--map",L"crossfire"); change(L"--project-client-prediction",L"off"); parser_good=parser_good && !parse_fixture();
        change(L"--project-client-prediction",L"reference"); change(L"--project-client-live-input",L"scripted-damage-respawn-check"); parser_good=parser_good && !parse_fixture();
        const auto launch=test_start_health_launch(fs::path{L"D:/fixture/0123456789abcdef0123456789abcdef"});
        change(L"--project-client-live-input",L"keyboard-mouse");
        fixture.insert(fixture.end(), {L"--validation-mode", L"fast"});
        const auto fast_options = parse_fixture();
        parser_good = parser_good && fast_options && fast_options->fast_manual;
        fixture.push_back(L"--project-client-mute-glock-fire-sound");
        const auto muted_options = parse_fixture();
        parser_good = parser_good && muted_options &&
            muted_options->project_client_mute_glock_fire_sound;
        fixture.push_back(L"--project-client-mute-glock-fire-sound");
        parser_good = parser_good && !parse_fixture();
        fixture.pop_back();
        change(L"--project-client-live-input",L"scripted-fire-reload-check");
        parser_good = parser_good && !parse_fixture();
        change(L"--project-client-live-input",L"keyboard-mouse");
        fixture.pop_back();
        change(L"--validation-mode",L"strict");
        const auto strict_options = parse_fixture();
        parser_good = parser_good && strict_options && !strict_options->fast_manual;
        change(L"--validation-mode",L"unknown");
        parser_good = parser_good && !parse_fixture();
        const fs::path prepared_metamod_fixture{
            L"D:/DEV/CPP/HLC-steamcfg-5e48b7c1/build/test-start-health-deps/addons/metamod/dlls/metamod.dll"};
        const auto prepared_launch=test_start_health_launch(
            fs::path{L"D:/fixture/0123456789abcdef0123456789abcdef"},true);
        parser_good = parser_good && prepared_launch && prepared_launch->player_name==launch->player_name &&
            prepared_launch->server_arguments==std::vector<std::wstring>{
                L"+localinfo",L"hlc_test_run",L"0123456789abcdef0123456789abcdef",
                L"+localinfo",L"hlc_test_profile",L"test_server_assisted"} &&
            !test_start_health_launch(fs::path{L"D:/fixture/../bad"},true);
        bool good=parser_good && launch && launch->player_name==L"HLC50_0123456789abcdef01234567" &&
            launch->server_arguments.size()==11U &&
            launch->server_arguments[0]==L"-dll" &&
            launch->server_arguments[1]==L"addons/hlclient_test50/0123456789abcdef0123456789abcdef/metamod.dll" &&
            launch->server_arguments[4]==L"addons/hlclient_test50/0123456789abcdef0123456789abcdef/config.ini" &&
            !test_start_health_launch(fs::path{L"D:/fixture/../bad"});
        std::vector<std::wstring> args{L"--name",L"old",L"--prediction",L"reference"};
        good=good && test_start_health_client_arguments(args,launch->player_name) &&
            args[1]==launch->player_name && args[4]==L"--test-start-health" && args[5]==L"50";
        std::vector<std::wstring> missing{L"--name"};
        good=good && !test_start_health_client_arguments(missing,L"test");
        std::vector<std::wstring> server_args{L"-console",L"-game",L"valve",L"+map",L"crossfire",L"+status"};
        good=good && test_start_health_server_arguments(server_args,*launch) &&
            server_args==std::vector<std::wstring>{L"-console",L"-game",L"valve",
                L"-dll",L"addons/hlclient_test50/0123456789abcdef0123456789abcdef/metamod.dll",
                L"+localinfo",L"mm_configfile",L"addons/hlclient_test50/0123456789abcdef0123456789abcdef/config.ini",
                L"+localinfo",L"hlc_test_run",L"0123456789abcdef0123456789abcdef",
                L"+localinfo",L"hlc_test_profile",L"test_server_assisted",
                L"+map",L"crossfire",L"+meta",L"require",L"HLC50",L"+status"};
        std::vector<std::wstring> bad_server{L"+map",L"crossfire"};
        good=good && !test_start_health_server_arguments(bad_server,*launch) && bad_server.size()==2U;
        // Independent compatibility model of the installed stock stuffcmds:
        // delimiters are characters, even inside an argument. Do not use a
        // modern token-based engine parser to validate this launch contract.
        const auto stock_startup_commands=[](const std::vector<std::wstring>& arguments) {
            std::wstring text;
            for (const auto& argument:arguments) { text+=argument; text+=L' '; }
            std::vector<std::wstring> commands;
            for (auto at=text.find(L'+'); at!=std::wstring::npos;) {
                const auto end=text.find_first_of(L"+-",at+1U);
                auto command=text.substr(at+1U,end==std::wstring::npos ? end : end-at-1U);
                while (!command.empty() && command.back()==L' ') command.pop_back();
                commands.push_back(std::move(command));
                at=end==std::wstring::npos ? end : text.find(L'+',end);
            }
            return commands;
        };
        good=good && stock_startup_commands({L"+localinfo",L"hlc_test_profile",L"test-server-assisted",
                L"+map",L"crossfire"})==std::vector<std::wstring>{L"localinfo hlc_test_profile test",L"map crossfire"};
        const auto commands=stock_startup_commands(server_args);
        good=good && commands==std::vector<std::wstring>{
            L"localinfo mm_configfile addons/hlclient_test50/0123456789abcdef0123456789abcdef/config.ini",
            L"localinfo hlc_test_run 0123456789abcdef0123456789abcdef",
            L"localinfo hlc_test_profile test_server_assisted",L"map crossfire",L"meta require HLC50",L"status"};
        std::vector<std::wstring> complete_server_args{L"-console",L"-game",L"valve",L"-port",L"27243",
            L"+ip",L"127.0.0.1",L"+log",L"on",L"+map",L"crossfire",L"+maxplayers",L"8",L"+sv_lan",L"1",L"+status"};
        auto fast_server_args=complete_server_args;
        // The installed stock launcher's CheckParm uses strstr: even a quoted
        // path containing "-steam" forces GUI/AdminServer before -console is
        // checked. Model that independently of our argv-aware options parser.
        const auto stock_selects_admin_server=[](const std::wstring& command_line) {
            return command_line.find(L"-steam")!=std::wstring::npos ||
                command_line.find(L"-console")==std::wstring::npos;
        };
        good=good && stock_selects_admin_server(L"hlds.exe -console -dll \""+
            prepared_metamod_fixture.generic_wstring()+L"\"") &&
            !stock_selects_admin_server(L"hlds.exe -console -game valve") &&
            stock_selects_admin_server(L"hlds.exe -console -steam");
        if (prepared_launch) {
            good=test_start_health_server_arguments(fast_server_args,*prepared_launch) && good;
            const auto command_line=windows::build_windows_command_line(
                fs::path{L"D:/DEV/HLCLIENT-RESEARCH/Half-Life/hlds.exe"},fast_server_args);
            const bool fast_console_safe=!stock_selects_admin_server(command_line) &&
                command_line.find(prepared_metamod_fixture.generic_wstring())==std::wstring::npos &&
                std::find(fast_server_args.begin(),fast_server_args.end(),L"-dll")==fast_server_args.end();
            std::cout << "[stock-runtime-orchestrator] fast-stock-console-regression="
                      << (fast_console_safe ? "passed" : "failed") << '\n';
            good=fast_console_safe && good;
            good=good && stock_startup_commands(fast_server_args)==std::vector<std::wstring>{
                L"ip 127.0.0.1",L"log on",L"localinfo hlc_test_run 0123456789abcdef0123456789abcdef",
                L"localinfo hlc_test_profile test_server_assisted",L"map crossfire",L"maxplayers 8",L"sv_lan 1",
                L"meta require HLC50",L"status"};
        } else { good=false; }
        good=good && test_start_health_server_arguments(complete_server_args,*launch) &&
            stock_startup_commands(complete_server_args)==std::vector<std::wstring>{L"ip 127.0.0.1",L"log on",
                L"localinfo mm_configfile addons/hlclient_test50/0123456789abcdef0123456789abcdef/config.ini",
                L"localinfo hlc_test_run 0123456789abcdef0123456789abcdef",
                L"localinfo hlc_test_profile test_server_assisted",L"map crossfire",L"maxplayers 8",L"sv_lan 1",
                L"meta require HLC50",L"status"};
        constexpr std::string_view server_evidence="L fixture: [hlclient-test-health] run=0123456789abcdef0123456789abcdef requested_start_health=50 server_setup_applied=true max_health=100 max_health_source=server_entvars\n";
        const auto retained=functional_diagnostic_excerpt(server_evidence);
        good=good && retained.find("run=0123456789abcdef0123456789abcdef requested_start_health=50 server_setup_applied=true")!=std::string::npos;
        constexpr std::string_view client_evidence="[test-start-health] requested_start_health=50 client_observed_health=50 source=fresh_clientdata result=ready\n";
        good=good && functional_diagnostic_excerpt(client_evidence).find(client_evidence)!=std::string::npos;
        constexpr std::string_view failure_evidence="[hlclient-test-health] run=0123456789abcdef0123456789abcdef requested_start_health=50 server_setup_applied=spawn_validation_failed max_health=unavailable max_health_source=unavailable\n";
        good=good && functional_diagnostic_excerpt(failure_evidence).find(failure_evidence)!=std::string::npos;
        constexpr std::string_view stage_evidence="[hlclient-test-health-stage] stage=attached\n[hlclient-test-health-stage] stage=server_activated configured=false\n";
        const auto stage_retained=functional_diagnostic_excerpt(stage_evidence);
        good=good && stage_retained.find("stage=attached")!=std::string::npos &&
            stage_retained.find("stage=server_activated configured=false")!=std::string::npos;
        good=good && !test_start_health_server_ready(stage_evidence) &&
            !test_start_health_server_ready("[hlclient-test-health-stage] stage=attached\n") &&
            test_start_health_server_ready("[hlclient-test-health-stage] stage=attached\n[hlclient-test-health-stage] stage=server_activated configured=true\n");
        for (const auto reason:{"run_missing","run_invalid","profile_missing","profile_mismatch",
                "globals_unavailable","deathmatch_unavailable","client_limit_invalid","map_mismatch","ready"}) {
            const auto diagnostic=std::string{"[hlclient-test-health-config] reason="}+reason+"\n";
            good=good && test_start_health_configuration_reason(diagnostic)==reason &&
                functional_diagnostic_excerpt(diagnostic).find(diagnostic)!=std::string::npos &&
                !test_start_health_server_ready(diagnostic);
        }
        good=good && !test_start_health_configuration_reason("[hlclient-test-health-config] reason=profile_mismatch_private_value\n") &&
            !test_start_health_configuration_reason("prefix[hlclient-test-health-config] reason=ready\n") &&
            test_start_health_configuration_reason("[hlclient-test-health-config] reason=profile_missing\n[hlclient-test-health-config] reason=ready\n")=="ready";
        constexpr std::string_view deadline="[test-start-health] result=failed reason=fresh_server_50_not_observed client_observed_health=100\n";
        good=good && functional_diagnostic_excerpt(deadline).find(deadline)!=std::string::npos;
        good=good && functional_diagnostic_excerpt("[META] ERROR: Failed to load plugin\n").find("Failed to load plugin")!=std::string::npos;
        print_key_value("test-start-health-contract",good ? "passed" : "failed");
        print_key_value("stock-processes-started","0"); print_key_value("result",good ? "success" : "failed");
        return good ? 0 : 2;
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
        constexpr std::string_view application_fixture =
            "[info] live_application_outcome result=error primary_error=runtime_record_failed "
            "runtime_error=decoder_failed parser_error=unsupported_opcode opcode=255 cursor=1032 "
            "record=361 source_sequence=380 scripted_coverage=not_evaluated prediction_coverage=limited "
            "inventory_notifications=2 feedback_rows=1 brush_candidates=9 brush_resolved=8 brush_prepared=71 "
            "brush_hidden=1 brush_material_unsupported=1 brush_submitted=6 brush_culled=1 brush_uploads=1 "
            "texture_uploads=94 brush_reject_entity=80 brush_reject_slot=27 brush_reject_submodel=1 "
            "brush_reject_reason=hidden_by_effects brush_reject_revision=361 "
            "collision_revision=362 collision_brushes=4 ground_entity=42 ground_model=1 "
            "ground_normal_x=0 ground_normal_y=0 ground_normal_z=1 grounded_server=true grounded_local=true "
            "movement_steps=3 brush_server_changes=8 brush_render_changes=8 base_velocity=observed_zero "
            "support_policy=current_server_frame_no_pusher_extrapolation prediction_fallbacks=2 "
            "prediction_raw_error=0.03125 prediction_camera_jump=0.02 prediction_ground_status=ready "
            "prediction_reason=active prediction_last_fallback=collision_solid_field_unavailable "
            "brush_server_last_entity=42 brush_server_last_model=21 brush_render_last_entity=42 brush_render_last_model=21 "
            "audio_backend=audio_unavailable audio_error=device_unavailable audio_start_messages=7 audio_stop_messages=2 "
            "audio_output_frames=48000 audio_underruns=unmeasured weapon_audio_fire=3 weapon_audio_muted=1 "
            "weapon_audio_marker_duplicates=2 weapon_audio_delivery_duplicates=4 weapon_audio_timeline_corrections=5\n";
        const auto application_observed = observe_project_client_signon_log(application_fixture);
        const auto application_retained = functional_diagnostic_excerpt(application_fixture);
        if (application_observed.application[0U] != "error" ||
            application_observed.application[1U] != "runtime_record_failed" ||
            application_observed.application[6U] != "361" ||
            application_observed.application[9U] != "limited" ||
            application_observed.application[10U] != "2" ||
            application_observed.application[55U] != "9" ||
            application_observed.application[67U] != "hidden_by_effects" ||
            application_observed.application[68U] != "361" ||
            application_observed.application[69U] != "362" ||
            application_observed.application[71U] != "42" ||
            application_observed.application[81U] != "observed_zero" ||
            application_observed.application[84U] != "0.03125" ||
            application_observed.application[88U] != "collision_solid_field_unavailable" ||
            application_observed.application[90U] != "21" ||
            application_observed.application[92U] != "21" ||
            application_observed.application[93U] != "audio_unavailable" ||
            application_observed.application[94U] != "device_unavailable" ||
            application_observed.application[109U] != "48000" ||
            application_observed.application[111U] != "unmeasured" ||
            application_observed.application[115U] != "3" ||
            application_observed.application[127U] != "1" ||
            application_observed.application[128U] != "2" ||
            application_observed.application[129U] != "4" ||
            application_observed.application[130U] != "5" ||
            !valid_application_metric("ground_normal_x","-0.894427") ||
            valid_application_metric("prediction_raw_error","../private") ||
            valid_application_metric("prediction_raw_error","nan") ||
            valid_application_metric("primary_error","private.config") ||
            application_retained.find(application_fixture.substr(7U)) == std::string::npos ||
            observe_project_client_signon_log("live_application_outcome result=error primary_error=\"private\"\n")
                .application[1U]) {
            std::cerr << "Application primary-error retention contract failed\n";
            return 1;
        }
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
            "replayed_commands=45 moving_presented_changes=80 "
            "interpolated_frames=200 endpoint_frames=45 "
            "collision_blocked_frames=5 visual_correction_frames=3 "
            "presentation_trace_queries=240 "
            "presentation_scratch_growths=1 "
            "long_stall_frames=2 active_time_ms=1700.5 "
            "fallback_time_ms=30.25 presentation_cpu_total_ms=14.5 "
            "presentation_cpu_max_ms=0.125 "
            "maximum_camera_correction_jump=0.03125 "
            "correction_pair_window=1:2:100:120:0.5:interpolated\n";
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
        auto project_visual_h4_fixture = project_visual_jump_duck_fixture;
        replace_all(project_visual_h4_fixture,
                    "result=fresh_project_client_jump_duck_server_verified",
                    "result=live_local_prediction_and_reconciliation_verified");
        project_visual_h4_fixture +=
            "[info] live_prediction mode=reference "
            "result=live_local_prediction_and_reconciliation_verified "
            "state=active active_frames=300 accepted_corrections=40 "
            "replayed_commands=20\n"
            "[info] live_h4_phase phase=jump_hold active_frames=40 "
            "fallback_frames=0 local_steps=30 corrections=8\n"
            "[info] live_h4_phase phase=duck_crouch_walk "
            "active_frames=80 fallback_frames=3 local_steps=75 "
            "corrections=12\n"
            "[info] live_h4_prediction "
            "result=live_jump_duck_crouchwalk_prediction_verified "
            "jump_rising_frames=20 stable_crouch_frames=50\n";
        const auto project_visual_h4 = observe_project_client_signon_log(
            project_visual_h4_fixture);
        const auto project_visual_h4_diagnostic =
            functional_diagnostic_excerpt(project_visual_h4_fixture);
        auto project_visual_h4_partial_fixture = project_visual_h4_fixture;
        replace_all(project_visual_h4_partial_fixture,
                    "result=live_jump_duck_crouchwalk_prediction_verified",
                    "result=crouch_walk_prediction_context_blocked");
        const auto project_visual_h4_partial =
            observe_project_client_signon_log(project_visual_h4_partial_fixture);
        auto project_visual_weapon_fixture = project_visual_prediction_fixture;
        replace_all(project_visual_weapon_fixture, "scripted-speed-check",
                    "scripted-weapon-check");
        project_visual_weapon_fixture +=
            "[info] live_weapon_presentation "
            "result=live_viewmodel_weapon_selection_and_basic_hud_verified "
            "viewmodel_frames=120 hud_frames=130 selection_queued=1 "
            "selection_confirmed=1 viewmodel_pixel_tested=1 "
            "viewmodel_pixels_distinct=1 hud_pixel_tested=1 "
            "hud_pixels_distinct=1 pending_selection=none model_index=17 "
            "active_id=2 hud_hash=123 canonical_hash=456\n";
        project_visual_weapon_fixture +=
            "live_viewmodel_camera result=live_viewmodel_camera_space_verified "
            "binding_status=ready_studio render_space=camera_local "
            "pitch=0 yaw=0 draw_count=120 pixel_observation_valid=1 "
            "pixel_count=321 bounds=1,2,3,4 resource_revision=1 "
            "model_slot=17 projection_profile=world_optics_camera_local "
            "probes=3\n";
        const auto project_visual_weapon =
            observe_project_client_signon_log(project_visual_weapon_fixture);
        auto project_visual_weapon_partial_fixture = project_visual_weapon_fixture;
        replace_all(project_visual_weapon_partial_fixture,
                    "result=live_viewmodel_weapon_selection_and_basic_hud_verified",
                    "result=live_viewmodel_hud_verified_selection_pending");
        replace_all(project_visual_weapon_partial_fixture,
                    "selection_confirmed=1", "selection_confirmed=0");
        const auto project_visual_weapon_partial =
            observe_project_client_signon_log(project_visual_weapon_partial_fixture);
        auto project_visual_fire_fixture = project_visual_weapon_fixture;
        replace_all(project_visual_fire_fixture, "scripted-weapon-check",
                    "scripted-fire-reload-check");
        project_visual_fire_fixture +=
            "[info] live_fire_reload "
            "result=live_primary_fire_reload_and_weapon_animation_verified "
            "attack_generated=16 reload_generated=4 attack_new_submitted=16 "
            "reload_new_submitted=4 server_confirmed_shots=3 "
            "reload_starts=1 reload_completions=1 clip_before_fire=17 "
            "clip_after_fire=14 clip_after_reload=17 "
            "reserve_before_reload=68 reserve_after_reload=65 "
            "svc_weaponanim=5 glock_fire_animation=3 "
            "glock_reload_animation=1 crowbar_attack_animation=1\n";
        const auto project_visual_fire =
            observe_project_client_signon_log(project_visual_fire_fixture);
        const auto project_visual_fire_diagnostic =
            functional_diagnostic_excerpt(project_visual_fire_fixture);
        auto project_visual_fire_partial_fixture = project_visual_fire_fixture;
        replace_all(project_visual_fire_partial_fixture,
            "result=live_primary_fire_reload_and_weapon_animation_verified",
            "result=primary_fire_verified_reload_pending");
        const auto project_visual_fire_partial =
            observe_project_client_signon_log(project_visual_fire_partial_fixture);
        auto project_visual_fire_bad_count_fixture = project_visual_fire_fixture;
        replace_all(project_visual_fire_bad_count_fixture,
                    "server_confirmed_shots=3", "server_confirmed_shots=true");
        const auto project_visual_fire_bad_count =
            observe_project_client_signon_log(project_visual_fire_bad_count_fixture);
        auto b1_fixture = project_visual_fire_fixture;
        replace_all(b1_fixture, "scripted-fire-reload-check", "scripted-fire-reload-presentation-check");
        b1_fixture += "[info] live_weapon_prediction result=live_client_predicted_weapon_presentation_verified "
            "server_fire_verified=1 server_reload_verified=1 glock_fire_animation_presented=1 "
            "glock_reload_animation_presented=1 glock_recoil_presented=1 crowbar_swing_presented=1 "
            "hud_server_state_updated=1 crowbar_hit_status=unavailable\n";
        const auto b1 = observe_project_client_signon_log(b1_fixture);
        const auto b1_excerpt = functional_diagnostic_excerpt(b1_fixture);
        auto b1_bad_fixture = b1_fixture;
        replace_all(b1_bad_fixture, "glock_recoil_presented=1", "glock_recoil_presented=banana");
        const auto b1_bad = observe_project_client_signon_log(b1_bad_fixture);
        auto b1_partial_fixture = b1_fixture;
        replace_all(b1_partial_fixture, "glock_recoil_presented=1", "glock_recoil_presented=0");
        const auto b1_partial = observe_project_client_signon_log(b1_partial_fixture);
        auto c_fixture = b1_fixture;
        replace_all(c_fixture, "scripted-fire-reload-presentation-check", "scripted-damage-respawn-check");
        c_fixture += "[info] live_damage_respawn result=live_death_respawn_verified_damage_pending "
            "application_runtime_result=completed phase=complete blocker=none generation=1 life_epoch=2 "
            "respawn_input_submitted=1 server_alive=true same_session=1 glock_bound=1 crowbar_bound=1 "
            "feature_verified=true damage_live=not_observed post_respawn_frames=30 "
            "local_deaths=1 post_respawn_commands=32 post_respawn_samples=12\n";
        const auto c_valid = observe_project_client_signon_log(c_fixture);
        auto c_bad_fixture = c_fixture;
        replace_all(c_bad_fixture, "server_alive=true", "server_alive=banana");
        const auto c_bad = observe_project_client_signon_log(c_bad_fixture);
        auto c_error_fixture = c_fixture;
        replace_all(c_error_fixture, "application_runtime_result=completed", "application_runtime_result=error");
        const auto c_error = observe_project_client_signon_log(c_error_fixture);
        auto c_missing_fixture = c_fixture;
        replace_all(c_missing_fixture, "same_session=1", "inert=1");
        const auto c_missing = observe_project_client_signon_log(c_missing_fixture);
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
        for (std::size_t index = 0U; index < 8U; ++index) {
            project_visual_logging_fixture +=
                "[info] live_weapon_observation generation=1 active_id=" +
                std::to_string(index) + " viewmodel_status=ready_studio\n";
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
        // Independent, project-owned numeric evidence. Neither fixture
        // instantiates a game module, reads installed assets nor opens a peer.
        const auto visibility_fixture_row = [](const std::size_t ordinal,
                                              const std::uint32_t entity) {
            return std::string{"[remote-player-visibility] generation=1 source="} +
                std::to_string(100U + ordinal) + " ordinal=" + std::to_string(ordinal) +
                " publication=" + std::to_string(200U + ordinal) + " entity=" +
                std::to_string(entity) + " model=87 stage=queued_visible game=1 "
                "server-seconds=47.0596 distance=42.545 effects=0 render-mode=0 "
                "camera=202.515625,1244.140625,-1791.973267 "
                "target=202.440644,1243.143440,-1791.908728 near=0.1 "
                "bounds=185.984375,1196.109375,-1819.968750;217.984375,1228.109375,-1747.968750 "
                "static-light=0.125,0.25,0.5\n";
        };
        const auto occurrence_count = [](const std::string_view haystack,
                                         const std::string_view needle) {
            std::size_t count = 0U;
            for (auto at = haystack.find(needle); at != std::string_view::npos;
                 at = haystack.find(needle, at + needle.size())) ++count;
            return count;
        };
        bool visibility_retention_valid = true;
        for (const auto entity : {1U, 2U}) {
            std::string fixture{
                "[remote-player-visibility-summary] changes=1 retained=1 dropped=0 evidence=cpu-frame-only\n"};
            for (std::size_t ordinal = 0U; ordinal < 100U; ++ordinal)
                fixture += "[info] live_visual_camera_sample synthetic=bounded\n";
            // Old verbose summaries previously exhausted the whole excerpt
            // before late visibility rows were considered.
            for (std::size_t ordinal = 0U; ordinal < 8U; ++ordinal)
                fixture += "live_application_outcome result=error primary_error=fixture_runtime_error " +
                    std::string(5'000U, 'x') + "\n";
            for (std::size_t ordinal = 0U; ordinal < 24U; ++ordinal)
                fixture += (entity == 2U ? "[info] " : "") + visibility_fixture_row(ordinal, entity);
            fixture += "[info] [remote-player-visibility-summary] changes=24 retained=16 dropped=8 evidence=cpu-frame-only\r\n";
            fixture += visibility_fixture_row(23U, entity); // No duplicate retention.
            fixture += "[remote-player-visibility-summary] changes=1 retained=17 dropped=0 evidence=failed_private_path\n";
            const auto excerpt = functional_diagnostic_excerpt(fixture);
            visibility_retention_valid = visibility_retention_valid &&
                excerpt.starts_with("[remote-player-visibility-summary] changes=24 retained=16 dropped=8 evidence=cpu-frame-only\n") &&
                excerpt.size() <= 16U * 1'024U &&
                std::ranges::count(excerpt, '\n') <= 64 &&
                occurrence_count(excerpt, "[remote-player-visibility] ") == 16U &&
                occurrence_count(excerpt, "[remote-player-visibility-summary] ") == 1U &&
                occurrence_count(excerpt, "live_application_outcome result=") == 1U &&
                excerpt.find("primary_error=fixture_runtime_error") != std::string::npos &&
                excerpt.find("failed_private_path") == std::string::npos;
            for (std::size_t ordinal = 0U; ordinal < 24U; ++ordinal)
                visibility_retention_valid = visibility_retention_valid &&
                    (excerpt.find(visibility_fixture_row(ordinal, entity)) != std::string::npos) == (ordinal >= 8U);
        }
        // The largest permitted journal must not let earlier verbose weapon
        // or prediction text crowd out the actual application's error prefix.
        std::string competing_visibility{
            "[remote-player-visibility-summary] changes=16 retained=16 dropped=0 evidence=cpu-frame-only\n"};
        for (std::size_t index=0U;index<6U;++index)
            competing_visibility += "live_weapon_observation generation=1 active_id=2 " +
                std::string(5'000U,'x') + "\n";
        competing_visibility += "live_prediction mode=reference " + std::string(5'000U,'x') + "\n";
        competing_visibility += "live_application_outcome result=error audio_detail=" +
            std::string(2'700U,'x') + " primary_error=fixture_runtime_error " +
            std::string(3'000U,'x') + "\n";
        for (std::size_t ordinal=0U;ordinal<16U;++ordinal) {
            auto row=visibility_fixture_row(ordinal,1U);
            row.pop_back(); // Pad legal numeric spellings, not unknown fields.
            for (const auto key : {"generation=","source=","ordinal=","publication=",
                     "entity=","model=","game=","effects=","render-mode="}) {
                const auto begin=row.find(key)+std::string_view{key}.size();
                const auto end=row.find(' ',begin);
                const auto length=(end==std::string::npos ? row.size() : end)-begin;
                row.insert(begin,(std::min)(20U-length,768U-row.size()),'0');
            }
            visibility_retention_valid=visibility_retention_valid && valid_visibility_row(row);
            competing_visibility += row+'\n';
        }
        const auto competing_excerpt=functional_diagnostic_excerpt(competing_visibility);
        visibility_retention_valid=visibility_retention_valid &&
            competing_excerpt.size()<=16U*1'024U &&
            std::ranges::count(competing_excerpt,'\n')<=64 &&
            occurrence_count(competing_excerpt,"[remote-player-visibility] ")==16U &&
            occurrence_count(competing_excerpt,"live_application_outcome result=")==1U &&
            competing_excerpt.find("primary_error=fixture_runtime_error")!=std::string::npos &&
            functional_diagnostic_excerpt("[remote-player-visibility] failed_secret_token live_application_outcome result=error primary_error=private_token\n").empty();
        auto unavailable_visibility = visibility_fixture_row(0U, 32U);
        replace_all(unavailable_visibility, "model=87", "model=0");
        replace_all(unavailable_visibility, "stage=queued_visible", "stage=source_absent");
        replace_all(unavailable_visibility, "game=1", "game=0");
        replace_all(unavailable_visibility, "server-seconds=47.0596", "server-seconds=-1");
        replace_all(unavailable_visibility, "distance=42.545", "distance=-1");
        replace_all(unavailable_visibility,
            "bounds=185.984375,1196.109375,-1819.968750;217.984375,1228.109375,-1747.968750",
            "bounds=unavailable");
        replace_all(unavailable_visibility, "static-light=0.125,0.25,0.5", "static-light=fallback");
        visibility_retention_valid = visibility_retention_valid &&
            functional_diagnostic_excerpt(unavailable_visibility) == unavailable_visibility &&
            functional_diagnostic_excerpt("[remote-player-visibility-summary] changes=0 retained=0 dropped=0 evidence=cpu-frame-only\n") ==
                "[remote-player-visibility-summary] changes=0 retained=0 dropped=0 evidence=cpu-frame-only\n";
        for (const auto stage : {"source_absent", "not_visual", "model_not_advertised",
                 "server_hidden128", "unsupported_render_mode", "game_not_ready",
                 "pose_unavailable", "frame_unavailable", "pvs_culled", "frustum_culled",
                 "queued_visible", "queued_unlit", "queued_dim"}) {
            auto fixture = visibility_fixture_row(0U, 1U);
            replace_all(fixture, "stage=queued_visible", std::string{"stage="} + stage);
            visibility_retention_valid = visibility_retention_valid &&
                functional_diagnostic_excerpt(fixture) == fixture;
        }
        constexpr std::array invalid_visibility_replacements{
            std::pair{std::string_view{"generation=1"}, std::string_view{"generation=0"}},
            std::pair{std::string_view{"source=100"}, std::string_view{"source=18446744073709551616"}},
            std::pair{std::string_view{"source=100"}, std::string_view{"source=failed_private/path"}},
            std::pair{std::string_view{"source=100"}, std::string_view{"source=100 source=100"}},
            std::pair{std::string_view{"entity=1"}, std::string_view{"entity=0"}},
            std::pair{std::string_view{"entity=1"}, std::string_view{"entity=33"}},
            std::pair{std::string_view{"model=87"}, std::string_view{"model=65536"}},
            std::pair{std::string_view{"model=87"}, std::string_view{"model=\"private\""}},
            std::pair{std::string_view{"game=1"}, std::string_view{"game=9"}},
            std::pair{std::string_view{"stage=queued_visible"}, std::string_view{"stage=failed_secret_token"}},
            std::pair{std::string_view{"stage=queued_visible"}, std::string_view{"stage=C:\\private"}},
            std::pair{std::string_view{"server-seconds=47.0596"}, std::string_view{"server-seconds=nan"}},
            std::pair{std::string_view{"server-seconds=47.0596"}, std::string_view{"server-seconds=-2"}},
            std::pair{std::string_view{"distance=42.545"}, std::string_view{"distance=-2"}},
            std::pair{std::string_view{"effects=0"}, std::string_view{"effects=4294967296"}},
            std::pair{std::string_view{"render-mode=0"}, std::string_view{"render-mode=-1"}},
            std::pair{std::string_view{"camera=202.515625,1244.140625,-1791.973267"}, std::string_view{"camera=1,2,3,4"}},
            std::pair{std::string_view{"camera=202.515625,1244.140625,-1791.973267"}, std::string_view{"camera=1,2,inf"}},
            std::pair{std::string_view{"target=202.440644,1243.143440,-1791.908728"}, std::string_view{"target=1,2,1e300"}},
            std::pair{std::string_view{"near=0.1"}, std::string_view{"near=0"}},
            std::pair{std::string_view{"bounds=185.984375,1196.109375,-1819.968750;217.984375,1228.109375,-1747.968750"}, std::string_view{"bounds=3,0,0;2,1,1"}},
            std::pair{std::string_view{"static-light=0.125,0.25,0.5"}, std::string_view{"static-light=1.1,0,0"}},
            std::pair{std::string_view{"static-light=0.125,0.25,0.5"}, std::string_view{"static-light=-0.1,0,0"}},
            std::pair{std::string_view{"static-light=0.125,0.25,0.5"}, std::string_view{"static-light=nan,0,0"}},
            std::pair{std::string_view{" static-light=0.125,0.25,0.5"}, std::string_view{""}},
            std::pair{std::string_view{"static-light=0.125,0.25,0.5"}, std::string_view{"static-light=0.125,0.25,0.5 token=failed_secret_token"}},
            std::pair{std::string_view{" publication="}, std::string_view{"  publication="}}};
        for (const auto& [before, after] : invalid_visibility_replacements) {
            auto fixture = visibility_fixture_row(0U, 1U);
            replace_all(fixture, before, after);
            visibility_retention_valid = visibility_retention_valid &&
                functional_diagnostic_excerpt(fixture).empty();
        }
        const auto oversized_visibility = visibility_fixture_row(0U, 1U) +
            "[remote-player-visibility] generation=1 failed_private_path=" + std::string(2'000U, 'x') + "\n";
        visibility_retention_valid = visibility_retention_valid &&
            functional_diagnostic_excerpt(oversized_visibility) == visibility_fixture_row(0U, 1U) &&
            functional_diagnostic_excerpt("[remote-player-visibility-summary] changes=1 retained=1 dropped=1 evidence=cpu-frame-only\n").empty() &&
            functional_diagnostic_excerpt("[remote-player-visibility-summary] changes=1 retained=1 dropped=0 evidence=failed_secret_token\n").empty() &&
            functional_diagnostic_excerpt("[info] [info] " + visibility_fixture_row(0U, 1U)).empty() &&
            functional_diagnostic_excerpt("failed private marker injection " + visibility_fixture_row(0U, 1U)).empty() &&
            functional_diagnostic_excerpt("failed private oversized marker injection " +
                std::string(5'000U,'x') + visibility_fixture_row(0U,1U)).empty();
        const std::string remote_effects_fixture{
            "[remote-effects-summary] received=12 accepted=9 unresolved=1 unsupported=1 local_echo=1 late=0 invalid=0 fire=7 swing=2 audio_submitted=15 audio_late=0 audio_missing=0 audio_rejected=0 shells=7 impact_hits=5 flash_submissions=42\n"};
        auto newer_remote_effects = remote_effects_fixture;
        replace_all(newer_remote_effects, "received=12", "received=20");
        const auto remote_effects_excerpt = functional_diagnostic_excerpt(
            remote_effects_fixture + competing_visibility + "[info] " + newer_remote_effects);
        bool remote_effects_retention_valid =
            remote_effects_excerpt.starts_with(newer_remote_effects) &&
            occurrence_count(remote_effects_excerpt, "[remote-effects-summary] ") == 1U &&
            remote_effects_excerpt.find("received=12") == std::string::npos &&
            remote_effects_excerpt.find("primary_error=fixture_runtime_error") != std::string::npos &&
            remote_effects_excerpt.size() <= 16U * 1'024U &&
            std::ranges::count(remote_effects_excerpt, '\n') <= 64;
        auto maximum_counter = remote_effects_fixture;
        replace_all(maximum_counter, "received=12", "received=18446744073709551615");
        remote_effects_retention_valid = remote_effects_retention_valid &&
            functional_diagnostic_excerpt(maximum_counter) == maximum_counter;
        for (const auto replacement : {"received=-1", "received=18446744073709551616",
                 "received=1.0", "received=failed_private_path", "received=1 received=2",
                 "received=+1", "received=", "received=nan"}) {
            auto invalid = remote_effects_fixture;
            replace_all(invalid, "received=12", replacement);
            remote_effects_retention_valid = remote_effects_retention_valid &&
                functional_diagnostic_excerpt(invalid).empty();
        }
        for (const auto injection : {
                 std::string{"[info] [info] "} + remote_effects_fixture,
                 std::string{"failed private prefix "} + remote_effects_fixture,
                 std::string{"live_application_outcome result=error "} + remote_effects_fixture,
                 std::string{"live_weapon_observation generation=1 "} + remote_effects_fixture,
                 std::string{"[remote-effects-summary] failed_private_path="} + std::string(1'000U, 'x') + "\n"})
            remote_effects_retention_valid = remote_effects_retention_valid &&
                functional_diagnostic_excerpt(injection).empty();
        auto trailing_private = remote_effects_fixture;
        replace_all(trailing_private, "flash_submissions=42\n", "flash_submissions=42 path=failed_private_path\n");
        auto missing_field = remote_effects_fixture;
        replace_all(missing_field, " audio_missing=0", "");
        auto reordered_fields = remote_effects_fixture;
        replace_all(reordered_fields, "received=12 accepted=9", "accepted=9 received=12");
        auto crlf_remote_effects = remote_effects_fixture;
        crlf_remote_effects.insert(crlf_remote_effects.size()-1U, "\r");
        remote_effects_retention_valid = remote_effects_retention_valid &&
            functional_diagnostic_excerpt(trailing_private).empty() &&
            functional_diagnostic_excerpt(missing_field).empty() &&
            functional_diagnostic_excerpt(reordered_fields).empty() &&
            functional_diagnostic_excerpt("[info] " + crlf_remote_effects) == remote_effects_fixture &&
            functional_diagnostic_excerpt(remote_effects_fixture + trailing_private) == remote_effects_fixture;
        const bool valid =
            visibility_retention_valid && remote_effects_retention_valid &&
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
            project_visual_h4.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_jump_duck_check, true) &&
            project_visual_h4.h4_result ==
                "live_jump_duck_crouchwalk_prediction_verified" &&
            project_visual_h4.h4_active_frames[1U] == 40U &&
            project_visual_h4.h4_local_steps[3U] == 75U &&
            project_visual_h4_diagnostic.find("live_h4_prediction result=") !=
                std::string::npos &&
            !project_visual_h4_partial.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_jump_duck_check, true) &&
            project_visual_h4_partial.h4_result ==
                "crouch_walk_prediction_context_blocked" &&
            project_visual_weapon.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_weapon_check, true) &&
            project_visual_weapon.weapon_result ==
                "live_viewmodel_weapon_selection_and_basic_hud_verified" &&
            project_visual_weapon.weapon_selection_queued == 1U &&
            project_visual_weapon.weapon_selection_confirmed == 1U &&
            project_visual_weapon.viewmodel_pixels_distinct == true &&
            project_visual_weapon.hud_pixels_distinct == true &&
            project_visual_weapon.viewmodel_camera_result ==
                "live_viewmodel_camera_space_verified" &&
            project_visual_weapon.viewmodel_camera_pixels_valid == true &&
            project_visual_weapon.viewmodel_camera_pixel_count == 321U &&
            project_visual_weapon_partial.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_weapon_check, true) &&
            project_visual_weapon_partial.weapon_result ==
                "live_viewmodel_hud_verified_selection_pending" &&
            project_visual_fire.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_fire_reload_check, true) &&
            project_visual_fire.fire_reload_result ==
                "live_primary_fire_reload_and_weapon_animation_verified" &&
            project_visual_fire.server_confirmed_shots == 3U &&
            project_visual_fire.reload_completions == 1U &&
            project_visual_fire_diagnostic.find(
                "live_fire_reload result=live_primary_fire_reload_and_weapon_animation_verified") !=
                std::string::npos &&
            !project_visual_fire_partial.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_fire_reload_check, true) &&
            project_visual_fire_partial.fire_reload_result ==
                "primary_fire_verified_reload_pending" &&
            !project_visual_fire_bad_count.complete(
                ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_fire_reload_check, true) &&
            b1.complete(ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_fire_reload_presentation_check, true) &&
            b1.weapon_prediction.verified() &&
            c_valid.complete(ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_damage_respawn_check, true) &&
            !c_bad.complete(ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_damage_respawn_check, true) &&
            !c_error.complete(ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_damage_respawn_check, true) &&
            !c_missing.complete(ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_damage_respawn_check, true) &&
            !b1.life.result &&
            functional_diagnostic_excerpt(c_fixture).find("live_damage_respawn result=") != std::string::npos &&
            b1_excerpt.find("live_weapon_prediction result=live_client_predicted_weapon_presentation_verified") != std::string::npos &&
            !b1_bad.complete(ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_fire_reload_presentation_check, true) &&
            !b1_partial.complete(ProjectClientStop::live_visual_control,
                ProjectClientLiveInput::scripted_fire_reload_presentation_check, true) &&
            !project_visual_fire.weapon_prediction.result &&
            project_visual_prediction.prediction_interpolated_frames == 200U &&
            project_visual_prediction.prediction_endpoint_frames == 45U &&
            project_visual_prediction.prediction_collision_blocked_frames == 5U &&
            project_visual_prediction.prediction_visual_correction_frames == 3U &&
            project_visual_prediction.prediction_trace_queries == 240U &&
            project_visual_prediction.prediction_scratch_growths == 1U &&
            project_visual_prediction.prediction_long_stall_frames == 2U &&
            project_visual_prediction.prediction_active_time_ms == 1700.5 &&
            project_visual_prediction.prediction_fallback_time_ms == 30.25 &&
            project_visual_prediction.prediction_cpu_total_ms == 14.5 &&
            project_visual_prediction.prediction_cpu_max_ms == 0.125 &&
            project_visual_prediction.prediction_maximum_camera_jump == 0.03125 &&
            project_visual_prediction.prediction_correction_pair_window ==
                "1:2:100:120:0.5:interpolated" &&
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
            project_visual_diagnostic.find(
                "live_weapon_observation generation=1 active_id=7") !=
                std::string::npos &&
            project_visual_diagnostic.find(
                "live_weapon_observation generation=1 active_id=1") ==
                std::string::npos &&
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
        if (options->runtime_failure_fixture) {
            std::error_code ec;
            const auto size = std::filesystem::file_size(*options->runtime_failure_fixture, ec);
            if (ec || size > 65'536U) return 1;
            std::ifstream input{*options->runtime_failure_fixture, std::ios::binary};
            std::string log{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
            if (!input.eof() && input.fail()) return 1;
            const auto first = observe_project_client_signon_log(log).application;
            log += "\nlive_application_outcome result=error primary_error=cleanup_failed\n";
            const bool exact_cause =
                (first[12U] == "protocol_decoder" && first[15U] == "unsupported_opcode" &&
                    first[30U] == "5" && first[32U] == "6") ||
                (first[12U] == "protocol_decoder" && first[15U] == "truncated_body" &&
                    first[34U] == "Health" && first[36U] == "1" && first[37U] == "0") ||
                (first[12U] == "game_module" && first[18U] == "invalid_token" &&
                    first[34U] == "ItemPickup" && first[32U] == "4");
            if (observe_project_client_signon_log(log).application != first || !exact_cause ||
                first[1U] != "runtime_record_failed" ||
                first[39U] != "3" || first[40U] != "2") return 1;
            if (!valid) return 5;
            // Exercise real bounded child wait/stdout/cleanup with the existing
            // project-owned fake client; no sockets, game, Steam or WFP.
            const auto identity = observe_project_binary(
                sibling_executable(L"hlclient_stock_runtime_fake_client.exe"));
            auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
            auto capture = windows::BoundedProcessLogCapture::create({});
            if (!identity || !created || !capture) return 1;
            std::string terminal{"live_application_outcome"};
            for (std::size_t i = 0U; i < first.size(); ++i) {
                terminal += " "; terminal += kApplicationMetrics[i]; terminal += "=";
                terminal += first[i].value_or("unavailable");
            }
            windows::OwnedProcessLaunchSpec spec;
            spec.executable = identity.identity->canonical_path;
            spec.working_directory = spec.executable.parent_path();
            spec.expected_identity = *identity.identity;
            spec.stdout_handle = capture->inherited_write_handle();
            spec.stderr_handle = capture->inherited_write_handle();
            spec.arguments = {L"--emit-runtime-failure-fixture",
                std::wstring{terminal.begin(), terminal.end()}};
            auto [child, launched] = job.launch(spec);
            capture->close_parent_write_handle();
            if (!launched) return 1;
            const auto waited = child.wait_result(std::chrono::seconds{5});
            const auto captured = capture->finish(std::chrono::seconds{1});
            const auto cleaned = job.terminate_and_wait(120U, std::chrono::seconds{3});
            if (!waited || waited.exit_code != 2U || !cleaned ||
                !windows::bounded_process_log_snapshot_complete(captured) ||
                observe_project_client_signon_log(captured.bytes).application != first) return 1;
            if (options->runtime_failure_status_fixture) {
                print_application_evidence(first);
                print_key_value("client-exit-code", std::to_string(*waited.exit_code));
                print_key_value("job-cleanup", "exact");
                print_key_value("result", "failed");
                return 0;
            }
            std::cout << "{\"schema\":\"hlclient.runtime-diagnostics-offline.v1\","
                "\"client_exit_code\":" << *waited.exit_code <<
                ",\"owned_process_cleanup\":\"exact\","
                "\"application_outcome\":";
            write_application_evidence(std::cout, first);
            std::cout << "}\n";
            return 0;
        }
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
    manual_phase(*options, "preflight");
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
    manual_phase(*options, "owned-cleanup-complete");
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
                        options->project_client_stock_signon && !options->fast_manual && !options->test_start_health && !options->remote_audio_peer && !options->manual_duration_seconds && !options->manual_no_time_limit ? "true" : "false");
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
        if(options->remote_audio_peer) {
            print_key_value("remote-audio-peer-process-id",std::to_string(summary.remote_audio_peer_process_id));
            print_key_value("remote-audio-peer-runtime-published",summary.remote_audio_peer_entered ? "true" : "false");
            print_key_value("remote-audio-peer-exit-code",summary.remote_audio_peer_exit ? std::to_string(*summary.remote_audio_peer_exit) : "not-observed");
            print_key_value("remote-audio-output-isolation","focused-client-output");
        }
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
            if (options->project_client_live_input == ProjectClientLiveInput::scripted_damage_respawn_check)
                print_key_value("damage-respawn-result", summary.project_life.result.value_or(
                    "damage_death_respawn_implemented_live_pending"));
            if (options->project_client_reference_prediction)
            print_key_value("prediction-result",
                                summary.project_prediction_result.value_or(
                                    "unavailable"));
            if (options->project_client_reference_prediction &&
                options->project_client_live_input ==
                    ProjectClientLiveInput::scripted_jump_duck_check)
                print_key_value("h4-result",
                            summary.project_h4_result.value_or(
                                    "unavailable"));
            if (options->project_client_live_input ==
                    ProjectClientLiveInput::scripted_weapon_check) {
                print_key_value("weapon-result",
                    summary.project_weapon_result.value_or("unavailable"));
                print_key_value("weapon-selection-queued",
                    summary.project_weapon_selection_queued
                        ? std::to_string(*summary.project_weapon_selection_queued)
                        : "unavailable");
                print_key_value("weapon-selection-confirmed",
                    summary.project_weapon_selection_confirmed
                        ? std::to_string(*summary.project_weapon_selection_confirmed)
                        : "unavailable");
                print_key_value("viewmodel-pixels-distinct",
                    summary.project_viewmodel_pixels_distinct
                        ? (*summary.project_viewmodel_pixels_distinct ? "true" : "false")
                        : "unavailable");
                print_key_value("hud-pixels-distinct",
                    summary.project_hud_pixels_distinct
                        ? (*summary.project_hud_pixels_distinct ? "true" : "false")
                        : "unavailable");
                print_key_value("viewmodel-camera-result",
                    summary.project_viewmodel_camera_result.value_or("unavailable"));
                print_key_value("viewmodel-camera-pixels-valid",
                    summary.project_viewmodel_camera_pixels_valid
                        ? (*summary.project_viewmodel_camera_pixels_valid
                            ? "true" : "false") : "unavailable");
                print_key_value("viewmodel-camera-pixel-count",
                    summary.project_viewmodel_camera_pixel_count
                        ? std::to_string(*summary.project_viewmodel_camera_pixel_count)
                        : "unavailable");
            }
            if (options->project_client_live_input ==
                    ProjectClientLiveInput::scripted_fire_reload_check ||
                options->project_client_live_input ==
                    ProjectClientLiveInput::scripted_fire_reload_presentation_check) {
                print_key_value("fire-reload-result",
                    summary.project_fire_reload_result.value_or("unavailable"));
                print_key_value("server-confirmed-shots",
                    summary.project_server_confirmed_shots
                        ? std::to_string(*summary.project_server_confirmed_shots)
                        : "unavailable");
                print_key_value("reload-completions",
                    summary.project_reload_completions
                        ? std::to_string(*summary.project_reload_completions)
                        : "unavailable");
            }
            if (options->project_client_live_input ==
                    ProjectClientLiveInput::scripted_fire_reload_presentation_check) {
                print_key_value("presentation-result",
                    summary.project_weapon_prediction.result.value_or("unavailable"));
                for (std::size_t i = 0U; i < kWeaponPredictionFlags.size(); ++i) {
                    auto key = std::string{kWeaponPredictionFlags[i]};
                    std::replace(key.begin(), key.end(), '_', '-');
                    const auto value = summary.project_weapon_prediction.flags[i];
                    print_key_value(key, value ? (*value ? "true" : "false") : "unavailable");
                }
                print_key_value("crowbar-hit-status",
                    summary.project_weapon_prediction.hit_status.value_or("unavailable"));
            }
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
        if (options->project_client_stock_signon)
            print_application_evidence(summary.project_application);
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
