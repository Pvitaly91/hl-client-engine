#include <hlclient/goldsrc/stock_runtime_capture.hpp>
#include <hlclient/network/network_address.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>

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

namespace {

namespace fs = std::filesystem;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

constexpr std::string_view kUsage =
    "Usage: hlclient_stock_runtime_capture --validate-config [limit options]\n"
    "   or: hlclient_stock_runtime_capture --listen-port <port> "
    "--server-port <port> --output-run-root <new ignored run directory> "
    "--scenario <name> [limit options] "
    "--private-ipv4-loopback-only --one-upstream-socket "
    "--byte-preserving --no-payload-rewrite\n";

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
    std::array<bool, 21U> seen{};
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
            argument == "--no-payload-rewrite") {
            const std::size_t seen_index = argument == "--private-ipv4-loopback-only" ? 1U
                : argument == "--one-upstream-socket" ? 2U
                : argument == "--byte-preserving" ? 3U
                                                      : 4U;
            if (!mark(seen_index)) return std::nullopt;
            if (seen_index == 1U) options.private_loopback_only = true;
            if (seen_index == 2U) options.one_upstream_socket = true;
            if (seen_index == 3U) options.byte_preserving = true;
            if (seen_index == 4U) options.no_payload_rewrite = true;
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
                       options.no_payload_rewrite
            ? std::nullopt
            : std::optional<Options>{std::move(options)};
    }
    if (!options.listen_port || !options.server_port ||
        *options.listen_port == *options.server_port || !options.output_run_root ||
        !options.scenario || !options.private_loopback_only ||
        !options.one_upstream_socket || !options.byte_preserving ||
        !options.no_payload_rewrite) {
        return std::nullopt;
    }
    return options;
}

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
        !is_lower_hex_run_id(full.filename().string()) || fs::exists(full, path_error) ||
        path_error) {
        error = "output run root must be one new GUID directory below the exact ignored root";
        return std::nullopt;
    }
    return full;
}

[[nodiscard]] bool write_raw_datagram(
    const fs::path& raw_root,
    const std::size_t ordinal,
    const goldsrc::StockRuntimeCaptureDirection direction,
    const std::span<const std::byte> payload,
    std::string& error)
{
    std::ostringstream name;
    name << std::setfill('0') << std::setw(8) << ordinal
         << (direction == goldsrc::StockRuntimeCaptureDirection::client_to_server
                 ? "-c2s.bin"
                 : "-s2c.bin");
    const auto path = raw_root / name.str();
    if (fs::exists(path)) {
        error = "raw datagram ordinal already exists";
        return false;
    }
    std::ofstream stream{path, std::ios::binary | std::ios::out};
    if (!stream) {
        error = "raw datagram file could not be created";
        return false;
    }
    stream.write(reinterpret_cast<const char*>(payload.data()),
                 static_cast<std::streamsize>(payload.size()));
    stream.flush();
    if (!stream) {
        error = "raw datagram file write failed";
        return false;
    }
    return true;
}

[[nodiscard]] bool write_metadata(
    const fs::path& run_root,
    const goldsrc::StockRuntimeCaptureMetadata& metadata,
    std::string& error)
{
    const auto path = run_root / "capture-metadata.json";
    const auto temporary = run_root / "capture-metadata.json.tmp";
    if (fs::exists(path) || fs::exists(temporary)) {
        error = "capture metadata path already exists";
        return false;
    }
    const auto json = goldsrc::serialize_stock_runtime_capture_metadata(metadata);
    std::ofstream stream{temporary, std::ios::binary | std::ios::out};
    stream.write(json.data(), static_cast<std::streamsize>(json.size()));
    stream.flush();
    if (!stream) {
        error = "capture metadata write failed";
        return false;
    }
    stream.close();
    std::error_code rename_error;
    fs::rename(temporary, path, rename_error);
    if (rename_error) {
        error = "capture metadata could not be atomically published";
        return false;
    }
    return true;
}

struct HeldDatagram final {
    std::vector<std::byte> payload;
    network::NetworkAddress destination;
    bool reorder_on_release{false};
};

[[nodiscard]] bool emit(
    network::UdpSocket& socket,
    const network::NetworkAddress destination,
    const std::span<const std::byte> payload,
    goldsrc::StockRuntimeCaptureCounters& counters,
    std::string& error)
{
    auto next = counters;
    if (!goldsrc::stock_runtime_capture_record_emission(next, payload.size())) {
        error = "emission counters overflowed";
        return false;
    }
    if (!socket.send_to(destination, payload, error)) return false;
    counters = next;
    return true;
}

[[nodiscard]] bool process_datagram(
    network::UdpSocket& socket,
    const network::NetworkAddress destination,
    const std::span<const std::byte> payload,
    const goldsrc::StockRuntimeCaptureAction action,
    std::optional<HeldDatagram>& held,
    goldsrc::StockRuntimeCaptureCounters& counters,
    std::size_t& perturbation_count,
    bool& reorder_completed,
    std::string& error)
{
    if (held && action == goldsrc::StockRuntimeCaptureAction::forward) {
        // A reorder forwards the new datagram first; a delay preserves order.
        const bool completing_reorder = held->reorder_on_release;
        if (completing_reorder) {
            if (!emit(socket, destination, payload, counters, error) ||
                !emit(socket, held->destination, held->payload, counters, error)) {
                return false;
            }
        } else {
            if (!emit(socket, held->destination, held->payload, counters, error) ||
                !emit(socket, destination, payload, counters, error)) {
                return false;
            }
        }
        if (completing_reorder) reorder_completed = true;
        held.reset();
        return true;
    }
    switch (action) {
    case goldsrc::StockRuntimeCaptureAction::forward:
        return emit(socket, destination, payload, counters, error);
    case goldsrc::StockRuntimeCaptureAction::drop:
        ++counters.dropped_datagrams;
        ++perturbation_count;
        return true;
    case goldsrc::StockRuntimeCaptureAction::duplicate:
        if (!emit(socket, destination, payload, counters, error) ||
            !emit(socket, destination, payload, counters, error)) {
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
                            action == goldsrc::StockRuntimeCaptureAction::hold_for_reorder};
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
    const auto run_root = validate_output_run_root(*options.output_run_root, error);
    if (!run_root) {
        std::cerr << "[stock-runtime-capture] result=unsafe-output-root\n";
        return 3;
    }
    std::error_code create_error;
    const bool run_created = fs::create_directory(*run_root, create_error);
    if (create_error || !run_created || has_symlink_component(*run_root)) {
        std::cerr << "[stock-runtime-capture] result=output-create-failed\n";
        return 4;
    }
    const auto raw_root = *run_root / "raw";
    const bool raw_created = fs::create_directory(raw_root, create_error);
    if (create_error || !raw_created || has_symlink_component(raw_root)) {
        std::cerr << "[stock-runtime-capture] result=output-create-failed\n";
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
    std::size_t raw_ordinal = 0U;
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < options.limits.maximum_duration) {
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
                ++counters.ignored_wrong_source_datagrams;
                std::cerr << "[stock-runtime-capture] result=unexpected-client-source\n";
                return 14;
            } else {
                if (!client) client = source;
                const auto& payload = from_client.datagram->payload;
                if (!goldsrc::stock_runtime_capture_observe_datagram(
                        counters, options.limits,
                        goldsrc::StockRuntimeCaptureDirection::client_to_server,
                        payload.size()) ||
                    !write_raw_datagram(raw_root, raw_ordinal++,
                        goldsrc::StockRuntimeCaptureDirection::client_to_server,
                        payload, error)) {
                    std::cerr << "[stock-runtime-capture] result=capture-bound-exceeded\n";
                    return 7;
                }
                const auto action = goldsrc::stock_runtime_capture_action(
                    *options.scenario,
                    goldsrc::StockRuntimeCaptureDirection::client_to_server,
                    counters.client_packets,
                    options.perturbation);
                if (!process_datagram(*upstream_socket, server, payload, action,
                                      held_client, counters, perturbation_count,
                                      reorder_completed, error)) {
                    std::cerr << "[stock-runtime-capture] result=client-forward-failed\n";
                    return 8;
                }
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
                ++counters.ignored_wrong_source_datagrams;
                std::cerr << "[stock-runtime-capture] result=unexpected-server-source\n";
                return 14;
            } else {
                const auto& payload = from_server.datagram->payload;
                if (!goldsrc::stock_runtime_capture_observe_datagram(
                        counters, options.limits,
                        goldsrc::StockRuntimeCaptureDirection::server_to_client,
                        payload.size()) ||
                    !write_raw_datagram(raw_root, raw_ordinal++,
                        goldsrc::StockRuntimeCaptureDirection::server_to_client,
                        payload, error)) {
                    std::cerr << "[stock-runtime-capture] result=capture-bound-exceeded\n";
                    return 7;
                }
                const auto action = goldsrc::stock_runtime_capture_action(
                    *options.scenario,
                    goldsrc::StockRuntimeCaptureDirection::server_to_client,
                    counters.server_packets,
                    options.perturbation);
                if (!process_datagram(*client_socket, *client, payload, action,
                                      held_server, counters, perturbation_count,
                                      reorder_completed, error)) {
                    std::cerr << "[stock-runtime-capture] result=server-forward-failed\n";
                    return 10;
                }
            }
        }
        if (!progressed) std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    // A lone held datagram is released at the bounded deadline. A requested
    // reorder that never saw a successor remains explicitly incomplete.
    const bool unresolved_reorder =
        (held_client && held_client->reorder_on_release) ||
        (held_server && held_server->reorder_on_release);
    if (held_client &&
        !emit(*upstream_socket, held_client->destination, held_client->payload,
              counters, error)) {
        std::cerr << "[stock-runtime-capture] result=delayed-flush-failed\n";
        return 11;
    }
    if (held_server && client &&
        !emit(*client_socket, *client, held_server->payload, counters, error)) {
        std::cerr << "[stock-runtime-capture] result=delayed-flush-failed\n";
        return 11;
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
    const bool complete = client.has_value() && counters.client_packets != 0U &&
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
    if (!write_metadata(*run_root, metadata, error)) {
        std::cerr << "[stock-runtime-capture] result=metadata-write-failed\n";
        return 12;
    }
    std::cout << "[stock-runtime-capture] profile="
              << goldsrc::kStockRuntimePendingProfile << '\n'
              << "[stock-runtime-capture] datagrams="
              << counters.observed_datagrams << '\n'
              << "[stock-runtime-capture] raw-bytes="
              << counters.observed_raw_bytes << '\n'
              << "[stock-runtime-capture] payload-rewrites=0\n"
              << "[stock-runtime-capture] processes-started=0\n"
              << "[stock-runtime-capture] result="
              << (complete ? "bounded-transport-complete-evidence-pending"
                           : "incomplete-evidence-pending") << '\n';
    return complete ? 0 : 13;
}

} // namespace

int main(const int argc, const char* const* argv)
{
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
    for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
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
}
