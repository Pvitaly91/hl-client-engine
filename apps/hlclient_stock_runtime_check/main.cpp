#include <hlclient/goldsrc/stock_runtime_capture.hpp>
#include <hlclient/hash/sha256.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace goldsrc = hlclient::goldsrc;

constexpr std::string_view kUsage =
    "Usage: hlclient_stock_runtime_check --capture-root <ignored run directory> "
    "--scenario transcript|baselines|entities|clientdata|authority|ack\n";

enum class Scenario {
    transcript,
    baselines,
    entities,
    clientdata,
    authority,
    ack,
};

struct Options final {
    fs::path capture_root;
    Scenario scenario{Scenario::transcript};
};

[[nodiscard]] std::optional<Scenario> parse_scenario(
    const std::string_view value) noexcept
{
    if (value == "transcript") return Scenario::transcript;
    if (value == "baselines") return Scenario::baselines;
    if (value == "entities") return Scenario::entities;
    if (value == "clientdata") return Scenario::clientdata;
    if (value == "authority") return Scenario::authority;
    if (value == "ack") return Scenario::ack;
    return std::nullopt;
}

[[nodiscard]] std::optional<Options> parse_options(
    const std::span<const std::string_view> arguments)
{
    Options options;
    bool root_seen = false;
    bool scenario_seen = false;
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if ((argument != "--capture-root" && argument != "--scenario") ||
            index + 1U >= arguments.size()) {
            return std::nullopt;
        }
        const auto value = arguments[++index];
        if (argument == "--capture-root") {
            if (root_seen || value.empty() ||
                value.find('\0') != std::string_view::npos) {
                return std::nullopt;
            }
            root_seen = true;
            options.capture_root = fs::path{std::string{value}};
        } else {
            if (scenario_seen) return std::nullopt;
            const auto parsed = parse_scenario(value);
            if (!parsed) return std::nullopt;
            scenario_seen = true;
            options.scenario = *parsed;
        }
    }
    return root_seen && scenario_seen
        ? std::optional<Options>{std::move(options)}
        : std::nullopt;
}

[[nodiscard]] bool is_lower_hex_run_id(const std::string_view value) noexcept
{
    return value.size() == 32U && std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
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

[[nodiscard]] std::optional<fs::path> validate_capture_root(
    const fs::path& requested)
{
    std::error_code error;
    const auto repository = fs::weakly_canonical(fs::current_path(), error);
    if (error) return std::nullopt;
    const auto approved = fs::weakly_canonical(
        repository / "manual-artifacts" / "stock-runtime", error);
    if (error || !fs::is_directory(approved, error) || error ||
        has_symlink_component(approved)) {
        return std::nullopt;
    }
    const auto root = fs::weakly_canonical(requested, error);
    if (error || !fs::is_directory(root, error) || error ||
        root.parent_path() != approved ||
        !is_lower_hex_run_id(root.filename().string()) ||
        has_symlink_component(root)) {
        return std::nullopt;
    }
    return root;
}

[[nodiscard]] std::optional<std::string> read_metadata(const fs::path& path)
{
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error || !fs::is_regular_file(status) || fs::is_symlink(status)) {
        return std::nullopt;
    }
    const auto size = fs::file_size(path, error);
    if (error || size == 0U || size > 65'536U) return std::nullopt;
    std::ifstream stream{path, std::ios::binary};
    if (!stream) return std::nullopt;
    std::string text(static_cast<std::size_t>(size), '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) return std::nullopt;
    return text;
}

[[nodiscard]] bool validate_raw_inventory(
    const fs::path& root,
    const goldsrc::StockRuntimeCaptureMetadata& metadata)
{
    const auto raw = root / "raw";
    std::error_code error;
    if (!fs::is_directory(raw, error) || error || has_symlink_component(raw)) {
        return false;
    }
    std::size_t files = 0U;
    std::size_t client_files = 0U;
    std::size_t server_files = 0U;
    std::uint64_t bytes = 0U;
    std::vector<std::size_t> ordinals;
    ordinals.reserve(metadata.counters.observed_datagrams);
    for (fs::directory_iterator iterator{raw, error}; !error && iterator != fs::directory_iterator{};
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || error || iterator->is_symlink(error) || error) {
            return false;
        }
        const auto name = iterator->path().filename().string();
        if (name.size() != 16U || name[8] != '-' ||
            (name.substr(9U) != "c2s.bin" && name.substr(9U) != "s2c.bin")) {
            return false;
        }
        if (!std::ranges::all_of(name.substr(0U, 8U), [](const char value) {
                return value >= '0' && value <= '9';
            })) {
            return false;
        }
        const auto prefix = name.substr(0U, 8U);
        std::size_t ordinal{};
        const auto converted = std::from_chars(
            prefix.data(), prefix.data() + prefix.size(), ordinal, 10);
        if (converted.ec != std::errc{} ||
            converted.ptr != prefix.data() + prefix.size()) {
            return false;
        }
        ordinals.push_back(ordinal);
        if (name.substr(9U) == "c2s.bin") ++client_files;
        else ++server_files;
        const auto size = iterator->file_size(error);
        if (error || size > metadata.limits.maximum_payload_bytes ||
            bytes > metadata.limits.maximum_total_raw_bytes - size) {
            return false;
        }
        bytes += size;
        ++files;
        if (files > metadata.limits.maximum_datagrams) return false;
    }
    if (error || files != metadata.counters.observed_datagrams ||
        client_files != metadata.counters.client_packets ||
        server_files != metadata.counters.server_packets ||
        bytes != metadata.counters.observed_raw_bytes) {
        return false;
    }
    std::ranges::sort(ordinals);
    for (std::size_t expected = 0U; expected < ordinals.size(); ++expected) {
        if (ordinals[expected] != expected) return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::string> structural_sha256(
    const goldsrc::StockRuntimeCaptureMetadata& metadata)
{
    const auto canonical = goldsrc::canonical_stock_runtime_capture_structure(metadata);
    const auto bytes = std::as_bytes(std::span{canonical.data(), canonical.size()});
    const auto digest = hlclient::hash::sha256(bytes);
    return digest ? std::optional<std::string>{hlclient::hash::sha256_hex(*digest)}
                  : std::nullopt;
}

[[nodiscard]] std::optional<std::string> raw_inventory_sha256(
    const fs::path& root,
    const goldsrc::StockRuntimeCaptureMetadata& metadata)
{
    const auto raw = root / "raw";
    std::error_code error;
    std::vector<fs::path> files;
    for (fs::directory_iterator iterator{raw, error};
         !error && iterator != fs::directory_iterator{};
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || error ||
            iterator->is_symlink(error) || error) {
            return std::nullopt;
        }
        files.push_back(iterator->path());
        if (files.size() > metadata.limits.maximum_datagrams) {
            return std::nullopt;
        }
    }
    if (error || files.size() != metadata.counters.observed_datagrams) {
        return std::nullopt;
    }
    std::ranges::sort(files, {}, [](const auto& path) {
        return path.filename().string();
    });

    std::string canonical;
    std::uint64_t total_bytes = 0U;
    for (const auto& path : files) {
        const auto size = fs::file_size(path, error);
        if (error || size > metadata.limits.maximum_payload_bytes ||
            total_bytes > metadata.limits.maximum_total_raw_bytes - size) {
            return std::nullopt;
        }
        std::ifstream stream{path, std::ios::binary};
        if (!stream) return std::nullopt;
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!stream) return std::nullopt;
        const auto digest = hlclient::hash::sha256(bytes);
        if (!digest) return std::nullopt;
        if (!canonical.empty()) canonical.push_back('\n');
        canonical += path.filename().string();
        canonical.push_back('|');
        canonical += std::to_string(size);
        canonical.push_back('|');
        canonical += hlclient::hash::sha256_hex(*digest);
        total_bytes += size;
    }
    if (total_bytes != metadata.counters.observed_raw_bytes) {
        return std::nullopt;
    }
    const auto canonical_bytes =
        std::as_bytes(std::span{canonical.data(), canonical.size()});
    const auto digest = hlclient::hash::sha256(canonical_bytes);
    return digest
        ? std::optional<std::string>{hlclient::hash::sha256_hex(*digest)}
        : std::nullopt;
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
    const auto capture_root = validate_capture_root(options->capture_root);
    if (!capture_root) {
        std::cerr << "[stock-runtime] result=unsafe-capture-root\n";
        return 3;
    }
    const auto json = read_metadata(*capture_root / "capture-metadata.json");
    if (!json) {
        std::cerr << "[stock-runtime] result=missing-capture-metadata\n";
        return 4;
    }
    auto parsed = goldsrc::parse_stock_runtime_capture_metadata(*json);
    if (!parsed || !parsed.metadata ||
        !validate_raw_inventory(*capture_root, *parsed.metadata)) {
        std::cerr << "[stock-runtime] result=invalid-capture-metadata\n";
        return 5;
    }
    const auto& metadata = *parsed.metadata;
    if (!metadata.bounded_transport_complete) {
        std::cerr << "[stock-runtime] result=incomplete-capture\n";
        return 6;
    }
    const auto hash = structural_sha256(metadata);
    const auto raw_hash = raw_inventory_sha256(*capture_root, metadata);
    if (!hash || !raw_hash) {
        std::cerr << "[stock-runtime] result=structural-hash-failed\n";
        return 7;
    }

    // No stock message grammar is promoted from transport metadata. The exact
    // current result is an evidence-pending boundary with zero fabricated
    // messages, frames, entities, client-local data, authority, or ACKs.
    std::cout << "[stock-runtime] profile=" << goldsrc::kStockRuntimePendingProfile << '\n'
              << "[stock-runtime] messages=0\n"
              << "[stock-runtime] runtime-frames=0\n"
              << "[stock-runtime] baselines=0\n"
              << "[stock-runtime] full-updates=0\n"
              << "[stock-runtime] delta-updates=0\n"
              << "[stock-runtime] removals=0\n"
              << "[stock-runtime] clientdata=0\n"
              << "[stock-runtime] local-player=evidence_pending\n"
              << "[stock-runtime] server-time=evidence_pending\n"
              << "[stock-runtime] authority=evidence_pending\n"
              << "[stock-runtime] command-ack=evidence_pending\n"
              << "[stock-runtime] structural-hash=" << *hash << '\n'
              << "[stock-runtime] raw-inventory-hash=" << *raw_hash << '\n'
              << "[stock-runtime] result=evidence_pending\n";
    return 0;
}
