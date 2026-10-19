#include <hlclient/console/console.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <system_error>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hlclient::console {
namespace {
bool reparse(const std::filesystem::path& path)
{
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
#else
    std::error_code ec;return std::filesystem::is_symlink(std::filesystem::symlink_status(path,ec));
#endif
}
bool safe_components(const std::filesystem::path& path)
{
    std::filesystem::path part;
    for(const auto& component:path){part/=component;if(reparse(part))return false;}
    return true;
}
#ifdef _WIN32
// Denying FILE_SHARE_DELETE on every existing directory prevents a junction
// swap/rename between validation and file operations in this scope.
struct ScopeLock {
    std::vector<HANDLE> handles;
    bool ready{};
    explicit ScopeLock(const std::filesystem::path& root,const bool create=false)
    {
        std::filesystem::path part;
        for(const auto& component:root) {
            part/=component;if(!part.is_absolute())continue;
            auto h=CreateFileW(part.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            const auto open_error=GetLastError();
            if(h==INVALID_HANDLE_VALUE && create && (open_error==ERROR_FILE_NOT_FOUND || open_error==ERROR_PATH_NOT_FOUND)) {
                if(!CreateDirectoryW(part.c_str(),nullptr))return;
                h=CreateFileW(part.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            }
            if(h==INVALID_HANDLE_VALUE)return;
            handles.push_back(h);BY_HANDLE_FILE_INFORMATION info{};
            if(!GetFileInformationByHandle(h,&info) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0 || (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==0)return;
        }
        ready=true;
    }
    ~ScopeLock(){for(const auto h:handles)CloseHandle(h);}
};
#endif
}
ConsoleConfigStore::ConsoleConfigStore(std::filesystem::path root):root_{std::move(root)}{}
const std::filesystem::path& ConsoleConfigStore::root()const noexcept{return root_;}
std::optional<std::filesystem::path> ConsoleConfigStore::checked_path(std::string_view name,std::string& error)const
{
    if(name.empty() || name.size()>64 || !name.ends_with(".cfg") || name.starts_with('.') || !std::all_of(name.begin(),name.end(),[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';}) || name.find("..")!=name.npos) {
        error="exec/config accepts only a safe .cfg basename in the user console directory";return {};
    }
    std::string base{name.substr(0,name.find('.'))};
    for(auto& c:base)if(c>='a'&&c<='z')c=static_cast<char>(c-('a'-'A'));
    if(base=="CON" || base=="PRN" || base=="AUX" || base=="NUL" || (base.size()==4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3]>='1' && base[3]<='9')) {error="Reserved device config basename";return {};}
    if(!root_.is_absolute() || root_!=root_.lexically_normal() || !safe_components(root_)) {error="Config root is unsafe or contains a reparse point";return {};}
    const auto path=root_/std::filesystem::path{std::string{name}};
    if(reparse(path)){error="Config file reparse points are forbidden";return {};}
    return path;
}
std::optional<std::string> ConsoleConfigStore::read(std::string_view name,std::string& error)const
{
    try {
        const auto path=checked_path(name,error);if(!path)return {};
#ifdef _WIN32
        ScopeLock scope{root_};if(!scope.ready){error="Config directory is unavailable or unsafe";return {};}
        const auto file=CreateFileW(path->c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE){error="Cannot read user config: "+std::string{name};return {};}
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
        if(!GetFileInformationByHandle(file,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))!=0 || !GetFileSizeEx(file,&size) || size.QuadPart<0 || size.QuadPart>65536) {CloseHandle(file);error="Config file is unsafe or exceeds 65536 bytes";return {};}
        std::string text(static_cast<std::size_t>(size.QuadPart),'\0');DWORD count{};
        const bool ok=ReadFile(file,text.data(),static_cast<DWORD>(text.size()),&count,nullptr)!=0 && count==text.size();CloseHandle(file);
        if(!ok || !valid_utf8(text)){error="Config read or UTF-8 validation failed";return {};}
        return text;
#else
        std::ifstream stream{*path,std::ios::binary};if(!stream){error="Cannot read user config: "+std::string{name};return {};}
        std::string text;std::array<char,4096> block{};
        while(stream){stream.read(block.data(),block.size());text.append(block.data(),static_cast<std::size_t>(stream.gcount()));if(text.size()>65536){error="Config file exceeds 65536 bytes";return {};}}
        if(!valid_utf8(text)){error="Config UTF-8 validation failed";return {};}return text;
#endif
    }catch(...){error="Config read failed";return {};}
}
bool ConsoleConfigStore::write(std::string_view name,std::string_view text,std::string& error)const
{
    try {
        if(text.size()>65536-32 || !valid_utf8(text)){error="Config size or UTF-8 validation failed";return false;}
        const auto path=checked_path(name,error);if(!path)return false;
        std::error_code ec;
        static std::atomic<unsigned> sequence{};
#ifdef _WIN32
        ScopeLock scope{root_,true};if(!scope.ready){error="Config directory is unsafe";return false;}
        const auto temp=root_/(L".console-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence)+L".tmp");
        const auto file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE){error="Cannot create config transaction";return false;}
        const std::string data="// hlclient-console-schema 1\n"+std::string{text};DWORD count{};
        const bool written=WriteFile(file,data.data(),static_cast<DWORD>(data.size()),&count,nullptr)!=0 && count==data.size() && FlushFileBuffers(file)!=0;
        CloseHandle(file);
        if(!written || reparse(*path) || !MoveFileExW(temp.c_str(),path->c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {DeleteFileW(temp.c_str());error="Atomic config write failed";return false;}
#else
        std::filesystem::create_directories(root_,ec);if(ec || !safe_components(root_)){error="Cannot create safe user config directory";return false;}
        const auto temp=root_/(".console-"+std::to_string(++sequence)+".tmp");
        if(std::filesystem::exists(temp)){error="Config transaction already exists";return false;}
        {std::ofstream file{temp,std::ios::binary};file<<"// hlclient-console-schema 1\n"<<text;file.flush();if(!file){error="Config write failed";std::filesystem::remove(temp,ec);return false;}}
        std::filesystem::rename(temp,*path,ec);if(ec){std::filesystem::remove(temp,ec);error="Atomic config rename failed";return false;}
#endif
        return true;
    }catch(...){error="Config write failed";return false;}
}
} // namespace hlclient::console
