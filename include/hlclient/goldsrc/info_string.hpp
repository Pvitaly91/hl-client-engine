#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::size_t kInfoStringHardMaximumKeyLength = 127U;
inline constexpr std::size_t kInfoStringHardMaximumValueLength = 511U;
inline constexpr std::size_t kInfoStringHardMaximumEntryCount = 64U;
inline constexpr std::size_t kInfoStringHardMaximumSerializedLength = 1'023U;

struct InfoStringLimits {
    std::size_t maximum_key_length{63U};
    std::size_t maximum_value_length{127U};
    std::size_t maximum_entry_count{32U};
    std::size_t maximum_serialized_length{255U};
};

struct InfoStringEntry {
    std::string key;
    std::string value;

    [[nodiscard]] friend bool operator==(
        const InfoStringEntry& left,
        const InfoStringEntry& right) = default;
};

enum class InfoStringErrorCode {
    invalid_limits,
    empty_key,
    empty_value,
    invalid_key_character,
    invalid_value_character,
    duplicate_key,
    key_too_long,
    value_too_long,
    too_many_entries,
    serialized_size_exceeded,
    missing_leading_separator,
    missing_value,
    trailing_separator,
};

struct InfoStringError {
    InfoStringErrorCode code{InfoStringErrorCode::invalid_limits};
    std::size_t byte_offset{0U};
    std::string context;
};

class InfoString final {
public:
    [[nodiscard]] std::span<const InfoStringEntry> entries() const noexcept;
    [[nodiscard]] std::string_view serialized() const noexcept;
    [[nodiscard]] std::size_t serialized_size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    InfoString(std::vector<InfoStringEntry> entries, std::string serialized) noexcept;

    std::vector<InfoStringEntry> entries_;
    std::string serialized_;

    friend struct InfoStringFactory;
};

struct InfoStringBuildResult {
    std::optional<InfoString> value;
    std::optional<InfoStringError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value();
    }
};

struct InfoStringParseResult {
    std::optional<InfoString> value;
    std::optional<InfoStringError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value();
    }
};

[[nodiscard]] InfoStringBuildResult build_info_string(
    std::span<const InfoStringEntry> entries,
    const InfoStringLimits& limits);

[[nodiscard]] InfoStringParseResult parse_info_string(
    std::string_view serialized,
    const InfoStringLimits& limits);

[[nodiscard]] constexpr std::string_view to_string(const InfoStringErrorCode code) noexcept
{
    switch (code) {
    case InfoStringErrorCode::invalid_limits:
        return "invalid_limits";
    case InfoStringErrorCode::empty_key:
        return "empty_key";
    case InfoStringErrorCode::empty_value:
        return "empty_value";
    case InfoStringErrorCode::invalid_key_character:
        return "invalid_key_character";
    case InfoStringErrorCode::invalid_value_character:
        return "invalid_value_character";
    case InfoStringErrorCode::duplicate_key:
        return "duplicate_key";
    case InfoStringErrorCode::key_too_long:
        return "key_too_long";
    case InfoStringErrorCode::value_too_long:
        return "value_too_long";
    case InfoStringErrorCode::too_many_entries:
        return "too_many_entries";
    case InfoStringErrorCode::serialized_size_exceeded:
        return "serialized_size_exceeded";
    case InfoStringErrorCode::missing_leading_separator:
        return "missing_leading_separator";
    case InfoStringErrorCode::missing_value:
        return "missing_value";
    case InfoStringErrorCode::trailing_separator:
        return "trailing_separator";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
