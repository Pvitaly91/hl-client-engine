#include <hlclient/goldsrc/bsp/goldsrc_worldspawn_wad_references.hpp>

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::goldsrc::bsp {
namespace {

inline constexpr std::size_t kHardMaximumEntityLumpBytes = 128U * 1024U;
inline constexpr std::size_t kHardMaximumPairCount = 8'192U;
inline constexpr std::size_t kHardMaximumKeyBytes = 4'096U;
inline constexpr std::size_t kHardMaximumValueBytes = 128U * 1024U;
inline constexpr std::size_t kHardMaximumWadReferences = 128U;
inline constexpr std::size_t kHardMaximumWadBasenameBytes = 255U;

[[nodiscard]] bool ascii_whitespace(const char value) noexcept
{
    const auto byte = static_cast<unsigned char>(value);
    return byte == 0x20U || (byte >= 0x09U && byte <= 0x0DU);
}

[[nodiscard]] char uppercase_ascii(const char value) noexcept
{
    return value >= 'a' && value <= 'z'
        ? static_cast<char>(value - 'a' + 'A')
        : value;
}

[[nodiscard]] std::string uppercase_ascii_copy(const std::string_view value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (const auto character : value) {
        normalized.push_back(uppercase_ascii(character));
    }
    return normalized;
}

[[nodiscard]] GoldSrcWadReferenceParseResult fail(
    const GoldSrcWorldspawnErrorCode code,
    const std::size_t byte_offset,
    const std::optional<std::size_t> element_index,
    std::string context)
{
    return GoldSrcWadReferenceParseResult{
        std::nullopt,
        GoldSrcWorldspawnError{
            code,
            byte_offset,
            element_index,
            std::move(context),
        },
    };
}

[[nodiscard]] bool ascii_case_equal(
    const std::string_view left,
    const std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (uppercase_ascii(left[index]) != uppercase_ascii(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_reserved_windows_device(const std::string_view basename) noexcept
{
    const auto dot = basename.find('.');
    const auto device = basename.substr(0U, dot);
    if (ascii_case_equal(device, "CON") || ascii_case_equal(device, "PRN") ||
        ascii_case_equal(device, "AUX") || ascii_case_equal(device, "NUL")) {
        return true;
    }
    if (device.size() == 4U &&
        (ascii_case_equal(device.substr(0U, 3U), "COM") ||
            ascii_case_equal(device.substr(0U, 3U), "LPT")) &&
        device[3U] >= '1' && device[3U] <= '9') {
        return true;
    }
    return false;
}

struct QuotedToken {
    std::string value;
    std::size_t begin_offset{0U};
};

struct QuotedTokenResult {
    std::optional<QuotedToken> token;
    std::optional<GoldSrcWorldspawnError> error;
};

[[nodiscard]] QuotedTokenResult parse_quoted(
    const std::span<const std::byte> entity_lump,
    std::size_t& cursor,
    const std::size_t maximum_bytes,
    const bool key)
{
    if (cursor >= entity_lump.size() || entity_lump[cursor] != std::byte{'"'}) {
        return QuotedTokenResult{
            std::nullopt,
            GoldSrcWorldspawnError{
                GoldSrcWorldspawnErrorCode::unexpected_token,
                cursor,
                std::nullopt,
                "Worldspawn key/value token must begin with a quote",
            },
        };
    }
    const auto begin = cursor;
    ++cursor;
    std::string value;
    try {
        value.reserve(std::min(maximum_bytes, entity_lump.size() - cursor));
        while (cursor < entity_lump.size()) {
            const auto byte = entity_lump[cursor];
            if (byte == std::byte{'"'}) {
                ++cursor;
                return QuotedTokenResult{
                    QuotedToken{std::move(value), begin},
                    std::nullopt,
                };
            }
            if (byte == std::byte{0}) {
                return QuotedTokenResult{
                    std::nullopt,
                    GoldSrcWorldspawnError{
                        GoldSrcWorldspawnErrorCode::nul_in_key_or_value,
                        cursor,
                        std::nullopt,
                        "NUL is not valid inside a worldspawn key or value",
                    },
                };
            }
            if (value.size() == maximum_bytes) {
                return QuotedTokenResult{
                    std::nullopt,
                    GoldSrcWorldspawnError{
                        key
                            ? GoldSrcWorldspawnErrorCode::key_length_limit_exceeded
                            : GoldSrcWorldspawnErrorCode::value_length_limit_exceeded,
                        cursor,
                        std::nullopt,
                        "Worldspawn quoted token exceeds its configured byte limit",
                    },
                };
            }
            value.push_back(static_cast<char>(
                std::to_integer<unsigned char>(byte)));
            ++cursor;
        }
    } catch (const std::bad_alloc&) {
        return QuotedTokenResult{
            std::nullopt,
            GoldSrcWorldspawnError{
                GoldSrcWorldspawnErrorCode::unable_to_retain_references,
                begin,
                std::nullopt,
                "Unable to retain bounded worldspawn token metadata",
            },
        };
    } catch (const std::length_error&) {
        return QuotedTokenResult{
            std::nullopt,
            GoldSrcWorldspawnError{
                GoldSrcWorldspawnErrorCode::unable_to_retain_references,
                begin,
                std::nullopt,
                "Worldspawn token exceeds an owning container limit",
            },
        };
    }
    return QuotedTokenResult{
        std::nullopt,
        GoldSrcWorldspawnError{
            GoldSrcWorldspawnErrorCode::unterminated_quote,
            begin,
            std::nullopt,
            "Worldspawn quoted token is unterminated",
        },
    };
}

} // namespace

bool valid_goldsrc_worldspawn_parse_limits(
    const GoldSrcWorldspawnParseLimits& limits) noexcept
{
    return limits.maximum_entity_lump_bytes > 0U &&
        limits.maximum_entity_lump_bytes <= kHardMaximumEntityLumpBytes &&
        limits.maximum_pair_count > 0U &&
        limits.maximum_pair_count <= kHardMaximumPairCount &&
        limits.maximum_key_bytes > 0U &&
        limits.maximum_key_bytes <= kHardMaximumKeyBytes &&
        limits.maximum_value_bytes > 0U &&
        limits.maximum_value_bytes <= kHardMaximumValueBytes &&
        limits.maximum_wad_reference_count > 0U &&
        limits.maximum_wad_reference_count <= kHardMaximumWadReferences &&
        limits.maximum_wad_basename_bytes >= 5U &&
        limits.maximum_wad_basename_bytes <= kHardMaximumWadBasenameBytes;
}

GoldSrcWadReferenceList::GoldSrcWadReferenceList(
    std::vector<GoldSrcWadReference> references) noexcept
    : references_{std::move(references)}
{
}

std::span<const GoldSrcWadReference> GoldSrcWadReferenceList::references()
    const noexcept
{
    return references_;
}

std::size_t GoldSrcWadReferenceList::size() const noexcept
{
    return references_.size();
}

bool GoldSrcWadReferenceList::empty() const noexcept
{
    return references_.empty();
}

std::string_view to_string(const GoldSrcWorldspawnErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcWorldspawnErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcWorldspawnErrorCode::entity_lump_too_large:
        return "entity_lump_too_large";
    case GoldSrcWorldspawnErrorCode::missing_first_entity:
        return "missing_first_entity";
    case GoldSrcWorldspawnErrorCode::unexpected_token: return "unexpected_token";
    case GoldSrcWorldspawnErrorCode::unterminated_quote:
        return "unterminated_quote";
    case GoldSrcWorldspawnErrorCode::missing_closing_brace:
        return "missing_closing_brace";
    case GoldSrcWorldspawnErrorCode::nul_in_key_or_value:
        return "nul_in_key_or_value";
    case GoldSrcWorldspawnErrorCode::key_length_limit_exceeded:
        return "key_length_limit_exceeded";
    case GoldSrcWorldspawnErrorCode::value_length_limit_exceeded:
        return "value_length_limit_exceeded";
    case GoldSrcWorldspawnErrorCode::pair_count_limit_exceeded:
        return "pair_count_limit_exceeded";
    case GoldSrcWorldspawnErrorCode::duplicate_key: return "duplicate_key";
    case GoldSrcWorldspawnErrorCode::first_entity_not_worldspawn:
        return "first_entity_not_worldspawn";
    case GoldSrcWorldspawnErrorCode::empty_wad_reference:
        return "empty_wad_reference";
    case GoldSrcWorldspawnErrorCode::wad_reference_count_limit_exceeded:
        return "wad_reference_count_limit_exceeded";
    case GoldSrcWorldspawnErrorCode::unsafe_wad_basename:
        return "unsafe_wad_basename";
    case GoldSrcWorldspawnErrorCode::unsupported_wad_extension:
        return "unsupported_wad_extension";
    case GoldSrcWorldspawnErrorCode::unable_to_retain_references:
        return "unable_to_retain_references";
    }
    return "unknown";
}

GoldSrcWadReferenceParseResult GoldSrcWadReferenceParser::parse(
    const std::string_view compiler_references,
    const GoldSrcWorldspawnParseLimits& limits)
{
    if (!valid_goldsrc_worldspawn_parse_limits(limits)) {
        return fail(GoldSrcWorldspawnErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            "Worldspawn parser limits are outside the supported bounded profile");
    }

    std::vector<GoldSrcWadReference> references;
    try {
        references.reserve(std::min(limits.maximum_wad_reference_count,
            static_cast<std::size_t>(16U)));
        std::size_t segment_begin = 0U;
        std::size_t declaration_ordinal = 0U;
        while (segment_begin <= compiler_references.size()) {
            const auto delimiter = compiler_references.find(';', segment_begin);
            const auto segment_end = delimiter == std::string_view::npos
                ? compiler_references.size()
                : delimiter;
            auto segment = compiler_references.substr(
                segment_begin, segment_end - segment_begin);
            while (!segment.empty() && ascii_whitespace(segment.front())) {
                segment.remove_prefix(1U);
            }
            while (!segment.empty() && ascii_whitespace(segment.back())) {
                segment.remove_suffix(1U);
            }
            const bool final_segment = delimiter == std::string_view::npos;
            if (segment.empty()) {
                if (final_segment && segment_begin != 0U &&
                    !compiler_references.empty() &&
                    compiler_references.rfind(';') != std::string_view::npos) {
                    break;
                }
                return fail(GoldSrcWorldspawnErrorCode::empty_wad_reference,
                    segment_begin,
                    declaration_ordinal,
                    "WAD declarations permit only one trailing empty segment");
            }
            if (declaration_ordinal >= limits.maximum_wad_reference_count) {
                return fail(
                    GoldSrcWorldspawnErrorCode::wad_reference_count_limit_exceeded,
                    segment_begin,
                    declaration_ordinal,
                    "WAD declaration count exceeds the configured limit");
            }
            const auto separator = segment.find_last_of("/\\");
            const auto basename = separator == std::string_view::npos
                ? segment
                : segment.substr(separator + 1U);
            if (basename.empty() || basename == "." || basename == ".." ||
                basename.size() > limits.maximum_wad_basename_bytes ||
                basename.back() == '.' || basename.back() == ' ' ||
                is_reserved_windows_device(basename)) {
                return fail(GoldSrcWorldspawnErrorCode::unsafe_wad_basename,
                    segment_begin,
                    declaration_ordinal,
                    "Compiler WAD reference does not yield a safe basename");
            }
            for (const auto character : basename) {
                const auto byte = static_cast<unsigned char>(character);
                if (byte < 0x20U || byte == 0x7FU || byte > 0x7FU ||
                    character == '/' || character == '\\' || character == ':') {
                    return fail(GoldSrcWorldspawnErrorCode::unsafe_wad_basename,
                        segment_begin,
                        declaration_ordinal,
                        "Compiler WAD reference does not yield an ASCII-safe basename");
                }
            }
            if (basename.size() < 4U ||
                !ascii_case_equal(basename.substr(basename.size() - 4U), ".wad")) {
                return fail(GoldSrcWorldspawnErrorCode::unsupported_wad_extension,
                    segment_begin,
                    declaration_ordinal,
                    "Approved WAD basenames require the .wad extension");
            }
            auto normalized = uppercase_ascii_copy(basename);
            const auto duplicate = std::find_if(references.begin(), references.end(),
                [&normalized](const GoldSrcWadReference& existing) {
                    return existing.normalized_basename == normalized;
                });
            if (duplicate == references.end()) {
                references.push_back(GoldSrcWadReference{
                    std::string{basename},
                    std::move(normalized),
                    static_cast<std::uint32_t>(declaration_ordinal),
                });
            }
            ++declaration_ordinal;
            if (final_segment) {
                break;
            }
            segment_begin = delimiter + 1U;
        }
    } catch (const std::bad_alloc&) {
        return fail(GoldSrcWorldspawnErrorCode::unable_to_retain_references,
            0U,
            std::nullopt,
            "Unable to retain bounded approved WAD basename metadata");
    } catch (const std::length_error&) {
        return fail(GoldSrcWorldspawnErrorCode::unable_to_retain_references,
            0U,
            std::nullopt,
            "Approved WAD basename metadata exceeds an owning container limit");
    }
    return GoldSrcWadReferenceParseResult{
        GoldSrcWadReferenceList{std::move(references)},
        std::nullopt,
    };
}

GoldSrcWadReferenceParseResult
GoldSrcEntityLumpParser::parse_worldspawn_wad_references(
    const std::span<const std::byte> entity_lump,
    const GoldSrcWorldspawnParseLimits& limits)
{
    if (!valid_goldsrc_worldspawn_parse_limits(limits)) {
        return fail(GoldSrcWorldspawnErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            "Worldspawn parser limits are outside the supported bounded profile");
    }
    if (entity_lump.size() > limits.maximum_entity_lump_bytes) {
        return fail(GoldSrcWorldspawnErrorCode::entity_lump_too_large,
            entity_lump.size(),
            std::nullopt,
            "Entity lump exceeds the configured inert-parser limit");
    }

    std::size_t cursor = 0U;
    const auto skip_whitespace = [&entity_lump, &cursor]() {
        while (cursor < entity_lump.size()) {
            const auto byte = std::to_integer<unsigned char>(entity_lump[cursor]);
            if (!ascii_whitespace(static_cast<char>(byte))) {
                break;
            }
            ++cursor;
        }
    };
    skip_whitespace();
    if (cursor >= entity_lump.size() || entity_lump[cursor] != std::byte{'{'}) {
        return fail(GoldSrcWorldspawnErrorCode::missing_first_entity,
            cursor,
            std::nullopt,
            "Entity lump does not begin with a first entity");
    }
    ++cursor;

    std::vector<std::string> normalized_keys;
    std::optional<std::string> classname;
    std::optional<std::string> preferred_wad;
    std::optional<std::string> fallback_wad;
    std::size_t pair_count = 0U;
    try {
        normalized_keys.reserve(std::min(limits.maximum_pair_count,
            static_cast<std::size_t>(32U)));
        while (true) {
            skip_whitespace();
            if (cursor >= entity_lump.size()) {
                return fail(GoldSrcWorldspawnErrorCode::missing_closing_brace,
                    cursor,
                    std::nullopt,
                    "First entity does not close before the entity-lump boundary");
            }
            if (entity_lump[cursor] == std::byte{'}'}) {
                ++cursor;
                break;
            }
            if (pair_count == limits.maximum_pair_count) {
                return fail(GoldSrcWorldspawnErrorCode::pair_count_limit_exceeded,
                    cursor,
                    pair_count,
                    "First entity exceeds the configured key/value pair limit");
            }
            auto key = parse_quoted(
                entity_lump, cursor, limits.maximum_key_bytes, true);
            if (!key.token) {
                return GoldSrcWadReferenceParseResult{
                    std::nullopt,
                    std::move(key.error),
                };
            }
            skip_whitespace();
            auto value = parse_quoted(
                entity_lump, cursor, limits.maximum_value_bytes, false);
            if (!value.token) {
                return GoldSrcWadReferenceParseResult{
                    std::nullopt,
                    std::move(value.error),
                };
            }
            auto normalized_key = uppercase_ascii_copy(key.token->value);
            if (std::find(normalized_keys.begin(), normalized_keys.end(),
                    normalized_key) != normalized_keys.end()) {
                return fail(GoldSrcWorldspawnErrorCode::duplicate_key,
                    key.token->begin_offset,
                    pair_count,
                    "Duplicate ASCII-case-insensitive key makes worldspawn ambiguous");
            }
            normalized_keys.push_back(normalized_key);
            if (normalized_key == "CLASSNAME") {
                classname = std::move(value.token->value);
            } else if (normalized_key == "_WAD") {
                preferred_wad = std::move(value.token->value);
            } else if (normalized_key == "WAD") {
                fallback_wad = std::move(value.token->value);
            }
            ++pair_count;
        }
    } catch (const std::bad_alloc&) {
        return fail(GoldSrcWorldspawnErrorCode::unable_to_retain_references,
            cursor,
            std::nullopt,
            "Unable to retain bounded inert worldspawn metadata");
    } catch (const std::length_error&) {
        return fail(GoldSrcWorldspawnErrorCode::unable_to_retain_references,
            cursor,
            std::nullopt,
            "Worldspawn metadata exceeds an owning container limit");
    }

    if (!classname || *classname != "worldspawn") {
        return fail(GoldSrcWorldspawnErrorCode::first_entity_not_worldspawn,
            0U,
            std::nullopt,
            "First entity classname is not exact supported worldspawn");
    }
    if (preferred_wad && !preferred_wad->empty()) {
        return GoldSrcWadReferenceParser::parse(*preferred_wad, limits);
    }
    if (fallback_wad && !fallback_wad->empty()) {
        return GoldSrcWadReferenceParser::parse(*fallback_wad, limits);
    }
    return GoldSrcWadReferenceParseResult{
        GoldSrcWadReferenceList{{}},
        std::nullopt,
    };
}

} // namespace hlclient::goldsrc::bsp
