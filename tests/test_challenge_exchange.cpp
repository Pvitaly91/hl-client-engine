#include <hlclient/goldsrc/challenge_exchange.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using hlclient::goldsrc::ChallengeExchange;
using hlclient::goldsrc::ChallengeExchangeConfig;
using hlclient::goldsrc::ChallengeExchangeErrorCategory;
using hlclient::goldsrc::ChallengeExchangeState;
using hlclient::goldsrc::ChallengeExchangeTimePoint;
using hlclient::goldsrc::ChallengeTraceEvent;
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
        if (!local_error.empty()) {
            return DatagramLocalAddressResult{std::nullopt, local_error};
        }
        return DatagramLocalAddressResult{local, {}};
    }

    [[nodiscard]] DatagramSendResult send_to(
        const NetworkAddress& destination,
        const std::span<const std::byte> payload) override
    {
        destinations.push_back(destination);
        sent.emplace_back(payload.begin(), payload.end());
        if (!send_error.empty()) {
            return DatagramSendResult{DatagramSendStatus::error, send_error};
        }
        return DatagramSendResult{DatagramSendStatus::sent, {}};
    }

    [[nodiscard]] DatagramTransportReceiveResult receive(
        const std::size_t maximum_size) override
    {
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

    void queue(NetworkAddress source, std::vector<std::byte> payload)
    {
        const auto payload_size = payload.size();
        incoming.push_back(DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::received,
            Datagram{source, std::move(payload)},
            source,
            payload_size,
            {},
        });
    }

    void queue_truncated(NetworkAddress source, const std::size_t lower_bound)
    {
        incoming.push_back(DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::truncated,
            std::nullopt,
            source,
            lower_bound,
            "synthetic truncated response",
        });
    }

    void queue_error(NetworkAddress source)
    {
        incoming.push_back(DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::error,
            std::nullopt,
            source,
            0U,
            "synthetic receive failure",
        });
    }

    void queue_source_less_truncated(const std::size_t lower_bound)
    {
        incoming.push_back(DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::truncated,
            std::nullopt,
            std::nullopt,
            lower_bound,
            "synthetic source-less truncation",
        });
    }

    NetworkAddress local{NetworkAddress::loopback(30'000)};
    std::string local_error;
    std::string send_error;
    std::vector<NetworkAddress> destinations;
    std::vector<std::vector<std::byte>> sent;
    std::vector<std::size_t> receive_limits;
    std::deque<DatagramTransportReceiveResult> incoming;
};

[[nodiscard]] std::vector<std::byte> ascii_bytes(const std::string_view text)
{
    std::vector<std::byte> result;
    result.reserve(text.size());
    std::ranges::transform(text, std::back_inserter(result), [](const char value) {
        return static_cast<std::byte>(static_cast<unsigned char>(value));
    });
    return result;
}

[[nodiscard]] std::vector<std::byte> live_shape_challenge(const std::uint32_t challenge)
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "A00000000 ";
    packet += std::to_string(challenge);
    packet += " 3 72057594037927936 0\n";
    packet.push_back('\0');
    return ascii_bytes(packet);
}

[[nodiscard]] ChallengeExchangeConfig test_config()
{
    ChallengeExchangeConfig config;
    config.retry_interval = 100ms;
    config.timeout = 350ms;
    config.maximum_attempts = 3U;
    config.maximum_datagrams_per_update = 2U;
    config.maximum_datagram_size = 1'024U;
    return config;
}

TEST_CASE("Challenge exchange validates configuration before sending", "[goldsrc][challenge]")
{
    SECTION("zero receive budget")
    {
        FakeDatagramTransport transport;
        auto config = test_config();
        config.maximum_datagrams_per_update = 0U;
        ChallengeExchange exchange{transport, NetworkAddress::loopback(27'015), config};

        CHECK_FALSE(exchange.start(ChallengeExchangeTimePoint{}));
        CHECK(exchange.state() == ChallengeExchangeState::protocol_error);
        REQUIRE(exchange.error());
        CHECK(exchange.error()->category == ChallengeExchangeErrorCategory::invalid_configuration);
        CHECK(exchange.attempts() == 0U);
        CHECK(transport.sent.empty());
    }

    SECTION("unbounded values and unsafe deadline")
    {
        auto assert_invalid = [](ChallengeExchangeConfig config,
                                 const ChallengeExchangeTimePoint now = {}) {
            FakeDatagramTransport transport;
            ChallengeExchange exchange{transport, NetworkAddress::loopback(27'015), config};
            CHECK_FALSE(exchange.start(now));
            CHECK(exchange.state() == ChallengeExchangeState::protocol_error);
            CHECK(transport.sent.empty());
        };

        auto config = test_config();
        config.retry_interval = hlclient::goldsrc::kMaximumChallengeRetryInterval + 1ms;
        assert_invalid(config);
        config = test_config();
        config.timeout = hlclient::goldsrc::kMaximumChallengeTimeout + 1ms;
        assert_invalid(config);
        config = test_config();
        config.maximum_attempts = hlclient::goldsrc::kMaximumChallengeAttempts + 1U;
        assert_invalid(config);
        config = test_config();
        config.maximum_datagrams_per_update =
            hlclient::goldsrc::kMaximumChallengeDatagramsPerUpdate + 1U;
        assert_invalid(config);
        config = test_config();
        config.maximum_datagram_size =
            hlclient::goldsrc::kMaximumConnectionlessChallengeDatagramSize + 1U;
        assert_invalid(config);
        config = test_config();
        assert_invalid(config, ChallengeExchangeTimePoint::max() - 1ms);

        FakeDatagramTransport transport;
        ChallengeExchange unspecified_remote{
            transport, NetworkAddress{0U, 27'015}, test_config()};
        CHECK_FALSE(unspecified_remote.start(ChallengeExchangeTimePoint{}));
        CHECK(transport.sent.empty());
    }

    SECTION("unbound local endpoint")
    {
        FakeDatagramTransport transport;
        transport.local = NetworkAddress::loopback(0);
        ChallengeExchange exchange{
            transport, NetworkAddress::loopback(27'015), test_config()};
        CHECK_FALSE(exchange.start(ChallengeExchangeTimePoint{}));
        CHECK(exchange.state() == ChallengeExchangeState::network_error);
        CHECK(transport.sent.empty());
    }
}

TEST_CASE("Challenge exchange retries and times out using injected time", "[goldsrc][challenge]")
{
    FakeDatagramTransport transport;
    const auto epoch = ChallengeExchangeTimePoint{};
    ChallengeExchange exchange{transport, NetworkAddress::loopback(27'015), test_config()};

    REQUIRE(exchange.start(epoch));
    CHECK(exchange.state() == ChallengeExchangeState::waiting_for_response);
    CHECK(exchange.attempts() == 1U);
    REQUIRE(transport.sent.size() == 1U);
    REQUIRE(transport.destinations.size() == 1U);
    CHECK(transport.destinations.front() == NetworkAddress::loopback(27'015));
    const auto expected_request = hlclient::goldsrc::build_getchallenge_request();
    REQUIRE(expected_request);
    CHECK(transport.sent.front() == *expected_request.datagram);
    const auto first_retry = exchange.next_retry();
    REQUIRE(first_retry);
    exchange.update(epoch + 99ms);
    CHECK(exchange.state() == ChallengeExchangeState::waiting_for_response);
    CHECK(exchange.attempts() == 1U);
    CHECK(exchange.next_retry() == first_retry);
    CHECK(transport.sent.size() == 1U);
    exchange.update(epoch + 100ms);
    CHECK(exchange.attempts() == 2U);
    exchange.update(epoch + 200ms);
    CHECK(exchange.attempts() == 3U);
    exchange.update(epoch + 350ms);
    CHECK(exchange.state() == ChallengeExchangeState::timed_out);
    CHECK(exchange.attempts() == 3U);
    CHECK(transport.sent.size() == 3U);
    CHECK_FALSE(exchange.challenge());
    CHECK_FALSE(exchange.error());

    exchange.update(epoch + 1s);
    exchange.cancel(epoch + 1s);
    CHECK_FALSE(exchange.start(epoch + 1s));
    CHECK(exchange.state() == ChallengeExchangeState::timed_out);
    CHECK(transport.sent.size() == 3U);
}

TEST_CASE("Challenge exchange never accepts a response at its deadline", "[goldsrc][challenge]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    transport.queue(endpoint, live_shape_challenge(123));
    const auto epoch = ChallengeExchangeTimePoint{};
    ChallengeExchange exchange{transport, endpoint, test_config()};

    REQUIRE(exchange.start(epoch));
    exchange.update(epoch + test_config().timeout);
    CHECK(exchange.state() == ChallengeExchangeState::timed_out);
    CHECK_FALSE(exchange.challenge());
    CHECK(transport.incoming.size() == 1U);
}

TEST_CASE("Challenge exchange ignores wrong endpoints within a bounded receive update", "[goldsrc][challenge]")
{
    FakeDatagramTransport transport;
    const auto expected = NetworkAddress::loopback(27'015);
    transport.queue(NetworkAddress::loopback(27'016), live_shape_challenge(111));
    transport.queue(NetworkAddress::loopback(27'017), live_shape_challenge(222));
    transport.queue(expected, live_shape_challenge(333));
    std::vector<ChallengeTraceEvent> events;
    auto config = test_config();
    config.maximum_datagrams_per_update = 2U;
    ChallengeExchange exchange{
        transport,
        expected,
        config,
        [&events](const ChallengeTraceEvent& event) { events.push_back(event); }};

    const auto epoch = ChallengeExchangeTimePoint{};
    REQUIRE(exchange.start(epoch));
    exchange.update(epoch + 1ms);
    CHECK(exchange.state() == ChallengeExchangeState::waiting_for_response);
    CHECK(transport.incoming.size() == 1U);
    CHECK(transport.receive_limits == std::vector<std::size_t>{1'024U, 1'024U});
    exchange.update(epoch + 2ms);
    CHECK(exchange.state() == ChallengeExchangeState::challenge_received);
    REQUIRE(exchange.challenge());
    CHECK(exchange.challenge()->challenge == 333);
    CHECK(std::ranges::count_if(events, [](const ChallengeTraceEvent& event) {
              return event.classification ==
                     hlclient::goldsrc::ChallengeTraceClassification::wrong_endpoint_ignored;
          }) == 2);
}

TEST_CASE("Challenge exchange classifies exact-endpoint protocol and network failures", "[goldsrc][challenge]")
{
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};

    SECTION("truncated response")
    {
        FakeDatagramTransport transport;
        transport.queue_truncated(endpoint, 1'025U);
        ChallengeExchange exchange{transport, endpoint, test_config()};
        REQUIRE(exchange.start(epoch));
        exchange.update(epoch + 1ms);
        CHECK(exchange.state() == ChallengeExchangeState::protocol_error);
        REQUIRE(exchange.error());
        CHECK(exchange.error()->category == ChallengeExchangeErrorCategory::protocol);
        REQUIRE(exchange.error()->protocol_code);
        CHECK(*exchange.error()->protocol_code ==
              hlclient::goldsrc::ChallengeProtocolErrorCode::payload_too_large);
    }

    SECTION("truncated response from a wrong endpoint is ignored")
    {
        FakeDatagramTransport transport;
        transport.queue_truncated(NetworkAddress::loopback(27'016), 1'025U);
        transport.queue(endpoint, live_shape_challenge(777));
        ChallengeExchange exchange{transport, endpoint, test_config()};
        REQUIRE(exchange.start(epoch));
        exchange.update(epoch + 1ms);
        CHECK(exchange.state() == ChallengeExchangeState::challenge_received);
        REQUIRE(exchange.challenge());
        CHECK(exchange.challenge()->challenge == 777);
    }

    SECTION("source-less truncation is a transport failure")
    {
        FakeDatagramTransport transport;
        transport.queue_source_less_truncated(1'025U);
        ChallengeExchange exchange{transport, endpoint, test_config()};
        REQUIRE(exchange.start(epoch));
        exchange.update(epoch + 1ms);
        CHECK(exchange.state() == ChallengeExchangeState::network_error);
        REQUIRE(exchange.error());
        CHECK(exchange.error()->category == ChallengeExchangeErrorCategory::network);
    }

    SECTION("malformed response")
    {
        FakeDatagramTransport transport;
        transport.queue(endpoint, ascii_bytes("not connectionless"));
        ChallengeExchange exchange{transport, endpoint, test_config()};
        REQUIRE(exchange.start(epoch));
        exchange.update(epoch + 1ms);
        CHECK(exchange.state() == ChallengeExchangeState::protocol_error);
        REQUIRE(exchange.error());
        CHECK(exchange.error()->category == ChallengeExchangeErrorCategory::protocol);
        CHECK(exchange.error()->protocol_code.has_value());
    }

    SECTION("send failure")
    {
        FakeDatagramTransport transport;
        transport.send_error = "synthetic send failure";
        ChallengeExchange exchange{transport, endpoint, test_config()};
        CHECK_FALSE(exchange.start(epoch));
        CHECK(exchange.state() == ChallengeExchangeState::network_error);
        REQUIRE(exchange.error());
        CHECK(exchange.error()->category == ChallengeExchangeErrorCategory::network);
    }

    SECTION("receive failure")
    {
        FakeDatagramTransport transport;
        transport.queue_error(endpoint);
        ChallengeExchange exchange{transport, endpoint, test_config()};
        REQUIRE(exchange.start(epoch));
        exchange.update(epoch + 1ms);
        CHECK(exchange.state() == ChallengeExchangeState::network_error);
        REQUIRE(exchange.error());
        CHECK(exchange.error()->category == ChallengeExchangeErrorCategory::network);
        CHECK(exchange.error()->context == "synthetic receive failure");
    }
}

TEST_CASE("Successful challenge exchange is terminal and ignores duplicate responses",
          "[goldsrc][challenge]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    transport.queue(endpoint, live_shape_challenge(111));
    transport.queue(endpoint, live_shape_challenge(222));
    const auto epoch = ChallengeExchangeTimePoint{};
    ChallengeExchange exchange{transport, endpoint, test_config()};

    REQUIRE(exchange.start(epoch));
    exchange.update(epoch + 1ms);
    REQUIRE(exchange.state() == ChallengeExchangeState::challenge_received);
    REQUIRE(exchange.challenge());
    CHECK(exchange.challenge()->challenge == 111);
    CHECK(transport.incoming.size() == 1U);

    exchange.update(epoch + 1s);
    exchange.cancel(epoch + 1s);
    CHECK_FALSE(exchange.start(epoch + 1s));
    CHECK(exchange.state() == ChallengeExchangeState::challenge_received);
    REQUIRE(exchange.challenge());
    CHECK(exchange.challenge()->challenge == 111);
    CHECK(exchange.attempts() == 1U);
    CHECK(transport.sent.size() == 1U);
    CHECK(transport.incoming.size() == 1U);
}

TEST_CASE("Challenge exchange cancellation is terminal and idempotent", "[goldsrc][challenge]")
{
    FakeDatagramTransport transport;
    const auto epoch = ChallengeExchangeTimePoint{};
    ChallengeExchange exchange{transport, NetworkAddress::loopback(27'015), test_config()};
    REQUIRE(exchange.start(epoch));

    exchange.cancel(epoch + 1ms);
    exchange.cancel(epoch + 2ms);
    exchange.update(epoch + 1s);
    CHECK(exchange.state() == ChallengeExchangeState::cancelled);
    CHECK(exchange.attempts() == 1U);
    CHECK(transport.sent.size() == 1U);
}

TEST_CASE("Challenge trace callbacks cannot reenter or corrupt the exchange",
          "[goldsrc][challenge]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    ChallengeExchange* exchange_pointer = nullptr;
    bool reentrant_start_result = true;
    std::size_t callback_count = 0U;
    ChallengeExchange exchange{
        transport,
        endpoint,
        test_config(),
        [&](const ChallengeTraceEvent& event) {
            ++callback_count;
            if (event.classification ==
                hlclient::goldsrc::ChallengeTraceClassification::exchange_started) {
                reentrant_start_result = exchange_pointer->start(epoch);
                exchange_pointer->update(epoch);
                exchange_pointer->cancel(epoch);
            }
            if (event.classification ==
                hlclient::goldsrc::ChallengeTraceClassification::request_send_started) {
                exchange_pointer->cancel(epoch);
                throw std::runtime_error{"synthetic trace failure"};
            }
        }};
    exchange_pointer = &exchange;

    bool started = false;
    REQUIRE_NOTHROW(started = exchange.start(epoch));
    REQUIRE(started);
    CHECK_FALSE(reentrant_start_result);
    CHECK(callback_count >= 3U);
    CHECK(exchange.state() == ChallengeExchangeState::waiting_for_response);
    CHECK(exchange.attempts() == 1U);
    CHECK(transport.sent.size() == 1U);
}

TEST_CASE("Challenge trace preview is escaped and bounded", "[goldsrc][challenge]")
{
    std::vector<std::byte> bytes(200U, std::byte{'A'});
    bytes[0] = std::byte{0};
    bytes[1] = std::byte{'\\'};
    const auto preview = hlclient::goldsrc::escape_challenge_datagram_preview(bytes);

    CHECK(preview.starts_with("\\x00\\\\"));
    CHECK(preview.ends_with("..."));
    CHECK(preview.size() <= hlclient::goldsrc::kChallengeTracePreviewByteLimit * 4U + 3U);
}

} // namespace
