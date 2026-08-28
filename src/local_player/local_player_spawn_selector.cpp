#include <hlclient/local_player/local_player_spawn_selector.hpp>

#include <hlclient/goldsrc/bsp/goldsrc_entity_transform.hpp>

#include <array>
#include <cmath>
#include <utility>

namespace hlclient::local_player {
namespace {

using InterpretedKey = goldsrc::bsp::GoldSrcInterpretedEntityKey;
using InterpretedKeyStatus = goldsrc::bsp::GoldSrcInterpretedKeyStatus;

[[nodiscard]] bool ambiguous(
    const goldsrc::bsp::GoldSrcInterpretedKeyLookup& lookup) noexcept
{
    return lookup.status == InterpretedKeyStatus::exact_duplicate ||
        lookup.status == InterpretedKeyStatus::ascii_case_collision;
}

[[nodiscard]] std::optional<LocalPlayerSpawnSourceClass> source_class(
    const std::string_view value) noexcept
{
    if (value == "info_player_start") {
        return LocalPlayerSpawnSourceClass::info_player_start;
    }
    if (value == "info_player_deathmatch") {
        return LocalPlayerSpawnSourceClass::info_player_deathmatch;
    }
    return std::nullopt;
}

[[nodiscard]] bool within_coordinate_limit(
    const assets::AssetVector3& value,
    const float maximum) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z) && std::abs(value.x) <= maximum &&
        std::abs(value.y) <= maximum && std::abs(value.z) <= maximum;
}

[[nodiscard]] bool dry_spawn_contents(
    const movement::PlayerMovementContents contents) noexcept
{
    // Dry-walk spawn selection accepts only ordinary empty space. Solid is
    // rejected independently by the standing-hull position test; liquid,
    // current, sky, and special terminals cannot silently seed dry movement.
    return contents == movement::PlayerMovementContents::empty;
}

[[nodiscard]] LocalPlayerSpawnSelectionResult failure(
    const LocalPlayerSpawnSelectionErrorCode code,
    const std::string_view context,
    const LocalPlayerSpawnSelectionStatistics& statistics,
    const std::optional<std::size_t> source_entity_ordinal = std::nullopt,
    std::optional<goldsrc::movement::LocalMovementCollisionError>
        collision_error = std::nullopt) noexcept
{
    return {
        std::nullopt,
        LocalPlayerSpawnSelectionError{
            code,
            source_entity_ordinal,
            std::move(collision_error),
            context,
        },
        statistics,
    };
}

struct ParsedCandidate {
    assets::AssetVector3 origin{};
    assets::AssetVector3 view_angles_degrees{};
};

[[nodiscard]] std::optional<ParsedCandidate> parse_candidate(
    const goldsrc::bsp::GoldSrcEntityRecord& entity,
    const LocalPlayerSpawnSelectorLimits& limits,
    LocalPlayerSpawnSelectionStatistics& statistics) noexcept
{
    const auto origin = goldsrc::bsp::find_interpreted_key(
        entity, InterpretedKey::origin);
    const auto angles = goldsrc::bsp::find_interpreted_key(
        entity, InterpretedKey::angles);
    const auto angle = goldsrc::bsp::find_interpreted_key(
        entity, InterpretedKey::angle);
    if (ambiguous(origin) || ambiguous(angles) || ambiguous(angle)) {
        ++statistics.rejected_ambiguous_metadata_count;
        return std::nullopt;
    }

    const auto* origin_pair = origin.unique_pair(entity);
    if (origin_pair == nullptr) {
        ++statistics.rejected_invalid_transform_count;
        return std::nullopt;
    }
    const auto parsed_origin =
        goldsrc::bsp::parse_entity_vector3(origin_pair->value);
    if (!parsed_origin || !parsed_origin.value ||
        !within_coordinate_limit(*parsed_origin.value,
            limits.maximum_coordinate_magnitude)) {
        ++statistics.rejected_invalid_transform_count;
        return std::nullopt;
    }

    std::optional<std::string_view> angles_value;
    std::optional<std::string_view> angle_value;
    if (const auto* pair = angles.unique_pair(entity); pair != nullptr) {
        angles_value = pair->value;
    }
    if (const auto* pair = angle.unique_pair(entity); pair != nullptr) {
        angle_value = pair->value;
    }
    const auto parsed_angles = goldsrc::bsp::parse_entity_angles(
        angles_value, angle_value);
    if (!parsed_angles || !parsed_angles.degrees ||
        !within_coordinate_limit(
            *parsed_angles.degrees, limits.maximum_coordinate_magnitude)) {
        ++statistics.rejected_invalid_transform_count;
        return std::nullopt;
    }
    return ParsedCandidate{*parsed_origin.value, *parsed_angles.degrees};
}

} // namespace

std::string_view to_string(
    const LocalPlayerSpawnSourceClass source_class_value) noexcept
{
    switch (source_class_value) {
    case LocalPlayerSpawnSourceClass::info_player_start:
        return "info_player_start";
    case LocalPlayerSpawnSourceClass::info_player_deathmatch:
        return "info_player_deathmatch";
    }
    return "unknown";
}

std::string_view to_string(
    const LocalPlayerSpawnSelectionProfile profile) noexcept
{
    switch (profile) {
    case LocalPlayerSpawnSelectionProfile::
            project_dry_walk_ordered_candidates_v1:
        return "project_dry_walk_ordered_candidates_v1";
    case LocalPlayerSpawnSelectionProfile::
            stock_player_spawn_semantics_evidence_pending:
        return "stock_player_spawn_semantics_evidence_pending";
    }
    return "unknown";
}

std::string_view to_string(
    const LocalPlayerSpawnEvidenceProfile profile) noexcept
{
    switch (profile) {
    case LocalPlayerSpawnEvidenceProfile::
            inert_entity_document_and_deterministic_collision_queries_v1:
        return "inert_entity_document_and_deterministic_collision_queries_v1";
    case LocalPlayerSpawnEvidenceProfile::
            stock_player_spawn_semantics_evidence_pending:
        return "stock_player_spawn_semantics_evidence_pending";
    }
    return "unknown";
}

bool valid_local_player_spawn_selector_limits(
    const LocalPlayerSpawnSelectorLimits& limits) noexcept
{
    return limits.maximum_candidates > 0U &&
        limits.maximum_candidates <= kHardMaximumLocalPlayerSpawnCandidates &&
        std::isfinite(limits.maximum_coordinate_magnitude) &&
        limits.maximum_coordinate_magnitude > 0.0F &&
        limits.maximum_coordinate_magnitude <= 1'000'000.0F;
}

bool valid_local_player_spawn_selector_config(
    const LocalPlayerSpawnSelectorConfig& config) noexcept
{
    return valid_local_player_spawn_selector_limits(config.limits) &&
        goldsrc::movement::valid_local_movement_collision_query_config(
            config.collision_query) &&
        config.hull == movement::PlayerMovementHull::standing &&
        config.selection_profile == LocalPlayerSpawnSelectionProfile::
            project_dry_walk_ordered_candidates_v1;
}

std::string_view to_string(
    const LocalPlayerSpawnSelectionErrorCode code) noexcept
{
    switch (code) {
    case LocalPlayerSpawnSelectionErrorCode::invalid_configuration:
        return "invalid_configuration";
    case LocalPlayerSpawnSelectionErrorCode::stock_evidence_pending:
        return "stock_evidence_pending";
    case LocalPlayerSpawnSelectionErrorCode::invalid_collision_source:
        return "invalid_collision_source";
    case LocalPlayerSpawnSelectionErrorCode::candidate_limit_exceeded:
        return "candidate_limit_exceeded";
    case LocalPlayerSpawnSelectionErrorCode::collision_query_failed:
        return "collision_query_failed";
    case LocalPlayerSpawnSelectionErrorCode::no_valid_local_player_spawn:
        return "no_valid_local_player_spawn";
    }
    return "unknown";
}

LocalPlayerSpawnSelectionResult LocalPlayerSpawnSelector::select(
    const goldsrc::bsp::GoldSrcEntityDocument& entity_document,
    const goldsrc::movement::ILocalMovementCollision& collision_source,
    hlclient::collision::CollisionQueryScratch& scratch,
    const LocalPlayerSpawnSelectorConfig& config)
{
    LocalPlayerSpawnSelectionStatistics statistics;
    statistics.source_entity_count = entity_document.size();
    if (config.selection_profile == LocalPlayerSpawnSelectionProfile::
            stock_player_spawn_semantics_evidence_pending) {
        return failure(LocalPlayerSpawnSelectionErrorCode::stock_evidence_pending,
            "Stock player-spawn semantics remain evidence-pending",
            statistics);
    }
    if (!valid_local_player_spawn_selector_config(config)) {
        return failure(LocalPlayerSpawnSelectionErrorCode::invalid_configuration,
            "Local player spawn-selector configuration is invalid",
            statistics);
    }
    if (!collision_source.valid()) {
        return failure(
            LocalPlayerSpawnSelectionErrorCode::invalid_collision_source,
            "Local player spawn selection requires a valid collision source",
            statistics);
    }

    const auto entities = entity_document.entities();
    for (std::size_t entity_ordinal = 0U;
         entity_ordinal < entities.size(); ++entity_ordinal) {
        const auto classname = goldsrc::bsp::find_interpreted_key(
            entities[entity_ordinal], InterpretedKey::classname);
        const auto& entity = entities[entity_ordinal];
        const auto* pair = classname.unique_pair(entity);
        const auto* supported_pair = pair;
        if (ambiguous(classname) && classname.first_pair_index &&
            *classname.first_pair_index < entity.pairs().size()) {
            supported_pair =
                &entity.pairs()[*classname.first_pair_index];
        }
        if (supported_pair != nullptr && source_class(supported_pair->value)) {
            if (statistics.supported_class_candidate_count ==
                config.limits.maximum_candidates) {
                return failure(
                    LocalPlayerSpawnSelectionErrorCode::
                        candidate_limit_exceeded,
                    "Local player spawn candidate count exceeds its limit",
                    statistics,
                    entity_ordinal);
            }
            ++statistics.supported_class_candidate_count;
            if (ambiguous(classname)) {
                ++statistics.rejected_ambiguous_metadata_count;
            }
        }
    }

    constexpr std::array ordered_classes{
        LocalPlayerSpawnSourceClass::info_player_start,
        LocalPlayerSpawnSourceClass::info_player_deathmatch,
    };
    for (const auto wanted_class : ordered_classes) {
        for (std::size_t entity_ordinal = 0U;
             entity_ordinal < entities.size(); ++entity_ordinal) {
            const auto& entity = entities[entity_ordinal];
            const auto classname = goldsrc::bsp::find_interpreted_key(
                entity, InterpretedKey::classname);
            if (ambiguous(classname)) {
                continue;
            }
            const auto* classname_pair = classname.unique_pair(entity);
            if (classname_pair == nullptr ||
                source_class(classname_pair->value) != wanted_class) {
                continue;
            }

            const auto parsed = parse_candidate(
                entity, config.limits, statistics);
            if (!parsed) {
                continue;
            }

            ++statistics.point_contents_query_count;
            auto contents = collision_source.point_contents(
                parsed->origin, scratch, config.collision_query);
            if (!contents || !contents.result) {
                return failure(
                    LocalPlayerSpawnSelectionErrorCode::collision_query_failed,
                    "Local player spawn contents query failed",
                    statistics,
                    entity_ordinal,
                    std::move(contents.error));
            }
            if (!dry_spawn_contents(contents.result->contents.category)) {
                ++statistics.rejected_unsupported_contents_count;
                continue;
            }

            ++statistics.position_test_query_count;
            auto position = collision_source.test_position(
                parsed->origin, config.hull, scratch, config.collision_query);
            if (!position || !position.result) {
                return failure(
                    LocalPlayerSpawnSelectionErrorCode::collision_query_failed,
                    "Local player spawn standing-hull query failed",
                    statistics,
                    entity_ordinal,
                    std::move(position.error));
            }
            if (position.result->status !=
                goldsrc::movement::LocalMovementPositionStatus::free) {
                ++statistics.rejected_blocking_position_count;
                continue;
            }

            LocalPlayerSpawnSelectionResult selected;
            selected.descriptor = LocalPlayerSpawnDescriptor{
                wanted_class,
                entity_ordinal,
                parsed->origin,
                parsed->view_angles_degrees,
                config.hull,
                collision_source.profile(),
                config.selection_profile,
                LocalPlayerSpawnEvidenceProfile::
                    inert_entity_document_and_deterministic_collision_queries_v1,
            };
            selected.statistics = statistics;
            return selected;
        }
    }
    return failure(
        LocalPlayerSpawnSelectionErrorCode::no_valid_local_player_spawn,
        "No collision-valid dry local player spawn is available",
        statistics);
}

} // namespace hlclient::local_player
