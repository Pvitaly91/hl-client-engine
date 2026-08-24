#include <hlclient/goldsrc/bsp/goldsrc_entity_document.hpp>

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace hlclient::goldsrc::bsp {
namespace {

inline constexpr std::size_t kHardMaximumEntityLumpBytes = 1024U * 1024U;
inline constexpr std::size_t kHardMaximumEntities = 8'192U;
// Defaults implement the M4.4 profile. These wider hard ceilings preserve the
// already-public bounded M4.2 worldspawn configuration when it delegates to
// this one canonical grammar implementation.
inline constexpr std::size_t kHardMaximumPairsPerEntity = 8'192U;
inline constexpr std::size_t kHardMaximumKeyBytes = 4'096U;
inline constexpr std::size_t kHardMaximumValueBytes = 128U * 1024U;
inline constexpr std::size_t kHardMaximumTotalPairs = 131'072U;

[[nodiscard]] bool ascii_whitespace(const unsigned char value) noexcept
{
    return value == 0x20U || (value >= 0x09U && value <= 0x0DU);
}

[[nodiscard]] char lowercase_ascii(const char value) noexcept
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

[[nodiscard]] bool ascii_case_equal(
    const std::string_view left,
    const std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (lowercase_ascii(left[index]) != lowercase_ascii(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] GoldSrcEntityDocumentParseResult fail(
    const GoldSrcEntityDocumentErrorCode code,
    const std::size_t byte_offset,
    const std::optional<std::size_t> entity_index,
    const std::optional<std::size_t> pair_index,
    std::string context)
{
    return {
        std::nullopt,
        GoldSrcEntityDocumentError{
            code,
            byte_offset,
            entity_index,
            pair_index,
            std::move(context),
        },
    };
}

struct QuotedToken {
    std::string value;
    std::size_t begin_offset{0U};
};

struct QuotedTokenResult {
    std::optional<QuotedToken> token;
    std::optional<GoldSrcEntityDocumentErrorCode> error;
    std::size_t error_offset{0U};
};

[[nodiscard]] QuotedTokenResult parse_quoted(
    const std::span<const std::byte> bytes,
    std::size_t& cursor,
    const std::size_t maximum_bytes,
    const bool key)
{
    if (cursor >= bytes.size() || bytes[cursor] != std::byte{'"'}) {
        return {
            std::nullopt,
            GoldSrcEntityDocumentErrorCode::unexpected_token,
            cursor,
        };
    }
    const auto begin = cursor;
    ++cursor;
    std::string value;
    value.reserve(std::min(maximum_bytes, bytes.size() - cursor));
    while (cursor < bytes.size()) {
        const auto value_byte = bytes[cursor];
        if (value_byte == std::byte{'"'}) {
            ++cursor;
            return {QuotedToken{std::move(value), begin}, std::nullopt, 0U};
        }
        if (value_byte == std::byte{0}) {
            return {
                std::nullopt,
                GoldSrcEntityDocumentErrorCode::nul_in_key_or_value,
                cursor,
            };
        }
        if (value.size() == maximum_bytes) {
            return {
                std::nullopt,
                key
                    ? GoldSrcEntityDocumentErrorCode::key_length_limit_exceeded
                    : GoldSrcEntityDocumentErrorCode::value_length_limit_exceeded,
                cursor,
            };
        }
        value.push_back(static_cast<char>(
            std::to_integer<unsigned char>(value_byte)));
        ++cursor;
    }
    return {
        std::nullopt,
        GoldSrcEntityDocumentErrorCode::unterminated_quote,
        begin,
    };
}

} // namespace

bool valid_goldsrc_entity_document_limits(
    const GoldSrcEntityDocumentLimits& limits) noexcept
{
    return limits.maximum_entity_lump_bytes > 0U &&
        limits.maximum_entity_lump_bytes <= kHardMaximumEntityLumpBytes &&
        limits.maximum_entities > 0U &&
        limits.maximum_entities <= kHardMaximumEntities &&
        limits.maximum_pairs_per_entity > 0U &&
        limits.maximum_pairs_per_entity <= kHardMaximumPairsPerEntity &&
        limits.maximum_key_bytes > 0U &&
        limits.maximum_key_bytes <= kHardMaximumKeyBytes &&
        limits.maximum_value_bytes > 0U &&
        limits.maximum_value_bytes <= kHardMaximumValueBytes &&
        limits.maximum_total_pairs > 0U &&
        limits.maximum_total_pairs <= kHardMaximumTotalPairs;
}

GoldSrcEntityRecord::GoldSrcEntityRecord(
    std::vector<GoldSrcEntityPair> ordered_pairs) noexcept
    : ordered_pairs_{std::move(ordered_pairs)}
{
}

std::span<const GoldSrcEntityPair> GoldSrcEntityRecord::pairs() const noexcept
{
    return ordered_pairs_;
}

std::size_t GoldSrcEntityRecord::size() const noexcept
{
    return ordered_pairs_.size();
}

bool GoldSrcEntityRecord::empty() const noexcept
{
    return ordered_pairs_.empty();
}

GoldSrcEntityDocument::GoldSrcEntityDocument(
    std::vector<GoldSrcEntityRecord> ordered_entities,
    const std::size_t total_pair_count) noexcept
    : ordered_entities_{std::move(ordered_entities)},
      total_pair_count_{total_pair_count}
{
}

std::span<const GoldSrcEntityRecord> GoldSrcEntityDocument::entities() const noexcept
{
    return ordered_entities_;
}

std::size_t GoldSrcEntityDocument::size() const noexcept
{
    return ordered_entities_.size();
}

bool GoldSrcEntityDocument::empty() const noexcept
{
    return ordered_entities_.empty();
}

std::size_t GoldSrcEntityDocument::total_pair_count() const noexcept
{
    return total_pair_count_;
}

std::string_view to_string(const GoldSrcEntityDocumentErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcEntityDocumentErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcEntityDocumentErrorCode::entity_lump_too_large:
        return "entity_lump_too_large";
    case GoldSrcEntityDocumentErrorCode::unexpected_token:
        return "unexpected_token";
    case GoldSrcEntityDocumentErrorCode::unterminated_quote:
        return "unterminated_quote";
    case GoldSrcEntityDocumentErrorCode::missing_value: return "missing_value";
    case GoldSrcEntityDocumentErrorCode::missing_closing_brace:
        return "missing_closing_brace";
    case GoldSrcEntityDocumentErrorCode::trailing_bytes_after_nul:
        return "trailing_bytes_after_nul";
    case GoldSrcEntityDocumentErrorCode::nul_in_key_or_value:
        return "nul_in_key_or_value";
    case GoldSrcEntityDocumentErrorCode::key_length_limit_exceeded:
        return "key_length_limit_exceeded";
    case GoldSrcEntityDocumentErrorCode::value_length_limit_exceeded:
        return "value_length_limit_exceeded";
    case GoldSrcEntityDocumentErrorCode::entity_count_limit_exceeded:
        return "entity_count_limit_exceeded";
    case GoldSrcEntityDocumentErrorCode::pair_count_limit_exceeded:
        return "pair_count_limit_exceeded";
    case GoldSrcEntityDocumentErrorCode::total_pair_count_limit_exceeded:
        return "total_pair_count_limit_exceeded";
    case GoldSrcEntityDocumentErrorCode::unable_to_retain_document:
        return "unable_to_retain_document";
    }
    return "unknown";
}

namespace {

[[nodiscard]] GoldSrcEntityDocumentParseResult parse_document(
    const std::span<const std::byte> entity_lump,
    const GoldSrcEntityDocumentLimits& limits,
    const bool first_entity_only)
{
    if (!valid_goldsrc_entity_document_limits(limits)) {
        return fail(GoldSrcEntityDocumentErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            std::nullopt,
            "Entity-document limits are outside the supported bounded profile");
    }
    if (entity_lump.size() > limits.maximum_entity_lump_bytes) {
        return fail(GoldSrcEntityDocumentErrorCode::entity_lump_too_large,
            entity_lump.size(),
            std::nullopt,
            std::nullopt,
            "Entity lump exceeds the configured byte limit");
    }

    std::size_t cursor = 0U;
    std::size_t total_pairs = 0U;
    std::vector<GoldSrcEntityRecord> entities;
    const auto skip_whitespace = [&entity_lump, &cursor]() {
        while (cursor < entity_lump.size() && ascii_whitespace(
            std::to_integer<unsigned char>(entity_lump[cursor]))) {
            ++cursor;
        }
    };

    try {
        entities.reserve(std::min(limits.maximum_entities,
            static_cast<std::size_t>(128U)));
        while (true) {
            skip_whitespace();
            if (cursor == entity_lump.size()) {
                break;
            }
            if (entity_lump[cursor] == std::byte{0}) {
                if (cursor + 1U != entity_lump.size()) {
                    return fail(
                        GoldSrcEntityDocumentErrorCode::trailing_bytes_after_nul,
                        cursor + 1U,
                        std::nullopt,
                        std::nullopt,
                        "Only one final compiler NUL is accepted");
                }
                break;
            }
            if (entity_lump[cursor] != std::byte{'{'}) {
                return fail(GoldSrcEntityDocumentErrorCode::unexpected_token,
                    cursor,
                    entities.size(),
                    std::nullopt,
                    "Expected an entity opening brace");
            }
            if (entities.size() == limits.maximum_entities) {
                return fail(
                    GoldSrcEntityDocumentErrorCode::entity_count_limit_exceeded,
                    cursor,
                    entities.size(),
                    std::nullopt,
                    "Entity count exceeds the configured limit");
            }
            ++cursor;

            std::vector<GoldSrcEntityPair> pairs;
            pairs.reserve(std::min(limits.maximum_pairs_per_entity,
                static_cast<std::size_t>(32U)));
            const auto entity_index = entities.size();
            while (true) {
                skip_whitespace();
                if (cursor == entity_lump.size() ||
                    entity_lump[cursor] == std::byte{0}) {
                    return fail(
                        GoldSrcEntityDocumentErrorCode::missing_closing_brace,
                        cursor,
                        entity_index,
                        pairs.size(),
                        "Entity does not close before the lump boundary");
                }
                if (entity_lump[cursor] == std::byte{'}'}) {
                    ++cursor;
                    break;
                }
                if (pairs.size() == limits.maximum_pairs_per_entity) {
                    return fail(
                        GoldSrcEntityDocumentErrorCode::pair_count_limit_exceeded,
                        cursor,
                        entity_index,
                        pairs.size(),
                        "Entity pair count exceeds the configured limit");
                }
                if (total_pairs == limits.maximum_total_pairs) {
                    return fail(
                        GoldSrcEntityDocumentErrorCode::total_pair_count_limit_exceeded,
                        cursor,
                        entity_index,
                        pairs.size(),
                        "Document pair count exceeds the configured limit");
                }

                auto key = parse_quoted(entity_lump,
                    cursor,
                    limits.maximum_key_bytes,
                    true);
                if (!key.token) {
                    return fail(*key.error,
                        key.error_offset,
                        entity_index,
                        pairs.size(),
                        "Entity key is not a bounded quoted token");
                }
                skip_whitespace();
                if (cursor == entity_lump.size() ||
                    entity_lump[cursor] == std::byte{'}'} ||
                    entity_lump[cursor] == std::byte{0}) {
                    return fail(GoldSrcEntityDocumentErrorCode::missing_value,
                        cursor,
                        entity_index,
                        pairs.size(),
                        "Entity key has no quoted value");
                }
                auto value = parse_quoted(entity_lump,
                    cursor,
                    limits.maximum_value_bytes,
                    false);
                if (!value.token) {
                    const auto code = *value.error ==
                            GoldSrcEntityDocumentErrorCode::unexpected_token
                        ? GoldSrcEntityDocumentErrorCode::missing_value
                        : *value.error;
                    return fail(code,
                        value.error_offset,
                        entity_index,
                        pairs.size(),
                        "Entity value is not a bounded quoted token");
                }
                pairs.push_back(GoldSrcEntityPair{
                    std::move(key.token->value),
                    std::move(value.token->value),
                    key.token->begin_offset,
                    value.token->begin_offset,
                });
                ++total_pairs;
            }
            entities.emplace_back(std::move(pairs));
            if (first_entity_only) {
                break;
            }
        }
    } catch (const std::bad_alloc&) {
        return fail(GoldSrcEntityDocumentErrorCode::unable_to_retain_document,
            cursor,
            entities.size(),
            std::nullopt,
            "Unable to retain bounded inert entity metadata");
    } catch (const std::length_error&) {
        return fail(GoldSrcEntityDocumentErrorCode::unable_to_retain_document,
            cursor,
            entities.size(),
            std::nullopt,
            "Entity metadata exceeds an owning-container limit");
    }

    return {
        GoldSrcEntityDocument{std::move(entities), total_pairs},
        std::nullopt,
    };
}

} // namespace

GoldSrcEntityDocumentParseResult GoldSrcEntityDocumentParser::parse(
    const std::span<const std::byte> entity_lump,
    const GoldSrcEntityDocumentLimits& limits)
{
    return parse_document(entity_lump, limits, false);
}

GoldSrcEntityDocumentParseResult GoldSrcEntityDocumentParser::parse_first_entity(
    const std::span<const std::byte> entity_lump,
    const GoldSrcEntityDocumentLimits& limits)
{
    return parse_document(entity_lump, limits, true);
}

std::string_view canonical_key(const GoldSrcInterpretedEntityKey key) noexcept
{
    switch (key) {
    case GoldSrcInterpretedEntityKey::classname: return "classname";
    case GoldSrcInterpretedEntityKey::model: return "model";
    case GoldSrcInterpretedEntityKey::origin: return "origin";
    case GoldSrcInterpretedEntityKey::angles: return "angles";
    case GoldSrcInterpretedEntityKey::angle: return "angle";
    case GoldSrcInterpretedEntityKey::rendermode: return "rendermode";
    case GoldSrcInterpretedEntityKey::renderamt: return "renderamt";
    }
    return {};
}

std::string_view to_string(const GoldSrcInterpretedKeyStatus status) noexcept
{
    switch (status) {
    case GoldSrcInterpretedKeyStatus::absent: return "absent";
    case GoldSrcInterpretedKeyStatus::unique: return "unique";
    case GoldSrcInterpretedKeyStatus::exact_duplicate:
        return "exact_duplicate";
    case GoldSrcInterpretedKeyStatus::ascii_case_collision:
        return "ascii_case_collision";
    }
    return "unknown";
}

const GoldSrcEntityPair* GoldSrcInterpretedKeyLookup::unique_pair(
    const GoldSrcEntityRecord& entity) const noexcept
{
    if (status != GoldSrcInterpretedKeyStatus::unique || !first_pair_index ||
        *first_pair_index >= entity.pairs().size()) {
        return nullptr;
    }
    return &entity.pairs()[*first_pair_index];
}

GoldSrcInterpretedKeyLookup find_interpreted_key(
    const GoldSrcEntityRecord& entity,
    const GoldSrcInterpretedEntityKey key) noexcept
{
    const auto canonical = canonical_key(key);
    GoldSrcInterpretedKeyLookup result;
    for (std::size_t index = 0U; index < entity.pairs().size(); ++index) {
        const auto& pair = entity.pairs()[index];
        if (!ascii_case_equal(pair.key, canonical)) {
            continue;
        }
        if (!result.first_pair_index) {
            result.status = GoldSrcInterpretedKeyStatus::unique;
            result.first_pair_index = index;
            continue;
        }
        result.conflicting_pair_index = index;
        const auto& first_key = entity.pairs()[*result.first_pair_index].key;
        if (pair.key != first_key) {
            result.status = GoldSrcInterpretedKeyStatus::ascii_case_collision;
        } else if (result.status !=
            GoldSrcInterpretedKeyStatus::ascii_case_collision) {
            result.status = GoldSrcInterpretedKeyStatus::exact_duplicate;
        }
    }
    return result;
}

} // namespace hlclient::goldsrc::bsp
