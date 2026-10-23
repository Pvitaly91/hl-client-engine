#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <windows.h>

namespace hlclient::platform::windows {

// Opt-in is a fixed marker beside this executable. It cannot supply arbitrary
// paths or credentials. The same rule is used by launcher, client and console.
[[nodiscard]] inline std::optional<std::filesystem::path> shared_test_storage_root() noexcept
{
    try {
        std::array<wchar_t,32768> module{};
        const auto count=::GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));
        if(!count || count>=module.size())return {};
        const auto package=std::filesystem::path{std::wstring{module.data(),count}}.parent_path();
        const auto marker=package/L"HLClientSharedTest.txt";
        const auto file=::CreateFileW(marker.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
            OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE)return {};
        BY_HANDLE_FILE_INFORMATION info{};std::array<char,64> bytes{};DWORD read{};
        const bool ordinary=::GetFileType(file)==FILE_TYPE_DISK && ::GetFileInformationByHandle(file,&info) &&
            info.nNumberOfLinks==1 && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
            info.nFileSizeHigh==0 && info.nFileSizeLow<bytes.size();
        const bool loaded=ordinary && ::ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) && read==info.nFileSizeLow;
        ::CloseHandle(file);
        const std::string_view value{bytes.data(),read};
        if(!loaded || (value!="hlclient-shared-test-v1\n" && value!="hlclient-shared-test-v1\r\n"))return {};
        std::array<wchar_t,256> computer{},user{};
        DWORD computer_size=static_cast<DWORD>(computer.size()),user_size=static_cast<DWORD>(user.size());
        if(!::GetComputerNameW(computer.data(),&computer_size) || !::GetUserNameW(user.data(),&user_size))return {};
        const auto safe=[](std::wstring_view text){
            std::wstring result;
            for(const auto c:text.substr(0,48))result+=(c>=L'A'&&c<=L'Z') || (c>=L'a'&&c<=L'z') ||
                (c>=L'0'&&c<=L'9') || c==L'-' || c==L'_' ? c : L'_';
            return result.empty()?std::wstring{L"unknown"}:result;
        };
        // Preserve uniqueness when different Unicode names sanitize alike.
        std::uint32_t identity=2166136261U;
        for(const auto* name:{computer.data(),user.data()}) {
            for(const auto* p=name;*p;++p){identity^=static_cast<std::uint32_t>(*p);identity*=16777619U;}
            identity^=0U;identity*=16777619U;
        }
        constexpr wchar_t hex[]=L"0123456789abcdef";std::wstring suffix(8,L'0');
        for(unsigned i=0;i<8;++i)suffix[7-i]=hex[(identity>>(i*4))&15U];
        return package/L"test-data"/(safe(computer.data())+L"-"+safe(user.data())+L"-"+suffix);
    }catch(...){return {};}
}
} // namespace hlclient::platform::windows
