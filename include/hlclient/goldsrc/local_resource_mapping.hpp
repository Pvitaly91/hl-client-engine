#pragma once

#include <hlclient/goldsrc/resource_list.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::goldsrc {

// Project safety policy for a mapped, forward-slash virtual resource name.
// These are not claims about stock-engine or Win32 path maxima.
inline constexpr std::size_t kDefaultMaximumLocalResourceComponentBytes = 255U;
inline constexpr std::size_t kMaximumLocalResourceComponentBytes = 255U;
inline constexpr std::size_t kDefaultMaximumLocalResourceVirtualNameBytes =
    1'024U;
inline constexpr std::size_t kMaximumLocalResourceVirtualNameBytes = 1'024U;
inline constexpr std::size_t kDefaultMaximumLocalResourceComponents = 64U;
inline constexpr std::size_t kMaximumLocalResourceComponents = 64U;

struct GoldSrcResourceNameLimits {
    std::size_t maximum_component_bytes{
        kDefaultMaximumLocalResourceComponentBytes};
    std::size_t maximum_virtual_name_bytes{
        kDefaultMaximumLocalResourceVirtualNameBytes};
    std::size_t maximum_components{kDefaultMaximumLocalResourceComponents};
};

[[nodiscard]] bool valid_goldsrc_resource_name_limits(
    const GoldSrcResourceNameLimits& limits) noexcept;

enum class GoldSrcLocalResourceMappingProfile {
    stock_protocol_48_standard,
};

enum class GoldSrcLocalResourceMappingEvidenceProfile {
    repeated_stock_names_installation_layout_and_valve_header_cross_check,
};

enum class GoldSrcResourceNameClassificationKind {
    mapped_file,
    metadata_only,
    unsafe_name,
    unsupported_name_encoding,
    unsupported_mapping,
};

enum class GoldSrcResourceNameIssue {
    none,
    empty_name,
    embedded_nul,
    absolute_or_rooted_path,
    drive_qualified_path,
    unc_or_device_path,
    dot_component,
    parent_component,
    empty_component,
    alternate_data_stream,
    backslash_separator,
    control_byte,
    delete_byte,
    trailing_dot_or_space,
    reserved_device_component,
    component_too_long,
    path_too_long,
    too_many_components,
    unsupported_name_encoding,
    unsupported_resource_type,
    unsupported_profile,
    invalid_limits,
};

class SafeVirtualResourceName final {
public:
    SafeVirtualResourceName(const SafeVirtualResourceName&) = default;
    SafeVirtualResourceName& operator=(const SafeVirtualResourceName&) = delete;
    SafeVirtualResourceName(SafeVirtualResourceName&&) noexcept = default;
    SafeVirtualResourceName& operator=(SafeVirtualResourceName&&) noexcept =
        delete;
    ~SafeVirtualResourceName() = default;

    [[nodiscard]] std::string_view value() const noexcept;
    [[nodiscard]] std::size_t byte_length() const noexcept;
    [[nodiscard]] std::size_t component_count() const noexcept;

private:
    friend class GoldSrcResourceNameClassifier;

    SafeVirtualResourceName(
        std::string value,
        std::size_t component_count) noexcept;

    std::string value_;
    std::size_t component_count_{0U};
};

class GoldSrcResourceNameClassification final {
public:
    GoldSrcResourceNameClassification(
        const GoldSrcResourceNameClassification&) = default;
    GoldSrcResourceNameClassification& operator=(
        const GoldSrcResourceNameClassification&) = delete;
    GoldSrcResourceNameClassification(
        GoldSrcResourceNameClassification&&) noexcept = default;
    GoldSrcResourceNameClassification& operator=(
        GoldSrcResourceNameClassification&&) noexcept = delete;
    ~GoldSrcResourceNameClassification() = default;

    [[nodiscard]] GoldSrcResourceNameClassificationKind kind() const noexcept;
    [[nodiscard]] GoldSrcResourceNameIssue issue() const noexcept;
    [[nodiscard]] std::size_t original_name_byte_length() const noexcept;
    [[nodiscard]] const std::optional<SafeVirtualResourceName>&
    safe_virtual_name() const noexcept;
    [[nodiscard]] bool file_backed() const noexcept;

private:
    friend class GoldSrcResourceNameClassifier;

    GoldSrcResourceNameClassification(
        GoldSrcResourceNameClassificationKind kind,
        GoldSrcResourceNameIssue issue,
        std::size_t original_name_byte_length,
        std::optional<SafeVirtualResourceName> safe_virtual_name) noexcept;

    GoldSrcResourceNameClassificationKind kind_{
        GoldSrcResourceNameClassificationKind::unsupported_mapping};
    GoldSrcResourceNameIssue issue_{
        GoldSrcResourceNameIssue::unsupported_profile};
    std::size_t original_name_byte_length_{0U};
    std::optional<SafeVirtualResourceName> safe_virtual_name_;
};

class GoldSrcResourceNameClassifier final {
public:
    explicit GoldSrcResourceNameClassifier(
        GoldSrcLocalResourceMappingProfile profile =
            GoldSrcLocalResourceMappingProfile::stock_protocol_48_standard,
        GoldSrcResourceNameLimits limits = {}) noexcept;

    [[nodiscard]] GoldSrcResourceNameClassification classify(
        ResourceType type,
        std::string_view resource_name_bytes) const;
    [[nodiscard]] GoldSrcResourceNameClassification classify(
        ResourceType type,
        const ResourceName& resource_name) const;

    [[nodiscard]] GoldSrcLocalResourceMappingProfile profile() const noexcept;
    [[nodiscard]] GoldSrcLocalResourceMappingEvidenceProfile evidence_profile()
        const noexcept;
    [[nodiscard]] const GoldSrcResourceNameLimits& limits() const noexcept;

private:
    [[nodiscard]] static GoldSrcResourceNameClassification rejected(
        GoldSrcResourceNameClassificationKind kind,
        GoldSrcResourceNameIssue issue,
        std::size_t original_name_byte_length) noexcept;
    [[nodiscard]] static GoldSrcResourceNameClassification unsafe(
        GoldSrcResourceNameIssue issue,
        std::size_t original_name_byte_length) noexcept;

    GoldSrcLocalResourceMappingProfile profile_;
    GoldSrcResourceNameLimits limits_;
};

using GoldSrcResourceNameMapper = GoldSrcResourceNameClassifier;

[[nodiscard]] constexpr std::string_view to_string(
    const GoldSrcResourceNameClassificationKind kind) noexcept
{
    switch (kind) {
    case GoldSrcResourceNameClassificationKind::mapped_file:
        return "mapped_file";
    case GoldSrcResourceNameClassificationKind::metadata_only:
        return "metadata_only";
    case GoldSrcResourceNameClassificationKind::unsafe_name:
        return "unsafe_name";
    case GoldSrcResourceNameClassificationKind::unsupported_name_encoding:
        return "unsupported_name_encoding";
    case GoldSrcResourceNameClassificationKind::unsupported_mapping:
        return "unsupported_mapping";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const GoldSrcResourceNameIssue issue) noexcept
{
    switch (issue) {
    case GoldSrcResourceNameIssue::none: return "none";
    case GoldSrcResourceNameIssue::empty_name: return "empty_name";
    case GoldSrcResourceNameIssue::embedded_nul: return "embedded_nul";
    case GoldSrcResourceNameIssue::absolute_or_rooted_path:
        return "absolute_or_rooted_path";
    case GoldSrcResourceNameIssue::drive_qualified_path:
        return "drive_qualified_path";
    case GoldSrcResourceNameIssue::unc_or_device_path:
        return "unc_or_device_path";
    case GoldSrcResourceNameIssue::dot_component: return "dot_component";
    case GoldSrcResourceNameIssue::parent_component:
        return "parent_component";
    case GoldSrcResourceNameIssue::empty_component:
        return "empty_component";
    case GoldSrcResourceNameIssue::alternate_data_stream:
        return "alternate_data_stream";
    case GoldSrcResourceNameIssue::backslash_separator:
        return "backslash_separator";
    case GoldSrcResourceNameIssue::control_byte: return "control_byte";
    case GoldSrcResourceNameIssue::delete_byte: return "delete_byte";
    case GoldSrcResourceNameIssue::trailing_dot_or_space:
        return "trailing_dot_or_space";
    case GoldSrcResourceNameIssue::reserved_device_component:
        return "reserved_device_component";
    case GoldSrcResourceNameIssue::component_too_long:
        return "component_too_long";
    case GoldSrcResourceNameIssue::path_too_long: return "path_too_long";
    case GoldSrcResourceNameIssue::too_many_components:
        return "too_many_components";
    case GoldSrcResourceNameIssue::unsupported_name_encoding:
        return "unsupported_name_encoding";
    case GoldSrcResourceNameIssue::unsupported_resource_type:
        return "unsupported_resource_type";
    case GoldSrcResourceNameIssue::unsupported_profile:
        return "unsupported_profile";
    case GoldSrcResourceNameIssue::invalid_limits: return "invalid_limits";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
