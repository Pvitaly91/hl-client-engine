#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::bsp {

struct GoldSrcWorldspawnParseLimits {
    std::size_t maximum_entity_lump_bytes{128U * 1024U};
    std::size_t maximum_pair_count{256U};
    std::size_t maximum_key_bytes{256U};
    std::size_t maximum_value_bytes{128U * 1024U};
    std::size_t maximum_wad_reference_count{128U};
    std::size_t maximum_wad_basename_bytes{128U};
};

[[nodiscard]] bool valid_goldsrc_worldspawn_parse_limits(
    const GoldSrcWorldspawnParseLimits& limits) noexcept;

struct GoldSrcWadReference {
    std::string basename;
    std::string normalized_basename;
    std::uint32_t declaration_ordinal{0U};
};

class GoldSrcWadReferenceList final {
public:
    explicit GoldSrcWadReferenceList(
        std::vector<GoldSrcWadReference> references) noexcept;

    [[nodiscard]] std::span<const GoldSrcWadReference> references() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    std::vector<GoldSrcWadReference> references_;
};

enum class GoldSrcWorldspawnErrorCode {
    invalid_configuration,
    entity_lump_too_large,
    missing_first_entity,
    unexpected_token,
    unterminated_quote,
    missing_closing_brace,
    nul_in_key_or_value,
    key_length_limit_exceeded,
    value_length_limit_exceeded,
    pair_count_limit_exceeded,
    duplicate_key,
    first_entity_not_worldspawn,
    empty_wad_reference,
    wad_reference_count_limit_exceeded,
    unsafe_wad_basename,
    unsupported_wad_extension,
    unable_to_retain_references,
};

[[nodiscard]] std::string_view to_string(GoldSrcWorldspawnErrorCode code) noexcept;

struct GoldSrcWorldspawnError {
    GoldSrcWorldspawnErrorCode code{
        GoldSrcWorldspawnErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::size_t> element_index;
    // Sanitized metadata-only context. Raw entity values and compiler paths
    // are never retained here.
    std::string context;
};

struct GoldSrcWadReferenceParseResult {
    std::optional<GoldSrcWadReferenceList> references;
    std::optional<GoldSrcWorldspawnError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return references.has_value();
    }
};

class GoldSrcWadReferenceParser final {
public:
    [[nodiscard]] static GoldSrcWadReferenceParseResult parse(
        std::string_view compiler_references,
        const GoldSrcWorldspawnParseLimits& limits = {});
};

class GoldSrcEntityLumpParser final {
public:
    // Parses only the first entity and extracts inert worldspawn metadata. No
    // entity objects, native paths, environment expansion, or escape handling
    // are involved.
    [[nodiscard]] static GoldSrcWadReferenceParseResult
        parse_worldspawn_wad_references(
            std::span<const std::byte> entity_lump,
            const GoldSrcWorldspawnParseLimits& limits = {});
};

} // namespace hlclient::goldsrc::bsp
