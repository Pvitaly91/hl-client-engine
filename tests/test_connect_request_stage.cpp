#include <hlclient/goldsrc/connect_request_stage.hpp>

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
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using hlclient::goldsrc::AuthenticationMaterial;
using hlclient::goldsrc::AuthenticationMaterialCreateResult;
using hlclient::goldsrc::ChallengeExchangeConfig;
using hlclient::goldsrc::ChallengeExchangeTimePoint;
using hlclient::goldsrc::ClientConnectionSettings;
using hlclient::goldsrc::ConnectCompatibilityProfile;
using hlclient::goldsrc::ConnectRequestErrorCode;
using hlclient::goldsrc::ConnectRequestStageState;
using hlclient::goldsrc::ConnectRequestTraceEvent;
using hlclient::goldsrc::GoldSrcHandshakeCoordinator;
using hlclient::goldsrc::GoldSrcHandshakeState;
using hlclient::goldsrc::HandshakeStopPoint;
using hlclient::goldsrc::PrepareConnectRequestResult;
using hlclient::network::Datagram;
using hlclient::network::DatagramLocalAddressResult;
using hlclient::network::DatagramSendResult;
using hlclient::network::DatagramSendStatus;
using hlclient::network::DatagramTransportReceiveResult;
using hlclient::network::DatagramTransportReceiveStatus;
using hlclient::network::IDatagramTransport;
using hlclient::network::NetworkAddress;

inline constexpr std::string_view kSyntheticProtectedAuthentication =
    "TEST_AUTH_MATERIAL_TEST_AUTH_MAT";
inline constexpr std::string_view kSyntheticAuthenticationMarker = "TEST_AUTH_MATERIAL";

[[nodiscard]] ConnectCompatibilityProfile synthetic_profile() noexcept
{
    auto profile = ConnectCompatibilityProfile{};
    profile.protected_authentication_is_ascii_hex = false;
    return profile;
}

struct SentDatagram {
    NetworkAddress destination;
    std::vector<std::byte> payload;
};

class FakeDatagramTransport final : public IDatagramTransport {
public:
    [[nodiscard]] DatagramLocalAddressResult local_address() const override
    {
        ++local_address_queries;
        if (!local_error.empty()) {
            return DatagramLocalAddressResult{std::nullopt, local_error};
        }
        if (local_after_first_query && local_address_queries > 1U) {
            return DatagramLocalAddressResult{local_after_first_query, {}};
        }
        return DatagramLocalAddressResult{local, {}};
    }

    [[nodiscard]] DatagramSendResult send_to(
        const NetworkAddress& destination,
        const std::span<const std::byte> payload) override
    {
        sent.push_back(SentDatagram{
            destination,
            std::vector<std::byte>{payload.begin(), payload.end()},
        });
        if (failing_send_call && sent.size() == *failing_send_call) {
            return DatagramSendResult{DatagramSendStatus::error, "synthetic send failure"};
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

    void queue(const NetworkAddress source, std::vector<std::byte> payload)
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

    NetworkAddress local{NetworkAddress::loopback(30'000)};
    std::optional<NetworkAddress> local_after_first_query;
    std::string local_error;
    std::optional<std::size_t> failing_send_call;
    mutable std::size_t local_address_queries{0U};
    std::vector<SentDatagram> sent;
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

[[nodiscard]] std::vector<std::byte> synthetic_binary_authentication(
    const std::size_t size = hlclient::goldsrc::kObservedConnectAuthenticationSuffixSize)
{
    std::vector<std::byte> result(size);
    for (std::size_t index = 0U; index < size; ++index) {
        result[index] =
            static_cast<std::byte>(static_cast<unsigned char>(
                kSyntheticAuthenticationMarker[index % kSyntheticAuthenticationMarker.size()]));
    }
    return result;
}

[[nodiscard]] AuthenticationMaterialCreateResult synthetic_authentication(
    const std::size_t suffix_size =
        hlclient::goldsrc::kObservedConnectAuthenticationSuffixSize)
{
    const auto protected_bytes = ascii_bytes(kSyntheticProtectedAuthentication);
    const auto suffix = synthetic_binary_authentication(suffix_size);
    return AuthenticationMaterial::create(protected_bytes, suffix);
}

[[nodiscard]] PrepareConnectRequestResult prepared_request(
    const ClientConnectionSettings& settings = {},
    const ConnectCompatibilityProfile& profile = synthetic_profile())
{
    auto authentication = synthetic_authentication();
    if (!authentication) {
        return PrepareConnectRequestResult{std::nullopt, std::move(authentication.error)};
    }
    return hlclient::goldsrc::prepare_connect_request(
        settings, std::move(*authentication.value), profile);
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

void deliver_challenge(
    FakeDatagramTransport& transport,
    GoldSrcHandshakeCoordinator& coordinator,
    const NetworkAddress endpoint,
    const std::uint32_t challenge,
    const ChallengeExchangeTimePoint now)
{
    transport.queue(endpoint, live_shape_challenge(challenge));
    coordinator.update(now);
}

[[nodiscard]] bool starts_with(
    const std::span<const std::byte> bytes,
    const std::span<const std::byte> prefix)
{
    return bytes.size() >= prefix.size() &&
           std::ranges::equal(bytes.first(prefix.size()), prefix);
}

TEST_CASE("Connect stage 1: challenge success supplies the token to the request",
          "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    auto prepared = prepared_request();
    REQUIRE(prepared);
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config()};

    REQUIRE(coordinator.start(epoch));
    deliver_challenge(transport, coordinator, endpoint, 123'456'789U, epoch + 1ms);

    REQUIRE(coordinator.state() == GoldSrcHandshakeState::request_sent);
    REQUIRE(transport.sent.size() == 2U);
    const auto parsed = hlclient::goldsrc::parse_connect_request(
        transport.sent[1].payload, synthetic_profile());
    REQUIRE(parsed);
    CHECK(parsed.request->challenge() == 123'456'789U);
}

TEST_CASE("Connect stage 2: builder preserves the exact 32-bit challenge bit pattern",
          "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    auto prepared = prepared_request();
    REQUIRE(prepared);
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config()};

    REQUIRE(coordinator.start(epoch));
    deliver_challenge(
        transport,
        coordinator,
        endpoint,
        UINT32_MAX,
        epoch + 1ms);

    REQUIRE(transport.sent.size() == 2U);
    auto expected_prefix = ascii_bytes("connect 48 -1 ");
    expected_prefix.insert(
        expected_prefix.begin(),
        {std::byte{0xFFU}, std::byte{0xFFU}, std::byte{0xFFU}, std::byte{0xFFU}});
    CHECK(starts_with(transport.sent[1].payload, expected_prefix));
    const auto parsed = hlclient::goldsrc::parse_connect_request(
        transport.sent[1].payload, synthetic_profile());
    REQUIRE(parsed);
    CHECK(parsed.request->challenge() == UINT32_MAX);
}

TEST_CASE("Connect stage 3: challenge and connect use the same transport instance",
          "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    auto prepared = prepared_request();
    REQUIRE(prepared);
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config()};

    REQUIRE(coordinator.start(epoch));
    REQUIRE(transport.sent.size() == 1U);
    const auto challenge_request = hlclient::goldsrc::build_getchallenge_request();
    REQUIRE(challenge_request);
    CHECK(transport.sent.front().payload == *challenge_request.datagram);

    deliver_challenge(transport, coordinator, endpoint, 99U, epoch + 1ms);
    REQUIRE(transport.sent.size() == 2U);
    CHECK(hlclient::goldsrc::parse_connect_request(
        transport.sent.back().payload, synthetic_profile()));
}

TEST_CASE("Connect stage 4: connect requires the challenge local endpoint",
          "[goldsrc][connect-stage]")
{
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};

    SECTION("stable endpoint is retained")
    {
        FakeDatagramTransport transport;
        transport.local = NetworkAddress::loopback(31'337);
        auto prepared = prepared_request();
        REQUIRE(prepared);
        GoldSrcHandshakeCoordinator coordinator{
            transport,
            endpoint,
            HandshakeStopPoint::connect_request,
            std::move(prepared.value),
            test_config()};

        REQUIRE(coordinator.start(epoch));
        deliver_challenge(transport, coordinator, endpoint, 101U, epoch + 1ms);

        CHECK(coordinator.state() == GoldSrcHandshakeState::request_sent);
        REQUIRE(coordinator.local_endpoint());
        CHECK(*coordinator.local_endpoint() == transport.local);
        CHECK(transport.local_address_queries == 2U);
    }

    SECTION("endpoint drift is terminal and suppresses connect")
    {
        FakeDatagramTransport transport;
        transport.local = NetworkAddress::loopback(31'337);
        transport.local_after_first_query = NetworkAddress::loopback(31'338);
        auto prepared = prepared_request();
        REQUIRE(prepared);
        GoldSrcHandshakeCoordinator coordinator{
            transport,
            endpoint,
            HandshakeStopPoint::connect_request,
            std::move(prepared.value),
            test_config()};

        REQUIRE(coordinator.start(epoch));
        deliver_challenge(transport, coordinator, endpoint, 101U, epoch + 1ms);

        CHECK(coordinator.state() == GoldSrcHandshakeState::network_error);
        CHECK(coordinator.terminal());
        CHECK(coordinator.connect_send_attempts() == 0U);
        CHECK(transport.sent.size() == 1U);
    }
}

TEST_CASE("Connect stage 5: connect uses the exact challenge remote endpoint",
          "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'099);
    const auto epoch = ChallengeExchangeTimePoint{};
    auto prepared = prepared_request();
    REQUIRE(prepared);
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config()};

    REQUIRE(coordinator.start(epoch));
    deliver_challenge(transport, coordinator, endpoint, 102U, epoch + 1ms);

    REQUIRE(transport.sent.size() == 2U);
    CHECK(transport.sent[0].destination == endpoint);
    CHECK(transport.sent[1].destination == endpoint);
}

TEST_CASE("Connect stage 6: request is sent exactly once", "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    auto prepared = prepared_request();
    REQUIRE(prepared);
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config()};

    REQUIRE(coordinator.start(epoch));
    deliver_challenge(transport, coordinator, endpoint, 103U, epoch + 1ms);
    REQUIRE(coordinator.state() == GoldSrcHandshakeState::request_sent);

    coordinator.update(epoch + 100ms);
    coordinator.update(epoch + 1s);
    coordinator.cancel(epoch + 2s);
    CHECK_FALSE(coordinator.start(epoch + 3s));
    CHECK(coordinator.connect_send_attempts() == 1U);
    CHECK(transport.sent.size() == 2U);
}

TEST_CASE("Connect stage 7: no connect is sent before challenge success",
          "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    auto prepared = prepared_request();
    REQUIRE(prepared);
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config()};

    REQUIRE(coordinator.start(epoch));
    coordinator.update(epoch + 1ms);
    coordinator.update(epoch + 2ms);

    CHECK(coordinator.state() == GoldSrcHandshakeState::waiting_for_challenge);
    CHECK(coordinator.connect_send_attempts() == 0U);
    REQUIRE(transport.sent.size() == 1U);
    const auto challenge_request = hlclient::goldsrc::build_getchallenge_request();
    REQUIRE(challenge_request);
    CHECK(transport.sent.front().payload == *challenge_request.datagram);
}

TEST_CASE("Connect stage 8: timeout suppresses the connect request",
          "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    auto prepared = prepared_request();
    REQUIRE(prepared);
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config()};

    REQUIRE(coordinator.start(epoch));
    coordinator.update(epoch + test_config().timeout);
    deliver_challenge(transport, coordinator, endpoint, 104U, epoch + 1s);

    CHECK(coordinator.state() == GoldSrcHandshakeState::timed_out);
    CHECK(coordinator.terminal());
    CHECK(coordinator.connect_send_attempts() == 0U);
    CHECK(transport.sent.size() == 1U);
}

TEST_CASE("Connect stage 9: cancellation suppresses the connect request",
          "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    auto prepared = prepared_request();
    REQUIRE(prepared);
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config()};

    REQUIRE(coordinator.start(epoch));
    coordinator.cancel(epoch + 1ms);
    deliver_challenge(transport, coordinator, endpoint, 105U, epoch + 2ms);

    CHECK(coordinator.state() == GoldSrcHandshakeState::cancelled);
    CHECK(coordinator.terminal());
    CHECK(coordinator.connect_send_attempts() == 0U);
    CHECK(transport.sent.size() == 1U);
}

TEST_CASE("Connect stage 10: configuration failure prevents all network sends",
          "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    auto profile = synthetic_profile();
    profile.maximum_datagram_size = 0U;
    auto prepared = prepared_request({}, profile);
    REQUIRE_FALSE(prepared);
    REQUIRE(prepared.error);
    CHECK(prepared.error->code == ConnectRequestErrorCode::invalid_configuration);
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config()};

    CHECK(coordinator.state() == GoldSrcHandshakeState::configuration_error);
    CHECK(coordinator.terminal());
    CHECK_FALSE(coordinator.start(ChallengeExchangeTimePoint{}));
    CHECK(transport.sent.empty());
}

TEST_CASE("Connect stage 11: malformed user info prevents all network sends",
          "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    ClientConnectionSettings settings;
    settings.display_name = "Invalid\\Name";
    auto prepared = prepared_request(settings);
    REQUIRE_FALSE(prepared);
    REQUIRE(prepared.error);
    CHECK(prepared.error->code == ConnectRequestErrorCode::invalid_user_info);
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        NetworkAddress::loopback(27'015),
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config()};

    CHECK_FALSE(coordinator.start(ChallengeExchangeTimePoint{}));
    CHECK(coordinator.state() == GoldSrcHandshakeState::configuration_error);
    CHECK(transport.sent.empty());
}

TEST_CASE("Connect stage 12: oversized authentication prevents all network sends",
          "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    const auto protected_bytes = ascii_bytes(kSyntheticProtectedAuthentication);
    const auto oversized_suffix = synthetic_binary_authentication(
        hlclient::goldsrc::kMaximumConnectAuthenticationSuffixSize + 1U);
    auto authentication = AuthenticationMaterial::create(protected_bytes, oversized_suffix);
    REQUIRE_FALSE(authentication);
    REQUIRE(authentication.error);
    CHECK(authentication.error->code == ConnectRequestErrorCode::authentication_too_large);
    std::optional<hlclient::goldsrc::PreparedConnectRequest> absent;
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        NetworkAddress::loopback(27'015),
        HandshakeStopPoint::connect_request,
        std::move(absent),
        test_config()};

    CHECK_FALSE(coordinator.start(ChallengeExchangeTimePoint{}));
    CHECK(coordinator.state() == GoldSrcHandshakeState::configuration_error);
    CHECK(transport.sent.empty());
}

TEST_CASE("Connect stage 13: connect send failure is terminal network error",
          "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    transport.failing_send_call = 2U;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    auto prepared = prepared_request();
    REQUIRE(prepared);
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config()};

    REQUIRE(coordinator.start(epoch));
    deliver_challenge(transport, coordinator, endpoint, 106U, epoch + 1ms);

    CHECK(coordinator.state() == GoldSrcHandshakeState::network_error);
    CHECK(coordinator.terminal());
    CHECK(coordinator.connect_send_attempts() == 1U);
    CHECK(coordinator.error_context() == "synthetic send failure");
    CHECK(transport.sent.size() == 2U);
}

TEST_CASE("Connect stage 14: every terminal coordinator state is idempotent",
          "[goldsrc][connect-stage]")
{
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};

    SECTION("request sent")
    {
        FakeDatagramTransport transport;
        auto prepared = prepared_request();
        REQUIRE(prepared);
        GoldSrcHandshakeCoordinator coordinator{
            transport,
            endpoint,
            HandshakeStopPoint::connect_request,
            std::move(prepared.value),
            test_config()};
        REQUIRE(coordinator.start(epoch));
        deliver_challenge(transport, coordinator, endpoint, 107U, epoch + 1ms);
        REQUIRE(coordinator.state() == GoldSrcHandshakeState::request_sent);
        const auto send_count = transport.sent.size();
        coordinator.update(epoch + 1s);
        coordinator.cancel(epoch + 2s);
        CHECK_FALSE(coordinator.start(epoch + 3s));
        CHECK(coordinator.state() == GoldSrcHandshakeState::request_sent);
        CHECK(transport.sent.size() == send_count);
    }

    SECTION("timed out")
    {
        FakeDatagramTransport transport;
        auto prepared = prepared_request();
        REQUIRE(prepared);
        GoldSrcHandshakeCoordinator coordinator{
            transport,
            endpoint,
            HandshakeStopPoint::connect_request,
            std::move(prepared.value),
            test_config()};
        REQUIRE(coordinator.start(epoch));
        coordinator.update(epoch + test_config().timeout);
        REQUIRE(coordinator.state() == GoldSrcHandshakeState::timed_out);
        const auto send_count = transport.sent.size();
        coordinator.update(epoch + 1s);
        coordinator.cancel(epoch + 2s);
        CHECK_FALSE(coordinator.start(epoch + 3s));
        CHECK(coordinator.state() == GoldSrcHandshakeState::timed_out);
        CHECK(transport.sent.size() == send_count);
    }

    SECTION("cancelled")
    {
        FakeDatagramTransport transport;
        auto prepared = prepared_request();
        REQUIRE(prepared);
        GoldSrcHandshakeCoordinator coordinator{
            transport,
            endpoint,
            HandshakeStopPoint::connect_request,
            std::move(prepared.value),
            test_config()};
        REQUIRE(coordinator.start(epoch));
        coordinator.cancel(epoch + 1ms);
        REQUIRE(coordinator.state() == GoldSrcHandshakeState::cancelled);
        const auto send_count = transport.sent.size();
        coordinator.cancel(epoch + 2ms);
        coordinator.update(epoch + 1s);
        CHECK_FALSE(coordinator.start(epoch + 2s));
        CHECK(coordinator.state() == GoldSrcHandshakeState::cancelled);
        CHECK(transport.sent.size() == send_count);
    }

    SECTION("configuration error")
    {
        FakeDatagramTransport transport;
        GoldSrcHandshakeCoordinator coordinator{
            transport,
            endpoint,
            HandshakeStopPoint::connect_request,
            std::nullopt,
            test_config()};
        REQUIRE(coordinator.state() == GoldSrcHandshakeState::configuration_error);
        coordinator.update(epoch + 1ms);
        coordinator.cancel(epoch + 2ms);
        CHECK_FALSE(coordinator.start(epoch + 3ms));
        CHECK(coordinator.state() == GoldSrcHandshakeState::configuration_error);
        CHECK(transport.sent.empty());
    }

    SECTION("network error")
    {
        FakeDatagramTransport transport;
        transport.failing_send_call = 2U;
        auto prepared = prepared_request();
        REQUIRE(prepared);
        GoldSrcHandshakeCoordinator coordinator{
            transport,
            endpoint,
            HandshakeStopPoint::connect_request,
            std::move(prepared.value),
            test_config()};
        REQUIRE(coordinator.start(epoch));
        deliver_challenge(transport, coordinator, endpoint, 108U, epoch + 1ms);
        REQUIRE(coordinator.state() == GoldSrcHandshakeState::network_error);
        const auto send_count = transport.sent.size();
        coordinator.update(epoch + 1s);
        coordinator.cancel(epoch + 2s);
        CHECK_FALSE(coordinator.start(epoch + 3s));
        CHECK(coordinator.state() == GoldSrcHandshakeState::network_error);
        CHECK(transport.sent.size() == send_count);
    }

    SECTION("protocol error")
    {
        FakeDatagramTransport transport;
        auto prepared = prepared_request();
        REQUIRE(prepared);
        GoldSrcHandshakeCoordinator coordinator{
            transport,
            endpoint,
            HandshakeStopPoint::connect_request,
            std::move(prepared.value),
            test_config()};
        REQUIRE(coordinator.start(epoch));
        transport.queue(endpoint, ascii_bytes("not a connectionless response"));
        coordinator.update(epoch + 1ms);
        REQUIRE(coordinator.state() == GoldSrcHandshakeState::protocol_error);
        const auto send_count = transport.sent.size();
        coordinator.update(epoch + 1s);
        coordinator.cancel(epoch + 2s);
        CHECK_FALSE(coordinator.start(epoch + 3s));
        CHECK(coordinator.state() == GoldSrcHandshakeState::protocol_error);
        CHECK(transport.sent.size() == send_count);
    }

    SECTION("challenge-only success")
    {
        FakeDatagramTransport transport;
        GoldSrcHandshakeCoordinator coordinator{
            transport,
            endpoint,
            HandshakeStopPoint::challenge,
            std::nullopt,
            test_config()};
        REQUIRE(coordinator.start(epoch));
        deliver_challenge(transport, coordinator, endpoint, 109U, epoch + 1ms);
        REQUIRE(coordinator.state() == GoldSrcHandshakeState::challenge_received);
        const auto send_count = transport.sent.size();
        coordinator.update(epoch + 1s);
        coordinator.cancel(epoch + 2s);
        CHECK_FALSE(coordinator.start(epoch + 3s));
        CHECK(coordinator.state() == GoldSrcHandshakeState::challenge_received);
        CHECK(transport.sent.size() == send_count);
    }
}

TEST_CASE("Connect stage 15: no follow-up netchan or sign-on datagram is emitted",
          "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    auto prepared = prepared_request();
    REQUIRE(prepared);
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config()};

    REQUIRE(coordinator.start(epoch));
    deliver_challenge(transport, coordinator, endpoint, 110U, epoch + 1ms);
    REQUIRE(coordinator.state() == GoldSrcHandshakeState::request_sent);
    transport.queue(endpoint, ascii_bytes("synthetic post-connect server traffic"));

    for (std::uint32_t step = 1U; step <= 8U; ++step) {
        coordinator.update(epoch + std::chrono::milliseconds{100LL * step});
    }

    REQUIRE(transport.sent.size() == 2U);
    CHECK(hlclient::goldsrc::parse_connect_request(
        transport.sent.back().payload, synthetic_profile()));
    CHECK(transport.incoming.size() == 1U);
}

TEST_CASE("Connect stage 16: M1 stop-after-challenge remains exact and sends no connect",
          "[goldsrc][connect-stage]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::challenge,
        std::nullopt,
        test_config()};

    REQUIRE(coordinator.start(epoch));
    REQUIRE(transport.sent.size() == 1U);
    const auto expected = hlclient::goldsrc::build_getchallenge_request();
    REQUIRE(expected);
    CHECK(transport.sent.front().payload == *expected.datagram);

    deliver_challenge(transport, coordinator, endpoint, 111U, epoch + 1ms);
    REQUIRE(coordinator.state() == GoldSrcHandshakeState::challenge_received);
    REQUIRE(coordinator.challenge());
    CHECK(coordinator.challenge()->challenge == 111U);
    CHECK(coordinator.connect_send_attempts() == 0U);
    CHECK(transport.sent.size() == 1U);
}

TEST_CASE("Connect trace is metadata-only and cannot duplicate sends through throw or reentry",
          "[goldsrc][connect-stage][trace]")
{
    FakeDatagramTransport transport;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    auto prepared = prepared_request();
    REQUIRE(prepared);
    GoldSrcHandshakeCoordinator* coordinator_pointer = nullptr;
    std::vector<ConnectRequestTraceEvent> events;
    bool reentrant_start_result = true;
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config(),
        {},
        [&](const ConnectRequestTraceEvent& event) {
            events.push_back(event);
            reentrant_start_result = coordinator_pointer->start(epoch);
            coordinator_pointer->update(epoch + 1ms);
            coordinator_pointer->cancel(epoch + 1ms);
            if (event.state == ConnectRequestStageState::sending_request) {
                throw std::runtime_error{"synthetic trace callback failure"};
            }
        }};
    coordinator_pointer = &coordinator;

    REQUIRE(coordinator.start(epoch));
    REQUIRE_NOTHROW(deliver_challenge(
        transport, coordinator, endpoint, 112U, epoch + 1ms));

    CHECK_FALSE(reentrant_start_result);
    CHECK(coordinator.state() == GoldSrcHandshakeState::request_sent);
    CHECK(coordinator.connect_send_attempts() == 1U);
    CHECK(transport.sent.size() == 2U);
    const std::vector expected_states{
        ConnectRequestStageState::building_request,
        ConnectRequestStageState::request_ready,
        ConnectRequestStageState::sending_request,
        ConnectRequestStageState::request_sent,
    };
    REQUIRE(events.size() == expected_states.size());
    for (std::size_t index = 0U; index < events.size(); ++index) {
        CHECK(events[index].state == expected_states[index]);
        CHECK(events[index].context.find(kSyntheticProtectedAuthentication) == std::string::npos);
        CHECK(events[index].context.find(kSyntheticAuthenticationMarker) == std::string::npos);
    }
    CHECK(events.back().authentication_size ==
          kSyntheticProtectedAuthentication.size() +
              hlclient::goldsrc::kObservedConnectAuthenticationSuffixSize);
    CHECK(events.back().protocol_info_size ==
          hlclient::goldsrc::kObservedConnectProtocolInfoSize);
}

TEST_CASE("Connect stage diagnostics never contain synthetic authentication values",
          "[goldsrc][connect-stage][redaction]")
{
    FakeDatagramTransport transport;
    transport.failing_send_call = 2U;
    const auto endpoint = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    auto prepared = prepared_request();
    REQUIRE(prepared);
    std::vector<ConnectRequestTraceEvent> events;
    GoldSrcHandshakeCoordinator coordinator{
        transport,
        endpoint,
        HandshakeStopPoint::connect_request,
        std::move(prepared.value),
        test_config(),
        {},
        [&events](const ConnectRequestTraceEvent& event) { events.push_back(event); }};

    REQUIRE(coordinator.start(epoch));
    deliver_challenge(transport, coordinator, endpoint, 113U, epoch + 1ms);

    REQUIRE(coordinator.state() == GoldSrcHandshakeState::network_error);
    CHECK(coordinator.error_context().find(kSyntheticProtectedAuthentication) ==
          std::string_view::npos);
    CHECK(coordinator.error_context().find(kSyntheticAuthenticationMarker) ==
          std::string_view::npos);
    CHECK(std::ranges::none_of(events, [](const ConnectRequestTraceEvent& event) {
        return event.context.find(kSyntheticProtectedAuthentication) != std::string::npos ||
               event.context.find(kSyntheticAuthenticationMarker) != std::string::npos;
    }));
}

} // namespace
