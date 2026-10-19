#include <hlclient/games/halflife/console_commands.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace console=hlclient::console;
using Action=hlclient::gameplay_input::GameplayInputAction;
struct GameConsoleFixture {
    console::CommandRegistry commands;
    console::ConsoleBindings bindings;
    std::vector<std::pair<Action,bool>> actions;
    std::vector<unsigned> slots;
    std::vector<int> cycles;
    unsigned lasts{};
    std::vector<std::string> weapons;
    std::vector<std::pair<std::string,std::string>> requests;
    bool accept{true};
    GameConsoleFixture()
    {
        console::SessionActions backend;
        backend.request=[this](std::string_view command,std::string_view argument){requests.emplace_back(command,argument);return accept;};
        backend.select_slot=[this](unsigned slot){slots.push_back(slot);return accept;};
        backend.cycle_inventory=[this](int direction){cycles.push_back(direction);return accept;};
        backend.last_inventory=[this]{++lasts;return accept;};
        backend.select_weapon=[this](std::string_view command){weapons.emplace_back(command);return accept;};
        hlclient::games::halflife::register_console_commands(commands,bindings,backend,[this](Action action,bool down){actions.emplace_back(action,down);});
    }
    console::CommandResult execute(std::string_view name,std::span<const std::string> arguments={})
    {
        return commands.execute(name,arguments,{console::ExecutionSource::local_console,console::SessionState::in_game,1});
    }
};
}

TEST_CASE("E33 production game commands map each action to the existing typed input", "[console][e33][console-game]")
{
    GameConsoleFixture f;
    const std::pair<const char*,Action> mapping[]{{"forward",Action::move_forward},{"back",Action::move_backward},
        {"moveleft",Action::move_left},{"moveright",Action::move_right},{"jump",Action::jump},{"duck",Action::duck},
        {"speed",Action::speed},{"attack",Action::attack_primary},{"attack2",Action::attack_secondary},
        {"use",Action::use},{"reload",Action::reload},{"showscores",Action::scoreboard}};
    for(const auto& [name,action]:mapping) {
        INFO(name);REQUIRE(f.execute("+"+std::string{name}).success);CHECK(f.actions.back()==std::pair{action,true});
        REQUIRE(f.execute("-"+std::string{name}).success);CHECK(f.actions.back()==std::pair{action,false});
    }
    CHECK(f.actions.size()==24);
    CHECK(f.bindings.bindings().at("Q")=="lastinv");CHECK(f.bindings.bindings().at("MOUSE1")=="+attack");
    CHECK(f.bindings.bindings().at("1")=="slot1");CHECK(f.bindings.bindings().at("MWHEELUP")=="invnext");
}

TEST_CASE("E33 production inventory commands call real backend interfaces once", "[console][e33][console-game]")
{
    GameConsoleFixture f;
    CHECK(f.commands.find("+attack")->semantics==console::CommandSemantics::local);
    CHECK(f.commands.find("slot1")->semantics==console::CommandSemantics::local);
    CHECK(f.commands.find("weapon_357")->semantics==console::CommandSemantics::server_request);
    for(unsigned slot=1;slot<=5;++slot){REQUIRE(f.execute("slot"+std::to_string(slot)).success);CHECK(f.slots.back()==slot);}
    REQUIRE(f.execute("invnext").success);REQUIRE(f.execute("invprev").success);CHECK(f.cycles==std::vector<int>{1,-1});
    REQUIRE(f.execute("lastinv").success);CHECK(f.lasts==1);
    for(const char* name:{"weapon_crowbar","weapon_9mmhandgun","weapon_357","weapon_9mmAR","weapon_shotgun","weapon_crossbow","weapon_rpg",
        "weapon_gauss","weapon_egon","weapon_hornetgun","weapon_handgrenade","weapon_satchel","weapon_tripmine","weapon_snark"}) {
        INFO(name);REQUIRE(f.execute(name).success);CHECK(f.weapons.back()==name);
    }
    CHECK(f.weapons.size()==14);
    f.accept=false;const auto rejected=f.execute("weapon_357");CHECK_FALSE(rejected.success);CHECK(rejected.message.find("rejected")!=std::string::npos);
}

TEST_CASE("E33 explicit server command adapters retain requested versus applied semantics", "[console][e33][console-game]")
{
    GameConsoleFixture f;
    const std::array<std::string,1> chat{"hello team"};
    for(const char* name:{"say","say_team"}){const auto result=f.execute(name,chat);REQUIRE(result.success);CHECK(result.message.find("server result pending")!=std::string::npos);CHECK(f.requests.back()==std::pair{std::string{name},std::string{"hello team"}});}
    for(const char* name:{"status","kill","drop"}){REQUIRE(f.execute(name).success);CHECK(f.requests.back()==std::pair{std::string{name},std::string{}});}
    const std::array<std::string,1> weapon{"weapon_357"};REQUIRE(f.execute("drop",weapon).success);CHECK(f.requests.back().second=="weapon_357");
    const auto count=f.requests.size();CHECK_FALSE(f.execute("say").success);
    const std::array<std::string,1> long_text{std::string(121,'x')};CHECK_FALSE(f.execute("say",long_text).success);CHECK(f.requests.size()==count);
    f.accept=false;CHECK_FALSE(f.execute("kill").success);CHECK(f.requests.size()==count+1);
}

TEST_CASE("E33 game command source and lifecycle rules guard every live backend", "[console][e33][console-game]")
{
    GameConsoleFixture f;
    for(const auto& descriptor:f.commands.descriptors()) {
        if(descriptor.semantics==console::CommandSemantics::unsupported)continue;
        INFO(descriptor.name);
        CHECK_FALSE(f.commands.execute(descriptor.name,{}, {console::ExecutionSource::server,console::SessionState::in_game,1}).success);
        CHECK_FALSE(f.commands.execute(descriptor.name,{}, {console::ExecutionSource::local_console,console::SessionState::idle,1}).success);
    }
    CHECK(f.actions.empty());CHECK(f.slots.empty());CHECK(f.weapons.empty());CHECK(f.requests.empty());CHECK(f.lasts==0);
    for(const char* name:{"name","model","topcolor","bottomcolor"}) {
        const auto* d=f.commands.find(name);REQUIRE(d);CHECK(d->semantics==console::CommandSemantics::unsupported);
        const std::array<std::string,1> value{"test"};CHECK_FALSE(f.execute(name,value).success);
    }
}
