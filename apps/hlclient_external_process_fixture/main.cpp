#include <windows.h>
#include <cstdint>
#include <cwchar>
#include <cstdio>
#include <string>
#include <string_view>

// Offline child only: no sockets, Steam, assets, game module or server code.
int wmain(int argc,wchar_t**argv)
{
    HANDLE cancel{};bool wait{},recover_and_close{};unsigned exit_code{};
    for(int i=1;i<argc;++i){const std::wstring_view arg{argv[i]};
        if(arg==L"--external-cancel-event" && i+1<argc)cancel=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::wcstoull(argv[++i],nullptr,10)));
        else if(arg==L"--wait")wait=true;else if(arg==L"--fail")exit_code=7;
        else if(arg==L"--recover-and-close")recover_and_close=true;else if(arg!=L"--help")return 9;}
    wchar_t app_id[8]{},game[8]{};
    if(!::GetEnvironmentVariableW(L"SteamAppId",app_id,8) || !::GetEnvironmentVariableW(L"SteamGameId",game,8) || std::wstring_view{app_id}!=L"70" || std::wstring_view{game}!=L"70")return 10;
    std::puts("[external-status] stage=connecting reason=fixture");std::fflush(stdout);
    if(recover_and_close){
        std::puts("[external-status] stage=error reason=server_unavailable");
        std::puts("[external-status] stage=recoverable_error reason=session_failed");
        std::puts("[external-status] stage=connecting reason=fixture");
        std::puts("[external-status] stage=in_game reason=fixture");
        std::puts("[external-status] stage=completed reason=application_closed");
        return static_cast<int>(exit_code);
    }
    if(wait && (!cancel || ::WaitForSingleObject(cancel,5000)!=WAIT_OBJECT_0))return 8;
    std::puts("[external-status] stage=completed reason=fixture");return static_cast<int>(exit_code);
}
