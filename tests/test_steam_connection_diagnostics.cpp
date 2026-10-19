#include <hlclient/app/steam_connection_diagnostics.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <span>
#include <utility>

namespace {
namespace app = hlclient::app;
namespace goldsrc = hlclient::goldsrc;
using Status = app::SteamAuthenticationTraceStatus;
using Response = goldsrc::ConnectResponseTraceClassification;

const app::SessionReceiptIdentity identity{
    "0123456789abcdef0123456789abcdef", app::SessionReceiptRole::A, 45U, 9876U};

void contains(const std::string& text, const std::string_view fragment)
{
    CAPTURE(fragment);
    CHECK(text.find(fragment) != std::string::npos);
}

std::size_t occurrences(const std::string_view text, const std::string_view fragment)
{
    std::size_t count = 0U;
    for (std::size_t position = 0U;
         (position = text.find(fragment, position)) != std::string_view::npos;
         position += fragment.size()) {
        ++count;
    }
    return count;
}

app::SteamAuthenticationTraceEvent acquired(const std::size_t bytes = 160U)
{
    app::SteamAuthenticationTraceEvent event;
    event.status = Status::material_acquired;
    event.material_size = bytes;
    event.request_context.emplace();
    event.request_context->remote_endpoint = hlclient::network::NetworkAddress::loopback(27015U);
    return event;
}

goldsrc::ConnectRequestTraceEvent sent(const std::size_t bytes = 160U)
{
    goldsrc::ConnectRequestTraceEvent event;
    event.state = goldsrc::ConnectRequestStageState::request_sent;
    event.endpoint = hlclient::network::NetworkAddress::loopback(27015U);
    event.datagram_size = 500U;
    event.protocol_info_size = 70U;
    event.user_info_size = 230U;
    // The existing total includes the textual cdkey. Comparisons must use the
    // separately measured binary ticket length, not this larger legacy value.
    event.authentication_size = bytes + 32U;
    event.authentication_suffix_size = bytes;
    return event;
}
} // namespace

TEST_CASE("Steam attempt diagnostics preserve unknown versus observed false and zero",
          "[steam-diagnostics][diagnostics][no-stock]")
{
    app::SteamConnectionDiagnostics diagnostics;
    CHECK_FALSE(diagnostics.observed());
    const auto empty = diagnostics.encode(identity);
    for (const auto field : {"actual_app_id", "logged_on", "request_context",
             "authentication_endpoint", "connect_endpoint", "endpoint_matches_authentication",
             "initiate_returned_length", "material_bytes", "connect_material_bytes",
             "material_size_matches_connect", "authentication_header_has_lf", "response_ms"}) {
        contains(empty, std::string{"\""} + field + "\":null");
    }

    auto event = acquired(0U);
    event.actual_app_id = 0U;
    event.logged_on = false;
    event.initiate_returned_length = 0;
    event.request_context->secure = false;
    event.request_context->server_id_universe = 0U;
    event.request_context->server_id_account_type = 0U;
    event.request_context->server_id_instance = 0U;
    diagnostics.observe(event, 0U);
    auto connect = sent(0U);
    connect.authentication_line_delimiter = false;
    diagnostics.observe(connect, 0U);
    CHECK(diagnostics.observed());
    const auto text = diagnostics.encode(identity);
    for (const auto fragment : {"\"actual_app_id\":0", "\"logged_on\":false",
             "\"secure\":false", "\"server_id_universe\":0", "\"server_id_account_type\":0",
             "\"server_id_instance\":0", "\"server_id_present\":false", "\"server_id_nonzero\":false",
             "\"initiate_returned_length\":0", "\"material_bytes\":0", "\"connect_material_bytes\":0",
             "\"material_size_matches_connect\":true", "\"connect_sent_ms\":0",
             "\"authentication_header_has_lf\":false"}) {
        contains(text, fragment);
    }
}

TEST_CASE("Steam attempt diagnostics compare exact endpoint and binary material lengths",
          "[steam-diagnostics][diagnostics][no-stock]")
{
    app::SteamConnectionDiagnostics diagnostics;
    diagnostics.observe(acquired(), 12U);
    contains(diagnostics.encode(identity), "\"material_size_matches_connect\":null");
    auto connect = sent();
    diagnostics.observe(connect, 20U);
    auto text = diagnostics.encode(identity);
    contains(text, "\"endpoint_matches_authentication\":true");
    contains(text, "\"material_size_matches_connect\":true");
    contains(text, "\"connect_material_bytes\":160");
    contains(text, "\"connect_datagram_bytes\":500");

    connect.endpoint = hlclient::network::NetworkAddress::loopback(27016U);
    connect.authentication_suffix_size = 159U;
    diagnostics.observe(connect, 30U);
    text = diagnostics.encode(identity);
    contains(text, "\"endpoint_matches_authentication\":false");
    contains(text, "\"material_size_matches_connect\":false");
    contains(text, "\"connect_endpoint\":\"127.0.0.1:27016\"");

    connect.authentication_suffix_size.reset();
    diagnostics.observe(connect, 40U);
    text = diagnostics.encode(identity);
    contains(text, "\"connect_material_bytes\":null");
    contains(text, "\"material_size_matches_connect\":null");
}

TEST_CASE("Steam attempt diagnostics retain separator metadata without binary ticket contents",
          "[steam-diagnostics][diagnostics][no-stock]")
{
    for (const auto state : {std::optional<bool>{}, std::optional<bool>{false}, std::optional<bool>{true}}) {
        app::SteamConnectionDiagnostics diagnostics;
        auto connect = sent();
        connect.authentication_line_delimiter = state;
        diagnostics.observe(connect, 50U);
        const auto expected = !state ? "null" : *state ? "true" : "false";
        contains(diagnostics.encode(identity), std::string{"\"authentication_header_has_lf\":"} + expected);
    }
}

TEST_CASE("Steam attempt diagnostics never copy challenge previews or connect text",
          "[steam-diagnostics][diagnostics][no-stock]")
{
    app::SteamConnectionDiagnostics diagnostics;
    goldsrc::ChallengeTraceEvent challenge;
    challenge.classification = goldsrc::ChallengeTraceClassification::request_sent;
    challenge.escaped_preview = "SECRET_CHALLENGE_PREVIEW";
    challenge.context = "SECRET_CHALLENGE_CONTEXT";
    diagnostics.observe(challenge, 1U);
    challenge.classification = goldsrc::ChallengeTraceClassification::challenge_accepted;
    challenge.datagram_size = 51U;
    diagnostics.observe(challenge, 2U);
    auto connect = sent();
    connect.challenge = 4179912345U;
    connect.protocol_info_field_names = {"SECRET_PROTOCOL_FIELD", "\\cdkey\\SECRET_AUTH_VALUE"};
    connect.user_info_field_names = {"SECRET_USER_FIELD", "\\password\\SECRET_PASSWORD"};
    connect.context = "SECRET_CONNECT_CONTEXT";
    diagnostics.observe(connect, 3U);
    const auto text = diagnostics.encode(identity);
    CHECK(text.find("SECRET") == std::string::npos);
    CHECK(text.find("4179912345") == std::string::npos);
    CHECK(text.find("cdkey") == std::string::npos);
    CHECK(text.find("password") == std::string::npos);
    contains(text, "\"challenge_requests\":1");
    contains(text, "\"challenge_datagram_bytes\":51");
    contains(text, "\"challenge_accepted_ms\":2");
    contains(text, "\"protocol_info_bytes\":70");
    contains(text, "\"user_info_bytes\":230");
}

TEST_CASE("Steam rejection diagnostics classify only bounded exact known messages",
          "[steam-diagnostics][diagnostics][no-stock]")
{
    for (const auto known : {"STEAM validation rejected", " \tSteam Validation Rejected.\r\n"}) {
        app::SteamConnectionDiagnostics diagnostics;
        diagnostics.rejection(known);
        contains(diagnostics.encode(identity), "\"rejection_detail\":\"steam_validation_rejected\"");
    }
    for (const auto& unknown : std::array<std::string, 6>{"SECRET_SERVER_MESSAGE",
             "STEAM validation rejected SECRET", "SECRET STEAM validation rejected",
             "STEAM validation rejected..", "", std::string(513U, 'x') + "SECRET"}) {
        app::SteamConnectionDiagnostics diagnostics;
        diagnostics.rejection("STEAM validation rejected");
        diagnostics.rejection(unknown);
        const auto text = diagnostics.encode(identity);
        contains(text, "\"rejection_detail\":\"unclassified\"");
        CHECK(text.find("SECRET") == std::string::npos);
        CHECK(text.find(std::string(513U, 'x')) == std::string::npos);
    }
}

TEST_CASE("Steam diagnostic event cap retains terminal teardown metadata",
          "[steam-diagnostics][diagnostics][no-stock]")
{
    app::SteamConnectionDiagnostics diagnostics;
    auto pending = acquired();
    pending.status = Status::material_pending;
    pending.phase = app::SteamAuthenticationPhase::material_acquisition;
    for (std::size_t i = 0U; i < app::SteamConnectionDiagnostics::maximum_events + 8U; ++i) {
        pending.callback_pump_count = i;
        pending.elapsed = std::chrono::milliseconds{static_cast<std::int64_t>(i)};
        diagnostics.observe(pending, static_cast<std::uint64_t>(i));
    }
    goldsrc::ConnectResponseTraceEvent rejected;
    rejected.classification = Response::connect_rejected;
    rejected.elapsed = std::chrono::milliseconds{75};
    rejected.datagram_size = 37U;
    diagnostics.observe(rejected, 90U);
    diagnostics.rejection("STEAM validation rejected");
    app::SteamAuthenticationTraceEvent terminal;
    terminal.status = Status::session_terminated;
    diagnostics.observe(terminal, 91U);
    terminal.status = Status::api_shutdown;
    diagnostics.observe(terminal, 92U);
    const auto text = diagnostics.encode(identity);
    CHECK(occurrences(text, "\"status\":") == app::SteamConnectionDiagnostics::maximum_events);
    CHECK(text.size() < 8192U);
    contains(text, "\"events_omitted\":11");
    contains(text, "\"response\":\"rejected\"");
    contains(text, "\"response_ms\":90");
    contains(text, "\"response_wait_ms\":75");
    contains(text, "\"response_datagram_bytes\":37");
    contains(text, "\"session_terminated_ms\":91");
    contains(text, "\"api_shutdown_ms\":92");
    contains(text, "\"callback_pump_count_during_acquisition\":39");
    contains(text, "\"rejection_detail\":\"steam_validation_rejected\"");
}

TEST_CASE("Steam response acceptance is not promoted to verified authentication",
          "[steam-diagnostics][diagnostics][no-stock]")
{
    app::SteamConnectionDiagnostics diagnostics;
    app::SteamAuthenticationTraceEvent initialized;
    initialized.status = Status::api_initialized;
    initialized.actual_app_id = app::kHalfLifeSteamAppId;
    initialized.logged_on = true;
    initialized.phase = app::SteamAuthenticationPhase::initialization;
    initialized.elapsed = std::chrono::milliseconds{123};
    diagnostics.observe(initialized, 130U);
    diagnostics.observe(acquired(), 150U);
    diagnostics.observe(sent(), 160U);
    goldsrc::ConnectResponseTraceEvent response;
    response.classification = Response::wrong_endpoint_ignored;
    diagnostics.observe(response, 161U);
    response.classification = Response::unrelated_connectionless_ignored;
    diagnostics.observe(response, 162U);
    response.classification = Response::connect_accepted;
    diagnostics.observe(response, 170U);
    const auto text = diagnostics.encode(identity);
    contains(text, "\"steam_api_initialized\":true");
    contains(text, "\"response\":\"accepted\"");
    contains(text, "\"rejection_detail\":\"not_observed\"");
    contains(text, "\"wrong_endpoint_responses\":1");
    contains(text, "\"unrelated_responses\":1");
    contains(text, "\"observed_ms\":130,\"phase\":\"initialization\",\"phase_elapsed_ms\":123");
    CHECK(text.find("verified") == std::string::npos);
    CHECK(text.find("validated") == std::string::npos);
    CHECK(occurrences(text, "\"status\":") == 4U);

    const app::SteamConnectionDiagnostics next_attempt;
    CHECK_FALSE(next_attempt.observed());
    const auto fresh = next_attempt.encode(identity);
    contains(fresh, "\"steam_api_initialized\":false");
    contains(fresh, "\"response\":\"not_observed\"");
    contains(fresh, "\"actual_app_id\":null");
    contains(fresh, "\"session_terminated_ms\":null");
    contains(fresh, "\"events\":[]");
    contains(fresh, "\"events_omitted\":0");
}

TEST_CASE("Steam serializer preserves the exact binary ticket after one header LF",
          "[steam-diagnostics][steam-wire-boundary][diagnostics][no-stock]")
{
    // This independent receiver model consumes one connectionless text line,
    // stopping after NUL or LF. Binary 0xff is an unsigned byte, not EOF.
    // It intentionally does not call our connect-request parser.
    const auto server_ticket = [](const std::span<const std::byte> packet) {
        std::size_t cursor = 4U;
        while (cursor < packet.size()) {
            const auto value = packet[cursor++];
            if (value == std::byte{0U} || value == std::byte{10U}) break;
        }
        return packet.subspan(cursor);
    };
    constexpr std::array<std::array<unsigned char, 4>, 6> tickets{{
        {{0U, 65U, 66U, 67U}}, {{65U, 66U, 0U, 67U}},
        {{10U, 65U, 66U, 67U}}, {{255U, 65U, 0U, 67U}},
        {{10U, 10U, 13U, 0U}}, {{32U, 13U, 10U, 255U}},
    }};
    constexpr std::string_view synthetic_cdkey = "0123456789abcdef0123456789abcdef";
    std::size_t variant = 0U;
    for (const auto& ticket : tickets) {
        const auto ticket_bytes = std::as_bytes(std::span{ticket});
        auto material = goldsrc::AuthenticationMaterial::create(
            std::as_bytes(std::span{synthetic_cdkey.data(), synthetic_cdkey.size()}), ticket_bytes);
        REQUIRE(material);
        REQUIRE(material.value);
        const auto profile = goldsrc::steam_legacy_connect_profile();
        auto prepared = goldsrc::prepare_connect_request(
            goldsrc::ClientConnectionSettings{}, std::move(*material.value), profile);
        REQUIRE(prepared);
        REQUIRE(prepared.value);
        const auto request = std::move(*prepared.value).make_request(123U);
        const auto built = goldsrc::ConnectRequestBuilder::build(request, profile);
        REQUIRE(built);
        REQUIRE(built.datagram);
        const auto& packet = *built.datagram;
        REQUIRE(packet.size() > ticket_bytes.size() + 4U);
        const auto text_size = packet.size() - ticket_bytes.size();
        REQUIRE(packet[text_size - 2U] == std::byte{'"'});
        REQUIRE(packet[text_size - 1U] == std::byte{10U});
        CHECK(std::ranges::count(std::span<const std::byte>{packet}.first(text_size), std::byte{10U}) == 1);
        CHECK(std::ranges::count(std::span<const std::byte>{packet}.first(text_size), std::byte{0U}) == 0);
        const auto visible_ticket = server_ticket(packet);
        INFO("synthetic_variant=" << variant++
             << " server_ticket_bytes=" << visible_ticket.size()
             << " sent_ticket_bytes=" << ticket_bytes.size());
        REQUIRE(std::ranges::equal(visible_ticket, ticket_bytes));
        REQUIRE(visible_ticket.size() == ticket_bytes.size());
        CHECK(packet.back() == ticket_bytes.back());

        // Parser consumption must be exactly one LF, never trim a leading
        // NUL, LF, CR, space or 0xff from opaque authentication material.
        const auto parsed = goldsrc::parse_connect_request(packet, profile);
        REQUIRE(parsed);
        REQUIRE(parsed.request);
        CHECK(parsed.request->authentication_matches(
            std::as_bytes(std::span{synthetic_cdkey.data(), synthetic_cdkey.size()}), ticket_bytes));
        const auto rebuilt = goldsrc::ConnectRequestBuilder::build(*parsed.request, profile);
        REQUIRE(rebuilt);
        REQUIRE(rebuilt.datagram);
        CHECK(*rebuilt.datagram == packet);
        const auto repeated = goldsrc::ConnectRequestBuilder::build(request, profile);
        REQUIRE(repeated);
        REQUIRE(repeated.datagram);
        CHECK(*repeated.datagram == packet);

        // Retain a negative control: the pre-fix wire shape loses ticket data
        // when a GoldSrc-compatible line reader consumes the header.
        auto legacy = packet;
        legacy.erase(legacy.begin() + static_cast<std::ptrdiff_t>(text_size - 1U));
        CHECK_FALSE(std::ranges::equal(server_ticket(legacy), ticket_bytes));
    }
}
