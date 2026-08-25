#include <hlclient/goldsrc/visual_assets/goldsrc_studio_companion_names.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace hlclient::goldsrc::visual_assets {
namespace {

inline constexpr std::size_t kCompanionNameDiagnosticTextLimit = 192U;

[[nodiscard]] constexpr char ascii_lower(const char value) noexcept
{
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value + ('a' - 'A'))
               : value;
}

[[nodiscard]] bool ascii_equal_insensitive(
    const std::string_view left,
    const std::string_view right) noexcept
{
    return left.size() == right.size() &&
           std::equal(
               left.begin(),
               left.end(),
               right.begin(),
               [](const char lhs, const char rhs) {
                   return ascii_lower(lhs) == ascii_lower(rhs);
               });
}

[[nodiscard]] GoldSrcStudioCompanionNameResult failure(
    const GoldSrcStudioCompanionNameErrorCode code,
    const std::string_view context) noexcept
{
    try {
        const auto size = (std::min)(
            context.size(), kCompanionNameDiagnosticTextLimit);
        return GoldSrcStudioCompanionNameResult{
            std::nullopt,
            GoldSrcStudioCompanionNameError{
                code, std::string{context.data(), size}}};
    } catch (...) {
        return GoldSrcStudioCompanionNameResult{
            std::nullopt,
            GoldSrcStudioCompanionNameError{
                GoldSrcStudioCompanionNameErrorCode::unable_to_retain_name,
                {}}};
    }
}

struct MainNameParts {
    std::string_view directory_prefix;
    std::string_view stem;
};

[[nodiscard]] std::optional<MainNameParts> split_main_name(
    const local_resources::LocalVirtualResourceName& main_name,
    GoldSrcStudioCompanionNameResult& error) noexcept
{
    const auto value = main_name.value();
    if (value.empty() ||
        value.size() >
            local_resources::kMaximumLocalVirtualResourcePathBytes ||
        main_name.component_count() == 0U ||
        main_name.component_count() >
            local_resources::kMaximumLocalVirtualResourceComponents) {
        error = failure(
            GoldSrcStudioCompanionNameErrorCode::invalid_main_virtual_name,
            "Approved main Studio virtual-name metadata is invalid");
        return std::nullopt;
    }

    const auto slash = value.find_last_of('/');
    const auto component_begin =
        slash == std::string_view::npos ? 0U : slash + 1U;
    const auto component = value.substr(component_begin);
    constexpr std::string_view extension = ".mdl";
    if (component.size() <= extension.size() ||
        !ascii_equal_insensitive(
            component.substr(component.size() - extension.size()),
            extension)) {
        error = failure(
            GoldSrcStudioCompanionNameErrorCode::unsupported_main_extension,
            "Studio companion derivation requires an approved .mdl main name");
        return std::nullopt;
    }

    const auto stem = component.substr(
        0U, component.size() - extension.size());
    if (stem.empty()) {
        error = failure(
            GoldSrcStudioCompanionNameErrorCode::empty_main_stem,
            "Studio main virtual name has an empty stem");
        return std::nullopt;
    }

    return MainNameParts{value.substr(0U, component_begin), stem};
}

[[nodiscard]] GoldSrcStudioCompanionNameResult derive(
    const local_resources::LocalVirtualResourceName& main_name,
    const std::string_view suffix) noexcept
{
    GoldSrcStudioCompanionNameResult error;
    const auto parts = split_main_name(main_name, error);
    if (!parts) {
        return error;
    }

    const auto required_size =
        parts->directory_prefix.size() + parts->stem.size() + suffix.size();
    if (required_size >
        local_resources::kMaximumLocalVirtualResourcePathBytes) {
        return failure(
            GoldSrcStudioCompanionNameErrorCode::derived_name_invalid,
            "Derived Studio companion virtual name exceeds the byte limit");
    }

    try {
        std::string derived;
        derived.reserve(required_size);
        derived.append(parts->directory_prefix);
        derived.append(parts->stem);
        derived.append(suffix);
        auto classified =
            local_resources::LocalVirtualResourceName::create(derived);
        if (!classified || !classified.name) {
            return failure(
                GoldSrcStudioCompanionNameErrorCode::derived_name_invalid,
                "Derived Studio companion virtual name failed safe classification");
        }
        return GoldSrcStudioCompanionNameResult{
            std::move(classified.name), std::nullopt};
    } catch (...) {
        return failure(
            GoldSrcStudioCompanionNameErrorCode::unable_to_retain_name,
            "Unable to retain the bounded Studio companion virtual name");
    }
}

} // namespace

GoldSrcStudioCompanionNameResult
derive_goldsrc_studio_texture_companion_name(
    const local_resources::LocalVirtualResourceName& main_name) noexcept
{
    return derive(main_name, "T.mdl");
}

GoldSrcStudioCompanionNameResult
derive_goldsrc_studio_sequence_group_companion_name(
    const local_resources::LocalVirtualResourceName& main_name,
    const std::uint8_t sequence_group_ordinal) noexcept
{
    if (sequence_group_ordinal <
            kGoldSrcStudioMinimumExternalSequenceGroup ||
        sequence_group_ordinal >
            kGoldSrcStudioMaximumExternalSequenceGroup) {
        return failure(
            GoldSrcStudioCompanionNameErrorCode::sequence_group_out_of_range,
            "Studio sequence-group ordinal is outside the supported 01..15 range");
    }

    const std::array<char, 7U> suffix{
        static_cast<char>('0' + sequence_group_ordinal / 10U),
        static_cast<char>('0' + sequence_group_ordinal % 10U),
        '.',
        'm',
        'd',
        'l',
        '\0'};
    return derive(
        main_name,
        std::string_view{suffix.data(), suffix.size() - 1U});
}

} // namespace hlclient::goldsrc::visual_assets
