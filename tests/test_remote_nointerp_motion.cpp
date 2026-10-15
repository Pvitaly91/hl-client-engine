#include <hlclient/goldsrc/entity_snapshot_interpolation.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace {
namespace g = hlclient::goldsrc;
namespace c = hlclient::client;
namespace v = hlclient::entity_visual;
using State = c::RuntimeClientObservationState;

constexpr std::array<std::uint32_t, 1> selected{2U};
constexpr auto nointerp_reason = static_cast<std::uint32_t>(
    v::PlayerContinuityReason::nointerp_effect);

std::shared_ptr<const State> observed(std::size_t ordinal, double x, double z,
                                    std::uint32_t effects = 0U)
{
    auto state = std::make_shared<State>();
    state->generation = 1U;
    state->publication_revision = ordinal;
    state->server_time_seconds = 10.0 + .04 * static_cast<double>(ordinal - 1U);
    const c::RuntimeSubstateMetadata metadata{
        1U, c::RuntimeObservationFreshness::observed_in_record,
        c::RuntimeObservationCompleteness::complete_reconstruction,
        c::RuntimeObservationSource{ordinal, ordinal, 1U, 0U, 8U}};
    state->entity_metadata = state->server_time_metadata = metadata;
    state->client_metadata.generation = 1U;
    c::RuntimePacketEntityObservation player;
    player.entity_number = 2U;
    player.player_movement_schema = player.ordinary_visual_schema = true;
    player.origin = {x, 0.0, z};
    player.angles = {0.0, 0.0, 0.0};
    player.model_index = 7U;
    player.sequence = 2U;
    player.frame = effects ? 0.0 : 32.0;
    player.animation_time_seconds = state->server_time_seconds;
    player.frame_rate = 1.0;
    player.gait_sequence = 1U;
    player.weapon_model_index = 21U;
    player.effects = effects;
    player.body = 0U;
    player.skin = 0;
    player.controllers.fill(127U);
    player.blending.fill(127U);
    state->packet_entities.push_back(player);
    state->canonical_state_hash = c::runtime_observation_canonical_hash(*state);
    REQUIRE(c::valid_runtime_observation(*state));
    return state;
}

std::shared_ptr<const State> changed(std::shared_ptr<const State> source,
                                     auto mutate)
{
    auto state = std::make_shared<State>(*source);
    mutate(*state);
    state->canonical_state_hash = c::runtime_observation_canonical_hash(*state);
    REQUIRE(c::valid_runtime_observation(*state));
    return state;
}

g::RuntimeEntityInterpolationOptions policy(
    std::span<const g::RuntimePlayerAnimationEffectException> exceptions = {})
{
    g::RuntimeEntityInterpolationOptions options;
    options.limits.maximum_snapshot_gap_seconds = .25;
    options.teleport_distance = 128.0;
    options.no_interpolation_effect_mask = 32U;
    options.animation_only_effect_exceptions = exceptions;
    return options;
}

auto draw(g::RuntimePlayerSnapshotPresentation& owner, const State& current,
          double now,
          std::span<const g::RuntimePlayerAnimationEffectException> exceptions = {},
          std::span<const g::RuntimePlayerContinuityBoundary> boundaries = {})
{
    auto result = owner.sample(current, now, policy(exceptions), .1, selected,
                               boundaries);
    INFO((result.presentation.error ? result.presentation.error->context : ""));
    REQUIRE(result);
    REQUIRE(result.players.size() == 1U);
    return result;
}

bool attack_pulse(std::size_t ordinal)
{
    // Each two-snapshot 0 -> 34 -> 0 pulse starts after the buffer is warm.
    return ordinal >= 17U && (ordinal % 7U == 3U || ordinal % 7U == 4U);
}
} // namespace

TEST_CASE("E32 approved animation pulses preserve delayed running and lift coordinates on both edges",
          "[e32-nointerp-motion][entity-interpolation][core-only]")
{
    struct Motion { double horizontal, vertical; };
    const std::array cases{Motion{320.0, 0.0}, Motion{80.0, 120.0},
                           Motion{80.0, -120.0}};
    for (const auto motion : cases) {
        CAPTURE(motion.horizontal, motion.vertical);
        std::vector<g::RuntimePlayerAnimationEffectException> exceptions;
        for (std::size_t ordinal = 17U; ordinal < 80U; ++ordinal)
            if (attack_pulse(ordinal)) exceptions.push_back({2U, ordinal, 32U});
        g::RuntimePlayerSnapshotPresentation approved, control, strict;
        const auto make = [&](std::size_t ordinal, bool pulse) {
            const double elapsed = .04 * static_cast<double>(ordinal - 1U);
            return observed(ordinal, motion.horizontal * elapsed,
                            64.0 + motion.vertical * elapsed,
                            pulse && attack_pulse(ordinal) ? 34U : 0U);
        };
        auto current = make(2U, true);
        auto clean = make(2U, false);
        approved.observe(make(1U, true));
        strict.observe(make(1U, true));
        control.observe(make(1U, false));
        approved.observe(current);
        strict.observe(current);
        control.observe(clean);
        std::size_t next = 3U, strict_holds = 0U;
        double last_x{}, last_z{}, strict_x{}, strict_z{};
        double approved_max_step{}, strict_max_step{};
        for (std::size_t frame = 0U; frame <= 320U; ++frame) {
            const double now = 100.0 + .005 * static_cast<double>(frame);
            while (next < 80U && now >= 100.0 + .04 * static_cast<double>(next - 2U)) {
                current = make(next, true);
                clean = make(next, false);
                ++next;
                approved.observe(current);
                strict.observe(current);
                control.observe(clean);
            }
            const auto immutable = *current;
            // Supply only already committed approvals, as the application does.
            const auto committed = std::upper_bound(exceptions.begin(), exceptions.end(),
                current->entity_metadata.source->record_ordinal,
                [](std::size_t ordinal, const auto& entry) { return ordinal < entry.record_ordinal; });
            const auto rows = std::span{exceptions}.first(static_cast<std::size_t>(committed - exceptions.begin()));
            const auto value = draw(approved, *current, now, rows);
            const auto baseline = draw(control, *clean, now);
            const auto negative = draw(strict, *current, now);
            const auto& point = value.presentation.state->packet_entities.front().origin;
            const auto& expected = baseline.presentation.state->packet_entities.front().origin;
            const auto& jumped = negative.presentation.state->packet_entities.front().origin;
            CHECK(*point.x == Catch::Approx(*expected.x).margin(1e-7));
            CHECK(*point.z == Catch::Approx(*expected.z).margin(1e-7));
            CHECK(*point.x == Catch::Approx(motion.horizontal * (value.presentation.sample_seconds - 10.0)).margin(1e-7));
            CHECK(*point.z == Catch::Approx(64.0 + motion.vertical * (value.presentation.sample_seconds - 10.0)).margin(1e-7));
            CHECK((value.players.front().diagnostics.new_boundary_reasons & nointerp_reason) == 0U);
            CHECK_FALSE(value.players.front().discontinuity);
            if (frame > 100U) {
                approved_max_step = std::max(approved_max_step, std::hypot(*point.x - last_x, *point.z - last_z));
                strict_max_step = std::max(strict_max_step, std::hypot(*jumped.x - strict_x, *jumped.z - strict_z));
                if (negative.players.front().diagnostics.decision == v::PlayerPresentationDecision::held_floor)
                    ++strict_holds;
                CHECK(value.players.front().diagnostics.decision != v::PlayerPresentationDecision::held_floor);
            }
            CHECK(approved.history_size() <= g::RuntimePlayerSnapshotPresentation::maximum_history);
            CHECK(*current == immutable);
            last_x = *point.x; last_z = *point.z;
            strict_x = *jumped.x; strict_z = *jumped.z;
        }
        CHECK(approved_max_step <= std::hypot(motion.horizontal, motion.vertical) * .005001);
        CHECK(strict_max_step > approved_max_step * 3.0);
        CHECK(strict_holds > 0U); // Production negative control reproduces snap -> hold.
    }
}

TEST_CASE("E32 animation approval is exact to an entity and committed record including the falling edge",
          "[e32-nointerp-motion][entity-interpolation][core-only]")
{
    for (const bool authorized : {false, true}) {
        CAPTURE(authorized);
        g::RuntimePlayerSnapshotPresentation owner;
        auto first = observed(1U, 0.0, 64.0);
        owner.observe(first);
        REQUIRE(draw(owner, *first, 100.0));
        auto pulse = observed(2U, 12.8, 68.8, 34U);
        owner.observe(pulse);
        const std::array exception{g::RuntimePlayerAnimationEffectException{2U, 2U, 32U}};
        const auto permitted = authorized ? std::span<const g::RuntimePlayerAnimationEffectException>{exception} :
            std::span<const g::RuntimePlayerAnimationEffectException>{};
        const auto onset = draw(owner, *pulse, 100.01, permitted);
        CHECK(((onset.players[0].diagnostics.floor_reasons & nointerp_reason) == 0U) == authorized);
        if (authorized) CHECK(*onset.presentation.state->packet_entities[0].origin.x == Catch::Approx(3.2).margin(1e-7));
        else CHECK(onset.presentation.state->packet_entities[0].origin.x == pulse->packet_entities[0].origin.x);
        auto released = observed(3U, 25.6, 73.6);
        owner.observe(released);
        const auto release = draw(owner, *released, 100.04, permitted);
        if (authorized) {
            CHECK((release.players[0].diagnostics.floor_reasons & nointerp_reason) == 0U);
            CHECK((release.players[0].diagnostics.new_boundary_reasons & nointerp_reason) == 0U);
            CHECK_FALSE(release.players[0].discontinuity);
            CHECK(*release.presentation.state->packet_entities[0].origin.x < *released->packet_entities[0].origin.x);
        } else {
            CHECK((release.players[0].diagnostics.new_boundary_reasons & nointerp_reason) != 0U);
            CHECK(release.presentation.state->packet_entities[0].origin.x == released->packet_entities[0].origin.x);
        }
        // A later, unapproved pulse cannot inherit approval from an old record.
        auto later = observed(4U, 38.4, 78.4, 34U);
        owner.observe(later);
        const auto unapproved = draw(owner, *later, 100.08, permitted);
        CHECK((unapproved.players[0].diagnostics.new_boundary_reasons & nointerp_reason) != 0U);
        CHECK(unapproved.presentation.state->packet_entities[0].origin.x == later->packet_entities[0].origin.x);
    }
}

TEST_CASE("E32 animation effect approval cannot suppress position or life discontinuity guards",
          "[e32-nointerp-motion][entity-interpolation][core-only]")
{
    constexpr std::array expected{
        v::PlayerContinuityReason::teleport_distance,
        v::PlayerContinuityReason::model_changed,
        v::PlayerContinuityReason::schema_changed,
        v::PlayerContinuityReason::removal,
        v::PlayerContinuityReason::game_boundary,
        v::PlayerContinuityReason::same_time_origin_correction};
    for (std::size_t mode = 0U; mode < expected.size(); ++mode) {
        CAPTURE(mode);
        g::RuntimePlayerSnapshotPresentation owner;
        const auto first = observed(1U, 0.0, 64.0);
        owner.observe(first);
        REQUIRE(draw(owner, *first, 100.0));
        auto next = observed(3U, 25.6, 73.6, 34U);
        if (mode == 0U) next = changed(next, [](auto& state) { state.packet_entities[0].origin.x = 256.0; });
        if (mode == 1U) next = changed(next, [](auto& state) { state.packet_entities[0].model_index = 8U; });
        if (mode == 2U) owner.observe(changed(observed(2U, 12.8, 68.8), [](auto& state) { state.packet_entities[0].player_movement_schema = false; }));
        if (mode == 3U) owner.observe(changed(observed(2U, 12.8, 68.8), [](auto& state) { state.packet_entities.clear(); }));
        if (mode == 5U) {
            owner.observe(observed(2U, 12.8, 68.8));
            next = changed(next, [](auto& state) { state.server_time_seconds = 10.04; });
        }
        owner.observe(next);
        const std::array exception{g::RuntimePlayerAnimationEffectException{2U, 3U, 32U}};
        const std::array boundary{g::RuntimePlayerContinuityBoundary{2U, 3U}};
        const auto boundaries = mode == 4U ? std::span<const g::RuntimePlayerContinuityBoundary>{boundary} :
            std::span<const g::RuntimePlayerContinuityBoundary>{};
        const auto value = draw(owner, *next, 100.01, exception, boundaries);
        CHECK((value.players[0].diagnostics.floor_reasons & static_cast<std::uint32_t>(expected[mode])) != 0U);
        CHECK(value.presentation.state->packet_entities[0].origin == next->packet_entities[0].origin);
        CHECK((value.players[0].diagnostics.floor_reasons & nointerp_reason) == 0U);
    }
}

TEST_CASE("E32 an approved player cannot disable another player's no interpolation guard",
          "[e32-nointerp-motion][entity-interpolation][core-only]")
{
    const auto with_peer = [](auto source) {
        return changed(source, [](auto& state) {
            auto peer = state.packet_entities[0];
            peer.entity_number = 3U;
            peer.origin.x = *peer.origin.x + 100.0;
            state.packet_entities.push_back(peer);
        });
    };
    g::RuntimePlayerSnapshotPresentation owner;
    const auto first = with_peer(observed(1U, 0.0, 64.0));
    owner.observe(first);
    const std::array<std::uint32_t, 2> actors{2U, 3U};
    REQUIRE(owner.sample(*first, 100.0, policy(), .1, actors));
    const auto pulse = with_peer(observed(2U, 12.8, 68.8, 34U));
    owner.observe(pulse);
    const std::array approved{g::RuntimePlayerAnimationEffectException{2U, 2U, 32U}};
    const auto result = owner.sample(*pulse, 100.01, policy(approved), .1, actors);
    REQUIRE(result);
    REQUIRE(result.players.size() == 2U);
    CHECK(*result.presentation.state->packet_entities[0].origin.x == Catch::Approx(3.2).margin(1e-7));
    CHECK(result.presentation.state->packet_entities[1].origin.x == pulse->packet_entities[1].origin.x);
    CHECK((result.players[0].diagnostics.new_boundary_reasons & nointerp_reason) == 0U);
    CHECK((result.players[1].diagnostics.new_boundary_reasons & nointerp_reason) != 0U);
}

TEST_CASE("E32 malformed animation effect approval tables fail before any position sample is committed",
          "[e32-nointerp-motion][entity-interpolation][core-only]")
{
    const std::array<std::vector<g::RuntimePlayerAnimationEffectException>, 7> invalid{
        std::vector<g::RuntimePlayerAnimationEffectException>{{0U, 2U, 32U}},
        std::vector<g::RuntimePlayerAnimationEffectException>{{33U, 2U, 32U}},
        std::vector<g::RuntimePlayerAnimationEffectException>{{2U, 0U, 32U}},
        std::vector<g::RuntimePlayerAnimationEffectException>{{2U, 2U, 0U}},
        std::vector<g::RuntimePlayerAnimationEffectException>{{2U, 2U, 2U}},
        std::vector<g::RuntimePlayerAnimationEffectException>{{2U, 2U, 32U}, {2U, 2U, 32U}},
        std::vector<g::RuntimePlayerAnimationEffectException>{{3U, 2U, 32U}, {2U, 2U, 32U}}};
    for (const auto& table : invalid) {
        g::RuntimePlayerSnapshotPresentation owner;
        owner.observe(observed(1U, 0.0, 64.0));
        const auto pulse = observed(2U, 12.8, 68.8, 34U);
        owner.observe(pulse);
        const auto denied = owner.sample(*pulse, 100.0, policy(table), .1, selected);
        CHECK_FALSE(denied);
        REQUIRE(denied.presentation.error);
        CHECK(denied.presentation.error->code == g::EntityInterpolationErrorCode::invalid_configuration);
        CHECK(owner.player_count() == 0U);
        const auto strict = draw(owner, *pulse, 100.0);
        CHECK(strict.presentation.state->packet_entities[0].origin.x == pulse->packet_entities[0].origin.x);
    }
    std::vector<g::RuntimePlayerAnimationEffectException> oversized;
    for (std::size_t ordinal = 1U; ordinal <= 513U; ++ordinal)
        oversized.push_back({2U, ordinal, 32U});
    g::RuntimePlayerSnapshotPresentation owner;
    const auto pulse = observed(2U, 12.8, 68.8, 34U);
    owner.observe(pulse);
    CHECK_FALSE(owner.sample(*pulse, 100.0, policy(oversized), .1, selected));
}
