#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/stock_runtime_capture.hpp>
#include <hlclient/goldsrc/stock_runtime_capture_corpus.hpp>
#include <hlclient/goldsrc/stock_runtime_campaign.hpp>
#include <hlclient/goldsrc/stock_runtime_first_observation.hpp>
#include <hlclient/goldsrc/stock_runtime_reconnect_lifecycle.hpp>
#include <hlclient/goldsrc/stock_runtime_transport_replay.hpp>
#include <hlclient/hash/sha256.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
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
    "--scenario transcript|baselines|entities|clientdata|authority|ack|transport|"
    "netchan|signon-replay|post-resource-first|first-observation|campaign-summary "
    "[--publication-stage prepublication] "
    "[--campaign-refresh-implementation-commit <40-lower-hex>] "
    "[--independent-walker-validated-run <32-lower-hex>]...\n";

enum class Scenario {
    transcript,
    baselines,
    entities,
    clientdata,
    authority,
    ack,
    transport,
    netchan,
    signon_replay,
    post_resource_first,
    first_observation,
    campaign_summary,
};

struct Options final {
    fs::path capture_root;
    Scenario scenario{Scenario::transcript};
    bool prepublication{false};
    std::optional<std::string> campaign_refresh_implementation_commit;
    std::vector<std::string> independent_walker_validated_runs;
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
    if (value == "transport") return Scenario::transport;
    if (value == "netchan") return Scenario::netchan;
    if (value == "signon-replay") return Scenario::signon_replay;
    if (value == "post-resource-first") return Scenario::post_resource_first;
    if (value == "first-observation") return Scenario::first_observation;
    if (value == "campaign-summary") return Scenario::campaign_summary;
    return std::nullopt;
}

[[nodiscard]] std::optional<Options> parse_options(
    const std::span<const std::string_view> arguments)
{
    Options options;
    bool root_seen = false;
    bool scenario_seen = false;
    bool publication_seen = false;
    bool campaign_refresh_seen = false;
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if ((argument != "--capture-root" && argument != "--scenario" &&
             argument != "--publication-stage" &&
             argument != "--campaign-refresh-implementation-commit" &&
             argument != "--independent-walker-validated-run") ||
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
        } else if (argument == "--scenario") {
            if (scenario_seen) return std::nullopt;
            const auto parsed = parse_scenario(value);
            if (!parsed) return std::nullopt;
            scenario_seen = true;
            options.scenario = *parsed;
        } else if (argument == "--publication-stage") {
            if (publication_seen || value != "prepublication") {
                return std::nullopt;
            }
            publication_seen = true;
            options.prepublication = true;
        } else if (argument == "--campaign-refresh-implementation-commit") {
            if (campaign_refresh_seen || value.size() != 40U ||
                !std::ranges::all_of(value, [](const char character) {
                    return (character >= '0' && character <= '9') ||
                           (character >= 'a' && character <= 'f');
                }) ||
                std::ranges::all_of(value, [](const char character) {
                    return character == '0';
                })) {
                return std::nullopt;
            }
            campaign_refresh_seen = true;
            options.campaign_refresh_implementation_commit = std::string{value};
        } else {
            const bool valid_run_id = value.size() == 32U &&
                std::ranges::all_of(value, [](const char character) {
                    return (character >= '0' && character <= '9') ||
                           (character >= 'a' && character <= 'f');
                });
            if (!valid_run_id ||
                options.independent_walker_validated_runs.size() >= 24U ||
                std::ranges::find(
                    options.independent_walker_validated_runs, value) !=
                    options.independent_walker_validated_runs.end()) {
                return std::nullopt;
            }
            options.independent_walker_validated_runs.emplace_back(value);
        }
    }
    std::ranges::sort(options.independent_walker_validated_runs);
    return root_seen && scenario_seen &&
            (!options.prepublication ||
             options.scenario == Scenario::first_observation) &&
            (!options.campaign_refresh_implementation_commit ||
             options.scenario == Scenario::campaign_summary) &&
            (options.independent_walker_validated_runs.empty() ||
             options.scenario == Scenario::campaign_summary)
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

[[nodiscard]] bool is_lower_hex_commit(const std::string_view value) noexcept
{
    return value.size() == 40U &&
           std::ranges::all_of(value, [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

[[nodiscard]] bool is_lower_hex_sha256(const std::string_view value) noexcept
{
    return value.size() == 64U &&
           std::ranges::all_of(value, [](const char character) {
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

[[nodiscard]] std::optional<fs::path> validate_campaign_root(
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
    if (error || root != approved || !fs::is_directory(root, error) || error ||
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

enum class TopLevelJsonKind { string, integer, boolean, compound, null_value };

struct TopLevelJsonValue final {
    TopLevelJsonKind kind{TopLevelJsonKind::null_value};
    std::string value;
};

using TopLevelJsonObject =
    std::map<std::string, TopLevelJsonValue, std::less<>>;

[[nodiscard]] bool parse_top_level_json_object(
    const std::string_view text,
    TopLevelJsonObject& result)
{
    std::size_t cursor = 0U;
    const auto whitespace = [&] {
        while (cursor < text.size() &&
               (text[cursor] == ' ' || text[cursor] == '\t' ||
                text[cursor] == '\r' || text[cursor] == '\n')) {
            ++cursor;
        }
    };
    const auto quoted = [&]() -> std::optional<std::string> {
        if (cursor >= text.size() || text[cursor++] != '"') {
            return std::nullopt;
        }
        std::string value;
        while (cursor < text.size() && text[cursor] != '"') {
            const auto character = text[cursor++];
            // All project-owned metadata values used here are canonical ASCII.
            // Reject escapes instead of accepting ambiguous alternative forms.
            if (character == '\\' ||
                static_cast<unsigned char>(character) < 0x20U) {
                return std::nullopt;
            }
            value.push_back(character);
        }
        if (cursor >= text.size() || text[cursor++] != '"') {
            return std::nullopt;
        }
        return value;
    };

    whitespace();
    if (cursor >= text.size() || text[cursor++] != '{') return false;
    whitespace();
    if (cursor < text.size() && text[cursor] == '}') {
        ++cursor;
        whitespace();
        return cursor == text.size();
    }
    while (cursor < text.size()) {
        whitespace();
        const auto name = quoted();
        if (!name || name->empty()) return false;
        whitespace();
        if (cursor >= text.size() || text[cursor++] != ':') return false;
        whitespace();

        TopLevelJsonValue value;
        if (cursor < text.size() && text[cursor] == '"') {
            const auto parsed = quoted();
            if (!parsed) return false;
            value.kind = TopLevelJsonKind::string;
            value.value = *parsed;
        } else if (cursor < text.size() &&
                   (text[cursor] == '{' || text[cursor] == '[')) {
            value.kind = TopLevelJsonKind::compound;
            const auto compound_begin = cursor;
            const char opening = text[cursor++];
            const char closing = opening == '{' ? '}' : ']';
            std::size_t depth = 1U;
            bool in_string = false;
            while (cursor < text.size() && depth != 0U) {
                const auto character = text[cursor++];
                if (in_string) {
                    if (character == '\\') {
                        if (cursor >= text.size()) return false;
                        ++cursor;
                    } else if (character == '"') {
                        in_string = false;
                    }
                    continue;
                }
                if (character == '"') in_string = true;
                else if (character == opening) ++depth;
                else if (character == closing) --depth;
            }
            if (depth != 0U || in_string) return false;
            value.value = std::string{
                text.substr(compound_begin, cursor - compound_begin)};
        } else {
            const auto begin = cursor;
            while (cursor < text.size() && text[cursor] != ',' &&
                   text[cursor] != '}' && text[cursor] != ' ' &&
                   text[cursor] != '\t' && text[cursor] != '\r' &&
                   text[cursor] != '\n') {
                ++cursor;
            }
            value.value = std::string{text.substr(begin, cursor - begin)};
            if (value.value == "true" || value.value == "false") {
                value.kind = TopLevelJsonKind::boolean;
            } else if (value.value == "null") {
                value.kind = TopLevelJsonKind::null_value;
            } else if (!value.value.empty() &&
                       std::ranges::all_of(value.value, [](const char character) {
                           return character >= '0' && character <= '9';
                       })) {
                value.kind = TopLevelJsonKind::integer;
            } else {
                return false;
            }
        }
        if (!result.emplace(*name, std::move(value)).second) return false;
        whitespace();
        if (cursor >= text.size()) return false;
        if (text[cursor] == '}') {
            ++cursor;
            whitespace();
            return cursor == text.size();
        }
        if (text[cursor++] != ',') return false;
    }
    return false;
}

[[nodiscard]] bool exact_top_level_properties(
    const TopLevelJsonObject& object,
    const std::span<const std::string_view> expected) noexcept
{
    if (object.size() != expected.size()) return false;
    return std::ranges::all_of(expected, [&object](const auto name) {
        return object.contains(name);
    });
}

[[nodiscard]] std::optional<std::vector<TopLevelJsonObject>>
parse_top_level_object_array(const std::string_view text)
{
    std::size_t cursor = 0U;
    const auto whitespace = [&] {
        while (cursor < text.size() &&
               (text[cursor] == ' ' || text[cursor] == '\t' ||
                text[cursor] == '\r' || text[cursor] == '\n')) {
            ++cursor;
        }
    };
    whitespace();
    if (cursor >= text.size() || text[cursor++] != '[') return std::nullopt;
    std::vector<TopLevelJsonObject> result;
    whitespace();
    if (cursor < text.size() && text[cursor] == ']') {
        ++cursor;
        whitespace();
        return cursor == text.size()
            ? std::optional<std::vector<TopLevelJsonObject>>{std::move(result)}
            : std::nullopt;
    }
    while (cursor < text.size()) {
        whitespace();
        if (cursor >= text.size() || text[cursor] != '{') return std::nullopt;
        const auto begin = cursor;
        std::size_t depth = 0U;
        bool in_string = false;
        do {
            const auto character = text[cursor++];
            if (in_string) {
                if (character == '\\') {
                    if (cursor >= text.size()) return std::nullopt;
                    ++cursor;
                } else if (character == '"') {
                    in_string = false;
                }
            } else if (character == '"') {
                in_string = true;
            } else if (character == '{') {
                ++depth;
            } else if (character == '}') {
                if (depth == 0U) return std::nullopt;
                --depth;
            }
        } while (cursor < text.size() && depth != 0U);
        if (depth != 0U || in_string) return std::nullopt;
        TopLevelJsonObject object;
        if (!parse_top_level_json_object(
                text.substr(begin, cursor - begin), object)) {
            return std::nullopt;
        }
        result.push_back(std::move(object));
        whitespace();
        if (cursor >= text.size()) return std::nullopt;
        if (text[cursor] == ']') {
            ++cursor;
            whitespace();
            return cursor == text.size()
                ? std::optional<std::vector<TopLevelJsonObject>>{
                      std::move(result)}
                : std::nullopt;
        }
        if (text[cursor++] != ',') return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] const TopLevelJsonValue* top_level_property(
    const TopLevelJsonObject& object,
    const std::string_view name,
    const TopLevelJsonKind kind) noexcept
{
    const auto found = object.find(name);
    return found != object.end() && found->second.kind == kind
        ? &found->second
        : nullptr;
}

[[nodiscard]] std::optional<std::size_t> top_level_integer(
    const TopLevelJsonObject& object,
    const std::string_view name,
    const std::size_t maximum) noexcept
{
    const auto* value = top_level_property(
        object, name, TopLevelJsonKind::integer);
    if (value == nullptr) return std::nullopt;
    std::size_t result = 0U;
    const auto parsed = std::from_chars(
        value->value.data(), value->value.data() + value->value.size(), result);
    return parsed.ec == std::errc{} &&
           parsed.ptr == value->value.data() + value->value.size() &&
           result <= maximum
        ? std::optional<std::size_t>{result}
        : std::nullopt;
}

[[nodiscard]] std::optional<bool> top_level_boolean(
    const TopLevelJsonObject& object,
    const std::string_view name) noexcept
{
    const auto* value = top_level_property(
        object, name, TopLevelJsonKind::boolean);
    if (value == nullptr) return std::nullopt;
    return value->value == "true" ? std::optional<bool>{true}
         : value->value == "false" ? std::optional<bool>{false}
                                    : std::nullopt;
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

[[nodiscard]] std::optional<std::string> replay_structure_sha256(
    const std::string_view run_id,
    const goldsrc::StockPostResourceResponseCursor& cursor,
    const std::size_t candidate_bit_width,
    const std::string_view candidate)
{
    // This digest is intentionally metadata-only. It binds the exact replay
    // cursor and neutral candidate geometry, but never hashes or embeds the
    // auth-bearing source payload.
    std::string canonical{"hlclient.stock-runtime-replay-structure.v1"};
    const auto append = [&canonical](const std::string_view name,
                                     const std::string_view value) {
        canonical.push_back('|');
        canonical.append(name);
        canonical.push_back('=');
        canonical.append(value);
    };
    const auto append_number = [&append](const std::string_view name,
                                         const auto value) {
        append(name, std::to_string(value));
    };
    append("run", run_id);
    append_number("replay-payload", cursor.replay_payload_ordinal);
    append_number("observed", cursor.corpus_observed_ordinal);
    append_number("delivery", cursor.delivery_ordinal);
    append_number("byte", cursor.byte_offset);
    append_number("bit", cursor.bit_offset);
    append_number("source-sequence", cursor.source_netchan_sequence);
    append_number("source-bytes", cursor.source_payload_byte_count);
    append_number("source-bits", cursor.source_payload_bit_count);
    append_number("remaining-bits", cursor.next_unconsumed_bit_count);
    append("reassembled", cursor.reassembled ? "true" : "false");
    append("decompressed", cursor.decompressed ? "true" : "false");
    append_number("candidate-width", candidate_bit_width);
    append("candidate", candidate);
    const auto bytes = std::as_bytes(
        std::span{canonical.data(), canonical.size()});
    const auto digest = hlclient::hash::sha256(bytes);
    return digest
        ? std::optional<std::string>{hlclient::hash::sha256_hex(*digest)}
        : std::nullopt;
}

struct DeliveredNetchanCounts final {
    std::size_t sequenced_client_to_server{0U};
    std::size_t sequenced_server_to_client{0U};
    std::size_t fragment_datagrams{0U};
};

[[nodiscard]] std::optional<DeliveredNetchanCounts>
delivered_netchan_counts(
    const goldsrc::StockRuntimeCaptureCorpusState& corpus)
{
    DeliveredNetchanCounts counts;
    for (const auto& datagram : corpus.delivered_datagrams()) {
        const auto classification =
            goldsrc::classify_netchan_datagram(datagram.bytes());
        if (classification.classification ==
            goldsrc::NetchanDatagramClassification::connectionless) {
            continue;
        }
        if (classification.classification !=
            goldsrc::NetchanDatagramClassification::sequenced) {
            return std::nullopt;
        }
        const auto header = goldsrc::peek_netchan_header(datagram.bytes());
        if (!header || !header.packet) return std::nullopt;
        if (datagram.direction() ==
            goldsrc::StockRuntimeCaptureDirection::client_to_server) {
            ++counts.sequenced_client_to_server;
        } else {
            ++counts.sequenced_server_to_client;
        }
        if (header.packet->header.sequence.flags.fragmented) {
            ++counts.fragment_datagrams;
        }
    }
    return counts;
}

[[nodiscard]] bool accepted_manifest_matches_replay(
    const goldsrc::StockRuntimeCaptureCorpusState& corpus,
    const goldsrc::StockRuntimeTransportReplayState& transport,
    const goldsrc::StockPostResourceResponseCursor& cursor,
    const goldsrc::StockRuntimeFirstObservationState& first,
    const std::string_view candidate,
    const std::string_view replay_structural_sha256)
{
    const auto& claimed = corpus.accepted_manifest_claims();
    if (!claimed) return false;

    goldsrc::StockRuntimeAcceptedManifestClaims observed;
    observed.reassembled_payload_count = transport.reassembled_payload_count();
    observed.decompressed_payload_count = transport.decompressed_payload_count();
    observed.replay_payload_ordinal = cursor.replay_payload_ordinal;
    observed.corpus_observed_ordinal = cursor.corpus_observed_ordinal;
    observed.delivery_ordinal = cursor.delivery_ordinal;
    observed.byte_offset = cursor.byte_offset;
    observed.bit_offset = cursor.bit_offset;
    observed.source_netchan_sequence = cursor.source_netchan_sequence;
    observed.source_payload_byte_count = cursor.source_payload_byte_count;
    observed.source_payload_bit_count = cursor.source_payload_bit_count;
    observed.next_unconsumed_bit_count = cursor.next_unconsumed_bit_count;
    observed.reassembled = cursor.reassembled;
    observed.decompressed = cursor.decompressed;
    observed.byte_aligned = first.byte_aligned();
    observed.first_candidate = candidate;
    observed.candidate_bit_width = first.candidate_bit_width();
    observed.candidate_recurrence = first.recurrence_count();
    observed.candidate_stability = goldsrc::to_string(first.stability());
    observed.replay_structural_sha256 = replay_structural_sha256;
    return *claimed == observed;
}

[[nodiscard]] bool legacy_scenario(const Scenario scenario) noexcept
{
    switch (scenario) {
    case Scenario::transcript:
    case Scenario::baselines:
    case Scenario::entities:
    case Scenario::clientdata:
    case Scenario::authority:
    case Scenario::ack:
        return true;
    case Scenario::transport:
    case Scenario::netchan:
    case Scenario::signon_replay:
    case Scenario::post_resource_first:
    case Scenario::first_observation:
    case Scenario::campaign_summary:
        return false;
    }
    return false;
}

[[nodiscard]] int offline_failure(
    const std::string_view category,
    const std::string_view detail,
    const int exit_code)
{
    std::cerr << "[stock-runtime] " << category << '=' << detail << '\n'
              << "[stock-runtime] accepted-run=false\n"
              << "[stock-runtime] publication-ready=false\n"
              << "[stock-runtime] result=" << detail << '\n';
    return exit_code;
}

[[nodiscard]] int run_reconnect_first_observation(
    const goldsrc::StockRuntimeCaptureCorpusState& corpus,
    const fs::path& capture_root,
    bool published_accepted,
    bool publication_ready);

[[nodiscard]] int run_offline_scenario(
    const Options& options,
    const fs::path& capture_root)
{
    const auto policy = options.prepublication
        ? goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication
        : goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published;
    const goldsrc::StockRuntimeCaptureCorpusLoader loader;
    const auto corpus_result = loader.load(capture_root, policy);
    if (!corpus_result || !corpus_result.state) {
        return offline_failure(
            "corpus", corpus_result.error
                ? goldsrc::to_string(corpus_result.error->code)
                : "invalid_corpus", 20);
    }
    const auto& corpus = *corpus_result.state;
    const bool published_accepted =
        corpus.publication_state() ==
        goldsrc::StockRuntimeCaptureCorpusPublicationState::published_accepted;
    const bool publication_ready = options.prepublication
        ? corpus.publication_state() ==
              goldsrc::StockRuntimeCaptureCorpusPublicationState::
                  ready_for_manifest_publication
        : published_accepted;
    if (!publication_ready) {
        return offline_failure("corpus", "publication_state_mismatch", 21);
    }

    if (options.scenario == Scenario::transport) {
        std::cout << "[stock-runtime] profile="
                  << goldsrc::kStockRuntimePendingProfile << '\n'
                  << "[stock-runtime] transport-valid=true\n"
                  << "[stock-runtime] observed-datagrams="
                  << corpus.observed_datagrams().size() << '\n'
                  << "[stock-runtime] delivered-datagrams="
                  << corpus.delivered_datagrams().size() << '\n'
                  << "[stock-runtime] structural-hash="
                  << corpus.structural_sha256() << '\n'
                  << "[stock-runtime] accepted-run=false\n"
                  << "[stock-runtime] result=transport\n";
        return 0;
    }

    if (options.scenario == Scenario::first_observation &&
        corpus.capture_metadata().scenario ==
            goldsrc::StockRuntimeCaptureScenario::reconnect) {
        return run_reconnect_first_observation(
            corpus, capture_root, published_accepted, publication_ready);
    }

    const goldsrc::StockRuntimeTransportReplay transport_replay;
    const auto transport = transport_replay.replay(corpus);
    if (!transport || !transport.state) {
        return offline_failure(
            "netchan-replay", transport.error
                ? goldsrc::to_string(transport.error->code)
                : "netchan_replay_failed", 22);
    }
    if (options.scenario == Scenario::netchan) {
        std::cout << "[stock-runtime] profile="
                  << goldsrc::kStockRuntimePendingProfile << '\n'
                  << "[stock-runtime] transport-valid=true\n"
                  << "[stock-runtime] sequenced-c2s="
                  << transport.state->sequenced_client_to_server_count() << '\n'
                  << "[stock-runtime] sequenced-s2c="
                  << transport.state->sequenced_server_to_client_count() << '\n'
                  << "[stock-runtime] fragments="
                  << transport.state->fragment_packet_count() << '\n'
                  << "[stock-runtime] reassembled="
                  << transport.state->reassembled_payload_count() << '\n'
                  << "[stock-runtime] decompressed="
                  << transport.state->decompressed_payload_count() << '\n'
                  << "[stock-runtime] accepted-run=false\n"
                  << "[stock-runtime] result=netchan\n";
        return 0;
    }

    const goldsrc::StockCapturedSignonReplay signon_replay;
    const auto signon = signon_replay.replay(*transport.state);
    if (!signon || !signon.state) {
        return offline_failure(
            "signon-replay", signon.error
                ? goldsrc::to_string(signon.error->code)
                : "signon_sequence_incomplete", 23);
    }
    if (options.scenario == Scenario::signon_replay) {
        std::cout << "[stock-runtime] signon-replay=complete\n"
                  << "[stock-runtime] observed-client-requests="
                  << signon.state->observed_client_request_count() << '\n'
                  << "[stock-runtime] decoded-signon-payloads="
                  << signon.state->decoded_server_signon_payload_count() << '\n'
                  << "[stock-runtime] generated-ack=false\n"
                  << "[stock-runtime] generated-client-request=false\n"
                  << "[stock-runtime] accepted-run=false\n"
                  << "[stock-runtime] result=signon-replay\n";
        return 0;
    }
    if (options.scenario == Scenario::post_resource_first) {
        const auto& cursor = signon.state->cursor();
        std::cout << "[stock-runtime] signon-replay=complete\n"
                  << "[stock-runtime] post-resource-boundary=observed\n"
                  << "[stock-runtime] boundary-payload-ordinal="
                  << cursor.replay_payload_ordinal << '\n'
                  << "[stock-runtime] boundary-observed-ordinal="
                  << cursor.corpus_observed_ordinal << '\n'
                  << "[stock-runtime] boundary-delivery-ordinal="
                  << cursor.delivery_ordinal << '\n'
                  << "[stock-runtime] boundary-byte-offset="
                  << cursor.byte_offset << '\n'
                  << "[stock-runtime] boundary-bit-offset="
                  << cursor.bit_offset << '\n'
                  << "[stock-runtime] boundary-source-sequence="
                  << cursor.source_netchan_sequence << '\n'
                  << "[stock-runtime] boundary-source-payload-bytes="
                  << cursor.source_payload_byte_count << '\n'
                  << "[stock-runtime] boundary-source-payload-bits="
                  << cursor.source_payload_bit_count << '\n'
                  << "[stock-runtime] boundary-next-unconsumed-bits="
                  << cursor.next_unconsumed_bit_count << '\n'
                  << "[stock-runtime] boundary-reassembled="
                  << (cursor.reassembled ? "true" : "false") << '\n'
                  << "[stock-runtime] boundary-decompressed="
                  << (cursor.decompressed ? "true" : "false") << '\n'
                  << "[stock-runtime] accepted-run=false\n"
                  << "[stock-runtime] result=post-resource-first\n";
        return 0;
    }

    const auto& cursor = signon.state->cursor();
    if (cursor.replay_payload_ordinal >= transport.state->payloads().size()) {
        return offline_failure(
            "post-resource-boundary", "post_resource_cursor_unavailable", 24);
    }
    const auto& source_payload =
        transport.state->payloads()[cursor.replay_payload_ordinal];
    goldsrc::StockRuntimeFirstObservationInput input;
    input.run_id = std::string{corpus.run_id()};
    input.version_profile = corpus.version_observation().structural_sha256;
    input.cursor = cursor;
    input.source_payload = source_payload.bytes();
    // A prepublication corpus has passed every local gate but cannot call
    // itself accepted until the final manifest is atomically published.
    input.accepted_evidence_run = publication_ready;
    input.known_signon_validated = signon.state->known_signon_validated();
    const std::array observations{input};
    const goldsrc::StockRuntimeFirstObservationBuilder builder;
    const auto first = builder.build(observations);
    if (!first || !first.state) {
        return offline_failure(
            "first-observation", first.error
                ? goldsrc::to_string(first.error->code)
                : "missing_candidate", 25);
    }
    std::string candidate = "pending";
    if (first.state->numeric_candidate()) {
        candidate = std::to_string(*first.state->numeric_candidate());
    } else if (first.state->bounded_bit_prefix()) {
        candidate = "bit-prefix:" +
            std::to_string(*first.state->bounded_bit_prefix());
    }
    const auto replay_hash = replay_structure_sha256(
        corpus.run_id(), cursor, first.state->candidate_bit_width(), candidate);
    if (!replay_hash) {
        return offline_failure(
            "first-observation", "replay_structural_hash_failed", 27);
    }
    const auto delivered_counts = delivered_netchan_counts(corpus);
    if (!delivered_counts) {
        return offline_failure(
            "first-observation", "delivered_netchan_count_failed", 28);
    }
    if (published_accepted &&
        !accepted_manifest_matches_replay(
            corpus, *transport.state, cursor, *first.state, candidate,
            *replay_hash)) {
        return offline_failure(
            "manifest-binding", "accepted_manifest_mismatch", 29);
    }
    std::cout << "[stock-runtime] profile="
              << goldsrc::kStockRuntimePendingProfile << '\n'
              << "[stock-runtime] transport-valid=true\n"
              // These existing values are the replay-accepted-new subset.
              << "[stock-runtime] sequenced-c2s="
              << transport.state->sequenced_client_to_server_count() << '\n'
              << "[stock-runtime] sequenced-s2c="
              << transport.state->sequenced_server_to_client_count() << '\n'
              << "[stock-runtime] fragments="
              << transport.state->fragment_packet_count() << '\n'
              << "[stock-runtime] duplicate-packets="
              << transport.state->duplicate_packet_count() << '\n'
              << "[stock-runtime] old-packets="
              << transport.state->old_packet_count() << '\n'
              // Delivery counts retain duplicate and reordered-old emissions.
              << "[stock-runtime] delivered-sequenced-c2s="
              << delivered_counts->sequenced_client_to_server << '\n'
              << "[stock-runtime] delivered-sequenced-s2c="
              << delivered_counts->sequenced_server_to_client << '\n'
              << "[stock-runtime] delivered-fragment-datagrams="
              << delivered_counts->fragment_datagrams << '\n'
              << "[stock-runtime] reassembled="
              << transport.state->reassembled_payload_count() << '\n'
              << "[stock-runtime] decompressed="
              << transport.state->decompressed_payload_count() << '\n'
              << "[stock-runtime] signon-replay=complete\n"
              << "[stock-runtime] post-resource-boundary=observed\n"
              << "[stock-runtime] boundary-payload-ordinal="
              << cursor.replay_payload_ordinal << '\n'
              << "[stock-runtime] boundary-observed-ordinal="
              << cursor.corpus_observed_ordinal << '\n'
              << "[stock-runtime] boundary-delivery-ordinal="
              << cursor.delivery_ordinal << '\n'
              << "[stock-runtime] boundary-byte-offset="
              << cursor.byte_offset << '\n'
              << "[stock-runtime] boundary-bit-offset="
              << cursor.bit_offset << '\n'
              << "[stock-runtime] boundary-source-sequence="
              << cursor.source_netchan_sequence << '\n'
              << "[stock-runtime] boundary-source-payload-bytes="
              << cursor.source_payload_byte_count << '\n'
              << "[stock-runtime] boundary-source-payload-bits="
              << cursor.source_payload_bit_count << '\n'
              << "[stock-runtime] boundary-next-unconsumed-bits="
              << cursor.next_unconsumed_bit_count << '\n'
              << "[stock-runtime] boundary-reassembled="
              << (cursor.reassembled ? "true" : "false") << '\n'
              << "[stock-runtime] boundary-decompressed="
              << (cursor.decompressed ? "true" : "false") << '\n'
              << "[stock-runtime] boundary-byte-aligned="
              << (first.state->byte_aligned() ? "true" : "false") << '\n'
              << "[stock-runtime] candidate-bit-width="
              << first.state->candidate_bit_width() << '\n'
              << "[stock-runtime] first-candidate=" << candidate << '\n'
              << "[stock-runtime] candidate-recurrence="
              << first.state->recurrence_count() << '\n'
              << "[stock-runtime] candidate-stability="
              << goldsrc::to_string(first.state->stability()) << '\n'
              << "[stock-runtime] accepted-run="
              << (published_accepted ? "true" : "false") << '\n'
              << "[stock-runtime] publication-ready="
              << (publication_ready ? "true" : "false") << '\n'
              << "[stock-runtime] result=first-observation\n"
              << "[stock-runtime] structural-hash="
              << corpus.structural_sha256() << '\n'
              << "[stock-runtime] replay-structural-hash="
              << *replay_hash << '\n';
    return 0;
}

[[nodiscard]] std::string candidate_representation(
    const goldsrc::StockRuntimeFirstObservationState& observation)
{
    if (observation.numeric_candidate()) {
        return std::to_string(*observation.numeric_candidate());
    }
    return observation.bounded_bit_prefix()
        ? "bit-prefix:" +
              std::to_string(*observation.bounded_bit_prefix())
        : "pending";
}

struct ReconnectTransportGenerationFacts final {
    std::size_t ordinal{0U};
    std::string endpoint_role;
    std::string process_role;
    std::size_t first_observed{0U};
    std::size_t last_observed{0U};
    std::size_t connectionless_count{0U};
    bool connect_observed{false};
    bool accept_observed{false};
    std::size_t first_sequenced_observed{0U};
    std::size_t client_packets{0U};
    std::size_t server_packets{0U};
};

struct ReconnectStagedFacts final {
    std::array<ReconnectTransportGenerationFacts, 2U> generations;
    std::size_t retired_generation_a_server_tail_packet_count{0U};
    bool generation_distinct{false};
    bool generation_a_controlled_shutdown{false};
    bool generation_a_endpoint_quiet{false};
    bool generation_b_fresh_owned_process{false};
    bool guard_continuity{false};
    bool server_continuity{false};
    bool relay_continuity{false};
    bool cleanup_exact{false};
};

[[nodiscard]] bool exact_string_property(
    const TopLevelJsonObject& object,
    const std::string_view name,
    const std::string_view expected) noexcept
{
    const auto* value = top_level_property(
        object, name, TopLevelJsonKind::string);
    return value != nullptr && value->value == expected;
}

[[nodiscard]] bool exact_boolean_property(
    const TopLevelJsonObject& object,
    const std::string_view name,
    const bool expected) noexcept
{
    const auto value = top_level_boolean(object, name);
    return value && *value == expected;
}

[[nodiscard]] std::optional<ReconnectStagedFacts> read_reconnect_staged_facts(
    const fs::path& run_root)
{
    static constexpr std::array transport_keys{
        std::string_view{"schema"},
        std::string_view{"connection_generation_count"},
        std::string_view{"generation_distinct"},
        std::string_view{"generation_a_tail_emitter_ready_before_shutdown"},
        std::string_view{"generation_a_controlled_shutdown"},
        std::string_view{"generation_a_endpoint_quiet"},
        std::string_view{"guard_continuity"},
        std::string_view{"server_continuity"},
        std::string_view{"relay_continuity"},
        std::string_view{"post_resource_boundary_status"},
        std::string_view{"candidate_status"},
        std::string_view{"candidate_body_consumed"},
        std::string_view{"candidate_semantic_category_assigned"},
        std::string_view{"retired_generation_a_tail_sink"},
        std::string_view{"retired_generation_a_server_tail_packet_count"},
        std::string_view{"generation_b_sequenced_after_fresh_accept"},
        std::string_view{"bounded_transport_complete"},
        std::string_view{"generations"},
    };
    static constexpr std::array generation_keys{
        std::string_view{"generation_ordinal"},
        std::string_view{"endpoint_role_identity"},
        std::string_view{"process_role_identity"},
        std::string_view{"first_observed_ordinal"},
        std::string_view{"last_observed_ordinal"},
        std::string_view{"connectionless_exchange_count"},
        std::string_view{"connect_observed"},
        std::string_view{"accept_observed"},
        std::string_view{"first_sequenced_packet_ordinal"},
        std::string_view{"client_to_server_packet_count"},
        std::string_view{"server_to_client_packet_count"},
        std::string_view{"profile_identity"},
        std::string_view{"post_resource_boundary_status"},
        std::string_view{"candidate_status"},
        std::string_view{"candidate_body_consumed"},
        std::string_view{"candidate_semantic_category_assigned"},
    };
    static constexpr std::array orchestration_keys{
        std::string_view{"schema"},
        std::string_view{"connection_generation_count"},
        std::string_view{"generation_distinct"},
        std::string_view{"generation_a_process_role_identity"},
        std::string_view{"generation_b_process_role_identity"},
        std::string_view{"generation_a_endpoint_role_identity"},
        std::string_view{"generation_b_endpoint_role_identity"},
        std::string_view{"generation_a_tail_emitter_ready_before_shutdown"},
        std::string_view{"generation_a_controlled_shutdown"},
        std::string_view{"generation_a_endpoint_quiet"},
        std::string_view{"generation_b_fresh_owned_process"},
        std::string_view{"generation_b_fresh_connection_lifecycle"},
        std::string_view{"guard_continuity"},
        std::string_view{"server_continuity"},
        std::string_view{"relay_continuity"},
        std::string_view{"cleanup_status"},
        std::string_view{"restoration_status"},
        std::string_view{"post_resource_boundary_status"},
        std::string_view{"candidate_status"},
        std::string_view{"candidate_body_consumed"},
        std::string_view{"candidate_semantic_category_assigned"},
        std::string_view{"publication_status"},
    };

    const auto transport_text = read_metadata(
        run_root / "reconnect-transport-observation.staged.json");
    const auto orchestration_text = read_metadata(
        run_root / "reconnect-orchestration.staged.json");
    TopLevelJsonObject transport;
    TopLevelJsonObject orchestration;
    if (!transport_text || !orchestration_text ||
        !parse_top_level_json_object(*transport_text, transport) ||
        !parse_top_level_json_object(*orchestration_text, orchestration) ||
        !exact_top_level_properties(transport, transport_keys) ||
        !exact_top_level_properties(orchestration, orchestration_keys) ||
        !exact_string_property(
            transport, "schema",
            goldsrc::kStockRuntimeReconnectTransportObservationSchema) ||
        !exact_string_property(
            orchestration, "schema",
            goldsrc::kStockRuntimeReconnectOrchestrationAttestationSchema) ||
        top_level_integer(transport, "connection_generation_count", 2U) != 2U ||
        top_level_integer(orchestration, "connection_generation_count", 2U) != 2U ||
        !exact_boolean_property(transport, "generation_distinct", true) ||
        !exact_boolean_property(orchestration, "generation_distinct", true) ||
        !exact_boolean_property(
            transport, "generation_a_tail_emitter_ready_before_shutdown",
            true) ||
        !exact_string_property(
            transport, "generation_a_controlled_shutdown",
            "observed_by_orchestrator") ||
        !exact_boolean_property(transport, "generation_a_endpoint_quiet", true) ||
        !exact_string_property(
            transport, "guard_continuity", "observed_by_orchestrator") ||
        !exact_string_property(
            transport, "server_continuity", "observed_by_orchestrator") ||
        !exact_string_property(transport, "relay_continuity", "observed") ||
        !exact_string_property(
            transport, "post_resource_boundary_status", "evidence_pending") ||
        !exact_string_property(transport, "candidate_status", "evidence_pending") ||
        !exact_boolean_property(transport, "candidate_body_consumed", false) ||
        !exact_boolean_property(
            transport, "candidate_semantic_category_assigned", false) ||
        !exact_string_property(
            transport, "retired_generation_a_tail_sink", "routing_only") ||
        !top_level_integer(
            transport, "retired_generation_a_server_tail_packet_count",
            65'536U) ||
        !exact_boolean_property(
            transport, "generation_b_sequenced_after_fresh_accept", true) ||
        !exact_boolean_property(transport, "bounded_transport_complete", true) ||
        !exact_string_property(
            orchestration, "generation_a_process_role_identity",
            goldsrc::kStockRuntimeGenerationAProcessRole) ||
        !exact_string_property(
            orchestration, "generation_b_process_role_identity",
            goldsrc::kStockRuntimeGenerationBProcessRole) ||
        !exact_string_property(
            orchestration, "generation_a_endpoint_role_identity",
            goldsrc::kStockRuntimeGenerationAEndpointRole) ||
        !exact_string_property(
            orchestration, "generation_b_endpoint_role_identity",
            goldsrc::kStockRuntimeGenerationBEndpointRole) ||
        !exact_boolean_property(
            orchestration,
            "generation_a_tail_emitter_ready_before_shutdown", true) ||
        !exact_boolean_property(
            orchestration, "generation_a_controlled_shutdown", true) ||
        !exact_boolean_property(
            orchestration, "generation_a_endpoint_quiet", true) ||
        !exact_boolean_property(
            orchestration, "generation_b_fresh_owned_process", true) ||
        !exact_string_property(
            orchestration, "generation_b_fresh_connection_lifecycle",
            "observed_by_relay") ||
        !exact_boolean_property(orchestration, "guard_continuity", true) ||
        !exact_boolean_property(orchestration, "server_continuity", true) ||
        !exact_boolean_property(orchestration, "relay_continuity", true) ||
        !exact_string_property(orchestration, "cleanup_status", "exact") ||
        !exact_string_property(
            orchestration, "restoration_status", "wrapper_pending") ||
        !exact_string_property(
            orchestration, "post_resource_boundary_status", "evidence_pending") ||
        !exact_string_property(
            orchestration, "candidate_status", "evidence_pending") ||
        !exact_boolean_property(
            orchestration, "candidate_body_consumed", false) ||
        !exact_boolean_property(
            orchestration, "candidate_semantic_category_assigned", false) ||
        !exact_string_property(orchestration, "publication_status", "staged")) {
        return std::nullopt;
    }
    const auto* generation_array = top_level_property(
        transport, "generations", TopLevelJsonKind::compound);
    const auto generation_objects = generation_array
        ? parse_top_level_object_array(generation_array->value)
        : std::nullopt;
    if (!generation_objects || generation_objects->size() != 2U) {
        return std::nullopt;
    }

    ReconnectStagedFacts result;
    result.retired_generation_a_server_tail_packet_count =
        *top_level_integer(
            transport, "retired_generation_a_server_tail_packet_count",
            65'536U);
    result.generation_distinct = true;
    result.generation_a_controlled_shutdown = true;
    result.generation_a_endpoint_quiet = true;
    result.generation_b_fresh_owned_process = true;
    result.guard_continuity = true;
    result.server_continuity = true;
    result.relay_continuity = true;
    result.cleanup_exact = true;
    const std::array expected_endpoints{
        goldsrc::kStockRuntimeGenerationAEndpointRole,
        goldsrc::kStockRuntimeGenerationBEndpointRole};
    const std::array expected_processes{
        goldsrc::kStockRuntimeGenerationAProcessRole,
        goldsrc::kStockRuntimeGenerationBProcessRole};
    for (std::size_t index = 0U; index < generation_objects->size(); ++index) {
        const auto& object = (*generation_objects)[index];
        const auto* endpoint = top_level_property(
            object, "endpoint_role_identity", TopLevelJsonKind::string);
        const auto* process = top_level_property(
            object, "process_role_identity", TopLevelJsonKind::string);
        const auto ordinal = top_level_integer(object, "generation_ordinal", 2U);
        const auto first = top_level_integer(
            object, "first_observed_ordinal", 65'535U);
        const auto last = top_level_integer(
            object, "last_observed_ordinal", 65'535U);
        const auto connectionless = top_level_integer(
            object, "connectionless_exchange_count", 65'536U);
        const auto first_sequence = top_level_integer(
            object, "first_sequenced_packet_ordinal", 65'535U);
        const auto c2s = top_level_integer(
            object, "client_to_server_packet_count", 65'536U);
        const auto s2c = top_level_integer(
            object, "server_to_client_packet_count", 65'536U);
        if (!exact_top_level_properties(object, generation_keys) ||
            !ordinal || *ordinal != index + 1U || !first || !last ||
            *first > *last || !connectionless || *connectionless == 0U ||
            !first_sequence || *first_sequence < *first ||
            *first_sequence > *last || !c2s || *c2s == 0U ||
            !s2c || *s2c == 0U || endpoint == nullptr || process == nullptr ||
            endpoint->value != expected_endpoints[index] ||
            process->value != expected_processes[index] ||
            !exact_boolean_property(object, "connect_observed", true) ||
            !exact_boolean_property(object, "accept_observed", true) ||
            !exact_string_property(
                object, "profile_identity", goldsrc::kStockRuntimePendingProfile) ||
            !exact_string_property(
                object, "post_resource_boundary_status", "evidence_pending") ||
            !exact_string_property(object, "candidate_status", "evidence_pending") ||
            !exact_boolean_property(object, "candidate_body_consumed", false) ||
            !exact_boolean_property(
                object, "candidate_semantic_category_assigned", false)) {
            return std::nullopt;
        }
        result.generations[index] = ReconnectTransportGenerationFacts{
            *ordinal, endpoint->value, process->value, *first, *last,
            *connectionless, true, true, *first_sequence, *c2s, *s2c};
    }
    if (result.generations[0U].last_observed >=
        result.generations[1U].first_observed) {
        return std::nullopt;
    }
    return result;
}

struct ReconnectGenerationReplay final {
    goldsrc::StockRuntimeConnectionGenerationObservation lifecycle;
    goldsrc::StockPostResourceResponseCursor global_cursor;
    std::string candidate;
    std::string replay_structural_sha256;
    std::size_t replay_sequenced_c2s{0U};
    std::size_t replay_sequenced_s2c{0U};
    std::size_t replay_fragments{0U};
    std::size_t replay_duplicates{0U};
    std::size_t replay_old_packets{0U};
    std::size_t replay_reassembled{0U};
    std::size_t replay_decompressed{0U};
};

struct ReconnectReplayResult final {
    std::array<ReconnectGenerationReplay, 2U> generations;
    std::optional<goldsrc::StockRuntimeReconnectLifecycleState> lifecycle;
    std::size_t retired_generation_a_server_tail_packet_count{0U};
};

[[nodiscard]] bool validate_reconnect_tail_journal(
    const goldsrc::StockRuntimeCaptureCorpusState& corpus,
    const ReconnectStagedFacts& staged) noexcept
{
    const auto range_begin = staged.generations[0U].last_observed;
    const auto range_end = staged.generations[1U].first_sequenced_observed;
    if (range_begin >= range_end) return false;
    std::size_t observed_tail = 0U;
    for (const auto& observed : corpus.observed_datagrams()) {
        const auto ordinal = observed.journal().observed_ordinal;
        if (ordinal <= range_begin || ordinal >= range_end) continue;
        const auto classification =
            goldsrc::classify_netchan_datagram(observed.bytes());
        if (classification.classification ==
            goldsrc::NetchanDatagramClassification::connectionless) {
            continue;
        }
        if (classification.classification !=
            goldsrc::NetchanDatagramClassification::sequenced) {
            return false;
        }
        if (observed.journal().direction ==
            goldsrc::StockRuntimeCaptureDirection::client_to_server) {
            continue;
        }
        ++observed_tail;
    }
    return observed_tail ==
        staged.retired_generation_a_server_tail_packet_count;
}

[[nodiscard]] std::optional<ReconnectGenerationReplay> replay_generation(
    const goldsrc::StockRuntimeCaptureCorpusState& corpus,
    const ReconnectTransportGenerationFacts& facts,
    const std::size_t generation_index)
{
    std::vector<goldsrc::StockRuntimeTransportReplayDatagram> datagrams;
    std::vector<std::size_t> global_delivery_ordinals;
    std::size_t observed_c2s = 0U;
    std::size_t observed_s2c = 0U;
    std::size_t observed_connectionless = 0U;
    std::optional<std::size_t> first_sequenced_s2c;
    std::size_t local_c2s_ordinal = 0U;
    std::size_t local_s2c_ordinal = 0U;
    try {
        for (const auto& observed : corpus.observed_datagrams()) {
            const auto ordinal = observed.journal().observed_ordinal;
            if (ordinal < facts.first_observed || ordinal > facts.last_observed) {
                continue;
            }
            const auto classification =
                goldsrc::classify_netchan_datagram(observed.bytes());
            const bool retired_a_tail = generation_index == 1U &&
                ordinal < facts.first_sequenced_observed &&
                observed.journal().direction ==
                    goldsrc::StockRuntimeCaptureDirection::server_to_client &&
                classification.classification ==
                    goldsrc::NetchanDatagramClassification::sequenced;
            if (retired_a_tail) continue;
            if (observed.journal().direction ==
                goldsrc::StockRuntimeCaptureDirection::client_to_server) {
                ++observed_c2s;
            } else {
                ++observed_s2c;
            }
            if (classification.classification ==
                goldsrc::NetchanDatagramClassification::connectionless) {
                ++observed_connectionless;
            } else if (classification.classification ==
                           goldsrc::NetchanDatagramClassification::sequenced &&
                       observed.journal().direction ==
                           goldsrc::StockRuntimeCaptureDirection::server_to_client &&
                       !first_sequenced_s2c) {
                first_sequenced_s2c = ordinal;
            }
        }
        for (const auto& delivered : corpus.delivered_datagrams()) {
            if (delivered.observed_ordinal() < facts.first_observed ||
                delivered.observed_ordinal() > facts.last_observed) {
                continue;
            }
            const auto classification =
                goldsrc::classify_netchan_datagram(delivered.bytes());
            if (generation_index == 1U &&
                delivered.observed_ordinal() < facts.first_sequenced_observed &&
                delivered.direction() ==
                    goldsrc::StockRuntimeCaptureDirection::server_to_client &&
                classification.classification ==
                    goldsrc::NetchanDatagramClassification::sequenced) {
                continue;
            }
            const bool c2s = delivered.direction() ==
                goldsrc::StockRuntimeCaptureDirection::client_to_server;
            auto& direction_ordinal = c2s ? local_c2s_ordinal : local_s2c_ordinal;
            ++direction_ordinal;
            goldsrc::StockRuntimeTransportReplayDatagram adapted;
            adapted.direction = delivered.direction();
            adapted.delivery_ordinal = datagrams.size();
            adapted.observed_ordinal = delivered.observed_ordinal();
            adapted.direction_ordinal = direction_ordinal;
            adapted.observed_relative_timestamp_us =
                delivered.observed_relative_timestamp_us();
            adapted.bytes.assign(
                delivered.bytes().begin(), delivered.bytes().end());
            datagrams.push_back(std::move(adapted));
            global_delivery_ordinals.push_back(delivered.delivery_ordinal());
        }
    } catch (...) {
        return std::nullopt;
    }
    if (observed_c2s != facts.client_packets ||
        observed_s2c != facts.server_packets ||
        observed_connectionless != facts.connectionless_count ||
        !first_sequenced_s2c ||
        *first_sequenced_s2c != facts.first_sequenced_observed ||
        datagrams.empty()) {
        return std::nullopt;
    }

    const goldsrc::StockRuntimeTransportReplay replay;
    const auto transport = replay.replay(datagrams);
    if (!transport || !transport.state ||
        transport.state->connectionless_datagram_count() !=
            facts.connectionless_count) {
        return std::nullopt;
    }
    const goldsrc::StockCapturedSignonReplay signon_replay;
    const auto signon = signon_replay.replay(*transport.state);
    if (!signon || !signon.state ||
        !signon.state->known_signon_validated()) {
        return std::nullopt;
    }
    const auto local_cursor = signon.state->cursor();
    if (local_cursor.replay_payload_ordinal >= transport.state->payloads().size() ||
        local_cursor.delivery_ordinal >= global_delivery_ordinals.size()) {
        return std::nullopt;
    }
    const auto& payload =
        transport.state->payloads()[local_cursor.replay_payload_ordinal];
    goldsrc::StockRuntimeFirstObservationInput first_input;
    first_input.run_id = std::string{corpus.run_id()};
    first_input.version_profile = corpus.version_observation().structural_sha256;
    first_input.cursor = local_cursor;
    first_input.source_payload = payload.bytes();
    first_input.accepted_evidence_run = true;
    first_input.known_signon_validated = true;
    const std::array inputs{first_input};
    const auto first =
        goldsrc::StockRuntimeFirstObservationBuilder{}.build(inputs);
    if (!first || !first.state) return std::nullopt;

    auto global_cursor = local_cursor;
    global_cursor.delivery_ordinal =
        global_delivery_ordinals[local_cursor.delivery_ordinal];
    const auto candidate = candidate_representation(*first.state);
    const auto replay_hash = replay_structure_sha256(
        corpus.run_id(), global_cursor, first.state->candidate_bit_width(),
        candidate);
    if (!replay_hash) return std::nullopt;

    goldsrc::StockRuntimeConnectionGenerationObservation lifecycle;
    lifecycle.generation_ordinal = generation_index + 1U;
    lifecycle.learned_client_endpoint_role_identity = facts.endpoint_role;
    lifecycle.owned_client_process_role_identity = facts.process_role;
    lifecycle.owned_client_process_observed = true;
    lifecycle.fresh_owned_client_process = true;
    lifecycle.learned_client_endpoint_observed = true;
    lifecycle.learned_client_endpoint_distinct_from_previous =
        generation_index != 0U;
    lifecycle.first_observed_ordinal = facts.first_observed;
    lifecycle.last_observed_ordinal = facts.last_observed;
    lifecycle.connectionless_exchange_count = facts.connectionless_count;
    lifecycle.connect_observed = facts.connect_observed;
    lifecycle.accept_observed = facts.accept_observed;
    lifecycle.first_sequenced_packet_ordinal = facts.first_sequenced_observed;
    lifecycle.exact_post_resource_boundary = {
        true,
        global_cursor.replay_payload_ordinal,
        global_cursor.corpus_observed_ordinal,
        global_cursor.delivery_ordinal,
        global_cursor.byte_offset,
        global_cursor.bit_offset,
        global_cursor.source_payload_byte_count,
        global_cursor.source_payload_bit_count,
        global_cursor.next_unconsumed_bit_count,
    };
    lifecycle.candidate_observation.observed = true;
    lifecycle.candidate_observation.candidate_bit_width =
        first.state->candidate_bit_width();
    lifecycle.candidate_observation.numeric_candidate =
        first.state->numeric_candidate();
    lifecycle.candidate_observation.bounded_bit_prefix =
        first.state->bounded_bit_prefix();
    lifecycle.candidate_observation.byte_aligned = first.state->byte_aligned();
    lifecycle.candidate_observation.body_consumed = first.state->body_consumed();
    lifecycle.candidate_observation.semantic_category_assigned =
        first.state->semantic_category_assigned();
    lifecycle.client_to_server_packet_count = facts.client_packets;
    lifecycle.server_to_client_packet_count = facts.server_packets;
    lifecycle.profile_identity = goldsrc::kStockRuntimePendingProfile;
    lifecycle.controlled_client_shutdown_observed = generation_index == 0U;
    lifecycle.retired_client_endpoint_quiet = generation_index == 0U;

    return ReconnectGenerationReplay{
        std::move(lifecycle), global_cursor, candidate, *replay_hash,
        transport.state->sequenced_client_to_server_count(),
        transport.state->sequenced_server_to_client_count(),
        transport.state->fragment_packet_count(),
        transport.state->duplicate_packet_count(),
        transport.state->old_packet_count(),
        transport.state->reassembled_payload_count(),
        transport.state->decompressed_payload_count()};
}

[[nodiscard]] std::optional<ReconnectReplayResult> replay_reconnect_generations(
    const goldsrc::StockRuntimeCaptureCorpusState& corpus,
    const fs::path& run_root)
{
    const auto staged = read_reconnect_staged_facts(run_root);
    if (!staged || !validate_reconnect_tail_journal(corpus, *staged)) {
        return std::nullopt;
    }
    ReconnectReplayResult result;
    result.retired_generation_a_server_tail_packet_count =
        staged->retired_generation_a_server_tail_packet_count;
    for (std::size_t index = 0U; index < result.generations.size(); ++index) {
        auto replayed = replay_generation(
            corpus, staged->generations[index], index);
        if (!replayed) return std::nullopt;
        result.generations[index] = std::move(*replayed);
    }
    std::array<goldsrc::StockRuntimeConnectionGenerationObservation, 2U>
        lifecycle_generations{
            result.generations[0U].lifecycle,
            result.generations[1U].lifecycle};
    goldsrc::StockRuntimeReconnectLifecycleInput input;
    input.generations = lifecycle_generations;
    input.guard_continuous_between_generations = staged->guard_continuity;
    input.server_continuous_between_generations = staged->server_continuity;
    input.relay_continuous_between_generations = staged->relay_continuity;
    input.cleanup_exact = staged->cleanup_exact;
    input.restoration_exact = true;
    input.transactional_publication_ready = true;
    auto validated = goldsrc::validate_stock_runtime_reconnect_lifecycle(input);
    if (!validated || !validated.state) return std::nullopt;
    result.lifecycle.emplace(std::move(*validated.state));
    return result;
}

[[nodiscard]] std::optional<std::string> reconnect_replay_structure_sha256(
    const std::string_view run_id,
    const ReconnectReplayResult& replay)
{
    std::string canonical{
        "hlclient.stock-runtime-reconnect-replay-structure.v1"};
    canonical.append("|run=").append(run_id);
    for (std::size_t index = 0U; index < replay.generations.size(); ++index) {
        const auto ordinal = index + 1U;
        const auto& generation = replay.generations[index];
        canonical.append("|generation-")
            .append(std::to_string(ordinal))
            .append("=")
            .append(generation.replay_structural_sha256);
        canonical.append("|observed-first-")
            .append(std::to_string(ordinal))
            .append("=")
            .append(std::to_string(generation.lifecycle.first_observed_ordinal));
        canonical.append("|observed-last-")
            .append(std::to_string(ordinal))
            .append("=")
            .append(std::to_string(generation.lifecycle.last_observed_ordinal));
    }
    const auto bytes = std::as_bytes(
        std::span{canonical.data(), canonical.size()});
    const auto digest = hlclient::hash::sha256(bytes);
    return digest
        ? std::optional<std::string>{hlclient::hash::sha256_hex(*digest)}
        : std::nullopt;
}

[[nodiscard]] bool exact_optional_u8_property(
    const TopLevelJsonObject& object,
    const std::string_view name,
    const std::optional<std::uint8_t> expected) noexcept
{
    if (!expected) {
        return top_level_property(object, name, TopLevelJsonKind::null_value) !=
               nullptr;
    }
    const auto value = top_level_integer(object, name, 255U);
    return value && *value == *expected;
}

[[nodiscard]] bool final_reconnect_observation_matches(
    const fs::path& run_root,
    const goldsrc::StockRuntimeReconnectLifecycleState& replay,
    const std::size_t retired_generation_a_server_tail_packet_count)
{
    static constexpr std::array root_keys{
        std::string_view{"schema"},
        std::string_view{"connection_generation_count"},
        std::string_view{"exact_boundary_count"},
        std::string_view{"runtime_candidate_count"},
        std::string_view{"generation_distinct"},
        std::string_view{"candidate_conflict"},
        std::string_view{"guard_continuity"},
        std::string_view{"server_continuity"},
        std::string_view{"relay_continuity"},
        std::string_view{"cleanup_exact"},
        std::string_view{"restoration_exact"},
        std::string_view{"candidate_body_consumed"},
        std::string_view{"candidate_semantic_category_assigned"},
        std::string_view{"retired_generation_a_tail_sink"},
        std::string_view{"retired_generation_a_server_tail_packet_count"},
        std::string_view{"generation_b_sequenced_after_fresh_accept"},
        std::string_view{"generations"}};
    static constexpr std::array generation_keys{
        std::string_view{"generation_ordinal"},
        std::string_view{"profile_identity"},
        std::string_view{"owned_client_process_role_identity"},
        std::string_view{"learned_client_endpoint_role_identity"},
        std::string_view{"fresh_owned_client_process"},
        std::string_view{"learned_client_endpoint_observed"},
        std::string_view{"learned_client_endpoint_distinct_from_previous"},
        std::string_view{"first_observed_ordinal"},
        std::string_view{"last_observed_ordinal"},
        std::string_view{"connectionless_exchange_count"},
        std::string_view{"connect_observed"},
        std::string_view{"accept_observed"},
        std::string_view{"first_sequenced_packet_ordinal"},
        std::string_view{"client_to_server_packet_count"},
        std::string_view{"server_to_client_packet_count"},
        std::string_view{"controlled_client_shutdown_observed"},
        std::string_view{"retired_client_endpoint_quiet"},
        std::string_view{"exact_post_resource_boundary"},
        std::string_view{"candidate_observation"}};
    static constexpr std::array boundary_keys{
        std::string_view{"observed"},
        std::string_view{"replay_payload_ordinal"},
        std::string_view{"corpus_observed_ordinal"},
        std::string_view{"delivery_ordinal"},
        std::string_view{"byte_offset"},
        std::string_view{"bit_offset"},
        std::string_view{"source_payload_byte_count"},
        std::string_view{"source_payload_bit_count"},
        std::string_view{"next_unconsumed_bit_count"}};
    static constexpr std::array candidate_keys{
        std::string_view{"observed"},
        std::string_view{"candidate_bit_width"},
        std::string_view{"numeric_candidate"},
        std::string_view{"bounded_bit_prefix"},
        std::string_view{"byte_aligned"},
        std::string_view{"body_consumed"},
        std::string_view{"semantic_category_assigned"}};

    const auto text = read_metadata(run_root / "reconnect-observation.json");
    TopLevelJsonObject root;
    if (!text || !parse_top_level_json_object(*text, root) ||
        !exact_top_level_properties(root, root_keys) ||
        !exact_string_property(
            root, "schema", goldsrc::kStockRuntimeReconnectObservationSchema) ||
        top_level_integer(root, "connection_generation_count", 2U) !=
            replay.connection_generation_count() ||
        top_level_integer(root, "exact_boundary_count", 2U) !=
            replay.exact_boundary_count() ||
        top_level_integer(root, "runtime_candidate_count", 2U) !=
            replay.runtime_candidate_count() ||
        !exact_boolean_property(
            root, "generation_distinct", replay.generation_distinct()) ||
        !exact_boolean_property(
            root, "candidate_conflict", replay.candidate_conflict()) ||
        !exact_boolean_property(root, "guard_continuity", true) ||
        !exact_boolean_property(root, "server_continuity", true) ||
        !exact_boolean_property(root, "relay_continuity", true) ||
        !exact_boolean_property(root, "cleanup_exact", true) ||
        !exact_boolean_property(root, "restoration_exact", true) ||
        !exact_boolean_property(
            root, "candidate_body_consumed", false) ||
        !exact_boolean_property(
            root, "candidate_semantic_category_assigned", false)) {
        return false;
    }
    if (!exact_string_property(
            root, "retired_generation_a_tail_sink", "routing_only") ||
        top_level_integer(
            root, "retired_generation_a_server_tail_packet_count",
            65'536U) != retired_generation_a_server_tail_packet_count ||
        !exact_boolean_property(
            root, "generation_b_sequenced_after_fresh_accept", true)) {
        return false;
    }
    const auto* generation_array = top_level_property(
        root, "generations", TopLevelJsonKind::compound);
    const auto generations = generation_array
        ? parse_top_level_object_array(generation_array->value)
        : std::nullopt;
    if (!generations || generations->size() != replay.generations().size()) {
        return false;
    }
    for (std::size_t index = 0U; index < generations->size(); ++index) {
        const auto& object = (*generations)[index];
        const auto& expected = replay.generations()[index];
        const auto* profile = top_level_property(
            object, "profile_identity", TopLevelJsonKind::string);
        const auto* process = top_level_property(
            object, "owned_client_process_role_identity",
            TopLevelJsonKind::string);
        const auto* endpoint = top_level_property(
            object, "learned_client_endpoint_role_identity",
            TopLevelJsonKind::string);
        const auto* boundary_value = top_level_property(
            object, "exact_post_resource_boundary", TopLevelJsonKind::compound);
        const auto* candidate_value = top_level_property(
            object, "candidate_observation", TopLevelJsonKind::compound);
        TopLevelJsonObject boundary;
        TopLevelJsonObject candidate;
        if (!exact_top_level_properties(object, generation_keys) ||
            profile == nullptr || profile->value != expected.profile_identity ||
            process == nullptr ||
            process->value != expected.owned_client_process_role_identity ||
            endpoint == nullptr ||
            endpoint->value != expected.learned_client_endpoint_role_identity ||
            top_level_integer(object, "generation_ordinal", 2U) !=
                expected.generation_ordinal ||
            top_level_integer(object, "first_observed_ordinal", 65'535U) !=
                expected.first_observed_ordinal ||
            top_level_integer(object, "last_observed_ordinal", 65'535U) !=
                expected.last_observed_ordinal ||
            top_level_integer(
                object, "connectionless_exchange_count", 65'536U) !=
                expected.connectionless_exchange_count ||
            top_level_integer(
                object, "first_sequenced_packet_ordinal", 65'535U) !=
                expected.first_sequenced_packet_ordinal ||
            top_level_integer(
                object, "client_to_server_packet_count", 65'536U) !=
                expected.client_to_server_packet_count ||
            top_level_integer(
                object, "server_to_client_packet_count", 65'536U) !=
                expected.server_to_client_packet_count ||
            !exact_boolean_property(
                object, "fresh_owned_client_process",
                expected.fresh_owned_client_process) ||
            !exact_boolean_property(
                object, "learned_client_endpoint_observed",
                expected.learned_client_endpoint_observed) ||
            !exact_boolean_property(
                object, "learned_client_endpoint_distinct_from_previous",
                expected.learned_client_endpoint_distinct_from_previous) ||
            !exact_boolean_property(
                object, "connect_observed", expected.connect_observed) ||
            !exact_boolean_property(
                object, "accept_observed", expected.accept_observed) ||
            !exact_boolean_property(
                object, "controlled_client_shutdown_observed",
                expected.controlled_client_shutdown_observed) ||
            !exact_boolean_property(
                object, "retired_client_endpoint_quiet",
                expected.retired_client_endpoint_quiet) ||
            boundary_value == nullptr || candidate_value == nullptr ||
            !parse_top_level_json_object(boundary_value->value, boundary) ||
            !parse_top_level_json_object(candidate_value->value, candidate) ||
            !exact_top_level_properties(boundary, boundary_keys) ||
            !exact_top_level_properties(candidate, candidate_keys)) {
            return false;
        }
        const auto& expected_boundary = expected.exact_post_resource_boundary;
        const auto& expected_candidate = expected.candidate_observation;
        if (!exact_boolean_property(
                boundary, "observed", expected_boundary.observed) ||
            top_level_integer(
                boundary, "replay_payload_ordinal", 65'535U) !=
                expected_boundary.replay_payload_ordinal ||
            top_level_integer(
                boundary, "corpus_observed_ordinal", 65'535U) !=
                expected_boundary.corpus_observed_ordinal ||
            top_level_integer(boundary, "delivery_ordinal", 65'535U) !=
                expected_boundary.delivery_ordinal ||
            top_level_integer(boundary, "byte_offset", 1'048'576U) !=
                expected_boundary.byte_offset ||
            top_level_integer(boundary, "bit_offset", 7U) !=
                expected_boundary.bit_offset ||
            top_level_integer(
                boundary, "source_payload_byte_count", 1'048'576U) !=
                expected_boundary.source_payload_byte_count ||
            top_level_integer(
                boundary, "source_payload_bit_count", 8'388'608U) !=
                expected_boundary.source_payload_bit_count ||
            top_level_integer(
                boundary, "next_unconsumed_bit_count", 8'388'608U) !=
                expected_boundary.next_unconsumed_bit_count ||
            !exact_boolean_property(
                candidate, "observed", expected_candidate.observed) ||
            top_level_integer(candidate, "candidate_bit_width", 8U) !=
                expected_candidate.candidate_bit_width ||
            !exact_optional_u8_property(
                candidate, "numeric_candidate",
                expected_candidate.numeric_candidate) ||
            !exact_optional_u8_property(
                candidate, "bounded_bit_prefix",
                expected_candidate.bounded_bit_prefix) ||
            !exact_boolean_property(
                candidate, "byte_aligned", expected_candidate.byte_aligned) ||
            !exact_boolean_property(
                candidate, "body_consumed", false) ||
            !exact_boolean_property(
                candidate, "semantic_category_assigned", false)) {
            return false;
        }
    }
    return true;
}

void print_reconnect_generation(
    const char label,
    const ReconnectGenerationReplay& generation)
{
    const std::string prefix{
        label == 'a' ? "[stock-runtime] generation-a-"
                     : "[stock-runtime] generation-b-"};
    const auto& lifecycle = generation.lifecycle;
    const auto& boundary = lifecycle.exact_post_resource_boundary;
    std::cout << prefix << "first-observed-ordinal="
              << lifecycle.first_observed_ordinal << '\n'
              << prefix << "last-observed-ordinal="
              << lifecycle.last_observed_ordinal << '\n'
              << prefix << "connectionless-exchanges="
              << lifecycle.connectionless_exchange_count << '\n'
              << prefix << "first-sequenced-packet-ordinal="
              << lifecycle.first_sequenced_packet_ordinal.value_or(0U) << '\n'
              << prefix << "client-to-server-packets="
              << lifecycle.client_to_server_packet_count << '\n'
              << prefix << "server-to-client-packets="
              << lifecycle.server_to_client_packet_count << '\n'
              << prefix << "boundary-payload-ordinal="
              << boundary.replay_payload_ordinal << '\n'
              << prefix << "boundary-observed-ordinal="
              << boundary.corpus_observed_ordinal << '\n'
              << prefix << "boundary-delivery-ordinal="
              << boundary.delivery_ordinal << '\n'
              << prefix << "boundary-byte-offset=" << boundary.byte_offset
              << '\n'
              << prefix << "boundary-bit-offset=" << boundary.bit_offset
              << '\n'
              << prefix << "boundary-source-payload-bytes="
              << boundary.source_payload_byte_count << '\n'
              << prefix << "boundary-source-payload-bits="
              << boundary.source_payload_bit_count << '\n'
              << prefix << "boundary-next-unconsumed-bits="
              << boundary.next_unconsumed_bit_count << '\n'
              << prefix << "boundary-source-sequence="
              << generation.global_cursor.source_netchan_sequence << '\n'
              << prefix << "boundary-reassembled="
              << (generation.global_cursor.reassembled ? "true" : "false")
              << '\n'
              << prefix << "boundary-decompressed="
              << (generation.global_cursor.decompressed ? "true" : "false")
              << '\n'
              << prefix << "boundary-byte-aligned="
              << (lifecycle.candidate_observation.byte_aligned
                      ? "true" : "false")
              << '\n'
              << prefix << "candidate-bit-width="
              << lifecycle.candidate_observation.candidate_bit_width << '\n'
              << prefix << "first-candidate=" << generation.candidate << '\n'
              << prefix << "candidate-body-consumed=false\n"
              << prefix << "candidate-semantic-category-assigned=false\n"
              << prefix << "replay-structural-hash="
              << generation.replay_structural_sha256 << '\n';
}

[[nodiscard]] int run_reconnect_first_observation(
    const goldsrc::StockRuntimeCaptureCorpusState& corpus,
    const fs::path& capture_root,
    const bool published_accepted,
    const bool publication_ready)
{
    const auto replay = replay_reconnect_generations(corpus, capture_root);
    if (!replay || !replay->lifecycle) {
        return offline_failure(
            "reconnect-replay", "generation_replay_failed", 25);
    }
    const auto& lifecycle = *replay->lifecycle;
    if (lifecycle.connection_generation_count() != 2U ||
        lifecycle.exact_boundary_count() != 2U ||
        lifecycle.runtime_candidate_count() != 2U ||
        !lifecycle.generation_distinct() || lifecycle.candidate_conflict() ||
        lifecycle.candidate_body_consumed() ||
        lifecycle.semantic_category_assigned()) {
        return offline_failure(
            "reconnect-replay", "lifecycle_evidence_incomplete", 25);
    }
    const auto delivered = delivered_netchan_counts(corpus);
    const auto replay_hash = reconnect_replay_structure_sha256(
        corpus.run_id(), *replay);
    if (!delivered || !replay_hash) {
        return offline_failure(
            "reconnect-replay", "reconnect_structure_failed", 28);
    }
    if (published_accepted &&
        !final_reconnect_observation_matches(
            capture_root, lifecycle,
            replay->retired_generation_a_server_tail_packet_count)) {
        return offline_failure(
            "manifest-binding", "reconnect_observation_mismatch", 29);
    }

    const auto replay_c2s = replay->generations[0U].replay_sequenced_c2s +
                            replay->generations[1U].replay_sequenced_c2s;
    const auto replay_s2c = replay->generations[0U].replay_sequenced_s2c +
                            replay->generations[1U].replay_sequenced_s2c;
    const auto fragments = replay->generations[0U].replay_fragments +
                           replay->generations[1U].replay_fragments;
    const auto duplicates = replay->generations[0U].replay_duplicates +
                            replay->generations[1U].replay_duplicates;
    const auto old_packets = replay->generations[0U].replay_old_packets +
                             replay->generations[1U].replay_old_packets;
    const auto reassembled = replay->generations[0U].replay_reassembled +
                             replay->generations[1U].replay_reassembled;
    const auto decompressed = replay->generations[0U].replay_decompressed +
                              replay->generations[1U].replay_decompressed;
    const auto& representative = replay->generations[0U];
    const auto& cursor = representative.global_cursor;
    const auto& candidate = representative.lifecycle.candidate_observation;
    std::cout << "[stock-runtime] profile="
              << goldsrc::kStockRuntimePendingProfile << '\n'
              << "[stock-runtime] transport-valid=true\n"
              << "[stock-runtime] sequenced-c2s=" << replay_c2s << '\n'
              << "[stock-runtime] sequenced-s2c=" << replay_s2c << '\n'
              << "[stock-runtime] fragments=" << fragments << '\n'
              << "[stock-runtime] duplicate-packets=" << duplicates << '\n'
              << "[stock-runtime] old-packets=" << old_packets << '\n'
              << "[stock-runtime] delivered-sequenced-c2s="
              << delivered->sequenced_client_to_server << '\n'
              << "[stock-runtime] delivered-sequenced-s2c="
              << delivered->sequenced_server_to_client << '\n'
              << "[stock-runtime] delivered-fragment-datagrams="
              << delivered->fragment_datagrams << '\n'
              << "[stock-runtime] reassembled=" << reassembled << '\n'
              << "[stock-runtime] decompressed=" << decompressed << '\n'
              << "[stock-runtime] signon-replay=complete\n"
              << "[stock-runtime] post-resource-boundary=observed\n"
              << "[stock-runtime] boundary-payload-ordinal="
              << cursor.replay_payload_ordinal << '\n'
              << "[stock-runtime] boundary-observed-ordinal="
              << cursor.corpus_observed_ordinal << '\n'
              << "[stock-runtime] boundary-delivery-ordinal="
              << cursor.delivery_ordinal << '\n'
              << "[stock-runtime] boundary-byte-offset=" << cursor.byte_offset
              << '\n'
              << "[stock-runtime] boundary-bit-offset=" << cursor.bit_offset
              << '\n'
              << "[stock-runtime] boundary-source-sequence="
              << cursor.source_netchan_sequence << '\n'
              << "[stock-runtime] boundary-source-payload-bytes="
              << cursor.source_payload_byte_count << '\n'
              << "[stock-runtime] boundary-source-payload-bits="
              << cursor.source_payload_bit_count << '\n'
              << "[stock-runtime] boundary-next-unconsumed-bits="
              << cursor.next_unconsumed_bit_count << '\n'
              << "[stock-runtime] boundary-reassembled="
              << (cursor.reassembled ? "true" : "false") << '\n'
              << "[stock-runtime] boundary-decompressed="
              << (cursor.decompressed ? "true" : "false") << '\n'
              << "[stock-runtime] boundary-byte-aligned="
              << (candidate.byte_aligned ? "true" : "false") << '\n'
              << "[stock-runtime] candidate-bit-width="
              << candidate.candidate_bit_width << '\n'
              << "[stock-runtime] first-candidate="
              << representative.candidate << '\n'
              << "[stock-runtime] candidate-recurrence=2\n"
              << "[stock-runtime] candidate-stability=stable_observation\n"
              << "[stock-runtime] connection-generation-count=2\n"
              << "[stock-runtime] exact-boundary-count=2\n"
              << "[stock-runtime] runtime-candidate-count=2\n"
              << "[stock-runtime] generation-distinct=true\n"
              << "[stock-runtime] candidate-conflict=false\n";
    std::cout << "[stock-runtime] retired-generation-a-tail-sink=routing_only\n"
              << "[stock-runtime] retired-generation-a-server-tail-packets="
              << replay->retired_generation_a_server_tail_packet_count << '\n'
              << "[stock-runtime] generation-b-sequenced-after-fresh-accept=true\n";
    print_reconnect_generation('a', replay->generations[0U]);
    print_reconnect_generation('b', replay->generations[1U]);
    std::cout << "[stock-runtime] accepted-run="
              << (published_accepted ? "true" : "false") << '\n'
              << "[stock-runtime] publication-ready="
              << (publication_ready ? "true" : "false") << '\n'
              << "[stock-runtime] result=first-observation\n"
              << "[stock-runtime] structural-hash="
              << corpus.structural_sha256() << '\n'
              << "[stock-runtime] replay-structural-hash="
              << *replay_hash << '\n';
    return 0;
}

[[nodiscard]] goldsrc::StockRuntimeCampaignRunObservation
campaign_rejected_observation(const std::string_view run_id)
{
    goldsrc::StockRuntimeCampaignRunObservation result;
    result.run_id = std::string{run_id};
    result.publication =
        goldsrc::StockRuntimeCampaignPublicationState::rejected;
    return result;
}

[[nodiscard]] std::optional<std::string> campaign_profile_fingerprint(
    const fs::path& run_root)
{
    const auto text = read_metadata(run_root / "version-observation.json");
    TopLevelJsonObject version;
    if (!text || !parse_top_level_json_object(*text, version)) {
        return std::nullopt;
    }
    constexpr std::array string_names{
        std::string_view{"client_file_version"},
        std::string_view{"server_launcher_version"},
        std::string_view{"server_engine_version"},
    };
    constexpr std::array integer_names{
        std::string_view{"protocol"},
        std::string_view{"server_build"},
        std::string_view{"steam_build_id"},
    };
    std::array<const TopLevelJsonValue*, 8U> values{
        top_level_property(version, string_names[0U], TopLevelJsonKind::string),
        top_level_property(version, string_names[1U], TopLevelJsonKind::string),
        top_level_property(version, string_names[2U], TopLevelJsonKind::string),
        top_level_property(version, integer_names[0U], TopLevelJsonKind::integer),
        top_level_property(version, integer_names[1U], TopLevelJsonKind::integer),
        top_level_property(version, integer_names[2U], TopLevelJsonKind::integer),
        top_level_property(
            version, "client_profile_fingerprint", TopLevelJsonKind::string),
        top_level_property(
            version, "server_profile_fingerprint", TopLevelJsonKind::string),
    };
    if (std::ranges::any_of(values, [](const auto* value) {
            return value == nullptr;
        })) {
        return std::nullopt;
    }
    std::string canonical;
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (index != 0U) canonical.push_back('|');
        canonical.append(values[index]->value);
    }
    const auto bytes = std::as_bytes(
        std::span{canonical.data(), canonical.size()});
    const auto digest = hlclient::hash::sha256(bytes);
    return digest
        ? std::optional<std::string>{hlclient::hash::sha256_hex(*digest)}
        : std::nullopt;
}

[[nodiscard]] goldsrc::StockRuntimeCampaignRunObservation
campaign_observation_from_run(
    const fs::path& run_root,
    const bool independent_walker_validated)
{
    const auto run_id = run_root.filename().string();
    goldsrc::StockRuntimeCampaignRunObservation result;
    result.run_id = run_id;
    result.profile_identity = goldsrc::kStockRuntimePendingProfile;

    const auto manifest_text = read_metadata(
        run_root / "research-run-metadata.json");
    TopLevelJsonObject manifest;
    if (!manifest_text ||
        !parse_top_level_json_object(*manifest_text, manifest)) {
        return campaign_rejected_observation(run_id);
    }
    const auto* schema = top_level_property(
        manifest, "schema", TopLevelJsonKind::string);
    const auto* manifest_run_id = top_level_property(
        manifest, "run_id", TopLevelJsonKind::string);
    const auto* scenario = top_level_property(
        manifest, "scenario", TopLevelJsonKind::string);
    const auto* map = top_level_property(
        manifest, "map_category", TopLevelJsonKind::string);
    const auto accepted = top_level_boolean(
        manifest, "accepted_evidence_run");
    const auto accepted_transport = top_level_boolean(
        manifest, "accepted_transport_run");
    const auto* failure = top_level_property(
        manifest, "failure_category", TopLevelJsonKind::string);
    const auto* isolation = top_level_property(
        manifest, "isolation_status", TopLevelJsonKind::string);
    const auto* version = top_level_property(
        manifest, "version_profile_status", TopLevelJsonKind::string);
    const auto* ready = top_level_property(
        manifest, "client_ready_status", TopLevelJsonKind::string);
    const auto* restoration = top_level_property(
        manifest, "restoration_status", TopLevelJsonKind::string);
    const auto* drift = top_level_property(
        manifest, "external_drift_status", TopLevelJsonKind::string);
    const auto* replay_status = top_level_property(
        manifest, "offline_replay_status", TopLevelJsonKind::string);
    if (schema == nullptr ||
        schema->value != "hlclient.stock-runtime-research-run.v1" ||
        manifest_run_id == nullptr || manifest_run_id->value != run_id ||
        scenario == nullptr || map == nullptr || !accepted ||
        !accepted_transport ||
        failure == nullptr) {
        return campaign_rejected_observation(run_id);
    }
    result.map_category = map->value;
    result.scenario = scenario->value;
    if (!*accepted) {
        if (*accepted_transport || failure->value.empty() ||
            failure->value == "none") {
            return campaign_rejected_observation(run_id);
        }
        result.publication =
            goldsrc::stock_runtime_campaign_failure_publication(
                failure->value);
        return result;
    }
    const goldsrc::StockRuntimeCaptureCorpusLoader loader;
    const auto loaded = loader.load(
        run_root, goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
    if (!loaded || !loaded.state) {
        return campaign_rejected_observation(run_id);
    }
    const auto& corpus = *loaded.state;
    if (!*accepted_transport || failure->value != "none" ||
        corpus.publication_state() !=
            goldsrc::StockRuntimeCaptureCorpusPublicationState::
                published_accepted ||
        result.scenario != goldsrc::to_string(
            corpus.capture_metadata().scenario) ||
        isolation == nullptr || isolation->value != "verified" ||
        version == nullptr || version->value != "verified" ||
        ready == nullptr || ready->value != "true" ||
        restoration == nullptr || restoration->value != "exact" ||
        drift == nullptr || drift->value != "none" ||
        replay_status == nullptr || replay_status->value != "success") {
        return campaign_rejected_observation(run_id);
    }
    const auto profile_fingerprint = campaign_profile_fingerprint(run_root);
    if (!profile_fingerprint) {
        return campaign_rejected_observation(run_id);
    }
    result.profile_identity = *profile_fingerprint;

    const auto delivered = delivered_netchan_counts(corpus);
    if (!delivered) return campaign_rejected_observation(run_id);

    std::size_t reassembled_payloads = 0U;
    std::size_t decompressed_payloads = 0U;
    std::size_t sequenced_client_to_server =
        delivered->sequenced_client_to_server;
    std::size_t sequenced_server_to_client =
        delivered->sequenced_server_to_client;
    std::size_t generations = 1U;
    std::size_t boundaries = 1U;
    bool distinct = false;
    bool reconnect_conflict = false;
    bool candidate_body_unconsumed = false;
    std::string replay_structural_sha256;
    std::vector<goldsrc::StockRuntimeCampaignCandidateObservation> candidates;
    if (result.scenario == "reconnect") {
        const auto reconnect = replay_reconnect_generations(corpus, run_root);
        if (!reconnect || !reconnect->lifecycle ||
            !final_reconnect_observation_matches(
                run_root, *reconnect->lifecycle,
                reconnect->retired_generation_a_server_tail_packet_count)) {
            return campaign_rejected_observation(run_id);
        }
        const auto replay_hash = reconnect_replay_structure_sha256(
            corpus.run_id(), *reconnect);
        if (!replay_hash) {
            return campaign_rejected_observation(run_id);
        }
        replay_structural_sha256 = *replay_hash;
        generations = reconnect->lifecycle->connection_generation_count();
        boundaries = reconnect->lifecycle->exact_boundary_count();
        distinct = reconnect->lifecycle->generation_distinct();
        reconnect_conflict = reconnect->lifecycle->candidate_conflict();
        candidate_body_unconsumed =
            !reconnect->lifecycle->candidate_body_consumed();
        sequenced_client_to_server = 0U;
        sequenced_server_to_client = 0U;
        try {
            candidates.reserve(reconnect->generations.size());
            for (const auto& generation : reconnect->generations) {
                const auto& lifecycle = generation.lifecycle;
                goldsrc::StockRuntimeCampaignCandidateObservation candidate;
                candidate.bit_offset =
                    lifecycle.exact_post_resource_boundary.bit_offset;
                candidate.bit_width =
                    lifecycle.candidate_observation.candidate_bit_width;
                candidate.byte_aligned =
                    lifecycle.candidate_observation.byte_aligned;
                candidate.numeric_candidate =
                    lifecycle.candidate_observation.numeric_candidate;
                candidate.bounded_bit_prefix =
                    lifecycle.candidate_observation.bounded_bit_prefix;
                candidates.push_back(std::move(candidate));
                sequenced_client_to_server +=
                    generation.replay_sequenced_c2s;
                sequenced_server_to_client +=
                    generation.replay_sequenced_s2c;
                reassembled_payloads += generation.replay_reassembled;
                decompressed_payloads += generation.replay_decompressed;
            }
        } catch (...) {
            return campaign_rejected_observation(run_id);
        }
    } else {
        const goldsrc::StockRuntimeTransportReplay transport_replay;
        const auto transport = transport_replay.replay(corpus);
        if (!transport || !transport.state) {
            return campaign_rejected_observation(run_id);
        }
        const goldsrc::StockCapturedSignonReplay signon_replay;
        const auto signon = signon_replay.replay(*transport.state);
        if (!signon || !signon.state ||
            !signon.state->known_signon_validated()) {
            return campaign_rejected_observation(run_id);
        }
        const auto& cursor = signon.state->cursor();
        if (cursor.replay_payload_ordinal >= transport.state->payloads().size()) {
            return campaign_rejected_observation(run_id);
        }
        const auto& payload =
            transport.state->payloads()[cursor.replay_payload_ordinal];
        goldsrc::StockRuntimeFirstObservationInput first_input;
        first_input.run_id = run_id;
        first_input.version_profile = result.profile_identity;
        first_input.cursor = cursor;
        first_input.source_payload = payload.bytes();
        first_input.accepted_evidence_run = true;
        first_input.known_signon_validated = true;
        const std::array first_inputs{first_input};
        const auto first =
            goldsrc::StockRuntimeFirstObservationBuilder{}.build(first_inputs);
        if (!first || !first.state) {
            return campaign_rejected_observation(run_id);
        }
        const auto replay_hash = replay_structure_sha256(
            corpus.run_id(), cursor, first.state->candidate_bit_width(),
            candidate_representation(*first.state));
        if (!replay_hash) {
            return campaign_rejected_observation(run_id);
        }
        replay_structural_sha256 = *replay_hash;
        goldsrc::StockRuntimeCampaignCandidateObservation candidate;
        candidate.bit_offset = cursor.bit_offset;
        candidate.bit_width = first.state->candidate_bit_width();
        candidate.byte_aligned = first.state->byte_aligned();
        candidate.numeric_candidate = first.state->numeric_candidate();
        candidate.bounded_bit_prefix = first.state->bounded_bit_prefix();
        try {
            candidates.push_back(std::move(candidate));
        } catch (...) {
            return campaign_rejected_observation(run_id);
        }
        candidate_body_unconsumed = !first.state->body_consumed();
        reassembled_payloads = transport.state->reassembled_payload_count();
        decompressed_payloads = transport.state->decompressed_payload_count();
    }

    result.publication =
        goldsrc::StockRuntimeCampaignPublicationState::accepted;
    result.transport_structural_sha256 =
        std::string{corpus.structural_sha256()};
    result.replay_structural_sha256 = std::move(replay_structural_sha256);
    result.isolation_verified = true;
    result.profile_verified = true;
    result.client_ready = true;
    result.bounded_transport_complete =
        corpus.capture_metadata().bounded_transport_complete;
    result.wrong_source_datagrams =
        corpus.capture_metadata().counters.ignored_wrong_source_datagrams;
    result.restoration_exact = true;
    result.external_drift_none = true;
    result.corpus_valid = true;
    // This process cannot independently execute or impersonate the PowerShell
    // transport walker. The campaign runner/verifier supplies only run IDs
    // whose two deterministic walker passes were reconciled with this
    // checker's complete structural output.
    result.independent_walker_valid = independent_walker_validated;
    result.signon_replay_complete = true;
    result.candidate_body_unconsumed = candidate_body_unconsumed;
    result.sequenced_client_to_server = sequenced_client_to_server;
    result.sequenced_server_to_client = sequenced_server_to_client;
    result.reassembled_payloads = reassembled_payloads;
    result.decompressed_payloads = decompressed_payloads;
    result.connection_generation_count = generations;
    result.exact_post_resource_boundary_count = boundaries;
    result.reconnect_generations_distinct = distinct;
    result.reconnect_candidate_conflict = reconnect_conflict;
    result.candidates = std::move(candidates);
    return result;
}

struct CampaignManifestContract final {
    std::string implementation_commit;
    std::string profile_fingerprint;
    std::array<std::size_t, 8U> matrix_accepted{};
    std::size_t attempted{0U};
    std::size_t accepted{0U};
    std::size_t rejected{0U};
    std::size_t incomplete{0U};
    std::size_t pending{0U};
    std::size_t sequenced_c2s{0U};
    std::size_t sequenced_s2c{0U};
    std::size_t reassembled{0U};
    std::size_t decompressed{0U};
    std::size_t exact_boundaries{0U};
    std::size_t candidates{0U};
    std::size_t reconnect_generations{0U};
    std::string candidate_stability;
    std::string threshold_status;
    std::string structural_sha256;
};

[[nodiscard]] std::optional<CampaignManifestContract>
read_campaign_manifest_contract(const fs::path& root)
{
    constexpr std::array root_keys{
        std::string_view{"schema"},
        std::string_view{"implementation_commit"},
        std::string_view{"profile_fingerprint"},
        std::string_view{"required_matrix"},
        std::string_view{"attempted_slots"},
        std::string_view{"accepted_slots"},
        std::string_view{"rejected_slots"},
        std::string_view{"incomplete_slots"},
        std::string_view{"pending_slots"},
        std::string_view{"packet_totals"},
        std::string_view{"boundary_totals"},
        std::string_view{"candidate_stability"},
        std::string_view{"threshold_status"},
        std::string_view{"campaign_structural_sha256"},
    };
    constexpr std::array matrix_keys{
        std::string_view{"map_category"},
        std::string_view{"scenario"},
        std::string_view{"required_runs"},
        std::string_view{"accepted_runs"},
    };
    constexpr std::array packet_keys{
        std::string_view{"sequenced_c2s"},
        std::string_view{"sequenced_s2c"},
        std::string_view{"reassembled"},
        std::string_view{"decompressed"},
    };
    constexpr std::array boundary_keys{
        std::string_view{"exact"},
        std::string_view{"candidates"},
        std::string_view{"reconnect_generations"},
    };
    constexpr auto maximum = (std::numeric_limits<std::size_t>::max)();

    const auto text = read_metadata(root / "campaign-manifest.json");
    TopLevelJsonObject manifest;
    if (!text || !parse_top_level_json_object(*text, manifest) ||
        !exact_top_level_properties(manifest, root_keys)) {
        return std::nullopt;
    }
    const auto* schema = top_level_property(
        manifest, "schema", TopLevelJsonKind::string);
    const auto* commit = top_level_property(
        manifest, "implementation_commit", TopLevelJsonKind::string);
    const auto* profile = top_level_property(
        manifest, "profile_fingerprint", TopLevelJsonKind::string);
    const auto* matrix_value = top_level_property(
        manifest, "required_matrix", TopLevelJsonKind::compound);
    const auto* packet_value = top_level_property(
        manifest, "packet_totals", TopLevelJsonKind::compound);
    const auto* boundary_value = top_level_property(
        manifest, "boundary_totals", TopLevelJsonKind::compound);
    const auto* stability = top_level_property(
        manifest, "candidate_stability", TopLevelJsonKind::string);
    const auto* threshold = top_level_property(
        manifest, "threshold_status", TopLevelJsonKind::string);
    const auto* structural = top_level_property(
        manifest, "campaign_structural_sha256", TopLevelJsonKind::string);
    const auto attempted = top_level_integer(manifest, "attempted_slots", 4'096U);
    const auto accepted = top_level_integer(manifest, "accepted_slots", 4'096U);
    const auto rejected = top_level_integer(manifest, "rejected_slots", 4'096U);
    const auto incomplete = top_level_integer(manifest, "incomplete_slots", 4'096U);
    const auto pending = top_level_integer(manifest, "pending_slots", 24U);
    if (schema == nullptr || schema->value !=
            goldsrc::kStockRuntimeFirstCampaignSchema ||
        commit == nullptr || !is_lower_hex_commit(commit->value) ||
        std::ranges::all_of(commit->value, [](const char value) {
            return value == '0';
        }) ||
        profile == nullptr ||
        (profile->value != "evidence_pending" &&
         !is_lower_hex_sha256(profile->value)) ||
        matrix_value == nullptr || packet_value == nullptr ||
        boundary_value == nullptr || stability == nullptr ||
        (stability->value != "evidence_pending" &&
         stability->value != "stable_observation" &&
         stability->value != "candidate_conflicting") ||
        threshold == nullptr ||
        (threshold->value != "pending" && threshold->value != "passed" &&
         threshold->value != "conflicting") ||
        structural == nullptr || !is_lower_hex_sha256(structural->value) ||
        !attempted || !accepted || !rejected || !incomplete || !pending ||
        *accepted + *rejected + *incomplete != *attempted ||
        (*accepted != 0U && !is_lower_hex_sha256(profile->value))) {
        return std::nullopt;
    }

    const auto matrix = parse_top_level_object_array(matrix_value->value);
    const auto specification = goldsrc::stock_runtime_first_campaign_matrix();
    if (!matrix || matrix->size() != specification.size()) {
        return std::nullopt;
    }
    CampaignManifestContract result;
    std::array<bool, 8U> seen{};
    for (const auto& entry : *matrix) {
        const auto* map = top_level_property(
            entry, "map_category", TopLevelJsonKind::string);
        const auto* scenario = top_level_property(
            entry, "scenario", TopLevelJsonKind::string);
        const auto required = top_level_integer(entry, "required_runs", 24U);
        const auto current = top_level_integer(entry, "accepted_runs", 24U);
        if (!exact_top_level_properties(entry, matrix_keys) || map == nullptr ||
            scenario == nullptr || !required || !current) {
            return std::nullopt;
        }
        std::optional<std::size_t> found;
        for (std::size_t index = 0U; index < specification.size(); ++index) {
            if (specification[index].map_category == map->value &&
                specification[index].scenario == scenario->value) {
                found = index;
                break;
            }
        }
        if (!found || seen[*found] ||
            *required != specification[*found].required_runs ||
            *current > *required) {
            return std::nullopt;
        }
        seen[*found] = true;
        result.matrix_accepted[*found] = *current;
    }

    TopLevelJsonObject packets;
    TopLevelJsonObject boundaries;
    if (!parse_top_level_json_object(packet_value->value, packets) ||
        !exact_top_level_properties(packets, packet_keys) ||
        !parse_top_level_json_object(boundary_value->value, boundaries) ||
        !exact_top_level_properties(boundaries, boundary_keys)) {
        return std::nullopt;
    }
    const auto sequenced_c2s =
        top_level_integer(packets, "sequenced_c2s", maximum);
    const auto sequenced_s2c =
        top_level_integer(packets, "sequenced_s2c", maximum);
    const auto reassembled = top_level_integer(packets, "reassembled", maximum);
    const auto decompressed =
        top_level_integer(packets, "decompressed", maximum);
    const auto exact = top_level_integer(boundaries, "exact", maximum);
    const auto candidates = top_level_integer(boundaries, "candidates", maximum);
    const auto reconnect =
        top_level_integer(boundaries, "reconnect_generations", maximum);
    if (!sequenced_c2s || !sequenced_s2c || !reassembled || !decompressed ||
        !exact || !candidates || !reconnect) {
        return std::nullopt;
    }

    result.implementation_commit = commit->value;
    result.profile_fingerprint = profile->value;
    result.attempted = *attempted;
    result.accepted = *accepted;
    result.rejected = *rejected;
    result.incomplete = *incomplete;
    result.pending = *pending;
    result.sequenced_c2s = *sequenced_c2s;
    result.sequenced_s2c = *sequenced_s2c;
    result.reassembled = *reassembled;
    result.decompressed = *decompressed;
    result.exact_boundaries = *exact;
    result.candidates = *candidates;
    result.reconnect_generations = *reconnect;
    result.candidate_stability = stability->value;
    result.threshold_status = threshold->value;
    result.structural_sha256 = structural->value;
    return result;
}

[[nodiscard]] bool campaign_manifest_matches(
    const CampaignManifestContract& manifest,
    const goldsrc::StockRuntimeCampaignState& state) noexcept
{
    const auto expected_profile = state.accepted_runs() == 0U
        ? std::string_view{"evidence_pending"}
        : state.profile_identity();
    if (manifest.implementation_commit != state.implementation_commit() ||
        manifest.profile_fingerprint != expected_profile ||
        manifest.attempted != state.attempted_runs() ||
        manifest.accepted != state.accepted_runs() ||
        manifest.rejected != state.rejected_runs() ||
        manifest.incomplete != state.incomplete_runs() ||
        manifest.pending != state.pending_runs() ||
        manifest.sequenced_c2s != state.sequenced_client_to_server() ||
        manifest.sequenced_s2c != state.sequenced_server_to_client() ||
        manifest.reassembled != state.reassembled_payloads() ||
        manifest.decompressed != state.decompressed_payloads() ||
        manifest.exact_boundaries != state.exact_post_resource_boundaries() ||
        manifest.candidates != state.candidate_observations() ||
        manifest.reconnect_generations != state.reconnect_generations() ||
        manifest.candidate_stability !=
            goldsrc::to_string(state.candidate_stability()) ||
        manifest.threshold_status !=
            goldsrc::to_string(state.threshold_status()) ||
        manifest.structural_sha256 != state.structural_sha256()) {
        return false;
    }
    const auto specification = goldsrc::stock_runtime_first_campaign_matrix();
    for (std::size_t index = 0U; index < specification.size(); ++index) {
        if (manifest.matrix_accepted[index] != state.accepted_runs_for(
                specification[index].map_category,
                specification[index].scenario)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] int run_campaign_summary(
    const fs::path& root,
    const std::optional<std::string>& refresh_implementation_commit,
    const std::span<const std::string> independent_walker_validated_runs)
{
    std::vector<fs::path> run_roots;
    bool manifest_present = false;
    std::error_code error;
    for (fs::directory_iterator iterator{root, error};
         !error && iterator != fs::directory_iterator{};
         iterator.increment(error)) {
        const auto name = iterator->path().filename().string();
        if (name == "campaign-manifest.json") {
            if (!iterator->is_regular_file(error) || error ||
                iterator->is_symlink(error) || error) {
                return offline_failure(
                    "campaign", "unsafe_campaign_manifest", 30);
            }
            manifest_present = true;
            continue;
        }
        if (!iterator->is_directory(error) || error ||
            iterator->is_symlink(error) || error ||
            !is_lower_hex_run_id(name)) {
            return offline_failure("campaign", "unexpected_campaign_entry", 30);
        }
        run_roots.push_back(iterator->path());
        if (run_roots.size() > 4'096U) {
            return offline_failure("campaign", "run_limit_exceeded", 30);
        }
    }
    if (error) {
        return offline_failure("campaign", "enumeration_failed", 30);
    }
    const auto manifest = manifest_present
        ? read_campaign_manifest_contract(root)
        : std::optional<CampaignManifestContract>{};
    if ((manifest_present && !manifest) ||
        (!refresh_implementation_commit && !manifest)) {
        return offline_failure("campaign", "invalid_campaign_manifest", 30);
    }
    const auto implementation_commit = refresh_implementation_commit
        ? *refresh_implementation_commit
        : manifest->implementation_commit;
    if (manifest && manifest->implementation_commit != implementation_commit) {
        return offline_failure("campaign", "implementation_commit_mismatch", 30);
    }
    std::ranges::sort(run_roots);
    std::vector<goldsrc::StockRuntimeCampaignRunObservation> observations;
    observations.reserve(run_roots.size());
    for (const auto& run_root : run_roots) {
        const auto run_id = run_root.filename().string();
        observations.push_back(campaign_observation_from_run(
            run_root,
            std::ranges::binary_search(
                independent_walker_validated_runs, run_id)));
    }
    const auto summary = goldsrc::StockRuntimeCampaignAggregator{}.build(
        observations, implementation_commit);
    if (!summary || !summary.state) {
        return offline_failure(
            "campaign", summary.error
                ? goldsrc::to_string(summary.error->code)
                : "campaign_aggregation_failed", 31);
    }
    const auto& state = *summary.state;
    if (!refresh_implementation_commit &&
        !campaign_manifest_matches(*manifest, state)) {
        return offline_failure("campaign", "campaign_manifest_mismatch", 31);
    }
    std::cout << "[stock-runtime] profile="
              << goldsrc::kStockRuntimePendingProfile << '\n'
              << "[stock-runtime] accepted=" << state.accepted_runs() << '\n'
              << "[stock-runtime] rejected=" << state.rejected_runs() << '\n'
              << "[stock-runtime] incomplete=" << state.incomplete_runs() << '\n'
              << "[stock-runtime] pending=" << state.pending_runs() << '\n'
              << "[stock-runtime] sequenced-c2s="
              << state.sequenced_client_to_server() << '\n'
              << "[stock-runtime] sequenced-s2c="
              << state.sequenced_server_to_client() << '\n'
              << "[stock-runtime] reassembled="
              << state.reassembled_payloads() << '\n'
              << "[stock-runtime] decompressed="
              << state.decompressed_payloads() << '\n'
              << "[stock-runtime] boundaries="
              << state.exact_post_resource_boundaries() << '\n'
              << "[stock-runtime] candidates="
              << state.candidate_observations() << '\n'
              << "[stock-runtime] reconnect-generations="
              << state.reconnect_generations() << '\n'
              << "[stock-runtime] candidate-stability="
              << goldsrc::to_string(state.candidate_stability()) << '\n'
              << "[stock-runtime] threshold="
              << goldsrc::to_string(state.threshold_status()) << '\n'
              << "[stock-runtime] implementation-commit="
              << state.implementation_commit() << '\n'
              << "[stock-runtime] structural-hash="
              << state.structural_sha256() << '\n'
              << "[stock-runtime] result=campaign-summary\n";
    return 0;
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
    if (options->scenario == Scenario::campaign_summary) {
        const auto campaign_root =
            validate_campaign_root(options->capture_root);
        if (!campaign_root) {
            std::cerr << "[stock-runtime] result=unsafe-campaign-root\n";
            return 3;
        }
        try {
            return run_campaign_summary(
                *campaign_root,
                options->campaign_refresh_implementation_commit,
                options->independent_walker_validated_runs);
        } catch (...) {
            return offline_failure(
                "campaign", "bounded_internal_failure", 31);
        }
    }
    const auto capture_root = validate_capture_root(options->capture_root);
    if (!capture_root) {
        std::cerr << "[stock-runtime] result=unsafe-capture-root\n";
        return 3;
    }
    if (!legacy_scenario(options->scenario)) {
        try {
            return run_offline_scenario(*options, *capture_root);
        } catch (...) {
            return offline_failure(
                "offline-replay", "bounded_internal_failure", 27);
        }
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
    if (!hash) {
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
              << "[stock-runtime] result=evidence_pending\n";
    return 0;
}
