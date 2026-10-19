#include <hlclient/app/external_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <deque>
#include <vector>

namespace {
namespace app=hlclient::app;
namespace core=hlclient::core;
namespace net=hlclient::network;
const std::vector<std::string_view> launch_arguments{
    "--external-config","D:/fixture/0123456789abcdef0123456789abcdef/launch-config.json",
    "--session-receipt-root","D:/fixture/0123456789abcdef0123456789abcdef",
    "--session-run-id","0123456789abcdef0123456789abcdef","--session-role","A"};
struct Transport final:net::IDatagramTransport {
    net::NetworkAddress local{0,49152};
    std::deque<net::DatagramTransportReceiveResult> incoming;
    std::vector<net::NetworkAddress> sent;
    net::DatagramLocalAddressResult local_address()const override{return {local,{}};}
    net::DatagramSendResult send_to(const net::NetworkAddress& destination,std::span<const std::byte>)override {
        sent.push_back(destination);return {net::DatagramSendStatus::sent,{}};
    }
    net::DatagramTransportReceiveResult receive(std::size_t)override {
        if(incoming.empty())return {};
        auto result=std::move(incoming.front());incoming.pop_front();return result;
    }
};
}

TEST_CASE("E31 external typed config maps one normal unlimited client without research overrides",
    "[e31][external-session][no-stock]") {
    auto parsed=core::parse_command_line(launch_arguments);REQUIRE(parsed);
    REQUIRE(parsed.options->external_server_client);
    core::ExternalLaunchConfiguration config;
    config.name="LAN_Player";config.volume=71;config.movement_diagnostics=true;
    config.video={1,core::VideoWindowMode::borderless,1920,1080,false};
    config.video.msaa_samples=8;config.hud_scale={false,.75F};
    config.image={0.1F,1.2F,1.3F};
    const auto encoded=core::encode_external_launch_configuration(config);REQUIRE(encoded);
    const auto decoded=core::decode_external_launch_configuration(*encoded);REQUIRE(decoded);
    REQUIRE(app::apply_external_launch_configuration(*decoded.configuration,*parsed.options));
    const auto& options=*parsed.options;
    CHECK(options.connect_endpoint=="192.168.0.104:27016");
    CHECK(options.player_name=="LAN_Player");CHECK(options.base_directory==config.asset_root);
    CHECK(options.game_directory=="valve");CHECK(options.steam_api_runtime==config.steam_api_runtime);
    CHECK(options.authentication_provider==core::AuthenticationProviderKind::steam);
    CHECK_FALSE(options.authentication_material_file);CHECK_FALSE(options.resource_consistency_provider);
    CHECK(options.stop_after==core::ConnectionStopPoint::live_visual_control);
    CHECK(options.live_input==core::LiveInputMode::keyboard_mouse);CHECK(options.reference_prediction);
    CHECK(options.live_session_unlimited);CHECK_FALSE(options.live_session_seconds);
    CHECK(options.video==config.video);CHECK(options.image==config.image);CHECK(options.audio_volume==71);
    CHECK(options.hud_scale==config.hud_scale);
    CHECK(options.movement_diagnostics);
    CHECK(options.renderer==core::RendererBackend::opengl);
    auto invalid=config;invalid.endpoint="127.0.0.1:27015";
    CHECK_FALSE(app::apply_external_launch_configuration(invalid,*parsed.options));
    CHECK(parsed.options->connect_endpoint==config.endpoint);
    for(const auto flag:{"--connect","--basedir","--live-input","--renderer","--resource-consistency-provider",
        "--runtime-replay-fixture","--auth-provider","--name","--audio-volume","--session-role"}) {
        auto arguments=launch_arguments;arguments.insert(arguments.end(),{flag,"fixture"});
        CHECK_FALSE(core::parse_command_line(arguments));
    }
    for(std::size_t pair=0;pair<launch_arguments.size();pair+=2) {
        auto arguments=launch_arguments;arguments.erase(arguments.begin()+pair,arguments.begin()+pair+2);
        CHECK_FALSE(core::parse_command_line(arguments));
    }
    auto arguments=launch_arguments;arguments.insert(arguments.end(),{"--external-cancel-event","1234"});
    REQUIRE(core::parse_command_line(arguments));
    arguments.back()="0";CHECK_FALSE(core::parse_command_line(arguments));
    arguments.back()="-1";CHECK_FALSE(core::parse_command_line(arguments));
}

TEST_CASE("E31 endpoint capability retains one ephemeral transport and refuses foreign endpoints",
    "[e31][external-session][transport][no-stock]") {
    const auto selected=net::NetworkAddress::parse("192.168.0.102:27016");REQUIRE(selected);
    CHECK(app::client_bind_endpoint(*selected)==net::NetworkAddress{0,0});
    CHECK(app::client_bind_endpoint(net::NetworkAddress::loopback(27243))==net::NetworkAddress::loopback(0));
    Transport actual;app::EndpointBoundTransport bound{actual,*selected};
    REQUIRE(bound.local_address());CHECK(*bound.local_address().address==actual.local);
    const std::array payload{std::byte{1}};
    for(unsigned phase=0;phase<5;++phase)REQUIRE(bound.send_to(*selected,payload));
    CHECK(actual.sent.size()==5);
    const net::NetworkAddress foreign{0xc0a80067U,27016};
    CHECK_FALSE(bound.send_to(foreign,payload));CHECK(actual.sent.size()==5);
    actual.incoming.push_back({net::DatagramTransportReceiveStatus::received,
        net::Datagram{foreign,{std::byte{2}}},foreign,1,{}});
    CHECK(bound.receive(1400).status==net::DatagramTransportReceiveStatus::would_block);
    actual.incoming.push_back({net::DatagramTransportReceiveStatus::truncated,{},foreign,9000,{}});
    CHECK(bound.receive(1400).status==net::DatagramTransportReceiveStatus::would_block);
    actual.incoming.push_back({net::DatagramTransportReceiveStatus::received,
        net::Datagram{*selected,{std::byte{3}}},*selected,1,{}});
    const auto received=bound.receive(1400);REQUIRE(received.datagram);
    CHECK(received.datagram->source==*selected);CHECK(received.datagram->payload==std::vector{std::byte{3}});
    CHECK(actual.local==*bound.local_address().address);
    CHECK(app::external_half_life_profile("valve"));CHECK_FALSE(app::external_half_life_profile("cstrike"));
}

TEST_CASE("E31 external success requires durable gameplay and close not research framebuffer counts",
    "[e31][external-session][receipt][no-stock]") {
    app::SessionReceiptJournal journal{{"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,42,999}};
    REQUIRE(journal.begin_generation({1,1,"maps/custom_lan.bsp"}));
    const auto facts=app::kSessionReceiptKnownFacts&~app::receipt_fact(app::SessionReceiptFact::live_visual_verified);
    REQUIRE(journal.observe_facts(facts,{1,1,1,false}));
    for(unsigned index=1;index<=5;++index)
        REQUIRE(journal.record(static_cast<app::SessionReceiptStage>(index),{index*10U,index,index,{}}));
    CHECK(app::external_gameplay_receipt_ready(*journal.snapshot()));
    CHECK_FALSE(app::session_receipt_keyboard_ready(*journal.snapshot()));
    CHECK_FALSE(app::external_close_succeeded(journal.snapshot(),false,false));
    app::SessionCloseReceipt close;
    close.reason=app::SessionCloseReason::window_close;close.outcome=app::SessionCloseOutcome::sent_but_unconfirmed;
    close.requested=true;close.eligible=true;close.queued=true;close.operation_identity=1;close.session_generation=1;
    close.send_attempt_count=close.successful_send_count=1;
    close.requested_process_ms=100;close.first_send_process_ms=101;close.last_send_process_ms=101;
    close.transport_closed_process_ms=200;
    REQUIRE(journal.finish(close));CHECK(app::external_close_succeeded(journal.snapshot(),false,false));
    CHECK_FALSE(app::external_close_succeeded(journal.snapshot(),true,false));
    CHECK_FALSE(app::external_close_succeeded(journal.snapshot(),false,true));
    auto missing=*journal.snapshot();missing.milestones.back().reset();
    CHECK_FALSE(app::external_gameplay_receipt_ready(missing));
    CHECK_FALSE(app::external_close_succeeded(&missing,false,false));
    app::SessionReceiptJournal early{{"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,43,1000}};
    REQUIRE(early.begin_generation({}));
    close={};close.reason=app::SessionCloseReason::startup_cancel;
    close.outcome=app::SessionCloseOutcome::cancel_without_protocol_disconnect;
    REQUIRE(early.finish(close));
    CHECK(app::external_close_succeeded(early.snapshot(),false,false));
    CHECK_FALSE(app::external_gameplay_receipt_ready(*early.snapshot()));
}

TEST_CASE("E31 early protocol failure is not replaced by a disconnect that never started",
    "[e31][external-session][receipt][no-stock]") {
    app::SessionReceiptJournal journal{{"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,42,999}};
    REQUIRE(journal.begin_generation({}));
    REQUIRE(journal.observe_facts(app::receipt_fact(app::SessionReceiptFact::keyboard_mouse),{}));
    app::SessionCloseReceipt close;
    close.reason=app::SessionCloseReason::fatal_failure;
    close.outcome=app::SessionCloseOutcome::fatal_failure;
    close.transport_closed_process_ms=698;
    REQUIRE(journal.finish(close));
    REQUIRE_FALSE(journal.snapshot()->close.requested);
    REQUIRE_FALSE(journal.snapshot()->close.eligible);
    CHECK_FALSE(app::external_close_succeeded(journal.snapshot(),true,true));
    CHECK(app::external_terminal_failure_reason("protocol_error",true)=="protocol_error");
    CHECK(app::external_terminal_failure_reason("challenge_protocol_error",true)=="challenge_protocol_error");
    CHECK(app::external_terminal_failure_reason("network_error",true)=="network_error");
    CHECK(app::external_terminal_failure_reason("unsupported_game",false)=="unsupported_game");
    CHECK(app::external_terminal_failure_reason("none",true)=="disconnect_failed");
    CHECK(app::external_terminal_failure_reason("",true)=="disconnect_failed");
    CHECK(app::external_terminal_failure_reason("none",false)=="gameplay_receipt_incomplete");
}
TEST_CASE("E33 console close is clean independently of game readiness and stays a distinct receipt",
    "[e33][external-session][receipt][no-stock]") {
    app::SessionReceiptJournal early{{"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,42,999}};
    REQUIRE(early.begin_generation({}));
    app::SessionCloseReceipt close;close.reason=app::SessionCloseReason::console_disconnect;
    close.outcome=app::SessionCloseOutcome::cancel_without_protocol_disconnect;
    REQUIRE(early.finish(close));CHECK(app::external_close_succeeded(early.snapshot(),false,false));
    CHECK_FALSE(app::external_gameplay_receipt_ready(*early.snapshot()));
    CHECK_FALSE(app::external_close_succeeded(early.snapshot(),true,false));
    CHECK_FALSE(app::external_close_succeeded(early.snapshot(),false,true));
    const auto encoded=app::encode_session_receipt(*early.snapshot());REQUIRE(encoded);
    CHECK(encoded->find("close_reason=console_disconnect\n")!=std::string::npos);
    const auto decoded=app::decode_session_receipt(*encoded);REQUIRE(decoded);
    CHECK(decoded->close.reason==app::SessionCloseReason::console_disconnect);
    auto window=*early.snapshot();window.close.reason=app::SessionCloseReason::window_close;
    CHECK_FALSE(app::external_close_succeeded(&window,false,false));
    auto cancelled=*early.snapshot();cancelled.close.reason=app::SessionCloseReason::startup_cancel;
    CHECK(app::external_close_succeeded(&cancelled,false,false));
    CHECK(app::to_string(cancelled.close.reason)!=app::to_string(decoded->close.reason));
}
