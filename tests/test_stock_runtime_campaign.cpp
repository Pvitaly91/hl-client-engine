#include <hlclient/goldsrc/stock_runtime_campaign.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

inline constexpr std::string_view kImplementationCommit{
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};

[[nodiscard]] std::string run_id(std::size_t value)
{
    constexpr std::string_view hex{"0123456789abcdef"};
    std::string result(32U, '0');
    for (std::size_t index = 0U; index < 8U; ++index) {
        result[31U - index] = hex[value & 0xfU];
        value >>= 4U;
    }
    return result;
}

[[nodiscard]] goldsrc::StockRuntimeCampaignCandidateObservation candidate(
    const std::uint8_t value = 42U)
{
    goldsrc::StockRuntimeCampaignCandidateObservation result;
    result.bit_offset = 0U;
    result.bit_width = 8U;
    result.byte_aligned = true;
    result.numeric_candidate = value;
    return result;
}

[[nodiscard]] goldsrc::StockRuntimeCampaignRunObservation accepted_run(
    const std::size_t ordinal,
    const std::string_view map,
    const std::string_view scenario)
{
    goldsrc::StockRuntimeCampaignRunObservation result;
    result.run_id = run_id(ordinal + 1U);
    result.map_category = map;
    result.scenario = scenario;
    result.profile_identity = "profile-a";
    result.transport_structural_sha256 = std::string(64U, 'a');
    result.replay_structural_sha256 = std::string(64U, 'b');
    result.publication =
        goldsrc::StockRuntimeCampaignPublicationState::accepted;
    result.isolation_verified = true;
    result.profile_verified = true;
    result.client_ready = true;
    result.bounded_transport_complete = true;
    result.restoration_exact = true;
    result.external_drift_none = true;
    result.corpus_valid = true;
    result.independent_walker_valid = true;
    result.signon_replay_complete = true;
    result.candidate_body_unconsumed = true;
    result.sequenced_client_to_server = 40U;
    result.sequenced_server_to_client = 100U;
    result.reassembled_payloads = 2U;
    result.decompressed_payloads = 1U;
    const bool reconnect = scenario == "reconnect";
    result.connection_generation_count = reconnect ? 2U : 1U;
    result.exact_post_resource_boundary_count = reconnect ? 2U : 1U;
    result.reconnect_generations_distinct = reconnect;
    result.candidates.assign(reconnect ? 2U : 1U, candidate());
    return result;
}

[[nodiscard]] std::vector<goldsrc::StockRuntimeCampaignRunObservation>
full_campaign()
{
    std::vector<goldsrc::StockRuntimeCampaignRunObservation> result;
    std::size_t ordinal = 0U;
    for (const auto& entry : goldsrc::stock_runtime_first_campaign_matrix()) {
        for (std::size_t index = 0U; index < entry.required_runs; ++index) {
            result.push_back(accepted_run(
                ordinal++, entry.map_category, entry.scenario));
        }
    }
    return result;
}

[[nodiscard]] goldsrc::StockRuntimeCampaignAggregator aggregator(
    const std::size_t global_s2c = 1'000U)
{
    goldsrc::StockRuntimeCampaignLimits limits;
    limits.required_profile_identity = "profile-a";
    limits.minimum_sequenced_server_packets = global_s2c;
    return goldsrc::StockRuntimeCampaignAggregator{std::move(limits)};
}

TEST_CASE("Typed campaign failure publication has one exact incomplete value",
          "[goldsrc][stock-runtime][campaign][mutation]")
{
    CHECK(goldsrc::stock_runtime_campaign_failure_publication(
              "bounded-session-incomplete") ==
          goldsrc::StockRuntimeCampaignPublicationState::incomplete);
    CHECK(goldsrc::stock_runtime_campaign_failure_publication(
              "client-ready-not-observed") ==
          goldsrc::StockRuntimeCampaignPublicationState::rejected);
    CHECK(goldsrc::stock_runtime_campaign_failure_publication("timeout") ==
          goldsrc::StockRuntimeCampaignPublicationState::rejected);
}

TEST_CASE("Empty campaign is deterministic and evidence pending",
          "[goldsrc][stock-runtime][campaign][fake-corpus]")
{
    const std::vector<goldsrc::StockRuntimeCampaignRunObservation> empty;
    const auto first = aggregator().build(empty, kImplementationCommit);
    const auto second = aggregator().build(empty, kImplementationCommit);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first.state);
    REQUIRE(second.state);
    CHECK(first.state->attempted_runs() == 0U);
    CHECK(first.state->accepted_runs() == 0U);
    CHECK(first.state->pending_runs() == 24U);
    CHECK(first.state->candidate_stability() ==
          goldsrc::StockRuntimeCampaignCandidateStability::evidence_pending);
    CHECK(first.state->threshold_status() ==
          goldsrc::StockRuntimeCampaignThresholdStatus::pending);
    CHECK_FALSE(first.state->evidence_publication_allowed());
    CHECK(first.state->structural_sha256() ==
          second.state->structural_sha256());
}

TEST_CASE("Supported 22-run campaign cannot omit reconnect",
          "[goldsrc][stock-runtime][campaign][resume]")
{
    auto runs = full_campaign();
    runs.erase(runs.end() - 2, runs.end());
    const auto built = aggregator().build(runs, kImplementationCommit);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->accepted_runs() == 22U);
    CHECK(built.state->pending_runs() == 2U);
    CHECK(built.state->reconnect_generations() == 0U);
    CHECK(built.state->exact_post_resource_boundaries() == 22U);
    CHECK(built.state->candidate_observations() == 22U);
    CHECK_FALSE(built.state->evidence_publication_allowed());
}

TEST_CASE("Incomplete reconnect publications remain missing resume slots",
          "[goldsrc][stock-runtime][campaign][resume]")
{
    auto runs = full_campaign();
    runs[22U].publication =
        goldsrc::StockRuntimeCampaignPublicationState::incomplete;
    runs[23U].publication =
        goldsrc::StockRuntimeCampaignPublicationState::incomplete;
    const auto built = aggregator().build(runs, kImplementationCommit);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->accepted_runs() == 22U);
    CHECK(built.state->incomplete_runs() == 2U);
    CHECK(built.state->pending_runs() == 2U);
}

TEST_CASE("One reconnect contributes two generations and two boundaries",
          "[goldsrc][stock-runtime][campaign][reconnect]")
{
    auto runs = full_campaign();
    runs.pop_back();
    const auto built = aggregator().build(runs, kImplementationCommit);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->accepted_runs() == 23U);
    CHECK(built.state->pending_runs() == 1U);
    CHECK(built.state->reconnect_generations() == 2U);
    CHECK(built.state->exact_post_resource_boundaries() == 24U);
    CHECK(built.state->candidate_observations() == 24U);
    CHECK_FALSE(built.state->evidence_publication_allowed());
}

TEST_CASE("Exact full campaign passes 24/4/1000/26 evidence gate",
          "[goldsrc][stock-runtime][campaign][threshold]")
{
    const auto runs = full_campaign();
    const auto built = aggregator().build(runs, kImplementationCommit);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->accepted_runs() == 24U);
    CHECK(built.state->pending_runs() == 0U);
    CHECK(built.state->sequenced_server_to_client() == 2'400U);
    CHECK(built.state->reconnect_generations() == 4U);
    CHECK(built.state->exact_post_resource_boundaries() == 26U);
    CHECK(built.state->candidate_observations() == 26U);
    CHECK(built.state->candidate_stability() ==
          goldsrc::StockRuntimeCampaignCandidateStability::stable_observation);
    CHECK(built.state->threshold_status() ==
          goldsrc::StockRuntimeCampaignThresholdStatus::passed);
    CHECK(built.state->evidence_publication_allowed());

    const auto manifest =
        goldsrc::serialize_stock_runtime_first_campaign_manifest(*built.state);
    CHECK(manifest.find("hlclient.stock-runtime-first-campaign.v1") !=
          std::string::npos);
    CHECK(manifest.find("\"accepted_slots\": 24") != std::string::npos);
    CHECK(manifest.find("\"reconnect_generations\": 4") !=
          std::string::npos);
    CHECK(manifest.find("\"threshold_status\": \"passed\"") !=
          std::string::npos);
    CHECK(manifest.find("\"profile_fingerprint\": \"profile-a\"") !=
          std::string::npos);
    CHECK(manifest.find(std::string{kImplementationCommit}) !=
          std::string::npos);
    CHECK(manifest.find(std::string{built.state->structural_sha256()}) !=
          std::string::npos);
    CHECK(manifest.find("svc_") == std::string::npos);
}

TEST_CASE("Accepted overflow does not replace or inflate a configured slot",
          "[goldsrc][stock-runtime][campaign][duplicate-slot]")
{
    auto runs = full_campaign();
    runs.push_back(accepted_run(100U, "boot_camp", "baseline"));
    const auto built = aggregator().build(runs, kImplementationCommit);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->attempted_runs() == 25U);
    CHECK(built.state->accepted_runs() == 24U);
    CHECK(built.state->rejected_runs() == 1U);
    CHECK(built.state->accepted_runs_for("boot_camp", "baseline") == 6U);
    CHECK(built.state->evidence_publication_allowed());
}

TEST_CASE("Wrong profile and missing per-run gates never fill resume slots",
          "[goldsrc][stock-runtime][campaign][mutation]")
{
    auto runs = full_campaign();
    runs[0U].profile_identity = "profile-b";
    runs[1U].wrong_source_datagrams = 1U;
    const auto built = aggregator().build(runs, kImplementationCommit);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->accepted_runs() == 22U);
    CHECK(built.state->rejected_runs() == 2U);
    CHECK(built.state->pending_runs() == 2U);
    CHECK_FALSE(built.state->evidence_publication_allowed());
}

TEST_CASE("Explicit rejected and incomplete publications retain distinct counts",
          "[goldsrc][stock-runtime][campaign][mutation]")
{
    auto runs = full_campaign();
    runs[0U].publication =
        goldsrc::StockRuntimeCampaignPublicationState::incomplete;
    runs[1U].publication =
        goldsrc::StockRuntimeCampaignPublicationState::rejected;
    runs[0U].transport_structural_sha256.clear();
    runs[0U].replay_structural_sha256.clear();
    runs[1U].transport_structural_sha256.clear();
    runs[1U].replay_structural_sha256.clear();
    const auto built = aggregator().build(runs, kImplementationCommit);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->accepted_runs() == 22U);
    CHECK(built.state->rejected_runs() == 1U);
    CHECK(built.state->incomplete_runs() == 1U);
    CHECK(built.state->pending_runs() == 2U);
}

TEST_CASE("Accepted campaign digests must be lowercase nonzero SHA-256 values",
          "[goldsrc][stock-runtime][campaign][hash][mutation]")
{
    auto runs = full_campaign();
    runs[0U].transport_structural_sha256.clear();
    runs[1U].replay_structural_sha256 = std::string(64U, '0');
    runs[2U].transport_structural_sha256[0U] = 'A';

    const auto built = aggregator().build(runs, kImplementationCommit);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->accepted_runs() == 21U);
    CHECK(built.state->rejected_runs() == 3U);
    CHECK(built.state->pending_runs() == 3U);
    CHECK_FALSE(built.state->evidence_publication_allowed());
}

TEST_CASE("Conflicting neutral candidate blocks evidence without a semantic name",
          "[goldsrc][stock-runtime][campaign][candidate]")
{
    auto runs = full_campaign();
    runs[10U].candidates[0U] = candidate(43U);
    const auto built = aggregator().build(runs, kImplementationCommit);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->accepted_runs() == 24U);
    CHECK(built.state->candidate_stability() ==
          goldsrc::StockRuntimeCampaignCandidateStability::candidate_conflicting);
    CHECK(built.state->threshold_status() ==
          goldsrc::StockRuntimeCampaignThresholdStatus::conflicting);
    CHECK_FALSE(built.state->evidence_publication_allowed());
}

TEST_CASE("Global packet threshold remains an independent evidence gate",
          "[goldsrc][stock-runtime][campaign][threshold]")
{
    const auto runs = full_campaign();
    const auto built = aggregator(3'000U).build(runs, kImplementationCommit);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->accepted_runs() == 24U);
    CHECK(built.state->sequenced_server_to_client() == 2'400U);
    CHECK(built.state->threshold_status() ==
          goldsrc::StockRuntimeCampaignThresholdStatus::pending);
    CHECK_FALSE(built.state->evidence_publication_allowed());
}

TEST_CASE("Resume fills only the deterministic missing matrix slots",
          "[goldsrc][stock-runtime][campaign][resume]")
{
    auto runs = full_campaign();
    const auto saved_reconnect = runs.back();
    runs.pop_back();
    const auto partial = aggregator().build(runs, kImplementationCommit);
    REQUIRE(partial);
    REQUIRE(partial.state);
    REQUIRE(partial.state->pending_slots().size() == 1U);
    CHECK(partial.state->pending_slots()[0U].map_category == "boot_camp");
    CHECK(partial.state->pending_slots()[0U].scenario == "reconnect");
    CHECK(partial.state->pending_slots()[0U].slot_ordinal == 1U);

    runs.push_back(saved_reconnect);
    std::ranges::reverse(runs);
    const auto resumed = aggregator().build(runs, kImplementationCommit);
    REQUIRE(resumed);
    REQUIRE(resumed.state);
    CHECK(resumed.state->pending_slots().empty());
    CHECK(resumed.state->threshold_status() ==
          goldsrc::StockRuntimeCampaignThresholdStatus::passed);

    const auto canonical = aggregator().build(full_campaign(), kImplementationCommit);
    REQUIRE(canonical);
    REQUIRE(canonical.state);
    CHECK(resumed.state->structural_sha256() ==
          canonical.state->structural_sha256());
}

TEST_CASE("Generation and evidence mutations fail closed transactionally",
          "[goldsrc][stock-runtime][campaign][mutation]")
{
    auto runs = full_campaign();
    runs[22U].reconnect_generations_distinct = false;
    runs[23U].candidates.pop_back();
    const auto built = aggregator().build(runs, kImplementationCommit);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->accepted_runs() == 22U);
    CHECK(built.state->rejected_runs() == 2U);
    CHECK(built.state->reconnect_generations() == 0U);
    CHECK(built.state->exact_post_resource_boundaries() == 22U);
    CHECK_FALSE(built.state->evidence_publication_allowed());
}

TEST_CASE("Independent walker validation is an explicit campaign gate",
          "[goldsrc][stock-runtime][campaign][walker][mutation]")
{
    auto runs = full_campaign();
    runs[0U].independent_walker_valid = false;
    const auto built = aggregator().build(runs, kImplementationCommit);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->accepted_runs() == 23U);
    CHECK(built.state->rejected_runs() == 1U);
    CHECK(built.state->pending_runs() == 1U);
    CHECK_FALSE(built.state->evidence_publication_allowed());
}

TEST_CASE("Unaligned candidate permits an eight-bit cross-byte prefix",
          "[goldsrc][stock-runtime][campaign][candidate][mutation]")
{
    auto runs = full_campaign();
    for (auto& run : runs) {
        for (auto& observation : run.candidates) {
            observation.bit_offset = 7U;
            observation.bit_width = 8U;
            observation.byte_aligned = false;
            observation.numeric_candidate.reset();
            observation.bounded_bit_prefix = 0xa5U;
        }
    }

    const auto built = aggregator().build(runs, kImplementationCommit);
    REQUIRE(built);
    REQUIRE(built.state);
    CHECK(built.state->accepted_runs() == 24U);
    CHECK(built.state->rejected_runs() == 0U);
    CHECK(built.state->evidence_publication_allowed());

    runs[0U].candidates[0U].bit_width = 9U;
    const auto invalid = aggregator().build(runs, kImplementationCommit);
    REQUIRE(invalid);
    REQUIRE(invalid.state);
    CHECK(invalid.state->accepted_runs() == 23U);
    CHECK(invalid.state->rejected_runs() == 1U);
    CHECK_FALSE(invalid.state->evidence_publication_allowed());
}

TEST_CASE("Malformed candidate optionals are rejected without undefined access",
          "[goldsrc][stock-runtime][campaign][candidate][mutation]")
{
    auto missing = full_campaign();
    missing[0U].candidates[0U].numeric_candidate.reset();
    missing[0U].candidates[0U].bounded_bit_prefix.reset();

    const auto missing_built =
        aggregator().build(missing, kImplementationCommit);
    REQUIRE(missing_built);
    REQUIRE(missing_built.state);
    CHECK(missing_built.state->accepted_runs() == 23U);
    CHECK(missing_built.state->rejected_runs() == 1U);
    CHECK_FALSE(missing_built.state->evidence_publication_allowed());

    auto ambiguous = full_campaign();
    ambiguous[0U].candidates[0U].bounded_bit_prefix = 42U;

    const auto ambiguous_built =
        aggregator().build(ambiguous, kImplementationCommit);
    REQUIRE(ambiguous_built);
    REQUIRE(ambiguous_built.state);
    CHECK(ambiguous_built.state->accepted_runs() == 23U);
    CHECK(ambiguous_built.state->rejected_runs() == 1U);
    CHECK_FALSE(ambiguous_built.state->evidence_publication_allowed());
}

TEST_CASE("Campaign hash binds every sanitized acceptance input and threshold",
          "[goldsrc][stock-runtime][campaign][hash][mutation]")
{
    const auto baseline_runs = full_campaign();
    const auto baseline = aggregator().build(
        baseline_runs, kImplementationCommit);
    REQUIRE(baseline);
    REQUIRE(baseline.state);
    const auto hash = std::string{baseline.state->structural_sha256()};

    const auto changed_hash = [&hash](
        std::vector<goldsrc::StockRuntimeCampaignRunObservation> runs) {
        const auto built = aggregator().build(runs, kImplementationCommit);
        REQUIRE(built);
        REQUIRE(built.state);
        CHECK(built.state->structural_sha256() != hash);
    };

    auto profile = baseline_runs;
    profile[0U].profile_identity = "profile-b";
    changed_hash(std::move(profile));

    auto transport_digest = baseline_runs;
    transport_digest[0U].transport_structural_sha256[0U] = 'c';
    changed_hash(std::move(transport_digest));

    auto replay_digest = baseline_runs;
    replay_digest[0U].replay_structural_sha256[0U] = 'd';
    changed_hash(std::move(replay_digest));

    auto restoration = baseline_runs;
    restoration[0U].restoration_exact = false;
    changed_hash(std::move(restoration));

    auto packets = baseline_runs;
    ++packets[0U].sequenced_client_to_server;
    ++packets[0U].reassembled_payloads;
    changed_hash(std::move(packets));

    auto generations = baseline_runs;
    generations[22U].connection_generation_count = 1U;
    generations[22U].exact_post_resource_boundary_count = 1U;
    changed_hash(std::move(generations));

    auto publication = baseline_runs;
    publication[0U].publication =
        goldsrc::StockRuntimeCampaignPublicationState::incomplete;
    changed_hash(std::move(publication));

    auto candidate_mutation = baseline_runs;
    candidate_mutation[4U].candidates[0U] = candidate(43U);
    changed_hash(std::move(candidate_mutation));

    const auto changed_threshold = aggregator(3'000U).build(
        baseline_runs, kImplementationCommit);
    REQUIRE(changed_threshold);
    REQUIRE(changed_threshold.state);
    CHECK(changed_threshold.state->structural_sha256() != hash);

    constexpr std::string_view other_commit{
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
    const auto changed_commit = aggregator().build(
        baseline_runs, other_commit);
    REQUIRE(changed_commit);
    REQUIRE(changed_commit.state);
    CHECK(changed_commit.state->structural_sha256() != hash);
}

} // namespace
