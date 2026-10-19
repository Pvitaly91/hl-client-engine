#include <hlclient/console/console.hpp>
#include <hlclient/core/log.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <limits>
#include <sstream>
#include <iostream>

namespace {
namespace console=hlclient::console;
using console::ConsoleValue;
using console::CommandResult;
using console::ExecutionContext;
using console::SessionState;
using console::ExecutionSource;

struct ConfigFixture {
    std::filesystem::path directory;
    ConfigFixture()
    {
        static std::atomic<unsigned> sequence{};
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        directory=std::filesystem::temp_directory_path()/("hlclient-e33-fixture-"+std::to_string(stamp)+"-"+std::to_string(++sequence));
        if(!std::filesystem::create_directory(directory))throw std::runtime_error{"Unable to create unique config fixture"};
    }
    ~ConfigFixture(){std::error_code error;std::filesystem::remove_all(directory,error);}
};
void add_echo(console::CommandRegistry& registry,std::vector<std::string>& calls)
{
    console::CommandDescriptor d;d.name="echo";d.owner="test";d.usage="echo [text]";d.variadic=true;
    REQUIRE(registry.add(std::move(d),[&](const ExecutionContext&,std::span<const ConsoleValue> values){
        std::string text;for(const auto& v:values){if(!text.empty())text+=' ';text+=console::value_text(v);}calls.push_back(text);return CommandResult{true,text};
    }));
}
bool contains_text(const std::vector<std::string>& lines,std::string_view text)
{
    return std::any_of(lines.begin(),lines.end(),[&](const auto& line){return line.find(text)!=line.npos;});
}
}

TEST_CASE("E33 console slide follows monotonic time at every frame rate", "[console][e33][console-animation]")
{
    for(const int fps:{30,60,144,240}) {
        INFO(fps);console::ConsoleController controller;
        controller.toggle(0.0);CHECK(controller.takes_input());CHECK(controller.target_open());
        for(int frame=1;frame<=fps;++frame)controller.advance(static_cast<double>(frame)/fps);
        CHECK(controller.height_fraction()==Catch::Approx(0.5));
        controller.toggle(1.0);CHECK_FALSE(controller.target_open());CHECK(controller.takes_input());
        for(int frame=1;frame<=fps;++frame)controller.advance(1.0+static_cast<double>(frame)/fps);
        CHECK(controller.height_fraction()==0.0);CHECK_FALSE(controller.takes_input());
    }
    console::ConsoleController c;c.toggle(5.0);c.advance(5.06);
    const auto before=c.height_fraction();c.toggle(5.06);
    CHECK(c.height_fraction()==before);c.advance(5.09);CHECK(c.height_fraction()<before);
    c.toggle(5.09,true);CHECK_FALSE(c.target_open());
    const auto prior=c.height_fraction();c.advance(1.0);CHECK(c.height_fraction()==prior);
    c.advance(std::numeric_limits<double>::quiet_NaN());CHECK(c.height_fraction()==prior);
    c.toggle(5.09);c.advance(5.5);CHECK(c.height_fraction()==0.5);
    // A viewport/DPI change cannot affect the normalized controller state.
    for(const double height:{480.0,1080.0,1440.0})CHECK(c.height_fraction()*height==Catch::Approx(height/2));
}

TEST_CASE("E33 editor preserves UTF8 boundaries and defers multiline paste", "[console][e33][console-editor]")
{
    console::ConsoleModel model;
    const std::string text="привіт 🌍";
    REQUIRE(model.insert(text));CHECK(model.cursor()==text.size());
    model.left();CHECK(model.cursor()==text.size()-4);model.erase_forward();CHECK(model.editor()=="привіт ");
    model.backspace();CHECK(model.editor()=="привіт");model.backspace();CHECK(model.editor()=="приві");
    model.home();model.right();CHECK(model.cursor()==2);REQUIRE(model.insert("X"));CHECK(model.editor()=="пXриві");
    model.select_all();CHECK(model.copy_selection()==model.editor());REQUIRE(model.insert("echo one\necho two"));
    CHECK(model.lines().empty());CHECK(model.editor()=="echo one\necho two");
    const auto submitted=model.submit();CHECK(submitted=="echo one\necho two");CHECK(model.editor().empty());
    REQUIRE(model.insert("a"));CHECK_FALSE(model.insert(std::string{"\xc0\xaf",2}));CHECK(model.editor()=="a");
    model.select_all();model.erase_forward();CHECK(model.editor().empty());
    CHECK_FALSE(model.insert(std::string(5000,'x')));CHECK(model.editor().empty());
    CHECK_FALSE(model.insert(std::string(32,'\n')));CHECK(model.editor().empty());
    CHECK(console::utf8_prefix("привіт",3)=="п");
    CHECK(console::valid_utf8(console::utf8_prefix("😀😀",7)));
}

TEST_CASE("E33 console history completion and scrollback remain bounded", "[console][e33][console-model]")
{
    console::ConsoleModel model{{3,64,64,2,8}};
    REQUIRE(model.set_editor("echo first"));CHECK(model.submit()=="echo first");
    REQUIRE(model.set_editor("echo second"));(void)model.submit();
    REQUIRE(model.set_editor("echo third"));(void)model.submit();
    REQUIRE(model.set_editor("draft"));model.history_previous();CHECK(model.editor()=="echo third");
    model.history_previous();CHECK(model.editor()=="echo second");model.history_previous();CHECK(model.editor()=="echo second");
    model.history_next();model.history_next();CHECK(model.editor()=="draft");CHECK(model.lines().empty());
    REQUIRE(model.set_editor("password hidden"));(void)model.submit(false);model.history_previous();CHECK(model.editor()=="echo third");
    REQUIRE(model.set_editor("con"));const std::array<std::string,2> candidates{"connect","console_test"};model.complete(candidates);CHECK(model.editor()=="con");
    const std::array<std::string,1> single{"connect"};model.complete(single);CHECK(model.editor()=="connect ");
    model.clear();model.append("one");model.append("two");model.append("three");model.scroll(1);
    CHECK(model.visible_lines(80,1)==std::vector<std::string>{"two"});
    model.append("four");CHECK(model.dropped_lines()==1);CHECK(model.visible_lines(80,1)==std::vector<std::string>{"two"});
    model.append("five");CHECK(model.visible_lines(80,1)==std::vector<std::string>{"three"});
    model.bottom();CHECK(model.visible_lines(80,1)==std::vector<std::string>{"five"});
    model.clear();model.append("абвгде");CHECK(model.visible_lines(2,8)==std::vector<std::string>{"аб","вг","де"});
    CHECK(model.visible_lines(6,8)==std::vector<std::string>{"абвгде"});CHECK(model.lines().front()=="абвгде");
    for(unsigned i=0;i<1000;++i)model.append("flood");CHECK(model.lines().size()<=3);CHECK(model.dropped_lines()>=998);
}

TEST_CASE("E33 parser separates quoted data from commands and cfg comments", "[console][e33][console-parser]")
{
    const auto parsed=console::parse_commands("echo \"one;two // literal\"; echo \"escaped \\\"quote\\\"\" // ignored\n echo three");
    REQUIRE(parsed);REQUIRE(parsed.commands.size()==3);
    CHECK(parsed.commands[0].words==std::vector<std::string>{"echo","one;two // literal"});
    CHECK(parsed.commands[1].words==std::vector<std::string>{"echo","escaped \"quote\""});
    CHECK(parsed.commands[2].words==std::vector<std::string>{"echo","three"});
    const std::string value="український текст\n\"quoted\"\\path;";
    const auto roundtrip=console::parse_commands("echo "+console::quote_argument(value));
    REQUIRE(roundtrip);CHECK(roundtrip.commands[0].words[1]==value);
    CHECK_FALSE(console::parse_commands("echo \"unterminated"));
    CHECK_FALSE(console::parse_commands("echo \"bad\\x\""));
    CHECK_FALSE(console::parse_commands(std::string{"echo a\0echo b",13}));
    CHECK_FALSE(console::parse_commands("echo 1; echo 2",1024,1));
    CHECK_FALSE(console::parse_commands(std::string(1025,'x'),1024));
    CHECK(console::parse_commands("// harmless\n \t\r").commands.empty());
}

TEST_CASE("E33 typed commands enforce source state and argument contract", "[console][e33][console-command]")
{
    console::CommandRegistry registry;unsigned calls=0;ConsoleValue seen;
    console::CommandDescriptor d;d.name="typed";d.owner="test";d.usage="typed <integer 2..8>";d.arguments={{"count",console::ValueType::integer,false,2.0,8.0}};
    d.allowed_sources=static_cast<std::uint8_t>(ExecutionSource::local_console);d.allowed_states=static_cast<std::uint8_t>(SessionState::in_game);
    REQUIRE(registry.add(std::move(d),[&](const ExecutionContext&,std::span<const ConsoleValue> v){++calls;seen=v.front();return CommandResult{};}));
    const std::array<std::string,1> valid{"5"};
    CHECK_FALSE(registry.execute("typed",valid,{ExecutionSource::server,SessionState::in_game}).success);
    CHECK_FALSE(registry.execute("typed",valid,{}).success);
    CHECK(registry.execute("TYPED",valid,{ExecutionSource::local_console,SessionState::in_game}).success);
    CHECK(calls==1);CHECK(std::get<std::int64_t>(seen)==5);
    for(const std::string bad:{"1","9","nan","5.0","5junk",""}) {
        const std::array<std::string,1> args{bad};CHECK_FALSE(registry.execute("typed",args,{ExecutionSource::local_console,SessionState::in_game}).success);
    }
    CHECK(calls==1);CHECK_FALSE(registry.execute("unknown",{},{}).success);
    CHECK(registry.complete("ty")==std::vector<std::string>{"typed"});
}

TEST_CASE("E33 cvars expose actual value and restart requested state", "[console][e33][console-cvar]")
{
    console::CVarRegistry cvars;unsigned calls=0;
    console::CVarDescriptor volume;volume.command.name="volume";volume.command.owner="test";volume.command.persistent=true;volume.value={"gain",console::ValueType::real,false,0.0,1.0};volume.default_value=0.5;
    REQUIRE(cvars.add(volume,0.5,[&](const ConsoleValue& requested,ConsoleValue& actual){++calls;actual=std::min(0.75,std::get<double>(requested));return CommandResult{};}));
    const std::array<std::string,1> full{"1"};CHECK(cvars.execute("volume",full,{}).success);
    REQUIRE(cvars.state("volume"));CHECK(std::get<double>(cvars.state("volume")->requested)==1.0);CHECK(std::get<double>(cvars.state("volume")->applied)==0.75);
    const auto query=cvars.execute("volume",{},{});CHECK(query.message.find("default=")!=query.message.npos);CHECK(query.message.find("applied=")!=query.message.npos);CHECK(query.message.find("min=0")!=query.message.npos);
    for(const std::string bad:{"-1","2","nan","inf"}){const std::array<std::string,1> args{bad};CHECK_FALSE(cvars.execute("volume",args,{}).success);}CHECK(calls==1);
    volume.command.name="restart_test";volume.command.restart_required=true;
    REQUIRE(cvars.add(volume,0.5,[&](const ConsoleValue&,ConsoleValue&){++calls;return CommandResult{};}));
    CHECK(cvars.execute("restart_test",full,{}).success);CHECK(calls==1);CHECK(std::get<double>(cvars.state("restart_test")->applied)==0.5);
    CHECK(cvars.execute("restart_test",{},{}).message.find("restart required")!=std::string::npos);
    volume.command.name="secret";volume.command.sensitive=true;REQUIRE(cvars.add(volume,0.5,[](const auto&,auto&){return CommandResult{};}));
    const auto dump=cvars.persistent_config();CHECK(dump.find("volume \"1\"")!=dump.npos);CHECK(dump.find("secret")==dump.npos);
    CHECK_FALSE(cvars.execute("volume",full,{ExecutionSource::server,SessionState::in_game}).success);
}

TEST_CASE("E33 command queue bounds cycles and retains source and session identity", "[console][e33][console-script]")
{
    console::CommandRegistry registry;console::CVarRegistry cvars;console::CommandExecutor executor{registry,cvars};
    std::vector<std::string> calls;add_echo(registry,calls);
    REQUIRE(executor.enqueue("echo \"first; literal\";echo second",ExecutionSource::local_console,3).success);
    CHECK(calls.empty());const auto output=executor.process(SessionState::idle,3,1);CHECK(calls==std::vector<std::string>{"first; literal"});CHECK(output==calls);
    (void)executor.process(SessionState::idle,3);CHECK(calls.size()==2);
    REQUIRE(executor.enqueue("echo stale",ExecutionSource::local_console,3).success);(void)executor.process(SessionState::idle,4);CHECK(calls.size()==2);
    CHECK_FALSE(executor.enqueue("echo server;exec config.cfg",ExecutionSource::server,4).success);
    REQUIRE(executor.define_alias("cycle_a","cycle_b").success);REQUIRE(executor.define_alias("cycle_b","cycle_a").success);
    REQUIRE(executor.enqueue("cycle_a;echo should_not_run",ExecutionSource::local_console,4).success);
    std::vector<std::string> errors;for(unsigned i=0;i<10;++i){auto current=executor.process(SessionState::idle,4,1);errors.insert(errors.end(),current.begin(),current.end());}
    CHECK(contains_text(errors,"cycle"));CHECK(calls.size()==2);
    CHECK_FALSE(executor.define_alias("held","+attack;echo unsafe").success);
    // A branching expansion cannot evade the whole-operation budget by running
    // just one command on each frame.
    REQUIRE(executor.define_alias("leaf","echo leaf;echo leaf;echo leaf;echo leaf").success);
    REQUIRE(executor.define_alias("branch","leaf;leaf;leaf;leaf").success);
    REQUIRE(executor.define_alias("tree","branch;branch;branch;branch").success);
    REQUIRE(executor.enqueue("tree;tree;tree;tree;tree;tree",ExecutionSource::local_console,4).success);
    errors.clear();for(unsigned i=0;i<500;++i){auto current=executor.process(SessionState::idle,4,1);errors.insert(errors.end(),current.begin(),current.end());}
    CHECK(contains_text(errors,"budget"));CHECK(calls.size()<=258);
    REQUIRE(executor.enqueue("wait",ExecutionSource::local_console,4).success);CHECK(contains_text(executor.process(SessionState::idle,4),"unsupported"));
    CHECK_FALSE(executor.safe_for_history("password hunter2"));CHECK_FALSE(executor.safe_for_history("echo auth_ticket=private"));
    CHECK_FALSE(executor.safe_for_history("alias x \"password secret\""));CHECK(executor.safe_for_history("echo normal"));
    CHECK(executor.persistent_aliases().find("password")==std::string::npos);
    for(const std::string command:{"echo access_token=Secret", "echo RAW_USERINFO:Secret",
        "echo token=Secret", "echo token:Secret", "echo token Secret", "echo token\tSecret", "echo rcon=Secret"}) {
        CHECK_FALSE(executor.safe_for_history(command));
        REQUIRE(executor.define_alias("private_alias",command).success);
        CHECK(executor.persistent_aliases().find("Secret")==std::string::npos);
        REQUIRE(executor.enqueue("alias private_alias",ExecutionSource::local_console,4).success);
        const auto description=executor.process(SessionState::idle,4);
        CHECK(contains_text(description,"[redacted]"));CHECK_FALSE(contains_text(description,"Secret"));
        console::ConsoleModel history;REQUIRE(history.set_editor(command));
        (void)history.submit(executor.safe_for_history(command));history.history_previous();CHECK(history.editor().empty());
        console::ConsoleBindings private_bind;REQUIRE(private_bind.bind("F","echo "+command).success);
        CHECK(private_bind.persistent_config().find("Secret")==std::string::npos);
    }
}

TEST_CASE("E33 user config is scoped atomic escaped and bounded", "[console][e33][console-config]")
{
    ConfigFixture fixture;console::ConsoleConfigStore store{fixture.directory/"new-console"};std::string error;
    const std::string data="echo "+console::quote_argument("український \"текст\"; data")+"\n";
    REQUIRE(store.write("settings.cfg",data,error));const auto first=store.read("settings.cfg",error);REQUIRE(first);
    CHECK(first->starts_with("// hlclient-console-schema 1\n"));CHECK(first->ends_with(data));
    REQUIRE(store.write("settings.cfg","echo updated\n",error));const auto replaced=store.read("settings.cfg",error);REQUIRE(replaced);CHECK(replaced->ends_with("echo updated\n"));
    for(const std::string name:{"../escape.cfg","sub/escape.cfg","C:\\escape.cfg","/escape.cfg","evil.txt","CON.cfg","LPT1.cfg","file.cfg:stream"}) {
        CHECK_FALSE(store.write(name,"echo nope",error));CHECK_FALSE(store.read(name,error));
    }
    CHECK_FALSE(store.write("large.cfg",std::string(65536,'x'),error));
    CHECK_FALSE(store.write("bad.cfg",std::string{"\xed\xa0\x80",3},error));
    console::ConsoleConfigStore relative{"relative"};CHECK_FALSE(relative.write("file.cfg","x",error));
    std::size_t files=0;for(const auto& file:std::filesystem::directory_iterator(store.root())){++files;CHECK(file.path().filename()=="settings.cfg");}CHECK(files==1);
}

TEST_CASE("E33 exec nesting cannot execute server text or change execution origin", "[console][e33][console-exec]")
{
    ConfigFixture fixture;console::ConsoleConfigStore store{fixture.directory};std::string error;
    REQUIRE(store.write("a.cfg","exec b.cfg",error));REQUIRE(store.write("b.cfg","exec a.cfg",error));
    console::CommandRegistry registry;console::CVarRegistry cvars;console::CommandExecutor executor{registry,cvars};executor.set_config_store(&store);
    std::vector<std::string> calls;add_echo(registry,calls);
    REQUIRE(executor.enqueue("exec a.cfg; echo after",ExecutionSource::local_console,1).success);
    CHECK(contains_text(executor.process(SessionState::idle,1),"cycle"));CHECK(calls.empty());
    console::CommandDescriptor restricted;restricted.name="restricted";restricted.owner="test";restricted.allowed_sources=static_cast<std::uint8_t>(ExecutionSource::local_console);
    REQUIRE(registry.add(std::move(restricted),[&](const auto&,auto){calls.emplace_back("restricted");return CommandResult{};}));
    REQUIRE(store.write("origin.cfg","restricted",error));REQUIRE(executor.define_alias("origin","exec origin.cfg").success);
    REQUIRE(executor.enqueue("origin",ExecutionSource::local_binding,1).success);
    CHECK(contains_text(executor.process(SessionState::idle,1),"not permitted"));CHECK(calls.empty());
    REQUIRE(executor.enqueue("origin",ExecutionSource::local_console,1).success);(void)executor.process(SessionState::idle,1);CHECK(calls==std::vector<std::string>{"restricted"});
}

TEST_CASE("E33 binding releases retain physical ownership across rebind and focus loss", "[console][e33][console-bind]")
{
    console::ConsoleBindings bindings;using Action=console::ConsoleBindings::Action;
    bindings.register_action("+attack",Action::attack_primary);bindings.register_action("+use",Action::use);
    REQUIRE(bindings.bind("f","+attack").success);REQUIRE(bindings.bind("MOUSE1","+attack").success);
    std::vector<std::pair<Action,bool>> edges;std::vector<std::string> commands;
    const auto action=[&](Action a,bool down){edges.emplace_back(a,down);};const auto command=[&](std::string_view c){commands.emplace_back(c);};
    CHECK(bindings.key_event("F",true,false,action,command));CHECK(bindings.key_event("MOUSE1",true,false,action,command));
    CHECK(edges==std::vector<std::pair<Action,bool>>{{Action::attack_primary,true}});
    CHECK(bindings.key_event("F",false,false,action,command));CHECK(edges.size()==1);
    REQUIRE(bindings.bind("MOUSE1","+use").success);CHECK(bindings.key_event("MOUSE1",true,true,action,command));CHECK(edges.size()==1);
    CHECK(bindings.key_event("MOUSE1",false,false,action,command));REQUIRE(edges.size()==2);CHECK(edges.back()==std::pair{Action::attack_primary,false});
    CHECK(bindings.key_event("MOUSE1",true,false,action,command));CHECK(edges.back()==std::pair{Action::use,true});
    bindings.unbind_all();bindings.release_all(action);CHECK(edges.back()==std::pair{Action::use,false});const auto count=edges.size();bindings.release_all(action);CHECK(edges.size()==count);
    REQUIRE(bindings.bind("F","echo one").success);CHECK(bindings.key_event("F",true,true,action,command));CHECK(commands.empty());
    CHECK(bindings.key_event("F",true,false,action,command));CHECK(commands==std::vector<std::string>{"echo one"});
    CHECK_FALSE(bindings.bind("F","+attack;echo second").success);CHECK_FALSE(bindings.bind("F","+unknown").success);CHECK_FALSE(bindings.bind("F","-attack").success);
    CHECK_FALSE(bindings.bind("MWHEELUP","+attack").success);CHECK_FALSE(bindings.bind("unmapped","echo hi").success);
    REQUIRE(bindings.bind("E","password secret").success);CHECK(bindings.persistent_config().find("secret")==std::string::npos);
}

TEST_CASE("E33 console logging is bounded and never invokes a GUI callback", "[console][e33][console-log]")
{
    struct Redirect {
        std::ostringstream capture;std::streambuf* original;
        Redirect():original{std::clog.rdbuf(capture.rdbuf())}{}
        ~Redirect(){std::clog.rdbuf(original);}
    } redirect;
    hlclient::core::reset_console_log();
    for(unsigned i=0;i<800;++i)hlclient::core::log(hlclient::core::LogLevel::error,"flood line");
    auto batch=hlclient::core::drain_console_log(512);CHECK(batch.records.size()<=512);CHECK(batch.dropped_lines>0);
    hlclient::core::reset_console_log();
    hlclient::core::log(hlclient::core::LogLevel::error,"auth_ticket=private");
    hlclient::core::log(hlclient::core::LogLevel::error,std::string(4095,'x')+"її");
    batch=hlclient::core::drain_console_log();REQUIRE(batch.records.size()==2);
    CHECK(batch.records[0].message=="[sensitive diagnostic omitted]");CHECK(console::valid_utf8(batch.records[1].message));CHECK(batch.records[1].message.size()<=4096);
    for(const std::string text:{"access_token=Secret","raw_userinfo:Secret","token=Secret","token:Secret","token\tSecret"}) {
        hlclient::core::reset_console_log();hlclient::core::log(hlclient::core::LogLevel::error,text);
        const auto secret=hlclient::core::drain_console_log();REQUIRE(secret.records.size()==1);
        CHECK(secret.records[0].message=="[sensitive diagnostic omitted]");
    }
    CHECK(redirect.capture.str().find("Secret")==std::string::npos);
    CHECK(redirect.capture.str().find("private")==std::string::npos);
    hlclient::core::reset_console_log();
}

TEST_CASE("E33.1 runtime diagnostic floods cannot consume user output or error capacity", "[console][e33][e33_1][console-log]")
{
    namespace core=hlclient::core;
    struct Redirect {
        std::ostringstream capture;std::streambuf* original;
        Redirect():original{std::clog.rdbuf(capture.rdbuf())}{core::initialize_logging(core::LogLevel::trace);core::reset_console_log();}
        ~Redirect(){std::clog.rdbuf(original);core::initialize_logging();core::reset_console_log();}
    } redirect;
    for(unsigned i=0;i<800U;++i) {
        core::log(core::LogLevel::info,"[session-progress] live_usercmd_transmission=active");
        core::log(core::LogLevel::debug,"Runtime packet diagnostic");
    }
    core::log(core::LogLevel::error,"Important connection failure");
    const auto batch=core::drain_console_log(512U);
    REQUIRE(batch.records.size()==1U);
    CHECK(batch.records.front().message=="Important connection failure");
    CHECK(batch.dropped_lines==0U);
    // User-console routing must not disable the original diagnostic/report sink.
    const auto report=redirect.capture.str();
    CHECK(report.find("live_usercmd_transmission=active")!=std::string::npos);
    CHECK(report.find("Runtime packet diagnostic")!=std::string::npos);
    CHECK(report.find("Important connection failure")!=std::string::npos);
}

TEST_CASE("E33.1 command output remains readable while runtime telemetry continues", "[console][e33][e33_1][console-log]")
{
    namespace core=hlclient::core;
    struct Redirect {
        std::ostringstream capture;std::streambuf* original;
        Redirect():original{std::clog.rdbuf(capture.rdbuf())}{core::initialize_logging();core::reset_console_log();}
        ~Redirect(){std::clog.rdbuf(original);core::reset_console_log();}
    } redirect;
    console::ConsoleModel model;
    model.append("] echo readable_marker");model.append("readable_marker");
    model.append("cmdlist\necho\nclear\nconnect");
    model.append("cvarlist\r\nvolume\r\nsensitivity");
    const auto output=model.visible_lines(80U,100U);
    REQUIRE(output.size()==9U);
    for(unsigned update=0;update<40U;++update) {
        // Model separate updates without depending on a wall-clock rate window.
        core::reset_console_log();
        for(unsigned record=0;record<40U;++record)
            core::log(core::LogLevel::info,"[session-progress] live_usercmd_transmission=active");
        const auto batch=core::drain_console_log();
        for(const auto& record:batch.records)model.append(record.message);
    }
    CHECK(model.visible_lines(80U,100U)==output);
    CHECK(model.dropped_lines()==0U);
    CHECK(redirect.capture.str().find("live_usercmd_transmission=active")!=std::string::npos);
}

TEST_CASE("E33.1 first critical error retains capacity after an ordinary warning burst", "[console][e33][e33_1][console-log]")
{
    namespace core=hlclient::core;
    struct Redirect {
        std::ostringstream capture;std::streambuf* original;
        Redirect():original{std::clog.rdbuf(capture.rdbuf())}{core::initialize_logging();core::reset_console_log();}
        ~Redirect(){std::clog.rdbuf(original);core::reset_console_log();}
    } redirect;
    for(unsigned i=0;i<800U;++i)core::log(core::LogLevel::warning,"Repeated warning");
    core::log(core::LogLevel::error,"First actionable error");
    const auto batch=core::drain_console_log(512U);
    CHECK(batch.records.size()<=512U);
    CHECK(batch.dropped_lines>0U);
    CHECK(std::any_of(batch.records.begin(),batch.records.end(),[](const auto& record){
        return record.level==core::LogLevel::error&&record.message=="First actionable error";
    }));
}

TEST_CASE("E33.1 typed user notices and inert Unicode server text remain visible", "[console][e33][e33_1][console-log]")
{
    namespace core=hlclient::core;
    struct Redirect {
        std::ostringstream capture;std::streambuf* original;
        Redirect():original{std::clog.rdbuf(capture.rdbuf())}{core::initialize_logging();core::reset_console_log();}
        ~Redirect(){std::clog.rdbuf(original);core::reset_console_log();}
    } redirect;
    core::log(core::LogLevel::info,"Technical update",core::LogChannel::diagnostic);
    core::log(core::LogLevel::info,"Connected",core::LogChannel::user_notice);
    core::log_server_text("Привіт; exec inert.cfg\r\nAnother server line");
    const auto batch=core::drain_console_log();REQUIRE(batch.records.size()==3U);
    CHECK(batch.records[0].message=="Connected");
    CHECK(batch.records[0].channel==core::LogChannel::user_notice);
    CHECK(batch.records[1].message=="[server] Привіт; exec inert.cfg");
    CHECK(batch.records[1].channel==core::LogChannel::server_text);
    CHECK(batch.records[2].message=="[server] Another server line");
    CHECK(batch.dropped_lines==0U);
    CHECK(redirect.capture.str().find("Technical update")!=std::string::npos);
}
