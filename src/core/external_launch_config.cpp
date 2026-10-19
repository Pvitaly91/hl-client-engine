#include <hlclient/core/external_launch_config.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <variant>

namespace hlclient::core {
namespace {
bool safe_utf8(const std::string_view s) noexcept
{
    for (std::size_t i=0;i<s.size();) {
        const auto first=static_cast<unsigned char>(s[i++]);
        if (first<128) continue;
        unsigned count{}; unsigned value{}; unsigned minimum{};
        if (first>=0xC2 && first<=0xDF) {count=1;value=first&31U;minimum=0x80;}
        else if(first>=0xE0 && first<=0xEF){count=2;value=first&15U;minimum=0x800;}
        else if(first>=0xF0 && first<=0xF4){count=3;value=first&7U;minimum=0x10000;}
        else return false;
        while(count--) {if(i==s.size())return false;const auto c=static_cast<unsigned char>(s[i++]);
            if((c&0xC0)!=0x80)return false;value=(value<<6U)|(c&63U);}
        if(value<minimum || value>0x10FFFF || (value>=0xD800 && value<=0xDFFF))return false;
    }
    return true;
}
bool safe_path_string(const std::string_view s) noexcept
{
    if(s.empty() || s.size()>4096 || !safe_utf8(s))return false;
    for(const auto c:s)if(static_cast<unsigned char>(c)<32 || c=='"' || c=='*' || c=='?' || c=='|' || c=='<' || c=='>')return false;
    return s.size()>3 && ((s[0]>='A' && s[0]<='Z') || (s[0]>='a' && s[0]<='z')) && s[1]==':' && (s[2]=='/' || s[2]=='\\') && s.find(':',2)==std::string_view::npos;
}
char permission_lower(const char c) noexcept
{
    return c>='A' && c<='Z'?static_cast<char>(c-'A'+'a'):c;
}
std::optional<std::string> permission_component(const std::string_view component)
{
    if(component.empty() || component.size()>255 || component=="." || component==".." ||
        component.front()==' ' || component.back()==' ' || component.back()=='.')return {};
    std::string normalized;
    normalized.reserve(component.size());
    for(const unsigned char c:component) {
        // Do not guess Windows Unicode case folding or accept Win32 aliases.
        if(c<32 || c>126 || c=='/' || c=='\\' || c==':' || c=='"' ||
            c=='*' || c=='?' || c=='|' || c=='<' || c=='>')return {};
        normalized+=permission_lower(static_cast<char>(c));
    }
    auto stem=std::string_view{normalized}.substr(0,normalized.find('.'));
    while(!stem.empty() && stem.back()==' ')stem.remove_suffix(1);
    if(stem=="con" || stem=="prn" || stem=="aux" || stem=="nul" ||
        stem=="conin$" || stem=="conout$" || stem=="clock$" ||
        (stem.size()==4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
            stem[3]>='0' && stem[3]<='9'))return {};
    return normalized;
}
std::optional<std::string> permission_installation_root(std::string_view root)
{
    if(root.size()<=3 || root.size()>4096 ||
        !((root[0]>='A' && root[0]<='Z') || (root[0]>='a' && root[0]<='z')) ||
        root[1]!=':' || (root[2]!='/' && root[2]!='\\'))return {};
    // One conventional trailing separator is equivalent. Empty interior
    // components, duplicate trailing separators and drive roots are not.
    if(root.back()=='/' || root.back()=='\\')root.remove_suffix(1);
    if(root.size()<=3 || root.back()=='/' || root.back()=='\\')return {};
    std::string normalized{permission_lower(root[0]),':','/'};
    std::size_t at=3;
    while(at<root.size()) {
        const auto separator=root.find_first_of("/\\",at);
        const auto end=separator==std::string_view::npos?root.size():separator;
        const auto component=permission_component(root.substr(at,end-at));
        if(!component)return {};
        normalized+=*component;
        if(end==root.size())break;
        normalized+='/';
        at=end+1;
    }
    return normalized;
}
using Value=std::variant<std::string,double,bool>;
class Parser final {
public:
    explicit Parser(std::string_view text):text_{text}{}
    std::optional<std::map<std::string,Value>> object()
    {
        std::map<std::string,Value> fields; skip();if(!take('{'))return {};
        skip();if(take('}'))return {};
        for(std::size_t count=0;count<24;++count) {
            auto key=string();if(!key || !take(':'))return {};
            skip();std::optional<Value> value;
            if(peek()=='"'){auto s=string();if(s)value=*s;}
            else if(text_.substr(at_,4)=="true"){at_+=4;value=true;}
            else if(text_.substr(at_,5)=="false"){at_+=5;value=false;}
            else {const auto begin=at_;while(at_<text_.size() && text_[at_]!=',' && text_[at_]!='}' && text_[at_]!=' ' && text_[at_]!='\r' && text_[at_]!='\n' && text_[at_]!='\t')++at_;
                const auto token=text_.substr(begin,at_-begin);double number{};
                // JSON's numeric grammar, including exponent, without accepting +1,
                // leading zeroes, NaN or implementation-specific infinity spellings.
                std::size_t p=0;if(p<token.size() && token[p]=='-')++p;
                if(p==token.size() || token[p]<'0' || token[p]>'9')return {};
                if(token[p]=='0')++p;else while(p<token.size() && token[p]>='0' && token[p]<='9')++p;
                if(p<token.size() && token[p]=='.'){++p;const auto start=p;while(p<token.size() && token[p]>='0' && token[p]<='9')++p;if(p==start)return {};}
                if(p<token.size() && (token[p]=='e' || token[p]=='E')){++p;if(p<token.size() && (token[p]=='+' || token[p]=='-'))++p;const auto start=p;while(p<token.size() && token[p]>='0' && token[p]<='9')++p;if(p==start)return {};}
                const auto r=std::from_chars(token.data(),token.data()+token.size(),number);
                if(p!=token.size() || r.ec!=std::errc{} || r.ptr!=token.data()+token.size() || !std::isfinite(number))return {};value=number;}
            if(!value || !fields.emplace(*key,*value).second)return {};
            skip();if(take('}')){skip();return at_==text_.size()?std::optional{fields}:std::nullopt;}
            if(!take(','))return {};
        }
        return {};
    }
private:
    char peek()const{return at_<text_.size()?text_[at_]:'\0';}
    void skip(){while(at_<text_.size() && (text_[at_]==' ' || text_[at_]=='\t' || text_[at_]=='\r' || text_[at_]=='\n'))++at_;}
    bool take(char c){skip();if(peek()!=c)return false;++at_;return true;}
    std::optional<unsigned> hex4(){unsigned n=0;for(int i=0;i<4;++i){if(at_==text_.size())return {};const char c=text_[at_++];unsigned d{};if(c>='0'&&c<='9')d=static_cast<unsigned>(c-'0');else if(c>='A'&&c<='F')d=static_cast<unsigned>(c-'A'+10);else if(c>='a'&&c<='f')d=static_cast<unsigned>(c-'a'+10);else return {};n=(n<<4U)|d;}return n;}
    std::optional<std::string> string(){if(!take('"'))return {};std::string out;
        while(at_<text_.size() && out.size()<=4096){char c=text_[at_++];if(c=='"')return safe_utf8(out)?std::optional{out}:std::nullopt;if(static_cast<unsigned char>(c)<32)return {};
            if(c!='\\'){out+=c;continue;}if(at_==text_.size())return {};c=text_[at_++];
            switch(c){case '"':case '\\':case '/':out+=c;break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
            case 'u':{auto n=hex4();if(!n)return {};if(*n>=0xD800 && *n<=0xDBFF){if(text_.substr(at_,2)!="\\u")return {};at_+=2;auto low=hex4();if(!low || *low<0xDC00 || *low>0xDFFF)return {};*n=0x10000+((*n-0xD800)<<10U)+(*low-0xDC00);}else if(*n>=0xDC00 && *n<=0xDFFF)return {};
                if(*n<128)out+=static_cast<char>(*n);else if(*n<2048){out+=static_cast<char>(0xC0|(*n>>6));out+=static_cast<char>(0x80|(*n&63));}else if(*n<65536){out+=static_cast<char>(0xE0|(*n>>12));out+=static_cast<char>(0x80|((*n>>6)&63));out+=static_cast<char>(0x80|(*n&63));}else{out+=static_cast<char>(0xF0|(*n>>18));out+=static_cast<char>(0x80|((*n>>12)&63));out+=static_cast<char>(0x80|((*n>>6)&63));out+=static_cast<char>(0x80|(*n&63));}break;}
            default:return {};}}
        return {};}
    std::string_view text_;std::size_t at_{};
};
std::string number(float v){std::array<char,40> b{};const auto r=std::to_chars(b.data(),b.data()+b.size(),v);return {b.data(),r.ptr};}
}

std::optional<std::string> normalize_external_endpoint(std::string_view s,const bool allow_loopback)
{
    if(s.empty() || s.size()>128)return {};
    while(!s.empty() && (s.front()==' ' || s.front()=='\t'))s.remove_prefix(1);
    while(!s.empty() && (s.back()==' ' || s.back()=='\t'))s.remove_suffix(1);
    for(const auto c:s)if(static_cast<unsigned char>(c)<32 || c==';' || c=='"' || c=='\\')return {};
    if(s.starts_with("connect ")){s.remove_prefix(8);while(!s.empty() && s.front()==' ')s.remove_prefix(1);}
    const auto colon=s.find(':');const auto address=s.substr(0,colon);unsigned port=27015;
    if(colon!=std::string_view::npos){const auto p=s.substr(colon+1);auto r=std::from_chars(p.data(),p.data()+p.size(),port);if(p.empty() || r.ec!=std::errc{} || r.ptr!=p.data()+p.size() || port==0 || port>65535)return {};}
    std::array<unsigned,4> octets{};std::size_t start=0;
    for(std::size_t i=0;i<4;++i){const auto dot=address.find('.',start);if((i<3)==(dot==std::string_view::npos))return {};const auto part=address.substr(start,dot==std::string_view::npos?dot:dot-start);if(part.empty() || part.size()>3)return {};const auto r=std::from_chars(part.data(),part.data()+part.size(),octets[i]);if(r.ec!=std::errc{} || r.ptr!=part.data()+part.size() || octets[i]>255)return {};start=dot+1;}
    // No unspecified, multicast, limited/direct .255 broadcast or class-E
    // destination. Loopback is explicit console opt-in, never a LAN default.
    if(octets[0]==0 || (!allow_loopback && octets[0]==127) || octets[0]>=224 || octets[3]==255)return {};
    return std::to_string(octets[0])+'.'+std::to_string(octets[1])+'.'+std::to_string(octets[2])+'.'+std::to_string(octets[3])+':'+std::to_string(port);
}
bool approved_installation_file_checks(const std::string_view json,
    const std::string_view endpoint,const std::string_view asset_root,
    const std::string_view game_directory) noexcept
{
    try {
        if(json.empty() || json.size()>kInstallationFileChecksMaximumBytes)return false;
        const auto fields=Parser{json}.object();
        if(!fields || fields->size()!=4)return false;
        const auto str=[&](const char* key)->const std::string* {
            const auto found=fields->find(key);
            return found==fields->end()?nullptr:std::get_if<std::string>(&found->second);
        };
        const auto schema=str("schema"), granted_endpoint=str("endpoint"),
            granted_root=str("asset_root"), granted_game=str("game_directory");
        if(!schema || *schema!=kInstallationFileChecksSchema || !granted_endpoint ||
            !granted_root || !granted_game)return false;
        const auto actual_endpoint=normalize_external_endpoint(endpoint);
        const auto expected_endpoint=normalize_external_endpoint(*granted_endpoint);
        const auto actual_root=permission_installation_root(asset_root);
        const auto expected_root=permission_installation_root(*granted_root);
        const auto actual_game=permission_component(game_directory);
        const auto expected_game=permission_component(*granted_game);
        return actual_endpoint && expected_endpoint && *actual_endpoint==*expected_endpoint &&
            actual_root && expected_root && *actual_root==*expected_root &&
            actual_game && expected_game && *actual_game==*expected_game;
    } catch(...) {
        // Permission must never be inferred when parsing/allocation fails.
        return false;
    }
}
bool valid_external_player_name(const std::string_view s) noexcept
{
    if(s.empty() || s.size()>31 || !safe_utf8(s))return false;
    bool visible=false;for(const auto c:s){if(static_cast<unsigned char>(c)<32 || static_cast<unsigned char>(c)>126 || c=='"' || c=='\\' || c==';')return false;if(c!=' ')visible=true;}return visible;
}
std::string validate_external_launch_configuration(const ExternalLaunchConfiguration& c)
{
    if(!normalize_external_endpoint(c.endpoint))return "invalid_endpoint";
    if(!valid_external_player_name(c.name))return "invalid_player_name";
    if(!safe_path_string(c.asset_root))return "invalid_asset_root";
    if(!safe_path_string(c.steam_api_runtime))return "invalid_steam_runtime_path";
    if(c.video.display_index>31 || c.video.width<640 || c.video.width>7680 || c.video.height<480 || c.video.height>4320)return "invalid_video_dimensions";
    if(c.video.mode!=VideoWindowMode::windowed && c.video.mode!=VideoWindowMode::borderless && c.video.mode!=VideoWindowMode::exclusive)return "invalid_window_mode";
    if(!valid_msaa_samples(c.video.msaa_samples))return "invalid_msaa_samples";
    if(!valid_hud_scale_settings(c.hud_scale))return "invalid_hud_scale";
    if(!std::isfinite(c.image.brightness) || c.image.brightness<-.5F || c.image.brightness>.5F || !std::isfinite(c.image.gamma) || c.image.gamma<.25F || c.image.gamma>4.F || !std::isfinite(c.image.contrast) || c.image.contrast<0.F || c.image.contrast>2.F)return "invalid_image_adjustments";
    if(c.volume>100)return "invalid_volume";
    return {};
}
ExternalLaunchConfigurationResult decode_external_launch_configuration(const std::string_view s)
{
    if(s.empty() || s.size()>kExternalLaunchMaximumBytes)return {{},"configuration_size_limit"};
    const auto fields=Parser{s}.object();if(!fields)return {{},"invalid_configuration_json"};
    ExternalLaunchConfiguration c;
    const auto str=[&](const char* key)->const std::string*{const auto i=fields->find(key);return i==fields->end()?nullptr:std::get_if<std::string>(&i->second);};
    const auto num=[&](const char* key)->const double*{const auto i=fields->find(key);return i==fields->end()?nullptr:std::get_if<double>(&i->second);};
    const auto schema=str("schema"), endpoint=str("endpoint"),name=str("name"),root=str("asset_root"),runtime=str("steam_api_runtime"),mode=str("window_mode");
    const auto display=num("display_index"),width=num("width"),height=num("height"),brightness=num("brightness"),gamma=num("gamma"),contrast=num("contrast"),volume=num("volume");
    const auto v=fields->find("vsync");const bool* vsync=v==fields->end()?nullptr:std::get_if<bool>(&v->second);
    const auto version=num("version");
    const bool legacy=schema && *schema=="hlclient.external-launch.v1" && version && *version==1;
    const bool previous=schema && *schema=="hlclient.external-launch.v2" && version && *version==2;
    const bool current=schema && *schema==kExternalLaunchSchema && version && *version==3;
    if((!legacy && !previous && !current) || fields->size()!=(legacy?15U:previous?18U:19U) || !endpoint || !name || !root || !runtime || !mode || !display || !width || !height || !brightness || !gamma || !contrast || !volume || !vsync)return {{},"invalid_configuration_fields"};
    for(const auto* p:{display,width,height,volume})if(*p<0 || *p>10000 || std::trunc(*p)!=*p)return {{},"invalid_configuration_integer"};
    const auto normalized=normalize_external_endpoint(*endpoint);if(!normalized)return {{},"invalid_endpoint"};
    c.endpoint=*normalized;c.name=*name;c.asset_root=*root;c.steam_api_runtime=*runtime;c.video.display_index=static_cast<std::size_t>(*display);c.video.width=static_cast<int>(*width);c.video.height=static_cast<int>(*height);c.video.vsync=*vsync;
    if(*mode=="windowed")c.video.mode=VideoWindowMode::windowed;else if(*mode=="borderless")c.video.mode=VideoWindowMode::borderless;else if(*mode=="exclusive")c.video.mode=VideoWindowMode::exclusive;else return {{},"invalid_window_mode"};
    c.image={static_cast<float>(*brightness),static_cast<float>(*gamma),static_cast<float>(*contrast)};c.volume=static_cast<unsigned>(*volume);
    if(previous || current) {
        const auto msaa=num("msaa_samples"),hud=num("hud_scale");
        const auto automatic=fields->find("hud_scale_auto");
        const auto* auto_scale=automatic==fields->end()?nullptr:std::get_if<bool>(&automatic->second);
        if(!msaa || !hud || !auto_scale)return {{},"invalid_configuration_fields"};
        if(*msaa<0 || *msaa>8 || std::trunc(*msaa)!=*msaa)return {{},"invalid_msaa_samples"};
        if(*hud<0.5 || *hud>2.0)return {{},"invalid_hud_scale"};
        c.video.msaa_samples=static_cast<unsigned>(*msaa);
        c.hud_scale={*auto_scale,static_cast<float>(*hud)};
    }
    if(current) {
        const auto diagnostic=fields->find("movement_diagnostics");
        const auto* enabled=diagnostic==fields->end()?nullptr:std::get_if<bool>(&diagnostic->second);
        if(!enabled)return {{},"invalid_configuration_fields"};
        c.movement_diagnostics=*enabled;
    }
    auto error=validate_external_launch_configuration(c);if(!error.empty())return {{},std::move(error)};return {std::move(c),{}};
}
std::string external_json_string(const std::string_view s)
{
    std::string out{"\""};constexpr char h[]="0123456789abcdef";
    for(const unsigned char c:s){if(c=='"' || c=='\\'){out+='\\';out+=static_cast<char>(c);}else if(c<32){out+="\\u00";out+=h[c>>4];out+=h[c&15];}else out+=static_cast<char>(c);}out+='"';return out;
}
std::optional<std::string> encode_external_launch_configuration(const ExternalLaunchConfiguration& c)
{
    if(!validate_external_launch_configuration(c).empty())return {};
    const auto mode=c.video.mode==VideoWindowMode::windowed?"windowed":c.video.mode==VideoWindowMode::borderless?"borderless":"exclusive";
    return "{\n\"schema\":"+external_json_string(kExternalLaunchSchema)+",\n\"version\":3,\n\"endpoint\":"+external_json_string(*normalize_external_endpoint(c.endpoint))+",\n\"name\":"+external_json_string(c.name)+",\n\"asset_root\":"+external_json_string(c.asset_root)+",\n\"steam_api_runtime\":"+external_json_string(c.steam_api_runtime)+",\n\"display_index\":"+std::to_string(c.video.display_index)+",\n\"window_mode\":"+external_json_string(mode)+",\n\"width\":"+std::to_string(c.video.width)+",\n\"height\":"+std::to_string(c.video.height)+",\n\"vsync\":"+(c.video.vsync?"true":"false")+",\n\"brightness\":"+number(c.image.brightness)+",\n\"gamma\":"+number(c.image.gamma)+",\n\"contrast\":"+number(c.image.contrast)+",\n\"volume\":"+std::to_string(c.volume)+",\n\"msaa_samples\":"+std::to_string(c.video.msaa_samples)+",\n\"hud_scale_auto\":"+(c.hud_scale.automatic?"true":"false")+",\n\"hud_scale\":"+number(c.hud_scale.scale)+",\n\"movement_diagnostics\":"+(c.movement_diagnostics?"true":"false")+"\n}\n";
}
bool ExternalLauncherLifecycle::busy() const noexcept{return phase_==ExternalLauncherPhase::starting || phase_==ExternalLauncherPhase::active || phase_==ExternalLauncherPhase::cancelling;}
std::optional<ExternalLaunchConfiguration> merge_external_launch_configuration(
    const ExternalLaunchConfiguration& baseline,const ExternalLaunchConfiguration& edited,
    const ExternalLaunchConfiguration& current)
{
    if(!validate_external_launch_configuration(baseline).empty() ||
        !validate_external_launch_configuration(edited).empty() ||
        !validate_external_launch_configuration(current).empty())return {};
    auto out=current;
    const auto apply=[](const auto& before,const auto& after,auto& destination) {
        if(before!=after)destination=after;
    };
    apply(baseline.endpoint,edited.endpoint,out.endpoint);
    apply(baseline.name,edited.name,out.name);
    apply(baseline.asset_root,edited.asset_root,out.asset_root);
    apply(baseline.steam_api_runtime,edited.steam_api_runtime,out.steam_api_runtime);
    apply(baseline.video.display_index,edited.video.display_index,out.video.display_index);
    apply(baseline.video.mode,edited.video.mode,out.video.mode);
    apply(baseline.video.width,edited.video.width,out.video.width);
    apply(baseline.video.height,edited.video.height,out.video.height);
    apply(baseline.video.vsync,edited.video.vsync,out.video.vsync);
    apply(baseline.video.msaa_samples,edited.video.msaa_samples,out.video.msaa_samples);
    apply(baseline.image.brightness,edited.image.brightness,out.image.brightness);
    apply(baseline.image.gamma,edited.image.gamma,out.image.gamma);
    apply(baseline.image.contrast,edited.image.contrast,out.image.contrast);
    apply(baseline.hud_scale.automatic,edited.hud_scale.automatic,out.hud_scale.automatic);
    apply(baseline.hud_scale.scale,edited.hud_scale.scale,out.hud_scale.scale);
    apply(baseline.volume,edited.volume,out.volume);
    apply(baseline.movement_diagnostics,edited.movement_diagnostics,out.movement_diagnostics);
    return validate_external_launch_configuration(out).empty()?std::optional{std::move(out)}:std::nullopt;
}
std::optional<SceneAntialiasingObservation> decode_external_msaa_observation(
        const std::string_view json,const std::string_view expected_run_id)
{
    if(json.empty() || json.size()>1024 || expected_run_id.size()!=32)return {};
    for(const char c:expected_run_id)if(!((c>='0' && c<='9') || (c>='a' && c<='f')))return {};
    const auto fields=Parser{json}.object();if(!fields || fields->size()!=5)return {};
    const auto str=[&](const char* key)->const std::string*{const auto i=fields->find(key);return i==fields->end()?nullptr:std::get_if<std::string>(&i->second);};
    const auto num=[&](const char* key)->const double*{const auto i=fields->find(key);return i==fields->end()?nullptr:std::get_if<double>(&i->second);};
    const auto schema=str("schema"),run=str("run_id"),reason=str("fallback_reason");
    const auto requested=num("requested_samples"),actual=num("actual_samples");
    if(!schema || *schema!="hlclient.external-msaa.v1" || !run || *run!=expected_run_id ||
        !reason || reason->empty() || reason->size()>64 || !requested || !actual)return {};
    for(const char c:*reason)if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_'))return {};
    for(const auto n:{*requested,*actual})if(n<0 || n>8 || std::trunc(n)!=n || !valid_msaa_samples(static_cast<unsigned>(n)))return {};
    if(*actual>*requested || (*actual==*requested && *reason!="none") ||
        (*actual<*requested && *reason=="none"))return {};
    return SceneAntialiasingObservation{static_cast<unsigned>(*requested),static_cast<unsigned>(*actual),*reason};
}
bool ExternalLauncherLifecycle::begin() noexcept{if(busy())return false;phase_=ExternalLauncherPhase::starting;return true;}
bool ExternalLauncherLifecycle::child_started() noexcept{if(phase_!=ExternalLauncherPhase::starting)return false;phase_=ExternalLauncherPhase::active;return true;}
bool ExternalLauncherLifecycle::cancel() noexcept{if(phase_!=ExternalLauncherPhase::starting && phase_!=ExternalLauncherPhase::active)return false;phase_=ExternalLauncherPhase::cancelling;return true;}
void ExternalLauncherLifecycle::finish(bool success) noexcept{if(busy())phase_=success?ExternalLauncherPhase::completed:ExternalLauncherPhase::failed;}
} // namespace hlclient::core
