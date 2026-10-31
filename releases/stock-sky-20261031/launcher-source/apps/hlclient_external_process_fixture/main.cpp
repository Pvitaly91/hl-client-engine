#include <windows.h>
#include <cstdint>
#include <cwchar>
#include <cstdio>
#include <string>
#include <string_view>

// Offline child only: no sockets, Steam, assets, game module or server code.
int wmain(int argc,wchar_t**argv)
{
    HANDLE cancel{};bool wait{},recover_and_close{},expected_runtime_off{};unsigned exit_code{};std::wstring_view expected_sky;
    for(int i=1;i<argc;++i){const std::wstring_view arg{argv[i]};
        if(arg==L"--external-cancel-event" && i+1<argc)cancel=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::wcstoull(argv[++i],nullptr,10)));
        else if(arg==L"--wait")wait=true;else if(arg==L"--fail")exit_code=7;
        else if(arg==L"--recover-and-close")recover_and_close=true;
        else if(arg==L"--expect-runtime-off")expected_runtime_off=true;
        else if(arg==L"--expect-sky-policy" && i+1<argc)expected_sky=argv[++i];else if(arg!=L"--help")return 9;}
    wchar_t app_id[8]{},game[8]{};
    if(!::GetEnvironmentVariableW(L"SteamAppId",app_id,8) || !::GetEnvironmentVariableW(L"SteamGameId",game,8) || std::wstring_view{app_id}!=L"70" || std::wstring_view{game}!=L"70")return 10;
    if(expected_runtime_off){wchar_t value[16]{};if(::GetEnvironmentVariableW(L"HLC_RUNTIME_TEXT_POLICY",value,16)!=3 || std::wstring_view{value}!=L"off")return 13;std::puts("fixture_runtime_text_off_verified=1");}
    if(!expected_sky.empty()){
        if(expected_sky!=L"original" && expected_sky!=L"depth" && expected_sky!=L"no_background" && expected_sky!=L"no_sky_color" && expected_sky!=L"world_sky_non_occluding" && expected_sky!=L"inherited-test-invalid" && expected_sky!=L"__unset__")return 12;
        wchar_t sky[64]{};::SetLastError(ERROR_SUCCESS);
        const DWORD count=::GetEnvironmentVariableW(L"HLC_BACKGROUND_SKY_POLICY",sky,64);
        if(expected_sky==L"__unset__"){
            if(count!=0 || ::GetLastError()!=ERROR_ENVVAR_NOT_FOUND)return 11;
        }else if(count==0 || count>=64 || std::wstring_view{sky,count}!=expected_sky)return 11;
        std::puts("fixture_sky_policy_verified=1");
    }
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


