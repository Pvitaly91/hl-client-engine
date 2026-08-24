#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::bsp {

struct GoldSrcEntityDocumentLimits {
    std::size_t maximum_entity_lump_bytes{1024U * 1024U};
    std::size_t maximum_entities{8'192U};
    std::size_t maximum_pairs_per_entity{256U};
    std::size_t maximum_key_bytes{128U};
    std::size_t maximum_value_bytes{4'096U};
    std::size_t maximum_total_pairs{131'072U};
};

[[nodiscard]] bool valid_goldsrc_entity_document_limits(
    const GoldSrcEntityDocumentLimits& limits) noexcept;

struct GoldSrcEntityPair {
    std::string key;
    std::string value;
    std::size_t key_byte_offset{0U};
    std::size_t value_byte_offset{0U};

    [[nodiscard]] friend bool operator==(
        const GoldSrcEntityPair&,
        const GoldSrcEntityPair&) = default;
};

class GoldSrcEntityRecord final {
public:
    explicit GoldSrcEntityRecord(
        std::vector<GoldSrcEntityPair> ordered_pairs) noexcept;

    [[nodiscard]] std::span<const GoldSrcEntityPair> pairs() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    std::vector<GoldSrcEntityPair> ordered_pairs_;
};

class GoldSrcEntityDocument final {
public:
    GoldSrcEntityDocument(
        std::vector<GoldSrcEntityRecord> ordered_entities,
        std::size_t total_pair_count) noexcept;

    [[nodiscard]] std::span<const GoldSrcEntityRecord> entities() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t total_pair_count() const noexcept;

private:
    std::vector<GoldSrcEntityRecord> ordered_entities_;
    std::size_t total_pair_count_{0U};
};

enum class GoldSrcEntityDocumentErrorCode {
    invalid_configuration,
    entity_lump_too_large,
    unexpected_token,
    unterminated_quote,
    missing_value,
    missing_closing_brace,
    trailing_bytes_after_nul,
    nul_in_key_or_value,
    key_length_limit_exceeded,
    value_length_limit_exceeded,
    entity_count_limit_exceeded,
    pair_count_limit_exceeded,
    total_pair_count_limit_exceeded,
    unable_to_retain_document,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcEntityDocumentErrorCode code) noexcept;

struct GoldSrcEntityDocumentError {
    GoldSrcEntityDocumentErrorCode code{
        GoldSrcEntityDocumentErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::size_t> entity_index;
    std::optional<std::size_t> pair_index;
    // Sanitized grammar/limit context only. Raw entity text is never copied
    // into errors or logs by this parser.
    std::string context;
};

struct GoldSrcEntityDocumentParseResult {
    std::optional<GoldSrcEntityDocument> document;
    std::optional<GoldSrcEntityDocumentError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return document.has_value();
    }
};

class GoldSrcEntityDocumentParser final {
public:
    // Parses the complete entity lump as inert, ordered, quoted key/value
    // records. Backslashes are ordinary bytes: no escapes, commands,
    // environment expansion or native-path conversion are performed.
    [[nodiscard]] static GoldSrcEntityDocumentParseResult parse(
        std::span<const std::byte> entity_lump,
        const GoldSrcEntityDocumentLimits& limits = {});

    // Compatibility boundary for consumers that intentionally interpret only
    // worldspawn metadata. The first entity uses the exact same tokenizer and
    // limits as parse(); bytes after its validated closing brace stay inert.
    [[nodiscard]] static GoldSrcEntityDocumentParseResult parse_first_entity(
        std::span<const std::byte> entity_lump,
        const GoldSrcEntityDocumentLimits& limits = {});
};

enum class GoldSrcInterpretedEntityKey {
    classname,
    model,
    origin,
    angles,
    angle,
    rendermode,
    renderamt,
};

[[nodiscard]] std::string_view canonical_key(
    GoldSrcInterpretedEntityKey key) noexcept;

enum class GoldSrcInterpretedKeyStatus {
    absent,
    unique,
    exact_duplicate,
    ascii_case_collision,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcInterpretedKeyStatus status) noexcept;

struct GoldSrcInterpretedKeyLookup {
    GoldSrcInterpretedKeyStatus status{GoldSrcInterpretedKeyStatus::absent};
    std::optional<std::size_t> first_pair_index;
    std::optional<std::size_t> conflicting_pair_index;

    [[nodiscard]] const GoldSrcEntityPair* unique_pair(
        const GoldSrcEntityRecord& entity) const noexcept;
};

// Exact duplicates and ASCII-case collisions are intentionally distinguished.
// Unknown duplicate keys remain inert and do not affect this helper.
[[nodiscard]] GoldSrcInterpretedKeyLookup find_interpreted_key(
    const GoldSrcEntityRecord& entity,
    GoldSrcInterpretedEntityKey key) noexcept;

} // namespace hlclient::goldsrc::bsp
