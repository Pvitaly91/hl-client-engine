#pragma once
#include <initializer_list>
#include <string_view>

namespace hlclient::core {
// Shared conservative privacy policy for console history, saved scripts and
// logging. Never retain a credential merely because its separator differs.
[[nodiscard]] inline bool contains_sensitive_console_text(std::string_view text) noexcept {
    const auto folded=[](char c){return c>='A' && c<='Z' ? static_cast<char>(c+('a'-'A')) : c;};
    const auto contains=[&](std::string_view marker,bool separator) {
        for(std::size_t start=0;start+marker.size()<=text.size();++start) {
            std::size_t n=0;while(n<marker.size() && folded(text[start+n])==marker[n])++n;
            if(n!=marker.size())continue;
            if(!separator)return true;
            const auto end=start+marker.size();
            if(end==text.size())return true;
            const auto next=static_cast<unsigned char>(text[end]);
            if(next<=32U || next=='=' || next==':' || next=='\"' || next=='\'')return true;
        }
        return false;
    };
    for(const std::string_view marker:{"password","passwd","auth_ticket","authticket","raw_userinfo","access_token"})
        if(contains(marker,false))return true;
    return contains("token",true) || contains("rcon",true);
}
}
