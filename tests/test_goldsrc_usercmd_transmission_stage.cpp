#include <hlclient/gameplay_camera/first_person_camera.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/usercmd_schema_binding.hpp>
#include <hlclient/goldsrc/usercmd_transmission_stage.hpp>
#include <hlclient/input/input_state_tracker.hpp>

#include "usercmd_transaction_test_access.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace camera = hlclient::gameplay_camera;
namespace gameplay = hlclient::gameplay_input;
namespace goldsrc = hlclient::goldsrc;
namespace goldsrc_detail = hlclient::goldsrc::detail;
namespace input = hlclient::input;
namespace network = hlclient::network;

class StageTransport final : public network::IDatagramTransport {
public:
    struct SentDatagram {
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
        if (fail_send) {
            return {network::DatagramSendStatus::error, "synthetic send failure"};
        }
        sent.push_back({destination, {payload.begin(), payload.end()}});
        return {network::DatagramSendStatus::sent, {}};
    }

    [[nodiscard]] network::DatagramTransportReceiveResult receive(
        const std::size_t) override
    {
        if (incoming.empty()) {
            return {network::DatagramTransportReceiveStatus::would_block,
                std::nullopt, std::nullopt, 0U, {}};
        }
        auto result = std::move(incoming.front());
        incoming.pop_front();
        return result;
    }

    void queue(const network::NetworkAddress source, std::vector<std::byte> bytes)
    {
        const auto size = bytes.size();
        incoming.push_back({network::DatagramTransportReceiveStatus::received,
            network::Datagram{source, std::move(bytes)}, source, size, {}});
    }

    std::optional<network::NetworkAddress> local{
        network::NetworkAddress::loopback(31'200U)};
    bool fail_send{false};
    std::deque<network::DatagramTransportReceiveResult> incoming;
    std::vector<SentDatagram> sent;
};

class LifetimeProbe final : public goldsrc::INetchanDriverLifetime {
public:
    explicit LifetimeProbe(std::shared_ptr<std::size_t> releases) noexcept
        : releases_{std::move(releases)}
    {
    }
    ~LifetimeProbe() override { ++*releases_; }

private:
    std::shared_ptr<std::size_t> releases_;
};

[[nodiscard]] goldsrc::NetchanSequence net_sequence(const std::uint32_t value)
{
    const auto sequence = goldsrc::NetchanSequence::from_numeric(value);
    if (!sequence) {
        throw std::runtime_error{"invalid test netchan sequence"};
    }
    return *sequence;
}

[[nodiscard]] std::vector<std::byte> server_packet(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement = false)
{
    goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                net_sequence(packet_sequence), {false, false}},
            goldsrc::NetchanAcknowledgementWord{
                net_sequence(acknowledgement), reliable_acknowledgement}},
        {},
        {std::byte{0x52U}},
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    if (!encoded || !encoded.datagram) {
        throw std::runtime_error{"failed to encode synthetic server packet"};
    }
    return std::move(*encoded.datagram);
}

[[nodiscard]] std::vector<std::byte> server_fragment_packet(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
    const std::uint16_t fragment_index,
    const std::uint16_t fragment_count,
    std::vector<std::byte> fragment_payload)
{
    const auto fragment_size = fragment_payload.size();
    if (fragment_size == 0U || fragment_size > UINT16_MAX) {
        throw std::runtime_error{"invalid synthetic server fragment"};
    }
    goldsrc::NetchanFragmentSlots fragments;
    fragments[0U] = goldsrc::NetchanFragmentDescriptor{
        0U,
        (static_cast<std::uint32_t>(fragment_index) << 16U) |
            static_cast<std::uint32_t>(fragment_count),
        0U,
        static_cast<std::uint16_t>(fragment_size),
        0U,
    };
    goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                net_sequence(packet_sequence), {true, true}},
            goldsrc::NetchanAcknowledgementWord{
                net_sequence(acknowledgement), false}},
        std::move(fragments),
        std::move(fragment_payload),
        fragment_size,
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    if (!encoded || !encoded.datagram) {
        throw std::runtime_error{"failed to encode synthetic server fragment"};
    }
    return std::move(*encoded.datagram);
}

[[nodiscard]] goldsrc::NetchanDriverConfig driver_config(
    const std::size_t maximum_outgoing_packets = 1U)
{
    goldsrc::NetchanDriverConfig config;
    config.channel_inactivity_timeout = 2'000ms;
    config.fragment_transfer_timeout = 500ms;
    config.maximum_outgoing_packets_per_update = maximum_outgoing_packets;
    return config;
}

void activate(
    goldsrc::NetchanDriver& driver,
    StageTransport& transport,
    const network::NetworkAddress remote,
    const goldsrc::NetchanDriverTimePoint epoch)
{
    REQUIRE(transport.local);
    REQUIRE(driver.start(epoch, *transport.local));
    transport.queue(remote, server_packet(1U, 0U));
    driver.update(epoch + 1ms);
    REQUIRE(driver.state() == goldsrc::NetchanDriverState::active);
    REQUIRE(driver.session().first_acknowledgement_sent());
    transport.sent.clear();
}

[[nodiscard]] goldsrc::GoldSrcUserCmdSchemaBinding binding()
{
    auto registry = goldsrc::make_synthetic_usercmd_schema_registry();
    REQUIRE(registry);
    auto result = goldsrc::bind_goldsrc_usercmd_schema(*registry.registry);
    REQUIRE(result);
    return std::move(*result.binding);
}

[[nodiscard]] gameplay::GameplayInputIntent intent_from_events(
    const std::initializer_list<input::InputEvent> events)
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
    auto result = gameplay::GameplayInputIntentBuilder{}.build(
        snapshot, *bindings.bindings, gameplay::MouseLookConfig{}, 0.01);
    REQUIRE(result);
    return std::move(*result.intent);
}

[[nodiscard]] camera::GameplayCameraState make_camera(
    const double yaw = 0.0,
    const double pitch = 0.0)
{
    camera::GameplayCameraStateCreateInfo info;
    info.yaw_degrees = yaw;
    info.pitch_degrees = pitch;
    auto result = camera::GameplayCameraState::create(info);
    REQUIRE(result);
    return std::move(*result.state);
}

[[nodiscard]] goldsrc::GoldSrcUserCmdSessionPrerequisite synthetic_ready()
{
    return {goldsrc::GoldSrcUserCmdSessionPrerequisiteProfile::
                synthetic_runtime_ready_v1,
        true};
}

[[nodiscard]] goldsrc::GoldSrcUserCmdSequence command_sequence(
    const std::uint32_t value)
{
    const auto created = goldsrc::GoldSrcUserCmdSequence::create(value);
    if (!created) {
        throw std::runtime_error{"invalid injected usercmd sequence"};
    }
    return *created;
}

[[nodiscard]] goldsrc::GoldSrcUserCmdState injected_command(
    const std::uint32_t value)
{
    auto info = goldsrc::goldsrc_usercmd_default_create_info(
        command_sequence(value),
        static_cast<std::int64_t>(value) * 10'000'000);
    info.msec = 10U;
    info.sample_duration_nanoseconds = 10'000'000U;
    auto created = goldsrc::GoldSrcUserCmdState::create(info);
    if (!created || !created.state) {
        throw std::runtime_error{"unable to build injected usercmd"};
    }
    return std::move(*created.state);
}

void check_same_stage_history(
    const goldsrc::GoldSrcUserCmdHistoryState& before,
    const goldsrc::GoldSrcUserCmdHistoryState& after)
{
    CHECK(after.revision() == before.revision());
    REQUIRE(after.entries().size() == before.entries().size());
    for (std::size_t index = 0U; index < before.entries().size(); ++index) {
        const auto& expected = before.entries()[index];
        const auto& actual = after.entries()[index];
        CHECK(actual.command == expected.command);
        CHECK(actual.new_transmission_count == expected.new_transmission_count);
        CHECK(actual.backup_transmission_count ==
              expected.backup_transmission_count);
        CHECK(actual.last_packet_sequence == expected.last_packet_sequence);
    }
}

TEST_CASE("Usercmd stage stock prerequisite is a zero-send evidence boundary",
          "[goldsrc][usercmd][transmission-stage][pending]")
{
    StageTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, driver_config()};
    activate(driver, transport, remote, epoch);
    const auto schema_binding = binding();
    goldsrc::GoldSrcUserCmdTransmissionStage stage{
        driver,
        schema_binding,
        {goldsrc::GoldSrcUserCmdSessionPrerequisiteProfile::
             stock_runtime_ready_evidence_pending,
            false}};

    const auto result = stage.update(
        epoch + 2ms,
        intent_from_events({input::InputEvent::focus_gained()}),
        make_camera());
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code == goldsrc::GoldSrcUserCmdTransmissionErrorCode::
        runtime_signon_evidence_pending);
    CHECK(stage.state() ==
        goldsrc::GoldSrcUserCmdTransmissionState::signon_evidence_pending);
    CHECK(stage.transmitted_packet_count() == 0U);
    CHECK(transport.sent.empty());
    CHECK(driver.cleanup_count() == 1U);
}

TEST_CASE("Usercmd stage rejects an unknown prerequisite profile without send",
          "[goldsrc][usercmd][transmission-stage][configuration][malformed]")
{
    StageTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, driver_config()};
    activate(driver, transport, remote, epoch);
    const auto schema_binding = binding();
    goldsrc::GoldSrcUserCmdTransmissionStage stage{
        driver,
        schema_binding,
        {static_cast<goldsrc::GoldSrcUserCmdSessionPrerequisiteProfile>(0xffU),
         true}};

    REQUIRE_FALSE(stage.valid_configuration());
    const auto result = stage.update(
        epoch + 2ms,
        intent_from_events({input::InputEvent::focus_gained()}),
        make_camera());
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->code ==
          goldsrc::GoldSrcUserCmdTransmissionErrorCode::invalid_configuration);
    CHECK(stage.state() ==
          goldsrc::GoldSrcUserCmdTransmissionState::protocol_error);
    CHECK(stage.sampled_command_count() == 0U);
    CHECK(stage.transmitted_packet_count() == 0U);
    CHECK(transport.sent.empty());
    CHECK(driver.cleanup_count() == 1U);
}

TEST_CASE("Synthetic usercmd stage samples plans submits and publishes metadata",
          "[goldsrc][usercmd][transmission-stage][synthetic]")
{
    StageTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, driver_config()};
    activate(driver, transport, remote, epoch);
    const auto schema_binding = binding();
    goldsrc::GoldSrcUserCmdTransmissionStage stage{
        driver, schema_binding, synthetic_ready()};
    const auto intent = intent_from_events({input::InputEvent::focus_gained(),
        input::InputEvent::key_pressed(input::PhysicalKey::w)});
    const auto camera_state = make_camera(45.0, -10.0);

    REQUIRE(stage.update(epoch + 2ms, intent, camera_state));
    CHECK(stage.sampled_command_count() == 0U);
    REQUIRE(stage.update(epoch + 12ms, intent, camera_state));
    CHECK(stage.state() ==
        goldsrc::GoldSrcUserCmdTransmissionState::waiting_for_next_sample);
    CHECK(stage.sampled_command_count() == 1U);
    CHECK(stage.transmitted_packet_count() == 1U);
    const auto history = stage.history();
    REQUIRE(history.size() == 1U);
    CHECK(history.entries()[0U].new_transmission_count == 1U);
    REQUIRE(transport.sent.size() == 1U);
    const auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.front().payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    REQUIRE_FALSE(decoded.packet->payload.empty());
    CHECK(std::to_integer<std::uint8_t>(decoded.packet->payload[0U]) ==
        goldsrc::kSyntheticClientMoveOpcode);
    CHECK(decoded.packet->header.sequence.sequence == net_sequence(2U));
    CHECK(transport.sent.front().destination == remote);

    std::vector<goldsrc::GoldSrcUserCmdTransmissionEventType> events;
    while (const auto event = stage.poll_event()) {
        events.push_back(event->type);
        CHECK(event->encoded_bytes <= 1'024U);
    }
    CHECK(std::ranges::find(events,
              goldsrc::GoldSrcUserCmdTransmissionEventType::usercmd_sampled) !=
        events.end());
    CHECK(std::ranges::find(events,
              goldsrc::GoldSrcUserCmdTransmissionEventType::move_packet_submitted) !=
        events.end());
    CHECK_FALSE(stage.poll_event());
}

TEST_CASE("Usercmd stage retains unsent history across unreliable backpressure",
          "[goldsrc][usercmd][transmission-stage][backpressure]")
{
    StageTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, driver_config()};
    activate(driver, transport, remote, epoch);
    const auto schema_binding = binding();
    goldsrc::GoldSrcUserCmdTransmissionStage stage{
        driver, schema_binding, synthetic_ready()};
    const auto intent = intent_from_events({input::InputEvent::focus_gained()});
    const auto camera_state = make_camera();
    REQUIRE(stage.update(epoch + 2ms, intent, camera_state));

    const std::vector<std::byte> occupied{std::byte{0x77U}};
    REQUIRE(driver.submit_unreliable(occupied));
    REQUIRE(stage.update(epoch + 12ms, intent, camera_state));
    CHECK(stage.state() ==
        goldsrc::GoldSrcUserCmdTransmissionState::unreliable_backpressure);
    CHECK(stage.history().entries()[0U].new_transmission_count == 0U);
    CHECK(stage.transmitted_packet_count() == 0U);
    REQUIRE(transport.sent.size() == 1U);

    REQUIRE(stage.update(epoch + 12ms, intent, camera_state));
    CHECK(stage.transmitted_packet_count() == 1U);
    CHECK(stage.history().entries()[0U].new_transmission_count == 1U);
    REQUIRE(transport.sent.size() == 2U);
}

TEST_CASE("Usercmd unreliable suffix coexists with a retained reliable prefix",
          "[goldsrc][usercmd][transmission-stage][reliable]")
{
    StageTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, driver_config()};
    activate(driver, transport, remote, epoch);
    const auto schema_binding = binding();
    goldsrc::GoldSrcUserCmdTransmissionStage stage{
        driver, schema_binding, synthetic_ready()};
    const auto intent = intent_from_events({input::InputEvent::focus_gained()});
    const auto camera_state = make_camera();
    REQUIRE(stage.update(epoch + 2ms, intent, camera_state));
    const std::vector<std::byte> reliable{
        std::byte{0x53U}, std::byte{0x49U},
        std::byte{0x47U}, std::byte{0x4eU}};
    REQUIRE(driver.queue_reliable(reliable));
    REQUIRE(stage.update(epoch + 12ms, intent, camera_state));

    REQUIRE(transport.sent.size() == 1U);
    auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.front().payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    CHECK(decoded.packet->header.sequence.flags.reliable);
    REQUIRE(decoded.packet->payload.size() > reliable.size());
    CHECK(std::ranges::equal(
        reliable,
        std::span<const std::byte>{decoded.packet->payload}.first(
            reliable.size())));
    CHECK(std::to_integer<std::uint8_t>(
              decoded.packet->payload[reliable.size()]) ==
        goldsrc::kSyntheticClientMoveOpcode);
    CHECK(stage.transmitted_packet_count() == 1U);
}

TEST_CASE("Usercmd stage carries pressed edges to only the first eligible command",
          "[goldsrc][usercmd][transmission-stage][buttons][one-shot]")
{
    StageTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, driver_config()};
    activate(driver, transport, remote, epoch);
    const auto schema_binding = binding();
    goldsrc::GoldSrcUserCmdTransmissionStage stage{
        driver, schema_binding, synthetic_ready()};
    const auto pressed = intent_from_events({input::InputEvent::focus_gained(),
        input::InputEvent::key_pressed(input::PhysicalKey::space)});
    const auto neutral =
        intent_from_events({input::InputEvent::focus_gained()});
    const auto camera_state = make_camera();

    REQUIRE(stage.update(epoch + 2ms, pressed, camera_state));
    CHECK(stage.sampled_command_count() == 0U);
    REQUIRE(stage.update(epoch + 22ms, neutral, camera_state));
    const auto history = stage.history();
    REQUIRE(history.size() == 2U);
    REQUIRE(history.entries()[0U].command);
    REQUIRE(history.entries()[1U].command);
    CHECK(history.entries()[0U].command->buttons() ==
        goldsrc::kSyntheticGoldSrcButtonJump);
    CHECK(history.entries()[1U].command->buttons() == 0U);
}

TEST_CASE("Usercmd stage retries after a temporarily narrow reliable composition",
          "[goldsrc][usercmd][transmission-stage][reliable][backpressure]")
{
    StageTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, driver_config()};
    activate(driver, transport, remote, epoch);
    const auto schema_binding = binding();
    goldsrc::GoldSrcUserCmdTransmissionStage stage{
        driver, schema_binding, synthetic_ready()};
    const auto intent = intent_from_events({input::InputEvent::focus_gained()});
    const auto camera_state = make_camera();
    REQUIRE(stage.update(epoch + 2ms, intent, camera_state));

    const std::vector<std::byte> large_reliable(
        driver_config().maximum_unreliable_payload_size - 2U,
        std::byte{0x6bU});
    REQUIRE(driver.queue_reliable(large_reliable));
    REQUIRE(stage.update(epoch + 12ms, intent, camera_state));
    CHECK(stage.state() ==
        goldsrc::GoldSrcUserCmdTransmissionState::unreliable_backpressure);
    REQUIRE(stage.history().size() == 1U);
    CHECK(stage.history().entries()[0U].new_transmission_count == 0U);
    CHECK(stage.transmitted_packet_count() == 0U);
    REQUIRE(transport.sent.size() == 1U);
    CHECK(driver.state() == goldsrc::NetchanDriverState::active);

    transport.queue(remote, server_packet(2U, 2U, true));
    REQUIRE(stage.update(epoch + 13ms, intent, camera_state));
    if (stage.transmitted_packet_count() == 0U) {
        REQUIRE(stage.update(epoch + 13ms, intent, camera_state));
    }
    CHECK(stage.transmitted_packet_count() == 1U);
    CHECK(stage.history().entries()[0U].new_transmission_count == 1U);
    CHECK(driver.state() == goldsrc::NetchanDriverState::active);
}

TEST_CASE("Usercmd contextual receipt tolerates additional driver TX in one update",
          "[goldsrc][usercmd][transmission-stage][context][fragment-ack]")
{
    StageTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, driver_config(2U)};
    activate(driver, transport, remote, epoch);
    const auto schema_binding = binding();
    goldsrc::GoldSrcUserCmdTransmissionStage stage{
        driver, schema_binding, synthetic_ready()};
    const auto intent = intent_from_events({input::InputEvent::focus_gained()});
    const auto camera_state = make_camera();
    REQUIRE(stage.update(epoch + 2ms, intent, camera_state));

    transport.queue(
        remote,
        server_fragment_packet(
            2U,
            1U,
            1U,
            2U,
            std::vector<std::byte>(
                goldsrc::kStockProtocol48NormalFragmentChunkSize,
                std::byte{0x5aU})));
    REQUIRE(stage.update(epoch + 12ms, intent, camera_state));
    CHECK(stage.transmitted_packet_count() == 1U);
    REQUIRE(stage.history().size() == 1U);
    CHECK(stage.history().entries()[0U].new_transmission_count == 1U);
    CHECK(transport.sent.size() == 2U);
}

TEST_CASE("Usercmd stage abandons a stale outgoing context and regenerates checksum",
          "[goldsrc][usercmd][transmission-stage][context][stale]")
{
    StageTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, driver_config()};
    activate(driver, transport, remote, epoch);
    const auto schema_binding = binding();
    goldsrc::GoldSrcUserCmdTransmissionConfig config;
    config.maximum_transmission_phases_per_update = 1U;
    goldsrc::GoldSrcUserCmdTransmissionStage stage{
        driver, schema_binding, synthetic_ready(), config};
    const auto intent = intent_from_events({input::InputEvent::focus_gained(),
        input::InputEvent::key_pressed(input::PhysicalKey::w)});
    const auto camera_state = make_camera(25.0, -5.0);

    REQUIRE(stage.update(epoch + 2ms, intent, camera_state));
    REQUIRE(stage.update(epoch + 12ms, intent, camera_state));
    CHECK(stage.state() ==
        goldsrc::GoldSrcUserCmdTransmissionState::
            waiting_for_unreliable_submission);
    REQUIRE(stage.history().size() == 1U);
    CHECK(stage.history().entries()[0U].new_transmission_count == 0U);
    CHECK(stage.transmitted_packet_count() == 0U);
    CHECK(transport.sent.empty());

    goldsrc::GoldSrcUserCmdPacketPlanner stale_checksum_probe;
    auto stale_plan = stale_checksum_probe.prepare(
        stage.history(), schema_binding, 2U);
    REQUIRE(stale_plan);
    REQUIRE(stale_plan.plan);
    const auto stale_checksum =
        stale_plan.plan->encoded_message().checksum();
    const auto stale_sequences = stale_plan.plan->ordered_sequences();
    const auto& stale_bytes = stale_plan.plan->encoded_message().bytes();
    REQUIRE(stale_bytes.size() >= 2U);
    const std::vector<std::byte> stale_command_body{
        stale_bytes.begin() + 2, stale_bytes.end()};
    REQUIRE(stale_checksum_probe.abandon(std::move(*stale_plan.plan)));

    std::vector<std::byte> unrelated_payload{std::byte{0x77U}};
    REQUIRE(driver.submit_unreliable(unrelated_payload));
    REQUIRE(stage.update(epoch + 13ms, intent, camera_state));
    CHECK(stage.state() ==
        goldsrc::GoldSrcUserCmdTransmissionState::waiting_for_packet_context);
    REQUIRE(stage.history().size() == 1U);
    CHECK(stage.history().entries()[0U].new_transmission_count == 0U);
    CHECK(stage.transmitted_packet_count() == 0U);
    CHECK(transport.sent.empty());

    bool saw_stale_context = false;
    bool saw_stale_encoding = false;
    bool saw_stale_submission = false;
    while (const auto event = stage.poll_event()) {
        if (event->type ==
                goldsrc::GoldSrcUserCmdTransmissionEventType::
                    move_packet_encoded &&
            event->outgoing_netchan_sequence == 2U) {
            saw_stale_encoding = true;
        }
        if (event->type ==
            goldsrc::GoldSrcUserCmdTransmissionEventType::move_context_stale) {
            saw_stale_context = true;
            CHECK(event->outgoing_netchan_sequence == 2U);
            CHECK(event->history_size == 1U);
        }
        saw_stale_submission = saw_stale_submission ||
            event->type == goldsrc::GoldSrcUserCmdTransmissionEventType::
                move_packet_submitted;
    }
    CHECK(saw_stale_context);
    CHECK(saw_stale_encoding);
    CHECK_FALSE(saw_stale_submission);

    // The unrelated owning payload advances sequence 2 while the usercmd
    // history remains retryable; it must not contain the stale move bytes.
    REQUIRE(stage.update(epoch + 13ms, intent, camera_state));
    REQUIRE(transport.sent.size() == 1U);
    auto unrelated = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent[0U].payload);
    REQUIRE(unrelated);
    REQUIRE(unrelated.packet);
    CHECK(unrelated.packet->header.sequence.sequence == net_sequence(2U));
    REQUIRE(unrelated.packet->payload.size() >= unrelated_payload.size());
    CHECK(std::ranges::equal(
        unrelated_payload,
        std::span<const std::byte>{unrelated.packet->payload}.first(
            unrelated_payload.size())));
    CHECK(std::to_integer<std::uint8_t>(
              unrelated.packet->payload[0U]) !=
        goldsrc::kSyntheticClientMoveOpcode);
    CHECK(stage.history().entries()[0U].new_transmission_count == 0U);

    REQUIRE(stage.update(epoch + 13ms, intent, camera_state));
    CHECK(stage.state() ==
        goldsrc::GoldSrcUserCmdTransmissionState::
            waiting_for_unreliable_submission);
    goldsrc::GoldSrcUserCmdPacketPlanner fresh_checksum_probe;
    auto fresh_plan = fresh_checksum_probe.prepare(
        stage.history(), schema_binding, 3U);
    REQUIRE(fresh_plan);
    REQUIRE(fresh_plan.plan);
    const auto fresh_checksum =
        fresh_plan.plan->encoded_message().checksum();
    CHECK(fresh_plan.plan->ordered_sequences() == stale_sequences);
    const auto fresh_bytes = fresh_plan.plan->encoded_message().bytes();
    REQUIRE(fresh_bytes.size() >= 2U);
    CHECK(std::ranges::equal(
        stale_command_body,
        std::span<const std::byte>{fresh_bytes}.subspan(2U)));
    CHECK(fresh_checksum != stale_checksum);
    REQUIRE(fresh_checksum_probe.abandon(std::move(*fresh_plan.plan)));

    bool saw_regenerated_encoding = false;
    while (const auto event = stage.poll_event()) {
        if (event->type ==
                goldsrc::GoldSrcUserCmdTransmissionEventType::
                    move_packet_encoded &&
            event->outgoing_netchan_sequence == 3U) {
            saw_regenerated_encoding = true;
        }
    }
    CHECK(saw_regenerated_encoding);

    REQUIRE(stage.update(epoch + 13ms, intent, camera_state));
    REQUIRE(transport.sent.size() == 2U);
    auto regenerated = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent[1U].payload);
    REQUIRE(regenerated);
    REQUIRE(regenerated.packet);
    CHECK(regenerated.packet->header.sequence.sequence == net_sequence(3U));
    REQUIRE(regenerated.packet->payload.size() >= 2U);
    CHECK(std::to_integer<std::uint8_t>(
              regenerated.packet->payload[0U]) ==
        goldsrc::kSyntheticClientMoveOpcode);
    CHECK(std::to_integer<std::uint8_t>(
              regenerated.packet->payload[1U]) == fresh_checksum);
    CHECK(std::to_integer<std::uint8_t>(
              regenerated.packet->payload[1U]) != stale_checksum);
    CHECK(std::ranges::equal(
        fresh_bytes,
        std::span<const std::byte>{regenerated.packet->payload}.first(
            fresh_bytes.size())));
    CHECK(stage.history().entries()[0U].new_transmission_count == 1U);
    CHECK(stage.transmitted_packet_count() == 1U);
}

TEST_CASE("Usercmd stage rejects an event budget below one atomic move",
          "[goldsrc][usercmd][transmission-stage][configuration][event-budget]")
{
    StageTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, driver_config()};
    activate(driver, transport, remote, epoch);
    const auto schema_binding = binding();
    goldsrc::GoldSrcUserCmdTransmissionConfig config;
    config.scheduler.maximum_commands_per_update = 1U;
    config.maximum_events = 2U;
    goldsrc::GoldSrcUserCmdTransmissionStage stage{
        driver, schema_binding, synthetic_ready(), config};
    REQUIRE_FALSE(stage.valid_configuration());
    const auto queued = stage.queue_impulse(9U);
    REQUIRE_FALSE(queued);
    REQUIRE(queued.error);
    CHECK(queued.error->code ==
          goldsrc::GoldSrcUserCmdTransmissionErrorCode::not_active);
    const auto intent = intent_from_events({input::InputEvent::focus_gained()});
    const auto camera_state = make_camera();
    const auto failed = stage.update(epoch + 2ms, intent, camera_state);
    REQUIRE_FALSE(failed);
    REQUIRE(failed.error);
    CHECK(failed.error->code ==
          goldsrc::GoldSrcUserCmdTransmissionErrorCode::invalid_configuration);
    CHECK(stage.state() ==
          goldsrc::GoldSrcUserCmdTransmissionState::protocol_error);
    CHECK(stage.sampled_command_count() == 0U);
    CHECK(stage.history().size() == 0U);
    CHECK(stage.transmitted_packet_count() == 0U);
    CHECK(transport.sent.empty());
    CHECK(driver.cleanup_count() == 1U);
}

TEST_CASE("Usercmd event backpressure drains and retries one-shot exactly once",
          "[goldsrc][usercmd][transmission-stage][transaction][event-budget][retry]")
{
    StageTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, driver_config()};
    activate(driver, transport, remote, epoch);
    const auto schema_binding = binding();
    goldsrc::GoldSrcUserCmdTransmissionConfig config;
    config.scheduler.maximum_commands_per_update = 1U;
    config.maximum_events = 9U;
    goldsrc::GoldSrcUserCmdTransmissionStage stage{
        driver, schema_binding, synthetic_ready(), config};
    REQUIRE(stage.valid_configuration());
    const auto intent = intent_from_events({input::InputEvent::focus_gained()});
    const auto camera_state = make_camera();
    REQUIRE(stage.update(epoch + 2ms, intent, camera_state));
    REQUIRE(stage.update(epoch + 12ms, intent, camera_state));
    REQUIRE(stage.sampled_command_count() == 1U);
    REQUIRE(stage.transmitted_packet_count() == 1U);
    REQUIRE(transport.sent.size() == 1U);
    const auto history_before = stage.history();
    REQUIRE(stage.queue_impulse(9U));

    const auto blocked = stage.update(epoch + 22ms, intent, camera_state);
    REQUIRE_FALSE(blocked);
    REQUIRE(blocked.error);
    CHECK(blocked.error->code ==
          goldsrc::GoldSrcUserCmdTransmissionErrorCode::event_backpressure);
    CHECK(stage.state() ==
          goldsrc::GoldSrcUserCmdTransmissionState::event_backpressure);
    CHECK_FALSE(stage.terminal());
    CHECK(stage.sampled_command_count() == 1U);
    CHECK(stage.transmitted_packet_count() == 1U);
    CHECK(transport.sent.size() == 1U);
    check_same_stage_history(history_before, stage.history());
    CHECK(goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::
              pending_impulse(stage) == 9U);

    std::size_t drained_event_count = 0U;
    while (stage.poll_event()) {
        ++drained_event_count;
    }
    CHECK(drained_event_count == 6U);

    REQUIRE(stage.update(epoch + 22ms, intent, camera_state));
    CHECK(stage.state() ==
          goldsrc::GoldSrcUserCmdTransmissionState::waiting_for_next_sample);
    CHECK(stage.sampled_command_count() == 2U);
    CHECK(stage.transmitted_packet_count() == 2U);
    CHECK(transport.sent.size() == 2U);
    const auto history_after = stage.history();
    REQUIRE(history_after.entries().size() == 2U);
    REQUIRE(history_after.entries()[1U].command);
    CHECK(history_after.entries()[1U].command->impulse() == 9U);
    CHECK_FALSE(goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::
                    pending_impulse(stage));
    CHECK(driver.cleanup_count() == 0U);
}

TEST_CASE("Usercmd stage preflights exhausted commits before driver transmission",
          "[goldsrc][usercmd][transmission-stage][transaction][overflow][preflight]")
{
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    const auto intent =
        intent_from_events({input::InputEvent::focus_gained()});
    const auto camera_state = make_camera();
    const auto require_preflight_failure = [](
        const goldsrc::GoldSrcUserCmdTransmissionOperationResult& result,
        const goldsrc::GoldSrcUserCmdTransmissionStage& stage,
        const goldsrc::NetchanDriver& driver,
        const StageTransport& transport,
        const goldsrc::GoldSrcUserCmdHistoryState& before) {
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              goldsrc::GoldSrcUserCmdTransmissionErrorCode::
                  packet_plan_failed);
        REQUIRE(result.error->planner_code);
        CHECK(*result.error->planner_code ==
              goldsrc::GoldSrcUserCmdPacketPlannerErrorCode::
                  history_commit_failed);
        CHECK(stage.state() ==
              goldsrc::GoldSrcUserCmdTransmissionState::protocol_error);
        CHECK(stage.transmitted_packet_count() == 0U);
        CHECK(transport.sent.empty());
        CHECK(driver.cleanup_count() == 1U);
        check_same_stage_history(before, stage.history());
    };

    SECTION("history revision")
    {
        StageTransport transport;
        goldsrc::NetchanDriver driver{
            transport, remote, driver_config()};
        activate(driver, transport, remote, epoch);
        const auto schema_binding = binding();
        goldsrc::GoldSrcUserCmdTransmissionStage stage{
            driver, schema_binding, synthetic_ready()};
        REQUIRE(stage.update(epoch + 2ms, intent, camera_state));
        auto& history =
            goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::history(
                stage);
        REQUIRE(history.insert(injected_command(1U)));
        goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::
            set_history_revision(
                history, std::numeric_limits<std::uint64_t>::max());
        const auto before = stage.history();

        const auto failed = stage.update(epoch + 2ms, intent, camera_state);
        require_preflight_failure(failed, stage, driver, transport, before);
    }

    SECTION("selected backup transmission count")
    {
        StageTransport transport;
        goldsrc::NetchanDriver driver{
            transport, remote, driver_config()};
        activate(driver, transport, remote, epoch);
        const auto schema_binding = binding();
        goldsrc::GoldSrcUserCmdTransmissionStage stage{
            driver, schema_binding, synthetic_ready()};
        REQUIRE(stage.update(epoch + 2ms, intent, camera_state));
        auto& history =
            goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::history(
                stage);
        REQUIRE(history.insert(injected_command(1U)));
        REQUIRE(history.insert(injected_command(2U)));
        REQUIRE(goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::
                    set_transmission_counts(
                        history,
                        command_sequence(1U),
                        1U,
                        std::numeric_limits<std::uint32_t>::max()));
        const auto before = stage.history();

        const auto failed = stage.update(epoch + 2ms, intent, camera_state);
        require_preflight_failure(failed, stage, driver, transport, before);
    }

    SECTION("planner revision")
    {
        StageTransport transport;
        goldsrc::NetchanDriver driver{
            transport, remote, driver_config()};
        activate(driver, transport, remote, epoch);
        const auto schema_binding = binding();
        goldsrc::GoldSrcUserCmdTransmissionStage stage{
            driver, schema_binding, synthetic_ready()};
        REQUIRE(stage.update(epoch + 2ms, intent, camera_state));
        auto& history =
            goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::history(
                stage);
        REQUIRE(history.insert(injected_command(1U)));
        goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::
            set_planner_revision(
                goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::planner(
                    stage),
                std::numeric_limits<std::uint64_t>::max());
        const auto before = stage.history();

        const auto failed = stage.update(epoch + 2ms, intent, camera_state);
        REQUIRE_FALSE(failed);
        REQUIRE(failed.error);
        CHECK(failed.error->code ==
              goldsrc::GoldSrcUserCmdTransmissionErrorCode::
                  packet_plan_failed);
        REQUIRE(failed.error->planner_code);
        CHECK(*failed.error->planner_code ==
              goldsrc::GoldSrcUserCmdPacketPlannerErrorCode::
                  revision_overflow);
        CHECK(stage.transmitted_packet_count() == 0U);
        CHECK(transport.sent.empty());
        CHECK(driver.cleanup_count() == 1U);
        check_same_stage_history(before, stage.history());
    }
}

TEST_CASE("Usercmd stage timeout cancellation and send failure clean lifetime once",
          "[goldsrc][usercmd][transmission-stage][terminal]")
{
    SECTION("timeout and idempotent close") {
        StageTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'015U);
        const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
        auto releases = std::make_shared<std::size_t>(0U);
        goldsrc::NetchanDriver driver{transport, remote, driver_config(),
            std::make_unique<LifetimeProbe>(releases)};
        activate(driver, transport, remote, epoch);
        const auto schema_binding = binding();
        goldsrc::GoldSrcUserCmdTransmissionConfig config;
        config.timeout = 20ms;
        goldsrc::GoldSrcUserCmdTransmissionStage stage{
            driver, schema_binding, synthetic_ready(), config};
        const auto intent = intent_from_events({input::InputEvent::focus_gained()});
        const auto camera_state = make_camera();
        REQUIRE(stage.update(epoch + 2ms, intent, camera_state));
        const auto timed_out = stage.update(epoch + 22ms, intent, camera_state);
        REQUIRE_FALSE(timed_out);
        CHECK(stage.state() == goldsrc::GoldSrcUserCmdTransmissionState::timed_out);
        CHECK(driver.cleanup_count() == 1U);
        CHECK(*releases == 1U);
        stage.close(epoch + 23ms);
        stage.close(epoch + 24ms);
        CHECK(driver.cleanup_count() == 1U);
        CHECK(*releases == 1U);
    }

    SECTION("timeout comparison is safe across the full clock domain") {
        StageTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'015U);
        const auto epoch = goldsrc::NetchanDriverTimePoint::min() + 1s;
        goldsrc::NetchanDriver driver{transport, remote, driver_config()};
        activate(driver, transport, remote, epoch);
        const auto schema_binding = binding();
        goldsrc::GoldSrcUserCmdTransmissionConfig config;
        config.timeout = 20ms;
        goldsrc::GoldSrcUserCmdTransmissionStage stage{
            driver, schema_binding, synthetic_ready(), config};
        const auto intent = intent_from_events({input::InputEvent::focus_gained()});
        const auto camera_state = make_camera();
        REQUIRE(stage.update(epoch + 2ms, intent, camera_state));
        const auto timed_out = stage.update(
            goldsrc::NetchanDriverTimePoint::max(), intent, camera_state);
        REQUIRE_FALSE(timed_out);
        REQUIRE(timed_out.error);
        CHECK(timed_out.error->code ==
            goldsrc::GoldSrcUserCmdTransmissionErrorCode::timed_out);
        CHECK(stage.state() ==
            goldsrc::GoldSrcUserCmdTransmissionState::timed_out);
    }

    SECTION("cancel") {
        StageTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'015U);
        const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
        goldsrc::NetchanDriver driver{transport, remote, driver_config()};
        activate(driver, transport, remote, epoch);
        const auto schema_binding = binding();
        goldsrc::GoldSrcUserCmdTransmissionStage stage{
            driver, schema_binding, synthetic_ready()};
        stage.cancel(epoch + 2ms);
        stage.cancel(epoch + 3ms);
        CHECK(stage.state() == goldsrc::GoldSrcUserCmdTransmissionState::cancelled);
        CHECK(driver.cleanup_count() == 1U);
    }

    SECTION("network send failure leaves the command retry metadata unsent") {
        StageTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'015U);
        const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
        goldsrc::NetchanDriver driver{transport, remote, driver_config()};
        activate(driver, transport, remote, epoch);
        const auto schema_binding = binding();
        goldsrc::GoldSrcUserCmdTransmissionConfig config;
        config.maximum_transmission_phases_per_update = 1U;
        goldsrc::GoldSrcUserCmdTransmissionStage stage{
            driver, schema_binding, synthetic_ready(), config};
        const auto intent = intent_from_events({input::InputEvent::focus_gained()});
        const auto camera_state = make_camera();
        REQUIRE(stage.update(epoch + 2ms, intent, camera_state));
        REQUIRE(stage.update(epoch + 12ms, intent, camera_state));
        CHECK(stage.state() == goldsrc::GoldSrcUserCmdTransmissionState::
                                   waiting_for_unreliable_submission);
        REQUIRE(stage.queue_impulse(9U));
        CHECK(goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::
                  pending_impulse(stage) == 9U);
        transport.fail_send = true;
        const auto failed = stage.update(epoch + 13ms, intent, camera_state);
        REQUIRE_FALSE(failed);
        CHECK(stage.state() ==
            goldsrc::GoldSrcUserCmdTransmissionState::network_error);
        REQUIRE(stage.history().size() == 1U);
        CHECK(stage.history().entries()[0U].new_transmission_count == 0U);
        CHECK(stage.transmitted_packet_count() == 0U);
        CHECK(transport.sent.empty());
        CHECK_FALSE(goldsrc_detail::GoldSrcUserCmdTransactionalTestAccess::
                        pending_impulse(stage));
        CHECK(driver.cleanup_count() == 1U);
    }
}

TEST_CASE("Usercmd stage progress deadline does not cap a healthy session lifetime",
          "[goldsrc][usercmd][transmission-stage][timeout][progress]")
{
    StageTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'015U);
    const auto epoch = goldsrc::NetchanDriverTimePoint{} + 1s;
    goldsrc::NetchanDriver driver{transport, remote, driver_config()};
    activate(driver, transport, remote, epoch);
    const auto schema_binding = binding();
    goldsrc::GoldSrcUserCmdTransmissionConfig config;
    config.timeout = 20ms;
    goldsrc::GoldSrcUserCmdTransmissionStage stage{
        driver, schema_binding, synthetic_ready(), config};
    const auto intent = intent_from_events({input::InputEvent::focus_gained()});
    const auto camera_state = make_camera();

    REQUIRE(stage.update(epoch + 2ms, intent, camera_state));
    for (std::size_t command = 1U; command <= 12U; ++command) {
        REQUIRE(stage.update(
            epoch + 2ms + std::chrono::milliseconds{
                static_cast<std::chrono::milliseconds::rep>(10U * command)},
            intent,
            camera_state));
    }

    CHECK_FALSE(stage.terminal());
    CHECK(stage.sampled_command_count() == 12U);
    CHECK(stage.transmitted_packet_count() == 12U);
    CHECK(transport.sent.size() == 12U);
}

} // namespace
