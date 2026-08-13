#include <hlclient/goldsrc/info_string.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool valid_limits(const InfoStringLimits& limits) noexcept
{
    return limits.maximum_key_length > 0U &&
           limits.maximum_key_length <= kInfoStringHardMaximumKeyLength &&
           limits.maximum_value_length > 0U &&
           limits.maximum_value_length <= kInfoStringHardMaximumValueLength &&
           limits.maximum_entry_count > 0U &&
           limits.maximum_entry_count <= kInfoStringHardMaximumEntryCount &&
           limits.maximum_serialized_length > 0U &&
           limits.maximum_serialized_length <= kInfoStringHardMaximumSerializedLength;
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] char ascii_fold(const char value) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

[[nodiscard]] bool keys_equal(const std::string_view left, const std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (ascii_fold(left[index]) != ascii_fold(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_key_character(const unsigned char value) noexcept
{
    return value >= 0x21U && value <= 0x7eU && value != '\\' && value != '"' && value != ';';
}

[[nodiscard]] bool valid_value_character(const unsigned char value) noexcept
{
    return value >= 0x20U && value <= 0x7eU && value != '\\' && value != '"' && value != ';';
}

[[nodiscard]] std::optional<std::size_t> invalid_character_offset(
    const std::string_view text,
    const bool key) noexcept
{
    for (std::size_t index = 0U; index < text.size(); ++index) {
        const auto value = static_cast<unsigned char>(text[index]);
        if (key ? !valid_key_character(value) : !valid_value_character(value)) {
            return index;
        }
    }
    return std::nullopt;
}

template<class Result>
[[nodiscard]] Result failure(
    const InfoStringErrorCode code,
    const std::size_t byte_offset,
    std::string context)
{
    return Result{
        std::nullopt,
        InfoStringError{code, byte_offset, std::move(context)},
    };
}

[[nodiscard]] std::optional<InfoStringError> validate_entries(
    const std::span<const InfoStringEntry> entries,
    const InfoStringLimits& limits,
    std::size_t& serialized_size)
{
    if (!valid_limits(limits)) {
        return InfoStringError{
            InfoStringErrorCode::invalid_limits,
            0U,
            "Info-string limits exceed the bounded codec profile",
        };
    }
    if (entries.size() > limits.maximum_entry_count) {
        return InfoStringError{
            InfoStringErrorCode::too_many_entries,
            entries.size(),
            "Info string contains too many entries",
        };
    }

    serialized_size = 0U;
    for (std::size_t entry_index = 0U; entry_index < entries.size(); ++entry_index) {
        const auto& entry = entries[entry_index];
        if (entry.key.empty()) {
            return InfoStringError{
                InfoStringErrorCode::empty_key,
                entry_index,
                "Info-string keys must not be empty",
            };
        }
        if (entry.value.empty()) {
            return InfoStringError{
                InfoStringErrorCode::empty_value,
                entry_index,
                "Info-string values must not be empty",
            };
        }
        if (entry.key.size() > limits.maximum_key_length) {
            return InfoStringError{
                InfoStringErrorCode::key_too_long,
                entry_index,
                "Info-string key exceeds the configured bound",
            };
        }
        if (entry.value.size() > limits.maximum_value_length) {
            return InfoStringError{
                InfoStringErrorCode::value_too_long,
                entry_index,
                "Info-string value exceeds the configured bound",
            };
        }
        if (const auto invalid = invalid_character_offset(entry.key, true)) {
            return InfoStringError{
                InfoStringErrorCode::invalid_key_character,
                *invalid,
                "Info-string key contains a forbidden byte",
            };
        }
        if (const auto invalid = invalid_character_offset(entry.value, false)) {
            return InfoStringError{
                InfoStringErrorCode::invalid_value_character,
                *invalid,
                "Info-string value contains a forbidden byte",
            };
        }

        for (std::size_t previous = 0U; previous < entry_index; ++previous) {
            if (keys_equal(entries[previous].key, entry.key)) {
                return InfoStringError{
                    InfoStringErrorCode::duplicate_key,
                    entry_index,
                    "Info-string keys must be unique under ASCII case folding",
                };
            }
        }

        std::size_t entry_size = 2U;
        if (!checked_add(entry_size, entry.key.size(), entry_size) ||
            !checked_add(entry_size, entry.value.size(), entry_size) ||
            !checked_add(serialized_size, entry_size, serialized_size) ||
            serialized_size > limits.maximum_serialized_length) {
            return InfoStringError{
                InfoStringErrorCode::serialized_size_exceeded,
                entry_index,
                "Serialized info string exceeds the configured bound",
            };
        }
    }
    return std::nullopt;
}

} // namespace

struct InfoStringFactory final {
    [[nodiscard]] static InfoString create(
        std::vector<InfoStringEntry> entries,
        std::string serialized) noexcept
    {
        return InfoString{std::move(entries), std::move(serialized)};
    }
};

InfoString::InfoString(std::vector<InfoStringEntry> entries, std::string serialized) noexcept
    : entries_{std::move(entries)}, serialized_{std::move(serialized)}
{
}

std::span<const InfoStringEntry> InfoString::entries() const noexcept
{
    return entries_;
}

std::string_view InfoString::serialized() const noexcept
{
    return serialized_;
}

std::size_t InfoString::serialized_size() const noexcept
{
    return serialized_.size();
}

bool InfoString::empty() const noexcept
{
    return entries_.empty();
}

InfoStringBuildResult build_info_string(
    const std::span<const InfoStringEntry> entries,
    const InfoStringLimits& limits)
{
    std::size_t serialized_size = 0U;
    if (auto error = validate_entries(entries, limits, serialized_size)) {
        return InfoStringBuildResult{std::nullopt, std::move(error)};
    }

    std::vector<InfoStringEntry> owned_entries{entries.begin(), entries.end()};
    std::string serialized;
    serialized.reserve(serialized_size);
    for (const auto& entry : owned_entries) {
        serialized.push_back('\\');
        serialized += entry.key;
        serialized.push_back('\\');
        serialized += entry.value;
    }
    return InfoStringBuildResult{
        InfoStringFactory::create(std::move(owned_entries), std::move(serialized)),
        std::nullopt,
    };
}

InfoStringParseResult parse_info_string(
    const std::string_view serialized,
    const InfoStringLimits& limits)
{
    if (!valid_limits(limits)) {
        return failure<InfoStringParseResult>(
            InfoStringErrorCode::invalid_limits,
            0U,
            "Info-string limits exceed the bounded codec profile");
    }
    if (serialized.size() > limits.maximum_serialized_length) {
        return failure<InfoStringParseResult>(
            InfoStringErrorCode::serialized_size_exceeded,
            limits.maximum_serialized_length,
            "Serialized info string exceeds the configured bound");
    }
    if (serialized.empty()) {
        return InfoStringParseResult{
            InfoStringFactory::create({}, {}),
            std::nullopt,
        };
    }
    if (serialized.front() != '\\') {
        return failure<InfoStringParseResult>(
            InfoStringErrorCode::missing_leading_separator,
            0U,
            "Non-empty info string must begin with a backslash separator");
    }
    if (serialized.size() == 1U) {
        return failure<InfoStringParseResult>(
            InfoStringErrorCode::trailing_separator,
            0U,
            "Info string must not end with a separator");
    }

    std::vector<InfoStringEntry> entries;
    entries.reserve(std::min(limits.maximum_entry_count, serialized.size() / 4U + 1U));
    std::size_t position = 1U;
    while (position < serialized.size()) {
        if (entries.size() >= limits.maximum_entry_count) {
            return failure<InfoStringParseResult>(
                InfoStringErrorCode::too_many_entries,
                position,
                "Info string contains too many entries");
        }

        const auto key_begin = position;
        const auto key_end = serialized.find('\\', key_begin);
        if (key_end == std::string_view::npos) {
            return failure<InfoStringParseResult>(
                InfoStringErrorCode::missing_value,
                serialized.size(),
                "Info-string key has no value separator");
        }
        if (key_end == key_begin) {
            return failure<InfoStringParseResult>(
                InfoStringErrorCode::empty_key,
                key_begin,
                "Info-string keys must not be empty");
        }

        const auto value_begin = key_end + 1U;
        if (value_begin >= serialized.size()) {
            return failure<InfoStringParseResult>(
                InfoStringErrorCode::empty_value,
                value_begin,
                "Info-string values must not be empty");
        }
        const auto next_separator = serialized.find('\\', value_begin);
        const auto value_end = next_separator == std::string_view::npos
                                   ? serialized.size()
                                   : next_separator;
        if (value_end == value_begin) {
            return failure<InfoStringParseResult>(
                InfoStringErrorCode::empty_value,
                value_begin,
                "Info-string values must not be empty");
        }

        entries.push_back(InfoStringEntry{
            std::string{serialized.substr(key_begin, key_end - key_begin)},
            std::string{serialized.substr(value_begin, value_end - value_begin)},
        });
        if (next_separator == std::string_view::npos) {
            position = serialized.size();
        } else {
            if (next_separator + 1U == serialized.size()) {
                return failure<InfoStringParseResult>(
                    InfoStringErrorCode::trailing_separator,
                    next_separator,
                    "Info string must not end with a separator");
            }
            position = next_separator + 1U;
        }
    }

    auto built = build_info_string(entries, limits);
    if (!built) {
        return InfoStringParseResult{std::nullopt, std::move(built.error)};
    }
    return InfoStringParseResult{std::move(built.value), std::nullopt};
}

} // namespace hlclient::goldsrc
