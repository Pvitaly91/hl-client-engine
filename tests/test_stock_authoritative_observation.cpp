#include <hlclient/goldsrc/stock_authoritative_movement.hpp>
#include <hlclient/prediction/stock_authoritative_player_state_adapter.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace prediction = hlclient::prediction;

class FreeCollision final
    : public goldsrc::movement::ILocalMovementCollision {
public:
    FreeCollision()
    {
        identity_.profile =
            goldsrc::movement::LocalMovementCollisionProfile::world_only_v1;
        identity_.collision_world_primary = 1U;
        identity_.collision_world_revision = 1U;
        identity_.scene_signature = 1U;
    }

    [[nodiscard]] goldsrc::movement::LocalMovementCollisionProfile profile()
        const noexcept override
    {
        return identity_.profile;
    }
    [[nodiscard]] bool valid() const noexcept override { return true; }
    [[nodiscard]] std::optional<
        goldsrc::movement::LocalMovementCollisionSessionIdentity>
    session_identity() const noexcept override
    {
        return identity_;
    }
    [[nodiscard]] goldsrc::movement::LocalMovementPointContentsQueryResult
    point_contents(
        const hlclient::assets::AssetVector3&,
        hlclient::collision::CollisionQueryScratch&,
        const goldsrc::movement::LocalMovementCollisionQueryConfig&)
        const override
    {
        goldsrc::movement::LocalMovementPointContents result;
        result.contents.category =
            hlclient::movement::PlayerMovementContents::empty;
        return {result, std::nullopt};
    }
    [[nodiscard]] goldsrc::movement::LocalMovementPositionQueryResult
    test_position(
        const hlclient::assets::AssetVector3&,
        const hlclient::movement::PlayerMovementHull,
        hlclient::collision::CollisionQueryScratch&,
        const goldsrc::movement::LocalMovementCollisionQueryConfig&)
        const override
    {
        goldsrc::movement::LocalMovementPositionTest result;
        result.status = goldsrc::movement::LocalMovementPositionStatus::free;
        result.contents.category =
            hlclient::movement::PlayerMovementContents::empty;
        return {result, std::nullopt};
    }
    [[nodiscard]] goldsrc::movement::LocalMovementTraceQueryResult trace_hull(
        const hlclient::assets::AssetVector3&,
        const hlclient::assets::AssetVector3& end,
        const hlclient::movement::PlayerMovementHull,
        hlclient::collision::CollisionQueryScratch&,
        const goldsrc::movement::LocalMovementCollisionQueryConfig&)
        const override
    {
        goldsrc::movement::LocalMovementTrace result;
        result.end_position = end;
        result.fraction = 1.0;
        result.collision_profile = identity_.profile;
        return {result, std::nullopt};
    }

    [[nodiscard]] const goldsrc::movement::
        LocalMovementCollisionSessionIdentity&
    identity() const noexcept
    {
        return identity_;
    }

private:
    goldsrc::movement::LocalMovementCollisionSessionIdentity identity_;
};

[[nodiscard]] goldsrc::StockRuntimeSourceCursor cursor(
    const std::size_t byte_offset)
{
    const auto created =
        goldsrc::StockRuntimeSourceCursor::create(byte_offset, 0U, 64U);
    REQUIRE(created);
    return *created;
}

[[nodiscard]] goldsrc::StockAuthoritativeFieldProvenance origin_provenance()
{
    goldsrc::StockAuthoritativeFieldProvenance provenance;
    provenance.semantic_target =
        goldsrc::StockAuthoritativeSemanticTarget::origin;
    provenance.source_schema = "entity_state_t";
    provenance.source_field_name = "origin[0..2]";
    provenance.source_kind =
        goldsrc::StockAuthoritativeSourceKind::entity_state;
    provenance.source_message_category =
        goldsrc::StockRuntimeMessageCategory::entity_delta_candidate;
    provenance.source_message_ordinal = 3U;
    provenance.source_start_cursor = cursor(6U);
    provenance.source_end_cursor = cursor(18U);
    return provenance;
}

[[nodiscard]] goldsrc::StockAuthoritativeFieldProvenance hull_provenance()
{
    auto provenance = origin_provenance();
    provenance.semantic_target =
        goldsrc::StockAuthoritativeSemanticTarget::hull;
    provenance.source_field_name = "usehull_candidate";
    provenance.source_start_cursor = cursor(20U);
    provenance.source_end_cursor = cursor(21U);
    return provenance;
}

TEST_CASE("Stock authoritative values require field-level provenance",
    "[goldsrc][stock-runtime][authority]")
{
    goldsrc::StockAuthoritativeMovementCreateInfo info;
    info.runtime_generation = 4U;
    info.update_ordinal = 1U;
    info.local_player_candidate_entity_number = 2U;
    info.values.origin = hlclient::assets::AssetVector3{1.0F, 2.0F, 3.0F};

    const auto rejected =
        goldsrc::StockAuthoritativeMovementObservation::create(info);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        goldsrc::StockAuthoritativeMovementErrorCode::
            missing_field_provenance);

    info.provenance.push_back(origin_provenance());
    const auto retained =
        goldsrc::StockAuthoritativeMovementObservation::create(info);
    REQUIRE(retained);
    CHECK(retained.observation->status() ==
        goldsrc::StockAuthoritativeMovementObservationStatus::
            partial_evidence_pending);
    CHECK(retained.observation->provenance_count_for(
              goldsrc::StockAuthoritativeSemanticTarget::origin) == 1U);
    CHECK_FALSE(retained.observation->complete_candidate_fields());
}

TEST_CASE("Pending stock profile rejects confirmed direct-field claims",
    "[goldsrc][stock-runtime][authority]")
{
    goldsrc::StockAuthoritativeMovementCreateInfo info;
    info.runtime_generation = 4U;
    info.update_ordinal = 2U;
    info.local_player_candidate_entity_number = 2U;
    info.values.origin = hlclient::assets::AssetVector3{};
    auto provenance = origin_provenance();
    provenance.confidence =
        goldsrc::StockAuthoritativeEvidenceConfidence::confirmed_for_profile;
    provenance.support =
        goldsrc::StockAuthoritativeFieldSupport::supported_for_profile;
    info.provenance.push_back(std::move(provenance));

    const auto rejected =
        goldsrc::StockAuthoritativeMovementObservation::create(info);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        goldsrc::StockAuthoritativeMovementErrorCode::stock_evidence_pending);
}

TEST_CASE("Conflicting authoritative hull evidence fails with its typed error",
    "[goldsrc][stock-runtime][authority][hull]")
{
    goldsrc::StockAuthoritativeMovementCreateInfo info;
    info.runtime_generation = 6U;
    info.update_ordinal = 1U;
    info.local_player_candidate_entity_number = 2U;
    info.values.hull = hlclient::movement::PlayerMovementHull::standing;
    auto provenance = hull_provenance();
    provenance.support = goldsrc::StockAuthoritativeFieldSupport::conflicting;
    info.provenance.push_back(std::move(provenance));

    const auto rejected =
        goldsrc::StockAuthoritativeMovementObservation::create(info);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
        goldsrc::StockAuthoritativeMovementErrorCode::
            authoritative_hull_conflict);
    CHECK(rejected.error->semantic_target ==
        goldsrc::StockAuthoritativeSemanticTarget::hull);
}

TEST_CASE("Authoritative observation failure leaves its input unchanged",
    "[goldsrc][stock-runtime][authority]")
{
    goldsrc::StockAuthoritativeMovementCreateInfo info;
    info.runtime_generation = 9U;
    info.update_ordinal = 3U;
    info.local_player_candidate_entity_number = 7U;
    info.values.origin = hlclient::assets::AssetVector3{};
    info.provenance.push_back(origin_provenance());
    info.provenance.push_back(origin_provenance());
    const auto original_size = info.provenance.size();

    const auto rejected =
        goldsrc::StockAuthoritativeMovementObservation::create(info);
    REQUIRE_FALSE(rejected);
    CHECK(info.provenance.size() == original_size);
    CHECK(info.values.origin.has_value());
}

TEST_CASE("Stock adapter validates collision without activating prediction",
    "[goldsrc][stock-runtime][authority][prediction]")
{
    goldsrc::StockAuthoritativeMovementCreateInfo observation_info;
    observation_info.runtime_generation = 12U;
    observation_info.update_ordinal = 1U;
    observation_info.local_player_candidate_entity_number = 2U;
    observation_info.values.origin =
        hlclient::assets::AssetVector3{0.0F, 0.0F, 36.0F};
    observation_info.values.hull =
        hlclient::movement::PlayerMovementHull::standing;
    observation_info.provenance.push_back(origin_provenance());
    observation_info.provenance.push_back(hull_provenance());
    const auto observation =
        goldsrc::StockAuthoritativeMovementObservation::create(
            observation_info);
    REQUIRE(observation);

    goldsrc::StockLocalPlayerIdentityBuilder identity_builder{12U};
    REQUIRE(identity_builder.observe_player_entity_candidate(2U, cursor(22U)));
    const auto identity = identity_builder.publish();
    REQUIRE(identity);

    goldsrc::StockRuntimeFrameCreateInfo frame_info;
    frame_info.runtime_generation = 12U;
    frame_info.frame_ordinal = 1U;
    frame_info.local_player_identity =
        std::make_shared<const goldsrc::StockLocalPlayerIdentityState>(
            *identity.state);
    frame_info.authoritative_observation =
        std::make_shared<const goldsrc::StockAuthoritativeMovementObservation>(
            *observation.observation);
    const auto runtime_frame = goldsrc::StockRuntimeFrameState::create(frame_info);
    REQUIRE(runtime_frame);

    const auto environment = goldsrc::movement::GoldSrcMovementEnvironmentBuilder::
        project_owned_offline_baseline();
    REQUIRE(environment);
    FreeCollision collision;
    const goldsrc::movement::GoldSrcLocalMovementConfig movement_config{};
    prediction::StockAuthoritativeAdapterContext context;
    context.runtime_generation = 12U;
    context.collision_session = collision.identity();
    context.movement_environment_signature =
        prediction::prediction_movement_environment_signature(
            *environment.environment);
    context.movement_config_signature =
        prediction::prediction_movement_config_signature(movement_config);
    hlclient::collision::CollisionQueryScratch scratch;

    const auto projected =
        prediction::StockAuthoritativePlayerStateAdapter::project(
            *runtime_frame.frame, context, *environment.environment, collision,
            scratch, movement_config);
    CHECK(projected.status == prediction::StockAuthoritativeProjectionStatus::
                                  local_player_identity_pending);
    REQUIRE(projected.collision_validation);
    CHECK(projected.collision_validation->status ==
        prediction::StockAuthoritativeCollisionValidationStatus::
            validated_free);
    CHECK_FALSE(projected.authoritative_state);
    CHECK_FALSE(projected.prediction_ready());
}

} // namespace
