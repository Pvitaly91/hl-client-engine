#include <hlclient/goldsrc/studio/goldsrc_studio_parser.hpp>

#include <hlclient/goldsrc/studio/goldsrc_studio_animation.hpp>
#include <hlclient/goldsrc/studio/goldsrc_studio_geometry.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace hlclient::goldsrc::studio {
namespace {

struct ByteRange {
    std::size_t offset{0U};
    std::size_t length{0U};
};

struct CheckedHeader {
    GoldSrcStudioHeader header;
    std::span<const std::byte> declared_source;
    std::vector<ByteRange> fixed_ranges;
};

// All offsets in Studio files are source-relative. Keep one registry per
// physical source so derived tables and variable-length streams cannot alias a
// fixed table (or one another) merely because each individual range is in
// bounds. Public Valve v10 writer evidence emits these ranges sequentially, so
// even exact aliases are rejected unless a future evidence-backed profile
// explicitly opts in to one.
struct SourceRangeRegistry {
    std::size_t source_length{0U};
    // Ordered lookup makes overlap, alias, and next-boundary validation
    // logarithmic in the number of retained animation streams. A vector scan
    // here turns the allowed blend-by-bone channel profile quadratic.
    std::map<std::size_t, std::size_t> occupied_ranges;
    std::set<std::size_t> stream_anchors;
};

struct SequenceGroupRangeRegistry {
    std::uint32_t ordinal{0U};
    SourceRangeRegistry ranges;
};

[[nodiscard]] GoldSrcStudioError make_error(
    const GoldSrcStudioErrorCode code,
    const std::size_t offset,
    std::string context,
    const std::optional<std::size_t> element = std::nullopt,
    const std::optional<std::uint32_t> source_group = std::nullopt)
{
    if (context.size() > kGoldSrcStudioMaximumDiagnosticContextBytes) {
        context.resize(kGoldSrcStudioMaximumDiagnosticContextBytes);
    }
    return GoldSrcStudioError{code, offset, element, source_group, std::move(context)};
}

[[nodiscard]] GoldSrcStudioParseResult parse_failure(GoldSrcStudioError error)
{
    return GoldSrcStudioParseResult{std::nullopt, std::move(error)};
}

[[nodiscard]] GoldSrcStudioDependencyPlanResult inspect_failure(
    GoldSrcStudioError error)
{
    return GoldSrcStudioDependencyPlanResult{std::nullopt, std::move(error)};
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

[[nodiscard]] bool ranges_overlap(const ByteRange& left, const ByteRange& right) noexcept
{
    return left.length != 0U && right.length != 0U &&
           left.offset < right.offset + right.length &&
           right.offset < left.offset + left.length;
}

[[nodiscard]] SourceRangeRegistry make_range_registry(
    const std::size_t source_length,
    const std::size_t header_size,
    const std::span<const ByteRange> fixed_ranges = {})
{
    SourceRangeRegistry result;
    result.source_length = source_length;
    result.occupied_ranges.emplace(0U, header_size);
    for (const auto& range : fixed_ranges) {
        result.occupied_ranges.emplace(range.offset, range.length);
    }
    return result;
}

[[nodiscard]] bool retain_source_range(
    SourceRangeRegistry& registry,
    const ByteRange range,
    GoldSrcStudioError& error,
    const std::string_view name,
    const std::optional<std::size_t> element = std::nullopt,
    const std::optional<std::uint32_t> source_group = std::nullopt)
{
    if (range.length == 0U) {
        if (range.offset <= registry.source_length) {
            return true;
        }
        error = make_error(GoldSrcStudioErrorCode::range_out_of_bounds,
            range.offset, std::string{name} + " marker exceeds its physical source",
            element, source_group);
        return false;
    }
    std::size_t end = 0U;
    if (!checked_add(range.offset, range.length, end)) {
        error = make_error(GoldSrcStudioErrorCode::range_overflow, range.offset,
            std::string{name} + " range arithmetic overflowed", element,
            source_group);
        return false;
    }
    if (end > registry.source_length) {
        error = make_error(GoldSrcStudioErrorCode::range_out_of_bounds,
            range.offset, std::string{name} + " exceeds its physical source",
            element, source_group);
        return false;
    }
    const auto next_range = registry.occupied_ranges.lower_bound(range.offset);
    if (next_range != registry.occupied_ranges.end() &&
        next_range->first < end) {
        error = make_error(GoldSrcStudioErrorCode::range_overlap,
            range.offset, std::string{name} + " overlaps another Studio range",
            element, source_group);
        return false;
    }
    if (next_range != registry.occupied_ranges.begin()) {
        const auto previous_range = std::prev(next_range);
        if (range.offset - previous_range->first < previous_range->second) {
            error = make_error(GoldSrcStudioErrorCode::range_overlap,
                range.offset, std::string{name} + " overlaps another Studio range",
                element, source_group);
            return false;
        }
    }
    const auto next_anchor = registry.stream_anchors.upper_bound(range.offset);
    if (next_anchor != registry.stream_anchors.end() && *next_anchor < end) {
        error = make_error(GoldSrcStudioErrorCode::range_overlap,
            range.offset, std::string{name} + " crosses another stream start",
            element, source_group);
        return false;
    }
    registry.occupied_ranges.emplace(range.offset, range.length);
    return true;
}

[[nodiscard]] bool retain_stream_anchor(
    SourceRangeRegistry& registry,
    const std::size_t anchor,
    GoldSrcStudioError& error,
    const std::string_view name,
    const std::optional<std::size_t> element = std::nullopt,
    const std::optional<std::uint32_t> source_group = std::nullopt,
    std::size_t* const aggregate_anchor_count = nullptr,
    const std::size_t maximum_anchors =
        std::numeric_limits<std::size_t>::max())
{
    if (anchor >= registry.source_length) {
        error = make_error(GoldSrcStudioErrorCode::range_out_of_bounds, anchor,
            std::string{name} + " starts outside its physical source", element,
            source_group);
        return false;
    }
    const auto after_anchor = registry.occupied_ranges.upper_bound(anchor);
    if (after_anchor != registry.occupied_ranges.begin()) {
        const auto occupied = std::prev(after_anchor);
        if (anchor - occupied->first < occupied->second) {
            error = make_error(GoldSrcStudioErrorCode::range_overlap, anchor,
                std::string{name} + " starts inside another Studio range", element,
                source_group);
            return false;
        }
    }
    if (registry.stream_anchors.contains(anchor)) {
        error = make_error(GoldSrcStudioErrorCode::range_overlap, anchor,
            std::string{name} + " aliases another stream start", element,
            source_group);
        return false;
    }
    const auto retained_anchor_count = aggregate_anchor_count != nullptr
        ? *aggregate_anchor_count
        : registry.stream_anchors.size();
    if (retained_anchor_count >= maximum_anchors) {
        error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded, anchor,
            std::string{name} + " exceeds the aggregate stream-anchor limit",
            element, source_group);
        return false;
    }
    registry.stream_anchors.emplace(anchor);
    if (aggregate_anchor_count != nullptr) {
        ++*aggregate_anchor_count;
    }
    return true;
}

[[nodiscard]] std::size_t stream_boundary(
    const SourceRangeRegistry& registry, const std::size_t start) noexcept
{
    auto boundary = registry.source_length;
    const auto occupied = registry.occupied_ranges.upper_bound(start);
    if (occupied != registry.occupied_ranges.end()) {
        boundary = std::min(boundary, occupied->first);
    }
    const auto anchor = registry.stream_anchors.upper_bound(start);
    if (anchor != registry.stream_anchors.end()) {
        boundary = std::min(boundary, *anchor);
    }
    return boundary;
}

[[nodiscard]] SourceRangeRegistry* find_sequence_registry(
    std::vector<SequenceGroupRangeRegistry>& registries,
    const std::uint32_t ordinal) noexcept
{
    const auto iterator = std::find_if(registries.begin(), registries.end(),
        [ordinal](const SequenceGroupRangeRegistry& candidate) {
            return candidate.ordinal == ordinal;
        });
    return iterator == registries.end() ? nullptr : &iterator->ranges;
}

[[nodiscard]] std::optional<std::int32_t> read_i32_le(
    const std::span<const std::byte> source,
    const std::size_t offset) noexcept
{
    if (offset > source.size() || source.size() - offset < 4U) {
        return std::nullopt;
    }
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(source[offset + index]))
                 << static_cast<unsigned int>(index * 8U);
    }
    return std::bit_cast<std::int32_t>(value);
}

[[nodiscard]] std::optional<std::int16_t> read_i16_le(
    const std::span<const std::byte> source,
    const std::size_t offset) noexcept
{
    if (offset > source.size() || source.size() - offset < 2U) {
        return std::nullopt;
    }
    const auto value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(source[offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>(source[offset + 1U]))
            << 8U));
    return std::bit_cast<std::int16_t>(value);
}

[[nodiscard]] bool vector_bounds_valid(
    const assets::AssetVector3& minimum,
    const assets::AssetVector3& maximum) noexcept
{
    return std::isfinite(minimum.x) && std::isfinite(minimum.y) &&
           std::isfinite(minimum.z) && std::isfinite(maximum.x) &&
           std::isfinite(maximum.y) && std::isfinite(maximum.z) &&
           minimum.x <= maximum.x && minimum.y <= maximum.y &&
           minimum.z <= maximum.z;
}

[[nodiscard]] bool vector_is_zero(const assets::AssetVector3& value) noexcept
{
    return value.x == 0.0F && value.y == 0.0F && value.z == 0.0F;
}

[[nodiscard]] bool count_offset_is_zero(
    const GoldSrcStudioCountOffset& value) noexcept
{
    return value.count == 0 && value.offset == 0;
}

[[nodiscard]] bool texture_companion_profile_matches(
    const GoldSrcStudioHeader& header) noexcept
{
    return header.textures.count > 0 && header.skin_reference_count > 0 &&
           header.skin_family_count > 0 &&
           count_offset_is_zero(header.bones) &&
           count_offset_is_zero(header.bone_controllers) &&
           count_offset_is_zero(header.hitboxes) &&
           count_offset_is_zero(header.sequences) &&
           count_offset_is_zero(header.sequence_groups) &&
           count_offset_is_zero(header.bodyparts) &&
           count_offset_is_zero(header.attachments) &&
           header.sound_table == 0 && header.sound_index == 0 &&
           header.sound_group_count == 0 && header.sound_group_offset == 0 &&
           header.transition_count == 0 && header.transition_offset == 0 &&
           header.flags == 0 && vector_is_zero(header.eye_position) &&
           vector_is_zero(header.movement_minimum) &&
           vector_is_zero(header.movement_maximum) &&
           vector_is_zero(header.clipping_minimum) &&
           vector_is_zero(header.clipping_maximum);
}

[[nodiscard]] bool identifier_matches(
    const std::span<const std::byte> source,
    const std::array<std::byte, 4U>& identifier) noexcept
{
    return source.size() >= identifier.size() &&
           std::equal(identifier.begin(), identifier.end(), source.begin());
}

[[nodiscard]] bool append_fixed_range(
    const GoldSrcStudioCountOffset descriptor,
    const std::size_t record_size,
    const std::size_t limit,
    const std::size_t declared_length,
    std::vector<ByteRange>& ranges,
    GoldSrcStudioError& error,
    const std::string_view name)
{
    if (descriptor.count < 0 || descriptor.offset < 0) {
        error = make_error(GoldSrcStudioErrorCode::negative_count_or_offset, 0U,
            std::string{name} + " has a negative count or offset");
        return false;
    }
    const auto count = static_cast<std::size_t>(
        static_cast<std::uint32_t>(descriptor.count));
    const auto offset = static_cast<std::size_t>(
        static_cast<std::uint32_t>(descriptor.offset));
    if (count > limit) {
        error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded, offset,
            std::string{name} + " exceeds the supported count limit");
        return false;
    }
    if (count == 0U) {
        if (offset > declared_length) {
            error = make_error(GoldSrcStudioErrorCode::range_out_of_bounds, offset,
                std::string{name} + " zero-count offset is beyond declared length");
            return false;
        }
        return true;
    }
    std::size_t length = 0U;
    std::size_t end = 0U;
    if (!checked_multiply(count, record_size, length) ||
        !checked_add(offset, length, end)) {
        error = make_error(GoldSrcStudioErrorCode::range_overflow, offset,
            std::string{name} + " range arithmetic overflowed");
        return false;
    }
    if (offset < kGoldSrcStudioHeaderWireSize) {
        error = make_error(GoldSrcStudioErrorCode::range_overlaps_header, offset,
            std::string{name} + " overlaps the Studio header");
        return false;
    }
    if (end > declared_length) {
        error = make_error(GoldSrcStudioErrorCode::range_out_of_bounds, offset,
            std::string{name} + " range exceeds declared length");
        return false;
    }
    ranges.push_back(ByteRange{offset, length});
    return true;
}

[[nodiscard]] std::optional<CheckedHeader> check_header(
    const std::span<const std::byte> source,
    const std::size_t maximum_source_bytes,
    const GoldSrcStudioModelImportLimits& limits,
    GoldSrcStudioError& error)
{
    if (source.size() > maximum_source_bytes) {
        error = make_error(GoldSrcStudioErrorCode::source_limit_exceeded, 0U,
            "Studio source exceeds the configured byte limit");
        return std::nullopt;
    }
    if (source.size() < 4U) {
        error = make_error(GoldSrcStudioErrorCode::source_too_small, 0U,
            "Studio source is shorter than its identifier");
        return std::nullopt;
    }
    if (!identifier_matches(source, kGoldSrcStudioIdentifier)) {
        error = make_error(GoldSrcStudioErrorCode::unsupported_identifier, 0U,
            "Source identifier is not IDST");
        return std::nullopt;
    }
    if (source.size() < 8U) {
        error = make_error(GoldSrcStudioErrorCode::source_too_small, 4U,
            "Studio source is shorter than its version");
        return std::nullopt;
    }
    const auto version = read_i32_le(source, 4U);
    if (!version || *version != kGoldSrcStudioVersion) {
        error = make_error(GoldSrcStudioErrorCode::unsupported_version, 4U,
            "Only little-endian GoldSrc Studio version 10 is supported");
        return std::nullopt;
    }
    if (source.size() < kGoldSrcStudioHeaderWireSize) {
        error = make_error(GoldSrcStudioErrorCode::source_too_small, source.size(),
            "Studio source is shorter than the 244-byte header");
        return std::nullopt;
    }
    const auto header = GoldSrcStudioWireDecoder::header(source);
    if (!header) {
        error = make_error(GoldSrcStudioErrorCode::invalid_float, 8U,
            "Studio header contains an invalid fixed string or non-finite float");
        return std::nullopt;
    }
    if (header->name.size() > limits.maximum_string_bytes) {
        error = make_error(GoldSrcStudioErrorCode::invalid_string,
            kGoldSrcStudioHeaderNameOffset,
            "Studio header name exceeds the configured string limit");
        return std::nullopt;
    }
    if (header->declared_length <
        static_cast<std::int32_t>(kGoldSrcStudioHeaderWireSize)) {
        error = make_error(GoldSrcStudioErrorCode::invalid_declared_length,
            kGoldSrcStudioHeaderLengthOffset,
            "Declared Studio length is below the header size");
        return std::nullopt;
    }
    const auto declared_length = static_cast<std::size_t>(
        static_cast<std::uint32_t>(header->declared_length));
    if (declared_length > source.size()) {
        error = make_error(GoldSrcStudioErrorCode::invalid_declared_length,
            kGoldSrcStudioHeaderLengthOffset,
            "Declared Studio length exceeds available source bytes");
        return std::nullopt;
    }
    if (declared_length != source.size()) {
        error = make_error(GoldSrcStudioErrorCode::unexplained_trailing_data,
            declared_length,
            "Unexplained bytes follow the declared Studio length");
        return std::nullopt;
    }
    if (!vector_bounds_valid(header->movement_minimum, header->movement_maximum) ||
        !vector_bounds_valid(header->clipping_minimum, header->clipping_maximum)) {
        error = make_error(GoldSrcStudioErrorCode::invalid_bounds,
            kGoldSrcStudioHeaderMovementMinimumOffset,
            "Studio header minimum/maximum bounds are invalid");
        return std::nullopt;
    }

    CheckedHeader checked{*header, source.first(declared_length), {}};
    const std::array directories{
        std::tuple{header->bones, kGoldSrcStudioBoneWireSize, limits.maximum_bones,
            std::string_view{"bones"}},
        std::tuple{header->bone_controllers, kGoldSrcStudioBoneControllerWireSize,
            limits.maximum_bone_controllers, std::string_view{"bone controllers"}},
        std::tuple{header->hitboxes, kGoldSrcStudioHitboxWireSize,
            limits.maximum_hitboxes, std::string_view{"hitboxes"}},
        std::tuple{header->sequences, kGoldSrcStudioSequenceWireSize,
            limits.maximum_sequences, std::string_view{"sequences"}},
        std::tuple{header->sequence_groups, kGoldSrcStudioSequenceGroupWireSize,
            limits.maximum_sequence_groups, std::string_view{"sequence groups"}},
        std::tuple{header->textures, kGoldSrcStudioTextureWireSize,
            limits.maximum_textures, std::string_view{"textures"}},
        std::tuple{header->bodyparts, kGoldSrcStudioBodyPartWireSize,
            limits.maximum_bodyparts, std::string_view{"bodyparts"}},
        std::tuple{header->attachments, kGoldSrcStudioAttachmentWireSize,
            limits.maximum_attachments, std::string_view{"attachments"}},
    };
    for (const auto& [descriptor, record_size, limit, name] : directories) {
        if (!append_fixed_range(descriptor, record_size, limit, declared_length,
                checked.fixed_ranges, error, name)) {
            return std::nullopt;
        }
    }

    if (header->skin_reference_count < 0 || header->skin_family_count < 0 ||
        header->skin_offset < 0 || header->transition_count < 0 ||
        header->transition_offset < 0 || header->texture_data_offset < 0) {
        error = make_error(GoldSrcStudioErrorCode::negative_count_or_offset,
            kGoldSrcStudioHeaderSkinReferencesOffset,
            "Skin, transition, or texture-data metadata is negative");
        return std::nullopt;
    }
    if (header->sound_table < 0 || header->sound_index < 0 ||
        header->sound_group_count < 0 || header->sound_group_offset < 0) {
        error = make_error(GoldSrcStudioErrorCode::negative_count_or_offset,
            kGoldSrcStudioHeaderSoundTableOffset,
            "Studio sound metadata contains a negative field");
        return std::nullopt;
    }
    // The v10 sound-table and sound-group grammars are outside this CPU visual
    // import profile. All four reserved fields therefore use the only
    // unambiguous marker policy supported by the pinned Valve corpus: zero.
    // Accepting any nonzero value would silently bless unvalidated metadata or
    // a source range whose record grammar is unknown here.
    if (header->sound_table != 0 || header->sound_index != 0 ||
        header->sound_group_count != 0 || header->sound_group_offset != 0) {
        error = make_error(GoldSrcStudioErrorCode::unsupported_sound_group,
            kGoldSrcStudioHeaderSoundTableOffset,
            "Studio sound metadata is unsupported and requires all fields zero");
        return std::nullopt;
    }
    if (static_cast<std::size_t>(header->texture_data_offset) > declared_length) {
        error = make_error(GoldSrcStudioErrorCode::range_out_of_bounds,
            kGoldSrcStudioHeaderTexturesOffset,
            "Texture-data offset exceeds declared length");
        return std::nullopt;
    }
    const auto skin_refs = static_cast<std::size_t>(
        static_cast<std::uint32_t>(header->skin_reference_count));
    const auto skin_families = static_cast<std::size_t>(
        static_cast<std::uint32_t>(header->skin_family_count));
    if (skin_refs > limits.maximum_skin_references ||
        skin_families > limits.maximum_skin_families) {
        error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded,
            kGoldSrcStudioHeaderSkinReferencesOffset,
            "Skin table dimensions exceed supported limits");
        return std::nullopt;
    }
    std::size_t skin_entries = 0U;
    std::size_t skin_bytes = 0U;
    if (!checked_multiply(skin_refs, skin_families, skin_entries) ||
        !checked_multiply(skin_entries, kGoldSrcStudioSkinReferenceWireSize,
            skin_bytes)) {
        error = make_error(GoldSrcStudioErrorCode::range_overflow,
            static_cast<std::size_t>(header->skin_offset),
            "Skin table range arithmetic overflowed");
        return std::nullopt;
    }
    if ((skin_refs == 0U) != (skin_families == 0U)) {
        error = make_error(GoldSrcStudioErrorCode::invalid_skin_table,
            kGoldSrcStudioHeaderSkinReferencesOffset,
            "Skin reference and family dimensions must both be zero or nonzero");
        return std::nullopt;
    }
    if (skin_bytes != 0U) {
        const auto skin_offset = static_cast<std::size_t>(
            static_cast<std::uint32_t>(header->skin_offset));
        std::size_t skin_end = 0U;
        if (!checked_add(skin_offset, skin_bytes, skin_end)) {
            error = make_error(GoldSrcStudioErrorCode::range_overflow, skin_offset,
                "Skin table range overflowed");
            return std::nullopt;
        }
        if (skin_offset < kGoldSrcStudioHeaderWireSize || skin_end > declared_length) {
            error = make_error(GoldSrcStudioErrorCode::range_out_of_bounds,
                skin_offset, "Skin table is outside the declared source");
            return std::nullopt;
        }
        checked.fixed_ranges.push_back(ByteRange{skin_offset, skin_bytes});
    } else if (static_cast<std::size_t>(header->skin_offset) > declared_length) {
        error = make_error(GoldSrcStudioErrorCode::range_out_of_bounds,
            static_cast<std::size_t>(header->skin_offset),
            "Zero-size skin table offset is beyond declared length");
        return std::nullopt;
    }

    const auto transitions = static_cast<std::size_t>(
        static_cast<std::uint32_t>(header->transition_count));
    std::size_t transition_bytes = 0U;
    if (!checked_multiply(transitions, transitions, transition_bytes)) {
        error = make_error(GoldSrcStudioErrorCode::range_overflow,
            static_cast<std::size_t>(header->transition_offset),
            "Transition table range arithmetic overflowed");
        return std::nullopt;
    }
    if (transition_bytes != 0U) {
        const auto transition_offset = static_cast<std::size_t>(
            static_cast<std::uint32_t>(header->transition_offset));
        std::size_t transition_end = 0U;
        if (!checked_add(transition_offset, transition_bytes, transition_end) ||
            transition_offset < kGoldSrcStudioHeaderWireSize ||
            transition_end > declared_length) {
            error = make_error(GoldSrcStudioErrorCode::invalid_transition_table,
                transition_offset, "Transition table is outside the declared source");
            return std::nullopt;
        }
        checked.fixed_ranges.push_back(ByteRange{transition_offset, transition_bytes});
    } else if (static_cast<std::size_t>(header->transition_offset) > declared_length) {
        error = make_error(GoldSrcStudioErrorCode::invalid_transition_table,
            static_cast<std::size_t>(header->transition_offset),
            "Zero-size transition offset is beyond declared length");
        return std::nullopt;
    }

    for (std::size_t left = 0U; left < checked.fixed_ranges.size(); ++left) {
        for (std::size_t right = left + 1U; right < checked.fixed_ranges.size(); ++right) {
            if (ranges_overlap(checked.fixed_ranges[left], checked.fixed_ranges[right])) {
                error = make_error(GoldSrcStudioErrorCode::range_overlap,
                    checked.fixed_ranges[right].offset,
                    "Fixed Studio record ranges overlap");
                return std::nullopt;
            }
        }
    }

    // Valve's v10 writer stores texturedataindex at the first indexed-pixel
    // byte, and each mstudiotexture_t::index is source-relative. Enforce that
    // relationship when texture records exist. A split main IDST has no
    // texture records/skins and leaves this field at zero; a record-free
    // non-model header may retain a bounded zero-length writer marker.
    const auto texture_count = static_cast<std::size_t>(header->textures.count);
    if (texture_count != 0U) {
        auto first_texture_data = declared_length;
        for (std::size_t index = 0U; index < texture_count; ++index) {
            const auto record_offset =
                static_cast<std::size_t>(header->textures.offset) +
                index * kGoldSrcStudioTextureWireSize;
            const auto texture = GoldSrcStudioWireDecoder::texture(
                checked.declared_source, record_offset);
            if (!texture || texture->name.size() > limits.maximum_string_bytes ||
                texture->data_offset <
                    static_cast<std::int32_t>(kGoldSrcStudioHeaderWireSize)) {
                error = make_error(GoldSrcStudioErrorCode::invalid_texture,
                    record_offset,
                    "Texture descriptor or fixed string is invalid", index);
                return std::nullopt;
            }
            first_texture_data = std::min(first_texture_data,
                static_cast<std::size_t>(texture->data_offset));
        }
        if (static_cast<std::size_t>(header->texture_data_offset) !=
            first_texture_data) {
            error = make_error(GoldSrcStudioErrorCode::invalid_texture,
                kGoldSrcStudioHeaderTextureDataOffset,
                "texturedataindex does not identify the first texture payload");
            return std::nullopt;
        }
    } else if (header->bodyparts.count > 0 && skin_refs == 0U &&
               skin_families == 0U) {
        if (header->texture_data_offset != 0) {
            error = make_error(GoldSrcStudioErrorCode::invalid_texture,
                kGoldSrcStudioHeaderTextureDataOffset,
                "Split model main header has a nonzero texture-data marker");
            return std::nullopt;
        }
    } else if (header->texture_data_offset != 0) {
        const auto marker = static_cast<std::size_t>(header->texture_data_offset);
        if (marker < kGoldSrcStudioHeaderWireSize) {
            error = make_error(GoldSrcStudioErrorCode::invalid_texture,
                kGoldSrcStudioHeaderTextureDataOffset,
                "Texture-data marker aliases the Studio header");
            return std::nullopt;
        }
        for (const auto& occupied : checked.fixed_ranges) {
            if (marker >= occupied.offset &&
                marker - occupied.offset < occupied.length) {
                error = make_error(GoldSrcStudioErrorCode::range_overlap,
                    marker, "Texture-data marker lies inside a fixed table");
                return std::nullopt;
            }
        }
    }
    return checked;
}

[[nodiscard]] std::optional<GoldSrcStudioSequenceHeader> check_sequence_header(
    const std::span<const std::byte> source,
    const GoldSrcStudioModelImportLimits& limits,
    const std::uint32_t ordinal,
    GoldSrcStudioError& error)
{
    if (source.size() > limits.maximum_companion_source_bytes) {
        error = make_error(GoldSrcStudioErrorCode::source_limit_exceeded, 0U,
            "Sequence-group source exceeds the configured byte limit", std::nullopt,
            ordinal);
        return std::nullopt;
    }
    if (source.size() < 4U ||
        !identifier_matches(source, kGoldSrcStudioSequenceIdentifier)) {
        error = make_error(GoldSrcStudioErrorCode::invalid_sequence_group, 0U,
            "Sequence-group companion identifier is not IDSQ", std::nullopt,
            ordinal);
        return std::nullopt;
    }
    if (source.size() < 8U || read_i32_le(source, 4U) != kGoldSrcStudioVersion) {
        error = make_error(GoldSrcStudioErrorCode::invalid_sequence_group, 4U,
            "Sequence-group companion version is not 10", std::nullopt, ordinal);
        return std::nullopt;
    }
    const auto header = GoldSrcStudioWireDecoder::sequence_header(source);
    if (!header || header->declared_length <
                       static_cast<std::int32_t>(kGoldSrcStudioSequenceHeaderWireSize) ||
        static_cast<std::size_t>(header->declared_length) != source.size()) {
        error = make_error(GoldSrcStudioErrorCode::invalid_sequence_group,
            kGoldSrcStudioHeaderLengthOffset,
            "Sequence-group companion has an invalid exact declared length",
            std::nullopt, ordinal);
        return std::nullopt;
    }
    if (header->name.size() > limits.maximum_string_bytes) {
        error = make_error(GoldSrcStudioErrorCode::invalid_string,
            kGoldSrcStudioHeaderNameOffset,
            "Sequence-group header name exceeds the configured string limit",
            std::nullopt, ordinal);
        return std::nullopt;
    }
    return header;
}

[[nodiscard]] bool validate_subrange(
    const std::int32_t signed_count,
    const std::int32_t signed_offset,
    const std::size_t record_size,
    const std::size_t count_limit,
    const std::size_t declared_length,
    ByteRange& range,
    GoldSrcStudioError& error,
    const GoldSrcStudioErrorCode code,
    const std::string_view name)
{
    if (signed_count < 0 || signed_offset < 0) {
        error = make_error(GoldSrcStudioErrorCode::negative_count_or_offset, 0U,
            std::string{name} + " has a negative count or offset");
        return false;
    }
    const auto count = static_cast<std::size_t>(
        static_cast<std::uint32_t>(signed_count));
    const auto offset = static_cast<std::size_t>(
        static_cast<std::uint32_t>(signed_offset));
    if (count > count_limit) {
        error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded, offset,
            std::string{name} + " exceeds its supported count limit");
        return false;
    }
    std::size_t length = 0U;
    std::size_t end = 0U;
    if (!checked_multiply(count, record_size, length) ||
        !checked_add(offset, length, end)) {
        error = make_error(GoldSrcStudioErrorCode::range_overflow, offset,
            std::string{name} + " range arithmetic overflowed");
        return false;
    }
    if (count != 0U && offset < kGoldSrcStudioHeaderWireSize) {
        error = make_error(code, offset, std::string{name} + " overlaps the header");
        return false;
    }
    if (end > declared_length) {
        error = make_error(code, offset,
            std::string{name} + " exceeds the declared source");
        return false;
    }
    range = ByteRange{offset, length};
    return true;
}

[[nodiscard]] GoldSrcStudioDependencyPlanResult inspect_checked(
    const CheckedHeader& checked,
    const GoldSrcStudioModelImportLimits& limits)
{
    GoldSrcStudioModelDependencyPlan plan;
    plan.texture_companion_required =
        checked.header.bodyparts.count > 0 && checked.header.textures.count == 0 &&
        checked.header.skin_reference_count == 0 &&
        checked.header.skin_family_count == 0;
    if (checked.header.sequences.count > 0 && checked.header.sequence_groups.count <= 0) {
        return inspect_failure(make_error(GoldSrcStudioErrorCode::invalid_sequence, 0U,
            "A model with sequences must declare sequence group zero"));
    }
    for (std::size_t index = 0U;
         index < static_cast<std::size_t>(checked.header.sequences.count); ++index) {
        const auto offset = static_cast<std::size_t>(checked.header.sequences.offset) +
                            index * kGoldSrcStudioSequenceWireSize;
        const auto sequence = GoldSrcStudioWireDecoder::sequence(
            checked.declared_source, offset);
        if (!sequence || sequence->label.size() > limits.maximum_string_bytes) {
            return inspect_failure(make_error(GoldSrcStudioErrorCode::invalid_sequence,
                offset, "Unable to decode bounded sequence metadata", index));
        }
        if (sequence->sequence_group < 0 ||
            sequence->sequence_group >= checked.header.sequence_groups.count ||
            static_cast<std::size_t>(sequence->sequence_group) >=
                limits.maximum_sequence_groups) {
            return inspect_failure(make_error(GoldSrcStudioErrorCode::invalid_sequence,
                offset, "Sequence references an invalid sequence group", index));
        }
        if (sequence->sequence_group > 0) {
            const auto ordinal = static_cast<std::uint32_t>(sequence->sequence_group);
            if (std::find(plan.required_sequence_group_ordinals.begin(),
                    plan.required_sequence_group_ordinals.end(), ordinal) ==
                plan.required_sequence_group_ordinals.end()) {
                plan.required_sequence_group_ordinals.push_back(ordinal);
            }
        }
    }
    std::sort(plan.required_sequence_group_ordinals.begin(),
        plan.required_sequence_group_ordinals.end());
    plan.expected_source_count = 1U +
                                 (plan.texture_companion_required ? 1U : 0U) +
                                 plan.required_sequence_group_ordinals.size();
    return GoldSrcStudioDependencyPlanResult{std::move(plan), std::nullopt};
}

[[nodiscard]] const GoldSrcStudioSequenceGroupSourceView* find_sequence_source(
    const std::span<const GoldSrcStudioSequenceGroupSourceView> sources,
    const std::uint32_t ordinal) noexcept
{
    const auto iterator = std::find_if(sources.begin(), sources.end(),
        [ordinal](const GoldSrcStudioSequenceGroupSourceView& source) {
            return source.ordinal == ordinal;
        });
    return iterator == sources.end() ? nullptr : &*iterator;
}

[[nodiscard]] bool append_textures_and_skins(
    const CheckedHeader& texture_header,
    const GoldSrcStudioModelImportLimits& limits,
    assets::SkeletalModelAssetData& model,
    GoldSrcStudioError& error,
    SourceRangeRegistry& source_ranges)
{
    const auto texture_count = static_cast<std::size_t>(
        texture_header.header.textures.count);
    std::size_t total_rgba_bytes = 0U;
    std::vector<ByteRange> data_ranges;
    model.textures.reserve(texture_count);
    for (std::size_t index = 0U; index < texture_count; ++index) {
        const auto record_offset =
            static_cast<std::size_t>(texture_header.header.textures.offset) +
            index * kGoldSrcStudioTextureWireSize;
        const auto texture = GoldSrcStudioWireDecoder::texture(
            texture_header.declared_source, record_offset);
        if (!texture || texture->name.size() > limits.maximum_string_bytes ||
            texture->width <= 0 || texture->height <= 0 ||
            texture->data_offset < 0) {
            error = make_error(GoldSrcStudioErrorCode::invalid_texture,
                record_offset, "Texture descriptor is invalid", index);
            return false;
        }
        const auto width = static_cast<std::uint32_t>(texture->width);
        const auto height = static_cast<std::uint32_t>(texture->height);
        if (width > limits.maximum_texture_dimension ||
            height > limits.maximum_texture_dimension) {
            error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded,
                record_offset, "Texture dimensions exceed the supported profile", index);
            return false;
        }
        std::size_t area = 0U;
        std::size_t data_length = 0U;
        std::size_t data_end = 0U;
        const auto data_offset = static_cast<std::size_t>(texture->data_offset);
        if (!checked_multiply(static_cast<std::size_t>(width),
                static_cast<std::size_t>(height), area) ||
            !checked_add(area, 768U, data_length) ||
            !checked_add(data_offset, data_length, data_end)) {
            error = make_error(GoldSrcStudioErrorCode::range_overflow, data_offset,
                "Texture pixel/palette range overflowed", index);
            return false;
        }
        if (data_offset < kGoldSrcStudioHeaderWireSize ||
            data_end > texture_header.declared_source.size()) {
            error = make_error(GoldSrcStudioErrorCode::invalid_texture, data_offset,
                "Texture pixels or palette are truncated", index);
            return false;
        }
        const ByteRange data_range{data_offset, data_length};
        for (const auto& fixed : texture_header.fixed_ranges) {
            if (ranges_overlap(data_range, fixed)) {
                error = make_error(GoldSrcStudioErrorCode::range_overlap, data_offset,
                    "Texture data overlaps a fixed record range", index);
                return false;
            }
        }
        for (const auto& previous : data_ranges) {
            if (ranges_overlap(data_range, previous)) {
                error = make_error(GoldSrcStudioErrorCode::range_overlap, data_offset,
                    "Texture data ranges overlap", index);
                return false;
            }
        }
        if (!retain_source_range(source_ranges, data_range, error,
                "Texture pixel/palette payload", index)) {
            return false;
        }
        if (area > (limits.maximum_total_rgba_bytes -
                       std::min(total_rgba_bytes, limits.maximum_total_rgba_bytes)) /
                       4U) {
            error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded,
                data_offset, "Aggregate RGBA output exceeds its limit", index);
            return false;
        }
        assets::ModelTextureAsset output;
        output.source_name = texture->name;
        output.width = width;
        output.height = height;
        output.source_flags = std::bit_cast<std::uint32_t>(texture->flags);
        output.alpha_mode = (output.source_flags & kGoldSrcStudioTextureMasked) != 0U
                                ? assets::ModelTextureAlphaMode::masked_index_255
                            : (output.source_flags &
                                  (kGoldSrcStudioTextureAlpha |
                                      kGoldSrcStudioTextureAdditive)) != 0U
                                ? assets::ModelTextureAlphaMode::source_metadata_only
                                : assets::ModelTextureAlphaMode::opaque;
        output.indexed_pixels.reserve(area);
        for (std::size_t pixel = 0U; pixel < area; ++pixel) {
            output.indexed_pixels.push_back(std::to_integer<std::uint8_t>(
                texture_header.declared_source[data_offset + pixel]));
        }
        const auto palette_offset = data_offset + area;
        for (std::size_t palette_index = 0U; palette_index < 256U; ++palette_index) {
            for (std::size_t component = 0U; component < 3U; ++component) {
                output.palette_rgb[palette_index][component] =
                    std::to_integer<std::uint8_t>(texture_header.declared_source[
                        palette_offset + palette_index * 3U + component]);
            }
        }
        output.rgba8_level_zero.reserve(area * 4U);
        for (const auto palette_index : output.indexed_pixels) {
            const auto& color = output.palette_rgb[palette_index];
            output.rgba8_level_zero.push_back(std::byte{color[0U]});
            output.rgba8_level_zero.push_back(std::byte{color[1U]});
            output.rgba8_level_zero.push_back(std::byte{color[2U]});
            const auto alpha =
                output.alpha_mode == assets::ModelTextureAlphaMode::masked_index_255 &&
                        palette_index == 255U
                    ? 0U
                    : 255U;
            output.rgba8_level_zero.push_back(
                std::byte{static_cast<std::uint8_t>(alpha)});
        }
        total_rgba_bytes += area * 4U;
        data_ranges.push_back(data_range);
        model.textures.push_back(std::move(output));
    }

    const auto skin_refs = static_cast<std::size_t>(
        texture_header.header.skin_reference_count);
    const auto skin_families = static_cast<std::size_t>(
        texture_header.header.skin_family_count);
    const auto skin_offset = static_cast<std::size_t>(texture_header.header.skin_offset);
    model.skin_families.reserve(skin_families);
    for (std::size_t family = 0U; family < skin_families; ++family) {
        assets::ModelSkinFamily output;
        output.texture_indices.reserve(skin_refs);
        for (std::size_t slot = 0U; slot < skin_refs; ++slot) {
            const auto entry_offset = skin_offset +
                                      (family * skin_refs + slot) *
                                          kGoldSrcStudioSkinReferenceWireSize;
            const auto texture_index = read_i16_le(
                texture_header.declared_source, entry_offset);
            if (!texture_index || *texture_index < 0 ||
                static_cast<std::size_t>(*texture_index) >= model.textures.size()) {
                error = make_error(GoldSrcStudioErrorCode::invalid_skin_table,
                    entry_offset, "Skin table references an invalid texture", family);
                return false;
            }
            output.texture_indices.push_back(
                static_cast<std::uint16_t>(*texture_index));
        }
        model.skin_families.push_back(std::move(output));
    }
    return true;
}

[[nodiscard]] bool append_skeleton(
    const CheckedHeader& main,
    const GoldSrcStudioModelImportLimits& limits,
    assets::SkeletalModelAssetData& model,
    GoldSrcStudioError& error)
{
    const auto bone_count = static_cast<std::size_t>(main.header.bones.count);
    model.bones.reserve(bone_count);
    for (std::size_t index = 0U; index < bone_count; ++index) {
        const auto offset = static_cast<std::size_t>(main.header.bones.offset) +
                            index * kGoldSrcStudioBoneWireSize;
        const auto bone = GoldSrcStudioWireDecoder::bone(main.declared_source, offset);
        if (!bone || bone->name.size() > limits.maximum_string_bytes ||
            bone->parent < -1 ||
            (bone->parent >= 0 && static_cast<std::size_t>(bone->parent) >= bone_count) ||
            bone->parent == static_cast<std::int32_t>(index)) {
            error = make_error(GoldSrcStudioErrorCode::invalid_skeleton, offset,
                "Bone parent reference is invalid", index);
            return false;
        }
        assets::ModelBone output;
        output.name = bone->name;
        output.parent_index = bone->parent;
        output.source_flags = std::bit_cast<std::uint32_t>(bone->flags);
        output.controller_indices = bone->controllers;
        output.default_translation =
            assets::AssetVector3{bone->values[0U], bone->values[1U], bone->values[2U]};
        output.default_rotation_radians =
            assets::AssetVector3{bone->values[3U], bone->values[4U], bone->values[5U]};
        output.source_scales = bone->scales;
        for (const auto controller : output.controller_indices) {
            if (controller < -1 ||
                (controller >= 0 &&
                    static_cast<std::size_t>(controller) >=
                        static_cast<std::size_t>(main.header.bone_controllers.count))) {
                error = make_error(GoldSrcStudioErrorCode::invalid_controller, offset,
                    "Bone references an invalid controller", index);
                return false;
            }
        }
        model.bones.push_back(std::move(output));
    }
    std::vector<std::uint8_t> colors(bone_count, 0U);
    std::function<bool(std::size_t)> visit = [&](const std::size_t index) {
        if (colors[index] == 1U) {
            return false;
        }
        if (colors[index] == 2U) {
            return true;
        }
        colors[index] = 1U;
        const auto parent = model.bones[index].parent_index;
        if (parent >= 0 && !visit(static_cast<std::size_t>(parent))) {
            return false;
        }
        colors[index] = 2U;
        return true;
    };
    for (std::size_t index = 0U; index < bone_count; ++index) {
        if (!visit(index)) {
            error = make_error(GoldSrcStudioErrorCode::invalid_skeleton,
                static_cast<std::size_t>(main.header.bones.offset) +
                    index * kGoldSrcStudioBoneWireSize,
                "Bone parent graph contains a cycle", index);
            return false;
        }
    }

    const auto controller_count = static_cast<std::size_t>(
        main.header.bone_controllers.count);
    model.bone_controllers.reserve(controller_count);
    for (std::size_t index = 0U; index < controller_count; ++index) {
        const auto offset =
            static_cast<std::size_t>(main.header.bone_controllers.offset) +
            index * kGoldSrcStudioBoneControllerWireSize;
        const auto controller = GoldSrcStudioWireDecoder::bone_controller(
            main.declared_source, offset);
        if (!controller || controller->bone < -1 ||
            (controller->bone >= 0 &&
                static_cast<std::size_t>(controller->bone) >= bone_count) ||
            controller->index < 0 || controller->index > 4) {
            error = make_error(GoldSrcStudioErrorCode::invalid_controller, offset,
                "Bone controller references are invalid", index);
            return false;
        }
        const auto source_type = std::bit_cast<std::uint32_t>(controller->type);
        const auto axis_type = source_type & kGoldSrcStudioControllerTypes;
        if ((source_type & ~(kGoldSrcStudioControllerTypes |
                               kGoldSrcStudioControllerWrap)) != 0U ||
            axis_type == 0U || (axis_type & (axis_type - 1U)) != 0U) {
            error = make_error(GoldSrcStudioErrorCode::invalid_controller, offset,
                "Bone controller uses unsupported type bits", index);
            return false;
        }
        model.bone_controllers.push_back(assets::ModelBoneController{
            controller->bone,
            source_type,
            controller->start,
            controller->end,
            controller->rest,
            controller->index,
            (source_type & kGoldSrcStudioControllerWrap) != 0U,
        });
    }
    static constexpr std::array<std::uint32_t, 6U> expected_controller_types{
        0x0001U, 0x0002U, 0x0004U, 0x0008U, 0x0010U, 0x0020U};
    for (std::size_t bone_index = 0U; bone_index < model.bones.size(); ++bone_index) {
        for (std::size_t slot = 0U; slot < expected_controller_types.size(); ++slot) {
            const auto controller_index =
                model.bones[bone_index].controller_indices[slot];
            if (controller_index < 0) {
                continue;
            }
            const auto& controller = model.bone_controllers[
                static_cast<std::size_t>(controller_index)];
            if (controller.bone_index != static_cast<std::int32_t>(bone_index) ||
                (controller.source_type & kGoldSrcStudioControllerTypes) !=
                    expected_controller_types[slot]) {
                error = make_error(GoldSrcStudioErrorCode::invalid_controller,
                    static_cast<std::size_t>(main.header.bones.offset) +
                        bone_index * kGoldSrcStudioBoneWireSize,
                    "Bone controller does not match its bone/axis slot",
                    bone_index);
                return false;
            }
        }
    }

    const auto hitbox_count = static_cast<std::size_t>(main.header.hitboxes.count);
    model.hitboxes.reserve(hitbox_count);
    for (std::size_t index = 0U; index < hitbox_count; ++index) {
        const auto offset = static_cast<std::size_t>(main.header.hitboxes.offset) +
                            index * kGoldSrcStudioHitboxWireSize;
        const auto hitbox = GoldSrcStudioWireDecoder::hitbox(main.declared_source, offset);
        if (!hitbox || hitbox->bone < 0 ||
            static_cast<std::size_t>(hitbox->bone) >= bone_count ||
            !vector_bounds_valid(hitbox->minimum, hitbox->maximum)) {
            error = make_error(GoldSrcStudioErrorCode::invalid_hitbox, offset,
                "Hitbox bone or bounds are invalid", index);
            return false;
        }
        model.hitboxes.push_back(assets::ModelHitbox{
            static_cast<std::uint32_t>(hitbox->bone),
            hitbox->group,
            assets::ModelBounds{hitbox->minimum, hitbox->maximum},
        });
    }

    const auto attachment_count = static_cast<std::size_t>(
        main.header.attachments.count);
    model.attachments.reserve(attachment_count);
    for (std::size_t index = 0U; index < attachment_count; ++index) {
        const auto offset = static_cast<std::size_t>(main.header.attachments.offset) +
                            index * kGoldSrcStudioAttachmentWireSize;
        const auto attachment = GoldSrcStudioWireDecoder::attachment(
            main.declared_source, offset);
        if (!attachment || attachment->name.size() > limits.maximum_string_bytes ||
            attachment->bone < 0 ||
            static_cast<std::size_t>(attachment->bone) >= bone_count) {
            error = make_error(GoldSrcStudioErrorCode::invalid_attachment, offset,
                "Attachment bone reference is invalid", index);
            return false;
        }
        model.attachments.push_back(assets::ModelAttachment{
            attachment->name,
            attachment->type,
            static_cast<std::uint32_t>(attachment->bone),
            attachment->origin,
            attachment->vectors,
        });
    }
    (void)limits;
    return true;
}

[[nodiscard]] bool append_geometry(
    const CheckedHeader& main,
    const GoldSrcStudioModelImportLimits& limits,
    const std::size_t skin_reference_count,
    assets::SkeletalModelAssetData& model,
    GoldSrcStudioError& error,
    SourceRangeRegistry& source_ranges)
{
    std::size_t total_submodels = 0U;
    std::size_t total_meshes = 0U;
    std::size_t total_triangles = 0U;
    std::size_t total_output_vertices = 0U;
    std::size_t total_output_indices = 0U;
    const auto bodypart_count = static_cast<std::size_t>(main.header.bodyparts.count);
    model.bodyparts.reserve(bodypart_count);
    for (std::size_t bodypart_index = 0U; bodypart_index < bodypart_count;
         ++bodypart_index) {
        const auto bodypart_offset =
            static_cast<std::size_t>(main.header.bodyparts.offset) +
            bodypart_index * kGoldSrcStudioBodyPartWireSize;
        const auto bodypart = GoldSrcStudioWireDecoder::bodypart(
            main.declared_source, bodypart_offset);
        ByteRange model_range;
        if (!bodypart || bodypart->name.size() > limits.maximum_string_bytes ||
            bodypart->model_count <= 0 ||
            !validate_subrange(bodypart ? bodypart->model_count : -1,
                bodypart ? bodypart->model_offset : -1,
                kGoldSrcStudioSubmodelWireSize,
                limits.maximum_models_per_bodypart,
                main.declared_source.size(), model_range, error,
                GoldSrcStudioErrorCode::invalid_bodypart, "bodypart submodels")) {
            if (!error.context.empty()) {
                return false;
            }
            error = make_error(GoldSrcStudioErrorCode::invalid_bodypart,
                bodypart_offset, "Bodypart descriptor is invalid", bodypart_index);
            return false;
        }
        const auto model_count = static_cast<std::size_t>(bodypart->model_count);
        if (model_count > limits.maximum_submodels -
                              std::min(total_submodels, limits.maximum_submodels)) {
            error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded,
                bodypart_offset, "Aggregate submodel count exceeds its limit",
                bodypart_index);
            return false;
        }
        if (!retain_source_range(source_ranges, model_range, error,
                "Bodypart submodel table", bodypart_index)) {
            return false;
        }
        assets::ModelBodyPart output_bodypart;
        output_bodypart.name = bodypart->name;
        output_bodypart.base = bodypart->base;
        output_bodypart.submodel_indices.reserve(model_count);
        for (std::size_t local_model_index = 0U; local_model_index < model_count;
             ++local_model_index) {
            const auto model_offset = model_range.offset +
                                      local_model_index *
                                          kGoldSrcStudioSubmodelWireSize;
            const auto source_model = GoldSrcStudioWireDecoder::submodel(
                main.declared_source, model_offset);
            if (!source_model ||
                source_model->name.size() > limits.maximum_string_bytes ||
                source_model->bounding_radius < 0.0F ||
                source_model->groups.count != 0 ||
                source_model->groups.offset < 0 ||
                static_cast<std::size_t>(source_model->groups.offset) >
                    main.declared_source.size()) {
                error = make_error(GoldSrcStudioErrorCode::invalid_submodel,
                    model_offset,
                    "Submodel radius or unsupported deformation groups are invalid",
                    total_submodels);
                return false;
            }
            ByteRange mesh_range;
            ByteRange vertex_bone_range;
            ByteRange vertex_range;
            ByteRange normal_bone_range;
            ByteRange normal_range;
            if (!validate_subrange(source_model->meshes.count,
                    source_model->meshes.offset, kGoldSrcStudioMeshWireSize,
                    limits.maximum_meshes, main.declared_source.size(), mesh_range,
                    error, GoldSrcStudioErrorCode::invalid_submodel, "submodel meshes") ||
                !validate_subrange(source_model->vertex_count,
                    source_model->vertex_bone_offset, 1U,
                    limits.maximum_vertices_per_submodel,
                    main.declared_source.size(), vertex_bone_range, error,
                    GoldSrcStudioErrorCode::invalid_submodel,
                    "submodel vertex-bone table") ||
                !validate_subrange(source_model->vertex_count,
                    source_model->vertex_offset, kGoldSrcStudioSourceVertexWireSize,
                    limits.maximum_vertices_per_submodel,
                    main.declared_source.size(), vertex_range, error,
                    GoldSrcStudioErrorCode::invalid_submodel, "submodel vertices") ||
                !validate_subrange(source_model->normal_count,
                    source_model->normal_bone_offset, 1U,
                    limits.maximum_normals_per_submodel,
                    main.declared_source.size(), normal_bone_range, error,
                    GoldSrcStudioErrorCode::invalid_submodel,
                    "submodel normal-bone table") ||
                !validate_subrange(source_model->normal_count,
                    source_model->normal_offset, kGoldSrcStudioSourceNormalWireSize,
                    limits.maximum_normals_per_submodel,
                    main.declared_source.size(), normal_range, error,
                    GoldSrcStudioErrorCode::invalid_submodel, "submodel normals")) {
                return false;
            }
            const std::array subranges{
                mesh_range, vertex_bone_range, vertex_range, normal_bone_range,
                normal_range};
            for (std::size_t left = 0U; left < subranges.size(); ++left) {
                for (std::size_t right = left + 1U; right < subranges.size(); ++right) {
                    if (ranges_overlap(subranges[left], subranges[right])) {
                        error = make_error(GoldSrcStudioErrorCode::range_overlap,
                            subranges[right].offset,
                            "Submodel fixed source ranges overlap", total_submodels);
                        return false;
                    }
                }
            }
            for (const auto range : subranges) {
                if (!retain_source_range(source_ranges, range, error,
                        "Submodel derived table", total_submodels)) {
                    return false;
                }
            }
            const auto mesh_count = static_cast<std::size_t>(source_model->meshes.count);
            if (mesh_count > limits.maximum_meshes -
                                 std::min(total_meshes, limits.maximum_meshes)) {
                error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded,
                    model_offset, "Aggregate mesh count exceeds its limit",
                    total_submodels);
                return false;
            }
            for (std::size_t mesh_index = 0U; mesh_index < mesh_count; ++mesh_index) {
                const auto mesh_offset = mesh_range.offset +
                                         mesh_index * kGoldSrcStudioMeshWireSize;
                const auto mesh = GoldSrcStudioWireDecoder::mesh(
                    main.declared_source, mesh_offset);
                if (!mesh || mesh->triangle_command_offset < 0 ||
                    !retain_stream_anchor(source_ranges,
                        static_cast<std::size_t>(mesh->triangle_command_offset),
                        error, "Triangle-command stream", mesh_index)) {
                    if (error.context.empty()) {
                        error = make_error(GoldSrcStudioErrorCode::invalid_mesh,
                            mesh_offset, "Mesh command offset is invalid", mesh_index);
                    }
                    return false;
                }
            }
            std::vector<assets::AssetVector3> positions;
            std::vector<assets::AssetVector3> normals;
            std::vector<std::uint8_t> position_bones;
            std::vector<std::uint8_t> normal_bones;
            const auto vertex_count = static_cast<std::size_t>(source_model->vertex_count);
            const auto normal_count = static_cast<std::size_t>(source_model->normal_count);
            positions.reserve(vertex_count);
            position_bones.reserve(vertex_count);
            normals.reserve(normal_count);
            normal_bones.reserve(normal_count);
            for (std::size_t vertex = 0U; vertex < vertex_count; ++vertex) {
                const auto position = GoldSrcStudioWireDecoder::source_vector(
                    main.declared_source,
                    vertex_range.offset + vertex * kGoldSrcStudioSourceVertexWireSize);
                const auto bone = std::to_integer<std::uint8_t>(
                    main.declared_source[vertex_bone_range.offset + vertex]);
                if (!position || static_cast<std::size_t>(bone) >= model.bones.size()) {
                    error = make_error(GoldSrcStudioErrorCode::invalid_reference,
                        vertex_range.offset, "Vertex or vertex-bone reference is invalid",
                        vertex);
                    return false;
                }
                positions.push_back(*position);
                position_bones.push_back(bone);
            }
            for (std::size_t normal = 0U; normal < normal_count; ++normal) {
                const auto source_normal = GoldSrcStudioWireDecoder::source_vector(
                    main.declared_source,
                    normal_range.offset + normal * kGoldSrcStudioSourceNormalWireSize);
                const auto bone = std::to_integer<std::uint8_t>(
                    main.declared_source[normal_bone_range.offset + normal]);
                if (!source_normal ||
                    static_cast<std::size_t>(bone) >= model.bones.size()) {
                    error = make_error(GoldSrcStudioErrorCode::invalid_reference,
                        normal_range.offset,
                        "Source normal or normal-bone reference is invalid",
                        normal);
                    return false;
                }
                normals.push_back(*source_normal);
                normal_bones.push_back(bone);
            }
            assets::ModelSubmodel output_model;
            output_model.name = source_model->name;
            output_model.bounding_radius = source_model->bounding_radius;
            output_model.source_model_ordinal = static_cast<std::uint32_t>(total_submodels);
            if (!positions.empty()) {
                auto minimum = positions.front();
                auto maximum = positions.front();
                for (const auto& position : positions) {
                    minimum.x = std::min(minimum.x, position.x);
                    minimum.y = std::min(minimum.y, position.y);
                    minimum.z = std::min(minimum.z, position.z);
                    maximum.x = std::max(maximum.x, position.x);
                    maximum.y = std::max(maximum.y, position.y);
                    maximum.z = std::max(maximum.z, position.z);
                }
                output_model.bounds = assets::ModelBounds{minimum, maximum};
            }
            output_model.meshes.reserve(mesh_count);
            std::size_t submodel_triangle_count = 0U;
            std::size_t submodel_normal_count = 0U;
            for (std::size_t mesh_index = 0U; mesh_index < mesh_count; ++mesh_index) {
                const auto mesh_offset = mesh_range.offset +
                                         mesh_index * kGoldSrcStudioMeshWireSize;
                const auto mesh = GoldSrcStudioWireDecoder::mesh(
                    main.declared_source, mesh_offset);
                if (!mesh || mesh->triangle_count < 0 ||
                    mesh->triangle_command_offset < 0 || mesh->skin_reference < 0 ||
                    static_cast<std::size_t>(mesh->skin_reference) >=
                        skin_reference_count ||
                    mesh->normal_count < 0 || mesh->normal_offset != 0) {
                    error = make_error(GoldSrcStudioErrorCode::invalid_mesh,
                        mesh_offset,
                        "Mesh descriptor, skin slot, or Valve normal marker is invalid",
                        mesh_index);
                    return false;
                }
                const auto expected_triangles = static_cast<std::size_t>(
                    mesh->triangle_count);
                if (expected_triangles >
                    limits.maximum_triangles_per_submodel -
                        std::min(submodel_triangle_count,
                            limits.maximum_triangles_per_submodel)) {
                    error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded,
                        mesh_offset,
                        "Aggregate submodel triangle count exceeds its limit",
                        mesh_index);
                    return false;
                }
                if (expected_triangles > limits.maximum_total_triangles -
                                             std::min(total_triangles,
                                                 limits.maximum_total_triangles)) {
                    error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded,
                        mesh_offset, "Aggregate triangle count exceeds its limit",
                        mesh_index);
                    return false;
                }
                const auto mesh_normal_count = static_cast<std::size_t>(
                    mesh->normal_count);
                if (mesh_normal_count > normal_count -
                                            std::min(submodel_normal_count,
                                                normal_count)) {
                    error = make_error(GoldSrcStudioErrorCode::invalid_mesh,
                        mesh_offset,
                        "Mesh normal counts exceed the submodel normal table",
                        mesh_index);
                    return false;
                }
                const auto command_offset = static_cast<std::size_t>(
                    mesh->triangle_command_offset);
                const auto command_boundary = stream_boundary(
                    source_ranges, command_offset);
                const auto decoded = decode_goldsrc_studio_mesh_commands(
                    GoldSrcStudioMeshCommandInput{
                        main.declared_source.first(command_boundary),
                        command_offset,
                        positions,
                        normals,
                        position_bones,
                        normal_bones,
                        model.bones.size(),
                        expected_triangles,
                    },
                    GoldSrcStudioMeshCommandLimits{
                        limits.maximum_triangle_commands,
                        limits.maximum_output_vertices - std::min(total_output_vertices,
                            limits.maximum_output_vertices),
                        limits.maximum_output_indices - std::min(total_output_indices,
                            limits.maximum_output_indices),
                    });
                if (!decoded) {
                    error = make_error(GoldSrcStudioErrorCode::invalid_geometry,
                        decoded.error ? decoded.error->byte_offset : mesh_offset,
                        decoded.error ? std::string{to_string(decoded.error->code)}
                                      : std::string{"mesh command decode failed"},
                        mesh_index);
                    return false;
                }
                if (!retain_source_range(source_ranges,
                        ByteRange{command_offset,
                            decoded.output->consumed_byte_count},
                        error, "Triangle-command stream", mesh_index)) {
                    return false;
                }
                const auto first_index = output_model.indices.size();
                const auto first_vertex = output_model.vertices.size();
                if (first_index > std::numeric_limits<std::uint32_t>::max() ||
                    first_vertex > std::numeric_limits<std::uint32_t>::max()) {
                    error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded,
                        mesh_offset, "Submodel output index range exceeds uint32", mesh_index);
                    return false;
                }
                output_model.vertices.insert(output_model.vertices.end(),
                    decoded.output->vertices.begin(), decoded.output->vertices.end());
                for (const auto index : decoded.output->indices) {
                    if (static_cast<std::size_t>(index) >
                        std::numeric_limits<std::uint32_t>::max() - first_vertex) {
                        error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded,
                            mesh_offset, "Adjusted submodel index overflows uint32",
                            mesh_index);
                        return false;
                    }
                    output_model.indices.push_back(
                        static_cast<std::uint32_t>(first_vertex + index));
                }
                output_model.meshes.push_back(assets::ModelMesh{
                    static_cast<std::uint32_t>(first_index),
                    static_cast<std::uint32_t>(decoded.output->indices.size()),
                    static_cast<std::uint32_t>(mesh->skin_reference),
                    static_cast<std::uint32_t>(total_meshes),
                    static_cast<std::uint32_t>(expected_triangles),
                    static_cast<std::uint32_t>(decoded.output->source_command_count),
                    static_cast<std::uint32_t>(
                        decoded.output->retained_degenerate_triangle_count),
                });
                total_output_vertices += decoded.output->vertices.size();
                total_output_indices += decoded.output->indices.size();
                total_triangles += expected_triangles;
                submodel_triangle_count += expected_triangles;
                submodel_normal_count += mesh_normal_count;
                ++total_meshes;
            }
            if (submodel_normal_count != normal_count) {
                error = make_error(GoldSrcStudioErrorCode::invalid_mesh,
                    model_offset,
                    "Mesh normal counts do not cover the submodel normal table",
                    total_submodels);
                return false;
            }
            output_bodypart.submodel_indices.push_back(
                static_cast<std::uint32_t>(model.submodels.size()));
            model.submodels.push_back(std::move(output_model));
            ++total_submodels;
        }
        model.bodyparts.push_back(std::move(output_bodypart));
    }
    return true;
}

[[nodiscard]] bool append_sequences(
    const CheckedHeader& main,
    const GoldSrcStudioSourceBundleView& sources,
    const GoldSrcStudioModelImportLimits& limits,
    assets::SkeletalModelAssetData& model,
    GoldSrcStudioError& error,
    SourceRangeRegistry& main_ranges,
    std::vector<SequenceGroupRangeRegistry>& sequence_group_ranges)
{
    const auto group_count = static_cast<std::size_t>(main.header.sequence_groups.count);
    std::size_t maximum_animation_stream_anchors = 0U;
    if (!checked_multiply(limits.maximum_animation_tracks, 6U,
            maximum_animation_stream_anchors)) {
        error = make_error(GoldSrcStudioErrorCode::invalid_configuration, 0U,
            "Animation track-to-channel limit arithmetic overflowed");
        return false;
    }
    maximum_animation_stream_anchors = std::min(
        maximum_animation_stream_anchors, limits.maximum_animation_runs);
    std::size_t group_zero_animation_base = 0U;
    model.sequence_groups.reserve(group_count);
    for (std::size_t index = 0U; index < group_count; ++index) {
        const auto offset = static_cast<std::size_t>(main.header.sequence_groups.offset) +
                            index * kGoldSrcStudioSequenceGroupWireSize;
        const auto group = GoldSrcStudioWireDecoder::sequence_group(
            main.declared_source, offset);
        if (!group || group->label.size() > limits.maximum_string_bytes ||
            group->untrusted_name.size() > limits.maximum_string_bytes) {
            error = make_error(GoldSrcStudioErrorCode::invalid_sequence_group,
                offset, "Unable to decode sequence-group metadata", index);
            return false;
        }
        if (index == 0U) {
            if (group->unused2 < 0 ||
                static_cast<std::size_t>(group->unused2) >
                    main.declared_source.size()) {
                error = make_error(GoldSrcStudioErrorCode::invalid_sequence_group,
                    offset + 100U,
                    "Sequence group zero has an invalid animation-data base",
                    index);
                return false;
            }
            group_zero_animation_base = static_cast<std::size_t>(
                static_cast<std::uint32_t>(group->unused2));
        }
        model.sequence_groups.push_back(assets::ModelSequenceGroupMetadata{
            group->label,
            group->untrusted_name,
            static_cast<std::uint32_t>(index),
            index != 0U,
        });
    }

    std::size_t total_events = 0U;
    std::size_t total_pivots = 0U;
    std::size_t total_animation_blends = 0U;
    std::size_t total_animation_tracks = 0U;
    std::size_t total_animation_stream_anchors = 0U;
    std::size_t total_runs = 0U;
    std::size_t total_value_bytes = 0U;
    const auto sequence_count = static_cast<std::size_t>(main.header.sequences.count);
    model.sequences.reserve(sequence_count);
    for (std::size_t sequence_index = 0U; sequence_index < sequence_count;
         ++sequence_index) {
        const auto sequence_offset =
            static_cast<std::size_t>(main.header.sequences.offset) +
            sequence_index * kGoldSrcStudioSequenceWireSize;
        const auto sequence = GoldSrcStudioWireDecoder::sequence(
            main.declared_source, sequence_offset);
        const bool zero_fps_static_sequence = sequence &&
            sequence->fps == 0.0F && sequence->frame_count == 1 &&
            sequence->motion_type == 0 &&
            sequence->linear_movement.x == 0.0F &&
            sequence->linear_movement.y == 0.0F &&
            sequence->linear_movement.z == 0.0F &&
            sequence->automatic_movement_position_offset == 0 &&
            sequence->automatic_movement_angle_offset == 0;
        if (!sequence || sequence->label.size() > limits.maximum_string_bytes ||
            sequence->fps < 0.0F ||
            (sequence->fps == 0.0F && !zero_fps_static_sequence) ||
            sequence->frame_count <= 0 ||
            sequence->blend_count <= 0 ||
            static_cast<std::size_t>(sequence->blend_count) >
                kGoldSrcStudioHardMaximumSequenceGroups ||
            sequence->sequence_group < 0 ||
            static_cast<std::size_t>(sequence->sequence_group) >= group_count ||
            !vector_bounds_valid(sequence->minimum, sequence->maximum) ||
            sequence->entry_node < 0 || sequence->exit_node < 0 ||
            sequence->entry_node > main.header.transition_count ||
            sequence->exit_node > main.header.transition_count ||
            (model.bones.empty() ? sequence->motion_bone != -1
                                 : (sequence->motion_bone < 0 ||
                                       static_cast<std::size_t>(sequence->motion_bone) >=
                                           model.bones.size()))) {
            error = make_error(GoldSrcStudioErrorCode::invalid_sequence,
                sequence_offset, "Sequence metadata is invalid", sequence_index);
            return false;
        }
        const auto blend_count =
            static_cast<std::size_t>(sequence->blend_count);
        std::size_t sequence_track_count = 0U;
        if (!checked_multiply(
                blend_count, model.bones.size(), sequence_track_count) ||
            blend_count >
                limits.maximum_animation_blends -
                    std::min(total_animation_blends,
                        limits.maximum_animation_blends) ||
            sequence_track_count >
                limits.maximum_animation_tracks -
                    std::min(total_animation_tracks,
                        limits.maximum_animation_tracks)) {
            error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded,
                sequence_offset,
                "Aggregate animation blend/track limit exceeded",
                sequence_index);
            return false;
        }
        total_animation_blends += blend_count;
        total_animation_tracks += sequence_track_count;
        ByteRange event_range;
        ByteRange pivot_range;
        if (!validate_subrange(sequence->events.count, sequence->events.offset,
                kGoldSrcStudioSequenceEventWireSize,
                limits.maximum_events - std::min(total_events, limits.maximum_events),
                main.declared_source.size(), event_range, error,
                GoldSrcStudioErrorCode::invalid_sequence, "sequence events") ||
            !validate_subrange(sequence->pivots.count, sequence->pivots.offset,
                kGoldSrcStudioPivotWireSize,
                limits.maximum_pivots - std::min(total_pivots, limits.maximum_pivots),
                main.declared_source.size(), pivot_range, error,
                GoldSrcStudioErrorCode::invalid_sequence, "sequence pivots")) {
            return false;
        }
        if (ranges_overlap(event_range, pivot_range)) {
            error = make_error(GoldSrcStudioErrorCode::range_overlap,
                pivot_range.offset, "Sequence event and pivot ranges overlap",
                sequence_index);
            return false;
        }
        if (!retain_source_range(main_ranges, event_range, error,
                "Sequence event table", sequence_index) ||
            !retain_source_range(main_ranges, pivot_range, error,
                "Sequence pivot table", sequence_index)) {
            return false;
        }
        assets::ModelSequence output;
        output.label = sequence->label;
        output.frames_per_second = sequence->fps;
        output.source_flags = std::bit_cast<std::uint32_t>(sequence->flags);
        output.activity = sequence->activity;
        output.activity_weight = sequence->activity_weight;
        output.frame_count = static_cast<std::uint32_t>(sequence->frame_count);
        output.blend_count = static_cast<std::uint32_t>(sequence->blend_count);
        output.blend_types = sequence->blend_types;
        output.blend_start = sequence->blend_start;
        output.blend_end = sequence->blend_end;
        output.blend_parent = sequence->blend_parent;
        output.motion_type = sequence->motion_type;
        output.motion_bone = sequence->motion_bone;
        output.linear_movement = sequence->linear_movement;
        output.bounds = assets::ModelBounds{sequence->minimum, sequence->maximum};
        output.sequence_group_index =
            static_cast<std::uint32_t>(sequence->sequence_group);
        output.entry_node = sequence->entry_node;
        output.exit_node = sequence->exit_node;
        output.node_flags = sequence->node_flags;
        output.next_sequence = sequence->next_sequence;
        output.events.reserve(static_cast<std::size_t>(sequence->events.count));
        for (std::size_t event_index = 0U;
             event_index < static_cast<std::size_t>(sequence->events.count);
             ++event_index) {
            const auto offset = event_range.offset +
                                event_index * kGoldSrcStudioSequenceEventWireSize;
            const auto event = GoldSrcStudioWireDecoder::sequence_event(
                main.declared_source, offset);
            if (!event || event->frame < 0 ||
                event->frame >= sequence->frame_count) {
                error = make_error(GoldSrcStudioErrorCode::invalid_sequence,
                    offset, "Sequence event frame is invalid", event_index);
                return false;
            }
            std::size_t option_size = 0U;
            while (option_size < event->options.size() &&
                   event->options[option_size] != std::byte{0}) {
                ++option_size;
            }
            output.events.push_back(assets::ModelSequenceEvent{
                event->frame,
                event->event_number,
                event->type,
                std::vector<std::byte>(event->options.begin(),
                    event->options.begin() + static_cast<std::ptrdiff_t>(option_size)),
            });
        }
        output.pivots.reserve(static_cast<std::size_t>(sequence->pivots.count));
        for (std::size_t pivot_index = 0U;
             pivot_index < static_cast<std::size_t>(sequence->pivots.count);
             ++pivot_index) {
            const auto offset = pivot_range.offset +
                                pivot_index * kGoldSrcStudioPivotWireSize;
            const auto pivot = GoldSrcStudioWireDecoder::pivot(
                main.declared_source, offset);
            if (!pivot || pivot->start < 0 || pivot->end < pivot->start ||
                pivot->end >= sequence->frame_count) {
                error = make_error(GoldSrcStudioErrorCode::invalid_sequence,
                    offset, "Sequence pivot range is invalid", pivot_index);
                return false;
            }
            output.pivots.push_back(
                assets::ModelSequencePivot{pivot->origin, pivot->start, pivot->end});
        }
        total_events += static_cast<std::size_t>(sequence->events.count);
        total_pivots += static_cast<std::size_t>(sequence->pivots.count);

        std::span<const std::byte> animation_source = main.declared_source;
        std::size_t animation_header_size = kGoldSrcStudioHeaderWireSize;
        SourceRangeRegistry* animation_ranges = &main_ranges;
        std::uint32_t animation_group_ordinal = 0U;
        if (sequence->sequence_group > 0) {
            const auto ordinal = static_cast<std::uint32_t>(sequence->sequence_group);
            const auto external = find_sequence_source(sources.sequence_groups, ordinal);
            if (external == nullptr) {
                error = make_error(GoldSrcStudioErrorCode::missing_sequence_group, 0U,
                    "Required sequence-group companion is absent", sequence_index,
                    ordinal);
                return false;
            }
            animation_source = external->bytes;
            animation_header_size = kGoldSrcStudioSequenceHeaderWireSize;
            animation_ranges = find_sequence_registry(
                sequence_group_ranges, ordinal);
            animation_group_ordinal = ordinal;
            if (animation_ranges == nullptr) {
                error = make_error(GoldSrcStudioErrorCode::invalid_sequence_group,
                    0U, "Sequence-group range registry is absent",
                    sequence_index, ordinal);
                return false;
            }
        }
        if (sequence->animation_offset < 0) {
            error = make_error(GoldSrcStudioErrorCode::invalid_animation,
                sequence_offset, "Sequence animation offset is negative",
                sequence_index);
            return false;
        }
        std::size_t animation_record_count = 0U;
        std::size_t animation_record_bytes = 0U;
        std::size_t animation_end = 0U;
        const auto relative_animation_offset = static_cast<std::size_t>(
            static_cast<std::uint32_t>(sequence->animation_offset));
        auto animation_offset = relative_animation_offset;
        // The pinned Valve mdlviewer v10 consumer resolves group zero as
        // header + seqgroup::unused2/data + seqdesc::animindex. Stock compiler
        // output normally leaves the base at zero, but it is still wire data.
        if (sequence->sequence_group == 0 &&
            !checked_add(group_zero_animation_base, relative_animation_offset,
                animation_offset)) {
            error = make_error(GoldSrcStudioErrorCode::invalid_animation,
                sequence_offset + 124U,
                "Sequence-group-zero data base plus animindex overflowed",
                sequence_index, 0U);
            return false;
        }
        if (!checked_multiply(static_cast<std::size_t>(sequence->blend_count),
                model.bones.size(), animation_record_count) ||
            !checked_multiply(animation_record_count,
                kGoldSrcStudioAnimationOffsetWireSize, animation_record_bytes) ||
            !checked_add(animation_offset, animation_record_bytes, animation_end) ||
            (animation_record_count != 0U && animation_offset < animation_header_size) ||
            animation_end > animation_source.size()) {
            error = make_error(GoldSrcStudioErrorCode::invalid_animation,
                animation_offset, "Animation-offset record range is invalid",
                sequence_index);
            return false;
        }
        const ByteRange animation_record_range{
            animation_offset, animation_record_bytes};
        if (!retain_source_range(*animation_ranges, animation_record_range,
                error, "Animation-offset records", sequence_index,
                animation_group_ordinal)) {
            return false;
        }
        for (std::size_t record_index = 0U;
             record_index < animation_record_count; ++record_index) {
            const auto record_offset = animation_offset +
                                       record_index *
                                           kGoldSrcStudioAnimationOffsetWireSize;
            const auto record = GoldSrcStudioWireDecoder::animation_offset(
                animation_source, record_offset);
            if (!record) {
                error = make_error(GoldSrcStudioErrorCode::invalid_animation,
                    record_offset, "Animation-offset record is truncated",
                    record_index, animation_group_ordinal);
                return false;
            }
            for (std::size_t channel_index = 0U; channel_index < 6U;
                 ++channel_index) {
                if (record->channel_offsets[channel_index] == 0U) {
                    continue;
                }
                std::size_t channel_offset = 0U;
                if (!checked_add(record_offset,
                        record->channel_offsets[channel_index], channel_offset) ||
                    channel_offset < animation_end ||
                    !retain_stream_anchor(*animation_ranges, channel_offset,
                        error, "Animation RLE stream", channel_index,
                        animation_group_ordinal,
                        &total_animation_stream_anchors,
                        maximum_animation_stream_anchors)) {
                    if (error.context.empty()) {
                        error = make_error(GoldSrcStudioErrorCode::invalid_animation,
                            record_offset,
                            "Animation channel offset crosses its source boundary",
                            channel_index, animation_group_ordinal);
                    }
                    return false;
                }
            }
        }
        output.animation_blends.reserve(static_cast<std::size_t>(sequence->blend_count));
        for (std::size_t blend_index = 0U;
             blend_index < static_cast<std::size_t>(sequence->blend_count);
             ++blend_index) {
            assets::ModelAnimationBlend blend;
            blend.source_blend_ordinal = static_cast<std::uint32_t>(blend_index);
            blend.bone_tracks.reserve(model.bones.size());
            for (std::size_t bone_index = 0U; bone_index < model.bones.size();
                 ++bone_index) {
                const auto record_offset = animation_offset +
                                           (blend_index * model.bones.size() + bone_index) *
                                               kGoldSrcStudioAnimationOffsetWireSize;
                const auto record = GoldSrcStudioWireDecoder::animation_offset(
                    animation_source, record_offset);
                if (!record) {
                    error = make_error(GoldSrcStudioErrorCode::invalid_animation,
                        record_offset, "Animation-offset record is truncated", bone_index);
                    return false;
                }
                assets::ModelBoneAnimationTrack track;
                track.bone_index = static_cast<std::uint32_t>(bone_index);
                const auto defaults = std::array{
                    model.bones[bone_index].default_translation.x,
                    model.bones[bone_index].default_translation.y,
                    model.bones[bone_index].default_translation.z,
                    model.bones[bone_index].default_rotation_radians.x,
                    model.bones[bone_index].default_rotation_radians.y,
                    model.bones[bone_index].default_rotation_radians.z,
                };
                for (std::size_t channel_index = 0U; channel_index < 6U;
                     ++channel_index) {
                    std::size_t absolute_channel_offset = 0U;
                    if (record->channel_offsets[channel_index] != 0U) {
                        if (!checked_add(record_offset,
                                record->channel_offsets[channel_index],
                                absolute_channel_offset) ||
                            absolute_channel_offset < animation_end ||
                            absolute_channel_offset >= animation_source.size()) {
                            error = make_error(GoldSrcStudioErrorCode::invalid_animation,
                                record_offset,
                                "Animation channel offset crosses its source boundary",
                                channel_index);
                            return false;
                        }
                    }
                    const auto channel_boundary =
                        record->channel_offsets[channel_index] == 0U
                            ? animation_source.size()
                            : stream_boundary(*animation_ranges,
                                  absolute_channel_offset);
                    const auto remaining_runs =
                        limits.maximum_animation_runs -
                        std::min(total_runs, limits.maximum_animation_runs);
                    const auto remaining_value_bytes =
                        limits.maximum_animation_value_bytes -
                        std::min(total_value_bytes,
                            limits.maximum_animation_value_bytes);
                    if (record->channel_offsets[channel_index] != 0U &&
                        (remaining_runs == 0U || remaining_value_bytes == 0U)) {
                        error = make_error(
                            GoldSrcStudioErrorCode::count_limit_exceeded,
                            absolute_channel_offset,
                            "Aggregate animation budget is exhausted before channel parse",
                            channel_index, animation_group_ordinal);
                        return false;
                    }
                    const auto parsed = parse_goldsrc_studio_animation_channel(
                        GoldSrcStudioAnimationChannelParseInput{
                            animation_source.first(channel_boundary),
                            record_offset,
                            record->channel_offsets[channel_index],
                            static_cast<std::uint32_t>(sequence->frame_count),
                            static_cast<assets::ModelAnimationChannelSemantic>(
                                channel_index),
                            defaults[channel_index],
                            model.bones[bone_index].source_scales[channel_index],
                            static_cast<std::uint32_t>(sequence->sequence_group),
                        },
                        GoldSrcStudioAnimationChannelLimits{
                            std::max<std::size_t>(remaining_runs, 1U),
                            std::max<std::size_t>(remaining_value_bytes, 1U),
                        });
                    if (!parsed) {
                        error = make_error(GoldSrcStudioErrorCode::invalid_animation,
                            record_offset,
                            parsed.error ? std::string{to_string(*parsed.error)}
                                         : std::string{"animation channel parse failed"},
                            channel_index);
                        return false;
                    }
                    if (record->channel_offsets[channel_index] != 0U) {
                        std::size_t run_header_bytes = 0U;
                        std::size_t channel_bytes = 0U;
                        if (!checked_multiply(parsed.channel->runs.size(), 2U,
                                run_header_bytes) ||
                            !checked_add(run_header_bytes,
                                parsed.source_value_bytes, channel_bytes) ||
                            !retain_source_range(*animation_ranges,
                                ByteRange{absolute_channel_offset, channel_bytes},
                                error, "Animation RLE stream", channel_index,
                                animation_group_ordinal)) {
                            if (error.context.empty()) {
                                error = make_error(
                                    GoldSrcStudioErrorCode::invalid_animation,
                                    absolute_channel_offset,
                                    "Animation RLE byte range is invalid",
                                    channel_index, animation_group_ordinal);
                            }
                            return false;
                        }
                    }
                    if (parsed.channel->runs.size() >
                            limits.maximum_animation_runs -
                                std::min(total_runs,
                                    limits.maximum_animation_runs) ||
                        parsed.source_value_bytes >
                            limits.maximum_animation_value_bytes -
                                std::min(total_value_bytes,
                                    limits.maximum_animation_value_bytes)) {
                        error = make_error(GoldSrcStudioErrorCode::count_limit_exceeded,
                            record_offset,
                            "Aggregate animation run/value limit exceeded",
                            channel_index);
                        return false;
                    }
                    total_runs += parsed.channel->runs.size();
                    total_value_bytes += parsed.source_value_bytes;
                    track.channels[channel_index] = std::move(*parsed.channel);
                }
                blend.bone_tracks.push_back(std::move(track));
            }
            output.animation_blends.push_back(std::move(blend));
        }
        model.sequences.push_back(std::move(output));
    }
    model.statistics.animation_run_count = total_runs;
    model.statistics.animation_value_bytes = total_value_bytes;
    return true;
}

} // namespace

bool valid_goldsrc_studio_model_import_limits(
    const GoldSrcStudioModelImportLimits& limits) noexcept
{
    return limits.maximum_main_source_bytes >= kGoldSrcStudioHeaderWireSize &&
           limits.maximum_main_source_bytes <=
               kGoldSrcStudioHardMaximumMainSourceBytes &&
           limits.maximum_companion_source_bytes >=
               kGoldSrcStudioSequenceHeaderWireSize &&
           limits.maximum_companion_source_bytes <=
               kGoldSrcStudioHardMaximumCompanionSourceBytes &&
           limits.maximum_total_bundle_bytes >= limits.maximum_main_source_bytes &&
           limits.maximum_total_bundle_bytes <=
               kGoldSrcStudioHardMaximumTotalBundleBytes &&
           limits.maximum_bones > 0U &&
           limits.maximum_bones <= kGoldSrcStudioHardMaximumBones &&
           limits.maximum_bone_controllers > 0U &&
           limits.maximum_bone_controllers <=
               kGoldSrcStudioHardMaximumBoneControllers &&
           limits.maximum_hitboxes > 0U &&
           limits.maximum_hitboxes <= kGoldSrcStudioHardMaximumHitboxes &&
           limits.maximum_attachments > 0U &&
           limits.maximum_attachments <= kGoldSrcStudioHardMaximumAttachments &&
           limits.maximum_sequences > 0U &&
           limits.maximum_sequences <= kGoldSrcStudioHardMaximumSequences &&
           limits.maximum_sequence_groups > 0U &&
           limits.maximum_sequence_groups <= kGoldSrcStudioHardMaximumSequenceGroups &&
           limits.maximum_events > 0U &&
           limits.maximum_events <= kGoldSrcStudioHardMaximumEvents &&
           limits.maximum_pivots > 0U &&
           limits.maximum_pivots <= kGoldSrcStudioHardMaximumPivots &&
           limits.maximum_animation_blends > 0U &&
           limits.maximum_animation_blends <=
               kGoldSrcStudioHardMaximumAnimationBlends &&
           limits.maximum_animation_tracks > 0U &&
           limits.maximum_animation_tracks <=
               kGoldSrcStudioHardMaximumAnimationTracks &&
           limits.maximum_animation_runs > 0U &&
           limits.maximum_animation_runs <=
               kGoldSrcStudioHardMaximumAnimationRuns &&
           limits.maximum_animation_value_bytes > 0U &&
           limits.maximum_animation_value_bytes <=
               kGoldSrcStudioHardMaximumAnimationValueBytes &&
           limits.maximum_bodyparts > 0U &&
           limits.maximum_bodyparts <= kGoldSrcStudioHardMaximumBodyParts &&
           limits.maximum_models_per_bodypart > 0U &&
           limits.maximum_models_per_bodypart <=
               kGoldSrcStudioHardMaximumModelsPerBodyPart &&
           limits.maximum_submodels > 0U &&
           limits.maximum_submodels <= kGoldSrcStudioHardMaximumSubmodels &&
           limits.maximum_meshes > 0U &&
           limits.maximum_meshes <= kGoldSrcStudioHardMaximumMeshes &&
           limits.maximum_vertices_per_submodel > 0U &&
           limits.maximum_vertices_per_submodel <=
               kGoldSrcStudioHardMaximumVerticesPerSubmodel &&
           limits.maximum_normals_per_submodel > 0U &&
           limits.maximum_normals_per_submodel <=
               kGoldSrcStudioHardMaximumVerticesPerSubmodel &&
           limits.maximum_triangles_per_submodel > 0U &&
           limits.maximum_triangles_per_submodel <=
               kGoldSrcStudioHardMaximumTrianglesPerSubmodel &&
           limits.maximum_total_triangles > 0U &&
           limits.maximum_total_triangles <=
               kGoldSrcStudioHardMaximumTotalTriangles &&
           limits.maximum_triangle_commands > 0U &&
           limits.maximum_triangle_commands <=
               kGoldSrcStudioHardMaximumTriangleCommands &&
           limits.maximum_output_vertices > 0U &&
           limits.maximum_output_vertices <=
               kGoldSrcStudioHardMaximumOutputVertices &&
           limits.maximum_output_indices > 0U &&
           limits.maximum_output_indices <=
               kGoldSrcStudioHardMaximumOutputIndices &&
           limits.maximum_textures > 0U &&
           limits.maximum_textures <= kGoldSrcStudioHardMaximumTextures &&
           limits.maximum_skin_references > 0U &&
           limits.maximum_skin_references <= kGoldSrcStudioHardMaximumTextures &&
           limits.maximum_skin_families > 0U &&
           limits.maximum_skin_families <=
               kGoldSrcStudioHardMaximumSkinFamilies &&
           limits.maximum_texture_dimension > 0U &&
           limits.maximum_texture_dimension <=
               kGoldSrcStudioHardMaximumTextureDimension &&
           limits.maximum_total_rgba_bytes > 0U &&
           limits.maximum_total_rgba_bytes <=
               kGoldSrcStudioHardMaximumTotalRgbaBytes &&
           limits.maximum_string_bytes > 0U &&
           limits.maximum_string_bytes <= kGoldSrcStudioHardMaximumStringBytes;
}

std::string_view to_string(const GoldSrcStudioErrorCode code) noexcept
{
    switch (code) {
    case GoldSrcStudioErrorCode::invalid_configuration: return "invalid_configuration";
    case GoldSrcStudioErrorCode::source_too_small: return "source_too_small";
    case GoldSrcStudioErrorCode::source_limit_exceeded: return "source_limit_exceeded";
    case GoldSrcStudioErrorCode::unsupported_identifier: return "unsupported_identifier";
    case GoldSrcStudioErrorCode::unsupported_version: return "unsupported_version";
    case GoldSrcStudioErrorCode::invalid_declared_length:
        return "invalid_declared_length";
    case GoldSrcStudioErrorCode::unexplained_trailing_data:
        return "unexplained_trailing_data";
    case GoldSrcStudioErrorCode::negative_count_or_offset:
        return "negative_count_or_offset";
    case GoldSrcStudioErrorCode::count_limit_exceeded: return "count_limit_exceeded";
    case GoldSrcStudioErrorCode::range_overflow: return "range_overflow";
    case GoldSrcStudioErrorCode::range_out_of_bounds: return "range_out_of_bounds";
    case GoldSrcStudioErrorCode::range_overlaps_header: return "range_overlaps_header";
    case GoldSrcStudioErrorCode::range_overlap: return "range_overlap";
    case GoldSrcStudioErrorCode::invalid_string: return "invalid_string";
    case GoldSrcStudioErrorCode::invalid_float: return "invalid_float";
    case GoldSrcStudioErrorCode::invalid_bounds: return "invalid_bounds";
    case GoldSrcStudioErrorCode::invalid_reference: return "invalid_reference";
    case GoldSrcStudioErrorCode::invalid_skeleton: return "invalid_skeleton";
    case GoldSrcStudioErrorCode::invalid_controller: return "invalid_controller";
    case GoldSrcStudioErrorCode::invalid_hitbox: return "invalid_hitbox";
    case GoldSrcStudioErrorCode::invalid_attachment: return "invalid_attachment";
    case GoldSrcStudioErrorCode::invalid_bodypart: return "invalid_bodypart";
    case GoldSrcStudioErrorCode::invalid_submodel: return "invalid_submodel";
    case GoldSrcStudioErrorCode::invalid_mesh: return "invalid_mesh";
    case GoldSrcStudioErrorCode::invalid_geometry: return "invalid_geometry";
    case GoldSrcStudioErrorCode::invalid_texture: return "invalid_texture";
    case GoldSrcStudioErrorCode::invalid_skin_table: return "invalid_skin_table";
    case GoldSrcStudioErrorCode::invalid_sequence: return "invalid_sequence";
    case GoldSrcStudioErrorCode::invalid_animation: return "invalid_animation";
    case GoldSrcStudioErrorCode::unsupported_sound_group:
        return "unsupported_sound_group";
    case GoldSrcStudioErrorCode::invalid_transition_table:
        return "invalid_transition_table";
    case GoldSrcStudioErrorCode::external_dependency_required:
        return "external_dependency_required";
    case GoldSrcStudioErrorCode::missing_texture_companion:
        return "missing_texture_companion";
    case GoldSrcStudioErrorCode::invalid_texture_companion:
        return "invalid_texture_companion";
    case GoldSrcStudioErrorCode::missing_sequence_group: return "missing_sequence_group";
    case GoldSrcStudioErrorCode::invalid_sequence_group: return "invalid_sequence_group";
    case GoldSrcStudioErrorCode::duplicate_sequence_group:
        return "duplicate_sequence_group";
    case GoldSrcStudioErrorCode::total_bundle_limit_exceeded:
        return "total_bundle_limit_exceeded";
    case GoldSrcStudioErrorCode::unable_to_retain_model: return "unable_to_retain_model";
    }
    return "unknown";
}

GoldSrcStudioDependencyPlanResult GoldSrcStudioParser::inspect_dependencies(
    const std::span<const std::byte> main_source,
    const GoldSrcStudioModelImportLimits& limits)
{
    if (!valid_goldsrc_studio_model_import_limits(limits)) {
        return inspect_failure(make_error(GoldSrcStudioErrorCode::invalid_configuration,
            0U, "Studio import limits are outside the supported hard profile"));
    }
    try {
        GoldSrcStudioError error;
        const auto checked = check_header(
            main_source, limits.maximum_main_source_bytes, limits, error);
        if (!checked) {
            return inspect_failure(std::move(error));
        }
        return inspect_checked(*checked, limits);
    } catch (const std::bad_alloc&) {
        return inspect_failure(make_error(GoldSrcStudioErrorCode::unable_to_retain_model,
            0U, "Unable to retain the bounded dependency plan"));
    } catch (const std::length_error&) {
        return inspect_failure(make_error(GoldSrcStudioErrorCode::unable_to_retain_model,
            0U, "Unable to retain the bounded dependency plan"));
    }
}

GoldSrcStudioParseResult GoldSrcStudioParser::parse(
    const GoldSrcStudioSourceBundleView& sources,
    const GoldSrcStudioModelImportLimits& limits)
{
    if (!valid_goldsrc_studio_model_import_limits(limits)) {
        return parse_failure(make_error(GoldSrcStudioErrorCode::invalid_configuration,
            0U, "Studio import limits are outside the supported hard profile"));
    }
    try {
        GoldSrcStudioError error;
        const auto main = check_header(
            sources.main_source, limits.maximum_main_source_bytes, limits, error);
        if (!main) {
            return parse_failure(std::move(error));
        }
        const auto inspected = inspect_checked(*main, limits);
        if (!inspected) {
            return parse_failure(*inspected.error);
        }
        const auto& plan = *inspected.plan;
        if (plan.texture_companion_required && !sources.texture_source) {
            return parse_failure(make_error(
                GoldSrcStudioErrorCode::external_dependency_required, 0U,
                "Validated model requires its derived texture companion"));
        }
        for (const auto ordinal : plan.required_sequence_group_ordinals) {
            if (find_sequence_source(sources.sequence_groups, ordinal) == nullptr) {
                return parse_failure(make_error(
                    GoldSrcStudioErrorCode::external_dependency_required, 0U,
                    "Validated model requires a derived sequence-group companion",
                    std::nullopt, ordinal));
            }
        }
        for (std::size_t left = 0U; left < sources.sequence_groups.size(); ++left) {
            const auto ordinal = sources.sequence_groups[left].ordinal;
            if (ordinal == 0U ||
                std::find(plan.required_sequence_group_ordinals.begin(),
                    plan.required_sequence_group_ordinals.end(), ordinal) ==
                    plan.required_sequence_group_ordinals.end()) {
                return parse_failure(make_error(
                    GoldSrcStudioErrorCode::invalid_sequence_group, 0U,
                    "Bundle contains an unexpected sequence-group ordinal",
                    left, ordinal));
            }
            for (std::size_t right = left + 1U;
                 right < sources.sequence_groups.size(); ++right) {
                if (ordinal == sources.sequence_groups[right].ordinal) {
                    return parse_failure(make_error(
                        GoldSrcStudioErrorCode::duplicate_sequence_group, 0U,
                        "Bundle contains a duplicate sequence-group ordinal",
                        right, ordinal));
                }
            }
        }
        std::size_t total_bundle_bytes = sources.main_source.size();
        if (sources.texture_source) {
            if (!checked_add(total_bundle_bytes, sources.texture_source->size(),
                    total_bundle_bytes)) {
                return parse_failure(make_error(
                    GoldSrcStudioErrorCode::total_bundle_limit_exceeded, 0U,
                    "Bundle byte count overflowed"));
            }
        }
        for (const auto& group : sources.sequence_groups) {
            if (!checked_add(total_bundle_bytes, group.bytes.size(), total_bundle_bytes)) {
                return parse_failure(make_error(
                    GoldSrcStudioErrorCode::total_bundle_limit_exceeded, 0U,
                    "Bundle byte count overflowed"));
            }
        }
        if (total_bundle_bytes > limits.maximum_total_bundle_bytes) {
            return parse_failure(make_error(
                GoldSrcStudioErrorCode::total_bundle_limit_exceeded, 0U,
                "Bundle exceeds the configured aggregate byte limit"));
        }
        std::vector<SequenceGroupRangeRegistry> sequence_group_ranges;
        sequence_group_ranges.reserve(sources.sequence_groups.size());
        for (const auto& group : sources.sequence_groups) {
            if (!check_sequence_header(group.bytes, limits, group.ordinal, error)) {
                return parse_failure(std::move(error));
            }
            sequence_group_ranges.push_back(SequenceGroupRangeRegistry{
                group.ordinal,
                make_range_registry(group.bytes.size(),
                    kGoldSrcStudioSequenceHeaderWireSize),
            });
        }

        CheckedHeader texture_header = *main;
        if (plan.texture_companion_required) {
            const auto parsed_texture_header = check_header(*sources.texture_source,
                limits.maximum_companion_source_bytes, limits, error);
            if (!parsed_texture_header ||
                !texture_companion_profile_matches(parsed_texture_header->header)) {
                return parse_failure(make_error(
                    GoldSrcStudioErrorCode::invalid_texture_companion,
                    error.byte_offset,
                    parsed_texture_header
                        ? "Texture companion contains unexpected model records"
                        : error.context));
            }
            texture_header = *parsed_texture_header;
        } else if (sources.texture_source) {
            return parse_failure(make_error(
                GoldSrcStudioErrorCode::invalid_texture_companion, 0U,
                "Self-contained model bundle contains an unexpected texture companion"));
        }

        auto main_ranges = make_range_registry(main->declared_source.size(),
            kGoldSrcStudioHeaderWireSize, main->fixed_ranges);
        auto texture_ranges = make_range_registry(
            texture_header.declared_source.size(), kGoldSrcStudioHeaderWireSize,
            texture_header.fixed_ranges);
        SourceRangeRegistry* selected_texture_ranges =
            plan.texture_companion_required ? &texture_ranges : &main_ranges;

        GoldSrcStudioParsedDocument document;
        document.main_header = main->header;
        document.dependency_plan = plan;
        auto& model = document.skeletal_model;
        model.source_eye_position = main->header.eye_position;
        model.source_movement_bounds = assets::ModelBounds{
            main->header.movement_minimum, main->header.movement_maximum};
        model.source_clipping_bounds = assets::ModelBounds{
            main->header.clipping_minimum, main->header.clipping_maximum};
        model.source_flags = std::bit_cast<std::uint32_t>(main->header.flags);
        if (!append_skeleton(*main, limits, model, error) ||
            !append_textures_and_skins(texture_header, limits, model, error,
                *selected_texture_ranges) ||
            !append_geometry(*main, limits,
                static_cast<std::size_t>(texture_header.header.skin_reference_count),
                model, error, main_ranges) ||
            !append_sequences(*main, sources, limits, model, error,
                main_ranges, sequence_group_ranges)) {
            return parse_failure(std::move(error));
        }
        const auto transition_count = static_cast<std::size_t>(
            main->header.transition_count);
        const auto transition_bytes = transition_count * transition_count;
        model.transition_node_count = static_cast<std::uint32_t>(transition_count);
        model.transition_table.reserve(transition_bytes);
        const auto transition_offset = static_cast<std::size_t>(
            main->header.transition_offset);
        for (std::size_t index = 0U; index < transition_bytes; ++index) {
            const auto node = std::to_integer<std::uint8_t>(
                main->declared_source[transition_offset + index]);
            if (static_cast<std::size_t>(node) > transition_count) {
                return parse_failure(make_error(
                    GoldSrcStudioErrorCode::invalid_transition_table,
                    transition_offset + index,
                    "Transition table contains an invalid one-based node"));
            }
            model.transition_table.push_back(node);
        }
        model.statistics.source_count = plan.expected_source_count;
        model.statistics.bone_count = model.bones.size();
        model.statistics.bodypart_count = model.bodyparts.size();
        model.statistics.submodel_count = model.submodels.size();
        model.statistics.texture_count = model.textures.size();
        model.statistics.skin_family_count = model.skin_families.size();
        model.statistics.sequence_count = model.sequences.size();
        model.statistics.sequence_group_count = model.sequence_groups.size();
        for (const auto& submodel : model.submodels) {
            model.statistics.mesh_count += submodel.meshes.size();
            model.statistics.emitted_vertex_count += submodel.vertices.size();
            model.statistics.emitted_triangle_count += submodel.indices.size() / 3U;
            for (const auto& mesh : submodel.meshes) {
                model.statistics.retained_degenerate_triangle_count +=
                    mesh.retained_degenerate_triangle_count;
            }
        }
        return GoldSrcStudioParseResult{std::move(document), std::nullopt};
    } catch (const std::bad_alloc&) {
        return parse_failure(make_error(GoldSrcStudioErrorCode::unable_to_retain_model,
            0U, "Unable to retain the bounded owning Studio model"));
    } catch (const std::length_error&) {
        return parse_failure(make_error(GoldSrcStudioErrorCode::unable_to_retain_model,
            0U, "Unable to retain the bounded owning Studio model"));
    }
}

} // namespace hlclient::goldsrc::studio
