#include <hlclient/platform/windows/network_isolation.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
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

namespace {

namespace windows = hlclient::platform::windows;

struct Options final {
    HANDLE readiness{nullptr};
    HANDLE heartbeat{nullptr};
    HANDLE isolation_release{nullptr};
    HANDLE campaign_job{nullptr};
    HANDLE guard_job{nullptr};
    bool job_cleanup_self_test{false};
    std::vector<std::filesystem::path> applications;
};

struct CampaignCleanupResult final {
    bool exact{false};
    bool termination_required{false};
    DWORD native_error{ERROR_SUCCESS};
    std::size_t active_process_count{0U};
};

[[nodiscard]] bool query_job_process_count(
    const HANDLE job,
    std::size_t& count,
    DWORD& error) noexcept
{
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    if (::QueryInformationJobObject(
            job, JobObjectBasicAccountingInformation, &accounting,
            sizeof(accounting), nullptr) == FALSE) {
        error = ::GetLastError();
        return false;
    }
    count = accounting.ActiveProcesses;
    error = ERROR_SUCCESS;
    return true;
}

[[nodiscard]] CampaignCleanupResult terminate_and_confirm_campaign_empty(
    const HANDLE campaign_job,
    const DWORD exit_code,
    const std::chrono::milliseconds timeout) noexcept
{
    CampaignCleanupResult result;
    if (campaign_job == nullptr || campaign_job == INVALID_HANDLE_VALUE ||
        timeout.count() < 0) {
        result.native_error = ERROR_INVALID_HANDLE;
        return result;
    }
    if (!query_job_process_count(
            campaign_job, result.active_process_count, result.native_error)) {
        return result;
    }
    if (result.active_process_count == 0U) {
        result.exact = true;
        return result;
    }
    result.termination_required = true;
    if (::TerminateJobObject(campaign_job, exit_code) == FALSE) {
        result.native_error = ::GetLastError();
        return result;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (!query_job_process_count(
                campaign_job, result.active_process_count,
                result.native_error)) {
            return result;
        }
        if (result.active_process_count == 0U) {
            result.exact = true;
            return result;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            result.native_error = ERROR_TIMEOUT;
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
}

[[nodiscard]] bool parse_handle(
    const std::wstring_view value,
    HANDLE& handle) noexcept
{
    std::uintptr_t parsed = 0U;
    if (value.empty()) return false;
    for (const wchar_t character : value) {
        if (character < L'0' || character > L'9' ||
            parsed > ((std::numeric_limits<std::uintptr_t>::max)() -
                      static_cast<unsigned int>(character - L'0')) / 10U) {
            return false;
        }
        parsed = parsed * 10U + static_cast<unsigned int>(character - L'0');
    }
    if (parsed == 0U) return false;
    handle = reinterpret_cast<HANDLE>(parsed);
    return true;
}

[[nodiscard]] std::optional<Options> parse_options(
    const int argc,
    wchar_t** argv)
{
    Options options;
    bool readiness = false;
    bool heartbeat = false;
    bool isolation_release = false;
    bool campaign_job = false;
    bool guard_job = false;
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view name{argv[index]};
        if (name == L"--job-cleanup-self-test" &&
            !options.job_cleanup_self_test) {
            options.job_cleanup_self_test = true;
            continue;
        }
        if (index + 1 >= argc) return std::nullopt;
        const std::wstring_view value{argv[++index]};
        if (name == L"--readiness-handle" && !readiness) {
            readiness = parse_handle(value, options.readiness);
            if (!readiness) return std::nullopt;
        } else if (name == L"--heartbeat-handle" && !heartbeat) {
            heartbeat = parse_handle(value, options.heartbeat);
            if (!heartbeat) return std::nullopt;
        } else if (name == L"--isolation-release-handle" &&
                   !isolation_release) {
            isolation_release = parse_handle(value, options.isolation_release);
            if (!isolation_release) return std::nullopt;
        } else if (name == L"--campaign-job-handle" && !campaign_job) {
            campaign_job = parse_handle(value, options.campaign_job);
            if (!campaign_job) return std::nullopt;
        } else if (name == L"--guard-job-handle" && !guard_job) {
            guard_job = parse_handle(value, options.guard_job);
            if (!guard_job) return std::nullopt;
        } else if (name == L"--application" &&
                   options.applications.size() <
                       windows::kMaximumIsolatedApplications &&
                   !value.empty()) {
            options.applications.emplace_back(value);
        } else {
            return std::nullopt;
        }
    }
    if (!readiness || !heartbeat || !isolation_release || !campaign_job ||
        !guard_job ||
        options.readiness == options.heartbeat ||
        options.readiness == options.isolation_release ||
        options.heartbeat == options.isolation_release ||
        options.campaign_job == options.guard_job ||
        options.campaign_job == options.readiness ||
        options.campaign_job == options.heartbeat ||
        options.campaign_job == options.isolation_release ||
        options.guard_job == options.readiness ||
        options.guard_job == options.heartbeat ||
        options.guard_job == options.isolation_release ||
        (options.job_cleanup_self_test
             ? !options.applications.empty()
             : options.applications.empty())) {
        return std::nullopt;
    }
    DWORD readiness_flags = 0U;
    DWORD heartbeat_flags = 0U;
    DWORD release_flags = 0U;
    DWORD campaign_job_flags = 0U;
    DWORD guard_job_flags = 0U;
    std::size_t campaign_process_count = 0U;
    std::size_t guard_process_count = 0U;
    DWORD job_error = ERROR_SUCCESS;
    if (::GetHandleInformation(options.readiness, &readiness_flags) == FALSE ||
        ::GetHandleInformation(options.heartbeat, &heartbeat_flags) == FALSE ||
        ::GetHandleInformation(options.isolation_release, &release_flags) == FALSE ||
        ::GetHandleInformation(options.campaign_job, &campaign_job_flags) == FALSE ||
        ::GetHandleInformation(options.guard_job, &guard_job_flags) == FALSE ||
        (readiness_flags & HANDLE_FLAG_INHERIT) == 0U ||
        (heartbeat_flags & HANDLE_FLAG_INHERIT) == 0U ||
        (release_flags & HANDLE_FLAG_INHERIT) == 0U ||
        (campaign_job_flags & HANDLE_FLAG_INHERIT) == 0U ||
        (guard_job_flags & HANDLE_FLAG_INHERIT) == 0U ||
        !query_job_process_count(
            options.campaign_job, campaign_process_count, job_error) ||
        !query_job_process_count(
            options.guard_job, guard_process_count, job_error) ||
        guard_process_count != 1U ||
        (!options.job_cleanup_self_test && campaign_process_count != 0U) ||
        ::WaitForSingleObject(options.isolation_release, 0U) != WAIT_TIMEOUT) {
        return std::nullopt;
    }
    return options.job_cleanup_self_test || !options.applications.empty()
        ? std::optional<Options>{std::move(options)} : std::nullopt;
}

[[nodiscard]] bool write_readiness(
    const HANDLE handle,
    const std::string_view value) noexcept
{
    if (value.size() > 1'024U) return false;
    DWORD written = 0U;
    return ::WriteFile(handle, value.data(), static_cast<DWORD>(value.size()),
                       &written, nullptr) != FALSE && written == value.size();
}

} // namespace

int wmain(const int argc, wchar_t** argv)
{
    const auto options = parse_options(argc, argv);
    if (!options) {
        std::cerr << "Usage: hlclient_stock_runtime_isolation_guard "
                     "--readiness-handle <inherited> --heartbeat-handle "
                     "<inherited-pipe-read> --isolation-release-handle "
                     "<inherited-event> --campaign-job-handle <inherited> "
                     "--guard-job-handle <inherited> --application "
                     "<canonical.exe> [...]\n";
        return 2;
    }
    windows::DynamicNetworkIsolationSession session;
    if (!options->job_cleanup_self_test) {
        windows::NetworkIsolationPolicy policy;
        for (const auto& executable : options->applications) {
            windows::WindowsBinaryIdentityErrorCode binary_error{};
            windows::NetworkIsolationErrorCode isolation_error{};
            auto application = windows::observe_network_isolation_application(
                executable, binary_error, isolation_error);
            if (!application) {
                std::cerr << "network-isolation=unavailable;failure-category="
                          << (isolation_error !=
                                      windows::NetworkIsolationErrorCode::none
                                  ? windows::to_string(isolation_error)
                                  : windows::to_string(binary_error))
                          << "\n";
                return 1;
            }
            policy.applications.push_back(std::move(*application));
        }
        auto [candidate, started] =
            windows::DynamicNetworkIsolationSession::start(policy);
        if (!started) {
            std::cerr << "network-isolation=unavailable;failure-category="
                      << windows::to_string(started.code)
                      << ";native-error=" << started.native_error << "\n";
            return 1;
        }
        session = std::move(candidate);
    }
    constexpr std::string_view production_readiness =
        "network-isolation=ready;session=dynamic;ipv4-loopback=allowed;"
        "ipv6-loopback=allowed;persistent-rules=0\n";
    constexpr std::string_view self_test_readiness =
        "job-cleanup-self-test=ready\n";
    const auto readiness = options->job_cleanup_self_test
        ? self_test_readiness : production_readiness;
    if (!write_readiness(options->readiness, readiness)) {
        return 1;
    }
    static_cast<void>(::CloseHandle(options->readiness));

    std::array<std::byte, 64U> heartbeat{};
    bool heartbeat_clean = true;
    for (;;) {
        DWORD count = 0U;
        const BOOL read = ::ReadFile(
            options->heartbeat, heartbeat.data(),
            static_cast<DWORD>(heartbeat.size()), &count, nullptr);
        const DWORD read_error = read != FALSE ? ERROR_SUCCESS : ::GetLastError();
        const auto disposition =
            windows::classify_network_isolation_heartbeat_read(
                read != FALSE, count, read_error);
        if (disposition ==
            windows::NetworkIsolationHeartbeatReadDisposition::error) {
            heartbeat_clean = false;
            break;
        }
        if (disposition ==
            windows::NetworkIsolationHeartbeatReadDisposition::clean_eof) {
            break;
        }
    }
    static_cast<void>(::CloseHandle(options->heartbeat));
    // Heartbeat closure is only evidence that the orchestrator disappeared.
    // Give the independent wrapper a bounded opportunity to prove the
    // campaign Job empty and explicitly signal normal release. If both owners
    // died, this guard retains both Job handles, terminates the campaign itself
    // and refuses to close WFP until ActiveProcesses is observed as zero.
    const DWORD release_wait = ::WaitForSingleObject(
        options->isolation_release,
        options->job_cleanup_self_test ? 100U : 15'000U);
    const DWORD release_error =
        release_wait == WAIT_FAILED ? ::GetLastError() : ERROR_SUCCESS;
    bool cleanup_failure_observed = false;
    bool campaign_termination_required = false;
    for (;;) {
        const auto cleanup = terminate_and_confirm_campaign_empty(
            options->campaign_job, 122U, std::chrono::seconds{10});
        campaign_termination_required =
            campaign_termination_required || cleanup.termination_required;
        if (cleanup.exact) break;
        cleanup_failure_observed = true;
        std::cerr << "campaign-cleanup=unconfirmed;native-error="
                  << cleanup.native_error
                  << ";active-processes=" << cleanup.active_process_count
                  << "\n";
        if (options->job_cleanup_self_test) return 3;
        // Fail closed: retaining this process retains the dynamic WFP session
        // and both Jobs. Never publish a guard exit while campaign zero is
        // unproven; keep retrying exact termination/accounting instead.
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }
    static_cast<void>(::CloseHandle(options->isolation_release));
    session.close();
    const bool clean_release = heartbeat_clean &&
        release_wait == WAIT_OBJECT_0 && !cleanup_failure_observed &&
        !campaign_termination_required;
    if (!clean_release && release_error != ERROR_SUCCESS) {
        std::cerr << "isolation-release=failed;native-error="
                  << release_error << "\n";
    }
    return clean_release ? 0 : 1;
}
