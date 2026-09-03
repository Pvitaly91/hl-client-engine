#include <hlclient/goldsrc/stock_runtime_capture.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace {

namespace goldsrc = hlclient::goldsrc;

TEST_CASE("Stock runtime capture limits are bounded and cross-validated",
          "[goldsrc][stock-runtime][capture][limits]")
{
    goldsrc::StockRuntimeCaptureLimits limits;
    CHECK(goldsrc::validate_stock_runtime_capture_limits(limits));

    limits.maximum_payload_bytes =
        goldsrc::StockRuntimeCaptureHardCaps::maximum_payload_bytes + 1U;
    const auto hard_cap = goldsrc::validate_stock_runtime_capture_limits(limits);
    REQUIRE_FALSE(hard_cap);
    CHECK(hard_cap.code ==
          goldsrc::StockRuntimeCaptureLimitErrorCode::hard_cap_exceeded);

    limits = {};
    limits.maximum_decompressed_bytes = limits.maximum_reassembled_bytes - 1U;
    CHECK_FALSE(goldsrc::validate_stock_runtime_capture_limits(limits));

    limits = {};
    limits.maximum_runtime_frames = limits.maximum_message_count + 1U;
    CHECK_FALSE(goldsrc::validate_stock_runtime_capture_limits(limits));
}

TEST_CASE("Stock runtime output roles bind only their exact ignored roots",
          "[goldsrc][stock-runtime][capture][output-role]")
{
    const auto normal =
        goldsrc::parse_stock_runtime_capture_output_role("normal-campaign-run");
    const auto canary =
        goldsrc::parse_stock_runtime_capture_output_role("pre-campaign-canary");
    REQUIRE(normal);
    REQUIRE(canary);
    CHECK(*normal ==
          goldsrc::StockRuntimeCaptureOutputRole::normal_campaign_run);
    CHECK(*canary ==
          goldsrc::StockRuntimeCaptureOutputRole::pre_campaign_canary);
    CHECK(goldsrc::to_string(*normal) == "normal-campaign-run");
    CHECK(goldsrc::to_string(*canary) == "pre-campaign-canary");
    CHECK(goldsrc::stock_runtime_capture_output_parent_directory(*normal) ==
          "stock-runtime");
    CHECK(goldsrc::stock_runtime_capture_output_parent_directory(*canary) ==
          "stock-runtime-canary");
    CHECK_FALSE(
        goldsrc::parse_stock_runtime_capture_output_role("stock-runtime"));
    CHECK_FALSE(goldsrc::parse_stock_runtime_capture_output_role(
        "manual-artifacts/stock-runtime-canary"));
    CHECK_FALSE(goldsrc::parse_stock_runtime_capture_output_role("canary"));
}

TEST_CASE("Stock runtime capture counter failures are transactional",
          "[goldsrc][stock-runtime][capture][transactional]")
{
    goldsrc::StockRuntimeCaptureLimits limits;
    limits.maximum_datagrams = 1U;
    limits.maximum_total_raw_bytes = 4U;
    limits.maximum_payload_bytes = 4U;
    limits.maximum_client_packets = 1U;
    limits.maximum_server_packets = 1U;
    REQUIRE(goldsrc::validate_stock_runtime_capture_limits(limits));

    goldsrc::StockRuntimeCaptureCounters counters;
    REQUIRE(goldsrc::stock_runtime_capture_observe_datagram(
        counters, limits,
        goldsrc::StockRuntimeCaptureDirection::client_to_server, 4U));
    CHECK(counters.observed_datagrams == 1U);
    CHECK(counters.observed_raw_bytes == 4U);
    CHECK(counters.client_packets == 1U);

    const auto rejected = goldsrc::stock_runtime_capture_observe_datagram(
        counters, limits,
        goldsrc::StockRuntimeCaptureDirection::server_to_client, 1U);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.code ==
          goldsrc::StockRuntimeCaptureBudgetErrorCode::datagram_limit);
    CHECK(counters.observed_datagrams == 1U);
    CHECK(counters.observed_raw_bytes == 4U);
    CHECK(counters.client_packets == 1U);
    CHECK(counters.server_packets == 0U);

    counters.emitted_datagrams = (std::numeric_limits<std::size_t>::max)();
    counters.emitted_bytes = 7U;
    REQUIRE_FALSE(goldsrc::stock_runtime_capture_record_emission(counters, 1U));
    CHECK(counters.emitted_datagrams ==
          (std::numeric_limits<std::size_t>::max)());
    CHECK(counters.emitted_bytes == 7U);

    counters.emitted_datagrams = 3U;
    counters.emitted_bytes = (std::numeric_limits<std::uint64_t>::max)();
    REQUIRE_FALSE(goldsrc::stock_runtime_capture_record_emission(counters, 1U));
    CHECK(counters.emitted_datagrams == 3U);
    CHECK(counters.emitted_bytes ==
          (std::numeric_limits<std::uint64_t>::max)());
}

TEST_CASE("Stock runtime perturbations never request payload edits",
          "[goldsrc][stock-runtime][capture][perturbation]")
{
    const auto perturbation = goldsrc::StockRuntimeCapturePerturbation{20U, 20U};
    CHECK(goldsrc::stock_runtime_capture_action(
              goldsrc::StockRuntimeCaptureScenario::delay_client_move,
              goldsrc::StockRuntimeCaptureDirection::client_to_server,
              20U, perturbation) ==
          goldsrc::StockRuntimeCaptureAction::hold_for_delay);
    CHECK(goldsrc::stock_runtime_capture_action(
              goldsrc::StockRuntimeCaptureScenario::reorder_server_runtime,
              goldsrc::StockRuntimeCaptureDirection::server_to_client,
              20U, perturbation) ==
          goldsrc::StockRuntimeCaptureAction::hold_for_reorder);
    CHECK(goldsrc::stock_runtime_capture_action(
              goldsrc::StockRuntimeCaptureScenario::drop_two_server_runtime,
              goldsrc::StockRuntimeCaptureDirection::server_to_client,
              21U, perturbation) ==
          goldsrc::StockRuntimeCaptureAction::drop);
    CHECK(goldsrc::stock_runtime_capture_action(
              goldsrc::StockRuntimeCaptureScenario::baseline,
              goldsrc::StockRuntimeCaptureDirection::server_to_client,
              20U, perturbation) ==
          goldsrc::StockRuntimeCaptureAction::forward);
}

TEST_CASE("Stock runtime capture metadata cannot promote pending evidence",
          "[goldsrc][stock-runtime][capture][metadata]")
{
    goldsrc::StockRuntimeCaptureMetadata metadata;
    metadata.counters.observed_datagrams = 2U;
    metadata.counters.observed_raw_bytes = 2U;
    metadata.counters.client_packets = 1U;
    metadata.counters.server_packets = 1U;
    metadata.counters.emitted_datagrams = 2U;
    metadata.counters.emitted_bytes = 2U;
    metadata.bounded_transport_complete = true;

    const auto json = goldsrc::serialize_stock_runtime_capture_metadata(metadata);
    const auto parsed = goldsrc::parse_stock_runtime_capture_metadata(json);
    REQUIRE(parsed);
    REQUIRE(parsed.metadata);
    CHECK(parsed.metadata->bounded_transport_complete);
    CHECK_FALSE(parsed.metadata->accepted_evidence_run);
    CHECK(goldsrc::canonical_stock_runtime_capture_structure(*parsed.metadata).find(
              "runtime=evidence_pending|authority=evidence_pending|ack=evidence_pending") !=
          std::string::npos);

    auto promoted = json;
    const auto gate = promoted.find("\"accepted_evidence_run\": false");
    REQUIRE(gate != std::string::npos);
    promoted.replace(gate, std::string{"\"accepted_evidence_run\": false"}.size(),
                     "\"accepted_evidence_run\": true");
    CHECK_FALSE(goldsrc::parse_stock_runtime_capture_metadata(promoted));

    metadata = {};
    metadata.bounded_transport_complete = true;
    CHECK_FALSE(goldsrc::parse_stock_runtime_capture_metadata(
        goldsrc::serialize_stock_runtime_capture_metadata(metadata)));
}

} // namespace
