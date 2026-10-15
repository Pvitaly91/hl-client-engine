#include <hlclient/goldsrc/reference_prediction_reconciliation.hpp>
#include <hlclient/goldsrc/reference_brush_collision.hpp>

#include <hlclient/prediction/local_prediction.hpp>

#include <cmath>
#include <algorithm>
#include <limits>
#include <new>
#include <vector>

namespace hlclient::goldsrc {

ReferenceRebaseResult rebase_reference_prediction(
    const prediction::LocalPredictionHistoryState& previous,
    const hlclient::movement::LocalPlayerMovementState& correction,
    const movement::GoldSrcMovementEnvironment& environment,
    const movement::ILocalMovementCollision& collision,
    movement::GoldSrcLocalMovementScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config,
    const ReferenceVerticalSupportMotion* support,
    const ReferenceBrushCollisionContext* ladder_context,
    const movement::ReferenceLadderMovementPolicy* ladder_policy)
{
    ReferenceRebaseResult result;
    const auto& old_session = previous.session();
    if ((old_session.prediction_profile != prediction::
             PredictionCompatibilityProfile::reference_carrier_dry_walk_v1 &&
         old_session.prediction_profile != prediction::
             PredictionCompatibilityProfile::reference_carrier_jump_duck_v2 &&
         old_session.prediction_profile != prediction::
             PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_v3 &&
         old_session.prediction_profile != prediction::PredictionCompatibilityProfile::reference_carrier_jump_duck_weapon_use_v4) ||
        correction.command_profile() != old_session.command_profile)
        return result;
    const auto boundary = correction.source_command_sequence();
    if (boundary == 0U)
        return {ReferenceRebaseStatus::boundary_missing};
    const hlclient::movement::LocalPlayerMovementState* matched = nullptr;
    if (previous.anchor().movement_state()->source_command_sequence() == boundary)
        matched = previous.anchor().movement_state().get();
    else if (const auto sequence = GoldSrcUserCmdSequence::create(boundary)) {
        if (const auto* entry = previous.find_exact(*sequence))
            matched = entry->post_command_state().get();
    }
    if (!matched)
        return {ReferenceRebaseStatus::boundary_missing};
    const auto& a = matched->origin();
    const auto& b = correction.origin();
    const auto dx = static_cast<double>(a.x) - b.x;
    const auto dy = static_cast<double>(a.y) - b.y;
    const auto dz = static_cast<double>(a.z) - b.z;
    result.raw_position_error = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(*result.raw_position_error))
        return {ReferenceRebaseStatus::publication_failed};
    if (old_session.prediction_generation == UINT64_MAX)
        return {ReferenceRebaseStatus::publication_failed};
    const auto session = prediction::create_prediction_session_identity(
        old_session.session_generation,
        old_session.prediction_generation + 1U, collision, environment, config,
        correction,
        old_session.prediction_profile,
        prediction::PredictionAcknowledgementProfile::
            reference_sent_carrier_boundary_v1);
    if (!session)
        return {ReferenceRebaseStatus::session_mismatch};
    const auto initial = prediction::LocalPredictionHistoryState::create_initial(
        correction, *session.session, previous.limits());
    if (!initial)
        return {ReferenceRebaseStatus::publication_failed};
    std::shared_ptr<const prediction::LocalPredictionHistoryState> candidate =
        initial.history;
    const auto maximum = previous.limits().maximum_replay_commands;
    try {
        std::vector<prediction::PredictedCommandAppend> suffix;
        suffix.reserve(previous.size());
        auto current = candidate->current_predicted_state();
        std::uint32_t expected = boundary;
        for (const auto& old : previous.entries()) {
            const auto number = old.command_sequence().value();
            if (number <= boundary)
                continue;
            if (expected == UINT32_MAX || number != expected + 1U)
                return {ReferenceRebaseStatus::history_gap};
            if (suffix.size() >= maximum)
                return {ReferenceRebaseStatus::replay_limit};
            const auto simulated = simulate_reference_movement(
                *current, *old.command(), environment, collision, scratch, config,
                support, ladder_context, ladder_policy);
            if (!simulated) {
                result.status = ReferenceRebaseStatus::simulation_failed;
                if (simulated.error)
                    result.simulation_error = simulated.error->code;
                return result;
            }
            auto post = std::make_shared<const hlclient::movement::
                LocalPlayerMovementState>(*simulated.state);
            suffix.push_back(prediction::PredictedCommandAppend{
                old.command(), current, post, simulated.statistics,
                prediction::summarize_prediction_touches(
                    simulated.touches, false, false)});
            current = std::move(post);
            expected = number;
        }
        if (!suffix.empty()) {
            const auto appended = prediction::append_local_prediction_commands(
                *candidate, suffix);
            if (!appended)
                return {ReferenceRebaseStatus::publication_failed};
            candidate = appended.history;
        }
        result.status = ReferenceRebaseStatus::ready;
        result.history = std::move(candidate);
        result.replayed_commands = suffix.size();
        return result;
    } catch (const std::bad_alloc&) {
        return {ReferenceRebaseStatus::publication_failed};
    }
}

std::optional<ReferenceExteriorCornerPresentation>
sample_reference_exterior_corner_presentation(
    const assets::AssetVector3 from,const assets::AssetVector3 to,const double alpha,
    const hlclient::movement::PlayerMovementHull hull,
    const movement::ILocalMovementCollision& collision,
    hlclient::collision::CollisionQueryScratch& scratch,
    const movement::GoldSrcLocalMovementConfig& config,const double maximum_route_length,
    std::size_t* const observed_trace_queries)
{
    const auto finite=[](const assets::AssetVector3& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    const auto length=[](const assets::AssetVector3& a,const assets::AssetVector3& b){
        return std::hypot(static_cast<double>(a.x)-b.x,static_cast<double>(a.y)-b.y,static_cast<double>(a.z)-b.z);};
    if(!finite(from)||!finite(to)||!std::isfinite(alpha)||alpha<0||alpha>1||
        !std::isfinite(maximum_route_length)||maximum_route_length<=0||maximum_route_length>4||
        std::abs(static_cast<double>(from.z)-to.z)>.001||length(from,to)>maximum_route_length||
        !movement::valid_goldsrc_local_movement_config(config))return {};
    const auto free=[&](const auto& p){const auto result=collision.test_position(p,hull,scratch,config.collision_query);
        return result&&result.result->status==movement::LocalMovementPositionStatus::free;};
    const auto trace=[&](const auto& a,const auto& b){
        if(observed_trace_queries&&*observed_trace_queries!=(std::numeric_limits<std::size_t>::max)())++*observed_trace_queries;
        return collision.trace_hull(a,b,hull,scratch,config.collision_query);};
    const auto clear=[&](const auto& a,const auto& b){const auto result=trace(a,b);
        return result&&!result.result->start_solid&&!result.result->all_solid&&!result.result->in_liquid&&result.result->fraction>=1;};
    if(!free(from)||!free(to))return {};
    const auto forward=trace(from,to);
    const auto reverse=trace(to,from);
    const auto eligible=[](const auto& trace){return trace&&trace.result&&!trace.result->start_solid&&!trace.result->all_solid&&
        !trace.result->in_liquid&&trace.result->fraction<1&&trace.result->hit&&trace.result->collision_plane&&
        trace.result->hit->kind==hlclient::movement::PlayerMovementHitKind::world;};
    if(!eligible(forward)||!eligible(reverse)||forward.result->hit!=reverse.result->hit)return {};
    const auto& a=*forward.result->collision_plane;const auto& b=*reverse.result->collision_plane;
    const auto& na=a.normal;const auto& nb=b.normal;
    const double dot=static_cast<double>(na.x)*nb.x+static_cast<double>(na.y)*nb.y;
    const double determinant=static_cast<double>(na.x)*nb.y-static_cast<double>(na.y)*nb.x;
    if(!finite(na)||!finite(nb)||!std::isfinite(a.distance)||!std::isfinite(b.distance)||
        std::abs(na.z)>.0001||std::abs(nb.z)>.0001||std::abs(dot)>.0001||std::abs(determinant)<.999||
        std::abs(std::hypot(na.x,na.y)-1)>.0001||std::abs(std::hypot(nb.x,nb.y)-1)>.0001)return {};
    const auto side=[](const auto& plane,const auto& p){return static_cast<double>(plane.normal.x)*p.x+
        static_cast<double>(plane.normal.y)*p.y+static_cast<double>(plane.normal.z)*p.z-plane.distance;};
    // Opposite free faces of the same exterior corner. A concave trap or a
    // parallel/opposing-wall passage must not acquire an invented escape.
    if(side(a,from)<0||side(a,to)>=0||side(b,to)<0||side(b,from)>=0)return {};
    assets::AssetVector3 bend{
        static_cast<float>((a.distance*nb.y-b.distance*na.y)/determinant),
        static_cast<float>((na.x*b.distance-nb.x*a.distance)/determinant),std::max(from.z,to.z)};
    if(!finite(bend))return {};
    // Resolve only our computed intersection, using <=4 representable free-
    // side values. No BSP, hull, server origin or global epsilon is changed.
    for(unsigned attempt=0;attempt<4 && !free(bend);++attempt) {
        const auto outward=[](float p,float direction){return direction==0?p:std::nextafter(p,
            std::copysign(std::numeric_limits<float>::infinity(),direction));};
        bend.x=outward(bend.x,na.x+nb.x);bend.y=outward(bend.y,na.y+nb.y);
    }
    if(!free(bend)||!clear(from,bend)||!clear(bend,to))return {};
    const auto supported=[&](const assets::AssetVector3& p){
        auto end=p;end.z-=static_cast<float>(config.ground_probe_distance);
        const auto ground=trace(p,end);
        return ground&&ground.result&&!ground.result->start_solid&&!ground.result->all_solid&&!ground.result->in_liquid&&
            ground.result->fraction<1&&ground.result->hit&&ground.result->hit->kind==hlclient::movement::PlayerMovementHitKind::world&&
            ground.result->collision_plane&&ground.result->collision_plane->normal.z>=config.minimum_walkable_normal_z;
    };
    if(!supported(from)||!supported(bend)||!supported(to))return {};
    const auto first_length=length(from,bend),second_length=length(bend,to),total=first_length+second_length;
    if(!std::isfinite(total)||total<=0||total>maximum_route_length)return {};
    const double along=total*alpha;assets::AssetVector3 sampled;
    const auto interpolate=[](const auto& start,const auto& end,double fraction){return assets::AssetVector3{
        static_cast<float>(start.x+(end.x-start.x)*fraction),static_cast<float>(start.y+(end.y-start.y)*fraction),
        static_cast<float>(start.z+(end.z-start.z)*fraction)};};
    if(along<=first_length && first_length>0)sampled=interpolate(from,bend,along/first_length);
    else sampled=interpolate(bend,to,second_length>0?(along-first_length)/second_length:1.0);
    if(!free(sampled)||!supported(sampled))return {};
    return ReferenceExteriorCornerPresentation{from,bend,to,sampled,total};
}

} // namespace hlclient::goldsrc
