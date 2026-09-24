#include <hlclient/goldsrc/reference_prediction_seed.hpp>

#include <algorithm>
#include <cmath>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool zero_vector(
    const client::RuntimeVector3Observation& value) noexcept
{
    return value.complete() && *value.x == 0.0 && *value.y == 0.0 &&
        *value.z == 0.0;
}

[[nodiscard]] bool finite_vector(
    const client::RuntimeVector3Observation& value) noexcept
{
    return value.complete() && std::isfinite(*value.x) &&
        std::isfinite(*value.y) && std::isfinite(*value.z);
}

} // namespace

ReferencePredictionSeedResult inspect_reference_prediction_seed(
    const client::RuntimeClientObservationState& observation,
    const std::uint8_t receiving_client_slot,
    const ReferencePredictionAnchorResult& anchor,
    const std::optional<ReferenceRetainedPredictionButtons> retained) noexcept
{
    if (!anchor.bound() || !anchor.last_new_value ||
        observation.generation == 0U)
        return {ReferencePredictionSeedStatus::invalid_anchor};
    if (!observation.receiving_client ||
        observation.client_metadata.generation != observation.generation ||
        observation.client_metadata.freshness !=
            client::RuntimeObservationFreshness::observed_in_record ||
        !observation.client_metadata.source)
        return {ReferencePredictionSeedStatus::clientdata_unavailable};
    if (observation.entity_metadata.generation != observation.generation ||
        observation.entity_metadata.freshness !=
            client::RuntimeObservationFreshness::observed_in_record ||
        !observation.entity_metadata.source)
        return {ReferencePredictionSeedStatus::player_entity_unavailable};
    const auto& client_source = *observation.client_metadata.source;
    const auto& entity_source = *observation.entity_metadata.source;
    if (anchor.generation != observation.generation ||
        anchor.source_record_identity != client_source.record_identity ||
        anchor.source_sequence != client_source.source_transport_sequence ||
        client_source.record_identity != entity_source.record_identity ||
        client_source.source_transport_sequence !=
            entity_source.source_transport_sequence)
        return {ReferencePredictionSeedStatus::incoherent_records};
    const auto player_number =
        static_cast<std::uint32_t>(receiving_client_slot) + 1U;
    const auto player = std::find_if(observation.packet_entities.begin(),
        observation.packet_entities.end(), [player_number](const auto& entity) {
            return entity.entity_number == player_number;
        });
    if (player == observation.packet_entities.end() ||
        !player->player_movement_schema)
        return {ReferencePredictionSeedStatus::player_entity_unavailable};
    const auto& client = *observation.receiving_client;
    const auto missing = [](const ReferencePredictionSeedField field) {
        return ReferencePredictionSeedResult{
            ReferencePredictionSeedStatus::missing_semantic_field, {}, field};
    };
    const auto unsupported = [](const ReferencePredictionSeedField field) {
        return ReferencePredictionSeedResult{
            ReferencePredictionSeedStatus::unsupported_context, {}, field};
    };
    if (!finite_vector(client.origin))
        return missing(ReferencePredictionSeedField::origin);
    if (!finite_vector(client.velocity))
        return missing(ReferencePredictionSeedField::velocity);
    const bool vertical_only_view_offset =
        !client.view_offset.x && !client.view_offset.y &&
        client.view_offset.z && std::isfinite(*client.view_offset.z);
    if (!finite_vector(client.view_offset) &&
        !vertical_only_view_offset)
        return missing(ReferencePredictionSeedField::view_offset);
    if (!client.flags) return missing(ReferencePredictionSeedField::flags);
    if (!client.water_level)
        return missing(ReferencePredictionSeedField::water_level);
    if (!client.dead_flag)
        return missing(ReferencePredictionSeedField::dead_flag);
    if (!client.in_duck)
        return missing(ReferencePredictionSeedField::in_duck);
    if (!player->move_type)
        return missing(ReferencePredictionSeedField::move_type);
    if (!player->use_hull)
        return missing(ReferencePredictionSeedField::use_hull);
    if (!player->gravity_multiplier)
        return missing(ReferencePredictionSeedField::gravity_multiplier);
    if (!player->friction_multiplier)
        return missing(ReferencePredictionSeedField::friction_multiplier);
    if (!finite_vector(player->base_velocity))
        return missing(ReferencePredictionSeedField::base_velocity);
    if (!player->spectator)
        return missing(ReferencePredictionSeedField::spectator);

    // MOVETYPE_WALK=3, DEAD_NO=0, FL_ONTRAIN/FL_FROZEN/FL_SPECTATOR/
    // FL_WATERJUMP from pinned Valve common/const.h. The current movement
    // kernel has no basevelocity, train or liquid implementation.
    constexpr std::uint32_t kUnsupportedFlags =
        (1U << 11U) | (1U << 12U) | (1U << 24U) | (1U << 26U);
    if (*player->move_type != 3U)
        return unsupported(ReferencePredictionSeedField::move_type);
    if (*client.dead_flag != 0U)
        return unsupported(ReferencePredictionSeedField::dead_flag);
    if (*player->spectator)
        return unsupported(ReferencePredictionSeedField::spectator);
    if (*client.water_level != 0U)
        return unsupported(ReferencePredictionSeedField::water_level);
    if ((*client.flags & kUnsupportedFlags) != 0U)
        return unsupported(ReferencePredictionSeedField::flags);
    if (!zero_vector(player->base_velocity))
        return unsupported(ReferencePredictionSeedField::base_velocity);
    if (*player->use_hull > 1U)
        return unsupported(ReferencePredictionSeedField::use_hull);
    if ((*player->use_hull == 1U) != *client.in_duck)
        return unsupported(ReferencePredictionSeedField::in_duck);
    if (!std::isfinite(*player->gravity_multiplier) ||
        *player->gravity_multiplier <= 0.0)
        return unsupported(ReferencePredictionSeedField::gravity_multiplier);
    if (!std::isfinite(*player->friction_multiplier) ||
        *player->friction_multiplier <= 0.0)
        return unsupported(ReferencePredictionSeedField::friction_multiplier);
    if (client.maximum_speed &&
        (!std::isfinite(*client.maximum_speed) ||
         *client.maximum_speed < 0.0))
        return unsupported(ReferencePredictionSeedField::maximum_speed);

    std::uint16_t old_buttons = 0U;
    auto old_buttons_origin = ReferencePredictionFieldOrigin::
        reference_anchored_neutral_command;
    if (retained) {
        if (retained->command_identity != *anchor.last_new_command)
            return {ReferencePredictionSeedStatus::retained_slot_mismatch};
        old_buttons = retained->old_buttons;
        old_buttons_origin = ReferencePredictionFieldOrigin::
            exact_retained_prediction_slot;
    } else if (anchor.last_new_value->buttons != 0U) {
        return {ReferencePredictionSeedStatus::matching_prediction_slot_required};
    }

    ReferencePredictionSeed seed;
    seed.generation = observation.generation;
    seed.record_identity = client_source.record_identity;
    seed.clientdata_source = client_source;
    seed.player_entity_source = entity_source;
    seed.receiving_entity_number = player_number;
    seed.command_boundary = *anchor.last_new_command;
    seed.origin = client.origin;
    seed.velocity = client.velocity;
    seed.view_offset = vertical_only_view_offset
        ? hlclient::client::RuntimeVector3Observation{
              0.0, 0.0, client.view_offset.z}
        : client.view_offset;
    seed.view_offset_origin = vertical_only_view_offset
        ? ReferencePredictionFieldOrigin::
              reference_vertical_only_view_offset_policy
        : ReferencePredictionFieldOrigin::clientdata_observed;
    seed.base_velocity = player->base_velocity;
    seed.flags = *client.flags;
    seed.move_type = *player->move_type;
    seed.use_hull = *player->use_hull;
    seed.water_level = *client.water_level;
    seed.in_duck = *client.in_duck;
    seed.old_buttons = old_buttons;
    seed.maximum_speed = client.maximum_speed;
    seed.gravity_multiplier = player->gravity_multiplier;
    seed.friction_multiplier = player->friction_multiplier;
    seed.old_buttons_origin = old_buttons_origin;
    return {ReferencePredictionSeedStatus::ready, seed};
}

ReferencePredictionGroundResult derive_reference_prediction_ground(
    const ReferencePredictionSeed& seed,
    const GoldSrcWireUserCmd& boundary_command,
    const movement::ILocalMovementCollision& collision,
    movement::GoldSrcLocalMovementScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config)
{
    namespace player = hlclient::movement;
    const auto identity = collision.session_identity();
    if (!collision.valid() || !identity || !identity->valid() ||
        identity->profile != movement::LocalMovementCollisionProfile::world_only_v1 ||
        !movement::valid_goldsrc_local_movement_config(config))
        return {ReferencePredictionGroundStatus::invalid_collision_identity};
    if (!finite_vector(seed.origin) || !finite_vector(seed.velocity) ||
        !finite_vector(seed.view_offset) || !seed.gravity_multiplier ||
        !seed.friction_multiplier ||
        !std::isfinite(*seed.gravity_multiplier) ||
        !std::isfinite(*seed.friction_multiplier))
        return {ReferencePredictionGroundStatus::invalid_movement_state};
    if (seed.use_hull > 1U || seed.in_duck != (seed.use_hull == 1U))
        return {ReferencePredictionGroundStatus::unsupported_hull};
    const auto hull = seed.use_hull == 0U ? player::PlayerMovementHull::standing
                                           : player::PlayerMovementHull::ducked;
    const assets::AssetVector3 origin{
        static_cast<float>(*seed.origin.x), static_cast<float>(*seed.origin.y),
        static_cast<float>(*seed.origin.z)};
    const assets::AssetVector3 velocity{
        static_cast<float>(*seed.velocity.x),
        static_cast<float>(*seed.velocity.y),
        static_cast<float>(*seed.velocity.z)};
    const auto position = collision.test_position(
        origin, hull, scratch.collision, config.collision_query);
    if (!position || !position.result)
        return {ReferencePredictionGroundStatus::collision_query_failed};
    if (position.result->status != movement::LocalMovementPositionStatus::free)
        return {ReferencePredictionGroundStatus::solid_start};
    const auto contents = collision.point_contents(
        origin, scratch.collision, config.collision_query);
    if (!contents || !contents.result)
        return {ReferencePredictionGroundStatus::collision_query_failed};
    if (contents.result->contents.category != player::PlayerMovementContents::empty ||
        position.result->contents.category != player::PlayerMovementContents::empty)
        return {ReferencePredictionGroundStatus::unsupported_contents};

    player::PlayerGroundStateCreateInfo ground;
    ground.contact_position = origin;
    if (velocity.z <= config.maximum_ground_snap_upward_velocity) {
        const assets::AssetVector3 end{
            origin.x, origin.y,
            static_cast<float>(static_cast<double>(origin.z) -
                               config.ground_probe_distance)};
        const auto trace = collision.trace_hull(
            origin, end, hull, scratch.collision, config.collision_query);
        if (!trace || !trace.result)
            return {ReferencePredictionGroundStatus::collision_query_failed};
        const auto& hit = *trace.result;
        if (hit.start_solid || hit.all_solid)
            return {ReferencePredictionGroundStatus::solid_start};
        if (!std::isfinite(hit.fraction) || hit.fraction < 0.0 ||
            hit.fraction > 1.0 ||
            hit.collision_profile != collision.profile())
            return {ReferencePredictionGroundStatus::invalid_ground_trace};
        if (hit.in_liquid)
            return {ReferencePredictionGroundStatus::unsupported_contents};
        if (hit.fraction < 1.0) {
            if (!hit.hit || !hit.collision_plane ||
                !std::isfinite(hit.collision_plane->normal.z))
                return {ReferencePredictionGroundStatus::invalid_ground_trace};
            if (hit.hit->kind != player::PlayerMovementHitKind::world)
                return {ReferencePredictionGroundStatus::unsupported_contents};
            if (hit.collision_plane->normal.z >=
                config.minimum_walkable_normal_z) {
                ground.grounded = true;
                ground.walkable = true;
                ground.hit = hit.hit;
                ground.plane = *hit.collision_plane;
                ground.contact_position = hit.end_position;
                ground.probe_fraction = hit.fraction;
            }
        }
    }
    constexpr std::uint32_t kValveOnGroundFlag = 1U << 9U;
    if (((seed.flags & kValveOnGroundFlag) != 0U) != ground.grounded)
        return {ReferencePredictionGroundStatus::ground_flag_disagreement};

    player::LocalPlayerMovementStateCreateInfo state;
    state.origin = origin;
    state.velocity = velocity;
    constexpr float kDegreesPerTurn = 360.0F / 65'536.0F;
    state.view_angles = {
        static_cast<float>(boundary_command.angle_turns[0U]) * kDegreesPerTurn,
        static_cast<float>(boundary_command.angle_turns[1U]) * kDegreesPerTurn,
        static_cast<float>(boundary_command.angle_turns[2U]) * kDegreesPerTurn};
    state.hull = hull;
    state.mode = ground.grounded ? player::PlayerMovementMode::walking
                                 : player::PlayerMovementMode::airborne;
    state.ground = ground;
    state.view_offset = {
        static_cast<float>(*seed.view_offset.x),
        static_cast<float>(*seed.view_offset.y),
        static_cast<float>(*seed.view_offset.z)};
    state.old_buttons = seed.old_buttons;
    state.source_command_sequence = seed.command_boundary.value();
    state.last_valid_contents = player::PlayerMovementContents::empty;
    state.gravity_multiplier = static_cast<float>(*seed.gravity_multiplier);
    state.friction_multiplier = static_cast<float>(*seed.friction_multiplier);
    state.command_profile =
        player::GoldSrcMovementCommandProfile::reference_wire_dry_walk_v1;
    const auto created = player::LocalPlayerMovementState::create(
        state, config.state_limits);
    if (!created)
        return {ReferencePredictionGroundStatus::invalid_movement_state};
    return {ReferencePredictionGroundStatus::ready, std::move(created.state),
            identity, ground};
}

} // namespace hlclient::goldsrc
