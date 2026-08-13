#include <hlclient/auth/authentication_provider.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
using hlclient::goldsrc::ChallengeExchangeTimePoint;
using hlclient::goldsrc::GoldSrcHandshakeCoordinator;
using hlclient::goldsrc::GoldSrcHandshakeState;
using hlclient::network::Datagram;
using hlclient::network::DatagramLocalAddressResult;
using hlclient::network::DatagramSendResult;
using hlclient::network::DatagramSendStatus;
using hlclient::network::DatagramTransportReceiveResult;
using hlclient::network::DatagramTransportReceiveStatus;
using hlclient::network::IDatagramTransport;
using hlclient::network::NetworkAddress;

inline constexpr std::string_view kSyntheticAuthenticationMarker = "TEST_AUTH_MATERIAL";
inline constexpr std::string_view kSyntheticProtectedAuthentication =
    "TEST_AUTH_MATERIAL_TEST_AUTH_MAT";

struct SentDatagram {
    NetworkAddress destination;
    std::vector<std::byte> payload;
};

class FakeTransport final : public IDatagramTransport {
public:
    [[nodiscard]] DatagramLocalAddressResult local_address() const override
    {
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

    [[nodiscard]] DatagramTransportReceiveResult receive(std::size_t) override
    {
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

    void queue_receive_error()
    {
        incoming.push_back(DatagramTransportReceiveResult{
            DatagramTransportReceiveStatus::error,
            std::nullopt,
            std::nullopt,
            0U,
            "synthetic receive failure",
        });
    }

    NetworkAddress local{NetworkAddress::loopback(30'000)};
    std::optional<std::size_t> failing_send_call;
    std::vector<SentDatagram> sent;
    std::deque<DatagramTransportReceiveResult> incoming;
};

class CountingLifetime final : public hlclient::auth::IAuthenticationSessionLifetime {
public:
    explicit CountingLifetime(std::size_t& releases) noexcept : releases_{releases} {}
    ~CountingLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text)
{
    const auto view = std::as_bytes(std::span{text.data(), text.size()});
    return std::vector<std::byte>{view.begin(), view.end()};
}

[[nodiscard]] std::vector<std::byte> challenge_response(const std::uint32_t challenge)
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "A00000000 " + std::to_string(challenge) +
              " 3 72057594037927936 0\n";
    packet.push_back('\0');
    return bytes(packet);
}

[[nodiscard]] std::vector<std::byte> accepted_response()
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "B 1 \"127.0.0.1:30000\" 0 10210";
    packet.push_back('\0');
    return bytes(packet);
}

[[nodiscard]] std::vector<std::byte> rejected_response()
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "9Invalid connection.\n";
    packet.push_back('\0');
    return bytes(packet);
}

struct PreparedWithSession {
    hlclient::goldsrc::PreparedConnectRequest request;
    hlclient::auth::AuthenticationSession session;
};

[[nodiscard]] PreparedWithSession prepare_with_session(std::size_t& releases)
{
    std::vector<std::byte> suffix(
        hlclient::goldsrc::kObservedConnectAuthenticationSuffixSize);
    const auto marker = bytes(kSyntheticAuthenticationMarker);
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        suffix[index] = marker[index % marker.size()];
    }
    auto material = hlclient::goldsrc::AuthenticationMaterial::create(
        bytes(kSyntheticProtectedAuthentication),
        suffix);
    REQUIRE(material);

    hlclient::auth::AuthenticationSession session{
        std::move(*material.value),
        std::make_unique<CountingLifetime>(releases)};
    auto transferred = session.take_material();
    REQUIRE(transferred);
    auto profile = hlclient::goldsrc::ConnectCompatibilityProfile{};
    profile.protected_authentication_is_ascii_hex = false;
    auto prepared = hlclient::goldsrc::prepare_connect_request(
        {},
        std::move(*transferred),
        profile);
    REQUIRE(prepared);
    return PreparedWithSession{std::move(*prepared.value), std::move(session)};
}

[[nodiscard]] hlclient::goldsrc::ChallengeExchangeConfig challenge_config()
{
    hlclient::goldsrc::ChallengeExchangeConfig config;
    config.retry_interval = 100ms;
    config.timeout = 350ms;
    config.maximum_attempts = 2U;
    config.maximum_datagrams_per_update = 4U;
    return config;
}

[[nodiscard]] hlclient::goldsrc::ConnectResponseWaitConfig response_config()
{
    hlclient::goldsrc::ConnectResponseWaitConfig config;
    config.timeout = 50ms;
    config.maximum_datagrams_per_update = 4U;
    return config;
}

[[nodiscard]] GoldSrcHandshakeCoordinator coordinator(
    FakeTransport& transport,
    const NetworkAddress remote,
    PreparedWithSession prepared)
{
    return GoldSrcHandshakeCoordinator{
        transport,
        remote,
        hlclient::goldsrc::HandshakeStopPoint::connect_response,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        response_config(),
        {},
        std::move(prepared.session)};
}

void reach_response_wait(
    FakeTransport& transport,
    GoldSrcHandshakeCoordinator& handshake,
    const NetworkAddress remote,
    const ChallengeExchangeTimePoint epoch)
{
    REQUIRE(handshake.start(epoch));
    REQUIRE(transport.sent.size() == 1U);
    transport.queue(remote, challenge_response(123'456U));
    handshake.update(epoch + 1ms);
    REQUIRE(handshake.state() == GoldSrcHandshakeState::waiting_for_connect_response);
    REQUIRE(transport.sent.size() == 2U);
}

TEST_CASE("M2.2 coordinator distinguishes immediate accept and reject without further sends",
          "[goldsrc][connect-response][coordinator]")
{
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};

    SECTION("accept")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        reach_response_wait(transport, handshake, remote, epoch);
        CHECK(releases == 0U);

        transport.queue(remote, accepted_response());
        handshake.update(epoch + 2ms);
        REQUIRE(handshake.state() == GoldSrcHandshakeState::accepted);
        REQUIRE(handshake.connect_response());
        CHECK(std::holds_alternative<hlclient::goldsrc::ConnectAccepted>(
            *handshake.connect_response()));
        CHECK(releases == 1U);
        const auto send_count = transport.sent.size();
        handshake.update(epoch + 1s);
        handshake.cancel(epoch + 2s);
        CHECK(transport.sent.size() == send_count);
        CHECK(releases == 1U);
    }

    SECTION("reject")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        reach_response_wait(transport, handshake, remote, epoch);
        CHECK(releases == 0U);

        transport.queue(remote, rejected_response());
        handshake.update(epoch + 2ms);
        REQUIRE(handshake.state() == GoldSrcHandshakeState::rejected);
        REQUIRE(handshake.connect_response());
        const auto& rejected = std::get<hlclient::goldsrc::ConnectRejected>(
            *handshake.connect_response());
        CHECK(rejected.message == "Invalid connection.");
        CHECK(releases == 1U);
        CHECK(transport.sent.size() == 2U);
    }
}

TEST_CASE("M2.2 response timeout and cancellation release authentication exactly once",
          "[goldsrc][connect-response][auth][lifetime]")
{
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};

    SECTION("timeout")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        reach_response_wait(transport, handshake, remote, epoch);
        CHECK(releases == 0U);
        handshake.update(epoch + 51ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::connect_response_timed_out);
        CHECK(releases == 1U);
        CHECK(transport.sent.size() == 2U);
    }

    SECTION("cancel")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        reach_response_wait(transport, handshake, remote, epoch);
        CHECK(releases == 0U);
        handshake.cancel(epoch + 2ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::cancelled);
        CHECK(releases == 1U);
        handshake.cancel(epoch + 3ms);
        CHECK(releases == 1U);
        CHECK(transport.sent.size() == 2U);
    }
}

TEST_CASE("M2.2 protocol failure releases authentication and never exposes its marker",
          "[goldsrc][connect-response][auth][redaction]")
{
    FakeTransport transport;
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    std::size_t releases = 0U;
    auto handshake = coordinator(transport, remote, prepare_with_session(releases));
    reach_response_wait(transport, handshake, remote, epoch);

    transport.queue(remote, bytes("sequenced TEST_AUTH_MATERIAL"));
    handshake.update(epoch + 2ms);
    CHECK(handshake.state() == GoldSrcHandshakeState::protocol_error);
    CHECK(releases == 1U);
    CHECK(handshake.error_context().find(kSyntheticAuthenticationMarker) ==
          std::string_view::npos);
    CHECK(transport.sent.size() == 2U);
}

TEST_CASE("M2.2 response trace contains metadata only",
          "[goldsrc][connect-response][trace][redaction]")
{
    FakeTransport transport;
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    std::size_t releases = 0U;
    auto prepared = prepare_with_session(releases);
    std::vector<hlclient::goldsrc::ConnectResponseTraceEvent> events;
    GoldSrcHandshakeCoordinator handshake{
        transport,
        remote,
        hlclient::goldsrc::HandshakeStopPoint::connect_response,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        response_config(),
        [&events](const hlclient::goldsrc::ConnectResponseTraceEvent& event) {
            events.push_back(event);
        },
        std::move(prepared.session)};

    reach_response_wait(transport, handshake, remote, epoch);
    transport.queue(remote, rejected_response());
    handshake.update(epoch + 2ms);
    REQUIRE(handshake.state() == GoldSrcHandshakeState::rejected);
    REQUIRE_FALSE(events.empty());
    CHECK(events.back().classification ==
          hlclient::goldsrc::ConnectResponseTraceClassification::connect_rejected);
    CHECK(events.back().endpoint == remote);
    CHECK(events.back().datagram_size == rejected_response().size());
    CHECK(releases == 1U);
}

TEST_CASE("M2.2 authentication lifetime closes on every failure phase",
          "[goldsrc][connect-response][auth][lifetime]")
{
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};

    SECTION("challenge timeout")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        REQUIRE(handshake.start(epoch));
        CHECK(releases == 0U);
        handshake.update(epoch + challenge_config().timeout);
        CHECK(handshake.state() == GoldSrcHandshakeState::timed_out);
        CHECK(releases == 1U);
    }

    SECTION("connect send failure")
    {
        FakeTransport transport;
        transport.failing_send_call = 2U;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        REQUIRE(handshake.start(epoch));
        transport.queue(remote, challenge_response(42U));
        handshake.update(epoch + 1ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::network_error);
        CHECK(releases == 1U);
        CHECK(transport.sent.size() == 2U);
    }

    SECTION("response receive failure")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        reach_response_wait(transport, handshake, remote, epoch);
        transport.queue_receive_error();
        handshake.update(epoch + 2ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::network_error);
        CHECK(releases == 1U);
        CHECK(handshake.error_context().find(kSyntheticAuthenticationMarker) ==
              std::string_view::npos);
    }

    SECTION("invalid response-wait configuration")
    {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto prepared = prepare_with_session(releases);
        auto invalid_response_config = response_config();
        invalid_response_config.timeout = 0ms;
        GoldSrcHandshakeCoordinator handshake{
            transport,
            remote,
            hlclient::goldsrc::HandshakeStopPoint::connect_response,
            std::move(prepared.request),
            challenge_config(),
            {},
            {},
            invalid_response_config,
            {},
            std::move(prepared.session)};
        REQUIRE(handshake.start(epoch));
        transport.queue(remote, challenge_response(43U));
        handshake.update(epoch + 1ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::configuration_error);
        CHECK(releases == 1U);
        CHECK(transport.sent.size() == 2U);
    }
}

} // namespace
