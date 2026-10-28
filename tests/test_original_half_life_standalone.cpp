// Standalone Windows test: cl /std:c++20 /EHsc /utf-8 /W4 this.cpp
// advapi32.lib user32.lib /Fe:<task-only output>. No Steam/game/process launch.
#include "../apps/hlclient_launcher/original_half_life.hpp"
#include <iostream>
#include <stdexcept>
#include <shellapi.h>

namespace original = hlclient::launcher::original_hl;
namespace {
unsigned checks{};
void require(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
original::Installation installed(const wchar_t* root = L"D:\\Steam\\steamapps\\common\\Half-Life") {
    original::Installation value; value.root = root; value.executable = value.root / L"hl.exe"; value.source = "fixture"; return value;
}
original::Request request() {
    original::Request value; value.asset_root = L"D:\\Resources"; value.endpoint = "192.168.0.104:27016";
    value.player_name = "Test Player"; value.width = 1920; value.height = 1080; return value;
}
// Independent regression model of legacy raw command-tail tokenization:
// whitespace separates tokens; optional quote grouping does NOT strip quotes.
// Windows argv round-tripping alone missed the real GoldSrc failure.
std::vector<std::wstring> legacy_tokens(std::wstring_view tail, bool grouped_quotes) {
    std::vector<std::wstring> out;
    for (std::size_t at=0; at<tail.size();) {
        while (at<tail.size() && tail[at]<=L' ') ++at;
        if (at==tail.size()) break;
        const auto begin=at; bool quoted=false;
        while (at<tail.size() && (tail[at]>L' ' || (grouped_quotes && quoted))) {
            if (tail[at]==L'"') quoted=!quoted;
            ++at;
        }
        out.emplace_back(tail.substr(begin,at-begin));
    }
    return out;
}
}
int main() {
    try {
        using namespace original;
        ProcessSnapshot ready; ready.complete = true; ready.steam_running = true;
        const auto good = make_plan(request(), installed(), ready);
        require(static_cast<bool>(good), "basic plan");
        require(good.arguments == std::vector<std::wstring>{L"-steam",L"-game",L"valve",L"-gl",L"-w",L"1920",L"-h",L"1080",L"-windowed",L"+name",L"Test Player",L"+connect",L"192.168.0.104:27016"}, "exact allowlisted argv");
        require(good.command_line == L"\"D:\\Steam\\steamapps\\common\\Half-Life\\hl.exe\" -steam -game valve -gl -w 1920 -h 1080 -windowed +name \"Test Player\" +connect 192.168.0.104:27016", "exact GoldSrc-compatible raw command line");
        for (const bool grouped : {false,true}) {
            const auto tail=std::wstring_view{good.command_line}.substr(quote_argument(good.executable.native()).size()+1);
            const auto tokens=legacy_tokens(tail,grouped);
            const auto parameter=[&](std::wstring_view name) {return std::find(tokens.begin(),tokens.end(),name);};
            for (const auto control : {L"-steam",L"-game",L"-gl",L"-w",L"-h",L"-windowed",L"+name",L"+connect"})
                require(parameter(control)!=tokens.end(), "legacy parser recognizes bare option/+command");
            require(*(parameter(L"-w")+1)==L"1920" && *(parameter(L"-h")+1)==L"1080", "legacy numeric values unquoted");
            require(*(parameter(L"+connect")+1)==L"192.168.0.104:27016", "legacy connect endpoint unquoted");
            std::wstring historical;
            for (const auto& token : good.arguments) {historical+=quote_argument(token);historical+=L' ';}
            const auto broken=legacy_tokens(historical,grouped);
            require(std::find(broken.begin(),broken.end(),L"-gl")==broken.end() && std::find(broken.begin(),broken.end(),L"+connect")==broken.end(), "negative control catches historical all-quoted bug");
        }
        require(good.working_directory == installed().root, "working directory");
        auto full = request(); full.windowed = false;
        require(make_plan(full, installed(), ready).arguments[8] == L"-full", "fullscreen maps to stock full");
        full.display_index = 1;
        require(make_plan(full, installed(), ready).error == "stock_display_selection_unsupported", "no guessed monitor flag");
        for (const auto endpoint : {"1.2.3.4:1", "192.168.0.104:65535"}) require(valid_endpoint(endpoint), "valid literal endpoint");
        for (const auto endpoint : {"", "1.2.3.4", "1.2.3.4:0", "1.2.3.4:65536", "1.2.3.256:1", "127.0.0.1:1", "0.0.0.0:1", "224.0.0.1:1", "1.2.3.255:1", "01.2.3.4:1", "1.2.3.4:01", "connect 1.2.3.4:1", "1.2.3.4:1;quit", "1.2.3.4:1 +quit", "1.2.3.4:1\n"}) {
            auto value = request(); value.endpoint = endpoint; require(!make_plan(value, installed(), ready), "invalid/injected endpoint");
        }
        for (const auto name : {"", " ", "+quit", "Player +quit", "-game", "Player -gl", "Player -w 640", "Player;quit", "Player\nquit", "Player\"", "Player\\", "Гравець"}) {
            auto value = request(); value.player_name = name; require(!make_plan(value, installed(), ready), "invalid/injected console name");
        }
        auto value = request(); value.player_name = std::string(31, 'a'); require(static_cast<bool>(make_plan(value, installed(), ready)), "name bound31");
        value.player_name.push_back('a'); require(!make_plan(value, installed(), ready), "name bound32");
        for (const auto dimensions : {std::pair{639,480}, {640,479}, {7681,1080}, {1920,4321}, {-1,1080}}) {
            value = request(); value.width = dimensions.first; value.height = dimensions.second; require(!make_plan(value, installed(), ready), "video bounds");
        }
        auto processes = ready; processes.half_life_running = true; processes.half_life_pids = {111};
        require(make_plan(request(), installed(), processes).error == "original_half_life_already_running_close_first", "existing fake process blocked");
        processes = ready; processes.complete = false; require(!make_plan(request(), installed(), processes), "incomplete inventory fails closed");
        processes = ready; processes.steam_running = false; require(make_plan(request(), installed(), processes).error == "steam_not_running", "Steam required");
        for (const auto root : {L"relative", L"D:relative", L"\\\\server\\game", L"\\\\?\\D:\\Game", L"D:\\Game:ads", L"D:\\Game\\..\\Other", L"D:\\bad\"path"}) require(!safe_local_path(root), "unsafe path");
        require(safe_local_path(L"D:\\Ігри та тести\\Steam\\steamapps\\common\\Half-Life"), "Unicode local path");
        const auto unicode = make_plan(request(), installed(L"D:\\Ігри та тести\\steamapps\\common\\Half-Life"), ready);
        require(static_cast<bool>(unicode), "Unicode plan");
        // Exercise Windows argument decoding, including quotes/trailing slashes,
        // without executing the quoted text or invoking a shell.
        for (const std::wstring argument : {L"", L"a b", L"тест Unicode", L"a\\", L"a\\\"b", L"literal ; & | + -", L"C:\\Folder name\\"}) {
            const auto command = std::wstring{L"fixture.exe "} + quote_argument(argument); int count{};
            auto parsed = ::CommandLineToArgvW(command.c_str(), &count);
            require(parsed && count == 2 && parsed[1] == argument, "Windows quoting roundtrip");
            if (parsed) ::LocalFree(parsed);
        }
        require(app70_manifest(R"("AppState" { "appid" "70" "installdir" "Half-Life" })", L"Half-Life"), "app70 manifest");
        for (const auto text : {R"("AppState" { "appid" "10" "installdir" "Half-Life" })", R"("AppState" { "appid" "70" "appid" "70" "installdir" "Half-Life" })", R"("AppState" { "appid" "70" "installdir" "Other" })", R"("AppState" { "appid" "70" "installdir" "..\\Half-Life" })", R"("AppState" { "appid" "70" )"}) require(!app70_manifest(text, L"Half-Life"), "bad manifest");
        auto libraries = library_paths(R"("libraryfolders" { "0" { "path" "D:\\Steam" "apps" { "70" "123" } } "1" "E:\\SteamLibrary" })");
        require(libraries && libraries->size() == 2 && (*libraries)[1] == L"E:\\SteamLibrary", "old/new library formats");
        for (const auto text : {R"("libraryfolders" { "0" { "path" "relative" } })", R"("libraryfolders" { "0" { "path" "D:\\Good" "path" "E:\\Other" } })", R"("libraryfolders" { "0" "\\\\server\\share" })"}) require(!library_paths(text), "unsafe library metadata");
        require(!library_paths(std::string(maximum_metadata_bytes + 1, 'x')), "metadata bound");
        Installation missing; missing.error = "missing";
        require(select_installation(installed(), true, {installed(L"E:\\Steam\\steamapps\\common\\Half-Life")}).root == installed().root, "explicit wins");
        require(select_installation(missing, true, {installed()}).root == installed().root, "invalid resource copy uses registered installation not copied exe");
        require(select_installation(missing, true, {}).error == "original_game_not_found", "invalid resource copy never launched without installed fallback");
        require(static_cast<bool>(select_installation(missing, false, {installed(), installed()})), "deduplicate fallback");
        require(select_installation(missing, false, {installed(), installed(L"E:\\Steam\\steamapps\\common\\Half-Life")}).error == "original_game_multiple_installations_choose_asset_root", "ambiguous fallback fails");
        require(select_installation(missing, false, {}).error == "original_game_not_found", "missing fallback");
        Process empty;
        require(!empty.poll().running && !empty.poll().exit_code && empty.pid() == 0 && !empty.request_close(), "unstarted RAII no process effects");
        std::cout << "PASS " << checks << " standalone original Half-Life checks; no Steam/game launch\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
