#include <hlclient/core/external_launch_config.hpp>
#include <hlclient/platform/windows/external_launcher.hpp>
#include <hlclient/platform/windows/secure_output.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <chrono>
#include <filesystem>
#include <limits>
#include <thread>
#include <windows.h>

namespace {
namespace core=hlclient::core;
namespace native=hlclient::platform::windows;
class Directory final {
public:
    Directory(){parent=std::filesystem::canonical(std::filesystem::temp_directory_path());path=parent/(L"hlclient-e31-Україна "+native::external_widen(native::new_external_run_id()));std::filesystem::create_directory(path);}
    ~Directory(){if(path.is_absolute() && path.parent_path()==parent && path.filename().wstring().starts_with(L"hlclient-e31-")){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}
    std::filesystem::path parent,path;
};
std::filesystem::path fixture()
{
    std::array<wchar_t,32768> value{};const auto n=::GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));return std::filesystem::path{std::wstring{value.data(),n}}.parent_path()/L"hlclient_external_process_fixture.exe";
}
native::ExternalChildStatus wait(native::ExternalClientProcess&child)
{
    for(unsigned i=0;i<300;++i){const auto p=child.poll();if(!p.running)return p;std::this_thread::sleep_for(std::chrono::milliseconds{10});}
    static_cast<void>(child.terminate_owned_child(127));return child.poll();
}
}
TEST_CASE("E31 literal endpoint parsing separates target from console text", "[e31][launcher][config]")
{
    CHECK(core::ExternalLaunchConfiguration{}.endpoint=="192.168.0.104:27016");
    CHECK(core::normalize_external_endpoint("192.168.0.104:27016")=="192.168.0.104:27016");
    CHECK(core::normalize_external_endpoint("192.168.0.102:27016")=="192.168.0.102:27016");
    CHECK(core::normalize_external_endpoint("\t connect 192.168.0.102:27016 \t")=="192.168.0.102:27016");
    CHECK(core::normalize_external_endpoint("192.168.0.102")=="192.168.0.102:27015");
    CHECK(core::normalize_external_endpoint("010.000.002.003:65535")=="10.0.2.3:65535");
    for(const auto s:{"192.168.0.102:0","192.168.0.102:65536","192.168.0.256:1","192.168.0:3","1.2.3.4.5:7","192.168.0.102;quit","connect 192.168.0.102\nquit","192.168.0.102\n","192.168.0.102:1234 extra","192.168.0.102:12:34","255.255.255.255:12","239.0.0.1:12","0.0.0.0:7","127.0.0.1:27015","192.168.0.255:9","192.168.0.1:-1","localhost:123","+connect 192.168.0.102:27016","192.168.0.1:\t123"}) {INFO(s);CHECK_FALSE(core::normalize_external_endpoint(s));}
    CHECK_FALSE(core::normalize_external_endpoint(std::string(129,'1')));
}
TEST_CASE("E31 names use the existing bounded printable ASCII wire values", "[e31][launcher][config]")
{
    CHECK(core::valid_external_player_name("Player A 123"));CHECK(core::valid_external_player_name(std::string(31,'a')));
    for(const auto s:{""," ","User;quit","User\\name","User\"name","Іван","test\nname"}){INFO(s);CHECK_FALSE(core::valid_external_player_name(s));}
    CHECK_FALSE(core::valid_external_player_name(std::string(32,'a')));
}
TEST_CASE("E31 typed configuration round trips all real settings and Unicode paths", "[e31][launcher][config]")
{
    core::ExternalLaunchConfiguration c;c.asset_root="D:\\Ігри\\Half Life";c.steam_api_runtime=c.asset_root+"\\steam_api.dll";c.name="Player A";c.video={1,core::VideoWindowMode::borderless,1600,900,false};c.image={.12F,2.25F,1.32F};c.volume=67;
    const auto bytes=core::encode_external_launch_configuration(c);REQUIRE(bytes);auto d=core::decode_external_launch_configuration(*bytes);REQUIRE(d);
    CHECK(d.configuration->endpoint==c.endpoint);CHECK(d.configuration->asset_root==c.asset_root);CHECK(d.configuration->steam_api_runtime==c.steam_api_runtime);CHECK(d.configuration->name==c.name);CHECK(d.configuration->video==c.video);CHECK(d.configuration->image==c.image);CHECK(d.configuration->volume==c.volume);
    CHECK(native::external_utf8(native::external_widen(c.asset_root))==c.asset_root);
    c.image.gamma=0;CHECK_FALSE(core::encode_external_launch_configuration(c));c.image.gamma=std::numeric_limits<float>::infinity();CHECK_FALSE(core::encode_external_launch_configuration(c));c.image.gamma=1;c.volume=101;CHECK_FALSE(core::encode_external_launch_configuration(c));
}
TEST_CASE("E31 JSON rejects corrupt duplicate unknown and nonfinite fields", "[e31][launcher][config]")
{
    const auto valid=core::encode_external_launch_configuration({});REQUIRE(valid);
    for(const auto bad:{"{}","[]","null","{\"schema\":1}","{\"x\":NaN}","{\"x\":Infinity}"})CHECK_FALSE(core::decode_external_launch_configuration(bad));
    const auto replace=[&](const std::string&old,const std::string&with){auto bad=*valid;const auto at=bad.find(old);REQUIRE(at!=std::string::npos);bad.replace(at,old.size(),with);return bad;};
    CHECK_FALSE(core::decode_external_launch_configuration(replace("\"version\":3","\"version\":4")));
    CHECK_FALSE(core::decode_external_launch_configuration(replace("\"width\":1280","\"width\":1280.5")));
    CHECK_FALSE(core::decode_external_launch_configuration(replace("\"gamma\":1","\"gamma\":1e999")));
    CHECK_FALSE(core::decode_external_launch_configuration(replace("\"gamma\":1","\"gamma\":01")));
    CHECK_FALSE(core::decode_external_launch_configuration(replace("\"gamma\":1","\"gamma\":+1")));
    CHECK_FALSE(core::decode_external_launch_configuration(replace("\"gamma\":1","\"gamma\":0")));
    CHECK_FALSE(core::decode_external_launch_configuration(replace("\"name\":\"Player\"","\"name\":\"Player\\u0000\"")));
    CHECK_FALSE(core::decode_external_launch_configuration(replace("\"name\":\"Player\"","\"name\":\"Player\\uD800\"")));
    CHECK_FALSE(core::decode_external_launch_configuration(replace("\"name\":\"Player\"","\"name\":\"Player\",\"name\":\"Other\"")));
    CHECK_FALSE(core::decode_external_launch_configuration(replace("\"name\":\"Player\"","\"unknown\":\"Player\"")));
    CHECK_FALSE(core::decode_external_launch_configuration(*valid+"more"));CHECK_FALSE(core::decode_external_launch_configuration(std::string(core::kExternalLaunchMaximumBytes+1,' ')));
}
TEST_CASE("E31.1 v2 HUD and scene MSAA settings persist independently", "[e311][e31][launcher][config]")
{
    Directory d;const auto path=d.path/L"launcher.json";
    for(const unsigned samples:{0U,2U,4U,8U})for(const float scale:{.5F,1.F,1.25F,2.F}) {
        core::ExternalLaunchConfiguration c;c.video.msaa_samples=samples;c.hud_scale={false,scale};
        c.image={.1F,1.3F,1.1F};c.endpoint="192.168.0.104:27016";
        REQUIRE(native::save_external_settings(path,c));
        const auto bytes=native::read_external_text(path,core::kExternalLaunchMaximumBytes);REQUIRE(bytes);
        CHECK(bytes->find(core::kExternalLaunchSchema)!=std::string::npos);
        const auto loaded=native::load_external_settings(path,{});REQUIRE(loaded.status=="loaded");
        CHECK(loaded.configuration.video==c.video);CHECK(loaded.configuration.hud_scale==c.hud_scale);
        CHECK(loaded.configuration.image==c.image);CHECK(loaded.configuration.endpoint==c.endpoint);
        c.hud_scale={};REQUIRE(native::save_external_settings(path,c));
        const auto reset=native::load_external_settings(path,{});REQUIRE(reset.status=="loaded");
        CHECK(reset.configuration.hud_scale==core::HudScaleSettings{});
        CHECK(reset.configuration.video==c.video);CHECK(reset.configuration.image==c.image);
    }
}
TEST_CASE("E31.1 strict legacy launch snapshots migrate to explicit defaults", "[e311][e31][launcher][config]")
{
    const std::string old=R"({"schema":"hlclient.external-launch.v1","version":1,"endpoint":"192.168.0.104:27016","name":"Old Profile","asset_root":"D:/HL","steam_api_runtime":"D:/HL/steam_api.dll","display_index":0,"window_mode":"borderless","width":1920,"height":1080,"vsync":false,"brightness":0.1,"gamma":1.4,"contrast":0.8,"volume":71})";
    const auto migrated=core::decode_external_launch_configuration(old);REQUIRE(migrated);
    CHECK(migrated.configuration->hud_scale==core::HudScaleSettings{});
    CHECK(migrated.configuration->video.msaa_samples==4);
    CHECK(migrated.configuration->video.width==1920);CHECK(migrated.configuration->video.height==1080);
    CHECK(migrated.configuration->video.mode==core::VideoWindowMode::borderless);
    CHECK(migrated.configuration->name=="Old Profile");CHECK(migrated.configuration->volume==71);
    const auto rewritten=core::encode_external_launch_configuration(*migrated.configuration);REQUIRE(rewritten);
    CHECK(rewritten->find(core::kExternalLaunchSchema)!=std::string::npos);
    auto mixed=old;mixed.insert(mixed.size()-1,",\"msaa_samples\":8");
    CHECK_FALSE(core::decode_external_launch_configuration(mixed));
    mixed=old;mixed.replace(mixed.find("\"version\":1"),11,"\"version\":2");
    CHECK_FALSE(core::decode_external_launch_configuration(mixed));
}
TEST_CASE("E31.1 HUD and MSAA malformed values are rejected not rounded", "[e311][e31][launcher][config]")
{
    const auto valid=core::encode_external_launch_configuration({});REQUIRE(valid);
    const auto replace=[&](std::string_view key,std::string_view value){auto json=*valid;const auto at=json.find(key);REQUIRE(at!=std::string::npos);json.replace(at,key.size(),value);return core::decode_external_launch_configuration(json);};
    for(const auto invalid:{"-1","1","3","16","2.5","\"4\"","true"})
        CHECK_FALSE(replace("\"msaa_samples\":4","\"msaa_samples\":"+std::string{invalid}));
    for(const auto invalid:{"0.499","2.001","0.499999999","2.000000001","-1","1e999","\"auto\"","null","true"})
        CHECK_FALSE(replace("\"hud_scale\":1","\"hud_scale\":"+std::string{invalid}));
    CHECK_FALSE(replace("\"hud_scale_auto\":true","\"hud_scale_auto\":1"));
    CHECK_FALSE(replace("\"hud_scale_auto\":true","\"hud_scale_auto\":true,\"hud_scale_auto\":false"));
    CHECK_FALSE(replace("\"hud_scale_auto\":true","\"unknown\":true"));
    core::ExternalLaunchConfiguration c;c.video.msaa_samples=3;CHECK_FALSE(core::encode_external_launch_configuration(c));
    c.video.msaa_samples=4;c.hud_scale.scale=std::numeric_limits<float>::infinity();CHECK_FALSE(core::encode_external_launch_configuration(c));
}
TEST_CASE("E32 strict v3 movement capture migrates prior settings without changing graphics or target", "[e32][e31][launcher][config]")
{
    core::ExternalLaunchConfiguration c;c.endpoint="192.168.0.104:27016";c.name="LAN User";
    c.video={1,core::VideoWindowMode::borderless,1920,1080,false};c.video.msaa_samples=8;
    c.hud_scale={false,.75F};c.image={.13F,.88F,1.13F};c.volume=35;
    c.asset_root="D:/Ресурси/HL";c.steam_api_runtime="D:/Ресурси/HL/steam_api.dll";
    for(const bool enabled:{false,true}) {
        c.movement_diagnostics=enabled;
        const auto encoded=core::encode_external_launch_configuration(c);REQUIRE(encoded);
        const auto decoded=core::decode_external_launch_configuration(*encoded);REQUIRE(decoded);
        CHECK(decoded.configuration->movement_diagnostics==enabled);
        CHECK(decoded.configuration->endpoint==c.endpoint);CHECK(decoded.configuration->name==c.name);
        CHECK(decoded.configuration->video==c.video);CHECK(decoded.configuration->hud_scale==c.hud_scale);
        CHECK(decoded.configuration->image==c.image);CHECK(decoded.configuration->volume==c.volume);
        CHECK(decoded.configuration->asset_root==c.asset_root);CHECK(decoded.configuration->steam_api_runtime==c.steam_api_runtime);
    }
    const std::string v2=R"({"schema":"hlclient.external-launch.v2","version":2,"endpoint":"192.168.0.104:27016","name":"LAN User","asset_root":"D:/Ресурси/HL","steam_api_runtime":"D:/Ресурси/HL/steam_api.dll","display_index":1,"window_mode":"borderless","width":1920,"height":1080,"vsync":false,"brightness":0.13,"gamma":0.88,"contrast":1.13,"volume":35,"msaa_samples":8,"hud_scale_auto":false,"hud_scale":0.75})";
    const auto migrated=core::decode_external_launch_configuration(v2);REQUIRE(migrated);
    CHECK_FALSE(migrated.configuration->movement_diagnostics);
    CHECK(migrated.configuration->video==c.video);CHECK(migrated.configuration->hud_scale==c.hud_scale);
    CHECK(migrated.configuration->image==c.image);CHECK(migrated.configuration->endpoint==c.endpoint);
    const auto current=core::encode_external_launch_configuration(c);REQUIRE(current);
    const auto replace=[&](std::string_view from,std::string_view to){auto text=*current;const auto at=text.find(from);REQUIRE(at!=text.npos);text.replace(at,from.size(),to);return core::decode_external_launch_configuration(text);};
    for(const auto bad:{"0","1","\"true\"","null","{}"})
        CHECK_FALSE(replace("\"movement_diagnostics\":true","\"movement_diagnostics\":"+std::string{bad}));
    CHECK_FALSE(replace("\"movement_diagnostics\":true","\"movement_diagnostics\":true,\"movement_diagnostics\":false"));
    CHECK_FALSE(replace("\"movement_diagnostics\":true","\"unknown\":true"));
    auto mixed=v2;mixed.insert(mixed.size()-1,",\"movement_diagnostics\":true");
    CHECK_FALSE(core::decode_external_launch_configuration(mixed));
}
TEST_CASE("E31.1 actual AA status requires matching bounded renderer evidence", "[e311][e31][launcher][config]")
{
    constexpr std::string_view id="0123456789abcdef0123456789abcdef";
    const auto message=[&](unsigned requested,unsigned actual,std::string_view reason){return std::string{"{\"schema\":\"hlclient.external-msaa.v1\",\"run_id\":\""}+std::string{id}+"\",\"requested_samples\":"+std::to_string(requested)+",\"actual_samples\":"+std::to_string(actual)+",\"fallback_reason\":"+core::external_json_string(reason)+"}";};
    for(const unsigned n:{0U,2U,4U,8U}) {const auto observation=core::decode_external_msaa_observation(message(n,n,"none"),id);REQUIRE(observation);CHECK(observation->actual_samples==n);}
    const auto fallback=core::decode_external_msaa_observation(message(8,4,"unsupported_sample_count"),id);REQUIRE(fallback);CHECK(fallback->requested_samples==8);CHECK(fallback->actual_samples==4);
    CHECK_FALSE(core::decode_external_msaa_observation(message(4,8,"none"),id));
    CHECK_FALSE(core::decode_external_msaa_observation(message(4,4,"unsupported"),id));
    CHECK_FALSE(core::decode_external_msaa_observation(message(4,0,"none"),id));
    CHECK_FALSE(core::decode_external_msaa_observation(message(4,0,"hidden/raw payload"),id));
    CHECK_FALSE(core::decode_external_msaa_observation(message(3,0,"unsupported"),id));
    CHECK_FALSE(core::decode_external_msaa_observation(message(4,4,"none"),"1123456789abcdef0123456789abcdef"));
    CHECK_FALSE(core::decode_external_msaa_observation(std::string(1025,' '),id));
    auto duplicate=message(4,4,"none");duplicate.insert(duplicate.size()-1,",\"actual_samples\":0");CHECK_FALSE(core::decode_external_msaa_observation(duplicate,id));
}
TEST_CASE("E31.1 GUI reads retained scene observation handles and clean updates", "[e311][e31][launcher][windows]")
{
    Directory d;auto directory=native::open_secure_output_directory(d.path);REQUIRE(directory);
    constexpr std::string_view id="0123456789abcdef0123456789abcdef";
    const std::string first=R"({"schema":"hlclient.external-msaa.v1","run_id":"0123456789abcdef0123456789abcdef","requested_samples":4,"actual_samples":4,"fallback_reason":"none"})";
    const std::string second=R"({"schema":"hlclient.external-msaa.v1","run_id":"0123456789abcdef0123456789abcdef","requested_samples":4,"actual_samples":2,"fallback_reason":"framebuffer_incomplete"})";
    native::SecureOutputPublishedFile published;
    CHECK_FALSE(native::read_external_msaa_observation(*directory.directory,id));
    REQUIRE(native::secure_atomic_write_new(*directory.directory,L"external-msaa.json",std::as_bytes(std::span{first.data(),first.size()}),published));
    REQUIRE(published.valid());const auto observed=native::read_external_msaa_observation(*directory.directory,id);REQUIRE(observed);CHECK(observed->actual_samples==4);
    CHECK_FALSE(native::read_external_msaa_observation(*directory.directory,"1123456789abcdef0123456789abcdef"));
    REQUIRE(published.remove_on_close());published.close();
    CHECK_FALSE(native::read_external_msaa_observation(*directory.directory,id));
    REQUIRE(native::secure_atomic_write_new(*directory.directory,L"external-msaa.json",std::as_bytes(std::span{second.data(),second.size()}),published));
    const auto changed=native::read_external_msaa_observation(*directory.directory,id);REQUIRE(changed);CHECK(changed->actual_samples==2);CHECK(changed->fallback_reason=="framebuffer_incomplete");
}
TEST_CASE("E31.1 actual MSAA publication retries reader-held deletion with the newest observation", "[e311][e31][launcher][windows]")
{
    Directory d;auto directory=native::open_secure_output_directory(d.path);REQUIRE(directory);
    constexpr std::string_view id="0123456789abcdef0123456789abcdef";
    const std::string first=R"({"schema":"hlclient.external-msaa.v1","run_id":"0123456789abcdef0123456789abcdef","requested_samples":4,"actual_samples":4,"fallback_reason":"none"})";
    const std::string second=R"({"schema":"hlclient.external-msaa.v1","run_id":"0123456789abcdef0123456789abcdef","requested_samples":4,"actual_samples":2,"fallback_reason":"memory_budget"})";
    const std::string latest=R"({"schema":"hlclient.external-msaa.v1","run_id":"0123456789abcdef0123456789abcdef","requested_samples":4,"actual_samples":0,"fallback_reason":"memory_budget"})";
    using Result=native::ExternalMsaaPublicationResult;using Publisher=native::ExternalMsaaObservationPublisher;
    const auto start=std::chrono::steady_clock::time_point{};
    Publisher publisher;REQUIRE(publisher.publish(*directory.directory,first,id,start)==Result::published);
    struct HeldReader {
        HANDLE value{INVALID_HANDLE_VALUE};
        ~HeldReader(){close();}
        void close(){if(value!=INVALID_HANDLE_VALUE){::CloseHandle(value);value=INVALID_HANDLE_VALUE;}}
    } reader;
    reader.value=::CreateFileW((d.path/L"external-msaa.json").c_str(),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    REQUIRE(reader.value!=INVALID_HANDLE_VALUE);
    // Production deletion closes its writer; our simultaneously held read
    // handle keeps the name delete-pending until this reader actually closes.
    const auto changed_at=start+Publisher::retry_cooldown;
    REQUIRE(publisher.publish(*directory.directory,second,id,changed_at)==Result::retry_pending);
    REQUIRE(publisher.retry_pending());CHECK(publisher.statistics().attempts==2U);
    CHECK(publisher.publish(*directory.directory,latest,id,changed_at)==Result::retry_pending);
    CHECK(publisher.statistics().attempts==2U); // No same-frame retry I/O.
    reader.close();
    std::string_view newest=latest;unsigned expected_samples=0;
    SECTION("newest fallback supersedes the failed intermediate observation") {}
    SECTION("returning to the previous setting still republishes its deleted leaf") {
        newest=first;expected_samples=4;
    }
    REQUIRE(publisher.publish(*directory.directory,newest,id,changed_at+Publisher::retry_cooldown)==Result::published);
    CHECK_FALSE(publisher.retry_pending());
    const auto actual=native::read_external_msaa_observation(*directory.directory,id);REQUIRE(actual);
    CHECK(actual->actual_samples==expected_samples);
    CHECK(publisher.statistics().attempts==3U);CHECK(publisher.statistics().publications==2U);
    CHECK(publisher.statistics().retryable_failures==1U);
    CHECK(publisher.publish(*directory.directory,newest,id,changed_at+std::chrono::seconds{1})==Result::unchanged);
    CHECK(publisher.statistics().attempts==3U);
}
TEST_CASE("E31.1 MSAA publication never replaces a foreign leaf and failures have a strict retry bound", "[e311][e31][launcher][windows]")
{
    Directory d;auto directory=native::open_secure_output_directory(d.path);REQUIRE(directory);
    constexpr std::string_view id="0123456789abcdef0123456789abcdef";
    const std::string observation=R"({"schema":"hlclient.external-msaa.v1","run_id":"0123456789abcdef0123456789abcdef","requested_samples":4,"actual_samples":4,"fallback_reason":"none"})";
    const std::string foreign="existing unrelated leaf";
    REQUIRE(native::secure_atomic_write_new(*directory.directory,L"external-msaa.json",std::as_bytes(std::span{foreign.data(),foreign.size()})));
    using Publisher=native::ExternalMsaaObservationPublisher;using Result=native::ExternalMsaaPublicationResult;
    Publisher publisher;const auto start=std::chrono::steady_clock::time_point{};
    for(unsigned i=0;i<Publisher::maximum_consecutive_attempts;++i) {
        const auto result=publisher.publish(*directory.directory,observation,id,start+i*Publisher::retry_cooldown);
        CHECK(result==(i+1U<Publisher::maximum_consecutive_attempts?Result::retry_pending:Result::failed));
    }
    CHECK(publisher.publish(*directory.directory,observation,id,start+std::chrono::hours{1})==Result::failed);
    CHECK(publisher.statistics().attempts==Publisher::maximum_consecutive_attempts);
    CHECK(publisher.statistics().publications==0U);CHECK_FALSE(publisher.retry_pending());
    CHECK(native::read_external_text(d.path/L"external-msaa.json",1024)==foreign);
}
TEST_CASE("E31 settings use atomic replacement and preserve corrupt settings backup", "[e31][launcher][windows]")
{
    Directory d;const auto path=d.path/L"launcher.json";core::ExternalLaunchConfiguration defaults;
    CHECK(native::load_external_settings(path,defaults).status=="defaults");REQUIRE(native::save_external_settings(path,defaults));
    auto loaded=native::load_external_settings(path,defaults);CHECK(loaded.status=="loaded");CHECK(loaded.configuration.endpoint==defaults.endpoint);
    auto next=defaults;next.name="Next";next.image.gamma=2;REQUIRE(native::save_external_settings(path,next));loaded=native::load_external_settings(path,defaults);CHECK(loaded.configuration.name=="Next");CHECK(loaded.configuration.image.gamma==2);
    REQUIRE(native::write_external_text_atomic(path,"corrupt",true));loaded=native::load_external_settings(path,defaults);CHECK(loaded.status=="corrupt_settings_backed_up");CHECK(loaded.configuration.name==defaults.name);CHECK(native::read_external_text(loaded.corrupt_backup,32)=="corrupt");CHECK_FALSE(std::filesystem::exists(path));
    CHECK_FALSE(native::write_external_text_atomic(loaded.corrupt_backup,"overwrite",false));CHECK(native::read_external_text(loaded.corrupt_backup,32)=="corrupt");
}
TEST_CASE("E31 lifecycle gates duplicate Connect cancel and clean retry", "[e31][launcher]")
{
    core::ExternalLauncherLifecycle state;CHECK_FALSE(state.cancel());REQUIRE(state.begin());CHECK_FALSE(state.begin());REQUIRE(state.cancel());CHECK_FALSE(state.cancel());CHECK_FALSE(state.child_started());CHECK_FALSE(state.begin());state.finish(false);CHECK_FALSE(state.busy());REQUIRE(state.begin());REQUIRE(state.child_started());CHECK_FALSE(state.child_started());CHECK_FALSE(state.begin());state.finish(true);CHECK(state.phase()==core::ExternalLauncherPhase::completed);REQUIRE(state.begin());
}
TEST_CASE("E31 status parser promotes only complete typed lines", "[e31][launcher]")
{
    CHECK_FALSE(native::latest_external_status_marker("CreateProcess succeeded\n"));CHECK_FALSE(native::latest_external_status_marker("[external-status] stage=in_game"));CHECK_FALSE(native::latest_external_status_marker("server said [external-status] stage=in_game\n"));CHECK_FALSE(native::latest_external_status_marker("[external-status] stage=in_game reason=ticket=secret\n"));
    auto status=native::latest_external_status_marker("[external-status] stage=connecting reason=none\n[external-status] stage=signon reason=none\r\n");REQUIRE(status);CHECK(status->stage=="signon");CHECK(status->reason=="none");
    CHECK(native::redacted_external_status_log("name=PrivateName ticket=Secret\n[external-status] stage=signon reason=none\nraw_userinfo=not_persisted\n")=="[external-status] stage=signon reason=none\n");
}
TEST_CASE("E31 launcher retains the startup cause before later cleanup failure", "[e31][launcher][reporting]")
{
    // Exact five typed lines retained by the failed user run. Both errors can
    // arrive before one GUI poll: merely remembering the first poll is unsafe.
    constexpr std::string_view log=
        "[external-status] stage=checking_resources reason=none\n"
        "[external-status] stage=connecting reason=none\n"
        "[external-status] stage=error reason=protocol_error\n"
        "[external-status] stage=disconnecting reason=none\n"
        "[external-status] stage=error reason=disconnect_failed\n";
    REQUIRE(native::latest_external_status_marker(log));
    CHECK(native::latest_external_status_marker(log)->reason=="disconnect_failed");
    const auto failure=native::external_failure_markers(log);
    CHECK(failure.primary_reason=="protocol_error");
    CHECK(failure.cleanup_reason=="disconnect_failed");
    CHECK(native::redacted_external_status_log(log)==log);
    const auto later=native::external_failure_markers(std::string{log}+
        "[external-status] stage=error reason=client_startup_failed\n");
    CHECK(later.primary_reason=="protocol_error");
    CHECK(later.cleanup_reason=="disconnect_failed");
    const auto cleanup_only=native::external_failure_markers(
        "[external-status] stage=error reason=disconnect_failed\n");
    CHECK(cleanup_only.primary_reason.empty());
    CHECK(cleanup_only.cleanup_reason=="disconnect_failed");
    const auto typed=native::external_failure_markers(
        "raw_userinfo=secret\n[external-status] stage=error reason=challenge_protocol_error\n"
        "[external-status] stage=error reason=disconnect_failed\n");
    CHECK(typed.primary_reason=="challenge_protocol_error");
    CHECK(typed.cleanup_reason=="disconnect_failed");
    for(const auto invalid:{"[external-status] stage=error reason=ticket=secret\n",
        "[external-status] stage=error reason=none\n","[external-status] stage=error reason=protocol_error",
        "server said [external-status] stage=error reason=protocol_error\n"}) {
        const auto ignored=native::external_failure_markers(invalid);
        CHECK(ignored.primary_reason.empty());CHECK(ignored.cleanup_reason.empty());
    }
    const auto bounded=native::external_failure_markers(std::string(64U*1024U,'x')+
        "\n[external-status] stage=error reason=outside_bound\n");
    CHECK(bounded.primary_reason.empty());CHECK(bounded.cleanup_reason.empty());
}
TEST_CASE("E31 native child exact path has bounded logs and normal repeat exit", "[e31][launcher][windows][process]")
{
    Directory d;const auto copied=d.path/L"клієнт fixture.exe";std::filesystem::copy_file(fixture(),copied);
    const auto identity=native::observe_windows_binary_identity(copied,native::kMaximumObservedExecutableBytes,{native::AuthenticodePolicy::not_required_for_project_owned_binary,false});REQUIRE(identity);
    for(int run=0;run<2;++run){native::ExternalClientProcess child;REQUIRE(child.launch(*identity.identity,{L"--help"}));CHECK(child.process_id()!=0);CHECK(child.process_creation()!=0);CHECK_FALSE(child.launch(*identity.identity,{L"--help"}));const auto result=wait(child);REQUIRE(result.exit_code);CHECK(*result.exit_code==0);const auto log=child.finish_log();CHECK(log.bytes.size()<=64U*1024U);REQUIRE(native::latest_external_status_marker(log.bytes));CHECK(native::latest_external_status_marker(log.bytes)->stage=="completed");}
}
TEST_CASE("E31 native cancel is delivered to only owned child and failures remain failures", "[e31][launcher][windows][process]")
{
    const auto identity=native::observe_windows_binary_identity(fixture(),native::kMaximumObservedExecutableBytes,{native::AuthenticodePolicy::not_required_for_project_owned_binary,false});REQUIRE(identity);
    SECTION("startup cancel event"){native::ExternalClientProcess child;REQUIRE(child.launch(*identity.identity,{L"--wait"}));CHECK(child.poll().running);child.request_cancel();const auto result=wait(child);REQUIRE(result.exit_code);CHECK(*result.exit_code==0);}
    SECTION("nonzero exit"){native::ExternalClientProcess child;REQUIRE(child.launch(*identity.identity,{L"--fail"}));const auto result=wait(child);REQUIRE(result.exit_code);CHECK(*result.exit_code==7);}
    SECTION("launcher exception cleanup retains owner through graceful close"){native::ExternalClientProcess child;REQUIRE(child.launch(*identity.identity,{L"--wait"}));const auto result=native::close_external_child_after_failure(child);CHECK(result.exited);CHECK_FALSE(result.forced);CHECK(result.exit_code==0);}
    SECTION("missing image does not fall back to PATH"){auto absent=*identity.identity;absent.canonical_path=absent.canonical_path.parent_path()/L"e31-missing-client.exe";native::ExternalClientProcess child;CHECK_FALSE(child.launch(absent,{L"--help"}));CHECK(child.process_id()==0);}
}
