#include <hlclient/platform/windows/stock_external_target_artifact.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

namespace hlclient::platform::windows {
namespace {

constexpr std::size_t kMaximumJsonDepth = 16U;
constexpr std::size_t kMaximumJsonNodes = 8'192U;
constexpr std::uint64_t kMaximumApprovalLifetimeSeconds =
    7U * 24U * 60U * 60U;

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(const HANDLE value) noexcept : value_{value} {}
    ~UniqueHandle()
    {
        if (valid()) static_cast<void>(::CloseHandle(value_));
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept
        : value_{std::exchange(other.value_, INVALID_HANDLE_VALUE)}
    {
    }
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other) {
            if (valid()) static_cast<void>(::CloseHandle(value_));
            value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    [[nodiscard]] bool valid() const noexcept
    {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

class UniqueAlgorithm final {
public:
    UniqueAlgorithm() = default;
    ~UniqueAlgorithm()
    {
        if (value_ != nullptr) {
            static_cast<void>(::BCryptCloseAlgorithmProvider(value_, 0U));
        }
    }
    UniqueAlgorithm(const UniqueAlgorithm&) = delete;
    UniqueAlgorithm& operator=(const UniqueAlgorithm&) = delete;
    [[nodiscard]] BCRYPT_ALG_HANDLE* put() noexcept { return &value_; }
    [[nodiscard]] BCRYPT_ALG_HANDLE get() const noexcept { return value_; }

private:
    BCRYPT_ALG_HANDLE value_{nullptr};
};

class UniqueHash final {
public:
    UniqueHash() = default;
    ~UniqueHash()
    {
        if (value_ != nullptr) {
            static_cast<void>(::BCryptDestroyHash(value_));
        }
    }
    UniqueHash(const UniqueHash&) = delete;
    UniqueHash& operator=(const UniqueHash&) = delete;
    [[nodiscard]] BCRYPT_HASH_HANDLE* put() noexcept { return &value_; }
    [[nodiscard]] BCRYPT_HASH_HANDLE get() const noexcept { return value_; }

private:
    BCRYPT_HASH_HANDLE value_{nullptr};
};

enum class JsonKind {
    string,
    unsigned_integer,
    boolean,
    object,
    array,
};

struct JsonValue final {
    JsonKind kind{JsonKind::string};
    std::string string_value;
    std::uint64_t unsigned_value{0U};
    bool boolean_value{false};
    std::vector<std::pair<std::string, JsonValue>> object_value;
    std::vector<JsonValue> array_value;
};

[[nodiscard]] bool is_ascii_hex_lower(const std::string_view value,
                                      const std::size_t exact_size) noexcept
{
    return value.size() == exact_size &&
           std::ranges::all_of(value, [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

[[nodiscard]] int hex_value(const char character) noexcept
{
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

[[nodiscard]] bool utf8_code_point(const std::string_view text,
                                   const std::size_t offset,
                                   std::size_t& width,
                                   std::uint32_t& point) noexcept
{
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80U) {
        width = 1U;
        point = first;
        return true;
    }
    std::size_t expected = 0U;
    std::uint32_t value = 0U;
    std::uint32_t minimum = 0U;
    if ((first & 0xe0U) == 0xc0U) {
        expected = 2U;
        value = first & 0x1fU;
        minimum = 0x80U;
    } else if ((first & 0xf0U) == 0xe0U) {
        expected = 3U;
        value = first & 0x0fU;
        minimum = 0x800U;
    } else if ((first & 0xf8U) == 0xf0U) {
        expected = 4U;
        value = first & 0x07U;
        minimum = 0x10000U;
    } else {
        return false;
    }
    if (offset + expected > text.size()) return false;
    for (std::size_t index = 1U; index < expected; ++index) {
        const auto next = static_cast<unsigned char>(text[offset + index]);
        if ((next & 0xc0U) != 0x80U) return false;
        value = (value << 6U) | (next & 0x3fU);
    }
    if (value < minimum || value > 0x10ffffU ||
        (value >= 0xd800U && value <= 0xdfffU)) {
        return false;
    }
    width = expected;
    point = value;
    return true;
}

[[nodiscard]] bool valid_utf8(const std::string_view text) noexcept
{
    std::size_t offset = 0U;
    while (offset < text.size()) {
        std::size_t width = 0U;
        std::uint32_t point = 0U;
        if (!utf8_code_point(text, offset, width, point)) return false;
        static_cast<void>(point);
        offset += width;
    }
    return true;
}

[[nodiscard]] bool append_utf8(std::string& output,
                               const std::uint32_t point)
{
    if (point <= 0x7fU) {
        output.push_back(static_cast<char>(point));
    } else if (point <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (point & 0x3fU)));
    } else if (point <= 0xffffU &&
               !(point >= 0xd800U && point <= 0xdfffU)) {
        output.push_back(static_cast<char>(0xe0U | (point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (point & 0x3fU)));
    } else if (point <= 0x10ffffU) {
        output.push_back(static_cast<char>(0xf0U | (point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((point >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (point & 0x3fU)));
    } else {
        return false;
    }
    return true;
}

class JsonParser final {
public:
    explicit JsonParser(const std::string_view input) noexcept : input_{input} {}

    [[nodiscard]] std::optional<JsonValue> parse() noexcept
    {
        if (input_.size() > kMaximumStockExternalArtifactBytes) {
            code_ = StockExternalArtifactErrorCode::artifact_too_large;
            return std::nullopt;
        }
        skip_whitespace();
        auto result = parse_value(0U);
        if (!result) return std::nullopt;
        skip_whitespace();
        if (position_ != input_.size()) {
            code_ = StockExternalArtifactErrorCode::malformed_json;
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] StockExternalArtifactErrorCode code() const noexcept
    {
        return code_;
    }

private:
    void skip_whitespace() noexcept
    {
        while (position_ < input_.size()) {
            const char current = input_[position_];
            if (current != ' ' && current != '\t' && current != '\r' &&
                current != '\n') {
                break;
            }
            ++position_;
        }
    }

    [[nodiscard]] std::optional<JsonValue> parse_value(
        const std::size_t depth) noexcept
    {
        if (depth > kMaximumJsonDepth || ++nodes_ > kMaximumJsonNodes ||
            position_ >= input_.size()) {
            code_ = StockExternalArtifactErrorCode::malformed_json;
            return std::nullopt;
        }
        switch (input_[position_]) {
        case '"': {
            JsonValue value{};
            value.kind = JsonKind::string;
            if (!parse_string(value.string_value)) return std::nullopt;
            return value;
        }
        case '{':
            return parse_object(depth + 1U);
        case '[':
            return parse_array(depth + 1U);
        case 't': {
            if (!consume("true")) return syntax_failure();
            JsonValue value{};
            value.kind = JsonKind::boolean;
            value.boolean_value = true;
            return value;
        }
        case 'f': {
            if (!consume("false")) return syntax_failure();
            JsonValue value{};
            value.kind = JsonKind::boolean;
            value.boolean_value = false;
            return value;
        }
        default:
            if (input_[position_] >= '0' && input_[position_] <= '9') {
                return parse_unsigned();
            }
            return syntax_failure();
        }
    }

    [[nodiscard]] std::optional<JsonValue> syntax_failure() noexcept
    {
        code_ = StockExternalArtifactErrorCode::malformed_json;
        return std::nullopt;
    }

    [[nodiscard]] bool consume(const std::string_view token) noexcept
    {
        if (!input_.substr(position_).starts_with(token)) return false;
        position_ += token.size();
        return true;
    }

    [[nodiscard]] std::optional<JsonValue> parse_unsigned() noexcept
    {
        const std::size_t begin = position_;
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && input_[position_] >= '0' &&
                input_[position_] <= '9') {
                return syntax_failure();
            }
        } else {
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') {
                ++position_;
            }
        }
        std::uint64_t parsed = 0U;
        const auto conversion = std::from_chars(
            input_.data() + begin, input_.data() + position_, parsed);
        if (conversion.ec != std::errc{} ||
            conversion.ptr != input_.data() + position_) {
            return syntax_failure();
        }
        JsonValue value{};
        value.kind = JsonKind::unsigned_integer;
        value.unsigned_value = parsed;
        return value;
    }

    [[nodiscard]] bool parse_string(std::string& output) noexcept
    {
        if (position_ >= input_.size() || input_[position_] != '"') {
            code_ = StockExternalArtifactErrorCode::malformed_json;
            return false;
        }
        ++position_;
        while (position_ < input_.size()) {
            const auto byte = static_cast<unsigned char>(input_[position_]);
            if (byte == '"') {
                ++position_;
                return true;
            }
            if (byte < 0x20U) {
                code_ = StockExternalArtifactErrorCode::malformed_json;
                return false;
            }
            if (byte == '\\') {
                ++position_;
                if (position_ >= input_.size()) {
                    code_ = StockExternalArtifactErrorCode::malformed_json;
                    return false;
                }
                const char escaped = input_[position_++];
                switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    std::uint32_t point = 0U;
                    if (!parse_hex_quad(point)) return false;
                    if (point >= 0xd800U && point <= 0xdbffU) {
                        if (position_ + 2U > input_.size() ||
                            input_[position_] != '\\' ||
                            input_[position_ + 1U] != 'u') {
                            code_ = StockExternalArtifactErrorCode::malformed_json;
                            return false;
                        }
                        position_ += 2U;
                        std::uint32_t low = 0U;
                        if (!parse_hex_quad(low) || low < 0xdc00U ||
                            low > 0xdfffU) {
                            code_ = StockExternalArtifactErrorCode::malformed_json;
                            return false;
                        }
                        point = 0x10000U + ((point - 0xd800U) << 10U) +
                                (low - 0xdc00U);
                    } else if (point >= 0xdc00U && point <= 0xdfffU) {
                        code_ = StockExternalArtifactErrorCode::malformed_json;
                        return false;
                    }
                    if (!append_utf8(output, point)) {
                        code_ = StockExternalArtifactErrorCode::invalid_utf8;
                        return false;
                    }
                    break;
                }
                default:
                    code_ = StockExternalArtifactErrorCode::malformed_json;
                    return false;
                }
                continue;
            }
            if (byte < 0x80U) {
                output.push_back(static_cast<char>(byte));
                ++position_;
                continue;
            }
            std::size_t width = 0U;
            std::uint32_t point = 0U;
            if (!utf8_code_point(input_, position_, width, point)) {
                code_ = StockExternalArtifactErrorCode::invalid_utf8;
                return false;
            }
            static_cast<void>(point);
            output.append(input_.substr(position_, width));
            position_ += width;
        }
        code_ = StockExternalArtifactErrorCode::malformed_json;
        return false;
    }

    [[nodiscard]] bool parse_hex_quad(std::uint32_t& value) noexcept
    {
        if (position_ + 4U > input_.size()) {
            code_ = StockExternalArtifactErrorCode::malformed_json;
            return false;
        }
        value = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            const int digit = hex_value(input_[position_ + index]);
            if (digit < 0) {
                code_ = StockExternalArtifactErrorCode::malformed_json;
                return false;
            }
            value = (value << 4U) | static_cast<std::uint32_t>(digit);
        }
        position_ += 4U;
        return true;
    }

    [[nodiscard]] std::optional<JsonValue> parse_object(
        const std::size_t depth) noexcept
    {
        ++position_;
        JsonValue result{};
        result.kind = JsonKind::object;
        skip_whitespace();
        if (position_ < input_.size() && input_[position_] == '}') {
            ++position_;
            return result;
        }
        for (;;) {
            skip_whitespace();
            std::string name;
            if (!parse_string(name)) return std::nullopt;
            if (std::ranges::any_of(result.object_value, [&](const auto& item) {
                    return item.first == name;
                })) {
                code_ = StockExternalArtifactErrorCode::duplicate_property;
                return std::nullopt;
            }
            skip_whitespace();
            if (position_ >= input_.size() || input_[position_] != ':') {
                return syntax_failure();
            }
            ++position_;
            skip_whitespace();
            auto value = parse_value(depth);
            if (!value) return std::nullopt;
            result.object_value.emplace_back(std::move(name), std::move(*value));
            skip_whitespace();
            if (position_ >= input_.size()) return syntax_failure();
            if (input_[position_] == '}') {
                ++position_;
                return result;
            }
            if (input_[position_] != ',') return syntax_failure();
            ++position_;
        }
    }

    [[nodiscard]] std::optional<JsonValue> parse_array(
        const std::size_t depth) noexcept
    {
        ++position_;
        JsonValue result{};
        result.kind = JsonKind::array;
        skip_whitespace();
        if (position_ < input_.size() && input_[position_] == ']') {
            ++position_;
            return result;
        }
        for (;;) {
            skip_whitespace();
            auto value = parse_value(depth);
            if (!value) return std::nullopt;
            result.array_value.push_back(std::move(*value));
            skip_whitespace();
            if (position_ >= input_.size()) return syntax_failure();
            if (input_[position_] == ']') {
                ++position_;
                return result;
            }
            if (input_[position_] != ',') return syntax_failure();
            ++position_;
        }
    }

    std::string_view input_;
    std::size_t position_{0U};
    std::size_t nodes_{0U};
    StockExternalArtifactErrorCode code_{
        StockExternalArtifactErrorCode::malformed_json};
};

[[nodiscard]] const JsonValue* member(const JsonValue& object,
                                      const std::string_view name) noexcept
{
    const auto found = std::ranges::find_if(
        object.object_value,
        [&](const auto& item) { return item.first == name; });
    return found == object.object_value.end() ? nullptr : &found->second;
}

[[nodiscard]] bool exact_object(
    const JsonValue& value,
    const std::initializer_list<std::string_view> fields,
    StockExternalArtifactErrorCode& code) noexcept
{
    if (value.kind != JsonKind::object) {
        code = StockExternalArtifactErrorCode::invalid_property_type;
        return false;
    }
    for (const auto& item : value.object_value) {
        if (std::ranges::find(fields, std::string_view{item.first}) ==
            fields.end()) {
            code = StockExternalArtifactErrorCode::unknown_property;
            return false;
        }
    }
    for (const auto field : fields) {
        if (member(value, field) == nullptr) {
            code = StockExternalArtifactErrorCode::missing_property;
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool string_member(const JsonValue& object,
                                 const std::string_view name,
                                 std::string& output,
                                 StockExternalArtifactErrorCode& code) noexcept
{
    const auto* value = member(object, name);
    if (value == nullptr) {
        code = StockExternalArtifactErrorCode::missing_property;
        return false;
    }
    if (value->kind != JsonKind::string) {
        code = StockExternalArtifactErrorCode::invalid_property_type;
        return false;
    }
    output = value->string_value;
    return true;
}

[[nodiscard]] bool unsigned_member(
    const JsonValue& object,
    const std::string_view name,
    std::uint64_t& output,
    StockExternalArtifactErrorCode& code) noexcept
{
    const auto* value = member(object, name);
    if (value == nullptr) {
        code = StockExternalArtifactErrorCode::missing_property;
        return false;
    }
    if (value->kind != JsonKind::unsigned_integer) {
        code = StockExternalArtifactErrorCode::invalid_property_type;
        return false;
    }
    output = value->unsigned_value;
    return true;
}

[[nodiscard]] bool bool_member(const JsonValue& object,
                               const std::string_view name,
                               bool& output,
                               StockExternalArtifactErrorCode& code) noexcept
{
    const auto* value = member(object, name);
    if (value == nullptr) {
        code = StockExternalArtifactErrorCode::missing_property;
        return false;
    }
    if (value->kind != JsonKind::boolean) {
        code = StockExternalArtifactErrorCode::invalid_property_type;
        return false;
    }
    output = value->boolean_value;
    return true;
}

[[nodiscard]] bool parse_file_id(
    const std::string_view value,
    std::array<std::byte, 16U>& output) noexcept
{
    if (!is_ascii_hex_lower(value, output.size() * 2U)) return false;
    for (std::size_t index = 0U; index < output.size(); ++index) {
        const int high = hex_value(value[index * 2U]);
        const int low = hex_value(value[index * 2U + 1U]);
        output[index] = static_cast<std::byte>((high << 4) | low);
    }
    return true;
}

[[nodiscard]] std::string file_id_hex(
    const std::array<std::byte, 16U>& value)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(value.size() * 2U, '\0');
    for (std::size_t index = 0U; index < value.size(); ++index) {
        const auto byte = std::to_integer<unsigned int>(value[index]);
        result[index * 2U] = digits[byte >> 4U];
        result[index * 2U + 1U] = digits[byte & 0x0fU];
    }
    return result;
}

[[nodiscard]] bool valid_profile(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > 128U || !valid_utf8(value)) return false;
    return std::ranges::all_of(value, [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return (byte >= 'a' && byte <= 'z') ||
               (byte >= 'A' && byte <= 'Z') ||
               (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' ||
               byte == '-';
    });
}

[[nodiscard]] bool valid_relative_path(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > 32'767U || !valid_utf8(value) ||
        value.front() == '/' || value.back() == '/' ||
        value.find('\\') != std::string_view::npos ||
        value.find(':') != std::string_view::npos) {
        return false;
    }
    std::size_t begin = 0U;
    while (begin < value.size()) {
        const auto end = value.find('/', begin);
        const auto component = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin
                                                 : end - begin);
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return true;
}

[[nodiscard]] bool valid_private_path(const std::string_view value) noexcept
{
    return !value.empty() && value.size() <= 32'767U && valid_utf8(value) &&
           std::ranges::none_of(value, [](const char character) {
               return static_cast<unsigned char>(character) < 0x20U;
           });
}

[[nodiscard]] bool valid_inventory(
    const StockExternalArtifactInventory& value) noexcept
{
    return is_ascii_hex_lower(value.inventory_sha256, 64U) &&
           value.executable_count <= value.entry_count &&
           value.script_or_command_count <= value.entry_count &&
           value.mutable_state_count <= value.entry_count &&
           value.nested_link_count <= value.entry_count;
}

[[nodiscard]] bool valid_identity(
    const StockExternalArtifactFileIdentity& value) noexcept
{
    return valid_private_path(value.final_path) &&
           is_ascii_hex_lower(value.identity_sha256, 64U);
}

[[nodiscard]] bool valid_nonce(const std::string_view value) noexcept
{
    return is_ascii_hex_lower(value, 32U);
}

[[nodiscard]] bool eligible_classification(
    const StockExternalArtifactClassification value) noexcept
{
    return value == StockExternalArtifactClassification::
                        eligible_non_executable_asset_tree;
}

[[nodiscard]] bool unique_summary_targets(
    const std::vector<StockExternalReviewTargetBindingArtifact>& targets) noexcept
{
    for (std::size_t left = 0U; left < targets.size(); ++left) {
        for (std::size_t right = left + 1U; right < targets.size(); ++right) {
            if (targets[left].ordinal == targets[right].ordinal ||
                targets[left].link_identity_sha256 ==
                    targets[right].link_identity_sha256 ||
                targets[left].target_identity_sha256 ==
                    targets[right].target_identity_sha256) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool unique_approved_targets(
    const std::vector<StockExternalApprovedTargetBindingArtifact>& targets) noexcept
{
    for (std::size_t left = 0U; left < targets.size(); ++left) {
        for (std::size_t right = left + 1U; right < targets.size(); ++right) {
            if (targets[left].ordinal == targets[right].ordinal ||
                targets[left].link_identity_sha256 ==
                    targets[right].link_identity_sha256 ||
                targets[left].target_identity_sha256 ==
                    targets[right].target_identity_sha256) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool valid_request(
    const StockExternalReviewRequestArtifact& value) noexcept
{
    return valid_identity(value.source_root_identity) &&
           value.source_root_identity.directory &&
           valid_inventory(value.source_inventory) &&
           is_ascii_hex_lower(value.review_root_fingerprint, 64U) &&
           valid_nonce(value.review_nonce) &&
           value.review_timestamp_unix_seconds != 0U &&
           valid_profile(value.implementation_profile) &&
           value.target_count <= kMaximumStockExternalArtifactTargets;
}

[[nodiscard]] bool valid_private_target(
    const StockExternalPrivateTargetArtifact& value) noexcept
{
    return value.ordinal >= 1U &&
           value.ordinal <= kMaximumStockExternalArtifactTargets &&
           valid_nonce(value.review_nonce) &&
           is_ascii_hex_lower(value.source_root_fingerprint, 64U) &&
           valid_relative_path(value.source_link_relative_path) &&
           valid_identity(value.source_link_identity) &&
           value.source_link_identity.reparse_tag != 0U &&
           valid_private_path(value.target_canonical_path) &&
           valid_identity(value.target_identity) &&
           valid_inventory(value.target_inventory) &&
           value.eligible == eligible_classification(value.classification);
}

[[nodiscard]] bool valid_summary_target(
    const StockExternalReviewTargetBindingArtifact& value) noexcept
{
    return value.ordinal >= 1U &&
           value.ordinal <= kMaximumStockExternalArtifactTargets &&
           is_ascii_hex_lower(value.private_record_sha256, 64U) &&
           is_ascii_hex_lower(value.link_identity_sha256, 64U) &&
           is_ascii_hex_lower(value.target_identity_sha256, 64U) &&
           is_ascii_hex_lower(value.target_inventory_sha256, 64U) &&
           value.eligible == eligible_classification(value.classification);
}

[[nodiscard]] bool valid_summary(
    const StockExternalReviewSummaryArtifact& value) noexcept
{
    if (!is_ascii_hex_lower(value.review_root_fingerprint, 64U) ||
        !is_ascii_hex_lower(value.source_root_fingerprint, 64U) ||
        !valid_inventory(value.source_inventory) ||
        !valid_nonce(value.review_nonce) ||
        value.review_timestamp_unix_seconds == 0U ||
        !valid_profile(value.implementation_profile) ||
        value.targets.size() > kMaximumStockExternalArtifactTargets ||
        !std::ranges::all_of(value.targets, valid_summary_target) ||
        !unique_summary_targets(value.targets)) {
        return false;
    }
    const auto count = static_cast<std::uint64_t>(value.targets.size());
    return value.eligible_count <= count && value.ineligible_count <= count &&
           value.eligible_count + value.ineligible_count == count &&
           value.unknown_count <= value.ineligible_count &&
           value.executable_target_count <= count &&
           value.mutable_state_target_count <= count &&
           value.all_targets_eligible ==
               (!value.targets.empty() && value.eligible_count == count);
}

[[nodiscard]] bool valid_approved_target(
    const StockExternalApprovedTargetBindingArtifact& value) noexcept
{
    return value.ordinal >= 1U &&
           value.ordinal <= kMaximumStockExternalArtifactTargets &&
           is_ascii_hex_lower(value.link_identity_sha256, 64U) &&
           is_ascii_hex_lower(value.target_identity_sha256, 64U) &&
           is_ascii_hex_lower(value.target_inventory_sha256, 64U) &&
           eligible_classification(value.classification);
}

[[nodiscard]] bool valid_approval(
    const StockExternalApprovalArtifact& value) noexcept
{
    if (value.review_schema != kStockExternalReviewSummarySchemaV1 ||
        value.review_version != 1U ||
        !is_ascii_hex_lower(value.review_root_fingerprint, 64U) ||
        !is_ascii_hex_lower(value.review_digest_sha256, 64U) ||
        !is_ascii_hex_lower(value.source_root_fingerprint, 64U) ||
        !valid_inventory(value.source_inventory) ||
        !valid_nonce(value.review_nonce) ||
        !valid_nonce(value.approval_nonce) ||
        value.review_nonce == value.approval_nonce ||
        value.approval_timestamp_unix_seconds == 0U ||
        value.expiration_unix_seconds <=
            value.approval_timestamp_unix_seconds ||
        value.expiration_unix_seconds -
                value.approval_timestamp_unix_seconds >
            kMaximumApprovalLifetimeSeconds ||
        value.confirmation_profile !=
            kStockExternalApprovalConfirmationProfileV1 ||
        !valid_profile(value.implementation_profile) ||
        value.approved_targets.empty() ||
        value.approved_targets.size() > kMaximumStockExternalArtifactTargets ||
        value.approval_count != value.approved_targets.size() ||
        !std::ranges::all_of(value.approved_targets, valid_approved_target) ||
        !unique_approved_targets(value.approved_targets)) {
        return false;
    }
    return true;
}

void append_json_string(std::string& output,
                        const std::string_view value,
                        bool& valid)
{
    if (!valid || !valid_utf8(value)) {
        valid = false;
        return;
    }
    static constexpr char digits[] = "0123456789abcdef";
    output.push_back('"');
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
        case '"': output.append("\\\""); break;
        case '\\': output.append("\\\\"); break;
        case '\b': output.append("\\b"); break;
        case '\f': output.append("\\f"); break;
        case '\n': output.append("\\n"); break;
        case '\r': output.append("\\r"); break;
        case '\t': output.append("\\t"); break;
        default:
            if (byte < 0x20U) {
                output.append("\\u00");
                output.push_back(digits[byte >> 4U]);
                output.push_back(digits[byte & 0x0fU]);
            } else {
                output.push_back(character);
            }
            break;
        }
    }
    output.push_back('"');
}

void append_key(std::string& output, const std::string_view key, bool& valid)
{
    append_json_string(output, key, valid);
    output.push_back(':');
}

void append_unsigned(std::string& output, const std::uint64_t value)
{
    std::array<char, 32U> buffer{};
    const auto converted = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value);
    output.append(buffer.data(), converted.ptr);
}

void append_bool(std::string& output, const bool value)
{
    output.append(value ? "true" : "false");
}

void append_string_property(std::string& output,
                            const std::string_view name,
                            const std::string_view value,
                            bool& valid)
{
    append_key(output, name, valid);
    append_json_string(output, value, valid);
}

void append_unsigned_property(std::string& output,
                              const std::string_view name,
                              const std::uint64_t value,
                              bool& valid)
{
    append_key(output, name, valid);
    append_unsigned(output, value);
}

void append_bool_property(std::string& output,
                          const std::string_view name,
                          const bool value,
                          bool& valid)
{
    append_key(output, name, valid);
    append_bool(output, value);
}

void append_identity(std::string& output,
                     const StockExternalArtifactFileIdentity& value,
                     bool& valid)
{
    output.push_back('{');
    append_unsigned_property(
        output, "volume-serial-number", value.volume_serial_number, valid);
    output.push_back(',');
    append_string_property(output, "file-id", file_id_hex(value.file_id), valid);
    output.push_back(',');
    append_string_property(output, "final-path", value.final_path, valid);
    output.push_back(',');
    append_string_property(
        output, "identity-sha256", value.identity_sha256, valid);
    output.push_back(',');
    append_unsigned_property(output, "reparse-tag", value.reparse_tag, valid);
    output.push_back(',');
    append_bool_property(output, "directory", value.directory, valid);
    output.push_back('}');
}

void append_inventory(std::string& output,
                      const StockExternalArtifactInventory& value,
                      bool& valid)
{
    output.push_back('{');
    append_unsigned_property(output, "entry-count", value.entry_count, valid);
    output.push_back(',');
    append_unsigned_property(output, "byte-count", value.byte_count, valid);
    output.push_back(',');
    append_string_property(
        output, "inventory-sha256", value.inventory_sha256, valid);
    output.push_back(',');
    append_unsigned_property(
        output, "executable-count", value.executable_count, valid);
    output.push_back(',');
    append_unsigned_property(
        output, "script-or-command-count", value.script_or_command_count,
        valid);
    output.push_back(',');
    append_unsigned_property(
        output, "mutable-state-count", value.mutable_state_count, valid);
    output.push_back(',');
    append_unsigned_property(
        output, "nested-link-count", value.nested_link_count, valid);
    output.push_back('}');
}

[[nodiscard]] bool parse_identity(
    const JsonValue& value,
    StockExternalArtifactFileIdentity& output,
    StockExternalArtifactErrorCode& code) noexcept
{
    if (!exact_object(
            value,
            {"volume-serial-number", "file-id", "final-path",
             "identity-sha256", "reparse-tag", "directory"},
            code)) {
        return false;
    }
    std::uint64_t reparse_tag = 0U;
    std::string file_id;
    if (!unsigned_member(
            value, "volume-serial-number", output.volume_serial_number, code) ||
        !string_member(value, "file-id", file_id, code) ||
        !string_member(value, "final-path", output.final_path, code) ||
        !string_member(
            value, "identity-sha256", output.identity_sha256, code) ||
        !unsigned_member(value, "reparse-tag", reparse_tag, code) ||
        !bool_member(value, "directory", output.directory, code) ||
        reparse_tag > std::numeric_limits<std::uint32_t>::max() ||
        !parse_file_id(file_id, output.file_id)) {
        if (code == StockExternalArtifactErrorCode::none) {
            code = StockExternalArtifactErrorCode::invalid_property_value;
        }
        return false;
    }
    output.reparse_tag = static_cast<std::uint32_t>(reparse_tag);
    if (!valid_identity(output)) {
        code = StockExternalArtifactErrorCode::invalid_property_value;
        return false;
    }
    return true;
}

[[nodiscard]] bool parse_inventory(
    const JsonValue& value,
    StockExternalArtifactInventory& output,
    StockExternalArtifactErrorCode& code) noexcept
{
    if (!exact_object(
            value,
            {"entry-count", "byte-count", "inventory-sha256",
             "executable-count", "script-or-command-count",
             "mutable-state-count", "nested-link-count"},
            code) ||
        !unsigned_member(value, "entry-count", output.entry_count, code) ||
        !unsigned_member(value, "byte-count", output.byte_count, code) ||
        !string_member(
            value, "inventory-sha256", output.inventory_sha256, code) ||
        !unsigned_member(
            value, "executable-count", output.executable_count, code) ||
        !unsigned_member(value, "script-or-command-count",
                         output.script_or_command_count, code) ||
        !unsigned_member(
            value, "mutable-state-count", output.mutable_state_count, code) ||
        !unsigned_member(
            value, "nested-link-count", output.nested_link_count, code)) {
        return false;
    }
    if (!valid_inventory(output)) {
        code = StockExternalArtifactErrorCode::invalid_property_value;
        return false;
    }
    return true;
}

[[nodiscard]] bool parse_classification(
    const JsonValue& object,
    StockExternalArtifactClassification& output,
    StockExternalArtifactErrorCode& code) noexcept
{
    std::string text;
    if (!string_member(object, "classification", text, code)) return false;
    const auto parsed = stock_external_artifact_classification_from_string(text);
    if (!parsed) {
        code = StockExternalArtifactErrorCode::invalid_property_value;
        return false;
    }
    output = *parsed;
    return true;
}

[[nodiscard]] StockExternalArtifactTextResult text_failure(
    const StockExternalArtifactErrorCode code,
    const DWORD native = 0U) noexcept
{
    return {std::nullopt, code, native};
}

template <typename T>
[[nodiscard]] StockExternalArtifactResult<T> value_failure(
    const StockExternalArtifactErrorCode code,
    const DWORD native = 0U) noexcept
{
    return {std::nullopt, code, native};
}

[[nodiscard]] StockExternalArtifactTextResult finish_serialized(
    std::string&& value,
    const bool valid)
{
    if (!valid) {
        return text_failure(StockExternalArtifactErrorCode::invalid_utf8);
    }
    value.push_back('\n');
    if (value.size() > kMaximumStockExternalArtifactBytes) {
        return text_failure(StockExternalArtifactErrorCode::artifact_too_large);
    }
    return {std::move(value), StockExternalArtifactErrorCode::none, 0U};
}

[[nodiscard]] bool parse_root(
    const std::string_view json,
    JsonValue& value,
    StockExternalArtifactErrorCode& code) noexcept
{
    JsonParser parser{json};
    auto parsed = parser.parse();
    if (!parsed) {
        code = parser.code();
        return false;
    }
    value = std::move(*parsed);
    if (value.kind != JsonKind::object) {
        code = StockExternalArtifactErrorCode::invalid_property_type;
        return false;
    }
    code = StockExternalArtifactErrorCode::none;
    return true;
}

[[nodiscard]] bool exact_schema(const JsonValue& object,
                                const std::string_view expected,
                                StockExternalArtifactErrorCode& code) noexcept
{
    std::string schema;
    if (!string_member(object, "schema", schema, code)) return false;
    if (schema != expected) {
        code = StockExternalArtifactErrorCode::invalid_property_value;
        return false;
    }
    return true;
}

[[nodiscard]] bool same_path_ignore_case(const std::wstring_view left,
                                         const std::wstring_view right) noexcept
{
    if (left.size() != right.size()) return false;
    return ::CompareStringOrdinal(
               left.data(), static_cast<int>(left.size()), right.data(),
               static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

[[nodiscard]] std::wstring normalized_dos_path(std::wstring value)
{
    if (value.starts_with(LR"(\\?\UNC\)")) {
        value = L"\\\\" + value.substr(8U);
    } else if (value.starts_with(LR"(\\?\)")) {
        value.erase(0U, 4U);
    }
    while (value.size() > 3U &&
           (value.back() == L'\\' || value.back() == L'/')) {
        value.pop_back();
    }
    return value;
}

[[nodiscard]] bool query_final_path(const HANDLE handle,
                                    std::wstring& output) noexcept
{
    constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    const DWORD required = ::GetFinalPathNameByHandleW(
        handle, nullptr, 0U, flags);
    if (required == 0U || required > 32'768U) return false;
    std::wstring buffer(static_cast<std::size_t>(required) + 1U, L'\0');
    const DWORD written = ::GetFinalPathNameByHandleW(
        handle, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
    if (written == 0U || written >= buffer.size()) return false;
    buffer.resize(written);
    output = normalized_dos_path(std::move(buffer));
    return true;
}

[[nodiscard]] bool exact_opened_path(const HANDLE handle,
                                     const std::filesystem::path& expected) noexcept
{
    std::wstring final_path;
    if (!query_final_path(handle, final_path)) return false;
    try {
        return same_path_ignore_case(
            final_path,
            normalized_dos_path(expected.lexically_normal().native()));
    } catch (...) {
        return false;
    }
}

struct FileSnapshot final {
    std::uint64_t volume{0U};
    std::array<std::byte, 16U> file_id{};
    std::uint64_t size{0U};
    std::uint32_t link_count{0U};
    std::uint32_t attributes{0U};
    std::int64_t creation_time{0};
    std::int64_t last_write_time{0};
    std::int64_t change_time{0};

    friend bool operator==(const FileSnapshot&,
                           const FileSnapshot&) noexcept = default;
};

[[nodiscard]] bool query_file_snapshot(const HANDLE handle,
                                       FileSnapshot& output,
                                       bool& directory) noexcept
{
    FILE_ID_INFO id{};
    FILE_STANDARD_INFO standard{};
    FILE_BASIC_INFO basic{};
    if (!::GetFileInformationByHandleEx(
            handle, FileIdInfo, &id, sizeof(id)) ||
        !::GetFileInformationByHandleEx(
            handle, FileStandardInfo, &standard, sizeof(standard)) ||
        !::GetFileInformationByHandleEx(
            handle, FileBasicInfo, &basic, sizeof(basic)) ||
        standard.EndOfFile.QuadPart < 0 || standard.NumberOfLinks == 0U) {
        return false;
    }
    output.volume = id.VolumeSerialNumber;
    for (std::size_t index = 0U; index < output.file_id.size(); ++index) {
        output.file_id[index] =
            static_cast<std::byte>(id.FileId.Identifier[index]);
    }
    output.size = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart);
    output.link_count = standard.NumberOfLinks;
    output.attributes = basic.FileAttributes;
    output.creation_time = basic.CreationTime.QuadPart;
    output.last_write_time = basic.LastWriteTime.QuadPart;
    output.change_time = basic.ChangeTime.QuadPart;
    directory = standard.Directory != FALSE;
    return true;
}

[[nodiscard]] bool only_default_stream(
    const std::filesystem::path& path) noexcept
{
    WIN32_FIND_STREAM_DATA stream{};
    const HANDLE search = ::FindFirstStreamW(
        path.c_str(), FindStreamInfoStandard, &stream, 0U);
    if (search == INVALID_HANDLE_VALUE) return false;
    bool exact = std::wstring_view{stream.cStreamName} == L"::$DATA";
    std::size_t count = 1U;
    while (::FindNextStreamW(search, &stream)) {
        ++count;
        exact = false;
    }
    const DWORD final_error = ::GetLastError();
    static_cast<void>(::FindClose(search));
    return exact && count == 1U && final_error == ERROR_HANDLE_EOF;
}

[[nodiscard]] bool valid_leaf(const std::wstring_view leaf) noexcept
{
    if (leaf.empty() || leaf == L"." || leaf == L".." ||
        leaf.find_first_of(L"\\/:") != std::wstring_view::npos) {
        return false;
    }
    if (leaf == kStockExternalReviewRequestLeaf ||
        leaf == kStockExternalReviewSummaryLeaf ||
        leaf == kStockExternalApprovalLeaf) {
        return true;
    }
    constexpr std::wstring_view prefix = L"target-";
    constexpr std::wstring_view suffix = L"-private.json";
    if (leaf.size() != prefix.size() + 4U + suffix.size() ||
        !leaf.starts_with(prefix) || !leaf.ends_with(suffix)) {
        return false;
    }
    std::uint64_t ordinal = 0U;
    for (std::size_t index = prefix.size(); index < prefix.size() + 4U;
         ++index) {
        if (leaf[index] < L'0' || leaf[index] > L'9') return false;
        ordinal = ordinal * 10U + static_cast<std::uint64_t>(leaf[index] - L'0');
    }
    return ordinal >= 1U && ordinal <= kMaximumStockExternalArtifactTargets;
}

[[nodiscard]] bool has_reparse_component(
    const std::filesystem::path& path) noexcept
{
    try {
        auto current = path.root_path();
        for (const auto& component : path.relative_path()) {
            current /= component;
            const DWORD attributes = ::GetFileAttributesW(current.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
                return true;
            }
        }
        return false;
    } catch (...) {
        return true;
    }
}

[[nodiscard]] bool local_fixed_volume(
    const std::filesystem::path& path) noexcept
{
    std::array<wchar_t, 32'768U> volume{};
    return ::GetVolumePathNameW(
               path.c_str(), volume.data(), static_cast<DWORD>(volume.size())) &&
           ::GetDriveTypeW(volume.data()) == DRIVE_FIXED;
}

} // namespace

std::string_view to_string(
    const StockExternalArtifactClassification value) noexcept
{
    switch (value) {
    case StockExternalArtifactClassification::eligible_non_executable_asset_tree:
        return "eligible_non_executable_asset_tree";
    case StockExternalArtifactClassification::contains_executable_code:
        return "contains_executable_code";
    case StockExternalArtifactClassification::contains_script_or_command:
        return "contains_script_or_command";
    case StockExternalArtifactClassification::contains_mutable_user_state:
        return "contains_mutable_user_state";
    case StockExternalArtifactClassification::another_application_tree:
        return "another_application_tree";
    case StockExternalArtifactClassification::operating_system_tree:
        return "operating_system_tree";
    case StockExternalArtifactClassification::temporary_or_cache_tree:
        return "temporary_or_cache_tree";
    case StockExternalArtifactClassification::remote_or_device_target:
        return "remote_or_device_target";
    case StockExternalArtifactClassification::nested_external_link:
        return "nested_external_link";
    case StockExternalArtifactClassification::unsupported_reparse_topology:
        return "unsupported_reparse_topology";
    case StockExternalArtifactClassification::content_limit_exceeded:
        return "content_limit_exceeded";
    case StockExternalArtifactClassification::changed_during_review:
        return "changed_during_review";
    case StockExternalArtifactClassification::unknown:
        return "unknown";
    }
    return "unknown";
}

std::optional<StockExternalArtifactClassification>
stock_external_artifact_classification_from_string(
    const std::string_view value) noexcept
{
    for (const auto candidate : {
             StockExternalArtifactClassification::
                 eligible_non_executable_asset_tree,
             StockExternalArtifactClassification::contains_executable_code,
             StockExternalArtifactClassification::contains_script_or_command,
             StockExternalArtifactClassification::contains_mutable_user_state,
             StockExternalArtifactClassification::another_application_tree,
             StockExternalArtifactClassification::operating_system_tree,
             StockExternalArtifactClassification::temporary_or_cache_tree,
             StockExternalArtifactClassification::remote_or_device_target,
             StockExternalArtifactClassification::nested_external_link,
             StockExternalArtifactClassification::unsupported_reparse_topology,
             StockExternalArtifactClassification::content_limit_exceeded,
             StockExternalArtifactClassification::changed_during_review,
             StockExternalArtifactClassification::unknown}) {
        if (value == to_string(candidate)) return candidate;
    }
    return std::nullopt;
}

std::string_view to_string(const StockExternalArtifactErrorCode value) noexcept
{
    switch (value) {
    case StockExternalArtifactErrorCode::none: return "none";
    case StockExternalArtifactErrorCode::invalid_argument: return "invalid_argument";
    case StockExternalArtifactErrorCode::artifact_too_large: return "artifact_too_large";
    case StockExternalArtifactErrorCode::malformed_json: return "malformed_json";
    case StockExternalArtifactErrorCode::duplicate_property: return "duplicate_property";
    case StockExternalArtifactErrorCode::unknown_property: return "unknown_property";
    case StockExternalArtifactErrorCode::missing_property: return "missing_property";
    case StockExternalArtifactErrorCode::invalid_property_type: return "invalid_property_type";
    case StockExternalArtifactErrorCode::invalid_property_value: return "invalid_property_value";
    case StockExternalArtifactErrorCode::invalid_utf8: return "invalid_utf8";
    case StockExternalArtifactErrorCode::invalid_review_directory: return "invalid_review_directory";
    case StockExternalArtifactErrorCode::invalid_leaf_name: return "invalid_leaf_name";
    case StockExternalArtifactErrorCode::review_directory_open_failed: return "review_directory_open_failed";
    case StockExternalArtifactErrorCode::review_directory_identity_invalid: return "review_directory_identity_invalid";
    case StockExternalArtifactErrorCode::artifact_open_failed: return "artifact_open_failed";
    case StockExternalArtifactErrorCode::artifact_not_ordinary_file: return "artifact_not_ordinary_file";
    case StockExternalArtifactErrorCode::artifact_hardlink_rejected: return "artifact_hardlink_rejected";
    case StockExternalArtifactErrorCode::artifact_alternate_data_stream: return "artifact_alternate_data_stream";
    case StockExternalArtifactErrorCode::artifact_identity_query_failed: return "artifact_identity_query_failed";
    case StockExternalArtifactErrorCode::artifact_exact_path_mismatch: return "artifact_exact_path_mismatch";
    case StockExternalArtifactErrorCode::artifact_read_failed: return "artifact_read_failed";
    case StockExternalArtifactErrorCode::artifact_changed: return "artifact_changed";
    case StockExternalArtifactErrorCode::digest_failed: return "digest_failed";
    }
    return "unknown";
}

StockExternalArtifactTextResult serialize_stock_external_review_request(
    const StockExternalReviewRequestArtifact& artifact) noexcept
{
    try {
        if (!valid_request(artifact)) {
            return text_failure(
                StockExternalArtifactErrorCode::invalid_property_value);
        }
        bool valid = true;
        std::string output{"{"};
        append_string_property(
            output, "schema", kStockExternalReviewRequestSchemaV1, valid);
        output.push_back(',');
        append_key(output, "source-root-identity", valid);
        append_identity(output, artifact.source_root_identity, valid);
        output.push_back(',');
        append_key(output, "source-inventory", valid);
        append_inventory(output, artifact.source_inventory, valid);
        output.push_back(',');
        append_string_property(output, "review-root-fingerprint",
                               artifact.review_root_fingerprint, valid);
        output.push_back(',');
        append_string_property(
            output, "review-nonce", artifact.review_nonce, valid);
        output.push_back(',');
        append_unsigned_property(output, "review-timestamp-unix-seconds",
                                 artifact.review_timestamp_unix_seconds, valid);
        output.push_back(',');
        append_string_property(output, "implementation-profile",
                               artifact.implementation_profile, valid);
        output.push_back(',');
        append_unsigned_property(
            output, "target-count", artifact.target_count, valid);
        output.push_back('}');
        return finish_serialized(std::move(output), valid);
    } catch (...) {
        return text_failure(StockExternalArtifactErrorCode::invalid_argument);
    }
}

StockExternalArtifactResult<StockExternalReviewRequestArtifact>
parse_stock_external_review_request(const std::string_view json) noexcept
{
    try {
        JsonValue root{};
        StockExternalArtifactErrorCode code{};
        StockExternalReviewRequestArtifact output{};
        if (!parse_root(json, root, code) ||
            !exact_object(
                root,
                {"schema", "source-root-identity", "source-inventory",
                 "review-root-fingerprint", "review-nonce",
                 "review-timestamp-unix-seconds", "implementation-profile",
                 "target-count"},
                code) ||
            !exact_schema(root, kStockExternalReviewRequestSchemaV1, code)) {
            return value_failure<StockExternalReviewRequestArtifact>(code);
        }
        const auto* identity = member(root, "source-root-identity");
        const auto* inventory = member(root, "source-inventory");
        if (identity == nullptr || inventory == nullptr ||
            !parse_identity(*identity, output.source_root_identity, code) ||
            !parse_inventory(*inventory, output.source_inventory, code) ||
            !string_member(root, "review-root-fingerprint",
                           output.review_root_fingerprint, code) ||
            !string_member(root, "review-nonce", output.review_nonce, code) ||
            !unsigned_member(root, "review-timestamp-unix-seconds",
                             output.review_timestamp_unix_seconds, code) ||
            !string_member(root, "implementation-profile",
                           output.implementation_profile, code) ||
            !unsigned_member(
                root, "target-count", output.target_count, code) ||
            !valid_request(output)) {
            if (code == StockExternalArtifactErrorCode::none) {
                code = StockExternalArtifactErrorCode::invalid_property_value;
            }
            return value_failure<StockExternalReviewRequestArtifact>(code);
        }
        return {std::move(output), StockExternalArtifactErrorCode::none, 0U};
    } catch (...) {
        return value_failure<StockExternalReviewRequestArtifact>(
            StockExternalArtifactErrorCode::invalid_argument);
    }
}

StockExternalArtifactTextResult serialize_stock_external_private_target(
    const StockExternalPrivateTargetArtifact& artifact) noexcept
{
    try {
        if (!valid_private_target(artifact)) {
            return text_failure(
                StockExternalArtifactErrorCode::invalid_property_value);
        }
        bool valid = true;
        std::string output{"{"};
        append_string_property(
            output, "schema", kStockExternalPrivateTargetSchemaV1, valid);
        output.push_back(',');
        append_unsigned_property(output, "ordinal", artifact.ordinal, valid);
        output.push_back(',');
        append_string_property(
            output, "review-nonce", artifact.review_nonce, valid);
        output.push_back(',');
        append_string_property(output, "source-root-fingerprint",
                               artifact.source_root_fingerprint, valid);
        output.push_back(',');
        append_string_property(output, "source-link-relative-path",
                               artifact.source_link_relative_path, valid);
        output.push_back(',');
        append_key(output, "source-link-identity", valid);
        append_identity(output, artifact.source_link_identity, valid);
        output.push_back(',');
        append_string_property(output, "target-canonical-path",
                               artifact.target_canonical_path, valid);
        output.push_back(',');
        append_key(output, "target-identity", valid);
        append_identity(output, artifact.target_identity, valid);
        output.push_back(',');
        append_key(output, "target-inventory", valid);
        append_inventory(output, artifact.target_inventory, valid);
        output.push_back(',');
        append_string_property(
            output, "classification", to_string(artifact.classification),
            valid);
        output.push_back(',');
        append_bool_property(output, "eligible", artifact.eligible, valid);
        output.push_back('}');
        return finish_serialized(std::move(output), valid);
    } catch (...) {
        return text_failure(StockExternalArtifactErrorCode::invalid_argument);
    }
}

StockExternalArtifactResult<StockExternalPrivateTargetArtifact>
parse_stock_external_private_target(const std::string_view json) noexcept
{
    try {
        JsonValue root{};
        StockExternalArtifactErrorCode code{};
        StockExternalPrivateTargetArtifact output{};
        if (!parse_root(json, root, code) ||
            !exact_object(
                root,
                {"schema", "ordinal", "review-nonce",
                 "source-root-fingerprint", "source-link-relative-path",
                 "source-link-identity", "target-canonical-path",
                 "target-identity", "target-inventory", "classification",
                 "eligible"},
                code) ||
            !exact_schema(root, kStockExternalPrivateTargetSchemaV1, code)) {
            return value_failure<StockExternalPrivateTargetArtifact>(code);
        }
        const auto* link_identity = member(root, "source-link-identity");
        const auto* target_identity = member(root, "target-identity");
        const auto* target_inventory = member(root, "target-inventory");
        if (link_identity == nullptr || target_identity == nullptr ||
            target_inventory == nullptr ||
            !unsigned_member(root, "ordinal", output.ordinal, code) ||
            !string_member(root, "review-nonce", output.review_nonce, code) ||
            !string_member(root, "source-root-fingerprint",
                           output.source_root_fingerprint, code) ||
            !string_member(root, "source-link-relative-path",
                           output.source_link_relative_path, code) ||
            !parse_identity(
                *link_identity, output.source_link_identity, code) ||
            !string_member(root, "target-canonical-path",
                           output.target_canonical_path, code) ||
            !parse_identity(
                *target_identity, output.target_identity, code) ||
            !parse_inventory(
                *target_inventory, output.target_inventory, code) ||
            !parse_classification(root, output.classification, code) ||
            !bool_member(root, "eligible", output.eligible, code) ||
            !valid_private_target(output)) {
            if (code == StockExternalArtifactErrorCode::none) {
                code = StockExternalArtifactErrorCode::invalid_property_value;
            }
            return value_failure<StockExternalPrivateTargetArtifact>(code);
        }
        return {std::move(output), StockExternalArtifactErrorCode::none, 0U};
    } catch (...) {
        return value_failure<StockExternalPrivateTargetArtifact>(
            StockExternalArtifactErrorCode::invalid_argument);
    }
}

namespace {

void append_summary_target(
    std::string& output,
    const StockExternalReviewTargetBindingArtifact& target,
    bool& valid)
{
    output.push_back('{');
    append_unsigned_property(output, "ordinal", target.ordinal, valid);
    output.push_back(',');
    append_string_property(output, "private-record-sha256",
                           target.private_record_sha256, valid);
    output.push_back(',');
    append_string_property(output, "link-identity-sha256",
                           target.link_identity_sha256, valid);
    output.push_back(',');
    append_string_property(output, "target-identity-sha256",
                           target.target_identity_sha256, valid);
    output.push_back(',');
    append_string_property(output, "target-inventory-sha256",
                           target.target_inventory_sha256, valid);
    output.push_back(',');
    append_string_property(
        output, "classification", to_string(target.classification), valid);
    output.push_back(',');
    append_bool_property(output, "eligible", target.eligible, valid);
    output.push_back('}');
}

[[nodiscard]] bool parse_summary_target(
    const JsonValue& value,
    StockExternalReviewTargetBindingArtifact& output,
    StockExternalArtifactErrorCode& code) noexcept
{
    if (!exact_object(
            value,
            {"ordinal", "private-record-sha256", "link-identity-sha256",
             "target-identity-sha256", "target-inventory-sha256",
             "classification", "eligible"},
            code) ||
        !unsigned_member(value, "ordinal", output.ordinal, code) ||
        !string_member(value, "private-record-sha256",
                       output.private_record_sha256, code) ||
        !string_member(value, "link-identity-sha256",
                       output.link_identity_sha256, code) ||
        !string_member(value, "target-identity-sha256",
                       output.target_identity_sha256, code) ||
        !string_member(value, "target-inventory-sha256",
                       output.target_inventory_sha256, code) ||
        !parse_classification(value, output.classification, code) ||
        !bool_member(value, "eligible", output.eligible, code)) {
        return false;
    }
    if (!valid_summary_target(output)) {
        code = StockExternalArtifactErrorCode::invalid_property_value;
        return false;
    }
    return true;
}

} // namespace

StockExternalArtifactTextResult serialize_stock_external_review_summary(
    const StockExternalReviewSummaryArtifact& artifact) noexcept
{
    try {
        if (!valid_summary(artifact)) {
            return text_failure(
                StockExternalArtifactErrorCode::invalid_property_value);
        }
        bool valid = true;
        std::string output{"{"};
        append_string_property(
            output, "schema", kStockExternalReviewSummarySchemaV1, valid);
        output.push_back(',');
        append_string_property(output, "review-root-fingerprint",
                               artifact.review_root_fingerprint, valid);
        output.push_back(',');
        append_string_property(output, "source-root-fingerprint",
                               artifact.source_root_fingerprint, valid);
        output.push_back(',');
        append_key(output, "source-inventory", valid);
        append_inventory(output, artifact.source_inventory, valid);
        output.push_back(',');
        append_string_property(
            output, "review-nonce", artifact.review_nonce, valid);
        output.push_back(',');
        append_unsigned_property(output, "review-timestamp-unix-seconds",
                                 artifact.review_timestamp_unix_seconds, valid);
        output.push_back(',');
        append_string_property(output, "implementation-profile",
                               artifact.implementation_profile, valid);
        output.push_back(',');
        append_key(output, "targets", valid);
        output.push_back('[');
        for (std::size_t index = 0U; index < artifact.targets.size(); ++index) {
            if (index != 0U) output.push_back(',');
            append_summary_target(output, artifact.targets[index], valid);
        }
        output.push_back(']');
        output.push_back(',');
        append_unsigned_property(
            output, "eligible-count", artifact.eligible_count, valid);
        output.push_back(',');
        append_unsigned_property(
            output, "ineligible-count", artifact.ineligible_count, valid);
        output.push_back(',');
        append_unsigned_property(
            output, "unknown-count", artifact.unknown_count, valid);
        output.push_back(',');
        append_unsigned_property(output, "executable-target-count",
                                 artifact.executable_target_count, valid);
        output.push_back(',');
        append_unsigned_property(output, "mutable-state-target-count",
                                 artifact.mutable_state_target_count, valid);
        output.push_back(',');
        append_bool_property(output, "all-targets-eligible",
                             artifact.all_targets_eligible, valid);
        output.push_back('}');
        return finish_serialized(std::move(output), valid);
    } catch (...) {
        return text_failure(StockExternalArtifactErrorCode::invalid_argument);
    }
}

StockExternalArtifactResult<StockExternalReviewSummaryArtifact>
parse_stock_external_review_summary(const std::string_view json) noexcept
{
    try {
        JsonValue root{};
        StockExternalArtifactErrorCode code{};
        StockExternalReviewSummaryArtifact output{};
        if (!parse_root(json, root, code) ||
            !exact_object(
                root,
                {"schema", "review-root-fingerprint",
                 "source-root-fingerprint", "source-inventory",
                 "review-nonce", "review-timestamp-unix-seconds",
                 "implementation-profile", "targets", "eligible-count",
                 "ineligible-count", "unknown-count",
                 "executable-target-count", "mutable-state-target-count",
                 "all-targets-eligible"},
                code) ||
            !exact_schema(root, kStockExternalReviewSummarySchemaV1, code)) {
            return value_failure<StockExternalReviewSummaryArtifact>(code);
        }
        const auto* inventory = member(root, "source-inventory");
        const auto* targets = member(root, "targets");
        if (inventory == nullptr || targets == nullptr ||
            targets->kind != JsonKind::array ||
            targets->array_value.size() >
                kMaximumStockExternalArtifactTargets ||
            !string_member(root, "review-root-fingerprint",
                           output.review_root_fingerprint, code) ||
            !string_member(root, "source-root-fingerprint",
                           output.source_root_fingerprint, code) ||
            !parse_inventory(*inventory, output.source_inventory, code) ||
            !string_member(root, "review-nonce", output.review_nonce, code) ||
            !unsigned_member(root, "review-timestamp-unix-seconds",
                             output.review_timestamp_unix_seconds, code) ||
            !string_member(root, "implementation-profile",
                           output.implementation_profile, code)) {
            if (code == StockExternalArtifactErrorCode::none) {
                code = StockExternalArtifactErrorCode::invalid_property_type;
            }
            return value_failure<StockExternalReviewSummaryArtifact>(code);
        }
        output.targets.reserve(targets->array_value.size());
        for (const auto& value : targets->array_value) {
            StockExternalReviewTargetBindingArtifact target{};
            if (!parse_summary_target(value, target, code)) {
                return value_failure<StockExternalReviewSummaryArtifact>(code);
            }
            output.targets.push_back(std::move(target));
        }
        if (!unsigned_member(
                root, "eligible-count", output.eligible_count, code) ||
            !unsigned_member(
                root, "ineligible-count", output.ineligible_count, code) ||
            !unsigned_member(
                root, "unknown-count", output.unknown_count, code) ||
            !unsigned_member(root, "executable-target-count",
                             output.executable_target_count, code) ||
            !unsigned_member(root, "mutable-state-target-count",
                             output.mutable_state_target_count, code) ||
            !bool_member(root, "all-targets-eligible",
                         output.all_targets_eligible, code) ||
            !valid_summary(output)) {
            if (code == StockExternalArtifactErrorCode::none) {
                code = StockExternalArtifactErrorCode::invalid_property_value;
            }
            return value_failure<StockExternalReviewSummaryArtifact>(code);
        }
        return {std::move(output), StockExternalArtifactErrorCode::none, 0U};
    } catch (...) {
        return value_failure<StockExternalReviewSummaryArtifact>(
            StockExternalArtifactErrorCode::invalid_argument);
    }
}

namespace {

void append_approved_target(
    std::string& output,
    const StockExternalApprovedTargetBindingArtifact& target,
    bool& valid)
{
    output.push_back('{');
    append_unsigned_property(output, "ordinal", target.ordinal, valid);
    output.push_back(',');
    append_string_property(output, "link-identity-sha256",
                           target.link_identity_sha256, valid);
    output.push_back(',');
    append_string_property(output, "target-identity-sha256",
                           target.target_identity_sha256, valid);
    output.push_back(',');
    append_string_property(output, "target-inventory-sha256",
                           target.target_inventory_sha256, valid);
    output.push_back(',');
    append_string_property(
        output, "classification", to_string(target.classification), valid);
    output.push_back('}');
}

[[nodiscard]] bool parse_approved_target(
    const JsonValue& value,
    StockExternalApprovedTargetBindingArtifact& output,
    StockExternalArtifactErrorCode& code) noexcept
{
    if (!exact_object(
            value,
            {"ordinal", "link-identity-sha256", "target-identity-sha256",
             "target-inventory-sha256", "classification"},
            code) ||
        !unsigned_member(value, "ordinal", output.ordinal, code) ||
        !string_member(value, "link-identity-sha256",
                       output.link_identity_sha256, code) ||
        !string_member(value, "target-identity-sha256",
                       output.target_identity_sha256, code) ||
        !string_member(value, "target-inventory-sha256",
                       output.target_inventory_sha256, code) ||
        !parse_classification(value, output.classification, code)) {
        return false;
    }
    if (!valid_approved_target(output)) {
        code = StockExternalArtifactErrorCode::invalid_property_value;
        return false;
    }
    return true;
}

} // namespace

StockExternalArtifactTextResult serialize_stock_external_approval(
    const StockExternalApprovalArtifact& artifact) noexcept
{
    try {
        if (!valid_approval(artifact)) {
            return text_failure(
                StockExternalArtifactErrorCode::invalid_property_value);
        }
        bool valid = true;
        std::string output{"{"};
        append_string_property(
            output, "schema", kStockExternalApprovalArtifactSchemaV1, valid);
        output.push_back(',');
        append_string_property(
            output, "review-schema", artifact.review_schema, valid);
        output.push_back(',');
        append_unsigned_property(
            output, "review-version", artifact.review_version, valid);
        output.push_back(',');
        append_string_property(output, "review-root-fingerprint",
                               artifact.review_root_fingerprint, valid);
        output.push_back(',');
        append_string_property(output, "review-digest-sha256",
                               artifact.review_digest_sha256, valid);
        output.push_back(',');
        append_string_property(output, "source-root-fingerprint",
                               artifact.source_root_fingerprint, valid);
        output.push_back(',');
        append_key(output, "source-inventory", valid);
        append_inventory(output, artifact.source_inventory, valid);
        output.push_back(',');
        append_string_property(
            output, "review-nonce", artifact.review_nonce, valid);
        output.push_back(',');
        append_string_property(
            output, "approval-nonce", artifact.approval_nonce, valid);
        output.push_back(',');
        append_unsigned_property(output, "approval-timestamp-unix-seconds",
                                 artifact.approval_timestamp_unix_seconds,
                                 valid);
        output.push_back(',');
        append_unsigned_property(output, "expiration-unix-seconds",
                                 artifact.expiration_unix_seconds, valid);
        output.push_back(',');
        append_unsigned_property(
            output, "approval-count", artifact.approval_count, valid);
        output.push_back(',');
        append_string_property(output, "confirmation-profile",
                               artifact.confirmation_profile, valid);
        output.push_back(',');
        append_string_property(output, "implementation-profile",
                               artifact.implementation_profile, valid);
        output.push_back(',');
        append_key(output, "approved-targets", valid);
        output.push_back('[');
        for (std::size_t index = 0U;
             index < artifact.approved_targets.size(); ++index) {
            if (index != 0U) output.push_back(',');
            append_approved_target(
                output, artifact.approved_targets[index], valid);
        }
        output.push_back(']');
        output.push_back('}');
        return finish_serialized(std::move(output), valid);
    } catch (...) {
        return text_failure(StockExternalArtifactErrorCode::invalid_argument);
    }
}

StockExternalArtifactResult<StockExternalApprovalArtifact>
parse_stock_external_approval(const std::string_view json) noexcept
{
    try {
        JsonValue root{};
        StockExternalArtifactErrorCode code{};
        StockExternalApprovalArtifact output{};
        if (!parse_root(json, root, code) ||
            !exact_object(
                root,
                {"schema", "review-schema", "review-version",
                 "review-root-fingerprint", "review-digest-sha256",
                 "source-root-fingerprint", "source-inventory",
                 "review-nonce", "approval-nonce",
                 "approval-timestamp-unix-seconds",
                 "expiration-unix-seconds", "approval-count",
                 "confirmation-profile", "implementation-profile",
                 "approved-targets"},
                code) ||
            !exact_schema(root, kStockExternalApprovalArtifactSchemaV1, code)) {
            return value_failure<StockExternalApprovalArtifact>(code);
        }
        const auto* inventory = member(root, "source-inventory");
        const auto* targets = member(root, "approved-targets");
        if (inventory == nullptr || targets == nullptr ||
            targets->kind != JsonKind::array ||
            targets->array_value.size() >
                kMaximumStockExternalArtifactTargets ||
            !string_member(
                root, "review-schema", output.review_schema, code) ||
            !unsigned_member(
                root, "review-version", output.review_version, code) ||
            !string_member(root, "review-root-fingerprint",
                           output.review_root_fingerprint, code) ||
            !string_member(root, "review-digest-sha256",
                           output.review_digest_sha256, code) ||
            !string_member(root, "source-root-fingerprint",
                           output.source_root_fingerprint, code) ||
            !parse_inventory(*inventory, output.source_inventory, code) ||
            !string_member(root, "review-nonce", output.review_nonce, code) ||
            !string_member(
                root, "approval-nonce", output.approval_nonce, code) ||
            !unsigned_member(root, "approval-timestamp-unix-seconds",
                             output.approval_timestamp_unix_seconds, code) ||
            !unsigned_member(root, "expiration-unix-seconds",
                             output.expiration_unix_seconds, code) ||
            !unsigned_member(
                root, "approval-count", output.approval_count, code) ||
            !string_member(root, "confirmation-profile",
                           output.confirmation_profile, code) ||
            !string_member(root, "implementation-profile",
                           output.implementation_profile, code)) {
            if (code == StockExternalArtifactErrorCode::none) {
                code = StockExternalArtifactErrorCode::invalid_property_type;
            }
            return value_failure<StockExternalApprovalArtifact>(code);
        }
        output.approved_targets.reserve(targets->array_value.size());
        for (const auto& value : targets->array_value) {
            StockExternalApprovedTargetBindingArtifact target{};
            if (!parse_approved_target(value, target, code)) {
                return value_failure<StockExternalApprovalArtifact>(code);
            }
            output.approved_targets.push_back(std::move(target));
        }
        if (!valid_approval(output)) {
            return value_failure<StockExternalApprovalArtifact>(
                StockExternalArtifactErrorCode::invalid_property_value);
        }
        return {std::move(output), StockExternalArtifactErrorCode::none, 0U};
    } catch (...) {
        return value_failure<StockExternalApprovalArtifact>(
            StockExternalArtifactErrorCode::invalid_argument);
    }
}

StockExternalArtifactTextResult read_stock_external_artifact_leaf(
    const std::filesystem::path& exact_review_directory,
    const std::wstring_view exact_leaf_name) noexcept
{
    try {
        if (!valid_leaf(exact_leaf_name)) {
            return text_failure(
                StockExternalArtifactErrorCode::invalid_leaf_name);
        }
        if (!exact_review_directory.is_absolute() ||
            exact_review_directory.empty()) {
            return text_failure(
                StockExternalArtifactErrorCode::invalid_review_directory);
        }
        const auto normalized = exact_review_directory.lexically_normal();
        if (!same_path_ignore_case(
                normalized.native(), exact_review_directory.native()) ||
            has_reparse_component(normalized) || !local_fixed_volume(normalized)) {
            return text_failure(
                StockExternalArtifactErrorCode::invalid_review_directory);
        }

        UniqueHandle directory{::CreateFileW(
            normalized.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr)};
        if (!directory.valid()) {
            return text_failure(
                StockExternalArtifactErrorCode::review_directory_open_failed,
                ::GetLastError());
        }
        FileSnapshot directory_before{};
        bool directory_type = false;
        if (!query_file_snapshot(
                directory.get(), directory_before, directory_type)) {
            return text_failure(
                StockExternalArtifactErrorCode::
                    review_directory_identity_invalid,
                ::GetLastError());
        }
        if (!directory_type ||
            (directory_before.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
            !exact_opened_path(directory.get(), normalized)) {
            return text_failure(
                StockExternalArtifactErrorCode::
                    review_directory_identity_invalid);
        }

        const auto artifact_path =
            normalized / std::wstring{exact_leaf_name};
        UniqueHandle artifact{::CreateFileW(
            artifact_path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr)};
        if (!artifact.valid()) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_open_failed,
                ::GetLastError());
        }
        FileSnapshot before{};
        bool artifact_directory = false;
        if (!query_file_snapshot(artifact.get(), before, artifact_directory)) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_identity_query_failed,
                ::GetLastError());
        }
        if (artifact_directory ||
            (before.attributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_not_ordinary_file);
        }
        if (before.link_count != 1U) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_hardlink_rejected);
        }
        if (before.size > kMaximumStockExternalArtifactBytes) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_too_large);
        }
        if (!exact_opened_path(artifact.get(), artifact_path)) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_exact_path_mismatch);
        }
        if (!only_default_stream(artifact_path)) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_alternate_data_stream);
        }

        std::string bytes(static_cast<std::size_t>(before.size), '\0');
        std::size_t offset = 0U;
        while (offset < bytes.size()) {
            const auto remaining = bytes.size() - offset;
            const DWORD requested = static_cast<DWORD>((std::min)(
                remaining,
                static_cast<std::size_t>(
                    (std::numeric_limits<DWORD>::max)())));
            DWORD read = 0U;
            if (!::ReadFile(
                    artifact.get(), bytes.data() + offset, requested, &read,
                    nullptr) ||
                read == 0U) {
                return text_failure(
                    StockExternalArtifactErrorCode::artifact_read_failed,
                    ::GetLastError());
            }
            offset += read;
        }
        char extra = '\0';
        DWORD extra_read = 0U;
        if (!::ReadFile(artifact.get(), &extra, 1U, &extra_read, nullptr)) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_read_failed,
                ::GetLastError());
        }
        if (extra_read != 0U) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_changed);
        }

        FileSnapshot after{};
        bool after_directory = false;
        FileSnapshot directory_after{};
        bool directory_after_type = false;
        if (!query_file_snapshot(artifact.get(), after, after_directory) ||
            !query_file_snapshot(
                directory.get(), directory_after, directory_after_type)) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_identity_query_failed,
                ::GetLastError());
        }
        if (after_directory || !directory_after_type || before != after ||
            directory_before != directory_after) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_changed);
        }
        if (!exact_opened_path(artifact.get(), artifact_path) ||
            !exact_opened_path(directory.get(), normalized)) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_exact_path_mismatch);
        }
        if (!only_default_stream(artifact_path)) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_alternate_data_stream);
        }
        return {std::move(bytes), StockExternalArtifactErrorCode::none, 0U};
    } catch (...) {
        return text_failure(StockExternalArtifactErrorCode::invalid_argument);
    }
}

std::optional<std::wstring> stock_external_private_target_leaf(
    const std::uint64_t ordinal) noexcept
{
    try {
        if (ordinal < 1U || ordinal > kMaximumStockExternalArtifactTargets) {
            return std::nullopt;
        }
        std::wstring number = std::to_wstring(ordinal);
        if (number.size() > 4U) return std::nullopt;
        number.insert(number.begin(), 4U - number.size(), L'0');
        return L"target-" + number + L"-private.json";
    } catch (...) {
        return std::nullopt;
    }
}

StockExternalArtifactTextResult stock_external_artifact_sha256(
    const std::string_view bytes) noexcept
{
    try {
        if (bytes.size() > kMaximumStockExternalArtifactBytes) {
            return text_failure(
                StockExternalArtifactErrorCode::artifact_too_large);
        }
        UniqueAlgorithm algorithm;
        if (::BCryptOpenAlgorithmProvider(
                algorithm.put(), BCRYPT_SHA256_ALGORITHM, nullptr, 0U) < 0) {
            return text_failure(
                StockExternalArtifactErrorCode::digest_failed);
        }
        DWORD object_size = 0U;
        DWORD returned = 0U;
        if (::BCryptGetProperty(
                algorithm.get(), BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                &returned, 0U) < 0 ||
            returned != sizeof(object_size) || object_size == 0U ||
            object_size > 1U * 1'024U * 1'024U) {
            return text_failure(
                StockExternalArtifactErrorCode::digest_failed);
        }
        std::vector<UCHAR> object(object_size);
        UniqueHash hash;
        if (::BCryptCreateHash(
                algorithm.get(), hash.put(), object.data(),
                static_cast<ULONG>(object.size()), nullptr, 0U, 0U) < 0) {
            return text_failure(
                StockExternalArtifactErrorCode::digest_failed);
        }
        if (!bytes.empty() &&
            ::BCryptHashData(
                hash.get(),
                reinterpret_cast<PUCHAR>(
                    const_cast<char*>(bytes.data())),
                static_cast<ULONG>(bytes.size()), 0U) < 0) {
            return text_failure(
                StockExternalArtifactErrorCode::digest_failed);
        }
        std::array<std::byte, 32U> digest{};
        if (::BCryptFinishHash(
                hash.get(), reinterpret_cast<PUCHAR>(digest.data()),
                static_cast<ULONG>(digest.size()), 0U) < 0) {
            return text_failure(
                StockExternalArtifactErrorCode::digest_failed);
        }
        static constexpr char digits[] = "0123456789abcdef";
        std::string output(digest.size() * 2U, '\0');
        for (std::size_t index = 0U; index < digest.size(); ++index) {
            const auto value = std::to_integer<unsigned int>(digest[index]);
            output[index * 2U] = digits[value >> 4U];
            output[index * 2U + 1U] = digits[value & 0x0fU];
        }
        return {std::move(output), StockExternalArtifactErrorCode::none, 0U};
    } catch (...) {
        return text_failure(StockExternalArtifactErrorCode::digest_failed);
    }
}

} // namespace hlclient::platform::windows
