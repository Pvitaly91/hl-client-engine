#include <hlclient/goldsrc/console_request.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/netchan_driver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace g=hlclient::goldsrc;
namespace n=hlclient::network;
using namespace std::chrono_literals;
std::vector<std::byte> literal(const std::string_view command) {
    std::vector<std::byte> bytes{std::byte{3}};
    for(const unsigned char c:command)bytes.push_back(std::byte{c});
    bytes.push_back(std::byte{0});return bytes;
}
struct ConsoleTransport final:n::IDatagramTransport {
    const n::NetworkAddress local=n::NetworkAddress::loopback(31033);
    std::vector<std::vector<std::byte>> sent;
    bool block_once{};
    std::size_t attempts{};
    n::DatagramLocalAddressResult local_address()const override{return {local,{}};}
    n::DatagramSendResult send_to(const n::NetworkAddress&,std::span<const std::byte> bytes)override {
        ++attempts;
        if(std::exchange(block_once,false))return {n::DatagramSendStatus::would_block,{}};
        sent.emplace_back(bytes.begin(),bytes.end());return {n::DatagramSendStatus::sent,{}};
    }
    n::DatagramTransportReceiveResult receive(std::size_t)override{return {};}
};
}

TEST_CASE("E33 typed console server codec has exact bounded command variants",
          "[e33][console][network][codec][no-stock]")
{
    using Kind=g::ConsoleServerCommandKind;
    for(const auto& [request,expected]:std::array{
        std::pair{g::ConsoleServerCommandRequest{Kind::say,"Hello player"},std::string{"say \"Hello player\""}},
        std::pair{g::ConsoleServerCommandRequest{Kind::say_team,"Left side"},std::string{"say_team \"Left side\""}},
        std::pair{g::ConsoleServerCommandRequest{Kind::status,{}},std::string{"status"}},
        std::pair{g::ConsoleServerCommandRequest{Kind::kill,{}},std::string{"kill"}},
        std::pair{g::ConsoleServerCommandRequest{Kind::drop,{}},std::string{"drop"}},
        std::pair{g::ConsoleServerCommandRequest{Kind::drop,"weapon_9mmhandgun"},std::string{"drop weapon_9mmhandgun"}}}) {
        const auto built=g::encode_console_server_command(request);REQUIRE(built);REQUIRE(built.bytes);
        CHECK(*built.bytes==literal(expected));CHECK_FALSE(built.error);
    }
    REQUIRE(g::encode_console_server_command({Kind::say,std::string(120U,'x')}));
}

TEST_CASE("E33 server console requests reject injection non-ASCII and unregistered operations",
          "[e33][console][network][codec][no-stock]")
{
    using Kind=g::ConsoleServerCommandKind;
    for(const auto kind:{Kind::say,Kind::say_team}) {
        for(const auto& text:std::vector<std::string>{"","   ","hello;quit","hello\nkill","hello\rkill",
                 "hello\tworld","hello\";quit","path\\name",std::string{"a\0b",3U},"Привіт",std::string(121U,'x')}) {
            const auto built=g::encode_console_server_command({kind,text});CHECK_FALSE(built);
            REQUIRE(built.error);CHECK_FALSE(built.bytes);
        }
    }
    for(const auto kind:{Kind::status,Kind::kill}) {
        CHECK_FALSE(g::encode_console_server_command({kind,"extra"}));
        CHECK_FALSE(g::encode_console_server_command({kind,";quit"}));
    }
    for(const auto& item:std::vector<std::string>{"weapon_","weapon_x;quit","weapon_x\nkill",
             "weapon_x extra","weapon_x\"","rcon","god",std::string(64U,'x')})
        CHECK_FALSE(g::encode_console_server_command({Kind::drop,item}));
    CHECK_FALSE(g::encode_console_server_command({static_cast<Kind>(255U),{}}));
}

TEST_CASE("E33 console requests use existing reliable delivery without claiming server execution",
          "[e33][console][network][netchan][no-stock]")
{
    using Kind=g::ConsoleServerCommandKind;
    const auto peer=n::NetworkAddress::loopback(27015);
    const auto epoch=g::NetchanDriverTimePoint{}+1s;
    ConsoleTransport transport;transport.block_once=true;
    g::NetchanDriver driver{transport,peer};REQUIRE(driver.start(epoch,transport.local));
    const auto request=g::encode_console_server_command({Kind::say,"Reliable once"});REQUIRE(request);
    REQUIRE(driver.queue_reliable(*request.bytes));
    // A queued request is not yet a sent datagram and never means the server
    // executed it. Transport retry belongs to the existing netchan owner.
    CHECK(transport.sent.empty());
    for(unsigned tick=0;tick<20U && transport.sent.empty();++tick) {
        driver.update(epoch+tick*50ms);while(driver.poll_event()){}
    }
    REQUIRE(transport.attempts>=2U);REQUIRE_FALSE(transport.sent.empty());
    const auto decoded=g::decode_client_to_server_netchan_packet(transport.sent.front());REQUIRE(decoded);
    const auto expected=literal("say \"Reliable once\"");
    CHECK(std::search(decoded.packet->payload.begin(),decoded.packet->payload.end(),
        expected.begin(),expected.end())!=decoded.packet->payload.end());
    REQUIRE(driver.request_graceful_close(epoch+1s));
    CHECK_FALSE(driver.queue_reliable(*request.bytes));
    g::GoldSrcHandshakeCoordinator coordinator{transport,peer,g::HandshakeStopPoint::challenge,{}};
    CHECK_FALSE(coordinator.request_console_command({Kind::status,{}}));
    REQUIRE(coordinator.request_graceful_close(epoch+1s));
    CHECK_FALSE(coordinator.request_console_command({Kind::kill,{}}));
}
