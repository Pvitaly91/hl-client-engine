#include <hlclient/platform/windows/external_launch_preflight.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdlib>
#include <memory>

TEST_CASE("E31 detects only the exact research application blocking filter", "[e31][launcher][isolation]") {
    using hlclient::platform::windows::is_research_application_block;
    const std::array expected{std::byte{1},std::byte{2},std::byte{3}};
    const std::array other{std::byte{1},std::byte{2},std::byte{4}};
    CHECK(is_research_application_block(L"HLClient temporary non-loopback block",true,true,expected,expected));
    CHECK_FALSE(is_research_application_block(L"HLClient temporary non-loopback block",true,true,other,expected));
    CHECK_FALSE(is_research_application_block(L"HLClient temporary loopback permit",true,true,expected,expected));
    CHECK_FALSE(is_research_application_block(L"foreign policy",true,true,expected,expected));
    CHECK_FALSE(is_research_application_block(L"HLClient temporary non-loopback block",false,true,expected,expected));
    CHECK_FALSE(is_research_application_block(L"HLClient temporary non-loopback block",true,false,expected,expected));
    CHECK_FALSE(is_research_application_block(L"HLClient temporary non-loopback block",true,true,{},{}));
}

TEST_CASE("E31 official runtime missing source fails before loading auth", "[e31][launcher][auth]") {
    const auto result=hlclient::platform::windows::observe_official_steam_runtime(L"missing-relative.dll");
    CHECK_FALSE(result);
    CHECK_FALSE(result.valve_signer);
    CHECK(result.binary.code==hlclient::platform::windows::WindowsBinaryIdentityErrorCode::path_not_absolute);
}

TEST_CASE("E31 optional installed Steam runtime is signed x86 Valve without initialization", "[e31][launcher][auth][local-assets]") {
    wchar_t* raw{};std::size_t count{};
    const auto error=_wdupenv_s(&raw,&count,L"HLCLIENT_E31_STEAM_RUNTIME");
    std::unique_ptr<wchar_t,decltype(&std::free)> path{raw,&std::free};
    if(error || !raw || count<=1) SKIP("HLCLIENT_E31_STEAM_RUNTIME explicit read-only runtime not selected");
    const auto result=hlclient::platform::windows::observe_official_steam_runtime(raw);
    INFO("binary status="<<hlclient::platform::windows::to_string(result.binary.code));
    REQUIRE(result.binary);
    CHECK(result.binary.identity->authenticode_valid);
    CHECK(result.binary.identity->pe_machine==hlclient::platform::windows::WindowsPeMachine::x86);
    CHECK(result.valve_signer);
    CHECK(static_cast<bool>(result));
}
