#include <hlclient/app/authentication_material_file.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace app = hlclient::app;

inline constexpr std::string_view kSyntheticMarker = "TEST_AUTH_MATERIAL";

static_assert(!std::is_copy_constructible_v<app::AuthenticationMaterialFileLoadResult>);
static_assert(!std::is_copy_assignable_v<app::AuthenticationMaterialFileLoadResult>);
static_assert(std::is_move_constructible_v<app::AuthenticationMaterialFileLoadResult>);
static_assert(std::is_move_assignable_v<app::AuthenticationMaterialFileLoadResult>);

class ScopedTemporaryDirectory final {
public:
    ScopedTemporaryDirectory()
        : temporary_root_{std::filesystem::temp_directory_path().lexically_normal()}
    {
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (std::size_t attempt = 0; attempt < 100U; ++attempt) {
            const auto name = std::string{"hlclient-TEST_AUTH_MATERIAL-"} +
                              std::to_string(timestamp) + '-' + std::to_string(attempt);
            auto candidate = temporary_root_ / name;
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = std::move(candidate);
                return;
            }
            if (error) {
                throw std::runtime_error{"Unable to create synthetic test directory"};
            }
        }
        throw std::runtime_error{"Unable to allocate a synthetic test directory"};
    }

    ~ScopedTemporaryDirectory()
    {
        const auto normalized = path_.lexically_normal();
        if (!normalized.empty() && normalized.parent_path() == temporary_root_ &&
            normalized.filename().string().starts_with("hlclient-TEST_AUTH_MATERIAL-")) {
            std::error_code ignored;
            std::filesystem::remove_all(normalized, ignored);
        }
    }

    ScopedTemporaryDirectory(const ScopedTemporaryDirectory&) = delete;
    ScopedTemporaryDirectory& operator=(const ScopedTemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path temporary_root_;
    std::filesystem::path path_;
};

[[nodiscard]] std::vector<std::byte> synthetic_bytes(const std::size_t size)
{
    std::vector<std::byte> bytes(size);
    for (std::size_t index = 0; index < size; ++index) {
        bytes[index] = static_cast<std::byte>(
            static_cast<unsigned char>(kSyntheticMarker[index % kSyntheticMarker.size()]));
    }
    return bytes;
}

void write_bytes(const std::filesystem::path& path, const std::span<const std::byte> bytes)
{
    std::ofstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"Unable to create synthetic authentication test input"};
    }
    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error{"Unable to write synthetic authentication test input"};
    }
}

void check_sanitized_error(
    const app::AuthenticationMaterialFileLoadResult& result,
    const std::filesystem::path& path)
{
    REQUIRE_FALSE(result);
    REQUIRE(result.error);
    CHECK(result.error->context.find(kSyntheticMarker) == std::string::npos);
    CHECK(result.error->context.find(path.string()) == std::string::npos);
}

TEST_CASE("Authentication material file loader reads one exact bounded record",
          "[app][authentication]")
{
    ScopedTemporaryDirectory temporary;
    const auto path = temporary.path() / "TEST_AUTH_MATERIAL.bin";
    const auto bytes = synthetic_bytes(app::kAuthenticationMaterialFileSize);
    write_bytes(path, bytes);

    auto result = app::load_authentication_material_file(path);

    REQUIRE(result);
    REQUIRE(result.material);
    CHECK(result.material->total_size() == app::kAuthenticationMaterialFileSize);
    CHECK(result.material->matches(
        std::span<const std::byte>{bytes}.first(app::kAuthenticationMaterialProtectedFileSize),
        std::span<const std::byte>{bytes}.subspan(
            app::kAuthenticationMaterialProtectedFileSize)));
}

TEST_CASE("Authentication material file loader rejects every adjacent size",
          "[app][authentication]")
{
    ScopedTemporaryDirectory temporary;

    for (const auto size : {app::kAuthenticationMaterialFileSize - 1U,
                            app::kAuthenticationMaterialFileSize + 1U}) {
        const auto path = temporary.path() /
                          (std::string{"TEST_AUTH_MATERIAL-"} + std::to_string(size) + ".bin");
        const auto bytes = synthetic_bytes(size);
        write_bytes(path, bytes);

        const auto result = app::load_authentication_material_file(path);

        INFO("synthetic byte count " << size);
        check_sanitized_error(result, path);
        CHECK(result.error->code == app::AuthenticationMaterialFileErrorCode::invalid_size);
    }
}

TEST_CASE("Authentication material file loader rejects a forbidden protected byte",
          "[app][authentication]")
{
    ScopedTemporaryDirectory temporary;
    const auto path = temporary.path() / "TEST_AUTH_MATERIAL-invalid.bin";
    auto bytes = synthetic_bytes(app::kAuthenticationMaterialFileSize);
    bytes[7] = std::byte{0};
    write_bytes(path, bytes);

    const auto result = app::load_authentication_material_file(path);

    check_sanitized_error(result, path);
    CHECK(result.error->code == app::AuthenticationMaterialFileErrorCode::invalid_material);
}

TEST_CASE("Authentication material file loader requires a regular local file",
          "[app][authentication]")
{
    ScopedTemporaryDirectory temporary;

    const auto result = app::load_authentication_material_file(temporary.path());

    check_sanitized_error(result, temporary.path());
    CHECK(result.error->code == app::AuthenticationMaterialFileErrorCode::not_regular_file);
}

} // namespace
