#pragma once

#include <hlclient/assets/asset_types.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hlclient::goldsrc::brush_models {

enum class GoldSrcSpawnCameraSourceClass {
    info_player_start,
    info_player_deathmatch,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcSpawnCameraSourceClass source_class) noexcept;

enum class GoldSrcSpawnCameraDescriptorStatus {
    supported_diagnostic_initial_pose,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcSpawnCameraDescriptorStatus status) noexcept;

struct GoldSrcSpawnCameraDescriptor {
    GoldSrcSpawnCameraSourceClass source_class{
        GoldSrcSpawnCameraSourceClass::info_player_start};
    std::size_t source_entity_ordinal{0U};
    GoldSrcSpawnCameraDescriptorStatus status{
        GoldSrcSpawnCameraDescriptorStatus::supported_diagnostic_initial_pose};
    assets::AssetVector3 position{};
    // Exact GoldSrc [pitch, yaw, roll] entity values retained in degrees.
    assets::AssetVector3 angles_degrees{};
    // Neutral right-handed camera basis derived from Valve AngleMatrix:
    // local +X is forward and local +Z is up.
    assets::AssetVector3 forward{};
    assets::AssetVector3 target{};
    assets::AssetVector3 up{};
};

struct GoldSrcSpawnCameraStatistics {
    std::uint64_t source_entity_count{0U};
    std::uint64_t supported_class_candidate_count{0U};
    std::uint64_t skipped_ambiguous_metadata_count{0U};
    std::uint64_t skipped_invalid_transform_count{0U};
};

enum class GoldSrcSpawnCameraExtractionStatus {
    selected,
    no_valid_supported_candidate,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcSpawnCameraExtractionStatus status) noexcept;

struct GoldSrcSpawnCameraExtractionResult {
    std::optional<GoldSrcSpawnCameraDescriptor> descriptor;
    GoldSrcSpawnCameraExtractionStatus status{
        GoldSrcSpawnCameraExtractionStatus::no_valid_supported_candidate};
    GoldSrcSpawnCameraStatistics statistics{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return descriptor.has_value();
    }
};

class GoldSrcSpawnCameraExtractor final {
public:
    // Full deterministic scan with no gameplay semantics:
    //   1. first valid info_player_start in source order;
    //   2. otherwise first valid info_player_deathmatch;
    //   3. otherwise no result, so the caller may use its bounds camera.
    //
    // Only classname/origin/angles/angle are interpreted. A supported-class
    // candidate is skipped when any of those interpreted keys is an exact
    // duplicate/ASCII-case collision, when origin is absent/malformed, or
    // when the initial angle transform is invalid. Scanning then continues;
    // unknown duplicate keys remain inert.
    [[nodiscard]] static GoldSrcSpawnCameraExtractionResult extract(
        const bsp::GoldSrcEntityDocument& entity_document) noexcept;
};

} // namespace hlclient::goldsrc::brush_models
