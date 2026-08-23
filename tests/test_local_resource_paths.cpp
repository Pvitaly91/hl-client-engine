#include <hlclient/goldsrc/local_resource_mapping.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::GoldSrcResourceNameClassification classify_model(
    const std::string_view bytes,
    const goldsrc::GoldSrcResourceNameLimits limits = {})
{
    return goldsrc::GoldSrcResourceNameClassifier{
        goldsrc::GoldSrcLocalResourceMappingProfile::
            stock_protocol_48_standard,
        limits,
    }
        .classify(goldsrc::ResourceType::model, bytes);
}

void check_unsafe(
    const std::string_view bytes,
    const goldsrc::GoldSrcResourceNameIssue expected_issue)
{
    const auto result = classify_model(bytes);
    CHECK(result.kind() ==
          goldsrc::GoldSrcResourceNameClassificationKind::unsafe_name);
    CHECK(result.issue() == expected_issue);
    CHECK(result.original_name_byte_length() == bytes.size());
    CHECK_FALSE(result.safe_virtual_name());
    CHECK_FALSE(result.file_backed());
}

[[nodiscard]] std::string safe_name_with_length(const std::size_t length)
{
    std::string result;
    result.reserve(length);

    std::size_t remaining = length;
    while (remaining > goldsrc::kMaximumLocalResourceComponentBytes) {
        const std::size_t component_length = (std::min)(
            goldsrc::kMaximumLocalResourceComponentBytes,
            remaining - 2U);
        result.append(component_length, 'a');
        result.push_back('/');
        remaining -= component_length + 1U;
    }
    result.append(remaining, 'a');
    return result;
}

TEST_CASE("GoldSrc local resource classifier preserves valid virtual names",
          "[goldsrc][local-resource][paths]")
{
    const auto simple = classify_model("models/barney.mdl");
    REQUIRE(simple.safe_virtual_name());
    CHECK(simple.kind() ==
          goldsrc::GoldSrcResourceNameClassificationKind::mapped_file);
    CHECK(simple.issue() == goldsrc::GoldSrcResourceNameIssue::none);
    CHECK(simple.original_name_byte_length() == 17U);
    CHECK(simple.safe_virtual_name()->value() == "models/barney.mdl");
    CHECK(simple.safe_virtual_name()->byte_length() == 17U);
    CHECK(simple.safe_virtual_name()->component_count() == 2U);
    CHECK(simple.file_backed());

    const auto nested = classify_model("models/player/gordon/gordon.mdl");
    REQUIRE(nested.safe_virtual_name());
    CHECK(nested.safe_virtual_name()->value() ==
          "models/player/gordon/gordon.mdl");
    CHECK(nested.safe_virtual_name()->component_count() == 4U);
}

TEST_CASE("GoldSrc local resource classifier rejects rooted and native paths",
          "[goldsrc][local-resource][paths][unsafe]")
{
    check_unsafe("/models/barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::absolute_or_rooted_path);
    check_unsafe("\\models\\barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::absolute_or_rooted_path);
    check_unsafe("C:/Half-Life/valve/barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::drive_qualified_path);
    check_unsafe("C:models/barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::drive_qualified_path);
    check_unsafe("//server/share/barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::unc_or_device_path);
    check_unsafe("\\\\server\\share\\barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::unc_or_device_path);
    check_unsafe("\\\\?\\C:\\Half-Life\\barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::unc_or_device_path);
    check_unsafe("\\\\.\\pipe\\resource",
                 goldsrc::GoldSrcResourceNameIssue::unc_or_device_path);
}

TEST_CASE("GoldSrc local resource classifier rejects traversal and empty components",
          "[goldsrc][local-resource][paths][unsafe]")
{
    check_unsafe(".", goldsrc::GoldSrcResourceNameIssue::dot_component);
    check_unsafe("models/./barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::dot_component);
    check_unsafe("..", goldsrc::GoldSrcResourceNameIssue::parent_component);
    check_unsafe("models/../barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::parent_component);
    check_unsafe("models//barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::empty_component);
    check_unsafe("models/barney.mdl/",
                 goldsrc::GoldSrcResourceNameIssue::empty_component);
}

TEST_CASE("GoldSrc local resource classifier rejects alternate path syntax",
          "[goldsrc][local-resource][paths][unsafe]")
{
    check_unsafe("models/barney.mdl:stream",
                 goldsrc::GoldSrcResourceNameIssue::alternate_data_stream);
    check_unsafe("models\\barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::backslash_separator);
    check_unsafe("models/barney.mdl.",
                 goldsrc::GoldSrcResourceNameIssue::trailing_dot_or_space);
    check_unsafe("models/barney.mdl ",
                 goldsrc::GoldSrcResourceNameIssue::trailing_dot_or_space);
}

TEST_CASE("GoldSrc local resource classifier rejects empty and binary names",
          "[goldsrc][local-resource][paths][encoding]")
{
    check_unsafe("", goldsrc::GoldSrcResourceNameIssue::empty_name);

    std::string embedded_nul{"models/barney"};
    embedded_nul.push_back('\0');
    embedded_nul.append(".mdl");
    check_unsafe(embedded_nul, goldsrc::GoldSrcResourceNameIssue::embedded_nul);

    std::string control{"models/barney"};
    control.push_back(static_cast<char>(0x1fU));
    control.append(".mdl");
    check_unsafe(control, goldsrc::GoldSrcResourceNameIssue::control_byte);

    std::string delete_byte{"models/barney"};
    delete_byte.push_back(static_cast<char>(0x7fU));
    delete_byte.append(".mdl");
    check_unsafe(delete_byte, goldsrc::GoldSrcResourceNameIssue::delete_byte);

    std::string non_ascii{"models/barney"};
    non_ascii.push_back(static_cast<char>(0x80U));
    non_ascii.append(".mdl");
    const auto unsupported_encoding = classify_model(non_ascii);
    CHECK(unsupported_encoding.kind() ==
          goldsrc::GoldSrcResourceNameClassificationKind::
              unsupported_name_encoding);
    CHECK(unsupported_encoding.issue() ==
          goldsrc::GoldSrcResourceNameIssue::unsupported_name_encoding);
    CHECK(unsupported_encoding.original_name_byte_length() == non_ascii.size());
    CHECK_FALSE(unsupported_encoding.safe_virtual_name());
}

TEST_CASE("GoldSrc local resource classifier rejects Windows device components",
          "[goldsrc][local-resource][paths][unsafe]")
{
    for (const std::string_view name : {
             "CON", "prn", "Aux", "nul", "COM1", "com9", "LPT1", "lpt9",
             "models/CON.mdl", "models/prn.txt", "models/AUX.anything",
             "models/nul.mdl", "models/COM1.wav", "models/lPt9.sc",
         }) {
        INFO(name);
        check_unsafe(
            name,
            goldsrc::GoldSrcResourceNameIssue::reserved_device_component);
    }

    for (const std::string_view name : {
             "CONSOLE.mdl", "PRName.txt", "AUXILIARY", "NULL.mdl", "COM0",
             "COM10", "LPT0", "LPT10",
         }) {
        INFO(name);
        const auto result = classify_model(name);
        REQUIRE(result.safe_virtual_name());
        CHECK(result.safe_virtual_name()->value() == name);
    }
}

TEST_CASE("GoldSrc local resource classifier applies exact component bounds",
          "[goldsrc][local-resource][paths][limits]")
{
    const std::string exact(
        goldsrc::kDefaultMaximumLocalResourceComponentBytes,
        'a');
    const auto accepted = classify_model(exact);
    REQUIRE(accepted.safe_virtual_name());
    CHECK(accepted.safe_virtual_name()->value() == exact);

    std::string over = exact;
    over.push_back('a');
    check_unsafe(over, goldsrc::GoldSrcResourceNameIssue::component_too_long);
}

TEST_CASE("GoldSrc local resource classifier applies exact mapped-path bounds",
          "[goldsrc][local-resource][paths][limits]")
{
    const auto exact = safe_name_with_length(
        goldsrc::kDefaultMaximumLocalResourceVirtualNameBytes);
    REQUIRE(exact.size() ==
            goldsrc::kDefaultMaximumLocalResourceVirtualNameBytes);
    const auto accepted = classify_model(exact);
    REQUIRE(accepted.safe_virtual_name());
    CHECK(accepted.safe_virtual_name()->value() == exact);

    const auto over = safe_name_with_length(
        goldsrc::kDefaultMaximumLocalResourceVirtualNameBytes + 1U);
    REQUIRE(over.size() ==
            goldsrc::kDefaultMaximumLocalResourceVirtualNameBytes + 1U);
    check_unsafe(over, goldsrc::GoldSrcResourceNameIssue::path_too_long);
}

TEST_CASE("GoldSrc local resource classifier applies exact component-count bounds",
          "[goldsrc][local-resource][paths][limits]")
{
    std::string exact;
    for (std::size_t index = 0U;
         index < goldsrc::kDefaultMaximumLocalResourceComponents;
         ++index) {
        if (!exact.empty()) {
            exact.push_back('/');
        }
        exact.push_back('a');
    }

    const auto accepted = classify_model(exact);
    REQUIRE(accepted.safe_virtual_name());
    CHECK(accepted.safe_virtual_name()->component_count() ==
          goldsrc::kDefaultMaximumLocalResourceComponents);

    std::string over = exact;
    over.append("/a");
    check_unsafe(over, goldsrc::GoldSrcResourceNameIssue::too_many_components);
}

TEST_CASE("GoldSrc local resource classifier never decodes or repairs names",
          "[goldsrc][local-resource][paths][literal]")
{
    const std::string_view percent_encoded =
        "models/%2e%2e/%2Fbarney.mdl";
    const auto literal = classify_model(percent_encoded);
    REQUIRE(literal.safe_virtual_name());
    CHECK(literal.safe_virtual_name()->value() == percent_encoded);
    CHECK(literal.safe_virtual_name()->component_count() == 3U);

    check_unsafe("models//barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::empty_component);
    check_unsafe("models/../barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::parent_component);
    check_unsafe("models\\barney.mdl",
                 goldsrc::GoldSrcResourceNameIssue::backslash_separator);
}

TEST_CASE("GoldSrc local resource classifier rejects invalid safety limits",
          "[goldsrc][local-resource][paths][limits]")
{
    CHECK(goldsrc::valid_goldsrc_resource_name_limits({}));
    CHECK_FALSE(goldsrc::valid_goldsrc_resource_name_limits({0U, 1'024U}));
    CHECK_FALSE(goldsrc::valid_goldsrc_resource_name_limits({255U, 0U}));
    CHECK_FALSE(goldsrc::valid_goldsrc_resource_name_limits({256U, 1'024U}));
    CHECK_FALSE(goldsrc::valid_goldsrc_resource_name_limits({255U, 1'025U}));
    CHECK_FALSE(goldsrc::valid_goldsrc_resource_name_limits({16U, 8U}));
    CHECK_FALSE(goldsrc::valid_goldsrc_resource_name_limits({16U, 16U, 0U}));
    CHECK_FALSE(goldsrc::valid_goldsrc_resource_name_limits({16U, 16U, 65U}));

    const auto invalid = classify_model("models/barney.mdl", {16U, 8U});
    CHECK(invalid.kind() ==
          goldsrc::GoldSrcResourceNameClassificationKind::unsupported_mapping);
    CHECK(invalid.issue() == goldsrc::GoldSrcResourceNameIssue::invalid_limits);
    CHECK_FALSE(invalid.safe_virtual_name());
}

static_assert(std::is_copy_constructible_v<goldsrc::SafeVirtualResourceName>);
static_assert(std::is_move_constructible_v<goldsrc::SafeVirtualResourceName>);
static_assert(!std::is_copy_assignable_v<goldsrc::SafeVirtualResourceName>);
static_assert(!std::is_move_assignable_v<goldsrc::SafeVirtualResourceName>);
static_assert(std::is_copy_constructible_v<
              goldsrc::GoldSrcResourceNameClassification>);
static_assert(std::is_move_constructible_v<
              goldsrc::GoldSrcResourceNameClassification>);

} // namespace
