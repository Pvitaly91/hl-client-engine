#include <hlclient/platform/windows/stock_external_target_review.hpp>

#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

#include <Windows.h>

namespace {
namespace windows = hlclient::platform::windows;

enum class Mode { none, review, approve };
struct Options final {
    Mode mode{Mode::none};
    std::filesystem::path source;
    std::filesystem::path output_parent;
    std::filesystem::path review_root;
    std::string phrase;
    windows::StockResearchCopyLimits limits{};
    std::uint64_t lifetime_hours{24U};
};

[[nodiscard]] bool parse_number(const std::wstring_view text, std::uint64_t& value)
{
    std::string ascii; ascii.reserve(text.size());
    for (const auto c : text) { if (c < L'0' || c > L'9') return false; ascii.push_back(static_cast<char>(c)); }
    const auto parsed = std::from_chars(ascii.data(), ascii.data() + ascii.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == ascii.data() + ascii.size();
}

[[nodiscard]] std::optional<Options> parse(const int argc, wchar_t** argv)
{
    Options o{};
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view a{argv[i]};
        if (a == L"--review") { if (o.mode != Mode::none) return {}; o.mode = Mode::review; }
        else if (a == L"--approve") { if (o.mode != Mode::none) return {}; o.mode = Mode::approve; }
        else if (a == L"--source-root") { if (++i >= argc || !o.source.empty()) return {}; o.source = argv[i]; }
        else if (a == L"--output-parent") { if (++i >= argc || !o.output_parent.empty()) return {}; o.output_parent = argv[i]; }
        else if (a == L"--review-root") { if (++i >= argc || !o.review_root.empty()) return {}; o.review_root = argv[i]; }
        else if (a == L"--approval-phrase") {
            if (++i >= argc || !o.phrase.empty()) return {};
            const std::wstring_view wide{argv[i]};
            for (const wchar_t value : wide) {
                if (value < 0 || value > 0x7f) return {};
                o.phrase.push_back(static_cast<char>(value));
            }
        } else if (a == L"--maximum-entries") {
            std::uint64_t n{}; if (++i >= argc || !parse_number(argv[i], n) || n == 0U || n > 1'000'000U) return {};
            o.limits.maximum_entries = static_cast<std::size_t>(n);
        } else if (a == L"--maximum-bytes") {
            std::uint64_t n{}; if (++i >= argc || !parse_number(argv[i], n) || n == 0U) return {};
            o.limits.maximum_total_bytes = n;
        } else if (a == L"--lifetime-hours") {
            if (++i >= argc || !parse_number(argv[i], o.lifetime_hours) || o.lifetime_hours < 1U || o.lifetime_hours > 168U) return {};
        } else return {};
    }
    if (o.mode == Mode::review && !o.source.empty() && !o.output_parent.empty() &&
        o.review_root.empty() && o.phrase.empty()) return o;
    if (o.mode == Mode::approve && !o.review_root.empty() && !o.phrase.empty() &&
        o.source.empty() && o.output_parent.empty()) return o;
    return {};
}

void failure(const windows::StockExternalReviewErrorCode code, const std::uint32_t native)
{
    std::cerr << "[source-review] result=" << windows::to_string(code) << '\n'
              << "[source-review] native-error-category="
              << windows::to_string(
                     windows::classify_windows_reparse_native_error(native))
              << '\n';
}
} // namespace

int wmain(const int argc, wchar_t** argv)
{
    const auto options = parse(argc, argv);
    if (!options) { failure(windows::StockExternalReviewErrorCode::invalid_argument, 0U); return 2; }
    if (options->mode == Mode::review) {
        const auto result = windows::review_stock_external_targets(
            options->source, options->output_parent, options->limits);
        if (!result) { failure(result.code, result.native_error); return 1; }
        const auto& review = *result.value;
        std::size_t eligible = 0U;
        std::size_t unknown = 0U;
        std::size_t completed = 0U;
        bool all_inventories_available = true;
        for (const auto& target : review.targets) {
            if (target.eligible) ++eligible;
            if (target.diagnostic_complete) ++completed;
            all_inventories_available =
                all_inventories_available && target.inventory_available;
            if (target.classification ==
                windows::StockExternalTargetClassification::unknown) {
                ++unknown;
            }
        }
        if (eligible > completed || completed > review.targets.size() ||
            review.all_targets_eligible !=
                (!review.targets.empty() &&
                 eligible == review.targets.size() &&
                 completed == review.targets.size())) {
            failure(
                windows::StockExternalReviewErrorCode::topology_read_failed,
                ERROR_INVALID_DATA);
            return 1;
        }
        std::cout << "[source-review] schema="
                  << windows::kStockExternalTargetReviewSchemaV2 << '\n'
                  << "[source-review] escaped-targets="
                  << review.targets.size() << '\n'
                  << "[source-review] completed-targets=" << completed << '\n'
                  << "[source-review] eligible=" << eligible << '\n'
                  << "[source-review] ineligible="
                  << (completed - eligible) << '\n'
                  << "[source-review] incomplete="
                  << (review.targets.size() - completed) << '\n'
                  << "[source-review] unknown=" << unknown << '\n'
                  << "[source-review] executable-targets=";
        if (all_inventories_available) {
            std::cout << review.executable_count;
        } else {
            std::cout << "unavailable";
        }
        std::cout << '\n' << "[source-review] mutable-data-targets=";
        if (all_inventories_available) {
            std::cout << review.mutable_state_count;
        } else {
            std::cout << "unavailable";
        }
        std::cout << '\n' << "[source-review] target-count="
                  << review.targets.size() << '\n'
                  << "[source-review] eligible-target-count=" << eligible
                  << '\n'
                  << "[source-review] all-targets-eligible="
                  << (review.all_targets_eligible ? "true" : "false")
                  << '\n';
        for (std::size_t index = 0U; index < review.targets.size(); ++index) {
            const auto& target = review.targets[index];
            const auto ordinal = index + 1U;
            std::cout << "[source-review] target-" << ordinal
                      << "-classification="
                      << windows::to_string(target.classification) << '\n';
            if (target.reparse_observation) {
                const auto& observation = *target.reparse_observation;
                std::cout << "[source-review] target-" << ordinal
                          << "-tag-category="
                          << windows::to_string(
                                 observation.provenance.tag.category)
                          << '\n'
                          << "[source-review] target-" << ordinal
                          << "-expression-kind="
                          << windows::to_string(
                                 observation.provenance.target_expression.kind)
                          << '\n'
                          << "[source-review] target-" << ordinal
                          << "-reachability="
                          << windows::to_string(observation.reachability) << '\n'
                          << "[source-review] target-" << ordinal
                          << "-native-error-category="
                          << windows::to_string(
                                 observation.native_error_category)
                          << '\n';
            } else {
                std::cout << "[source-review] target-" << ordinal
                          << "-tag-category=none\n"
                          << "[source-review] target-" << ordinal
                          << "-expression-kind=none\n"
                          << "[source-review] target-" << ordinal
                          << "-reachability=not_applicable\n"
                          << "[source-review] target-" << ordinal
                          << "-native-error-category=none\n";
            }
            std::cout << "[source-review] target-" << ordinal
                      << "-failure-phase=";
            if (target.failure_witness) {
                std::cout << windows::to_string(
                    target.failure_witness->failure_phase);
            } else {
                std::cout << "none";
            }
            std::cout << '\n' << "[source-review] target-" << ordinal
                      << "-inventory="
                      << (target.inventory_available ? "available"
                                                     : "unavailable")
                      << '\n';
            const auto count = [&](const std::string_view name,
                                   const auto value) {
                std::cout << "[source-review] target-" << ordinal << '-'
                          << name << '=';
                if (target.inventory_available) {
                    std::cout << value;
                } else {
                    std::cout << "unavailable";
                }
                std::cout << '\n';
            };
            count("entry-count", target.entry_count);
            count("byte-count", target.byte_count);
            count("executable-count", target.executable_count);
            count("script-count", target.script_or_command_count);
            count("mutable-state-count", target.mutable_state_count);
            count("nested-link-count", target.nested_link_count);
            std::cout << "[source-review] target-" << ordinal
                      << "-eligible=" << (target.eligible ? "true" : "false")
                      << '\n';
        }
        std::cout << "[source-review] review-id="
                  << review.review_root.filename().string() << '\n'
                  << "[source-review] result="
                  << (review.all_targets_eligible ? "success" : "ineligible")
                  << '\n';
        // Review is a completed read-only diagnostic even when one or more
        // targets are ineligible. Approval remains fail-closed.
        return 0;
    }
    const auto result = windows::approve_stock_external_target_review(
        options->review_root, options->phrase, std::chrono::hours{options->lifetime_hours});
    if (!result) { failure(result.code, result.native_error); return 1; }
    std::cout << "[source-review] schema="
              << windows::kStockExternalTargetApprovalSchemaV1 << '\n'
              << "[source-review] lifetime-hours=" << options->lifetime_hours
              << '\n'
              << "[source-review] private-handoff=local-only\n"
              << "[source-review] result=success\n";
    return 0;
}
