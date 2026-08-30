#include <hlclient/platform/windows/secure_output.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

namespace {

namespace fs = std::filesystem;
namespace windows = hlclient::platform::windows;

class ExactTemporaryDirectory final {
public:
    ExactTemporaryDirectory()
    {
        std::wstring buffer(32'768U, L'\0');
        const auto length = ::GetTempPathW(
            static_cast<DWORD>(buffer.size()), buffer.data());
        if (length == 0U || length >= buffer.size()) return;
        buffer.resize(length);
        // Hosted Windows runners may expose their temporary directory through
        // a runner-owned junction.  Secure output deliberately rejects an
        // unresolved reparse-backed path, so build the disposable fixture
        // beneath the resolved target instead of weakening that boundary.
        std::error_code canonical_error;
        parent_ = fs::canonical(
            fs::path{std::move(buffer)}, canonical_error).lexically_normal();
        if (canonical_error || parent_.empty() || !parent_.is_absolute()) {
            parent_.clear();
            return;
        }
        static std::atomic_uint32_t ordinal{0U};
        for (std::uint32_t attempt = 0U; attempt < 32U; ++attempt) {
            const auto name = L"hlclient-secure-output-test-" +
                std::to_wstring(::GetCurrentProcessId()) + L"-" +
                std::to_wstring(::GetTickCount64()) + L"-" +
                std::to_wstring(ordinal.fetch_add(1U));
            const auto candidate = (parent_ / name).lexically_normal();
            if (::CreateDirectoryW(candidate.c_str(), nullptr) != FALSE) {
                path_ = candidate;
                break;
            }
        }
    }

    ~ExactTemporaryDirectory()
    {
        if (!path_.empty() && path_.is_absolute() &&
            path_.parent_path() == parent_ &&
            path_.filename().wstring().starts_with(
                L"hlclient-secure-output-test-")) {
            std::error_code error;
            static_cast<void>(fs::remove_all(path_, error));
        }
    }

    ExactTemporaryDirectory(const ExactTemporaryDirectory&) = delete;
    ExactTemporaryDirectory& operator=(const ExactTemporaryDirectory&) = delete;

    [[nodiscard]] bool valid() const noexcept { return !path_.empty(); }
    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path parent_;
    fs::path path_;
};

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text)
{
    const auto view = std::as_bytes(std::span{text.data(), text.size()});
    return {view.begin(), view.end()};
}

[[nodiscard]] bool write_text(
    const fs::path& path, const std::string_view text)
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    return static_cast<bool>(output);
}

[[nodiscard]] std::string read_text(const fs::path& path)
{
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("Secure output publishes new files atomically without predictable-temp reuse",
          "[windows][stock-runtime][secure-output]")
{
    ExactTemporaryDirectory temporary;
    REQUIRE(temporary.valid());
    const auto output = temporary.path() / L"run";
    REQUIRE(::CreateDirectoryW(output.c_str(), nullptr) != FALSE);
    REQUIRE(write_text(output / L"capture.bin.tmp", "attacker-owned"));

    const auto opened = windows::open_secure_output_directory(output);
    INFO("open code=" << (opened.error
            ? windows::to_string(opened.error->code) : "none")
         << " os=" << (opened.error
            ? opened.error->operating_system_error : 0U));
    REQUIRE(opened);
    REQUIRE(opened.directory);
    const auto payload = bytes("exact-capture-bytes");
    const auto written = windows::secure_atomic_write_new(
        *opened.directory, L"capture.bin", payload);
    INFO("write code=" << (written.error
            ? windows::to_string(written.error->code) : "none")
         << " os=" << (written.error
            ? written.error->operating_system_error : 0U));
    REQUIRE(written);
    CHECK(read_text(output / L"capture.bin") == "exact-capture-bytes");
    CHECK(read_text(output / L"capture.bin.tmp") == "attacker-owned");

    std::size_t temporary_count = 0U;
    for (const auto& entry : fs::directory_iterator{output}) {
        if (entry.path().filename().wstring().starts_with(
                L".hlclient-stock-runtime-")) {
            ++temporary_count;
        }
    }
    CHECK(temporary_count == 0U);
}

TEST_CASE("Secure output never replaces an existing hardlink destination",
          "[windows][stock-runtime][secure-output][hardlink]")
{
    ExactTemporaryDirectory temporary;
    REQUIRE(temporary.valid());
    const auto output = temporary.path() / L"run";
    REQUIRE(::CreateDirectoryW(output.c_str(), nullptr) != FALSE);
    const auto victim = temporary.path() / L"victim.bin";
    REQUIRE(write_text(victim, "do-not-change"));
    const auto destination = output / L"capture.bin";
    REQUIRE(::CreateHardLinkW(destination.c_str(), victim.c_str(), nullptr) !=
            FALSE);

    const auto opened = windows::open_secure_output_directory(output);
    INFO("open code=" << (opened.error
            ? windows::to_string(opened.error->code) : "none")
         << " os=" << (opened.error
            ? opened.error->operating_system_error : 0U));
    REQUIRE(opened);
    REQUIRE(opened.directory);
    const auto payload = bytes("replacement");
    const auto rejected = windows::secure_atomic_write_new(
        *opened.directory, L"capture.bin", payload);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
          windows::SecureOutputErrorCode::destination_exists);
    CHECK(read_text(victim) == "do-not-change");
    CHECK(read_text(destination) == "do-not-change");
}

TEST_CASE("Held secure output directory blocks replacement and rejects unsafe leaves",
          "[windows][stock-runtime][secure-output][directory-capability]")
{
    ExactTemporaryDirectory temporary;
    REQUIRE(temporary.valid());
    const auto parent = temporary.path() / L"parent";
    const auto output = parent / L"run";
    const auto renamed_parent = temporary.path() / L"replaced-parent";
    REQUIRE(::CreateDirectoryW(parent.c_str(), nullptr) != FALSE);
    REQUIRE(::CreateDirectoryW(output.c_str(), nullptr) != FALSE);

    {
        const auto opened = windows::open_secure_output_directory(output);
        INFO("open code=" << (opened.error
                ? windows::to_string(opened.error->code) : "none")
             << " os=" << (opened.error
                ? opened.error->operating_system_error : 0U));
        REQUIRE(opened);
        REQUIRE(opened.directory);
        CHECK_FALSE(::MoveFileExW(
            parent.c_str(), renamed_parent.c_str(), 0U));
        CHECK(fs::exists(output));
        CHECK_FALSE(fs::exists(renamed_parent));

        const auto payload = bytes("x");
        for (const auto leaf : {L"..", L"nested/file", L"stream:ads"}) {
            const auto rejected = windows::secure_atomic_write_new(
                *opened.directory, leaf, payload);
            REQUIRE_FALSE(rejected);
            REQUIRE(rejected.error);
            CHECK(rejected.error->code ==
                  windows::SecureOutputErrorCode::invalid_leaf_name);
        }
    }

    REQUIRE(::MoveFileExW(
        parent.c_str(), renamed_parent.c_str(), 0U) != FALSE);
    CHECK_FALSE(fs::exists(parent));
    CHECK(fs::exists(renamed_parent / L"run"));
}
