#include <hlclient/goldsrc/studio/goldsrc_studio_format.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>

namespace hlclient::goldsrc::studio {
namespace {

class Reader final {
public:
    Reader(const std::span<const std::byte> source, const std::size_t offset) noexcept
        : source_{offset <= source.size() ? source.subspan(offset)
                                         : std::span<const std::byte>{}}
        , valid_{offset <= source.size()}
    {
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }

    [[nodiscard]] std::optional<std::span<const std::byte>> bytes(
        const std::size_t count) noexcept
    {
        if (!valid_ || count > source_.size() - position_) {
            valid_ = false;
            return std::nullopt;
        }
        const auto value = source_.subspan(position_, count);
        position_ += count;
        return value;
    }

    [[nodiscard]] std::optional<std::uint16_t> u16() noexcept
    {
        const auto value = bytes(2U);
        if (!value) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(std::to_integer<std::uint8_t>((*value)[0U])) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(std::to_integer<std::uint8_t>((*value)[1U]))
                << 8U));
    }

    [[nodiscard]] std::optional<std::int16_t> i16() noexcept
    {
        const auto value = u16();
        return value ? std::optional{std::bit_cast<std::int16_t>(*value)} : std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint32_t> u32() noexcept
    {
        const auto value = bytes(4U);
        if (!value) {
            return std::nullopt;
        }
        std::uint32_t result = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            result |= static_cast<std::uint32_t>(
                          std::to_integer<std::uint8_t>((*value)[index]))
                      << static_cast<unsigned int>(index * 8U);
        }
        return result;
    }

    [[nodiscard]] std::optional<std::int32_t> i32() noexcept
    {
        const auto value = u32();
        return value ? std::optional{std::bit_cast<std::int32_t>(*value)} : std::nullopt;
    }

    [[nodiscard]] std::optional<float> f32() noexcept
    {
        static_assert(sizeof(float) == sizeof(std::uint32_t));
        static_assert(std::numeric_limits<float>::is_iec559);
        const auto value = u32();
        if (!value) {
            return std::nullopt;
        }
        const auto decoded = std::bit_cast<float>(*value);
        return std::isfinite(decoded) ? std::optional{decoded} : std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> fixed_string(const std::size_t width)
    {
        const auto value = bytes(width);
        if (!value) {
            return std::nullopt;
        }
        std::size_t length = 0U;
        while (length < value->size() && (*value)[length] != std::byte{0}) {
            const auto character = std::to_integer<std::uint8_t>((*value)[length]);
            if (character < 0x20U || character > 0x7EU) {
                valid_ = false;
                return std::nullopt;
            }
            ++length;
        }
        std::string result;
        result.reserve(length);
        for (std::size_t index = 0U; index < length; ++index) {
            result.push_back(static_cast<char>(
                std::to_integer<std::uint8_t>((*value)[index])));
        }
        return result;
    }

private:
    std::span<const std::byte> source_;
    std::size_t position_{0U};
    bool valid_{true};
};

[[nodiscard]] std::optional<assets::AssetVector3> vector3(Reader& reader) noexcept
{
    const auto x = reader.f32();
    const auto y = reader.f32();
    const auto z = reader.f32();
    if (!x || !y || !z) {
        return std::nullopt;
    }
    return assets::AssetVector3{*x, *y, *z};
}

[[nodiscard]] std::optional<GoldSrcStudioCountOffset> count_offset(
    Reader& reader) noexcept
{
    const auto count = reader.i32();
    const auto offset = reader.i32();
    if (!count || !offset) {
        return std::nullopt;
    }
    return GoldSrcStudioCountOffset{*count, *offset};
}

[[nodiscard]] bool identifier_matches(
    const std::span<const std::byte> source,
    const std::array<std::byte, 4U>& identifier) noexcept
{
    return source.size() >= identifier.size() &&
           std::equal(identifier.begin(), identifier.end(), source.begin());
}

} // namespace

std::optional<GoldSrcStudioHeader> GoldSrcStudioWireDecoder::header(
    const std::span<const std::byte> source)
{
    if (source.size() < kGoldSrcStudioHeaderWireSize ||
        !identifier_matches(source, kGoldSrcStudioIdentifier)) {
        return std::nullopt;
    }
    Reader reader{source, 4U};
    const auto version = reader.i32();
    const auto name = reader.fixed_string(64U);
    const auto length = reader.i32();
    const auto eye = vector3(reader);
    const auto movement_min = vector3(reader);
    const auto movement_max = vector3(reader);
    const auto clipping_min = vector3(reader);
    const auto clipping_max = vector3(reader);
    const auto flags = reader.i32();
    const auto bones = count_offset(reader);
    const auto controllers = count_offset(reader);
    const auto hitboxes = count_offset(reader);
    const auto sequences = count_offset(reader);
    const auto sequence_groups = count_offset(reader);
    const auto textures = count_offset(reader);
    const auto texture_data = reader.i32();
    const auto skin_refs = reader.i32();
    const auto skin_families = reader.i32();
    const auto skin_offset = reader.i32();
    const auto bodyparts = count_offset(reader);
    const auto attachments = count_offset(reader);
    const auto sound_table = reader.i32();
    const auto sound_index = reader.i32();
    const auto sound_groups = reader.i32();
    const auto sound_group_offset = reader.i32();
    const auto transitions = reader.i32();
    const auto transition_offset = reader.i32();
    if (!version || *version != kGoldSrcStudioVersion || !name || !length || !eye ||
        !movement_min || !movement_max || !clipping_min || !clipping_max || !flags ||
        !bones || !controllers || !hitboxes || !sequences || !sequence_groups ||
        !textures || !texture_data || !skin_refs || !skin_families || !skin_offset ||
        !bodyparts || !attachments || !sound_table || !sound_index || !sound_groups ||
        !sound_group_offset || !transitions || !transition_offset || !reader.valid()) {
        return std::nullopt;
    }
    return GoldSrcStudioHeader{
        *name,
        *length,
        *eye,
        *movement_min,
        *movement_max,
        *clipping_min,
        *clipping_max,
        *flags,
        *bones,
        *controllers,
        *hitboxes,
        *sequences,
        *sequence_groups,
        *textures,
        *texture_data,
        *skin_refs,
        *skin_families,
        *skin_offset,
        *bodyparts,
        *attachments,
        *sound_table,
        *sound_index,
        *sound_groups,
        *sound_group_offset,
        *transitions,
        *transition_offset,
    };
}

std::optional<GoldSrcStudioSequenceHeader> GoldSrcStudioWireDecoder::sequence_header(
    const std::span<const std::byte> source)
{
    if (source.size() < kGoldSrcStudioSequenceHeaderWireSize ||
        !identifier_matches(source, kGoldSrcStudioSequenceIdentifier)) {
        return std::nullopt;
    }
    Reader reader{source, 4U};
    const auto version = reader.i32();
    const auto name = reader.fixed_string(64U);
    const auto length = reader.i32();
    if (!version || *version != kGoldSrcStudioVersion || !name || !length ||
        !reader.valid()) {
        return std::nullopt;
    }
    return GoldSrcStudioSequenceHeader{*name, *length};
}

std::optional<GoldSrcStudioBoneRecord> GoldSrcStudioWireDecoder::bone(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    const auto name = reader.fixed_string(32U);
    const auto parent = reader.i32();
    const auto flags = reader.i32();
    GoldSrcStudioBoneRecord record;
    if (!name || !parent || !flags) {
        return std::nullopt;
    }
    record.name = *name;
    record.parent = *parent;
    record.flags = *flags;
    for (auto& controller : record.controllers) {
        const auto value = reader.i32();
        if (!value) {
            return std::nullopt;
        }
        controller = *value;
    }
    for (auto& value : record.values) {
        const auto decoded = reader.f32();
        if (!decoded) {
            return std::nullopt;
        }
        value = *decoded;
    }
    for (auto& scale : record.scales) {
        const auto decoded = reader.f32();
        if (!decoded) {
            return std::nullopt;
        }
        scale = *decoded;
    }
    return reader.valid() ? std::optional{std::move(record)} : std::nullopt;
}

std::optional<GoldSrcStudioBoneControllerRecord>
GoldSrcStudioWireDecoder::bone_controller(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    const auto bone = reader.i32();
    const auto type = reader.i32();
    const auto start = reader.f32();
    const auto end = reader.f32();
    const auto rest = reader.i32();
    const auto index = reader.i32();
    if (!bone || !type || !start || !end || !rest || !index) {
        return std::nullopt;
    }
    return GoldSrcStudioBoneControllerRecord{*bone, *type, *start, *end, *rest, *index};
}

std::optional<GoldSrcStudioHitboxRecord> GoldSrcStudioWireDecoder::hitbox(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    const auto bone = reader.i32();
    const auto group = reader.i32();
    const auto minimum = vector3(reader);
    const auto maximum = vector3(reader);
    if (!bone || !group || !minimum || !maximum) {
        return std::nullopt;
    }
    return GoldSrcStudioHitboxRecord{*bone, *group, *minimum, *maximum};
}

std::optional<GoldSrcStudioSequenceGroupRecord>
GoldSrcStudioWireDecoder::sequence_group(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    const auto label = reader.fixed_string(32U);
    const auto name = reader.fixed_string(64U);
    const auto unused1 = reader.i32();
    const auto unused2 = reader.i32();
    if (!label || !name || !unused1 || !unused2) {
        return std::nullopt;
    }
    return GoldSrcStudioSequenceGroupRecord{*label, *name, *unused1, *unused2};
}

std::optional<GoldSrcStudioSequenceRecord> GoldSrcStudioWireDecoder::sequence(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    GoldSrcStudioSequenceRecord record;
    const auto label = reader.fixed_string(32U);
    const auto fps = reader.f32();
    const auto flags = reader.i32();
    const auto activity = reader.i32();
    const auto activity_weight = reader.i32();
    const auto events = count_offset(reader);
    const auto frames = reader.i32();
    const auto pivots = count_offset(reader);
    const auto motion_type = reader.i32();
    const auto motion_bone = reader.i32();
    const auto movement = vector3(reader);
    const auto automatic_position = reader.i32();
    const auto automatic_angle = reader.i32();
    const auto minimum = vector3(reader);
    const auto maximum = vector3(reader);
    const auto blends = reader.i32();
    const auto animation = reader.i32();
    if (!label || !fps || !flags || !activity || !activity_weight || !events ||
        !frames || !pivots || !motion_type || !motion_bone || !movement ||
        !automatic_position || !automatic_angle || !minimum || !maximum || !blends ||
        !animation) {
        return std::nullopt;
    }
    record.label = *label;
    record.fps = *fps;
    record.flags = *flags;
    record.activity = *activity;
    record.activity_weight = *activity_weight;
    record.events = *events;
    record.frame_count = *frames;
    record.pivots = *pivots;
    record.motion_type = *motion_type;
    record.motion_bone = *motion_bone;
    record.linear_movement = *movement;
    record.automatic_movement_position_offset = *automatic_position;
    record.automatic_movement_angle_offset = *automatic_angle;
    record.minimum = *minimum;
    record.maximum = *maximum;
    record.blend_count = *blends;
    record.animation_offset = *animation;
    for (auto& type : record.blend_types) {
        const auto value = reader.i32();
        if (!value) {
            return std::nullopt;
        }
        type = *value;
    }
    for (auto& value : record.blend_start) {
        const auto decoded = reader.f32();
        if (!decoded) {
            return std::nullopt;
        }
        value = *decoded;
    }
    for (auto& value : record.blend_end) {
        const auto decoded = reader.f32();
        if (!decoded) {
            return std::nullopt;
        }
        value = *decoded;
    }
    const auto blend_parent = reader.i32();
    const auto sequence_group_value = reader.i32();
    const auto entry = reader.i32();
    const auto exit = reader.i32();
    const auto node_flags = reader.i32();
    const auto next = reader.i32();
    if (!blend_parent || !sequence_group_value || !entry || !exit || !node_flags ||
        !next) {
        return std::nullopt;
    }
    record.blend_parent = *blend_parent;
    record.sequence_group = *sequence_group_value;
    record.entry_node = *entry;
    record.exit_node = *exit;
    record.node_flags = *node_flags;
    record.next_sequence = *next;
    return record;
}

std::optional<GoldSrcStudioSequenceEventRecord>
GoldSrcStudioWireDecoder::sequence_event(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    const auto frame = reader.i32();
    const auto event_number = reader.i32();
    const auto type = reader.i32();
    const auto options = reader.bytes(64U);
    if (!frame || !event_number || !type || !options) {
        return std::nullopt;
    }
    GoldSrcStudioSequenceEventRecord result;
    result.frame = *frame;
    result.event_number = *event_number;
    result.type = *type;
    std::copy(options->begin(), options->end(), result.options.begin());
    return result;
}

std::optional<GoldSrcStudioPivotRecord> GoldSrcStudioWireDecoder::pivot(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    const auto origin = vector3(reader);
    const auto start = reader.i32();
    const auto end = reader.i32();
    if (!origin || !start || !end) {
        return std::nullopt;
    }
    return GoldSrcStudioPivotRecord{*origin, *start, *end};
}

std::optional<GoldSrcStudioAttachmentRecord> GoldSrcStudioWireDecoder::attachment(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    const auto name = reader.fixed_string(32U);
    const auto type = reader.i32();
    const auto bone = reader.i32();
    const auto origin = vector3(reader);
    if (!name || !type || !bone || !origin) {
        return std::nullopt;
    }
    GoldSrcStudioAttachmentRecord result;
    result.name = *name;
    result.type = *type;
    result.bone = *bone;
    result.origin = *origin;
    for (auto& vector : result.vectors) {
        const auto decoded = vector3(reader);
        if (!decoded) {
            return std::nullopt;
        }
        vector = *decoded;
    }
    return result;
}

std::optional<GoldSrcStudioAnimationOffsetRecord>
GoldSrcStudioWireDecoder::animation_offset(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    GoldSrcStudioAnimationOffsetRecord result;
    for (auto& value : result.channel_offsets) {
        const auto decoded = reader.u16();
        if (!decoded) {
            return std::nullopt;
        }
        value = *decoded;
    }
    return result;
}

std::optional<GoldSrcStudioBodyPartRecord> GoldSrcStudioWireDecoder::bodypart(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    const auto name = reader.fixed_string(64U);
    const auto count = reader.i32();
    const auto base = reader.i32();
    const auto model_offset = reader.i32();
    if (!name || !count || !base || !model_offset) {
        return std::nullopt;
    }
    return GoldSrcStudioBodyPartRecord{*name, *count, *base, *model_offset};
}

std::optional<GoldSrcStudioTextureRecord> GoldSrcStudioWireDecoder::texture(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    const auto name = reader.fixed_string(64U);
    const auto flags = reader.i32();
    const auto width = reader.i32();
    const auto height = reader.i32();
    const auto data_offset = reader.i32();
    if (!name || !flags || !width || !height || !data_offset) {
        return std::nullopt;
    }
    return GoldSrcStudioTextureRecord{*name, *flags, *width, *height, *data_offset};
}

std::optional<GoldSrcStudioSubmodelRecord> GoldSrcStudioWireDecoder::submodel(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    GoldSrcStudioSubmodelRecord result;
    const auto name = reader.fixed_string(64U);
    const auto type = reader.i32();
    const auto radius = reader.f32();
    const auto meshes = count_offset(reader);
    const auto vertex_count = reader.i32();
    const auto vertex_bones = reader.i32();
    const auto vertices = reader.i32();
    const auto normal_count = reader.i32();
    const auto normal_bones = reader.i32();
    const auto normals = reader.i32();
    const auto groups = count_offset(reader);
    if (!name || !type || !radius || !meshes || !vertex_count || !vertex_bones ||
        !vertices || !normal_count || !normal_bones || !normals || !groups) {
        return std::nullopt;
    }
    result.name = *name;
    result.type = *type;
    result.bounding_radius = *radius;
    result.meshes = *meshes;
    result.vertex_count = *vertex_count;
    result.vertex_bone_offset = *vertex_bones;
    result.vertex_offset = *vertices;
    result.normal_count = *normal_count;
    result.normal_bone_offset = *normal_bones;
    result.normal_offset = *normals;
    result.groups = *groups;
    return result;
}

std::optional<GoldSrcStudioMeshRecord> GoldSrcStudioWireDecoder::mesh(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    const auto triangles = reader.i32();
    const auto commands = reader.i32();
    const auto skin_reference = reader.i32();
    const auto normals = reader.i32();
    const auto normal_offset = reader.i32();
    if (!triangles || !commands || !skin_reference || !normals || !normal_offset) {
        return std::nullopt;
    }
    return GoldSrcStudioMeshRecord{
        *triangles, *commands, *skin_reference, *normals, *normal_offset};
}

std::optional<GoldSrcStudioTriangleCommandVertex>
GoldSrcStudioWireDecoder::triangle_command_vertex(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    const auto vertex = reader.i16();
    const auto normal = reader.i16();
    const auto s = reader.i16();
    const auto t = reader.i16();
    if (!vertex || !normal || !s || !t) {
        return std::nullopt;
    }
    return GoldSrcStudioTriangleCommandVertex{*vertex, *normal, *s, *t};
}

std::optional<assets::AssetVector3> GoldSrcStudioWireDecoder::source_vector(
    const std::span<const std::byte> source, const std::size_t offset)
{
    Reader reader{source, offset};
    return vector3(reader);
}

} // namespace hlclient::goldsrc::studio
