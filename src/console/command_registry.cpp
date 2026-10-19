#include <hlclient/console/console.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <type_traits>

namespace hlclient::console {
namespace {
std::string normalized(std::string_view value)
{
    std::string name{value};
    for(auto& c:name) if(c>='A' && c<='Z') c=static_cast<char>(c+('a'-'A'));
    return name;
}
bool valid_name(std::string_view name)
{
    return !name.empty() && name.size()<=64 && std::all_of(name.begin(),name.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='+'||c=='-';});
}
std::optional<ConsoleValue> parse_value(std::string_view s,const ArgumentSpec& spec)
{
    if(s.size()>spec.maximum_bytes || !valid_utf8(s) || s.find('\0')!=s.npos) return {};
    if(spec.type==ValueType::text) return std::string{s};
    if(spec.type==ValueType::boolean) {
        if(s=="1" || s=="true")return true;
        if(s=="0" || s=="false")return false;
        return {};
    }
    double numeric{}; ConsoleValue value;
    if(spec.type==ValueType::integer) {
        std::int64_t n{}; const auto r=std::from_chars(s.data(),s.data()+s.size(),n);
        if(r.ec!=std::errc{} || r.ptr!=s.data()+s.size() || s.empty())return {};
        numeric=static_cast<double>(n); value=n;
    } else {
        const auto r=std::from_chars(s.data(),s.data()+s.size(),numeric);
        if(r.ec!=std::errc{} || r.ptr!=s.data()+s.size() || s.empty() || !std::isfinite(numeric)) return {};
        value=numeric;
    }
    if((spec.minimum && numeric<*spec.minimum) || (spec.maximum && numeric>*spec.maximum)) return {};
    return value;
}
bool allowed(const CommandDescriptor& d,const ExecutionContext& ctx)
{
    return (d.allowed_sources&static_cast<std::uint8_t>(ctx.source))!=0 && (d.allowed_states&static_cast<std::uint8_t>(ctx.state))!=0;
}
std::string describe_cvar(const CVarDescriptor& d,const CVarState& s)
{
    auto result=d.command.name+" = "+quote_argument(value_text(s.requested))+"; applied="+quote_argument(value_text(s.applied))+"; default="+quote_argument(value_text(d.default_value));
    if(d.value.minimum)result+="; min="+value_text(*d.value.minimum);
    if(d.value.maximum)result+="; max="+value_text(*d.value.maximum);
    result+=s.requested!=s.applied ? (d.command.restart_required?"; restart required":"; pending or backend fallback") : "; applied";
    return result;
}
}
std::string value_text(const ConsoleValue& value)
{
    return std::visit([](const auto& v)->std::string {
        using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,std::string>)return v;
        else if constexpr(std::is_same_v<T,bool>)return v?"1":"0";
        else { std::array<char,64> b{}; const auto r=std::to_chars(b.data(),b.data()+b.size(),v); return r.ec==std::errc{}?std::string{b.data(),r.ptr}:std::string{}; }
    },value);
}
bool CommandRegistry::add(CommandDescriptor d,CommandHandler handler)
{
    d.name=normalized(d.name);
    if(!valid_name(d.name) || !handler || entries_.size()>=512 || d.arguments.size()>32 || d.owner.empty())return false;
    const auto name=d.name;
    return entries_.emplace(name,Entry{std::move(d),std::move(handler)}).second;
}
const CommandDescriptor* CommandRegistry::find(std::string_view name) const
{
    const auto i=entries_.find(normalized(name));return i==entries_.end()?nullptr:&i->second.descriptor;
}
std::vector<CommandDescriptor> CommandRegistry::descriptors() const
{
    std::vector<CommandDescriptor> out;for(const auto& [name,e]:entries_){(void)name;out.push_back(e.descriptor);}return out;
}
std::vector<std::string> CommandRegistry::complete(std::string_view text) const
{
    const auto name=normalized(text);std::vector<std::string> out;
    const auto space=name.find(' ');
    if(space!=name.npos) {
        if(const auto* d=find(name.substr(0,space)); d && d->completion) {
            auto choices=d->completion(text.substr(space+1));
            for(const auto& c:choices)if(out.size()<128 && valid_utf8(c) && c.size()<1024)out.push_back(name.substr(0,space+1)+c);
        }
    } else for(const auto& [key,entry]:entries_) { (void)entry; if(key.starts_with(name))out.push_back(key); if(out.size()==128)break; }
    return out;
}
CommandResult CommandRegistry::execute(std::string_view name,std::span<const std::string> args,const ExecutionContext& ctx) const
{
    const auto i=entries_.find(normalized(name));if(i==entries_.end())return {false,"Unknown command: "+std::string{name}};
    const auto& d=i->second.descriptor;
    if(!allowed(d,ctx))return {false,"Command is not permitted from this source or session state: "+d.name};
    const auto required=static_cast<std::size_t>(std::count_if(d.arguments.begin(),d.arguments.end(),[](const auto& a){return !a.optional;}));
    if(args.size()<required || (!d.variadic && args.size()>d.arguments.size()) || args.size()>64)return {false,"Usage: "+d.usage};
    std::vector<ConsoleValue> values; values.reserve(args.size());
    for(std::size_t n=0;n<args.size();++n) {
        const ArgumentSpec fallback{"text"};
        const auto& spec=d.arguments.empty()?fallback:d.arguments[std::min(n,d.arguments.size()-1)];
        auto v=parse_value(args[n],spec);if(!v)return {false,"Invalid argument "+spec.name+". Usage: "+d.usage};
        values.push_back(std::move(*v));
    }
    try {return i->second.handler(ctx,values);}catch(...){return {false,"Command backend failed: "+d.name};}
}
bool CVarRegistry::add(CVarDescriptor d,ConsoleValue initial,CVarApply apply)
{
    d.command.name=normalized(d.command.name);
    if(!valid_name(d.command.name) || !apply || entries_.size()>=256 || d.command.owner.empty() ||
        !parse_value(value_text(initial),d.value) || !parse_value(value_text(d.default_value),d.value))return false;
    const auto name=d.command.name;
    return entries_.emplace(name,Entry{std::move(d),{initial,initial},std::move(apply)}).second;
}
const CVarDescriptor* CVarRegistry::find(std::string_view name) const
{
    const auto i=entries_.find(normalized(name));return i==entries_.end()?nullptr:&i->second.descriptor;
}
std::optional<CVarState> CVarRegistry::state(std::string_view name) const
{
    const auto i=entries_.find(normalized(name));return i==entries_.end()?std::nullopt:std::optional{i->second.state};
}
std::vector<CVarDescriptor> CVarRegistry::descriptors() const
{
    std::vector<CVarDescriptor> out;for(const auto& [name,e]:entries_){(void)name;out.push_back(e.descriptor);}return out;
}
CommandResult CVarRegistry::execute(std::string_view name,std::span<const std::string> args,const ExecutionContext& ctx)
{
    const auto i=entries_.find(normalized(name));if(i==entries_.end())return {false,"Unknown cvar: "+std::string{name}};
    auto& e=i->second;
    if(!allowed(e.descriptor.command,ctx))return {false,"Cvar is not permitted from this source or session state"};
    if(args.empty())return {true,describe_cvar(e.descriptor,e.state)};
    if(args.size()!=1)return {false,"Usage: "+e.descriptor.command.name+" <value>"};
    auto value=parse_value(args.front(),e.descriptor.value);
    if(!value)return {false,"Invalid value or range for "+e.descriptor.command.name};
    if(e.descriptor.command.restart_required) {e.state.requested=*value;return {true,describe_cvar(e.descriptor,e.state)};}
    auto actual=e.state.applied; CommandResult result;
    try { result=e.apply(*value,actual); } catch(...) {return {false,"Cvar backend failed"};}
    if(!result.success)return result;
    if(!parse_value(value_text(actual),e.descriptor.value))return {false,"Cvar backend returned invalid applied value"};
    e.state.requested=*value;e.state.applied=std::move(actual);
    return {true,describe_cvar(e.descriptor,e.state)+(result.message.empty()?"":"; "+result.message)};
}
std::string CVarRegistry::persistent_config() const
{
    std::string out;
    for(const auto& [name,e]:entries_)if(e.descriptor.command.persistent && !e.descriptor.command.sensitive)
        out+=name+" "+quote_argument(value_text(e.state.requested))+"\n";
    return out;
}
bool CVarRegistry::observe_applied(std::string_view name,ConsoleValue value)
{
    const auto entry=entries_.find(normalized(name));
    if(entry==entries_.end()||!parse_value(value_text(value),entry->second.descriptor.value))return false;
    entry->second.state.applied=std::move(value);return true;
}
ParseResult parse_commands(std::string_view text,const std::size_t maximum_bytes,const std::size_t maximum_commands)
{
    ParseResult out;
    const auto fail=[&](std::string message){out.commands.clear();out.error=std::move(message);return out;};
    if(text.size()>maximum_bytes || maximum_bytes>1024*1024 || maximum_commands>4096 || !valid_utf8(text))return fail("Script size or UTF-8 validation failed");
    ParsedCommand command;std::string word;bool in_quotes=false,started=false;
    const auto end_word=[&]{if(started){command.words.push_back(std::move(word));word.clear();started=false;}};
    const auto end_command=[&]{end_word();if(!command.words.empty()){out.commands.push_back(std::move(command));command={};}};
    for(std::size_t at=0;at<text.size();++at) {
        const char c=text[at];
        if(c=='\0' || (static_cast<unsigned char>(c)<32U && c!='\r' && c!='\n' && c!='\t'))return fail("Invalid control character");
        if(in_quotes) {
            if(c=='"'){in_quotes=false;continue;}
            if(c=='\\') {
                if(++at==text.size())return fail("Incomplete quoted escape");
                const char e=text[at];
                if(e=='n')word+='\n';else if(e=='r')word+='\r';else if(e=='t')word+='\t';
                else if(e=='\\'||e=='"')word+=e;else return fail("Unsupported quoted escape");
            }else word+=c;
        }else if(c=='"'){in_quotes=true;started=true;}
        else if(c=='/' && at+1<text.size() && text[at+1]=='/') {
            end_word();while(at<text.size() && text[at]!='\n')++at;end_command();
        }else if(c==';'||c=='\n'||c=='\r')end_command();
        else if(c==' '||c=='\t')end_word();
        else {word+=c;started=true;}
        if(command.words.size()>64 || word.size()>16384 || out.commands.size()>maximum_commands)return fail("Script command/argument budget exceeded");
    }
    if(in_quotes)return fail("Unclosed quote");
    end_command();if(out.commands.size()>maximum_commands)return fail("Script command budget exceeded");
    return out;
}
} // namespace hlclient::console
