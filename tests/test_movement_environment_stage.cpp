#include "delta_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"

#include <hlclient/goldsrc/movement_environment_stage.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/network/datagram_transport.hpp>

#include <catch2/catch_test_macros.hpp>

#include <bzlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace delta_fixture = hlclient::test::delta_fixture;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

struct SentDatagram {
    network::NetworkAddress destination;
    std::vector<std::byte> payload;
};

class FakeTransport final : public network::IDatagramTransport {
public:
    [[nodiscard]] network::DatagramLocalAddressResult local_address() const override
    {
        return network::DatagramLocalAddressResult{local, {}};
    }

    [[nodiscard]] network::DatagramSendResult send_to(
        const network::NetworkAddress& destination,
        const std::span<const std::byte> payload) override
    {
        sent.push_back(SentDatagram{
            destination,
            std::vector<std::byte>{payload.begin(), payload.end()},
        });
        return {network::DatagramSendStatus::sent, {}};
    }

    [[nodiscard]] network::DatagramTransportReceiveResult receive(std::size_t) override
    {
        ++receive_calls;
        if (incoming.empty()) {
            return {
                network::DatagramTransportReceiveStatus::would_block,
                std::nullopt,
                std::nullopt,
                0U,
                {},
            };
        }
        auto result = std::move(incoming.front());
        incoming.pop_front();
        return result;
    }

    void queue(
        const network::NetworkAddress source,
        std::vector<std::byte> payload)
    {
        const auto size = payload.size();
        incoming.push_back({
            network::DatagramTransportReceiveStatus::received,
            network::Datagram{source, std::move(payload)},
            source,
            size,
            {},
        });
    }

    void queue_error()
    {
        incoming.push_back({
            network::DatagramTransportReceiveStatus::error,
            std::nullopt,
            std::nullopt,
            0U,
            "synthetic movement-stage receive failure",
        });
    }

    network::NetworkAddress local{network::NetworkAddress::loopback(30'601U)};
    std::size_t receive_calls{0U};
    std::vector<SentDatagram> sent;
    std::deque<network::DatagramTransportReceiveResult> incoming;
};

class CountingLifetime final : public goldsrc::INetchanDriverLifetime {
public:
    explicit CountingLifetime(std::size_t& releases) noexcept : releases_{releases} {}
    ~CountingLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto parsed = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(parsed);
    return *parsed;
}

[[nodiscard]] std::vector<std::byte> service_envelope(
    const std::span<const std::byte> semantic)
{
    REQUIRE_FALSE(semantic.empty());
    REQUIRE(semantic.size() <= (std::numeric_limits<unsigned int>::max)());
    std::vector<char> source;
    source.reserve(semantic.size());
    std::ranges::transform(
        semantic,
        std::back_inserter(source),
        [](const std::byte value) {
            return static_cast<char>(std::to_integer<std::uint8_t>(value));
        });

    const auto bound = source.size() + source.size() / 100U + 601U;
    REQUIRE(bound <= (std::numeric_limits<unsigned int>::max)());
    std::vector<char> compressed(bound);
    auto compressed_size = static_cast<unsigned int>(compressed.size());
    REQUIRE(BZ2_bzBuffToBuffCompress(
                compressed.data(),
                &compressed_size,
                source.data(),
                static_cast<unsigned int>(source.size()),
                9,
                0,
                30) == BZ_OK);
    compressed.resize(compressed_size);

    std::vector<std::byte> envelope{
        std::byte{0x42U},
        std::byte{0x5aU},
        std::byte{0x32U},
        std::byte{0x00U},
    };
    std::ranges::transform(
        compressed,
        std::back_inserter(envelope),
        [](const char value) {
            return static_cast<std::byte>(static_cast<unsigned char>(value));
        });
    return envelope;
}

[[nodiscard]] std::vector<std::byte> server_packet(
    const std::uint32_t acknowledgement,
    std::vector<std::byte> payload,
    const std::uint32_t server_sequence = 1U)
{
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(server_sequence),
                goldsrc::NetchanSequenceFlags{true, false},
            },
            goldsrc::NetchanAcknowledgementWord{
                sequence(acknowledgement),
                true,
            },
        },
        {},
        std::move(payload),
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return std::move(*encoded.datagram);
}

[[nodiscard]] std::vector<std::byte> normal_fragment_packet(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
    const std::uint16_t fragment_index,
    std::vector<std::byte> fragment_payload)
{
    REQUIRE_FALSE(fragment_payload.empty());
    REQUIRE(fragment_payload.size() <=
            (std::numeric_limits<std::uint16_t>::max)());
    const auto fragment_size = fragment_payload.size();
    goldsrc::NetchanFragmentSlots fragments;
    fragments[0U] = goldsrc::NetchanFragmentDescriptor{
        0U,
        (static_cast<std::uint32_t>(fragment_index) << 16U) | 2U,
        0U,
        static_cast<std::uint16_t>(fragment_size),
        0U,
    };
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(packet_sequence),
                goldsrc::NetchanSequenceFlags{true, true},
            },
            goldsrc::NetchanAcknowledgementWord{
                sequence(acknowledgement),
                false,
            },
        },
        std::move(fragments),
        std::move(fragment_payload),
        fragment_size,
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return std::move(*encoded.datagram);
}

[[nodiscard]] goldsrc::MovementEnvironmentStageConfig test_config()
{
    goldsrc::MovementEnvironmentStageConfig config;
    config.delta.pre_resource.initial_signon.driver.channel_inactivity_timeout = 50ms;
    config.delta.pre_resource.initial_signon.driver.fragment_transfer_timeout = 50ms;
    config.delta.pre_resource.initial_signon.driver.maximum_datagrams_per_update = 8U;
    config.delta.pre_resource.initial_signon.driver.maximum_outgoing_packets_per_update = 8U;
    config.delta.pre_resource.initial_signon.driver.maximum_events = 32U;
    config.delta.pre_resource.initial_signon.maximum_events = 32U;
    config.delta.pre_resource.initial_signon.maximum_driver_events_per_update = 32U;
    config.delta.pre_resource.maximum_events = 32U;
    config.delta.maximum_events = 32U;
    config.maximum_events = 64U;
    return config;
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket start_and_send_request(
    goldsrc::MovementEnvironmentStage& stage,
    FakeTransport& transport,
    const goldsrc::MovementEnvironmentStageTimePoint epoch,
    std::unique_ptr<goldsrc::INetchanDriverLifetime> lifetime = {})
{
    REQUIRE(stage.start(epoch, transport.local, std::move(lifetime)));
    CHECK(stage.state() ==
          goldsrc::MovementEnvironmentStageState::waiting_for_delta_state);
    stage.update(epoch + 1ms);
    REQUIRE(transport.sent.size() == 1U);
    auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent.front().payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    REQUIRE(decoded.packet->payload.size() == 8U);
    const std::array exact{
        std::byte{0x03U},
        std::byte{0x6eU},
        std::byte{0x65U},
        std::byte{0x77U},
        std::byte{0x00U},
    };
    CHECK(std::ranges::equal(
        exact,
        std::span<const std::byte>{decoded.packet->payload}.first(exact.size())));
    return std::move(*decoded.packet);
}

[[nodiscard]] std::vector<std::vector<std::byte>> two_schemas()
{
    return {
        delta_fixture::schema("alpha_t", delta_fixture::kSchemaAlphaFields),
        delta_fixture::schema("bravo_t", delta_fixture::kSchemaBravoFields),
    };
}

[[nodiscard]] std::vector<std::byte> semantic_payload(
    std::vector<std::byte> post_delta_body =
        move_fixture::move_vars_body_and_post_stream())
{
    const auto schemas = two_schemas();
    return delta_fixture::service_payload(
        schemas,
        goldsrc::kMoveVarsOpcode,
        post_delta_body);
}

[[nodiscard]] std::vector<std::byte> stock_sized_post_delta_body()
{
    std::vector<std::byte> body;
    move_fixture::append_move_vars_body(body);
    move_fixture::append_opcode_32_control(body);
    move_fixture::append_opcode_5_control(body);
    for (std::size_t index = 0U; index < 40U; ++index) {
        const auto name = "Msg" + std::to_string(index);
        move_fixture::append_opcode_39_control(
            body,
            static_cast<std::uint8_t>(64U + index),
            static_cast<std::int8_t>(index),
            name);
    }
    move_fixture::append_opcode_13_boundary(body);
    return body;
}

void deliver(
    goldsrc::MovementEnvironmentStage& stage,
    FakeTransport& transport,
    const network::NetworkAddress remote,
    const goldsrc::ClientToServerNetchanPacket& request,
    std::vector<std::byte> semantic,
    const goldsrc::MovementEnvironmentStageTimePoint now)
{
    transport.queue(
        remote,
        server_packet(
            request.header.sequence.sequence.value(),
            service_envelope(semantic)));
    stage.update(now);
}

void check_request_then_padding_ack(
    const FakeTransport& transport,
    const network::NetworkAddress remote)
{
    REQUIRE(transport.sent.size() == 2U);
    CHECK(transport.sent[0U].destination == remote);
    CHECK(transport.sent[1U].destination == remote);
    const auto acknowledgement = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent[1U].payload);
    REQUIRE(acknowledgement);
    REQUIRE(acknowledgement.packet);
    CHECK(std::ranges::all_of(
        acknowledgement.packet->payload,
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));
}

struct SemanticNegativeCase {
    std::string_view name;
    std::vector<std::byte> post_delta_body;
    goldsrc::MovementEnvironmentStageState expected_state;
    goldsrc::MoveVarsStreamErrorCode expected_stream_code;
    std::optional<goldsrc::MoveVarsErrorCode> expected_parser_code;
};

[[nodiscard]] std::vector<SemanticNegativeCase> semantic_negative_cases()
{
    std::vector<SemanticNegativeCase> cases;

    auto nan = move_fixture::move_vars_body_and_post_stream();
    REQUIRE(nan.size() >= 4U);
    nan[0U] = std::byte{0x00U};
    nan[1U] = std::byte{0x00U};
    nan[2U] = std::byte{0xc0U};
    nan[3U] = std::byte{0x7fU};
    cases.push_back({
        "NaN move-vars field",
        std::move(nan),
        goldsrc::MovementEnvironmentStageState::protocol_error,
        goldsrc::MoveVarsStreamErrorCode::move_vars_parse_failed,
        goldsrc::MoveVarsErrorCode::non_finite_numeric_field,
    });

    auto invalid_footsteps = move_fixture::move_vars_body_and_post_stream();
    REQUIRE(invalid_footsteps.size() > 16U * sizeof(float));
    invalid_footsteps[16U * sizeof(float)] = std::byte{2U};
    cases.push_back({
        "invalid footsteps byte",
        std::move(invalid_footsteps),
        goldsrc::MovementEnvironmentStageState::protocol_error,
        goldsrc::MoveVarsStreamErrorCode::move_vars_parse_failed,
        goldsrc::MoveVarsErrorCode::invalid_footsteps,
    });

    std::vector<std::byte> unterminated_sky;
    move_fixture::append_move_vars_body(unterminated_sky);
    REQUIRE_FALSE(unterminated_sky.empty());
    unterminated_sky.pop_back();
    cases.push_back({
        "unterminated sky name",
        std::move(unterminated_sky),
        goldsrc::MovementEnvironmentStageState::protocol_error,
        goldsrc::MoveVarsStreamErrorCode::move_vars_parse_failed,
        goldsrc::MoveVarsErrorCode::unterminated_sky_name,
    });

    std::vector<std::byte> bad_user_message_padding;
    move_fixture::append_move_vars_body(bad_user_message_padding);
    const auto user_message_offset = bad_user_message_padding.size();
    move_fixture::append_opcode_39_control(
        bad_user_message_padding,
        64U,
        std::int8_t{-1},
        "HudText");
    REQUIRE(bad_user_message_padding.size() > user_message_offset + 18U);
    bad_user_message_padding[user_message_offset + 18U] = std::byte{1U};
    move_fixture::append_opcode_13_boundary(bad_user_message_padding);
    cases.push_back({
        "opcode-39 nonzero reserved padding",
        std::move(bad_user_message_padding),
        goldsrc::MovementEnvironmentStageState::protocol_error,
        goldsrc::MoveVarsStreamErrorCode::invalid_control_value,
        std::nullopt,
    });

    std::vector<std::byte> duplicate_move_vars;
    move_fixture::append_move_vars_body(duplicate_move_vars);
    move_fixture::append_move_vars_message(duplicate_move_vars);
    move_fixture::append_opcode_13_boundary(duplicate_move_vars);
    cases.push_back({
        "duplicate opcode-44 message",
        std::move(duplicate_move_vars),
        goldsrc::MovementEnvironmentStageState::protocol_error,
        goldsrc::MoveVarsStreamErrorCode::duplicate_move_vars,
        std::nullopt,
    });

    std::vector<std::byte> bodyless_boundary;
    move_fixture::append_move_vars_body(bodyless_boundary);
    bodyless_boundary.push_back(
        static_cast<std::byte>(goldsrc::kStockPostMoveVarsBoundaryOpcode));
    cases.push_back({
        "bodyless opcode-13 boundary",
        std::move(bodyless_boundary),
        goldsrc::MovementEnvironmentStageState::protocol_error,
        goldsrc::MoveVarsStreamErrorCode::malformed_post_movevars_boundary,
        std::nullopt,
    });

    std::vector<std::byte> unknown_opcode;
    move_fixture::append_move_vars_body(unknown_opcode);
    unknown_opcode.push_back(std::byte{99U});
    unknown_opcode.push_back(std::byte{44U});
    unknown_opcode.push_back(
        static_cast<std::byte>(goldsrc::kStockPostMoveVarsBoundaryOpcode));
    unknown_opcode.push_back(std::byte{0xa5U});
    cases.push_back({
        "unknown opcode with later false synchronization points",
        std::move(unknown_opcode),
        goldsrc::MovementEnvironmentStageState::unsupported_message,
        goldsrc::MoveVarsStreamErrorCode::unsupported_post_movevars_opcode,
        std::nullopt,
    });

    return cases;
}

TEST_CASE("Movement environment stage publishes owning state and neutral boundary once",
          "[goldsrc][movevars][stage][success][lifetime][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'241U);
    const auto epoch = goldsrc::MovementEnvironmentStageTimePoint{} + 1s;
    std::size_t releases = 0U;
    std::size_t ready_traces = 0U;
    std::size_t control_traces = 0U;
    std::size_t boundary_traces = 0U;
    bool trace_reentry_target_present = false;
    goldsrc::MovementEnvironmentStage* stage_pointer = nullptr;
    goldsrc::MovementEnvironmentStage stage{
        transport,
        remote,
        test_config(),
        {},
        {},
        {},
        [&](const goldsrc::MovementEnvironmentTraceEvent& event) {
            using Classification =
                goldsrc::MovementEnvironmentTraceClassification;
            if (event.classification == Classification::movement_environment_ready) {
                ++ready_traces;
                trace_reentry_target_present = stage_pointer != nullptr;
                if (stage_pointer != nullptr) {
                    stage_pointer->update(epoch + 50ms);
                    stage_pointer->cancel(epoch + 50ms);
                }
                throw std::runtime_error{"synthetic trace exception"};
            }
            if (event.classification == Classification::post_environment_control) {
                ++control_traces;
            }
            if (event.classification ==
                Classification::post_environment_boundary_reached) {
                ++boundary_traces;
            }
        }};
    stage_pointer = &stage;
    const auto request = start_and_send_request(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    CHECK(releases == 0U);

    deliver(
        stage,
        transport,
        remote,
        request,
        semantic_payload(),
        epoch + 2ms);

    CHECK(stage.state() ==
          goldsrc::MovementEnvironmentStageState::
              post_environment_boundary_reached);
    CHECK(stage.terminal());
    CHECK_FALSE(stage.error());
    REQUIRE(stage.result());
    const auto& result = *stage.result();
    CHECK(result.delta_description().registry().schema_count() == 2U);
    CHECK(result.delta_description().registry().total_field_count() == 4U);
    CHECK(result.move_vars().gravity() == 800.0F);
    CHECK(result.move_vars().maximum_speed() == 320.0F);
    CHECK(result.move_vars().footsteps());
    CHECK(result.move_vars().sky_name() == "desert");
    CHECK(result.control_count() == 6U);
    CHECK(result.boundary().opcode() ==
          goldsrc::kStockPostMoveVarsBoundaryOpcode);
    CHECK(result.boundary().remaining_byte_count() == 1U);
    CHECK(result.bytes_consumed() ==
          result.boundary().byte_offset() -
              result.delta_description().boundary().byte_offset());
    CHECK(stage.request_queue_count() == 1U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);

    std::vector<goldsrc::MovementEnvironmentStageEventType> events;
    while (auto event = stage.poll_event()) {
        events.push_back(event->type);
        CHECK(event->occurred_at == epoch + 2ms);
    }
    REQUIRE(events.size() == 8U);
    CHECK(events.front() ==
          goldsrc::MovementEnvironmentStageEventType::
              movement_environment_ready);
    CHECK(std::ranges::count(
              events,
              goldsrc::MovementEnvironmentStageEventType::
                  post_environment_control) == 6);
    CHECK(events.back() ==
          goldsrc::MovementEnvironmentStageEventType::
              post_environment_boundary);
    CHECK(ready_traces == 1U);
    CHECK(trace_reentry_target_present);
    CHECK(control_traces == 6U);
    CHECK(boundary_traces == 1U);

    REQUIRE(transport.sent.size() == 2U);
    const auto acknowledgement = goldsrc::decode_client_to_server_netchan_packet(
        transport.sent[1U].payload);
    REQUIRE(acknowledgement);
    REQUIRE(acknowledgement.packet);
    CHECK(std::ranges::all_of(
        acknowledgement.packet->payload,
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));

    const auto sends = transport.sent.size();
    const auto receives = transport.receive_calls;
    stage.update(epoch + 3ms);
    stage.cancel(epoch + 4ms);
    CHECK(transport.sent.size() == sends);
    CHECK(transport.receive_calls == receives);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Movement environment stage stock-sized event batch fits exactly and fails at limit minus one",
          "[goldsrc][movevars][stage][events][limit][backpressure]")
{
    const auto remote = network::NetworkAddress::loopback(27'241U);
    const auto epoch = goldsrc::MovementEnvironmentStageTimePoint{} + 1s;

    SECTION("44 required events fit a capacity of 44")
    {
        FakeTransport transport;
        auto config = test_config();
        config.maximum_events = 44U;
        goldsrc::MovementEnvironmentStage stage{transport, remote, config};
        const auto request = start_and_send_request(stage, transport, epoch);
        deliver(
            stage,
            transport,
            remote,
            request,
            semantic_payload(stock_sized_post_delta_body()),
            epoch + 2ms);
        REQUIRE(stage.result());
        CHECK(stage.result()->control_count() == 42U);
        CHECK(stage.pending_event_count() == 44U);
        CHECK(stage.state() ==
              goldsrc::MovementEnvironmentStageState::
                  post_environment_boundary_reached);
    }

    SECTION("44 required events fail atomically at capacity 43")
    {
        FakeTransport transport;
        auto config = test_config();
        config.maximum_events = 43U;
        std::size_t releases = 0U;
        goldsrc::MovementEnvironmentStage stage{transport, remote, config};
        const auto request = start_and_send_request(
            stage,
            transport,
            epoch,
            std::make_unique<CountingLifetime>(releases));
        deliver(
            stage,
            transport,
            remote,
            request,
            semantic_payload(stock_sized_post_delta_body()),
            epoch + 2ms);
        CHECK(stage.state() ==
              goldsrc::MovementEnvironmentStageState::backpressure);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::MovementEnvironmentStageErrorCode::event_backpressure);
        CHECK_FALSE(stage.result());
        CHECK_FALSE(stage.poll_event());
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }
}

TEST_CASE("Movement environment stage rejects malformed and unsupported continuations transactionally",
          "[goldsrc][movevars][stage][negative][transaction]")
{
    const auto remote = network::NetworkAddress::loopback(27'241U);
    const auto epoch = goldsrc::MovementEnvironmentStageTimePoint{} + 1s;

    SECTION("truncated movevars field")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        goldsrc::MovementEnvironmentStage stage{transport, remote, test_config()};
        const auto request = start_and_send_request(
            stage,
            transport,
            epoch,
            std::make_unique<CountingLifetime>(releases));
        std::vector<std::byte> truncated(12U, std::byte{0U});
        deliver(
            stage,
            transport,
            remote,
            request,
            semantic_payload(std::move(truncated)),
            epoch + 2ms);
        CHECK(stage.state() ==
              goldsrc::MovementEnvironmentStageState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::MovementEnvironmentStageErrorCode::
                  move_vars_stream_decode_failed);
        CHECK(stage.error()->stream_code ==
              goldsrc::MoveVarsStreamErrorCode::move_vars_parse_failed);
        CHECK(stage.error()->parser_code ==
              goldsrc::MoveVarsErrorCode::truncated_numeric_field);
        CHECK_FALSE(stage.result());
        CHECK_FALSE(stage.poll_event());
        CHECK(releases == 1U);
    }

    SECTION("unknown post-movevars opcode")
    {
        FakeTransport transport;
        goldsrc::MovementEnvironmentStage stage{transport, remote, test_config()};
        const auto request = start_and_send_request(stage, transport, epoch);
        std::vector<std::byte> body;
        move_fixture::append_move_vars_body(body);
        body.push_back(std::byte{99U});
        body.push_back(std::byte{13U});
        body.push_back(std::byte{0xa5U});
        deliver(
            stage,
            transport,
            remote,
            request,
            semantic_payload(std::move(body)),
            epoch + 2ms);
        CHECK(stage.state() ==
              goldsrc::MovementEnvironmentStageState::unsupported_message);
        REQUIRE(stage.error());
        CHECK(stage.error()->stream_code ==
              goldsrc::MoveVarsStreamErrorCode::
                  unsupported_post_movevars_opcode);
        CHECK_FALSE(stage.result());
        CHECK_FALSE(stage.poll_event());
    }

    SECTION("wrong retained boundary opcode")
    {
        FakeTransport transport;
        goldsrc::MovementEnvironmentStage stage{transport, remote, test_config()};
        const auto request = start_and_send_request(stage, transport, epoch);
        const auto schemas = two_schemas();
        auto semantic = delta_fixture::service_payload(
            schemas,
            99U,
            move_fixture::move_vars_body_and_post_stream());
        deliver(stage, transport, remote, request, std::move(semantic), epoch + 2ms);
        CHECK(stage.state() ==
              goldsrc::MovementEnvironmentStageState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->stream_code ==
              goldsrc::MoveVarsStreamErrorCode::wrong_initial_opcode);
        CHECK_FALSE(stage.result());
        CHECK_FALSE(stage.poll_event());
    }
}

TEST_CASE("Movement environment stage rejects each malformed movement semantic batch atomically",
          "[goldsrc][movevars][stage][negative][table][lifetime][transaction]")
{
    const auto remote = network::NetworkAddress::loopback(27'241U);
    const auto epoch = goldsrc::MovementEnvironmentStageTimePoint{} + 1s;

    for (auto& test_case : semantic_negative_cases()) {
        DYNAMIC_SECTION(test_case.name)
        {
            FakeTransport transport;
            std::size_t releases = 0U;
            std::size_t movement_publications = 0U;
            goldsrc::MovementEnvironmentStage stage{
                transport,
                remote,
                test_config(),
                {},
                {},
                {},
                [&](const goldsrc::MovementEnvironmentTraceEvent& event) {
                    using Classification =
                        goldsrc::MovementEnvironmentTraceClassification;
                    if (event.classification ==
                            Classification::movement_environment_ready ||
                        event.classification ==
                            Classification::post_environment_control ||
                        event.classification ==
                            Classification::post_environment_boundary_reached) {
                        ++movement_publications;
                    }
                }};
            const auto request = start_and_send_request(
                stage,
                transport,
                epoch,
                std::make_unique<CountingLifetime>(releases));

            deliver(
                stage,
                transport,
                remote,
                request,
                semantic_payload(std::move(test_case.post_delta_body)),
                epoch + 2ms);

            CHECK(stage.state() == test_case.expected_state);
            CHECK(stage.terminal());
            REQUIRE(stage.error());
            CHECK(stage.error()->code ==
                  goldsrc::MovementEnvironmentStageErrorCode::
                      move_vars_stream_decode_failed);
            CHECK(stage.error()->delta_code == std::nullopt);
            CHECK(stage.error()->stream_code ==
                  std::optional{test_case.expected_stream_code});
            CHECK(stage.error()->parser_code ==
                  test_case.expected_parser_code);
            CHECK(stage.error()->driver_code == std::nullopt);
            CHECK_FALSE(stage.error()->context.empty());
            CHECK(stage.error()->context.size() <=
                  goldsrc::kMovementEnvironmentStageDiagnosticTextLimit);
            CHECK_FALSE(stage.result());
            CHECK(stage.pending_event_count() == 0U);
            CHECK_FALSE(stage.poll_event());
            CHECK(movement_publications == 0U);
            CHECK(stage.request_queue_count() == 1U);
            CHECK(stage.transmitted_packet_count() == 2U);
            CHECK(stage.cleanup_count() == 1U);
            CHECK(releases == 1U);
            check_request_then_padding_ack(transport, remote);

            const auto sends_at_failure = transport.sent.size();
            const auto receives_at_failure = transport.receive_calls;
            stage.update(epoch + 3ms);
            stage.cancel(epoch + 4ms);
            CHECK(transport.sent.size() == sends_at_failure);
            CHECK(transport.receive_calls == receives_at_failure);
            CHECK(stage.pending_event_count() == 0U);
            CHECK_FALSE(stage.poll_event());
            CHECK_FALSE(stage.result());
            CHECK(movement_publications == 0U);
            CHECK(stage.cleanup_count() == 1U);
            CHECK(releases == 1U);
        }
    }
}

TEST_CASE("Movement environment stage ignores a wrong endpoint and consumes a valid batch once",
          "[goldsrc][movevars][stage][endpoint][duplicate][terminal][lifetime]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'241U);
    const auto rogue = network::NetworkAddress::loopback(27'242U);
    const auto epoch = goldsrc::MovementEnvironmentStageTimePoint{} + 1s;
    std::size_t releases = 0U;
    std::size_t movement_publications = 0U;
    goldsrc::MovementEnvironmentStage stage{
        transport,
        remote,
        test_config(),
        {},
        {},
        {},
        [&](const goldsrc::MovementEnvironmentTraceEvent& event) {
            using Classification =
                goldsrc::MovementEnvironmentTraceClassification;
            if (event.classification ==
                    Classification::movement_environment_ready ||
                event.classification ==
                    Classification::post_environment_control ||
                event.classification ==
                    Classification::post_environment_boundary_reached) {
                ++movement_publications;
            }
        }};
    const auto request = start_and_send_request(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    const auto response = server_packet(
        request.header.sequence.sequence.value(),
        service_envelope(semantic_payload()));

    transport.queue(rogue, response);
    stage.update(epoch + 2ms);
    CHECK(stage.state() ==
          goldsrc::MovementEnvironmentStageState::waiting_for_delta_state);
    CHECK_FALSE(stage.terminal());
    CHECK_FALSE(stage.error());
    CHECK_FALSE(stage.result());
    CHECK(stage.pending_event_count() == 0U);
    CHECK_FALSE(stage.poll_event());
    CHECK(movement_publications == 0U);
    CHECK(stage.request_queue_count() == 1U);
    CHECK(stage.transmitted_packet_count() == 1U);
    CHECK(transport.sent.size() == 1U);
    CHECK(stage.cleanup_count() == 0U);
    CHECK(releases == 0U);

    transport.queue(remote, response);
    stage.update(epoch + 3ms);
    CHECK(stage.state() ==
          goldsrc::MovementEnvironmentStageState::
              post_environment_boundary_reached);
    CHECK(stage.terminal());
    CHECK_FALSE(stage.error());
    REQUIRE(stage.result());
    CHECK(stage.result()->control_count() == 6U);
    CHECK(stage.pending_event_count() == 8U);
    CHECK(movement_publications == 8U);
    CHECK(stage.request_queue_count() == 1U);
    CHECK(stage.transmitted_packet_count() == 2U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
    check_request_then_padding_ack(transport, remote);

    std::size_t event_count = 0U;
    while (stage.poll_event()) {
        ++event_count;
    }
    CHECK(event_count == 8U);
    CHECK(stage.pending_event_count() == 0U);

    const auto sends_at_boundary = transport.sent.size();
    const auto receives_at_boundary = transport.receive_calls;
    transport.queue(remote, response);
    stage.update(epoch + 4ms);
    CHECK(stage.state() ==
          goldsrc::MovementEnvironmentStageState::
              post_environment_boundary_reached);
    REQUIRE(stage.result());
    CHECK(stage.result()->control_count() == 6U);
    CHECK(transport.incoming.size() == 1U);
    CHECK(transport.sent.size() == sends_at_boundary);
    CHECK(transport.receive_calls == receives_at_boundary);
    CHECK(stage.pending_event_count() == 0U);
    CHECK_FALSE(stage.poll_event());
    CHECK(movement_publications == 8U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Movement environment stage maps an invalid BZ2 envelope without partial state",
          "[goldsrc][movevars][stage][envelope][negative][lifetime][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'241U);
    const auto epoch = goldsrc::MovementEnvironmentStageTimePoint{} + 1s;
    std::size_t releases = 0U;
    std::size_t movement_publications = 0U;
    goldsrc::MovementEnvironmentStage stage{
        transport,
        remote,
        test_config(),
        {},
        {},
        {},
        [&](const goldsrc::MovementEnvironmentTraceEvent& event) {
            using Classification =
                goldsrc::MovementEnvironmentTraceClassification;
            if (event.classification ==
                    Classification::movement_environment_ready ||
                event.classification ==
                    Classification::post_environment_control ||
                event.classification ==
                    Classification::post_environment_boundary_reached) {
                ++movement_publications;
            }
        }};
    const auto request = start_and_send_request(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    transport.queue(
        remote,
        server_packet(
            request.header.sequence.sequence.value(),
            std::vector<std::byte>{
                std::byte{0x42U},
                std::byte{0x5aU},
                std::byte{0x32U},
                std::byte{0x00U},
                std::byte{0x42U},
                std::byte{0x5aU},
            }));
    stage.update(epoch + 2ms);

    CHECK(stage.state() ==
          goldsrc::MovementEnvironmentStageState::protocol_error);
    CHECK(stage.terminal());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::MovementEnvironmentStageErrorCode::delta_failed);
    CHECK(stage.error()->delta_code ==
          std::optional{goldsrc::DeltaDescriptionStageErrorCode::
                            pre_resource_failed});
    CHECK(stage.error()->stream_code == std::nullopt);
    CHECK(stage.error()->parser_code == std::nullopt);
    CHECK(stage.error()->driver_code == std::nullopt);
    CHECK_FALSE(stage.error()->context.empty());
    CHECK_FALSE(stage.result());
    CHECK(stage.pending_event_count() == 0U);
    CHECK_FALSE(stage.poll_event());
    CHECK(movement_publications == 0U);
    CHECK(stage.request_queue_count() == 1U);
    CHECK(stage.transmitted_packet_count() == 2U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
    check_request_then_padding_ack(transport, remote);

    const auto sends_at_failure = transport.sent.size();
    const auto receives_at_failure = transport.receive_calls;
    stage.update(epoch + 3ms);
    stage.cancel(epoch + 4ms);
    CHECK(transport.sent.size() == sends_at_failure);
    CHECK(transport.receive_calls == receives_at_failure);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Movement environment stage times out an incomplete two-fragment batch without partial state",
          "[goldsrc][movevars][stage][fragment][timeout][lifetime][transaction]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'241U);
    const auto epoch = goldsrc::MovementEnvironmentStageTimePoint{} + 1s;
    auto config = test_config();
    config.delta.pre_resource.initial_signon.driver.channel_inactivity_timeout =
        100ms;
    config.delta.pre_resource.initial_signon.driver.fragment_transfer_timeout =
        50ms;
    std::size_t releases = 0U;
    std::size_t movement_publications = 0U;
    goldsrc::MovementEnvironmentStage stage{
        transport,
        remote,
        config,
        {},
        {},
        {},
        [&](const goldsrc::MovementEnvironmentTraceEvent& event) {
            using Classification =
                goldsrc::MovementEnvironmentTraceClassification;
            if (event.classification ==
                    Classification::movement_environment_ready ||
                event.classification ==
                    Classification::post_environment_control ||
                event.classification ==
                    Classification::post_environment_boundary_reached) {
                ++movement_publications;
            }
        }};
    const auto request = start_and_send_request(
        stage,
        transport,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    auto fragment = normal_fragment_packet(
        1U,
        request.header.sequence.sequence.value(),
        2U,
        std::vector<std::byte>{std::byte{0x42U}});
    REQUIRE(fragment.size() > 7U);
    // Patch only the independently encoded reliable-ACK generation bit. The
    // fragment descriptor still declares two normal-stream fragments, while
    // this test supplies only fragment two.
    fragment[7U] |= std::byte{0x80U};
    transport.queue(remote, std::move(fragment));
    stage.update(epoch + 2ms);

    CHECK(stage.state() ==
          goldsrc::MovementEnvironmentStageState::waiting_for_delta_state);
    CHECK_FALSE(stage.terminal());
    CHECK_FALSE(stage.error());
    CHECK_FALSE(stage.result());
    CHECK(stage.pending_event_count() == 0U);
    CHECK_FALSE(stage.poll_event());
    CHECK(movement_publications == 0U);
    CHECK(stage.request_queue_count() == 1U);
    CHECK(stage.transmitted_packet_count() == 2U);
    CHECK(stage.cleanup_count() == 0U);
    CHECK(releases == 0U);
    check_request_then_padding_ack(transport, remote);
    const auto sends_while_incomplete = transport.sent.size();

    stage.update(epoch + 52ms);
    CHECK(stage.state() == goldsrc::MovementEnvironmentStageState::timed_out);
    CHECK(stage.terminal());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::MovementEnvironmentStageErrorCode::delta_failed);
    CHECK(stage.error()->delta_code ==
          std::optional{goldsrc::DeltaDescriptionStageErrorCode::
                            pre_resource_failed});
    CHECK(stage.error()->stream_code == std::nullopt);
    CHECK(stage.error()->parser_code == std::nullopt);
    CHECK(stage.error()->driver_code ==
          std::optional{goldsrc::NetchanDriverErrorCode::
                            fragment_transfer_timed_out});
    CHECK_FALSE(stage.result());
    CHECK(stage.pending_event_count() == 0U);
    CHECK_FALSE(stage.poll_event());
    CHECK(movement_publications == 0U);
    CHECK(transport.sent.size() == sends_while_incomplete);
    CHECK(stage.transmitted_packet_count() == 2U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);

    const auto receives_at_timeout = transport.receive_calls;
    stage.update(epoch + 53ms);
    stage.cancel(epoch + 54ms);
    CHECK(transport.sent.size() == sends_while_incomplete);
    CHECK(transport.receive_calls == receives_at_timeout);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Movement environment stage maps timeout cancellation and network failure",
          "[goldsrc][movevars][stage][terminal][lifetime]")
{
    const auto remote = network::NetworkAddress::loopback(27'241U);
    const auto epoch = goldsrc::MovementEnvironmentStageTimePoint{} + 1s;

    SECTION("timeout")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        goldsrc::MovementEnvironmentStage stage{transport, remote, test_config()};
        static_cast<void>(start_and_send_request(
            stage,
            transport,
            epoch,
            std::make_unique<CountingLifetime>(releases)));
        stage.update(epoch + 51ms);
        CHECK(stage.state() ==
              goldsrc::MovementEnvironmentStageState::timed_out);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }

    SECTION("cancellation")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        goldsrc::MovementEnvironmentStage stage{transport, remote, test_config()};
        REQUIRE(stage.start(
            epoch,
            transport.local,
            std::make_unique<CountingLifetime>(releases)));
        stage.cancel(epoch + 1ms);
        CHECK(stage.state() ==
              goldsrc::MovementEnvironmentStageState::cancelled);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }

    SECTION("driver receive error")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        goldsrc::MovementEnvironmentStage stage{transport, remote, test_config()};
        static_cast<void>(start_and_send_request(
            stage,
            transport,
            epoch,
            std::make_unique<CountingLifetime>(releases)));
        transport.queue_error();
        stage.update(epoch + 2ms);
        CHECK(stage.state() ==
              goldsrc::MovementEnvironmentStageState::network_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->driver_code ==
              goldsrc::NetchanDriverErrorCode::receive_failed);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }
}

} // namespace
