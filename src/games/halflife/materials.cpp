#include <hlclient/games/halflife/materials.hpp>
#include <algorithm>

namespace hlclient::games::halflife {
namespace {
char upper(char c) noexcept { return c>='a' && c<='z' ? static_cast<char>(c-'a'+'A') : c; }
bool space(char c) noexcept { return c==' ' || c=='\t' || c=='\r' || c=='\n'; }
std::optional<MaterialKind> kind(char c) noexcept {
    switch (upper(c)) {
    case 'C': return MaterialKind::concrete; case 'M': return MaterialKind::metal;
    case 'D': return MaterialKind::dirt; case 'V': return MaterialKind::vent;
    case 'G': return MaterialKind::grate; case 'T': return MaterialKind::tile;
    case 'S': return MaterialKind::slosh; case 'W': return MaterialKind::wood;
    case 'P': return MaterialKind::computer; case 'Y': return MaterialKind::glass;
    case 'F': return MaterialKind::flesh; case 'N': return MaterialKind::snow;
    default: return {};
    }
}
game_api::LocalSoundReference reference(std::string_view name) noexcept {
    game_api::LocalSoundReference result;
    result.source=game_api::SoundReferenceSource::pinned_halflife_client_sound_profile;
    std::copy(name.begin(),name.end(),result.sample.begin());
    return result;
}
struct Family { std::array<std::string_view,4> names{}; std::size_t count{}; float volume{},bar{}; };
Family family(MaterialKind k) noexcept {
    switch(k) {
    case MaterialKind::metal:return {{{"player/pl_metal1.wav","player/pl_metal2.wav"}},2,.9F,.3F};
    case MaterialKind::dirt:return {{{"player/pl_dirt1.wav","player/pl_dirt2.wav","player/pl_dirt3.wav"}},3,.9F,.1F};
    case MaterialKind::vent:return {{{"player/pl_duct1.wav"}},1,.5F,.3F};
    case MaterialKind::grate:return {{{"player/pl_grate1.wav","player/pl_grate4.wav"}},2,.9F,.5F};
    case MaterialKind::tile:return {{{"player/pl_tile1.wav","player/pl_tile2.wav","player/pl_tile3.wav","player/pl_tile4.wav"}},4,.8F,.2F};
    case MaterialKind::slosh:return {{{"player/pl_slosh1.wav","player/pl_slosh2.wav","player/pl_slosh3.wav","player/pl_slosh4.wav"}},4,.9F,0};
    case MaterialKind::wood:return {{{"debris/wood1.wav","debris/wood2.wav","debris/wood3.wav"}},3,.9F,.2F};
    case MaterialKind::computer: case MaterialKind::glass:
        return {{{"debris/glass1.wav","debris/glass2.wav","debris/glass3.wav"}},3,.8F,.2F};
    // The pinned SDK has no snow impact case. Static BSP flesh is not a
    // supported entity hit and is intentionally treated as concrete here.
    default:return {{{"player/pl_step1.wav","player/pl_step2.wav"}},2,.9F,.6F};
    }
}
}
void HalfLifeMaterials::reset() noexcept { count_=0;status_=MaterialTableStatus::missing; }
std::optional<std::array<char,13>> HalfLifeMaterials::normalize(
    std::string_view name) noexcept {
    if(name.empty() || name.size()>32) return {};
    if(name.front()=='-' || name.front()=='+') {
        if(name.size()<3) return {};
        name.remove_prefix(2);
    }
    if(!name.empty() && (name.front()=='{' || name.front()=='!' ||
        name.front()=='~' || name.front()==' ')) name.remove_prefix(1);
    if(name.empty()) return {};
    std::array<char,13> key{};
    for(std::size_t i=0;i<std::min<std::size_t>(12,name.size());++i) {
        if(name[i]<'!' || name[i]>'~') return {};
        key[i]=upper(name[i]);
    }
    return key;
}
MaterialTableStatus HalfLifeMaterials::configure(std::string_view text) noexcept {
    reset();
    if(text.empty()) return status_;
    if(text.size()>128U*1024U) return status_=MaterialTableStatus::limit_exceeded;
    while(!text.empty()) {
        const auto eol=text.find('\n');
        auto line=text.substr(0,eol);
        text=eol==text.npos ? std::string_view{} : text.substr(eol+1);
        if(line.size()>511) { reset();return status_=MaterialTableStatus::limit_exceeded; }
        while(!line.empty() && space(line.front())) line.remove_prefix(1);
        if(line.empty() || line.front()=='/') continue;
        if(line.size()<2 || !kind(line.front()) || !space(line[1])) {
            reset();return status_=MaterialTableStatus::malformed;
        }
        const auto value=*kind(line.front());line.remove_prefix(1);
        while(!line.empty() && space(line.front())) line.remove_prefix(1);
        const auto end=line.find_first_of(" \t\r\n");
        const auto token=line.substr(0,end);
        if(token.empty() || token.front()=='/') {reset();return status_=MaterialTableStatus::malformed;}
        // Table entries are raw SDK names; animated/special stripping is
        // applied only to the traced texture before lookup.
        std::array<char,13> key{};
        for(std::size_t i=0;i<std::min<std::size_t>(12,token.size());++i) {
            if(token[i]<'!' || token[i]>'~') {reset();return status_=MaterialTableStatus::malformed;}
            key[i]=upper(token[i]);
        }
        const bool duplicate=std::any_of(entries_.begin(),entries_.begin()+count_,
            [&](const Entry& entry){return entry.key==key;});
        // The SDK's duplicate sort order is unspecified. First normalized
        // entry wins, including the installed table's occasional duplicates.
        if(duplicate) continue;
        if(count_==entries_.size()) {reset();return status_=MaterialTableStatus::limit_exceeded;}
        entries_[count_++]={key,value};
    }
    return status_=MaterialTableStatus::ready;
}
MaterialLookup HalfLifeMaterials::lookup(std::string_view texture) const noexcept {
    MaterialLookup out;
    if(const auto key=normalize(texture)) out.key=*key;
    if(status_!=MaterialTableStatus::ready) {
        out.source=status_==MaterialTableStatus::missing ?
            MaterialSource::missing_table_concrete_fallback :
            MaterialSource::malformed_table_concrete_fallback;
        return out;
    }
    for(std::size_t i=0;i<count_;++i) if(entries_[i].key==out.key) {
        out.kind=entries_[i].kind;out.source=MaterialSource::table_match;return out;
    }
    out.source=MaterialSource::unknown_concrete_fallback;
    return out;
}
char material_code(MaterialKind kind) noexcept {
    switch(kind) {
    case MaterialKind::metal:return 'M';case MaterialKind::dirt:return 'D';
    case MaterialKind::vent:return 'V';case MaterialKind::grate:return 'G';
    case MaterialKind::tile:return 'T';case MaterialKind::slosh:return 'S';
    case MaterialKind::wood:return 'W';case MaterialKind::computer:return 'P';
    case MaterialKind::glass:return 'Y';case MaterialKind::flesh:return 'F';
    case MaterialKind::snow:return 'N';default:return 'C';
    }
}
std::string_view material_name(MaterialKind kind) noexcept {
    switch(kind) {
    case MaterialKind::metal:return "metal";case MaterialKind::dirt:return "dirt";
    case MaterialKind::vent:return "vent";case MaterialKind::grate:return "grate";
    case MaterialKind::tile:return "tile";case MaterialKind::slosh:return "slosh";
    case MaterialKind::wood:return "wood";case MaterialKind::computer:return "computer";
    case MaterialKind::glass:return "glass";case MaterialKind::flesh:return "flesh";
    case MaterialKind::snow:return "snow";default:return "concrete";
    }
}
std::string_view material_source_name(MaterialSource source) noexcept {
    switch(source) {
    case MaterialSource::table_match:return "table_match";
    case MaterialSource::unknown_concrete_fallback:return "unknown_concrete_fallback";
    case MaterialSource::missing_table_concrete_fallback:return "missing_table_concrete_fallback";
    default:return "malformed_table_concrete_fallback";
    }
}
MaterialImpactSound material_impact_sound(MaterialKind kind,
    const game_api::LocalWeaponActionIdentity& action,bool) noexcept {
    const auto f=family(kind);
    const auto index=(action.command_sequence ? action.command_sequence-1U : 0U)%f.count;
    return {reference(f.names[index]),f.volume};
}
float crowbar_strike_volume(MaterialKind kind) noexcept { return family(kind).bar; }
std::array<game_api::LocalSoundReference,24> material_impact_preload() noexcept {
    std::array<game_api::LocalSoundReference,24> out{};
    std::size_t n=0;
    for(const auto kind:{MaterialKind::concrete,MaterialKind::metal,MaterialKind::dirt,
        MaterialKind::vent,MaterialKind::grate,MaterialKind::tile,MaterialKind::slosh,
        MaterialKind::wood,MaterialKind::glass}) {
        const auto f=family(kind);
        for(std::size_t i=0;i<f.count;++i) out[n++]=reference(f.names[i]);
    }
    return out;
}
std::optional<game_api::LocalSoundReference> glock_ricochet_sound(
    const game_api::LocalWeaponActionIdentity& action) noexcept {
    // The SDK draws a 15-bit random value and emits ric1..5 below its
    // half-range threshold. We preserve that frequency/family but use an
    // independent action-stable hash, not its mutable client RNG stream.
    std::uint32_t value=action.command_sequence+
        static_cast<std::uint32_t>(action.generation)*5U;
    value^=value>>16U;
    value*=0x7feb352dU;
    value^=value>>15U;
    value*=0x846ca68bU;
    value^=value>>16U;
    const auto roll=(value>>17U)&0x7fffU;
    if(roll>=0x7fffU/2U) return {};
    return glock_ricochet_preload()[roll%5U];
}
std::array<game_api::LocalSoundReference,5> glock_ricochet_preload() noexcept {
    return {reference("weapons/ric1.wav"),reference("weapons/ric2.wav"),
        reference("weapons/ric3.wav"),reference("weapons/ric4.wav"),
        reference("weapons/ric5.wav")};
}
} // namespace hlclient::games::halflife
