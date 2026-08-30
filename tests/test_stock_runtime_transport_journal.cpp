#include <hlclient/goldsrc/stock_runtime_transport_journal.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] std::string raw_name(
    const std::size_t ordinal,
    const goldsrc::StockRuntimeCaptureDirection direction)
{
    std::string digits = std::to_string(ordinal);
    digits.insert(digits.begin(), 8U - digits.size(), '0');
    return digits +
           (direction == goldsrc::StockRuntimeCaptureDirection::client_to_server
                ? "-c2s.bin"
                : "-s2c.bin");
}

[[nodiscard]] goldsrc::StockRuntimeTransportJournalEntry entry(
    const std::size_t observed,
    const goldsrc::StockRuntimeCaptureDirection direction,
    const std::size_t direction_ordinal,
    const goldsrc::StockRuntimeCaptureAction action,
    const goldsrc::StockRuntimeTransportHoldState hold,
    std::vector<std::size_t> emissions)
{
    const bool c2s =
        direction == goldsrc::StockRuntimeCaptureDirection::client_to_server;
    return goldsrc::StockRuntimeTransportJournalEntry{
        observed,
        direction,
        direction_ordinal,
        static_cast<std::uint64_t>(observed * 10U),
        3U,
        raw_name(observed, direction),
        c2s ? goldsrc::StockRuntimeTransportRole::research_client
            : goldsrc::StockRuntimeTransportRole::research_server,
        c2s ? goldsrc::StockRuntimeTransportRole::research_server
            : goldsrc::StockRuntimeTransportRole::research_client,
        action,
        hold,
        std::move(emissions),
        false,
        false,
        std::string(64U, 'a'),
    };
}

TEST_CASE("Stock transport journal round-trips its exact v1 JSONL contract",
          "[goldsrc][stock-runtime][journal][jsonl]")
{
    auto original = entry(
        0U, goldsrc::StockRuntimeCaptureDirection::client_to_server, 1U,
        goldsrc::StockRuntimeCaptureAction::duplicate,
        goldsrc::StockRuntimeTransportHoldState::none, {0U, 1U});
    original.delivered = true;
    std::ranges::transform(original.sha256, original.sha256.begin(),
                           [](const char value) {
                               return static_cast<char>(value - 'a' + 'A');
                           });

    const auto json = goldsrc::serialize_stock_runtime_transport_journal_entry(
        original);
    CHECK(json.find("hlclient.stock-runtime-transport-journal.v1") !=
          std::string::npos);
    CHECK(json.find("\"direction\":\"client_to_server\"") !=
          std::string::npos);
    CHECK(json.find("\"raw_filename\":\"00000000-c2s.bin\"") !=
          std::string::npos);
    CHECK(json.find("\"emitted_ordinals\":[0,1]") != std::string::npos);

    const auto parsed =
        goldsrc::parse_stock_runtime_transport_journal_entry(json);
    REQUIRE(parsed);
    REQUIRE(parsed.entry);
    CHECK(parsed.entry->sha256 == std::string(64U, 'a'));
    CHECK(parsed.entry->direction_ordinal == 1U);
    CHECK(parsed.entry->emitted_ordinals == std::vector<std::size_t>{0U, 1U});

    const auto validated = goldsrc::validate_stock_runtime_transport_journal(
        std::span<const goldsrc::StockRuntimeTransportJournalEntry>{
            parsed.entry.operator->(), 1U});
    REQUIRE(validated);
    CHECK(validated.emitted_datagram_count == 2U);
    CHECK(validated.transport_complete);
}

TEST_CASE("Journal validates peer-visible delay and reorder order",
          "[goldsrc][stock-runtime][journal][perturbation]")
{
    SECTION("delay emits held datagram before same-direction successor")
    {
        auto held = entry(
            0U, goldsrc::StockRuntimeCaptureDirection::client_to_server, 1U,
            goldsrc::StockRuntimeCaptureAction::hold_for_delay,
            goldsrc::StockRuntimeTransportHoldState::released, {0U});
        auto successor = entry(
            1U, goldsrc::StockRuntimeCaptureDirection::client_to_server, 2U,
            goldsrc::StockRuntimeCaptureAction::forward,
            goldsrc::StockRuntimeTransportHoldState::none, {1U});
        held.delivered = successor.delivered = true;
        const std::array entries{held, successor};
        CHECK(goldsrc::validate_stock_runtime_transport_journal(entries));
    }

    SECTION("reorder emits successor before held datagram")
    {
        auto held = entry(
            0U, goldsrc::StockRuntimeCaptureDirection::server_to_client, 1U,
            goldsrc::StockRuntimeCaptureAction::hold_for_reorder,
            goldsrc::StockRuntimeTransportHoldState::released, {1U});
        auto successor = entry(
            1U, goldsrc::StockRuntimeCaptureDirection::server_to_client, 2U,
            goldsrc::StockRuntimeCaptureAction::forward,
            goldsrc::StockRuntimeTransportHoldState::none, {0U});
        held.delivered = successor.delivered = true;
        const std::array entries{held, successor};
        CHECK(goldsrc::validate_stock_runtime_transport_journal(entries));

        held.emitted_ordinals = {0U};
        successor.emitted_ordinals = {1U};
        const std::array wrong_order{held, successor};
        const auto rejected =
            goldsrc::validate_stock_runtime_transport_journal(wrong_order);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeTransportJournalErrorCode::
                  emission_reference_mismatch);
    }

    SECTION("deadline-flushed delay is complete without a successor")
    {
        auto delayed = entry(
            0U, goldsrc::StockRuntimeCaptureDirection::client_to_server, 1U,
            goldsrc::StockRuntimeCaptureAction::hold_for_delay,
            goldsrc::StockRuntimeTransportHoldState::released, {0U});
        delayed.delivered = true;
        const std::array entries{delayed};
        const auto validated =
            goldsrc::validate_stock_runtime_transport_journal(entries);
        REQUIRE(validated);
        CHECK(validated.transport_complete);
    }
}

TEST_CASE("Incomplete journal policy preserves but never accepts unresolved evidence",
          "[goldsrc][stock-runtime][journal][incomplete]")
{
    auto unresolved = entry(
        0U, goldsrc::StockRuntimeCaptureDirection::server_to_client, 1U,
        goldsrc::StockRuntimeCaptureAction::hold_for_reorder,
        goldsrc::StockRuntimeTransportHoldState::unresolved, {0U});
    unresolved.delivered = true;
    const std::array entries{unresolved};

    const auto retained = goldsrc::validate_stock_runtime_transport_journal(
        entries, {},
        goldsrc::StockRuntimeTransportJournalValidationPolicy::
            incomplete_capture);
    REQUIRE(retained);
    CHECK_FALSE(retained.transport_complete);

    const auto rejected =
        goldsrc::validate_stock_runtime_transport_journal(entries);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
          goldsrc::StockRuntimeTransportJournalErrorCode::unresolved_hold);
}

TEST_CASE("Journal rejects reordered duplicate references and unexpected sources",
          "[goldsrc][stock-runtime][journal][mutation]")
{
    SECTION("duplicate emission references must be ascending and consecutive")
    {
        auto duplicate = entry(
            0U, goldsrc::StockRuntimeCaptureDirection::client_to_server, 1U,
            goldsrc::StockRuntimeCaptureAction::duplicate,
            goldsrc::StockRuntimeTransportHoldState::none, {1U, 0U});
        duplicate.delivered = true;
        const std::array entries{duplicate};
        const auto rejected =
            goldsrc::validate_stock_runtime_transport_journal(entries);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeTransportJournalErrorCode::
                  invalid_emitted_ordinals);
    }

    SECTION("unexpected source has a non-fabricated role and is incomplete")
    {
        auto unexpected = entry(
            0U, goldsrc::StockRuntimeCaptureDirection::client_to_server, 1U,
            goldsrc::StockRuntimeCaptureAction::drop,
            goldsrc::StockRuntimeTransportHoldState::none, {});
        unexpected.source_role =
            goldsrc::StockRuntimeTransportRole::unexpected_source;
        unexpected.wrong_source = true;
        const std::array entries{unexpected};

        const auto retained = goldsrc::validate_stock_runtime_transport_journal(
            entries, {},
            goldsrc::StockRuntimeTransportJournalValidationPolicy::
                incomplete_capture);
        REQUIRE(retained);
        CHECK_FALSE(retained.transport_complete);

        const auto rejected =
            goldsrc::validate_stock_runtime_transport_journal(entries);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeTransportJournalErrorCode::
                  invalid_wrong_source_state);
    }

    SECTION("unknown JSON fields fail closed")
    {
        auto value = entry(
            0U, goldsrc::StockRuntimeCaptureDirection::server_to_client, 1U,
            goldsrc::StockRuntimeCaptureAction::drop,
            goldsrc::StockRuntimeTransportHoldState::none, {});
        auto json =
            goldsrc::serialize_stock_runtime_transport_journal_entry(value);
        json.insert(json.size() - 1U, ",\"auth_dump\":\"forbidden\"");
        const auto parsed =
            goldsrc::parse_stock_runtime_transport_journal_entry(json);
        REQUIRE_FALSE(parsed);
        REQUIRE(parsed.error);
        CHECK(parsed.error->code ==
              goldsrc::StockRuntimeTransportJournalErrorCode::unknown_property);
    }
}

TEST_CASE("Baseline journal accounts exact forwards drops bytes and hashes",
          "[goldsrc][stock-runtime][journal][baseline][accounting]")
{
    auto client_forward = entry(
        0U, goldsrc::StockRuntimeCaptureDirection::client_to_server, 1U,
        goldsrc::StockRuntimeCaptureAction::forward,
        goldsrc::StockRuntimeTransportHoldState::none, {0U});
    auto server_drop = entry(
        1U, goldsrc::StockRuntimeCaptureDirection::server_to_client, 1U,
        goldsrc::StockRuntimeCaptureAction::drop,
        goldsrc::StockRuntimeTransportHoldState::none, {});
    auto server_forward = entry(
        2U, goldsrc::StockRuntimeCaptureDirection::server_to_client, 2U,
        goldsrc::StockRuntimeCaptureAction::forward,
        goldsrc::StockRuntimeTransportHoldState::none, {1U});
    client_forward.delivered = true;
    server_forward.delivered = true;

    const std::array baseline{client_forward, server_drop, server_forward};
    const auto validated =
        goldsrc::validate_stock_runtime_transport_journal(baseline);
    REQUIRE(validated);
    CHECK(validated.transport_complete);
    CHECK(validated.emitted_datagram_count == 2U);
    CHECK(validated.observed_raw_bytes == 9U);
    CHECK(validated.client_to_server_count == 1U);
    CHECK(validated.server_to_client_count == 2U);

    auto invalid_hash = baseline;
    invalid_hash[2U].sha256.back() = 'g';
    const auto rejected_hash =
        goldsrc::validate_stock_runtime_transport_journal(invalid_hash);
    REQUIRE_FALSE(rejected_hash);
    REQUIRE(rejected_hash.error);
    CHECK(rejected_hash.error->code ==
          goldsrc::StockRuntimeTransportJournalErrorCode::invalid_sha256);
    CHECK(rejected_hash.error->entry_ordinal == 2U);

    goldsrc::StockRuntimeTransportJournalLimits two_entry_limit;
    two_entry_limit.maximum_entries = 2U;
    const auto rejected_count = goldsrc::validate_stock_runtime_transport_journal(
        baseline, two_entry_limit);
    REQUIRE_FALSE(rejected_count);
    REQUIRE(rejected_count.error);
    CHECK(rejected_count.error->code ==
          goldsrc::StockRuntimeTransportJournalErrorCode::count_mismatch);
}

TEST_CASE("Journal parser rejects every truncation and exact schema bounds",
          "[goldsrc][stock-runtime][journal][jsonl][truncation][bounds]")
{
    auto value = entry(
        0U, goldsrc::StockRuntimeCaptureDirection::client_to_server, 1U,
        goldsrc::StockRuntimeCaptureAction::forward,
        goldsrc::StockRuntimeTransportHoldState::none, {0U});
    value.delivered = true;
    const auto json =
        goldsrc::serialize_stock_runtime_transport_journal_entry(value);
    REQUIRE(json.size() < goldsrc::kMaximumStockRuntimeJournalLineBytes);

    for (std::size_t size = 0U; size < json.size(); ++size) {
        CAPTURE(size);
        const auto truncated =
            goldsrc::parse_stock_runtime_transport_journal_entry(
                std::string_view{json}.substr(0U, size));
        CHECK_FALSE(truncated);
        CHECK_FALSE(truncated.entry);
        REQUIRE(truncated.error);
        CHECK(truncated.error->byte_offset <= size);
    }

    auto wrong_schema = json;
    const auto schema_offset = wrong_schema.find(
        goldsrc::kStockRuntimeTransportJournalSchema);
    REQUIRE(schema_offset != std::string::npos);
    wrong_schema[schema_offset] = 'x';
    const auto schema_rejected =
        goldsrc::parse_stock_runtime_transport_journal_entry(wrong_schema);
    REQUIRE_FALSE(schema_rejected);
    REQUIRE(schema_rejected.error);
    CHECK(schema_rejected.error->code ==
          goldsrc::StockRuntimeTransportJournalErrorCode::wrong_schema);

    const std::string oversized(
        goldsrc::kMaximumStockRuntimeJournalLineBytes + 1U, 'x');
    const auto size_rejected =
        goldsrc::parse_stock_runtime_transport_journal_entry(oversized);
    REQUIRE_FALSE(size_rejected);
    REQUIRE(size_rejected.error);
    CHECK(size_rejected.error->code ==
          goldsrc::StockRuntimeTransportJournalErrorCode::line_too_large);

    goldsrc::StockRuntimeTransportJournalLimits invalid_limits;
    invalid_limits.maximum_entries =
        goldsrc::StockRuntimeCaptureHardCaps::maximum_datagrams + 1U;
    const std::array entries{value};
    const auto configuration_rejected =
        goldsrc::validate_stock_runtime_transport_journal(
            entries, invalid_limits);
    REQUIRE_FALSE(configuration_rejected);
    REQUIRE(configuration_rejected.error);
    CHECK(configuration_rejected.error->code ==
          goldsrc::StockRuntimeTransportJournalErrorCode::
              invalid_configuration);
}

} // namespace
