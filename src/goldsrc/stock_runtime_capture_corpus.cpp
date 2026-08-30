#include <hlclient/goldsrc/stock_runtime_capture_corpus.hpp>

#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/hash/sha256.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <system_error>
#include <utility>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace hlclient::goldsrc {
namespace {

namespace fs = std::filesystem;

struct ManifestScalar final {
    enum class Kind { string, integer, boolean, null_value };
    Kind kind{Kind::string};
    std::string value;
};

using ManifestProperties = std::map<std::string, ManifestScalar, std::less<>>;

struct ManifestReadResult final {
    std::optional<ManifestProperties> properties;
    std::optional<StockRuntimeCaptureCorpusError> error;
};

[[nodiscard]] StockRuntimeCaptureCorpusLoadResult failure(
    const StockRuntimeCaptureCorpusErrorCode code,
    std::string context,
    const std::size_t ordinal = 0U,
    const std::optional<StockRuntimeTransportJournalErrorCode> journal_code =
        std::nullopt)
{
    return {
        std::nullopt,
        StockRuntimeCaptureCorpusError{
            code, ordinal, std::move(context), journal_code},
    };
}

[[nodiscard]] bool valid_run_id(const std::string_view value) noexcept
{
    return value.size() == 32U &&
           std::ranges::all_of(value, [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

[[nodiscard]] bool safe_leaf_name(const std::string_view value) noexcept
{
    return !value.empty() && value.size() <= 96U && value.front() != '.' &&
           std::ranges::all_of(value, [](const char character) {
               return (character >= 'a' && character <= 'z') ||
                      (character >= '0' && character <= '9') ||
                      character == '-' || character == '_' || character == '.';
           });
}

[[nodiscard]] bool path_is_reparse(const fs::path& path) noexcept
{
#ifdef _WIN32
    const auto attributes = ::GetFileAttributesW(path.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES ||
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
#else
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    return error || fs::is_symlink(status);
#endif
}

enum class ExistingPathComponentsState {
    safe,
    missing_or_unreadable,
    reparse,
    non_directory_ancestor,
};

// Lexical normalization alone does not make a path capability-safe on
// Windows: an otherwise ordinary run directory can be reached through a
// junction in one of its parents.  Inspect every existing component before
// opening any corpus artifact.  The run leaf may be any filesystem object here
// so the caller can retain its more specific missing/not-a-directory status.
[[nodiscard]] ExistingPathComponentsState inspect_existing_path_components(
    const fs::path& absolute) noexcept
{
    try {
        std::error_code error;
#ifdef _WIN32
        static_cast<void>(error);
#endif
        auto current = absolute.root_path();
        if (current.empty()) {
            return ExistingPathComponentsState::missing_or_unreadable;
        }

        auto inspect = [&error](
                           const fs::path& component,
                           const bool directory_required) noexcept {
#ifdef _WIN32
            const auto attributes = ::GetFileAttributesW(component.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) {
                return ExistingPathComponentsState::missing_or_unreadable;
            }
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
                return ExistingPathComponentsState::reparse;
            }
            if (directory_required &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
                return ExistingPathComponentsState::non_directory_ancestor;
            }
#else
            error.clear();
            const auto status = fs::symlink_status(component, error);
            if (error || !fs::exists(status)) {
                return ExistingPathComponentsState::missing_or_unreadable;
            }
            if (fs::is_symlink(status) || path_is_reparse(component)) {
                return ExistingPathComponentsState::reparse;
            }
            if (directory_required && !fs::is_directory(status)) {
                return ExistingPathComponentsState::non_directory_ancestor;
            }
#endif
            return ExistingPathComponentsState::safe;
        };

        auto state = inspect(current, true);
        if (state != ExistingPathComponentsState::safe) return state;

        const auto relative = absolute.relative_path();
        for (auto iterator = relative.begin(); iterator != relative.end();
             ++iterator) {
            current /= *iterator;
            const auto next = std::next(iterator);
            state = inspect(current, next != relative.end());
            if (state != ExistingPathComponentsState::safe) return state;
        }
        return ExistingPathComponentsState::safe;
    } catch (...) {
        return ExistingPathComponentsState::missing_or_unreadable;
    }
}

[[nodiscard]] bool file_has_multiple_links(const fs::path& path) noexcept
{
#ifdef _WIN32
    const HANDLE file = ::CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return true;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const bool invalid = !::GetFileInformationByHandle(file, &information) ||
                         information.nNumberOfLinks != 1U ||
                         (information.dwFileAttributes &
                          (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U;
    static_cast<void>(::CloseHandle(file));
    return invalid;
#else
    // Portable filesystem does not expose link count. A regular non-symlink
    // file remains the strongest capability available to non-Windows CI.
    static_cast<void>(path);
    return false;
#endif
}

struct BoundedFileRead final {
    std::optional<std::vector<std::byte>> bytes;
    std::optional<StockRuntimeCaptureCorpusError> error;
};

[[nodiscard]] BoundedFileRead read_bounded_regular_file(
    const fs::path& path,
    const std::size_t maximum_bytes,
    const bool reject_hardlinks = true)
{
    std::error_code status_error;
    const auto status = fs::symlink_status(path, status_error);
    if (status_error || !fs::exists(status)) {
        return {std::nullopt, StockRuntimeCaptureCorpusError{
            StockRuntimeCaptureCorpusErrorCode::open_failed, 0U,
            "required corpus file is absent", std::nullopt}};
    }
    if (fs::is_symlink(status) || path_is_reparse(path)) {
        return {std::nullopt, StockRuntimeCaptureCorpusError{
            StockRuntimeCaptureCorpusErrorCode::reparse_point, 0U,
            "corpus file is a reparse point", std::nullopt}};
    }
    if (!fs::is_regular_file(status)) {
        return {std::nullopt, StockRuntimeCaptureCorpusError{
            StockRuntimeCaptureCorpusErrorCode::file_not_regular, 0U,
            "corpus file is not regular", std::nullopt}};
    }
    if (reject_hardlinks && file_has_multiple_links(path)) {
        return {std::nullopt, StockRuntimeCaptureCorpusError{
            StockRuntimeCaptureCorpusErrorCode::hardlink_detected, 0U,
            "corpus file does not have exclusive file identity", std::nullopt}};
    }
    const auto size = fs::file_size(path, status_error);
    if (status_error || size > maximum_bytes ||
        size > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)())) {
        return {std::nullopt, StockRuntimeCaptureCorpusError{
            StockRuntimeCaptureCorpusErrorCode::file_too_large, 0U,
            "corpus file exceeds its configured bound", std::nullopt}};
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return {std::nullopt, StockRuntimeCaptureCorpusError{
            StockRuntimeCaptureCorpusErrorCode::open_failed, 0U,
            "corpus file could not be opened", std::nullopt}};
    }
    std::vector<std::byte> bytes;
    try {
        bytes.resize(static_cast<std::size_t>(size));
    } catch (...) {
        return {std::nullopt, StockRuntimeCaptureCorpusError{
            StockRuntimeCaptureCorpusErrorCode::file_too_large, 0U,
            "corpus file buffer allocation failed", std::nullopt}};
    }
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (input.bad() || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return {std::nullopt, StockRuntimeCaptureCorpusError{
            StockRuntimeCaptureCorpusErrorCode::read_failed, 0U,
            "corpus file could not be read exactly", std::nullopt}};
    }
    char extra = 0;
    input.read(&extra, 1);
    if (input.gcount() != 0 || !input.eof()) {
        return {std::nullopt, StockRuntimeCaptureCorpusError{
            StockRuntimeCaptureCorpusErrorCode::read_failed, 0U,
            "corpus file changed while it was read", std::nullopt}};
    }
    return {std::move(bytes), std::nullopt};
}

[[nodiscard]] std::string bytes_as_string(const std::span<const std::byte> bytes)
{
    return std::string{
        reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

class FlatManifestReader final {
public:
    explicit FlatManifestReader(const std::string_view input) noexcept
        : input_{input}
    {
    }

    [[nodiscard]] ManifestReadResult read()
    {
        ManifestProperties properties;
        skip_space();
        if (!consume('{')) return syntax("manifest must begin with an object");
        skip_space();
        while (peek() != '}') {
            if (properties.size() >= 64U) {
                return syntax("manifest property count exceeds its bound");
            }
            std::string name;
            if (!read_string(name)) return syntax("manifest property name is invalid");
            if (properties.contains(name)) {
                return ManifestReadResult{
                    std::nullopt,
                    StockRuntimeCaptureCorpusError{
                        StockRuntimeCaptureCorpusErrorCode::duplicate_property,
                        0U, "manifest property is duplicated", std::nullopt}};
            }
            skip_space();
            if (!consume(':')) return syntax("manifest property lacks a colon");
            skip_space();
            ManifestScalar scalar;
            if (peek() == '"') {
                scalar.kind = ManifestScalar::Kind::string;
                if (!read_string(scalar.value)) return syntax("manifest string is invalid");
            } else {
                const auto begin = offset_;
                while (offset_ < input_.size() && input_[offset_] != ',' &&
                       input_[offset_] != '}' && input_[offset_] != ' ' &&
                       input_[offset_] != '\t' && input_[offset_] != '\r' &&
                       input_[offset_] != '\n') {
                    ++offset_;
                }
                if (begin == offset_) return syntax("manifest scalar is absent");
                scalar.value.assign(input_.substr(begin, offset_ - begin));
                if (scalar.value == "true" || scalar.value == "false") {
                    scalar.kind = ManifestScalar::Kind::boolean;
                } else if (scalar.value == "null") {
                    scalar.kind = ManifestScalar::Kind::null_value;
                } else {
                    scalar.kind = ManifestScalar::Kind::integer;
                    std::uint64_t ignored = 0U;
                    const auto converted = std::from_chars(
                        scalar.value.data(),
                        scalar.value.data() + scalar.value.size(), ignored, 10);
                    if (converted.ec != std::errc{} ||
                        converted.ptr != scalar.value.data() + scalar.value.size() ||
                        scalar.value.front() == '-' ||
                        (scalar.value.size() > 1U && scalar.value.front() == '0')) {
                        return syntax("manifest integer is invalid");
                    }
                }
            }
            properties.emplace(std::move(name), std::move(scalar));
            skip_space();
            if (peek() == '}') break;
            if (!consume(',')) return syntax("manifest properties are not comma separated");
            skip_space();
        }
        if (!consume('}')) return syntax("manifest is unterminated");
        skip_space();
        if (offset_ != input_.size()) return syntax("manifest has trailing bytes");
        return {std::move(properties), std::nullopt};
    }

private:
    void skip_space() noexcept
    {
        while (offset_ < input_.size()) {
            const auto value = input_[offset_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') break;
            ++offset_;
        }
    }
    [[nodiscard]] char peek() const noexcept
    {
        return offset_ < input_.size() ? input_[offset_] : '\0';
    }
    [[nodiscard]] bool consume(const char value) noexcept
    {
        if (peek() != value) return false;
        ++offset_;
        return true;
    }
    [[nodiscard]] bool read_string(std::string& output)
    {
        if (!consume('"')) return false;
        const auto begin = offset_;
        while (offset_ < input_.size() && input_[offset_] != '"') {
            const auto value = static_cast<unsigned char>(input_[offset_]);
            if (value < 0x20U || value > 0x7eU || input_[offset_] == '\\') return false;
            ++offset_;
        }
        if (!consume('"')) return false;
        output.assign(input_.substr(begin, offset_ - begin - 1U));
        return true;
    }
    [[nodiscard]] ManifestReadResult syntax(std::string context) const
    {
        return {
            std::nullopt,
            StockRuntimeCaptureCorpusError{
                StockRuntimeCaptureCorpusErrorCode::invalid_json,
                offset_, std::move(context), std::nullopt},
        };
    }

    std::string_view input_;
    std::size_t offset_{0U};
};

[[nodiscard]] const ManifestScalar* property(
    const ManifestProperties& properties,
    const std::string_view name,
    const ManifestScalar::Kind kind) noexcept
{
    const auto found = properties.find(name);
    return found == properties.end() || found->second.kind != kind
               ? nullptr
               : &found->second;
}

[[nodiscard]] bool property_equals(
    const ManifestProperties& properties,
    const std::string_view name,
    const ManifestScalar::Kind kind,
    const std::string_view expected) noexcept
{
    const auto* value = property(properties, name, kind);
    return value != nullptr && value->value == expected;
}

[[nodiscard]] bool exact_properties(
    const ManifestProperties& properties,
    const std::span<const std::string_view> names) noexcept
{
    if (properties.size() != names.size()) {
        return false;
    }
    return std::ranges::all_of(names, [&properties](const auto name) {
        return properties.contains(name);
    });
}

[[nodiscard]] bool hexadecimal_sha256(
    const ManifestProperties& properties,
    const std::string_view name,
    const bool require_lowercase,
    std::string* canonical = nullptr)
{
    const auto* scalar = property(properties, name, ManifestScalar::Kind::string);
    if (scalar == nullptr || scalar->value.size() != 64U) {
        return false;
    }
    std::string lowered;
    if (canonical != nullptr) {
        lowered.reserve(64U);
    }
    for (const auto character : scalar->value) {
        const bool decimal = character >= '0' && character <= '9';
        const bool lower = character >= 'a' && character <= 'f';
        const bool upper = character >= 'A' && character <= 'F';
        if (!decimal && !lower && (!upper || require_lowercase)) {
            return false;
        }
        if (canonical != nullptr) {
            lowered.push_back(upper
                                  ? static_cast<char>(character - 'A' + 'a')
                                  : character);
        }
    }
    if (canonical != nullptr) {
        *canonical = std::move(lowered);
    }
    return true;
}

template<typename Integer>
[[nodiscard]] bool manifest_integer(
    const ManifestProperties& properties,
    const std::string_view name,
    Integer& value) noexcept
{
    const auto* scalar = property(properties, name, ManifestScalar::Kind::integer);
    if (scalar == nullptr) return false;
    Integer candidate{};
    const auto converted = std::from_chars(
        scalar->value.data(), scalar->value.data() + scalar->value.size(),
        candidate, 10);
    if (converted.ec != std::errc{} ||
        converted.ptr != scalar->value.data() + scalar->value.size()) return false;
    value = candidate;
    return true;
}

[[nodiscard]] bool nullable_kind(
    const ManifestProperties& properties,
    const std::string_view name,
    const ManifestScalar::Kind populated_kind) noexcept
{
    const auto found = properties.find(name);
    return found != properties.end() &&
           (found->second.kind == populated_kind ||
            found->second.kind == ManifestScalar::Kind::null_value);
}

[[nodiscard]] bool valid_version_document(
    const ManifestProperties& properties,
    const bool require_accepted_profile)
{
    constexpr std::array names{
        std::string_view{"schema"},
        std::string_view{"map_category"},
        std::string_view{"client_file_version"},
        std::string_view{"client_pe_machine"},
        std::string_view{"client_signature"},
        std::string_view{"client_profile_fingerprint"},
        std::string_view{"server_launcher_version"},
        std::string_view{"server_pe_machine"},
        std::string_view{"server_signature"},
        std::string_view{"server_profile_fingerprint"},
        std::string_view{"steam_app_id"},
        std::string_view{"steam_build_id"},
        std::string_view{"server_engine_version"},
        std::string_view{"protocol"},
        std::string_view{"server_build"},
        std::string_view{"evidence_status"},
    };
    if (!exact_properties(properties, names) ||
        property(properties, "map_category",
                 ManifestScalar::Kind::string) == nullptr ||
        property(properties, "client_file_version",
                 ManifestScalar::Kind::string) == nullptr ||
        property(properties, "client_pe_machine",
                 ManifestScalar::Kind::string) == nullptr ||
        property(properties, "client_signature",
                 ManifestScalar::Kind::string) == nullptr ||
        property(properties, "server_launcher_version",
                 ManifestScalar::Kind::string) == nullptr ||
        property(properties, "server_pe_machine",
                 ManifestScalar::Kind::string) == nullptr ||
        property(properties, "server_signature",
                 ManifestScalar::Kind::string) == nullptr ||
        property(properties, "server_engine_version",
                 ManifestScalar::Kind::string) == nullptr ||
        property(properties, "evidence_status",
                 ManifestScalar::Kind::string) == nullptr ||
        property(properties, "steam_app_id",
                 ManifestScalar::Kind::integer) == nullptr ||
        property(properties, "steam_build_id",
                 ManifestScalar::Kind::integer) == nullptr ||
        property(properties, "protocol",
                 ManifestScalar::Kind::integer) == nullptr ||
        property(properties, "server_build",
                 ManifestScalar::Kind::integer) == nullptr ||
        !hexadecimal_sha256(
            properties, "client_profile_fingerprint", true) ||
        !hexadecimal_sha256(
            properties, "server_profile_fingerprint", true)) {
        return false;
    }
    if (!require_accepted_profile) {
        return true;
    }
    const auto* map_category = property(
        properties, "map_category", ManifestScalar::Kind::string);
    return (map_category->value == "boot_camp" ||
            map_category->value == "crossfire" ||
            map_category->value == "stalkyard") &&
           property_equals(properties, "client_file_version",
                           ManifestScalar::Kind::string, "1.1.1.1") &&
           property_equals(properties, "client_pe_machine",
                           ManifestScalar::Kind::string, "x86") &&
           property_equals(properties, "client_signature",
                           ManifestScalar::Kind::string, "valid") &&
           property_equals(properties, "server_launcher_version",
                           ManifestScalar::Kind::string, "4.1.1.1") &&
           property_equals(properties, "server_pe_machine",
                           ManifestScalar::Kind::string, "x86") &&
           property_equals(properties, "server_signature",
                           ManifestScalar::Kind::string, "valid") &&
           property_equals(properties, "steam_app_id",
                           ManifestScalar::Kind::integer, "70") &&
           property_equals(properties, "steam_build_id",
                           ManifestScalar::Kind::integer, "15961492") &&
           property_equals(properties, "server_engine_version",
                           ManifestScalar::Kind::string, "1.1.2.2") &&
           property_equals(properties, "protocol",
                           ManifestScalar::Kind::integer, "48") &&
           property_equals(properties, "server_build",
                           ManifestScalar::Kind::integer, "10210") &&
           property_equals(properties, "evidence_status",
                           ManifestScalar::Kind::string, "observed");
}

[[nodiscard]] bool valid_isolation_document(
    const ManifestProperties& properties,
    const bool require_accepted_profile) noexcept
{
    constexpr std::array names{
        std::string_view{"schema"},
        std::string_view{"session_type"},
        std::string_view{"persistent_rule_count"},
        std::string_view{"ipv4_loopback"},
        std::string_view{"ipv6_loopback"},
        std::string_view{"non_loopback_canary"},
        std::string_view{"cleanup_status"},
        std::string_view{"evidence_status"},
    };
    if (!exact_properties(properties, names) ||
        property(properties, "persistent_rule_count",
                 ManifestScalar::Kind::integer) == nullptr ||
        std::ranges::any_of(
            std::array{
                std::string_view{"session_type"},
                std::string_view{"ipv4_loopback"},
                std::string_view{"ipv6_loopback"},
                std::string_view{"non_loopback_canary"},
                std::string_view{"cleanup_status"},
                std::string_view{"evidence_status"},
            },
            [&properties](const auto name) {
                return property(properties, name,
                                ManifestScalar::Kind::string) == nullptr;
            })) {
        return false;
    }
    if (!require_accepted_profile) {
        return true;
    }
    const auto* ipv6 = property(
        properties, "ipv6_loopback", ManifestScalar::Kind::string);
    return property_equals(properties, "session_type",
                           ManifestScalar::Kind::string, "dynamic") &&
           property_equals(properties, "persistent_rule_count",
                           ManifestScalar::Kind::integer, "0") &&
           property_equals(properties, "ipv4_loopback",
                           ManifestScalar::Kind::string, "allowed") &&
           (ipv6->value == "allowed" ||
            ipv6->value == "capability_unavailable") &&
           property_equals(properties, "non_loopback_canary",
                           ManifestScalar::Kind::string,
                           "denied_os_classified") &&
           property_equals(properties, "cleanup_status",
                           ManifestScalar::Kind::string, "exact") &&
           property_equals(properties, "evidence_status",
                           ManifestScalar::Kind::string, "observed");
}

[[nodiscard]] bool valid_restoration_document(
    const ManifestProperties& properties,
    const bool require_accepted_profile)
{
    constexpr std::array names{
        std::string_view{"schema"},
        std::string_view{"external_file_drift"},
        std::string_view{"snapshot_entry_count"},
        std::string_view{"pre_manifest_sha256"},
        std::string_view{"post_manifest_sha256"},
        std::string_view{"external_snapshot_entry_count"},
        std::string_view{"external_pre_manifest_sha256"},
        std::string_view{"external_post_manifest_sha256"},
        std::string_view{"created_files_removed"},
        std::string_view{"protected_paths_included"},
        std::string_view{"owned_processes_stopped"},
        std::string_view{"input_automation_used"},
        std::string_view{"input_events_injected"},
        std::string_view{"orchestrator_exit_code"},
        std::string_view{"restoration_status"},
    };
    if (!exact_properties(properties, names) ||
        property(properties, "external_file_drift",
                 ManifestScalar::Kind::string) == nullptr ||
        property(properties, "restoration_status",
                 ManifestScalar::Kind::string) == nullptr ||
        std::ranges::any_of(
            std::array{
                std::string_view{"snapshot_entry_count"},
                std::string_view{"external_snapshot_entry_count"},
                std::string_view{"input_events_injected"},
                std::string_view{"orchestrator_exit_code"},
            },
            [&properties](const auto name) {
                return property(properties, name,
                                ManifestScalar::Kind::integer) == nullptr;
            }) ||
        std::ranges::any_of(
            std::array{
                std::string_view{"created_files_removed"},
                std::string_view{"protected_paths_included"},
                std::string_view{"owned_processes_stopped"},
                std::string_view{"input_automation_used"},
            },
            [&properties](const auto name) {
                return property(properties, name,
                                ManifestScalar::Kind::boolean) == nullptr;
            })) {
        return false;
    }
    std::string before;
    std::string after;
    std::string external_before;
    std::string external_after;
    if (!hexadecimal_sha256(properties, "pre_manifest_sha256", false,
                            &before) ||
        !hexadecimal_sha256(properties, "post_manifest_sha256", false,
                            &after) ||
        !hexadecimal_sha256(
            properties, "external_pre_manifest_sha256", false,
            &external_before) ||
        !hexadecimal_sha256(
            properties, "external_post_manifest_sha256", false,
            &external_after)) {
        return false;
    }
    if (!require_accepted_profile) {
        return true;
    }
    return before == after && external_before == external_after &&
           property_equals(properties, "external_file_drift",
                           ManifestScalar::Kind::string, "none") &&
           property_equals(properties, "created_files_removed",
                           ManifestScalar::Kind::boolean, "true") &&
           property_equals(properties, "protected_paths_included",
                           ManifestScalar::Kind::boolean, "true") &&
           property_equals(properties, "owned_processes_stopped",
                           ManifestScalar::Kind::boolean, "true") &&
           property_equals(properties, "input_automation_used",
                           ManifestScalar::Kind::boolean, "false") &&
           property_equals(properties, "input_events_injected",
                           ManifestScalar::Kind::integer, "0") &&
           property_equals(properties, "orchestrator_exit_code",
                           ManifestScalar::Kind::integer, "0") &&
           property_equals(properties, "restoration_status",
                           ManifestScalar::Kind::string, "exact");
}

[[nodiscard]] bool valid_research_manifest_shape(
    const ManifestProperties& properties) noexcept
{
    constexpr std::array names{
        std::string_view{"schema"},
        std::string_view{"run_id"},
        std::string_view{"scenario"},
        std::string_view{"map_category"},
        std::string_view{"duration_ms"},
        std::string_view{"isolation_status"},
        std::string_view{"process_ownership_status"},
        std::string_view{"version_profile_status"},
        std::string_view{"relay_status"},
        std::string_view{"client_ready_status"},
        std::string_view{"restoration_status"},
        std::string_view{"external_drift_status"},
        std::string_view{"raw_datagram_count"},
        std::string_view{"journal_entry_count"},
        std::string_view{"delivered_sequenced_c2s_count"},
        std::string_view{"delivered_sequenced_s2c_count"},
        std::string_view{"delivered_fragment_datagram_count"},
        std::string_view{"reassembled_payload_count"},
        std::string_view{"decompressed_payload_count"},
        std::string_view{"offline_replay_status"},
        std::string_view{"post_resource_boundary_status"},
        std::string_view{"post_resource_replay_payload_ordinal"},
        std::string_view{"post_resource_corpus_observed_ordinal"},
        std::string_view{"post_resource_delivery_ordinal"},
        std::string_view{"post_resource_byte_offset"},
        std::string_view{"post_resource_bit_offset"},
        std::string_view{"post_resource_source_sequence"},
        std::string_view{"post_resource_source_payload_bytes"},
        std::string_view{"post_resource_source_payload_bits"},
        std::string_view{"post_resource_next_unconsumed_bits"},
        std::string_view{"post_resource_reassembled"},
        std::string_view{"post_resource_decompressed"},
        std::string_view{"post_resource_boundary_byte_aligned"},
        std::string_view{"first_observation_status"},
        std::string_view{"first_candidate"},
        std::string_view{"first_candidate_bit_width"},
        std::string_view{"first_candidate_recurrence"},
        std::string_view{"candidate_stability"},
        std::string_view{"transport_structural_sha256"},
        std::string_view{"replay_structural_sha256"},
        std::string_view{"last_observed_transport_timestamp_us"},
        std::string_view{"last_delivered_sequenced_s2c_timestamp_us"},
        std::string_view{"accepted_transport_run"},
        std::string_view{"accepted_evidence_run"},
        std::string_view{"failure_category"},
    };
    if (!exact_properties(properties, names)) {
        return false;
    }
    constexpr std::array required_strings{
        std::string_view{"schema"}, std::string_view{"run_id"},
        std::string_view{"scenario"}, std::string_view{"map_category"},
        std::string_view{"isolation_status"},
        std::string_view{"process_ownership_status"},
        std::string_view{"version_profile_status"},
        std::string_view{"relay_status"},
        std::string_view{"client_ready_status"},
        std::string_view{"restoration_status"},
        std::string_view{"external_drift_status"},
        std::string_view{"offline_replay_status"},
        std::string_view{"post_resource_boundary_status"},
        std::string_view{"first_observation_status"},
        std::string_view{"failure_category"},
    };
    if (std::ranges::any_of(required_strings, [&properties](const auto name) {
            return property(properties, name,
                            ManifestScalar::Kind::string) == nullptr;
        }) ||
        property(properties, "accepted_transport_run",
                 ManifestScalar::Kind::boolean) == nullptr ||
        property(properties, "accepted_evidence_run",
                 ManifestScalar::Kind::boolean) == nullptr) {
        return false;
    }
    constexpr std::array nullable_integers{
        std::string_view{"duration_ms"},
        std::string_view{"raw_datagram_count"},
        std::string_view{"journal_entry_count"},
        std::string_view{"delivered_sequenced_c2s_count"},
        std::string_view{"delivered_sequenced_s2c_count"},
        std::string_view{"delivered_fragment_datagram_count"},
        std::string_view{"reassembled_payload_count"},
        std::string_view{"decompressed_payload_count"},
        std::string_view{"post_resource_replay_payload_ordinal"},
        std::string_view{"post_resource_corpus_observed_ordinal"},
        std::string_view{"post_resource_delivery_ordinal"},
        std::string_view{"post_resource_byte_offset"},
        std::string_view{"post_resource_bit_offset"},
        std::string_view{"post_resource_source_sequence"},
        std::string_view{"post_resource_source_payload_bytes"},
        std::string_view{"post_resource_source_payload_bits"},
        std::string_view{"post_resource_next_unconsumed_bits"},
        std::string_view{"first_candidate_bit_width"},
        std::string_view{"first_candidate_recurrence"},
        std::string_view{"last_observed_transport_timestamp_us"},
        std::string_view{"last_delivered_sequenced_s2c_timestamp_us"},
    };
    return std::ranges::all_of(
               nullable_integers, [&properties](const auto name) {
                   return nullable_kind(
                       properties, name, ManifestScalar::Kind::integer);
               }) &&
           nullable_kind(properties, "post_resource_boundary_byte_aligned",
                          ManifestScalar::Kind::boolean) &&
           nullable_kind(properties, "post_resource_reassembled",
                         ManifestScalar::Kind::boolean) &&
           nullable_kind(properties, "post_resource_decompressed",
                         ManifestScalar::Kind::boolean) &&
           nullable_kind(properties, "first_candidate",
                          ManifestScalar::Kind::string) &&
           nullable_kind(properties, "transport_structural_sha256",
                         ManifestScalar::Kind::string) &&
           nullable_kind(properties, "replay_structural_sha256",
                         ManifestScalar::Kind::string) &&
           nullable_kind(properties, "candidate_stability",
                          ManifestScalar::Kind::string);
}

[[nodiscard]] std::optional<std::string_view> canonical_runtime_scenario(
    const std::string_view value) noexcept
{
    if (value == "drop-server-runtime") {
        return "drop-server-to-client-transport-ordinal";
    }
    if (value == "duplicate-server-runtime") {
        return "duplicate-server-to-client-transport-ordinal";
    }
    if (value == "reorder-server-runtime") {
        return "reorder-server-to-client-transport-ordinal";
    }
    constexpr std::array canonical{
        std::string_view{"baseline"},
        std::string_view{"idle-runtime"},
        std::string_view{"reconnect"},
        std::string_view{"drop-server-to-client-transport-ordinal"},
        std::string_view{"duplicate-server-to-client-transport-ordinal"},
        std::string_view{"reorder-server-to-client-transport-ordinal"},
    };
    return std::ranges::find(canonical, value) != canonical.end()
        ? std::optional<std::string_view>{value}
        : std::nullopt;
}

[[nodiscard]] bool accepted_runtime_map_category(
    const std::string_view value) noexcept
{
    return value == "boot_camp" || value == "crossfire" ||
           value == "stalkyard";
}

[[nodiscard]] bool valid_accepted_research_manifest(
    const ManifestProperties& properties) noexcept
{
    constexpr std::array required_integers{
        std::string_view{"duration_ms"},
        std::string_view{"raw_datagram_count"},
        std::string_view{"journal_entry_count"},
        std::string_view{"delivered_sequenced_c2s_count"},
        std::string_view{"delivered_sequenced_s2c_count"},
        std::string_view{"delivered_fragment_datagram_count"},
        std::string_view{"reassembled_payload_count"},
        std::string_view{"decompressed_payload_count"},
        std::string_view{"post_resource_replay_payload_ordinal"},
        std::string_view{"post_resource_corpus_observed_ordinal"},
        std::string_view{"post_resource_delivery_ordinal"},
        std::string_view{"post_resource_byte_offset"},
        std::string_view{"post_resource_bit_offset"},
        std::string_view{"post_resource_source_sequence"},
        std::string_view{"post_resource_source_payload_bytes"},
        std::string_view{"post_resource_source_payload_bits"},
        std::string_view{"post_resource_next_unconsumed_bits"},
        std::string_view{"first_candidate_bit_width"},
        std::string_view{"first_candidate_recurrence"},
        std::string_view{"last_observed_transport_timestamp_us"},
        std::string_view{"last_delivered_sequenced_s2c_timestamp_us"},
    };
    if (std::ranges::any_of(required_integers, [&properties](const auto name) {
            return property(properties, name,
                            ManifestScalar::Kind::integer) == nullptr;
        }) ||
        property(properties, "post_resource_boundary_byte_aligned",
                  ManifestScalar::Kind::boolean) == nullptr ||
        property(properties, "post_resource_reassembled",
                  ManifestScalar::Kind::boolean) == nullptr ||
        property(properties, "post_resource_decompressed",
                  ManifestScalar::Kind::boolean) == nullptr ||
        property(properties, "first_candidate",
                  ManifestScalar::Kind::string) == nullptr ||
        property(properties, "transport_structural_sha256",
                  ManifestScalar::Kind::string) == nullptr ||
        property(properties, "replay_structural_sha256",
                  ManifestScalar::Kind::string) == nullptr ||
        property(properties, "candidate_stability",
                 ManifestScalar::Kind::string) == nullptr) {
        return false;
    }
    const auto* stability = property(
        properties, "candidate_stability", ManifestScalar::Kind::string);
    const auto* scenario = property(
        properties, "scenario", ManifestScalar::Kind::string);
    const auto* map_category = property(
        properties, "map_category", ManifestScalar::Kind::string);
    std::uint64_t duration_ms = 0U;
    std::uint64_t byte_offset = 0U;
    std::uint64_t bit_offset = 0U;
    std::uint64_t source_payload_bytes = 0U;
    std::uint64_t source_payload_bits = 0U;
    std::uint64_t next_unconsumed_bits = 0U;
    std::uint64_t candidate_bit_width = 0U;
    std::uint64_t candidate_recurrence = 0U;
    std::uint64_t last_observed_us = 0U;
    std::uint64_t last_s2c_us = 0U;
    if (scenario == nullptr || map_category == nullptr ||
        !canonical_runtime_scenario(scenario->value) ||
        !accepted_runtime_map_category(map_category->value) ||
        !manifest_integer(properties, "duration_ms", duration_ms) ||
        !manifest_integer(properties, "post_resource_byte_offset", byte_offset) ||
        !manifest_integer(properties, "post_resource_bit_offset", bit_offset) ||
        !manifest_integer(properties, "post_resource_source_payload_bytes",
                          source_payload_bytes) ||
        !manifest_integer(properties, "post_resource_source_payload_bits",
                          source_payload_bits) ||
        !manifest_integer(properties, "post_resource_next_unconsumed_bits",
                          next_unconsumed_bits) ||
        !manifest_integer(properties, "first_candidate_bit_width",
                          candidate_bit_width) ||
        !manifest_integer(properties, "first_candidate_recurrence",
                          candidate_recurrence) ||
        !manifest_integer(properties, "last_observed_transport_timestamp_us",
                          last_observed_us) ||
        !manifest_integer(properties,
                          "last_delivered_sequenced_s2c_timestamp_us",
                          last_s2c_us) ||
        bit_offset > 7U || source_payload_bytes == 0U ||
        source_payload_bytes >
            (std::numeric_limits<std::uint64_t>::max)() / 8U ||
        source_payload_bits != source_payload_bytes * 8U ||
        byte_offset > source_payload_bytes ||
        byte_offset * 8U + bit_offset > source_payload_bits ||
        next_unconsumed_bits !=
            source_payload_bits - (byte_offset * 8U + bit_offset) ||
        next_unconsumed_bits == 0U || candidate_bit_width == 0U ||
        candidate_bit_width > 8U ||
        candidate_bit_width > next_unconsumed_bits ||
        candidate_recurrence != 1U ||
        !hexadecimal_sha256(properties, "transport_structural_sha256", true) ||
        !hexadecimal_sha256(properties, "replay_structural_sha256", true)) {
        return false;
    }
    const bool duration_gated = scenario->value == "baseline" ||
                                scenario->value == "idle-runtime";
    if (duration_gated && (duration_ms < 30'000U ||
                           last_observed_us < 30'000'000U)) {
        return false;
    }
    if (scenario->value == "idle-runtime" && last_s2c_us < 30'000'000U) {
        return false;
    }
    const auto* aligned = property(
        properties, "post_resource_boundary_byte_aligned",
        ManifestScalar::Kind::boolean);
    const auto* candidate = property(
        properties, "first_candidate", ManifestScalar::Kind::string);
    if (aligned == nullptr || candidate == nullptr ||
        (aligned->value == "true") != (bit_offset == 0U) ||
        ((bit_offset == 0U) !=
         !candidate->value.starts_with("bit-prefix:")) ||
        (bit_offset == 0U && candidate_bit_width != 8U)) {
        return false;
    }
    const auto candidate_text = candidate->value.starts_with("bit-prefix:")
        ? std::string_view{candidate->value}.substr(11U)
        : std::string_view{candidate->value};
    std::uint32_t candidate_value = 0U;
    const auto converted = std::from_chars(
        candidate_text.data(), candidate_text.data() + candidate_text.size(),
        candidate_value, 10);
    if (candidate_text.empty() || converted.ec != std::errc{} ||
        converted.ptr != candidate_text.data() + candidate_text.size() ||
        candidate_value > 255U ||
        (candidate_bit_width < 8U &&
         candidate_value >= (std::uint32_t{1U} << candidate_bit_width))) {
        return false;
    }
    return property_equals(properties, "isolation_status",
                           ManifestScalar::Kind::string, "verified") &&
           property_equals(properties, "process_ownership_status",
                           ManifestScalar::Kind::string,
                           "verified-cleanup") &&
           property_equals(properties, "version_profile_status",
                           ManifestScalar::Kind::string, "verified") &&
           property_equals(properties, "relay_status",
                           ManifestScalar::Kind::string, "true") &&
           property_equals(properties, "client_ready_status",
                           ManifestScalar::Kind::string, "true") &&
           property_equals(properties, "restoration_status",
                           ManifestScalar::Kind::string, "exact") &&
           property_equals(properties, "external_drift_status",
                           ManifestScalar::Kind::string, "none") &&
           property_equals(properties, "offline_replay_status",
                           ManifestScalar::Kind::string, "success") &&
           property_equals(properties, "post_resource_boundary_status",
                           ManifestScalar::Kind::string, "observed") &&
           property_equals(properties, "first_observation_status",
                           ManifestScalar::Kind::string, "observed") &&
           property_equals(properties, "accepted_transport_run",
                           ManifestScalar::Kind::boolean, "true") &&
           property_equals(properties, "accepted_evidence_run",
                           ManifestScalar::Kind::boolean, "true") &&
           property_equals(properties, "failure_category",
                           ManifestScalar::Kind::string, "none") &&
           stability->value == "single_observation";
}

[[nodiscard]] std::optional<StockRuntimeAcceptedManifestClaims>
parse_accepted_manifest_claims(const ManifestProperties& properties)
{
    StockRuntimeAcceptedManifestClaims claims;
    const auto* reassembled = property(
        properties, "post_resource_reassembled", ManifestScalar::Kind::boolean);
    const auto* decompressed = property(
        properties, "post_resource_decompressed", ManifestScalar::Kind::boolean);
    const auto* aligned = property(
        properties, "post_resource_boundary_byte_aligned",
        ManifestScalar::Kind::boolean);
    const auto* candidate = property(
        properties, "first_candidate", ManifestScalar::Kind::string);
    const auto* stability = property(
        properties, "candidate_stability", ManifestScalar::Kind::string);
    const auto* replay_hash = property(
        properties, "replay_structural_sha256", ManifestScalar::Kind::string);
    if (reassembled == nullptr || decompressed == nullptr || aligned == nullptr ||
        candidate == nullptr || stability == nullptr || replay_hash == nullptr ||
        !manifest_integer(properties, "reassembled_payload_count",
                          claims.reassembled_payload_count) ||
        !manifest_integer(properties, "decompressed_payload_count",
                          claims.decompressed_payload_count) ||
        !manifest_integer(properties, "post_resource_replay_payload_ordinal",
                          claims.replay_payload_ordinal) ||
        !manifest_integer(properties, "post_resource_corpus_observed_ordinal",
                          claims.corpus_observed_ordinal) ||
        !manifest_integer(properties, "post_resource_delivery_ordinal",
                          claims.delivery_ordinal) ||
        !manifest_integer(properties, "post_resource_byte_offset",
                          claims.byte_offset) ||
        !manifest_integer(properties, "post_resource_bit_offset",
                          claims.bit_offset) ||
        !manifest_integer(properties, "post_resource_source_sequence",
                          claims.source_netchan_sequence) ||
        !manifest_integer(properties, "post_resource_source_payload_bytes",
                          claims.source_payload_byte_count) ||
        !manifest_integer(properties, "post_resource_source_payload_bits",
                          claims.source_payload_bit_count) ||
        !manifest_integer(properties, "post_resource_next_unconsumed_bits",
                          claims.next_unconsumed_bit_count) ||
        !manifest_integer(properties, "first_candidate_bit_width",
                          claims.candidate_bit_width) ||
        !manifest_integer(properties, "first_candidate_recurrence",
                          claims.candidate_recurrence)) {
        return std::nullopt;
    }
    claims.reassembled = reassembled->value == "true";
    claims.decompressed = decompressed->value == "true";
    claims.byte_aligned = aligned->value == "true";
    claims.first_candidate = candidate->value;
    claims.candidate_stability = stability->value;
    claims.replay_structural_sha256 = replay_hash->value;
    return claims;
}

[[nodiscard]] std::optional<StockRuntimeCorpusDocument> read_document(
    const fs::path& path,
    const std::string_view expected_schema,
    const std::size_t maximum_bytes,
    StockRuntimeCaptureCorpusError& error,
    ManifestProperties* properties = nullptr)
{
    auto read = read_bounded_regular_file(path, maximum_bytes);
    if (!read.bytes) {
        error = std::move(*read.error);
        return std::nullopt;
    }
    const auto manifest_text = bytes_as_string(*read.bytes);
    FlatManifestReader reader{manifest_text};
    auto parsed = reader.read();
    if (!parsed.properties) {
        error = std::move(*parsed.error);
        return std::nullopt;
    }
    const auto* schema = property(
        *parsed.properties, "schema", ManifestScalar::Kind::string);
    if (schema == nullptr || schema->value != expected_schema) {
        error = StockRuntimeCaptureCorpusError{
            StockRuntimeCaptureCorpusErrorCode::wrong_schema, 0U,
            "manifest schema does not match its file role", std::nullopt};
        return std::nullopt;
    }
    const auto digest = hash::sha256(*read.bytes);
    if (!digest) {
        error = StockRuntimeCaptureCorpusError{
            StockRuntimeCaptureCorpusErrorCode::structural_hash_failed, 0U,
            "manifest structural hash could not be computed", std::nullopt};
        return std::nullopt;
    }
    if (properties != nullptr) {
        *properties = std::move(*parsed.properties);
    }
    return StockRuntimeCorpusDocument{
        std::string{expected_schema}, hash::sha256_hex(*digest)};
}

[[nodiscard]] bool valid_corpus_limits(
    const StockRuntimeCaptureCorpusLimits& limits) noexcept
{
    return limits.maximum_manifest_bytes > 0U &&
           limits.maximum_manifest_bytes <= 8U * 1'024U * 1'024U &&
           limits.maximum_journal_bytes > 0U &&
           limits.maximum_journal_bytes <= 256U * 1'024U * 1'024U &&
           limits.maximum_log_files <= 64U &&
           limits.maximum_total_log_bytes <= 64U * 1'024U * 1'024U &&
           limits.journal.maximum_entries > 0U &&
           limits.journal.maximum_entries <=
               StockRuntimeCaptureHardCaps::maximum_datagrams;
}

} // namespace

StockRuntimeCorpusObservedDatagram::StockRuntimeCorpusObservedDatagram(
    StockRuntimeTransportJournalEntry journal,
    std::shared_ptr<const std::vector<std::byte>> bytes) noexcept
    : journal_{std::move(journal)}, bytes_{std::move(bytes)}
{
}

const StockRuntimeTransportJournalEntry&
StockRuntimeCorpusObservedDatagram::journal() const noexcept
{
    return journal_;
}

std::span<const std::byte> StockRuntimeCorpusObservedDatagram::bytes() const noexcept
{
    return *bytes_;
}

StockRuntimeCorpusDeliveredDatagram::StockRuntimeCorpusDeliveredDatagram(
    const std::size_t delivery_ordinal,
    const StockRuntimeTransportJournalEntry& journal,
    std::shared_ptr<const std::vector<std::byte>> bytes) noexcept
    : delivery_ordinal_{delivery_ordinal},
      observed_ordinal_{journal.observed_ordinal},
      direction_{journal.direction},
      direction_ordinal_{journal.direction_ordinal},
      observed_relative_timestamp_us_{journal.relative_timestamp_us},
      bytes_{std::move(bytes)}
{
}

std::size_t StockRuntimeCorpusDeliveredDatagram::delivery_ordinal() const noexcept
{
    return delivery_ordinal_;
}
std::size_t StockRuntimeCorpusDeliveredDatagram::observed_ordinal() const noexcept
{
    return observed_ordinal_;
}
StockRuntimeCaptureDirection
StockRuntimeCorpusDeliveredDatagram::direction() const noexcept
{
    return direction_;
}
std::size_t StockRuntimeCorpusDeliveredDatagram::direction_ordinal() const noexcept
{
    return direction_ordinal_;
}
std::uint64_t
StockRuntimeCorpusDeliveredDatagram::observed_relative_timestamp_us() const noexcept
{
    return observed_relative_timestamp_us_;
}
std::span<const std::byte> StockRuntimeCorpusDeliveredDatagram::bytes() const noexcept
{
    return *bytes_;
}

StockRuntimeCaptureCorpusState::StockRuntimeCaptureCorpusState(
    std::string run_id,
    StockRuntimeCaptureMetadata capture_metadata,
    const StockRuntimeCaptureCorpusLoadPolicy load_policy,
    const StockRuntimeCaptureCorpusPublicationState publication_state,
    std::vector<StockRuntimeCorpusObservedDatagram> observed_datagrams,
    std::vector<StockRuntimeCorpusDeliveredDatagram> delivered_datagrams,
    std::vector<StockRuntimeCorpusDeliveredDatagram> delivered_client_to_server,
    std::vector<StockRuntimeCorpusDeliveredDatagram> delivered_server_to_client,
    StockRuntimeCorpusDocument version_observation,
    StockRuntimeCorpusDocument isolation_attestation,
    StockRuntimeCorpusDocument restoration_attestation,
    std::optional<StockRuntimeCorpusDocument> research_run_metadata,
    std::optional<StockRuntimeAcceptedManifestClaims> accepted_manifest_claims,
    std::string structural_sha256) noexcept
    : run_id_{std::move(run_id)},
      capture_metadata_{std::move(capture_metadata)},
      load_policy_{load_policy},
      publication_state_{publication_state},
      observed_datagrams_{std::move(observed_datagrams)},
      delivered_datagrams_{std::move(delivered_datagrams)},
      delivered_client_to_server_{std::move(delivered_client_to_server)},
      delivered_server_to_client_{std::move(delivered_server_to_client)},
      version_observation_{std::move(version_observation)},
      isolation_attestation_{std::move(isolation_attestation)},
      restoration_attestation_{std::move(restoration_attestation)},
      research_run_metadata_{std::move(research_run_metadata)},
      accepted_manifest_claims_{std::move(accepted_manifest_claims)},
      structural_sha256_{std::move(structural_sha256)}
{
}

std::string_view StockRuntimeCaptureCorpusState::run_id() const noexcept { return run_id_; }
const StockRuntimeCaptureMetadata&
StockRuntimeCaptureCorpusState::capture_metadata() const noexcept { return capture_metadata_; }
StockRuntimeCaptureCorpusLoadPolicy
StockRuntimeCaptureCorpusState::load_policy() const noexcept { return load_policy_; }
StockRuntimeCaptureCorpusPublicationState
StockRuntimeCaptureCorpusState::publication_state() const noexcept { return publication_state_; }
bool StockRuntimeCaptureCorpusState::accepted_evidence_run() const noexcept
{
    return publication_state_ ==
           StockRuntimeCaptureCorpusPublicationState::published_accepted;
}
const std::vector<StockRuntimeCorpusObservedDatagram>&
StockRuntimeCaptureCorpusState::observed_datagrams() const noexcept
{
    return observed_datagrams_;
}
const std::vector<StockRuntimeCorpusDeliveredDatagram>&
StockRuntimeCaptureCorpusState::delivered_datagrams() const noexcept
{
    return delivered_datagrams_;
}
const std::vector<StockRuntimeCorpusDeliveredDatagram>&
StockRuntimeCaptureCorpusState::delivered_client_to_server() const noexcept
{
    return delivered_client_to_server_;
}
const std::vector<StockRuntimeCorpusDeliveredDatagram>&
StockRuntimeCaptureCorpusState::delivered_server_to_client() const noexcept
{
    return delivered_server_to_client_;
}
const StockRuntimeCorpusDocument&
StockRuntimeCaptureCorpusState::version_observation() const noexcept
{
    return version_observation_;
}
const StockRuntimeCorpusDocument&
StockRuntimeCaptureCorpusState::isolation_attestation() const noexcept
{
    return isolation_attestation_;
}
const StockRuntimeCorpusDocument&
StockRuntimeCaptureCorpusState::restoration_attestation() const noexcept
{
    return restoration_attestation_;
}
const std::optional<StockRuntimeCorpusDocument>&
StockRuntimeCaptureCorpusState::research_run_metadata() const noexcept
{
    return research_run_metadata_;
}
const std::optional<StockRuntimeAcceptedManifestClaims>&
StockRuntimeCaptureCorpusState::accepted_manifest_claims() const noexcept
{
    return accepted_manifest_claims_;
}
std::string_view StockRuntimeCaptureCorpusState::structural_sha256() const noexcept
{
    return structural_sha256_;
}

StockRuntimeCaptureCorpusLoader::StockRuntimeCaptureCorpusLoader(
    StockRuntimeCaptureCorpusLimits limits) noexcept
    : limits_{std::move(limits)}
{
}

bool StockRuntimeCaptureCorpusLoader::valid_configuration() const noexcept
{
    return valid_corpus_limits(limits_);
}

const StockRuntimeCaptureCorpusLimits&
StockRuntimeCaptureCorpusLoader::limits() const noexcept
{
    return limits_;
}

StockRuntimeCaptureCorpusLoadResult StockRuntimeCaptureCorpusLoader::load(
    const fs::path& exact_run_directory,
    const StockRuntimeCaptureCorpusLoadPolicy policy) const
{
    if (!valid_configuration()) {
        return failure(StockRuntimeCaptureCorpusErrorCode::invalid_configuration,
                       "corpus loader limits are invalid");
    }
    if (exact_run_directory.empty() || exact_run_directory.has_filename() == false) {
        return failure(StockRuntimeCaptureCorpusErrorCode::unsafe_run_path,
                       "exact run directory is absent");
    }
    const auto run_id = exact_run_directory.filename().string();
    if (!valid_run_id(run_id)) {
        return failure(StockRuntimeCaptureCorpusErrorCode::invalid_run_id,
                       "run directory name is not 32 lowercase hexadecimal characters");
    }
    std::error_code path_error;
    const auto absolute = fs::absolute(exact_run_directory, path_error).lexically_normal();
    if (path_error || absolute.filename().string() != run_id ||
        absolute == absolute.root_path()) {
        return failure(StockRuntimeCaptureCorpusErrorCode::unsafe_run_path,
                       "run directory could not be normalized safely");
    }
    const auto component_state = inspect_existing_path_components(absolute);
    if (component_state == ExistingPathComponentsState::reparse) {
        return failure(
            StockRuntimeCaptureCorpusErrorCode::reparse_point,
            "run directory is reached through a reparse-backed path component");
    }
    if (component_state != ExistingPathComponentsState::safe) {
        return failure(
            StockRuntimeCaptureCorpusErrorCode::missing_directory,
            "run directory path has a missing, unreadable, or non-directory component");
    }
    const auto root_status = fs::symlink_status(absolute, path_error);
    if (path_error || !fs::is_directory(root_status)) {
        return failure(StockRuntimeCaptureCorpusErrorCode::missing_directory,
                       "run directory is absent or not a directory");
    }
    if (fs::is_symlink(root_status) || path_is_reparse(absolute)) {
        return failure(StockRuntimeCaptureCorpusErrorCode::reparse_point,
                       "run directory is a reparse point");
    }

    const bool prepublication =
        policy == StockRuntimeCaptureCorpusLoadPolicy::prepublication;
    const std::string version_filename = prepublication
        ? "version-observation.staged.json"
        : "version-observation.json";
    const std::string isolation_filename = prepublication
        ? "isolation-attestation.staged.json"
        : "isolation-attestation.json";
    const std::string restoration_filename = prepublication
        ? "restoration-attestation.staged.json"
        : "restoration-attestation.json";
    std::set<std::string, std::less<>> required_root_entries{
        "capture-metadata.json", version_filename, isolation_filename,
        restoration_filename, "transport-journal.jsonl", "raw", "logs",
    };
    if (!prepublication) {
        required_root_entries.insert("version-observation.staged.json");
        required_root_entries.insert("isolation-attestation.staged.json");
        required_root_entries.insert("restoration-attestation.staged.json");
    }
    std::set<std::string, std::less<>> seen_root_entries;
    for (const auto& item : fs::directory_iterator(absolute, path_error)) {
        if (path_error) {
            return failure(StockRuntimeCaptureCorpusErrorCode::read_failed,
                           "run directory enumeration failed");
        }
        const auto name = item.path().filename().string();
        if (name == "research-run-metadata.json") {
            if (policy == StockRuntimeCaptureCorpusLoadPolicy::prepublication) {
                return failure(StockRuntimeCaptureCorpusErrorCode::unexpected_manifest,
                               "prepublication corpus already has a final run manifest");
            }
        } else if (!required_root_entries.contains(name)) {
            return failure(StockRuntimeCaptureCorpusErrorCode::unexpected_file,
                           "run directory contains an unexpected entry");
        }
        if (!seen_root_entries.insert(name).second) {
            return failure(StockRuntimeCaptureCorpusErrorCode::unexpected_file,
                           "run directory entry is duplicated");
        }
        if (path_is_reparse(item.path())) {
            return failure(StockRuntimeCaptureCorpusErrorCode::reparse_point,
                           "run directory contains a reparse point");
        }
    }
    if (path_error) {
        return failure(StockRuntimeCaptureCorpusErrorCode::read_failed,
                       "run directory enumeration failed");
    }
    for (const auto& required : required_root_entries) {
        if (!seen_root_entries.contains(required)) {
            return failure(StockRuntimeCaptureCorpusErrorCode::missing_manifest,
                           "run directory is missing a required artifact");
        }
    }
    if (policy == StockRuntimeCaptureCorpusLoadPolicy::published &&
        !seen_root_entries.contains("research-run-metadata.json")) {
        return failure(StockRuntimeCaptureCorpusErrorCode::missing_manifest,
                       "published corpus lacks its final run manifest");
    }

    const auto raw_root = absolute / "raw";
    const auto logs_root = absolute / "logs";
    if (!fs::is_directory(fs::symlink_status(raw_root, path_error)) || path_error ||
        path_is_reparse(raw_root) ||
        !fs::is_directory(fs::symlink_status(logs_root, path_error)) || path_error ||
        path_is_reparse(logs_root)) {
        return failure(StockRuntimeCaptureCorpusErrorCode::reparse_point,
                       "raw or logs directory is absent, unsafe, or reparse-backed");
    }

    std::size_t log_count = 0U;
    std::uint64_t log_bytes = 0U;
    for (const auto& log : fs::directory_iterator(logs_root, path_error)) {
        if (path_error || ++log_count > limits_.maximum_log_files ||
            !safe_leaf_name(log.path().filename().string()) || path_is_reparse(log.path())) {
            return failure(StockRuntimeCaptureCorpusErrorCode::unexpected_file,
                           "logs directory violates its explicit bound");
        }
        const auto status = fs::symlink_status(log.path(), path_error);
        const auto size = fs::file_size(log.path(), path_error);
        if (path_error || !fs::is_regular_file(status) ||
            size > limits_.maximum_total_log_bytes ||
            log_bytes > limits_.maximum_total_log_bytes - size) {
            return failure(StockRuntimeCaptureCorpusErrorCode::file_too_large,
                           "bounded process logs violate their configured limit");
        }
        if (file_has_multiple_links(log.path())) {
            return failure(
                StockRuntimeCaptureCorpusErrorCode::hardlink_detected,
                "bounded process log does not have exclusive file identity");
        }
        log_bytes += size;
    }

    auto capture_file = read_bounded_regular_file(
        absolute / "capture-metadata.json", limits_.maximum_manifest_bytes);
    if (!capture_file.bytes) {
        return {std::nullopt, std::move(capture_file.error)};
    }
    const auto capture_parse = parse_stock_runtime_capture_metadata(
        bytes_as_string(*capture_file.bytes));
    if (!capture_parse || !capture_parse.metadata) {
        return failure(StockRuntimeCaptureCorpusErrorCode::invalid_capture_metadata,
                       "capture metadata v1 is invalid");
    }

    StockRuntimeCaptureCorpusError document_error;
    ManifestProperties version_properties;
    ManifestProperties isolation_properties;
    ManifestProperties restoration_properties;
    ManifestProperties staged_version_properties;
    ManifestProperties staged_isolation_properties;
    ManifestProperties staged_restoration_properties;
    auto version = read_document(
        absolute / version_filename,
        kStockRuntimeVersionObservationSchema, limits_.maximum_manifest_bytes,
        document_error, &version_properties);
    if (!version) return {std::nullopt, std::move(document_error)};
    auto isolation = read_document(
        absolute / isolation_filename,
        kStockRuntimeIsolationAttestationSchema, limits_.maximum_manifest_bytes,
        document_error, &isolation_properties);
    if (!isolation) return {std::nullopt, std::move(document_error)};
    auto restoration = read_document(
        absolute / restoration_filename,
        kStockRuntimeRestorationAttestationSchema, limits_.maximum_manifest_bytes,
        document_error, &restoration_properties);
    if (!restoration) return {std::nullopt, std::move(document_error)};
    std::optional<StockRuntimeCorpusDocument> staged_version;
    std::optional<StockRuntimeCorpusDocument> staged_isolation;
    std::optional<StockRuntimeCorpusDocument> staged_restoration;
    if (!prepublication) {
        staged_version = read_document(
            absolute / "version-observation.staged.json",
            kStockRuntimeVersionObservationSchema,
            limits_.maximum_manifest_bytes, document_error,
            &staged_version_properties);
        if (!staged_version) return {std::nullopt, std::move(document_error)};
        staged_isolation = read_document(
            absolute / "isolation-attestation.staged.json",
            kStockRuntimeIsolationAttestationSchema,
            limits_.maximum_manifest_bytes, document_error,
            &staged_isolation_properties);
        if (!staged_isolation) return {std::nullopt, std::move(document_error)};
        staged_restoration = read_document(
            absolute / "restoration-attestation.staged.json",
            kStockRuntimeRestorationAttestationSchema,
            limits_.maximum_manifest_bytes, document_error,
            &staged_restoration_properties);
        if (!staged_restoration) {
            return {std::nullopt, std::move(document_error)};
        }
    }

    std::optional<StockRuntimeCorpusDocument> research_run;
    std::optional<StockRuntimeAcceptedManifestClaims> accepted_manifest_claims;
    ManifestProperties research_properties;
    bool manifest_accepted = false;
    if (policy == StockRuntimeCaptureCorpusLoadPolicy::published) {
        research_run = read_document(
            absolute / "research-run-metadata.json", kStockRuntimeResearchRunSchema,
            limits_.maximum_manifest_bytes, document_error, &research_properties);
        if (!research_run) return {std::nullopt, std::move(document_error)};
        if (!valid_research_manifest_shape(research_properties)) {
            return failure(
                StockRuntimeCaptureCorpusErrorCode::invalid_json,
                "final manifest fields do not match the exact flat v1 contract");
        }
        const auto* manifest_run_id = property(
            research_properties, "run_id", ManifestScalar::Kind::string);
        const auto* accepted = property(
            research_properties, "accepted_evidence_run",
            ManifestScalar::Kind::boolean);
        if (manifest_run_id == nullptr || manifest_run_id->value != run_id) {
            return failure(StockRuntimeCaptureCorpusErrorCode::wrong_run_id,
                           "final manifest run ID differs from the directory");
        }
        if (accepted == nullptr) {
            return failure(StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch,
                           "final manifest lacks accepted-evidence state");
        }
        manifest_accepted = accepted->value == "true";
        if (manifest_accepted &&
            !valid_accepted_research_manifest(research_properties)) {
            return failure(
                StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch,
                "accepted final manifest does not satisfy every evidence gate");
        }
        if (manifest_accepted) {
            accepted_manifest_claims =
                parse_accepted_manifest_claims(research_properties);
            if (!accepted_manifest_claims) {
                return failure(
                    StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch,
                    "accepted final manifest claims could not be typed");
            }
        }
    }

    const bool acceptance_required =
        policy == StockRuntimeCaptureCorpusLoadPolicy::prepublication ||
        manifest_accepted;
    if (!valid_version_document(version_properties, acceptance_required) ||
        !valid_isolation_document(isolation_properties, acceptance_required) ||
        !valid_restoration_document(
            restoration_properties, acceptance_required) ||
        (!prepublication &&
         (!valid_version_document(staged_version_properties, true) ||
          !valid_isolation_document(staged_isolation_properties, true) ||
          !valid_restoration_document(staged_restoration_properties, true) ||
          !staged_version || !staged_isolation || !staged_restoration ||
          staged_version->structural_sha256 != version->structural_sha256 ||
          staged_isolation->structural_sha256 != isolation->structural_sha256 ||
          staged_restoration->structural_sha256 !=
              restoration->structural_sha256))) {
        return failure(
            StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch,
            "version, isolation, or restoration attestation is not acceptable");
    }

    auto journal_file = read_bounded_regular_file(
        absolute / "transport-journal.jsonl", limits_.maximum_journal_bytes);
    if (!journal_file.bytes) return {std::nullopt, std::move(journal_file.error)};
    const auto journal_text = bytes_as_string(*journal_file.bytes);
    std::vector<StockRuntimeTransportJournalEntry> journal;
    std::size_t line_begin = 0U;
    while (line_begin < journal_text.size()) {
        const auto line_end = journal_text.find('\n', line_begin);
        const auto end = line_end == std::string::npos ? journal_text.size() : line_end;
        auto line = std::string_view{journal_text}.substr(line_begin, end - line_begin);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
        if (line.empty()) {
            return failure(StockRuntimeCaptureCorpusErrorCode::invalid_journal,
                           "journal contains an empty line", journal.size());
        }
        const auto parsed = parse_stock_runtime_transport_journal_entry(line);
        if (!parsed || !parsed.entry) {
            return failure(
                StockRuntimeCaptureCorpusErrorCode::invalid_journal,
                "journal line is invalid", journal.size(),
                parsed.error ? std::optional{parsed.error->code} : std::nullopt);
        }
        journal.push_back(std::move(*parsed.entry));
        if (journal.size() > limits_.journal.maximum_entries) {
            return failure(StockRuntimeCaptureCorpusErrorCode::count_mismatch,
                           "journal entry count exceeds its bound", journal.size());
        }
        if (line_end == std::string::npos) break;
        line_begin = line_end + 1U;
    }
    // A finalized rejected run remains inspectable as bounded research
    // evidence.  Only that published-and-explicitly-unaccepted state may
    // retain unresolved holds or unexpected-source observations.  A
    // prepublication transaction must be complete before the final manifest
    // can be written, and an accepted manifest is always fail-closed.
    const auto journal_policy =
        policy == StockRuntimeCaptureCorpusLoadPolicy::published &&
                !manifest_accepted
            ? StockRuntimeTransportJournalValidationPolicy::incomplete_capture
            : StockRuntimeTransportJournalValidationPolicy::complete_capture;
    const auto journal_validation = validate_stock_runtime_transport_journal(
        journal, limits_.journal, journal_policy);
    if (!journal_validation) {
        return failure(
            StockRuntimeCaptureCorpusErrorCode::invalid_journal,
            "journal cross-entry validation failed",
            journal_validation.error ? journal_validation.error->entry_ordinal : 0U,
            journal_validation.error ? std::optional{journal_validation.error->code}
                                     : std::nullopt);
    }

    const auto& metadata = *capture_parse.metadata;
    std::uint64_t emitted_bytes = 0U;
    std::size_t dropped_count = 0U;
    std::size_t duplicated_count = 0U;
    std::size_t delayed_count = 0U;
    std::size_t wrong_source_count = 0U;
    for (const auto& entry : journal) {
        const auto emission_multiplier = entry.emitted_ordinals.size();
        if (emission_multiplier != 0U &&
            entry.payload_byte_count >
                ((std::numeric_limits<std::uint64_t>::max)() - emitted_bytes) /
                    emission_multiplier) {
            return failure(
                StockRuntimeCaptureCorpusErrorCode::byte_count_mismatch,
                "journal delivered-byte accounting overflowed");
        }
        emitted_bytes += static_cast<std::uint64_t>(entry.payload_byte_count) *
                         emission_multiplier;
        dropped_count +=
            entry.action == StockRuntimeCaptureAction::drop ? 1U : 0U;
        duplicated_count +=
            entry.action == StockRuntimeCaptureAction::duplicate ? 1U : 0U;
        delayed_count +=
            entry.action == StockRuntimeCaptureAction::hold_for_delay ||
                    entry.action == StockRuntimeCaptureAction::hold_for_reorder
                ? 1U
                : 0U;
        wrong_source_count += entry.wrong_source ? 1U : 0U;
    }
    if (metadata.counters.observed_datagrams != journal.size() ||
        metadata.counters.emitted_datagrams !=
            journal_validation.emitted_datagram_count ||
        metadata.counters.client_packets != journal_validation.client_to_server_count ||
        metadata.counters.server_packets != journal_validation.server_to_client_count) {
        return failure(StockRuntimeCaptureCorpusErrorCode::count_mismatch,
                       "capture metadata counters disagree with the journal");
    }
    if (metadata.counters.observed_raw_bytes !=
            journal_validation.observed_raw_bytes ||
        metadata.counters.emitted_bytes != emitted_bytes) {
        return failure(StockRuntimeCaptureCorpusErrorCode::byte_count_mismatch,
                       "capture metadata byte counters disagree with the journal");
    }
    if (metadata.counters.dropped_datagrams != dropped_count ||
        metadata.counters.duplicated_datagrams != duplicated_count ||
        metadata.counters.delayed_datagrams != delayed_count ||
        metadata.counters.ignored_wrong_source_datagrams !=
            wrong_source_count ||
        metadata.perturbation_count !=
            dropped_count + duplicated_count + delayed_count) {
        return failure(
            StockRuntimeCaptureCorpusErrorCode::count_mismatch,
            "capture metadata perturbation counters disagree with the journal");
    }
    if (metadata.bounded_transport_complete !=
        journal_validation.transport_complete) {
        return failure(
            StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch,
            "capture metadata and journal disagree about transport completeness");
    }
    const bool has_wrong_source = std::ranges::any_of(
        journal, [](const auto& entry) { return entry.wrong_source; });
    if (has_wrong_source &&
        (policy == StockRuntimeCaptureCorpusLoadPolicy::prepublication ||
         manifest_accepted)) {
        return failure(StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch,
                       "unexpected-source datagram prevents publication readiness");
    }
    if ((policy == StockRuntimeCaptureCorpusLoadPolicy::prepublication ||
         manifest_accepted) &&
        !metadata.bounded_transport_complete) {
        return failure(StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch,
                       "publication-ready corpus lacks complete bounded transport");
    }
    if (policy == StockRuntimeCaptureCorpusLoadPolicy::published &&
        manifest_accepted) {
        const auto* manifest_scenario_property = property(
            research_properties, "scenario", ManifestScalar::Kind::string);
        const auto* manifest_map_property = property(
            research_properties, "map_category", ManifestScalar::Kind::string);
        const auto* observed_map_property = property(
            version_properties, "map_category", ManifestScalar::Kind::string);
        const auto manifest_scenario = manifest_scenario_property != nullptr
            ? canonical_runtime_scenario(manifest_scenario_property->value)
            : std::nullopt;
        const auto captured_scenario = canonical_runtime_scenario(
            to_string(metadata.scenario));
        if (!manifest_scenario || !captured_scenario ||
            *manifest_scenario != *captured_scenario ||
            manifest_map_property == nullptr || observed_map_property == nullptr ||
            manifest_map_property->value != observed_map_property->value) {
            return failure(
                StockRuntimeCaptureCorpusErrorCode::publication_state_mismatch,
                "final manifest scenario/map disagree with immutable capture observations");
        }
        std::size_t manifest_raw_count = 0U;
        std::size_t manifest_journal_count = 0U;
        if (!manifest_integer(research_properties, "raw_datagram_count",
                              manifest_raw_count) ||
            !manifest_integer(research_properties, "journal_entry_count",
                              manifest_journal_count) ||
            manifest_raw_count != journal.size() ||
            manifest_journal_count != journal.size()) {
            return failure(StockRuntimeCaptureCorpusErrorCode::count_mismatch,
                           "final manifest counters disagree with the corpus");
        }
    }

    std::set<std::string, std::less<>> raw_names;
    for (const auto& raw : fs::directory_iterator(raw_root, path_error)) {
        if (path_error || path_is_reparse(raw.path())) {
            return failure(StockRuntimeCaptureCorpusErrorCode::reparse_point,
                           "raw directory enumeration reached an unsafe entry");
        }
        const auto name = raw.path().filename().string();
        if (!safe_leaf_name(name) || !raw_names.insert(name).second) {
            return failure(StockRuntimeCaptureCorpusErrorCode::invalid_filename,
                           "raw directory contains an invalid filename");
        }
    }
    if (raw_names.size() < journal.size()) {
        return failure(StockRuntimeCaptureCorpusErrorCode::missing_raw_file,
                       "raw directory lacks a journal-owned file");
    }
    if (raw_names.size() > journal.size()) {
        return failure(StockRuntimeCaptureCorpusErrorCode::count_mismatch,
                       "raw file cardinality exceeds journal cardinality");
    }
    for (const auto& raw_name : raw_names) {
        if (std::ranges::none_of(journal, [&raw_name](const auto& entry) {
                return entry.raw_filename == raw_name;
            })) {
            return failure(StockRuntimeCaptureCorpusErrorCode::unexpected_raw_file,
                           "raw directory contains a file absent from the journal");
        }
    }

    std::vector<StockRuntimeCorpusObservedDatagram> observed;
    std::vector<StockRuntimeCorpusDeliveredDatagram> delivered;
    std::vector<std::optional<StockRuntimeCorpusDeliveredDatagram>>
        delivered_slots;
    std::vector<StockRuntimeCorpusDeliveredDatagram> delivered_c2s;
    std::vector<StockRuntimeCorpusDeliveredDatagram> delivered_s2c;
    try {
        observed.reserve(journal.size());
        delivered.reserve(journal_validation.emitted_datagram_count);
        delivered_slots.resize(journal_validation.emitted_datagram_count);
        delivered_c2s.reserve(journal_validation.emitted_datagram_count);
        delivered_s2c.reserve(journal_validation.emitted_datagram_count);
    } catch (...) {
        return failure(StockRuntimeCaptureCorpusErrorCode::file_too_large,
                       "corpus publication allocation failed");
    }

    // A default delivered element has no byte owner. Each contiguous emission
    // ordinal is assigned exactly once by the already-validated journal.
    for (auto& entry : journal) {
        if (!raw_names.contains(entry.raw_filename)) {
            return failure(StockRuntimeCaptureCorpusErrorCode::missing_raw_file,
                           "journal raw file is absent", entry.observed_ordinal);
        }
        auto raw = read_bounded_regular_file(
            raw_root / entry.raw_filename, limits_.journal.maximum_payload_bytes);
        if (!raw.bytes) return {std::nullopt, std::move(raw.error)};
        if (raw.bytes->size() != entry.payload_byte_count) {
            return failure(StockRuntimeCaptureCorpusErrorCode::raw_size_mismatch,
                           "raw file size differs from its journal entry",
                           entry.observed_ordinal);
        }
        const auto digest = hash::sha256(*raw.bytes);
        if (!digest || hash::sha256_hex(*digest) != entry.sha256) {
            return failure(StockRuntimeCaptureCorpusErrorCode::raw_hash_mismatch,
                           "raw file SHA-256 differs from its journal entry",
                           entry.observed_ordinal);
        }
        auto bytes = std::make_shared<const std::vector<std::byte>>(
            std::move(*raw.bytes));
        for (const auto emission : entry.emitted_ordinals) {
            delivered_slots[emission] = StockRuntimeCorpusDeliveredDatagram{
                emission, entry, bytes};
        }
        observed.push_back(StockRuntimeCorpusObservedDatagram{
            std::move(entry), std::move(bytes)});
    }
    for (auto& slot : delivered_slots) {
        if (!slot) {
            return failure(StockRuntimeCaptureCorpusErrorCode::count_mismatch,
                           "validated emission ordinal lacks a raw owner");
        }
        delivered.push_back(std::move(*slot));
    }
    for (const auto& datagram : delivered) {
        if (datagram.direction() == StockRuntimeCaptureDirection::client_to_server) {
            delivered_c2s.push_back(datagram);
        } else {
            delivered_s2c.push_back(datagram);
        }
    }

    std::size_t delivered_sequenced_c2s = 0U;
    std::size_t delivered_sequenced_s2c = 0U;
    std::size_t delivered_fragment_datagrams = 0U;
    std::uint64_t last_delivered_sequenced_s2c_us = 0U;
    if (policy == StockRuntimeCaptureCorpusLoadPolicy::published &&
        manifest_accepted) {
        for (const auto& datagram : delivered) {
            const auto classification = classify_netchan_datagram(
                datagram.bytes());
            if (classification.classification ==
                NetchanDatagramClassification::connectionless) {
                continue;
            }
            if (classification.classification !=
                NetchanDatagramClassification::sequenced) {
                return failure(
                    StockRuntimeCaptureCorpusErrorCode::invalid_journal,
                    "accepted delivery has no valid netchan classifier",
                    datagram.delivery_ordinal());
            }
            const auto header = peek_netchan_header(datagram.bytes());
            if (!header || !header.packet) {
                return failure(
                    StockRuntimeCaptureCorpusErrorCode::invalid_journal,
                    "accepted delivery has an invalid netchan header",
                    datagram.delivery_ordinal());
            }
            if (datagram.direction() ==
                StockRuntimeCaptureDirection::client_to_server) {
                ++delivered_sequenced_c2s;
            } else {
                ++delivered_sequenced_s2c;
                last_delivered_sequenced_s2c_us = (std::max)(
                    last_delivered_sequenced_s2c_us,
                    datagram.observed_relative_timestamp_us());
            }
            if (header.packet->header.sequence.flags.fragmented) {
                ++delivered_fragment_datagrams;
            }
        }
    }

    std::string canonical;
    canonical.reserve(journal.size() * 96U);
    canonical.append(run_id);
    canonical.push_back('|');
    canonical.append(canonical_stock_runtime_capture_structure(metadata));
    canonical.push_back('|');
    // The public transport digest must not transitively fingerprint the
    // private snapshot/profile digests stored in local attestations.  Exact
    // staged/final byte hashes remain a loader-only integrity check above;
    // successful role validators are represented here by fixed literals.
    canonical.append("version-observation=validated-v1");
    canonical.push_back('|');
    canonical.append("isolation-attestation=validated-v1");
    canonical.push_back('|');
    canonical.append("restoration-attestation=validated-v1");
    for (const auto& datagram : observed) {
        canonical.push_back('|');
        canonical.append(std::to_string(datagram.journal().observed_ordinal));
        canonical.push_back(':');
        canonical.append(
            datagram.journal().direction ==
                    StockRuntimeCaptureDirection::client_to_server
                ? "c2s"
                : "s2c");
        canonical.push_back(':');
        canonical.append(std::to_string(datagram.journal().payload_byte_count));
        canonical.push_back(':');
        for (const auto emission : datagram.journal().emitted_ordinals) {
            canonical.append(std::to_string(emission));
            canonical.push_back(',');
        }
    }
    const auto canonical_bytes = std::as_bytes(
        std::span{canonical.data(), canonical.size()});
    const auto structural_digest = hash::sha256(canonical_bytes);
    if (!structural_digest) {
        return failure(StockRuntimeCaptureCorpusErrorCode::structural_hash_failed,
                       "corpus structural hash could not be computed");
    }
    const auto structural_hex = hash::sha256_hex(*structural_digest);
    if (policy == StockRuntimeCaptureCorpusLoadPolicy::published &&
        manifest_accepted) {
        std::size_t manifest_delivered_c2s = 0U;
        std::size_t manifest_delivered_s2c = 0U;
        std::size_t manifest_delivered_fragments = 0U;
        std::uint64_t manifest_last_observed_us = 0U;
        std::uint64_t manifest_last_delivered_s2c_us = 0U;
        const auto* manifest_transport_hash = property(
            research_properties, "transport_structural_sha256",
            ManifestScalar::Kind::string);
        const auto last_observed_us = journal.empty()
            ? std::uint64_t{0U}
            : journal.back().relative_timestamp_us;
        if (!manifest_integer(
                research_properties, "delivered_sequenced_c2s_count",
                manifest_delivered_c2s) ||
            !manifest_integer(
                research_properties, "delivered_sequenced_s2c_count",
                manifest_delivered_s2c) ||
            !manifest_integer(
                research_properties, "delivered_fragment_datagram_count",
                manifest_delivered_fragments) ||
            !manifest_integer(
                research_properties,
                "last_observed_transport_timestamp_us",
                manifest_last_observed_us) ||
            !manifest_integer(
                research_properties,
                "last_delivered_sequenced_s2c_timestamp_us",
                manifest_last_delivered_s2c_us) ||
            manifest_delivered_c2s != delivered_sequenced_c2s ||
            manifest_delivered_s2c != delivered_sequenced_s2c ||
            manifest_delivered_fragments != delivered_fragment_datagrams ||
            manifest_last_observed_us != last_observed_us ||
            manifest_last_delivered_s2c_us !=
                last_delivered_sequenced_s2c_us ||
            manifest_transport_hash == nullptr ||
            manifest_transport_hash->value != structural_hex) {
            return failure(
                StockRuntimeCaptureCorpusErrorCode::count_mismatch,
                "accepted final manifest delivery/hash/timestamp facts "
                "disagree with the corpus");
        }
    }

    const auto publication_state =
        policy == StockRuntimeCaptureCorpusLoadPolicy::prepublication
            ? StockRuntimeCaptureCorpusPublicationState::ready_for_manifest_publication
            : manifest_accepted
                ? StockRuntimeCaptureCorpusPublicationState::published_accepted
                : StockRuntimeCaptureCorpusPublicationState::published_incomplete;
    return StockRuntimeCaptureCorpusLoadResult{
        StockRuntimeCaptureCorpusState{
            run_id, metadata, policy, publication_state,
            std::move(observed), std::move(delivered),
            std::move(delivered_c2s), std::move(delivered_s2c),
            std::move(*version), std::move(*isolation),
            std::move(*restoration), std::move(research_run),
            std::move(accepted_manifest_claims),
            std::move(structural_hex)},
        std::nullopt,
    };
}

} // namespace hlclient::goldsrc
