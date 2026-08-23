#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

namespace hlclient::tests {

class ScopedLocalResourceTestRoot final {
public:
    ScopedLocalResourceTestRoot()
        : temporary_parent_{
              std::filesystem::temp_directory_path().lexically_normal()}
    {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        for (std::size_t attempt = 0U; attempt < 100U; ++attempt) {
            auto candidate = temporary_parent_ /
                             (std::string{"hlclient-LOCAL-RESOURCE-TEST-"} +
                              std::to_string(::GetCurrentProcessId()) + '-' +
                              std::to_string(stamp) + '-' +
                              std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                root_ = std::move(candidate);
                create_game("valve");
                return;
            }
            if (error) {
                throw std::runtime_error{
                    "Unable to create synthetic local-resource test root"};
            }
        }
        throw std::runtime_error{
            "Unable to allocate synthetic local-resource test root"};
    }

    ~ScopedLocalResourceTestRoot()
    {
        const auto normalized = root_.lexically_normal();
        if (!normalized.empty() && normalized.parent_path() == temporary_parent_ &&
            normalized.filename().string().starts_with(
                "hlclient-LOCAL-RESOURCE-TEST-")) {
            std::error_code ignored;
            std::filesystem::remove_all(normalized, ignored);
        }
    }

    ScopedLocalResourceTestRoot(const ScopedLocalResourceTestRoot&) = delete;
    ScopedLocalResourceTestRoot& operator=(
        const ScopedLocalResourceTestRoot&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return root_;
    }

    [[nodiscard]] std::filesystem::path game_path(
        const std::string_view game) const
    {
        return root_ / std::string{game};
    }

    void create_game(const std::string_view game) const
    {
        std::error_code error;
        if (!std::filesystem::create_directories(game_path(game), error) &&
            error) {
            throw std::runtime_error{
                "Unable to create synthetic local-resource game root"};
        }
    }

    void write(
        const std::string_view game,
        const std::filesystem::path& relative,
        const std::span<const std::byte> bytes) const
    {
        const auto target = game_path(game) / relative;
        std::error_code error;
        std::filesystem::create_directories(target.parent_path(), error);
        if (error) {
            throw std::runtime_error{
                "Unable to create synthetic local-resource directory"};
        }
        std::ofstream stream{target, std::ios::binary | std::ios::trunc};
        if (!stream) {
            throw std::runtime_error{
                "Unable to create synthetic local-resource file"};
        }
        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            throw std::runtime_error{
                "Unable to write synthetic local-resource file"};
        }
    }

    void write(
        const std::string_view game,
        const std::filesystem::path& relative,
        const std::string_view bytes) const
    {
        write(
            game,
            relative,
            std::span<const std::byte>{
                reinterpret_cast<const std::byte*>(bytes.data()),
                bytes.size()});
    }

    void write_repeated(
        const std::string_view game,
        const std::filesystem::path& relative,
        const std::size_t size,
        const std::byte value = std::byte{0x61U}) const
    {
        const std::vector<std::byte> bytes(size, value);
        write(game, relative, std::span<const std::byte>{bytes});
    }

private:
    std::filesystem::path temporary_parent_;
    std::filesystem::path root_;
};

[[nodiscard]] inline bool create_directory_link_if_supported(
    const std::filesystem::path& target,
    const std::filesystem::path& link,
    std::error_code& error)
{
    error.clear();
    std::filesystem::create_directory_symlink(target, link, error);
    return !error;
}

[[nodiscard]] inline bool enable_case_sensitive_directory(
    const std::filesystem::path& directory)
{
    const HANDLE raw = ::CreateFileW(
        directory.c_str(),
        FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (raw == INVALID_HANDLE_VALUE) {
        return false;
    }
    FILE_CASE_SENSITIVE_INFO information{};
    information.Flags = FILE_CS_FLAG_CASE_SENSITIVE_DIR;
    const bool succeeded = ::SetFileInformationByHandle(
                               raw,
                               FileCaseSensitiveInfo,
                               &information,
                               sizeof(information)) != FALSE;
    static_cast<void>(::CloseHandle(raw));
    return succeeded;
}

} // namespace hlclient::tests
