#include <hlclient/platform/windows/process_orchestrator.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
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

namespace hlclient::platform::windows {
namespace {

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
    [[nodiscard]] HANDLE release() noexcept
    {
        return std::exchange(handle_, INVALID_HANDLE_VALUE);
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    void reset() noexcept
    {
        if (*this) {
            static_cast<void>(::CloseHandle(handle_));
        }
        handle_ = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

[[nodiscard]] DWORD bounded_timeout(
    const std::chrono::milliseconds timeout) noexcept
{
    if (timeout <= std::chrono::milliseconds::zero()) {
        return 0U;
    }
    return static_cast<DWORD>((std::min<std::int64_t>)(
        timeout.count(),
        static_cast<std::int64_t>((std::numeric_limits<DWORD>::max)() - 1U)));
}

[[nodiscard]] bool path_equal(
    const std::filesystem::path& left,
    const std::filesystem::path& right) noexcept
{
    try {
        auto a = left.wstring();
        auto b = right.wstring();
        const auto strip_extended = [](std::wstring& value) {
            if (value.starts_with(LR"(\\?\)")) {
                value.erase(0U, 4U);
            }
        };
        strip_extended(a);
        strip_extended(b);
        if (a.size() != b.size() ||
            a.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
            return false;
        }
        return ::CompareStringOrdinal(
                   a.data(), static_cast<int>(a.size()), b.data(),
                   static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
    } catch (...) {
        return false;
    }
}

template<typename Integer>
[[nodiscard]] bool parse_decimal(const std::string_view value, Integer& output) noexcept
{
    if (value.empty()) {
        return false;
    }
    Integer parsed{};
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed, 10);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return false;
    }
    output = parsed;
    return true;
}

[[nodiscard]] std::vector<std::string_view> split_lines(
    const std::string_view output)
{
    std::vector<std::string_view> lines;
    std::size_t begin = 0U;
    while (begin <= output.size() && lines.size() < 8'192U) {
        const auto end = output.find('\n', begin);
        auto line = output.substr(
            begin, end == std::string_view::npos ? output.size() - begin
                                                 : end - begin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }
        lines.push_back(line);
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return lines;
}

[[nodiscard]] bool safe_profile_token(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > 64U) {
        return false;
    }
    return std::ranges::all_of(value, [](const char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '_' ||
               character == '-';
    });
}

} // namespace

struct BoundedProcessLogCapture::Impl final {
    UniqueHandle read;
    UniqueHandle write;
    UniqueHandle complete_event;
    BoundedProcessLogLimits limits{};
    std::thread worker;
    std::atomic_bool cancellation_requested{false};
    mutable std::mutex mutex;
    BoundedProcessLogSnapshot snapshot;

    ~Impl()
    {
        write.reset();
        if (worker.joinable()) {
            cancellation_requested.store(true, std::memory_order_relaxed);
            static_cast<void>(::CancelIoEx(read.get(), nullptr));
            static_cast<void>(::CancelSynchronousIo(worker.native_handle()));
            static_cast<void>(::WaitForSingleObject(complete_event.get(), 2'000U));
            worker.join();
        }
        read.reset();
    }
};

bool validate_bounded_process_log_limits(
    const BoundedProcessLogLimits& limits) noexcept
{
    return limits.maximum_bytes > 0U &&
           limits.maximum_bytes <= 16U * 1'024U * 1'024U &&
           limits.maximum_line_length > 0U &&
           limits.maximum_line_length <= 64U * 1'024U &&
           limits.maximum_line_count > 0U &&
           limits.maximum_line_count <= 65'536U;
}

bool bounded_process_log_snapshot_complete(
    const BoundedProcessLogSnapshot& snapshot) noexcept
{
    return !snapshot.capture_failed && !snapshot.byte_truncated &&
           !snapshot.line_count_truncated &&
           !snapshot.line_length_truncated;
}

BoundedProcessLogReadDisposition classify_bounded_process_log_read(
    const bool read_succeeded,
    const std::uint32_t byte_count,
    const std::uint32_t native_error,
    const bool locally_cancelled) noexcept
{
    if (read_succeeded) {
        return byte_count == 0U
            ? BoundedProcessLogReadDisposition::clean_eof
            : BoundedProcessLogReadDisposition::data;
    }
    if (native_error == ERROR_BROKEN_PIPE ||
        (native_error == ERROR_OPERATION_ABORTED && locally_cancelled)) {
        return BoundedProcessLogReadDisposition::clean_eof;
    }
    return BoundedProcessLogReadDisposition::error;
}

BoundedProcessLogCapture::BoundedProcessLogCapture() noexcept = default;
BoundedProcessLogCapture::~BoundedProcessLogCapture() = default;
BoundedProcessLogCapture::BoundedProcessLogCapture(
    BoundedProcessLogCapture&&) noexcept = default;
BoundedProcessLogCapture& BoundedProcessLogCapture::operator=(
    BoundedProcessLogCapture&&) noexcept = default;
BoundedProcessLogCapture::BoundedProcessLogCapture(
    std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)}
{
}

std::optional<BoundedProcessLogCapture> BoundedProcessLogCapture::create(
    const BoundedProcessLogLimits& limits) noexcept
{
    try {
        if (!validate_bounded_process_log_limits(limits)) {
            return std::nullopt;
        }
        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        HANDLE read = nullptr;
        HANDLE write = nullptr;
        if (!::CreatePipe(&read, &write, &security, 64U * 1'024U)) {
            return std::nullopt;
        }
        auto impl = std::make_unique<Impl>();
        impl->read = UniqueHandle{read};
        impl->write = UniqueHandle{write};
        if (!::SetHandleInformation(impl->read.get(), HANDLE_FLAG_INHERIT, 0U)) {
            return std::nullopt;
        }
        impl->complete_event = UniqueHandle{
            ::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (!impl->complete_event) {
            return std::nullopt;
        }
        impl->limits = limits;
        impl->snapshot.bytes.reserve(limits.maximum_bytes);
        Impl* state = impl.get();
        impl->worker = std::thread{[state]() noexcept {
            try {
                std::array<char, 4'096U> buffer{};
                std::size_t current_line = 0U;
                for (;;) {
                    DWORD count = 0U;
                    const BOOL read = ::ReadFile(
                        state->read.get(), buffer.data(),
                        static_cast<DWORD>(buffer.size()), &count, nullptr);
                    const DWORD native = read != FALSE
                        ? ERROR_SUCCESS : ::GetLastError();
                    const auto disposition = classify_bounded_process_log_read(
                        read != FALSE, count, native,
                        state->cancellation_requested.load(
                            std::memory_order_relaxed));
                    if (disposition == BoundedProcessLogReadDisposition::clean_eof) {
                        break;
                    }
                    if (disposition == BoundedProcessLogReadDisposition::error) {
                        std::lock_guard lock{state->mutex};
                        state->snapshot.capture_failed = true;
                        state->snapshot.native_error = native;
                        break;
                    }
                    std::lock_guard lock{state->mutex};
                    if (state->snapshot.observed_bytes <=
                        (std::numeric_limits<std::size_t>::max)() - count) {
                        state->snapshot.observed_bytes += count;
                    } else {
                        state->snapshot.observed_bytes =
                            (std::numeric_limits<std::size_t>::max)();
                        state->snapshot.byte_truncated = true;
                    }
                    for (DWORD index = 0U; index < count; ++index) {
                        const char value = buffer[index];
                        if (state->snapshot.bytes.size() < state->limits.maximum_bytes) {
                            state->snapshot.bytes.push_back(value);
                        } else {
                            state->snapshot.byte_truncated = true;
                        }
                        if (value == '\n') {
                            if (state->snapshot.observed_line_count !=
                                (std::numeric_limits<std::size_t>::max)()) {
                                ++state->snapshot.observed_line_count;
                            }
                            if (state->snapshot.observed_line_count >
                                state->limits.maximum_line_count) {
                                state->snapshot.line_count_truncated = true;
                            }
                            current_line = 0U;
                        } else {
                            if (current_line !=
                                (std::numeric_limits<std::size_t>::max)()) {
                                ++current_line;
                            }
                            state->snapshot.maximum_observed_line_length =
                                (std::max)(state->snapshot.maximum_observed_line_length,
                                          current_line);
                            if (current_line > state->limits.maximum_line_length) {
                                state->snapshot.line_length_truncated = true;
                            }
                        }
                    }
                }
                if (current_line != 0U) {
                    std::lock_guard lock{state->mutex};
                    if (state->snapshot.observed_line_count !=
                        (std::numeric_limits<std::size_t>::max)()) {
                        ++state->snapshot.observed_line_count;
                    }
                    if (state->snapshot.observed_line_count >
                        state->limits.maximum_line_count) {
                        state->snapshot.line_count_truncated = true;
                    }
                }
            } catch (...) {
                std::lock_guard lock{state->mutex};
                state->snapshot.capture_failed = true;
                state->snapshot.native_error = ERROR_NOT_ENOUGH_MEMORY;
                state->snapshot.byte_truncated = true;
            }
            static_cast<void>(::SetEvent(state->complete_event.get()));
        }};
        return BoundedProcessLogCapture{std::move(impl)};
    } catch (...) {
        return std::nullopt;
    }
}

void* BoundedProcessLogCapture::inherited_write_handle() const noexcept
{
    return impl_ ? impl_->write.get() : nullptr;
}

void BoundedProcessLogCapture::close_parent_write_handle() noexcept
{
    if (impl_) {
        impl_->write.reset();
    }
}

BoundedProcessLogSnapshot BoundedProcessLogCapture::snapshot() const
{
    if (!impl_) {
        return {};
    }
    std::lock_guard lock{impl_->mutex};
    return impl_->snapshot;
}

BoundedProcessLogSnapshot BoundedProcessLogCapture::finish(
    const std::chrono::milliseconds timeout) noexcept
{
    if (!impl_) {
        return {};
    }
    impl_->write.reset();
    const DWORD wait = ::WaitForSingleObject(
        impl_->complete_event.get(), bounded_timeout(timeout));
    if (wait != WAIT_OBJECT_0) {
        {
            std::lock_guard lock{impl_->mutex};
            impl_->snapshot.capture_failed = true;
            impl_->snapshot.native_error = WAIT_TIMEOUT;
        }
        if (impl_->worker.joinable()) {
            impl_->cancellation_requested.store(true, std::memory_order_relaxed);
            static_cast<void>(::CancelIoEx(impl_->read.get(), nullptr));
            static_cast<void>(::CancelSynchronousIo(impl_->worker.native_handle()));
        }
        static_cast<void>(::WaitForSingleObject(
            impl_->complete_event.get(), 2'000U));
    }
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
    std::lock_guard lock{impl_->mutex};
    return std::move(impl_->snapshot);
}

bool apply_stock_runtime_startup_event(
    StockRuntimeStartupState& state,
    const StockRuntimeStartupEvent event) noexcept
{
    const auto advance = [&](const StockRuntimeStartupStage expected,
                             const StockRuntimeStartupStage next) noexcept {
        if (state.stage != expected ||
            state.failure != StockRuntimeStartupFailure::none) {
            state.stage = StockRuntimeStartupStage::failed;
            state.failure = StockRuntimeStartupFailure::out_of_order;
            return false;
        }
        state.stage = next;
        return true;
    };
    const auto fail = [&](const StockRuntimeStartupFailure failure) noexcept {
        state.stage = StockRuntimeStartupStage::failed;
        state.failure = failure;
        return false;
    };
    const auto active_between = [&](const StockRuntimeStartupStage first,
                                    const StockRuntimeStartupStage last) noexcept {
        const auto current = static_cast<int>(state.stage);
        return current >= static_cast<int>(first) &&
               current <= static_cast<int>(last);
    };
    switch (event) {
    case StockRuntimeStartupEvent::guard_started:
        return advance(StockRuntimeStartupStage::initial,
                       StockRuntimeStartupStage::guard_running);
    case StockRuntimeStartupEvent::guard_readiness_observed:
        return advance(StockRuntimeStartupStage::guard_running,
                       StockRuntimeStartupStage::guard_ready);
    case StockRuntimeStartupEvent::active_canary_succeeded:
        return advance(StockRuntimeStartupStage::guard_ready,
                       StockRuntimeStartupStage::active_canary_passed);
    case StockRuntimeStartupEvent::run_root_created:
        return advance(StockRuntimeStartupStage::active_canary_passed,
                       StockRuntimeStartupStage::run_root_ready);
    case StockRuntimeStartupEvent::relay_started:
        return advance(StockRuntimeStartupStage::run_root_ready,
                       StockRuntimeStartupStage::relay_running);
    case StockRuntimeStartupEvent::relay_readiness_observed:
        return advance(StockRuntimeStartupStage::relay_running,
                       StockRuntimeStartupStage::relay_ready);
    case StockRuntimeStartupEvent::server_started:
        return advance(StockRuntimeStartupStage::relay_ready,
                       StockRuntimeStartupStage::server_running);
    case StockRuntimeStartupEvent::server_readiness_observed:
        return advance(StockRuntimeStartupStage::server_running,
                       StockRuntimeStartupStage::server_ready);
    case StockRuntimeStartupEvent::client_started:
        return advance(StockRuntimeStartupStage::server_ready,
                       StockRuntimeStartupStage::client_running);
    case StockRuntimeStartupEvent::client_readiness_observed:
        return advance(StockRuntimeStartupStage::client_running,
                       StockRuntimeStartupStage::capture_running);
    case StockRuntimeStartupEvent::relay_readiness_timeout:
        return state.stage == StockRuntimeStartupStage::relay_running
            ? fail(StockRuntimeStartupFailure::relay_not_ready)
            : fail(StockRuntimeStartupFailure::out_of_order);
    case StockRuntimeStartupEvent::server_banner_mismatch:
        return state.stage == StockRuntimeStartupStage::server_running
            ? fail(StockRuntimeStartupFailure::server_banner_mismatch)
            : fail(StockRuntimeStartupFailure::out_of_order);
    case StockRuntimeStartupEvent::server_readiness_timeout:
        return state.stage == StockRuntimeStartupStage::server_running
            ? fail(StockRuntimeStartupFailure::server_timeout)
            : fail(StockRuntimeStartupFailure::out_of_order);
    case StockRuntimeStartupEvent::client_readiness_timeout:
        return state.stage == StockRuntimeStartupStage::client_running
            ? fail(StockRuntimeStartupFailure::client_timeout)
            : fail(StockRuntimeStartupFailure::out_of_order);
    case StockRuntimeStartupEvent::guard_early_exit:
        return active_between(StockRuntimeStartupStage::guard_running,
                              StockRuntimeStartupStage::capture_running)
            ? fail(StockRuntimeStartupFailure::guard_early_exit)
            : fail(StockRuntimeStartupFailure::out_of_order);
    case StockRuntimeStartupEvent::relay_early_exit:
        return active_between(StockRuntimeStartupStage::relay_running,
                              StockRuntimeStartupStage::capture_running)
            ? fail(StockRuntimeStartupFailure::relay_early_exit)
            : fail(StockRuntimeStartupFailure::out_of_order);
    case StockRuntimeStartupEvent::server_early_exit:
        return active_between(StockRuntimeStartupStage::server_running,
                              StockRuntimeStartupStage::capture_running)
            ? fail(StockRuntimeStartupFailure::server_early_exit)
            : fail(StockRuntimeStartupFailure::out_of_order);
    case StockRuntimeStartupEvent::client_early_exit:
        return active_between(StockRuntimeStartupStage::client_running,
                              StockRuntimeStartupStage::capture_running)
            ? fail(StockRuntimeStartupFailure::client_early_exit)
            : fail(StockRuntimeStartupFailure::out_of_order);
    case StockRuntimeStartupEvent::cancellation_requested:
        if (state.stage == StockRuntimeStartupStage::initial ||
            state.stage == StockRuntimeStartupStage::complete ||
            state.stage == StockRuntimeStartupStage::cleanup_in_progress) {
            return fail(StockRuntimeStartupFailure::out_of_order);
        }
        state.stage = StockRuntimeStartupStage::cleanup_in_progress;
        return true;
    case StockRuntimeStartupEvent::cleanup_completed:
        if (state.stage != StockRuntimeStartupStage::cleanup_in_progress) {
            return fail(StockRuntimeStartupFailure::out_of_order);
        }
        state.stage = StockRuntimeStartupStage::complete;
        return true;
    case StockRuntimeStartupEvent::cleanup_inexact:
        return fail(StockRuntimeStartupFailure::cleanup_inexact);
    }
    return fail(StockRuntimeStartupFailure::out_of_order);
}

std::string_view to_string(
    const StockRuntimeStartupFailure failure) noexcept
{
    switch (failure) {
    case StockRuntimeStartupFailure::none: return "none";
    case StockRuntimeStartupFailure::out_of_order: return "out-of-order";
    case StockRuntimeStartupFailure::relay_not_ready: return "relay-not-ready";
    case StockRuntimeStartupFailure::server_banner_mismatch:
        return "server-banner-mismatch";
    case StockRuntimeStartupFailure::server_timeout: return "server-timeout";
    case StockRuntimeStartupFailure::client_timeout: return "client-timeout";
    case StockRuntimeStartupFailure::guard_early_exit:
        return "guard-early-exit";
    case StockRuntimeStartupFailure::relay_early_exit:
        return "relay-early-exit";
    case StockRuntimeStartupFailure::server_early_exit:
        return "server-early-exit";
    case StockRuntimeStartupFailure::client_early_exit:
        return "client-early-exit";
    case StockRuntimeStartupFailure::cleanup_inexact:
        return "cleanup-inexact";
    }
    return "unknown";
}

std::wstring quote_windows_command_line_argument(const std::wstring_view argument)
{
    if (argument.empty()) {
        return L"\"\"";
    }
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring{argument};
    }
    std::wstring result{L"\""};
    std::size_t backslashes = 0U;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'\"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'\"');
            backslashes = 0U;
        } else {
            result.append(backslashes, L'\\');
            backslashes = 0U;
            result.push_back(character);
        }
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}

std::wstring build_windows_command_line(
    const std::filesystem::path& executable,
    const std::span<const std::wstring> arguments)
{
    std::wstring result =
        quote_windows_command_line_argument(executable.wstring());
    for (const auto& argument : arguments) {
        result.push_back(L' ');
        result += quote_windows_command_line_argument(argument);
    }
    return result;
}

struct OwnedProcess::Impl final {
    UniqueHandle process;
    std::uint32_t process_id{0U};
};

OwnedProcess::OwnedProcess() noexcept = default;
OwnedProcess::~OwnedProcess() = default;
OwnedProcess::OwnedProcess(OwnedProcess&&) noexcept = default;
OwnedProcess& OwnedProcess::operator=(OwnedProcess&&) noexcept = default;
OwnedProcess::OwnedProcess(std::unique_ptr<Impl> impl) noexcept
    : impl_{std::move(impl)}
{
}

bool OwnedProcess::valid() const noexcept
{
    return impl_ != nullptr && static_cast<bool>(impl_->process);
}

std::uint32_t OwnedProcess::process_id() const noexcept
{
    return impl_ ? impl_->process_id : 0U;
}

void* OwnedProcess::native_process_handle() const noexcept
{
    return impl_ ? impl_->process.get() : nullptr;
}

bool OwnedProcess::running() const noexcept
{
    if (!valid()) {
        return false;
    }
    DWORD code = 0U;
    return ::GetExitCodeProcess(impl_->process.get(), &code) != FALSE &&
           code == STILL_ACTIVE;
}

void OwnedProcess::terminate(const std::uint32_t exit_code) noexcept
{
    if (valid()) {
        static_cast<void>(::TerminateProcess(impl_->process.get(), exit_code));
    }
}

std::optional<std::uint32_t> OwnedProcess::wait(
    const std::chrono::milliseconds timeout) noexcept
{
    if (!valid() ||
        ::WaitForSingleObject(impl_->process.get(), bounded_timeout(timeout)) !=
            WAIT_OBJECT_0) {
        return std::nullopt;
    }
    DWORD exit_code = 0U;
    if (!::GetExitCodeProcess(impl_->process.get(), &exit_code)) {
        return std::nullopt;
    }
    return exit_code;
}

struct KillOnCloseProcessJob::Impl final {
    UniqueHandle job;
    // Atomic job-list creation should make this path unreachable. If the
    // post-create membership invariant ever fails and immediate termination
    // cannot be confirmed, retain the exact child handle for typed cleanup.
    UniqueHandle failed_child_process;
    std::uint32_t failed_child_process_id{0U};
    std::size_t maximum_processes{0U};
};

KillOnCloseProcessJob::KillOnCloseProcessJob() noexcept = default;
KillOnCloseProcessJob::~KillOnCloseProcessJob() = default;
KillOnCloseProcessJob::KillOnCloseProcessJob(
    KillOnCloseProcessJob&&) noexcept = default;
KillOnCloseProcessJob& KillOnCloseProcessJob::operator=(
    KillOnCloseProcessJob&&) noexcept = default;
KillOnCloseProcessJob::KillOnCloseProcessJob(
    std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)}
{
}

std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>
KillOnCloseProcessJob::create(const std::size_t maximum_processes) noexcept
{
    try {
        if (maximum_processes == 0U || maximum_processes > 16U) {
            return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
                KillOnCloseProcessJob{},
                OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::invalid_specification,
                    ERROR_INVALID_PARAMETER, 0U}};
        }
        auto impl = std::make_unique<Impl>();
        impl->job = UniqueHandle{::CreateJobObjectW(nullptr, nullptr)};
        if (!impl->job) {
            return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
                KillOnCloseProcessJob{},
                OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::job_creation_failed,
                    ::GetLastError(), 0U}};
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
            JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
            JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit =
            static_cast<DWORD>(maximum_processes);
        if (!::SetInformationJobObject(
                impl->job.get(), JobObjectExtendedLimitInformation,
                &limits, sizeof(limits))) {
            return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
                KillOnCloseProcessJob{},
                OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::job_limit_failed,
                    ::GetLastError(), 0U}};
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION observed{};
        if (!::QueryInformationJobObject(
                impl->job.get(), JobObjectExtendedLimitInformation,
                &observed, sizeof(observed), nullptr) ||
            (observed.BasicLimitInformation.LimitFlags &
             JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) == 0U ||
            observed.BasicLimitInformation.ActiveProcessLimit !=
                maximum_processes) {
            return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
                KillOnCloseProcessJob{},
                OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::job_limit_failed,
                    ERROR_INVALID_DATA, 0U}};
        }
        impl->maximum_processes = maximum_processes;
        return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
            KillOnCloseProcessJob{std::move(impl)}, OwnedProcessLaunchResult{}};
    } catch (...) {
        return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
            KillOnCloseProcessJob{},
            OwnedProcessLaunchResult{
                OwnedProcessErrorCode::job_creation_failed,
                ERROR_NOT_ENOUGH_MEMORY, 0U}};
    }
}

std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>
KillOnCloseProcessJob::adopt_inherited(
    void* const job_handle,
    const std::size_t maximum_processes) noexcept
{
    try {
        if (job_handle == nullptr || job_handle == INVALID_HANDLE_VALUE ||
            maximum_processes == 0U || maximum_processes > 16U) {
            return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
                KillOnCloseProcessJob{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::invalid_specification,
                    ERROR_INVALID_PARAMETER, 0U}};
        }
        DWORD handle_flags = 0U;
        if (::GetHandleInformation(
                static_cast<HANDLE>(job_handle), &handle_flags) == FALSE ||
            (handle_flags & HANDLE_FLAG_INHERIT) == 0U) {
            return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
                KillOnCloseProcessJob{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::invalid_specification,
                    ::GetLastError(), 0U}};
        }
        auto impl = std::make_unique<Impl>();
        impl->job = UniqueHandle{static_cast<HANDLE>(job_handle)};
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        if (::QueryInformationJobObject(
                impl->job.get(), JobObjectBasicAccountingInformation,
                &accounting, sizeof(accounting), nullptr) == FALSE) {
            return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
                KillOnCloseProcessJob{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::job_creation_failed,
                    ::GetLastError(), 0U}};
        }
        if (accounting.ActiveProcesses != 0U) {
            return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
                KillOnCloseProcessJob{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::preexisting_process,
                    ERROR_BUSY, 0U}};
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
            JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
            JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit =
            static_cast<DWORD>(maximum_processes);
        if (::SetInformationJobObject(
                impl->job.get(), JobObjectExtendedLimitInformation,
                &limits, sizeof(limits)) == FALSE) {
            return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
                KillOnCloseProcessJob{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::job_limit_failed,
                    ::GetLastError(), 0U}};
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION observed{};
        if (::QueryInformationJobObject(
                impl->job.get(), JobObjectExtendedLimitInformation,
                &observed, sizeof(observed), nullptr) == FALSE ||
            (observed.BasicLimitInformation.LimitFlags &
             JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) == 0U ||
            observed.BasicLimitInformation.ActiveProcessLimit !=
                maximum_processes) {
            return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
                KillOnCloseProcessJob{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::job_limit_failed,
                    ERROR_INVALID_DATA, 0U}};
        }
        impl->maximum_processes = maximum_processes;
        return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
            KillOnCloseProcessJob{std::move(impl)}, OwnedProcessLaunchResult{}};
    } catch (...) {
        return std::pair<KillOnCloseProcessJob, OwnedProcessLaunchResult>{
            KillOnCloseProcessJob{}, OwnedProcessLaunchResult{
                OwnedProcessErrorCode::job_creation_failed,
                ERROR_NOT_ENOUGH_MEMORY, 0U}};
    }
}

bool KillOnCloseProcessJob::valid() const noexcept
{
    return impl_ != nullptr && static_cast<bool>(impl_->job);
}

std::optional<std::size_t>
KillOnCloseProcessJob::active_process_count() const noexcept
{
    if (!valid()) {
        return std::nullopt;
    }
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    if (!::QueryInformationJobObject(
            impl_->job.get(), JobObjectBasicAccountingInformation,
            &accounting, sizeof(accounting), nullptr)) {
        return std::nullopt;
    }
    return accounting.ActiveProcesses;
}

std::pair<OwnedProcess, OwnedProcessLaunchResult>
KillOnCloseProcessJob::launch(const OwnedProcessLaunchSpec& spec) noexcept
{
    try {
        if (!valid() || impl_->failed_child_process || spec.executable.empty() ||
            !spec.executable.is_absolute() || spec.working_directory.empty() ||
            !spec.working_directory.is_absolute() ||
            !path_equal(spec.executable, spec.expected_identity.canonical_path) ||
            spec.expected_identity.snapshot.size == 0U ||
            spec.arguments.size() > 128U) {
            return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
                OwnedProcess{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::invalid_specification,
                    ERROR_INVALID_PARAMETER, 0U}};
        }
        auto command = build_windows_command_line(spec.executable, spec.arguments);
        if (command.size() + 1U > 32'767U) {
            return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
                OwnedProcess{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::command_line_too_long,
                    ERROR_FILENAME_EXCED_RANGE, 0U}};
        }
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');

        SECURITY_ATTRIBUTES null_security{};
        null_security.nLength = sizeof(null_security);
        null_security.bInheritHandle = TRUE;
        UniqueHandle null_input{::CreateFileW(
            L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &null_security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (!null_input) {
            return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
                OwnedProcess{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::startup_attribute_failed,
                    ::GetLastError(), 0U}};
        }
        std::vector<HANDLE> inherited{null_input.get()};
        if (spec.stdout_handle != nullptr &&
            spec.stdout_handle != INVALID_HANDLE_VALUE) {
            inherited.push_back(static_cast<HANDLE>(spec.stdout_handle));
        }
        if (spec.stderr_handle != nullptr &&
            spec.stderr_handle != INVALID_HANDLE_VALUE &&
            spec.stderr_handle != spec.stdout_handle) {
            inherited.push_back(static_cast<HANDLE>(spec.stderr_handle));
        }
        for (void* raw : spec.additional_inherited_handles) {
            if (raw == nullptr || raw == INVALID_HANDLE_VALUE) {
                return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
                    OwnedProcess{}, OwnedProcessLaunchResult{
                        OwnedProcessErrorCode::invalid_specification,
                        ERROR_INVALID_HANDLE, 0U}};
            }
            const auto handle = static_cast<HANDLE>(raw);
            if (std::ranges::find(inherited, handle) == inherited.end()) {
                inherited.push_back(handle);
            }
        }
        for (const HANDLE handle : inherited) {
            DWORD flags = 0U;
            if (!::GetHandleInformation(handle, &flags) ||
                (flags & HANDLE_FLAG_INHERIT) == 0U) {
                return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
                    OwnedProcess{}, OwnedProcessLaunchResult{
                        OwnedProcessErrorCode::startup_attribute_failed,
                        ERROR_INVALID_HANDLE, 0U}};
            }
        }
        const DWORD attribute_count =
            static_cast<DWORD>((spec.prohibit_child_processes ? 1U : 0U) +
                               (!inherited.empty() ? 1U : 0U) + 1U);
        SIZE_T attribute_bytes = 0U;
        static_cast<void>(::InitializeProcThreadAttributeList(
            nullptr, attribute_count, 0U, &attribute_bytes));
        if (attribute_bytes == 0U || attribute_bytes > 1U * 1'024U * 1'024U) {
            return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
                OwnedProcess{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::startup_attribute_failed,
                    ::GetLastError(), 0U}};
        }
        std::vector<std::byte> attribute_storage(attribute_bytes);
        auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            attribute_storage.data());
        if (!::InitializeProcThreadAttributeList(
                attributes, attribute_count, 0U, &attribute_bytes)) {
            return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
                OwnedProcess{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::startup_attribute_failed,
                    ::GetLastError(), 0U}};
        }
        struct AttributeCleanup final {
            LPPROC_THREAD_ATTRIBUTE_LIST list;
            ~AttributeCleanup() { ::DeleteProcThreadAttributeList(list); }
        } cleanup{attributes};
        DWORD child_policy = PROCESS_CREATION_CHILD_PROCESS_RESTRICTED;
        if (spec.prohibit_child_processes &&
            !::UpdateProcThreadAttribute(
                attributes, 0U, PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY,
                &child_policy, sizeof(child_policy), nullptr, nullptr)) {
            return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
                OwnedProcess{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::startup_attribute_failed,
                    ::GetLastError(), 0U}};
        }
        if (!inherited.empty() &&
            !::UpdateProcThreadAttribute(
                attributes, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                inherited.data(), inherited.size() * sizeof(HANDLE),
                nullptr, nullptr)) {
            return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
                OwnedProcess{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::startup_attribute_failed,
                    ::GetLastError(), 0U}};
        }
        HANDLE launch_job = impl_->job.get();
        if (!::UpdateProcThreadAttribute(
                attributes, 0U, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                &launch_job, sizeof(launch_job), nullptr, nullptr)) {
            return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
                OwnedProcess{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::startup_attribute_failed,
                    ::GetLastError(), 0U}};
        }

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.lpAttributeList = attributes;
        if (spec.stdout_handle != nullptr || spec.stderr_handle != nullptr) {
            startup.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
            startup.StartupInfo.hStdOutput =
                spec.stdout_handle != nullptr
                    ? static_cast<HANDLE>(spec.stdout_handle)
                    : static_cast<HANDLE>(spec.stderr_handle);
            startup.StartupInfo.hStdError =
                spec.stderr_handle != nullptr
                    ? static_cast<HANDLE>(spec.stderr_handle)
                    : startup.StartupInfo.hStdOutput;
            startup.StartupInfo.hStdInput = null_input.get();
        }

        // Allocate the returned owner before creating any OS process. Once an
        // exact suspended child exists, every remaining failure path is
        // allocation-free and converges through fail_child.
        auto child = std::make_unique<OwnedProcess::Impl>();
        PROCESS_INFORMATION information{};
        DWORD flags = CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT;
        if (spec.create_no_window) {
            flags |= CREATE_NO_WINDOW;
        }
        if (!::CreateProcessW(
                spec.executable.c_str(), mutable_command.data(), nullptr, nullptr,
                TRUE, flags, nullptr,
                spec.working_directory.c_str(), &startup.StartupInfo,
                &information)) {
            return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
                OwnedProcess{}, OwnedProcessLaunchResult{
                    OwnedProcessErrorCode::create_process_failed,
                    ::GetLastError(), 0U}};
        }
        UniqueHandle process{information.hProcess};
        UniqueHandle thread{information.hThread};
        const auto fail_child = [&](const OwnedProcessErrorCode code,
                                    const DWORD native) noexcept {
            const BOOL termination_requested =
                ::TerminateProcess(process.get(), 126U);
            const DWORD termination_error = termination_requested != FALSE
                ? ERROR_SUCCESS
                : ::GetLastError();
            const DWORD wait_result =
                ::WaitForSingleObject(process.get(), 2'000U);
            if (wait_result != WAIT_OBJECT_0) {
                const DWORD cleanup_error = wait_result == WAIT_FAILED
                    ? ::GetLastError()
                    : termination_error != ERROR_SUCCESS
                        ? termination_error
                        : WAIT_TIMEOUT;
                impl_->failed_child_process = std::move(process);
                impl_->failed_child_process_id = information.dwProcessId;
                return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
                    OwnedProcess{}, OwnedProcessLaunchResult{
                        OwnedProcessErrorCode::child_cleanup_unconfirmed,
                        cleanup_error, information.dwProcessId}};
            }
            return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
                OwnedProcess{}, OwnedProcessLaunchResult{
                    code, native, information.dwProcessId}};
        };
        BOOL assigned_to_job = FALSE;
        if (::IsProcessInJob(
                process.get(), impl_->job.get(), &assigned_to_job) == FALSE ||
            assigned_to_job == FALSE) {
            return fail_child(OwnedProcessErrorCode::assign_job_failed,
                              ::GetLastError());
        }
        const WindowsBinaryObservationPolicy observation_policy{
            spec.expected_identity.authenticode_valid
                ? AuthenticodePolicy::required
                : AuthenticodePolicy::not_required_for_project_owned_binary,
            spec.expected_identity.file_version.has_value()};
        const auto verified = verify_windows_process_image_identity(
            process.get(), spec.expected_identity,
            kMaximumObservedExecutableBytes, observation_policy);
        if (!verified) {
            return fail_child(OwnedProcessErrorCode::process_identity_mismatch,
                              verified.native_error);
        }
        const DWORD previous_suspend_count = ::ResumeThread(thread.get());
        if (previous_suspend_count != 1U) {
            return fail_child(OwnedProcessErrorCode::resume_failed,
                              previous_suspend_count == static_cast<DWORD>(-1)
                                  ? ::GetLastError()
                                  : ERROR_INVALID_DATA);
        }
        thread.reset();
        child->process = std::move(process);
        child->process_id = information.dwProcessId;
        return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
            OwnedProcess{std::move(child)}, OwnedProcessLaunchResult{
                OwnedProcessErrorCode::none, 0U, information.dwProcessId}};
    } catch (...) {
        return std::pair<OwnedProcess, OwnedProcessLaunchResult>{
            OwnedProcess{}, OwnedProcessLaunchResult{
                OwnedProcessErrorCode::create_process_failed,
                ERROR_NOT_ENOUGH_MEMORY, 0U}};
    }
}

void KillOnCloseProcessJob::terminate(const std::uint32_t exit_code) noexcept
{
    if (valid()) {
        static_cast<void>(::TerminateJobObject(impl_->job.get(), exit_code));
    }
}

OwnedJobCleanupResult KillOnCloseProcessJob::terminate_and_wait(
    const std::uint32_t exit_code,
    const std::chrono::milliseconds timeout) noexcept
{
    if (!valid() || timeout.count() < 0) {
        return {OwnedJobCleanupErrorCode::invalid_job,
                ERROR_INVALID_HANDLE, 0U};
    }
    DWORD failed_child_error = ERROR_SUCCESS;
    if (impl_->failed_child_process) {
        const DWORD child_wait = ::WaitForSingleObject(
            impl_->failed_child_process.get(), 0U);
        if (child_wait == WAIT_FAILED) {
            return {OwnedJobCleanupErrorCode::failed_child_cleanup_failed,
                    ::GetLastError(), 1U};
        }
        if (child_wait == WAIT_TIMEOUT &&
            ::TerminateProcess(impl_->failed_child_process.get(), exit_code) ==
                FALSE) {
            failed_child_error = ::GetLastError();
        }
    }
    const auto initial_count = active_process_count();
    if (initial_count && *initial_count == 0U &&
        !impl_->failed_child_process) {
        return {};
    }
    if ((!initial_count || *initial_count != 0U) &&
        ::TerminateJobObject(impl_->job.get(), exit_code) == FALSE) {
        return {OwnedJobCleanupErrorCode::terminate_failed,
                ::GetLastError(), initial_count.value_or(0U) +
                    (impl_->failed_child_process ? 1U : 0U)};
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        std::size_t failed_child_count = 0U;
        if (impl_->failed_child_process) {
            const DWORD child_wait = ::WaitForSingleObject(
                impl_->failed_child_process.get(), 0U);
            if (child_wait == WAIT_OBJECT_0) {
                impl_->failed_child_process.reset();
                impl_->failed_child_process_id = 0U;
            } else if (child_wait == WAIT_FAILED) {
                return {
                    OwnedJobCleanupErrorCode::failed_child_cleanup_failed,
                    ::GetLastError(), 1U};
            } else {
                failed_child_count = 1U;
            }
        }
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        if (::QueryInformationJobObject(
                impl_->job.get(), JobObjectBasicAccountingInformation,
                &accounting, sizeof(accounting), nullptr)) {
            if (accounting.ActiveProcesses == 0U &&
                failed_child_count == 0U) {
                return {};
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                if (failed_child_count != 0U &&
                    failed_child_error != ERROR_SUCCESS) {
                    return {
                        OwnedJobCleanupErrorCode::failed_child_cleanup_failed,
                        failed_child_error,
                        accounting.ActiveProcesses + failed_child_count};
                }
                return {OwnedJobCleanupErrorCode::timeout, 0U,
                        accounting.ActiveProcesses + failed_child_count};
            }
        } else {
            const auto query_error = ::GetLastError();
            if (std::chrono::steady_clock::now() >= deadline) {
                return {OwnedJobCleanupErrorCode::accounting_query_failed,
                        query_error, failed_child_count};
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}

void KillOnCloseProcessJob::close() noexcept
{
    impl_.reset();
}

bool stock_runtime_graceful_cleanup_is_exact(
    const std::optional<std::uint32_t>& client_exit,
    const std::optional<std::uint32_t>& server_exit,
    const std::optional<std::uint32_t>& relay_exit,
    const std::optional<std::uint32_t>& guard_exit,
    const std::optional<std::size_t>& active_process_count) noexcept
{
    return client_exit && *client_exit == 0U &&
           server_exit && *server_exit == 0U &&
           relay_exit && *relay_exit == 0U &&
           guard_exit && *guard_exit == 0U &&
           active_process_count && *active_process_count == 0U;
}

bool stock_runtime_stock_process_shutdown_confirmed(
    const std::optional<std::uint32_t>& client_exit,
    const std::optional<std::uint32_t>& server_exit) noexcept
{
    return client_exit && *client_exit == 0U &&
           server_exit && *server_exit == 0U;
}

bool stock_runtime_owned_jobs_allow_isolation_release(
    const OwnedJobCleanupResult& campaign_cleanup,
    const std::optional<OwnedJobCleanupResult>& guard_cleanup) noexcept
{
    return static_cast<bool>(campaign_cleanup) && guard_cleanup &&
           static_cast<bool>(*guard_cleanup);
}

ExactImageProcessScanResult find_processes_with_exact_image_identity(
    const WindowsBinaryIdentity& identity) noexcept
{
    ExactImageProcessScanResult result;
    try {
        const auto target_name = identity.canonical_path.filename().wstring();
        if (target_name.empty() || target_name.size() >
                static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
            result.code = ExactImageProcessScanErrorCode::enumeration_failed;
            result.native_error = ERROR_INVALID_PARAMETER;
            return result;
        }
        UniqueHandle snapshot{::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0U)};
        if (!snapshot) {
            result.code = ExactImageProcessScanErrorCode::snapshot_failed;
            result.native_error = ::GetLastError();
            return result;
        }
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (!::Process32FirstW(snapshot.get(), &entry)) {
            const auto native = ::GetLastError();
            if (native != ERROR_NO_MORE_FILES) {
                result.code = ExactImageProcessScanErrorCode::enumeration_failed;
                result.native_error = native;
            }
            return result;
        }
        for (;;) {
            const int name_comparison = ::CompareStringOrdinal(
                entry.szExeFile, -1, target_name.c_str(),
                static_cast<int>(target_name.size()), TRUE);
            if (name_comparison == 0) {
                result.code = ExactImageProcessScanErrorCode::enumeration_failed;
                result.native_error = ::GetLastError();
                result.process_id = entry.th32ProcessID;
                return result;
            }
            if (name_comparison != CSTR_EQUAL) {
                if (::Process32NextW(snapshot.get(), &entry)) {
                    continue;
                }
                const auto native = ::GetLastError();
                if (native != ERROR_NO_MORE_FILES) {
                    result.code =
                        ExactImageProcessScanErrorCode::enumeration_failed;
                    result.native_error = native;
                }
                break;
            }
            UniqueHandle process{::OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
            if (!process) {
                result.code = ExactImageProcessScanErrorCode::process_open_failed;
                result.native_error = ::GetLastError();
                result.process_id = entry.th32ProcessID;
                return result;
            }
            std::wstring path(32'768U, L'\0');
            DWORD size = static_cast<DWORD>(path.size());
            if (!::QueryFullProcessImageNameW(
                    process.get(), 0U, path.data(), &size) || size == 0U ||
                size >= path.size()) {
                result.code =
                    ExactImageProcessScanErrorCode::process_path_query_failed;
                result.native_error = ::GetLastError();
                result.process_id = entry.th32ProcessID;
                return result;
            }
            path.resize(size);
            if (!path_equal(path, identity.canonical_path)) {
                if (::Process32NextW(snapshot.get(), &entry)) {
                    continue;
                }
                const auto native = ::GetLastError();
                if (native != ERROR_NO_MORE_FILES) {
                    result.code =
                        ExactImageProcessScanErrorCode::enumeration_failed;
                    result.native_error = native;
                }
                break;
            }
            const WindowsBinaryObservationPolicy observation_policy{
                identity.authenticode_valid
                    ? AuthenticodePolicy::required
                    : AuthenticodePolicy::not_required_for_project_owned_binary,
                identity.file_version.has_value()};
            const auto observed = verify_windows_process_image_identity(
                process.get(), identity, kMaximumObservedExecutableBytes,
                observation_policy);
            if (!observed) {
                result.code =
                    ExactImageProcessScanErrorCode::process_identity_query_failed;
                result.native_error = observed.native_error;
                result.process_id = entry.th32ProcessID;
                return result;
            }
            result.process_ids.push_back(entry.th32ProcessID);
            if (!::Process32NextW(snapshot.get(), &entry)) {
                const auto native = ::GetLastError();
                if (native != ERROR_NO_MORE_FILES) {
                    result.code =
                        ExactImageProcessScanErrorCode::enumeration_failed;
                    result.native_error = native;
                }
                break;
            }
        }
        std::ranges::sort(result.process_ids);
    } catch (...) {
        result.process_ids.clear();
        result.code = ExactImageProcessScanErrorCode::enumeration_failed;
        result.native_error = ERROR_NOT_ENOUGH_MEMORY;
    }
    return result;
}

HldsBannerParseResult parse_required_hlds_runtime_banner(
    const std::string_view output,
    const std::string_view requested_map,
    const std::uint16_t requested_port) noexcept
{
    try {
        if (output.size() > 1U * 1'024U * 1'024U ||
            !safe_profile_token(requested_map) || requested_port == 0U) {
            return {std::nullopt, HldsBannerParseErrorCode::too_large};
        }
        HldsRuntimeProfile profile;
        std::size_t engine_count = 0U;
        std::size_t protocol_count = 0U;
        std::size_t build_count = 0U;
        std::size_t endpoint_count = 0U;
        std::size_t map_count = 0U;
        for (const auto line : split_lines(output)) {
            if (line.size() > 4'096U) {
                return {std::nullopt, HldsBannerParseErrorCode::line_too_long};
            }
            if (line.starts_with("Exe version ")) {
                ++engine_count;
                constexpr std::string_view expected =
                    "Exe version 1.1.2.2/Stdio (valve)";
                if (line != expected) {
                    return {std::nullopt,
                            HldsBannerParseErrorCode::profile_mismatch};
                }
                profile.engine_version = {1U, 1U, 2U, 2U};
                profile.game = "valve";
            } else if (line.starts_with("Protocol version ")) {
                ++protocol_count;
                if (!parse_decimal(line.substr(std::string_view{"Protocol version "}.size()),
                                   profile.protocol)) {
                    return {std::nullopt, HldsBannerParseErrorCode::malformed};
                }
            } else if (line.starts_with("Exe build:")) {
                ++build_count;
                const auto open = line.rfind('(');
                if (open == std::string_view::npos || line.back() != ')' ||
                    !parse_decimal(line.substr(open + 1U,
                                               line.size() - open - 2U),
                                   profile.build)) {
                    return {std::nullopt, HldsBannerParseErrorCode::malformed};
                }
            } else if (line.starts_with("Server IP address ")) {
                ++endpoint_count;
                const auto colon = line.rfind(':');
                unsigned int parsed_port = 0U;
                if (colon == std::string_view::npos ||
                    line.substr(std::string_view{"Server IP address "}.size(),
                                colon - std::string_view{"Server IP address "}.size()) !=
                        "127.0.0.1" ||
                    !parse_decimal(line.substr(colon + 1U), parsed_port) ||
                    parsed_port > 65'535U) {
                    return {std::nullopt, HldsBannerParseErrorCode::malformed};
                }
                profile.port = static_cast<std::uint16_t>(parsed_port);
            } else if (line.starts_with("map     : ")) {
                ++map_count;
                constexpr std::string_view prefix{"map     : "};
                const auto map_end = line.find(" at: ", prefix.size());
                if (map_end == std::string_view::npos ||
                    map_end == prefix.size()) {
                    return {std::nullopt,
                            HldsBannerParseErrorCode::malformed};
                }
                const auto map_name =
                    line.substr(prefix.size(), map_end - prefix.size());
                if (!safe_profile_token(map_name)) {
                    return {std::nullopt,
                            HldsBannerParseErrorCode::malformed};
                }
                profile.map = std::string{map_name};
            }
        }
        if (engine_count > 1U || protocol_count > 1U || build_count > 1U ||
            endpoint_count > 1U || map_count > 1U) {
            return {std::nullopt, HldsBannerParseErrorCode::duplicate_field};
        }
        if (engine_count != 1U || protocol_count != 1U || build_count != 1U ||
            endpoint_count != 1U || map_count != 1U) {
            return {std::nullopt, HldsBannerParseErrorCode::missing_field};
        }
        if (profile.protocol != 48U || profile.build != 10'210U ||
            profile.game != "valve" || profile.map != requested_map ||
            profile.port != requested_port) {
            return {std::nullopt, HldsBannerParseErrorCode::profile_mismatch};
        }
        profile.ready = true;
        return {std::move(profile), HldsBannerParseErrorCode::none};
    } catch (...) {
        return {std::nullopt, HldsBannerParseErrorCode::malformed};
    }
}

std::string_view to_string(const OwnedProcessErrorCode code) noexcept
{
    switch (code) {
    case OwnedProcessErrorCode::none: return "none";
    case OwnedProcessErrorCode::invalid_specification: return "invalid-specification";
    case OwnedProcessErrorCode::job_creation_failed: return "job-creation-failed";
    case OwnedProcessErrorCode::job_limit_failed: return "job-limit-failed";
    case OwnedProcessErrorCode::preexisting_process: return "preexisting-process";
    case OwnedProcessErrorCode::command_line_too_long: return "command-line-too-long";
    case OwnedProcessErrorCode::startup_attribute_failed: return "startup-attribute-failed";
    case OwnedProcessErrorCode::create_process_failed: return "create-process-failed";
    case OwnedProcessErrorCode::assign_job_failed: return "assign-job-failed";
    case OwnedProcessErrorCode::child_cleanup_unconfirmed:
        return "child-cleanup-unconfirmed";
    case OwnedProcessErrorCode::process_identity_mismatch: return "process-identity-mismatch";
    case OwnedProcessErrorCode::resume_failed: return "resume-failed";
    case OwnedProcessErrorCode::process_query_failed: return "process-query-failed";
    case OwnedProcessErrorCode::process_timeout: return "process-timeout";
    case OwnedProcessErrorCode::process_exit_unexpected: return "process-exit-unexpected";
    case OwnedProcessErrorCode::log_capture_failed: return "log-capture-failed";
    case OwnedProcessErrorCode::unexpected_child_process: return "unexpected-child-process";
    }
    return "unknown";
}

std::string_view to_string(const OwnedJobCleanupErrorCode code) noexcept
{
    switch (code) {
    case OwnedJobCleanupErrorCode::none: return "none";
    case OwnedJobCleanupErrorCode::invalid_job: return "invalid-job";
    case OwnedJobCleanupErrorCode::terminate_failed: return "terminate-failed";
    case OwnedJobCleanupErrorCode::failed_child_cleanup_failed:
        return "failed-child-cleanup-failed";
    case OwnedJobCleanupErrorCode::accounting_query_failed:
        return "accounting-query-failed";
    case OwnedJobCleanupErrorCode::timeout: return "timeout";
    }
    return "unknown";
}

std::string_view to_string(const ExactImageProcessScanErrorCode code) noexcept
{
    switch (code) {
    case ExactImageProcessScanErrorCode::none: return "none";
    case ExactImageProcessScanErrorCode::snapshot_failed:
        return "snapshot-failed";
    case ExactImageProcessScanErrorCode::enumeration_failed:
        return "enumeration-failed";
    case ExactImageProcessScanErrorCode::process_open_failed:
        return "process-open-failed";
    case ExactImageProcessScanErrorCode::process_path_query_failed:
        return "process-path-query-failed";
    case ExactImageProcessScanErrorCode::process_identity_query_failed:
        return "process-identity-query-failed";
    }
    return "unknown";
}

std::string_view to_string(const HldsBannerParseErrorCode code) noexcept
{
    switch (code) {
    case HldsBannerParseErrorCode::none: return "none";
    case HldsBannerParseErrorCode::too_large: return "too-large";
    case HldsBannerParseErrorCode::line_too_long: return "line-too-long";
    case HldsBannerParseErrorCode::duplicate_field: return "duplicate-field";
    case HldsBannerParseErrorCode::malformed: return "malformed";
    case HldsBannerParseErrorCode::missing_field: return "missing-field";
    case HldsBannerParseErrorCode::profile_mismatch: return "profile-mismatch";
    }
    return "unknown";
}

} // namespace hlclient::platform::windows
