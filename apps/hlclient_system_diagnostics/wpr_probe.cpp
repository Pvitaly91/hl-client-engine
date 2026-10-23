#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "wpr_probe.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace hlclient::system_diagnostics {
namespace {

constexpr std::size_t kMaximumOutputBytes = 64U * 1024U;

struct Handle final {
    HANDLE value{};
    explicit Handle(HANDLE initial = nullptr) noexcept : value(initial) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
    [[nodiscard]] bool valid() const noexcept { return value && value != INVALID_HANDLE_VALUE; }
};

[[nodiscard]] bool ordinary_file(const std::wstring& path) noexcept {
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0U;
}

[[nodiscard]] std::wstring windows_directory() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const UINT count = ::GetWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    if (count == 0U || count >= buffer.size()) return {};
    return std::wstring{buffer.data(), count};
}

[[nodiscard]] std::wstring find_wpr() {
    const auto root = windows_directory();
    if (root.empty()) return {};
    BOOL wow64 = FALSE;
    if (::IsWow64Process(::GetCurrentProcess(), &wow64) && wow64) {
        const auto native = root + L"\\Sysnative\\wpr.exe";
        if (ordinary_file(native)) return native;
    }
    const auto system = root + L"\\System32\\wpr.exe";
    if (ordinary_file(system)) return system;
    return {};
}

struct QueryResult final {
    bool launched{}, timed_out{}, output_truncated{};
    DWORD exit_code{STILL_ACTIVE};
    std::string output;
    [[nodiscard]] bool succeeded() const noexcept {
        return launched && !timed_out && !output_truncated && exit_code == 0U;
    }
};

[[nodiscard]] QueryResult query(const std::wstring& executable,
    const std::wstring_view fixed_argument, const std::chrono::milliseconds timeout) {
    QueryResult result;
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE raw_read{}, raw_write{};
    if (!::CreatePipe(&raw_read, &raw_write, &inherit, 0U)) return result;
    Handle read{raw_read}, write{raw_write};
    if (!::SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0U)) return result;
    Handle input{::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!input.valid()) return result;

    SIZE_T attribute_bytes{};
    (void)::InitializeProcThreadAttributeList(nullptr, 1U, 0U, &attribute_bytes);
    if (!attribute_bytes || attribute_bytes > 64U * 1024U) return result;
    std::vector<std::byte> attribute_storage(attribute_bytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    if (!::InitializeProcThreadAttributeList(attributes, 1U, 0U, &attribute_bytes)) return result;
    struct DeleteAttributes final {
        LPPROC_THREAD_ATTRIBUTE_LIST value;
        ~DeleteAttributes() { ::DeleteProcThreadAttributeList(value); }
    } delete_attributes{attributes};
    std::array<HANDLE, 2> inherited{write.value, input.value};
    if (!::UpdateProcThreadAttribute(attributes, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited.data(), sizeof(inherited), nullptr, nullptr)) return result;

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = write.value;
    startup.StartupInfo.hStdError = write.value;
    startup.lpAttributeList = attributes;
    auto command = L'"' + executable + L"\" " + std::wstring{fixed_argument};
    PROCESS_INFORMATION created{};
    if (!::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
        &startup.StartupInfo, &created)) return result;
    result.launched = true;
    Handle process{created.hProcess}, thread{created.hThread};
    // The parent must not keep an extra write end: EOF then reflects only the
    // owned child. A descendant cannot inherit it through our handle list.
    ::CloseHandle(write.value);
    write.value = nullptr;
    const auto end = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        DWORD available{};
        if (::PeekNamedPipe(read.value, nullptr, 0U, nullptr, &available, nullptr) && available) {
            if (result.output.size() >= kMaximumOutputBytes) {
                result.output_truncated = true;
                break;
            }
            std::array<char, 4096> buffer{};
            const auto requested = static_cast<DWORD>(std::min<std::size_t>(
                {buffer.size(), static_cast<std::size_t>(available),
                    kMaximumOutputBytes - result.output.size()}));
            DWORD received{};
            if (requested && ::ReadFile(read.value, buffer.data(), requested, &received, nullptr) && received)
                result.output.append(buffer.data(), received);
        }
        if (::WaitForSingleObject(process.value, 0U) == WAIT_OBJECT_0) {
            DWORD remaining_available{};
            if (!::PeekNamedPipe(read.value, nullptr, 0U, nullptr, &remaining_available, nullptr) ||
                !remaining_available)
                break;
        }
        if (std::chrono::steady_clock::now() >= end) {
            result.timed_out = true;
            break;
        }
        ::Sleep(10U);
    }
    if (result.timed_out || result.output_truncated) {
        (void)::TerminateProcess(process.value, 126U); // exact owned probe only
        (void)::WaitForSingleObject(process.value, 1000U);
    }
    (void)::GetExitCodeProcess(process.value, &result.exit_code);
    return result;
}

[[nodiscard]] std::optional<std::uint64_t> number_after_colon(const std::string_view line) {
    const auto colon = line.find(':');
    if (colon == line.npos) return {};
    auto at = colon + 1U;
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
    std::uint64_t value{};
    const auto* begin = line.data() + at;
    const auto [end, error] = std::from_chars(begin, line.data() + line.size(), value);
    if (error != std::errc{} || end == begin) return {};
    return value;
}

[[nodiscard]] std::optional<std::uint64_t> declared_buffers(const std::string_view output) {
    constexpr std::string_view size_label = "Buffer Size (KB)";
    constexpr std::string_view count_label = "Number of Buffers";
    std::optional<std::uint64_t> size_kib;
    std::uint64_t total{};
    std::size_t pairs{};
    for (std::size_t begin{}; begin < output.size();) {
        const auto end = output.find('\n', begin);
        const auto line = output.substr(begin,
            end == output.npos ? output.npos : end - begin);
        if (line.find(size_label) != line.npos) size_kib = number_after_colon(line);
        else if (line.find(count_label) != line.npos) {
            const auto count = number_after_colon(line);
            if (!size_kib || !count || *size_kib == 0U || *count == 0U ||
                *size_kib > std::numeric_limits<std::uint64_t>::max() / 1024U / *count)
                return {};
            const auto bytes = *size_kib * *count * 1024U;
            if (bytes > std::numeric_limits<std::uint64_t>::max() - total) return {};
            total += bytes;
            ++pairs;
            size_kib.reset();
        }
        if (end == output.npos) break;
        begin = end + 1U;
    }
    return pairs && !size_kib ? std::optional{total} : std::nullopt;
}

[[nodiscard]] std::string version_in(const std::string_view output) {
    const auto at = output.find("Version ");
    if (at == output.npos) return {};
    const auto begin = at + sizeof("Version ") - 1U;
    auto end = begin;
    while (end < output.size() && end - begin < 32U &&
        ((output[end] >= '0' && output[end] <= '9') || output[end] == '.')) ++end;
    return std::string{output.substr(begin, end - begin)};
}

} // namespace

WprProbeResult probe_wpr_read_only(const std::chrono::milliseconds timeout) noexcept {
    WprProbeResult result;
    try {
        const auto executable = find_wpr();
        if (executable.empty()) {
            result.status = result.reason = "not_installed";
            return result;
        }
        result.installed = true;
        // Only a successful, explicitly idle status query may clear this.
        result.active_other_recording = true;
        const auto safe_timeout = std::clamp(timeout,
            std::chrono::milliseconds{100}, std::chrono::milliseconds{2000});
        const auto profiles = query(executable, L"-profiles", safe_timeout);
        if (!profiles.succeeded()) {
            result.status = result.reason = profiles.timed_out ? "profiles_probe_timeout" :
                "profiles_probe_unavailable";
            return result;
        }
        result.version = version_in(profiles.output);
        const auto cpu = query(executable, L"-profiledetails CPU.light", safe_timeout);
        const auto gpu = query(executable, L"-profiledetails GPU.light", safe_timeout);
        result.cpu_light_supported = cpu.succeeded() &&
            cpu.output.find("CPU.Light.Memory") != std::string::npos;
        result.gpu_light_supported = gpu.succeeded() &&
            gpu.output.find("GPU.Light.Memory") != std::string::npos;
        if (result.cpu_light_supported)
            result.declared_cpu_buffer_bytes = declared_buffers(cpu.output);
        if (result.gpu_light_supported)
            result.declared_gpu_buffer_bytes = declared_buffers(gpu.output);
        const auto status = query(executable, L"-status", safe_timeout);
        const bool idle = status.succeeded() &&
            status.output.find("WPR is not recording") != std::string::npos;
        result.active_other_recording = !idle;
        if (!status.succeeded() || (!idle &&
            status.output.find("recording") == std::string::npos)) {
            result.status = result.reason = "recording_status_unverified";
        } else if (!idle) {
            result.status = result.reason = "foreign_recording_active";
        } else if (!result.cpu_light_supported || !result.gpu_light_supported) {
            result.status = result.reason = cpu.timed_out || gpu.timed_out ?
                "profile_detail_timeout" : "light_profiles_unsupported";
        } else if (!result.declared_cpu_buffer_bytes ||
            !result.declared_gpu_buffer_bytes) {
            result.status = result.reason = "profile_buffer_budget_unverified";
        } else if (*result.declared_cpu_buffer_bytes > kWprMaximumDeclaredBufferBytes ||
            *result.declared_gpu_buffer_bytes >
                kWprMaximumDeclaredBufferBytes - *result.declared_cpu_buffer_bytes) {
            result.status = result.reason = "verified_builtin_profiles_over_budget";
        } else {
            result.within_memory_budget = true;
            result.status = "verified_within_budget_not_recorded";
            result.reason = "recording_not_implemented";
        }
    } catch (...) {
        result.status = result.reason = "probe_exception";
        result.active_other_recording = true;
    }
    return result;
}

} // namespace hlclient::system_diagnostics
