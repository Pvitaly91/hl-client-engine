#include <hlclient/console/console.hpp>
#include <hlclient/core/console_text_privacy.hpp>
#include <algorithm>
#include <set>

namespace hlclient::console {
namespace {
std::string key_name(std::string_view key)
{
    std::string s{key};for(auto& c:s)if(c>='a'&&c<='z')c=static_cast<char>(c-('a'-'A'));
    if(s=="UP")s="UPARROW";else if(s=="DOWN")s="DOWNARROW";else if(s=="LEFT")s="LEFTARROW";else if(s=="RIGHT")s="RIGHTARROW";else if(s=="DELETE")s="DEL";
    return s;
}
bool valid_key(std::string_view key)
{
    if(key.size()==1)return (key[0]>='A'&&key[0]<='Z')||(key[0]>='0'&&key[0]<='9');
    static constexpr std::string_view names[]{"SPACE","CTRL","SHIFT","ALT","RCTRL","RSHIFT","RALT","TAB","ENTER","ESCAPE","UPARROW","DOWNARROW","LEFTARROW","RIGHTARROW","HOME","END","PGUP","PGDN","BACKSPACE","DEL","MOUSE1","MOUSE2","MOUSE3","MOUSE4","MOUSE5","MWHEELUP","MWHEELDOWN","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
    return std::find(std::begin(names),std::end(names),key)!=std::end(names);
}
}
void ConsoleBindings::register_action(std::string name,Action action)
{
    if(!name.empty() && name.front()=='+' && action<Action::count && actions_.size()<64)actions_.insert_or_assign(std::move(name),action);
}
CommandResult ConsoleBindings::bind(std::string_view key,std::string_view command)
{
    const auto normalized=key_name(key);
    if(normalized=="F8" || normalized=="ESCAPE" || normalized=="GRAVE" || normalized=="BACKQUOTE" || normalized=="`" || normalized=="~")
        return {false,"Reserved physical key: console toggle, Escape and F8 diagnostics cannot be rebound"};
    if(!valid_key(normalized))return {false,"Unknown physical key name"};
    const auto parsed=parse_commands(command,4096,1);
    if(!parsed || parsed.commands.size()!=1)return {false,"A bind requires exactly one command; multi-command +/- scripts are unsupported"};
    const auto& words=parsed.commands.front().words;
    if(words.front().starts_with('-'))return {false,"Bind a +action, not its release command"};
    if(words.front().starts_with('+') && (words.size()!=1 || !actions_.contains(words.front())))return {false,"Unknown or compound +action bind"};
    if(normalized.starts_with("MWHEEL") && words.front().starts_with('+'))return {false,"Wheel impulses cannot hold a +action"};
    bindings_.insert_or_assign(normalized,std::string{command});return {};
}
void ConsoleBindings::unbind(std::string_view key){bindings_.erase(key_name(key));}
void ConsoleBindings::unbind_all(){bindings_.clear();}
const std::map<std::string,std::string,std::less<>>& ConsoleBindings::bindings() const noexcept{return bindings_;}
bool ConsoleBindings::key_event(std::string_view key,bool down,bool repeated,const ActionSink& action_sink,const CommandSink& command_sink)
{
    const auto normalized=key_name(key);
    if(!down) {
        const auto held=held_.find(normalized);
        if(held==held_.end())return bindings_.contains(normalized);
        const auto action=held->second;held_.erase(held);
        if(std::none_of(held_.begin(),held_.end(),[&](const auto& p){return p.second==action;}) && action_sink)action_sink(action,false);
        return true;
    }
    if(held_.contains(normalized))return true;
    const auto bound=bindings_.find(normalized);if(bound==bindings_.end())return false;
    if(repeated)return true;
    const auto parsed=parse_commands(bound->second,4096,1);if(!parsed || parsed.commands.empty())return true;
    const auto& command=parsed.commands.front().words.front();
    if(const auto action=actions_.find(command);action!=actions_.end()) {
        const bool already=std::any_of(held_.begin(),held_.end(),[&](const auto& p){return p.second==action->second;});
        held_.insert_or_assign(normalized,action->second);
        if(!already && action_sink)action_sink(action->second,true);
    }else if(command_sink)command_sink(bound->second);
    return true;
}
void ConsoleBindings::release_all(const ActionSink& sink)
{
    std::set<Action> actions;for(const auto& [key,action]:held_){(void)key;actions.insert(action);}held_.clear();
    for(const auto action:actions)if(sink)sink(action,false);
}
std::string ConsoleBindings::persistent_config() const
{
    std::string out;
    for(const auto& [key,command]:bindings_) {
        if(core::contains_sensitive_console_text(command))continue;
        out+="bind "+quote_argument(key)+" "+quote_argument(command)+"\n";
    }
    return out;
}
} // namespace hlclient::console
