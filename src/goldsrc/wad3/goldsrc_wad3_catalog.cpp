#include <hlclient/goldsrc/wad3/goldsrc_wad3_catalog.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <new>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::goldsrc::wad3 {
namespace {

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

[[nodiscard]] bool checked_multiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] std::uint32_t read_u32_le(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    std::uint32_t value = 0U;
    for (std::size_t byte_index = 0U; byte_index < 4U; ++byte_index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + byte_index]))
                 << static_cast<unsigned int>(byte_index * 8U);
    }
    return value;
}

[[nodiscard]] std::int32_t read_i32_le(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    return std::bit_cast<std::int32_t>(read_u32_le(bytes, offset));
}

[[nodiscard]] constexpr bool is_supported_name_byte(const std::uint8_t value) noexcept
{
    return value >= 0x20U && value <= 0x7EU;
}

[[nodiscard]] constexpr char ascii_upper(const char value) noexcept
{
    return value >= 'a' && value <= 'z'
        ? static_cast<char>(value - ('a' - 'A'))
        : value;
}

[[nodiscard]] bool valid_query_name(const std::string_view name) noexcept
{
    if (name.empty() || name.size() > kGoldSrcWad3EntryNameWireSize) {
        return false;
    }
    return std::ranges::all_of(name, [](const char value) {
        return is_supported_name_byte(static_cast<std::uint8_t>(value));
    });
}

[[nodiscard]] bool ascii_case_equal(
    const std::string_view left,
    const std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (ascii_upper(left[index]) != ascii_upper(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string bounded_context(const std::string_view context)
{
    return std::string{context.substr(0U, kGoldSrcWad3MaximumDiagnosticContextBytes)};
}

[[nodiscard]] GoldSrcWad3CatalogParseResult failure_result(
    const GoldSrcWad3CatalogErrorCode code,
    const std::size_t byte_offset,
    const std::optional<std::size_t> directory_ordinal,
    const std::string_view context)
{
    return GoldSrcWad3CatalogParseResult{
        std::nullopt,
        GoldSrcWad3CatalogError{
            code,
            byte_offset,
            directory_ordinal,
            bounded_context(context),
        },
    };
}

[[nodiscard]] std::optional<std::pair<std::string, std::string>> decode_entry_name(
    const std::span<const std::byte> source,
    const std::size_t offset)
{
    std::size_t length = 0U;
    while (length < kGoldSrcWad3EntryNameWireSize) {
        const auto value = std::to_integer<std::uint8_t>(source[offset + length]);
        if (value == 0U) {
            break;
        }
        if (!is_supported_name_byte(value)) {
            return std::nullopt;
        }
        ++length;
    }
    if (length == 0U) {
        return std::nullopt;
    }

    std::string source_name;
    source_name.reserve(length);
    std::string normalized_name;
    normalized_name.reserve(length);
    for (std::size_t index = 0U; index < length; ++index) {
        const auto value = static_cast<char>(
            std::to_integer<std::uint8_t>(source[offset + index]));
        source_name.push_back(value);
        normalized_name.push_back(ascii_upper(value));
    }
    return std::pair{std::move(source_name), std::move(normalized_name)};
}

[[nodiscard]] bool ranges_overlap(
    const std::size_t left_offset,
    const std::size_t left_size,
    const std::size_t right_offset,
    const std::size_t right_size) noexcept
{
    if (left_size == 0U || right_size == 0U) {
        return false;
    }
    return left_offset < right_offset + right_size &&
        right_offset < left_offset + left_size;
}

} // namespace

bool valid_goldsrc_wad3_catalog_limits(
    const GoldSrcWad3CatalogLimits& limits) noexcept
{
    return limits.maximum_source_bytes >= kGoldSrcWad3HeaderWireSize &&
        limits.maximum_source_bytes <= kGoldSrcWad3HardMaximumSourceBytes &&
        limits.maximum_lump_count > 0U &&
        limits.maximum_lump_count <= kGoldSrcWad3HardMaximumLumpCount;
}

std::string_view to_string(const GoldSrcWad3CatalogErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcWad3CatalogErrorCode::invalid_configuration:
        return "invalid_configuration";
    case GoldSrcWad3CatalogErrorCode::source_too_small: return "source_too_small";
    case GoldSrcWad3CatalogErrorCode::source_limit_exceeded:
        return "source_limit_exceeded";
    case GoldSrcWad3CatalogErrorCode::invalid_identification:
        return "invalid_identification";
    case GoldSrcWad3CatalogErrorCode::negative_lump_count:
        return "negative_lump_count";
    case GoldSrcWad3CatalogErrorCode::zero_lump_count: return "zero_lump_count";
    case GoldSrcWad3CatalogErrorCode::lump_count_limit_exceeded:
        return "lump_count_limit_exceeded";
    case GoldSrcWad3CatalogErrorCode::negative_directory_offset:
        return "negative_directory_offset";
    case GoldSrcWad3CatalogErrorCode::directory_range_overflow:
        return "directory_range_overflow";
    case GoldSrcWad3CatalogErrorCode::directory_out_of_bounds:
        return "directory_out_of_bounds";
    case GoldSrcWad3CatalogErrorCode::directory_overlaps_header:
        return "directory_overlaps_header";
    case GoldSrcWad3CatalogErrorCode::negative_file_position:
        return "negative_file_position";
    case GoldSrcWad3CatalogErrorCode::negative_disk_size: return "negative_disk_size";
    case GoldSrcWad3CatalogErrorCode::negative_uncompressed_size:
        return "negative_uncompressed_size";
    case GoldSrcWad3CatalogErrorCode::lump_range_overflow:
        return "lump_range_overflow";
    case GoldSrcWad3CatalogErrorCode::lump_out_of_bounds: return "lump_out_of_bounds";
    case GoldSrcWad3CatalogErrorCode::lump_overlaps_header:
        return "lump_overlaps_header";
    case GoldSrcWad3CatalogErrorCode::lump_overlaps_directory:
        return "lump_overlaps_directory";
    case GoldSrcWad3CatalogErrorCode::lump_overlap: return "lump_overlap";
    case GoldSrcWad3CatalogErrorCode::unsupported_compression:
        return "unsupported_compression";
    case GoldSrcWad3CatalogErrorCode::uncompressed_size_mismatch:
        return "uncompressed_size_mismatch";
    case GoldSrcWad3CatalogErrorCode::nonzero_padding: return "nonzero_padding";
    case GoldSrcWad3CatalogErrorCode::invalid_entry_name:
        return "invalid_entry_name";
    case GoldSrcWad3CatalogErrorCode::ambiguous_texture_name:
        return "ambiguous_texture_name";
    case GoldSrcWad3CatalogErrorCode::unable_to_retain_catalog:
        return "unable_to_retain_catalog";
    }
    return "unknown";
}

const GoldSrcWad3Entry* GoldSrcWad3Catalog::find_miptex(
    const std::string_view texture_name) const noexcept
{
    if (!valid_query_name(texture_name)) {
        return nullptr;
    }
    const auto match = std::ranges::find_if(entries_, [texture_name](const auto& entry) {
        return entry.is_miptex() && ascii_case_equal(entry.source_name, texture_name);
    });
    return match == entries_.end() ? nullptr : &*match;
}

const GoldSrcWad3Entry* GoldSrcWad3Catalog::find_exact_miptex(
    const std::string_view texture_name) const noexcept
{
    if (!valid_query_name(texture_name)) {
        return nullptr;
    }
    const auto match = std::ranges::find_if(entries_, [texture_name](const auto& entry) {
        return entry.is_miptex() && entry.source_name == texture_name;
    });
    return match == entries_.end() ? nullptr : &*match;
}

GoldSrcWad3CatalogParseResult GoldSrcWad3CatalogParser::parse(
    const std::span<const std::byte> source,
    const GoldSrcWad3CatalogLimits& limits)
{
    try {
        if (!valid_goldsrc_wad3_catalog_limits(limits)) {
            return failure_result(
                GoldSrcWad3CatalogErrorCode::invalid_configuration,
                0U,
                std::nullopt,
                "WAD3 catalog limits are outside the supported hard profile");
        }
        if (source.size() > limits.maximum_source_bytes) {
            return failure_result(
                GoldSrcWad3CatalogErrorCode::source_limit_exceeded,
                source.size(),
                std::nullopt,
                "WAD3 source exceeds the configured byte limit");
        }
        if (source.size() < kGoldSrcWad3HeaderWireSize) {
            return failure_result(
                GoldSrcWad3CatalogErrorCode::source_too_small,
                source.size(),
                std::nullopt,
                "WAD3 source does not contain the exact 12-byte header");
        }
        constexpr std::array<std::uint8_t, 4U> identification{'W', 'A', 'D', '3'};
        for (std::size_t index = 0U; index < identification.size(); ++index) {
            if (std::to_integer<std::uint8_t>(source[index]) != identification[index]) {
                return failure_result(
                    GoldSrcWad3CatalogErrorCode::invalid_identification,
                    index,
                    std::nullopt,
                    "Only the exact WAD3 identification is supported");
            }
        }

        const auto signed_lump_count = read_i32_le(source, 4U);
        if (signed_lump_count < 0) {
            return failure_result(
                GoldSrcWad3CatalogErrorCode::negative_lump_count,
                4U,
                std::nullopt,
                "WAD3 lump count is a signed non-negative field");
        }
        if (signed_lump_count == 0) {
            return failure_result(
                GoldSrcWad3CatalogErrorCode::zero_lump_count,
                4U,
                std::nullopt,
                "The supported world-texture WAD3 profile requires at least one entry");
        }
        const auto lump_count = static_cast<std::size_t>(
            static_cast<std::uint32_t>(signed_lump_count));
        if (lump_count > limits.maximum_lump_count) {
            return failure_result(
                GoldSrcWad3CatalogErrorCode::lump_count_limit_exceeded,
                4U,
                std::nullopt,
                "WAD3 lump count exceeds the configured catalog limit");
        }

        const auto signed_directory_offset = read_i32_le(source, 8U);
        if (signed_directory_offset < 0) {
            return failure_result(
                GoldSrcWad3CatalogErrorCode::negative_directory_offset,
                8U,
                std::nullopt,
                "WAD3 directory offset is a signed non-negative field");
        }
        const auto directory_offset = static_cast<std::size_t>(
            static_cast<std::uint32_t>(signed_directory_offset));
        std::size_t directory_size = 0U;
        std::size_t directory_end = 0U;
        if (!checked_multiply(
                lump_count, kGoldSrcWad3DirectoryEntryWireSize, directory_size) ||
            !checked_add(directory_offset, directory_size, directory_end)) {
            return failure_result(
                GoldSrcWad3CatalogErrorCode::directory_range_overflow,
                8U,
                std::nullopt,
                "WAD3 directory byte range overflows the host size domain");
        }
        if (directory_offset < kGoldSrcWad3HeaderWireSize) {
            return failure_result(
                GoldSrcWad3CatalogErrorCode::directory_overlaps_header,
                directory_offset,
                std::nullopt,
                "WAD3 directory overlaps the exact 12-byte header");
        }
        if (directory_end > source.size()) {
            return failure_result(
                GoldSrcWad3CatalogErrorCode::directory_out_of_bounds,
                directory_offset,
                std::nullopt,
                "WAD3 directory extends beyond the retained source");
        }

        GoldSrcWad3Catalog catalog;
        catalog.entries_.reserve(lump_count);
        for (std::size_t ordinal = 0U; ordinal < lump_count; ++ordinal) {
            const auto entry_offset =
                directory_offset + ordinal * kGoldSrcWad3DirectoryEntryWireSize;
            const auto signed_file_offset = read_i32_le(source, entry_offset);
            const auto signed_disk_size = read_i32_le(source, entry_offset + 4U);
            const auto signed_uncompressed_size = read_i32_le(source, entry_offset + 8U);
            if (signed_file_offset < 0) {
                return failure_result(
                    GoldSrcWad3CatalogErrorCode::negative_file_position,
                    entry_offset,
                    ordinal,
                    "WAD3 entry file position is negative");
            }
            if (signed_disk_size < 0) {
                return failure_result(
                    GoldSrcWad3CatalogErrorCode::negative_disk_size,
                    entry_offset + 4U,
                    ordinal,
                    "WAD3 entry disk size is negative");
            }
            if (signed_uncompressed_size < 0) {
                return failure_result(
                    GoldSrcWad3CatalogErrorCode::negative_uncompressed_size,
                    entry_offset + 8U,
                    ordinal,
                    "WAD3 entry uncompressed size is negative");
            }

            const auto file_offset = static_cast<std::size_t>(
                static_cast<std::uint32_t>(signed_file_offset));
            const auto disk_size = static_cast<std::size_t>(
                static_cast<std::uint32_t>(signed_disk_size));
            const auto uncompressed_size = static_cast<std::size_t>(
                static_cast<std::uint32_t>(signed_uncompressed_size));
            std::size_t lump_end = 0U;
            if (!checked_add(file_offset, disk_size, lump_end)) {
                return failure_result(
                    GoldSrcWad3CatalogErrorCode::lump_range_overflow,
                    entry_offset,
                    ordinal,
                    "WAD3 entry byte range overflows the host size domain");
            }
            if (lump_end > source.size()) {
                return failure_result(
                    GoldSrcWad3CatalogErrorCode::lump_out_of_bounds,
                    entry_offset,
                    ordinal,
                    "WAD3 entry extends beyond the retained source");
            }
            if (ranges_overlap(
                    file_offset,
                    disk_size,
                    0U,
                    kGoldSrcWad3HeaderWireSize)) {
                return failure_result(
                    GoldSrcWad3CatalogErrorCode::lump_overlaps_header,
                    entry_offset,
                    ordinal,
                    "WAD3 entry overlaps the exact 12-byte header");
            }
            if (ranges_overlap(
                    file_offset,
                    disk_size,
                    directory_offset,
                    directory_size)) {
                return failure_result(
                    GoldSrcWad3CatalogErrorCode::lump_overlaps_directory,
                    entry_offset,
                    ordinal,
                    "WAD3 entry overlaps the directory table");
            }

            const auto type = std::to_integer<std::uint8_t>(source[entry_offset + 12U]);
            const auto compression =
                std::to_integer<std::uint8_t>(source[entry_offset + 13U]);
            const auto padding0 =
                std::to_integer<std::uint8_t>(source[entry_offset + 14U]);
            const auto padding1 =
                std::to_integer<std::uint8_t>(source[entry_offset + 15U]);
            if (compression != kGoldSrcWad3NoCompression) {
                return failure_result(
                    GoldSrcWad3CatalogErrorCode::unsupported_compression,
                    entry_offset + 13U,
                    ordinal,
                    "M4.2 supports only uncompressed WAD3 entries");
            }
            if (disk_size != uncompressed_size) {
                return failure_result(
                    GoldSrcWad3CatalogErrorCode::uncompressed_size_mismatch,
                    entry_offset + 4U,
                    ordinal,
                    "Uncompressed WAD3 disk and logical sizes must match exactly");
            }
            if (padding0 != 0U || padding1 != 0U) {
                return failure_result(
                    GoldSrcWad3CatalogErrorCode::nonzero_padding,
                    entry_offset + 14U,
                    ordinal,
                    "WAD3 directory padding bytes must be zero");
            }

            auto names = decode_entry_name(source, entry_offset + 16U);
            if (!names) {
                return failure_result(
                    GoldSrcWad3CatalogErrorCode::invalid_entry_name,
                    entry_offset + 16U,
                    ordinal,
                    "WAD3 entry name is empty or outside printable ASCII");
            }
            catalog.entries_.push_back(GoldSrcWad3Entry{
                ordinal,
                std::move(names->first),
                std::move(names->second),
                file_offset,
                disk_size,
                uncompressed_size,
                type,
                compression,
            });
        }

        std::vector<std::size_t> range_order(lump_count);
        std::iota(range_order.begin(), range_order.end(), 0U);
        std::ranges::sort(range_order, [&catalog](const auto left, const auto right) {
            const auto& left_entry = catalog.entries_[left];
            const auto& right_entry = catalog.entries_[right];
            return left_entry.file_offset < right_entry.file_offset ||
                (left_entry.file_offset == right_entry.file_offset &&
                 left_entry.disk_size < right_entry.disk_size);
        });
        std::optional<std::size_t> previous_nonempty;
        for (const auto index : range_order) {
            const auto& entry = catalog.entries_[index];
            if (entry.disk_size == 0U) {
                continue;
            }
            if (previous_nonempty) {
                const auto& previous = catalog.entries_[*previous_nonempty];
                if (ranges_overlap(
                        previous.file_offset,
                        previous.disk_size,
                        entry.file_offset,
                        entry.disk_size)) {
                    return failure_result(
                        GoldSrcWad3CatalogErrorCode::lump_overlap,
                        directory_offset +
                            entry.directory_ordinal * kGoldSrcWad3DirectoryEntryWireSize,
                        entry.directory_ordinal,
                        "WAD3 entry byte ranges overlap");
                }
            }
            previous_nonempty = index;
        }

        std::vector<std::size_t> miptex_name_order;
        miptex_name_order.reserve(lump_count);
        for (std::size_t index = 0U; index < catalog.entries_.size(); ++index) {
            if (catalog.entries_[index].is_miptex()) {
                miptex_name_order.push_back(index);
            }
        }
        std::ranges::sort(miptex_name_order, [&catalog](const auto left, const auto right) {
            const auto& left_entry = catalog.entries_[left];
            const auto& right_entry = catalog.entries_[right];
            return left_entry.normalized_name < right_entry.normalized_name ||
                (left_entry.normalized_name == right_entry.normalized_name &&
                 left_entry.directory_ordinal < right_entry.directory_ordinal);
        });
        for (std::size_t index = 1U; index < miptex_name_order.size(); ++index) {
            const auto& previous = catalog.entries_[miptex_name_order[index - 1U]];
            const auto& entry = catalog.entries_[miptex_name_order[index]];
            if (previous.normalized_name == entry.normalized_name) {
                return failure_result(
                    GoldSrcWad3CatalogErrorCode::ambiguous_texture_name,
                    directory_offset +
                        entry.directory_ordinal * kGoldSrcWad3DirectoryEntryWireSize + 16U,
                    entry.directory_ordinal,
                    "WAD3 contains an ambiguous normalized miptex name");
            }
        }

        catalog.source_byte_count_ = source.size();
        return GoldSrcWad3CatalogParseResult{std::move(catalog), std::nullopt};
    } catch (const std::bad_alloc&) {
        return failure_result(
            GoldSrcWad3CatalogErrorCode::unable_to_retain_catalog,
            0U,
            std::nullopt,
            "Unable to retain bounded owning WAD3 catalog metadata");
    } catch (...) {
        return failure_result(
            GoldSrcWad3CatalogErrorCode::unable_to_retain_catalog,
            0U,
            std::nullopt,
            "Unexpected failure while constructing transactional WAD3 catalog metadata");
    }
}

} // namespace hlclient::goldsrc::wad3
