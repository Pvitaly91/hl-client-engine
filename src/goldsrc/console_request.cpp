#include <hlclient/goldsrc/console_request.hpp>

#include <algorithm>
#include <utility>

namespace hlclient::goldsrc {

ClientMessageBuildResult encode_console_server_command(
    const ConsoleServerCommandRequest& request)
{
    const auto reject=[] {
        return ClientMessageBuildResult{{},ClientMessageError{
            ClientMessageErrorCode::unsupported_command_variant,0U,
            "Console request is outside the bounded typed server-command profile"}};
    };
    std::string command;
    switch(request.kind) {
    case ConsoleServerCommandKind::say:
    case ConsoleServerCommandKind::say_team: {
        if(request.argument.empty() || request.argument.size()>120U)return reject();
        bool visible=false;
        for(const unsigned char c:request.argument) {
            if(c<32U || c>126U || c=='"' || c=='\\' || c==';')return reject();
            visible|=c!=' ';
        }
        if(!visible)return reject();
        command=request.kind==ConsoleServerCommandKind::say?"say \"":"say_team \"";
        command+=request.argument;command+='"';
        break;
    }
    case ConsoleServerCommandKind::status:
        if(!request.argument.empty())return reject();
        command="status";break;
    case ConsoleServerCommandKind::kill:
        if(!request.argument.empty())return reject();
        command="kill";break;
    case ConsoleServerCommandKind::drop:
        command="drop";
        if(!request.argument.empty()) {
            if(request.argument.size()>63U || !request.argument.starts_with("weapon_") ||
                request.argument.size()<=7U || !std::ranges::all_of(request.argument,[](unsigned char c) {
                    return (c>='a' && c<='z') || (c>='A' && c<='Z') ||
                        (c>='0' && c<='9') || c=='_';
                }))return reject();
            command+=' ';command+=request.argument;
        }
        break;
    default:return reject();
    }
    std::vector<std::byte> bytes;
    bytes.reserve(command.size()+2U);
    bytes.push_back(std::byte{static_cast<std::uint8_t>(ClientMessageOpcode::string_command)});
    for(const unsigned char c:command)bytes.push_back(std::byte{c});
    bytes.push_back(std::byte{0U});
    return {std::move(bytes),{}};
}

} // namespace hlclient::goldsrc
