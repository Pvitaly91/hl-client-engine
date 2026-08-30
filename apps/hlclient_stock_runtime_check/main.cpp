#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/stock_runtime_capture.hpp>
#include <hlclient/goldsrc/stock_runtime_capture_corpus.hpp>
#include <hlclient/goldsrc/stock_runtime_first_observation.hpp>
#include <hlclient/goldsrc/stock_runtime_transport_replay.hpp>
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
    "--scenario transcript|baselines|entities|clientdata|authority|ack|transport|"
    "netchan|signon-replay|post-resource-first|first-observation "
    "[--publication-stage prepublication]\n";

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
};

struct Options final {
    fs::path capture_root;
    Scenario scenario{Scenario::transcript};
    bool prepublication{false};
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
    return std::nullopt;
}

[[nodiscard]] std::optional<Options> parse_options(
    const std::span<const std::string_view> arguments)
{
    Options options;
    bool root_seen = false;
    bool scenario_seen = false;
    bool publication_seen = false;
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if ((argument != "--capture-root" && argument != "--scenario" &&
             argument != "--publication-stage") ||
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
        } else {
            if (publication_seen || value != "prepublication") {
                return std::nullopt;
            }
            publication_seen = true;
            options.prepublication = true;
        }
    }
    return root_seen && scenario_seen &&
            (!options.prepublication ||
             options.scenario == Scenario::first_observation)
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
