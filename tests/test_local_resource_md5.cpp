#define HLCLIENT_LOCAL_RESOURCE_TEST_ACCESS

#include <hlclient/hash/md5.hpp>
#include <hlclient/local_resources/local_resource_file_inspection.hpp>
#include <hlclient/local_resources/local_resource_resolver.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>

#include "../src/local_resources/win32_local_resource_detail.hpp"
#include "local_resource_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

namespace hash = hlclient::hash;
namespace local = hlclient::local_resources;
using hlclient::tests::ScopedLocalResourceTestRoot;

class WritableMappingGuard final {
public:
    WritableMappingGuard(HANDLE mapping, void* view) noexcept
        : mapping_{mapping}, view_{view}
    {
    }

    ~WritableMappingGuard()
    {
        if (view_ != nullptr) {
            static_cast<void>(::UnmapViewOfFile(view_));
        }
        if (mapping_ != nullptr) {
            static_cast<void>(::CloseHandle(mapping_));
        }
    }

    WritableMappingGuard(const WritableMappingGuard&) = delete;
    WritableMappingGuard& operator=(const WritableMappingGuard&) = delete;

private:
    HANDLE mapping_{nullptr};
    void* view_{nullptr};
};

[[nodiscard]] constexpr std::uint8_t hex_nibble(const char value) noexcept
{
    return value >= '0' && value <= '9'
               ? static_cast<std::uint8_t>(value - '0')
               : static_cast<std::uint8_t>(10 + value - 'a');
}

[[nodiscard]] hash::Md5Digest digest_literal(const std::string_view hex)
{
    hash::Md5Digest digest{};
    if (hex.size() != digest.size() * 2U) {
        throw std::runtime_error{"Invalid test digest literal"};
    }
    for (std::size_t index = 0U; index < digest.size(); ++index) {
        digest[index] = static_cast<std::byte>(
            static_cast<std::uint8_t>(
                (hex_nibble(hex[index * 2U]) << 4U) |
                hex_nibble(hex[index * 2U + 1U])));
    }
    return digest;
}

[[nodiscard]] std::unique_ptr<local::LocalResourceResolver> make_resolver(
    ScopedLocalResourceTestRoot& temporary,
    const local::LocalResourceResolverLimits limits = {})
{
    auto roots =
        local::LocalResourceSearchRoots::create(temporary.path(), "valve");
    if (!roots) {
        throw std::runtime_error{"Unable to create synthetic search roots"};
    }
    auto resolver =
        local::LocalResourceResolver::create(std::move(*roots.roots), limits);
    if (!resolver) {
        throw std::runtime_error{"Unable to create synthetic resolver"};
    }
    return std::move(resolver.resolver);
}

TEST_CASE("Local file inspection streams known MD5-compatible material",
          "[local-resource][md5]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "abc.bin", "abc");
    auto resolver = make_resolver(temporary);
    auto resolved = resolver->resolve("abc.bin");
    REQUIRE(resolved);

    const local::LocalResourceFileInspectionLimits limits{
        local::kDefaultMaximumLocalResourceFileSize,
        1U,
        true,
    };
    const auto inspected =
        local::inspect_local_resource_file(*resolved.file, limits);
    REQUIRE(inspected);
    CHECK(inspected.inspection->byte_count == 3U);
    CHECK(inspected.inspection->compatibility_md5 ==
          digest_literal("900150983cd24fb0d6963f7d28e17f72"));
    CHECK(inspected.inspection->compatibility_md5.size() == 16U);
    CHECK(inspected.inspection->read_chunk_size == 1U);
    CHECK(resolved.file->bytes_consumed() == 3U);
}

TEST_CASE("Local file inspection spans exact and adjacent chunk boundaries",
          "[local-resource][md5][bounds]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write_repeated("valve", "exact.bin", 65'536U);
    temporary.write_repeated("valve", "adjacent.bin", 65'537U);
    auto resolver = make_resolver(temporary);

    auto exact = resolver->resolve("exact.bin");
    REQUIRE(exact);
    const auto exact_result = local::inspect_local_resource_file(
        *exact.file,
        local::LocalResourceFileInspectionLimits{
            local::kDefaultMaximumLocalResourceFileSize,
            65'536U,
            true});
    REQUIRE(exact_result);
    CHECK(exact_result.inspection->compatibility_md5 ==
          digest_literal("2d61aa54b58c2e94403fb092c3dbc027"));

    auto adjacent = resolver->resolve("adjacent.bin");
    REQUIRE(adjacent);
    const auto adjacent_result = local::inspect_local_resource_file(
        *adjacent.file,
        local::LocalResourceFileInspectionLimits{
            local::kDefaultMaximumLocalResourceFileSize,
            55U,
            true});
    REQUIRE(adjacent_result);
    CHECK(adjacent_result.inspection->byte_count == 65'537U);
    CHECK(adjacent_result.inspection->compatibility_md5 ==
          digest_literal("b3c6fc238e908636e53aabd5ad830cf7"));
}

TEST_CASE("Local file inspection applies explicit empty-file policy",
          "[local-resource][md5][bounds]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "empty.bin", "");

    SECTION("current consistency profile rejects empty files")
    {
        auto resolver = make_resolver(temporary);
        auto resolved = resolver->resolve("empty.bin");
        REQUIRE(resolved);
        const auto result = local::inspect_local_resource_file(*resolved.file);
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              local::LocalResourceFileInspectionErrorCode::empty_file);
        CHECK(resolved.file->bytes_consumed() == 0U);
    }

    SECTION("general inspection can explicitly allow an empty snapshot")
    {
        auto resolver = make_resolver(temporary);
        auto resolved = resolver->resolve("empty.bin");
        REQUIRE(resolved);
        const auto result = local::inspect_local_resource_file(
            *resolved.file,
            local::LocalResourceFileInspectionLimits{
                local::kDefaultMaximumLocalResourceFileSize,
                local::kDefaultLocalResourceReadChunkSize,
                false});
        REQUIRE(result);
        CHECK(result.inspection->byte_count == 0U);
        CHECK(result.inspection->compatibility_md5 ==
              digest_literal("d41d8cd98f00b204e9800998ecf8427e"));
    }
}

TEST_CASE("Local file inspection enforces size chunk and lifecycle bounds",
          "[local-resource][md5][bounds]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write_repeated("valve", "bounded.bin", 33U);
    auto resolver = make_resolver(
        temporary,
        local::LocalResourceResolverLimits{64U, 64U});

    SECTION("inspection maximum is independent and exact")
    {
        auto resolved = resolver->resolve("bounded.bin");
        REQUIRE(resolved);
        const auto result = local::inspect_local_resource_file(
            *resolved.file,
            local::LocalResourceFileInspectionLimits{32U, 16U, true});
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              local::LocalResourceFileInspectionErrorCode::too_large);
    }

    SECTION("hard chunk limit plus one is invalid")
    {
        auto resolved = resolver->resolve("bounded.bin");
        REQUIRE(resolved);
        const auto result = local::inspect_local_resource_file(
            *resolved.file,
            local::LocalResourceFileInspectionLimits{
                64U,
                local::kHardMaximumLocalResourceReadChunkSize + 1U,
                true});
        REQUIRE_FALSE(result);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              local::LocalResourceFileInspectionErrorCode::
                  invalid_configuration);
    }

    SECTION("the same handle cannot be hashed twice")
    {
        auto resolved = resolver->resolve("bounded.bin");
        REQUIRE(resolved);
        const auto first = local::inspect_local_resource_file(
            *resolved.file,
            local::LocalResourceFileInspectionLimits{64U, 8U, true});
        REQUIRE(first);
        const auto second = local::inspect_local_resource_file(
            *resolved.file,
            local::LocalResourceFileInspectionLimits{64U, 8U, true});
        REQUIRE_FALSE(second);
        REQUIRE(second.error);
        CHECK(second.error->code ==
              local::LocalResourceFileInspectionErrorCode::invalid_state);
    }
}

TEST_CASE("Local file inspection proves same-handle final metadata",
          "[local-resource][md5][toctou]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "stable.bin", "message digest");
    auto resolver = make_resolver(temporary);
    auto resolved = resolver->resolve("stable.bin");
    REQUIRE(resolved);
    const auto initial = resolved.file->initial_snapshot();
    const auto result = local::inspect_local_resource_file(
        *resolved.file,
        local::LocalResourceFileInspectionLimits{128U, 3U, true});
    REQUIRE(result);
    const auto final = resolved.file->metadata_snapshot();
    REQUIRE(final);
    CHECK(*final.snapshot == initial);
    CHECK(result.inspection->compatibility_md5 ==
          digest_literal("f96b697d7cb7938d525a2f31aaf161d0"));
}

TEST_CASE("Local resolver rejects a pre-existing writable mapping",
          "[local-resource][md5][toctou][sharing]")
{
    ScopedLocalResourceTestRoot temporary;
    constexpr std::size_t file_size = 64U * 1024U;
    temporary.write_repeated("valve", "mutable.bin", file_size);
    const auto native_path = temporary.game_path("valve") / "mutable.bin";

    const HANDLE writable = ::CreateFileW(
        native_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    REQUIRE(writable != INVALID_HANDLE_VALUE);
    const HANDLE mapping = ::CreateFileMappingW(
        writable,
        nullptr,
        PAGE_READWRITE,
        0U,
        0U,
        nullptr);
    static_cast<void>(::CloseHandle(writable));
    REQUIRE(mapping != nullptr);
    void* const view =
        ::MapViewOfFile(mapping, FILE_MAP_WRITE, 0U, 0U, file_size);
    REQUIRE(view != nullptr);
    WritableMappingGuard mapping_guard{mapping, view};

    auto resolver = make_resolver(temporary);
    auto resolved = resolver->resolve("mutable.bin");
    REQUIRE_FALSE(resolved);
    CHECK(resolved.code == local::LocalResourceResolutionCode::io_error);
}

TEST_CASE("Local file inspection rejects changed same-handle metadata",
          "[local-resource][md5][toctou][mutation]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "mutable.bin", "stable bytes");
    auto resolver = make_resolver(temporary);
    auto resolved = resolver->resolve("mutable.bin");
    REQUIRE(resolved);

    local::detail::LocalReadOnlyFileTestAccess::simulate_change_metadata(
        *resolved.file);
    const auto result =
        local::inspect_local_resource_file(*resolved.file);
    REQUIRE_FALSE(result);
    CHECK_FALSE(result.inspection.has_value());
    REQUIRE(result.error);
    CHECK(result.error->code ==
          local::LocalResourceFileInspectionErrorCode::state_changed);
}

TEST_CASE("Local file inspection rejects a stable-metadata short read",
          "[local-resource][md5][short-read]")
{
    ScopedLocalResourceTestRoot temporary;
    temporary.write("valve", "short.bin", "short read fixture");
    auto resolver = make_resolver(temporary);
    auto resolved = resolver->resolve("short.bin");
    REQUIRE(resolved);
    REQUIRE(local::detail::LocalReadOnlyFileTestAccess::
                seek_native_handle_to_end_without_tracking(*resolved.file));

    const auto result =
        local::inspect_local_resource_file(*resolved.file);
    REQUIRE_FALSE(result);
    CHECK_FALSE(result.inspection.has_value());
    REQUIRE(result.error);
    CHECK(result.error->code ==
          local::LocalResourceFileInspectionErrorCode::read_failed);
}

} // namespace
