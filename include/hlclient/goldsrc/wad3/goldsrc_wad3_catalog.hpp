#pragma once

#include <hlclient/goldsrc/wad3/goldsrc_wad3_format.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc::wad3 {

inline constexpr std::size_t kGoldSrcWad3MaximumDiagnosticContextBytes = 192U;

struct GoldSrcWad3CatalogLimits {
    std::size_t maximum_source_bytes{kGoldSrcWad3DefaultMaximumSourceBytes};
    std::size_t maximum_lump_count{kGoldSrcWad3DefaultMaximumLumpCount};
};

[[nodiscard]] bool valid_goldsrc_wad3_catalog_limits(
    const GoldSrcWad3CatalogLimits& limits) noexcept;

enum class GoldSrcWad3CatalogErrorCode {
    invalid_configuration,
    source_too_small,
    source_limit_exceeded,
    invalid_identification,
    negative_lump_count,
    zero_lump_count,
    lump_count_limit_exceeded,
    negative_directory_offset,
    directory_range_overflow,
    directory_out_of_bounds,
    directory_overlaps_header,
    negative_file_position,
    negative_disk_size,
    negative_uncompressed_size,
    lump_range_overflow,
    lump_out_of_bounds,
    lump_overlaps_header,
    lump_overlaps_directory,
    lump_overlap,
    unsupported_compression,
    uncompressed_size_mismatch,
    nonzero_padding,
    invalid_entry_name,
    ambiguous_texture_name,
    unable_to_retain_catalog,
};

[[nodiscard]] std::string_view to_string(
    GoldSrcWad3CatalogErrorCode code) noexcept;

struct GoldSrcWad3CatalogError {
    GoldSrcWad3CatalogErrorCode code{
        GoldSrcWad3CatalogErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::size_t> directory_ordinal;
    std::string context;
};

enum class GoldSrcWad3CompatibilityProfile {
    wad3_uncompressed_miptex_v1,
};

enum class GoldSrcWad3EvidenceProfile {
    pinned_valve_tools_and_synthetic_fixture,
};

struct GoldSrcWad3Entry {
    std::size_t directory_ordinal{0U};
    std::string source_name;
    std::string normalized_name;
    std::size_t file_offset{0U};
    std::size_t disk_size{0U};
    std::size_t uncompressed_size{0U};
    std::uint8_t type{0U};
    std::uint8_t compression{0U};
    GoldSrcWad3CompatibilityProfile compatibility_profile{
        GoldSrcWad3CompatibilityProfile::wad3_uncompressed_miptex_v1};
    GoldSrcWad3EvidenceProfile evidence_profile{
        GoldSrcWad3EvidenceProfile::pinned_valve_tools_and_synthetic_fixture};

    [[nodiscard]] bool is_miptex() const noexcept
    {
        return type == kGoldSrcWad3MiptexType;
    }
};

class GoldSrcWad3Catalog final {
public:
    GoldSrcWad3Catalog(const GoldSrcWad3Catalog&) = default;
    GoldSrcWad3Catalog(GoldSrcWad3Catalog&&) noexcept = default;
    GoldSrcWad3Catalog& operator=(const GoldSrcWad3Catalog&) = delete;
    GoldSrcWad3Catalog& operator=(GoldSrcWad3Catalog&&) noexcept = delete;
    ~GoldSrcWad3Catalog() = default;

    [[nodiscard]] const std::vector<GoldSrcWad3Entry>& entries() const noexcept
    {
        return entries_;
    }

    [[nodiscard]] std::size_t entry_count() const noexcept
    {
        return entries_.size();
    }

    [[nodiscard]] std::size_t source_byte_count() const noexcept
    {
        return source_byte_count_;
    }

    // The query is validated and normalized as an ASCII texture name. Invalid
    // queries and valid names absent from the catalog both return nullptr.
    [[nodiscard]] const GoldSrcWad3Entry* find_miptex(
        std::string_view texture_name) const noexcept;

    [[nodiscard]] const GoldSrcWad3Entry* find_exact_miptex(
        std::string_view texture_name) const noexcept;

private:
    friend class GoldSrcWad3CatalogParser;

    GoldSrcWad3Catalog() = default;

    std::vector<GoldSrcWad3Entry> entries_;
    std::size_t source_byte_count_{0U};
};

struct GoldSrcWad3CatalogParseResult {
    std::optional<GoldSrcWad3Catalog> catalog;
    std::optional<GoldSrcWad3CatalogError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return catalog.has_value();
    }
};

class GoldSrcWad3CatalogParser final {
public:
    [[nodiscard]] static GoldSrcWad3CatalogParseResult parse(
        std::span<const std::byte> source,
        const GoldSrcWad3CatalogLimits& limits = {});
};

} // namespace hlclient::goldsrc::wad3
