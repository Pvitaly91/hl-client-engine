#pragma once

#include <hlclient/hash/sha256.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>
#include <shlobj.h>

namespace hlclient::launcher {

// Small, always-on startup evidence. This does not enable gameplay sampling or
// system collectors. Every record is flushed so an early child failure leaves
// something useful even when it never creates a session receipt.
class StartupJournal final {
public:
    static constexpr std::size_t maximum_journal_bytes=64U*1024U;
    static constexpr std::size_t maximum_artifact_bytes=128U*1024U;

    explicit StartupJournal(const std::filesystem::path& root={}) noexcept
    {
        try {
            SYSTEMTIME now{};::GetSystemTime(&now);
            std::array<wchar_t,128> id{};
            static_cast<void>(::swprintf_s(id.data(),id.size(),
                L"%04u%02u%02uT%02u%02u%02uZ-%lu-%llu",now.wYear,now.wMonth,now.wDay,
                now.wHour,now.wMinute,now.wSecond,::GetCurrentProcessId(),
                static_cast<unsigned long long>(::GetTickCount64())));
            id_=id.data();
            PWSTR local{};
            if(!root.empty())static_cast<void>(open_at(root));
            if(root.empty() && SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&local))) {
                const std::filesystem::path base{local};::CoTaskMemFree(local);local=nullptr;
                static_cast<void>(open_at(base/L"HLClientEngine"/L"startup"));
            }
            if(handle_==INVALID_HANDLE_VALUE) {
                std::array<wchar_t,32768> temp{};
                const auto count=::GetTempPathW(static_cast<DWORD>(temp.size()),temp.data());
                if(count!=0 && count<temp.size())static_cast<void>(open_at(
                    std::filesystem::path{temp.data()}/L"HLClientEngine"/L"startup"));
            }
            record("launcher_entry");
            record("startup_logging","always_on_bounded_no_gameplay_sampling");
            number("launcher_pointer_bits",sizeof(void*)*8U);
            number("launcher_process_id",::GetCurrentProcessId());
#ifdef _MSC_VER
            number("msvc_compiler",_MSC_VER);
#endif
            write_line("build=" __DATE__ " " __TIME__);
            SYSTEM_INFO system{};::GetNativeSystemInfo(&system);
            number("native_processor_architecture",system.wProcessorArchitecture);
            number("logical_processors",system.dwNumberOfProcessors);
            number("sse2_available",::IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE)?1U:0U);
            // GetVersionEx lies for executables without a current OS manifest.
            // Resolve the read-only ntdll version query dynamically instead.
            using VersionFunction=LONG (WINAPI*)(OSVERSIONINFOW*);
            const auto ntdll=::GetModuleHandleW(L"ntdll.dll");
            const auto version=ntdll?reinterpret_cast<VersionFunction>(::GetProcAddress(ntdll,"RtlGetVersion")):nullptr;
            OSVERSIONINFOW os{};os.dwOSVersionInfoSize=sizeof(os);
            if(version && version(&os)==0) {
                number("windows_major",os.dwMajorVersion);number("windows_minor",os.dwMinorVersion);
                number("windows_build",os.dwBuildNumber);
            } else record("windows_version","unavailable");
            std::array<wchar_t,32768> executable{};
            const auto count=::GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
            if(count!=0 && count<executable.size())package_=std::filesystem::path{
                std::wstring{executable.data(),count}}.parent_path();
        } catch(...) { /* Failure to log must never prevent the launcher. */ }
    }
    ~StartupJournal(){if(handle_!=INVALID_HANDLE_VALUE)::CloseHandle(handle_);}
    StartupJournal(const StartupJournal&)=delete;
    StartupJournal&operator=(const StartupJournal&)=delete;

    [[nodiscard]] const std::filesystem::path& directory() const noexcept{return directory_;}
    [[nodiscard]] const std::filesystem::path& path() const noexcept{return path_;}
    [[nodiscard]] bool available() const noexcept{return handle_!=INVALID_HANDLE_VALUE;}

    // Callers pass only fixed local stage/reason tokens, never command lines,
    // endpoints, configuration, server text, or exception.what() indiscriminately.
    void record(std::string_view stage,std::string_view reason={},DWORD native_error=0) noexcept
    {
        try {
            std::string line="stage="+token(stage);
            if(!reason.empty())line+=" reason="+token(reason);
            if(native_error) {
                line+=" native_error="+std::to_string(native_error);
                line+=" native_hex="+hex(native_error);
                std::array<wchar_t,1024> message{};
                const auto count=::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr,native_error,0,message.data(),static_cast<DWORD>(message.size()),nullptr);
                if(count)line+=" native_message="+single_line(utf8(std::wstring_view{message.data(),count}),1024);
                line+=" hint="+std::string{native_hint(native_error)};
            }
            write_line(line);
        }catch(...){}
    }
    void number(std::string_view key,std::uint64_t value) noexcept
    {try{write_line(token(key)+"="+std::to_string(value));}catch(...) {}}

    void client_exit(DWORD code) noexcept
    {
        const auto hint=code==1U?std::string_view{"client_startup_or_application_failure_see_client_startup_log"}:
            code==2U?std::string_view{"client_arguments_or_configuration_rejected_see_client_startup_log"}:native_hint(code);
        try {write_line("client_exit="+std::to_string(code)+" client_exit_hex="+hex(code)+
            " hint="+std::string{hint});}catch(...){}
    }

    // Fixed package leaves only. No Steam files, configuration, environment or
    // directory enumeration enter startup logs. The small binaries are hashed
    // once per launcher start, before any optional diagnostic helper is used.
    void observe_package() noexcept
    {
        try {
            if(package_.empty()){record("package_identity","launcher_path_unavailable");return;}
            for(const auto* leaf:{L"HLClientLauncher.exe",L"hlclient.exe",L"SDL3.dll",
                L"HLClientSystemDiagnostics.exe",L"msvcp140.dll",L"msvcp140_atomic_wait.dll",
                L"msvcp140_2.dll",L"vcruntime140.dll"})observe_file(package_/leaf);
        }catch(...){record("package_identity","observation_exception");}
    }

    // The only free-text child input has an explicit startup/error allowlist.
    // Normal server/chat output and sensitive authentication lines are dropped.
    void capture_client_output(std::string_view bytes) noexcept
    {
        try {
            std::string output="HL Client startup/error output (bounded and filtered)\r\n";
            std::size_t kept{},omitted{};
            while(!bytes.empty() && output.size()<maximum_artifact_bytes-2048U) {
                const auto newline=bytes.find('\n');
                const auto line=bytes.substr(0,std::min(newline,bytes.size()));
                if(is_startup_error(line)) {
                    output+=single_line(line,2048);output+="\r\n";++kept;
                }else if(!line.empty())++omitted;
                if(newline==std::string_view::npos){bytes={};break;}
                bytes.remove_prefix(newline+1);
            }
            output+="retained_startup_lines="+std::to_string(kept)+
                " omitted_other_lines="+std::to_string(omitted)+"\r\n";
            if(!bytes.empty())output+="input_truncated=true\r\n";
            static_cast<void>(write_artifact("client-startup.log",output));
            record("child_output_saved",kept?"startup_errors_retained":"no_startup_error_lines");
        }catch(...){record("child_output_saved","unavailable");}
    }

    // Restrict exportable auxiliary evidence to known, small local leaves.
    [[nodiscard]] bool write_artifact(std::string_view leaf,std::string_view bytes) noexcept
    {
        try {
            if(directory_.empty() || !artifact_leaf(leaf) || bytes.size()>maximum_artifact_bytes)return false;
            std::scoped_lock lock{artifacts_mutex_};
            const auto file=directory_/std::filesystem::path{std::string{leaf}};
            const auto output=::CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(output==INVALID_HANDLE_VALUE)return false;
            BY_HANDLE_FILE_INFORMATION info{};
            const bool regular=::GetFileType(output)==FILE_TYPE_DISK && ::GetFileInformationByHandle(output,&info) &&
                (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))==0 && info.nNumberOfLinks==1;
            DWORD count{};
            const bool written=regular && ::SetFilePointer(output,0,nullptr,FILE_BEGIN)!=INVALID_SET_FILE_POINTER &&
                ::SetEndOfFile(output) && ::WriteFile(output,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr) &&
                count==bytes.size() && ::FlushFileBuffers(output);
            ::CloseHandle(output);return written;
        }catch(...){return false;}
    }

    // Built-in ZIP (stored entries) avoids requiring PowerShell or an archiver
    // on the target PC. It exports only this journal and its allowlisted leaves.
    [[nodiscard]] std::filesystem::path export_zip() noexcept
    {
        try {
            if(directory_.empty())return {};
            record("startup_export","requested");
            // A UI export and a worker's final capture may coincide. Take one
            // complete capture snapshot instead of racing its truncate/write.
            std::scoped_lock lock{artifacts_mutex_};
            struct Entry{std::string name,bytes;std::uint32_t crc{},offset{};};
            std::vector<Entry> entries;
            for(const auto* leaf:{"launcher-startup.log","client-startup.log","startup-summary.txt"}) {
                const auto bytes=read_file(directory_/leaf,maximum_artifact_bytes);
                if(bytes)entries.push_back(Entry{leaf,*bytes,crc32(*bytes),0});
            }
            if(entries.empty())return {};
            std::string zip;
            const auto u16=[&](std::uint16_t value){zip.push_back(static_cast<char>(value&255U));zip.push_back(static_cast<char>((value>>8U)&255U));};
            const auto u32=[&](std::uint32_t value){u16(static_cast<std::uint16_t>(value&65535U));u16(static_cast<std::uint16_t>(value>>16U));};
            for(auto& e:entries) {
                e.offset=static_cast<std::uint32_t>(zip.size());
                u32(0x04034b50U);u16(20);u16(0);u16(0);u16(0);u16(33);u32(e.crc);
                u32(static_cast<std::uint32_t>(e.bytes.size()));u32(static_cast<std::uint32_t>(e.bytes.size()));
                u16(static_cast<std::uint16_t>(e.name.size()));u16(0);zip+=e.name;zip+=e.bytes;
            }
            const auto central=static_cast<std::uint32_t>(zip.size());
            for(const auto& e:entries) {
                u32(0x02014b50U);u16(20);u16(20);u16(0);u16(0);u16(0);u16(33);u32(e.crc);
                u32(static_cast<std::uint32_t>(e.bytes.size()));u32(static_cast<std::uint32_t>(e.bytes.size()));
                u16(static_cast<std::uint16_t>(e.name.size()));u16(0);u16(0);u16(0);u16(0);u32(0);u32(e.offset);zip+=e.name;
            }
            const auto central_size=static_cast<std::uint32_t>(zip.size())-central;
            u32(0x06054b50U);u16(0);u16(0);u16(static_cast<std::uint16_t>(entries.size()));
            u16(static_cast<std::uint16_t>(entries.size()));u32(central_size);u32(central);u16(0);
            const auto file=directory_.parent_path()/(L"HLClient-startup-"+id_+L"-"+
                std::to_wstring(::GetTickCount64())+L".zip");
            const auto output=::CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(output==INVALID_HANDLE_VALUE){record("startup_export","open_failed",::GetLastError());return {};}
            DWORD count{};const bool written=::WriteFile(output,zip.data(),static_cast<DWORD>(zip.size()),&count,nullptr) &&
                count==zip.size() && ::FlushFileBuffers(output);
            const DWORD error=written?0: ::GetLastError();::CloseHandle(output);
            if(!written){record("startup_export","write_failed",error);return {};}
            record("startup_export","ready");return file;
        }catch(...){record("startup_export","exception");return {};}
    }

    [[nodiscard]] static bool is_startup_error(std::string_view line) noexcept
    {
        try {
            if(line.empty() || line.size()>8192U)return false;
            const auto lower=lowercase(line);
            for(const auto word:{"password","passwd","ticket","token","steamid","steam_id","userinfo",
                "setinfo","authorization","authentication","[chat]","[game]","[server]","rcon"})
                if(lower.find(word)!=std::string::npos)return false;
            if(lower.starts_with("[external-status]"))return true;
            if(lower.starts_with("[opengl-startup]"))return true;
            const bool fatal=lower.starts_with("[fatal]") || lower.starts_with("fatal:");
            const bool local_prefix=fatal || lower.starts_with("[error]") || lower.starts_with("[warning]") ||
                lower.starts_with("sdl ") || lower.starts_with("opengl ") || lower.starts_with("wgl ");
            if(!local_prefix)return false;
            const bool startup=lower.find("sdl")!=std::string::npos || lower.find("opengl")!=std::string::npos ||
                lower.find("wgl")!=std::string::npos || lower.find("renderer")!=std::string::npos ||
                lower.find("video")!=std::string::npos || lower.find("driver")!=std::string::npos ||
                lower.find("runtime")!=std::string::npos;
            const bool error=lower.find("failed")!=std::string::npos || lower.find("failure")!=std::string::npos ||
                lower.find("error")!=std::string::npos || lower.find("unavailable")!=std::string::npos ||
                lower.find("unsupported")!=std::string::npos || lower.find("exception")!=std::string::npos;
            return fatal || (startup && error);
        }catch(...){return false;}
    }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
    std::filesystem::path directory_,path_,package_;
    std::wstring id_;
    std::mutex mutex_,artifacts_mutex_;
    std::size_t written_{};
    bool limit_reported_{};

    [[nodiscard]] static bool artifact_leaf(std::string_view name) noexcept
    {return name=="client-startup.log" || name=="startup-summary.txt";}
    [[nodiscard]] static std::string lowercase(std::string_view value)
    {std::string text{value};for(auto& c:text)if(c>='A' && c<='Z')c=static_cast<char>(c-'A'+'a');return text;}
    [[nodiscard]] static std::string token(std::string_view text)
    {
        std::string result;result.reserve(std::min<std::size_t>(text.size(),160));
        for(const char c:text.substr(0,160))result.push_back((c>='a' && c<='z') || (c>='A' && c<='Z') ||
            (c>='0' && c<='9') || c=='_' || c=='-' || c=='.'?c:'_');return result;
    }
    [[nodiscard]] static std::string single_line(std::string_view text,std::size_t maximum)
    {
        std::string result;result.reserve(std::min(text.size(),maximum));
        for(const unsigned char c:text.substr(0,maximum))result.push_back(c<32U || c==127U?' ':static_cast<char>(c));
        return result;
    }
    [[nodiscard]] static std::string utf8(std::wstring_view text)
    {
        if(text.empty() || text.size()>32767U)return {};
        const int count=::WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
        if(count<=0)return {};std::string value(static_cast<std::size_t>(count),0);
        if(::WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),value.data(),count,nullptr,nullptr)!=count)return {};
        return value;
    }
    [[nodiscard]] static std::string hex(DWORD value)
    {std::array<char,16> text{};static_cast<void>(::sprintf_s(text.data(),text.size(),"0x%08lX",value));return text.data();}
    [[nodiscard]] static std::string_view native_hint(DWORD value) noexcept
    {
        switch(value) {
        case 0:return "normal_exit";
        case 2:case 3:return "required_file_or_directory_missing";
        case 5:return "access_denied";
        case 126:case 0xC0000135U:return "required_dll_missing";
        case 127:case 0xC0000139U:return "required_dll_entry_point_missing";
        case 193:case 0xC000007BU:return "binary_architecture_or_image_mismatch";
        case 1114:case 0xC0000142U:return "dll_initialization_failed";
        case 0xC0000005U:return "access_violation";
        case 0xC000001DU:return "unsupported_cpu_instruction";
        case 0xC00000FDU:return "stack_overflow";
        case 0xC0000409U:return "fail_fast_or_stack_buffer_failure";
        default:return "see_native_error_and_client_startup_log";
        }
    }
    [[nodiscard]] bool open_at(const std::filesystem::path& base)
    {
        if(!base.is_absolute())return false;
        std::error_code error;std::filesystem::create_directories(base,error);if(error)return false;
        const auto attributes=::GetFileAttributesW(base.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0)return false;
        const auto directory=base/id_;
        if(!::CreateDirectoryW(directory.c_str(),nullptr))return false;
        const auto file=directory/L"launcher-startup.log";
        const auto output=::CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(output==INVALID_HANDLE_VALUE)return false;
        handle_=output;directory_=directory;path_=file;return true;
    }
    void write_line(std::string_view line) noexcept
    {
        try {
            if(handle_==INVALID_HANDLE_VALUE)return;
            std::scoped_lock lock{mutex_};
            if(limit_reported_)return;
            SYSTEMTIME now{};::GetSystemTime(&now);
            std::array<char,64> time{};
            static_cast<void>(::sprintf_s(time.data(),time.size(),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ ",
                now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds));
            std::string bytes=time.data();bytes+=single_line(line,4096);bytes+="\r\n";
            if(written_+bytes.size()>maximum_journal_bytes) {
                bytes="startup_journal_limit_reached=true\r\n";limit_reported_=true;
                if(written_+bytes.size()>maximum_journal_bytes)return;
            }
            DWORD count{};
            if(::WriteFile(handle_,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr))written_+=count;
            static_cast<void>(::FlushFileBuffers(handle_));
        }catch(...){}
    }
    [[nodiscard]] static std::optional<std::string> read_file(const std::filesystem::path& path,std::size_t maximum)
    {
        const auto file=::CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if(file==INVALID_HANDLE_VALUE)return {};
        struct Close{HANDLE value;~Close(){::CloseHandle(value);}}close{file};
        BY_HANDLE_FILE_INFORMATION info{};
        if(::GetFileType(file)!=FILE_TYPE_DISK || !::GetFileInformationByHandle(file,&info) || info.nFileSizeHigh ||
            info.nFileSizeLow>maximum || info.nNumberOfLinks!=1 ||
            (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))!=0)return {};
        std::string bytes(info.nFileSizeLow,0);DWORD count{};
        if(!::ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr) || count!=bytes.size())return {};
        return bytes;
    }
    void observe_file(const std::filesystem::path& file)
    {
        const auto leaf=utf8(file.filename().wstring());
        const auto bytes=read_file(file,32U*1024U*1024U);
        if(!bytes){record("package_file_unavailable",leaf,::GetLastError());return;}
        std::string line="package_file="+leaf+" bytes="+std::to_string(bytes->size());
        if(const auto digest=hash::sha256(std::as_bytes(std::span{bytes->data(),bytes->size()})))
            line+=" sha256="+hash::sha256_hex(*digest);
        const auto read16=[&](std::size_t at)->std::uint16_t {
            return static_cast<std::uint16_t>(static_cast<std::uint16_t>(static_cast<unsigned char>((*bytes)[at])) |
                static_cast<std::uint16_t>(static_cast<unsigned char>((*bytes)[at+1]))<<8U);
        };
        const auto read32=[&](std::size_t at)->std::uint32_t {
            return static_cast<std::uint32_t>(read16(at)) | static_cast<std::uint32_t>(read16(at+2))<<16U;
        };
        if(bytes->size()>=64 && read16(0)==0x5a4dU) {
            const auto offset=static_cast<std::size_t>(read32(0x3c));
            if(offset<=bytes->size()-24 && read32(offset)==0x4550U)
                line+=" pe_machine="+std::to_string(read16(offset+4))+" pe_timestamp="+std::to_string(read32(offset+8));
        }
        write_line(line);
    }
    [[nodiscard]] static std::uint32_t crc32(std::string_view bytes) noexcept
    {
        std::uint32_t crc=0xffffffffU;
        for(const unsigned char value:bytes){crc^=value;for(int bit=0;bit<8;++bit)crc=(crc>>1U)^(0xedb88320U&
            static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc&1U)));}
        return ~crc;
    }
};

} // namespace hlclient::launcher
