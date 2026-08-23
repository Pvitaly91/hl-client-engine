#include <hlclient/goldsrc/local_resource_mapping.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hlclient::goldsrc {
namespace {

struct ValidatedName {
    std::size_t component_count{0U};
};

struct NameValidationResult {
    std::optional<ValidatedName> name;
    GoldSrcResourceNameIssue issue{GoldSrcResourceNameIssue::none};
};

[[nodiscard]] constexpr unsigned char ascii_lower(
    const unsigned char value) noexcept
{
    if (value >= static_cast<unsigned char>('A') &&
        value <= static_cast<unsigned char>('Z')) {
        return static_cast<unsigned char>(
            value + static_cast<unsigned char>('a' - 'A'));
    }
    return value;
}

[[nodiscard]] bool ascii_case_equal(
    const std::string_view left,
    const std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto left_byte = static_cast<unsigned char>(left[index]);
        const auto right_byte = static_cast<unsigned char>(right[index]);
        if (ascii_lower(left_byte) != ascii_lower(right_byte)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool reserved_device_component(
    const std::string_view component) noexcept
{
    const auto dot = component.find('.');
    const auto stem = component.substr(0U, dot);

    if (ascii_case_equal(stem, "con") || ascii_case_equal(stem, "prn") ||
        ascii_case_equal(stem, "aux") || ascii_case_equal(stem, "nul")) {
        return true;
    }

    if (stem.size() != 4U) {
        return false;
    }

    const auto prefix = stem.substr(0U, 3U);
    const char suffix = stem[3U];
    return (ascii_case_equal(prefix, "com") ||
            ascii_case_equal(prefix, "lpt")) &&
           suffix >= '1' && suffix <= '9';
}

[[nodiscard]] bool unc_or_device_path(const std::string_view bytes) noexcept
{
    if (bytes.size() < 2U) {
        return false;
    }

    const bool double_slash = bytes[0U] == '/' && bytes[1U] == '/';
    const bool double_backslash = bytes[0U] == '\\' && bytes[1U] == '\\';
    return double_slash || double_backslash;
}

[[nodiscard]] bool drive_qualified_path(
    const std::string_view bytes) noexcept
{
    if (bytes.size() < 2U || bytes[1U] != ':') {
        return false;
    }

    const unsigned char first = static_cast<unsigned char>(bytes[0U]);
    return (first >= static_cast<unsigned char>('A') &&
            first <= static_cast<unsigned char>('Z')) ||
           (first >= static_cast<unsigned char>('a') &&
            first <= static_cast<unsigned char>('z'));
}

[[nodiscard]] NameValidationResult validate_virtual_name(
    const std::string_view bytes,
    const GoldSrcResourceNameLimits& limits) noexcept
{
    if (bytes.empty()) {
        return {std::nullopt, GoldSrcResourceNameIssue::empty_name};
    }
    if (bytes.size() > limits.maximum_virtual_name_bytes) {
        return {std::nullopt, GoldSrcResourceNameIssue::path_too_long};
    }
    if (unc_or_device_path(bytes)) {
        return {std::nullopt, GoldSrcResourceNameIssue::unc_or_device_path};
    }
    if (bytes.front() == '/' || bytes.front() == '\\') {
        return {
            std::nullopt,
            GoldSrcResourceNameIssue::absolute_or_rooted_path,
        };
    }
    if (drive_qualified_path(bytes)) {
        return {std::nullopt, GoldSrcResourceNameIssue::drive_qualified_path};
    }

    std::size_t component_start = 0U;
    std::size_t component_count = 0U;

    for (std::size_t index = 0U; index <= bytes.size(); ++index) {
        if (index < bytes.size()) {
            const unsigned char value =
                static_cast<unsigned char>(bytes[index]);
            if (value == 0U) {
                return {std::nullopt, GoldSrcResourceNameIssue::embedded_nul};
            }
            if (value < 0x20U) {
                return {std::nullopt, GoldSrcResourceNameIssue::control_byte};
            }
            if (value == 0x7fU) {
                return {std::nullopt, GoldSrcResourceNameIssue::delete_byte};
            }
            if (value >= 0x80U) {
                return {
                    std::nullopt,
                    GoldSrcResourceNameIssue::unsupported_name_encoding,
                };
            }
            if (value == static_cast<unsigned char>('\\')) {
                return {
                    std::nullopt,
                    GoldSrcResourceNameIssue::backslash_separator,
                };
            }
            if (value == static_cast<unsigned char>(':')) {
                return {
                    std::nullopt,
                    GoldSrcResourceNameIssue::alternate_data_stream,
                };
            }
            if (value != static_cast<unsigned char>('/')) {
                continue;
            }
        }

        const auto component = bytes.substr(
            component_start,
            index - component_start);
        if (component.empty()) {
            return {std::nullopt, GoldSrcResourceNameIssue::empty_component};
        }
        if (component.size() > limits.maximum_component_bytes) {
            return {std::nullopt, GoldSrcResourceNameIssue::component_too_long};
        }
        if (component == ".") {
            return {std::nullopt, GoldSrcResourceNameIssue::dot_component};
        }
        if (component == "..") {
            return {std::nullopt, GoldSrcResourceNameIssue::parent_component};
        }
        if (component.back() == '.' || component.back() == ' ') {
            return {
                std::nullopt,
                GoldSrcResourceNameIssue::trailing_dot_or_space,
            };
        }
        if (reserved_device_component(component)) {
            return {
                std::nullopt,
                GoldSrcResourceNameIssue::reserved_device_component,
            };
        }

        ++component_count;
        if (component_count > limits.maximum_components) {
            return {
                std::nullopt,
                GoldSrcResourceNameIssue::too_many_components,
            };
        }
        component_start = index + 1U;
    }

    return {ValidatedName{component_count}, GoldSrcResourceNameIssue::none};
}

} // namespace

bool valid_goldsrc_resource_name_limits(
    const GoldSrcResourceNameLimits& limits) noexcept
{
    return limits.maximum_component_bytes > 0U &&
           limits.maximum_component_bytes <=
               kMaximumLocalResourceComponentBytes &&
           limits.maximum_virtual_name_bytes > 0U &&
           limits.maximum_virtual_name_bytes <=
               kMaximumLocalResourceVirtualNameBytes &&
           limits.maximum_virtual_name_bytes >=
               limits.maximum_component_bytes &&
           limits.maximum_components > 0U &&
           limits.maximum_components <= kMaximumLocalResourceComponents;
}

SafeVirtualResourceName::SafeVirtualResourceName(
    std::string value,
    const std::size_t component_count) noexcept
    : value_{std::move(value)},
      component_count_{component_count}
{
}

std::string_view SafeVirtualResourceName::value() const noexcept
{
    return value_;
}

std::size_t SafeVirtualResourceName::byte_length() const noexcept
{
    return value_.size();
}

std::size_t SafeVirtualResourceName::component_count() const noexcept
{
    return component_count_;
}

GoldSrcResourceNameClassification::GoldSrcResourceNameClassification(
    const GoldSrcResourceNameClassificationKind kind,
    const GoldSrcResourceNameIssue issue,
    const std::size_t original_name_byte_length,
    std::optional<SafeVirtualResourceName> safe_virtual_name) noexcept
    : kind_{kind},
      issue_{issue},
      original_name_byte_length_{original_name_byte_length},
      safe_virtual_name_{std::move(safe_virtual_name)}
{
}

GoldSrcResourceNameClassificationKind
GoldSrcResourceNameClassification::kind() const noexcept
{
    return kind_;
}

GoldSrcResourceNameIssue GoldSrcResourceNameClassification::issue()
    const noexcept
{
    return issue_;
}

std::size_t GoldSrcResourceNameClassification::original_name_byte_length()
    const noexcept
{
    return original_name_byte_length_;
}

const std::optional<SafeVirtualResourceName>&
GoldSrcResourceNameClassification::safe_virtual_name() const noexcept
{
    return safe_virtual_name_;
}

bool GoldSrcResourceNameClassification::file_backed() const noexcept
{
    return kind_ == GoldSrcResourceNameClassificationKind::mapped_file;
}

GoldSrcResourceNameClassifier::GoldSrcResourceNameClassifier(
    const GoldSrcLocalResourceMappingProfile profile,
    const GoldSrcResourceNameLimits limits) noexcept
    : profile_{profile},
      limits_{limits}
{
}

GoldSrcResourceNameClassification GoldSrcResourceNameClassifier::rejected(
    const GoldSrcResourceNameClassificationKind kind,
    const GoldSrcResourceNameIssue issue,
    const std::size_t original_name_byte_length) noexcept
{
    return GoldSrcResourceNameClassification{
        kind,
        issue,
        original_name_byte_length,
        std::nullopt,
    };
}

GoldSrcResourceNameClassification GoldSrcResourceNameClassifier::unsafe(
    const GoldSrcResourceNameIssue issue,
    const std::size_t original_name_byte_length) noexcept
{
    const auto kind =
        issue == GoldSrcResourceNameIssue::unsupported_name_encoding
            ? GoldSrcResourceNameClassificationKind::unsupported_name_encoding
            : GoldSrcResourceNameClassificationKind::unsafe_name;
    return rejected(kind, issue, original_name_byte_length);
}

GoldSrcResourceNameClassification GoldSrcResourceNameClassifier::classify(
    const ResourceType type,
    const std::string_view resource_name_bytes) const
{
    const std::size_t original_length = resource_name_bytes.size();
    if (!valid_goldsrc_resource_name_limits(limits_)) {
        return rejected(
            GoldSrcResourceNameClassificationKind::unsupported_mapping,
            GoldSrcResourceNameIssue::invalid_limits,
            original_length);
    }
    if (profile_ !=
        GoldSrcLocalResourceMappingProfile::stock_protocol_48_standard) {
        return rejected(
            GoldSrcResourceNameClassificationKind::unsupported_mapping,
            GoldSrcResourceNameIssue::unsupported_profile,
            original_length);
    }

    const auto wire_name = validate_virtual_name(resource_name_bytes, limits_);
    if (!wire_name.name) {
        return unsafe(wire_name.issue, original_length);
    }

    if (type == ResourceType::decal) {
        return GoldSrcResourceNameClassification{
            GoldSrcResourceNameClassificationKind::metadata_only,
            GoldSrcResourceNameIssue::none,
            original_length,
            std::nullopt,
        };
    }

    std::string mapped_name;
    switch (type) {
    case ResourceType::sound:
        mapped_name.reserve(6U + resource_name_bytes.size());
        mapped_name.append("sound/");
        mapped_name.append(resource_name_bytes);
        break;
    case ResourceType::model:
    case ResourceType::generic:
    case ResourceType::event_script:
        mapped_name.assign(resource_name_bytes);
        break;
    case ResourceType::decal:
        break;
    default:
        return rejected(
            GoldSrcResourceNameClassificationKind::unsupported_mapping,
            GoldSrcResourceNameIssue::unsupported_resource_type,
            original_length);
    }

    const auto mapped = validate_virtual_name(mapped_name, limits_);
    if (!mapped.name) {
        return unsafe(mapped.issue, original_length);
    }

    return GoldSrcResourceNameClassification{
        GoldSrcResourceNameClassificationKind::mapped_file,
        GoldSrcResourceNameIssue::none,
        original_length,
        SafeVirtualResourceName{
            std::move(mapped_name),
            mapped.name->component_count,
        },
    };
}

GoldSrcResourceNameClassification GoldSrcResourceNameClassifier::classify(
    const ResourceType type,
    const ResourceName& resource_name) const
{
    return classify(type, resource_name.bytes());
}

GoldSrcLocalResourceMappingProfile GoldSrcResourceNameClassifier::profile()
    const noexcept
{
    return profile_;
}

GoldSrcLocalResourceMappingEvidenceProfile
GoldSrcResourceNameClassifier::evidence_profile() const noexcept
{
    return GoldSrcLocalResourceMappingEvidenceProfile::
        repeated_stock_names_installation_layout_and_valve_header_cross_check;
}

const GoldSrcResourceNameLimits& GoldSrcResourceNameClassifier::limits()
    const noexcept
{
    return limits_;
}

} // namespace hlclient::goldsrc
