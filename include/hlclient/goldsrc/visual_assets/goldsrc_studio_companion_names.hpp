#pragma once

#include <hlclient/local_resources/local_virtual_resource_name.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::goldsrc::visual_assets {

inline constexpr std::uint8_t kGoldSrcStudioMinimumExternalSequenceGroup = 1U;
inline constexpr std::uint8_t kGoldSrcStudioMaximumExternalSequenceGroup = 15U;

enum class GoldSrcStudioCompanionNameErrorCode {
    invalid_main_virtual_name,
    unsupported_main_extension,
    empty_main_stem,
    sequence_group_out_of_range,
    derived_name_invalid,
    unable_to_retain_name,
};

struct GoldSrcStudioCompanionNameError {
    GoldSrcStudioCompanionNameErrorCode code{
        GoldSrcStudioCompanionNameErrorCode::invalid_main_virtual_name};
    // Bounded metadata-only context. It never contains the virtual name.
    std::string context;
};

struct GoldSrcStudioCompanionNameResult {
    std::optional<local_resources::LocalVirtualResourceName> name;
    std::optional<GoldSrcStudioCompanionNameError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return name.has_value();
    }
};

// Derives only a same-directory, same-stem `<stem>T.mdl` sibling from the
// already classified main virtual name. No header metadata is accepted.
[[nodiscard]] GoldSrcStudioCompanionNameResult
derive_goldsrc_studio_texture_companion_name(
    const local_resources::LocalVirtualResourceName& main_name) noexcept;

// Derives only a same-directory, same-stem `<stem>NN.mdl` sibling, where NN is
// exactly two decimal digits in the supported 01..15 range.
[[nodiscard]] GoldSrcStudioCompanionNameResult
derive_goldsrc_studio_sequence_group_companion_name(
    const local_resources::LocalVirtualResourceName& main_name,
    std::uint8_t sequence_group_ordinal) noexcept;

[[nodiscard]] constexpr std::string_view to_string(
    GoldSrcStudioCompanionNameErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcStudioCompanionNameErrorCode::invalid_main_virtual_name:
        return "invalid_main_virtual_name";
    case GoldSrcStudioCompanionNameErrorCode::unsupported_main_extension:
        return "unsupported_main_extension";
    case GoldSrcStudioCompanionNameErrorCode::empty_main_stem:
        return "empty_main_stem";
    case GoldSrcStudioCompanionNameErrorCode::sequence_group_out_of_range:
        return "sequence_group_out_of_range";
    case GoldSrcStudioCompanionNameErrorCode::derived_name_invalid:
        return "derived_name_invalid";
    case GoldSrcStudioCompanionNameErrorCode::unable_to_retain_name:
        return "unable_to_retain_name";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc::visual_assets
