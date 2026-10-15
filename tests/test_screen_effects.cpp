#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <limits>

namespace {
namespace api=hlclient::game_api;
struct ScreenFixture {
    api::GameClientHost host{hlclient::games::halflife::make_half_life_client_module()};
    hlclient::client::RuntimeClientObservationState state;
    std::uint64_t record{};
    ScreenFixture(){state.generation=1;host.reset({1,1,1});}
    api::GameRecordResult stage(std::string_view name,std::span<const std::byte> body,
        std::optional<double> now=10.) {
        ++record;
        api::GameMessageView m{api::GameMessageKind::user_message,name,body,
            {record,static_cast<std::size_t>(record),0,8,8+body.size()*8},64};
        return host.stage_record({&state,state,{&m,1},1,8,{},now});
    }
};
// Pinned SDK protocol facts only: 4.12 seconds/amount, 8.8 frequency,
// unsigned shorts followed by RGBA. Fixture bytes are project owned.
std::array<std::byte,10> fade(unsigned flags=0) {
    return {std::byte{0},std::byte{0x20},std::byte{0},std::byte{0x10},
        static_cast<std::byte>(flags),std::byte{0},std::byte{255},std::byte{255},std::byte{255},std::byte{255}};
}
const std::array shake{std::byte{0},std::byte{0x50},std::byte{0},std::byte{0x20},std::byte{0},std::byte{0x0a}};
}
TEST_CASE("Server screen fade strict decode and transactional publication","[screen-effects][no-stock][user-message]") {
    ScreenFixture f;
    const auto bytes=fade();
    auto result=f.stage("ScreenFade",bytes);REQUIRE(result);
    REQUIRE(result.state->screen_fades.size()==1);
    CHECK(result.state->screen_fades[0].duration_seconds==2);
    CHECK(result.state->screen_fades[0].hold_seconds==1);
    CHECK(result.state->screen_effect_seconds==10);
    CHECK(f.host.screen_effects(10).overlay_rgba[3]==0); // Staging is inert.
    f.host.commit_record(std::move(*result.state));
    CHECK(f.host.screen_effects(10).overlay_rgba[3]==1);
    CHECK(f.host.screen_effects(10.9).overlay_rgba[3]==1);
    CHECK(f.host.screen_effects(12).overlay_rgba[3]==Catch::Approx(.5));
    CHECK(f.host.screen_effects(13).overlay_rgba[3]==0);
    for(std::size_t n=0;n<10;++n)CHECK_FALSE(f.stage("ScreenFade",std::span{bytes}.first(n)));
    auto invalid=fade(16);CHECK_FALSE(f.stage("ScreenFade",invalid));
    CHECK_FALSE(f.stage("ScreenFade",bytes,std::numeric_limits<double>::quiet_NaN()));
    CHECK(f.host.screen_effects(12).overlay_rgba[3]==Catch::Approx(.5));
}
TEST_CASE("Server screen fade out stay modulation replacement and no stale replay","[screen-effects][no-stock]") {
    ScreenFixture f;auto bytes=fade(1);auto result=f.stage("ScreenFade",bytes);REQUIRE(result);
    const auto replay=*result.state;f.host.commit_record(std::move(*result.state));
    CHECK(f.host.screen_effects(10).overlay_rgba[3]==0);
    CHECK(f.host.screen_effects(11).overlay_rgba[3]==Catch::Approx(.5));
    CHECK(f.host.screen_effects(12.5).overlay_rgba[3]==1);
    CHECK(f.host.screen_effects(13).overlay_rgba[3]==0);
    bytes=fade(6);result=f.stage("ScreenFade",bytes,20);REQUIRE(result);f.host.commit_record(std::move(*result.state));
    CHECK(f.host.screen_effects(100).overlay_rgba[3]==1);
    CHECK(f.host.screen_effects(100).modulate);
    auto old=replay;f.host.commit_record(std::move(old));CHECK(f.host.screen_effects(100).overlay_rgba[3]==1);
    bytes=fade();bytes[9]=std::byte{0};result=f.stage("ScreenFade",bytes,101);REQUIRE(result);
    f.host.commit_record(std::move(*result.state));CHECK(f.host.screen_effects(101).overlay_rgba[3]==0);
    f.host.reset({2,2,1});CHECK(f.host.screen_effects(101).overlay_rgba[3]==0);
}
TEST_CASE("Server screen shake is bounded deterministic presentation not movement state","[screen-effects][no-stock]") {
    ScreenFixture f;const auto generation=f.state.generation;
    auto result=f.stage("ScreenShake",shake);REQUIRE(result);
    const auto replay=*result.state;f.host.commit_record(std::move(*result.state));
    auto a=f.host.screen_effects(10.125),b=f.host.screen_effects(10.125);
    CHECK(a.translation==b.translation);CHECK(a.roll_degrees==b.roll_degrees);
    CHECK(std::any_of(a.translation.begin(),a.translation.end(),[](float n){return std::abs(n)>.1F;}));
    for(double t=10;t<12;t+=.003) {
        const auto frame=f.host.screen_effects(t);
        for(float value:frame.translation)CHECK(std::abs(value)<=5);
        CHECK(std::abs(frame.roll_degrees)<=1);
    }
    CHECK(f.state.generation==generation);CHECK(f.state.publication_revision==0);
    auto old=replay;f.host.commit_record(std::move(old));
    CHECK(f.host.screen_effects(10.125).translation==a.translation);
    CHECK(f.host.screen_effects(12).translation==std::array<float,3>{});
    CHECK(f.host.screen_effects(9).translation==std::array<float,3>{});
    f.host.teardown();CHECK(f.host.screen_effects(10.125).translation==std::array<float,3>{});
}
TEST_CASE("Clockless and cleared record effects never play later","[screen-effects][no-stock]") {
    ScreenFixture f;auto bytes=fade();auto result=f.stage("ScreenFade",bytes,std::nullopt);REQUIRE(result);
    f.host.commit_record(std::move(*result.state));CHECK(f.host.screen_effects(10).overlay_rgba[3]==0);
    result=f.stage("ScreenShake",shake,std::nullopt);REQUIRE(result);f.host.commit_record(std::move(*result.state));
    CHECK(f.host.screen_effects(10).translation==std::array<float,3>{});
    result=f.stage("ScreenFade",bytes);REQUIRE(result);f.host.commit_record(std::move(*result.state));
    result=f.stage("UnknownInertMessage",{});REQUIRE(result);
    CHECK(result.state->screen_fades.empty());CHECK(result.state->screen_shakes.empty());
    f.host.commit_record(std::move(*result.state));CHECK(f.host.screen_effects(10.5).overlay_rgba[3]==1);
}
TEST_CASE("Screen effects follow record ordinal not opaque identity","[screen-effects][no-stock]") {
    ScreenFixture f;auto bytes=fade(4);
    api::GameMessageView message{api::GameMessageKind::user_message,"ScreenFade",bytes,{900,101,0,8,88},64};
    auto result=f.host.stage_record({&f.state,f.state,{&message,1},1,8,{},10});REQUIRE(result);
    f.host.commit_record(std::move(*result.state));CHECK(f.host.screen_effects(10).overlay_rgba[3]==1);
    bytes[9]=std::byte{0};message.source={5,102,0,8,88};
    result=f.host.stage_record({&f.state,f.state,{&message,1},1,8,{},11});REQUIRE(result);
    f.host.commit_record(std::move(*result.state));CHECK(f.host.screen_effects(11).overlay_rgba[3]==0);
    bytes[9]=std::byte{255};message.source={1000,100,0,8,88};
    result=f.host.stage_record({&f.state,f.state,{&message,1},1,8,{},12});REQUIRE(result);
    f.host.commit_record(std::move(*result.state));CHECK(f.host.screen_effects(12).overlay_rgba[3]==0);
}
