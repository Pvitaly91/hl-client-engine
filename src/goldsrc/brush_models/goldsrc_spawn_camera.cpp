#include <hlclient/goldsrc/brush_models/goldsrc_spawn_camera.hpp>

#include <hlclient/goldsrc/brush_models/goldsrc_brush_entity.hpp>
#include <hlclient/goldsrc/brush_models/goldsrc_brush_transform.hpp>

#include <cmath>

namespace hlclient::goldsrc::brush_models {
namespace {

[[nodiscard]] bool ambiguous(
    const bsp::GoldSrcInterpretedKeyLookup& lookup) noexcept
{
    return lookup.status == bsp::GoldSrcInterpretedKeyStatus::exact_duplicate ||
        lookup.status == bsp::GoldSrcInterpretedKeyStatus::ascii_case_collision;
}

[[nodiscard]] std::optional<GoldSrcSpawnCameraSourceClass> source_class(
    const std::string_view classname) noexcept
{
    if (classname == "info_player_start") {
        return GoldSrcSpawnCameraSourceClass::info_player_start;
    }
    if (classname == "info_player_deathmatch") {
        return GoldSrcSpawnCameraSourceClass::info_player_deathmatch;
    }
    return std::nullopt;
}

[[nodiscard]] bool finite_vector(const assets::AssetVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

} // namespace

std::string_view to_string(
    const GoldSrcSpawnCameraSourceClass source_class_value) noexcept
{
    switch (source_class_value) {
    case GoldSrcSpawnCameraSourceClass::info_player_start:
        return "info_player_start";
    case GoldSrcSpawnCameraSourceClass::info_player_deathmatch:
        return "info_player_deathmatch";
    }
    return "unknown";
}

std::string_view to_string(
    const GoldSrcSpawnCameraDescriptorStatus status) noexcept
{
    switch (status) {
    case GoldSrcSpawnCameraDescriptorStatus::supported_diagnostic_initial_pose:
        return "supported_diagnostic_initial_pose";
    }
    return "unknown";
}

std::string_view to_string(
    const GoldSrcSpawnCameraExtractionStatus status) noexcept
{
    switch (status) {
    case GoldSrcSpawnCameraExtractionStatus::selected: return "selected";
    case GoldSrcSpawnCameraExtractionStatus::no_valid_supported_candidate:
        return "no_valid_supported_candidate";
    }
    return "unknown";
}

GoldSrcSpawnCameraExtractionResult GoldSrcSpawnCameraExtractor::extract(
    const bsp::GoldSrcEntityDocument& entity_document) noexcept
{
    using Key = bsp::GoldSrcInterpretedEntityKey;
    GoldSrcSpawnCameraExtractionResult output;
    output.statistics.source_entity_count = entity_document.size();
    std::optional<GoldSrcSpawnCameraDescriptor> first_start;
    std::optional<GoldSrcSpawnCameraDescriptor> first_deathmatch;

    const auto entities = entity_document.entities();
    for (std::size_t entity_ordinal = 0U;
         entity_ordinal < entities.size();
         ++entity_ordinal) {
        const auto& entity = entities[entity_ordinal];
        const auto classname = bsp::find_interpreted_key(entity, Key::classname);
        if (classname.status == bsp::GoldSrcInterpretedKeyStatus::absent) {
            continue;
        }
        if (ambiguous(classname)) {
            if (classname.first_pair_index &&
                *classname.first_pair_index < entity.pairs().size() &&
                source_class(entity.pairs()[*classname.first_pair_index].value)) {
                ++output.statistics.supported_class_candidate_count;
                ++output.statistics.skipped_ambiguous_metadata_count;
            }
            continue;
        }
        const auto* classname_pair = classname.unique_pair(entity);
        if (classname_pair == nullptr) {
            continue;
        }
        const auto candidate_class = source_class(classname_pair->value);
        if (!candidate_class) {
            continue;
        }
        ++output.statistics.supported_class_candidate_count;

        const auto origin = bsp::find_interpreted_key(entity, Key::origin);
        const auto angles = bsp::find_interpreted_key(entity, Key::angles);
        const auto angle = bsp::find_interpreted_key(entity, Key::angle);
        if (ambiguous(origin) || ambiguous(angles) || ambiguous(angle)) {
            ++output.statistics.skipped_ambiguous_metadata_count;
            continue;
        }

        const auto* origin_pair = origin.unique_pair(entity);
        if (origin_pair == nullptr) {
            ++output.statistics.skipped_invalid_transform_count;
            continue;
        }
        const auto parsed_origin = parse_entity_vector3(origin_pair->value);
        if (!parsed_origin) {
            ++output.statistics.skipped_invalid_transform_count;
            continue;
        }

        std::optional<std::string_view> angles_value;
        std::optional<std::string_view> angle_value;
        if (const auto* pair = angles.unique_pair(entity); pair != nullptr) {
            angles_value = pair->value;
        }
        if (const auto* pair = angle.unique_pair(entity); pair != nullptr) {
            angle_value = pair->value;
        }
        const auto parsed_angles = parse_entity_angles(angles_value, angle_value);
        if (!parsed_angles) {
            ++output.statistics.skipped_invalid_transform_count;
            continue;
        }
        const auto rotation = make_brush_submodel_transform(
            {}, *parsed_angles.degrees);
        if (!rotation) {
            ++output.statistics.skipped_invalid_transform_count;
            continue;
        }

        const auto forward = transform_brush_normal(
            *rotation.transform, {1.0F, 0.0F, 0.0F});
        const auto up = transform_brush_normal(
            *rotation.transform, {0.0F, 0.0F, 1.0F});
        const assets::AssetVector3 target{
            parsed_origin.value->x + forward.x,
            parsed_origin.value->y + forward.y,
            parsed_origin.value->z + forward.z,
        };
        if (!finite_vector(forward) || !finite_vector(up) ||
            !finite_vector(target)) {
            ++output.statistics.skipped_invalid_transform_count;
            continue;
        }

        const GoldSrcSpawnCameraDescriptor descriptor{
            *candidate_class,
            entity_ordinal,
            GoldSrcSpawnCameraDescriptorStatus::supported_diagnostic_initial_pose,
            *parsed_origin.value,
            *parsed_angles.degrees,
            forward,
            target,
            up,
        };
        if (*candidate_class ==
            GoldSrcSpawnCameraSourceClass::info_player_start) {
            if (!first_start) {
                first_start = descriptor;
            }
        } else if (!first_deathmatch) {
            first_deathmatch = descriptor;
        }
    }

    output.descriptor = first_start ? first_start : first_deathmatch;
    output.status = output.descriptor
        ? GoldSrcSpawnCameraExtractionStatus::selected
        : GoldSrcSpawnCameraExtractionStatus::no_valid_supported_candidate;
    return output;
}

} // namespace hlclient::goldsrc::brush_models
