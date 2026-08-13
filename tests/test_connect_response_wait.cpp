#include <hlclient/goldsrc/connect_response_wait.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
using hlclient::goldsrc::ConnectAccepted;
using hlclient::goldsrc::ConnectRejected;
using hlclient::goldsrc::ConnectResponseTraceClassification;
using hlclient::goldsrc::ConnectResponseTraceEvent;
using hlclient::goldsrc::ConnectResponseWaitConfig;
using hlclient::goldsrc::ConnectResponseWaitErrorCode;
using hlclient::goldsrc::ConnectResponseWaitStage;
using hlclient::goldsrc::ConnectResponseWaitState;
using hlclient::goldsrc::ConnectResponseWaitTimePoint;
using hlclient::network::Datagram;
using hlclient::network::DatagramLocalAddressResult;
using hlclient::network::DatagramSendResult;
using hlclient::network::DatagramSendStatus;
using hlclient::network::DatagramTransportReceiveResult;
using hlclient::network::DatagramTransportReceiveStatus;
using hlclient::network::IDatagramTransport;
using hlclient::network::NetworkAddress;

class FakeDatagramTransport final : public IDatagramTransport {
public:
    [[nodiscard]] DatagramLocalAddressResult local_address() const override
    {
        ++local_address_queries;
        if (!local_error.empty()) {
            return DatagramLocalAddressResult{std::nullopt, local_error};
        }
        return DatagramLocalAddressResult{local, {}};
    }

    [[nodiscard]] DatagramSendResult send_to(
        const NetworkAddress&,
        std::span<const std::byte>) override
    {
        ++send_count;
        return DatagramSendResult{DatagramSendStatus::sent, {}};
    }

    [[nodiscard]] DatagramTransportReceiveResult receive(
        const std::size_t maximum_size) override
    {
        ++receive_count;
        receive_limits.push_back(maximum_size);
        if (incoming.empty()) {
            return DatagramTransportReceiveResult{
                DatagramTransportReceiveStatus::would_block,
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

    void queue(const NetworkAddress source, std::vector<std::byte> payload)
    {
        const auto size = payload.size();
        incoming.push_back(DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::received,
            Datagram{source, std::move(payload)},
            source,
            size,
            {},
        });
    }

    void queue_truncated(const NetworkAddress source, const std::size_t lower_bound)
    {
        incoming.push_back(DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::truncated,
            std::nullopt,
            source,
            lower_bound,
            "synthetic truncated response",
        });
    }

    NetworkAddress local{NetworkAddress::loopback(30'000)};
    std::string local_error;
    mutable std::size_t local_address_queries{0U};
    std::size_t send_count{0U};
    std::size_t receive_count{0U};
    std::vector<std::size_t> receive_limits;
    std::deque<DatagramTransportReceiveResult> incoming;
};

[[nodiscard]] std::vector<std::byte> connectionless_packet(std::string body)
{
    std::vector<std::byte> packet{
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    const auto bytes = std::as_bytes(std::span{body.data(), body.size()});
    packet.insert(packet.end(), bytes.begin(), bytes.end());
    return packet;
}

[[nodiscard]] std::vector<std::byte> accepted_packet(
    const std::string_view address = "127.0.0.1:30000")
{
    std::string body{"B 1 \""};
    body += address;
    body += "\" 0 10210";
    body.push_back('\0');
    return connectionless_packet(std::move(body));
}

[[nodiscard]] std::vector<std::byte> rejected_packet()
{
    std::string body{"9Invalid connection.\n"};
    body.push_back('\0');
    return connectionless_packet(std::move(body));
}

[[nodiscard]] std::vector<std::byte> unrelated_connectionless_packet()
{
    std::string body{"A00000000 1 3 72057594037927936 0\n"};
    body.push_back('\0');
    return connectionless_packet(std::move(body));
}

[[nodiscard]] ConnectResponseWaitConfig test_config()
{
    ConnectResponseWaitConfig config;
    config.timeout = 350ms;
    config.maximum_datagrams_per_update = 2U;
    config.maximum_datagram_size = 1'024U;
    return config;
}

TEST_CASE("Connect response wait defaults are bounded and the stage never sends",
          "[goldsrc][connect-response][wait]")
{
    const ConnectResponseWaitConfig defaults;
    CHECK(defaults.timeout == 5s);
    CHECK(defaults.maximum_datagrams_per_update == 8U);
    CHECK(defaults.maximum_datagram_size == 1'024U);
    CHECK(hlclient::goldsrc::kMaximumConnectResponseWaitTimeout == 30s);
    CHECK(hlclient::goldsrc::kMaximumConnectResponseDatagramsPerUpdate == 64U);
    CHECK(hlclient::goldsrc::kMaximumConnectResponseWaitDatagramSize == 1'024U);

    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ConnectResponseWaitTimePoint{};
    ConnectResponseWaitStage stage{transport, endpoint};

    REQUIRE(stage.start(epoch, transport.local));
    CHECK(stage.state() == ConnectResponseWaitState::waiting);
    CHECK_FALSE(stage.terminal());
    CHECK(stage.remote_endpoint() == endpoint);
    REQUIRE(stage.local_endpoint());
    CHECK(*stage.local_endpoint() == transport.local);
    REQUIRE(stage.deadline());
    CHECK(*stage.deadline() == epoch + 5s);
    CHECK(transport.local_address_queries == 1U);
    CHECK(transport.send_count == 0U);

    auto maximums = ConnectResponseWaitConfig{};
    maximums.timeout = hlclient::goldsrc::kMaximumConnectResponseWaitTimeout;
    maximums.maximum_datagrams_per_update =
        hlclient::goldsrc::kMaximumConnectResponseDatagramsPerUpdate;
    maximums.maximum_datagram_size =
        hlclient::goldsrc::kMaximumConnectResponseWaitDatagramSize;
    FakeDatagramTransport maximum_transport;
    ConnectResponseWaitStage maximum_stage{maximum_transport, endpoint, maximums};
    REQUIRE(maximum_stage.start(epoch, maximum_transport.local));
    REQUIRE(maximum_stage.deadline());
    CHECK(*maximum_stage.deadline() == epoch + 30s);
    CHECK(maximum_transport.send_count == 0U);
}

TEST_CASE("Connect response wait rejects invalid configuration without polling or sending",
          "[goldsrc][connect-response][wait]")
{
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto expected_local = NetworkAddress::loopback(30'000);

    const auto assert_invalid = [&](const ConnectResponseWaitConfig config,
                                    const NetworkAddress remote =
                                        NetworkAddress::loopback(27'015),
                                    const NetworkAddress local =
                                        NetworkAddress::loopback(30'000),
                                    const ConnectResponseWaitTimePoint now = {}) {
        FakeDatagramTransport transport;
        transport.local = expected_local;
        ConnectResponseWaitStage stage{transport, remote, config};
        CHECK_FALSE(stage.start(now, local));
        CHECK(stage.state() == ConnectResponseWaitState::protocol_error);
        CHECK(stage.terminal());
        REQUIRE(stage.error());
        CHECK(stage.error()->code == ConnectResponseWaitErrorCode::invalid_configuration);
        CHECK(transport.send_count == 0U);
        CHECK(transport.receive_count == 0U);
    };

    auto config = test_config();
    config.timeout = 0ms;
    assert_invalid(config);
    config = test_config();
    config.timeout = -1ms;
    assert_invalid(config);
    config = test_config();
    config.timeout = hlclient::goldsrc::kMaximumConnectResponseWaitTimeout + 1ms;
    assert_invalid(config);
    config = test_config();
    config.maximum_datagrams_per_update = 0U;
    assert_invalid(config);
    config = test_config();
    config.maximum_datagrams_per_update =
        hlclient::goldsrc::kMaximumConnectResponseDatagramsPerUpdate + 1U;
    assert_invalid(config);
    config = test_config();
    config.maximum_datagram_size = 0U;
    assert_invalid(config);
    config = test_config();
    config.maximum_datagram_size =
        hlclient::goldsrc::kMaximumConnectResponseWaitDatagramSize + 1U;
    assert_invalid(config);
    assert_invalid(test_config(), NetworkAddress{0U, 27'015});
    assert_invalid(test_config(), NetworkAddress::loopback(0U));
    assert_invalid(test_config(), endpoint, NetworkAddress::loopback(0U));
    assert_invalid(
        test_config(),
        endpoint,
        expected_local,
        ConnectResponseWaitTimePoint::max() - 1ms);
}

TEST_CASE("Connect response wait requires the exact existing local endpoint",
          "[goldsrc][connect-response][wait]")
{
    const auto endpoint = NetworkAddress::loopback(27'015);

    SECTION("local endpoint query fails")
    {
        FakeDatagramTransport transport;
        transport.local_error.assign(600U, 'x');
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        CHECK_FALSE(stage.start({}, NetworkAddress::loopback(30'000)));
        CHECK(stage.state() == ConnectResponseWaitState::network_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code == ConnectResponseWaitErrorCode::local_endpoint_unavailable);
        CHECK(stage.error()->context.size() ==
              hlclient::goldsrc::kConnectResponseWaitDiagnosticTextLimit);
        CHECK(transport.send_count == 0U);
    }

    SECTION("local endpoint changed")
    {
        FakeDatagramTransport transport;
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        CHECK_FALSE(stage.start({}, NetworkAddress::loopback(30'001)));
        CHECK(stage.state() == ConnectResponseWaitState::network_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code == ConnectResponseWaitErrorCode::local_endpoint_changed);
        CHECK(transport.send_count == 0U);
    }
}

TEST_CASE("Connect response deadline and clock regression are deterministic",
          "[goldsrc][connect-response][wait]")
{
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ConnectResponseWaitTimePoint{};

    SECTION("deadline is checked before receive")
    {
        FakeDatagramTransport transport;
        transport.queue(endpoint, accepted_packet());
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + test_config().timeout);
        CHECK(stage.state() == ConnectResponseWaitState::timed_out);
        CHECK_FALSE(stage.response());
        CHECK(transport.receive_count == 0U);
        CHECK(transport.incoming.size() == 1U);
        CHECK(transport.send_count == 0U);
    }

    SECTION("time moving backwards is a typed protocol error")
    {
        FakeDatagramTransport transport;
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch + 10ms, transport.local));
        stage.update(epoch + 9ms);
        CHECK(stage.state() == ConnectResponseWaitState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code == ConnectResponseWaitErrorCode::time_moved_backwards);
        CHECK(transport.receive_count == 0U);
        CHECK(transport.send_count == 0U);
    }

    SECTION("a response immediately before the deadline is accepted")
    {
        FakeDatagramTransport transport;
        transport.queue(endpoint, accepted_packet());
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + test_config().timeout - 1ms);
        CHECK(stage.state() == ConnectResponseWaitState::accepted);
        CHECK(transport.receive_count == 1U);
        CHECK(transport.send_count == 0U);
    }
}

TEST_CASE("Would-block is normal and receive polling is bounded per update",
          "[goldsrc][connect-response][wait]")
{
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ConnectResponseWaitTimePoint{};

    SECTION("would block")
    {
        FakeDatagramTransport transport;
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        CHECK(stage.state() == ConnectResponseWaitState::waiting);
        CHECK(transport.receive_count == 1U);
        CHECK(transport.receive_limits == std::vector<std::size_t>{1'024U});
        CHECK(transport.send_count == 0U);
    }

    SECTION("receive budget")
    {
        FakeDatagramTransport transport;
        transport.queue(endpoint, unrelated_connectionless_packet());
        transport.queue(endpoint, unrelated_connectionless_packet());
        transport.queue(endpoint, unrelated_connectionless_packet());
        transport.queue(endpoint, accepted_packet());
        auto config = test_config();
        config.maximum_datagrams_per_update = 2U;
        ConnectResponseWaitStage stage{transport, endpoint, config};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        CHECK(stage.state() == ConnectResponseWaitState::waiting);
        CHECK(transport.receive_count == 2U);
        CHECK(transport.incoming.size() == 2U);
        stage.update(epoch + 2ms);
        CHECK(stage.state() == ConnectResponseWaitState::accepted);
        CHECK(transport.receive_count == 4U);
        CHECK(transport.incoming.empty());
        CHECK(transport.send_count == 0U);
    }
}

TEST_CASE("Connect response wait classifies receive failures and inconsistent metadata",
          "[goldsrc][connect-response][wait]")
{
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ConnectResponseWaitTimePoint{};

    const auto assert_network_error = [&](DatagramTransportReceiveResult result,
                                          const ConnectResponseWaitErrorCode code) {
        FakeDatagramTransport transport;
        transport.incoming.push_back(std::move(result));
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        CHECK(stage.state() == ConnectResponseWaitState::network_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code == code);
        const auto receive_count = transport.receive_count;
        stage.update(epoch + 2ms);
        stage.cancel(epoch + 2ms);
        CHECK_FALSE(stage.start(epoch + 2ms, transport.local));
        CHECK(stage.state() == ConnectResponseWaitState::network_error);
        CHECK(transport.receive_count == receive_count);
        CHECK(transport.send_count == 0U);
    };

    assert_network_error(
        DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::error,
            std::nullopt,
            std::nullopt,
            0U,
            "synthetic receive failure",
        },
        ConnectResponseWaitErrorCode::receive_failed);

    assert_network_error(
        DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::error,
            std::nullopt,
            endpoint,
            0U,
            "synthetic inconsistent receive failure",
        },
        ConnectResponseWaitErrorCode::inconsistent_receive_result);

    assert_network_error(
        DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::received,
            Datagram{endpoint, accepted_packet()},
            std::nullopt,
            accepted_packet().size(),
            {},
        },
        ConnectResponseWaitErrorCode::inconsistent_receive_result);

    assert_network_error(
        DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::received,
            Datagram{endpoint, accepted_packet()},
            NetworkAddress::loopback(27'016),
            accepted_packet().size(),
            {},
        },
        ConnectResponseWaitErrorCode::inconsistent_receive_result);

    assert_network_error(
        DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::received,
            std::nullopt,
            endpoint,
            accepted_packet().size(),
            {},
        },
        ConnectResponseWaitErrorCode::inconsistent_receive_result);

    assert_network_error(
        DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::received,
            Datagram{endpoint, accepted_packet()},
            endpoint,
            accepted_packet().size() + 1U,
            {},
        },
        ConnectResponseWaitErrorCode::inconsistent_receive_result);

    assert_network_error(
        DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::would_block,
            std::nullopt,
            endpoint,
            0U,
            {},
        },
        ConnectResponseWaitErrorCode::inconsistent_receive_result);

    assert_network_error(
        DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::truncated,
            std::nullopt,
            std::nullopt,
            1'025U,
            "synthetic source-less truncation",
        },
        ConnectResponseWaitErrorCode::inconsistent_receive_result);
}

TEST_CASE("Wrong endpoint traffic is ignored before truncation or parsing",
          "[goldsrc][connect-response][wait]")
{
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto spoofed = NetworkAddress::loopback(27'016);
    const auto epoch = ConnectResponseWaitTimePoint{};

    SECTION("valid spoof")
    {
        FakeDatagramTransport transport;
        transport.queue(spoofed, accepted_packet());
        transport.queue(endpoint, accepted_packet());
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        CHECK(stage.state() == ConnectResponseWaitState::accepted);
        CHECK(transport.receive_count == 2U);
        CHECK(transport.send_count == 0U);
    }

    SECTION("truncated spoof")
    {
        FakeDatagramTransport transport;
        transport.queue_truncated(spoofed, 2'000U);
        transport.queue(endpoint, rejected_packet());
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        CHECK(stage.state() == ConnectResponseWaitState::rejected);
        CHECK(transport.receive_count == 2U);
        CHECK(transport.send_count == 0U);
    }
}

TEST_CASE("Exact endpoint ACCEPT and REJECT become distinct terminal outcomes",
          "[goldsrc][connect-response][wait]")
{
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ConnectResponseWaitTimePoint{};

    SECTION("accepted")
    {
        FakeDatagramTransport transport;
        transport.queue(endpoint, unrelated_connectionless_packet());
        transport.queue(endpoint, accepted_packet());
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        CHECK(stage.state() == ConnectResponseWaitState::accepted);
        REQUIRE(stage.response());
        CHECK(std::holds_alternative<ConnectAccepted>(*stage.response()));
        CHECK_FALSE(stage.error());
        CHECK(transport.send_count == 0U);
    }

    SECTION("rejected")
    {
        FakeDatagramTransport transport;
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        REQUIRE(stage.state() == ConnectResponseWaitState::waiting);
        transport.queue(endpoint, rejected_packet());
        stage.update(epoch + 20ms);
        CHECK(stage.state() == ConnectResponseWaitState::rejected);
        REQUIRE(stage.response());
        CHECK(std::holds_alternative<ConnectRejected>(*stage.response()));
        CHECK_FALSE(stage.error());
        const auto receive_count = transport.receive_count;
        stage.update(epoch + 2ms);
        stage.cancel(epoch + 2ms);
        CHECK_FALSE(stage.start(epoch + 2ms, transport.local));
        CHECK(stage.state() == ConnectResponseWaitState::rejected);
        CHECK(transport.receive_count == receive_count);
        CHECK(transport.send_count == 0U);
    }
}

TEST_CASE("Malformed known responses and target truncation are terminal protocol errors",
          "[goldsrc][connect-response][wait]")
{
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ConnectResponseWaitTimePoint{};

    SECTION("shorter than a connectionless header")
    {
        FakeDatagramTransport transport;
        transport.queue(endpoint, std::vector<std::byte>{std::byte{0xff}, std::byte{0xff}});
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        CHECK(stage.state() == ConnectResponseWaitState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              ConnectResponseWaitErrorCode::malformed_connect_response);
        CHECK(stage.error()->protocol_code ==
              hlclient::goldsrc::ConnectResponseErrorCode::packet_too_short);
        CHECK(transport.send_count == 0U);
    }

    SECTION("malformed accept")
    {
        FakeDatagramTransport transport;
        transport.queue(endpoint, connectionless_packet("B"));
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        CHECK(stage.state() == ConnectResponseWaitState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              ConnectResponseWaitErrorCode::malformed_connect_response);
        CHECK(stage.error()->protocol_code.has_value());
        CHECK(transport.send_count == 0U);
    }

    SECTION("malformed reject")
    {
        FakeDatagramTransport transport;
        transport.queue(endpoint, connectionless_packet("9Invalid connection."));
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        CHECK(stage.state() == ConnectResponseWaitState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              ConnectResponseWaitErrorCode::malformed_connect_response);
        CHECK(stage.error()->protocol_code.has_value());
        CHECK(transport.send_count == 0U);
    }

    SECTION("target truncation")
    {
        FakeDatagramTransport transport;
        transport.queue_truncated(endpoint, 1'025U);
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        CHECK(stage.state() == ConnectResponseWaitState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->code == ConnectResponseWaitErrorCode::response_truncated);
        CHECK(transport.send_count == 0U);
    }
}

TEST_CASE("Sequenced traffic is a typed M2.3 boundary and emits no acknowledgement",
          "[goldsrc][connect-response][wait]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    transport.queue(endpoint, std::vector<std::byte>{
                                  std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0},
                                  std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0}});
    ConnectResponseWaitStage stage{transport, endpoint, test_config()};

    REQUIRE(stage.start({}, transport.local));
    stage.update(ConnectResponseWaitTimePoint{} + 1ms);
    CHECK(stage.state() == ConnectResponseWaitState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          ConnectResponseWaitErrorCode::unexpected_sequenced_packet_pending_m2_3);
    const auto receive_count = transport.receive_count;
    stage.update(ConnectResponseWaitTimePoint{} + 2ms);
    stage.cancel(ConnectResponseWaitTimePoint{} + 2ms);
    CHECK_FALSE(stage.start(ConnectResponseWaitTimePoint{} + 2ms, transport.local));
    CHECK(stage.state() == ConnectResponseWaitState::protocol_error);
    CHECK(transport.receive_count == receive_count);
    CHECK(transport.send_count == 0U);
}

TEST_CASE("Cancellation timeout and success are terminal and idempotent",
          "[goldsrc][connect-response][wait]")
{
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ConnectResponseWaitTimePoint{};

    SECTION("cancel")
    {
        FakeDatagramTransport transport;
        transport.queue(endpoint, accepted_packet());
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.cancel(epoch + 1ms);
        stage.cancel(epoch + 2ms);
        stage.update(epoch + 3ms);
        CHECK_FALSE(stage.start(epoch + 4ms, transport.local));
        CHECK(stage.state() == ConnectResponseWaitState::cancelled);
        CHECK(transport.receive_count == 0U);
        CHECK(transport.incoming.size() == 1U);
        CHECK(transport.send_count == 0U);
    }

    SECTION("timeout")
    {
        FakeDatagramTransport transport;
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + test_config().timeout);
        stage.update(epoch + 1s);
        stage.cancel(epoch + 1s);
        CHECK_FALSE(stage.start(epoch + 1s, transport.local));
        CHECK(stage.state() == ConnectResponseWaitState::timed_out);
        CHECK(transport.receive_count == 0U);
        CHECK(transport.send_count == 0U);
    }

    SECTION("accepted")
    {
        FakeDatagramTransport transport;
        transport.queue(endpoint, accepted_packet());
        transport.queue(endpoint, rejected_packet());
        ConnectResponseWaitStage stage{transport, endpoint, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        stage.update(epoch + 2ms);
        stage.cancel(epoch + 2ms);
        CHECK_FALSE(stage.start(epoch + 2ms, transport.local));
        CHECK(stage.state() == ConnectResponseWaitState::accepted);
        CHECK(transport.receive_count == 1U);
        CHECK(transport.incoming.size() == 1U);
        CHECK(transport.send_count == 0U);
    }
}

TEST_CASE("Connect response trace is metadata-only and callbacks cannot reenter the stage",
          "[goldsrc][connect-response][wait]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ConnectResponseWaitTimePoint{};
    transport.queue(endpoint, accepted_packet());

    ConnectResponseWaitStage* stage_pointer = nullptr;
    std::vector<ConnectResponseTraceEvent> events;
    bool reentrant_start = true;
    ConnectResponseWaitStage stage{
        transport,
        endpoint,
        test_config(),
        [&](const ConnectResponseTraceEvent& event) {
            events.push_back(event);
            reentrant_start = stage_pointer->start(epoch, transport.local);
            stage_pointer->update(epoch + 1ms);
            stage_pointer->cancel(epoch + 1ms);
            throw std::runtime_error{"synthetic trace exception"};
        }};
    stage_pointer = &stage;

    bool started = false;
    REQUIRE_NOTHROW(started = stage.start(epoch, transport.local));
    REQUIRE(started);
    CHECK_FALSE(reentrant_start);
    CHECK(stage.state() == ConnectResponseWaitState::waiting);
    REQUIRE_NOTHROW(stage.update(epoch + 1ms));
    CHECK(stage.state() == ConnectResponseWaitState::accepted);
    REQUIRE(events.size() == 2U);
    CHECK(events[0].classification == ConnectResponseTraceClassification::wait_started);
    CHECK(events[0].datagram_size == 0U);
    CHECK(events[1].classification == ConnectResponseTraceClassification::connect_accepted);
    CHECK(events[1].datagram_size == accepted_packet().size());
    CHECK(events[1].endpoint == endpoint);
    CHECK(transport.send_count == 0U);
}

} // namespace
