#include <hlclient/goldsrc/stock_runtime_capture_corpus.hpp>

#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/stock_runtime_reconnect_lifecycle.hpp>
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

struct StrictJsonValue final {
    enum class Kind { string, integer, boolean, null_value, object, array };
    Kind kind{Kind::null_value};
    std::string scalar;
    std::map<std::string, StrictJsonValue, std::less<>> object;
    std::vector<StrictJsonValue> array;
};

class StrictJsonReader final {
public:
    explicit StrictJsonReader(const std::string_view input) noexcept
        : input_{input}
    {
    }

    [[nodiscard]] std::optional<StrictJsonValue> read()
    {
        auto value = read_value(0U);
        whitespace();
        return value && cursor_ == input_.size()
            ? std::move(value) : std::nullopt;
    }

private:
    static constexpr std::size_t kMaximumDepth = 8U;
    static constexpr std::size_t kMaximumMembers = 512U;

    void whitespace() noexcept
    {
        while (cursor_ < input_.size() &&
               (input_[cursor_] == ' ' || input_[cursor_] == '\t' ||
                input_[cursor_] == '\r' || input_[cursor_] == '\n')) {
            ++cursor_;
        }
    }

    [[nodiscard]] std::optional<std::string> string()
    {
        if (cursor_ >= input_.size() || input_[cursor_++] != '"') {
            return std::nullopt;
        }
        std::string result;
        while (cursor_ < input_.size() && input_[cursor_] != '"') {
            const auto character = input_[cursor_++];
            // Project-owned reconnect metadata is canonical printable ASCII.
            // Reject escape aliases and control bytes rather than normalizing.
            if (character == '\\' ||
                static_cast<unsigned char>(character) < 0x20U ||
                static_cast<unsigned char>(character) > 0x7eU) {
                return std::nullopt;
            }
            result.push_back(character);
        }
        if (cursor_ >= input_.size() || input_[cursor_++] != '"') {
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] bool literal(const std::string_view expected) noexcept
    {
        if (input_.substr(cursor_, expected.size()) != expected) return false;
        cursor_ += expected.size();
        return true;
    }

    [[nodiscard]] std::optional<StrictJsonValue> read_value(
        const std::size_t depth)
    {
        if (depth > kMaximumDepth) return std::nullopt;
        whitespace();
        if (cursor_ >= input_.size()) return std::nullopt;
        StrictJsonValue value;
        if (input_[cursor_] == '"') {
            auto parsed = string();
            if (!parsed) return std::nullopt;
            value.kind = StrictJsonValue::Kind::string;
            value.scalar = std::move(*parsed);
            return value;
        }
        if (input_[cursor_] == '{') {
            value.kind = StrictJsonValue::Kind::object;
            ++cursor_;
            whitespace();
            if (cursor_ < input_.size() && input_[cursor_] == '}') {
                ++cursor_;
                return value;
            }
            while (cursor_ < input_.size()) {
                whitespace();
                auto name = string();
                whitespace();
                if (!name || name->empty() || cursor_ >= input_.size() ||
                    input_[cursor_++] != ':') {
                    return std::nullopt;
                }
                auto child = read_value(depth + 1U);
                if (!child || value.object.size() >= kMaximumMembers ||
                    !value.object.emplace(
                        std::move(*name), std::move(*child)).second) {
                    return std::nullopt;
                }
                whitespace();
                if (cursor_ >= input_.size()) return std::nullopt;
                if (input_[cursor_] == '}') {
                    ++cursor_;
                    return value;
                }
                if (input_[cursor_++] != ',') return std::nullopt;
            }
            return std::nullopt;
        }
        if (input_[cursor_] == '[') {
            value.kind = StrictJsonValue::Kind::array;
            ++cursor_;
            whitespace();
            if (cursor_ < input_.size() && input_[cursor_] == ']') {
                ++cursor_;
                return value;
            }
            while (cursor_ < input_.size()) {
                auto child = read_value(depth + 1U);
                if (!child || value.array.size() >= kMaximumMembers) {
                    return std::nullopt;
                }
                value.array.push_back(std::move(*child));
                whitespace();
                if (cursor_ >= input_.size()) return std::nullopt;
                if (input_[cursor_] == ']') {
                    ++cursor_;
                    return value;
                }
                if (input_[cursor_++] != ',') return std::nullopt;
            }
            return std::nullopt;
        }
        if (literal("true")) {
            value.kind = StrictJsonValue::Kind::boolean;
            value.scalar = "true";
            return value;
        }
        if (literal("false")) {
            value.kind = StrictJsonValue::Kind::boolean;
            value.scalar = "false";
            return value;
        }
        if (literal("null")) {
            value.kind = StrictJsonValue::Kind::null_value;
            value.scalar = "null";
            return value;
        }
        const auto begin = cursor_;
        while (cursor_ < input_.size() && input_[cursor_] >= '0' &&
               input_[cursor_] <= '9') {
            ++cursor_;
        }
        if (cursor_ == begin ||
            (cursor_ - begin > 1U && input_[begin] == '0')) {
            return std::nullopt;
        }
        value.kind = StrictJsonValue::Kind::integer;
        value.scalar = std::string{input_.substr(begin, cursor_ - begin)};
        return value;
    }

    std::string_view input_;
    std::size_t cursor_{0U};
};

[[nodiscard]] const StrictJsonValue* json_property(
    const StrictJsonValue& object,
    const std::string_view name,
    const StrictJsonValue::Kind kind) noexcept
{
    if (object.kind != StrictJsonValue::Kind::object) return nullptr;
    const auto found = object.object.find(name);
    return found != object.object.end() && found->second.kind == kind
        ? &found->second : nullptr;
}

[[nodiscard]] bool json_exact_properties(
    const StrictJsonValue& object,
    const std::span<const std::string_view> names) noexcept
{
    return object.kind == StrictJsonValue::Kind::object &&
           object.object.size() == names.size() &&
           std::ranges::all_of(names, [&object](const auto name) {
               return object.object.contains(name);
           });
}

[[nodiscard]] bool json_string_equals(
    const StrictJsonValue& object,
    const std::string_view name,
    const std::string_view expected) noexcept
{
    const auto* value = json_property(
        object, name, StrictJsonValue::Kind::string);
    return value != nullptr && value->scalar == expected;
}

[[nodiscard]] bool json_boolean_equals(
    const StrictJsonValue& object,
    const std::string_view name,
    const bool expected) noexcept
{
    const auto* value = json_property(
        object, name, StrictJsonValue::Kind::boolean);
    return value != nullptr &&
           value->scalar == (expected ? "true" : "false");
}

[[nodiscard]] std::optional<std::size_t> json_integer(
    const StrictJsonValue& object,
    const std::string_view name,
    const std::size_t maximum) noexcept
{
    const auto* value = json_property(
        object, name, StrictJsonValue::Kind::integer);
    if (value == nullptr) return std::nullopt;
    std::size_t result = 0U;
    const auto converted = std::from_chars(
        value->scalar.data(), value->scalar.data() + value->scalar.size(),
        result, 10);
    return converted.ec == std::errc{} &&
           converted.ptr == value->scalar.data() + value->scalar.size() &&
           result <= maximum
        ? std::optional<std::size_t>{result} : std::nullopt;
}

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
    constexpr std::array base_names{
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
    constexpr std::array reconnect_names{
        std::string_view{"connection_generation_count"},
        std::string_view{"exact_boundary_count"},
        std::string_view{"runtime_candidate_count"},
        std::string_view{"generation_distinct"},
        std::string_view{"candidate_conflict"},
    };
    const auto* scenario = property(
        properties, "scenario", ManifestScalar::Kind::string);
    const auto* accepted = property(
        properties, "accepted_evidence_run", ManifestScalar::Kind::boolean);
    if (scenario == nullptr || accepted == nullptr) return false;
    const bool accepted_reconnect = scenario->value == "reconnect" &&
                                    accepted->value == "true";
    if (properties.size() != base_names.size() +
            (accepted_reconnect ? reconnect_names.size() : 0U) ||
        !std::ranges::all_of(base_names, [&properties](const auto name) {
            return properties.contains(name);
        }) ||
        (accepted_reconnect &&
         !std::ranges::all_of(reconnect_names, [&properties](const auto name) {
             return properties.contains(name);
         }))) {
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
    const bool base_valid = std::ranges::all_of(
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
    if (!base_valid || !accepted_reconnect) return base_valid;
    return property(properties, "connection_generation_count",
                    ManifestScalar::Kind::integer) != nullptr &&
           property(properties, "exact_boundary_count",
                    ManifestScalar::Kind::integer) != nullptr &&
           property(properties, "runtime_candidate_count",
                    ManifestScalar::Kind::integer) != nullptr &&
           property(properties, "generation_distinct",
                    ManifestScalar::Kind::boolean) != nullptr &&
           property(properties, "candidate_conflict",
                    ManifestScalar::Kind::boolean) != nullptr;
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
        !hexadecimal_sha256(properties, "transport_structural_sha256", true) ||
        !hexadecimal_sha256(properties, "replay_structural_sha256", true)) {
        return false;
    }
    const bool reconnect_scenario = scenario->value == "reconnect";
    if (candidate_recurrence != (reconnect_scenario ? 2U : 1U) ||
        stability->value !=
            (reconnect_scenario ? "stable_observation"
                                : "single_observation")) {
        return false;
    }
    if (reconnect_scenario &&
        (!property_equals(
             properties, "connection_generation_count",
             ManifestScalar::Kind::integer, "2") ||
         !property_equals(
             properties, "exact_boundary_count",
             ManifestScalar::Kind::integer, "2") ||
         !property_equals(
             properties, "runtime_candidate_count",
             ManifestScalar::Kind::integer, "2") ||
         !property_equals(
             properties, "generation_distinct",
             ManifestScalar::Kind::boolean, "true") ||
         !property_equals(
             properties, "candidate_conflict",
             ManifestScalar::Kind::boolean, "false"))) {
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
                           ManifestScalar::Kind::string, "none");
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

[[nodiscard]] bool valid_reconnect_transport_generation(
    const StrictJsonValue& object,
    const std::size_t index,
    StockRuntimeConnectionGenerationObservation& generation) noexcept
{
    static constexpr std::array names{
        std::string_view{"generation_ordinal"},
        std::string_view{"endpoint_role_identity"},
        std::string_view{"process_role_identity"},
        std::string_view{"first_observed_ordinal"},
        std::string_view{"last_observed_ordinal"},
        std::string_view{"connectionless_exchange_count"},
        std::string_view{"connect_observed"},
        std::string_view{"accept_observed"},
        std::string_view{"first_sequenced_packet_ordinal"},
        std::string_view{"client_to_server_packet_count"},
        std::string_view{"server_to_client_packet_count"},
        std::string_view{"profile_identity"},
        std::string_view{"post_resource_boundary_status"},
        std::string_view{"candidate_status"},
        std::string_view{"candidate_body_consumed"},
        std::string_view{"candidate_semantic_category_assigned"}};
    const std::array endpoint_roles{
        kStockRuntimeGenerationAEndpointRole,
        kStockRuntimeGenerationBEndpointRole};
    const std::array process_roles{
        kStockRuntimeGenerationAProcessRole,
        kStockRuntimeGenerationBProcessRole};
    const auto ordinal = json_integer(object, "generation_ordinal", 2U);
    const auto first = json_integer(
        object, "first_observed_ordinal",
        StockRuntimeCaptureHardCaps::maximum_datagrams - 1U);
    const auto last = json_integer(
        object, "last_observed_ordinal",
        StockRuntimeCaptureHardCaps::maximum_datagrams - 1U);
    const auto connectionless = json_integer(
        object, "connectionless_exchange_count",
        StockRuntimeCaptureHardCaps::maximum_datagrams);
    const auto first_sequenced = json_integer(
        object, "first_sequenced_packet_ordinal",
        StockRuntimeCaptureHardCaps::maximum_datagrams - 1U);
    const auto c2s = json_integer(
        object, "client_to_server_packet_count",
        StockRuntimeCaptureHardCaps::maximum_datagrams);
    const auto s2c = json_integer(
        object, "server_to_client_packet_count",
        StockRuntimeCaptureHardCaps::maximum_datagrams);
    if (index >= 2U || !json_exact_properties(object, names) || !ordinal ||
        *ordinal != index + 1U || !first || !last || *first > *last ||
        !connectionless || *connectionless == 0U || !first_sequenced ||
        *first_sequenced < *first || *first_sequenced > *last ||
        !c2s || *c2s == 0U || !s2c || *s2c == 0U ||
        !json_string_equals(
            object, "endpoint_role_identity", endpoint_roles[index]) ||
        !json_string_equals(
            object, "process_role_identity", process_roles[index]) ||
        !json_boolean_equals(object, "connect_observed", true) ||
        !json_boolean_equals(object, "accept_observed", true) ||
        !json_string_equals(
            object, "profile_identity", kStockRuntimePendingProfile) ||
        !json_string_equals(
            object, "post_resource_boundary_status", "evidence_pending") ||
        !json_string_equals(object, "candidate_status", "evidence_pending") ||
        !json_boolean_equals(object, "candidate_body_consumed", false) ||
        !json_boolean_equals(
            object, "candidate_semantic_category_assigned", false)) {
        return false;
    }
    generation.generation_ordinal = *ordinal;
    generation.learned_client_endpoint_role_identity =
        std::string{endpoint_roles[index]};
    generation.owned_client_process_role_identity =
        std::string{process_roles[index]};
    generation.first_observed_ordinal = *first;
    generation.last_observed_ordinal = *last;
    generation.connectionless_exchange_count = *connectionless;
    generation.connect_observed = true;
    generation.accept_observed = true;
    generation.first_sequenced_packet_ordinal = *first_sequenced;
    generation.client_to_server_packet_count = *c2s;
    generation.server_to_client_packet_count = *s2c;
    generation.profile_identity = std::string{kStockRuntimePendingProfile};
    return true;
}

[[nodiscard]] bool valid_reconnect_transport_document(
    const StrictJsonValue& root) noexcept
{
    static constexpr std::array names{
        std::string_view{"schema"},
        std::string_view{"connection_generation_count"},
        std::string_view{"generation_distinct"},
        std::string_view{"generation_a_tail_emitter_ready_before_shutdown"},
        std::string_view{"generation_a_controlled_shutdown"},
        std::string_view{"generation_a_endpoint_quiet"},
        std::string_view{"guard_continuity"},
        std::string_view{"server_continuity"},
        std::string_view{"relay_continuity"},
        std::string_view{"post_resource_boundary_status"},
        std::string_view{"candidate_status"},
        std::string_view{"candidate_body_consumed"},
        std::string_view{"candidate_semantic_category_assigned"},
        std::string_view{"retired_generation_a_tail_sink"},
        std::string_view{"retired_generation_a_server_tail_packet_count"},
        std::string_view{"generation_b_sequenced_after_fresh_accept"},
        std::string_view{"bounded_transport_complete"},
        std::string_view{"generations"}};
    const auto generations = json_property(
        root, "generations", StrictJsonValue::Kind::array);
    if (!json_exact_properties(root, names) ||
        !json_string_equals(
            root, "schema", kStockRuntimeReconnectTransportObservationSchema) ||
        json_integer(root, "connection_generation_count", 2U) != 2U ||
        !json_boolean_equals(root, "generation_distinct", true) ||
        !json_boolean_equals(
            root, "generation_a_tail_emitter_ready_before_shutdown", true) ||
        !json_string_equals(
            root, "generation_a_controlled_shutdown",
            "observed_by_orchestrator") ||
        !json_boolean_equals(root, "generation_a_endpoint_quiet", true) ||
        !json_string_equals(
            root, "guard_continuity", "observed_by_orchestrator") ||
        !json_string_equals(
            root, "server_continuity", "observed_by_orchestrator") ||
        !json_string_equals(root, "relay_continuity", "observed") ||
        !json_string_equals(
            root, "post_resource_boundary_status", "evidence_pending") ||
        !json_string_equals(root, "candidate_status", "evidence_pending") ||
        !json_boolean_equals(root, "candidate_body_consumed", false) ||
        !json_boolean_equals(
            root, "candidate_semantic_category_assigned", false) ||
        !json_string_equals(
            root, "retired_generation_a_tail_sink", "routing_only") ||
        !json_integer(
            root, "retired_generation_a_server_tail_packet_count",
            StockRuntimeCaptureHardCaps::maximum_datagrams) ||
        !json_boolean_equals(
            root, "generation_b_sequenced_after_fresh_accept", true) ||
        !json_boolean_equals(root, "bounded_transport_complete", true) ||
        generations == nullptr || generations->array.size() != 2U) {
        return false;
    }
    std::array<StockRuntimeConnectionGenerationObservation, 2U> parsed;
    return valid_reconnect_transport_generation(
               generations->array[0U], 0U, parsed[0U]) &&
           valid_reconnect_transport_generation(
               generations->array[1U], 1U, parsed[1U]) &&
           parsed[0U].last_observed_ordinal <
               parsed[1U].first_observed_ordinal;
}

[[nodiscard]] bool valid_reconnect_orchestration_document(
    const StrictJsonValue& root) noexcept
{
    static constexpr std::array names{
        std::string_view{"schema"},
        std::string_view{"connection_generation_count"},
        std::string_view{"generation_distinct"},
        std::string_view{"generation_a_process_role_identity"},
        std::string_view{"generation_b_process_role_identity"},
        std::string_view{"generation_a_endpoint_role_identity"},
        std::string_view{"generation_b_endpoint_role_identity"},
        std::string_view{"generation_a_tail_emitter_ready_before_shutdown"},
        std::string_view{"generation_a_controlled_shutdown"},
        std::string_view{"generation_a_endpoint_quiet"},
        std::string_view{"generation_b_fresh_owned_process"},
        std::string_view{"generation_b_fresh_connection_lifecycle"},
        std::string_view{"guard_continuity"},
        std::string_view{"server_continuity"},
        std::string_view{"relay_continuity"},
        std::string_view{"cleanup_status"},
        std::string_view{"restoration_status"},
        std::string_view{"post_resource_boundary_status"},
        std::string_view{"candidate_status"},
        std::string_view{"candidate_body_consumed"},
        std::string_view{"candidate_semantic_category_assigned"},
        std::string_view{"publication_status"}};
    return json_exact_properties(root, names) &&
           json_string_equals(
               root, "schema",
               kStockRuntimeReconnectOrchestrationAttestationSchema) &&
           json_integer(root, "connection_generation_count", 2U) == 2U &&
           json_boolean_equals(root, "generation_distinct", true) &&
           json_string_equals(
               root, "generation_a_process_role_identity",
               kStockRuntimeGenerationAProcessRole) &&
           json_string_equals(
               root, "generation_b_process_role_identity",
               kStockRuntimeGenerationBProcessRole) &&
           json_string_equals(
               root, "generation_a_endpoint_role_identity",
               kStockRuntimeGenerationAEndpointRole) &&
           json_string_equals(
               root, "generation_b_endpoint_role_identity",
               kStockRuntimeGenerationBEndpointRole) &&
           json_boolean_equals(
               root, "generation_a_tail_emitter_ready_before_shutdown",
               true) &&
           json_boolean_equals(
               root, "generation_a_controlled_shutdown", true) &&
           json_boolean_equals(root, "generation_a_endpoint_quiet", true) &&
           json_boolean_equals(
               root, "generation_b_fresh_owned_process", true) &&
           json_string_equals(
               root, "generation_b_fresh_connection_lifecycle",
               "observed_by_relay") &&
           json_boolean_equals(root, "guard_continuity", true) &&
           json_boolean_equals(root, "server_continuity", true) &&
           json_boolean_equals(root, "relay_continuity", true) &&
           json_string_equals(root, "cleanup_status", "exact") &&
           json_string_equals(root, "restoration_status", "wrapper_pending") &&
           json_string_equals(
               root, "post_resource_boundary_status", "evidence_pending") &&
           json_string_equals(root, "candidate_status", "evidence_pending") &&
           json_boolean_equals(root, "candidate_body_consumed", false) &&
           json_boolean_equals(
               root, "candidate_semantic_category_assigned", false) &&
           json_string_equals(root, "publication_status", "staged");
}

[[nodiscard]] bool read_final_boundary(
    const StrictJsonValue& object,
    StockRuntimeGenerationBoundaryObservation& boundary) noexcept
{
    static constexpr std::array names{
        std::string_view{"observed"},
        std::string_view{"replay_payload_ordinal"},
        std::string_view{"corpus_observed_ordinal"},
        std::string_view{"delivery_ordinal"},
        std::string_view{"byte_offset"},
        std::string_view{"bit_offset"},
        std::string_view{"source_payload_byte_count"},
        std::string_view{"source_payload_bit_count"},
        std::string_view{"next_unconsumed_bit_count"}};
    const auto replay_payload = json_integer(
        object, "replay_payload_ordinal",
        StockRuntimeCaptureHardCaps::maximum_datagrams - 1U);
    const auto observed = json_integer(
        object, "corpus_observed_ordinal",
        StockRuntimeCaptureHardCaps::maximum_datagrams - 1U);
    const auto delivery = json_integer(
        object, "delivery_ordinal",
        StockRuntimeCaptureHardCaps::maximum_datagrams * 2U - 1U);
    const auto byte_offset = json_integer(
        object, "byte_offset", StockRuntimeCaptureHardCaps::maximum_payload_bytes);
    const auto bit_offset = json_integer(object, "bit_offset", 7U);
    const auto source_bytes = json_integer(
        object, "source_payload_byte_count",
        StockRuntimeCaptureHardCaps::maximum_decompressed_bytes);
    const auto source_bits = json_integer(
        object, "source_payload_bit_count",
        StockRuntimeCaptureHardCaps::maximum_decompressed_bytes * 8U);
    const auto remaining = json_integer(
        object, "next_unconsumed_bit_count",
        StockRuntimeCaptureHardCaps::maximum_decompressed_bytes * 8U);
    if (!json_exact_properties(object, names) ||
        !json_boolean_equals(object, "observed", true) || !replay_payload ||
        !observed || !delivery || !byte_offset || !bit_offset || !source_bytes ||
        *source_bytes == 0U || !source_bits || !remaining) {
        return false;
    }
    boundary = {true, *replay_payload, *observed, *delivery, *byte_offset,
                *bit_offset, *source_bytes, *source_bits, *remaining};
    return true;
}

[[nodiscard]] bool read_optional_candidate_byte(
    const StrictJsonValue& object,
    const std::string_view name,
    std::optional<std::uint8_t>& result) noexcept
{
    if (json_property(object, name, StrictJsonValue::Kind::null_value) !=
        nullptr) {
        result.reset();
        return true;
    }
    const auto value = json_integer(object, name, 255U);
    if (!value) return false;
    result = static_cast<std::uint8_t>(*value);
    return true;
}

[[nodiscard]] bool read_final_candidate(
    const StrictJsonValue& object,
    StockRuntimeGenerationCandidateObservation& candidate) noexcept
{
    static constexpr std::array names{
        std::string_view{"observed"},
        std::string_view{"candidate_bit_width"},
        std::string_view{"numeric_candidate"},
        std::string_view{"bounded_bit_prefix"},
        std::string_view{"byte_aligned"},
        std::string_view{"body_consumed"},
        std::string_view{"semantic_category_assigned"}};
    const auto width = json_integer(object, "candidate_bit_width", 8U);
    if (!json_exact_properties(object, names) || !width || *width == 0U ||
        !json_boolean_equals(object, "observed", true) ||
        !read_optional_candidate_byte(
            object, "numeric_candidate", candidate.numeric_candidate) ||
        !read_optional_candidate_byte(
            object, "bounded_bit_prefix", candidate.bounded_bit_prefix) ||
        candidate.numeric_candidate.has_value() ==
            candidate.bounded_bit_prefix.has_value() ||
        !json_boolean_equals(object, "body_consumed", false) ||
        !json_boolean_equals(object, "semantic_category_assigned", false)) {
        return false;
    }
    candidate.observed = true;
    candidate.candidate_bit_width = *width;
    candidate.byte_aligned =
        json_boolean_equals(object, "byte_aligned", true);
    candidate.body_consumed = false;
    candidate.semantic_category_assigned = false;
    return json_boolean_equals(
        object, "byte_aligned", candidate.byte_aligned);
}

[[nodiscard]] bool read_final_generation(
    const StrictJsonValue& object,
    const std::size_t index,
    StockRuntimeConnectionGenerationObservation& generation) noexcept
{
    static constexpr std::array names{
        std::string_view{"generation_ordinal"},
        std::string_view{"profile_identity"},
        std::string_view{"owned_client_process_role_identity"},
        std::string_view{"learned_client_endpoint_role_identity"},
        std::string_view{"fresh_owned_client_process"},
        std::string_view{"learned_client_endpoint_observed"},
        std::string_view{"learned_client_endpoint_distinct_from_previous"},
        std::string_view{"first_observed_ordinal"},
        std::string_view{"last_observed_ordinal"},
        std::string_view{"connectionless_exchange_count"},
        std::string_view{"connect_observed"},
        std::string_view{"accept_observed"},
        std::string_view{"first_sequenced_packet_ordinal"},
        std::string_view{"client_to_server_packet_count"},
        std::string_view{"server_to_client_packet_count"},
        std::string_view{"controlled_client_shutdown_observed"},
        std::string_view{"retired_client_endpoint_quiet"},
        std::string_view{"exact_post_resource_boundary"},
        std::string_view{"candidate_observation"}};
    if (!json_exact_properties(object, names) || index >= 2U) return false;
    const std::array endpoint_roles{
        kStockRuntimeGenerationAEndpointRole,
        kStockRuntimeGenerationBEndpointRole};
    const std::array process_roles{
        kStockRuntimeGenerationAProcessRole,
        kStockRuntimeGenerationBProcessRole};
    const auto ordinal = json_integer(object, "generation_ordinal", 2U);
    const auto first = json_integer(
        object, "first_observed_ordinal",
        StockRuntimeCaptureHardCaps::maximum_datagrams - 1U);
    const auto last = json_integer(
        object, "last_observed_ordinal",
        StockRuntimeCaptureHardCaps::maximum_datagrams - 1U);
    const auto connectionless = json_integer(
        object, "connectionless_exchange_count",
        StockRuntimeCaptureHardCaps::maximum_datagrams);
    const auto first_sequenced = json_integer(
        object, "first_sequenced_packet_ordinal",
        StockRuntimeCaptureHardCaps::maximum_datagrams - 1U);
    const auto c2s = json_integer(
        object, "client_to_server_packet_count",
        StockRuntimeCaptureHardCaps::maximum_datagrams);
    const auto s2c = json_integer(
        object, "server_to_client_packet_count",
        StockRuntimeCaptureHardCaps::maximum_datagrams);
    const auto* boundary = json_property(
        object, "exact_post_resource_boundary", StrictJsonValue::Kind::object);
    const auto* candidate = json_property(
        object, "candidate_observation", StrictJsonValue::Kind::object);
    if (!ordinal || *ordinal != index + 1U || !first || !last ||
        *first > *last || !connectionless || *connectionless == 0U ||
        !first_sequenced || *first_sequenced < *first ||
        *first_sequenced > *last || !c2s || *c2s == 0U || !s2c ||
        *s2c == 0U ||
        !json_string_equals(
            object, "profile_identity", kStockRuntimePendingProfile) ||
        !json_string_equals(
            object, "owned_client_process_role_identity",
            process_roles[index]) ||
        !json_string_equals(
            object, "learned_client_endpoint_role_identity",
            endpoint_roles[index]) ||
        !json_boolean_equals(object, "fresh_owned_client_process", true) ||
        !json_boolean_equals(
            object, "learned_client_endpoint_observed", true) ||
        !json_boolean_equals(
            object, "learned_client_endpoint_distinct_from_previous",
            index != 0U) ||
        !json_boolean_equals(object, "connect_observed", true) ||
        !json_boolean_equals(object, "accept_observed", true) ||
        !json_boolean_equals(
            object, "controlled_client_shutdown_observed", index == 0U) ||
        !json_boolean_equals(
            object, "retired_client_endpoint_quiet", index == 0U) ||
        boundary == nullptr || candidate == nullptr) {
        return false;
    }
    generation.generation_ordinal = *ordinal;
    generation.profile_identity = std::string{kStockRuntimePendingProfile};
    generation.owned_client_process_role_identity =
        std::string{process_roles[index]};
    generation.learned_client_endpoint_role_identity =
        std::string{endpoint_roles[index]};
    generation.owned_client_process_observed = true;
    generation.fresh_owned_client_process = true;
    generation.learned_client_endpoint_observed = true;
    generation.learned_client_endpoint_distinct_from_previous = index != 0U;
    generation.first_observed_ordinal = *first;
    generation.last_observed_ordinal = *last;
    generation.connectionless_exchange_count = *connectionless;
    generation.connect_observed = true;
    generation.accept_observed = true;
    generation.first_sequenced_packet_ordinal = *first_sequenced;
    generation.client_to_server_packet_count = *c2s;
    generation.server_to_client_packet_count = *s2c;
    generation.controlled_client_shutdown_observed = index == 0U;
    generation.retired_client_endpoint_quiet = index == 0U;
    return read_final_boundary(
               *boundary, generation.exact_post_resource_boundary) &&
           read_final_candidate(*candidate, generation.candidate_observation);
}

[[nodiscard]] bool valid_reconnect_final_document(
    const StrictJsonValue& root)
{
    static constexpr std::array names{
        std::string_view{"schema"},
        std::string_view{"connection_generation_count"},
        std::string_view{"exact_boundary_count"},
        std::string_view{"runtime_candidate_count"},
        std::string_view{"generation_distinct"},
        std::string_view{"candidate_conflict"},
        std::string_view{"guard_continuity"},
        std::string_view{"server_continuity"},
        std::string_view{"relay_continuity"},
        std::string_view{"cleanup_exact"},
        std::string_view{"restoration_exact"},
        std::string_view{"candidate_body_consumed"},
        std::string_view{"candidate_semantic_category_assigned"},
        std::string_view{"retired_generation_a_tail_sink"},
        std::string_view{"retired_generation_a_server_tail_packet_count"},
        std::string_view{"generation_b_sequenced_after_fresh_accept"},
        std::string_view{"generations"}};
    const auto* generations = json_property(
        root, "generations", StrictJsonValue::Kind::array);
    if (!json_exact_properties(root, names) ||
        !json_string_equals(
            root, "schema", kStockRuntimeReconnectObservationSchema) ||
        json_integer(root, "connection_generation_count", 2U) != 2U ||
        json_integer(root, "exact_boundary_count", 2U) != 2U ||
        json_integer(root, "runtime_candidate_count", 2U) != 2U ||
        !json_boolean_equals(root, "generation_distinct", true) ||
        !json_boolean_equals(root, "candidate_conflict", false) ||
        !json_boolean_equals(root, "guard_continuity", true) ||
        !json_boolean_equals(root, "server_continuity", true) ||
        !json_boolean_equals(root, "relay_continuity", true) ||
        !json_boolean_equals(root, "cleanup_exact", true) ||
        !json_boolean_equals(root, "restoration_exact", true) ||
        !json_boolean_equals(root, "candidate_body_consumed", false) ||
        !json_boolean_equals(
            root, "candidate_semantic_category_assigned", false) ||
        !json_string_equals(
            root, "retired_generation_a_tail_sink", "routing_only") ||
        !json_integer(
            root, "retired_generation_a_server_tail_packet_count",
            StockRuntimeCaptureHardCaps::maximum_datagrams) ||
        !json_boolean_equals(
            root, "generation_b_sequenced_after_fresh_accept", true) ||
        generations == nullptr || generations->array.size() != 2U) {
        return false;
    }
    std::array<StockRuntimeConnectionGenerationObservation, 2U> parsed;
    if (!read_final_generation(generations->array[0U], 0U, parsed[0U]) ||
        !read_final_generation(generations->array[1U], 1U, parsed[1U])) {
        return false;
    }
    StockRuntimeReconnectLifecycleInput input;
    input.generations = parsed;
    input.guard_continuous_between_generations = true;
    input.server_continuous_between_generations = true;
    input.relay_continuous_between_generations = true;
    input.cleanup_exact = true;
    input.restoration_exact = true;
    input.transactional_publication_ready = true;
    return static_cast<bool>(validate_stock_runtime_reconnect_lifecycle(input));
}

using StrictDocumentValidator = bool (*)(const StrictJsonValue&);

[[nodiscard]] std::optional<StockRuntimeCorpusDocument>
read_strict_reconnect_document(
    const fs::path& path,
    const std::string_view expected_schema,
    const std::size_t maximum_bytes,
    const StrictDocumentValidator validator,
    StockRuntimeCaptureCorpusError& error)
{
    auto read = read_bounded_regular_file(path, maximum_bytes);
    if (!read.bytes) {
        error = std::move(*read.error);
        return std::nullopt;
    }
    const auto text = bytes_as_string(*read.bytes);
    auto parsed = StrictJsonReader{text}.read();
    if (!parsed || parsed->kind != StrictJsonValue::Kind::object ||
        !validator(*parsed)) {
        error = StockRuntimeCaptureCorpusError{
            StockRuntimeCaptureCorpusErrorCode::invalid_json, 0U,
            "reconnect document violates its exact bounded schema",
            std::nullopt};
        return std::nullopt;
    }
    const auto digest = hash::sha256(*read.bytes);
    if (!digest) {
        error = StockRuntimeCaptureCorpusError{
            StockRuntimeCaptureCorpusErrorCode::structural_hash_failed, 0U,
            "reconnect document structural hash could not be computed",
            std::nullopt};
        return std::nullopt;
    }
    return StockRuntimeCorpusDocument{
        std::string{expected_schema}, hash::sha256_hex(*digest)};
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
    std::optional<StockRuntimeCorpusDocument> reconnect_transport_observation,
    std::optional<StockRuntimeCorpusDocument> reconnect_orchestration_attestation,
    std::optional<StockRuntimeCorpusDocument> reconnect_observation,
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
      reconnect_transport_observation_{
          std::move(reconnect_transport_observation)},
      reconnect_orchestration_attestation_{
          std::move(reconnect_orchestration_attestation)},
      reconnect_observation_{std::move(reconnect_observation)},
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
const std::optional<StockRuntimeCorpusDocument>&
StockRuntimeCaptureCorpusState::reconnect_transport_observation() const noexcept
{
    return reconnect_transport_observation_;
}
const std::optional<StockRuntimeCorpusDocument>&
StockRuntimeCaptureCorpusState::reconnect_orchestration_attestation() const noexcept
{
    return reconnect_orchestration_attestation_;
}
const std::optional<StockRuntimeCorpusDocument>&
StockRuntimeCaptureCorpusState::reconnect_observation() const noexcept
{
    return reconnect_observation_;
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
        const bool reconnect_document =
            name == "reconnect-transport-observation.staged.json" ||
            name == "reconnect-orchestration.staged.json" ||
            name == "reconnect-observation.json";
        if (name == "research-run-metadata.json") {
            if (policy == StockRuntimeCaptureCorpusLoadPolicy::prepublication) {
                return failure(StockRuntimeCaptureCorpusErrorCode::unexpected_manifest,
                               "prepublication corpus already has a final run manifest");
            }
        } else if (!reconnect_document &&
                   !required_root_entries.contains(name)) {
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
    const bool reconnect_capture = capture_parse.metadata->scenario ==
        StockRuntimeCaptureScenario::reconnect;
    const bool has_reconnect_transport = seen_root_entries.contains(
        "reconnect-transport-observation.staged.json");
    const bool has_reconnect_orchestration = seen_root_entries.contains(
        "reconnect-orchestration.staged.json");
    const bool has_reconnect_final = seen_root_entries.contains(
        "reconnect-observation.json");
    if (!reconnect_capture &&
        (has_reconnect_transport || has_reconnect_orchestration ||
         has_reconnect_final)) {
        return failure(
            StockRuntimeCaptureCorpusErrorCode::unexpected_manifest,
            "non-reconnect corpus contains a reconnect-only document");
    }
    if (reconnect_capture &&
        has_reconnect_transport != has_reconnect_orchestration) {
        return failure(
            StockRuntimeCaptureCorpusErrorCode::missing_manifest,
            "reconnect staged transport and orchestration documents are atomic");
    }
    if (prepublication && has_reconnect_final) {
        return failure(
            StockRuntimeCaptureCorpusErrorCode::unexpected_manifest,
            "prepublication reconnect corpus already has a final observation");
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

    std::optional<StockRuntimeCorpusDocument> reconnect_transport;
    std::optional<StockRuntimeCorpusDocument> reconnect_orchestration;
    std::optional<StockRuntimeCorpusDocument> reconnect_final;
    if (reconnect_capture) {
        if ((prepublication || manifest_accepted) &&
            (!has_reconnect_transport || !has_reconnect_orchestration)) {
            return failure(
                StockRuntimeCaptureCorpusErrorCode::missing_manifest,
                "publication-ready reconnect corpus lacks staged lifecycle proof");
        }
        if (manifest_accepted != has_reconnect_final) {
            return failure(
                manifest_accepted
                    ? StockRuntimeCaptureCorpusErrorCode::missing_manifest
                    : StockRuntimeCaptureCorpusErrorCode::unexpected_manifest,
                "final reconnect observation exists only for an accepted run");
        }
        if (has_reconnect_transport) {
            reconnect_transport = read_strict_reconnect_document(
                absolute / "reconnect-transport-observation.staged.json",
                kStockRuntimeReconnectTransportObservationSchema,
                limits_.maximum_manifest_bytes,
                valid_reconnect_transport_document, document_error);
            if (!reconnect_transport) {
                return {std::nullopt, std::move(document_error)};
            }
            reconnect_orchestration = read_strict_reconnect_document(
                absolute / "reconnect-orchestration.staged.json",
                kStockRuntimeReconnectOrchestrationAttestationSchema,
                limits_.maximum_manifest_bytes,
                valid_reconnect_orchestration_document, document_error);
            if (!reconnect_orchestration) {
                return {std::nullopt, std::move(document_error)};
            }
        }
        if (has_reconnect_final) {
            reconnect_final = read_strict_reconnect_document(
                absolute / "reconnect-observation.json",
                kStockRuntimeReconnectObservationSchema,
                limits_.maximum_manifest_bytes,
                valid_reconnect_final_document, document_error);
            if (!reconnect_final) {
                return {std::nullopt, std::move(document_error)};
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
    if (reconnect_transport && reconnect_orchestration) {
        canonical.append("|reconnect-transport=")
            .append(reconnect_transport->structural_sha256);
        canonical.append("|reconnect-orchestration=")
            .append(reconnect_orchestration->structural_sha256);
    }
    // The final reconnect document is created from this prepublication replay
    // and therefore cannot feed back into the transport identity without a
    // hash cycle. Its own exact byte digest remains exposed by
    // reconnect_observation(); the staged transport/orchestration inputs above
    // are the reconnect material bound into the stable corpus hash.
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
            std::move(reconnect_transport),
            std::move(reconnect_orchestration),
            std::move(reconnect_final),
            std::move(accepted_manifest_claims),
            std::move(structural_hex)},
        std::nullopt,
    };
}

} // namespace hlclient::goldsrc
