#include <hlclient/goldsrc/runtime_control_decoder.hpp>
#include <hlclient/goldsrc/runtime_replay_session.hpp>

#include "player_origin_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
namespace g = hlclient::goldsrc;
namespace player_fixture = hlclient::test::player_origin_fixture;

// Independently authored Protocol 48 fixture following ReHLDS SV_SendCustomization:
// opcode, player byte, resource type byte, name NUL, index short, size long,
// flags byte, and exactly 16 opaque MD5 bytes only when flags & RES_CUSTOM(4).
// The digest deliberately contains NULs and valid/invalid service opcodes.
constexpr std::array kCustomization{
    std::byte{46}, std::byte{1}, std::byte{3},
    std::byte{'t'}, std::byte{'e'}, std::byte{'m'}, std::byte{'p'},
    std::byte{'d'}, std::byte{'e'}, std::byte{'c'}, std::byte{'a'},
    std::byte{'l'}, std::byte{'.'}, std::byte{'w'}, std::byte{'a'},
    std::byte{'d'}, std::byte{0},
    std::byte{0x34}, std::byte{0x12},
    std::byte{0}, std::byte{0x10}, std::byte{0}, std::byte{0},
    std::byte{4},
    std::byte{0}, std::byte{1}, std::byte{7}, std::byte{15},
    std::byte{40}, std::byte{43}, std::byte{46}, std::byte{47},
    std::byte{52}, std::byte{53}, std::byte{56}, std::byte{57},
    std::byte{58}, std::byte{128}, std::byte{255}, std::byte{0}};

[[nodiscard]] g::StockRuntimeSourceCursor at(
    const std::size_t offset, const std::size_t size) {
    const auto result = g::StockRuntimeSourceCursor::create(offset, 0U, size);
    REQUIRE(result);
    return *result;
}

[[nodiscard]] g::OwnedServicePayload payload(std::vector<std::byte> bytes) {
    auto result = player_fixture::payload(std::move(bytes), 91U);
    result.source_acknowledgement = 73U;
    return result;
}

[[nodiscard]] g::RuntimeControlDecodeResult decode(
    const std::vector<std::byte>& bytes, g::RuntimeControlState& state,
    const std::size_t start = 0U) {
    const auto owned = payload(bytes);
    return g::RuntimeControlDecoder{}.decode_and_apply(
        {owned, at(start, owned.bytes.size()), state.source_generation(), 3U},
        state);
}

void append(std::vector<std::byte>& target,
            const std::span<const std::byte> bytes) {
    target.insert(target.end(), bytes.begin(), bytes.end());
}

[[nodiscard]] std::vector<std::byte> customization(
    const std::string_view name, const std::uint8_t flags) {
    std::vector<std::byte> result{std::byte{46}, std::byte{1}, std::byte{3}};
    for (const char value : name) {
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    result.push_back(std::byte{0});
    append(result, std::span{kCustomization}.subspan(17U, 6U));
    result.push_back(static_cast<std::byte>(flags));
    if ((flags & 4U) != 0U) append(result, std::span{kCustomization}.last(16U));
    return result;
}

void require_failure(const g::RuntimeControlDecodeResult& result,
                     const g::RuntimeControlDecodeErrorCode code,
                     const std::size_t offset) {
    CHECK_FALSE(result);
    CHECK_FALSE(result.batch);
    REQUIRE(result.error);
    CHECK(result.error->code == code);
    REQUIRE(result.error->cursor);
    CHECK(result.error->cursor->byte_offset() == offset);
}

TEST_CASE("Public server customization consumes its exact wire body and suffix",
          "[public-server][runtime-control][customization][no-stock]") {
    // Ignore two bytes before the explicitly provided validated cursor.
    std::vector<std::byte> bytes{std::byte{255}, std::byte{255}, std::byte{1}};
    append(bytes, kCustomization);
    append(bytes, std::array{std::byte{47}, std::byte{0x80}, std::byte{0x7f},
        std::byte{7}, std::byte{0}, std::byte{0}, std::byte{0x28}, std::byte{0x42},
        std::byte{1}});
    g::RuntimeControlState state{9U};
    const auto result = decode(bytes, state, 2U);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 5U);
    const auto& event = result.batch->events[1];
    CHECK(event.opcode == g::RuntimeControlOpcode::svc_customization);
    CHECK(event.kind == g::RuntimeControlMessageKind::customization);
    CHECK(g::to_string(event.opcode) == "svc_customization");
    CHECK(g::to_string(event.kind) == "customization");
    CHECK(event.provenance.start_cursor.byte_offset() == 3U);
    CHECK(event.provenance.end_cursor.byte_offset() == 3U + kCustomization.size());
    const auto& body = std::get<g::RuntimeControlCustomization>(event.body);
    CHECK(body.player_index == 1U);
    CHECK(body.resource_type == 3U);
    CHECK(body.resource_name_length == 13U);
    CHECK(body.resource_index == 0x1234U);
    CHECK(body.download_size == 4096);
    CHECK(body.flags == 4U);
    CHECK(body.opaque_hash_size == 16U);
    CHECK(result.batch->events[2].opcode == g::RuntimeControlOpcode::svc_crosshairangle);
    CHECK(result.batch->events[3].opcode == g::RuntimeControlOpcode::svc_time);
    REQUIRE(state.server_time());
    CHECK(state.server_time()->seconds == 42.0F);
    CHECK(result.batch->end_cursor.byte_offset() == bytes.size());
    CHECK(result.batch->consumed_byte_count == bytes.size() - 2U);
    CHECK(state.committed_message_count() == 5U);
}

TEST_CASE("Customization hash presence depends only on RES_CUSTOM flag",
          "[public-server][runtime-control][customization][flags][no-stock]") {
    for (unsigned value = 0U; value <= 255U; ++value) {
        CAPTURE(value);
        auto bytes = customization("../inert+8.wad", static_cast<std::uint8_t>(value));
        const auto boundary = bytes.size();
        bytes.push_back(std::byte{1});
        g::RuntimeControlState state{9U};
        const auto result = decode(bytes, state);
        REQUIRE(result);
        REQUIRE(result.batch->events.size() == 2U);
        const auto& body = std::get<g::RuntimeControlCustomization>(result.batch->events[0].body);
        CHECK(body.flags == value);
        CHECK(body.opaque_hash_size == ((value & 4U) != 0U ? 16U : 0U));
        CHECK(result.batch->events[0].provenance.end_cursor.byte_offset() == boundary);
        CHECK(result.batch->events[1].opcode == g::RuntimeControlOpcode::svc_nop);
    }
}

TEST_CASE("Customization bounded names numeric ranges and ownership are explicit",
          "[public-server][runtime-control][customization][bounds][ownership][no-stock]") {
    for (const auto length : {0U, 1U, 63U}) {
        CAPTURE(length);
        auto owned = payload(customization(std::string(length, 'x'), 4U));
        const auto result = g::RuntimeControlDecoder{}.decode_one(
            {owned, at(0U, owned.bytes.size()), 9U, 3U}, 0U);
        REQUIRE(result);
        owned.bytes.assign(owned.bytes.size(), std::byte{255});
        const auto& body = std::get<g::RuntimeControlCustomization>(result.event->body);
        CHECK(body.resource_name_length == length);
        CHECK(body.opaque_hash_size == 16U);
    }
    for (const auto flags : {std::uint8_t{0}, std::uint8_t{4}}) {
        auto bytes = customization("", flags);
        bytes[1] = std::byte{31};
        bytes[2] = std::byte{6};
        bytes[4] = std::byte{255};
        bytes[5] = std::byte{255};
        bytes[6] = std::byte{255};
        bytes[7] = std::byte{255};
        bytes[8] = std::byte{255};
        bytes[9] = std::byte{127};
        g::RuntimeControlState state{9U};
        const auto result = decode(bytes, state);
        REQUIRE(result);
        const auto& body = std::get<g::RuntimeControlCustomization>(result.batch->events[0].body);
        CHECK(body.player_index == 31U);
        CHECK(body.resource_type == 6U);
        CHECK(body.resource_index == 65535U);
        CHECK(body.download_size == 2147483647);
    }
}

TEST_CASE("Every incomplete customization fails atomically without opcode resync",
          "[public-server][runtime-control][customization][truncation][transactional][no-stock]") {
    for (const auto flags : {std::uint8_t{0}, std::uint8_t{4}}) {
        const auto complete = customization("tempdecal.wad", flags);
        for (std::size_t length = 1U; length < complete.size(); ++length) {
            CAPTURE(flags, length);
            std::vector<std::byte> bytes{std::byte{7}, std::byte{0}, std::byte{0},
                std::byte{0x80}, std::byte{0x3f}};
            append(bytes, std::span{complete}.first(length));
            g::RuntimeControlState state{9U};
            REQUIRE(decode({std::byte{5}, std::byte{9}, std::byte{0}}, state));
            const auto before = state;
            const auto result = decode(bytes, state);
            require_failure(result, g::RuntimeControlDecodeErrorCode::truncated_body, 5U);
            CHECK(result.error->wire_opcode == 46U);
            CHECK(result.error->context.find("tempdecal.wad") == std::string::npos);
            CHECK(state == before);
        }
    }
}

TEST_CASE("Customization rejects oversized names and invalid fields without private diagnostics",
          "[public-server][runtime-control][customization][bounds][privacy][no-stock]") {
    const std::string private_name = "private-marker-name-" + std::string(80U, 'x');
    for (unsigned bad = 0U; bad < 6U; ++bad) {
        CAPTURE(bad);
        auto bytes = customization(bad == 0U ? private_name : "tempdecal.wad", 4U);
        if (bad == 1U) bytes[1] = std::byte{32};
        if (bad == 2U) bytes[1] = std::byte{255};
        if (bad == 3U) bytes[2] = std::byte{7};
        if (bad == 4U) bytes[2] = std::byte{255};
        if (bad == 5U) bytes[22] = std::byte{128}; // negative signed size
        bytes.insert(bytes.begin(), std::byte{1});
        bytes.push_back(std::byte{1});
        g::RuntimeControlState state{9U};
        const auto before = state;
        const auto result = decode(bytes, state);
        require_failure(result, g::RuntimeControlDecodeErrorCode::invalid_numeric_value, 1U);
        CHECK(result.error->context.find("private-marker-name") == std::string::npos);
        CHECK(result.error->context.find("tempdecal.wad") == std::string::npos);
        CHECK(state == before);
    }
    // A following opcode-looking byte cannot rescue a nonterminated long name.
    std::vector<std::byte> bytes{std::byte{46}, std::byte{1}, std::byte{3}};
    bytes.insert(bytes.end(), 64U, std::byte{'x'});
    bytes.push_back(std::byte{43});
    g::RuntimeControlState state{9U};
    CHECK_FALSE(decode(bytes, state));
    CHECK(state.committed_message_count() == 0U);
}

TEST_CASE("Customization cannot swallow malformed continuation or mutate prior state",
          "[public-server][runtime-control][customization][transactional][no-stock]") {
    for (const auto suffix : {
        std::vector<std::byte>{std::byte{255}},
        std::vector<std::byte>{std::byte{7}, std::byte{0}, std::byte{0}},
        std::vector<std::byte>{std::byte{46}, std::byte{1}}}) {
        std::vector<std::byte> bytes{std::byte{1}};
        append(bytes, kCustomization);
        append(bytes, suffix);
        g::RuntimeControlState state{9U};
        REQUIRE(decode({std::byte{5}, std::byte{9}, std::byte{0}}, state));
        const auto before = state;
        const auto result = decode(bytes, state);
        CHECK_FALSE(result);
        CHECK_FALSE(result.batch);
        REQUIRE(result.error);
        REQUIRE(result.error->cursor);
        CHECK(result.error->cursor->byte_offset() == 1U + kCustomization.size());
        CHECK(state == before);
    }
}

TEST_CASE("Customization apply_events validates typed shape before atomic commit",
          "[public-server][runtime-control][customization][transactional][shared][no-stock]") {
    std::vector<std::byte> bytes{std::byte{1}};
    append(bytes, kCustomization);
    const auto owned = payload(bytes);
    const g::RuntimeControlDecoder decoder;
    const auto one = decoder.decode_one({owned, at(0U, bytes.size()), 9U, 3U}, 0U);
    const auto custom = decoder.decode_one({owned, at(1U, bytes.size()), 9U, 3U}, 1U);
    REQUIRE(one);
    REQUIRE(custom);
    for (unsigned bad = 0U; bad < 9U; ++bad) {
        CAPTURE(bad);
        std::array events{*one.event, *custom.event};
        auto& event = events[1];
        auto& body = std::get<g::RuntimeControlCustomization>(event.body);
        if (bad == 0U) body.player_index = 32U;
        if (bad == 1U) body.resource_type = 7U;
        if (bad == 2U) body.resource_name_length = 64U;
        if (bad == 3U) body.download_size = -1;
        if (bad == 4U) body.opaque_hash_size = 15U;
        if (bad == 5U) body.flags = 0U;
        if (bad == 6U) event.opcode = g::RuntimeControlOpcode::svc_nop;
        if (bad == 7U) event.kind = g::RuntimeControlMessageKind::nop;
        if (bad == 8U) event.body = g::RuntimeControlNop{};
        g::RuntimeControlState state{9U};
        const auto before = state;
        const auto error = decoder.apply_events(events, state);
        REQUIRE(error);
        CHECK(error->code == g::RuntimeControlDecodeErrorCode::invalid_configuration);
        CHECK(state == before);
    }
}

TEST_CASE("Customization passes through production replay around entity and clientdata records",
          "[public-server][runtime-replay][customization][integration][no-stock]") {
    const auto schemas = player_fixture::schemas();
    const auto baselines = player_fixture::baselines(*schemas);
    for (const bool before_entities : {true, false}) {
        for (const bool malformed : {false, true}) {
            CAPTURE(before_entities, malformed);
            hlclient::client::ClientWorldState world;
            g::RuntimeReplayInitialization init;
            init.generation = 9U;
            init.max_clients = 2U;
            init.receiving_player_entity = 1U;
            init.schemas = schemas;
            init.baselines = baselines;
            const auto initialized = g::RuntimeReplaySession::initialize(init, world);
            REQUIRE(initialized);
            const auto initial_observation = world.runtime_observation();
            const auto initial_controls = initialized.session->decoder_state().control_state();
            std::vector<std::byte> bytes;
            if (before_entities) append(bytes, kCustomization);
            append(bytes, player_fixture::full(1U, {10.0, 20.0, 30.0},
                {40.0, 50.0, 60.0}, false));
            if (!before_entities) append(bytes, kCustomization);
            if (malformed) bytes.push_back(std::byte{255});
            g::RuntimeReplayRecord record;
            record.generation = 9U;
            record.record_identity = 1U;
            record.record_ordinal = 1U;
            record.payload = payload(bytes);
            record.initial_cursor = at(0U, bytes.size());
            const auto result = initialized.session->apply_record(record);
            INFO((result.error ? result.error->context : "record accepted"));
            if (malformed) {
                CHECK_FALSE(result);
                CHECK(world.runtime_observation() == initial_observation);
                CHECK(initialized.session->decoder_state().control_state() == initial_controls);
                continue;
            }
            REQUIRE(result);
            REQUIRE(result.event);
            CHECK(result.event->clientdata_observed);
            CHECK(result.event->entities_observed);
            CHECK(result.event->decoded_batch.end_cursor.byte_offset() == bytes.size());
            REQUIRE(world.runtime_observation());
            CHECK(world.runtime_observation()->packet_entities.size() == 2U);
            std::size_t observed_customizations = 0U;
            for (const auto& item : result.event->decoded_batch.events) {
                const auto* control = std::get_if<g::RuntimeControlEvent>(&item);
                if (control && control->opcode == g::RuntimeControlOpcode::svc_customization) {
                    ++observed_customizations;
                    CHECK(std::get<g::RuntimeControlCustomization>(control->body).opaque_hash_size == 16U);
                }
            }
            CHECK(observed_customizations == 1U);
        }
    }
}

struct AuxiliaryFixture {
    std::vector<std::byte> wire;
    std::size_t text_length;
    std::optional<std::int32_t> request_id;
    std::optional<std::uint8_t> value;
};

[[nodiscard]] std::array<AuxiliaryFixture, 6U> auxiliary_fixtures() {
    return {{
        {{std::byte{49}, std::byte{'a'}, std::byte{0}}, 1U, {}, {}},
        // The director payload is opaque even when it contains NUL/opcodes.
        {{std::byte{51}, std::byte{4}, std::byte{0}, std::byte{46},
            std::byte{56}, std::byte{255}}, 0U, {}, {}},
        {{std::byte{54}, std::byte{'v'}, std::byte{0}, std::byte{255}},
            1U, {}, std::uint8_t{255}},
        {{std::byte{56}, std::byte{'h'}, std::byte{'+'}, std::byte{'8'},
            std::byte{0}}, 3U, {}, {}},
        {{std::byte{57}, std::byte{'r'}, std::byte{0}}, 1U, {}, {}},
        {{std::byte{58}, std::byte{42}, std::byte{0}, std::byte{0},
            std::byte{0}, std::byte{'r'}, std::byte{0}}, 1U,
            std::int32_t{42}, {}},
    }};
}

TEST_CASE("Verified public server auxiliary controls are bounded inert wire observations",
          "[public-server][runtime-control][auxiliary][no-stock]") {
    for (const auto& fixture : auxiliary_fixtures()) {
        CAPTURE(std::to_integer<unsigned>(fixture.wire.front()));
        auto bytes = fixture.wire;
        bytes.push_back(std::byte{1});
        g::RuntimeControlState state{9U};
        const auto result = decode(bytes, state);
        REQUIRE(result);
        REQUIRE(result.batch->events.size() == 2U);
        const auto& event = result.batch->events[0];
        CHECK(event.kind == g::RuntimeControlMessageKind::auxiliary_control);
        CHECK(static_cast<std::uint8_t>(event.opcode) == std::to_integer<std::uint8_t>(fixture.wire.front()));
        CHECK(event.provenance.end_cursor.byte_offset() == fixture.wire.size());
        const auto& body = std::get<g::RuntimeControlAuxiliaryControl>(event.body);
        CHECK(body.body_size == fixture.wire.size() - 1U);
        CHECK(body.text_length == fixture.text_length);
        CHECK(body.request_id == fixture.request_id);
        CHECK(body.value == fixture.value);
        CHECK(result.batch->events[1].opcode == g::RuntimeControlOpcode::svc_nop);
        for (std::size_t length = 1U; length < fixture.wire.size(); ++length) {
            CAPTURE(length);
            std::vector<std::byte> truncated{std::byte{1}};
            append(truncated, std::span{fixture.wire}.first(length));
            g::RuntimeControlState clean{9U};
            const auto before = clean;
            const auto rejected = decode(truncated, clean);
            require_failure(rejected, g::RuntimeControlDecodeErrorCode::truncated_body, 1U);
            CHECK(clean == before);
        }
    }
}

TEST_CASE("Auxiliary strings enforce independent limits and typed fields cannot be forged",
          "[public-server][runtime-control][auxiliary][bounds][shared][no-stock]") {
    for (const auto opcode : {49U, 54U, 56U, 57U, 58U}) {
        const std::size_t bound = opcode == 56U ? 128U : 255U;
        for (const auto length : {std::size_t{0U}, bound, bound + 1U}) {
            CAPTURE(opcode, length);
            std::vector<std::byte> bytes{static_cast<std::byte>(opcode)};
            if (opcode == 58U) append(bytes, std::array{std::byte{255},
                std::byte{255}, std::byte{255}, std::byte{255}});
            bytes.insert(bytes.end(), length, std::byte{'x'});
            bytes.push_back(std::byte{0});
            if (opcode == 54U) bytes.push_back(std::byte{0});
            bytes.push_back(std::byte{1});
            g::RuntimeControlState state{9U};
            const auto result = decode(bytes, state);
            if (length > bound) {
                require_failure(result, g::RuntimeControlDecodeErrorCode::invalid_numeric_value, 0U);
                CHECK(state.committed_message_count() == 0U);
            } else {
                REQUIRE(result);
                REQUIRE(result.batch->events.size() == 2U);
                CHECK(std::get<g::RuntimeControlAuxiliaryControl>(result.batch->events[0].body).text_length == length);
            }
        }
    }
    const g::RuntimeControlDecoder decoder;
    for (const auto& fixture : auxiliary_fixtures()) {
        const auto owned = payload(fixture.wire);
        const auto parsed = decoder.decode_one({owned, at(0U, owned.bytes.size()), 9U, 3U}, 0U);
        REQUIRE(parsed);
        for (unsigned mutation = 0U; mutation < 3U; ++mutation) {
            auto forged = *parsed.event;
            auto& body = std::get<g::RuntimeControlAuxiliaryControl>(forged.body);
            if (mutation == 0U) body.body_size = 0U;
            if (mutation == 1U) body.text_length = 256U;
            if (mutation == 2U) {
                if (body.request_id) body.request_id.reset();
                else body.request_id = 17;
            }
            g::RuntimeControlState state{9U};
            const auto before = state;
            REQUIRE(decoder.apply_events(std::span{&forged, 1U}, state));
            CHECK(state == before);
        }
    }
}

TEST_CASE("Intermission finale CD tracks and timescale keep their actual protocol opcodes",
          "[public-server][runtime-control][lookahead][no-stock]") {
    std::vector<std::byte> bytes{
        std::byte{30},
        std::byte{31}, std::byte{'f'}, std::byte{0},
        std::byte{32}, std::byte{0}, std::byte{255},
        std::byte{55}, std::byte{0}, std::byte{0}, std::byte{0x80}, std::byte{0x3f},
        std::byte{1}};
    g::RuntimeControlState state{9U};
    const auto result = decode(bytes, state);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 5U);
    CHECK(result.batch->events[0].opcode == g::RuntimeControlOpcode::svc_intermission);
    CHECK(std::get<g::RuntimeControlExactFixedBody>(result.batch->events[0].body).body_size == 0U);
    CHECK(result.batch->events[1].opcode == g::RuntimeControlOpcode::svc_finale);
    CHECK(std::get<g::RuntimeControlText>(result.batch->events[1].body).text_length == 1U);
    CHECK(result.batch->events[2].opcode == g::RuntimeControlOpcode::svc_cdtrack);
    CHECK(std::get<g::RuntimeControlExactFixedBody>(result.batch->events[2].body).body_size == 2U);
    CHECK(result.batch->events[3].opcode == g::RuntimeControlOpcode::svc_timescale);
    CHECK(std::get<g::RuntimeControlExactFixedBody>(result.batch->events[3].body).body_size == 4U);
    for (const std::uint32_t bits : {0x7f800000U, 0xff800000U, 0x7fc00000U}) {
        std::vector<std::byte> invalid{std::byte{55}};
        for (unsigned shift = 0U; shift < 32U; shift += 8U)
            invalid.push_back(static_cast<std::byte>((bits >> shift) & 255U));
        g::RuntimeControlState clean{9U};
        require_failure(decode(invalid, clean), g::RuntimeControlDecodeErrorCode::invalid_numeric_value, 0U);
    }
}

[[nodiscard]] std::vector<std::byte> ping_wire(const std::size_t entries) {
    hlclient::test::delta_fixture::BitWriter writer;
    writer.write(17U, 8U);
    for (std::size_t index = 0U; index < entries; ++index) {
        writer.write(1U, 1U);
        writer.write(static_cast<std::uint32_t>(index % 32U), 5U);
        writer.write(4095U, 12U);
        writer.write(127U, 7U);
    }
    writer.write(0U, 1U);
    writer.align_zero();
    return writer.bytes();
}

TEST_CASE("Ping bit records consume exact padding and cannot overrun their limit",
          "[public-server][runtime-control][pings][no-stock]") {
    for (const std::size_t count : {0U, 1U, 7U, 32U}) {
        CAPTURE(count);
        auto bytes = ping_wire(count);
        const auto size = bytes.size();
        bytes.push_back(std::byte{1});
        g::RuntimeControlState state{9U};
        const auto result = decode(bytes, state);
        REQUIRE(result);
        REQUIRE(result.batch->events.size() == 2U);
        const auto& event = result.batch->events[0];
        CHECK(event.opcode == g::RuntimeControlOpcode::svc_pings);
        CHECK(event.kind == g::RuntimeControlMessageKind::player_pings);
        const auto& body = std::get<g::RuntimeControlPings>(event.body);
        CHECK(body.entry_count == count);
        CHECK(body.encoded_body_bits == count * 25U + 1U);
        REQUIRE(body.entries.size() == count);
        for (std::size_t index = 0U; index < count; ++index) {
            CHECK(body.entries[index].player_index == index);
            CHECK(body.entries[index].ping_ms == 4095U);
            CHECK(body.entries[index].packet_loss == 127U);
        }
        CHECK(event.provenance.end_cursor.byte_offset() == size);
        for (std::size_t length = 1U; length < size; ++length) {
            g::RuntimeControlState clean{9U};
            CHECK_FALSE(decode(std::vector<std::byte>(bytes.begin(), bytes.begin() +
                static_cast<std::ptrdiff_t>(length)), clean));
            CHECK(clean.committed_message_count() == 0U);
        }
    }
    g::RuntimeControlState state{9U};
    CHECK_FALSE(decode(ping_wire(33U), state));
    CHECK(state.committed_message_count() == 0U);
}

TEST_CASE("Ping entries retain exact measurements zero values and repeated slots",
          "[public-server][runtime-control][pings][values][no-stock]") {
    // Literal independent GoldSrc little-endian bitstream: slots 0,31,7,7;
    // ping 0,4095,47,91; loss 0,127,3,1. The repeated slot is legal wire data.
    const std::vector<std::byte> wire{
        std::byte{0x11}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
        std::byte{0xfe}, std::byte{0xff}, std::byte{0xff}, std::byte{0x3f},
        std::byte{0x2f}, std::byte{0x30}, std::byte{0x78}, std::byte{0xb6},
        std::byte{0x20}, std::byte{0x00}};
    const std::vector<g::RuntimeControlPingEntry> expected{
        {0U, 0U, 0U}, {31U, 4095U, 127U}, {7U, 47U, 3U}, {7U, 91U, 1U}};
    auto owned = payload(wire);
    const auto result = g::RuntimeControlDecoder{}.decode_one(
        {owned, at(0U, owned.bytes.size()), 9U, 3U}, 0U);
    REQUIRE(result);
    owned.bytes.assign(owned.bytes.size(), std::byte{255});
    const auto& body = std::get<g::RuntimeControlPings>(result.event->body);
    CHECK(body.entry_count == 4U);
    CHECK(body.encoded_body_bits == 101U);
    CHECK(body.entries == expected);
    CHECK(result.event->provenance.end_cursor.byte_offset() == wire.size());
    for (std::size_t length = 1U; length < wire.size(); ++length) {
        CAPTURE(length);
        g::RuntimeControlState state{9U};
        const auto before = state;
        const auto rejected = decode(std::vector<std::byte>(wire.begin(),
            wire.begin() + static_cast<std::ptrdiff_t>(length)), state);
        require_failure(rejected, g::RuntimeControlDecodeErrorCode::truncated_body, 0U);
        CHECK(state == before);
    }
}

TEST_CASE("Ping typed events validate measurements and count atomically",
          "[public-server][runtime-control][pings][transactional][no-stock]") {
    std::vector<std::byte> bytes{std::byte{1}};
    append(bytes, ping_wire(2U));
    const auto owned = payload(bytes);
    const g::RuntimeControlDecoder decoder;
    const auto nop = decoder.decode_one({owned, at(0U, bytes.size()), 9U, 3U}, 0U);
    const auto pings = decoder.decode_one({owned, at(1U, bytes.size()), 9U, 3U}, 1U);
    REQUIRE(nop);
    REQUIRE(pings);
    for (unsigned bad = 0U; bad < 10U; ++bad) {
        CAPTURE(bad);
        std::array events{*nop.event, *pings.event};
        auto& event = events[1];
        auto& body = std::get<g::RuntimeControlPings>(event.body);
        if (bad == 0U) body.entries[0].player_index = 32U;
        if (bad == 1U) body.entries[0].ping_ms = 4096U;
        if (bad == 2U) body.entries[0].packet_loss = 128U;
        if (bad == 3U) body.entry_count = 1U;
        if (bad == 4U) body.entries.pop_back();
        if (bad == 5U) body.encoded_body_bits = 50U;
        if (bad == 6U) body.entry_count = 33U;
        if (bad == 7U) event.opcode = g::RuntimeControlOpcode::svc_nop;
        if (bad == 8U) event.kind = g::RuntimeControlMessageKind::nop;
        if (bad == 9U) event.body = g::RuntimeControlNop{};
        g::RuntimeControlState state{9U};
        const auto before = state;
        const auto error = decoder.apply_events(events, state);
        REQUIRE(error);
        CHECK(error->code == g::RuntimeControlDecodeErrorCode::invalid_configuration);
        CHECK(state == before);
    }
    std::array repeated{*nop.event, *pings.event};
    auto& entries = std::get<g::RuntimeControlPings>(repeated[1].body).entries;
    entries[1] = {entries[0].player_index, 17U, 2U};
    g::RuntimeControlState state{9U};
    CHECK_FALSE(decoder.apply_events(repeated, state));
    CHECK(state.committed_message_count() == 2U);
}

TEST_CASE("Voice data is framed as bounded opaque bytes rather than service opcodes",
          "[public-server][runtime-control][voice][no-stock]") {
    for (const std::size_t size : {0U, 16U, 4096U, 4097U}) {
        CAPTURE(size);
        std::vector<std::byte> bytes{std::byte{53}, std::byte{31},
            static_cast<std::byte>(size & 255U), static_cast<std::byte>(size >> 8U)};
        for (std::size_t index = 0U; index < size; ++index)
            bytes.push_back(kCustomization[index % kCustomization.size()]);
        const auto boundary = bytes.size();
        bytes.push_back(std::byte{1});
        g::RuntimeControlState state{9U};
        const auto result = decode(bytes, state);
        if (size > 4096U) {
            require_failure(result, g::RuntimeControlDecodeErrorCode::invalid_numeric_value, 0U);
            continue;
        }
        REQUIRE(result);
        REQUIRE(result.batch->events.size() == 2U);
        const auto& event = result.batch->events[0];
        CHECK(event.opcode == g::RuntimeControlOpcode::svc_voicedata);
        const auto& body = std::get<g::RuntimeControlVoiceData>(event.body);
        CHECK(body.sender_index == 31U);
        CHECK(body.payload_size == size);
        CHECK(event.provenance.end_cursor.byte_offset() == boundary);
        if (size == 16U) {
            for (std::size_t length = 1U; length < boundary; ++length) {
                g::RuntimeControlState clean{9U};
                const auto rejected = decode(std::vector<std::byte>(bytes.begin(), bytes.begin() +
                    static_cast<std::ptrdiff_t>(length)), clean);
                require_failure(rejected, g::RuntimeControlDecodeErrorCode::truncated_body, 0U);
                CHECK(clean.committed_message_count() == 0U);
            }
        }
    }
    for (const auto sender : {32U, 255U}) {
        g::RuntimeControlState state{9U};
        require_failure(decode({std::byte{53}, static_cast<std::byte>(sender),
            std::byte{0}, std::byte{0}}, state),
            g::RuntimeControlDecodeErrorCode::invalid_numeric_value, 0U);
    }
}

TEST_CASE("Public control lookahead stays inert through production replay",
          "[public-server][runtime-replay][lookahead][integration][no-stock]") {
    const auto schemas = player_fixture::schemas();
    hlclient::client::ClientWorldState world;
    g::RuntimeReplayInitialization init;
    init.generation = 9U;
    init.max_clients = 2U;
    init.receiving_player_entity = 1U;
    init.schemas = schemas;
    init.baselines = player_fixture::baselines(*schemas);
    const auto initialized = g::RuntimeReplaySession::initialize(init, world);
    REQUIRE(initialized);
    auto bytes = ping_wire(2U);
    append(bytes, kCustomization);
    for (const auto& fixture : auxiliary_fixtures()) append(bytes, fixture.wire);
    append(bytes, std::array{std::byte{53}, std::byte{1}, std::byte{2},
        std::byte{0}, std::byte{255}, std::byte{46}});
    append(bytes, player_fixture::full(1U, {10.0, 20.0, 30.0},
        {40.0, 50.0, 60.0}, false));
    g::RuntimeReplayRecord record;
    record.generation = 9U;
    record.record_identity = 1U;
    record.record_ordinal = 1U;
    record.payload = payload(bytes);
    record.initial_cursor = at(0U, bytes.size());
    const auto result = initialized.session->apply_record(record);
    INFO((result.error ? result.error->context : "record accepted"));
    REQUIRE(result);
    REQUIRE(result.event);
    CHECK(result.event->decoded_batch.end_cursor.byte_offset() == bytes.size());
    CHECK(result.event->clientdata_observed);
    CHECK(result.event->entities_observed);
    REQUIRE(world.runtime_observation());
    CHECK(world.runtime_observation()->packet_entities.size() == 2U);
}

constexpr std::array kPlayerDecal{
    std::byte{23}, std::byte{112}, std::byte{255},
    std::byte{0}, std::byte{128}, // signed minimum coordinate
    std::byte{255}, std::byte{127}, // signed maximum coordinate
    std::byte{0}, std::byte{0},
    std::byte{255}, std::byte{255}, // opaque entity reference
    std::byte{255}};

TEST_CASE("Player decal after customization has exact inert framing and time continuation",
          "[public-server][runtime-control][customization][player-decal][no-stock]") {
    std::vector<std::byte> bytes;
    append(bytes, kCustomization);
    append(bytes, kPlayerDecal);
    append(bytes, std::array{std::byte{7}, std::byte{0}, std::byte{0},
        std::byte{0x80}, std::byte{0x3f}});
    g::RuntimeControlState state{9U};
    const auto result = decode(bytes, state);
    REQUIRE(result);
    REQUIRE(result.batch->events.size() == 3U);
    const auto& event = result.batch->events[1];
    CHECK(event.opcode == g::RuntimeControlOpcode::svc_temp_entity);
    CHECK(event.kind == g::RuntimeControlMessageKind::temporary_entity);
    const auto& body = std::get<g::RuntimeControlPlayerDecal>(event.body);
    CHECK(body.player == 255U);
    CHECK(body.coordinate_eighths == std::array<std::int16_t, 3U>{-32768, 32767, 0});
    CHECK(body.entity_reference == 65535U);
    CHECK(body.decal_reference == 255U);
    CHECK(event.provenance.start_cursor.byte_offset() == kCustomization.size());
    CHECK(event.provenance.end_cursor.byte_offset() == kCustomization.size() + kPlayerDecal.size());
    REQUIRE(state.server_time());
    CHECK(state.server_time()->seconds == 1.0F);
    for (std::size_t length = 1U; length < kPlayerDecal.size(); ++length) {
        CAPTURE(length);
        std::vector<std::byte> incomplete;
        append(incomplete, kCustomization);
        append(incomplete, std::span{kPlayerDecal}.first(length));
        g::RuntimeControlState clean{9U};
        const auto before = clean;
        const auto rejected = decode(incomplete, clean);
        require_failure(rejected, g::RuntimeControlDecodeErrorCode::truncated_body, kCustomization.size());
        CHECK(clean == before);
    }
    auto forged = event;
    forged.opcode = g::RuntimeControlOpcode::svc_customization;
    g::RuntimeControlState clean{9U};
    const auto before = clean;
    REQUIRE(g::RuntimeControlDecoder{}.apply_events(std::span{&forged, 1U}, clean));
    CHECK(clean == before);
}

TEST_CASE("Player decal cannot interrupt full production replay or fabricate a temporary effect",
          "[public-server][runtime-replay][customization][player-decal][integration][no-stock]") {
    const auto schemas = player_fixture::schemas();
    hlclient::client::ClientWorldState world;
    g::RuntimeReplayInitialization init;
    init.generation = 9U;
    init.max_clients = 2U;
    init.receiving_player_entity = 1U;
    init.schemas = schemas;
    init.baselines = player_fixture::baselines(*schemas);
    const auto initialized = g::RuntimeReplaySession::initialize(init, world);
    REQUIRE(initialized);
    std::vector<std::byte> bytes;
    append(bytes, kCustomization);
    append(bytes, kPlayerDecal);
    append(bytes, player_fixture::full(1U, {10.0, 20.0, 30.0},
        {40.0, 50.0, 60.0}, false));
    g::RuntimeReplayRecord record;
    record.generation = 9U;
    record.record_identity = 1U;
    record.record_ordinal = 1U;
    record.payload = payload(bytes);
    record.initial_cursor = at(0U, bytes.size());
    const auto result = initialized.session->apply_record(record);
    INFO((result.error ? result.error->context : "record accepted"));
    REQUIRE(result);
    REQUIRE(result.event);
    CHECK(result.event->decoded_batch.end_cursor.byte_offset() == bytes.size());
    CHECK(result.event->temporary_effects.empty());
    CHECK(result.event->entities_observed);
    CHECK(result.event->clientdata_observed);
}
// Independent Valve SDK UTIL_HudMessage layout. x/y are signed 13-bit fixed
// point; timing words are unsigned 8-bit fixed point. Only effect 2 adds fxTime.
[[nodiscard]] std::vector<std::byte> text_message(
    const std::uint8_t effect, const std::string_view text) {
    std::vector<std::byte> result{
        std::byte{23}, std::byte{29}, std::byte{255},
        std::byte{0}, std::byte{128}, std::byte{255}, std::byte{127},
        static_cast<std::byte>(effect),
        std::byte{0}, std::byte{1}, std::byte{255}, std::byte{128},
        std::byte{7}, std::byte{9}, std::byte{23}, std::byte{29},
        std::byte{0x34}, std::byte{0x12}, std::byte{0}, std::byte{128},
        std::byte{255}, std::byte{255}};
    if (effect == 2U) append(result, std::array{std::byte{0xcd}, std::byte{0xab}});
    for (const char value : text)
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    result.push_back(std::byte{0});
    return result;
}

TEST_CASE("TE_TEXTMESSAGE preserves exact normal and fx2 boundaries before following controls",
          "[public-server][runtime-control][textmessage][no-stock]") {
    for (const auto effect : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{255}}) {
        CAPTURE(effect);
        const std::string text{"quit;exec private.cfg\nconnect 1.2.3.4:5\x01\x09\x2b"};
        const auto message = text_message(effect, text);
        CHECK(message.size() == text.size() + (effect == 2U ? 25U : 23U));
        std::vector<std::byte> bytes{std::byte{255}, std::byte{1}};
        append(bytes, message);
        append(bytes, std::array{std::byte{7}, std::byte{0}, std::byte{0},
            std::byte{0x80}, std::byte{0x3f}, std::byte{1}});
        g::RuntimeControlState state{9U};
        const auto result = decode(bytes, state, 1U);
        REQUIRE(result);
        REQUIRE(result.batch->events.size() == 4U);
        const auto& event = result.batch->events[1U];
        CHECK(event.opcode == g::RuntimeControlOpcode::svc_temp_entity);
        CHECK(event.kind == g::RuntimeControlMessageKind::temporary_entity);
        CHECK(event.provenance.start_cursor.byte_offset() == 2U);
        CHECK(event.provenance.end_cursor.byte_offset() == 2U + message.size());
        const auto& body = std::get<g::RuntimeControlTextMessage>(event.body);
        CHECK(body.channel == 255U);
        CHECK(body.position_fixed13 == std::array<std::int16_t, 2U>{-32768, 32767});
        CHECK(body.effect == effect);
        CHECK(body.text_color == std::array<std::uint8_t, 4U>{0U, 1U, 255U, 128U});
        CHECK(body.effect_color == std::array<std::uint8_t, 4U>{7U, 9U, 23U, 29U});
        CHECK(body.fade_in_ticks == 0x1234U);
        CHECK(body.fade_out_ticks == 0x8000U);
        CHECK(body.hold_ticks == 0xffffU);
        CHECK(body.text_length == text.size());
        if (effect == 2U) CHECK(body.effect_ticks == 0xabcdU);
        else CHECK_FALSE(body.effect_ticks);
        CHECK(result.batch->events[2U].opcode == g::RuntimeControlOpcode::svc_time);
        CHECK(result.batch->events[3U].opcode == g::RuntimeControlOpcode::svc_nop);
        CHECK(result.batch->end_cursor.byte_offset() == bytes.size());
        CHECK(state.committed_message_count() == 4U);
        REQUIRE(state.server_time());
        CHECK(state.server_time()->seconds == 1.0F);
        // Command-looking text and embedded service opcodes remain one inert
        // temporary-entity record, never a stufftext/console command event.
        for (const auto& control : result.batch->events)
            CHECK(control.opcode != g::RuntimeControlOpcode::svc_stufftext);
    }
}

TEST_CASE("TE_TEXTMESSAGE text bounds and typed metadata are independently owned and validated",
          "[public-server][runtime-control][textmessage][bounds][ownership][no-stock]") {
    CHECK(g::kMaximumRuntimeTextMessageBytes == 511U);
    for (const auto effect : {std::uint8_t{0}, std::uint8_t{2}}) {
        for (const auto length : {0U, 1U, 511U}) {
            CAPTURE(effect, length);
            auto bytes = text_message(effect, std::string(length, 'x'));
            const auto boundary = bytes.size();
            bytes.push_back(std::byte{1});
            auto owned = payload(bytes);
            const auto one = g::RuntimeControlDecoder{}.decode_one(
                {owned, at(0U, owned.bytes.size()), 9U, 3U}, 0U);
            REQUIRE(one);
            owned.bytes.assign(owned.bytes.size(), std::byte{255});
            CHECK(std::get<g::RuntimeControlTextMessage>(one.event->body).text_length == length);
            CHECK(one.event->provenance.end_cursor.byte_offset() == boundary);
            g::RuntimeControlState state{9U};
            const auto result = decode(bytes, state);
            REQUIRE(result);
            REQUIRE(result.batch->events.size() == 2U);
            CHECK(result.batch->events[1U].opcode == g::RuntimeControlOpcode::svc_nop);
            CHECK(result.batch->events[1U].provenance.start_cursor.byte_offset() == boundary);
        }
        for (const auto length : {512U, 700U}) {
            CAPTURE(effect, length);
            const std::string private_text = "private-message-" + std::string(length - 16U, 'x');
            auto bytes = text_message(effect, private_text);
            bytes.insert(bytes.begin(), std::byte{1});
            bytes.push_back(std::byte{1});
            g::RuntimeControlState state{9U};
            const auto before = state;
            const auto rejected = decode(bytes, state);
            require_failure(rejected, g::RuntimeControlDecodeErrorCode::invalid_numeric_value, 1U);
            CHECK(rejected.error->temporary_entity_type == 29U);
            CHECK(rejected.error->context.find("private-message-") == std::string::npos);
            CHECK(state == before);
        }
    }

    const auto bytes = text_message(2U, "inert");
    auto owned = payload(bytes);
    const auto decoded = g::RuntimeControlDecoder{}.decode_one(
        {owned, at(0U, owned.bytes.size()), 9U, 3U}, 0U);
    REQUIRE(decoded);
    for (unsigned bad = 0U; bad < 6U; ++bad) {
        CAPTURE(bad);
        auto event = *decoded.event;
        auto& body = std::get<g::RuntimeControlTextMessage>(event.body);
        if (bad == 0U) body.text_length = 512U;
        if (bad == 1U) body.effect_ticks.reset();
        if (bad == 2U) body.effect = 0U;
        if (bad == 3U) event.opcode = g::RuntimeControlOpcode::svc_stufftext;
        if (bad == 4U) event.kind = g::RuntimeControlMessageKind::nop;
        if (bad == 5U) event.body = g::RuntimeControlNop{};
        g::RuntimeControlState state{9U};
        const auto before = state;
        const auto rejected = g::RuntimeControlDecoder{}.apply_events(std::span{&event, 1U}, state);
        REQUIRE(rejected);
        CHECK(rejected->code == g::RuntimeControlDecodeErrorCode::invalid_configuration);
        CHECK(state == before);
    }
}

TEST_CASE("TE_TEXTMESSAGE incomplete fixed fields optional timing and text are atomic",
          "[public-server][runtime-control][textmessage][truncation][transactional][no-stock]") {
    for (const auto effect : {std::uint8_t{0}, std::uint8_t{2}}) {
        const auto complete = text_message(effect, "private-message;quit\x01\x09\x2b");
        for (std::size_t length = 1U; length < complete.size(); ++length) {
            CAPTURE(effect, length);
            std::vector<std::byte> bytes{std::byte{7}, std::byte{0}, std::byte{0},
                std::byte{0x80}, std::byte{0x3f}};
            append(bytes, std::span{complete}.first(length));
            g::RuntimeControlState state{9U};
            REQUIRE(decode({std::byte{5}, std::byte{9}, std::byte{0}}, state));
            const auto before = state;
            const auto rejected = decode(bytes, state);
            require_failure(rejected, g::RuntimeControlDecodeErrorCode::truncated_body, 5U);
            CHECK(rejected.error->wire_opcode == 23U);
            if (length >= 2U) CHECK(rejected.error->temporary_entity_type == 29U);
            CHECK(rejected.error->context.find("private-message") == std::string::npos);
            CHECK(state == before);
        }
        auto missing_terminator = complete;
        missing_terminator.back() = std::byte{1};
        g::RuntimeControlState state{9U};
        const auto before = state;
        require_failure(decode(missing_terminator, state),
            g::RuntimeControlDecodeErrorCode::truncated_body, 0U);
        CHECK(state == before);
    }
}

TEST_CASE("TE_TEXTMESSAGE does not hide invalid following opcode or mutate production replay",
          "[public-server][runtime-replay][textmessage][integration][transactional][no-stock]") {
    const auto schemas = player_fixture::schemas();
    for (const bool malformed : {false, true}) {
        CAPTURE(malformed);
        hlclient::client::ClientWorldState world;
        g::RuntimeReplayInitialization init;
        init.generation = 9U;
        init.max_clients = 2U;
        init.receiving_player_entity = 1U;
        init.schemas = schemas;
        init.baselines = player_fixture::baselines(*schemas);
        const auto initialized = g::RuntimeReplaySession::initialize(init, world);
        REQUIRE(initialized);
        const auto initial_observation = world.runtime_observation();
        const auto initial_controls = initialized.session->decoder_state().control_state();
        auto bytes = text_message(2U, "quit;exec test.cfg;connect 1.2.3.4:5");
        append(bytes, player_fixture::full(1U, {10.0, 20.0, 30.0},
            {40.0, 50.0, 60.0}, false));
        const auto malformed_offset = bytes.size();
        if (malformed) bytes.push_back(std::byte{255});
        g::RuntimeReplayRecord record;
        record.generation = 9U;
        record.record_identity = 1U;
        record.record_ordinal = 1U;
        record.payload = payload(bytes);
        record.initial_cursor = at(0U, bytes.size());
        const auto result = initialized.session->apply_record(record);
        INFO((result.error ? result.error->context : "record accepted"));
        if (malformed) {
            CHECK_FALSE(result);
            REQUIRE(result.error);
            REQUIRE(result.error->decoder_cursor);
            CHECK(result.error->decoder_cursor->byte_offset() == malformed_offset);
            CHECK(world.runtime_observation() == initial_observation);
            CHECK(initialized.session->decoder_state().control_state() == initial_controls);
        } else {
            REQUIRE(result);
            REQUIRE(result.event);
            CHECK(result.event->decoded_batch.end_cursor.byte_offset() == bytes.size());
            CHECK(result.event->temporary_effects.empty());
            CHECK(result.event->entities_observed);
            CHECK(result.event->clientdata_observed);
            CHECK(initialized.session->status() == g::RuntimeReplaySessionStatus::active);
        }
    }
}
} // namespace
