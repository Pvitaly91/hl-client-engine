#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/usercmd_schema_binding.hpp>
#include <hlclient/goldsrc/usercmd_transmission_stage.hpp>
#include <hlclient/input/input_state_tracker.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace camera = hlclient::gameplay_camera;
namespace gameplay = hlclient::gameplay_input;
namespace goldsrc = hlclient::goldsrc;
namespace input = hlclient::input;
namespace network = hlclient::network;

constexpr std::array<std::uint8_t, 15U> kWidths{
    9U, 8U, 16U, 16U, 16U, 12U, 8U, 12U,
    12U, 8U, 16U, 6U, 16U, 16U, 16U};
using RawCommand = std::array<std::uint32_t, 15U>;

class FakeHldsTransport final : public network::IDatagramTransport {
public:
    struct DatagramRecord {
        std::optional<network::NetworkAddress> source;
        network::NetworkAddress destination;
        std::vector<std::byte> payload;
    };

    [[nodiscard]] network::DatagramLocalAddressResult local_address() const override
    {
        return {local, {}};
    }
    [[nodiscard]] network::DatagramSendResult send_to(
        const network::NetworkAddress& destination,
        const std::span<const std::byte> payload) override
    {
        sent.push_back({local, destination, {payload.begin(), payload.end()}});
        return {network::DatagramSendStatus::sent, {}};
    }
    [[nodiscard]] network::DatagramTransportReceiveResult receive(
        const std::size_t) override
    {
        if (incoming.empty()) {
            return {network::DatagramTransportReceiveStatus::would_block,
                std::nullopt, std::nullopt, 0U, {}};
        }
        auto next = std::move(incoming.front());
        incoming.pop_front();
        return next;
    }
    void queue(const network::NetworkAddress source, std::vector<std::byte> payload)
    {
        const auto size = payload.size();
        incoming.push_back({network::DatagramTransportReceiveStatus::received,
            network::Datagram{source, std::move(payload)}, source, size, {}});
    }

    std::optional<network::NetworkAddress> local{
        network::NetworkAddress::loopback(31'300U)};
    std::deque<network::DatagramTransportReceiveResult> incoming;
    std::vector<DatagramRecord> sent;
};

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    auto result = goldsrc::NetchanSequence::from_numeric(value);
    if (!result) {
        throw std::runtime_error{"invalid fake-HLDS netchan sequence"};
    }
    return *result;
}

[[nodiscard]] std::vector<std::byte> bootstrap_packet()
{
    goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{sequence(1U), {false, false}},
            goldsrc::NetchanAcknowledgementWord{sequence(0U), false}},
        {},
        {std::byte{0x42U}},
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    if (!encoded || !encoded.datagram) {
        throw std::runtime_error{"fake-HLDS bootstrap encoding failed"};
    }
    return std::move(*encoded.datagram);
}

[[nodiscard]] std::vector<std::byte> reliable_gap_packet()
{
    goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{sequence(2U), {false, false}},
            goldsrc::NetchanAcknowledgementWord{sequence(3U), false}},
        {},
        {std::byte{0x43U}},
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    if (!encoded || !encoded.datagram) {
        throw std::runtime_error{"fake-HLDS reliable-gap encoding failed"};
    }
    return std::move(*encoded.datagram);
}

[[nodiscard]] goldsrc::NetchanDriverConfig driver_config()
{
    goldsrc::NetchanDriverConfig config;
    config.channel_inactivity_timeout = 2'000ms;
    config.fragment_transfer_timeout = 500ms;
    config.maximum_outgoing_packets_per_update = 1U;
    return config;
}

[[nodiscard]] goldsrc::GoldSrcUserCmdSchemaBinding make_binding()
{
    auto registry = goldsrc::make_synthetic_usercmd_schema_registry();
    REQUIRE(registry);
    auto bound = goldsrc::bind_goldsrc_usercmd_schema(*registry.registry);
    REQUIRE(bound);
    return std::move(*bound.binding);
}

[[nodiscard]] gameplay::GameplayInputIntent make_intent(
    const std::span<const input::InputEvent> events)
{
    input::InputStateTracker tracker;
    tracker.begin_frame();
    for (const auto& event : events) {
        tracker.apply_event(event);
    }
    const auto snapshot = tracker.publish_snapshot();
    tracker.end_frame();
    auto bindings = gameplay::GameplayInputBindings::project_default_v1();
    REQUIRE(bindings);
    auto built = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, *bindings.bindings, gameplay::MouseLookConfig{}, 0.01);
    REQUIRE(built);
    return std::move(*built.intent);
}

[[nodiscard]] gameplay::GameplayInputIntent make_intent(
    const std::initializer_list<input::InputEvent> events)
{
    return make_intent(
        std::span<const input::InputEvent>{events.begin(), events.size()});
}

[[nodiscard]] camera::GameplayCameraState make_camera(
    const double yaw,
    const double pitch = 0.0)
{
    camera::GameplayCameraStateCreateInfo info;
    info.yaw_degrees = yaw;
    info.pitch_degrees = pitch;
    auto created = camera::GameplayCameraState::create(info);
    REQUIRE(created);
    return std::move(*created.state);
}

struct ScriptFrame {
    std::vector<input::InputEvent> events;
    double yaw{0.0};
    double pitch{0.0};
    RawCommand expected{};
};

[[nodiscard]] std::uint32_t angle_raw(const double degrees)
{
    auto normalized = std::fmod(degrees, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    auto value = static_cast<std::uint64_t>(
        std::floor(normalized * 65'536.0 / 360.0 + 0.5));
    value %= 65'536U;
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] RawCommand expected_command(
    const float forward,
    const float side,
    const double yaw,
    const double pitch,
    const std::uint16_t buttons = 0U)
{
    RawCommand values{};
    values[1U] = 10U;
    values[2U] = angle_raw(yaw);
    values[3U] = angle_raw(pitch);
    values[4U] = buttons;
    values[5U] = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(forward)) & 0x0fffU;
    values[7U] = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(side)) & 0x0fffU;
    return values;
}

[[nodiscard]] std::optional<std::uint32_t> read_lsb_bits(
    const std::span<const std::byte> bytes,
    std::size_t& bit_offset,
    const std::size_t bit_limit,
    const std::uint8_t width)
{
    if (width > 32U || bit_offset > bit_limit ||
        width > bit_limit - bit_offset || bit_limit > bytes.size() * 8U) {
        return std::nullopt;
    }
    std::uint32_t value = 0U;
    for (std::uint8_t bit = 0U; bit < width; ++bit) {
        const auto absolute = bit_offset + bit;
        const auto byte = std::to_integer<std::uint8_t>(bytes[absolute / 8U]);
        value |= static_cast<std::uint32_t>((byte >> (absolute & 7U)) & 1U)
                 << bit;
    }
    bit_offset += width;
    return value;
}

[[nodiscard]] std::uint8_t crc8_update(
    std::uint8_t crc,
    const std::uint8_t value)
{
    crc ^= value;
    for (std::size_t bit = 0U; bit < 8U; ++bit) {
        crc = (crc & 0x80U) != 0U
            ? static_cast<std::uint8_t>((crc << 1U) ^ 0x07U)
            : static_cast<std::uint8_t>(crc << 1U);
    }
    return crc;
}

[[nodiscard]] std::uint8_t independent_checksum(
    const std::uint32_t outgoing_sequence,
    const std::span<const std::byte> body)
{
    std::uint8_t crc = 0xa7U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        crc = crc8_update(crc, static_cast<std::uint8_t>(
            outgoing_sequence >> (index * 8U)));
    }
    for (const auto value : body) {
        crc = crc8_update(crc, std::to_integer<std::uint8_t>(value));
    }
    return crc8_update(crc, 0U);
}

struct IndependentlyDecodedMove {
    std::uint32_t outgoing_sequence{0U};
    std::uint8_t loss_metadata{0U};
    std::size_t backup_count{0U};
    std::size_t new_count{0U};
    std::vector<RawCommand> commands;
};

[[nodiscard]] IndependentlyDecodedMove decode_move_independently(
    const std::span<const std::byte> payload,
    const std::uint32_t outgoing_sequence)
{
    REQUIRE(payload.size() >= 4U);
    REQUIRE(std::to_integer<std::uint8_t>(payload[0U]) == 0xe1U);
    CHECK(std::to_integer<std::uint8_t>(payload[1U]) ==
        independent_checksum(outgoing_sequence, payload.subspan(2U)));
    const auto count_byte = std::to_integer<std::uint8_t>(payload[3U]);
    IndependentlyDecodedMove move;
    move.outgoing_sequence = outgoing_sequence;
    move.loss_metadata = std::to_integer<std::uint8_t>(payload[2U]);
    move.backup_count = count_byte & 0x0fU;
    move.new_count = count_byte >> 4U;
    REQUIRE(move.new_count > 0U);
    REQUIRE(move.backup_count + move.new_count <= 16U);

    auto base = RawCommand{};
    auto cursor = std::size_t{4U};
    const auto command_count = move.backup_count + move.new_count;
    move.commands.reserve(command_count);
    for (std::size_t command_index = 0U;
         command_index < command_count;
         ++command_index) {
        REQUIRE(payload.size() - cursor >= 2U);
        const auto delta_bits = static_cast<std::size_t>(
            std::to_integer<std::uint8_t>(payload[cursor])) |
            (static_cast<std::size_t>(
                 std::to_integer<std::uint8_t>(payload[cursor + 1U]))
             << 8U);
        cursor += 2U;
        const auto delta_bytes = (delta_bits + 7U) / 8U;
        REQUIRE(delta_bits != 0U);
        REQUIRE(delta_bytes <= payload.size() - cursor);
        const auto delta = payload.subspan(cursor, delta_bytes);
        auto bit_offset = std::size_t{0U};
        const auto mask_count = read_lsb_bits(delta, bit_offset, delta_bits, 8U);
        REQUIRE(mask_count);
        REQUIRE(*mask_count <= 2U);
        std::array<std::uint8_t, 2U> mask{};
        for (std::size_t index = 0U; index < *mask_count; ++index) {
            const auto value = read_lsb_bits(delta, bit_offset, delta_bits, 8U);
            REQUIRE(value);
            mask[index] = static_cast<std::uint8_t>(*value);
        }
        if (*mask_count != 0U) {
            REQUIRE(mask[*mask_count - 1U] != 0U);
        }
        REQUIRE((mask[1U] & 0x80U) == 0U);

        auto current = base;
        for (std::size_t field = 0U; field < current.size(); ++field) {
            if ((mask[field / 8U] &
                    (std::uint8_t{1U} << (field & 7U))) == 0U) {
                continue;
            }
            const auto raw = read_lsb_bits(
                delta, bit_offset, delta_bits, kWidths[field]);
            REQUIRE(raw);
            current[field] = *raw;
        }
        while ((bit_offset & 7U) != 0U) {
            const auto padding = read_lsb_bits(delta, bit_offset, delta_bits, 1U);
            REQUIRE(padding);
            REQUIRE(*padding == 0U);
        }
        REQUIRE(bit_offset == delta_bits);
        move.commands.push_back(current);
        base = current;
        cursor += delta_bytes;
    }
    REQUIRE(cursor == payload.size());
    return move;
}

struct CapturedClientDatagram {
    network::NetworkAddress source;
    network::NetworkAddress destination;
    std::uint32_t outgoing_sequence{0U};
    bool reliable_present{false};
    std::vector<std::byte> reliable_payload;
    std::optional<std::vector<std::byte>> move_payload;
};

struct CampaignResult {
    std::vector<CapturedClientDatagram> datagrams;
    std::size_t final_history_size{0U};
    std::size_t cleanup_count{0U};
    network::NetworkAddress local_endpoint;
    network::NetworkAddress remote_endpoint;
};

struct CampaignOptions {
    bool queue_reliable{false};
    bool trigger_reliable_retry{false};
};

enum class IndependentSequenceComparison : std::uint8_t {
    equal,
    newer,
    older,
    half_range_ambiguous,
};

[[nodiscard]] IndependentSequenceComparison compare_sequences_independently(
    const std::uint32_t candidate,
    const std::uint32_t reference) noexcept
{
    constexpr auto mask = std::uint32_t{0x3fff'ffffU};
    constexpr auto half_range = std::uint32_t{0x2000'0000U};
    const auto distance = (candidate - reference) & mask;
    if (distance == 0U) {
        return IndependentSequenceComparison::equal;
    }
    if (distance == half_range) {
        return IndependentSequenceComparison::half_range_ambiguous;
    }
    return distance < half_range
        ? IndependentSequenceComparison::newer
        : IndependentSequenceComparison::older;
}

class IndependentFakeHlds final {
public:
    IndependentFakeHlds(
        const network::NetworkAddress expected_client_endpoint,
        const network::NetworkAddress expected_server_endpoint,
        const std::span<const RawCommand> expected_commands)
        : expected_client_endpoint_{expected_client_endpoint},
          expected_server_endpoint_{expected_server_endpoint},
          expected_commands_{expected_commands.begin(), expected_commands.end()}
    {
    }

    void deliver(const CapturedClientDatagram& datagram)
    {
        REQUIRE(datagram.source == expected_client_endpoint_);
        REQUIRE(datagram.destination == expected_server_endpoint_);
        ++delivered_datagram_count_;

        // Decode every delivered synthetic client-move message before sequence
        // admission. This is intentionally independent of the production
        // decoder and makes old and duplicate delivery observations exercise
        // the raw captured bytes.
        std::optional<IndependentlyDecodedMove> decoded_move;
        if (datagram.move_payload) {
            decoded_move = decode_move_independently(
                *datagram.move_payload, datagram.outgoing_sequence);
            ++decoded_move_count_;
        }

        if (accepted_sequence_) {
            switch (compare_sequences_independently(
                datagram.outgoing_sequence, *accepted_sequence_)) {
            case IndependentSequenceComparison::equal:
                ++ignored_duplicate_datagram_count_;
                return;
            case IndependentSequenceComparison::older:
                ++ignored_older_datagram_count_;
                return;
            case IndependentSequenceComparison::half_range_ambiguous:
                ++ignored_ambiguous_datagram_count_;
                return;
            case IndependentSequenceComparison::newer:
                break;
            }
        }

        accepted_sequence_ = datagram.outgoing_sequence;
        ++accepted_datagram_count_;
        if (datagram.reliable_present) {
            REQUIRE_FALSE(datagram.reliable_payload.empty());
            if (accepted_reliable_payloads_.empty()) {
                accepted_reliable_payloads_.push_back(
                    datagram.reliable_payload);
            } else if (accepted_reliable_payloads_.back() ==
                       datagram.reliable_payload) {
                ++accepted_reliable_retry_count_;
            } else {
                accepted_reliable_payloads_.push_back(
                    datagram.reliable_payload);
            }
        } else {
            REQUIRE(datagram.reliable_payload.empty());
        }

        if (!decoded_move) {
            return;
        }
        ++accepted_move_count_;
        accepted_moves_.push_back(*decoded_move);
        REQUIRE(decoded_move->commands.size() ==
                decoded_move->backup_count + decoded_move->new_count);
        for (std::size_t index = 0U;
             index < decoded_move->commands.size();
             ++index) {
            const auto& command = decoded_move->commands[index];
            const bool declared_backup = index < decoded_move->backup_count;
            if (accepted_command_history_.size() < expected_commands_.size() &&
                command == expected_commands_[accepted_command_history_.size()]) {
                accepted_command_history_.push_back(command);
                if (declared_backup) {
                    ++recovered_command_from_backup_count_;
                }
                continue;
            }
            const auto duplicate = std::ranges::find(
                accepted_command_history_, command);
            REQUIRE(duplicate != accepted_command_history_.end());
            if (declared_backup) {
                ++duplicate_backup_command_count_;
            } else {
                ++duplicate_new_command_count_;
            }
        }
    }

    [[nodiscard]] const std::vector<RawCommand>& accepted_command_history()
        const noexcept
    {
        return accepted_command_history_;
    }
    [[nodiscard]] const std::vector<IndependentlyDecodedMove>& accepted_moves()
        const noexcept
    {
        return accepted_moves_;
    }
    [[nodiscard]] std::size_t delivered_datagram_count() const noexcept
    {
        return delivered_datagram_count_;
    }
    [[nodiscard]] std::size_t decoded_move_count() const noexcept
    {
        return decoded_move_count_;
    }
    [[nodiscard]] std::size_t accepted_datagram_count() const noexcept
    {
        return accepted_datagram_count_;
    }
    [[nodiscard]] std::size_t accepted_move_count() const noexcept
    {
        return accepted_move_count_;
    }
    [[nodiscard]] std::size_t ignored_duplicate_datagram_count() const noexcept
    {
        return ignored_duplicate_datagram_count_;
    }
    [[nodiscard]] std::size_t ignored_older_datagram_count() const noexcept
    {
        return ignored_older_datagram_count_;
    }
    [[nodiscard]] std::size_t ignored_ambiguous_datagram_count() const noexcept
    {
        return ignored_ambiguous_datagram_count_;
    }
    [[nodiscard]] std::size_t duplicate_backup_command_count() const noexcept
    {
        return duplicate_backup_command_count_;
    }
    [[nodiscard]] std::size_t recovered_command_from_backup_count() const noexcept
    {
        return recovered_command_from_backup_count_;
    }
    [[nodiscard]] std::size_t duplicate_new_command_count() const noexcept
    {
        return duplicate_new_command_count_;
    }
    [[nodiscard]] const std::vector<std::vector<std::byte>>&
    accepted_reliable_payloads() const noexcept
    {
        return accepted_reliable_payloads_;
    }
    [[nodiscard]] std::size_t accepted_reliable_retry_count() const noexcept
    {
        return accepted_reliable_retry_count_;
    }

private:
    network::NetworkAddress expected_client_endpoint_;
    network::NetworkAddress expected_server_endpoint_;
    std::vector<RawCommand> expected_commands_;
    std::optional<std::uint32_t> accepted_sequence_;
    std::vector<RawCommand> accepted_command_history_;
    std::vector<IndependentlyDecodedMove> accepted_moves_;
    std::vector<std::vector<std::byte>> accepted_reliable_payloads_;
    std::size_t delivered_datagram_count_{0U};
    std::size_t decoded_move_count_{0U};
    std::size_t accepted_datagram_count_{0U};
    std::size_t accepted_move_count_{0U};
    std::size_t ignored_duplicate_datagram_count_{0U};
    std::size_t ignored_older_datagram_count_{0U};
    std::size_t ignored_ambiguous_datagram_count_{0U};
    std::size_t duplicate_backup_command_count_{0U};
    std::size_t recovered_command_from_backup_count_{0U};
    std::size_t duplicate_new_command_count_{0U};
    std::size_t accepted_reliable_retry_count_{0U};
};

[[nodiscard]] CampaignResult run_campaign(
    const goldsrc::GoldSrcUserCmdSchemaBinding& schema_binding,
    const std::span<const ScriptFrame> frames,
    const CampaignOptions options = {})
{
    REQUIRE_FALSE((options.trigger_reliable_retry && !options.queue_reliable));
    REQUIRE_FALSE((options.trigger_reliable_retry && frames.size() < 3U));
    FakeHldsTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, driver_config()};
    REQUIRE(transport.local);
    REQUIRE(driver.start(epoch, *transport.local));
    transport.queue(remote, bootstrap_packet());
    driver.update(epoch + 1ms);
    REQUIRE(driver.state() == goldsrc::NetchanDriverState::active);
    transport.sent.clear();

    goldsrc::GoldSrcUserCmdTransmissionStage stage{
        driver,
        schema_binding,
        {goldsrc::GoldSrcUserCmdSessionPrerequisiteProfile::
             synthetic_runtime_ready_v1,
            true}};
    const auto neutral = make_intent({input::InputEvent::focus_gained()});
    REQUIRE(stage.update(epoch + 2ms, neutral, make_camera(0.0)));

    const std::vector<std::byte> reliable{
        std::byte{0x46U}, std::byte{0x41U}, std::byte{0x4bU}, std::byte{0x45U}};
    if (options.queue_reliable) {
        REQUIRE(driver.queue_reliable(reliable));
    }
    for (std::size_t index = 0U; index < frames.size(); ++index) {
        const auto& frame = frames[index];
        REQUIRE(stage.update(
            epoch + 12ms + std::chrono::milliseconds{10 * index},
            make_intent(frame.events),
            make_camera(frame.yaw, frame.pitch)));
        if (options.trigger_reliable_retry && index == 1U) {
            // The first reliable generation travelled on sequence 2. A
            // non-reliable acknowledgement covering sequence 3 is the stock
            // advanced-ACK-gap trigger for an exact sequence-4 retry.
            transport.queue(remote, reliable_gap_packet());
            driver.update(epoch + 23ms);
        }
    }
    REQUIRE(transport.sent.size() ==
            frames.size() + (options.trigger_reliable_retry ? 1U : 0U));
    CampaignResult result;
    result.local_endpoint = *transport.local;
    result.remote_endpoint = remote;
    for (std::size_t index = 0U; index < transport.sent.size(); ++index) {
        const auto& sent = transport.sent[index];
        REQUIRE(sent.source);
        CHECK(*sent.source == *transport.local);
        CHECK(sent.destination == remote);
        auto netchan = goldsrc::decode_client_to_server_netchan_packet(sent.payload);
        REQUIRE(netchan);
        REQUIRE(netchan.packet);
        const auto outgoing = netchan.packet->header.sequence.sequence.value();
        CHECK(outgoing == index + 2U);
        CapturedClientDatagram captured{
            *sent.source,
            sent.destination,
            outgoing,
            netchan.packet->header.sequence.flags.reliable,
            {},
            std::nullopt,
        };
        auto suffix = std::span<const std::byte>{netchan.packet->payload};
        if (captured.reliable_present) {
            REQUIRE(options.queue_reliable);
            REQUIRE(suffix.size() >= reliable.size());
            CHECK(std::ranges::equal(reliable, suffix.first(reliable.size())));
            captured.reliable_payload.assign(
                suffix.begin(), suffix.begin() + reliable.size());
            suffix = suffix.subspan(reliable.size());
        }
        if (!suffix.empty() &&
            std::to_integer<std::uint8_t>(suffix.front()) == 0xe1U) {
            captured.move_payload.emplace(suffix.begin(), suffix.end());
        } else {
            CHECK(std::ranges::all_of(suffix, [](const std::byte value) {
                return value == goldsrc::kStockProtocol48NetchanPaddingByte;
            }));
        }
        result.datagrams.push_back(std::move(captured));
    }
    CHECK(std::ranges::count_if(
              result.datagrams,
              [](const CapturedClientDatagram& datagram) {
                  return datagram.move_payload.has_value();
              }) == static_cast<std::ptrdiff_t>(frames.size()));
    result.final_history_size = stage.history().size();
    stage.close(epoch + 1s);
    result.cleanup_count = driver.cleanup_count();
    return result;
}

[[nodiscard]] std::vector<ScriptFrame> idle_frames()
{
    return {{
        {input::InputEvent::focus_gained()},
        0.0,
        0.0,
        expected_command(0.0F, 0.0F, 0.0, 0.0),
    }};
}

[[nodiscard]] std::vector<ScriptFrame> movement_frames()
{
    return {
        {{input::InputEvent::focus_gained(),
             input::InputEvent::key_pressed(input::PhysicalKey::w)},
            0.0,
            0.0,
            expected_command(400.0F, 0.0F, 0.0, 0.0)},
        {{input::InputEvent::focus_gained(),
             input::InputEvent::key_pressed(input::PhysicalKey::w)},
            45.0,
            -10.0,
            expected_command(400.0F, 0.0F, 45.0, -10.0)},
        {{input::InputEvent::focus_gained(),
             input::InputEvent::key_pressed(input::PhysicalKey::w),
             input::InputEvent::key_pressed(input::PhysicalKey::space)},
            45.0,
            -10.0,
            expected_command(400.0F, 0.0F, 45.0, -10.0,
                goldsrc::kSyntheticGoldSrcButtonJump)},
        {{input::InputEvent::focus_gained()},
            45.0,
            -10.0,
            expected_command(0.0F, 0.0F, 45.0, -10.0)},
    };
}

[[nodiscard]] std::vector<RawCommand> expected_commands(
    const std::span<const ScriptFrame> frames)
{
    std::vector<RawCommand> expected;
    expected.reserve(frames.size());
    std::ranges::transform(
        frames, std::back_inserter(expected),
        [](const ScriptFrame& frame) { return frame.expected; });
    return expected;
}

TEST_CASE("Independent fake HLDS campaigns pass idle movement loss and reliable flows 20 of 20",
          "[goldsrc][usercmd][fake-hlds][integration][repeat]")
{
    const auto schema_binding = make_binding();
    for (std::size_t run = 0U; run < 20U; ++run) {
        CAPTURE(run);
        const auto idle = idle_frames();
        const auto idle_result = run_campaign(schema_binding, idle);
        const auto idle_expected = expected_commands(idle);
        IndependentFakeHlds idle_peer{
            idle_result.local_endpoint,
            idle_result.remote_endpoint,
            idle_expected};
        REQUIRE(idle_result.datagrams.size() == 1U);
        idle_peer.deliver(idle_result.datagrams[0U]);
        REQUIRE(idle_peer.accepted_moves().size() == 1U);
        CHECK(idle_peer.accepted_moves()[0U].backup_count == 0U);
        CHECK(idle_peer.accepted_moves()[0U].new_count == 1U);
        CHECK(idle_peer.accepted_command_history() == idle_expected);
        CHECK(idle_peer.delivered_datagram_count() == 1U);
        CHECK(idle_peer.decoded_move_count() == 1U);
        CHECK(idle_peer.accepted_datagram_count() == 1U);
        CHECK(idle_result.final_history_size == 1U);
        CHECK(idle_result.cleanup_count == 1U);

        const auto movement = movement_frames();
        const auto moved = run_campaign(schema_binding, movement);
        const auto movement_expected = expected_commands(movement);
        IndependentFakeHlds movement_peer{
            moved.local_endpoint, moved.remote_endpoint, movement_expected};
        REQUIRE(moved.datagrams.size() == movement.size());
        for (const auto& datagram : moved.datagrams) {
            movement_peer.deliver(datagram);
        }
        REQUIRE(movement_peer.accepted_moves().size() == movement.size());
        for (std::size_t index = 0U;
             index < movement_peer.accepted_moves().size();
             ++index) {
            const auto& packet = movement_peer.accepted_moves()[index];
            CHECK(packet.new_count == 1U);
            CHECK(packet.backup_count == std::min<std::size_t>(index, 2U));
            REQUIRE_FALSE(packet.commands.empty());
            CHECK(packet.commands.back() == movement[index].expected);
        }
        CHECK(movement_peer.accepted_command_history() == movement_expected);
        CHECK(movement_peer.duplicate_backup_command_count() == 5U);
        CHECK(movement_peer.recovered_command_from_backup_count() == 0U);
        CHECK(movement_peer.duplicate_new_command_count() == 0U);
        CHECK(moved.local_endpoint == idle_result.local_endpoint);
        CHECK(moved.remote_endpoint == idle_result.remote_endpoint);
        CHECK(moved.final_history_size == 4U);
        CHECK(moved.cleanup_count == 1U);

        const std::array drop_one_frames{movement[0U], movement[1U]};
        const auto drop_one = run_campaign(schema_binding, drop_one_frames);
        const auto drop_one_expected = expected_commands(drop_one_frames);
        IndependentFakeHlds drop_one_peer{
            drop_one.local_endpoint,
            drop_one.remote_endpoint,
            drop_one_expected};
        REQUIRE(drop_one.datagrams.size() == 2U);
        // Datagram zero is actually dropped. Only the second raw synthetic
        // client-move datagram is delivered to the fake peer, which must
        // recover command zero from its declared backup before accepting
        // command one as new.
        drop_one_peer.deliver(drop_one.datagrams[1U]);
        CHECK(drop_one_peer.accepted_command_history() == drop_one_expected);
        REQUIRE(drop_one_peer.accepted_moves().size() == 1U);
        CHECK(drop_one_peer.accepted_moves()[0U].backup_count == 1U);
        CHECK(drop_one_peer.accepted_moves()[0U].new_count == 1U);
        CHECK(drop_one_peer.recovered_command_from_backup_count() == 1U);
        CHECK(drop_one_peer.delivered_datagram_count() == 1U);

        const std::array drop_two_frames{
            movement[0U], movement[1U], movement[2U]};
        const auto drop_two = run_campaign(schema_binding, drop_two_frames);
        const auto drop_two_expected = expected_commands(drop_two_frames);
        IndependentFakeHlds drop_two_peer{
            drop_two.local_endpoint,
            drop_two.remote_endpoint,
            drop_two_expected};
        REQUIRE(drop_two.datagrams.size() == 3U);
        // Two consecutive client datagrams are actually lost.
        drop_two_peer.deliver(drop_two.datagrams[2U]);
        CHECK(drop_two_peer.accepted_command_history() == drop_two_expected);
        REQUIRE(drop_two_peer.accepted_moves().size() == 1U);
        CHECK(drop_two_peer.accepted_moves()[0U].backup_count == 2U);
        CHECK(drop_two_peer.accepted_moves()[0U].new_count == 1U);
        CHECK(drop_two_peer.recovered_command_from_backup_count() == 2U);
        CHECK(drop_two_peer.delivered_datagram_count() == 1U);

        const std::array reliable_frames{
            movement[0U], movement[1U], movement[2U]};
        const auto reliable = run_campaign(
            schema_binding,
            reliable_frames,
            CampaignOptions{true, true});
        const auto reliable_expected = expected_commands(reliable_frames);
        IndependentFakeHlds reliable_peer{
            reliable.local_endpoint,
            reliable.remote_endpoint,
            reliable_expected};
        REQUIRE(reliable.datagrams.size() == 4U);
        for (const auto& datagram : reliable.datagrams) {
            reliable_peer.deliver(datagram);
        }
        CHECK(reliable_peer.accepted_command_history() == reliable_expected);
        CHECK(reliable_peer.accepted_move_count() == 3U);
        REQUIRE(reliable_peer.accepted_reliable_payloads().size() == 1U);
        CHECK(reliable_peer.accepted_reliable_payloads()[0U] ==
              std::vector<std::byte>{
                  std::byte{0x46U}, std::byte{0x41U},
                  std::byte{0x4bU}, std::byte{0x45U}});
        CHECK(reliable_peer.accepted_reliable_retry_count() == 1U);
        CHECK(std::ranges::count_if(
                  reliable.datagrams,
                  [](const CapturedClientDatagram& datagram) {
                      return datagram.reliable_present;
                  }) == 2);
        CHECK(std::ranges::count_if(
                  reliable.datagrams,
                  [](const CapturedClientDatagram& datagram) {
                      return datagram.reliable_present &&
                             datagram.move_payload.has_value();
                  }) == 1);
        CHECK(std::ranges::count_if(
                  reliable.datagrams,
                  [](const CapturedClientDatagram& datagram) {
                      return datagram.reliable_present &&
                             !datagram.move_payload.has_value();
                  }) == 1);
        CHECK(reliable.final_history_size == 3U);
        CHECK(reliable.cleanup_count == 1U);
    }
}

TEST_CASE("Fake HLDS duplicate and reordered datagrams cannot regress accepted history",
          "[goldsrc][usercmd][fake-hlds][duplicate][reorder]")
{
    const auto schema_binding = make_binding();
    const auto movement = movement_frames();
    const std::array frames{movement[0U], movement[1U], movement[2U]};
    const auto result = run_campaign(schema_binding, frames);
    REQUIRE(result.datagrams.size() == 3U);
    const auto expected = expected_commands(frames);
    IndependentFakeHlds peer{
        result.local_endpoint, result.remote_endpoint, expected};

    // Deliver newest first, then two old packets and a duplicate newest packet.
    // The newest packet independently carries both bounded backups, so accepting
    // it once reconstructs all three semantic commands; modular netchan ordering
    // rejects every later old/duplicate observation.
    peer.deliver(result.datagrams[2U]);
    peer.deliver(result.datagrams[0U]);
    peer.deliver(result.datagrams[1U]);
    peer.deliver(result.datagrams[2U]);
    CHECK(peer.accepted_command_history() == expected);
    REQUIRE(peer.accepted_moves().size() == 1U);
    REQUIRE(peer.accepted_moves()[0U].commands.size() == 3U);
    CHECK(peer.accepted_moves()[0U].commands[0U] == movement[0U].expected);
    CHECK(peer.accepted_moves()[0U].commands[1U] == movement[1U].expected);
    CHECK(peer.accepted_moves()[0U].commands[2U] == movement[2U].expected);
    CHECK(peer.recovered_command_from_backup_count() == 2U);
    CHECK(peer.delivered_datagram_count() == 4U);
    CHECK(peer.decoded_move_count() == 4U);
    CHECK(peer.accepted_datagram_count() == 1U);
    CHECK(peer.ignored_older_datagram_count() == 2U);
    CHECK(peer.ignored_duplicate_datagram_count() == 1U);
    CHECK(peer.ignored_ambiguous_datagram_count() == 0U);

    // The peer's admission comparator is independently implemented over the
    // 30-bit wire ring, including wrap and the exact half-range rejection.
    CHECK(compare_sequences_independently(0U, 0x3fff'ffffU) ==
          IndependentSequenceComparison::newer);
    CHECK(compare_sequences_independently(0x3fff'ffffU, 0U) ==
          IndependentSequenceComparison::older);
    CHECK(compare_sequences_independently(0x2000'0000U, 0U) ==
          IndependentSequenceComparison::half_range_ambiguous);
    CHECK(result.final_history_size == 3U);
    CHECK(result.cleanup_count == 1U);
}

} // namespace
