#include <hlclient/goldsrc/stock_runtime_reconnect_lifecycle.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::StockRuntimeGenerationBoundaryObservation boundary(
    const std::size_t replay_payload_ordinal,
    const std::size_t observed_ordinal,
    const std::size_t delivery_ordinal)
{
    goldsrc::StockRuntimeGenerationBoundaryObservation value;
    value.observed = true;
    value.replay_payload_ordinal = replay_payload_ordinal;
    value.corpus_observed_ordinal = observed_ordinal;
    value.delivery_ordinal = delivery_ordinal;
    value.byte_offset = 3U;
    value.bit_offset = 0U;
    value.source_payload_byte_count = 8U;
    value.source_payload_bit_count = 64U;
    value.next_unconsumed_bit_count = 40U;
    return value;
}

[[nodiscard]] goldsrc::StockRuntimeGenerationCandidateObservation candidate(
    const std::uint8_t value = 44U)
{
    goldsrc::StockRuntimeGenerationCandidateObservation observation;
    observation.observed = true;
    observation.candidate_bit_width = 8U;
    observation.numeric_candidate = value;
    observation.byte_aligned = true;
    return observation;
}

[[nodiscard]] std::array<
    goldsrc::StockRuntimeConnectionGenerationObservation, 2U>
valid_fake_generations()
{
    std::array<goldsrc::StockRuntimeConnectionGenerationObservation, 2U>
        generations{};

    auto& first = generations[0];
    first.generation_ordinal = 1U;
    first.learned_client_endpoint_role_identity =
        goldsrc::kStockRuntimeGenerationAEndpointRole;
    first.owned_client_process_role_identity =
        goldsrc::kStockRuntimeGenerationAProcessRole;
    first.owned_client_process_observed = true;
    first.fresh_owned_client_process = true;
    first.learned_client_endpoint_observed = true;
    first.first_observed_ordinal = 0U;
    first.last_observed_ordinal = 99U;
    first.connectionless_exchange_count = 4U;
    first.connect_observed = true;
    first.accept_observed = true;
    first.first_sequenced_packet_ordinal = 10U;
    first.exact_post_resource_boundary = boundary(1U, 40U, 40U);
    first.candidate_observation = candidate();
    first.client_to_server_packet_count = 31U;
    first.server_to_client_packet_count = 70U;
    first.profile_identity = "stock_protocol_48_build_10210_evidence_pending";
    first.controlled_client_shutdown_observed = true;
    first.retired_client_endpoint_quiet = true;

    auto& second = generations[1];
    second.generation_ordinal = 2U;
    second.learned_client_endpoint_role_identity =
        goldsrc::kStockRuntimeGenerationBEndpointRole;
    second.owned_client_process_role_identity =
        goldsrc::kStockRuntimeGenerationBProcessRole;
    second.owned_client_process_observed = true;
    second.fresh_owned_client_process = true;
    second.learned_client_endpoint_observed = true;
    second.learned_client_endpoint_distinct_from_previous = true;
    second.first_observed_ordinal = 100U;
    second.last_observed_ordinal = 219U;
    second.connectionless_exchange_count = 5U;
    second.connect_observed = true;
    second.accept_observed = true;
    second.first_sequenced_packet_ordinal = 112U;
    second.exact_post_resource_boundary = boundary(2U, 170U, 170U);
    second.candidate_observation = candidate();
    second.client_to_server_packet_count = 38U;
    second.server_to_client_packet_count = 82U;
    second.profile_identity = first.profile_identity;
    return generations;
}

[[nodiscard]] goldsrc::StockRuntimeReconnectLifecycleValidationResult validate(
    const std::span<const goldsrc::StockRuntimeConnectionGenerationObservation>
        generations,
    const bool guard_continuous = true,
    const bool server_continuous = true,
    const bool relay_continuous = true,
    const bool cleanup_exact = true,
    const bool restoration_exact = true,
    const bool publication_ready = true)
{
    return goldsrc::validate_stock_runtime_reconnect_lifecycle(
        {generations, guard_continuous, server_continuous, relay_continuous,
         cleanup_exact, restoration_exact, publication_ready});
}

void check_error(
    const goldsrc::StockRuntimeReconnectLifecycleValidationResult& result,
    const goldsrc::StockRuntimeReconnectLifecycleErrorCode expected)
{
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == expected);
}

void prepare_reconnect_transition(
    goldsrc::StockRuntimeReconnectRelayTransition& transition)
{
    REQUIRE(transition.prepare_generation_a_retirement(true, true));
    REQUIRE(transition.confirm_generation_a_shutdown(true, true));
    REQUIRE(transition.begin_generation_b_relearn(true, true, true));
}

} // namespace

TEST_CASE("Fake reconnect orchestration proves two distinct generations",
          "[goldsrc][stock-runtime][reconnect][fake-orchestrator]")
{
    const auto generations = valid_fake_generations();
    const auto result = validate(generations);
    REQUIRE(result);
    REQUIRE(result.state);
    CHECK(result.state->connection_generation_count() == 2U);
    CHECK(result.state->exact_boundary_count() == 2U);
    CHECK(result.state->runtime_candidate_count() == 2U);
    CHECK(result.state->client_to_server_packet_count() == 69U);
    CHECK(result.state->server_to_client_packet_count() == 152U);
    CHECK(result.state->generation_distinct());
    CHECK_FALSE(result.state->candidate_conflict());
    CHECK_FALSE(result.state->candidate_body_consumed());
    CHECK_FALSE(result.state->semantic_category_assigned());
    REQUIRE(result.state->generations().size() == 2U);
    CHECK(result.state->generations()[0].generation_ordinal == 1U);
    CHECK(result.state->generations()[1].generation_ordinal == 2U);
}

TEST_CASE("Reconnect relay keeps exact server tail on the retired A sink",
          "[goldsrc][stock-runtime][reconnect][fake-server][tail]")
{
    goldsrc::StockRuntimeReconnectRelayTransition transition;
    prepare_reconnect_transition(transition);

    // HLDS can emit an old sequenced packet after the owned A process/socket
    // is gone. It remains a routing-only A tail and cannot activate B.
    CHECK(transition.route_server_datagram(true, false, false) ==
          goldsrc::StockRuntimeReconnectServerRoute::retired_generation_a);
    CHECK(transition.retired_generation_a_server_tail_packet_count() == 1U);
    CHECK_FALSE(transition.generation_b_endpoint_learned());
    CHECK_FALSE(transition.generation_b_accept_observed());
}

TEST_CASE("Reconnect tail emitter is ready before A shutdown and B waits for quiet",
          "[goldsrc][stock-runtime][reconnect][fake-orchestrator][tail][quiet]")
{
    goldsrc::StockRuntimeReconnectRelayTransition transition;

    // Phase one models the relay creating and binding its send-only emitter
    // while A is still alive. Neither the A-tail classifier nor generation B
    // can activate from this readiness ACK alone.
    REQUIRE(transition.prepare_generation_a_retirement(true, true));
    CHECK(transition.generation_a_retirement_prepared());
    CHECK_FALSE(transition.generation_a_shutdown_confirmed());
    CHECK_FALSE(transition.generation_b_relearn_ready());
    CHECK(transition.route_server_datagram(true, false, false) ==
          goldsrc::StockRuntimeReconnectServerRoute::reject);
    CHECK_FALSE(transition.begin_generation_b_relearn(true, true, true));

    // Phase two is accepted only after the orchestrator has stopped A and
    // proved that no owned client process remains. The quiet window is still a
    // separate prerequisite, so B cannot begin at the shutdown signal.
    CHECK_FALSE(transition.confirm_generation_a_shutdown(false, true));
    CHECK_FALSE(transition.confirm_generation_a_shutdown(true, false));
    REQUIRE(transition.confirm_generation_a_shutdown(true, true));
    CHECK(transition.generation_a_shutdown_confirmed());
    CHECK_FALSE(transition.begin_generation_b_relearn(true, false, true));
    CHECK_FALSE(transition.generation_b_relearn_ready());

    CHECK(transition.route_server_datagram(true, false, false) ==
          goldsrc::StockRuntimeReconnectServerRoute::retired_generation_a);
    CHECK(transition.retired_generation_a_server_tail_packet_count() == 1U);
    CHECK_FALSE(transition.generation_b_endpoint_learned());

    REQUIRE(transition.begin_generation_b_relearn(true, true, true));
    CHECK(transition.generation_b_relearn_ready());
    CHECK(transition.route_server_datagram(true, false, false) ==
          goldsrc::StockRuntimeReconnectServerRoute::retired_generation_a);
    CHECK(transition.retired_generation_a_server_tail_packet_count() == 2U);
}

TEST_CASE("Reconnect relay excludes A tail between B connect and ACCEPT",
          "[goldsrc][stock-runtime][reconnect][fake-client][fake-server][tail]")
{
    goldsrc::StockRuntimeReconnectRelayTransition transition;
    prepare_reconnect_transition(transition);

    // Fresh B first learns an endpoint with a connectionless challenge, then
    // emits its distinct connectionless connect request.
    REQUIRE(transition.observe_generation_b_client_datagram(
        true, true, false));
    CHECK(transition.route_server_datagram(true, true, false) ==
          goldsrc::StockRuntimeReconnectServerRoute::generation_b);
    REQUIRE(transition.observe_generation_b_client_datagram(
        true, true, true));
    CHECK(transition.generation_b_connect_observed());

    // A sequenced server tail arriving now still goes only to the retired A
    // sink; it is never counted as B's first sequenced packet.
    CHECK(transition.route_server_datagram(true, false, false) ==
          goldsrc::StockRuntimeReconnectServerRoute::retired_generation_a);
    CHECK(transition.retired_generation_a_server_tail_packet_count() == 1U);
    CHECK_FALSE(transition.generation_b_accept_observed());

    CHECK(transition.route_server_datagram(true, true, true) ==
          goldsrc::StockRuntimeReconnectServerRoute::generation_b);
    CHECK(transition.generation_b_accept_observed());
}

TEST_CASE("First B sequenced packet is admitted only after fresh ACCEPT",
          "[goldsrc][stock-runtime][reconnect][fake-server][sequence]")
{
    goldsrc::StockRuntimeReconnectRelayTransition transition;
    prepare_reconnect_transition(transition);
    REQUIRE(transition.observe_generation_b_client_datagram(
        true, true, true));
    REQUIRE(transition.route_server_datagram(true, true, true) ==
            goldsrc::StockRuntimeReconnectServerRoute::generation_b);

    CHECK(transition.route_server_datagram(true, false, false) ==
          goldsrc::StockRuntimeReconnectServerRoute::generation_b);
    CHECK(transition.retired_generation_a_server_tail_packet_count() == 0U);

    CHECK(transition.route_server_datagram(false, false, false) ==
          goldsrc::StockRuntimeReconnectServerRoute::reject);
}

TEST_CASE("Reconnect rejects client A refusing controlled exit",
          "[goldsrc][stock-runtime][reconnect][fake-client][failure]")
{
    auto generations = valid_fake_generations();
    generations[0].controlled_client_shutdown_observed = false;
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::
            generation_a_shutdown_not_proven);
}

TEST_CASE("Reconnect rejects failed generation B launch",
          "[goldsrc][stock-runtime][reconnect][fake-client][failure]")
{
    auto generations = valid_fake_generations();
    generations[1].owned_client_process_observed = false;
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::
            owned_client_process_not_observed);
}

TEST_CASE("Reconnect rejects an incorrectly reused endpoint",
          "[goldsrc][stock-runtime][reconnect][endpoint][failure]")
{
    auto generations = valid_fake_generations();
    generations[1].learned_client_endpoint_role_identity =
        goldsrc::kStockRuntimeGenerationAEndpointRole;
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::invalid_role_identity);

    generations = valid_fake_generations();
    generations[1].learned_client_endpoint_distinct_from_previous = false;
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::
            endpoint_generation_not_proven);
}

TEST_CASE("Reconnect requires a fresh connect and ACCEPT for generation B",
          "[goldsrc][stock-runtime][reconnect][connectionless][failure]")
{
    auto generations = valid_fake_generations();
    generations[1].connect_observed = false;
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::missing_connect);

    generations = valid_fake_generations();
    generations[1].accept_observed = false;
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::missing_accept);
}

TEST_CASE("Reconnect requires a second exact boundary",
          "[goldsrc][stock-runtime][reconnect][boundary][failure]")
{
    auto generations = valid_fake_generations();
    generations[1].exact_post_resource_boundary.observed = false;
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::
            missing_post_resource_boundary);
}

TEST_CASE("Reconnect rejects candidate conflict without parsing either body",
          "[goldsrc][stock-runtime][reconnect][candidate][failure]")
{
    auto generations = valid_fake_generations();
    generations[1].candidate_observation = candidate(45U);
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::candidate_conflicting);

    generations = valid_fake_generations();
    generations[1].candidate_observation.body_consumed = true;
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::candidate_body_consumed);

    generations = valid_fake_generations();
    generations[1].candidate_observation.semantic_category_assigned = true;
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::
            candidate_semantic_name_assigned);
}

TEST_CASE("Reconnect binds neutral candidate alignment to each exact boundary",
          "[goldsrc][stock-runtime][reconnect][candidate][boundary][mutation]")
{
    auto generations = valid_fake_generations();
    generations[1].exact_post_resource_boundary.bit_offset = 1U;
    generations[1].exact_post_resource_boundary.next_unconsumed_bit_count = 39U;
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::
            invalid_candidate_geometry);

    generations = valid_fake_generations();
    for (auto& generation : generations) {
        generation.exact_post_resource_boundary.bit_offset = 7U;
        generation.exact_post_resource_boundary.next_unconsumed_bit_count = 33U;
        generation.candidate_observation.numeric_candidate.reset();
        generation.candidate_observation.bounded_bit_prefix = 0xa5U;
        generation.candidate_observation.candidate_bit_width = 8U;
        generation.candidate_observation.byte_aligned = false;
    }
    const auto cross_byte = validate(generations);
    REQUIRE(cross_byte);
    REQUIRE(cross_byte.state);
    CHECK(cross_byte.state->runtime_candidate_count() == 2U);
    CHECK_FALSE(cross_byte.state->candidate_conflict());

    generations[1].exact_post_resource_boundary.bit_offset = 1U;
    generations[1].exact_post_resource_boundary.next_unconsumed_bit_count = 39U;
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::candidate_conflicting);

    generations[1].exact_post_resource_boundary.bit_offset = 7U;
    generations[1].exact_post_resource_boundary.next_unconsumed_bit_count = 33U;
    generations[1].candidate_observation.candidate_bit_width = 9U;
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::
            invalid_candidate_geometry);

    generations = valid_fake_generations();
    generations[1].exact_post_resource_boundary.byte_offset = 7U;
    generations[1].exact_post_resource_boundary.bit_offset = 7U;
    generations[1].exact_post_resource_boundary.next_unconsumed_bit_count = 1U;
    generations[1].candidate_observation.numeric_candidate.reset();
    generations[1].candidate_observation.bounded_bit_prefix = 1U;
    generations[1].candidate_observation.candidate_bit_width = 2U;
    generations[1].candidate_observation.byte_aligned = false;
    check_error(
        validate(generations),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::
            invalid_candidate_geometry);
}

TEST_CASE("Reconnect fails closed when guard or HLDS is lost between generations",
          "[goldsrc][stock-runtime][reconnect][guard][fake-server][failure]")
{
    const auto generations = valid_fake_generations();
    check_error(
        validate(generations, false),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::
            guard_lost_between_generations);
    check_error(
        validate(generations, true, false),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::
            server_exited_between_generations);
    check_error(
        validate(generations, true, true, false),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::
            relay_exited_between_generations);
}

TEST_CASE("Second-generation failure cannot become a transactional publication",
          "[goldsrc][stock-runtime][reconnect][cleanup][transaction]")
{
    const auto generations = valid_fake_generations();
    check_error(
        validate(generations, true, true, true, false),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::cleanup_inexact);
    check_error(
        validate(generations, true, true, true, true, false),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::restoration_inexact);
    check_error(
        validate(generations, true, true, true, true, true, false),
        goldsrc::StockRuntimeReconnectLifecycleErrorCode::
            transactional_publication_not_ready);
}

TEST_CASE("Active preflight attestation is bounded and fail closed",
          "[goldsrc][stock-runtime][preflight]")
{
    goldsrc::StockActiveCapturePreflightAttestation attestation;
    attestation.elevated = true;
    attestation.binary_profile_valid = true;
    attestation.app_manifest_valid = true;
    attestation.dynamic_wfp_session = true;
    attestation.ipv4_loopback_allowed = true;
    attestation.ipv6_loopback_allowed = true;
    attestation.ipv6_capability_available = true;
    attestation.non_loopback_denied_by_os = true;
    attestation.isolation_cleanup_exact = true;
    attestation.timestamp_category = "current-session";
    attestation.success = true;
    CHECK(goldsrc::validate_stock_active_capture_preflight_attestation(
        attestation));

    attestation.ipv6_loopback_allowed = false;
    attestation.ipv6_capability_available = false;
    CHECK(goldsrc::validate_stock_active_capture_preflight_attestation(
        attestation));

    attestation.ipv6_loopback_allowed = true;
    CHECK_FALSE(goldsrc::validate_stock_active_capture_preflight_attestation(
        attestation));

    attestation.ipv6_capability_available = true;
    attestation.isolation_cleanup_exact = false;
    CHECK_FALSE(goldsrc::validate_stock_active_capture_preflight_attestation(
        attestation));
}
