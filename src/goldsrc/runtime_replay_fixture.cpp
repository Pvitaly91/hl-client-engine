#include <hlclient/goldsrc/runtime_replay_fixture.hpp>

#include <hlclient/goldsrc/entity_baseline_decoder.hpp>

#include <array>
#include <bit>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <utility>

namespace hlclient::goldsrc {
namespace {

constexpr std::array kEntitySchema{
    0x0eU,0x65U,0x6eU,0x74U,0x69U,0x74U,0x79U,0x5fU,0x73U,0x74U,0x61U,0x74U,0x65U,0x5fU,0x74U,0x00U,
    0x06U,0x00U,0xd9U,0x23U,0x00U,0x00U,0x00U,0x7cU,0x93U,0x4bU,0x3bU,0x4bU,0x73U,0xdbU,0x82U,0xe9U,
    0x02U,0x08U,0x80U,0x00U,0xe8U,0x03U,0x00U,0x00U,0x7dU,0x00U,0x00U,0xc8U,0x1fU,0x01U,0x00U,0x00U,0xe0U,
    0x9bU,0x5cU,0xdaU,0x59U,0x9aU,0xdbU,0x56U,0x4cU,0x17U,0x00U,0x01U,0x40U,0x00U,0x04U,0x40U,0x1fU,0x00U,
    0x00U,0xe8U,0x03U,0x00U,0x40U,0xfeU,0x08U,0x00U,0x00U,0x00U,0xdfU,0xe4U,0xd2U,0xceU,0xd2U,0xdcU,0xb6U,
    0x64U,0xbaU,0x00U,0x10U,0x00U,0x02U,0x20U,0x00U,0xfaU,0x00U,0x00U,0x40U,0x1fU,0x00U,0x00U,0xf2U,0x07U,
    0x01U,0x00U,0x00U,0x10U,0xe6U,0x76U,0xc6U,0x56U,0x36U,0xb7U,0x05U,0xd3U,0x05U,0xc0U,0x00U,0x10U,0x00U,
    0x01U,0xfaU,0x00U,0x00U,0x00U,0xfaU,0x00U,0x00U,0x90U,0x3fU,0x08U,0x00U,0x00U,0x80U,0x30U,0xb7U,0x33U,
    0xb6U,0xb2U,0xb9U,0xadU,0x98U,0x2eU,0x00U,0x08U,0x80U,0x00U,0x08U,0xd0U,0x07U,0x00U,0x00U,0xd0U,0x07U,
    0x00U,0x80U,0xfcU,0x41U,0x00U,0x00U,0x00U,0x84U,0xb9U,0x9dU,0xb1U,0x95U,0xcdU,0x6dU,0xc9U,0x74U,0x01U,
    0x50U,0x00U,0x04U,0x40U,0x80U,0x3eU,0x00U,0x00U,0x80U,0x3eU,0x00U,0x00U,0x00U};
constexpr std::array kClientSchema{
    0x0eU,0x63U,0x6cU,0x69U,0x65U,0x6eU,0x74U,0x64U,0x61U,0x74U,0x61U,0x5fU,0x74U,0x00U,0x05U,0x00U,
    0xd9U,0x23U,0x00U,0x00U,0x00U,0x44U,0x2bU,0x0bU,0x63U,0xa3U,0x43U,0x03U,0x08U,0x50U,0x00U,0x7dU,
    0x00U,0x00U,0x00U,0x7dU,0x00U,0x00U,0xc8U,0x1fU,0x01U,0x00U,0x00U,0xa0U,0x5dU,0x19U,0xdbU,0xdbU,
    0x58U,0x1aU,0x5dU,0xdeU,0x16U,0x4cU,0x17U,0x00U,0x01U,0x40U,0x00U,0x04U,0x40U,0x1fU,0x00U,0x00U,
    0xe8U,0x03U,0x00U,0x40U,0xfeU,0x08U,0x00U,0x00U,0x00U,0xedU,0xcaU,0xd8U,0xdeU,0xc6U,0xd2U,0xe8U,
    0xf2U,0xb6U,0x62U,0xbaU,0x00U,0x10U,0x00U,0x02U,0x20U,0x00U,0xfaU,0x00U,0x00U,0x40U,0x1fU,0x00U,
    0x00U,0xf2U,0x47U,0x00U,0x00U,0x00U,0x68U,0x57U,0xc6U,0xf6U,0x36U,0x96U,0x46U,0x97U,0xb7U,0x25U,
    0xd3U,0x05U,0xc0U,0x00U,0x10U,0x00U,0x01U,0xd0U,0x07U,0x00U,0x00U,0xfaU,0x00U,0x00U,0x90U,0x3fU,
    0x02U,0x00U,0x00U,0x40U,0xbbU,0xb4U,0xb2U,0xbbU,0xafU,0x37U,0xb3U,0xb9U,0x2dU,0x99U,0x2eU,0x00U,
    0x08U,0x80U,0x00U,0x05U,0x40U,0x1fU,0x00U,0x00U,0xd0U,0x07U,0x00U,0x00U};
constexpr std::array kWeaponSchema{
    0x0eU,0x77U,0x65U,0x61U,0x70U,0x6fU,0x6eU,0x5fU,0x64U,0x61U,0x74U,0x61U,0x5fU,0x74U,0x00U,0x04U,
    0x00U,0xd9U,0x43U,0x00U,0x00U,0x00U,0x6cU,0xfbU,0x4aU,0x1bU,0x62U,0x4bU,0x83U,0x03U,0x08U,0x50U,
    0x00U,0x7dU,0x00U,0x00U,0x00U,0x7dU,0x00U,0x00U,0xc8U,0x1fU,0x02U,0x00U,0x00U,0x40U,0xdbU,0x97U,
    0x59U,0x92U,0x9bU,0x54U,0x19U,0xdbU,0x5bU,0x18U,0x19U,0x00U,0x01U,0x40U,0x40U,0x00U,0xe8U,0x03U,
    0x00U,0x00U,0xe8U,0x03U,0x00U,0x40U,0xfeU,0x08U,0x00U,0x00U,0x00U,0xdbU,0xbeU,0xccU,0xd8U,0x9cU,
    0xcaU,0xf0U,0xe8U,0xa4U,0xcaU,0xd8U,0xdeU,0xc2U,0xc8U,0x00U,0x10U,0x00U,0x02U,0x2cU,0x00U,0x12U,
    0x7aU,0x00U,0x40U,0x1fU,0x00U,0x00U,0xf2U,0x47U,0x00U,0x00U,0x00U,0xd8U,0xf6U,0x65U,0xc6U,0xe6U,
    0x54U,0x86U,0x47U,0x07U,0x25U,0x97U,0xd6U,0x16U,0x26U,0x97U,0x17U,0x44U,0x47U,0x17U,0x36U,0xb6U,
    0x06U,0xc0U,0x00U,0x10U,0x60U,0x01U,0x90U,0xd0U,0x03U,0x00U,0xfaU,0x00U,0x00U,0x00U};

constexpr std::array kBaselines{
    0x02U,0x00U,0x0aU,0x00U,0x14U,0x00U,0x1eU,0x00U,0xffU,0xffU,0x00U};

// An independent raw-bit fixture writer keeps project-owned replay bytes in
// the same public GoldSrc representation as captured traffic. It does not use
// the decoder or a production encoder.
class FixtureBitWriter final {
public:
    void write(const std::uint32_t value, const std::size_t width)
    {
        for (std::size_t index = 0U; index < width; ++index) {
            const auto byte_index = bit_offset_ >> 3U;
            if (byte_index == bytes_.size()) bytes_.push_back(std::byte{0U});
            if (((value >> index) & 1U) != 0U) {
                bytes_[byte_index] |= static_cast<std::byte>(
                    1U << (bit_offset_ & 7U));
            }
            ++bit_offset_;
        }
    }

    void align_zero()
    {
        while ((bit_offset_ & 7U) != 0U) write(0U, 1U);
    }

    [[nodiscard]] std::vector<std::byte> finish() &&
    {
        return std::move(bytes_);
    }

private:
    std::vector<std::byte> bytes_;
    std::size_t bit_offset_{0U};
};

[[nodiscard]] constexpr std::uint32_t signed_wire(
    const std::int32_t quantized) noexcept
{
    const auto magnitude = quantized < 0
        ? static_cast<std::uint32_t>(-quantized)
        : static_cast<std::uint32_t>(quantized);
    return (magnitude << 1U) | (quantized < 0 ? 1U : 0U);
}

void time_prefix(FixtureBitWriter& writer, const float seconds)
{
    writer.write(7U, 8U);
    writer.write(std::bit_cast<std::uint32_t>(seconds), 32U);
}

void delta(
    FixtureBitWriter& writer,
    const std::uint8_t mask,
    const std::span<const std::pair<std::uint32_t, std::size_t>> values = {})
{
    writer.write(mask == 0U ? 0U : 1U, 3U);
    if (mask == 0U) return;
    writer.write(mask, 8U);
    for (const auto [value, width] : values) writer.write(value, width);
}

void client_no_base(
    FixtureBitWriter& writer,
    const std::int32_t health,
    const std::int32_t velocity_x,
    const std::int32_t view_z,
    const std::int32_t clip)
{
    writer.write(kGoldSrcSvcClientDataOpcode, 8U);
    writer.write(0U, 1U);
    const std::pair<std::uint32_t, std::size_t> client_values[]{
        {signed_wire(health), 10U}, {signed_wire(velocity_x), 16U},
        {signed_wire(view_z), 10U}};
    delta(writer, 0x13U, client_values);
    writer.write(1U, 1U); writer.write(2U, 6U);
    const std::pair<std::uint32_t, std::size_t> weapon_values[]{
        {signed_wire(clip), 10U}, {0U, 1U},
        {signed_wire(1'500), 22U}, {signed_wire(250), 22U}};
    delta(writer, 0x0fU, weapon_values);
    writer.write(0U, 1U); writer.align_zero();
}

void client_delta(
    FixtureBitWriter& writer,
    const std::uint8_t base_tag,
    const std::int32_t health,
    const std::optional<std::int32_t> clip)
{
    writer.write(kGoldSrcSvcClientDataOpcode, 8U);
    writer.write(1U, 1U); writer.write(base_tag, 8U);
    const std::pair<std::uint32_t, std::size_t> health_value[]{
        {signed_wire(health), 10U}};
    delta(writer, 0x01U, health_value);
    if (clip) {
        writer.write(1U, 1U); writer.write(2U, 6U);
        const std::pair<std::uint32_t, std::size_t> clip_value[]{
            {signed_wire(*clip), 10U}};
        delta(writer, 0x01U, clip_value);
    }
    writer.write(0U, 1U); writer.align_zero();
}

void entity_header(
    FixtureBitWriter& writer,
    const std::uint8_t opcode,
    const std::uint16_t count,
    const std::optional<std::uint8_t> base_tag = std::nullopt)
{
    writer.write(opcode, 8U); writer.write(count, 16U);
    if (base_tag) writer.write(*base_tag, 8U);
}

void full_entity(
    FixtureBitWriter& writer,
    const std::uint32_t difference,
    const std::uint8_t mask,
    const std::span<const std::pair<std::uint32_t, std::size_t>> values = {})
{
    writer.write(0U, 1U); writer.write(0U, 1U);
    writer.write(difference, 6U); writer.write(0U, 1U);
    writer.write(0U, 1U); delta(writer, mask, values);
}

void delta_entity(
    FixtureBitWriter& writer,
    const std::uint32_t difference,
    const std::uint8_t mask,
    const std::span<const std::pair<std::uint32_t, std::size_t>> values = {})
{
    writer.write(0U, 1U); writer.write(0U, 1U);
    writer.write(difference, 6U); writer.write(0U, 1U);
    delta(writer, mask, values);
}

void remove_entity(FixtureBitWriter& writer, const std::uint32_t difference)
{
    writer.write(1U, 1U); writer.write(0U, 1U);
    writer.write(difference, 6U);
}

void finish_entities(FixtureBitWriter& writer)
{
    writer.write(0U, 16U); writer.align_zero();
}

[[nodiscard]] std::vector<std::byte> fixture_initial_mixed(
    const float time = 100.0F)
{
    FixtureBitWriter writer;
    time_prefix(writer, time);
    client_no_base(writer, 100, 12, 112, 8);
    entity_header(writer, kGoldSrcSvcPacketEntitiesOpcode, 3U);
    for (const auto [number, origin] : std::array{
             std::pair{2U, 80}, std::pair{8U, 160}, std::pair{10U, 240}}) {
        const std::pair<std::uint32_t, std::size_t> value[]{
            {signed_wire(origin), 16U}};
        full_entity(writer, number, 0x01U, value);
    }
    finish_entities(writer);
    writer.write(5U, 8U); writer.write(2U, 16U);
    return std::move(writer).finish();
}

[[nodiscard]] std::vector<std::byte> fixture_mixed_delta()
{
    FixtureBitWriter writer;
    time_prefix(writer, 101.0F);
    client_delta(writer, 100U, 75, 5);
    entity_header(
        writer, kGoldSrcSvcDeltaPacketEntitiesOpcode, 3U,
        static_cast<std::uint8_t>(100U));
    const std::pair<std::uint32_t, std::size_t> first[]{
        {signed_wire(96), 16U}};
    delta_entity(writer, 2U, 0x01U, first);
    remove_entity(writer, 18U);
    const std::pair<std::uint32_t, std::size_t> added[]{
        {signed_wire(320), 16U}};
    delta_entity(writer, 10U, 0x01U, added);
    finish_entities(writer);
    return std::move(writer).finish();
}

[[nodiscard]] std::vector<std::byte> fixture_client_only(
    const std::uint8_t base_tag,
    const std::int32_t health,
    const float time,
    const bool append_nop)
{
    FixtureBitWriter writer;
    time_prefix(writer, time); client_delta(writer, base_tag, health, std::nullopt);
    if (append_nop) writer.write(1U, 8U);
    return std::move(writer).finish();
}

[[nodiscard]] std::vector<std::byte> fixture_entity_only(
    const std::uint8_t base_tag,
    const std::int32_t origin,
    const float time)
{
    FixtureBitWriter writer;
    time_prefix(writer, time);
    entity_header(writer, kGoldSrcSvcDeltaPacketEntitiesOpcode, 3U, base_tag);
    const std::pair<std::uint32_t, std::size_t> value[]{
        {signed_wire(origin), 16U}};
    delta_entity(writer, 10U, 0x01U, value);
    finish_entities(writer);
    return std::move(writer).finish();
}

[[nodiscard]] std::vector<std::byte> fixture_entity_recovery()
{
    FixtureBitWriter writer;
    time_prefix(writer, 106.0F);
    entity_header(writer, kGoldSrcSvcPacketEntitiesOpcode, 1U);
    const std::pair<std::uint32_t, std::size_t> value[]{
        {signed_wire(400), 16U}};
    full_entity(writer, 2U, 0x01U, value);
    finish_entities(writer);
    return std::move(writer).finish();
}

[[nodiscard]] std::vector<std::byte> fixture_client_recovery()
{
    FixtureBitWriter writer;
    time_prefix(writer, 108.0F); client_no_base(writer, 60, 0, 112, 3);
    return std::move(writer).finish();
}

[[nodiscard]] std::vector<std::byte> fixture_visual_initial()
{
    FixtureBitWriter writer;
    time_prefix(writer, 200.0F); client_no_base(writer, 100, 12, 112, 8);
    entity_header(writer, kGoldSrcSvcPacketEntitiesOpcode, 3U);
    const std::pair<std::uint32_t, std::size_t> negative[]{
        {signed_wire(-256), 16U}};
    full_entity(writer, 2U, 0x01U, negative);
    full_entity(writer, 8U, 0U);
    const std::pair<std::uint32_t, std::size_t> positive[]{
        {signed_wire(256), 16U}};
    full_entity(writer, 10U, 0x01U, positive);
    finish_entities(writer);
    return std::move(writer).finish();
}

[[nodiscard]] std::vector<std::byte> fixture_visual_move()
{
    FixtureBitWriter writer;
    time_prefix(writer, 201.0F);
    entity_header(
        writer, kGoldSrcSvcDeltaPacketEntitiesOpcode, 3U,
        static_cast<std::uint8_t>(100U));
    const std::pair<std::uint32_t, std::size_t> value[]{
        {signed_wire(-64), 16U}};
    delta_entity(writer, 2U, 0x01U, value); finish_entities(writer);
    return std::move(writer).finish();
}

[[nodiscard]] std::vector<std::byte> fixture_visual_rotate()
{
    FixtureBitWriter writer;
    time_prefix(writer, 202.0F);
    entity_header(
        writer, kGoldSrcSvcDeltaPacketEntitiesOpcode, 3U,
        static_cast<std::uint8_t>(101U));
    const std::pair<std::uint32_t, std::size_t> value[]{{16'384U, 16U}};
    delta_entity(writer, 10U, 0x20U, value); finish_entities(writer);
    return std::move(writer).finish();
}

[[nodiscard]] std::vector<std::byte> fixture_visual_add_remove()
{
    FixtureBitWriter writer;
    time_prefix(writer, 203.0F);
    entity_header(
        writer, kGoldSrcSvcDeltaPacketEntitiesOpcode, 3U,
        static_cast<std::uint8_t>(102U));
    remove_entity(writer, 20U);
    const std::pair<std::uint32_t, std::size_t> values[]{
        {signed_wire(192), 16U}, {32'768U, 16U}};
    delta_entity(writer, 10U, 0x22U, values); finish_entities(writer);
    return std::move(writer).finish();
}

enum class LiteralPayload : std::uint8_t {
    initial_mixed,
    mixed_delta,
    client_only,
    entity_only,
    missing_entity,
    entity_recovery,
    missing_client,
    client_recovery,
    visual_initial,
    visual_move,
    visual_rotate,
    visual_add_remove,
    visual_client_only,
};

template <typename Value, std::size_t Size>
[[nodiscard]] std::vector<std::byte> bytes(
    const std::array<Value, Size>& literal)
{
    std::vector<std::byte> result;
    result.reserve(Size);
    for (const auto value : literal) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> bytes(const LiteralPayload payload)
{
    switch (payload) {
    case LiteralPayload::initial_mixed: return fixture_initial_mixed();
    case LiteralPayload::mixed_delta: return fixture_mixed_delta();
    case LiteralPayload::client_only:
        return fixture_client_only(100U, 80, 103.0F, true);
    case LiteralPayload::entity_only:
        return fixture_entity_only(100U, 176, 104.0F);
    case LiteralPayload::missing_entity:
        return fixture_entity_only(99U, 200, 104.0F);
    case LiteralPayload::entity_recovery: return fixture_entity_recovery();
    case LiteralPayload::missing_client:
        return fixture_client_only(99U, 50, 103.0F, true);
    case LiteralPayload::client_recovery: return fixture_client_recovery();
    case LiteralPayload::visual_initial: return fixture_visual_initial();
    case LiteralPayload::visual_move: return fixture_visual_move();
    case LiteralPayload::visual_rotate: return fixture_visual_rotate();
    case LiteralPayload::visual_add_remove: return fixture_visual_add_remove();
    case LiteralPayload::visual_client_only:
        return fixture_client_only(100U, 80, 204.0F, false);
    }
    return {};
}

[[nodiscard]] OwnedServicePayload payload(
    std::vector<std::byte> body,
    const std::uint32_t sequence)
{
    OwnedServicePayload result;
    result.bytes = std::move(body);
    result.source_sequence = sequence;
    result.source_acknowledgement = sequence - 1U;
    result.source_reliable = true;
    result.reassembled = true;
    result.decompressed = true;
    result.acknowledgement_reliable = true;
    result.direction = NetchanDirection::server_to_client;
    return result;
}

[[nodiscard]] RuntimeReplayFixtureError failure(
    const RuntimeReplayFixtureErrorCode code,
    std::string context)
{
    return RuntimeReplayFixtureError{code, std::move(context)};
}

struct BaselineResult final {
    std::shared_ptr<const EntityBaselineRegistryState> registry;
    std::size_t consumed_bits{0U};
};

[[nodiscard]] std::shared_ptr<const DeltaSchemaRegistryState> schemas()
{
    DeltaSchemaRegistryBuilder builder;
    for (const auto body : std::array{
             bytes(kEntitySchema), bytes(kClientSchema), bytes(kWeaponSchema)}) {
        const auto parsed = DeltaDescriptionParser{}.parse(body, 0U);
        if (!parsed || !parsed.schema || !builder.insert(*parsed.schema)) {
            return {};
        }
    }
    return std::make_shared<const DeltaSchemaRegistryState>(
        std::move(builder).publish());
}

[[nodiscard]] BaselineResult baselines(
    const DeltaSchemaRegistryState& registry,
    const std::uint64_t generation)
{
    auto source = payload(bytes(kBaselines), 17U);
    const auto cursor = StockRuntimeSourceCursor::create(
        0U, 0U, source.bytes.size());
    if (!cursor) {
        return {};
    }
    const auto decoded = GoldSrcEntityBaselineDecoder{}.decode(
        EntityBaselineDecodeInput{
            &source, *cursor, 1U, generation, 1U,
            "entity_state_t", "entity_state_t", "entity_state_t",
            source.bytes.size() * 8U},
        registry);
    if (!decoded || !decoded.registry || decoded.entity_count != 4U ||
        decoded.end_cursor.absolute_bit_offset() != source.bytes.size() * 8U) {
        return {};
    }
    return {
        std::make_shared<const EntityBaselineRegistryState>(
            std::move(*decoded.registry)),
        decoded.end_cursor.absolute_bit_offset()};
}

[[nodiscard]] RuntimeReplayRecord record(
    const LiteralPayload literal,
    const RuntimeReplayFixtureRequest& request,
    const std::uint32_t sequence_offset,
    const std::size_t ordinal)
{
    auto source = payload(
        bytes(literal), request.source_sequence_base + sequence_offset);
    const auto cursor = StockRuntimeSourceCursor::create(
        0U, 0U, source.bytes.size());
    return RuntimeReplayRecord{
        request.generation,
        request.record_identity_base + ordinal,
        ordinal,
        std::move(source),
        *cursor};
}

} // namespace

RuntimeReplayFixtureResult make_runtime_replay_fixture(
    const RuntimeReplayFixtureRequest& request)
{
    constexpr auto maximum_sequence =
        (std::numeric_limits<std::uint32_t>::max)();
    constexpr auto maximum_identity =
        (std::numeric_limits<std::uint64_t>::max)();
    if (request.generation == 0U || request.source_sequence_base == 0U ||
        request.source_sequence_base > maximum_sequence - 8U ||
        request.record_identity_base == 0U ||
        request.record_identity_base > maximum_identity - 8U) {
        return {std::nullopt, failure(
            RuntimeReplayFixtureErrorCode::invalid_request,
            "literal replay fixture identity domain is invalid")};
    }

    try {
        auto registry = schemas();
        if (!registry) {
            return {std::nullopt, failure(
                RuntimeReplayFixtureErrorCode::schema_decode_failed,
                "literal delta schemas did not decode")};
        }
        auto baseline = baselines(*registry, request.generation);
        if (!baseline.registry) {
            return {std::nullopt, failure(
                RuntimeReplayFixtureErrorCode::baseline_decode_failed,
                "literal svc_spawnbaseline did not decode")};
        }

        RuntimeReplayFixture fixture;
        fixture.kind = request.kind;
        fixture.initialization.generation = request.generation;
        fixture.initialization.max_clients = 1U;
        fixture.initialization.schemas = std::move(registry);
        fixture.initialization.baselines = std::move(baseline.registry);
        fixture.initialization.schema_bindings.player_entity = "entity_state_t";
        fixture.initialization.schema_bindings.custom_entity = "entity_state_t";
        fixture.baseline_consumed_bits = baseline.consumed_bits;

        auto append = [&](const LiteralPayload body,
                          const std::uint32_t sequence_offset) {
            const auto ordinal = fixture.records.size() + 1U;
            fixture.records.push_back(
                record(body, request, sequence_offset, ordinal));
        };
        switch (request.kind) {
        case RuntimeReplayFixtureKind::basic_mixed:
            append(LiteralPayload::initial_mixed, 0U);
            append(LiteralPayload::mixed_delta, 1U);
            append(LiteralPayload::client_only, 3U);
            append(LiteralPayload::entity_only, 4U);
            break;
        case RuntimeReplayFixtureKind::missing_entity_base:
            append(LiteralPayload::initial_mixed, 0U);
            append(LiteralPayload::missing_entity, 5U);
            append(LiteralPayload::entity_recovery, 6U);
            break;
        case RuntimeReplayFixtureKind::reference_checker:
            append(LiteralPayload::initial_mixed, 0U);
            append(LiteralPayload::mixed_delta, 1U);
            append(LiteralPayload::client_only, 3U);
            append(LiteralPayload::entity_only, 4U);
            append(LiteralPayload::missing_entity, 5U);
            append(LiteralPayload::entity_recovery, 6U);
            append(LiteralPayload::missing_client, 7U);
            append(LiteralPayload::client_recovery, 8U);
            break;
        case RuntimeReplayFixtureKind::visual_entities:
            append(LiteralPayload::visual_initial, 0U);
            append(LiteralPayload::visual_move, 1U);
            append(LiteralPayload::visual_rotate, 2U);
            append(LiteralPayload::visual_add_remove, 3U);
            append(LiteralPayload::visual_client_only, 4U);
            fixture.presentation_offsets_seconds = {
                0.0, 1.5, 3.0, 4.5, 6.0};
            break;
        }
        return {std::move(fixture), std::nullopt};
    } catch (const std::bad_alloc&) {
        return {std::nullopt, failure(
            RuntimeReplayFixtureErrorCode::unable_to_retain_fixture,
            "unable to retain the literal replay fixture")};
    }
}

std::string_view to_string(const RuntimeReplayFixtureKind kind) noexcept
{
    switch (kind) {
    case RuntimeReplayFixtureKind::basic_mixed: return "basic-mixed";
    case RuntimeReplayFixtureKind::missing_entity_base:
        return "missing-entity-base";
    case RuntimeReplayFixtureKind::reference_checker:
        return "reference-checker";
    case RuntimeReplayFixtureKind::visual_entities:
        return "visual-entities";
    }
    return "unknown";
}

std::string_view to_string(const RuntimeReplayFixtureErrorCode code) noexcept
{
    switch (code) {
    case RuntimeReplayFixtureErrorCode::invalid_request:
        return "invalid_request";
    case RuntimeReplayFixtureErrorCode::schema_decode_failed:
        return "schema_decode_failed";
    case RuntimeReplayFixtureErrorCode::baseline_decode_failed:
        return "baseline_decode_failed";
    case RuntimeReplayFixtureErrorCode::unable_to_retain_fixture:
        return "unable_to_retain_fixture";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
