#pragma once
#include <hlclient/console/console.hpp>
#include <hlclient/console/game_actions.hpp>
namespace hlclient::games::halflife {
void register_console_commands(console::CommandRegistry&,console::ConsoleBindings&,
    const console::SessionActions&,const std::function<void(gameplay_input::GameplayInputAction,bool)>&);
}
