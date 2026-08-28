#pragma once

#include <hlclient/collision/collision_world_query.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>
#include <hlclient/goldsrc/movement/local_movement_collision.hpp>
#include <hlclient/movement/local_player_movement_state.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::local_player {

inline constexpr std::size_t kDefaultMaximumLocalPlayerSpawnCandidates = 256U;
inline constexpr std::size_t kHardMaximumLocalPlayerSpawnCandidates = 8'192U;

enum class LocalPlayerSpawnSourceClass : std::uint8_t {
    info_player_start,
    info_player_deathmatch,
};

enum class LocalPlayerSpawnSelectionProfile : std::uint8_t {
    project_dry_walk_ordered_candidates_v1,
    stock_player_spawn_semantics_evidence_pending,
};

enum class LocalPlayerSpawnEvidenceProfile : std::uint8_t {
    inert_entity_document_and_deterministic_collision_queries_v1,
    stock_player_spawn_semantics_evidence_pending,
};

[[nodiscard]] std::string_view to_string(
    LocalPlayerSpawnSourceClass source_class) noexcept;
[[nodiscard]] std::string_view to_string(
    LocalPlayerSpawnSelectionProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(
    LocalPlayerSpawnEvidenceProfile profile) noexcept;

struct LocalPlayerSpawnSelectorLimits {
    std::size_t maximum_candidates{
        kDefaultMaximumLocalPlayerSpawnCandidates};
    float maximum_coordinate_magnitude{1'000'000.0F};
};

[[nodiscard]] bool valid_local_player_spawn_selector_limits(
    const LocalPlayerSpawnSelectorLimits& limits) noexcept;

struct LocalPlayerSpawnSelectorConfig {
    LocalPlayerSpawnSelectorLimits limits{};
    goldsrc::movement::LocalMovementCollisionQueryConfig collision_query{};
    movement::PlayerMovementHull hull{movement::PlayerMovementHull::standing};
    LocalPlayerSpawnSelectionProfile selection_profile{
        LocalPlayerSpawnSelectionProfile::
            project_dry_walk_ordered_candidates_v1};
};

[[nodiscard]] bool valid_local_player_spawn_selector_config(
    const LocalPlayerSpawnSelectorConfig& config) noexcept;

struct LocalPlayerSpawnDescriptor {
    LocalPlayerSpawnSourceClass source_class{
        LocalPlayerSpawnSourceClass::info_player_start};
    std::size_t source_entity_ordinal{0U};
    assets::AssetVector3 origin{};
    // Exact parsed GoldSrc entity order: pitch, yaw, roll in degrees.
    assets::AssetVector3 view_angles_degrees{};
    movement::PlayerMovementHull hull{movement::PlayerMovementHull::standing};
    goldsrc::movement::LocalMovementCollisionProfile collision_profile{
        goldsrc::movement::LocalMovementCollisionProfile::world_only_v1};
    LocalPlayerSpawnSelectionProfile selection_profile{
        LocalPlayerSpawnSelectionProfile::
            project_dry_walk_ordered_candidates_v1};
    LocalPlayerSpawnEvidenceProfile evidence_profile{
        LocalPlayerSpawnEvidenceProfile::
            inert_entity_document_and_deterministic_collision_queries_v1};
};

struct LocalPlayerSpawnSelectionStatistics {
    std::uint64_t source_entity_count{0U};
    std::uint64_t supported_class_candidate_count{0U};
    std::uint64_t rejected_ambiguous_metadata_count{0U};
    std::uint64_t rejected_invalid_transform_count{0U};
    std::uint64_t rejected_unsupported_contents_count{0U};
    std::uint64_t rejected_blocking_position_count{0U};
    std::uint64_t point_contents_query_count{0U};
    std::uint64_t position_test_query_count{0U};
};

enum class LocalPlayerSpawnSelectionErrorCode : std::uint8_t {
    invalid_configuration,
    stock_evidence_pending,
    invalid_collision_source,
    candidate_limit_exceeded,
    collision_query_failed,
    no_valid_local_player_spawn,
};

[[nodiscard]] std::string_view to_string(
    LocalPlayerSpawnSelectionErrorCode code) noexcept;

struct LocalPlayerSpawnSelectionError {
    LocalPlayerSpawnSelectionErrorCode code{
        LocalPlayerSpawnSelectionErrorCode::invalid_configuration};
    std::optional<std::size_t> source_entity_ordinal;
    std::optional<goldsrc::movement::LocalMovementCollisionError>
        collision_error;
    std::string_view context;
};

struct LocalPlayerSpawnSelectionResult {
    std::optional<LocalPlayerSpawnDescriptor> descriptor;
    std::optional<LocalPlayerSpawnSelectionError> error;
    LocalPlayerSpawnSelectionStatistics statistics{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return descriptor.has_value() && !error.has_value();
    }
};

class LocalPlayerSpawnSelector final {
public:
    // Deterministic two-pass scan: all info_player_start records in source
    // order, then all info_player_deathmatch records in source order. Invalid,
    // blocking, or non-dry candidates are skipped without stuck nudging.
    [[nodiscard]] static LocalPlayerSpawnSelectionResult select(
        const goldsrc::bsp::GoldSrcEntityDocument& entity_document,
        const goldsrc::movement::ILocalMovementCollision& collision_source,
        hlclient::collision::CollisionQueryScratch& scratch,
        const LocalPlayerSpawnSelectorConfig& config = {});
};

} // namespace hlclient::local_player
