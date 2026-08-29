#include <hlclient/goldsrc/stock_command_ack_evidence.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::StockRuntimeSourceCursor cursor(
    const std::size_t byte_offset)
{
    const auto created =
        goldsrc::StockRuntimeSourceCursor::create(byte_offset, 0U, 64U);
    REQUIRE(created);
    return *created;
}

TEST_CASE("Netchan acknowledgement evidence is not a usercmd acknowledgement",
    "[goldsrc][stock-runtime][ack]")
{
    using Domain = goldsrc::StockCommandAcknowledgementCandidateDomain;
    goldsrc::StockCommandAcknowledgementEvidenceBuilder builder{5U};
    for (std::size_t sample = 0U; sample < 8U; ++sample) {
        REQUIRE(builder.observe_candidate(
            Domain::client_to_server_netchan_sequence, 20U + sample,
            sample, cursor(1U)));
        REQUIRE(builder.observe_candidate(
            Domain::server_to_client_netchan_acknowledgement,
            20U + sample, sample, cursor(2U)));
    }
    REQUIRE(builder.observe_correlation({
        Domain::server_to_client_netchan_acknowledgement,
        Domain::client_to_server_netchan_sequence,
        8U,
        8U,
        0U,
        1U,
        1U,
        1U,
    }));
    const auto state = builder.publish();
    REQUIRE(state);
    CHECK(state.state->status() ==
        goldsrc::StockCommandAcknowledgementEvidenceStatus::
            correlates_with_netchan_sequence);
    CHECK_FALSE(state.state->exact_usercmd_sequence_available());
}

TEST_CASE("Exact-domain stock ACK candidate remains evidence-pending",
    "[goldsrc][stock-runtime][ack]")
{
    using Domain = goldsrc::StockCommandAcknowledgementCandidateDomain;
    goldsrc::StockCommandAcknowledgementEvidenceBuilder builder{6U};
    REQUIRE(builder.observe_candidate(
        Domain::exact_usercmd_sequence, 91U, 0U, cursor(3U)));
    const auto state = builder.publish();
    REQUIRE(state);
    CHECK(state.state->status() ==
        goldsrc::StockCommandAcknowledgementEvidenceStatus::
            exact_usercmd_sequence_pending);
    CHECK_FALSE(state.state->exact_usercmd_sequence_available());
}

TEST_CASE("Zero matching progress cannot publish ACK correlation",
    "[goldsrc][stock-runtime][ack]")
{
    using Domain = goldsrc::StockCommandAcknowledgementCandidateDomain;
    goldsrc::StockCommandAcknowledgementEvidenceBuilder builder{7U};
    REQUIRE(builder.observe_candidate(
        Domain::explicit_clientdata_field, 12U, 0U, cursor(6U)));
    REQUIRE(builder.observe_candidate(
        Domain::client_move_packet_ordinal, 15U, 0U, cursor(7U)));
    REQUIRE(builder.observe_correlation({Domain::explicit_clientdata_field,
        Domain::client_move_packet_ordinal, 12U, 0U, 0U, 2U, 2U, 1U}));

    const auto state = builder.publish();
    REQUIRE(state);
    CHECK(state.state->status() ==
        goldsrc::StockCommandAcknowledgementEvidenceStatus::
            candidate_value_observed);
    CHECK_FALSE(state.state->exact_usercmd_sequence_available());
}

TEST_CASE("Contradictory ACK correlation publishes typed conflict",
    "[goldsrc][stock-runtime][ack]")
{
    using Domain = goldsrc::StockCommandAcknowledgementCandidateDomain;
    goldsrc::StockCommandAcknowledgementEvidenceBuilder builder{8U};
    REQUIRE(builder.observe_candidate(
        Domain::explicit_clientdata_field, 7U, 0U, cursor(4U)));
    REQUIRE(builder.observe_candidate(
        Domain::client_move_packet_ordinal, 7U, 0U, cursor(5U)));
    REQUIRE(builder.observe_correlation({Domain::explicit_clientdata_field,
        Domain::client_move_packet_ordinal, 10U, 8U, 2U, 1U, 1U, 1U}));
    const auto state = builder.publish();
    REQUIRE(state);
    CHECK(state.state->status() ==
        goldsrc::StockCommandAcknowledgementEvidenceStatus::conflicting);
    CHECK_FALSE(state.state->exact_usercmd_sequence_available());
}

TEST_CASE("ACK candidates at equal offsets in different records do not conflict",
    "[goldsrc][stock-runtime][ack]")
{
    using Domain = goldsrc::StockCommandAcknowledgementCandidateDomain;
    goldsrc::StockCommandAcknowledgementEvidenceBuilder builder{9U};
    REQUIRE(builder.observe_candidate(
        Domain::explicit_clientdata_field, 7U, 10U, cursor(4U)));
    REQUIRE(builder.observe_candidate(
        Domain::explicit_clientdata_field, 8U, 11U, cursor(4U)));
    const auto state = builder.publish();
    REQUIRE(state);
    CHECK(state.state->status() ==
        goldsrc::StockCommandAcknowledgementEvidenceStatus::
            candidate_value_observed);
}

TEST_CASE("Aggregate ACK counters cannot outrun retained source records",
    "[goldsrc][stock-runtime][ack]")
{
    using Domain = goldsrc::StockCommandAcknowledgementCandidateDomain;
    goldsrc::StockCommandAcknowledgementEvidenceBuilder builder{10U};
    REQUIRE(builder.observe_candidate(
        Domain::explicit_clientdata_field, 7U, 0U, cursor(4U)));
    REQUIRE(builder.observe_candidate(
        Domain::client_move_packet_ordinal, 7U, 0U, cursor(5U)));
    REQUIRE(builder.observe_correlation({Domain::explicit_clientdata_field,
        Domain::client_move_packet_ordinal, 8U, 8U, 0U, 1U, 1U, 1U}));
    const auto state = builder.publish();
    REQUIRE(state);
    CHECK(state.state->status() ==
        goldsrc::StockCommandAcknowledgementEvidenceStatus::
            candidate_value_observed);
}

TEST_CASE("Invalid ACK enum domains fail closed",
    "[goldsrc][stock-runtime][ack]")
{
    using Domain = goldsrc::StockCommandAcknowledgementCandidateDomain;
    goldsrc::StockCommandAcknowledgementEvidenceBuilder builder{11U};
    const auto invalid = static_cast<Domain>(255U);
    const auto candidate =
        builder.observe_candidate(invalid, 1U, 0U, cursor(1U));
    REQUIRE_FALSE(candidate);
    REQUIRE(candidate.error);
    CHECK(candidate.error->code ==
        goldsrc::StockCommandAcknowledgementEvidenceErrorCode::
            invalid_candidate_domain);
}

} // namespace
