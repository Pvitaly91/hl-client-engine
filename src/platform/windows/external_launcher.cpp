#include <hlclient/platform/windows/external_launcher.hpp>
#include <hlclient/platform/windows/secure_output.hpp>
#include <algorithm>
#include <array>
#include <cwchar>
#include <span>
#include <utility>
#include <thread>
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>

namespace hlclient::platform::windows {
namespace {
class Handle final {
public:
    explicit Handle(HANDLE h=nullptr):h_{h}{}~Handle(){reset();}
    Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;
    void reset(HANDLE h=nullptr){if(h_ && h_!=INVALID_HANDLE_VALUE)::CloseHandle(h_);h_=h;}
    [[nodiscard]] HANDLE get()const{return h_;}
    [[nodiscard]] explicit operator bool()const{return h_ && h_!=INVALID_HANDLE_VALUE;}
private:HANDLE h_{};
};
std::vector<wchar_t> child_environment()
{
    auto* raw=::GetEnvironmentStringsW();if(!raw)return {};
    std::vector<std::wstring> entries;
    for(const wchar_t* p=raw;*p;p+=std::wcslen(p)+1){std::wstring e{p};const auto n=e.find(L'=');const auto key=e.substr(0,n);if(::_wcsicmp(key.c_str(),L"SteamAppId")!=0 && ::_wcsicmp(key.c_str(),L"SteamGameId")!=0)entries.push_back(std::move(e));}
    ::FreeEnvironmentStringsW(raw);entries.emplace_back(L"SteamAppId=70");entries.emplace_back(L"SteamGameId=70");
    std::sort(entries.begin(),entries.end(),[](const auto&a,const auto&b){return ::_wcsicmp(a.c_str(),b.c_str())<0;});
    std::vector<wchar_t> out;for(const auto&e:entries){out.insert(out.end(),e.begin(),e.end());out.push_back(0);}out.push_back(0);return out;
}
}
std::wstring external_widen(const std::string_view text)
{
    if(text.empty())return {};if(text.size()>32767)return {};
    const int size=::MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);if(size<=0)return {};
    std::wstring out(static_cast<std::size_t>(size),0);if(::MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),size)!=size)return {};return out;
}
std::string external_utf8(const std::wstring_view text)
{
    if(text.empty())return {};if(text.size()>32767)return {};
    const int size=::WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);if(size<=0)return {};
    std::string out(static_cast<std::size_t>(size),0);if(::WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),size,nullptr,nullptr)!=size)return {};return out;
}
std::string new_external_run_id()
{
    std::array<unsigned char,16> bytes{};if(::BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return {};
    constexpr char hex[]="0123456789abcdef";std::string out;for(const auto b:bytes){out+=hex[b>>4];out+=hex[b&15];}return out;
}
namespace {
std::optional<std::string> read_external_text_shared(const std::filesystem::path& path,std::size_t limit,DWORD sharing)
{
    if(!path.is_absolute() || limit==0 || limit>1024U*1024U)return {};
    const Handle h{::CreateFileW(path.c_str(),GENERIC_READ,sharing,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
    if(!h)return {};BY_HANDLE_FILE_INFORMATION before{},after{};
    if(::GetFileType(h.get())!=FILE_TYPE_DISK || !::GetFileInformationByHandle(h.get(),&before) || before.nNumberOfLinks!=1 || (before.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))!=0 || before.nFileSizeHigh || before.nFileSizeLow>limit)return {};
    std::string out(before.nFileSizeLow,0);DWORD count{};if(!::ReadFile(h.get(),out.data(),static_cast<DWORD>(out.size()),&count,nullptr) || count!=out.size() || !::GetFileInformationByHandle(h.get(),&after) || before.nFileSizeLow!=after.nFileSizeLow || ::CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)!=0)return {};return out;
}
}
std::optional<std::string> read_external_text(const std::filesystem::path& path,std::size_t limit)
{
    return read_external_text_shared(path,limit,FILE_SHARE_READ);
}
std::optional<core::SceneAntialiasingObservation> read_external_msaa_observation(
        const SecureOutputDirectory& directory,const std::string_view expected_run_id)
{
    if(!directory.valid())return {};
    // The renderer pins this exact leaf with write/delete access until its next
    // observation. Share those accesses without granting them to this reader.
    // One held read handle, stable metadata, strict fields and run identity
    // reject incomplete/replaced/cross-session evidence; absence is not Off.
    const auto bytes=read_external_text_shared(directory.canonical_path()/L"external-msaa.json",
        1024,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE);
    return bytes?core::decode_external_msaa_observation(*bytes,expected_run_id):std::nullopt;
}
ExternalMsaaPublicationResult ExternalMsaaObservationPublisher::publish(
        const SecureOutputDirectory& directory,const std::string_view json,
        const std::string_view expected_run_id,const std::chrono::steady_clock::time_point now)
{
    using Result=ExternalMsaaPublicationResult;
    if(failed_)return Result::failed;
    if(!directory.valid() || !core::decode_external_msaa_observation(json,expected_run_id)) {
        failed_=true;return Result::failed;
    }
    if(file_.valid() && json==last_published_)return Result::unchanged;
    if(now<next_attempt_)return Result::retry_pending;
    if(statistics_.attempts>=maximum_session_attempts) {failed_=true;return Result::failed;}
    ++statistics_.attempts;++consecutive_attempts_;
    next_attempt_=now+retry_cooldown;
    // remove_on_close marks and closes this exact retained handle. Another
    // reader may keep its deleted directory entry alive briefly; never reopen
    // that path for deletion or replace an entry created by somebody else.
    const bool removed=!file_.valid() || static_cast<bool>(file_.remove_on_close());
    if(removed && secure_atomic_write_new(directory,L"external-msaa.json",
            std::as_bytes(std::span{json.data(),json.size()}),file_)) {
        last_published_=json;consecutive_attempts_=0;++statistics_.publications;
        return Result::published;
    }
    ++statistics_.retryable_failures;
    if(consecutive_attempts_>=maximum_consecutive_attempts) {
        failed_=true;return Result::failed;
    }
    return Result::retry_pending;
}
bool write_external_text_atomic(const std::filesystem::path& path,const std::string_view text,const bool replace)
{
    if(!path.is_absolute() || path.filename().empty() || text.size()>1024U*1024U)return false;
    auto pinned=open_secure_output_directory(path.parent_path());if(!pinned)return false;
    const auto bytes=std::as_bytes(std::span{text.data(),text.size()});
    if(!replace)return static_cast<bool>(secure_atomic_write_new(*pinned.directory,path.filename().native(),bytes));
    const auto id=new_external_run_id();if(id.empty())return false;const auto temp=L"settings-"+external_widen(id)+L".tmp";
    if(!secure_atomic_write_new(*pinned.directory,temp,bytes))return false;
    const auto source=pinned.directory->canonical_path()/temp;
    if(::MoveFileExW(source.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
    static_cast<void>(::DeleteFileW(source.c_str()));return false;
}
ExternalSettingsLoad load_external_settings(const std::filesystem::path& path,const core::ExternalLaunchConfiguration& defaults)
{
    ExternalSettingsLoad out{defaults,"defaults",{}};
    const auto attr=::GetFileAttributesW(path.c_str());if(attr==INVALID_FILE_ATTRIBUTES){if(::GetLastError()!=ERROR_FILE_NOT_FOUND && ::GetLastError()!=ERROR_PATH_NOT_FOUND)out.status="settings_read_failed";return out;}
    const auto text=read_external_text(path,core::kExternalLaunchMaximumBytes);
    if(text){auto parsed=core::decode_external_launch_configuration(*text);if(parsed){out.configuration=std::move(*parsed.configuration);out.status="loaded";return out;}}
    auto pinned=open_secure_output_directory(path.parent_path());const auto id=new_external_run_id();
    if(pinned && !id.empty() && (attr&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))==0){const auto backup=path.parent_path()/(L"launcher.corrupt-"+external_widen(id)+L".json");if(::MoveFileExW(path.c_str(),backup.c_str(),MOVEFILE_WRITE_THROUGH)){out.corrupt_backup=backup;out.status="corrupt_settings_backed_up";return out;}}
    out.status="corrupt_settings_backup_failed";return out;
}
bool save_external_settings(const std::filesystem::path&path,const core::ExternalLaunchConfiguration&configuration)
{
    auto text=core::encode_external_launch_configuration(configuration);return text && write_external_text_atomic(path,*text,true);
}
std::optional<std::filesystem::path> default_external_settings_path()
{
    PWSTR local{};
    if(FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&local)))return {};
    const auto result=std::filesystem::path{local}/L"HLClientEngine"/L"launcher.json";
    ::CoTaskMemFree(local);
    return result;
}
bool merge_external_settings(const std::filesystem::path& path,
    const core::ExternalLaunchConfiguration& baseline,const core::ExternalLaunchConfiguration& edited)
{
    if(!path.is_absolute() || path.filename().empty())return false;
    // A bounded native lock serializes the three-way read/replace across
    // launcher and client processes. Failure leaves the previous file intact.
    Handle mutex{::CreateMutexW(nullptr,FALSE,L"Local\\HLClientEngine.SharedLauncherSettings.v1")};
    if(!mutex)return false;
    const auto acquired=::WaitForSingleObject(mutex.get(),100);
    if(acquired!=WAIT_OBJECT_0 && acquired!=WAIT_ABANDONED)return false;
    struct Release final {HANDLE value;~Release(){::ReleaseMutex(value);}} release{mutex.get()};
    auto current=baseline;
    const auto attributes=::GetFileAttributesW(path.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES) {
        const auto bytes=read_external_text(path,core::kExternalLaunchMaximumBytes);
        const auto loaded=bytes?core::decode_external_launch_configuration(*bytes):core::ExternalLaunchConfigurationResult{};
        if(!loaded)return false;
        current=*loaded.configuration;
    } else if(::GetLastError()!=ERROR_FILE_NOT_FOUND && ::GetLastError()!=ERROR_PATH_NOT_FOUND)return false;
    const auto merged=core::merge_external_launch_configuration(baseline,edited,current);
    return merged && save_external_settings(path,*merged);
}
struct ExternalClientProcess::Impl {
    Handle process,cancel;DWORD pid{},error{};std::uint64_t creation{};
    std::optional<BoundedProcessLogCapture> log;
};
ExternalClientProcess::ExternalClientProcess():impl_{std::make_unique<Impl>()}{}
ExternalClientProcess::~ExternalClientProcess()=default;
bool ExternalClientProcess::launch(const WindowsBinaryIdentity&binary,const std::vector<std::wstring>&arguments,const bool add_cancel)
{
    if(impl_->process){impl_->error=ERROR_ALREADY_EXISTS;return false;}
    if(!binary.canonical_path.is_absolute() || binary.snapshot.size==0 || arguments.size()>32){impl_->error=ERROR_INVALID_PARAMETER;return false;}
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    impl_->cancel.reset(::CreateEventW(&security,TRUE,FALSE,nullptr));if(!impl_->cancel){impl_->error=::GetLastError();return false;}
    impl_->log=BoundedProcessLogCapture::create({64U*1024U,4096U,4096U,16U*1024U});if(!impl_->log){impl_->error=ERROR_NOT_ENOUGH_MEMORY;return false;}
    Handle null{::CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr)};
    if(!null){impl_->error=::GetLastError();return false;}
    auto args=arguments;if(add_cancel){args.emplace_back(L"--external-cancel-event");args.push_back(std::to_wstring(reinterpret_cast<std::uintptr_t>(impl_->cancel.get())));}
    auto command=build_windows_command_line(binary.canonical_path,args);if(command.size()>32766){impl_->error=ERROR_INVALID_PARAMETER;return false;}
    std::vector<wchar_t> mutable_command(command.begin(),command.end());mutable_command.push_back(0);
    SIZE_T bytes{};static_cast<void>(::InitializeProcThreadAttributeList(nullptr,1,0,&bytes));if(!bytes || bytes>1024U*1024U){impl_->error=ERROR_INVALID_PARAMETER;return false;}
    std::vector<std::byte> storage(bytes);auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!::InitializeProcThreadAttributeList(attributes,1,0,&bytes)){impl_->error=::GetLastError();return false;}
    struct Cleanup{LPPROC_THREAD_ATTRIBUTE_LIST p;~Cleanup(){::DeleteProcThreadAttributeList(p);}}cleanup{attributes};
    const std::array handles{null.get(),static_cast<HANDLE>(impl_->log->inherited_write_handle()),impl_->cancel.get()};
    if(!::UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,const_cast<HANDLE*>(handles.data()),sizeof(handles),nullptr,nullptr)){impl_->error=::GetLastError();return false;}
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=null.get();startup.StartupInfo.hStdOutput=handles[1];startup.StartupInfo.hStdError=handles[1];
    auto environment=child_environment();if(environment.empty()){impl_->error=ERROR_NOT_ENOUGH_MEMORY;return false;}
    PROCESS_INFORMATION child{};const auto directory=binary.canonical_path.parent_path();
    if(!::CreateProcessW(binary.canonical_path.c_str(),mutable_command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT,environment.data(),directory.c_str(),&startup.StartupInfo,&child)){impl_->error=::GetLastError();return false;}
    impl_->process.reset(child.hProcess);Handle thread{child.hThread};impl_->pid=child.dwProcessId;impl_->log->close_parent_write_handle();
    const auto checked=verify_windows_process_image_identity(child.hProcess,binary,kMaximumObservedExecutableBytes,{AuthenticodePolicy::not_required_for_project_owned_binary,false});
    FILETIME created{},exited{},kernel{},user{};
    if(!checked || !::GetProcessTimes(child.hProcess,&created,&exited,&kernel,&user)){impl_->error=checked?::GetLastError():ERROR_INVALID_IMAGE_HASH;static_cast<void>(::TerminateProcess(child.hProcess,126));static_cast<void>(::WaitForSingleObject(child.hProcess,2000));return false;}
    impl_->creation=(static_cast<std::uint64_t>(created.dwHighDateTime)<<32U)|created.dwLowDateTime;
    if(::ResumeThread(thread.get())!=1){impl_->error=::GetLastError();static_cast<void>(::TerminateProcess(child.hProcess,126));static_cast<void>(::WaitForSingleObject(child.hProcess,2000));return false;}return true;
}
void ExternalClientProcess::request_cancel()noexcept{if(impl_->cancel)static_cast<void>(::SetEvent(impl_->cancel.get()));}
bool ExternalClientProcess::terminate_owned_child(std::uint32_t code)noexcept{return impl_->process && ::TerminateProcess(impl_->process.get(),code)!=FALSE;}
ExternalChildStatus ExternalClientProcess::poll()noexcept{if(!impl_->process)return {false,{},impl_->error};const auto state=::WaitForSingleObject(impl_->process.get(),0);if(state==WAIT_TIMEOUT)return {true,{},0};if(state!=WAIT_OBJECT_0)return {false,{},::GetLastError()};DWORD code{};if(!::GetExitCodeProcess(impl_->process.get(),&code))return {false,{},::GetLastError()};return {false,code,0};}
std::uint32_t ExternalClientProcess::process_id()const noexcept{return impl_->pid;}
std::uint64_t ExternalClientProcess::process_creation()const noexcept{return impl_->creation;}
std::uint32_t ExternalClientProcess::launch_error()const noexcept{return impl_->error;}
BoundedProcessLogSnapshot ExternalClientProcess::log_snapshot()const{return impl_->log?impl_->log->snapshot():BoundedProcessLogSnapshot{};}
BoundedProcessLogSnapshot ExternalClientProcess::finish_log(){return impl_->log?impl_->log->finish(std::chrono::milliseconds{500}):BoundedProcessLogSnapshot{};}
std::optional<ExternalStatusMarker> latest_external_status_marker(const std::string_view log)
{
    std::optional<ExternalStatusMarker> out;std::size_t start=0;
    constexpr std::array stages{"idle","recoverable_error","checking_resources","connecting","authentication","signon","preparing_map","in_game","disconnecting","completed","error"};
    while(start<log.size()){const auto end=log.find('\n',start);if(end==std::string_view::npos)break;auto line=log.substr(start,end-start);start=end+1;if(!line.empty() && line.back()=='\r')line.remove_suffix(1);
        constexpr std::string_view prefix="[external-status] stage=";if(!line.starts_with(prefix) || line.size()>256)continue;line.remove_prefix(prefix.size());const auto space=line.find(' ');const auto stage=line.substr(0,space);if(std::find(stages.begin(),stages.end(),stage)==stages.end())continue;std::string reason;
        if(space!=std::string_view::npos){line.remove_prefix(space+1);if(!line.starts_with("reason="))continue;line.remove_prefix(7);if(line.size()>96)continue;bool valid=true;for(const auto c:line)if(!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_' || c=='-'))valid=false;if(!valid)continue;reason=line;}
        out=ExternalStatusMarker{std::string{stage},std::move(reason)};
    }return out;
}
std::string external_progress_stage(const std::string_view previous_stage,
    const std::optional<ExternalStatusMarker>& current_marker,
    const ExternalFailureMarkers& history,const bool gameplay_receipt_ready,const bool cancelled)
{
    if(cancelled)return "disconnecting";
    if(current_marker) {
        constexpr std::array stages{"idle","recoverable_error","checking_resources","connecting","authentication","signon","preparing_map","in_game","disconnecting","completed","error"};
        if(std::find(stages.begin(),stages.end(),current_marker->stage)!=stages.end())return current_marker->stage;
    }
    if(!history.primary_reason.empty() || !history.cleanup_reason.empty())return "error";
    if(gameplay_receipt_ready)return "in_game";
    return std::string{previous_stage};
}
bool external_application_close_succeeded(const std::optional<ExternalStatusMarker>& final_marker,
    const std::optional<std::uint32_t> exit_code,const bool forced) noexcept
{
    return !forced && exit_code && *exit_code==0U && final_marker &&
        final_marker->stage=="completed" && final_marker->reason=="application_closed";
}
std::string redacted_external_status_log(const std::string_view log)
{
    std::string out;std::size_t start=0;
    while(start<log.size() && out.size()<16U*1024U){const auto end=log.find('\n',start);if(end==std::string_view::npos)break;const auto line=log.substr(start,end-start+1);start=end+1;
        if(const auto marker=latest_external_status_marker(line))out+="[external-status] stage="+marker->stage+" reason="+marker->reason+'\n';}
    return out;
}
ExternalFailureMarkers external_failure_markers(std::string_view log)
{
    ExternalFailureMarkers out;
    log=log.substr(0,64U*1024U);
    std::size_t start=0;
    for(std::size_t lines=0;start<log.size() && lines<4096U;++lines) {
        const auto end=log.find('\n',start);if(end==std::string_view::npos)break;
        const auto marker=latest_external_status_marker(log.substr(start,end-start+1U));start=end+1U;
        if(!marker || marker->stage!="error" || marker->reason.empty() || marker->reason=="none")continue;
        auto& reason=marker->reason=="disconnect_failed" ? out.cleanup_reason : out.primary_reason;
        if(reason.empty())reason=marker->reason;
    }
    return out;
}
ExternalEmergencyCloseResult close_external_child_after_failure(ExternalClientProcess&child)
{
    child.request_cancel();auto state=child.poll();
    const auto grace=std::chrono::steady_clock::now()+std::chrono::milliseconds{1600};
    while(state.running && std::chrono::steady_clock::now()<grace){std::this_thread::sleep_for(std::chrono::milliseconds{20});state=child.poll();}
    bool forced=false;if(state.running){forced=child.terminate_owned_child(125);const auto bound=std::chrono::steady_clock::now()+std::chrono::milliseconds{2000};while(state.running && std::chrono::steady_clock::now()<bound){std::this_thread::sleep_for(std::chrono::milliseconds{20});state=child.poll();}}
    return {!state.running && state.exit_code.has_value(),forced,state.exit_code};
}
} // namespace hlclient::platform::windows
