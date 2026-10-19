#include <hlclient/games/halflife/console_commands.hpp>
#include <stdexcept>
namespace hlclient::games::halflife {
void register_console_commands(console::CommandRegistry& registry,console::ConsoleBindings& bindings,
    const console::SessionActions& backend,const std::function<void(gameplay_input::GameplayInputAction,bool)>& action) {
    using namespace console;using A=gameplay_input::GameplayInputAction;
    const auto add=[&](std::string name,std::string description,CommandHandler handler,std::vector<ArgumentSpec> args={}){
        CommandDescriptor d;d.name=std::move(name);d.owner="halflife";d.description=description;d.usage=d.name+" "+description;
        d.allowed_states=static_cast<std::uint8_t>(SessionState::in_game);d.arguments=std::move(args);
        d.semantics=CommandSemantics::server_request;
        if(d.name.starts_with("+") || d.name.starts_with("-") || d.name.starts_with("slot") ||
            d.name=="invnext" || d.name=="invprev" || d.name=="lastinv")
            d.semantics=CommandSemantics::local;
        if(!registry.add(std::move(d),std::move(handler)))throw std::runtime_error{"Duplicate game console command"};};
    const auto result=[](bool ok){return CommandResult{ok,ok?"Request queued; server result pending.":"Request unavailable or rejected by current game state."};};
    const auto inventory_result=[](bool ok){return CommandResult{ok,ok?"Inventory menu/selection request accepted; weapon changes require server confirmation.":"Inventory request unavailable or rejected by current game state."};};
    const std::pair<const char*,A> actions[]{{"forward",A::move_forward},{"back",A::move_backward},
        {"moveleft",A::move_left},{"moveright",A::move_right},{"jump",A::jump},{"duck",A::duck},
        {"speed",A::speed},{"attack",A::attack_primary},{"attack2",A::attack_secondary},
        {"use",A::use},{"reload",A::reload},{"showscores",A::scoreboard}};
    for(const auto& [name,value]:actions){bindings.register_action("+"+std::string{name},value);
        for(const bool down:{true,false})add(std::string{down?"+":"-"}+name,"Typed gameplay action",[action,value,down](auto&,auto){action(value,down);return CommandResult{};});}
    const std::pair<const char*,const char*> defaults[]{{"W","+forward"},{"S","+back"},{"A","+moveleft"},{"D","+moveright"},
        {"SPACE","+jump"},{"CTRL","+duck"},{"SHIFT","+speed"},{"MOUSE1","+attack"},{"MOUSE2","+attack2"},
        {"E","+use"},{"R","+reload"},{"TAB","+showscores"},{"Q","lastinv"},{"MWHEELUP","invnext"},
        {"MWHEELDOWN","invprev"},{"1","slot1"},{"2","slot2"},{"3","slot3"},{"4","slot4"},{"5","slot5"}};
    for(const auto& [key,command]:defaults)(void)bindings.bind(key,command);
    for(unsigned slot=1;slot<=5;++slot)add("slot"+std::to_string(slot),"Open inventory group",[backend,slot,inventory_result](auto&,auto){return inventory_result(backend.select_slot&&backend.select_slot(slot));});
    add("invnext","Next inventory item",[backend,inventory_result](auto&,auto){return inventory_result(backend.cycle_inventory&&backend.cycle_inventory(1));});
    add("invprev","Previous inventory item",[backend,inventory_result](auto&,auto){return inventory_result(backend.cycle_inventory&&backend.cycle_inventory(-1));});
    add("lastinv","Previously confirmed selected weapon",[backend,inventory_result](auto&,auto){return inventory_result(backend.last_inventory&&backend.last_inventory());});
    for(const char* name:{"weapon_crowbar","weapon_9mmhandgun","weapon_357","weapon_9mmAR","weapon_shotgun","weapon_crossbow", "weapon_rpg",
        "weapon_gauss","weapon_egon","weapon_hornetgun","weapon_handgrenade","weapon_satchel","weapon_tripmine","weapon_snark"}){
        const std::string command{name};add(command,"Select through server weapon catalogue",[backend,command,result](auto&,auto){return result(backend.select_weapon&&backend.select_weapon(command));});}
    for(const char* name:{"say","say_team","status","kill","drop"}){
        const std::string command{name};std::vector<ArgumentSpec> args;
        if(command=="say"||command=="say_team")args.push_back({"quoted text",ValueType::text,false,{},{},120});
        if(command=="drop")args.push_back({"weapon",ValueType::text,true,{},{},63});
        add(command,"Explicit server request (printable ASCII chat)",[backend,command,result](auto&,auto values){
            return result(backend.request&&backend.request(command,values.empty()?"":value_text(values[0])));},std::move(args));
    }
    for(const char* name:{"name","model","topcolor","bottomcolor"}){
        CommandDescriptor d;d.name=name;d.owner="halflife";d.description="Runtime userinfo update has no validated outgoing path; use launcher for next connection";
        d.usage=d.name+" [value]";d.arguments={{"value",ValueType::text,true}};d.semantics=CommandSemantics::unsupported;
        (void)registry.add(std::move(d),[](auto&,auto){return CommandResult{false,"Runtime userinfo update unsupported; change next connection in launcher."};});
    }
}
}
