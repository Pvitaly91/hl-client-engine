#include <hlclient/console/console.hpp>
#include <hlclient/core/console_text_privacy.hpp>
#include <algorithm>

namespace hlclient::console {
namespace {
std::string lower(std::string_view value)
{
    std::string out{value};for(auto& c:out)if(c>='A'&&c<='Z')c=static_cast<char>(c+('a'-'A'));return out;
}
bool safe_name(std::string_view name)
{
    return !name.empty() && name.size()<=64 && std::all_of(name.begin(),name.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_';});
}
bool secret_text(std::string_view text)
{
    return core::contains_sensitive_console_text(text);
}
}
CommandExecutor::CommandExecutor(CommandRegistry& commands,CVarRegistry& cvars):commands_{commands},cvars_{cvars}
{
    CommandDescriptor alias;alias.name="alias";alias.description="List or define a bounded local command alias";alias.usage="alias [name [quoted commands]]";alias.owner="console";
    alias.arguments={{"name",ValueType::text,true},{"commands",ValueType::text,true,{},{},4096}};
    (void)commands_.add(std::move(alias),[this](const ExecutionContext&,std::span<const ConsoleValue> values){
        if(values.empty()){std::string out;for(const auto& [name,body]:aliases_){(void)body;out+=name+"\n";}return CommandResult{true,out};}
        const auto name=lower(std::get<std::string>(values[0]));
        if(values.size()==1){const auto i=aliases_.find(name);return i==aliases_.end()?CommandResult{false,"Unknown alias"}:CommandResult{true,name+" = "+(secret_text(i->second)?"[redacted]":i->second)};}
        return define_alias(name,std::get<std::string>(values[1]));
    });
    CommandDescriptor exec;exec.name="exec";exec.description="Execute a cfg from the user's own console directory";exec.usage="exec <name.cfg>";exec.owner="console";exec.arguments={{"filename"}};
    (void)commands_.add(std::move(exec),[](const ExecutionContext&,std::span<const ConsoleValue>){return CommandResult{};});
    CommandDescriptor wait;wait.name="wait";wait.description="Frame-delayed scripts are unsupported";wait.usage="wait";wait.owner="console";wait.semantics=CommandSemantics::unsupported;
    (void)commands_.add(std::move(wait),[](const ExecutionContext&,std::span<const ConsoleValue>){return CommandResult{false,"wait is unsupported; scripts never block the application thread"};});
}
void CommandExecutor::set_config_store(ConsoleConfigStore* store) noexcept { config_=store; }
CommandResult CommandExecutor::enqueue(std::string_view text,ExecutionSource source,std::uint64_t generation)
{
    if(source!=ExecutionSource::local_console && source!=ExecutionSource::local_config && source!=ExecutionSource::local_binding)return {false,"Server text is data and cannot enter the local console dispatcher"};
    auto parsed=parse_commands(text);if(!parsed)return {false,parsed.error};
    if(pending_.size()+parsed.commands.size()>512 || budgets_.size()>=64)return {false,"Console command queue is full"};
    if(parsed.commands.empty())return {};
    const auto operation=++next_operation_;budgets_[operation]={0,text.size(),0};
    for(auto& command:parsed.commands)pending_.push_back({std::move(command),source,generation,operation,{}});
    return {};
}
CommandResult CommandExecutor::define_alias(std::string_view requested_name,std::string_view body)
{
    const auto name=lower(requested_name);
    if(!safe_name(name) || commands_.find(name) || cvars_.find(name))return {false,"Alias name is invalid or already registered"};
    if(body.size()>4096 || (!aliases_.contains(name) && aliases_.size()>=128))return {false,"Alias budget exceeded"};
    const auto parsed=parse_commands(body,4096,64);if(!parsed)return {false,parsed.error};
    // Hold/release ownership is defined only for direct +action binds. An alias
    // cannot hide a held action or erase the key which owes its corresponding up.
    for(const auto& c:parsed.commands)if(!c.words.empty() && (c.words[0].starts_with('+') || c.words[0].starts_with('-')))
        return {false,"Aliases containing +/- actions are unsupported; bind the action directly"};
    aliases_.insert_or_assign(name,std::string{body});return {true,"Alias defined: "+name};
}
CommandResult CommandExecutor::expand(const Pending& parent,std::string_view body,std::string key)
{
    auto& budget=budgets_[parent.operation];
    if(parent.ancestry.size()>=16 || std::find(parent.ancestry.begin(),parent.ancestry.end(),key)!=parent.ancestry.end())return {false,"Alias/exec cycle or nesting limit rejected"};
    if(++budget.expansions>64 || body.size()>131072-budget.bytes)return {false,"Script expansion budget exhausted"};
    budget.bytes+=body.size();auto parsed=parse_commands(body);if(!parsed)return {false,parsed.error};
    if(pending_.size()+parsed.commands.size()>512)return {false,"Expanded command queue budget exceeded"};
    auto ancestry=parent.ancestry;ancestry.push_back(std::move(key));
    for(auto i=parsed.commands.rbegin();i!=parsed.commands.rend();++i)pending_.push_front({std::move(*i),parent.source,parent.generation,parent.operation,ancestry});
    return {};
}
std::vector<std::string> CommandExecutor::process(SessionState state,std::uint64_t generation,std::size_t maximum_commands,
    const std::function<void(std::string_view)>& output_sink)
{
    std::vector<std::string> output;maximum_commands=std::min(maximum_commands,std::size_t{128});
    for(std::size_t n=0;n<maximum_commands && !pending_.empty();++n) {
        auto item=std::move(pending_.front());pending_.pop_front();
        CommandResult result;
        bool abort_operation=false;
        if(item.generation!=generation){abort_operation=true;}
        else if(++budgets_[item.operation].commands>256){result={false,"Total script operation budget exhausted"};abort_operation=true;}
        else {
            const auto name=lower(item.command.words.front());
            const std::span<const std::string> args{item.command.words.data()+1,item.command.words.size()-1};
            const ExecutionContext context{item.source,state,generation};
            if(const auto alias=aliases_.find(name);alias!=aliases_.end()) {
                result=args.empty()?expand(item,alias->second,"alias:"+name):CommandResult{false,"Aliases do not accept parameters"};
                abort_operation=!result.success;
            }else if(name=="exec") {
                result=commands_.execute(name,args,context);
                if(result.success) {
                    if(!config_)result={false,"User console config directory is unavailable"};
                    else {std::string error;auto text=config_->read(args.front(),error);result=text?expand(item,*text,"exec:"+lower(args.front())):CommandResult{false,error};}
                }
                abort_operation=!result.success;
            }else if(cvars_.find(name))result=cvars_.execute(name,args,context);
            else result=commands_.execute(name,args,context);
        }
        if(!result.message.empty()){
            if(output_sink)output_sink(result.message);
            output.push_back(std::move(result.message));
        }
        if(abort_operation)std::erase_if(pending_,[&](const auto& p){return p.operation==item.operation;});
        if(std::none_of(pending_.begin(),pending_.end(),[&](const auto& p){return p.operation==item.operation;}))budgets_.erase(item.operation);
    }
    return output;
}
std::vector<std::string> CommandExecutor::complete(std::string_view text) const
{
    auto out=commands_.complete(text);const auto prefix=lower(text);
    for(const auto& d:cvars_.descriptors())if(d.command.name.starts_with(prefix))out.push_back(d.command.name);
    for(const auto& [name,body]:aliases_){(void)body;if(name.starts_with(prefix))out.push_back(name);}
    std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());if(out.size()>128)out.resize(128);return out;
}
std::string CommandExecutor::persistent_aliases() const
{
    std::string out;for(const auto& [name,body]:aliases_)if(safe_for_history(body))out+="alias "+quote_argument(name)+" "+quote_argument(body)+"\n";return out;
}
bool CommandExecutor::safe_for_history(std::string_view text) const
{
    if(secret_text(text))return false;
    const auto parsed=parse_commands(text);if(!parsed)return false;
    for(const auto& c:parsed.commands) {
        const auto name=lower(c.words.front());
        if(name=="alias" || name=="exec")return false;
        if(const auto* d=commands_.find(name);d && d->sensitive)return false;
        if(const auto* d=cvars_.find(name);d && d->command.sensitive)return false;
    }
    return true;
}
void CommandExecutor::discard_pending() noexcept {pending_.clear();budgets_.clear();}
} // namespace hlclient::console
