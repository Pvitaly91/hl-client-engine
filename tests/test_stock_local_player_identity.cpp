#include <hlclient/goldsrc/stock_local_player_identity.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::StockRuntimeSourceCursor cursor(
    const std::size_t byte_offset)
{
    const auto created =
        goldsrc::StockRuntimeSourceCursor::create(byte_offset, 0U, 32U);
    REQUIRE(created);
    return *created;
}

TEST_CASE("Stock local-player candidates never self-promote to confirmed identity",
    "[goldsrc][stock-runtime][identity]")
{
    goldsrc::StockLocalPlayerIdentityBuilder builder{7U};
    REQUIRE(builder.valid_configuration());
    REQUIRE(builder.observe_view_entity_candidate(3U, cursor(1U)));

    const auto published = builder.publish();
    REQUIRE(published);
    REQUIRE(published.state);
    CHECK(published.state->status() ==
        goldsrc::StockLocalPlayerIdentityStatus::single_client_candidate);
    CHECK(published.state->candidate_entity_number() == 3U);
    CHECK_FALSE(published.state->confirmed_entity_number());
}

TEST_CASE("Stock local-player identity retains differential evidence and conflicts",
    "[goldsrc][stock-runtime][identity]")
{
    goldsrc::StockLocalPlayerIdentityBuilder correlated{11U};
    REQUIRE(correlated.observe_player_entity_candidate(4U, cursor(2U)));
    REQUIRE(correlated.observe_two_client_correlation(1U, 4U, cursor(3U)));
    const auto correlated_state = correlated.publish();
    REQUIRE(correlated_state);
    CHECK(correlated_state.state->status() ==
        goldsrc::StockLocalPlayerIdentityStatus::multi_client_correlated);
    CHECK_FALSE(correlated_state.state->confirmed_entity_number());

    goldsrc::StockLocalPlayerIdentityBuilder conflicting{11U};
    REQUIRE(conflicting.observe_view_entity_candidate(4U, cursor(4U)));
    REQUIRE(conflicting.observe_player_entity_candidate(5U, cursor(5U)));
    const auto conflict_state = conflicting.publish();
    REQUIRE(conflict_state);
    CHECK(conflict_state.state->status() ==
        goldsrc::StockLocalPlayerIdentityStatus::conflicting);
    CHECK_FALSE(conflict_state.state->candidate_entity_number());
}

TEST_CASE("Stock local-player identity publication is bounded and transactional",
    "[goldsrc][stock-runtime][identity]")
{
    goldsrc::StockLocalPlayerIdentityLimits limits;
    limits.maximum_evidence_records = 1U;
    goldsrc::StockLocalPlayerIdentityBuilder builder{13U, limits};
    REQUIRE(builder.observe_view_entity_candidate(2U, cursor(6U)));
    const auto before = builder.publish();
    REQUIRE(before);
    const auto rejected =
        builder.observe_player_entity_candidate(2U, cursor(7U));
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        goldsrc::StockLocalPlayerIdentityErrorCode::evidence_limit_exceeded);
    const auto after = builder.publish();
    REQUIRE(after);
    CHECK(after.state->evidence_record_count() ==
        before.state->evidence_record_count());
}

} // namespace
