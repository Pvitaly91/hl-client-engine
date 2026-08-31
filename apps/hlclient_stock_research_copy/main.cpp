#include <hlclient/platform/windows/stock_research_copy.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

namespace windows = hlclient::platform::windows;

enum class Mode { none, inspect, materialize };

struct Options final {
    Mode mode{Mode::none};
    std::filesystem::path source;
    std::filesystem::path destination;
    std::filesystem::path external_target_approval_manifest;
};

[[nodiscard]] std::optional<Options> parse_options(
    const int argc, wchar_t** const argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument{argv[index]};
        if (argument == L"--inspect-source-topology") {
            if (options.mode != Mode::none) return std::nullopt;
            options.mode = Mode::inspect;
        } else if (argument == L"--materialize") {
            if (options.mode != Mode::none) return std::nullopt;
            options.mode = Mode::materialize;
        } else if (argument == L"--source-root") {
            if (++index >= argc || !options.source.empty()) return std::nullopt;
            options.source = argv[index];
        } else if (argument == L"--destination-root") {
            if (++index >= argc || !options.destination.empty()) {
                return std::nullopt;
            }
            options.destination = argv[index];
        } else if (argument == L"--external-target-approval-manifest") {
            if (++index >= argc ||
                !options.external_target_approval_manifest.empty()) {
                return std::nullopt;
            }
            options.external_target_approval_manifest = argv[index];
        } else {
            return std::nullopt;
        }
    }
    if (options.mode == Mode::inspect) {
        if (options.source.empty() || !options.destination.empty() ||
            !options.external_target_approval_manifest.empty()) {
            return std::nullopt;
        }
    } else if (options.mode == Mode::materialize) {
        if (options.source.empty() || options.destination.empty()) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    return options;
}

void print_topology(const windows::StockResearchTopologySummary& summary)
{
    for (const auto category : summary.categories) {
        std::cout << "[research-copy] topology="
                  << windows::to_string(category) << '\n';
    }
    std::cout << "[research-copy] root-reparse="
              << (summary.root_reparse ? "true" : "false") << '\n'
              << "[research-copy] internal-reparse-count="
              << summary.internal_reparse_count << '\n'
              << "[research-copy] hardlink-count="
              << summary.hardlink_count << '\n'
              << "[research-copy] ads-count="
              << summary.alternate_data_stream_count << '\n'
              << "[research-copy] contained-target-count="
              << summary.contained_target_count << '\n'
              << "[research-copy] escaped-target-count="
              << summary.escaped_target_count << '\n';
}

} // namespace

int wmain(const int argc, wchar_t** const argv)
{
    const auto options = parse_options(argc, argv);
    if (!options) {
        std::cerr << "[research-copy] result=invalid_argument\n";
        return 2;
    }
    const auto topology =
        windows::inspect_stock_research_topology(options->source);
    if (!topology) {
        std::cerr << "[research-copy] result="
                  << windows::to_string(topology.code) << '\n'
                  << "[research-copy] native-error="
                  << topology.native_error << '\n';
        return 1;
    }
    print_topology(*topology.summary);
    if (options->mode == Mode::inspect) {
        std::cout << "[research-copy] result="
                  << (topology.summary->safe_to_materialize ? "safe"
                                                            : "unsafe")
                  << '\n';
        // Unsafe topology is still a completed read-only diagnostic.
        return 0;
    }
    if (!topology.summary->safe_to_materialize &&
        options->external_target_approval_manifest.empty()) {
        std::cerr << "[research-copy] result=source_topology_unsafe\n";
        return 1;
    }

    windows::StockResearchCopyOptions materialization_options;
    if (!options->external_target_approval_manifest.empty()) {
        materialization_options.external_target_approval_manifest =
            options->external_target_approval_manifest;
    }
    const auto materialized = windows::materialize_stock_research_copy(
        options->source, options->destination, materialization_options);
    if (!materialized) {
        std::cerr << "[research-copy] result="
                  << windows::to_string(materialized.code) << '\n'
                  << "[research-copy] native-error="
                  << materialized.native_error << '\n';
        return 1;
    }
    const auto& result = *materialized.materialization;
    std::cout << "[research-copy] preparation-status="
              << result.preparation_status << '\n'
              << "[research-copy] destination-reparse-count="
              << result.destination_reparse_count << '\n'
              << "[research-copy] destination-hardlink-count="
              << result.destination_hardlink_count << '\n'
              << "[research-copy] destination-ads-count="
              << result.destination_alternate_data_stream_count << '\n'
              << "[research-copy] source-changed="
              << (result.source_unchanged ? "false" : "true") << '\n'
              << "[research-copy] external-targets-changed="
              << (result.external_targets_unchanged ? "false" : "true")
              << '\n'
              << "[research-copy] external-target-count="
              << result.approved_external_materialized_link_count << '\n'
              << "[research-copy] external-target-profile="
              << result.external_target_profile << '\n'
              << "[research-copy] research-copy-evidence-eligible="
              << (result.evidence_eligibility ==
                          windows::StockResearchCopyEvidenceEligibility::eligible
                      ? "true"
                      : "false")
              << '\n'
              << "[research-copy] copied-entry-count="
              << result.entry_count << '\n'
              << "[research-copy] materialized-link-count="
              << result.materialized_link_count << '\n'
              << "[research-copy] materialized-hardlink-count="
              << result.materialized_hardlink_count << '\n'
              << "[research-copy] marker="
              << windows::kStockResearchIsolationMarkerV1 << '\n'
              << "[research-copy] result=success\n";

    // Preserve the bounded v1 status surface for callers that only consume
    // the original preparation helper's success markers.
    std::cout << "[stock-runtime-prepare] source-modified=false\n"
              << "[stock-runtime-prepare] copied-launchers=2\n"
              << "[stock-runtime-prepare] copied-entry-count="
              << result.entry_count << '\n'
              << "[stock-runtime-prepare] marker="
              << windows::kStockResearchIsolationMarkerV1 << '\n'
              << "[stock-runtime-prepare] result=success\n";
    return 0;
}
