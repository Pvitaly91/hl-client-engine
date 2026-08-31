#include <hlclient/goldsrc/stock_runtime_capture_corpus.hpp>
#include <hlclient/hash/sha256.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace goldsrc = hlclient::goldsrc;

inline constexpr std::string_view kRunId{
    "0123456789abcdef0123456789abcdef"};
inline constexpr std::array kSequencedClientDatagram{
    std::byte{0x01U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}};
inline constexpr std::array kSequencedServerDatagram{
    std::byte{0x02U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U}};

class TemporaryCorpus final {
public:
    TemporaryCorpus()
    {
        static std::atomic<unsigned int> ordinal{0U};
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        owner_ = fs::temp_directory_path() /
                 ("hlclient-stock-runtime-corpus-test-" +
                  std::to_string(nonce) + "-" +
                  std::to_string(ordinal.fetch_add(1U)));
        run_ = owner_ / kRunId;
        fs::create_directories(run_ / "raw");
        fs::create_directories(run_ / "logs");
    }

    TemporaryCorpus(const TemporaryCorpus&) = delete;
    TemporaryCorpus& operator=(const TemporaryCorpus&) = delete;

    ~TemporaryCorpus()
    {
        std::error_code ignored;
        if (owner_.filename().string().starts_with(
                "hlclient-stock-runtime-corpus-test-")) {
            fs::remove_all(owner_, ignored);
        }
    }

    [[nodiscard]] const fs::path& run() const noexcept { return run_; }

private:
    fs::path owner_;
    fs::path run_;
};

void write_bytes(
    const fs::path& path,
    const std::span<const std::byte> bytes)
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    REQUIRE(output);
    if (!bytes.empty()) {
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    output.close();
    REQUIRE(output);
}

void write_text(const fs::path& path, const std::string_view text)
{
    write_bytes(path, std::as_bytes(std::span{text.data(), text.size()}));
}

void replace_text_once(
    const fs::path& path,
    const std::string_view before,
    const std::string_view after)
{
    std::ifstream input{path, std::ios::binary};
    REQUIRE(input);
    std::string text{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    REQUIRE_FALSE(input.bad());
    const auto position = text.find(before);
    REQUIRE(position != std::string::npos);
    REQUIRE(text.find(before, position + before.size()) == std::string::npos);
    text.replace(position, before.size(), after);
    write_text(path, text);
}

[[nodiscard]] std::string digest(
    const std::span<const std::byte> bytes)
{
    const auto value = hlclient::hash::sha256(bytes);
    REQUIRE(value);
    return hlclient::hash::sha256_hex(*value);
}

[[nodiscard]] goldsrc::StockRuntimeTransportJournalEntry journal_entry(
    const std::size_t observed,
    const goldsrc::StockRuntimeCaptureDirection direction,
    const std::span<const std::byte> bytes)
{
    const bool c2s =
        direction == goldsrc::StockRuntimeCaptureDirection::client_to_server;
    return {
        observed,
        direction,
        1U,
        observed == 0U ? 0U : 30'000'000U,
        bytes.size(),
        observed == 0U ? "00000000-c2s.bin" : "00000001-s2c.bin",
        c2s ? goldsrc::StockRuntimeTransportRole::research_client
            : goldsrc::StockRuntimeTransportRole::research_server,
        c2s ? goldsrc::StockRuntimeTransportRole::research_server
            : goldsrc::StockRuntimeTransportRole::research_client,
        goldsrc::StockRuntimeCaptureAction::forward,
        goldsrc::StockRuntimeTransportHoldState::none,
        {observed},
        true,
        false,
        digest(bytes),
    };
}

void populate_corpus_bytes(
    const fs::path& run,
    const std::span<const std::byte> c2s,
    const std::span<const std::byte> s2c)
{
    write_bytes(run / "raw" / "00000000-c2s.bin", c2s);
    write_bytes(run / "raw" / "00000001-s2c.bin", s2c);

    const std::array journal{
        journal_entry(
            0U, goldsrc::StockRuntimeCaptureDirection::client_to_server,
            c2s),
        journal_entry(
            1U, goldsrc::StockRuntimeCaptureDirection::server_to_client,
            s2c),
    };
    std::string journal_text;
    for (const auto& value : journal) {
        journal_text +=
            goldsrc::serialize_stock_runtime_transport_journal_entry(value);
        journal_text.push_back('\n');
    }
    write_text(run / "transport-journal.jsonl", journal_text);

    goldsrc::StockRuntimeCaptureMetadata metadata;
    metadata.counters.observed_datagrams = 2U;
    metadata.counters.observed_raw_bytes = c2s.size() + s2c.size();
    metadata.counters.client_packets = 1U;
    metadata.counters.server_packets = 1U;
    metadata.counters.emitted_datagrams = 2U;
    metadata.counters.emitted_bytes = c2s.size() + s2c.size();
    metadata.bounded_transport_complete = true;
    write_text(
        run / "capture-metadata.json",
        goldsrc::serialize_stock_runtime_capture_metadata(metadata));

    write_text(
        run / "version-observation.staged.json",
        "{\"schema\":\"hlclient.stock-runtime-version-observation.v1\","
        "\"map_category\":\"boot_camp\","
        "\"client_file_version\":\"1.1.1.1\","
        "\"client_pe_machine\":\"x86\","
        "\"client_signature\":\"valid\","
        "\"client_profile_fingerprint\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"server_launcher_version\":\"4.1.1.1\","
        "\"server_pe_machine\":\"x86\","
        "\"server_signature\":\"valid\","
        "\"server_profile_fingerprint\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\","
        "\"steam_app_id\":70,\"steam_build_id\":15961492,"
        "\"server_engine_version\":\"1.1.2.2\","
        "\"protocol\":48,\"server_build\":10210,"
        "\"evidence_status\":\"observed\"}");
    write_text(
        run / "isolation-attestation.staged.json",
        "{\"schema\":\"hlclient.stock-runtime-isolation-attestation.v1\","
        "\"session_type\":\"dynamic\",\"persistent_rule_count\":0,"
        "\"ipv4_loopback\":\"allowed\","
        "\"ipv6_loopback\":\"capability_unavailable\","
        "\"non_loopback_canary\":\"denied_os_classified\","
        "\"cleanup_status\":\"exact\",\"evidence_status\":\"observed\"}");
    write_text(
        run / "restoration-attestation.staged.json",
        "{\"schema\":\"hlclient.stock-runtime-restoration.v1\","
        "\"external_file_drift\":\"none\",\"snapshot_entry_count\":12,"
        "\"pre_manifest_sha256\":\"CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC\","
        "\"post_manifest_sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\","
        "\"external_snapshot_entry_count\":3,"
        "\"external_pre_manifest_sha256\":\"DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD\","
        "\"external_post_manifest_sha256\":\"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\","
        "\"created_files_removed\":true,\"protected_paths_included\":true,"
        "\"owned_processes_stopped\":true,\"input_automation_used\":false,"
        "\"input_events_injected\":0,\"orchestrator_exit_code\":0,"
        "\"restoration_status\":\"exact\"}");
}

void populate_corpus(
    const fs::path& run,
    const std::array<std::byte, 3U>& c2s,
    const std::array<std::byte, 3U>& s2c)
{
    populate_corpus_bytes(run, c2s, s2c);
}

void publish_manifest(const fs::path& run)
{
    const auto prepublication = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
        run, goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
    REQUIRE(prepublication);
    REQUIRE(prepublication.state);
    const auto transport_hash =
        std::string{prepublication.state->structural_sha256()};
    REQUIRE(fs::copy_file(
        run / "version-observation.staged.json",
        run / "version-observation.json"));
    REQUIRE(fs::copy_file(
        run / "isolation-attestation.staged.json",
        run / "isolation-attestation.json"));
    REQUIRE(fs::copy_file(
        run / "restoration-attestation.staged.json",
        run / "restoration-attestation.json"));
    write_text(
        run / "research-run-metadata.json",
        "{\"schema\":\"hlclient.stock-runtime-research-run.v1\","
        "\"run_id\":\"0123456789abcdef0123456789abcdef\","
        "\"scenario\":\"baseline\",\"map_category\":\"boot_camp\","
        "\"duration_ms\":30000,\"isolation_status\":\"verified\","
        "\"process_ownership_status\":\"verified-cleanup\","
        "\"version_profile_status\":\"verified\","
        "\"relay_status\":\"true\",\"client_ready_status\":\"true\","
        "\"restoration_status\":\"exact\",\"external_drift_status\":\"none\","
        "\"external_target_profile\":\"none\",\"external_target_count\":0,"
        "\"raw_datagram_count\":2,\"journal_entry_count\":2,"
        "\"delivered_sequenced_c2s_count\":1,"
        "\"delivered_sequenced_s2c_count\":1,"
        "\"delivered_fragment_datagram_count\":0,"
        "\"reassembled_payload_count\":0,"
        "\"decompressed_payload_count\":0,\"offline_replay_status\":\"success\","
        "\"post_resource_boundary_status\":\"observed\","
        "\"post_resource_replay_payload_ordinal\":1,"
        "\"post_resource_corpus_observed_ordinal\":1,"
        "\"post_resource_delivery_ordinal\":1,"
        "\"post_resource_byte_offset\":0,\"post_resource_bit_offset\":0,"
        "\"post_resource_source_sequence\":1,"
        "\"post_resource_source_payload_bytes\":3,"
        "\"post_resource_source_payload_bits\":24,"
        "\"post_resource_next_unconsumed_bits\":24,"
        "\"post_resource_reassembled\":false,"
        "\"post_resource_decompressed\":false,"
        "\"post_resource_boundary_byte_aligned\":true,"
        "\"first_observation_status\":\"observed\","
        "\"first_candidate\":\"7\",\"first_candidate_bit_width\":8,"
        "\"first_candidate_recurrence\":1,"
        "\"candidate_stability\":\"single_observation\","
        "\"transport_structural_sha256\":\"" + transport_hash + "\","
        "\"replay_structural_sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\","
        "\"last_observed_transport_timestamp_us\":30000000,"
        "\"last_delivered_sequenced_s2c_timestamp_us\":30000000,"
        "\"accepted_transport_run\":true,\"accepted_evidence_run\":true,"
        "\"failure_category\":\"none\"}");
}

void populate_reconnect_documents(const fs::path& run)
{
    replace_text_once(
        run / "capture-metadata.json", "\"scenario\": \"baseline\"",
        "\"scenario\": \"reconnect\"");
    write_text(
        run / "reconnect-transport-observation.staged.json",
        R"json({"schema":"hlclient.stock-runtime-reconnect-transport-observation.v1","connection_generation_count":2,"generation_distinct":true,"generation_a_tail_emitter_ready_before_shutdown":true,"generation_a_controlled_shutdown":"observed_by_orchestrator","generation_a_endpoint_quiet":true,"guard_continuity":"observed_by_orchestrator","server_continuity":"observed_by_orchestrator","relay_continuity":"observed","post_resource_boundary_status":"evidence_pending","candidate_status":"evidence_pending","candidate_body_consumed":false,"candidate_semantic_category_assigned":false,"retired_generation_a_tail_sink":"routing_only","retired_generation_a_server_tail_packet_count":0,"generation_b_sequenced_after_fresh_accept":true,"bounded_transport_complete":true,"generations":[{"generation_ordinal":1,"endpoint_role_identity":"research_client_generation_a","process_role_identity":"owned_client_generation_a","first_observed_ordinal":0,"last_observed_ordinal":0,"connectionless_exchange_count":1,"connect_observed":true,"accept_observed":true,"first_sequenced_packet_ordinal":0,"client_to_server_packet_count":1,"server_to_client_packet_count":1,"profile_identity":"stock_protocol_48_build_10210_evidence_pending","post_resource_boundary_status":"evidence_pending","candidate_status":"evidence_pending","candidate_body_consumed":false,"candidate_semantic_category_assigned":false},{"generation_ordinal":2,"endpoint_role_identity":"research_client_generation_b","process_role_identity":"owned_client_generation_b","first_observed_ordinal":1,"last_observed_ordinal":1,"connectionless_exchange_count":1,"connect_observed":true,"accept_observed":true,"first_sequenced_packet_ordinal":1,"client_to_server_packet_count":1,"server_to_client_packet_count":1,"profile_identity":"stock_protocol_48_build_10210_evidence_pending","post_resource_boundary_status":"evidence_pending","candidate_status":"evidence_pending","candidate_body_consumed":false,"candidate_semantic_category_assigned":false}]})json");
    write_text(
        run / "reconnect-orchestration.staged.json",
        R"json({"schema":"hlclient.stock-runtime-reconnect-orchestration.v1","connection_generation_count":2,"generation_distinct":true,"generation_a_process_role_identity":"owned_client_generation_a","generation_b_process_role_identity":"owned_client_generation_b","generation_a_endpoint_role_identity":"research_client_generation_a","generation_b_endpoint_role_identity":"research_client_generation_b","generation_a_tail_emitter_ready_before_shutdown":true,"generation_a_controlled_shutdown":true,"generation_a_endpoint_quiet":true,"generation_b_fresh_owned_process":true,"generation_b_fresh_connection_lifecycle":"observed_by_relay","guard_continuity":true,"server_continuity":true,"relay_continuity":true,"cleanup_status":"exact","restoration_status":"wrapper_pending","post_resource_boundary_status":"evidence_pending","candidate_status":"evidence_pending","candidate_body_consumed":false,"candidate_semantic_category_assigned":false,"publication_status":"staged"})json");
}

void publish_reconnect_manifest(const fs::path& run)
{
    publish_manifest(run);
    replace_text_once(
        run / "research-run-metadata.json", "\"scenario\":\"baseline\"",
        "\"scenario\":\"reconnect\"");
    replace_text_once(
        run / "research-run-metadata.json",
        "\"first_candidate_recurrence\":1",
        "\"first_candidate_recurrence\":2");
    replace_text_once(
        run / "research-run-metadata.json",
        "\"candidate_stability\":\"single_observation\"",
        "\"candidate_stability\":\"stable_observation\"");
    replace_text_once(
        run / "research-run-metadata.json",
        "\"failure_category\":\"none\"}",
        "\"connection_generation_count\":2,"
        "\"exact_boundary_count\":2,\"runtime_candidate_count\":2,"
        "\"generation_distinct\":true,\"candidate_conflict\":false,"
        "\"failure_category\":\"none\"}");
    write_text(
        run / "reconnect-observation.json",
        R"json({"schema":"hlclient.stock-runtime-reconnect-observation.v1","connection_generation_count":2,"exact_boundary_count":2,"runtime_candidate_count":2,"generation_distinct":true,"candidate_conflict":false,"guard_continuity":true,"server_continuity":true,"relay_continuity":true,"cleanup_exact":true,"restoration_exact":true,"candidate_body_consumed":false,"candidate_semantic_category_assigned":false,"retired_generation_a_tail_sink":"routing_only","retired_generation_a_server_tail_packet_count":0,"generation_b_sequenced_after_fresh_accept":true,"generations":[{"generation_ordinal":1,"profile_identity":"stock_protocol_48_build_10210_evidence_pending","owned_client_process_role_identity":"owned_client_generation_a","learned_client_endpoint_role_identity":"research_client_generation_a","fresh_owned_client_process":true,"learned_client_endpoint_observed":true,"learned_client_endpoint_distinct_from_previous":false,"first_observed_ordinal":0,"last_observed_ordinal":0,"connectionless_exchange_count":1,"connect_observed":true,"accept_observed":true,"first_sequenced_packet_ordinal":0,"client_to_server_packet_count":1,"server_to_client_packet_count":1,"controlled_client_shutdown_observed":true,"retired_client_endpoint_quiet":true,"exact_post_resource_boundary":{"observed":true,"replay_payload_ordinal":0,"corpus_observed_ordinal":0,"delivery_ordinal":0,"byte_offset":0,"bit_offset":0,"source_payload_byte_count":8,"source_payload_bit_count":64,"next_unconsumed_bit_count":64},"candidate_observation":{"observed":true,"candidate_bit_width":8,"numeric_candidate":7,"bounded_bit_prefix":null,"byte_aligned":true,"body_consumed":false,"semantic_category_assigned":false}},{"generation_ordinal":2,"profile_identity":"stock_protocol_48_build_10210_evidence_pending","owned_client_process_role_identity":"owned_client_generation_b","learned_client_endpoint_role_identity":"research_client_generation_b","fresh_owned_client_process":true,"learned_client_endpoint_observed":true,"learned_client_endpoint_distinct_from_previous":true,"first_observed_ordinal":1,"last_observed_ordinal":1,"connectionless_exchange_count":1,"connect_observed":true,"accept_observed":true,"first_sequenced_packet_ordinal":1,"client_to_server_packet_count":1,"server_to_client_packet_count":1,"controlled_client_shutdown_observed":false,"retired_client_endpoint_quiet":false,"exact_post_resource_boundary":{"observed":true,"replay_payload_ordinal":0,"corpus_observed_ordinal":1,"delivery_ordinal":1,"byte_offset":0,"bit_offset":0,"source_payload_byte_count":8,"source_payload_bit_count":64,"next_unconsumed_bit_count":64},"candidate_observation":{"observed":true,"candidate_bit_width":8,"numeric_candidate":7,"bounded_bit_prefix":null,"byte_aligned":true,"body_consumed":false,"semantic_category_assigned":false}}]})json");
}

TEST_CASE("Corpus prepublication is a fail-closed transaction boundary",
          "[goldsrc][stock-runtime][corpus][prepublication]")
{
    TemporaryCorpus fixture;
    populate_corpus_bytes(
        fixture.run(), kSequencedClientDatagram, kSequencedServerDatagram);

    const goldsrc::StockRuntimeCaptureCorpusLoader loader;
    const auto prepublication = loader.load(
        fixture.run(),
        goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
    const auto prepublication_diagnostic = prepublication.error
        ? std::string{goldsrc::to_string(prepublication.error->code)} +
              ": " + prepublication.error->context
        : std::string{"no corpus error"};
    INFO(prepublication_diagnostic);
    REQUIRE(prepublication);
    REQUIRE(prepublication.state);
    CHECK(prepublication.state->publication_state() ==
          goldsrc::StockRuntimeCaptureCorpusPublicationState::
              ready_for_manifest_publication);
    CHECK_FALSE(prepublication.state->accepted_evidence_run());
    CHECK_FALSE(prepublication.state->research_run_metadata());
    CHECK_FALSE(prepublication.state->accepted_manifest_claims());
    CHECK(prepublication.state->delivered_datagrams().size() == 2U);
    CHECK(prepublication.state->delivered_client_to_server().size() == 1U);
    CHECK(prepublication.state->delivered_server_to_client().size() == 1U);

    publish_manifest(fixture.run());
    const auto wrong_stage = loader.load(
        fixture.run(),
        goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
    REQUIRE_FALSE(wrong_stage);
    REQUIRE(wrong_stage.error);
    CHECK((wrong_stage.error->code ==
               goldsrc::StockRuntimeCaptureCorpusErrorCode::unexpected_manifest ||
           wrong_stage.error->code ==
               goldsrc::StockRuntimeCaptureCorpusErrorCode::unexpected_file));

    const auto published = loader.load(
        fixture.run(),
        goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
    REQUIRE(published);
    REQUIRE(published.state);
    CHECK(published.state->accepted_evidence_run());
    CHECK(published.state->publication_state() ==
          goldsrc::StockRuntimeCaptureCorpusPublicationState::published_accepted);
    REQUIRE(published.state->research_run_metadata());

    SECTION("external target metadata is required and exactly typed") {
        replace_text_once(
            fixture.run() / "research-run-metadata.json",
            "\"external_target_profile\":\"none\",\"external_target_count\":0,",
            "\"external_target_profile\":\"none\",");
        CHECK_FALSE(goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published));
    }
    SECTION("reviewed non-executable external targets are accepted") {
        replace_text_once(
            fixture.run() / "research-run-metadata.json",
            "\"external_target_profile\":\"none\",\"external_target_count\":0,",
            "\"external_target_profile\":\"reviewed-non-executable-v1\","
            "\"external_target_count\":1,");
        CHECK(goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published));
    }
    SECTION("unknown positive external target profile is rejected") {
        replace_text_once(
            fixture.run() / "research-run-metadata.json",
            "\"external_target_profile\":\"none\",\"external_target_count\":0,",
            "\"external_target_profile\":\"syntactically-valid-unknown\","
            "\"external_target_count\":1,");
        CHECK_FALSE(goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published));
    }
    SECTION("reviewed external target profile requires a positive count") {
        replace_text_once(
            fixture.run() / "research-run-metadata.json",
            "\"external_target_profile\":\"none\",\"external_target_count\":0,",
            "\"external_target_profile\":\"reviewed-non-executable-v1\","
            "\"external_target_count\":0,");
        CHECK_FALSE(goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published));
    }
    SECTION("none external target profile requires a zero count") {
        replace_text_once(
            fixture.run() / "research-run-metadata.json",
            "\"external_target_profile\":\"none\",\"external_target_count\":0,",
            "\"external_target_profile\":\"none\","
            "\"external_target_count\":1,");
        CHECK_FALSE(goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published));
    }
    SECTION("unknown zero-count external target profile is rejected") {
        replace_text_once(
            fixture.run() / "research-run-metadata.json",
            "\"external_target_profile\":\"none\",\"external_target_count\":0,",
            "\"external_target_profile\":\"syntactically-valid-unknown\","
            "\"external_target_count\":0,");
        CHECK_FALSE(goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published));
    }
    SECTION("external target count is bounded by materialization") {
        replace_text_once(
            fixture.run() / "research-run-metadata.json",
            "\"external_target_profile\":\"none\",\"external_target_count\":0,",
            "\"external_target_profile\":\"reviewed-non-executable-v1\","
            "\"external_target_count\":4097,");
        CHECK_FALSE(goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published));
    }
    REQUIRE(published.state->accepted_manifest_claims());
    goldsrc::StockRuntimeAcceptedManifestClaims expected_claims;
    expected_claims.replay_payload_ordinal = 1U;
    expected_claims.corpus_observed_ordinal = 1U;
    expected_claims.delivery_ordinal = 1U;
    expected_claims.source_netchan_sequence = 1U;
    expected_claims.source_payload_byte_count = 3U;
    expected_claims.source_payload_bit_count = 24U;
    expected_claims.next_unconsumed_bit_count = 24U;
    expected_claims.byte_aligned = true;
    expected_claims.first_candidate = "7";
    expected_claims.candidate_bit_width = 8U;
    expected_claims.candidate_recurrence = 1U;
    expected_claims.candidate_stability = "single_observation";
    expected_claims.replay_structural_sha256 =
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    CHECK(*published.state->accepted_manifest_claims() == expected_claims);

    const auto rejects_claim_mutation = [&expected_claims](
        const std::string_view name, const auto& mutate) {
        auto altered = expected_claims;
        mutate(altered);
        INFO(name);
        CHECK_FALSE(altered == expected_claims);
    };
    rejects_claim_mutation("reassembled count", [](auto& value) {
        ++value.reassembled_payload_count;
    });
    rejects_claim_mutation("decompressed count", [](auto& value) {
        ++value.decompressed_payload_count;
    });
    rejects_claim_mutation("replay payload ordinal", [](auto& value) {
        ++value.replay_payload_ordinal;
    });
    rejects_claim_mutation("observed ordinal", [](auto& value) {
        ++value.corpus_observed_ordinal;
    });
    rejects_claim_mutation("delivery ordinal", [](auto& value) {
        ++value.delivery_ordinal;
    });
    rejects_claim_mutation("byte offset", [](auto& value) {
        ++value.byte_offset;
    });
    rejects_claim_mutation("bit offset", [](auto& value) {
        ++value.bit_offset;
    });
    rejects_claim_mutation("source sequence", [](auto& value) {
        ++value.source_netchan_sequence;
    });
    rejects_claim_mutation("source payload bytes", [](auto& value) {
        ++value.source_payload_byte_count;
    });
    rejects_claim_mutation("source payload bits", [](auto& value) {
        ++value.source_payload_bit_count;
    });
    rejects_claim_mutation("remaining bits", [](auto& value) {
        ++value.next_unconsumed_bit_count;
    });
    rejects_claim_mutation("reassembled flag", [](auto& value) {
        value.reassembled = !value.reassembled;
    });
    rejects_claim_mutation("decompressed flag", [](auto& value) {
        value.decompressed = !value.decompressed;
    });
    rejects_claim_mutation("byte alignment", [](auto& value) {
        value.byte_aligned = !value.byte_aligned;
    });
    rejects_claim_mutation("candidate", [](auto& value) {
        value.first_candidate = "8";
    });
    rejects_claim_mutation("candidate width", [](auto& value) {
        --value.candidate_bit_width;
    });
    rejects_claim_mutation("candidate recurrence", [](auto& value) {
        ++value.candidate_recurrence;
    });
    rejects_claim_mutation("candidate stability", [](auto& value) {
        value.candidate_stability = "stable_observation";
    });
    rejects_claim_mutation("replay structural hash", [](auto& value) {
        value.replay_structural_sha256.front() = 'a';
    });
}

TEST_CASE("Raw integrity is local and excluded from corpus structural identity",
          "[goldsrc][stock-runtime][corpus][privacy][mutation]")
{
    TemporaryCorpus first;
    TemporaryCorpus second;
    populate_corpus(
        first.run(),
        {std::byte{0x01U}, std::byte{0x02U}, std::byte{0x03U}},
        {std::byte{0x04U}, std::byte{0x05U}, std::byte{0x06U}});
    populate_corpus(
        second.run(),
        {std::byte{0xa1U}, std::byte{0xa2U}, std::byte{0xa3U}},
        {std::byte{0xb4U}, std::byte{0xb5U}, std::byte{0xb6U}});

    const goldsrc::StockRuntimeCaptureCorpusLoader loader;
    const auto left = loader.load(
        first.run(), goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
    const auto right = loader.load(
        second.run(), goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
    const auto left_diagnostic = left.error
        ? std::string{goldsrc::to_string(left.error->code)} + ": " +
              left.error->context
        : std::string{"no left corpus error"};
    const auto right_diagnostic = right.error
        ? std::string{goldsrc::to_string(right.error->code)} + ": " +
              right.error->context
        : std::string{"no right corpus error"};
    INFO(left_diagnostic);
    INFO(right_diagnostic);
    REQUIRE(left);
    REQUIRE(right);
    REQUIRE(left.state);
    REQUIRE(right.state);
    CHECK(left.state->structural_sha256() == right.state->structural_sha256());
    CHECK(left.state->observed_datagrams()[0].journal().sha256 !=
          right.state->observed_datagrams()[0].journal().sha256);
    CHECK(left.state->structural_sha256() !=
          left.state->observed_datagrams()[0].journal().sha256);

    write_bytes(
        first.run() / "raw" / "00000000-c2s.bin",
        std::array{std::byte{0xffU}, std::byte{0x02U}, std::byte{0x03U}});
    const auto mutated = loader.load(
        first.run(), goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
    REQUIRE_FALSE(mutated);
    REQUIRE(mutated.error);
    CHECK(mutated.error->code ==
          goldsrc::StockRuntimeCaptureCorpusErrorCode::raw_hash_mismatch);
}

TEST_CASE("Corpus publication leaf sets are transactional and cross-bound",
          "[goldsrc][stock-runtime][corpus][publication][mutation]")
{
    SECTION("prepublication rejects any final attestation leaf")
    {
        TemporaryCorpus fixture;
        populate_corpus_bytes(
            fixture.run(), kSequencedClientDatagram,
            kSequencedServerDatagram);
        REQUIRE(fs::copy_file(
            fixture.run() / "version-observation.staged.json",
            fixture.run() / "version-observation.json"));

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::unexpected_file);
    }

    SECTION("published corpus requires every retained staged leaf")
    {
        TemporaryCorpus fixture;
        populate_corpus_bytes(
            fixture.run(), kSequencedClientDatagram,
            kSequencedServerDatagram);
        publish_manifest(fixture.run());
        REQUIRE(fs::remove(
            fixture.run() / "isolation-attestation.staged.json"));

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::missing_manifest);
    }

    SECTION("published staged and final leaves must be byte-identical")
    {
        TemporaryCorpus fixture;
        populate_corpus_bytes(
            fixture.run(), kSequencedClientDatagram,
            kSequencedServerDatagram);
        publish_manifest(fixture.run());
        replace_text_once(
            fixture.run() / "version-observation.staged.json",
            "\"map_category\":\"boot_camp\"",
            "\"map_category\":\"crossfire\"");

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch);
    }
}

TEST_CASE("Private restoration digests do not fingerprint public transport identity",
          "[goldsrc][stock-runtime][corpus][privacy][structural-hash]")
{
    TemporaryCorpus first;
    TemporaryCorpus second;
    constexpr std::array c2s{
        std::byte{0x11U}, std::byte{0x12U}, std::byte{0x13U}};
    constexpr std::array s2c{
        std::byte{0x21U}, std::byte{0x22U}, std::byte{0x23U}};
    populate_corpus(first.run(), c2s, s2c);
    populate_corpus(second.run(), c2s, s2c);
    const auto private_restoration =
        second.run() / "restoration-attestation.staged.json";
    replace_text_once(
        private_restoration,
        "\"pre_manifest_sha256\":\"CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC\"",
        "\"pre_manifest_sha256\":\"EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE\"");
    replace_text_once(
        private_restoration,
        "\"post_manifest_sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"",
        "\"post_manifest_sha256\":\"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\"");
    replace_text_once(
        private_restoration,
        "\"external_pre_manifest_sha256\":\"DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD\"",
        "\"external_pre_manifest_sha256\":\"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF\"");
    replace_text_once(
        private_restoration,
        "\"external_post_manifest_sha256\":\"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\"",
        "\"external_post_manifest_sha256\":\"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\"");

    const goldsrc::StockRuntimeCaptureCorpusLoader loader;
    const auto left = loader.load(
        first.run(), goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
    const auto right = loader.load(
        second.run(), goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
    REQUIRE(left);
    REQUIRE(right);
    REQUIRE(left.state);
    REQUIRE(right.state);
    CHECK(left.state->restoration_attestation().structural_sha256 !=
          right.state->restoration_attestation().structural_sha256);
    CHECK(left.state->structural_sha256() == right.state->structural_sha256());
}

TEST_CASE("Published corpus binds scenario and map to immutable observations",
          "[goldsrc][stock-runtime][corpus][manifest][mutation]")
{
    SECTION("three explicit relay aliases canonicalize")
    {
        constexpr std::array aliases{
            std::string_view{"drop-server-runtime"},
            std::string_view{"duplicate-server-runtime"},
            std::string_view{"reorder-server-runtime"},
        };
        constexpr std::array canonical{
            std::string_view{"drop-server-to-client-transport-ordinal"},
            std::string_view{"duplicate-server-to-client-transport-ordinal"},
            std::string_view{"reorder-server-to-client-transport-ordinal"},
        };
        for (std::size_t index = 0U; index < aliases.size(); ++index) {
            TemporaryCorpus fixture;
            populate_corpus_bytes(
                fixture.run(), kSequencedClientDatagram,
                kSequencedServerDatagram);
            replace_text_once(
                fixture.run() / "capture-metadata.json",
                "\"scenario\": \"baseline\"",
                "\"scenario\": \"" + std::string{aliases[index]} + "\"");
            publish_manifest(fixture.run());
            replace_text_once(
                fixture.run() / "research-run-metadata.json",
                "\"scenario\":\"baseline\"",
                "\"scenario\":\"" + std::string{canonical[index]} + "\"");

            const auto accepted =
                goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
                    fixture.run(),
                    goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
            const auto diagnostic = accepted.error
                ? accepted.error->context
                : std::string{"no corpus error"};
            INFO(diagnostic);
            REQUIRE(accepted);
            REQUIRE(accepted.state);
            CHECK(accepted.state->accepted_evidence_run());
        }
    }

    SECTION("scenario mismatch")
    {
        TemporaryCorpus fixture;
        populate_corpus_bytes(
            fixture.run(), kSequencedClientDatagram,
            kSequencedServerDatagram);
        publish_manifest(fixture.run());
        replace_text_once(
            fixture.run() / "research-run-metadata.json",
            "\"scenario\":\"baseline\"", "\"scenario\":\"idle-runtime\"");

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch);
    }

    SECTION("unknown scenario alias")
    {
        TemporaryCorpus fixture;
        populate_corpus_bytes(
            fixture.run(), kSequencedClientDatagram,
            kSequencedServerDatagram);
        publish_manifest(fixture.run());
        replace_text_once(
            fixture.run() / "research-run-metadata.json",
            "\"scenario\":\"baseline\"", "\"scenario\":\"future-alias\"");

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch);
    }

    SECTION("final map differs from orchestrator observation")
    {
        TemporaryCorpus fixture;
        populate_corpus_bytes(
            fixture.run(), kSequencedClientDatagram,
            kSequencedServerDatagram);
        publish_manifest(fixture.run());
        replace_text_once(
            fixture.run() / "research-run-metadata.json",
            "\"map_category\":\"boot_camp\"",
            "\"map_category\":\"crossfire\"");

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch);
    }

    SECTION("orchestrator map is outside the immutable allowlist")
    {
        TemporaryCorpus fixture;
        populate_corpus(
            fixture.run(),
            {std::byte{0x41U}, std::byte{0x42U}, std::byte{0x43U}},
            {std::byte{0x44U}, std::byte{0x45U}, std::byte{0x46U}});
        replace_text_once(
            fixture.run() / "version-observation.staged.json",
            "\"map_category\":\"boot_camp\"",
            "\"map_category\":\"unknown_map\"");

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch);
    }

    SECTION("three delivered counter families are corpus-bound")
    {
        constexpr std::array before{
            std::string_view{"\"delivered_sequenced_c2s_count\":1"},
            std::string_view{"\"delivered_sequenced_s2c_count\":1"},
            std::string_view{"\"delivered_fragment_datagram_count\":0"},
        };
        constexpr std::array after{
            std::string_view{"\"delivered_sequenced_c2s_count\":2"},
            std::string_view{"\"delivered_sequenced_s2c_count\":2"},
            std::string_view{"\"delivered_fragment_datagram_count\":1"},
        };
        for (std::size_t index = 0U; index < before.size(); ++index) {
            TemporaryCorpus fixture;
            populate_corpus_bytes(
                fixture.run(), kSequencedClientDatagram,
                kSequencedServerDatagram);
            publish_manifest(fixture.run());
            replace_text_once(
                fixture.run() / "research-run-metadata.json",
                before[index], after[index]);

            const auto rejected =
                goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
                    fixture.run(),
                    goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
            REQUIRE_FALSE(rejected);
            CHECK_FALSE(rejected.state);
            REQUIRE(rejected.error);
            CHECK(rejected.error->code ==
                  goldsrc::StockRuntimeCaptureCorpusErrorCode::count_mismatch);
        }
    }

    SECTION("transport structural hash is corpus-bound")
    {
        TemporaryCorpus fixture;
        populate_corpus_bytes(
            fixture.run(), kSequencedClientDatagram,
            kSequencedServerDatagram);
        const auto prepublication =
            goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
                fixture.run(),
                goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        REQUIRE(prepublication);
        REQUIRE(prepublication.state);
        const auto hash = std::string{prepublication.state->structural_sha256()};
        publish_manifest(fixture.run());
        replace_text_once(
            fixture.run() / "research-run-metadata.json",
            "\"transport_structural_sha256\":\"" + hash + "\"",
            "\"transport_structural_sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"");

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::count_mismatch);
    }

    SECTION("both transport timestamps are journal-bound")
    {
        constexpr std::array before{
            std::string_view{
                "\"last_observed_transport_timestamp_us\":30000000"},
            std::string_view{
                "\"last_delivered_sequenced_s2c_timestamp_us\":30000000"},
        };
        constexpr std::array after{
            std::string_view{
                "\"last_observed_transport_timestamp_us\":30000001"},
            std::string_view{
                "\"last_delivered_sequenced_s2c_timestamp_us\":30000001"},
        };
        for (std::size_t index = 0U; index < before.size(); ++index) {
            TemporaryCorpus fixture;
            populate_corpus_bytes(
                fixture.run(), kSequencedClientDatagram,
                kSequencedServerDatagram);
            publish_manifest(fixture.run());
            replace_text_once(
                fixture.run() / "research-run-metadata.json",
                before[index], after[index]);

            const auto rejected =
                goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
                    fixture.run(),
                    goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
            REQUIRE_FALSE(rejected);
            CHECK_FALSE(rejected.state);
            REQUIRE(rejected.error);
            CHECK(rejected.error->code ==
                  goldsrc::StockRuntimeCaptureCorpusErrorCode::count_mismatch);
        }
    }

    SECTION("per-run manifest cannot claim cross-run stability")
    {
        TemporaryCorpus fixture;
        populate_corpus_bytes(
            fixture.run(), kSequencedClientDatagram,
            kSequencedServerDatagram);
        publish_manifest(fixture.run());
        replace_text_once(
            fixture.run() / "research-run-metadata.json",
            "\"candidate_stability\":\"single_observation\"",
            "\"candidate_stability\":\"stable_observation\"");

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch);
    }

    SECTION("ambiguous legacy delivered counter names are rejected")
    {
        TemporaryCorpus fixture;
        populate_corpus_bytes(
            fixture.run(), kSequencedClientDatagram,
            kSequencedServerDatagram);
        publish_manifest(fixture.run());
        replace_text_once(
            fixture.run() / "research-run-metadata.json",
            "\"delivered_sequenced_c2s_count\"",
            "\"sequenced_c2s_count\"");

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::invalid_json);
    }

    SECTION("candidate width exceeds the exact remaining source bits")
    {
        TemporaryCorpus fixture;
        populate_corpus_bytes(
            fixture.run(), kSequencedClientDatagram,
            kSequencedServerDatagram);
        publish_manifest(fixture.run());
        const auto manifest = fixture.run() / "research-run-metadata.json";
        replace_text_once(
            manifest, "\"post_resource_byte_offset\":0",
            "\"post_resource_byte_offset\":2");
        replace_text_once(
            manifest, "\"post_resource_bit_offset\":0",
            "\"post_resource_bit_offset\":4");
        replace_text_once(
            manifest, "\"post_resource_next_unconsumed_bits\":24",
            "\"post_resource_next_unconsumed_bits\":4");
        replace_text_once(
            manifest, "\"post_resource_boundary_byte_aligned\":true",
            "\"post_resource_boundary_byte_aligned\":false");
        replace_text_once(
            manifest, "\"first_candidate\":\"7\"",
            "\"first_candidate\":\"bit-prefix:7\"");

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch);
    }
}

TEST_CASE("Corpus rejects path and manifest mutations without partial state",
          "[goldsrc][stock-runtime][corpus][mutation]")
{
    SECTION("unexpected root entry")
    {
        TemporaryCorpus fixture;
        populate_corpus(
            fixture.run(),
            {std::byte{0x01U}, std::byte{0x02U}, std::byte{0x03U}},
            {std::byte{0x04U}, std::byte{0x05U}, std::byte{0x06U}});
        write_text(fixture.run() / "unexpected.txt", "no");

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::unexpected_file);
    }

    SECTION("reparse-backed parent component")
    {
        TemporaryCorpus target;
        TemporaryCorpus path_owner;
        populate_corpus(
            target.run(),
            {std::byte{0x11U}, std::byte{0x12U}, std::byte{0x13U}},
            {std::byte{0x21U}, std::byte{0x22U}, std::byte{0x23U}});

        const auto linked_parent = path_owner.run() / "junction-parent";
        std::error_code link_error;
        fs::create_directory_symlink(
            target.run().parent_path(), linked_parent, link_error);
        if (link_error) {
            SKIP("Directory reparse creation is unavailable: "
                 << link_error.message());
        }

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            linked_parent / kRunId,
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::reparse_point);
    }

    SECTION("hardlinked ignored log")
    {
        TemporaryCorpus fixture;
        populate_corpus(
            fixture.run(),
            {std::byte{0x31U}, std::byte{0x32U}, std::byte{0x33U}},
            {std::byte{0x41U}, std::byte{0x42U}, std::byte{0x43U}});
        const auto first_log = fixture.run() / "logs" / "client.log";
        const auto second_log = fixture.run() / "logs" / "alias.log";
        write_text(first_log, "bounded log");
        std::error_code link_error;
        fs::create_hard_link(first_log, second_log, link_error);
        if (link_error) {
            SKIP("Hard links are unavailable: " << link_error.message());
        }

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::hardlink_detected);
    }
}

TEST_CASE("Corpus reports missing manifests raw files and duplicate ordinals",
          "[goldsrc][stock-runtime][corpus][mutation][missing][ordinal]")
{
    SECTION("required manifest is absent")
    {
        TemporaryCorpus fixture;
        populate_corpus(
            fixture.run(),
            {std::byte{0x01U}, std::byte{0x02U}, std::byte{0x03U}},
            {std::byte{0x04U}, std::byte{0x05U}, std::byte{0x06U}});
        REQUIRE(fs::remove(
            fixture.run() / "version-observation.staged.json"));

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::missing_manifest);
    }

    SECTION("journal-owned raw file is absent")
    {
        TemporaryCorpus fixture;
        populate_corpus(
            fixture.run(),
            {std::byte{0x11U}, std::byte{0x12U}, std::byte{0x13U}},
            {std::byte{0x14U}, std::byte{0x15U}, std::byte{0x16U}});
        REQUIRE(fs::remove(
            fixture.run() / "raw" / "00000001-s2c.bin"));

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::missing_raw_file);
    }

    SECTION("duplicate observed ordinal reaches cross-entry validation")
    {
        TemporaryCorpus fixture;
        constexpr std::array c2s{
            std::byte{0x21U}, std::byte{0x22U}, std::byte{0x23U}};
        constexpr std::array s2c{
            std::byte{0x24U}, std::byte{0x25U}, std::byte{0x26U}};
        populate_corpus(fixture.run(), c2s, s2c);

        auto first = journal_entry(
            0U, goldsrc::StockRuntimeCaptureDirection::client_to_server,
            c2s);
        auto duplicate = journal_entry(
            0U, goldsrc::StockRuntimeCaptureDirection::server_to_client,
            s2c);
        duplicate.raw_filename = "00000000-s2c.bin";
        duplicate.relative_timestamp_us = 25U;
        duplicate.emitted_ordinals = {1U};
        const auto old_raw =
            fixture.run() / "raw" / "00000001-s2c.bin";
        const auto duplicate_raw =
            fixture.run() / "raw" / duplicate.raw_filename;
        fs::rename(old_raw, duplicate_raw);
        REQUIRE(fs::exists(duplicate_raw));

        std::string journal_text =
            goldsrc::serialize_stock_runtime_transport_journal_entry(first);
        journal_text.push_back('\n');
        journal_text +=
            goldsrc::serialize_stock_runtime_transport_journal_entry(duplicate);
        journal_text.push_back('\n');
        write_text(
            fixture.run() / "transport-journal.jsonl", journal_text);

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::invalid_journal);
        REQUIRE(rejected.error->journal_code);
        CHECK(*rejected.error->journal_code ==
              goldsrc::StockRuntimeTransportJournalErrorCode::invalid_ordinal);
        CHECK(rejected.error->ordinal == 1U);
    }
}

TEST_CASE("Corpus rejects raw filename and log limit plus one",
          "[goldsrc][stock-runtime][corpus][mutation][bounds]")
{
    SECTION("raw directory leaf is not in the safe alphabet")
    {
        TemporaryCorpus fixture;
        populate_corpus(
            fixture.run(),
            {std::byte{0x31U}, std::byte{0x32U}, std::byte{0x33U}},
            {std::byte{0x34U}, std::byte{0x35U}, std::byte{0x36U}});
        fs::rename(
            fixture.run() / "raw" / "00000001-s2c.bin",
            fixture.run() / "raw" / "bad name.bin");

        const auto rejected = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::invalid_filename);
    }

    SECTION("configured log count plus one is rejected")
    {
        TemporaryCorpus fixture;
        populate_corpus(
            fixture.run(),
            {std::byte{0x41U}, std::byte{0x42U}, std::byte{0x43U}},
            {std::byte{0x44U}, std::byte{0x45U}, std::byte{0x46U}});
        write_text(fixture.run() / "logs" / "first.log", "one");
        write_text(fixture.run() / "logs" / "second.log", "two");
        goldsrc::StockRuntimeCaptureCorpusLimits limits;
        limits.maximum_log_files = 1U;
        const goldsrc::StockRuntimeCaptureCorpusLoader loader{limits};
        REQUIRE(loader.valid_configuration());

        const auto rejected = loader.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeCaptureCorpusErrorCode::unexpected_file);
    }
}

TEST_CASE("Published corpus owns bytes after its exact sources are destroyed",
          "[goldsrc][stock-runtime][corpus][ownership][source-destruction]")
{
    TemporaryCorpus fixture;
    constexpr std::array c2s{
        std::byte{0x51U}, std::byte{0x52U}, std::byte{0x53U}};
    constexpr std::array s2c{
        std::byte{0x54U}, std::byte{0x55U}, std::byte{0x56U}};
    populate_corpus(fixture.run(), c2s, s2c);

    auto loaded = goldsrc::StockRuntimeCaptureCorpusLoader{}.load(
        fixture.run(),
        goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
    REQUIRE(loaded);
    REQUIRE(loaded.state);
    REQUIRE(loaded.state->observed_datagrams().size() == 2U);
    const auto structural_sha256 =
        std::string{loaded.state->structural_sha256()};

    REQUIRE(fs::remove(
        fixture.run() / "raw" / "00000000-c2s.bin"));
    REQUIRE(fs::remove(
        fixture.run() / "raw" / "00000001-s2c.bin"));
    REQUIRE(fs::remove(
        fixture.run() / "transport-journal.jsonl"));
    CHECK_FALSE(fs::exists(
        fixture.run() / "raw" / "00000000-c2s.bin"));

    CHECK(std::ranges::equal(
        loaded.state->observed_datagrams()[0U].bytes(), c2s));
    CHECK(std::ranges::equal(
        loaded.state->observed_datagrams()[1U].bytes(), s2c));
    CHECK(std::ranges::equal(
        loaded.state->delivered_datagrams()[0U].bytes(), c2s));
    CHECK(std::ranges::equal(
        loaded.state->delivered_datagrams()[1U].bytes(), s2c));
    CHECK(loaded.state->structural_sha256() == structural_sha256);
}

TEST_CASE("Reconnect corpus leaves are strict and scenario dependent",
          "[goldsrc][stock-runtime][corpus][reconnect][mutation]")
{
    const goldsrc::StockRuntimeCaptureCorpusLoader loader;

    SECTION("staged pair is accepted only by reconnect prepublication")
    {
        TemporaryCorpus fixture;
        populate_corpus_bytes(
            fixture.run(), kSequencedClientDatagram, kSequencedServerDatagram);
        populate_reconnect_documents(fixture.run());
        const auto loaded = loader.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        REQUIRE(loaded);
        REQUIRE(loaded.state);
        CHECK(loaded.state->reconnect_transport_observation().has_value());
        CHECK(loaded.state->reconnect_orchestration_attestation().has_value());
        CHECK_FALSE(loaded.state->reconnect_observation().has_value());

        replace_text_once(
            fixture.run() / "capture-metadata.json",
            "\"scenario\": \"reconnect\"", "\"scenario\": \"baseline\"");
        const auto wrong_scenario = loader.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        CHECK_FALSE(wrong_scenario);
    }

    SECTION("staged pair is atomic and structurally bound")
    {
        TemporaryCorpus first;
        TemporaryCorpus second;
        populate_corpus_bytes(
            first.run(), kSequencedClientDatagram, kSequencedServerDatagram);
        populate_corpus_bytes(
            second.run(), kSequencedClientDatagram, kSequencedServerDatagram);
        populate_reconnect_documents(first.run());
        populate_reconnect_documents(second.run());
        replace_text_once(
            second.run() / "reconnect-transport-observation.staged.json",
            "\"retired_generation_a_server_tail_packet_count\":0",
            "\"retired_generation_a_server_tail_packet_count\":1");
        const auto left = loader.load(
            first.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        const auto right = loader.load(
            second.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        REQUIRE(left);
        REQUIRE(left.state);
        REQUIRE(right);
        REQUIRE(right.state);
        CHECK(left.state->structural_sha256() !=
              right.state->structural_sha256());

        REQUIRE(fs::remove(
            second.run() / "reconnect-orchestration.staged.json"));
        const auto partial = loader.load(
            second.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::prepublication);
        CHECK_FALSE(partial);
    }

    SECTION("accepted reconnect requires strict final observation and claims")
    {
        TemporaryCorpus fixture;
        populate_corpus_bytes(
            fixture.run(), kSequencedClientDatagram, kSequencedServerDatagram);
        populate_reconnect_documents(fixture.run());
        publish_reconnect_manifest(fixture.run());
        const auto published = loader.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published);
        REQUIRE(published);
        REQUIRE(published.state);
        REQUIRE(published.state->reconnect_observation());
        REQUIRE(published.state->accepted_manifest_claims());
        CHECK(published.state->accepted_manifest_claims()->candidate_recurrence ==
              2U);
        CHECK(published.state->accepted_manifest_claims()->candidate_stability ==
              "stable_observation");

        const auto manifest = fixture.run() / "research-run-metadata.json";
        replace_text_once(
            manifest, "\"connection_generation_count\":2",
            "\"connection_generation_count\":1");
        CHECK_FALSE(loader.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published));
        replace_text_once(
            manifest, "\"connection_generation_count\":1",
            "\"connection_generation_count\":2");

        const auto final = fixture.run() / "reconnect-observation.json";
        replace_text_once(
            final, "\"candidate_semantic_category_assigned\":false",
            "\"candidate_semantic_category_assigned\":true");
        CHECK_FALSE(loader.load(
            fixture.run(),
            goldsrc::StockRuntimeCaptureCorpusLoadPolicy::published));
    }
}

} // namespace
