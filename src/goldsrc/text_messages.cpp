#include <hlclient/goldsrc/text_messages.hpp>
#include <algorithm>
#include <charconv>
#include <set>
#include <string_view>

namespace hlclient::goldsrc {
std::optional<game_api::TextMessageCatalogue> parse_text_message_catalogue(std::span<const std::byte> bytes) {
    if(bytes.size()>game_api::kMaximumTextMessageBytes)return {};
    std::string_view input{reinterpret_cast<const char*>(bytes.data()),bytes.size()};
    if(input.find('\0')!=input.npos)return {};
    if(input.starts_with("\xef\xbb\xbf"))input.remove_prefix(3);
    const auto trim=[](std::string_view s) {
        while(!s.empty() && (s.front()==' ' || s.front()=='\t'))s.remove_prefix(1);
        while(!s.empty() && (s.back()==' ' || s.back()=='\t'))s.remove_suffix(1);
        return s;
    };
    game_api::TextMessageCatalogue result;std::set<std::string> keys;
    std::string key,text;std::optional<game_api::TextDestination> destination;
    bool body{};std::size_t lines{};
    while(!input.empty()) {
        if(++lines>16384U)return {};
        const auto end=input.find_first_of("\r\n");const auto raw=input.substr(0,end);
        if(raw.size()>4096U)return {};
        if(end==input.npos)input={};
        else {const char first=input[end];input.remove_prefix(end+1U);if(first=='\r' && input.starts_with('\n'))input.remove_prefix(1);}
        const auto line=trim(raw);
        if(body) {
            if(line=="}") {
                if(result.size()>=game_api::kMaximumTextMessageEntries || !keys.insert(key).second)return {};
                result.push_back({std::move(key),std::move(text),destination});key.clear();text.clear();body=false;
            } else {
                if(text.size()+raw.size()+1U>4096U)return {};
                text.append(raw);text+='\n';
            }
            continue;
        }
        if(line.empty() || line.starts_with("//"))continue;
        if(line.starts_with('$')) {
            if(!key.empty())return {};
            if(line.starts_with("$effect") && line.size()>7U && (line[7]==' ' || line[7]=='\t')) {
                auto value=trim(line.substr(7));const auto comment=value.find("//");value=trim(value.substr(0,comment));
                int effect{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),effect);
                if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size())return {};
                destination.reset();if(effect<0) {
                    if(effect < -4)return {};
                    destination=static_cast<game_api::TextDestination>(-effect);
                }
            }
            continue;
        }
        if(line=="{") {if(key.empty())return {};body=true;continue;}
        if(!key.empty() || line.size()>63U || std::any_of(line.begin(),line.end(),[](unsigned char c) {
            return !((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-');
        }))return {};
        key=std::string{line};
    }
    if(body || !key.empty())return {};
    return result;
}
}
