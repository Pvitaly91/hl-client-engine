#include <hlclient/platform/windows/stock_external_target_artifact.hpp>
#include <hlclient/platform/windows/stock_external_target_review.hpp>

#include <catch2/catch_test_macros.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace windows = hlclient::platform::windows;

struct MountPointBuffer final {
    ULONG tag;
    USHORT length;
    USHORT reserved;
    USHORT substitute_offset;
    USHORT substitute_length;
    USHORT print_offset;
    USHORT print_length;
    WCHAR names[1];
};

[[nodiscard]] fs::path unique_root()
{
    std::array<wchar_t, 32'768U> module{};
    const DWORD size = ::GetModuleFileNameW(
        nullptr, module.data(), static_cast<DWORD>(module.size()));
    REQUIRE(size > 0U);
    REQUIRE(size < module.size());
    return fs::path{std::wstring_view{module.data(), size}}.parent_path() /
           (L"hlclient-extapproval-test-" +
            std::to_wstring(::GetCurrentProcessId()) + L"-" +
            std::to_wstring(
                std::chrono::steady_clock::now().time_since_epoch().count()));
}

void write_file(const fs::path& path, const std::string_view bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    REQUIRE(output.good());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

[[nodiscard]] std::string read_file(const fs::path& path)
{
    std::ifstream input{path, std::ios::binary};
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

void write_minimal_bsp30(const fs::path& path)
{
    std::array<unsigned char, 124U> bytes{};
    bytes[0] = 30U;
    for (std::size_t lump = 0U; lump < 15U; ++lump) {
        bytes[4U + lump * 8U] =
            static_cast<unsigned char>(bytes.size());
    }
    fs::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    REQUIRE(output.good());
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

void write_appmanifest70(const fs::path& steamapps_root)
{
    write_file(
        steamapps_root / L"appmanifest_70.acf",
        "\"AppState\"\r\n{\r\n\t\"appid\"\t\"70\"\r\n"
        "\t\"installdir\"\t\"Half-Life\"\r\n}\r\n");
}

[[nodiscard]] std::string vdf_path(const fs::path& path)
{
    const auto value = path.lexically_normal().native();
    const int required = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    REQUIRE(required > 0);
    std::string utf8(static_cast<std::size_t>(required), '\0');
    REQUIRE(::WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                static_cast<int>(value.size()), utf8.data(), required,
                nullptr, nullptr) == required);
    std::string escaped;
    for (const char item : utf8) {
        if (item == '\\' || item == '"') escaped.push_back('\\');
        escaped.push_back(item);
    }
    return escaped;
}

void write_libraryfolders70(const fs::path& steamapps_root)
{
    write_file(
        steamapps_root / L"libraryfolders.vdf",
        "\"libraryfolders\"\r\n{\r\n\t\"0\"\r\n\t{\r\n"
        "\t\t\"path\"\t\"" + vdf_path(steamapps_root.parent_path()) +
            "\"\r\n\t\t\"apps\"\r\n\t\t{\r\n"
            "\t\t\t\"70\"\t\"1\"\r\n\t\t}\r\n\t}\r\n}\r\n");
}

[[nodiscard]] bool create_junction(
    const fs::path& link, const fs::path& target)
{
    std::error_code error;
    fs::create_directories(link, error);
    if (error) return false;
    const HANDLE handle = ::CreateFileW(
        link.c_str(), GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const std::wstring substitute = L"\\??\\" + target.native();
    const auto substitute_bytes =
        static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
    const auto print_bytes =
        static_cast<USHORT>(target.native().size() * sizeof(wchar_t));
    const auto bytes = static_cast<std::size_t>(substitute_bytes) +
                       sizeof(wchar_t) + print_bytes + sizeof(wchar_t);
    constexpr auto header = offsetof(MountPointBuffer, substitute_offset);
    std::vector<std::byte> storage(header + 8U + bytes);
    auto* data = reinterpret_cast<MountPointBuffer*>(storage.data());
    data->tag = IO_REPARSE_TAG_MOUNT_POINT;
    data->reserved = 0U;
    data->substitute_offset = 0U;
    data->substitute_length = substitute_bytes;
    data->print_offset =
        static_cast<USHORT>(substitute_bytes + sizeof(wchar_t));
    data->print_length = print_bytes;
    std::copy(substitute.begin(), substitute.end(), data->names);
    auto* print_at = reinterpret_cast<wchar_t*>(
        reinterpret_cast<std::byte*>(data->names) + data->print_offset);
    std::copy(target.native().begin(), target.native().end(), print_at);
    data->length = static_cast<USHORT>(8U + bytes);
    DWORD returned = 0U;
    const BOOL ok = ::DeviceIoControl(
        handle, FSCTL_SET_REPARSE_POINT, data,
        static_cast<DWORD>(header + data->length), nullptr, 0U, &returned,
        nullptr);
    static_cast<void>(::CloseHandle(handle));
    return ok != FALSE;
}

struct Fixture final {
    Fixture() : root{unique_root()}
    {
        const auto deepest_secure_output_path =
            root / L"manual-artifacts" / L"stock-runtime-source-review" /
            std::wstring(32U, L'0') /
            (L".hlclient-output-capability-" +
             std::wstring(32U, L'0') + L".lock");
        REQUIRE(deepest_secure_output_path.native().size() < MAX_PATH);
        fs::create_directories(source_root());
        fs::create_directories(external_root());
        fs::create_directories(root / L"manual-artifacts");
        write_appmanifest70(steamapps_root());
        write_libraryfolders70(steamapps_root());
        write_file(source_root() / L"ordinary.txt", "source");
        write_minimal_bsp30(external_root() / L"maps" / L"arena.bsp");
    }
    ~Fixture()
    {
        static_cast<void>(::RemoveDirectoryW(
            (source_root() / L"shared-assets").c_str()));
        std::error_code error;
        fs::remove_all(root, error);
    }
    [[nodiscard]] bool link_external() const
    {
        return create_junction(source_root() / L"shared-assets",
                               external_root());
    }
    [[nodiscard]] fs::path steamapps_root() const
    {
        return root / L"review-library" / L"steamapps";
    }
    [[nodiscard]] fs::path app_root() const
    {
        return steamapps_root() / L"common" / L"Half-Life";
    }
    [[nodiscard]] fs::path source_root() const
    {
        return app_root() / L"source";
    }
    [[nodiscard]] fs::path external_root() const
    {
        return app_root() / L"external-assets";
    }
    fs::path root;
};

[[nodiscard]] fs::path review_parent(const Fixture& fixture)
{
    return fixture.root / L"manual-artifacts" /
           L"stock-runtime-source-review";
}

[[nodiscard]] windows::StockExternalReviewResult<
    windows::StockExternalApproval>
review_and_approve(Fixture& fixture)
{
    const auto review = windows::review_stock_external_targets(
        fixture.source_root(), review_parent(fixture));
    REQUIRE(review);
    REQUIRE(review.value->all_targets_eligible);
    return windows::approve_stock_external_target_review(
        review.value->review_root,
        windows::kStockExternalTargetApprovalPhraseV1);
}

[[nodiscard]] windows::StockExternalApprovalArtifact read_approval_artifact(
    const fs::path& path)
{
    const auto bytes = read_file(path);
    const auto parsed = windows::parse_stock_external_approval(bytes);
    REQUIRE(parsed);
    return *parsed.value;
}

void rewrite_review_as_canonical_v1(const fs::path& review_root)
{
    const auto request_v2 = windows::parse_stock_external_review_request_v2(
        read_file(review_root / windows::kStockExternalReviewRequestLeaf));
    const auto summary_v2 = windows::parse_stock_external_review_summary_v2(
        read_file(review_root / windows::kStockExternalReviewSummaryLeaf));
    REQUIRE(request_v2);
    REQUIRE(summary_v2);
    REQUIRE(summary_v2.value->targets.size() == 1U);

    const auto private_leaf = windows::stock_external_private_target_leaf(1U);
    REQUIRE(private_leaf);
    const auto private_v2 = windows::parse_stock_external_private_target_v2(
        read_file(review_root / *private_leaf));
    REQUIRE(private_v2);
    REQUIRE(private_v2.value->target_canonical_path);
    REQUIRE(private_v2.value->target_identity);
    REQUIRE(private_v2.value->target_inventory);
    REQUIRE(private_v2.value->eligible);

    constexpr std::string_view v1_profile =
        "hlclient.stock-external-target-review.windows-v1";
    auto request_v1 = *request_v2.value;
    request_v1.implementation_profile = v1_profile;

    windows::StockExternalPrivateTargetArtifact private_v1{};
    private_v1.ordinal = private_v2.value->ordinal;
    private_v1.review_nonce = private_v2.value->review_nonce;
    private_v1.source_root_fingerprint =
        private_v2.value->source_root_fingerprint;
    private_v1.source_link_relative_path =
        private_v2.value->source_link_relative_path;
    private_v1.source_link_identity =
        private_v2.value->source_link_identity;
    private_v1.target_canonical_path =
        *private_v2.value->target_canonical_path;
    private_v1.target_identity = *private_v2.value->target_identity;
    private_v1.target_inventory = *private_v2.value->target_inventory;
    private_v1.classification = private_v2.value->classification;
    private_v1.eligible = private_v2.value->eligible;
    const auto request_json =
        windows::serialize_stock_external_review_request(request_v1);
    const auto private_json =
        windows::serialize_stock_external_private_target(private_v1);
    REQUIRE(request_json);
    REQUIRE(private_json);
    const auto private_digest =
        windows::stock_external_artifact_sha256(*private_json.value);
    REQUIRE(private_digest);

    windows::StockExternalReviewSummaryArtifact summary_v1{};
    summary_v1.review_root_fingerprint =
        summary_v2.value->review_root_fingerprint;
    summary_v1.source_root_fingerprint =
        summary_v2.value->source_root_fingerprint;
    summary_v1.source_inventory = summary_v2.value->source_inventory;
    summary_v1.review_nonce = summary_v2.value->review_nonce;
    summary_v1.review_timestamp_unix_seconds =
        summary_v2.value->review_timestamp_unix_seconds;
    summary_v1.implementation_profile = v1_profile;
    summary_v1.targets.push_back(
        windows::StockExternalReviewTargetBindingArtifact{
            private_v1.ordinal,
            *private_digest.value,
            private_v1.source_link_identity.identity_sha256,
            private_v1.target_identity.identity_sha256,
            private_v1.target_inventory.inventory_sha256,
            private_v1.classification,
            true});
    summary_v1.eligible_count = 1U;
    summary_v1.all_targets_eligible = true;
    const auto summary_json =
        windows::serialize_stock_external_review_summary(summary_v1);
    REQUIRE(summary_json);

    write_file(review_root / windows::kStockExternalReviewRequestLeaf,
               *request_json.value);
    write_file(review_root / *private_leaf, *private_json.value);
    write_file(review_root / windows::kStockExternalReviewSummaryLeaf,
               *summary_json.value);
}

void replace_approval_artifact(
    const fs::path& path,
    const windows::StockExternalApprovalArtifact& artifact)
{
    const auto serialized = windows::serialize_stock_external_approval(artifact);
    REQUIRE(serialized);
    write_file(path, *serialized.value);
}

struct UnexpectedApprovalLeaf final {
    fs::path review_root;
    fs::path unexpected_leaf;
    HANDLE held{INVALID_HANDLE_VALUE};
    bool invoked{false};

    ~UnexpectedApprovalLeaf()
    {
        if (held != INVALID_HANDLE_VALUE) {
            static_cast<void>(::CloseHandle(held));
        }
        if (!unexpected_leaf.empty()) {
            static_cast<void>(::DeleteFileW(unexpected_leaf.c_str()));
        }
    }

    static void inject(
        const windows::StockExternalPublicationTestPhase phase,
        void* const opaque) noexcept
    {
        auto& state = *static_cast<UnexpectedApprovalLeaf*>(opaque);
        state.invoked = true;
        if (phase != windows::StockExternalPublicationTestPhase::
                         approval_manifest_published) {
            return;
        }
        try {
            state.unexpected_leaf =
                state.review_root / L"unexpected-approval-entry.bin";
            state.held = ::CreateFileW(
                state.unexpected_leaf.c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                nullptr);
        } catch (...) {
            state.held = INVALID_HANDLE_VALUE;
        }
    }
};

} // namespace

TEST_CASE(
    "External target approval requires the exact phrase and validates exactly",
    "[windows][stock-runtime][external-target][approval]")
{
    Fixture fixture;
    if (!fixture.link_external()) {
        SKIP("Windows junction fixture capability is unavailable");
    }
    const auto review = windows::review_stock_external_targets(
        fixture.source_root(), review_parent(fixture));
    REQUIRE(review);
    REQUIRE(review.value->all_targets_eligible);

    const auto wrong = windows::approve_stock_external_target_review(
        review.value->review_root, "approve");
    CHECK_FALSE(wrong);
    CHECK(wrong.code ==
          windows::StockExternalReviewErrorCode::approval_phrase_mismatch);

    const auto approved = windows::approve_stock_external_target_review(
        review.value->review_root,
        windows::kStockExternalTargetApprovalPhraseV1);
    REQUIRE(approved);
    CHECK(approved.value->review_set_sha256 ==
          review.value->review_set_sha256);
    const auto validated = windows::validate_stock_external_target_approval(
        approved.value->approval_manifest, fixture.source_root());
    INFO("validation code=" << windows::to_string(validated.code)
                             << " native=" << validated.native_error);
    REQUIRE(validated);
    REQUIRE(validated.value->targets.size() == 1U);
    CHECK(validated.value->review_set_sha256 ==
          review.value->review_set_sha256);
    CHECK_FALSE(validated.value->approval_manifest_sha256.empty());
    CHECK(validated.value->executable_count == 0U);
    CHECK(validated.value->script_or_command_count == 0U);
    CHECK(validated.value->mutable_state_count == 0U);
}

TEST_CASE(
    "External target approval consumes and validates a canonical eligible V1 review",
    "[windows][stock-runtime][external-target][approval][v1-compat]")
{
    Fixture fixture;
    if (!fixture.link_external()) {
        SKIP("Windows junction fixture capability is unavailable");
    }
    const auto review = windows::review_stock_external_targets(
        fixture.source_root(), review_parent(fixture));
    REQUIRE(review);
    REQUIRE(review.value->all_targets_eligible);
    rewrite_review_as_canonical_v1(review.value->review_root);

    const auto approved = windows::approve_stock_external_target_review(
        review.value->review_root,
        windows::kStockExternalTargetApprovalPhraseV1);
    REQUIRE(approved);
    const auto approval = read_approval_artifact(
        approved.value->approval_manifest);
    CHECK(approval.review_schema ==
          windows::kStockExternalReviewSummarySchemaV1);
    CHECK(approval.review_version == 1U);

    const auto validated = windows::validate_stock_external_target_approval(
        approved.value->approval_manifest, fixture.source_root());
    INFO("validation code=" << windows::to_string(validated.code)
                             << " native=" << validated.native_error);
    REQUIRE(validated);
    REQUIRE(validated.value->targets.size() == 1U);
}

TEST_CASE(
    "External target approval rolls back an unconsumable manifest after postcheck failure",
    "[windows][stock-runtime][external-target][approval][publication-rollback]")
{
    Fixture fixture;
    if (!fixture.link_external()) {
        SKIP("Windows junction fixture capability is unavailable");
    }
    const auto review = windows::review_stock_external_targets(
        fixture.source_root(), review_parent(fixture));
    REQUIRE(review);
    REQUIRE(review.value->all_targets_eligible);

    UnexpectedApprovalLeaf injection{};
    injection.review_root = review.value->review_root;
    const windows::StockExternalPublicationTestHook hook{
        &UnexpectedApprovalLeaf::inject, &injection};
    const auto result = windows::approve_stock_external_target_review(
        review.value->review_root,
        windows::kStockExternalTargetApprovalPhraseV1,
        std::chrono::hours{24}, &hook);
    CHECK_FALSE(result);
    CHECK(result.code == windows::StockExternalReviewErrorCode::publication_failed);
    REQUIRE(injection.invoked);
    REQUIRE(injection.held != INVALID_HANDLE_VALUE);
    CHECK(fs::exists(injection.unexpected_leaf));
    const auto approval_path =
        review.value->review_root / windows::kStockExternalApprovalLeaf;
    CHECK_FALSE(fs::exists(approval_path));
    const auto approval = windows::read_stock_external_artifact_leaf(
        review.value->review_root, windows::kStockExternalApprovalLeaf);
    CHECK_FALSE(approval);
}

TEST_CASE(
    "External target approval rejects subsequent target mutation",
    "[windows][stock-runtime][external-target][approval][mutation]")
{
    Fixture fixture;
    if (!fixture.link_external()) {
        SKIP("Windows junction fixture capability is unavailable");
    }
    const auto approved = review_and_approve(fixture);
    REQUIRE(approved);

    write_file(fixture.external_root() / L"maps" / L"arena.bsp",
               "mutated after approval");
    const auto validated = windows::validate_stock_external_target_approval(
        approved.value->approval_manifest, fixture.source_root());
    CHECK_FALSE(validated);
    CHECK(validated.code ==
          windows::StockExternalReviewErrorCode::approval_mismatch);
}

TEST_CASE(
    "External target approval is bound to source inventory and exact target set",
    "[windows][stock-runtime][external-target][approval][binding]")
{
    SECTION("source inventory mutation") {
        Fixture fixture;
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto approved = review_and_approve(fixture);
        REQUIRE(approved);
        write_file(fixture.source_root() / L"ordinary.txt",
                   "changed source inventory");
        const auto result = windows::validate_stock_external_target_approval(
            approved.value->approval_manifest, fixture.source_root());
        CHECK_FALSE(result);
        CHECK(result.code ==
              windows::StockExternalReviewErrorCode::approval_mismatch);
    }

    SECTION("application provenance mutation") {
        Fixture fixture;
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto approved = review_and_approve(fixture);
        REQUIRE(approved);
        write_file(
            fixture.steamapps_root() / L"appmanifest_70.acf",
            "\"AppState\"\n{\n\"appid\" \"70\"\n"
            "\"installdir\" \"Half-Life\"\n}\n");
        const auto result = windows::validate_stock_external_target_approval(
            approved.value->approval_manifest, fixture.source_root());
        CHECK_FALSE(result);
        CHECK(result.code ==
              windows::StockExternalReviewErrorCode::approval_mismatch);
    }

    SECTION("Steam library membership provenance mutation") {
        Fixture fixture;
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto approved = review_and_approve(fixture);
        REQUIRE(approved);
        write_file(
            fixture.steamapps_root() / L"libraryfolders.vdf",
            "\"libraryfolders\"\n{\n}\n");
        const auto result = windows::validate_stock_external_target_approval(
            approved.value->approval_manifest, fixture.source_root());
        CHECK_FALSE(result);
        CHECK(result.code ==
              windows::StockExternalReviewErrorCode::approval_mismatch);
    }

    SECTION("wrong source root") {
        Fixture fixture;
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto approved = review_and_approve(fixture);
        REQUIRE(approved);
        const auto other = fixture.root / L"other-source";
        write_file(other / L"ordinary.txt", "source");
        const auto result = windows::validate_stock_external_target_approval(
            approved.value->approval_manifest, other);
        CHECK_FALSE(result);
        CHECK(result.code ==
              windows::StockExternalReviewErrorCode::approval_mismatch);
    }

    SECTION("unapproved additional target") {
        Fixture fixture;
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto approved = review_and_approve(fixture);
        REQUIRE(approved);
        write_file(fixture.root / L"second-assets" / L"second.bsp", "second");
        const auto second_link = fixture.source_root() / L"second-assets";
        if (!create_junction(second_link, fixture.root / L"second-assets")) {
            SKIP("Second Windows junction fixture capability is unavailable");
        }
        const auto result = windows::validate_stock_external_target_approval(
            approved.value->approval_manifest, fixture.source_root());
        CHECK_FALSE(result);
        CHECK(result.code ==
              windows::StockExternalReviewErrorCode::approval_mismatch);
        static_cast<void>(::RemoveDirectoryW(second_link.c_str()));
    }

    SECTION("approved source link replacement") {
        Fixture fixture;
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto approved = review_and_approve(fixture);
        REQUIRE(approved);
        const auto link = fixture.source_root() / L"shared-assets";
        REQUIRE(::RemoveDirectoryW(link.c_str()) != FALSE);
        write_file(fixture.root / L"replacement-assets" / L"replacement.bsp",
                   "replacement");
        if (!create_junction(link, fixture.root / L"replacement-assets")) {
            SKIP("Replacement Windows junction fixture capability is unavailable");
        }
        const auto result = windows::validate_stock_external_target_approval(
            approved.value->approval_manifest, fixture.source_root());
        CHECK_FALSE(result);
        CHECK(result.code ==
              windows::StockExternalReviewErrorCode::approval_mismatch);
    }

    SECTION("wildcard approval path") {
        Fixture fixture;
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto approved = review_and_approve(fixture);
        REQUIRE(approved);
        const auto result = windows::validate_stock_external_target_approval(
            approved.value->approval_manifest.parent_path() / L"*.json",
            fixture.source_root());
        CHECK_FALSE(result);
        CHECK(result.code ==
              windows::StockExternalReviewErrorCode::invalid_argument);
    }
}

TEST_CASE(
    "External target approval rejects missing consent and ineligible review",
    "[windows][stock-runtime][external-target][approval][reject]")
{
    SECTION("missing token") {
        Fixture fixture;
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto review = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(review);
        const auto result = windows::approve_stock_external_target_review(
            review.value->review_root, {});
        CHECK_FALSE(result);
        CHECK(result.code == windows::StockExternalReviewErrorCode::
                                 approval_phrase_mismatch);
    }

    SECTION("ineligible target") {
        Fixture fixture;
        write_file(fixture.external_root() / L"payload.dat",
                   "MZ disguised executable");
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto review = windows::review_stock_external_targets(
            fixture.source_root(), review_parent(fixture));
        REQUIRE(review);
        REQUIRE_FALSE(review.value->all_targets_eligible);
        const auto result = windows::approve_stock_external_target_review(
            review.value->review_root,
            windows::kStockExternalTargetApprovalPhraseV1);
        CHECK_FALSE(result);
        CHECK(result.code ==
              windows::StockExternalReviewErrorCode::target_ineligible);
    }
}

TEST_CASE(
    "External target approval rejects an expired manifest",
    "[windows][stock-runtime][external-target][approval][expiry]")
{
    Fixture fixture;
    if (!fixture.link_external()) {
        SKIP("Windows junction fixture capability is unavailable");
    }
    const auto approved = review_and_approve(fixture);
    REQUIRE(approved);

    std::ifstream input{approved.value->approval_manifest, std::ios::binary};
    REQUIRE(input.good());
    std::string bytes{std::istreambuf_iterator<char>{input},
                      std::istreambuf_iterator<char>{}};
    input.close();
    const auto parsed = windows::parse_stock_external_approval(bytes);
    REQUIRE(parsed);
    auto expired = *parsed.value;
    expired.expiration_unix_seconds =
        expired.approval_timestamp_unix_seconds + 1U;
    const auto serialized = windows::serialize_stock_external_approval(expired);
    REQUIRE(serialized);
    write_file(approved.value->approval_manifest, *serialized.value);
    std::this_thread::sleep_for(std::chrono::seconds{2});

    const auto validated = windows::validate_stock_external_target_approval(
        approved.value->approval_manifest, fixture.source_root());
    CHECK_FALSE(validated);
    CHECK(validated.code ==
          windows::StockExternalReviewErrorCode::approval_expired);
}

TEST_CASE(
    "External target approval rejects canonical binding tampering",
    "[windows][stock-runtime][external-target][approval][artifact-tamper]")
{
    SECTION("wrong review digest") {
        Fixture fixture;
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto approved = review_and_approve(fixture);
        REQUIRE(approved);
        auto artifact =
            read_approval_artifact(approved.value->approval_manifest);
        artifact.review_digest_sha256.assign(64U, '0');
        replace_approval_artifact(
            approved.value->approval_manifest, artifact);
        const auto result = windows::validate_stock_external_target_approval(
            approved.value->approval_manifest, fixture.source_root());
        CHECK_FALSE(result);
        CHECK(result.code ==
              windows::StockExternalReviewErrorCode::approval_mismatch);
    }

    SECTION("wrong target identity") {
        Fixture fixture;
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto approved = review_and_approve(fixture);
        REQUIRE(approved);
        auto artifact =
            read_approval_artifact(approved.value->approval_manifest);
        REQUIRE(artifact.approved_targets.size() == 1U);
        artifact.approved_targets.front().target_identity_sha256.assign(
            64U, '0');
        replace_approval_artifact(
            approved.value->approval_manifest, artifact);
        const auto result = windows::validate_stock_external_target_approval(
            approved.value->approval_manifest, fixture.source_root());
        CHECK_FALSE(result);
        CHECK(result.code ==
              windows::StockExternalReviewErrorCode::approval_mismatch);
    }

    SECTION("wrong target inventory digest") {
        Fixture fixture;
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto approved = review_and_approve(fixture);
        REQUIRE(approved);
        auto artifact =
            read_approval_artifact(approved.value->approval_manifest);
        REQUIRE(artifact.approved_targets.size() == 1U);
        artifact.approved_targets.front().target_inventory_sha256.assign(
            64U, '0');
        replace_approval_artifact(
            approved.value->approval_manifest, artifact);
        const auto result = windows::validate_stock_external_target_approval(
            approved.value->approval_manifest, fixture.source_root());
        CHECK_FALSE(result);
        CHECK(result.code ==
              windows::StockExternalReviewErrorCode::approval_mismatch);
    }

    SECTION("duplicate approved target") {
        Fixture fixture;
        if (!fixture.link_external()) {
            SKIP("Windows junction fixture capability is unavailable");
        }
        const auto approved = review_and_approve(fixture);
        REQUIRE(approved);
        auto artifact =
            read_approval_artifact(approved.value->approval_manifest);
        REQUIRE(artifact.approved_targets.size() == 1U);
        artifact.approved_targets.push_back(
            artifact.approved_targets.front());
        artifact.approval_count = artifact.approved_targets.size();
        const auto serialized =
            windows::serialize_stock_external_approval(artifact);
        CHECK_FALSE(serialized);
        CHECK(serialized.code ==
              windows::StockExternalArtifactErrorCode::invalid_property_value);
    }
}
