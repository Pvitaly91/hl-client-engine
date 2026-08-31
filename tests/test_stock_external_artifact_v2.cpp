#include <hlclient/platform/windows/stock_external_target_artifact.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace {

namespace windows = hlclient::platform::windows;

[[nodiscard]] windows::StockExternalArtifactFileIdentity identity(
    const std::string& path, const bool directory,
    const std::uint32_t reparse_tag = 0U)
{
    windows::StockExternalArtifactFileIdentity value{};
    value.volume_serial_number = 7U;
    value.final_path = path;
    value.identity_sha256 = std::string(64U, '1');
    value.reparse_tag = reparse_tag;
    value.directory = directory;
    return value;
}

[[nodiscard]] windows::StockExternalArtifactInventory inventory()
{
    windows::StockExternalArtifactInventory value{};
    value.entry_count = 1U;
    value.byte_count = 4U;
    value.inventory_sha256 = std::string(64U, '2');
    return value;
}

[[nodiscard]] windows::StockExternalPrivateTargetArtifactV2 dangling()
{
    windows::StockExternalPrivateTargetArtifactV2 value{};
    value.ordinal = 1U;
    value.review_nonce = std::string(32U, 'a');
    value.source_root_fingerprint = std::string(64U, 'b');
    value.source_link_relative_path = "missing-assets";
    value.source_link_identity = identity(
        "C:\\source\\missing-assets", true, 0xA0000003U);
    value.raw_reparse_tag = 0xA0000003U;
    value.microsoft_tag = true;
    value.name_surrogate_tag = true;
    value.directory = true;
    value.payload_byte_count = 64U;
    value.payload_sha256 = std::string(64U, 'c');
    value.tag_category = "mount_point";
    value.substitute_name = "\\??\\Z:\\missing";
    value.print_name = "Z:\\missing";
    value.normalized_target_expression = "\\??\\Z:\\missing";
    value.expression_kind = "nt_object_manager_path";
    value.reachability = "target_volume_not_found";
    value.failure_phase = "target_open";
    value.native_error_category = "device_not_exist";
    value.native_error = 55U;
    value.witness_sha256 = std::string(64U, 'd');
    value.classification = windows::StockExternalArtifactClassification::
        unsupported_reparse_topology;
    value.diagnostic_complete = true;
    return value;
}

[[nodiscard]] windows::StockExternalPrivateTargetArtifactV2 eligible()
{
    auto value = dangling();
    value.reachability = "reachable";
    value.failure_phase = "target_identity";
    value.native_error_category = "none";
    value.native_error = 0U;
    value.witness_sha256.clear();
    value.target_canonical_path = "C:\\target";
    value.target_identity = identity("C:\\target", true);
    value.target_inventory = inventory();
    value.classification = windows::StockExternalArtifactClassification::
        eligible_non_executable_asset_tree;
    value.eligible = true;
    return value;
}

[[nodiscard]] std::string replace_once(
    std::string text, const std::string_view from,
    const std::string_view to)
{
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

} // namespace

TEST_CASE(
    "Closed V1 review artifacts retain canonical serializer parser compatibility",
    "[windows][stock-runtime][external-target][artifact-v1][v1-compat]")
{
    windows::StockExternalReviewRequestArtifact request{};
    request.source_root_identity = identity("C:\\source", true);
    request.source_inventory = inventory();
    request.review_root_fingerprint = std::string(64U, '3');
    request.review_nonce = std::string(32U, '4');
    request.review_timestamp_unix_seconds = 5U;
    request.implementation_profile =
        "hlclient.stock-external-target-review.windows-v1";
    request.target_count = 1U;
    const auto request_json =
        windows::serialize_stock_external_review_request(request);
    REQUIRE(request_json);
    const auto request_round_trip =
        windows::parse_stock_external_review_request(*request_json.value);
    REQUIRE(request_round_trip);
    CHECK(*request_round_trip.value == request);

    windows::StockExternalPrivateTargetArtifact private_target{};
    private_target.ordinal = 1U;
    private_target.review_nonce = request.review_nonce;
    private_target.source_root_fingerprint =
        request.source_root_identity.identity_sha256;
    private_target.source_link_relative_path = "shared-assets";
    private_target.source_link_identity = identity(
        "C:\\source\\shared-assets", true, 0xA0000003U);
    private_target.target_canonical_path = "C:\\target";
    private_target.target_identity = identity("C:\\target", true);
    private_target.target_inventory = inventory();
    private_target.classification =
        windows::StockExternalArtifactClassification::
            eligible_non_executable_asset_tree;
    private_target.eligible = true;
    const auto private_json =
        windows::serialize_stock_external_private_target(private_target);
    REQUIRE(private_json);
    const auto private_round_trip =
        windows::parse_stock_external_private_target(*private_json.value);
    REQUIRE(private_round_trip);
    CHECK(*private_round_trip.value == private_target);
    const auto private_digest =
        windows::stock_external_artifact_sha256(*private_json.value);
    REQUIRE(private_digest);

    windows::StockExternalReviewSummaryArtifact summary{};
    summary.review_root_fingerprint = request.review_root_fingerprint;
    summary.source_root_fingerprint =
        request.source_root_identity.identity_sha256;
    summary.source_inventory = request.source_inventory;
    summary.review_nonce = request.review_nonce;
    summary.review_timestamp_unix_seconds =
        request.review_timestamp_unix_seconds;
    summary.implementation_profile = request.implementation_profile;
    summary.targets.push_back(
        windows::StockExternalReviewTargetBindingArtifact{
            1U,
            *private_digest.value,
            private_target.source_link_identity.identity_sha256,
            private_target.target_identity.identity_sha256,
            private_target.target_inventory.inventory_sha256,
            private_target.classification,
            true});
    summary.eligible_count = 1U;
    summary.all_targets_eligible = true;
    const auto summary_json =
        windows::serialize_stock_external_review_summary(summary);
    REQUIRE(summary_json);
    const auto summary_round_trip =
        windows::parse_stock_external_review_summary(*summary_json.value);
    REQUIRE(summary_round_trip);
    CHECK(*summary_round_trip.value == summary);

    const auto request_v2_json =
        windows::serialize_stock_external_review_request_v2(request);
    REQUIRE(request_v2_json);
    CHECK_FALSE(windows::parse_stock_external_review_request(
        *request_v2_json.value));
}

TEST_CASE(
    "V2 private diagnostics enforce exact typed tokens and unavailable state",
    "[windows][stock-runtime][external-target][artifact-v2]")
{
    const auto valid = dangling();
    const auto serialized =
        windows::serialize_stock_external_private_target_v2(valid);
    REQUIRE(serialized);
    const auto forged = replace_once(
        *serialized.value, "mount_point", "invented_tag");
    CHECK_FALSE(windows::parse_stock_external_private_target_v2(forged));

    auto arbitrary_tag = valid;
    arbitrary_tag.tag_category = "invented_tag";
    CHECK_FALSE(windows::serialize_stock_external_private_target_v2(
        arbitrary_tag));

    auto arbitrary_reachability = valid;
    arbitrary_reachability.reachability = "maybe_reachable";
    CHECK_FALSE(windows::serialize_stock_external_private_target_v2(
        arbitrary_reachability));

    auto substituted_zero = valid;
    substituted_zero.target_inventory = inventory();
    CHECK_FALSE(windows::serialize_stock_external_private_target_v2(
        substituted_zero));

    auto absent_witness = valid;
    absent_witness.witness_sha256.clear();
    CHECK_FALSE(windows::serialize_stock_external_private_target_v2(
        absent_witness));

    auto mismatched_native_category = valid;
    mismatched_native_category.native_error_category = "none";
    CHECK_FALSE(windows::serialize_stock_external_private_target_v2(
        mismatched_native_category));

    auto mismatched_nonzero_native_category = valid;
    mismatched_nonzero_native_category.native_error_category =
        "path_not_found";
    CHECK_FALSE(windows::serialize_stock_external_private_target_v2(
        mismatched_nonzero_native_category));

    auto mismatched_reachability = valid;
    mismatched_reachability.reachability = "target_access_denied";
    CHECK_FALSE(windows::serialize_stock_external_private_target_v2(
        mismatched_reachability));
    const auto forged_reachability = replace_once(
        *serialized.value, "target_volume_not_found",
        "target_access_denied");
    CHECK_FALSE(windows::parse_stock_external_private_target_v2(
        forged_reachability));

    auto policy_remote = valid;
    policy_remote.reachability = "target_remote_or_device";
    policy_remote.native_error_category = "none";
    policy_remote.native_error = 0U;
    REQUIRE(windows::serialize_stock_external_private_target_v2(
        policy_remote));

    auto changed_during_revalidation = valid;
    changed_during_revalidation.reachability = "target_changed";
    changed_during_revalidation.failure_phase =
        "post_inventory_revalidation";
    REQUIRE(windows::serialize_stock_external_private_target_v2(
        changed_during_revalidation));

    auto malformed_standard_payload = valid;
    malformed_standard_payload.substitute_name.clear();
    malformed_standard_payload.print_name.clear();
    malformed_standard_payload.normalized_target_expression.clear();
    malformed_standard_payload.expression_kind = "malformed";
    malformed_standard_payload.reachability = "not_applicable";
    malformed_standard_payload.failure_phase = "reparse_payload_decode";
    malformed_standard_payload.native_error_category = "none";
    malformed_standard_payload.native_error = 0U;
    const auto malformed_json =
        windows::serialize_stock_external_private_target_v2(
            malformed_standard_payload);
    REQUIRE(malformed_json);
    const auto malformed_round_trip =
        windows::parse_stock_external_private_target_v2(
            *malformed_json.value);
    REQUIRE(malformed_round_trip);
    CHECK(*malformed_round_trip.value == malformed_standard_payload);

    auto forged_opaque_expression = malformed_standard_payload;
    forged_opaque_expression.tag_category = "third_party_other";
    forged_opaque_expression.raw_reparse_tag = 0x00000042U;
    forged_opaque_expression.microsoft_tag = false;
    forged_opaque_expression.name_surrogate_tag = false;
    forged_opaque_expression.source_link_identity.reparse_tag = 0x00000042U;
    CHECK_FALSE(windows::serialize_stock_external_private_target_v2(
        forged_opaque_expression));

    auto malformed_with_native_failure = malformed_standard_payload;
    malformed_with_native_failure.native_error = 13U; // ERROR_INVALID_DATA
    malformed_with_native_failure.native_error_category = "other";
    CHECK_FALSE(windows::serialize_stock_external_private_target_v2(
        malformed_with_native_failure));
}

TEST_CASE(
    "V2 eligible private targets require a reachable witnessed-free inventory",
    "[windows][stock-runtime][external-target][artifact-v2]")
{
    const auto valid = eligible();
    REQUIRE(windows::serialize_stock_external_private_target_v2(valid));

    auto unavailable = valid;
    unavailable.target_inventory.reset();
    CHECK_FALSE(windows::serialize_stock_external_private_target_v2(
        unavailable));

    auto witnessed = valid;
    witnessed.witness_sha256 = std::string(64U, 'e');
    CHECK_FALSE(windows::serialize_stock_external_private_target_v2(
        witnessed));

    auto wrong_phase_token = valid;
    wrong_phase_token.failure_phase = "none";
    CHECK_FALSE(windows::serialize_stock_external_private_target_v2(
        wrong_phase_token));
}

TEST_CASE(
    "V2 summary bindings reject identity and inventory for unreachable targets",
    "[windows][stock-runtime][external-target][artifact-v2]")
{
    windows::StockExternalReviewSummaryArtifactV2 summary{};
    summary.review_root_fingerprint = std::string(64U, '1');
    summary.source_root_fingerprint = std::string(64U, '2');
    summary.source_inventory = inventory();
    summary.review_nonce = std::string(32U, '3');
    summary.review_timestamp_unix_seconds = 1U;
    summary.implementation_profile =
        "hlclient.stock-external-target-review.windows-v2";
    windows::StockExternalReviewTargetBindingArtifactV2 target{};
    target.ordinal = 1U;
    target.private_record_sha256 = std::string(64U, '4');
    target.link_identity_sha256 = std::string(64U, '5');
    target.classification = windows::StockExternalArtifactClassification::
        unsupported_reparse_topology;
    target.tag_category = "mount_point";
    target.expression_kind = "nt_object_manager_path";
    target.reachability = "target_volume_not_found";
    target.failure_phase = "target_open";
    target.native_error_category = "device_not_exist";
    target.diagnostic_complete = true;
    summary.targets.push_back(target);
    summary.completed_count = 1U;
    summary.ineligible_count = 1U;
    REQUIRE(windows::serialize_stock_external_review_summary_v2(summary));

    auto malformed_summary = summary;
    malformed_summary.targets.front().expression_kind = "malformed";
    malformed_summary.targets.front().reachability = "not_applicable";
    malformed_summary.targets.front().failure_phase =
        "reparse_payload_decode";
    malformed_summary.targets.front().native_error_category = "none";
    const auto malformed_json =
        windows::serialize_stock_external_review_summary_v2(
            malformed_summary);
    REQUIRE(malformed_json);
    const auto malformed_round_trip =
        windows::parse_stock_external_review_summary_v2(
            *malformed_json.value);
    REQUIRE(malformed_round_trip);
    CHECK(*malformed_round_trip.value == malformed_summary);

    auto mismatched_reachability = summary;
    mismatched_reachability.targets.front().reachability =
        "target_access_denied";
    CHECK_FALSE(windows::serialize_stock_external_review_summary_v2(
        mismatched_reachability));
    const auto serialized =
        windows::serialize_stock_external_review_summary_v2(summary);
    REQUIRE(serialized);
    const auto forged_reachability = replace_once(
        *serialized.value, "target_volume_not_found",
        "target_access_denied");
    CHECK_FALSE(windows::parse_stock_external_review_summary_v2(
        forged_reachability));

    auto policy_remote = summary;
    policy_remote.targets.front().reachability = "target_remote_or_device";
    policy_remote.targets.front().native_error_category = "none";
    REQUIRE(windows::serialize_stock_external_review_summary_v2(
        policy_remote));

    auto changed_during_revalidation = summary;
    changed_during_revalidation.targets.front().reachability =
        "target_changed";
    changed_during_revalidation.targets.front().failure_phase =
        "post_inventory_revalidation";
    REQUIRE(windows::serialize_stock_external_review_summary_v2(
        changed_during_revalidation));

    summary.targets.front().target_identity_sha256 = std::string(64U, '6');
    CHECK_FALSE(windows::serialize_stock_external_review_summary_v2(summary));
}
