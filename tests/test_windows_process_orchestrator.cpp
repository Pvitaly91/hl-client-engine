#include <hlclient/platform/windows/binary_identity.hpp>
#include <hlclient/platform/windows/process_orchestrator.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace {

namespace windows = hlclient::platform::windows;

class TestHandle final {
public:
    explicit TestHandle(HANDLE value = nullptr) noexcept : value_{value} {}
    ~TestHandle()
    {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(::CloseHandle(value_));
        }
    }
    TestHandle(const TestHandle&) = delete;
    TestHandle& operator=(const TestHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] HANDLE release() noexcept
    {
        return std::exchange(value_, nullptr);
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE value_{nullptr};
};

class ScopedStandardOutputHandles final {
public:
    explicit ScopedStandardOutputHandles(const HANDLE replacement) noexcept
        : stdout_{::GetStdHandle(STD_OUTPUT_HANDLE)},
          stderr_{::GetStdHandle(STD_ERROR_HANDLE)}
    {
        if (::SetStdHandle(STD_OUTPUT_HANDLE, replacement) != FALSE &&
            ::SetStdHandle(STD_ERROR_HANDLE, replacement) != FALSE) {
            active_ = true;
            return;
        }
        static_cast<void>(::SetStdHandle(STD_OUTPUT_HANDLE, stdout_));
        static_cast<void>(::SetStdHandle(STD_ERROR_HANDLE, stderr_));
    }

    ~ScopedStandardOutputHandles()
    {
        if (active_) {
            static_cast<void>(::SetStdHandle(STD_OUTPUT_HANDLE, stdout_));
            static_cast<void>(::SetStdHandle(STD_ERROR_HANDLE, stderr_));
        }
    }

    ScopedStandardOutputHandles(const ScopedStandardOutputHandles&) = delete;
    ScopedStandardOutputHandles& operator=(
        const ScopedStandardOutputHandles&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return active_; }

private:
    HANDLE stdout_{INVALID_HANDLE_VALUE};
    HANDLE stderr_{INVALID_HANDLE_VALUE};
    bool active_{false};
};

[[nodiscard]] std::filesystem::path sibling(const wchar_t* name)
{
    std::wstring module(32'768U, L'\0');
    const DWORD size = ::GetModuleFileNameW(
        nullptr, module.data(), static_cast<DWORD>(module.size()));
    REQUIRE(size > 0U);
    REQUIRE(size < module.size());
    module.resize(size);
    return std::filesystem::path{module}.parent_path() / name;
}

[[nodiscard]] windows::WindowsBinaryIdentity observe_fake_server()
{
    const auto result = windows::observe_windows_binary_identity(
        sibling(L"hlclient_stock_runtime_fake_server.exe"),
        windows::kMaximumObservedExecutableBytes,
        {windows::AuthenticodePolicy::not_required_for_project_owned_binary,
         false});
    REQUIRE(result);
    return *result.identity;
}

[[nodiscard]] windows::WindowsBinaryIdentity observe_fake_client()
{
    const auto result = windows::observe_windows_binary_identity(
        sibling(L"hlclient_stock_runtime_fake_client.exe"),
        windows::kMaximumObservedExecutableBytes,
        {windows::AuthenticodePolicy::not_required_for_project_owned_binary,
         false});
    REQUIRE(result);
    return *result.identity;
}

[[nodiscard]] windows::WindowsBinaryIdentity observe_capture_relay()
{
    const auto result = windows::observe_windows_binary_identity(
        sibling(L"hlclient_stock_runtime_capture.exe"),
        windows::kMaximumObservedExecutableBytes,
        {windows::AuthenticodePolicy::not_required_for_project_owned_binary,
         false});
    REQUIRE(result);
    return *result.identity;
}

[[nodiscard]] windows::WindowsBinaryIdentity observe_active_orchestrator()
{
    const auto result = windows::observe_windows_binary_identity(
        sibling(L"hlclient_stock_runtime_orchestrator.exe"),
        windows::kMaximumObservedExecutableBytes,
        {windows::AuthenticodePolicy::not_required_for_project_owned_binary,
         false});
    REQUIRE(result);
    return *result.identity;
}

[[nodiscard]] windows::WindowsBinaryIdentity observe_isolation_guard()
{
    const auto result = windows::observe_windows_binary_identity(
        sibling(L"hlclient_stock_runtime_isolation_guard.exe"),
        windows::kMaximumObservedExecutableBytes,
        {windows::AuthenticodePolicy::not_required_for_project_owned_binary,
         false});
    REQUIRE(result);
    return *result.identity;
}

[[nodiscard]] std::wstring handle_decimal(const HANDLE handle)
{
    return std::to_wstring(reinterpret_cast<std::uintptr_t>(handle));
}

[[nodiscard]] std::uint16_t reserve_then_release_loopback_port()
{
    WSADATA data{};
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    const SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(socket != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.S_un.S_addr = htonl(0x7f000001U);
    address.sin_port = 0U;
    REQUIRE(::bind(socket, reinterpret_cast<const sockaddr*>(&address),
                   sizeof(address)) != SOCKET_ERROR);
    int size = sizeof(address);
    REQUIRE(::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) !=
            SOCKET_ERROR);
    const auto port = ntohs(address.sin_port);
    static_cast<void>(::closesocket(socket));
    static_cast<void>(::WSACleanup());
    return port;
}

[[nodiscard]] windows::OwnedProcessLaunchSpec fake_server_spec(
    const windows::WindowsBinaryIdentity& identity,
    const std::uint16_t port,
    const std::uint32_t duration_ms,
    windows::BoundedProcessLogCapture* log = nullptr,
    const std::uint32_t emit_bytes = 0U,
    const bool suppress_ready = false,
    const std::uint32_t ready_delay_ms = 0U,
    const std::string_view profile_variant = {},
    const std::uint32_t profile_chunk_delay_ms = 25U)
{
    windows::OwnedProcessLaunchSpec spec;
    spec.executable = identity.canonical_path;
    spec.working_directory = identity.canonical_path.parent_path();
    spec.expected_identity = identity;
    spec.arguments = {
        L"--port", std::to_wstring(port),
        L"--duration-ms", std::to_wstring(duration_ms),
        L"--emit-bytes", std::to_wstring(emit_bytes),
        L"--ready-delay-ms", std::to_wstring(ready_delay_ms)};
    if (suppress_ready) spec.arguments.push_back(L"--suppress-ready");
    if (!profile_variant.empty()) {
        spec.arguments.push_back(L"--hlds-profile-variant");
        spec.arguments.emplace_back(
            profile_variant.begin(), profile_variant.end());
        spec.arguments.push_back(L"--profile-chunk-delay-ms");
        spec.arguments.push_back(std::to_wstring(profile_chunk_delay_ms));
    }
    if (log != nullptr) {
        spec.stdout_handle = log->inherited_write_handle();
        spec.stderr_handle = log->inherited_write_handle();
    }
    return spec;
}

[[nodiscard]] windows::OwnedProcessLaunchSpec fake_client_spec(
    const windows::WindowsBinaryIdentity& identity,
    const std::uint16_t port,
    const std::uint32_t timeout_ms,
    windows::BoundedProcessLogCapture* log = nullptr)
{
    windows::OwnedProcessLaunchSpec spec;
    spec.executable = identity.canonical_path;
    spec.working_directory = identity.canonical_path.parent_path();
    spec.expected_identity = identity;
    spec.arguments = {
        L"--port", std::to_wstring(port),
        L"--timeout-ms", std::to_wstring(timeout_ms),
        L"--exit-code", L"0"};
    if (log != nullptr) {
        spec.stdout_handle = log->inherited_write_handle();
        spec.stderr_handle = log->inherited_write_handle();
    }
    return spec;
}

[[nodiscard]] bool wait_for_log_marker(
    windows::BoundedProcessLogCapture& log,
    windows::OwnedProcess& process,
    const std::string_view marker,
    const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline && process.running()) {
        const auto snapshot = log.snapshot();
        if (!windows::bounded_process_log_snapshot_complete(snapshot)) return false;
        if (snapshot.bytes.find(marker) != std::string::npos) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return log.snapshot().bytes.find(marker) != std::string::npos;
}

void advance_to_relay_ready(windows::StockRuntimeStartupState& state)
{
    using Event = windows::StockRuntimeStartupEvent;
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::guard_started));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::guard_readiness_observed));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::active_canary_succeeded));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::run_root_created));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::relay_started));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::relay_readiness_observed));
}

} // namespace

TEST_CASE("Windows command-line quoting follows CreateProcess argv rules",
          "[platform][windows][stock-runtime][orchestrator][command-line]")
{
    CHECK(windows::quote_windows_command_line_argument(L"plain") == L"plain");
    CHECK(windows::quote_windows_command_line_argument(L"") == L"\"\"");
    CHECK(windows::quote_windows_command_line_argument(L"two words") ==
          L"\"two words\"");
    CHECK(windows::quote_windows_command_line_argument(L"a\\\"b") ==
          L"\"a\\\\\\\"b\"");
    CHECK(windows::quote_windows_command_line_argument(L"ends \\") ==
          L"\"ends \\\\\"" );
}

TEST_CASE("Owned children without log handles cannot leak status to wrapper stdout",
          "[platform][windows][stock-runtime][orchestrator][stdout-containment]")
{
    const auto server = observe_fake_server();
    auto leaked_output = windows::BoundedProcessLogCapture::create({});
    REQUIRE(leaked_output);
    auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(created);

    {
        ScopedStandardOutputHandles wrapper_output{
            static_cast<HANDLE>(leaked_output->inherited_write_handle())};
        REQUIRE(wrapper_output);
        auto [child, launched] = job.launch(fake_server_spec(
            server, reserve_then_release_loopback_port(), 25U));
        REQUIRE(launched);
        const auto exit_code = child.wait(std::chrono::seconds{2});
        REQUIRE(exit_code);
        CHECK(*exit_code == 0U);
    }

    leaked_output->close_parent_write_handle();
    const auto captured = leaked_output->finish();
    REQUIRE(windows::bounded_process_log_snapshot_complete(captured));
    CHECK(captured.bytes.empty());
    CHECK(job.active_process_count() == 0U);
}

TEST_CASE("Bounded log read errors are clean only for EOF or local cancellation",
          "[platform][windows][stock-runtime][orchestrator][logs][read-error]")
{
    using Disposition = windows::BoundedProcessLogReadDisposition;
    CHECK(windows::classify_bounded_process_log_read(
              true, 1U, ERROR_SUCCESS, false) == Disposition::data);
    CHECK(windows::classify_bounded_process_log_read(
              true, 0U, ERROR_SUCCESS, false) == Disposition::clean_eof);
    CHECK(windows::classify_bounded_process_log_read(
              false, 0U, ERROR_BROKEN_PIPE, false) == Disposition::clean_eof);
    CHECK(windows::classify_bounded_process_log_read(
              false, 0U, ERROR_OPERATION_ABORTED, true) ==
          Disposition::clean_eof);
    CHECK(windows::classify_bounded_process_log_read(
              false, 0U, ERROR_OPERATION_ABORTED, false) ==
          Disposition::error);
    CHECK(windows::classify_bounded_process_log_read(
              false, 0U, ERROR_INVALID_HANDLE, true) == Disposition::error);
}

TEST_CASE("Raw stock capture rejects a non-orchestrator parent before side effects",
          "[platform][windows][stock-runtime][orchestrator][capture-capability]")
{
    const auto capture = observe_capture_relay();
    auto log = windows::BoundedProcessLogCapture::create({});
    REQUIRE(log);
    auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(created);

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    TestHandle stop{::CreateEventW(&security, TRUE, FALSE, nullptr)};
    TestHandle capability{::CreateEventW(&security, TRUE, FALSE, nullptr)};
    REQUIRE(stop);
    REQUIRE(capability);

    const auto output = std::filesystem::temp_directory_path() /
        (L"hlclient-stock-runtime-capability-reject-" +
         std::to_wstring(::GetCurrentProcessId()) + L"-" +
         std::to_wstring(::GetTickCount64()));
    REQUIRE_FALSE(std::filesystem::exists(output));

    windows::OwnedProcessLaunchSpec spec;
    spec.executable = capture.canonical_path;
    spec.working_directory = capture.canonical_path.parent_path();
    spec.expected_identity = capture;
    spec.stdout_handle = log->inherited_write_handle();
    spec.stderr_handle = log->inherited_write_handle();
    spec.additional_inherited_handles = {stop.get(), capability.get()};
    spec.arguments = {
        L"--listen-port", L"27140", L"--server-port", L"27141",
        L"--output-run-root", output.wstring(), L"--scenario", L"baseline",
        L"--output-role", L"normal-campaign-run",
        L"--private-ipv4-loopback-only", L"--one-upstream-socket",
        L"--byte-preserving", L"--no-payload-rewrite",
        L"--precreated-empty-run-root", L"--stop-handle",
        handle_decimal(stop.get()), L"--orchestrator-capability-handle",
        handle_decimal(capability.get()), L"--orchestrator-process-id",
        std::to_wstring(::GetCurrentProcessId()),
    };
    auto [child, launched] = job.launch(spec);
    REQUIRE(launched);
    log->close_parent_write_handle();
    const auto exit = child.wait(std::chrono::seconds{5});
    REQUIRE(exit);
    CHECK(*exit == 18U);
    const auto captured = log->finish();
    CHECK(windows::bounded_process_log_snapshot_complete(captured));
    CHECK(captured.bytes.find(
              "[stock-runtime-capture] result="
              "orchestrator-capability-required") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(output));
    CHECK(job.active_process_count() == 0U);
}

TEST_CASE("UDP relay rejects the server profile diagnostic output role",
          "[platform][windows][stock-runtime][orchestrator][capture]"
          "[server-profile-diagnostic][output-role]")
{
    const auto capture = observe_capture_relay();
    auto log = windows::BoundedProcessLogCapture::create({});
    REQUIRE(log);
    auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(created);
    const auto output = std::filesystem::temp_directory_path() /
        (L"hlclient-stock-profile-relay-reject-" +
         std::to_wstring(::GetCurrentProcessId()) + L"-" +
         std::to_wstring(::GetTickCount64()));
    REQUIRE_FALSE(std::filesystem::exists(output));

    windows::OwnedProcessLaunchSpec spec;
    spec.executable = capture.canonical_path;
    spec.working_directory = capture.canonical_path.parent_path();
    spec.expected_identity = capture;
    spec.stdout_handle = log->inherited_write_handle();
    spec.stderr_handle = log->inherited_write_handle();
    spec.arguments = {
        L"--listen-port", L"27140", L"--server-port", L"27141",
        L"--output-run-root", output.wstring(), L"--scenario", L"baseline",
        L"--output-role", L"server-profile-diagnostic",
        L"--private-ipv4-loopback-only", L"--one-upstream-socket",
        L"--byte-preserving", L"--no-payload-rewrite",
        L"--precreated-empty-run-root"};
    auto [child, launched] = job.launch(spec);
    REQUIRE(launched);
    log->close_parent_write_handle();
    REQUIRE(child.wait(std::chrono::seconds{5}) == 2U);
    const auto captured = log->finish();
    REQUIRE(windows::bounded_process_log_snapshot_complete(captured));
    CHECK(captured.bytes.find("Usage: hlclient_stock_runtime_capture") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(output));
    CHECK(job.active_process_count() == 0U);
}

TEST_CASE("Active orchestrator rejects direct invocation before side effects",
          "[platform][windows][stock-runtime][orchestrator]"
          "[wrapper-capability]")
{
    const auto orchestrator = observe_active_orchestrator();
    auto log = windows::BoundedProcessLogCapture::create({});
    REQUIRE(log);
    auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(created);
    const auto output = std::filesystem::temp_directory_path() /
        (L"hlclient-stock-runtime-direct-orchestrator-reject-" +
         std::to_wstring(::GetCurrentProcessId()) + L"-" +
         std::to_wstring(::GetTickCount64()));
    REQUIRE_FALSE(std::filesystem::exists(output));

    windows::OwnedProcessLaunchSpec spec;
    spec.executable = orchestrator.canonical_path;
    spec.working_directory = orchestrator.canonical_path.parent_path();
    spec.expected_identity = orchestrator;
    spec.stdout_handle = log->inherited_write_handle();
    spec.stderr_handle = log->inherited_write_handle();
    spec.arguments = {
        L"--confirmation-token", L"HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1",
        L"--output-role", L"normal-campaign-run",
        L"--run-root", output.wstring(),
        L"--research-root", L"Z:\\hlclient-absent-research",
        L"--client", L"Z:\\hlclient-absent-research\\hl.exe",
        L"--server", L"Z:\\hlclient-absent-research\\hlds.exe",
        L"--relay", L"Z:\\hlclient-absent-relay.exe",
        L"--isolation-guard", L"Z:\\hlclient-absent-guard.exe",
        L"--app-manifest", L"Z:\\appmanifest_70.acf",
        L"--game", L"valve", L"--map", L"boot_camp",
        L"--scenario", L"baseline", L"--relay-port", L"27140",
        L"--server-port", L"27141"};
    auto [child, launched] = job.launch(spec);
    REQUIRE(launched);
    log->close_parent_write_handle();
    REQUIRE(child.wait(std::chrono::seconds{5}) == 3U);
    const auto captured = log->finish();
    REQUIRE(windows::bounded_process_log_snapshot_complete(captured));
    CHECK(captured.bytes.find(
              "[stock-runtime-orchestrator] failure-category="
              "wrapper_transaction_capability_required") != std::string::npos);
    CHECK(captured.bytes.find(
              "[stock-runtime-orchestrator] processes-started=0") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(output));
    CHECK(job.active_process_count() == 0U);
}

TEST_CASE("Wrapper cleanup capability attests exact exit independently of stdout",
          "[platform][windows][stock-runtime][orchestrator]"
          "[wrapper-capability][cleanup-capability]")
{
    const auto orchestrator = observe_active_orchestrator();
    auto log = windows::BoundedProcessLogCapture::create({});
    REQUIRE(log);
    auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(created);

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    TestHandle startup_capability{
        ::CreateEventW(&security, TRUE, FALSE, nullptr)};
    TestHandle cleanup_capability{
        ::CreateEventW(&security, TRUE, FALSE, nullptr)};
    TestHandle wrapper_job{::CreateJobObjectW(&security, nullptr)};
    TestHandle wrapper_guard_job{::CreateJobObjectW(&security, nullptr)};
    TestHandle isolation_release{
        ::CreateEventW(&security, TRUE, FALSE, nullptr)};
    REQUIRE(startup_capability);
    REQUIRE(cleanup_capability);
    REQUIRE(wrapper_job);
    REQUIRE(wrapper_guard_job);
    REQUIRE(isolation_release);

    const auto output = std::filesystem::temp_directory_path() /
        (L"hlclient-stock-runtime-wrapper-cleanup-" +
         std::to_wstring(::GetCurrentProcessId()) + L"-" +
         std::to_wstring(::GetTickCount64()));
    REQUIRE_FALSE(std::filesystem::exists(output));

    windows::OwnedProcessLaunchSpec spec;
    spec.executable = orchestrator.canonical_path;
    spec.working_directory = orchestrator.canonical_path.parent_path();
    spec.expected_identity = orchestrator;
    spec.stdout_handle = log->inherited_write_handle();
    spec.stderr_handle = log->inherited_write_handle();
    spec.additional_inherited_handles = {
        startup_capability.get(), cleanup_capability.get(), wrapper_job.get(),
        wrapper_guard_job.get(), isolation_release.get()};
    spec.arguments = {
        L"--confirmation-token", L"HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1",
        L"--output-role", L"normal-campaign-run",
        L"--wrapper-capability-handle", handle_decimal(startup_capability.get()),
        L"--wrapper-cleanup-capability-handle",
        handle_decimal(cleanup_capability.get()),
        L"--wrapper-job-handle", handle_decimal(wrapper_job.get()),
        L"--wrapper-guard-job-handle",
        handle_decimal(wrapper_guard_job.get()),
        L"--isolation-release-handle", handle_decimal(isolation_release.get()),
        L"--wrapper-process-id", std::to_wstring(::GetCurrentProcessId()),
        L"--run-root", output.wstring(),
        L"--research-root", L"Z:\\hlclient-absent-research",
        L"--client", L"Z:\\hlclient-absent-research\\hl.exe",
        L"--server", L"Z:\\hlclient-absent-research\\hlds.exe",
        L"--relay", L"Z:\\hlclient-absent-relay.exe",
        L"--isolation-guard", L"Z:\\hlclient-absent-guard.exe",
        L"--app-manifest", L"Z:\\appmanifest_70.acf",
        L"--game", L"valve", L"--map", L"boot_camp",
        L"--scenario", L"baseline", L"--relay-port", L"27140",
        L"--server-port", L"27141"};
    auto [child, launched] = job.launch(spec);
    REQUIRE(launched);
    log->close_parent_write_handle();
    REQUIRE(child.wait(std::chrono::seconds{5}) == 1U);
    CHECK(::WaitForSingleObject(startup_capability.get(), 0U) == WAIT_OBJECT_0);
    CHECK(::WaitForSingleObject(cleanup_capability.get(), 0U) == WAIT_OBJECT_0);
    CHECK(::WaitForSingleObject(isolation_release.get(), 0U) == WAIT_OBJECT_0);
    const auto captured = log->finish();
    REQUIRE(windows::bounded_process_log_snapshot_complete(captured));
    CHECK(captured.bytes.find(
              "[stock-runtime-orchestrator] job-cleanup=exact") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(output));
    CHECK(job.active_process_count() == 0U);
}

TEST_CASE("Wrapper-owned Job remains queryable after adopted campaign cleanup",
          "[platform][windows][stock-runtime][orchestrator][job]"
          "[wrapper-job][cleanup]")
{
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    TestHandle wrapper_job{::CreateJobObjectW(&security, nullptr)};
    REQUIRE(wrapper_job);
    HANDLE inherited_copy = nullptr;
    REQUIRE(::DuplicateHandle(
        ::GetCurrentProcess(), wrapper_job.get(), ::GetCurrentProcess(),
        &inherited_copy, 0U, TRUE, DUPLICATE_SAME_ACCESS));
    TestHandle inherited_job{inherited_copy};

    auto [job, adopted] = windows::KillOnCloseProcessJob::adopt_inherited(
        inherited_job.release(), 1U);
    REQUIRE(adopted);
    const auto identity = observe_fake_server();
    const auto port = reserve_then_release_loopback_port();
    auto [child, launched] = job.launch(
        fake_server_spec(identity, port, 30'000U));
    REQUIRE(launched);
    REQUIRE(child.running());

    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    REQUIRE(::QueryInformationJobObject(
        wrapper_job.get(), JobObjectBasicAccountingInformation,
        &accounting, sizeof(accounting), nullptr));
    CHECK(accounting.ActiveProcesses == 1U);
    const auto cleanup =
        job.terminate_and_wait(120U, std::chrono::seconds{5});
    REQUIRE(cleanup);
    REQUIRE(child.wait(std::chrono::seconds{2}) == 120U);
    job.close();
    REQUIRE(::QueryInformationJobObject(
        wrapper_job.get(), JobObjectBasicAccountingInformation,
        &accounting, sizeof(accounting), nullptr));
    CHECK(accounting.ActiveProcesses == 0U);
}

TEST_CASE("Wrapper-owned Job proves zero after forced orchestrator-handle loss",
          "[platform][windows][stock-runtime][orchestrator][job]"
          "[wrapper-job][forced-cleanup]")
{
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    TestHandle wrapper_job{::CreateJobObjectW(&security, nullptr)};
    REQUIRE(wrapper_job);
    HANDLE inherited_copy = nullptr;
    REQUIRE(::DuplicateHandle(
        ::GetCurrentProcess(), wrapper_job.get(), ::GetCurrentProcess(),
        &inherited_copy, 0U, TRUE, DUPLICATE_SAME_ACCESS));
    TestHandle inherited_job{inherited_copy};
    auto [job, adopted] = windows::KillOnCloseProcessJob::adopt_inherited(
        inherited_job.release(), 1U);
    REQUIRE(adopted);
    const auto identity = observe_fake_server();
    const auto port = reserve_then_release_loopback_port();
    auto [child, launched] = job.launch(
        fake_server_spec(identity, port, 30'000U));
    REQUIRE(launched);
    REQUIRE(child.running());

    // Simulate loss of the orchestrator's inherited Job handle. The wrapper's
    // retained handle prevents ambiguous PID-based cleanup and remains exact.
    job.close();
    REQUIRE(child.running());
    REQUIRE(::TerminateJobObject(wrapper_job.get(), 121U));
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds{5};
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    do {
        REQUIRE(::QueryInformationJobObject(
            wrapper_job.get(), JobObjectBasicAccountingInformation,
            &accounting, sizeof(accounting), nullptr));
        if (accounting.ActiveProcesses == 0U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    } while (std::chrono::steady_clock::now() < deadline);
    CHECK(accounting.ActiveProcesses == 0U);
    REQUIRE(child.wait(std::chrono::seconds{2}) == 121U);
}

TEST_CASE("HLDS banner parser requires every exact stock profile field",
          "[platform][windows][stock-runtime][orchestrator][banner]")
{
    constexpr std::string_view valid =
        "Protocol version 48\r\n"
        "Exe version 1.1.2.2/Stdio (valve)\r\n"
        "Exe build: 00:00:00 Jan 1 2026 (10210)\r\n"
        "Server IP address 127.0.0.1:27141\r\n"
        "map     : boot_camp at: 0 x, 0 y, 0 z\r\n";
    constexpr std::string_view without_map =
        "Protocol version 48\r\n"
        "Exe version 1.1.2.2/Stdio (valve)\r\n"
        "Exe build: 00:00:00 Jan 1 2026 (10210)\r\n"
        "Server IP address 127.0.0.1:27141\r\n";
    const auto parsed = windows::parse_required_hlds_runtime_banner(
        valid, "boot_camp", 27'141U);
    REQUIRE(parsed);
    CHECK(parsed.profile->protocol == 48U);
    CHECK(parsed.profile->build == 10'210U);
    CHECK(parsed.profile->engine_version ==
          windows::WindowsFileVersion{1U, 1U, 2U, 2U});
    CHECK(parsed.profile->game == "valve");
    CHECK(parsed.profile->map == "boot_camp");
    CHECK(parsed.profile->port == 27'141U);
    CHECK(parsed.profile->ready);

    CHECK(windows::parse_required_hlds_runtime_banner(
              std::string{valid} + "Protocol version 48\n",
              "boot_camp", 27'141U).code ==
          windows::HldsBannerParseErrorCode::duplicate_field);
    CHECK(windows::parse_required_hlds_runtime_banner(
              valid, "crossfire", 27'141U).code ==
          windows::HldsBannerParseErrorCode::profile_mismatch);
    CHECK(windows::parse_required_hlds_runtime_banner(
              valid, "boot_camp", 27'142U).code ==
          windows::HldsBannerParseErrorCode::profile_mismatch);
    CHECK(windows::parse_required_hlds_runtime_banner(
              without_map, "boot_camp", 27'141U).code ==
          windows::HldsBannerParseErrorCode::missing_field);
    CHECK(windows::parse_required_hlds_runtime_banner(
              std::string{valid} +
                  "map     : boot_camp at: 0 x, 0 y, 0 z\n",
              "boot_camp", 27'141U).code ==
          windows::HldsBannerParseErrorCode::duplicate_field);
    CHECK(windows::parse_required_hlds_runtime_banner(
              "Protocol version 48\n"
              "Exe version 1.1.2.2/Stdio (valve)\n"
              "Exe build: 00:00:00 Jan 1 2026 (10210)\n"
              "Server IP address 127.0.0.1:27141\n"
              "map     : crossfire at: 0 x, 0 y, 0 z\n",
              "boot_camp", 27'141U).code ==
          windows::HldsBannerParseErrorCode::profile_mismatch);
}

TEST_CASE("HLDS profile diagnostics identify one bounded mismatch field",
          "[platform][windows][stock-runtime][orchestrator][banner]"
          "[server-profile-diagnostic]")
{
    const auto banner = [](
        const std::string_view engine = "1.1.2.2",
        const std::string_view mode = "Stdio",
        const std::string_view game = "valve",
        const std::string_view protocol = "48",
        const std::string_view build = "10210",
        const std::string_view address = "127.0.0.1",
        const std::string_view port = "27141",
        const std::string_view map = "boot_camp",
        const std::string_view newline = "\r\n") {
        return std::string{"Protocol version "} + std::string{protocol} +
            std::string{newline} + "Exe version " + std::string{engine} +
            "/" + std::string{mode} + " (" + std::string{game} + ")" +
            std::string{newline} +
            "Exe build: 00:00:00 Jan 1 2026 (" + std::string{build} + ")" +
            std::string{newline} + "Server IP address " +
            std::string{address} + ":" + std::string{port} +
            std::string{newline} + "map     : " + std::string{map} +
            " at: 0 x, 0 y, 0 z" + std::string{newline};
    };
    using Field = windows::HldsRuntimeProfileField;
    using ParseStatus = windows::HldsRuntimeProfileParseStatus;
    using Status = windows::HldsRuntimeProfileFieldStatus;

    SECTION("exact CRLF and LF profiles remain equivalent") {
        const auto crlf = windows::parse_required_hlds_runtime_banner(
            banner(), "boot_camp", 27'141U);
        const auto lf = windows::parse_required_hlds_runtime_banner(
            banner("1.1.2.2", "Stdio", "valve", "48", "10210",
                   "127.0.0.1", "27141", "boot_camp", "\n"),
            "boot_camp", 27'141U);
        REQUIRE(crlf);
        REQUIRE(lf);
        CHECK(crlf.diagnostic.parse_status == ParseStatus::valid);
        CHECK(crlf.diagnostic.mismatch_field == Field::none);
        CHECK(crlf.diagnostic.engine_version.status == Status::match);
        CHECK(crlf.diagnostic.runtime_mode.status == Status::match);
        CHECK(crlf.diagnostic.game.status == Status::match);
        CHECK(crlf.diagnostic.protocol.status == Status::match);
        CHECK(crlf.diagnostic.build.status == Status::match);
        CHECK(crlf.diagnostic.endpoint_address.status == Status::match);
        CHECK(crlf.diagnostic.endpoint_port.status == Status::match);
        CHECK(crlf.diagnostic.map.status == Status::match);
    }

    const auto require_mismatch = [&](
        const std::string& value,
        const Field field) -> windows::HldsBannerParseResult {
        auto result = windows::parse_required_hlds_runtime_banner(
            value, "boot_camp", 27'141U);
        REQUIRE_FALSE(result);
        REQUIRE(result.code ==
                windows::HldsBannerParseErrorCode::profile_mismatch);
        REQUIRE(result.diagnostic.parse_status ==
                ParseStatus::profile_mismatch);
        REQUIRE(result.diagnostic.mismatch_field == field);
        return result;
    };

    SECTION("each public expected field is distinguished") {
        const auto engine = require_mismatch(
            banner("1.1.2.3"), Field::engine_version);
        REQUIRE(engine.diagnostic.observed_engine_version);
        CHECK(*engine.diagnostic.observed_engine_version ==
              windows::WindowsFileVersion{1U, 1U, 2U, 3U});
        CHECK(engine.diagnostic.protocol.status == Status::match);

        const auto mode = require_mismatch(
            banner("1.1.2.2", "Steam"), Field::runtime_mode);
        CHECK(mode.diagnostic.engine_version.status == Status::match);
        CHECK(mode.diagnostic.runtime_mode.status == Status::mismatch);

        const auto game = require_mismatch(
            banner("1.1.2.2", "Stdio", "gearbox"), Field::game);
        CHECK_FALSE(game.diagnostic.game_matches);

        const auto protocol = require_mismatch(
            banner("1.1.2.2", "Stdio", "valve", "47"),
            Field::protocol);
        CHECK(protocol.diagnostic.observed_protocol == 47U);

        const auto build = require_mismatch(
            banner("1.1.2.2", "Stdio", "valve", "48", "10211"),
            Field::build);
        CHECK(build.diagnostic.observed_build == 10'211U);

        const auto address = require_mismatch(
            banner("1.1.2.2", "Stdio", "valve", "48", "10210",
                   "127.0.0.2"),
            Field::endpoint_address);
        CHECK(address.diagnostic.endpoint_address_category ==
              windows::HldsRuntimeEndpointAddressCategory::non_loopback);

        const auto port = require_mismatch(
            banner("1.1.2.2", "Stdio", "valve", "48", "10210",
                   "127.0.0.1", "27142"),
            Field::endpoint_port);
        CHECK_FALSE(port.diagnostic.endpoint_port_matches);

        const auto map = require_mismatch(
            banner("1.1.2.2", "Stdio", "valve", "48", "10210",
                   "127.0.0.1", "27141", "crossfire"),
            Field::map);
        CHECK_FALSE(map.diagnostic.map_matches);
    }

    SECTION("missing duplicate malformed and partial states stay typed") {
        auto missing_text = banner();
        missing_text.erase(missing_text.find("map     : "));
        const auto missing = windows::parse_required_hlds_runtime_banner(
            missing_text, "boot_camp", 27'141U);
        CHECK(missing.code ==
              windows::HldsBannerParseErrorCode::missing_field);
        CHECK(missing.diagnostic.parse_status == ParseStatus::incomplete);
        CHECK(missing.diagnostic.mismatch_field == Field::missing_field);
        CHECK(missing.diagnostic.map.status == Status::absent);

        const auto duplicate = windows::parse_required_hlds_runtime_banner(
            banner() + "Protocol version 48\n",
            "boot_camp", 27'141U);
        CHECK(duplicate.code ==
              windows::HldsBannerParseErrorCode::duplicate_field);
        CHECK(duplicate.diagnostic.mismatch_field == Field::duplicate_field);
        CHECK(duplicate.diagnostic.duplicate_field_count == 1U);

        const auto malformed = windows::parse_required_hlds_runtime_banner(
            banner("1.1.2"), "boot_camp", 27'141U);
        CHECK(malformed.code ==
              windows::HldsBannerParseErrorCode::malformed);
        CHECK(malformed.diagnostic.mismatch_field == Field::malformed_field);
        CHECK(malformed.diagnostic.engine_version.status == Status::malformed);
        CHECK_FALSE(malformed.diagnostic.observed_engine_version);

        auto partial = banner();
        partial.pop_back();
        const auto partial_result =
            windows::parse_required_hlds_runtime_banner(
                partial, "boot_camp", 27'141U);
        CHECK(partial_result.code ==
              windows::HldsBannerParseErrorCode::missing_field);
        CHECK(partial_result.diagnostic.map.status == Status::absent);

        auto completed_mismatch = banner("1.1.2.3");
        completed_mismatch += "unrelated bounded output\n";
        CHECK(require_mismatch(completed_mismatch, Field::engine_version)
                  .diagnostic.observed_line_count == 6U);
    }

    SECTION("source lifetime and process-log bounds cannot leak raw lines") {
        std::string transient = banner("1.1.2.3");
        auto result = windows::parse_required_hlds_runtime_banner(
            transient, "boot_camp", 27'141U);
        transient.assign(transient.size(), 'X');
        REQUIRE(result.diagnostic.observed_engine_version);
        CHECK(*result.diagnostic.observed_engine_version ==
              windows::WindowsFileVersion{1U, 1U, 2U, 3U});
        CHECK(result.diagnostic.observed_byte_count == transient.size());

        windows::BoundedProcessLogSnapshot truncated;
        truncated.bytes = banner();
        truncated.observed_bytes = truncated.bytes.size() + 1U;
        truncated.observed_line_count = 5U;
        truncated.byte_truncated = true;
        const auto bounded = windows::diagnose_required_hlds_runtime_banner(
            truncated, "boot_camp", 27'141U);
        CHECK(bounded.code ==
              windows::HldsBannerParseErrorCode::process_log_truncated);
        CHECK(bounded.diagnostic.mismatch_field ==
              Field::process_log_truncated);
        CHECK(bounded.diagnostic.process_log_truncated);

        const std::string long_line(4'097U, 'X');
        CHECK(windows::parse_required_hlds_runtime_banner(
                  long_line + "\n", "boot_camp", 27'141U).code ==
              windows::HldsBannerParseErrorCode::line_too_long);
        const std::string too_large(1U * 1'024U * 1'024U + 1U, 'X');
        CHECK(windows::parse_required_hlds_runtime_banner(
                  too_large, "boot_camp", 27'141U).code ==
              windows::HldsBannerParseErrorCode::too_large);
    }
}

TEST_CASE("Fake HLDS profile variants stay server-only and bounded",
          "[platform][windows][stock-runtime][orchestrator]"
          "[server-profile-diagnostic][fake-integration]")
{
    using Field = windows::HldsRuntimeProfileField;
    using ParseStatus = windows::HldsRuntimeProfileParseStatus;
    struct Fixture final {
        std::string_view variant;
        Field field;
    };
    constexpr std::array<Fixture, 8U> mismatches{{
        {"engine-version-mismatch", Field::engine_version},
        {"runtime-mode-mismatch", Field::runtime_mode},
        {"game-mismatch", Field::game},
        {"protocol-mismatch", Field::protocol},
        {"build-mismatch", Field::build},
        {"endpoint-address-mismatch", Field::endpoint_address},
        {"endpoint-port-mismatch", Field::endpoint_port},
        {"map-mismatch", Field::map},
    }};
    const auto server_identity = observe_fake_server();
    const auto client_identity = observe_fake_client();
    const auto require_no_client = [&]() {
        const auto clients =
            windows::find_processes_with_exact_image_identity(client_identity);
        REQUIRE(clients);
        CHECK(clients.process_ids.empty());
    };
    const auto run_variant = [&server_identity, &require_no_client](
        const std::string_view variant,
        const windows::BoundedProcessLogLimits limits = {},
        const std::uint32_t ready_delay_ms = 0U,
        bool* incomplete_observed = nullptr) {
        auto log = windows::BoundedProcessLogCapture::create(limits);
        REQUIRE(log);
        auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
        REQUIRE(created);
        const auto port = reserve_then_release_loopback_port();
        auto [server, launched] = job.launch(fake_server_spec(
            server_identity, port, 5'000U, &*log, 0U, false,
            ready_delay_ms, variant, 75U));
        REQUIRE(launched);
        log->close_parent_write_handle();
        windows::HldsBannerParseResult result;
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds{3};
        do {
            result = windows::diagnose_required_hlds_runtime_banner(
                log->snapshot(), "boot_camp", port);
            if (incomplete_observed != nullptr &&
                result.diagnostic.parse_status == ParseStatus::incomplete) {
                *incomplete_observed = true;
            }
            if (result || result.diagnostic.parse_status !=
                              ParseStatus::incomplete) {
                break;
            }
            if (!server.running()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        } while (std::chrono::steady_clock::now() < deadline);
        require_no_client();
        const auto cleanup =
            job.terminate_and_wait(120U, std::chrono::seconds{5});
        REQUIRE(cleanup);
        static_cast<void>(log->finish());
        require_no_client();
        return result;
    };

    for (const auto& fixture : mismatches) {
        INFO("variant=" << fixture.variant);
        const auto result = run_variant(fixture.variant);
        CHECK(result.diagnostic.parse_status == ParseStatus::profile_mismatch);
        CHECK(result.diagnostic.mismatch_field == fixture.field);
    }

    SECTION("valid, chunked and late profiles complete without early mismatch") {
        CHECK(run_variant("valid"));
        bool partial_incomplete = false;
        CHECK(run_variant("partial-chunks", {}, 0U, &partial_incomplete));
        CHECK(partial_incomplete);
        bool late_incomplete = false;
        CHECK(run_variant("late-completion", {}, 100U, &late_incomplete));
        CHECK(late_incomplete);
    }

    SECTION("duplicate, early exit, timeout and truncation remain typed") {
        CHECK(run_variant("duplicate-field").diagnostic.parse_status ==
              ParseStatus::duplicate_field);
        const auto early = run_variant("exit-before-profile");
        CHECK(early.diagnostic.parse_status == ParseStatus::incomplete);
        CHECK(early.diagnostic.mismatch_field == Field::missing_field);

        auto timeout_log = windows::BoundedProcessLogCapture::create({});
        REQUIRE(timeout_log);
        auto [timeout_job, timeout_created] =
            windows::KillOnCloseProcessJob::create(1U);
        REQUIRE(timeout_created);
        const auto timeout_port = reserve_then_release_loopback_port();
        auto [timeout_server, timeout_started] = timeout_job.launch(
            fake_server_spec(server_identity, timeout_port, 5'000U,
                             &*timeout_log, 0U, true));
        REQUIRE(timeout_started);
        timeout_log->close_parent_write_handle();
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        const auto timeout = windows::diagnose_required_hlds_runtime_banner(
            timeout_log->snapshot(), "boot_camp", timeout_port);
        CHECK(timeout.diagnostic.parse_status == ParseStatus::incomplete);
        CHECK(timeout_server.running());
        REQUIRE(timeout_job.terminate_and_wait(
            120U, std::chrono::seconds{5}));
        static_cast<void>(timeout_log->finish());
        require_no_client();

        windows::BoundedProcessLogLimits truncation_limits;
        truncation_limits.maximum_bytes = 1'024U;
        truncation_limits.maximum_line_length = 4'096U;
        truncation_limits.maximum_line_count = 1'024U;
        const auto truncated = run_variant(
            "log-truncation", truncation_limits);
        CHECK(truncated.diagnostic.parse_status ==
              ParseStatus::process_log_truncated);
        CHECK(truncated.diagnostic.mismatch_field ==
              Field::process_log_truncated);
    }
}

TEST_CASE("Exact image scan exposes enumeration failure instead of an empty set",
          "[platform][windows][stock-runtime][orchestrator][process-scan]")
{
    const windows::WindowsBinaryIdentity invalid_identity{};
    const auto scan =
        windows::find_processes_with_exact_image_identity(invalid_identity);
    CHECK_FALSE(scan);
    CHECK(scan.code ==
          windows::ExactImageProcessScanErrorCode::enumeration_failed);
    CHECK(scan.native_error == ERROR_INVALID_PARAMETER);
    CHECK(scan.process_ids.empty());
}

TEST_CASE("Stock runtime startup state accepts only the exact process order",
          "[platform][windows][stock-runtime][orchestrator][startup-order]")
{
    using Event = windows::StockRuntimeStartupEvent;
    windows::StockRuntimeStartupState state;
    advance_to_relay_ready(state);
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::server_started));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::server_readiness_observed));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::client_started));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::client_readiness_observed));
    CHECK(state.stage == windows::StockRuntimeStartupStage::capture_running);
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::cancellation_requested));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::cleanup_completed));
    CHECK(state.stage == windows::StockRuntimeStartupStage::complete);
    CHECK(state.failure == windows::StockRuntimeStartupFailure::none);

    windows::StockRuntimeStartupState out_of_order;
    CHECK_FALSE(windows::apply_stock_runtime_startup_event(
        out_of_order, Event::relay_started));
    CHECK(out_of_order.failure ==
          windows::StockRuntimeStartupFailure::out_of_order);
}

TEST_CASE("Stock runtime startup state types relay not-ready",
          "[platform][windows][stock-runtime][orchestrator][relay-not-ready]")
{
    using Event = windows::StockRuntimeStartupEvent;
    windows::StockRuntimeStartupState state;
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::guard_started));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::guard_readiness_observed));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::active_canary_succeeded));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::run_root_created));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        state, Event::relay_started));
    CHECK_FALSE(windows::apply_stock_runtime_startup_event(
        state, Event::relay_readiness_timeout));
    CHECK(state.failure ==
          windows::StockRuntimeStartupFailure::relay_not_ready);
}

TEST_CASE("Stock runtime startup state types server banner and timeout",
          "[platform][windows][stock-runtime][orchestrator][server-timeout]")
{
    using Event = windows::StockRuntimeStartupEvent;
    windows::StockRuntimeStartupState mismatch;
    advance_to_relay_ready(mismatch);
    REQUIRE(windows::apply_stock_runtime_startup_event(
        mismatch, Event::server_started));
    CHECK_FALSE(windows::apply_stock_runtime_startup_event(
        mismatch, Event::server_banner_mismatch));
    CHECK(mismatch.failure ==
          windows::StockRuntimeStartupFailure::server_banner_mismatch);

    windows::StockRuntimeStartupState timeout;
    advance_to_relay_ready(timeout);
    REQUIRE(windows::apply_stock_runtime_startup_event(
        timeout, Event::server_started));
    CHECK_FALSE(windows::apply_stock_runtime_startup_event(
        timeout, Event::server_readiness_timeout));
    CHECK(timeout.failure ==
          windows::StockRuntimeStartupFailure::server_timeout);
}

TEST_CASE("Stock runtime startup state types guard and client early exit",
          "[platform][windows][stock-runtime][orchestrator][early-exit]")
{
    using Event = windows::StockRuntimeStartupEvent;
    windows::StockRuntimeStartupState guard;
    REQUIRE(windows::apply_stock_runtime_startup_event(
        guard, Event::guard_started));
    CHECK_FALSE(windows::apply_stock_runtime_startup_event(
        guard, Event::guard_early_exit));
    CHECK(guard.failure ==
          windows::StockRuntimeStartupFailure::guard_early_exit);

    windows::StockRuntimeStartupState client;
    advance_to_relay_ready(client);
    REQUIRE(windows::apply_stock_runtime_startup_event(
        client, Event::server_started));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        client, Event::server_readiness_observed));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        client, Event::client_started));
    CHECK_FALSE(windows::apply_stock_runtime_startup_event(
        client, Event::client_early_exit));
    CHECK(client.failure ==
          windows::StockRuntimeStartupFailure::client_early_exit);
}

TEST_CASE("Kill-on-close job owns only the exact launched fake process",
          "[platform][windows][stock-runtime][orchestrator][job]")
{
    const auto identity = observe_fake_server();
    const auto port = reserve_then_release_loopback_port();
    auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(created);
    auto [child, launched] = job.launch(
        fake_server_spec(identity, port, 30'000U));
    INFO("launch_code=" << windows::to_string(launched.code)
                        << " native_error=" << launched.native_error
                        << " process_id=" << launched.process_id);
    REQUIRE(launched);
    REQUIRE(child.running());
    CHECK(job.active_process_count() == 1U);

    const auto matches = windows::find_processes_with_exact_image_identity(identity);
    REQUIRE(matches);
    CHECK(std::ranges::find(matches.process_ids, child.process_id()) !=
          matches.process_ids.end());
    DWORD current_exit = 0U;
    REQUIRE(::GetExitCodeProcess(::GetCurrentProcess(), &current_exit) != FALSE);
    CHECK(current_exit == STILL_ACTIVE);

    job.close();
    CHECK_FALSE(job.active_process_count());
    REQUIRE(child.wait(std::chrono::seconds{5}));
    REQUIRE(::GetExitCodeProcess(::GetCurrentProcess(), &current_exit) != FALSE);
    CHECK(current_exit == STILL_ACTIVE);
}

TEST_CASE("Owned launch denies child process creation without side effects",
          "[platform][windows][stock-runtime][orchestrator][job]"
          "[child-process-restriction][fake-integration]")
{
    const auto identity = observe_fake_server();
    const auto port = reserve_then_release_loopback_port();
    const auto marker = std::filesystem::temp_directory_path() /
        (L"hlclient-child-process-denial-" +
         std::to_wstring(::GetCurrentProcessId()) + L"-" +
         std::to_wstring(::GetTickCount64()) + L".txt");
    REQUIRE_FALSE(std::filesystem::exists(marker));
    auto log = windows::BoundedProcessLogCapture::create({});
    REQUIRE(log);
    auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(created);
    auto spec = fake_server_spec(identity, port, 100U, &*log);
    spec.arguments.push_back(L"--attempt-child-create-file");
    spec.arguments.push_back(marker.wstring());
    auto [child, launched] = job.launch(spec);
    REQUIRE(launched);
    log->close_parent_write_handle();
    REQUIRE(child.wait(std::chrono::seconds{5}) == 0U);
    const auto snapshot = log->finish();
    INFO("child restriction log=" << snapshot.bytes);
    CHECK(snapshot.bytes.find(
              "[hlclient-fake-server] child-create=denied;native-error=367\n") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(marker));
    std::error_code ignored;
    static_cast<void>(std::filesystem::remove(marker, ignored));
    const auto cleanup =
        job.terminate_and_wait(120U, std::chrono::seconds{2});
    REQUIRE(cleanup);
    CHECK(cleanup.active_process_count == 0U);
}

TEST_CASE("Active-process-limit rejection creates no unassigned suspended child",
          "[platform][windows][stock-runtime][orchestrator][job]"
          "[atomic-job-assignment]")
{
    const auto identity = observe_fake_server();
    const auto first_port = reserve_then_release_loopback_port();
    auto second_port = reserve_then_release_loopback_port();
    while (second_port == first_port) {
        second_port = reserve_then_release_loopback_port();
    }
    auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(created);
    auto [first, first_result] = job.launch(
        fake_server_spec(identity, first_port, 30'000U));
    REQUIRE(first_result);
    REQUIRE(first.running());

    auto [rejected, rejected_result] = job.launch(
        fake_server_spec(identity, second_port, 30'000U));
    CHECK_FALSE(rejected_result);
    CHECK_FALSE(rejected.valid());
    CHECK(rejected_result.code ==
          windows::OwnedProcessErrorCode::create_process_failed);
    CHECK(rejected_result.process_id == 0U);

    const auto cleanup =
        job.terminate_and_wait(120U, std::chrono::seconds{5});
    REQUIRE(cleanup);
    REQUIRE(first.wait(std::chrono::seconds{2}) == 120U);
}

TEST_CASE("Typed job cleanup waits for zero owned processes",
          "[platform][windows][stock-runtime][orchestrator][job][cleanup]")
{
    const auto identity = observe_fake_server();
    const auto port = reserve_then_release_loopback_port();
    auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(created);
    auto [child, launched] = job.launch(
        fake_server_spec(identity, port, 30'000U));
    REQUIRE(launched);
    REQUIRE(child.running());

    const auto cleanup =
        job.terminate_and_wait(120U, std::chrono::seconds{5});
    REQUIRE(cleanup);
    CHECK(cleanup.code == windows::OwnedJobCleanupErrorCode::none);
    CHECK(cleanup.active_process_count == 0U);
    CHECK(job.active_process_count() == 0U);
    REQUIRE(child.wait(std::chrono::seconds{2}) == 120U);
    job.close();
    CHECK_FALSE(job.active_process_count());

    const windows::KillOnCloseProcessJob invalid;
    const auto invalid_cleanup =
        const_cast<windows::KillOnCloseProcessJob&>(invalid).terminate_and_wait(
            120U, std::chrono::milliseconds{0});
    CHECK_FALSE(invalid_cleanup);
    CHECK(invalid_cleanup.code ==
          windows::OwnedJobCleanupErrorCode::invalid_job);
}

TEST_CASE("Isolation guard survives owner death and empties the campaign before exit",
          "[platform][windows][stock-runtime][orchestrator][job]"
          "[joint-death][fake-integration]")
{
    const auto fake_server = observe_fake_server();
    const auto isolation_guard = observe_isolation_guard();

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    TestHandle campaign_owner{::CreateJobObjectW(&security, nullptr)};
    TestHandle guard_owner{::CreateJobObjectW(&security, nullptr)};
    REQUIRE(campaign_owner);
    REQUIRE(guard_owner);
    const HANDLE campaign_handle = campaign_owner.get();
    const HANDLE guard_handle = guard_owner.get();
    auto [campaign_job, campaign_adopted] =
        windows::KillOnCloseProcessJob::adopt_inherited(campaign_handle, 2U);
    REQUIRE(campaign_adopted);
    static_cast<void>(campaign_owner.release());
    auto [guard_job, guard_adopted] =
        windows::KillOnCloseProcessJob::adopt_inherited(guard_handle, 1U);
    REQUIRE(guard_adopted);
    static_cast<void>(guard_owner.release());

    HANDLE audit_raw = nullptr;
    REQUIRE(::DuplicateHandle(
        ::GetCurrentProcess(), campaign_handle, ::GetCurrentProcess(),
        &audit_raw, 0U, FALSE, DUPLICATE_SAME_ACCESS));
    TestHandle campaign_audit{audit_raw};

    HANDLE readiness_read_raw = nullptr;
    HANDLE readiness_write_raw = nullptr;
    REQUIRE(::CreatePipe(
        &readiness_read_raw, &readiness_write_raw, &security, 0U));
    TestHandle readiness_read{readiness_read_raw};
    TestHandle readiness_write{readiness_write_raw};
    REQUIRE(::SetHandleInformation(
        readiness_read.get(), HANDLE_FLAG_INHERIT, 0U));

    HANDLE heartbeat_read_raw = nullptr;
    HANDLE heartbeat_write_raw = nullptr;
    REQUIRE(::CreatePipe(
        &heartbeat_read_raw, &heartbeat_write_raw, &security, 0U));
    TestHandle heartbeat_read{heartbeat_read_raw};
    TestHandle heartbeat_write{heartbeat_write_raw};
    REQUIRE(::SetHandleInformation(
        heartbeat_write.get(), HANDLE_FLAG_INHERIT, 0U));
    TestHandle isolation_release{
        ::CreateEventW(&security, TRUE, FALSE, nullptr)};
    REQUIRE(isolation_release);

    const auto port = reserve_then_release_loopback_port();
    auto [campaign_child, campaign_started] = campaign_job.launch(
        fake_server_spec(fake_server, port, 30'000U));
    REQUIRE(campaign_started);
    REQUIRE(campaign_child.running());
    REQUIRE(campaign_job.active_process_count() == 1U);

    windows::OwnedProcessLaunchSpec guard_spec;
    guard_spec.executable = isolation_guard.canonical_path;
    guard_spec.working_directory = isolation_guard.canonical_path.parent_path();
    guard_spec.expected_identity = isolation_guard;
    guard_spec.additional_inherited_handles = {
        readiness_write.get(), heartbeat_read.get(), isolation_release.get(),
        campaign_handle, guard_handle};
    guard_spec.arguments = {
        L"--readiness-handle", handle_decimal(readiness_write.get()),
        L"--heartbeat-handle", handle_decimal(heartbeat_read.get()),
        L"--isolation-release-handle", handle_decimal(isolation_release.get()),
        L"--campaign-job-handle", handle_decimal(campaign_handle),
        L"--guard-job-handle", handle_decimal(guard_handle),
        L"--job-cleanup-self-test"};
    auto [guard, guard_started] = guard_job.launch(guard_spec);
    REQUIRE(guard_started);
    REQUIRE(guard.running());
    REQUIRE(::CloseHandle(readiness_write.release()));
    REQUIRE(::CloseHandle(heartbeat_read.release()));

    std::array<char, 128U> readiness{};
    DWORD readiness_bytes = 0U;
    REQUIRE(::ReadFile(
        readiness_read.get(), readiness.data(),
        static_cast<DWORD>(readiness.size()), &readiness_bytes, nullptr));
    REQUIRE(std::string_view{readiness.data(), readiness_bytes} ==
            "job-cleanup-self-test=ready\n");

    // Simulate simultaneous wrapper/orchestrator death: their Job handles and
    // the heartbeat writer disappear, while the guard's inherited copies keep
    // both Jobs alive long enough to perform exact campaign cleanup.
    campaign_job.close();
    guard_job.close();
    REQUIRE(::CloseHandle(heartbeat_write.release()));

    REQUIRE(guard.wait(std::chrono::seconds{5}) == 1U);
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    REQUIRE(::QueryInformationJobObject(
        campaign_audit.get(), JobObjectBasicAccountingInformation,
        &accounting, sizeof(accounting), nullptr));
    CHECK(accounting.ActiveProcesses == 0U);
    REQUIRE(campaign_child.wait(std::chrono::seconds{2}) == 122U);
}

TEST_CASE("Graceful cleanup requires every exact exit and accounting result",
          "[platform][windows][stock-runtime][orchestrator][cleanup-predicate]")
{
    using OptionalExit = std::optional<std::uint32_t>;
    using OptionalCount = std::optional<std::size_t>;
    CHECK(windows::stock_runtime_stock_process_shutdown_confirmed(
        OptionalExit{0U}, OptionalExit{0U}));
    CHECK_FALSE(windows::stock_runtime_stock_process_shutdown_confirmed(
        std::nullopt, OptionalExit{0U}));
    CHECK_FALSE(windows::stock_runtime_stock_process_shutdown_confirmed(
        OptionalExit{0U}, std::nullopt));
    CHECK_FALSE(windows::stock_runtime_stock_process_shutdown_confirmed(
        OptionalExit{1U}, OptionalExit{0U}));
    CHECK_FALSE(windows::stock_runtime_stock_process_shutdown_confirmed(
        OptionalExit{0U}, OptionalExit{1U}));
    CHECK(windows::stock_runtime_graceful_cleanup_is_exact(
        OptionalExit{0U}, OptionalExit{0U}, OptionalExit{0U}, OptionalExit{0U},
        OptionalCount{0U}));
    CHECK_FALSE(windows::stock_runtime_graceful_cleanup_is_exact(
        std::nullopt, OptionalExit{0U}, OptionalExit{0U}, OptionalExit{0U},
        OptionalCount{0U}));
    CHECK_FALSE(windows::stock_runtime_graceful_cleanup_is_exact(
        OptionalExit{0U}, std::nullopt, OptionalExit{0U}, OptionalExit{0U},
        OptionalCount{0U}));
    CHECK_FALSE(windows::stock_runtime_graceful_cleanup_is_exact(
        OptionalExit{0U}, OptionalExit{0U}, OptionalExit{0U}, OptionalExit{1U},
        OptionalCount{0U}));
    CHECK_FALSE(windows::stock_runtime_graceful_cleanup_is_exact(
        OptionalExit{0U}, OptionalExit{0U}, OptionalExit{0U}, OptionalExit{0U},
        std::nullopt));
    CHECK_FALSE(windows::stock_runtime_graceful_cleanup_is_exact(
        OptionalExit{0U}, OptionalExit{0U}, OptionalExit{0U}, OptionalExit{0U},
        OptionalCount{1U}));

    const windows::OwnedJobCleanupResult exact_cleanup{};
    const windows::OwnedJobCleanupResult failed_cleanup{
        windows::OwnedJobCleanupErrorCode::timeout, ERROR_TIMEOUT, 1U};
    CHECK(windows::stock_runtime_owned_jobs_allow_isolation_release(
        exact_cleanup,
        std::optional<windows::OwnedJobCleanupResult>{exact_cleanup}));
    CHECK_FALSE(windows::stock_runtime_owned_jobs_allow_isolation_release(
        failed_cleanup,
        std::optional<windows::OwnedJobCleanupResult>{exact_cleanup}));
    CHECK_FALSE(windows::stock_runtime_owned_jobs_allow_isolation_release(
        exact_cleanup,
        std::optional<windows::OwnedJobCleanupResult>{failed_cleanup}));
    CHECK_FALSE(windows::stock_runtime_owned_jobs_allow_isolation_release(
        exact_cleanup, std::nullopt));
}

TEST_CASE("Abrupt guard loss retains redundant isolation until both Jobs are zero",
          "[platform][windows][stock-runtime][orchestrator]"
          "[redundant-isolation][cleanup-predicate]")
{
    bool redundant_owner_active = true;
    const windows::OwnedJobCleanupResult campaign_live{
        windows::OwnedJobCleanupErrorCode::timeout, ERROR_TIMEOUT, 1U};
    const windows::OwnedJobCleanupResult exact{};

    // Primary guard death is represented by an absent guard cleanup result.
    CHECK_FALSE(windows::stock_runtime_owned_jobs_allow_isolation_release(
        campaign_live, std::nullopt));
    CHECK(redundant_owner_active);
    CHECK_FALSE(windows::stock_runtime_owned_jobs_allow_isolation_release(
        exact, std::nullopt));
    CHECK(redundant_owner_active);

    const auto release =
        windows::stock_runtime_owned_jobs_allow_isolation_release(
            exact, std::optional<windows::OwnedJobCleanupResult>{exact});
    REQUIRE(release);
    redundant_owner_active = false;
    CHECK_FALSE(redundant_owner_active);
}

TEST_CASE("Process logs are drained concurrently and remain bounded",
          "[platform][windows][stock-runtime][orchestrator][logs]")
{
    windows::BoundedProcessLogLimits limits;
    limits.maximum_bytes = 1'024U;
    limits.maximum_line_length = 64U;
    limits.maximum_line_count = 16U;
    auto log = windows::BoundedProcessLogCapture::create(limits);
    REQUIRE(log);

    const auto identity = observe_fake_server();
    const auto port = reserve_then_release_loopback_port();
    auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(created);
    const HANDLE inherited_output =
        static_cast<HANDLE>(log->inherited_write_handle());
    auto [child, launched] = job.launch(
        fake_server_spec(identity, port, 5'000U, &*log, 128U * 1'024U));
    INFO("launch_code=" << windows::to_string(launched.code)
                        << " native_error=" << launched.native_error
                        << " process_id=" << launched.process_id);
    REQUIRE(launched);
    HANDLE copied_output = nullptr;
    const bool copied = ::DuplicateHandle(
        static_cast<HANDLE>(child.native_process_handle()), inherited_output,
        ::GetCurrentProcess(), &copied_output, 0U, FALSE,
        DUPLICATE_SAME_ACCESS) != FALSE;
    INFO("child_inherited_output=" << copied
                                    << " duplicate_error=" << ::GetLastError());
    REQUIRE(copied);
    CHECK(::GetFileType(copied_output) == FILE_TYPE_PIPE);
    REQUIRE(::CloseHandle(copied_output) != FALSE);
    log->close_parent_write_handle();

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline &&
           log->snapshot().observed_bytes < 128U * 1'024U) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    const bool running_before_terminate = child.running();
    const auto early_exit = running_before_terminate
        ? std::optional<std::uint32_t>{}
        : child.wait(std::chrono::milliseconds{0});
    INFO("running_before_terminate=" << running_before_terminate
                                     << " early_exit="
                                     << (early_exit ? std::to_string(*early_exit)
                                                    : std::string{"none"}));
    child.terminate(0U);
    REQUIRE(child.wait(std::chrono::seconds{3}));
    const auto captured = log->finish();
    CHECK(captured.observed_bytes >= 128U * 1'024U);
    CHECK(captured.bytes.size() == limits.maximum_bytes);
    CHECK(captured.byte_truncated);
    CHECK(captured.line_count_truncated);
    CHECK(captured.line_length_truncated);
    CHECK_FALSE(captured.capture_failed);
    CHECK_FALSE(windows::bounded_process_log_snapshot_complete(captured));
}

TEST_CASE("Fake orchestration starts server before client and reaches capture",
          "[platform][windows][stock-runtime][orchestrator][fake-integration]"
          "[startup-order]")
{
    using Event = windows::StockRuntimeStartupEvent;
    const auto server_identity = observe_fake_server();
    const auto client_identity = observe_fake_client();
    const auto port = reserve_then_release_loopback_port();
    auto server_log = windows::BoundedProcessLogCapture::create({});
    auto client_log = windows::BoundedProcessLogCapture::create({});
    REQUIRE(server_log);
    REQUIRE(client_log);
    auto [job, created] = windows::KillOnCloseProcessJob::create(2U);
    REQUIRE(created);

    windows::StockRuntimeStartupState startup;
    advance_to_relay_ready(startup);
    auto [server, server_started] = job.launch(
        fake_server_spec(server_identity, port, 5'000U, &*server_log));
    REQUIRE(server_started);
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::server_started));
    server_log->close_parent_write_handle();
    REQUIRE(wait_for_log_marker(
        *server_log, server, "[hlclient-fake-server] ready=true",
        std::chrono::seconds{2}));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::server_readiness_observed));

    auto [client, client_started] = job.launch(
        fake_client_spec(client_identity, port, 2'000U, &*client_log));
    REQUIRE(client_started);
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::client_started));
    client_log->close_parent_write_handle();
    REQUIRE(client.wait(std::chrono::seconds{3}) == 0U);
    REQUIRE(server.wait(std::chrono::seconds{3}) == 0U);
    const auto client_output = client_log->finish();
    const auto server_output = server_log->finish();
    REQUIRE(windows::bounded_process_log_snapshot_complete(client_output));
    REQUIRE(windows::bounded_process_log_snapshot_complete(server_output));
    REQUIRE(client_output.bytes.find(
                "[hlclient-fake-client] ready=true") != std::string::npos);
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::client_readiness_observed));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::cancellation_requested));
    CHECK(job.active_process_count() == 0U);
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::cleanup_completed));
    CHECK(startup.stage == windows::StockRuntimeStartupStage::complete);
}

TEST_CASE("Fake relay not-ready and server timeout fail before client launch",
          "[platform][windows][stock-runtime][orchestrator][fake-integration]"
          "[relay-not-ready][server-timeout]")
{
    using Event = windows::StockRuntimeStartupEvent;
    const auto identity = observe_fake_server();

    SECTION("relay never publishes readiness") {
        const auto port = reserve_then_release_loopback_port();
        auto log = windows::BoundedProcessLogCapture::create({});
        REQUIRE(log);
        auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
        REQUIRE(created);
        windows::StockRuntimeStartupState startup;
        REQUIRE(windows::apply_stock_runtime_startup_event(
            startup, Event::guard_started));
        REQUIRE(windows::apply_stock_runtime_startup_event(
            startup, Event::guard_readiness_observed));
        REQUIRE(windows::apply_stock_runtime_startup_event(
            startup, Event::active_canary_succeeded));
        REQUIRE(windows::apply_stock_runtime_startup_event(
            startup, Event::run_root_created));
        auto [relay, launched] = job.launch(fake_server_spec(
            identity, port, 5'000U, &*log, 0U, true));
        REQUIRE(launched);
        REQUIRE(windows::apply_stock_runtime_startup_event(
            startup, Event::relay_started));
        log->close_parent_write_handle();
        CHECK_FALSE(wait_for_log_marker(
            *log, relay, "[hlclient-fake-server] ready=true",
            std::chrono::milliseconds{100}));
        REQUIRE(relay.running());
        CHECK_FALSE(windows::apply_stock_runtime_startup_event(
            startup, Event::relay_readiness_timeout));
        CHECK(startup.failure ==
              windows::StockRuntimeStartupFailure::relay_not_ready);
        job.close();
        REQUIRE(relay.wait(std::chrono::seconds{3}));
        static_cast<void>(log->finish());
    }

    SECTION("server readiness exceeds its deadline") {
        const auto port = reserve_then_release_loopback_port();
        auto log = windows::BoundedProcessLogCapture::create({});
        REQUIRE(log);
        auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
        REQUIRE(created);
        windows::StockRuntimeStartupState startup;
        advance_to_relay_ready(startup);
        auto [server, launched] = job.launch(fake_server_spec(
            identity, port, 5'000U, &*log, 0U, false, 1'000U));
        REQUIRE(launched);
        REQUIRE(windows::apply_stock_runtime_startup_event(
            startup, Event::server_started));
        log->close_parent_write_handle();
        CHECK_FALSE(wait_for_log_marker(
            *log, server, "[hlclient-fake-server] ready=true",
            std::chrono::milliseconds{100}));
        REQUIRE(server.running());
        CHECK_FALSE(windows::apply_stock_runtime_startup_event(
            startup, Event::server_readiness_timeout));
        CHECK(startup.failure ==
              windows::StockRuntimeStartupFailure::server_timeout);
        job.close();
        REQUIRE(server.wait(std::chrono::seconds{3}));
        static_cast<void>(log->finish());
    }
}

TEST_CASE("Fake guard early exit is typed before relay startup",
          "[platform][windows][stock-runtime][orchestrator][fake-integration]"
          "[guard-early-exit]")
{
    using Event = windows::StockRuntimeStartupEvent;
    const auto identity = observe_fake_server();
    const auto port = reserve_then_release_loopback_port();
    auto log = windows::BoundedProcessLogCapture::create({});
    REQUIRE(log);
    auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(created);
    auto [guard, launched] = job.launch(fake_server_spec(
        identity, port, 0U, &*log, 0U, true));
    REQUIRE(launched);
    log->close_parent_write_handle();
    windows::StockRuntimeStartupState startup;
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::guard_started));
    REQUIRE(guard.wait(std::chrono::seconds{3}) == 0U);
    CHECK_FALSE(windows::apply_stock_runtime_startup_event(
        startup, Event::guard_early_exit));
    CHECK(startup.failure ==
          windows::StockRuntimeStartupFailure::guard_early_exit);
    CHECK(job.active_process_count() == 0U);
    static_cast<void>(log->finish());
}

TEST_CASE("Client early exit cleanup leaves unrelated owned process alive",
          "[platform][windows][stock-runtime][orchestrator][fake-integration]"
          "[client-early-exit][unrelated-process]")
{
    using Event = windows::StockRuntimeStartupEvent;
    const auto server_identity = observe_fake_server();
    const auto client_identity = observe_fake_client();
    const auto unrelated_port = reserve_then_release_loopback_port();
    auto unused_port = reserve_then_release_loopback_port();
    while (unused_port == unrelated_port) {
        unused_port = reserve_then_release_loopback_port();
    }
    auto unrelated_log = windows::BoundedProcessLogCapture::create({});
    auto client_log = windows::BoundedProcessLogCapture::create({});
    REQUIRE(unrelated_log);
    REQUIRE(client_log);
    auto [unrelated_job, unrelated_created] =
        windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(unrelated_created);
    auto [unrelated, unrelated_started] = unrelated_job.launch(
        fake_server_spec(server_identity, unrelated_port, 30'000U,
                         &*unrelated_log));
    REQUIRE(unrelated_started);
    unrelated_log->close_parent_write_handle();
    REQUIRE(wait_for_log_marker(
        *unrelated_log, unrelated, "[hlclient-fake-server] ready=true",
        std::chrono::seconds{2}));

    auto [campaign_job, campaign_created] =
        windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(campaign_created);
    auto [client, client_started] = campaign_job.launch(
        fake_client_spec(client_identity, unused_port, 100U, &*client_log));
    REQUIRE(client_started);
    client_log->close_parent_write_handle();
    const auto client_exit = client.wait(std::chrono::seconds{3});
    REQUIRE(client_exit);
    CHECK(*client_exit == 5U);

    windows::StockRuntimeStartupState startup;
    advance_to_relay_ready(startup);
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::server_started));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::server_readiness_observed));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::client_started));
    CHECK_FALSE(windows::apply_stock_runtime_startup_event(
        startup, Event::client_early_exit));
    campaign_job.close();
    CHECK(unrelated.running());

    unrelated_job.close();
    REQUIRE(unrelated.wait(std::chrono::seconds{3}));
    static_cast<void>(client_log->finish());
    static_cast<void>(unrelated_log->finish());
}

TEST_CASE("Cancellation cleanup closes the job and kills every campaign child",
          "[platform][windows][stock-runtime][orchestrator][fake-integration]"
          "[ctrl-c-cleanup]")
{
    using Event = windows::StockRuntimeStartupEvent;
    const auto identity = observe_fake_server();
    const auto port = reserve_then_release_loopback_port();
    auto log = windows::BoundedProcessLogCapture::create({});
    REQUIRE(log);
    auto [job, created] = windows::KillOnCloseProcessJob::create(1U);
    REQUIRE(created);
    auto [child, launched] = job.launch(
        fake_server_spec(identity, port, 30'000U, &*log));
    REQUIRE(launched);
    log->close_parent_write_handle();
    REQUIRE(wait_for_log_marker(
        *log, child, "[hlclient-fake-server] ready=true",
        std::chrono::seconds{2}));

    windows::StockRuntimeStartupState startup;
    advance_to_relay_ready(startup);
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::server_started));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::server_readiness_observed));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::client_started));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::client_readiness_observed));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::cancellation_requested));
    job.close();
    REQUIRE(child.wait(std::chrono::seconds{3}));
    REQUIRE(windows::apply_stock_runtime_startup_event(
        startup, Event::cleanup_completed));
    CHECK(startup.stage == windows::StockRuntimeStartupStage::complete);
    static_cast<void>(log->finish());
}
