#include <hlclient/auth/authentication_provider.hpp>
#include <hlclient/app/external_session.hpp>
#include <hlclient/app/external_diagnostics.hpp>
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

class ObservingUnavailableProvider final : public hlclient::auth::IAuthenticationProvider {
public:
    hlclient::auth::AuthenticationBeginResult begin(
        const hlclient::auth::AuthenticationRequestContext& context) override {
        ++begins;
        observed = context;
        return hlclient::auth::AuthenticationBeginResult::failed({
            hlclient::auth::AuthenticationErrorCode::unavailable,
            "Project-owned provider deliberately stops before authentication"});
    }
    std::size_t begins{};
    std::optional<hlclient::auth::AuthenticationRequestContext> observed;
};

TEST_CASE("E31 suffixed challenge preserves deferred provider identity and explicit security",
          "[e31][goldsrc][coordinator][auth]")
{
    for (const bool secure : {false, true}) {
        FakeTransport transport;
        ObservingUnavailableProvider provider;
        const auto remote = NetworkAddress::loopback(27'015);
        const auto epoch = ChallengeExchangeTimePoint{};
        GoldSrcHandshakeCoordinator handshake{
            transport, remote, hlclient::goldsrc::HandshakeStopPoint::connect_response,
            std::nullopt, challenge_config(), {}, {}, response_config(), {}, std::nullopt,
            {}, {}, // netchan config/trace
            {}, {}, // initial signon config/trace
            {}, {}, // pre-resource signon config/trace
            {}, {}, // delta config/trace
            {}, {}, // movement environment config/trace
            {}, {}, // userinfo config/trace
            {}, {}, // resource transition config/trace
            {}, {}, // resource list config/trace
            {}, nullptr, {}, // resource response config/consistency provider/trace
            {}, // local resource environment
            {}, {}, // precache manifest config/trace
            nullptr, {}, {}, // importer registries/dispatch config/trace
            {}, {}, // world texture config/trace
            {}, {}, // world render config/trace
            {}, {}, // post-resource entity config/trace
            &provider};
        REQUIRE(handshake.start(epoch));
        CHECK(provider.begins == 0U);
        REQUIRE(transport.sent.size() == 1U);
        std::string response{"\xFF\xFF\xFF\xFF", 4U};
        response += "A00000000 123456789 3 72057594037927936m ";
        response += secure ? "1\n" : "0\n";
        response.push_back('\0');
        transport.queue(remote, bytes(response));
        handshake.update(epoch + 1ms);
        REQUIRE(provider.observed);
        CHECK(provider.begins == 1U);
        CHECK(provider.observed->remote_endpoint == remote);
        CHECK(provider.observed->challenge == 123'456'789U);
        CHECK(provider.observed->game_server_steam_id == 72'057'594'037'927'936ULL);
        REQUIRE(provider.observed->game_server_secure.has_value());
        CHECK(*provider.observed->game_server_secure == secure);
        CHECK(handshake.state() == GoldSrcHandshakeState::authentication_failed);
        // Understanding the wire suffix cannot bypass a failing provider.
        CHECK(transport.sent.size() == 1U);
        transport.queue(remote, bytes(response));
        handshake.update(epoch + 2ms);
        CHECK(provider.begins == 1U);
        CHECK(transport.sent.size() == 1U);
    }
}

TEST_CASE("E31 suffixed challenge sends one prepared connect and accepts the normal response",
          "[e31][goldsrc][coordinator]")
{
    for (const bool secure : {false, true}) {
        FakeTransport transport;
        std::size_t releases{};
        const auto remote = NetworkAddress::loopback(27'015);
        const auto epoch = ChallengeExchangeTimePoint{};
        {
            auto handshake = coordinator(transport, remote, prepare_with_session(releases));
            REQUIRE(handshake.start(epoch));
            std::string response{"\xFF\xFF\xFF\xFF", 4U};
            response += "A00000000 123456789 3 72057594037927936m ";
            response += secure ? "1\n" : "0\n";
            response.push_back('\0');
            transport.queue(remote, bytes(response));
            handshake.update(epoch + 1ms);
            REQUIRE(handshake.state() == GoldSrcHandshakeState::waiting_for_connect_response);
            REQUIRE(transport.sent.size() == 2U);
            CHECK(releases == 0U);
            transport.queue(remote, accepted_response());
            handshake.update(epoch + 2ms);
            CHECK(handshake.state() == GoldSrcHandshakeState::accepted);
            handshake.update(epoch + 3ms);
            CHECK(transport.sent.size() == 2U);
            CHECK(releases == 1U);
        }
        CHECK(releases == 1U);
    }
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

TEST_CASE("Application preparation must precede the challenge deadline",
          "[goldsrc][coordinator][startup]")
{
    const auto remote = NetworkAddress::loopback(27'015);
    const auto epoch = ChallengeExchangeTimePoint{};
    const auto prepared_at = epoch + 8s;
    SECTION("old constructor start expires before the first RX pump") {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        REQUIRE(handshake.start(epoch));
        transport.queue(remote, challenge_response(123'456U));
        handshake.update(prepared_at);
        CHECK(handshake.state() == GoldSrcHandshakeState::timed_out);
        CHECK(transport.incoming.size() == 1U);
        CHECK(releases == 1U);
    }
    SECTION("first prepared update starts the clock; repeated updates do not restart it") {
        FakeTransport transport;
        std::size_t releases = 0U;
        auto handshake = coordinator(transport, remote, prepare_with_session(releases));
        CHECK(handshake.state() == GoldSrcHandshakeState::idle);
        CHECK(transport.sent.empty());
        REQUIRE(handshake.start(prepared_at));
        handshake.update(prepared_at);
        CHECK_FALSE(handshake.terminal());
        CHECK(transport.sent.size() == 1U);
        transport.queue(remote, challenge_response(123'456U));
        handshake.update(prepared_at + 1ms);
        REQUIRE(handshake.state() == GoldSrcHandshakeState::waiting_for_connect_response);
        transport.queue(remote, accepted_response());
        handshake.update(prepared_at + 2ms);
        CHECK(handshake.state() == GoldSrcHandshakeState::accepted);
        CHECK(transport.sent.size() == 2U);
        CHECK(releases == 1U);
    }
}

TEST_CASE("E31 selected LAN endpoint uses the same fake transport through challenge and connect",
          "[e31][coordinator][external-session][no-stock]")
{
    const auto remote=NetworkAddress::parse("192.168.0.102:27016");REQUIRE(remote);
    FakeTransport transport;transport.local=NetworkAddress{0,49152};
    hlclient::app::EndpointBoundTransport bound{transport,*remote};
    std::size_t releases{};auto prepared=prepare_with_session(releases);
    GoldSrcHandshakeCoordinator handshake{bound,*remote,
        hlclient::goldsrc::HandshakeStopPoint::connect_response,std::move(prepared.request),
        challenge_config(),{},{},response_config(),{},std::move(prepared.session)};
    const auto epoch=ChallengeExchangeTimePoint{};
    REQUIRE(handshake.start(epoch));REQUIRE(transport.sent.size()==1);
    // Wrong host cannot supply even a syntactically correct challenge.
    transport.queue(NetworkAddress{0xc0a80067U,27016},challenge_response(123456));
    handshake.update(epoch+1ms);CHECK(transport.sent.size()==1);
    transport.queue(*remote,challenge_response(123456));
    handshake.update(epoch+2ms);
    REQUIRE(handshake.state()==GoldSrcHandshakeState::waiting_for_connect_response);
    REQUIRE(transport.sent.size()==2);
    for(const auto& datagram:transport.sent)CHECK(datagram.destination==*remote);
    CHECK(bound.local_address().address==transport.local);CHECK(releases==0);
    // Server-owned text is not a capability to redirect the selected socket.
    const auto redirect=bytes(std::string{"\xFF\xFF\xFF\xFF",4}+"L192.168.0.103:27016\n");
    transport.queue(*remote,redirect);handshake.update(epoch+3ms);
    for(const auto& datagram:transport.sent)CHECK(datagram.destination==*remote);
    handshake.cancel(epoch+4ms);CHECK(releases==1);
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

TEST_CASE("E31 early challenge failures retain typed durable diagnostics without packet values",
          "[e31][goldsrc][challenge][external-session][diagnostics][no-stock]")
{
    struct Case {
        std::string fields;
        std::string_view code,field;
        std::size_t offset;
        bool valid_terminator{true};
    };
    // Project-owned malformed packets, NOT captures from the user's server.
    const std::vector<Case> cases{
        {"X_UNTRUSTED_PRIVATE_RESPONSE","unexpected_response_type","response_type",4},
        {"A00000000 -7654321 3 72057594037927936 0","invalid_challenge","challenge_token",14},
        {"A00000000 7654321 2 72057594037927936 0","unsupported_variant","authentication_profile",22},
        {"A00000000 7654321 3 0 0","unsupported_variant","server_identity",24},
        {"A00000000 7654321 3 72057594037927936 9","unsupported_variant","security_flag",42},
        {"A00000000 7654321 3 72057594037927936 0","invalid_terminator","terminator",43,false},
    };
    const auto remote=*NetworkAddress::parse("192.168.0.104:27016");
    const auto epoch=ChallengeExchangeTimePoint{};
    for(const auto& test:cases) {
        INFO(test.field);
        FakeTransport transport;
        std::size_t releases{};
        auto handshake=coordinator(transport,remote,prepare_with_session(releases));
        REQUIRE(handshake.start(epoch));
        std::string packet{"\xFF\xFF\xFF\xFF",4};
        packet+=test.fields;packet+=test.valid_terminator?'\n':'\r';packet.push_back('\0');
        transport.queue(remote,bytes(packet));
        handshake.update(epoch+1ms);
        REQUIRE(handshake.state()==GoldSrcHandshakeState::protocol_error);
        CHECK(transport.sent.size()==1U); // getchallenge only; no connect/auth bytes
        CHECK(handshake.connect_send_attempts()==0U);CHECK(releases==1U);
        const auto diagnostic=handshake.early_failure_diagnostic();
        REQUIRE(diagnostic);
        CHECK(diagnostic->phase=="challenge");CHECK(diagnostic->category=="protocol");
        CHECK(diagnostic->code==test.code);CHECK(diagnostic->field==test.field);
        CHECK(diagnostic->byte_offset==test.offset);
        hlclient::app::ExternalSessionDiagnostics report;
        report.early_failure(diagnostic->phase,diagnostic->category,diagnostic->code,
            diagnostic->byte_offset,diagnostic->field);
        const auto text=report.encode({"0123456789abcdef0123456789abcdef",hlclient::app::SessionReceiptRole::A,45,9876});
        CHECK(text.find("\"reason\":\"challenge_protocol_error\"")!=text.npos);
        CHECK(text.find("\"failure_phase\":\"challenge\"")!=text.npos);
        CHECK(text.find("\"protocol_code\":\""+std::string{test.code}+"\"")!=text.npos);
        CHECK(text.find("\"protocol_byte_offset\":"+std::to_string(test.offset))!=text.npos);
        CHECK(text.find("\"protocol_field\":\""+std::string{test.field}+"\"")!=text.npos);
        for(const auto private_value:{"7654321","72057594037927936","UNTRUSTED_PRIVATE", "TEST_AUTH_MATERIAL"})
            CHECK(text.find(private_value)==text.npos);
        handshake.cancel(epoch+2ms);
        REQUIRE(handshake.early_failure_diagnostic());
        CHECK(handshake.early_failure_diagnostic()->code==test.code);
    }
}

TEST_CASE("E31 early connect response failure is not reported as challenge rejection",
          "[e31][goldsrc][connect-response][external-session][diagnostics][no-stock]")
{
    const auto remote=*NetworkAddress::parse("192.168.0.104:27016");
    const auto epoch=ChallengeExchangeTimePoint{};
    FakeTransport transport;
    std::size_t releases{};
    auto handshake=coordinator(transport,remote,prepare_with_session(releases));
    reach_response_wait(transport,handshake,remote,epoch);
    std::string packet{"\xFF\xFF\xFF\xFF",4};packet+="B invalid";packet.push_back('\0');
    transport.queue(remote,bytes(packet));handshake.update(epoch+2ms);
    REQUIRE(handshake.state()==GoldSrcHandshakeState::protocol_error);
    const auto diagnostic=handshake.early_failure_diagnostic();REQUIRE(diagnostic);
    CHECK(diagnostic->phase=="connect_response");CHECK(diagnostic->category=="protocol");
    CHECK(diagnostic->code=="invalid_user_id");CHECK(diagnostic->byte_offset==6U);
    CHECK(diagnostic->field=="not_observed");CHECK(releases==1U);
}

} // namespace
