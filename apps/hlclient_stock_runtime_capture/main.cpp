#include <hlclient/goldsrc/stock_runtime_capture.hpp>
#include <hlclient/goldsrc/stock_runtime_transport_journal.hpp>
#include <hlclient/hash/sha256.hpp>
#include <hlclient/network/network_address.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#    include <hlclient/platform/windows/binary_identity.hpp>
#    include <hlclient/platform/windows/secure_output.hpp>

#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <tlhelp32.h>
#endif

namespace {

namespace fs = std::filesystem;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;
#ifdef _WIN32
namespace windows_platform = hlclient::platform::windows;
#endif

constexpr std::string_view kUsage =
    "Usage: hlclient_stock_runtime_capture --validate-config [limit options]\n"
    "   or: hlclient_stock_runtime_capture --listen-port <port> "
    "--server-port <port> --output-run-root <ignored run directory> "
    "--scenario <name> [limit options] "
    "--private-ipv4-loopback-only --one-upstream-socket "
    "--byte-preserving --no-payload-rewrite "
    "--precreated-empty-run-root "
    "--stop-handle <inherited event handle> "
    "--orchestrator-capability-handle <inherited event handle> "
    "--orchestrator-process-id <pid>\n";

struct Options final {
    bool validate_config{false};
    std::optional<std::uint16_t> listen_port;
    std::optional<std::uint16_t> server_port;
    std::optional<fs::path> output_run_root;
    std::optional<goldsrc::StockRuntimeCaptureScenario> scenario;
    goldsrc::StockRuntimeCaptureLimits limits{};
    goldsrc::StockRuntimeCapturePerturbation perturbation{};
    bool private_loopback_only{false};
    bool one_upstream_socket{false};
    bool byte_preserving{false};
    bool no_payload_rewrite{false};
    bool precreated_empty_run_root{false};
#ifdef _WIN32
    HANDLE stop_handle{nullptr};
    HANDLE orchestrator_capability_handle{nullptr};
#else
    std::uintptr_t stop_handle{0U};
    std::uintptr_t orchestrator_capability_handle{0U};
#endif
    std::uint32_t orchestrator_process_id{0U};
};

template<typename Integer>
[[nodiscard]] bool parse_integer(
    const std::string_view text,
    Integer& value) noexcept
{
    if (text.empty()) {
        return false;
    }
    Integer parsed{};
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), parsed, 10);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    value = parsed;
    return true;
}

[[nodiscard]] std::optional<Options> parse_options(
    const std::span<const std::string_view> arguments)
{
    Options options;
    std::array<bool, 25U> seen{};
    const auto mark = [&seen](const std::size_t index) {
        if (seen[index]) {
            return false;
        }
        seen[index] = true;
        return true;
    };
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (argument == "--validate-config") {
            if (!mark(0U)) return std::nullopt;
            options.validate_config = true;
            continue;
        }
        if (argument == "--private-ipv4-loopback-only" ||
            argument == "--one-upstream-socket" ||
            argument == "--byte-preserving" ||
            argument == "--no-payload-rewrite" ||
            argument == "--precreated-empty-run-root") {
            const std::size_t seen_index = argument == "--private-ipv4-loopback-only" ? 1U
                : argument == "--one-upstream-socket" ? 2U
                : argument == "--byte-preserving" ? 3U
                : argument == "--no-payload-rewrite" ? 4U
                                                        : 22U;
            if (!mark(seen_index)) return std::nullopt;
            if (seen_index == 1U) options.private_loopback_only = true;
            if (seen_index == 2U) options.one_upstream_socket = true;
            if (seen_index == 3U) options.byte_preserving = true;
            if (seen_index == 4U) options.no_payload_rewrite = true;
            if (seen_index == 22U) options.precreated_empty_run_root = true;
            continue;
        }
        if (index + 1U >= arguments.size()) {
            return std::nullopt;
        }
        const auto value = arguments[++index];
        std::size_t option_index = 0U;
        if (argument == "--listen-port") option_index = 5U;
        else if (argument == "--server-port") option_index = 6U;
        else if (argument == "--output-run-root") option_index = 7U;
        else if (argument == "--scenario") option_index = 8U;
        else if (argument == "--max-duration-ms") option_index = 9U;
        else if (argument == "--max-datagrams") option_index = 10U;
        else if (argument == "--max-total-raw-bytes") option_index = 11U;
        else if (argument == "--max-payload-bytes") option_index = 12U;
        else if (argument == "--max-reassembled-bytes") option_index = 13U;
        else if (argument == "--max-decompressed-bytes") option_index = 14U;
        else if (argument == "--max-message-count") option_index = 15U;
        else if (argument == "--max-runtime-frames") option_index = 16U;
        else if (argument == "--max-client-packets") option_index = 17U;
        else if (argument == "--max-server-packets") option_index = 18U;
        else if (argument == "--mutation-after-client-packets") option_index = 19U;
        else if (argument == "--mutation-after-server-packets") option_index = 20U;
        else if (argument == "--stop-handle") option_index = 21U;
        else if (argument == "--orchestrator-capability-handle") option_index = 23U;
        else if (argument == "--orchestrator-process-id") option_index = 24U;
        else return std::nullopt;

        if (!mark(option_index)) return std::nullopt;

        if (argument == "--listen-port" || argument == "--server-port") {
            unsigned int parsed{};
            if (!parse_integer(value, parsed) || parsed < 1024U || parsed > 65'534U) {
                return std::nullopt;
            }
            const auto port = static_cast<std::uint16_t>(parsed);
            if (argument == "--listen-port") options.listen_port = port;
            else options.server_port = port;
        } else if (argument == "--output-run-root") {
            if (value.empty() || value.find('\0') != std::string_view::npos) {
                return std::nullopt;
            }
            options.output_run_root = fs::path{std::string{value}};
        } else if (argument == "--scenario") {
            options.scenario = goldsrc::parse_stock_runtime_capture_scenario(value);
            if (!options.scenario) return std::nullopt;
        } else if (argument == "--max-duration-ms") {
            std::int64_t parsed{};
            if (!parse_integer(value, parsed)) return std::nullopt;
            options.limits.maximum_duration = std::chrono::milliseconds{parsed};
        } else if (argument == "--max-total-raw-bytes") {
            if (!parse_integer(value, options.limits.maximum_total_raw_bytes)) {
                return std::nullopt;
            }
        } else if (argument == "--stop-handle" ||
                   argument == "--orchestrator-capability-handle") {
            std::uintptr_t parsed{};
            if (!parse_integer(value, parsed) || parsed == 0U) {
                return std::nullopt;
            }
#ifdef _WIN32
            if (argument == "--stop-handle") {
                options.stop_handle = reinterpret_cast<HANDLE>(parsed);
            } else {
                options.orchestrator_capability_handle =
                    reinterpret_cast<HANDLE>(parsed);
            }
#else
            if (argument == "--stop-handle") {
                options.stop_handle = parsed;
            } else {
                options.orchestrator_capability_handle = parsed;
            }
#endif
        } else if (argument == "--orchestrator-process-id") {
            if (!parse_integer(value, options.orchestrator_process_id) ||
                options.orchestrator_process_id == 0U) {
                return std::nullopt;
            }
        } else {
            std::size_t parsed{};
            if (!parse_integer(value, parsed)) return std::nullopt;
            if (argument == "--max-datagrams") options.limits.maximum_datagrams = parsed;
            else if (argument == "--max-payload-bytes") options.limits.maximum_payload_bytes = parsed;
            else if (argument == "--max-reassembled-bytes") options.limits.maximum_reassembled_bytes = parsed;
            else if (argument == "--max-decompressed-bytes") options.limits.maximum_decompressed_bytes = parsed;
            else if (argument == "--max-message-count") options.limits.maximum_message_count = parsed;
            else if (argument == "--max-runtime-frames") options.limits.maximum_runtime_frames = parsed;
            else if (argument == "--max-client-packets") options.limits.maximum_client_packets = parsed;
            else if (argument == "--max-server-packets") options.limits.maximum_server_packets = parsed;
            else if (argument == "--mutation-after-client-packets") options.perturbation.client_packet_ordinal = parsed;
            else if (argument == "--mutation-after-server-packets") options.perturbation.server_packet_ordinal = parsed;
        }
    }

    if (!goldsrc::validate_stock_runtime_capture_limits(options.limits) ||
        options.perturbation.client_packet_ordinal == 0U ||
        options.perturbation.server_packet_ordinal == 0U ||
        options.perturbation.client_packet_ordinal > options.limits.maximum_client_packets ||
        options.perturbation.server_packet_ordinal > options.limits.maximum_server_packets) {
        return std::nullopt;
    }
    if (options.validate_config) {
        return options.listen_port || options.server_port || options.output_run_root ||
                       options.scenario || options.private_loopback_only ||
                        options.one_upstream_socket || options.byte_preserving ||
                        options.no_payload_rewrite ||
                        options.precreated_empty_run_root ||
                        options.orchestrator_process_id != 0U ||
#ifdef _WIN32
                       options.stop_handle != nullptr ||
                        options.orchestrator_capability_handle != nullptr
#else
                       options.stop_handle != 0U ||
                        options.orchestrator_capability_handle != 0U
#endif
            ? std::nullopt
            : std::optional<Options>{std::move(options)};
    }
    if (!options.listen_port || !options.server_port ||
        *options.listen_port == *options.server_port || !options.output_run_root ||
        !options.scenario || !options.private_loopback_only ||
        !options.one_upstream_socket || !options.byte_preserving ||
        !options.no_payload_rewrite || !options.precreated_empty_run_root) {
        return std::nullopt;
    }
#ifdef _WIN32
    if (options.stop_handle == nullptr ||
        options.orchestrator_capability_handle == nullptr ||
        options.stop_handle == options.orchestrator_capability_handle ||
        options.orchestrator_process_id == 0U) {
        return std::nullopt;
    }
    DWORD stop_flags = 0U;
    DWORD capability_flags = 0U;
    if (!::GetHandleInformation(options.stop_handle, &stop_flags) ||
        !::GetHandleInformation(
            options.orchestrator_capability_handle, &capability_flags) ||
        (stop_flags & HANDLE_FLAG_INHERIT) == 0U ||
        (capability_flags & HANDLE_FLAG_INHERIT) == 0U) {
        return std::nullopt;
    }
#else
    // The active relay is a Windows-only child of the verified orchestrator.
    // Non-Windows builds retain only --validate-config.
    return std::nullopt;
#endif
    return options;
}

#ifdef _WIN32
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

[[nodiscard]] fs::path sibling_executable(const wchar_t* filename)
{
    std::wstring module(32'768U, L'\0');
    const DWORD size = ::GetModuleFileNameW(
        nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (size == 0U || size >= module.size()) return {};
    module.resize(size);
    return fs::path{std::move(module)}.parent_path() / filename;
}

[[nodiscard]] bool validate_orchestrator_capability(
    const Options& options,
    std::string& error) noexcept
{
    const auto observed_parent = current_parent_process_id();
    if (!observed_parent || *observed_parent != options.orchestrator_process_id ||
        options.orchestrator_process_id == ::GetCurrentProcessId()) {
        error = "capture parent is not the declared orchestrator";
        return false;
    }

    const auto expected_path = sibling_executable(
        L"hlclient_stock_runtime_orchestrator.exe");
    if (expected_path.empty()) {
        error = "orchestrator sibling path is unavailable";
        return false;
    }
    const windows_platform::WindowsBinaryObservationPolicy project_policy{
        windows_platform::AuthenticodePolicy::
            not_required_for_project_owned_binary,
        false};
    const auto expected = windows_platform::observe_windows_binary_identity(
        expected_path, windows_platform::kMaximumObservedExecutableBytes,
        project_policy);
    if (!expected || !expected.identity) {
        error = "orchestrator executable identity is invalid";
        return false;
    }

    const HANDLE parent = ::OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
        options.orchestrator_process_id);
    if (parent == nullptr) {
        error = "orchestrator process cannot be opened";
        return false;
    }
    const auto verified = windows_platform::verify_windows_process_image_identity(
        parent, *expected.identity,
        windows_platform::kMaximumObservedExecutableBytes, project_policy);
    DWORD exit_code = 0U;
    const bool parent_running = ::GetExitCodeProcess(parent, &exit_code) != FALSE &&
                                exit_code == STILL_ACTIVE;
    static_cast<void>(::CloseHandle(parent));
    if (!verified || !verified.identity || !parent_running) {
        error = "capture parent image identity is not the exact orchestrator";
        return false;
    }

    if (::WaitForSingleObject(options.stop_handle, 0U) != WAIT_TIMEOUT ||
        ::WaitForSingleObject(options.orchestrator_capability_handle, 0U) !=
            WAIT_TIMEOUT ||
        ::SetEvent(options.orchestrator_capability_handle) == FALSE ||
        ::WaitForSingleObject(options.orchestrator_capability_handle, 0U) !=
            WAIT_OBJECT_0) {
        error = "inherited orchestrator capability handles are invalid";
        return false;
    }
    return true;
}
#endif

[[nodiscard]] bool is_lower_hex_run_id(const std::string_view value) noexcept
{
    if (value.size() != 32U) return false;
    for (const auto character : value) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool has_symlink_component(const fs::path& path)
{
    fs::path current;
    for (const auto& component : path) {
        current /= component;
        std::error_code error;
        const auto status = fs::symlink_status(current, error);
        if (!error && fs::is_symlink(status)) return true;
    }
    return false;
}

[[nodiscard]] std::optional<fs::path> validate_output_run_root(
    const fs::path& requested,
    const bool precreated_empty_run_root,
    std::string& error)
{
    std::error_code path_error;
    const auto repository = fs::weakly_canonical(fs::current_path(), path_error);
    if (path_error) {
        error = "working directory cannot be canonicalized";
        return std::nullopt;
    }
    const auto approved = fs::weakly_canonical(
        repository / "manual-artifacts" / "stock-runtime", path_error);
    if (path_error || !fs::is_directory(approved, path_error) || path_error ||
        has_symlink_component(approved)) {
        error = "approved ignored stock-runtime root is absent or unsafe";
        return std::nullopt;
    }
    const auto full = fs::absolute(requested, path_error).lexically_normal();
    if (path_error || full.parent_path() != approved ||
        !is_lower_hex_run_id(full.filename().string())) {
        error = "output run root must be one GUID directory below the exact ignored root";
        return std::nullopt;
    }
    const bool exists = fs::exists(full, path_error);
    if (path_error || exists != precreated_empty_run_root) {
        error = precreated_empty_run_root
            ? "precreated output run root is absent"
            : "new output run root already exists";
        return std::nullopt;
    }
    if (precreated_empty_run_root) {
        if (!fs::is_directory(full, path_error) || path_error ||
            has_symlink_component(full) ||
            !fs::is_empty(full, path_error) || path_error) {
            error = "precreated output run root is not an empty ordinary directory";
            return std::nullopt;
        }
    }
    return full;
}

struct HeldOutputDirectory final {
    fs::path path;
#ifdef _WIN32
    windows_platform::SecureOutputDirectory capability;
#endif
};

[[nodiscard]] std::optional<HeldOutputDirectory> hold_output_directory(
    const fs::path& path,
    std::string& error)
{
#ifdef _WIN32
    auto opened = windows_platform::open_secure_output_directory(path);
    if (!opened || !opened.directory) {
        error = "secure output directory rejected";
        if (opened.error) {
            error += ": ";
            error += windows_platform::to_string(opened.error->code);
        }
        return std::nullopt;
    }
    return HeldOutputDirectory{path, std::move(*opened.directory)};
#else
    return HeldOutputDirectory{path};
#endif
}

[[nodiscard]] bool write_new_output_file(
    const HeldOutputDirectory& directory,
    const std::string_view leaf_name,
    const std::span<const std::byte> bytes,
    std::string& error)
{
#ifdef _WIN32
    std::wstring wide_leaf;
    try {
        wide_leaf.assign(leaf_name.begin(), leaf_name.end());
    } catch (...) {
        error = "secure output leaf allocation failed";
        return false;
    }
    const auto written = windows_platform::secure_atomic_write_new(
        directory.capability, wide_leaf, bytes);
    if (!written) {
        error = "secure output publication failed";
        if (written.error) {
            error += ": ";
            error += windows_platform::to_string(written.error->code);
        }
        return false;
    }
    return true;
#else
    const auto path = directory.path / leaf_name;
    const auto temporary = directory.path /
        (std::string{leaf_name} + ".non-active.tmp");
    std::error_code path_error;
    if (fs::exists(path, path_error) || path_error ||
        fs::exists(temporary, path_error) || path_error) {
        error = "output leaf already exists";
        return false;
    }
    std::ofstream stream{
        temporary, std::ios::binary | std::ios::out | std::ios::trunc};
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    if (!stream) {
        stream.close();
        static_cast<void>(fs::remove(temporary, path_error));
        error = "output temporary write failed";
        return false;
    }
    stream.close();
    fs::create_hard_link(temporary, path, path_error);
    if (path_error || !fs::remove(temporary, path_error) || path_error) {
        static_cast<void>(fs::remove(temporary, path_error));
        error = "output publication failed";
        return false;
    }
    return true;
#endif
}

[[nodiscard]] bool write_raw_datagram(
    const HeldOutputDirectory& raw_root,
    const std::string_view raw_filename,
    const std::span<const std::byte> payload,
    std::string& error)
{
    return write_new_output_file(raw_root, raw_filename, payload, error);
}

[[nodiscard]] std::string raw_datagram_filename(
    const std::size_t ordinal,
    const goldsrc::StockRuntimeCaptureDirection direction)
{
    std::ostringstream name;
    name << std::setfill('0') << std::setw(8) << ordinal
         << (direction == goldsrc::StockRuntimeCaptureDirection::client_to_server
                 ? "-c2s.bin"
                 : "-s2c.bin");
    return name.str();
}

[[nodiscard]] std::optional<std::string> payload_sha256(
    const std::span<const std::byte> payload) noexcept
{
    const auto digest = hlclient::hash::sha256(payload);
    return digest
        ? std::optional<std::string>{hlclient::hash::sha256_hex(*digest)}
        : std::nullopt;
}

[[nodiscard]] std::optional<goldsrc::StockRuntimeTransportJournalEntry>
make_journal_entry(
    const std::size_t observed_ordinal,
    const goldsrc::StockRuntimeCaptureDirection direction,
    const std::size_t direction_ordinal,
    const std::uint64_t relative_timestamp_us,
    const std::span<const std::byte> payload,
    const goldsrc::StockRuntimeCaptureAction action,
    const bool wrong_source)
{
    const auto digest = payload_sha256(payload);
    if (!digest) {
        return std::nullopt;
    }
    goldsrc::StockRuntimeTransportJournalEntry entry;
    entry.observed_ordinal = observed_ordinal;
    entry.direction = direction;
    entry.direction_ordinal = direction_ordinal;
    entry.relative_timestamp_us = relative_timestamp_us;
    entry.payload_byte_count = payload.size();
    entry.raw_filename = raw_datagram_filename(observed_ordinal, direction);
    entry.source_role = wrong_source
        ? goldsrc::StockRuntimeTransportRole::unexpected_source
        : direction == goldsrc::StockRuntimeCaptureDirection::client_to_server
            ? goldsrc::StockRuntimeTransportRole::research_client
            : goldsrc::StockRuntimeTransportRole::research_server;
    entry.destination_role = direction ==
            goldsrc::StockRuntimeCaptureDirection::client_to_server
        ? goldsrc::StockRuntimeTransportRole::research_server
        : goldsrc::StockRuntimeTransportRole::research_client;
    entry.action = action;
    entry.hold_state = goldsrc::StockRuntimeTransportHoldState::none;
    entry.emitted_ordinals.reserve(
        goldsrc::kMaximumStockRuntimeJournalEmissionsPerEntry);
    entry.delivered = false;
    entry.wrong_source = wrong_source;
    entry.sha256 = *digest;
    return entry;
}

[[nodiscard]] std::uint64_t bounded_relative_timestamp_us(
    const std::chrono::steady_clock::time_point start,
    const std::uint64_t maximum) noexcept
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (elapsed <= 0) {
        return 0U;
    }
    return (std::min)(static_cast<std::uint64_t>(elapsed), maximum);
}

[[nodiscard]] bool write_metadata(
    const HeldOutputDirectory& run_root,
    const goldsrc::StockRuntimeCaptureMetadata& metadata,
    std::string& error)
{
    const auto json = goldsrc::serialize_stock_runtime_capture_metadata(metadata);
    return write_new_output_file(
        run_root, "capture-metadata.json",
        std::as_bytes(std::span{json.data(), json.size()}), error);
}

[[nodiscard]] bool write_transport_journal(
    const HeldOutputDirectory& run_root,
    const std::span<const goldsrc::StockRuntimeTransportJournalEntry> entries,
    const goldsrc::StockRuntimeCaptureLimits& capture_limits,
    const bool complete_capture,
    std::string& error)
{
    goldsrc::StockRuntimeTransportJournalLimits limits;
    limits.maximum_entries = capture_limits.maximum_datagrams;
    limits.maximum_payload_bytes = capture_limits.maximum_payload_bytes;
    limits.maximum_total_raw_bytes = capture_limits.maximum_total_raw_bytes;
    limits.maximum_emitted_datagrams = capture_limits.maximum_datagrams * 2U;
    limits.maximum_relative_timestamp_us = static_cast<std::uint64_t>(
        capture_limits.maximum_duration.count()) * 1'000U;
    const auto validation = goldsrc::validate_stock_runtime_transport_journal(
        entries, limits,
        complete_capture
            ? goldsrc::StockRuntimeTransportJournalValidationPolicy::complete_capture
            : goldsrc::StockRuntimeTransportJournalValidationPolicy::incomplete_capture);
    if (!validation || validation.transport_complete != complete_capture) {
        error = validation.error
            ? "transport journal validation failed: " +
                  std::string{goldsrc::to_string(validation.error->code)}
            : "transport journal completeness mismatch";
        return false;
    }

    constexpr std::size_t maximum_journal_bytes = 64U * 1'024U * 1'024U;
    std::string journal_bytes;
    std::size_t written = 0U;
    try {
        journal_bytes.reserve((std::min)(
            maximum_journal_bytes,
            entries.size() * goldsrc::kMaximumStockRuntimeJournalLineBytes));
    } catch (...) {
        error = "transport journal allocation failed";
        return false;
    }
    for (const auto& entry : entries) {
        const auto line =
            goldsrc::serialize_stock_runtime_transport_journal_entry(entry);
        if (line.empty() ||
            line.size() > goldsrc::kMaximumStockRuntimeJournalLineBytes ||
            written > maximum_journal_bytes - line.size() - 1U) {
            error = "transport journal bound exceeded";
            return false;
        }
        try {
            journal_bytes.append(line);
            journal_bytes.push_back('\n');
        } catch (...) {
            error = "transport journal allocation failed";
            return false;
        }
        written += line.size() + 1U;
    }
    return write_new_output_file(
        run_root, "transport-journal.jsonl",
        std::as_bytes(std::span{journal_bytes.data(), journal_bytes.size()}),
        error);
}

struct HeldDatagram final {
    std::vector<std::byte> payload;
    network::NetworkAddress destination;
    std::size_t journal_index{0U};
    bool reorder_on_release{false};
};

[[nodiscard]] bool emit_and_record(
    network::UdpSocket& socket,
    const network::NetworkAddress destination,
    const std::span<const std::byte> payload,
    goldsrc::StockRuntimeCaptureCounters& counters,
    goldsrc::StockRuntimeTransportJournalEntry& journal_entry,
    std::string& error)
{
    auto next = counters;
    if (!goldsrc::stock_runtime_capture_record_emission(next, payload.size())) {
        error = "emission counters overflowed";
        return false;
    }
    try {
        journal_entry.emitted_ordinals.push_back(counters.emitted_datagrams);
    } catch (...) {
        error = "journal emission allocation failed";
        return false;
    }
    if (!socket.send_to(destination, payload, error)) {
        journal_entry.emitted_ordinals.pop_back();
        return false;
    }
    journal_entry.delivered = true;
    counters = next;
    return true;
}

[[nodiscard]] bool process_datagram(
    network::UdpSocket& socket,
    const network::NetworkAddress destination,
    const std::span<const std::byte> payload,
    const goldsrc::StockRuntimeCaptureAction action,
    const std::size_t journal_index,
    std::optional<HeldDatagram>& held,
    goldsrc::StockRuntimeCaptureCounters& counters,
    std::vector<goldsrc::StockRuntimeTransportJournalEntry>& journal,
    std::size_t& perturbation_count,
    bool& reorder_completed,
    std::string& error)
{
    if (held && action == goldsrc::StockRuntimeCaptureAction::forward) {
        // A reorder forwards the new datagram first; a delay preserves order.
        const bool completing_reorder = held->reorder_on_release;
        if (completing_reorder) {
            if (!emit_and_record(socket, destination, payload, counters,
                                 journal[journal_index], error) ||
                !emit_and_record(socket, held->destination, held->payload, counters,
                                 journal[held->journal_index], error)) {
                return false;
            }
        } else {
            if (!emit_and_record(socket, held->destination, held->payload, counters,
                                 journal[held->journal_index], error) ||
                !emit_and_record(socket, destination, payload, counters,
                                 journal[journal_index], error)) {
                return false;
            }
        }
        journal[held->journal_index].hold_state =
            goldsrc::StockRuntimeTransportHoldState::released;
        if (completing_reorder) reorder_completed = true;
        held.reset();
        return true;
    }
    switch (action) {
    case goldsrc::StockRuntimeCaptureAction::forward:
        return emit_and_record(socket, destination, payload, counters,
                               journal[journal_index], error);
    case goldsrc::StockRuntimeCaptureAction::drop:
        ++counters.dropped_datagrams;
        ++perturbation_count;
        return true;
    case goldsrc::StockRuntimeCaptureAction::duplicate:
        if (!emit_and_record(socket, destination, payload, counters,
                             journal[journal_index], error) ||
            !emit_and_record(socket, destination, payload, counters,
                             journal[journal_index], error)) {
            return false;
        }
        ++counters.duplicated_datagrams;
        ++perturbation_count;
        return true;
    case goldsrc::StockRuntimeCaptureAction::hold_for_delay:
    case goldsrc::StockRuntimeCaptureAction::hold_for_reorder:
        if (held) {
            error = "a second delayed datagram was requested before release";
            return false;
        }
        held = HeldDatagram{std::vector<std::byte>{payload.begin(), payload.end()},
                            destination,
                            journal_index,
                            action == goldsrc::StockRuntimeCaptureAction::hold_for_reorder};
        journal[journal_index].hold_state =
            goldsrc::StockRuntimeTransportHoldState::held;
        ++counters.delayed_datagrams;
        ++perturbation_count;
        return true;
    }
    error = "unknown datagram action";
    return false;
}

[[nodiscard]] std::size_t expected_perturbations(
    const goldsrc::StockRuntimeCaptureScenario scenario) noexcept
{
    if (scenario == goldsrc::StockRuntimeCaptureScenario::drop_two_server_runtime) {
        return 2U;
    }
    switch (scenario) {
    case goldsrc::StockRuntimeCaptureScenario::drop_server_runtime:
    case goldsrc::StockRuntimeCaptureScenario::duplicate_server_runtime:
    case goldsrc::StockRuntimeCaptureScenario::reorder_server_runtime:
    case goldsrc::StockRuntimeCaptureScenario::drop_client_move:
    case goldsrc::StockRuntimeCaptureScenario::delay_client_move:
        return 1U;
    default:
        return 0U;
    }
}

[[nodiscard]] int run_capture(const Options& options)
{
    std::string error;
#ifdef _WIN32
    // This check deliberately precedes output-root validation, directory
    // creation, network-runtime construction and socket creation. The relay is
    // an internal child capability, not a public active-capture entry point.
    if (!validate_orchestrator_capability(options, error)) {
        std::cerr << "[stock-runtime-capture] result="
                     "orchestrator-capability-required\n";
        return 18;
    }
#else
    static_cast<void>(options);
    std::cerr << "[stock-runtime-capture] result="
                 "orchestrator-capability-required\n";
    return 18;
#endif
    const auto run_root = validate_output_run_root(
        *options.output_run_root, options.precreated_empty_run_root, error);
    if (!run_root) {
        std::cerr << "[stock-runtime-capture] result=unsafe-output-root\n";
        return 3;
    }
    std::error_code create_error;
    if (!options.precreated_empty_run_root) {
        const bool run_created = fs::create_directory(*run_root, create_error);
        if (create_error || !run_created || has_symlink_component(*run_root)) {
            std::cerr << "[stock-runtime-capture] result=output-create-failed\n";
            return 4;
        }
    }
    auto held_run_root = hold_output_directory(*run_root, error);
    if (!held_run_root) {
        std::cerr << "[stock-runtime-capture] result=output-identity-failed\n";
        return 4;
    }
    const auto raw_root = *run_root / "raw";
    const bool raw_created = fs::create_directory(raw_root, create_error);
    if (create_error || !raw_created || has_symlink_component(raw_root)) {
        std::cerr << "[stock-runtime-capture] result=output-create-failed\n";
        return 4;
    }
    auto held_raw_root = hold_output_directory(raw_root, error);
    if (!held_raw_root) {
        std::cerr << "[stock-runtime-capture] result=output-identity-failed\n";
        return 4;
    }

    network::NetworkRuntime runtime;
    if (!runtime.valid()) {
        std::cerr << "[stock-runtime-capture] result=network-runtime-unavailable\n";
        return 5;
    }
    auto client_socket = network::UdpSocket::open_ipv4(runtime, error);
    auto upstream_socket = network::UdpSocket::open_ipv4(runtime, error);
    if (!client_socket || !upstream_socket ||
        !client_socket->bind(network::NetworkAddress::loopback(*options.listen_port), error) ||
        !upstream_socket->bind(network::NetworkAddress::loopback(0U), error)) {
        std::cerr << "[stock-runtime-capture] result=socket-bind-failed\n";
        return 5;
    }

    const auto server = network::NetworkAddress::loopback(*options.server_port);
    std::optional<network::NetworkAddress> client;
    std::optional<HeldDatagram> held_client;
    std::optional<HeldDatagram> held_server;
    goldsrc::StockRuntimeCaptureCounters counters;
    std::size_t perturbation_count = 0U;
    bool reorder_completed = false;
    bool accepted_client_observed = false;
    bool accepted_server_observed = false;
    bool bidirectional_reported = false;
    bool wrong_source_observed = false;
    bool stop_requested = false;
    std::vector<goldsrc::StockRuntimeTransportJournalEntry> journal;
    journal.reserve(options.limits.maximum_datagrams);
    const auto start = std::chrono::steady_clock::now();
    const auto maximum_relative_timestamp_us = static_cast<std::uint64_t>(
        options.limits.maximum_duration.count()) * 1'000U;
    std::cout << "[stock-runtime-capture] relay-ready=true\n" << std::flush;

    const auto record_observation = [&](const goldsrc::StockRuntimeCaptureDirection direction,
                                        const std::span<const std::byte> payload,
                                        const bool wrong_source)
        -> std::optional<std::size_t> {
        auto next = counters;
        if (!goldsrc::stock_runtime_capture_observe_datagram(
                next, options.limits, direction, payload.size())) {
            error = "capture budget exceeded";
            return std::nullopt;
        }
        if (wrong_source) {
            ++next.ignored_wrong_source_datagrams;
            ++next.dropped_datagrams;
        }
        const auto direction_ordinal =
            direction == goldsrc::StockRuntimeCaptureDirection::client_to_server
            ? next.client_packets
            : next.server_packets;
        const auto action = wrong_source
            ? goldsrc::StockRuntimeCaptureAction::drop
            : goldsrc::stock_runtime_capture_action(
                  *options.scenario, direction, direction_ordinal,
                  options.perturbation);
        auto entry = make_journal_entry(
            journal.size(), direction, direction_ordinal,
            bounded_relative_timestamp_us(start, maximum_relative_timestamp_us),
            payload, action, wrong_source);
        if (!entry || !write_raw_datagram(
                          *held_raw_root, entry->raw_filename, payload, error)) {
            if (error.empty()) {
                error = "journal entry could not be created";
            }
            return std::nullopt;
        }
        journal.push_back(std::move(*entry));
        counters = next;
        if (wrong_source) {
            ++perturbation_count;
        }
        return journal.size() - 1U;
    };

    const auto report_bidirectional = [&]() {
        if (!bidirectional_reported && accepted_client_observed &&
            accepted_server_observed) {
            std::cout << "[stock-runtime-capture] bidirectional-traffic=true\n"
                      << std::flush;
            bidirectional_reported = true;
        }
    };

    while (std::chrono::steady_clock::now() - start < options.limits.maximum_duration) {
#ifdef _WIN32
        if (options.stop_handle != nullptr) {
            const DWORD stop_state = ::WaitForSingleObject(options.stop_handle, 0U);
            if (stop_state == WAIT_OBJECT_0) {
                stop_requested = true;
                break;
            }
            if (stop_state == WAIT_FAILED) {
                std::cerr << "[stock-runtime-capture] result=stop-handle-failed\n";
                return 17;
            }
        }
#endif
        bool progressed = false;
        auto from_client = client_socket->receive(options.limits.maximum_payload_bytes);
        if (from_client.status == network::ReceiveStatus::truncated ||
            from_client.status == network::ReceiveStatus::error) {
            std::cerr << "[stock-runtime-capture] result=client-receive-failed\n";
            return 6;
        }
        if (from_client.status == network::ReceiveStatus::received &&
            from_client.datagram) {
            progressed = true;
            const auto source = from_client.datagram->source;
            if (source.ipv4_host_order() != network::NetworkAddress::loopback(0U).ipv4_host_order() ||
                (client && source != *client)) {
                if (!record_observation(
                        goldsrc::StockRuntimeCaptureDirection::client_to_server,
                        from_client.datagram->payload, true)) {
                    std::cerr << "[stock-runtime-capture] result=capture-bound-exceeded\n";
                    return 7;
                }
                wrong_source_observed = true;
                break;
            } else {
                if (!client) client = source;
                const auto& payload = from_client.datagram->payload;
                const auto journal_index = record_observation(
                    goldsrc::StockRuntimeCaptureDirection::client_to_server,
                    payload, false);
                if (!journal_index) {
                    std::cerr << "[stock-runtime-capture] result=capture-bound-exceeded\n";
                    return 7;
                }
                if (!process_datagram(
                                      *upstream_socket, server, payload,
                                      journal[*journal_index].action,
                                      *journal_index, held_client, counters,
                                      journal, perturbation_count,
                                      reorder_completed, error)) {
                    std::cerr << "[stock-runtime-capture] result=client-forward-failed\n";
                    return 8;
                }
                accepted_client_observed = true;
                report_bidirectional();
            }
        }

        auto from_server = upstream_socket->receive(options.limits.maximum_payload_bytes);
        if (from_server.status == network::ReceiveStatus::truncated ||
            from_server.status == network::ReceiveStatus::error) {
            std::cerr << "[stock-runtime-capture] result=server-receive-failed\n";
            return 9;
        }
        if (from_server.status == network::ReceiveStatus::received &&
            from_server.datagram) {
            progressed = true;
            if (from_server.datagram->source != server || !client) {
                if (!record_observation(
                        goldsrc::StockRuntimeCaptureDirection::server_to_client,
                        from_server.datagram->payload, true)) {
                    std::cerr << "[stock-runtime-capture] result=capture-bound-exceeded\n";
                    return 7;
                }
                wrong_source_observed = true;
                break;
            } else {
                const auto& payload = from_server.datagram->payload;
                const auto journal_index = record_observation(
                    goldsrc::StockRuntimeCaptureDirection::server_to_client,
                    payload, false);
                if (!journal_index) {
                    std::cerr << "[stock-runtime-capture] result=capture-bound-exceeded\n";
                    return 7;
                }
                if (!process_datagram(
                                      *client_socket, *client, payload,
                                      journal[*journal_index].action,
                                      *journal_index, held_server, counters,
                                      journal, perturbation_count,
                                      reorder_completed, error)) {
                    std::cerr << "[stock-runtime-capture] result=server-forward-failed\n";
                    return 10;
                }
                accepted_server_observed = true;
                report_bidirectional();
            }
        }
        if (wrong_source_observed) {
            break;
        }
        if (!progressed) std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    // A lone held datagram is released at the bounded deadline. A requested
    // reorder that never saw a successor remains explicitly incomplete.
    const bool unresolved_reorder =
        (held_client && held_client->reorder_on_release) ||
        (held_server && held_server->reorder_on_release);
    if (held_client) {
        journal[held_client->journal_index].hold_state =
            held_client->reorder_on_release
            ? goldsrc::StockRuntimeTransportHoldState::unresolved
            : goldsrc::StockRuntimeTransportHoldState::released;
        if (!emit_and_record(
                *upstream_socket, held_client->destination, held_client->payload,
                counters, journal[held_client->journal_index], error)) {
            std::cerr << "[stock-runtime-capture] result=delayed-flush-failed\n";
            return 11;
        }
    }
    if (held_server && client) {
        journal[held_server->journal_index].hold_state =
            held_server->reorder_on_release
            ? goldsrc::StockRuntimeTransportHoldState::unresolved
            : goldsrc::StockRuntimeTransportHoldState::released;
        if (!emit_and_record(
                *client_socket, *client, held_server->payload, counters,
                journal[held_server->journal_index], error)) {
            std::cerr << "[stock-runtime-capture] result=delayed-flush-failed\n";
            return 11;
        }
    }

    const bool mutation_counters_consistent =
        counters.dropped_datagrams <= counters.observed_datagrams &&
        counters.emitted_datagrams ==
            counters.observed_datagrams - counters.dropped_datagrams +
                counters.duplicated_datagrams &&
        perturbation_count == counters.dropped_datagrams +
            counters.duplicated_datagrams + counters.delayed_datagrams;
    const bool scenario_requires_reorder =
        *options.scenario ==
        goldsrc::StockRuntimeCaptureScenario::reorder_server_runtime;
    const bool complete = !wrong_source_observed && client.has_value() &&
        counters.client_packets != 0U &&
        counters.server_packets != 0U &&
        counters.ignored_wrong_source_datagrams == 0U &&
        mutation_counters_consistent &&
        perturbation_count == expected_perturbations(*options.scenario) &&
        (!scenario_requires_reorder ||
            (reorder_completed && !unresolved_reorder));
    goldsrc::StockRuntimeCaptureMetadata metadata;
    metadata.scenario = *options.scenario;
    metadata.limits = options.limits;
    metadata.counters = counters;
    metadata.perturbation_count = perturbation_count;
    metadata.bounded_transport_complete = complete;
    if (!write_transport_journal(
            *held_run_root, journal, options.limits, complete, error)) {
        std::cerr << "[stock-runtime-capture] result=journal-write-failed\n";
        return 15;
    }
    if (!write_metadata(*held_run_root, metadata, error)) {
        std::cerr << "[stock-runtime-capture] result=metadata-write-failed\n";
        return 12;
    }
    std::cout << "[stock-runtime-capture] profile="
              << goldsrc::kStockRuntimePendingProfile << '\n'
              << "[stock-runtime-capture] datagrams="
              << counters.observed_datagrams << '\n'
              << "[stock-runtime-capture] raw-bytes="
              << counters.observed_raw_bytes << '\n'
              << "[stock-runtime-capture] stop-reason="
              << (stop_requested ? "orchestrator-request" : "duration-bound") << '\n'
              << "[stock-runtime-capture] payload-rewrites=0\n"
              << "[stock-runtime-capture] processes-started=0\n"
              << "[stock-runtime-capture] result="
              << (wrong_source_observed
                      ? "unexpected-source"
                      : complete
                          ? "bounded-transport-complete-evidence-pending"
                          : "incomplete-evidence-pending") << '\n';
    return wrong_source_observed ? 14 : complete ? 0 : 13;
}

} // namespace

int main(const int argc, const char* const* argv)
{
    try {
        std::vector<std::string_view> arguments;
        arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
        for (int index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }
        const auto options = parse_options(arguments);
        if (!options) {
            std::cerr << kUsage;
            return 2;
        }
        if (options->validate_config) {
            std::cout << "[stock-runtime-capture] configuration=valid\n"
                      << "[stock-runtime-capture] sockets-opened=0\n"
                      << "[stock-runtime-capture] files-written=0\n"
                      << "[stock-runtime-capture] processes-started=0\n"
                      << "[stock-runtime-capture] result=success\n";
            return 0;
        }
        return run_capture(*options);
    } catch (...) {
        std::cerr << "[stock-runtime-capture] result=bounded-internal-failure\n";
        return 16;
    }
}
