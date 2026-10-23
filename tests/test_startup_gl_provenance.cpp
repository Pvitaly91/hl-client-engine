#include "../apps/hlclient_launcher/startup_journal.hpp"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <fstream>
#include <iterator>

namespace {
namespace fs = std::filesystem;
struct OwnedStartupDirectory {
    fs::path parent, root;
    OwnedStartupDirectory() {
        std::array<wchar_t, 32768> buffer{};
        const auto size = ::GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
        REQUIRE(size > 0U);
        REQUIRE(size < buffer.size());
        parent = fs::path{buffer.data()}.lexically_normal();
        static std::atomic_uint32_t ordinal{};
        root = parent / (L"hlclient-startup-gl-test-" + std::to_wstring(::GetCurrentProcessId()) +
            L"-" + std::to_wstring(::GetTickCount64()) + L"-" + std::to_wstring(ordinal++));
        REQUIRE(fs::create_directory(root));
    }
    ~OwnedStartupDirectory() {
        if(root.is_absolute() && root.parent_path() == parent &&
            root.filename().wstring().starts_with(L"hlclient-startup-gl-test-")) {
            std::error_code error;
            static_cast<void>(fs::remove_all(root, error));
        }
    }
};
std::string read(const fs::path& path) {
    std::ifstream input{path, std::ios::binary};
    REQUIRE(input.is_open());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
}

TEST_CASE("Startup GL provenance survives an early failure without video.json", "[windows][startup][offline]") {
    OwnedStartupDirectory fixture;
    hlclient::launcher::StartupJournal journal{fixture.root};
    REQUIRE(journal.available());
    REQUIRE(journal.directory().parent_path() == fixture.root);
    const std::array facts{
        "[opengl-startup] stage=context_ready",
        "[opengl-startup] vendor=Intel",
        "[opengl-startup] renderer=Intel_HD_Graphics",
        "[opengl-startup] version=3.3.0",
        "[opengl-startup] glsl=3.30",
        "[opengl-startup] profile=core",
        "[opengl-startup] dsa_core=false dsa_arb=false",
        "[fatal] Native scene resolve failed with invalid_enum"};
    const std::array omitted{
        "[opengl-startup] password=private-password",
        "[opengl-startup] ticket=private-ticket",
        "[opengl-startup] authentication=private-auth",
        "[info] unrelated-info",
        "[server] unrelated-server",
        "[chat] unrelated-chat"};
    std::string captured;
    for(const auto* line : facts) captured += std::string{line} + '\n';
    for(const auto* line : omitted) captured += std::string{line} + '\n';
    journal.capture_client_output(captured);
    const auto output = read(journal.directory() / "client-startup.log");
    for(const auto* line : facts) CHECK(output.find(line) != std::string::npos);
    for(const auto* line : omitted) CHECK(output.find(line) == std::string::npos);
    CHECK_FALSE(fs::exists(journal.directory() / "video.json"));
    const auto zip_path = journal.export_zip();
    REQUIRE(zip_path.parent_path() == fixture.root);
    const auto zip = read(zip_path);
    REQUIRE(zip.starts_with("PK\x03\x04"));
    CHECK(zip.find("PK\x01\x02") != std::string::npos);
    CHECK(zip.find("PK\x05\x06") != std::string::npos);
    CHECK(zip.find("client-startup.log") != std::string::npos);
    CHECK(zip.find(output) != std::string::npos); // Native export uses stored ZIP entries.
    for(const auto* line : omitted) CHECK(zip.find(line) == std::string::npos);
}

TEST_CASE("Startup GL provenance keeps line and artifact budgets", "[windows][startup][offline]") {
    OwnedStartupDirectory fixture;
    hlclient::launcher::StartupJournal journal{fixture.root};
    REQUIRE(journal.available());
    const std::string oversized = "[opengl-startup] " + std::string(8193U, 'X');
    const std::string bounded = "[opengl-startup] " + std::string(4000U, 'Y');
    std::string captured = oversized + '\n';
    for(unsigned i = 0; i < 100; ++i) captured += bounded + '\n';
    journal.capture_client_output(captured);
    const auto output = read(journal.directory() / "client-startup.log");
    CHECK(output.size() <= hlclient::launcher::StartupJournal::maximum_artifact_bytes);
    CHECK(output.find(std::string(100U, 'X')) == std::string::npos);
    CHECK(output.find(bounded.substr(0, 2048U)) != std::string::npos);
    CHECK(output.find(std::string(2049U, 'Y')) == std::string::npos);
    CHECK(output.find("input_truncated=true") != std::string::npos);
}
