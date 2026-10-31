// Offline fixture only. No game, Steam client, renderer or GPU setting is used.
#include <hlclient/platform/windows/executable_launch_path.hpp>
#include <hlclient/platform/windows/external_launcher.hpp>
#include <windows.h>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
namespace native = hlclient::platform::windows;
std::size_t assertions{};
void require(bool condition, const char* message) {
    ++assertions;
    if (!condition) throw std::runtime_error{message};
}
void pure_tests() {
    static_assert(static_cast<int>(native::BackgroundSkyPolicy::original)==0);
    static_assert(static_cast<int>(native::BackgroundSkyPolicy::depth)==1);
    static_assert(static_cast<int>(native::BackgroundSkyPolicy::no_background)==2);
    static_assert(static_cast<int>(native::BackgroundSkyPolicy::no_sky_color)==3);
    static_assert(static_cast<int>(native::BackgroundSkyPolicy::world_sky_non_occluding)==6);
    for (const auto& [input, expected] : {
        std::pair{L"\\\\?\\C:\\Program Files\\HL Client\\hlclient.exe", L"C:\\Program Files\\HL Client\\hlclient.exe"},
        std::pair{L"\\\\?\\d:\\Україна\\клієнт.exe", L"d:\\Україна\\клієнт.exe"},
        std::pair{L"\\\\?\\UNC\\server\\share\\HL Client\\hlclient.exe", L"\\\\server\\share\\HL Client\\hlclient.exe"},
        std::pair{L"C:\\client.exe", L"C:\\client.exe"},
        std::pair{L"\\\\192.168.0.104\\storage\\HLClient-Win10-Test\\hlclient.exe", L"\\\\192.168.0.104\\storage\\HLClient-Win10-Test\\hlclient.exe"}}) {
        const auto converted = native::ordinary_executable_launch_path(input);
        require(converted && converted->native() == expected, "exact ordinary launch spelling");
    }
    for (const auto invalid : {
        L"", L"client.exe", L"C:client.exe", L"\\client.exe", L"C:/client.exe",
        L"\\\\?\\Volume{1234}\\client.exe", L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\client.exe",
        L"\\\\.\\C:\\client.exe", L"\\??\\C:\\client.exe", L"\\\\?\\1:\\client.exe",
        L"\\\\?\\C:\\", L"\\\\?\\UNC\\server\\share", L"\\\\server\\share\\",
        L"C:\\part\\.\\client.exe", L"C:\\part\\..\\client.exe", L"C:\\part.\\client.exe",
        L"C:\\part \\client.exe", L"C:\\client.exe.", L"C:\\client.exe ", L"C:\\part\\\\client.exe",
        L"C:\\client.exe:stream", L"C:\\quoted\"client.exe", L"C:\\bad\nclient.exe",
        L"C:\\*.exe", L"C:\\?.exe", L"C:\\bad|client.exe", L"C:\\<client>.exe",
        L"C:\\CON.exe", L"C:\\prn\\client.exe", L"C:\\AUX .exe", L"C:\\NUL.txt",
        L"C:\\COM1.exe", L"C:\\LPT9.txt", L"C:\\COM\u00b9.exe", L"C:\\LPT\u00b2.exe",
        L"C:\\CONIN$", L"C:\\CONOUT$", L"C:\\CLOCK$"}) {
        require(!native::ordinary_executable_launch_path(invalid), "unsafe or ambiguous spelling rejected");
    }
    std::wstring nul{L"C:\\client.exe"}; nul.push_back(L'\0'); nul += L"suffix";
    require(!native::ordinary_executable_launch_path(std::filesystem::path{nul}), "embedded NUL rejected");
    const std::wstring longest = L"C:\\a\\" + std::wstring(254, L'x');
    require(longest.size() == 259 && native::ordinary_executable_launch_path(L"\\\\?\\" + longest).has_value(),
        "MAX_PATH bound counts ordinary spelling, not extended prefix");
    require(!native::ordinary_executable_launch_path(longest + L'x'), "overlong ordinary spelling fails closed");
    require(!native::ordinary_executable_launch_path(L"C:\\" + std::wstring(256, L'x')), "overlong component rejected");
}

class FixtureCopy final {
public:
    std::filesystem::path directory, executable;
    std::vector<std::filesystem::path> created;
    explicit FixtureCopy(const std::filesystem::path& source) {
        require(source.is_absolute() && source.filename() == L"hlclient_external_process_fixture.exe",
            "explicit accepted offline fixture argument required");
        require(std::filesystem::is_regular_file(source), "fixture exists");
        const auto parent = std::filesystem::temp_directory_path();
        for (unsigned attempt = 0; attempt != 16; ++attempt) {
            const auto candidate = parent / (L"hlclient-gpu-path-" + std::to_wstring(::GetCurrentProcessId()) +
                L"-" + std::to_wstring(::GetTickCount64()) + L"-" + std::to_wstring(attempt));
            if (std::filesystem::create_directory(candidate)) { directory = candidate; break; }
        }
        require(!directory.empty(), "fresh owned fixture directory required");
        try {
            executable = directory / L"клієнт fixture.exe";
            copy(source, executable);
            // Use only the accepted fixture's adjacent runtime dependencies.
            for (const auto name : {L"msvcp140.dll", L"msvcp140_2.dll", L"msvcp140_atomic_wait.dll", L"vcruntime140.dll"}) {
                const auto dependency = source.parent_path() / name;
                if (std::filesystem::is_regular_file(dependency)) copy(dependency, directory / name);
            }
        } catch (...) { cleanup(); throw; }
    }
    ~FixtureCopy() { cleanup(); }
private:
    void copy(const std::filesystem::path& from, const std::filesystem::path& to) {
        created.push_back(to);
        require(std::filesystem::copy_file(from, to, std::filesystem::copy_options::none), "copy into fresh owned fixture");
    }
    void cleanup() noexcept {
        std::error_code ignored;
        for (auto i = created.rbegin(); i != created.rend(); ++i) std::filesystem::remove(*i, ignored);
        if (!directory.empty()) std::filesystem::remove(directory, ignored); // never recursive
    }
};
struct CloseHandle final {
    HANDLE value{};
    ~CloseHandle() { if (value) ::CloseHandle(value); }
};
struct ChildCleanup final {
    native::ExternalClientProcess& child;
    ~ChildCleanup() {
        if (!child.poll().running) return;
        child.request_cancel();
        for (unsigned i = 0; i < 100 && child.poll().running; ++i) ::Sleep(10);
        if (child.poll().running) {
            static_cast<void>(child.terminate_owned_child(127));
            for (unsigned i = 0; i < 200 && child.poll().running; ++i) ::Sleep(10);
        }
    }
};
native::ExternalChildStatus wait(native::ExternalClientProcess& child) {
    for (unsigned i = 0; i < 300; ++i) {
        const auto state = child.poll();
        if (!state.running) return state;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    throw std::runtime_error{"offline child exit timeout"};
}
std::optional<std::wstring> environment_value(const wchar_t* key) {
    ::SetLastError(ERROR_SUCCESS);
    const DWORD size=::GetEnvironmentVariableW(key,nullptr,0);
    if(size==0){
        if(::GetLastError()==ERROR_ENVVAR_NOT_FOUND)return std::nullopt;
        require(::GetLastError()==ERROR_SUCCESS,"read empty environment value");
        return std::wstring{};
    }
    std::wstring value(size,L'\0');
    const DWORD count=::GetEnvironmentVariableW(key,value.data(),size);
    require(count<size,"read complete environment value");value.resize(count);return value;
}
class ScopedSkyEnvironment final {
    static constexpr const wchar_t* key=L"HLC_BACKGROUND_SKY_POLICY";
    std::optional<std::wstring> previous_{environment_value(key)};
public:
    ~ScopedSkyEnvironment(){static_cast<void>(::SetEnvironmentVariableW(key,previous_?previous_->c_str():nullptr));}
    void set(const wchar_t* value){
        // Lowercase spelling proves the override removes the inherited key case-insensitively.
        require(::SetEnvironmentVariableW(L"hlc_background_sky_policy",value)!=FALSE,"set fixture-only parent environment");
    }
};
void sky_environment_tests(const native::WindowsBinaryIdentity& fixture) {
    ScopedSkyEnvironment environment;
    using Sky=native::BackgroundSkyPolicy;
    const auto check=[&](const wchar_t* inherited,std::optional<Sky> selection,const wchar_t* expected){
        environment.set(inherited);
        const auto before=environment_value(L"HLC_BACKGROUND_SKY_POLICY");
        const auto runtime_before=environment_value(L"HLC_RUNTIME_TEXT_POLICY");
        native::ExternalClientProcess child;ChildCleanup cleanup{child};
        std::vector<std::wstring> arguments{L"--expect-sky-policy",expected};
        if(selection)arguments.emplace_back(L"--expect-runtime-off");
        require(child.launch(fixture,arguments,false,selection),"launch fixture with scoped sky environment");
        require(wait(child).exit_code==0,"fixture observes exact expected sky policy");
        const auto child_log=child.finish_log().bytes;
        require(child_log.find("fixture_sky_policy_verified=1")!=std::string::npos,"child confirmed sky policy");
        require(environment_value(L"HLC_BACKGROUND_SKY_POLICY")==before,"launch never mutates parent sky environment");
        require(environment_value(L"HLC_RUNTIME_TEXT_POLICY")==runtime_before,"launch never mutates parent runtime-text environment");
        if(selection)require(child_log.find("fixture_runtime_text_off_verified=1")!=std::string::npos,"child-only runtime text is explicitly OFF");
    };
    check(nullptr,std::nullopt,L"__unset__");
    check(L"inherited-test-invalid",std::nullopt,L"inherited-test-invalid");
    check(L"original",std::nullopt,L"original");
    check(L"depth",std::nullopt,L"depth");
    for(int repeat=0;repeat<2;++repeat){
        check(nullptr,Sky::original,L"original");
        check(nullptr,Sky::depth,L"depth");
        check(L"depth",Sky::original,L"original");
        check(L"original",Sky::depth,L"depth");
        check(L"inherited-test-invalid",Sky::original,L"original");
        check(L"inherited-test-invalid",Sky::depth,L"depth");
        check(nullptr,Sky::no_background,L"no_background");
        check(nullptr,Sky::no_sky_color,L"no_sky_color");
        check(L"depth",Sky::no_background,L"no_background");
        check(L"original",Sky::no_sky_color,L"no_sky_color");
        check(L"inherited-test-invalid",Sky::no_background,L"no_background");
        check(L"inherited-test-invalid",Sky::no_sky_color,L"no_sky_color");
        check(nullptr,Sky::world_sky_non_occluding,L"world_sky_non_occluding");
        check(L"depth",Sky::world_sky_non_occluding,L"world_sky_non_occluding");
        check(L"no_background",Sky::world_sky_non_occluding,L"world_sky_non_occluding");
        check(L"inherited-test-invalid",Sky::world_sky_non_occluding,L"world_sky_non_occluding");
        check(L"world_sky_non_occluding",Sky::original,L"original");
    }
    environment.set(L"depth");
    const auto before=environment_value(L"HLC_BACKGROUND_SKY_POLICY");
    native::ExternalClientProcess child;ChildCleanup cleanup{child};
    require(!child.launch(fixture,{L"--help"},false,static_cast<Sky>(-1)),"invalid sky enum rejected");
    require(child.process_id()==0 && child.launch_error()==ERROR_INVALID_PARAMETER,"invalid sky enum fails before child creation");
    require(environment_value(L"HLC_BACKGROUND_SKY_POLICY")==before,"invalid selection preserves parent environment");
    for(const int reserved:{4,5,7}){
        native::ExternalClientProcess rejected;ChildCleanup rejected_cleanup{rejected};
        require(!rejected.launch(fixture,{L"--help"},false,static_cast<Sky>(reserved)),"reserved sky enum rejected");
        require(rejected.process_id()==0 && rejected.launch_error()==ERROR_INVALID_PARAMETER,"reserved sky enum fails before child creation");
    }
}
void process_tests(const std::filesystem::path& fixture_path) {
    FixtureCopy fixture{fixture_path};
    const native::WindowsBinaryObservationPolicy policy{native::AuthenticodePolicy::not_required_for_project_owned_binary, false};
    const auto observed = native::observe_windows_binary_identity(fixture.executable, native::kMaximumObservedExecutableBytes, policy);
    require(static_cast<bool>(observed), "observe copied fixture identity");
    const auto original = *observed.identity;
    require(original.canonical_path.native().starts_with(L"\\\\?\\"), "fixture observation retains extended canonical identity");
    const auto ordinary = native::ordinary_executable_launch_path(original.canonical_path);
    require(ordinary.has_value(), "fixture has safe ordinary spelling");
    sky_environment_tests(original);
    {
        native::ExternalClientProcess child; ChildCleanup cleanup{child};
        require(child.launch(original, {L"--wait"}), "launch verified fixture");
        require(child.process_id() != 0 && child.process_creation() != 0 && child.poll().running, "live owned fixture recorded");
        CloseHandle handle{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, child.process_id())};
        require(handle.value != nullptr, "open own child for read-only image query");
        std::array<wchar_t, 32768> image{}; DWORD size = static_cast<DWORD>(image.size());
        require(::QueryFullProcessImageNameW(handle.value, 0, image.data(), &size) != FALSE, "query actual child image spelling");
        const std::wstring actual{image.data(), size};
        require(!actual.starts_with(L"\\\\?\\") && ::CompareStringOrdinal(actual.c_str(), -1,
            ordinary->c_str(), -1, TRUE) == CSTR_EQUAL, "actual process image is exact ordinary path");
        require(static_cast<bool>(native::verify_windows_process_image_identity(handle.value, original,
            native::kMaximumObservedExecutableBytes, policy)), "ordinary process still matches original canonical identity and hash");
        require(*observed.identity == original, "launch does not mutate expected canonical identity");
        require(!child.launch(original, {L"--help"}), "cannot replace owned process");
        child.request_cancel(); const auto state = wait(child);
        require(state.exit_code == 0, "private cancel event exits owned fixture normally");
        require(child.finish_log().bytes.find("stage=completed") != std::string::npos, "completed marker retained");
    }
    for (int repeat = 0; repeat < 2; ++repeat) {
        native::ExternalClientProcess child; ChildCleanup cleanup{child};
        require(child.launch(original, {L"--help"}), "repeat fixture launch");
        require(wait(child).exit_code == 0, "repeat fixture normal exit");
    }
    {
        native::ExternalClientProcess child; ChildCleanup cleanup{child};
        require(child.launch(original, {L"--fail"}), "nonzero fixture launch");
        require(wait(child).exit_code == 7, "nonzero exit is not hidden");
    }
    for (const bool wrong_hash : {true, false}) {
        auto wrong = original;
        if (wrong_hash) wrong.sha256[0] ^= std::byte{1};
        else wrong.snapshot.identity.file_id[0] ^= std::byte{1};
        native::ExternalClientProcess child; ChildCleanup cleanup{child};
        require(!child.launch(wrong, {L"--help"}), "wrong hash or file identity rejected by suspended-child verification");
        require(child.launch_error() == ERROR_INVALID_IMAGE_HASH, "identity mismatch has exact error");
        require(child.process_id() != 0 && wait(child).exit_code == 126, "mismatched suspended child terminated and awaited");
        require(child.finish_log().bytes.empty(), "rejected fixture emitted no user-code marker");
    }
    {
        auto missing = original; missing.canonical_path = original.canonical_path.parent_path() / L"missing-fixture.exe";
        native::ExternalClientProcess child; ChildCleanup cleanup{child};
        require(!child.launch(missing, {L"--help"}) && child.process_id() == 0, "missing exact EXE never searches PATH");
    }
    {
        auto unsafe = original; unsafe.canonical_path = L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\fixture.exe";
        native::ExternalClientProcess child; ChildCleanup cleanup{child};
        require(!child.launch(unsafe, {L"--help"}) && child.process_id() == 0 && child.launch_error() == ERROR_INVALID_NAME,
            "unsupported namespace rejected before child creation");
    }
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 2, "Usage: test_launcher_gpu_path_standalone.exe <absolute hlclient_external_process_fixture.exe>");
        pure_tests(); process_tests(std::filesystem::path{argv[1]});
        std::cout << "PASS launcher_gpu_path assertions=" << assertions
            << " game_launches=0 gpu_preference_writes=0 original_canonical_identity_preserved=1 sky_environment_override_verified=1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL launcher_gpu_path: " << error.what() << '\n';
        return 1;
    }
}


