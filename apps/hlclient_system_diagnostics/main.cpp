// Opt-in, separate-process system recorder for one launcher-owned run. This
// executable never changes process priority, power settings, ETW sessions, or
// game input. It deliberately leaves temperature and WPR tracing unavailable
// rather than presenting guessed values as measurements.
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <pdh.h>
#include <PdhMsg.h>
#include <powrprof.h>
#include "wpr_probe.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr auto sample_period = std::chrono::seconds{1};
constexpr auto maximum_recording = std::chrono::minutes{60};
constexpr auto marker_grace = std::chrono::seconds{30};
constexpr std::uint64_t maximum_sample_file_bytes = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximum_zip_entry_bytes = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximum_zip_total_bytes = 30ULL * 1024ULL * 1024ULL;
constexpr std::size_t maximum_processes_observed = 512;
constexpr std::size_t maximum_processes_recorded = 16;
constexpr std::size_t maximum_gpu_engines_recorded = 8;

struct Handle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) noexcept : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
    [[nodiscard]] bool valid() const noexcept { return value && value != INVALID_HANDLE_VALUE; }
};

[[nodiscard]] std::uint64_t as_u64(const FILETIME value) noexcept {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) |
           static_cast<std::uint64_t>(value.dwLowDateTime);
}
[[nodiscard]] std::uint64_t utc_filetime() noexcept {
    FILETIME value{}; ::GetSystemTimePreciseAsFileTime(&value); return as_u64(value);
}
[[nodiscard]] std::uint64_t qpc_ticks() noexcept {
    LARGE_INTEGER value{}; ::QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}
[[nodiscard]] std::string utc_iso(const std::uint64_t ticks) {
    FILETIME ft{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32U)};
    SYSTEMTIME value{}; ::FileTimeToSystemTime(&ft, &value);
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << value.wYear << '-'
        << std::setw(2) << value.wMonth << '-' << std::setw(2) << value.wDay << 'T'
        << std::setw(2) << value.wHour << ':' << std::setw(2) << value.wMinute << ':'
        << std::setw(2) << value.wSecond << '.' << std::setw(3) << value.wMilliseconds << 'Z';
    return out.str();
}
[[nodiscard]] std::string utf8(const std::wstring_view value) {
    if (value.empty() || value.size() > static_cast<std::size_t>(INT_MAX)) return {};
    const auto size = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    (void)::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
}
[[nodiscard]] std::string json_string(const std::string_view value) {
    std::string out{"\""};
    for (const unsigned char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20U) {
                constexpr char hex[] = "0123456789abcdef";
                out += "\\u00"; out += hex[c >> 4U]; out += hex[c & 15U];
            } else out += static_cast<char>(c);
        }
    }
    out += '"'; return out;
}
[[nodiscard]] std::string csv_string(const std::string_view value) {
    std::string out{"\""};
    for (const char c : value) {
        if (c == '"') out += '"';
        if (c == '\r' || c == '\n') out += ' '; else out += c;
    }
    out += '"'; return out;
}
[[nodiscard]] std::string decimal(const double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream out; out << std::fixed << std::setprecision(3) << value;
    return out.str();
}
[[nodiscard]] std::string maybe_number(const std::optional<double>& value) {
    return value ? decimal(*value) : "null";
}
[[nodiscard]] std::string metric(const std::optional<double>& value,
    const std::string_view unit, const std::string_view source) {
    return "{\"value\":" + maybe_number(value) + ",\"unit\":" + json_string(unit) +
        ",\"source\":" + json_string(source) + ",\"status\":\"" +
        (value ? "available" : "unsupported") + "\"}";
}
[[nodiscard]] std::optional<std::uint64_t> parse_unsigned(const std::wstring_view value) {
    if (value.empty()) return {};
    std::uint64_t result{};
    for (const wchar_t c : value) {
        if (c < L'0' || c > L'9' || result >
            (std::numeric_limits<std::uint64_t>::max() - static_cast<unsigned>(c - L'0')) / 10ULL) return {};
        result = result * 10ULL + static_cast<unsigned>(c - L'0');
    }
    return result;
}
[[nodiscard]] bool run_id_valid(const std::string_view value) {
    if (value.size() != 32U) return false;
    return std::ranges::all_of(value, [](const char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
[[nodiscard]] std::optional<std::string> read_bounded(const fs::path& path, const std::uint64_t limit) {
    Handle file{::CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!file.valid()) return {};
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    if (!::GetFileInformationByHandle(file.value, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0U ||
        !::GetFileSizeEx(file.value, &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > limit ||
        static_cast<std::uint64_t>(size.QuadPart) > std::numeric_limits<std::size_t>::max()) return {};
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t at{};
    while (at < bytes.size()) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - at, 1U << 20U));
        DWORD received{};
        if (!::ReadFile(file.value, bytes.data() + at, chunk, &received, nullptr) ||
            received != chunk) return {};
        at += received;
    }
    return bytes;
}
[[nodiscard]] std::optional<std::string> json_text_field(
    std::string_view bytes, std::string_view key);
[[nodiscard]] bool contains_json_number(const std::string_view json, const std::string_view key,
    const std::uint64_t value) {
    const auto marker = "\"" + std::string(key) + "\"";
    auto at = json.find(marker); if (at == std::string_view::npos) return false;
    at = json.find(':', at + marker.size()); if (at == std::string_view::npos) return false;
    ++at; while (at < json.size() && (json[at] == ' ' || json[at] == '\n')) ++at;
    const auto digits = std::to_string(value);
    return json.substr(at, digits.size()) == digits &&
        (at + digits.size() == json.size() || json[at + digits.size()] < '0' ||
         json[at + digits.size()] > '9');
}
struct Args final {
    fs::path run_root;
    std::string run_id;
    DWORD pid{};
    std::uint64_t creation{};
    bool await_client{};
};
[[nodiscard]] std::optional<Args> parse_args(const int argc, wchar_t* argv[]) {
    if ((argc != 6 && argc != 10) ||
        (std::wstring_view{argv[1]} != L"--monitor" &&
         std::wstring_view{argv[1]} != L"--await-client") ||
        std::wstring_view{argv[2]} != L"--run-root" ||
        std::wstring_view{argv[4]} != L"--run-id") return {};
    Args value; value.run_root = argv[3]; value.run_id = utf8(argv[5]);
    value.await_client = std::wstring_view{argv[1]} == L"--await-client";
    if (value.await_client != (argc == 6)) return {};
    if (!run_id_valid(value.run_id) ||
        !value.run_root.is_absolute() || utf8(value.run_root.filename().wstring()) != value.run_id)
        return {};
    const auto attributes = ::GetFileAttributesW(value.run_root.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) return {};
    if (value.await_client) return value;
    if (std::wstring_view{argv[6]} != L"--pid" ||
        std::wstring_view{argv[8]} != L"--creation-filetime") return {};
    const auto pid = parse_unsigned(argv[7]), creation = parse_unsigned(argv[9]);
    if (!pid || !creation || *pid == 0U ||
        *pid > std::numeric_limits<DWORD>::max() || *creation == 0U) return {};
    value.pid = static_cast<DWORD>(*pid); value.creation = *creation;
    const auto manifest = read_bounded(value.run_root / L"process-manifest.json", 65536U);
    if (!manifest || json_text_field(*manifest, "run_id") != value.run_id ||
        !contains_json_number(*manifest, "process_id", value.pid) ||
        !contains_json_number(*manifest, "process_creation", value.creation)) return {};
    return value;
}

struct ProcessSample final {
    DWORD pid{};
    std::uint64_t creation{};
    std::string name;
    std::optional<double> cpu_percent;
    std::optional<std::uint64_t> working_set_bytes;
    std::optional<double> gpu_engine_sum_percent;
};
struct ProcessPrevious final { std::uint64_t cpu_time{}, wall_time{}; };
using ProcessKey = std::pair<DWORD, std::uint64_t>;
class ProcessSampler final {
public:
    [[nodiscard]] std::vector<ProcessSample> sample(const std::uint64_t monotonic_ticks,
        const std::uint64_t qpc_frequency,
        const unsigned logical_processors, bool& truncated) {
        std::vector<ProcessSample> out;
        std::map<ProcessKey, ProcessPrevious> next;
        Handle snapshot{::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0U)};
        if (!snapshot.valid()) return out;
        PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
        if (!::Process32FirstW(snapshot.value, &entry)) return out;
        do {
            if (out.size() >= maximum_processes_observed) { truncated = true; break; }
            Handle process{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                FALSE, entry.th32ProcessID)};
            if (!process.valid()) process.value = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE, entry.th32ProcessID);
            if (!process.valid()) continue;
            FILETIME creation{}, exit{}, kernel{}, user{};
            if (!::GetProcessTimes(process.value, &creation, &exit, &kernel, &user)) continue;
            const auto process_creation = as_u64(creation);
            const auto cpu_time = as_u64(kernel) + as_u64(user);
            ProcessSample row{entry.th32ProcessID, process_creation, utf8(entry.szExeFile)};
            const ProcessKey key{row.pid, row.creation};
            if (const auto previous = previous_.find(key); previous != previous_.end() &&
                monotonic_ticks > previous->second.wall_time && cpu_time >= previous->second.cpu_time &&
                logical_processors != 0U && qpc_frequency != 0U) {
                const auto elapsed = monotonic_ticks - previous->second.wall_time;
                row.cpu_percent = std::clamp(100.0 * static_cast<double>(cpu_time - previous->second.cpu_time) /
                    (static_cast<double>(elapsed) * 10000000.0 / static_cast<double>(qpc_frequency)) /
                    static_cast<double>(logical_processors), 0.0, 100.0);
            }
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
            if (::GetProcessMemoryInfo(process.value, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                sizeof(memory))) row.working_set_bytes = memory.WorkingSetSize;
            next.emplace(key, ProcessPrevious{cpu_time, monotonic_ticks});
            out.push_back(std::move(row));
        } while (::Process32NextW(snapshot.value, &entry));
        previous_ = std::move(next);
        return out;
    }
private:
    std::map<ProcessKey, ProcessPrevious> previous_;
};

struct GpuEngine final { std::string instance; DWORD pid{}; double percent{}; };
class GpuSampler final {
public:
    GpuSampler() {
        if (::PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) return;
        if (::PdhAddEnglishCounterW(query_, L"\\GPU Engine(*)\\Utilization Percentage",
            0, &counter_) != ERROR_SUCCESS) return;
        supported_ = true;
        (void)::PdhCollectQueryData(query_); // First PDH observation primes rate counters.
    }
    GpuSampler(const GpuSampler&) = delete;
    GpuSampler& operator=(const GpuSampler&) = delete;
    ~GpuSampler() { if (query_) ::PdhCloseQuery(query_); }
    [[nodiscard]] bool supported() const noexcept { return supported_; }
    struct Result final { bool valid{}; std::vector<GpuEngine> engines; };
    [[nodiscard]] Result sample() {
        Result result;
        std::vector<GpuEngine> values;
        if (!supported_ || ::PdhCollectQueryData(query_) != ERROR_SUCCESS) return result;
        DWORD bytes{}, count{};
        const auto probe = ::PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE, &bytes,
            &count, nullptr);
        if (probe != PDH_MORE_DATA || bytes == 0U || bytes > 1024U * 1024U) return result;
        std::vector<std::byte> storage(bytes);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
        if (::PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE, &bytes, &count,
            items) != ERROR_SUCCESS) return result;
        result.valid = true;
        for (DWORD i = 0; i < count && i < 1024U; ++i) {
            const auto status = items[i].FmtValue.CStatus;
            if (status != PDH_CSTATUS_VALID_DATA && status != PDH_CSTATUS_NEW_DATA) continue;
            const auto percent = items[i].FmtValue.doubleValue;
            if (!std::isfinite(percent) || percent <= 0.0) continue;
            const std::wstring_view name{items[i].szName ? items[i].szName : L""};
            const auto marker = name.find(L"pid_");
            DWORD pid{};
            if (marker != std::wstring_view::npos) {
                const auto begin = marker + 4U;
                const auto end = name.find(L'_', begin);
                if (const auto parsed = parse_unsigned(name.substr(begin, end - begin));
                    parsed && *parsed <= std::numeric_limits<DWORD>::max()) pid = static_cast<DWORD>(*parsed);
            }
            values.push_back({utf8(name.substr(0U, 160U)), pid, percent});
        }
        std::ranges::sort(values, [](const auto& a, const auto& b) { return a.percent > b.percent; });
        result.engines = std::move(values);
        return result;
    }
private:
    PDH_HQUERY query_{};
    PDH_HCOUNTER counter_{};
    bool supported_{};
};

[[nodiscard]] std::string display_adapter() {
    DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
    for (DWORD ordinal = 0U; ordinal < 16U; ++ordinal) {
        device = {}; device.cb = sizeof(device);
        if (!::EnumDisplayDevicesW(nullptr, ordinal, &device, 0)) break;
        if ((device.StateFlags & DISPLAY_DEVICE_ACTIVE) != 0U)
            return utf8(device.DeviceString);
    }
    return {};
}
[[nodiscard]] std::optional<double> processor_mhz(const unsigned logical_processors) {
    if (logical_processors == 0U || logical_processors > 256U) return {};
    // The documented ProcessorInformation output layout is not declared by
    // this Windows SDK. Keep it local; on failure report unsupported.
    struct ProcessorPowerInformation final {
        ULONG number, max_mhz, current_mhz, mhz_limit, max_idle_state, current_idle_state;
    };
    std::vector<ProcessorPowerInformation> values(logical_processors);
    const auto result = ::CallNtPowerInformation(ProcessorInformation, nullptr, 0,
        values.data(), static_cast<ULONG>(values.size() * sizeof(values.front())));
    if (result != 0) return {};
    double sum{}; unsigned count{};
    for (const auto& item : values) if (item.current_mhz > 0U) {
        sum += static_cast<double>(item.current_mhz); ++count;
    }
    return count == 0U ? std::nullopt : std::optional<double>{sum / count};
}
[[nodiscard]] std::string active_power_scheme() {
    GUID* scheme{};
    if (::PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS || !scheme) return {};
    wchar_t buffer[64]{};
    (void)::swprintf_s(buffer, L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        scheme->Data1, scheme->Data2, scheme->Data3, scheme->Data4[0], scheme->Data4[1],
        scheme->Data4[2], scheme->Data4[3], scheme->Data4[4], scheme->Data4[5],
        scheme->Data4[6], scheme->Data4[7]);
    ::LocalFree(scheme);
    return utf8(buffer);
}
struct CpuPrevious final { std::uint64_t idle{}, kernel{}, user{}; bool valid{}; };
[[nodiscard]] std::optional<double> system_cpu_percent(CpuPrevious& previous) {
    FILETIME idle{}, kernel{}, user{};
    if (!::GetSystemTimes(&idle, &kernel, &user)) return {};
    const CpuPrevious now{as_u64(idle), as_u64(kernel), as_u64(user), true};
    std::optional<double> result;
    if (previous.valid && now.kernel >= previous.kernel && now.user >= previous.user &&
        now.idle >= previous.idle) {
        const auto elapsed = now.kernel - previous.kernel + now.user - previous.user;
        if (elapsed != 0U) result = std::clamp(100.0 *
            (1.0 - static_cast<double>(now.idle - previous.idle) /
                static_cast<double>(elapsed)), 0.0, 100.0);
    }
    previous = now;
    return result;
}
struct Sample final {
    std::uint64_t utc{}, qpc{};
    std::optional<double> cpu_percent, used_memory_bytes, processor_mhz, gpu_engine_sum_percent;
    std::optional<int> ac_online;
    std::string power_scheme;
    std::vector<ProcessSample> processes;
    std::vector<GpuEngine> engines;
    bool process_truncated{};
    double collection_ms{};
};
[[nodiscard]] Sample collect_sample(ProcessSampler& processes, GpuSampler& gpu,
    CpuPrevious& cpu, const unsigned logical_processors, const std::uint64_t qpc_frequency,
    const DWORD target_pid, const std::uint64_t target_creation) {
    const auto beginning = Clock::now();
    Sample result; result.utc = utc_filetime(); result.qpc = qpc_ticks();
    result.cpu_percent = system_cpu_percent(cpu);
    MEMORYSTATUSEX memory{}; memory.dwLength = sizeof(memory);
    if (::GlobalMemoryStatusEx(&memory))
        result.used_memory_bytes = static_cast<double>(memory.ullTotalPhys - memory.ullAvailPhys);
    SYSTEM_POWER_STATUS power{};
    if (::GetSystemPowerStatus(&power) && power.ACLineStatus <= 1U)
        result.ac_online = static_cast<int>(power.ACLineStatus);
    result.power_scheme = active_power_scheme();
    result.processor_mhz = processor_mhz(logical_processors);
    result.processes = processes.sample(result.qpc, qpc_frequency, logical_processors, result.process_truncated);
    auto gpu_result = gpu.sample();
    result.engines = std::move(gpu_result.engines);
    if (gpu_result.valid) {
        double sum{};
        for (const auto& engine : result.engines) sum += engine.percent;
        result.gpu_engine_sum_percent = sum;
    }
    // A GPU PDH instance carries PID only. Attribute it to the exact process
    // identity observed in this same snapshot; never carry a reused PID over.
    for (auto& process : result.processes) {
        double sum{}; bool seen{};
        for (const auto& engine : result.engines) if (engine.pid == process.pid) {
            sum += engine.percent; seen = true;
        }
        if (seen) process.gpu_engine_sum_percent = sum;
    }
    std::ranges::sort(result.processes, [](const auto& a, const auto& b) {
        const auto a_cpu = a.cpu_percent.value_or(-1.0), b_cpu = b.cpu_percent.value_or(-1.0);
        if (a_cpu != b_cpu) return a_cpu > b_cpu;
        return a.working_set_bytes.value_or(0U) > b.working_set_bytes.value_or(0U);
    });
    if (result.processes.size() > maximum_processes_recorded) {
        const auto target = std::ranges::find_if(result.processes, [&](const auto& p) {
            return p.pid == target_pid && p.creation == target_creation;
        });
        const bool keep_target = target != result.processes.end() &&
            static_cast<std::size_t>(target - result.processes.begin()) >= maximum_processes_recorded;
        const auto saved = keep_target ? std::optional<ProcessSample>{*target} : std::nullopt;
        result.processes.resize(maximum_processes_recorded);
        if (saved) result.processes.back() = *saved;
    }
    if (result.engines.size() > maximum_gpu_engines_recorded)
        result.engines.resize(maximum_gpu_engines_recorded);
    result.collection_ms = std::chrono::duration<double, std::milli>(Clock::now() - beginning).count();
    return result;
}

[[nodiscard]] std::string sample_json(const Sample& sample, const std::string_view run_id,
    const std::uint64_t qpc_frequency) {
    std::ostringstream out;
    out << "{\"schema\":\"hlclient.system-sample.v1\",\"run_id\":" << json_string(run_id)
        << ",\"utc\":" << json_string(utc_iso(sample.utc))
        << ",\"utc_filetime_100ns\":" << sample.utc
        << ",\"qpc_ticks\":" << sample.qpc
        << ",\"qpc_frequency_hz\":" << qpc_frequency
        << ",\"cpu_total\":" << metric(sample.cpu_percent, "percent", "GetSystemTimes")
        << ",\"memory_physical_used\":" << metric(sample.used_memory_bytes, "bytes", "GlobalMemoryStatusEx")
        << ",\"cpu_frequency_mean\":" << metric(sample.processor_mhz, "MHz", "CallNtPowerInformation(ProcessorInformation)")
        << ",\"gpu_engine_sum\":" << metric(sample.gpu_engine_sum_percent,
            "sum_of_engine_percent_not_total_gpu_utilization", "PDH GPU Engine")
        << ",\"ac_online\":" << metric(sample.ac_online ? std::optional<double>{static_cast<double>(*sample.ac_online)} : std::nullopt,
            "boolean_0_or_1", "GetSystemPowerStatus")
        << ",\"power_scheme\":{\"value\":" << (sample.power_scheme.empty() ? "null" : json_string(sample.power_scheme))
        << ",\"unit\":\"GUID\",\"source\":\"PowerGetActiveScheme\",\"status\":\""
        << (sample.power_scheme.empty() ? "unsupported" : "available") << "\"}"
        << ",\"temperature\":{\"value\":null,\"unit\":\"Celsius\",\"source\":\"no_supported_sensor_interface\",\"status\":\"unsupported\"}"
        << ",\"thermal_limit\":{\"value\":null,\"unit\":\"boolean\",\"source\":\"no_supported_sensor_interface\",\"status\":\"unsupported\"}"
        << ",\"process_scan_truncated\":" << (sample.process_truncated ? "true" : "false")
        << ",\"collection_ms\":" << decimal(sample.collection_ms)
        << ",\"processes\":[";
    bool first = true;
    for (const auto& process : sample.processes) {
        if (!first) out << ','; first = false;
        out << "{\"pid\":" << process.pid << ",\"creation_filetime_100ns\":" << process.creation
            << ",\"image_basename\":" << json_string(process.name)
            << ",\"cpu\":" << metric(process.cpu_percent, "percent_of_total_logical_CPU", "GetProcessTimes")
            << ",\"working_set\":" << metric(process.working_set_bytes ?
                std::optional<double>{static_cast<double>(*process.working_set_bytes)} : std::nullopt,
                "bytes", "GetProcessMemoryInfo")
            << ",\"gpu_engine_sum\":" << metric(process.gpu_engine_sum_percent,
                "sum_of_engine_percent", "PDH GPU Engine PID approximate join to current-process snapshot") << '}';
    }
    out << "],\"gpu_engines\":["; first = true;
    for (const auto& engine : sample.engines) {
        if (!first) out << ','; first = false;
        out << "{\"instance\":" << json_string(engine.instance) << ",\"pid\":" << engine.pid
            << ",\"utilization\":" << metric(engine.percent, "percent", "PDH GPU Engine") << '}';
    }
    out << "]}\n";
    return out.str();
}
[[nodiscard]] std::string system_csv_row(const Sample& sample) {
    std::ostringstream out;
    out << csv_string(utc_iso(sample.utc)) << ',' << sample.utc << ',' << sample.qpc << ','
        << (sample.cpu_percent ? decimal(*sample.cpu_percent) : "") << ','
        << (sample.used_memory_bytes ? decimal(*sample.used_memory_bytes) : "") << ','
        << (sample.processor_mhz ? decimal(*sample.processor_mhz) : "") << ','
        << (sample.gpu_engine_sum_percent ? decimal(*sample.gpu_engine_sum_percent) : "") << ','
        << (sample.ac_online ? std::to_string(*sample.ac_online) : "") << ','
        << csv_string(sample.power_scheme) << ',' << decimal(sample.collection_ms) << '\n';
    return out.str();
}
[[nodiscard]] std::string process_csv_rows(const Sample& sample) {
    std::ostringstream out;
    for (const auto& process : sample.processes) {
        out << csv_string(utc_iso(sample.utc)) << ',' << sample.qpc << ','
            << process.pid << ',' << process.creation << ',' << csv_string(process.name) << ','
            << (process.cpu_percent ? decimal(*process.cpu_percent) : "") << ','
            << (process.working_set_bytes ? std::to_string(*process.working_set_bytes) : "") << ','
            << (process.gpu_engine_sum_percent ? decimal(*process.gpu_engine_sum_percent) : "") << '\n';
    }
    return out.str();
}
[[nodiscard]] bool write_new(const fs::path& path, const std::string_view data) {
    Handle file{::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file.valid()) return false;
    if (data.size() > std::numeric_limits<DWORD>::max()) return false;
    DWORD written{};
    return (data.empty() || (::WriteFile(file.value, data.data(),
        static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size())) &&
        ::FlushFileBuffers(file.value);
}
class OutputFile final {
public:
    explicit OutputFile(const fs::path& path) : handle_{::CreateFileW(path.c_str(),
        GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr)} {}
    OutputFile(const OutputFile&) = delete;
    OutputFile& operator=(const OutputFile&) = delete;
    [[nodiscard]] bool good() const noexcept { return handle_.valid() && !failed_; }
    [[nodiscard]] std::uint64_t bytes() const noexcept { return bytes_; }
    void write(const std::string_view data) {
        if (!good() || data.size() > std::numeric_limits<DWORD>::max()) { failed_ = true; return; }
        DWORD written{};
        if (!::WriteFile(handle_.value, data.data(), static_cast<DWORD>(data.size()),
            &written, nullptr) || written != data.size()) failed_ = true;
        else bytes_ += written;
    }
    [[nodiscard]] bool flush() {
        if (!good() || !::FlushFileBuffers(handle_.value)) failed_ = true;
        return good();
    }
    void close() noexcept {
        if (handle_.valid()) { ::CloseHandle(handle_.value); handle_.value = INVALID_HANDLE_VALUE; }
    }
    void discard() noexcept {
        if (!handle_.valid()) return;
        FILE_DISPOSITION_INFO disposition{TRUE};
        (void)::SetFileInformationByHandle(handle_.value, FileDispositionInfo,
            &disposition, sizeof(disposition));
        close();
    }
private:
    Handle handle_;
    bool failed_{};
    std::uint64_t bytes_{};
};
[[nodiscard]] bool safe_regular_file(const fs::path& path) {
    const auto attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0U;
}

struct ZipEntry final { fs::path file; std::string name; std::uint32_t bytes{}; };
[[nodiscard]] std::vector<ZipEntry> package_entries(const fs::path& root, const fs::path& system,
    std::vector<std::string>& omitted) {
    const std::array<std::wstring_view, 9> selected{
        L"external-run-report.json", L"external-diagnostics.json", L"external-movement.json",
        L"client-status.log", L"process-manifest.json", L"launch-manifest.json",
        L"external-video-final.json", L"external-msaa.json", L"external-player-appearance.json"};
    const std::array<std::wstring_view, 5> own{
        L"samples.jsonl", L"system.csv", L"processes.csv", L"manifest.json", L"report.txt"};
    std::vector<ZipEntry> out;
    std::uint64_t total{};
    const auto append = [&](const fs::path& file, const std::string& name) {
        std::error_code ec;
        if (!safe_regular_file(file)) return;
        const auto bytes = fs::file_size(file, ec);
        if (ec || bytes > maximum_zip_entry_bytes || total + bytes > maximum_zip_total_bytes) {
            omitted.push_back(name + ":size_budget"); return;
        }
        total += bytes;
        out.push_back({file, name, static_cast<std::uint32_t>(bytes)});
    };
    for (const auto name : own)
        append(system / name, "system-diagnostics/" + utf8(name));
    for (const auto name : selected) append(root / name, utf8(name));
    // The dynamically numbered receipt leaves are project-created, direct
    // children only. Never recurse or include arbitrary .json/.log files.
    std::error_code ec;
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const auto filename = it->path().filename().wstring();
        if (filename.starts_with(L"session-receipt-A-") && filename.ends_with(L".txt") &&
            filename.size() < 128U) append(it->path(), utf8(filename));
    }
    return out;
}

[[nodiscard]] std::uint32_t crc32(const std::string_view bytes) noexcept {
    std::uint32_t crc = 0xffffffffU;
    for (const unsigned char c : bytes) {
        crc ^= c;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return crc ^ 0xffffffffU;
}
void write16(OutputFile& out, const std::uint16_t value) {
    const std::array<char, 2> bytes{static_cast<char>(value), static_cast<char>(value >> 8U)};
    out.write(std::string_view{bytes.data(), bytes.size()});
}
void write32(OutputFile& out, const std::uint32_t value) {
    write16(out, static_cast<std::uint16_t>(value)); write16(out, static_cast<std::uint16_t>(value >> 16U));
}
[[nodiscard]] bool write_zip(const fs::path& target, const std::vector<ZipEntry>& entries) {
    const auto partial = fs::path{target.wstring() + L".partial"};
    if (fs::exists(target) || fs::exists(partial) ||
        entries.size() > std::numeric_limits<std::uint16_t>::max()) return false;
    struct Central final { ZipEntry entry; std::uint32_t crc{}, offset{}; };
    std::vector<Central> central;
    OutputFile out(partial);
    if (!out.good()) return false;
    for (const auto& entry : entries) {
        const auto bytes = read_bounded(entry.file, maximum_zip_entry_bytes);
        if (!bytes || bytes->size() != entry.bytes || entry.name.size() > 65535U) {
            out.discard(); return false;
        }
        const auto offset = static_cast<std::uint32_t>(out.bytes());
        const auto crc = crc32(*bytes);
        write32(out, 0x04034b50U); write16(out, 20U); write16(out, 0U); write16(out, 0U);
        write16(out, 0U); write16(out, 0U); write32(out, crc); write32(out, entry.bytes);
        write32(out, entry.bytes); write16(out, static_cast<std::uint16_t>(entry.name.size()));
        write16(out, 0U); out.write(entry.name);
        out.write(*bytes);
        central.push_back({entry, crc, offset});
    }
    const auto central_begin = static_cast<std::uint32_t>(out.bytes());
    for (const auto& item : central) {
        write32(out, 0x02014b50U); write16(out, 20U); write16(out, 20U);
        write16(out, 0U); write16(out, 0U); write16(out, 0U); write16(out, 0U);
        write32(out, item.crc); write32(out, item.entry.bytes); write32(out, item.entry.bytes);
        write16(out, static_cast<std::uint16_t>(item.entry.name.size())); write16(out, 0U);
        write16(out, 0U); write16(out, 0U); write16(out, 0U); write32(out, 0U);
        write32(out, item.offset);
        out.write(item.entry.name);
    }
    const auto central_end = static_cast<std::uint32_t>(out.bytes());
    write32(out, 0x06054b50U); write16(out, 0U); write16(out, 0U);
    write16(out, static_cast<std::uint16_t>(central.size()));
    write16(out, static_cast<std::uint16_t>(central.size()));
    write32(out, central_end - central_begin); write32(out, central_begin); write16(out, 0U);
    if (!out.flush()) { out.discard(); return false; }
    out.close();
    // Publish only the newly created private staging leaf. Never replace an
    // existing package from a repeated run or an unrelated process.
    if (::MoveFileExW(partial.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
    (void)::DeleteFileW(partial.c_str());
    return false;
}

struct TimeAnchor final { std::uint64_t qpc_before{}, utc{}, qpc_after{}; };
[[nodiscard]] TimeAnchor time_anchor() noexcept {
    TimeAnchor result;
    result.qpc_before = qpc_ticks(); result.utc = utc_filetime(); result.qpc_after = qpc_ticks();
    return result;
}
struct HelperOverhead final {
    std::optional<double> cpu_ms;
    std::optional<std::uint64_t> peak_working_set_bytes;
};
[[nodiscard]] HelperOverhead helper_overhead() {
    HelperOverhead result;
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (::GetProcessTimes(::GetCurrentProcess(), &creation, &exit, &kernel, &user))
        result.cpu_ms = static_cast<double>(as_u64(kernel) + as_u64(user)) / 10000.0;
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    if (::GetProcessMemoryInfo(::GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        result.peak_working_set_bytes = memory.PeakWorkingSetSize;
    return result;
}
[[nodiscard]] std::optional<std::uint64_t> json_unsigned_field(
    const std::string_view bytes, const std::string_view key) {
    const auto marker = "\"" + std::string(key) + "\"";
    auto at = bytes.find(marker); if (at == std::string_view::npos) return {};
    at = bytes.find(':', at + marker.size()); if (at == std::string_view::npos) return {};
    ++at; while (at < bytes.size() && (bytes[at] == ' ' || bytes[at] == '\n')) ++at;
    const auto begin = at;
    while (at < bytes.size() && bytes[at] >= '0' && bytes[at] <= '9') ++at;
    if (begin == at) return {};
    std::uint64_t result{};
    for (std::size_t i = begin; i < at; ++i) {
        const auto digit = static_cast<unsigned>(bytes[i] - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10ULL) return {};
        result = result * 10ULL + digit;
    }
    return result;
}
[[nodiscard]] std::optional<std::string> json_text_field(
    const std::string_view bytes, const std::string_view key) {
    const auto marker = "\"" + std::string(key) + "\"";
    auto at = bytes.find(marker); if (at == std::string_view::npos) return {};
    at = bytes.find(':', at + marker.size()); if (at == std::string_view::npos) return {};
    at = bytes.find('"', at + 1U); if (at == std::string_view::npos) return {};
    const auto end = bytes.find('"', at + 1U);
    if (end == std::string_view::npos || end - at > 256U) return {};
    return std::string(bytes.substr(at + 1U, end - at - 1U));
}
[[nodiscard]] bool matching_marker(const fs::path& run, const Args& args) {
    const auto bytes = read_bounded(run / L"launcher-finished.marker", 4096U);
    return bytes && json_text_field(*bytes, "run_id") == args.run_id &&
        json_unsigned_field(*bytes, "pid") == args.pid &&
        json_unsigned_field(*bytes, "creation_filetime") == args.creation;
}
[[nodiscard]] std::optional<std::uint64_t> nanoseconds_to_qpc(
    const std::uint64_t ns, const std::uint64_t frequency) {
    if (frequency == 0U) return {};
    constexpr std::uint64_t billion = 1000000000ULL;
    const auto seconds = ns / billion, fraction = ns % billion;
    if (seconds > std::numeric_limits<std::uint64_t>::max() / frequency) return {};
    // QPC frequency on supported Windows hardware is far below 1e9; guard
    // even a malformed frequency rather than overflowing the fraction term.
    if (frequency > std::numeric_limits<std::uint64_t>::max() / billion) return {};
    return seconds * frequency + (fraction * frequency + billion / 2U) / billion;
}
struct F8Bridge final {
    std::string status{"unavailable"};
    std::size_t markers_total{}, markers_with_pre_and_post_context{};
    std::uint64_t first_qpc{}, last_qpc{};
};
[[nodiscard]] F8Bridge f8_bridge(const fs::path& run, const Args& args,
    const std::uint64_t frequency, const std::vector<std::uint64_t>& sample_qpcs) {
    F8Bridge result;
    const auto bytes = read_bounded(run / L"external-movement.json", maximum_zip_entry_bytes);
    if (!bytes || !json_unsigned_field(*bytes, "application_steady_origin_ns")) return result;
    if (json_text_field(*bytes, "run_id") != args.run_id ||
        json_unsigned_field(*bytes, "process_id") != args.pid ||
        json_unsigned_field(*bytes, "process_creation") != args.creation) {
        result.status = "game_record_identity_mismatch"; return result;
    }
    const auto receipt = read_bounded(run / L"session-receipt-A-terminal.txt", 65536U);
    if (!receipt || receipt->find("run_id=" + args.run_id) == std::string::npos ||
        receipt->find("process_id=" + std::to_string(args.pid)) == std::string::npos ||
        receipt->find("process_creation=" + std::to_string(args.creation)) == std::string::npos ||
        receipt->find("host_qpc_frequency=" + std::to_string(frequency)) == std::string::npos) {
        result.status = "game_clock_receipt_unverified"; return result;
    }
    // The game records manual_windows.trigger_ns at the frame-swap boundary.
    // On MSVC steady_clock is QPC-backed. This aligns clocks, not key-down
    // timing or causal attribution of a one-second metric to a short frame.
    const auto windows = bytes->find("\"manual_windows\"");
    if (windows == std::string::npos) return result;
    const auto open = bytes->find('[', windows + 16U);
    if (open == std::string::npos) return result;
    std::size_t close{};
    unsigned depth{}; bool inside_string{}, escaped{};
    for (auto cursor = open; cursor < bytes->size(); ++cursor) {
        const char c = (*bytes)[cursor];
        if (inside_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inside_string = false;
        } else if (c == '"') inside_string = true;
        else if (c == '[') ++depth;
        else if (c == ']' && --depth == 0U) { close = cursor; break; }
    }
    if (close <= open) { result.status = "manual_window_array_invalid"; return result; }
    const std::string_view manual{bytes->data() + open, close - open + 1U};
    auto at = std::size_t{};
    for (unsigned i = 0; i < 256U; ++i) {
        at = manual.find("\"trigger_ns\"", at);
        if (at == std::string_view::npos) break;
        const auto colon = manual.find(':', at + 12U);
        if (colon == std::string_view::npos) break;
        auto digit = colon + 1U;
        while (digit < manual.size() && (manual[digit] == ' ' || manual[digit] == '\n')) ++digit;
        auto end = digit;
        while (end < manual.size() && manual[end] >= '0' && manual[end] <= '9') ++end;
        std::uint64_t ns{}; bool valid = end > digit;
        for (auto cursor = digit; cursor < end && valid; ++cursor) {
            const auto value = static_cast<unsigned>(manual[cursor] - '0');
            if (ns > (std::numeric_limits<std::uint64_t>::max() - value) / 10ULL) valid = false;
            else ns = ns * 10ULL + value;
        }
        if (valid) if (const auto qpc = nanoseconds_to_qpc(ns, frequency)) {
            if (result.markers_total == 0U) result.first_qpc = *qpc;
            result.last_qpc = *qpc; ++result.markers_total;
            // Alignment alone is not before/after context. Require a sample
            // on each side, both no more than two sampling intervals away.
            const auto next = std::lower_bound(sample_qpcs.begin(), sample_qpcs.end(), *qpc);
            if (next != sample_qpcs.begin() && next != sample_qpcs.end() &&
                *qpc - *(next - 1) <= frequency * 2U &&
                *next - *qpc <= frequency * 2U)
                ++result.markers_with_pre_and_post_context;
        }
        at = end;
    }
    result.status = result.markers_total == 0U ? "origin_present_no_manual_marker" :
        result.markers_with_pre_and_post_context == result.markers_total ?
        "qpc_aligned_one_second_context_only" : "partial_marker_outside_sample_window";
    return result;
}

[[nodiscard]] std::string build_manifest(const Args& args, const TimeAnchor& start,
    const TimeAnchor& end, const std::uint64_t frequency, const std::size_t samples,
    const std::uint64_t first_sample_qpc, const std::uint64_t last_sample_qpc,
    const std::string_view stop_reason, const std::optional<DWORD> exit_code,
    const bool marker_seen, const bool gpu_supported, const std::string_view adapter,
    const hlclient::system_diagnostics::WprProbeResult& wpr,
    const bool storage_truncated, const bool process_truncated,
    const double collection_mean_ms, const double collection_max_ms,
    const HelperOverhead& helper,
    const F8Bridge& f8, const std::vector<std::string>& omitted) {
    std::ostringstream out;
    out << "{\n\"schema\":\"hlclient.system-diagnostics.v1\",\n\"run_id\":"
        << json_string(args.run_id) << ",\n\"process_id\":" << args.pid
        << ",\n\"process_creation_filetime_100ns\":" << args.creation
        << ",\n\"status\":\"partial\",\n\"stop_reason\":" << json_string(stop_reason)
        << ",\n\"client_exit_code\":" << (exit_code ? std::to_string(*exit_code) : "null")
        << ",\n\"launcher_marker_matched\":" << (marker_seen ? "true" : "false")
        << ",\n\"time\":{\"clock\":\"QueryPerformanceCounter\",\"frequency_hz\":" << frequency
        << ",\"start\":{\"qpc_before\":" << start.qpc_before
        << ",\"utc_filetime_100ns\":" << start.utc
        << ",\"utc\":" << json_string(utc_iso(start.utc))
        << ",\"qpc_after\":" << start.qpc_after << "},\"end\":{\"qpc_before\":"
        << end.qpc_before << ",\"utc_filetime_100ns\":" << end.utc
        << ",\"utc\":" << json_string(utc_iso(end.utc))
        << ",\"qpc_after\":" << end.qpc_after << "},\"first_sample_qpc\":"
        << first_sample_qpc << ",\"last_sample_qpc\":" << last_sample_qpc << "},\n"
        << "\"sampling\":{\"target_interval_ms\":1000,\"count\":" << samples
        << ",\"maximum_duration_minutes\":60,\"maximum_file_bytes_each\":"
        << maximum_sample_file_bytes << ",\"storage_truncated\":"
        << (storage_truncated ? "true" : "false") << ",\"process_scan_truncated\":"
        << (process_truncated ? "true" : "false") << ",\"collection_mean_ms\":"
        << decimal(collection_mean_ms) << ",\"collection_max_ms\":" << decimal(collection_max_ms)
        << ",\"durable_flush_interval_samples\":10,\"crash_tail_risk\":\"last_unflushed_ten_seconds_may_be_lost_on_system_failure\"},\n\"helper_overhead_pre_archive\":{\"cpu_ms\":"
        << maybe_number(helper.cpu_ms) << ",\"peak_working_set_bytes\":"
        << (helper.peak_working_set_bytes ? std::to_string(*helper.peak_working_set_bytes) : "null")
        << ",\"status\":\"observed_process_cost_not_game_fps_impact\"},\n"
        << "\"sources\":{\"cpu_total\":\"GetSystemTimes\",\"process_cpu\":\"GetProcessTimes + PID/creation FILETIME\","
        << "\"memory\":\"GlobalMemoryStatusEx/GetProcessMemoryInfo\",\"power\":\"GetSystemPowerStatus/PowerGetActiveScheme\","
        << "\"frequency\":\"CallNtPowerInformation(ProcessorInformation)\",\"gpu_engine\":{\"source\":\"PDH GPU Engine\","
        << "\"status\":\"" << (gpu_supported ? "available_or_counter_empty" : "unsupported") << "\",\"adapter\":"
        << (adapter.empty() ? "null" : json_string(adapter)) << "},\"temperature\":{\"status\":\"unsupported\","
        << "\"reason\":\"no_supported_sensor_interface\"},\"thermal_limit\":{\"status\":\"unsupported\","
        << "\"reason\":\"no_supported_sensor_interface\"},\"wpr\":{\"installed\":"
        << (wpr.installed ? "true" : "false") << ",\"probe_status\":" << json_string(wpr.status)
        << ",\"cpu_light_supported\":" << (wpr.cpu_light_supported ? "true" : "false")
        << ",\"gpu_light_supported\":" << (wpr.gpu_light_supported ? "true" : "false")
        << ",\"active_other_recording\":" << (wpr.active_other_recording ? "true" : "false")
        << ",\"declared_cpu_buffer_bytes\":" << (wpr.declared_cpu_buffer_bytes ?
            std::to_string(*wpr.declared_cpu_buffer_bytes) : "null")
        << ",\"declared_gpu_buffer_bytes\":" << (wpr.declared_gpu_buffer_bytes ?
            std::to_string(*wpr.declared_gpu_buffer_bytes) : "null")
        << ",\"within_256mib_memory_budget\":" << (wpr.within_memory_budget ? "true" : "false")
        << ",\"trace_status\":\"not_started\",\"reason\":" << json_string(wpr.reason)
        << "}},\n"
        << "\"f8\":{\"status\":" << json_string(f8.status)
        << ",\"marker_count\":" << f8.markers_total
        << ",\"markers_with_pre_and_post_context\":" << f8.markers_with_pre_and_post_context
        << ",\"first_qpc\":" << (f8.markers_total ? std::to_string(f8.first_qpc) : "null")
        << ",\"last_qpc\":" << (f8.markers_total ? std::to_string(f8.last_qpc) : "null")
        << ",\"interpretation\":\"frame-swap marker, not key-down; 1s sample cannot prove short-frame cause; game ring may overwrite older context\"},\n"
        << "\"archive\":{\"etl_in_zip\":false,\"etl_status\":\"not_recorded\",\"omitted\":[";
    for (std::size_t i = 0; i < omitted.size(); ++i) {
        if (i) out << ','; out << json_string(omitted[i]);
    }
    out << "]},\n\"privacy\":\"local_only_no_upload_no_command_lines_no_window_content\"\n}\n";
    return out.str();
}
[[nodiscard]] std::string build_report(const Args& args, const std::size_t samples,
    const std::string_view stop_reason, const std::optional<DWORD> exit_code,
    const bool marker_seen, const bool gpu_supported,
    const hlclient::system_diagnostics::WprProbeResult& wpr, const F8Bridge& f8,
    const bool storage_truncated, const double collection_mean_ms, const double collection_max_ms,
    const HelperOverhead& helper) {
    std::ostringstream out;
    out << "HL Client: системна діагностика CPU/GPU\nRun ID: " << args.run_id
        << "\nPID і час створення: " << args.pid << " / " << args.creation
        << "\nСтатус: часткове покриття. Зупинка: " << stop_reason << ".\n"
        << "Код завершення клієнта: " << (exit_code ? std::to_string(*exit_code) : "невідомий")
        << ". Фінальний маркер лаунчера: " << (marker_seen ? "перевірений" : "не отриманий") << ".\n"
        << "Записано секундних вибірок: " << samples << ". Показники GPU: "
        << (gpu_supported ? "PDH доступний (сума engine не є загальним GPU%)" : "PDH недоступний") << ".\n"
        << "Витрати helper на вибірку: середнє " << decimal(collection_mean_ms)
        << " мс, максимум " << decimal(collection_max_ms) << " мс; це не вимір повного впливу на FPS.\n"
        << "Дані записуються щосекунди, примусовий flush кожні 10 вибірок і при завершенні;"
        << " останні до 10 секунд можуть втратитися при системному збої.\n"
        << "Сам helper до архівування: CPU " << maybe_number(helper.cpu_ms)
        << " мс, пік working set "
        << (helper.peak_working_set_bytes ? std::to_string(*helper.peak_working_set_bytes) : "недоступний")
        << " байт. Це не A/B доказ впливу на FPS.\n"
        << "Температурні обмеження не перевірені: датчики недоступні.\n"
        << "WPR ETL не записувався: " << wpr.reason << " (перевірка: " << wpr.status
        << "). Чужі ETW/WPR-сесії не змінювалися.\n"
        << "F8: " << f8.markers_total << " маркерів; QPC-контекст секундних вибірок: "
        << f8.markers_with_pre_and_post_context << ". Статус: " << f8.status << ".\n"
        << "Observed: зазначені системні вибірки й власні виміри гри. Correlated: тільки часовий збіг QPC.\n"
        << "Confirmed cause of FPS drop: unknown. Секундна активність, температура й вентилятори"
        << " самі по собі не доводять причину окремого довгого кадру.\n"
        << "F8 позначає кадр після swap, а не точний момент натискання. Старі події кільцевого"
        << " буфера гри могли бути перезаписані.\n"
        << "Ліміт storage: " << (storage_truncated ? "досягнутий, покриття обірване" : "не досягнутий")
        << ". Архів локальний, без автоматичного завантаження.\n";
    return out.str();
}

[[nodiscard]] int publish_preclient_partial(const Args& args, const TimeAnchor& initial,
    const std::optional<Sample>& baseline, const GpuSampler& gpu,
    std::string_view reason, bool launcher_marker_seen);
[[nodiscard]] int monitor(const Args& args, const bool precreated_directory,
    const std::optional<Sample>& prelaunch_sample, const TimeAnchor& initial_anchor,
    GpuSampler& gpu) {
    const auto system = args.run_root / L"system-diagnostics";
    std::error_code ec;
    if (precreated_directory) {
        if (!fs::is_directory(system, ec) || ec) return 3;
    } else if (!fs::create_directory(system, ec) || ec) return 3;
    if ((::GetFileAttributesW(system.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) return 3;
    Handle client{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
        FALSE, args.pid)};
    FILETIME creation{}, exit_time{}, kernel{}, user{};
    if (!client.valid() || !::GetProcessTimes(client.value, &creation, &exit_time, &kernel, &user) ||
        as_u64(creation) != args.creation)
        return publish_preclient_partial(args, initial_anchor, prelaunch_sample, gpu,
            "client_exited_before_helper_attach_or_pid_reused", false);
    OutputFile json(system / L"samples.jsonl");
    OutputFile csv(system / L"system.csv");
    OutputFile pcsv(system / L"processes.csv");
    if (!json.good() || !csv.good() || !pcsv.good()) return 3;
    csv.write("utc,utc_filetime_100ns,qpc_ticks,cpu_total_percent,memory_physical_used_bytes,"
              "cpu_frequency_mean_mhz,gpu_engine_sum_percent,ac_online,power_scheme_guid,collection_ms\n");
    pcsv.write("utc,qpc_ticks,pid,creation_filetime_100ns,image_basename,cpu_percent_of_total,"
               "working_set_bytes,gpu_engine_sum_percent\n");

    LARGE_INTEGER qpf{};
    if (!::QueryPerformanceFrequency(&qpf) || qpf.QuadPart <= 0) return 3;
    const auto frequency = static_cast<std::uint64_t>(qpf.QuadPart);
    const auto start = initial_anchor;
    const auto started = Clock::now();
    const auto adapter = display_adapter();
    const unsigned logical = ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    ProcessSampler processes; CpuPrevious cpu;
    bool storage_truncated{}, process_truncated{}, marker_seen{};
    std::string stop_reason{"client_exited"};
    std::size_t samples{};
    std::uint64_t first_sample_qpc{}, last_sample_qpc{};
    std::vector<std::uint64_t> sample_qpcs;
    double collection_sum_ms{}, collection_max_ms{};
    std::optional<DWORD> exit_code;
    const auto record = [&](const Sample& sample) -> bool {
        const auto json_row = sample_json(sample, args.run_id, frequency);
        const auto csv_row = system_csv_row(sample), pcsv_rows = process_csv_rows(sample);
        if (json.bytes() + json_row.size() > maximum_sample_file_bytes ||
            csv.bytes() + csv_row.size() > maximum_sample_file_bytes ||
            pcsv.bytes() + pcsv_rows.size() > maximum_sample_file_bytes) {
            storage_truncated = true; stop_reason = "sample_storage_limit"; return false;
        }
        json.write(json_row); csv.write(csv_row); pcsv.write(pcsv_rows);
        // WriteFile is buffered by Windows. A disk flush on every 1 Hz row
        // can introduce the frame-pacing spikes this tool is investigating.
        // Flush every ten rows and at normal shutdown instead.
        if (!json.good() || !csv.good() || !pcsv.good() ||
            ((samples + 1U) % 10U == 0U &&
             (!json.flush() || !csv.flush() || !pcsv.flush()))) {
            stop_reason = "sample_write_failed"; return false;
        }
        if (samples == 0U) first_sample_qpc = sample.qpc;
        last_sample_qpc = sample.qpc;
        sample_qpcs.push_back(sample.qpc);
        ++samples;
        process_truncated |= sample.process_truncated;
        collection_sum_ms += sample.collection_ms;
        collection_max_ms = std::max(collection_max_ms, sample.collection_ms);
        return true;
    };
    if (prelaunch_sample && !record(*prelaunch_sample)) return 3;
    for (;;) {
        if (Clock::now() - started >= maximum_recording) {
            stop_reason = "recording_duration_limit"; break;
        }
        if (::WaitForSingleObject(client.value, 0U) == WAIT_OBJECT_0) {
            DWORD code{}; if (::GetExitCodeProcess(client.value, &code)) exit_code = code;
            break;
        }
        const auto sample = collect_sample(processes, gpu, cpu, logical, frequency,
            args.pid, args.creation);
        if (!record(sample)) break;
        const auto observed = ::WaitForSingleObject(client.value, 1000U);
        if (observed == WAIT_OBJECT_0) {
            DWORD code{}; if (::GetExitCodeProcess(client.value, &code)) exit_code = code;
            break;
        }
        if (observed == WAIT_FAILED) { stop_reason = "client_wait_failed"; break; }
    }
    if (!json.flush() || !csv.flush() || !pcsv.flush())
        stop_reason = "sample_flush_failed";
    json.close(); csv.close(); pcsv.close();
    if (!exit_code) {
        // No indefinite background process after reaching the duration or
        // storage bound. The package is explicitly partial while game lives.
        if (::WaitForSingleObject(client.value, 0U) == WAIT_OBJECT_0) {
            DWORD code{}; if (::GetExitCodeProcess(client.value, &code)) exit_code = code;
        }
    }
    if (exit_code) {
        const auto deadline = Clock::now() + marker_grace;
        do {
            if (matching_marker(args.run_root, args)) { marker_seen = true; break; }
            ::Sleep(100U);
        } while (Clock::now() < deadline);
    }
    const auto end = time_anchor();
    const auto f8 = f8_bridge(args.run_root, args, frequency, sample_qpcs);
    const auto wpr = hlclient::system_diagnostics::probe_wpr_read_only();
    const auto overhead = helper_overhead();
    std::vector<std::string> omitted;
    (void)package_entries(args.run_root, system, omitted);
    const auto mean_ms = samples ? collection_sum_ms / static_cast<double>(samples) : 0.0;
    if (!write_new(system / L"manifest.json", build_manifest(args, start, end, frequency,
            samples, first_sample_qpc, last_sample_qpc, stop_reason, exit_code,
            marker_seen, gpu.supported(), adapter, wpr, storage_truncated,
            process_truncated, mean_ms, collection_max_ms, overhead, f8, omitted))) return 3;
    if (!write_new(system / L"report.txt", build_report(args, samples, stop_reason,
            exit_code, marker_seen, gpu.supported(), wpr, f8, storage_truncated,
            mean_ms, collection_max_ms, overhead))) return 3;
    omitted.clear();
    const auto entries = package_entries(args.run_root, system, omitted);
    if (!write_zip(args.run_root / L"system-diagnostics.zip", entries)) return 3;
    return 0;
}
[[nodiscard]] int publish_preclient_partial(const Args& args, const TimeAnchor& initial,
    const std::optional<Sample>& baseline, const GpuSampler& gpu,
    const std::string_view reason, const bool launcher_marker_seen) {
    const auto system = args.run_root / L"system-diagnostics";
    LARGE_INTEGER qpf{};
    if (!::QueryPerformanceFrequency(&qpf) || qpf.QuadPart <= 0) return 3;
    const auto frequency = static_cast<std::uint64_t>(qpf.QuadPart);
    const auto start_qpc = baseline ? baseline->qpc : 0U;
    if (!write_new(system / L"samples.jsonl", baseline ?
            sample_json(*baseline, args.run_id, frequency) : "") ||
        !write_new(system / L"system.csv", std::string{
            "utc,utc_filetime_100ns,qpc_ticks,cpu_total_percent,memory_physical_used_bytes,"
            "cpu_frequency_mean_mhz,gpu_engine_sum_percent,ac_online,power_scheme_guid,collection_ms\n"} +
            (baseline ? system_csv_row(*baseline) : "")) ||
        !write_new(system / L"processes.csv", std::string{
            "utc,qpc_ticks,pid,creation_filetime_100ns,image_basename,cpu_percent_of_total,"
            "working_set_bytes,gpu_engine_sum_percent\n"} +
            (baseline ? process_csv_rows(*baseline) : ""))) return 3;
    const auto end = time_anchor();
    const F8Bridge f8{};
    const auto wpr = hlclient::system_diagnostics::probe_wpr_read_only();
    const auto overhead = helper_overhead();
    std::vector<std::string> omitted;
    (void)package_entries(args.run_root, system, omitted);
    if (!write_new(system / L"manifest.json", build_manifest(args, initial, end, frequency,
            baseline ? 1U : 0U, start_qpc, start_qpc, reason, std::nullopt,
            launcher_marker_seen, gpu.supported(), display_adapter(), wpr,
            false, baseline && baseline->process_truncated,
            baseline ? baseline->collection_ms : 0.0,
            baseline ? baseline->collection_ms : 0.0, overhead, f8, omitted)) ||
        !write_new(system / L"report.txt", build_report(args, baseline ? 1U : 0U,
            reason, std::nullopt, launcher_marker_seen, gpu.supported(), wpr, f8, false,
            baseline ? baseline->collection_ms : 0.0,
            baseline ? baseline->collection_ms : 0.0, overhead))) return 3;
    omitted.clear();
    const auto entries = package_entries(args.run_root, system, omitted);
    return write_zip(args.run_root / L"system-diagnostics.zip", entries) ? 0 : 3;
}
[[nodiscard]] int await_client(Args args) {
    const auto system = args.run_root / L"system-diagnostics";
    std::error_code ec;
    if (!fs::create_directory(system, ec) || ec) return 3;
    const auto attributes = ::GetFileAttributesW(system.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) return 3;
    LARGE_INTEGER qpf{};
    if (!::QueryPerformanceFrequency(&qpf) || qpf.QuadPart <= 0) return 3;
    const auto initial = time_anchor();
    ProcessSampler process_sampler; CpuPrevious cpu; GpuSampler gpu;
    const auto baseline = collect_sample(process_sampler, gpu, cpu,
        ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),
        static_cast<std::uint64_t>(qpf.QuadPart), 0U, 0U);
    const auto ready = std::string{"{\"schema\":\"hlclient.system-helper-ready.v1\",\"run_id\":"} +
        json_string(args.run_id) + ",\"utc_filetime_100ns\":" + std::to_string(initial.utc) +
        ",\"qpc_ticks\":" + std::to_string(initial.qpc_after) + "}\n";
    if (!write_new(args.run_root / L"system-helper-ready.marker", ready)) return 3;
    const auto deadline = Clock::now() + std::chrono::seconds{30};
    for (;;) {
        if (const auto manifest = read_bounded(args.run_root / L"process-manifest.json", 65536U)) {
            const auto id = json_text_field(*manifest, "run_id");
            const auto pid = json_unsigned_field(*manifest, "process_id");
            const auto creation = json_unsigned_field(*manifest, "process_creation");
            if (id != args.run_id || !pid || *pid == 0U ||
                *pid > std::numeric_limits<DWORD>::max() || !creation || *creation == 0U)
                return publish_preclient_partial(args, initial, baseline, gpu,
                    "process_manifest_identity_invalid", false);
            args.pid = static_cast<DWORD>(*pid); args.creation = *creation;
            return monitor(args, true, baseline, initial, gpu);
        }
        if (const auto marker = read_bounded(args.run_root / L"launcher-finished.marker", 4096U);
            marker && json_text_field(*marker, "run_id") == args.run_id)
            return publish_preclient_partial(args, initial, baseline, gpu,
                "launcher_finished_before_client", true);
        if (Clock::now() >= deadline) return publish_preclient_partial(args, initial,
            baseline, gpu, "process_manifest_wait_timeout", false);
        ::Sleep(50U);
    }
}
} // namespace

int wmain(const int argc, wchar_t* argv[]) {
    const auto args = parse_args(argc, argv);
    if (!args) return 2;
    try {
        if (args->await_client) return await_client(*args);
        GpuSampler gpu;
        return monitor(*args, false, std::nullopt, time_anchor(), gpu);
    }
    catch (...) { return 3; }
}
