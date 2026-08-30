#include <hlclient/goldsrc/stock_runtime_transport_journal.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <limits>
#include <sstream>
#include <utility>

namespace hlclient::goldsrc {
namespace {

enum class JsonKind { string, integer, boolean, integer_array };

struct JsonProperty final {
    std::string name;
    JsonKind kind{JsonKind::string};
    std::string scalar;
    std::vector<std::size_t> integers;
    std::size_t offset{0U};
};

[[nodiscard]] StockRuntimeTransportJournalEntryParseResult parse_failure(
    const StockRuntimeTransportJournalErrorCode code,
    const std::size_t offset,
    std::string context)
{
    return {
        std::nullopt,
        StockRuntimeTransportJournalError{code, 0U, offset, std::move(context)},
    };
}

class JournalJsonReader final {
public:
    explicit JournalJsonReader(const std::string_view input) noexcept
        : input_{input}
    {
    }

    [[nodiscard]] bool read(
        std::vector<JsonProperty>& properties,
        StockRuntimeTransportJournalError& error)
    {
        skip_space();
        if (!consume('{')) {
            return fail(error, StockRuntimeTransportJournalErrorCode::invalid_json,
                        "journal entry must begin with an object");
        }
        skip_space();
        while (peek() != '}') {
            if (properties.size() >= 15U) {
                return fail(error, StockRuntimeTransportJournalErrorCode::unknown_property,
                            "journal entry has too many properties");
            }
            JsonProperty property;
            property.offset = offset_;
            if (!read_string(property.name)) {
                return fail(error, StockRuntimeTransportJournalErrorCode::invalid_json,
                            "journal property name is invalid");
            }
            if (std::ranges::any_of(properties, [&property](const auto& existing) {
                    return existing.name == property.name;
                })) {
                return fail(error, StockRuntimeTransportJournalErrorCode::duplicate_property,
                            "journal property is duplicated");
            }
            skip_space();
            if (!consume(':')) {
                return fail(error, StockRuntimeTransportJournalErrorCode::invalid_json,
                            "journal property lacks a colon");
            }
            skip_space();
            if (peek() == '"') {
                property.kind = JsonKind::string;
                if (!read_string(property.scalar)) {
                    return fail(error, StockRuntimeTransportJournalErrorCode::invalid_json,
                                "journal string is invalid");
                }
            } else if (peek() == '[') {
                property.kind = JsonKind::integer_array;
                if (!read_integer_array(property.integers)) {
                    return fail(error, StockRuntimeTransportJournalErrorCode::invalid_json,
                                "journal integer array is invalid");
                }
            } else {
                if (!read_scalar(property.scalar)) {
                    return fail(error, StockRuntimeTransportJournalErrorCode::invalid_json,
                                "journal scalar is invalid");
                }
                property.kind = property.scalar == "true" || property.scalar == "false"
                                    ? JsonKind::boolean
                                    : JsonKind::integer;
            }
            properties.push_back(std::move(property));
            skip_space();
            if (peek() == '}') {
                break;
            }
            if (!consume(',')) {
                return fail(error, StockRuntimeTransportJournalErrorCode::invalid_json,
                            "journal properties are not comma separated");
            }
            skip_space();
        }
        if (!consume('}')) {
            return fail(error, StockRuntimeTransportJournalErrorCode::invalid_json,
                        "journal object is unterminated");
        }
        skip_space();
        if (offset_ != input_.size()) {
            return fail(error, StockRuntimeTransportJournalErrorCode::invalid_json,
                        "journal entry has trailing bytes");
        }
        return true;
    }

private:
    void skip_space() noexcept
    {
        while (offset_ < input_.size()) {
            const auto value = input_[offset_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
                break;
            }
            ++offset_;
        }
    }

    [[nodiscard]] char peek() const noexcept
    {
        return offset_ < input_.size() ? input_[offset_] : '\0';
    }

    [[nodiscard]] bool consume(const char value) noexcept
    {
        if (peek() != value) {
            return false;
        }
        ++offset_;
        return true;
    }

    [[nodiscard]] bool read_string(std::string& output)
    {
        if (!consume('"')) {
            return false;
        }
        const auto begin = offset_;
        while (offset_ < input_.size() && input_[offset_] != '"') {
            const auto value = static_cast<unsigned char>(input_[offset_]);
            if (value < 0x20U || value > 0x7eU || input_[offset_] == '\\') {
                return false;
            }
            ++offset_;
        }
        if (!consume('"')) {
            return false;
        }
        output.assign(input_.substr(begin, offset_ - begin - 1U));
        return true;
    }

    [[nodiscard]] bool read_scalar(std::string& output)
    {
        const auto begin = offset_;
        while (offset_ < input_.size() && input_[offset_] != ',' &&
               input_[offset_] != '}' && input_[offset_] != ' ' &&
               input_[offset_] != '\t' && input_[offset_] != '\r' &&
               input_[offset_] != '\n') {
            ++offset_;
        }
        if (begin == offset_) {
            return false;
        }
        output.assign(input_.substr(begin, offset_ - begin));
        return true;
    }

    [[nodiscard]] bool read_integer_array(std::vector<std::size_t>& values)
    {
        if (!consume('[')) {
            return false;
        }
        skip_space();
        if (consume(']')) {
            return true;
        }
        while (values.size() < kMaximumStockRuntimeJournalEmissionsPerEntry) {
            const auto begin = offset_;
            while (offset_ < input_.size() && input_[offset_] >= '0' &&
                   input_[offset_] <= '9') {
                ++offset_;
            }
            if (begin == offset_) {
                return false;
            }
            std::size_t value = 0U;
            const auto converted = std::from_chars(
                input_.data() + begin, input_.data() + offset_, value, 10);
            if (converted.ec != std::errc{} ||
                converted.ptr != input_.data() + offset_ ||
                (offset_ - begin > 1U && input_[begin] == '0')) {
                return false;
            }
            values.push_back(value);
            skip_space();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
            skip_space();
        }
        return false;
    }

    [[nodiscard]] bool fail(
        StockRuntimeTransportJournalError& error,
        const StockRuntimeTransportJournalErrorCode code,
        std::string context) const
    {
        error = StockRuntimeTransportJournalError{
            code, 0U, offset_, std::move(context)};
        return false;
    }

    std::string_view input_;
    std::size_t offset_{0U};
};

[[nodiscard]] const JsonProperty* find_property(
    const std::vector<JsonProperty>& properties,
    const std::string_view name) noexcept
{
    const auto found = std::ranges::find_if(properties, [name](const auto& property) {
        return property.name == name;
    });
    return found == properties.end() ? nullptr : &*found;
}

template<typename Integer>
[[nodiscard]] bool read_integer(
    const std::vector<JsonProperty>& properties,
    const std::string_view name,
    Integer& value) noexcept
{
    const auto* property = find_property(properties, name);
    if (property == nullptr || property->kind != JsonKind::integer ||
        property->scalar.empty() || property->scalar.front() == '-' ||
        (property->scalar.size() > 1U && property->scalar.front() == '0')) {
        return false;
    }
    Integer candidate{};
    const auto converted = std::from_chars(
        property->scalar.data(),
        property->scalar.data() + property->scalar.size(),
        candidate,
        10);
    if (converted.ec != std::errc{} ||
        converted.ptr != property->scalar.data() + property->scalar.size()) {
        return false;
    }
    value = candidate;
    return true;
}

[[nodiscard]] bool read_string(
    const std::vector<JsonProperty>& properties,
    const std::string_view name,
    std::string_view& value) noexcept
{
    const auto* property = find_property(properties, name);
    if (property == nullptr || property->kind != JsonKind::string) {
        return false;
    }
    value = property->scalar;
    return true;
}

[[nodiscard]] bool read_bool(
    const std::vector<JsonProperty>& properties,
    const std::string_view name,
    bool& value) noexcept
{
    const auto* property = find_property(properties, name);
    if (property == nullptr || property->kind != JsonKind::boolean) {
        return false;
    }
    value = property->scalar == "true";
    return true;
}

[[nodiscard]] bool read_array(
    const std::vector<JsonProperty>& properties,
    const std::string_view name,
    std::vector<std::size_t>& value)
{
    const auto* property = find_property(properties, name);
    if (property == nullptr || property->kind != JsonKind::integer_array) {
        return false;
    }
    value = property->integers;
    return true;
}

[[nodiscard]] bool lower_hex_sha256(std::string_view value, std::string& result)
{
    if (value.size() != 64U) {
        return false;
    }
    result.clear();
    result.reserve(value.size());
    for (const auto character : value) {
        const auto unsigned_character = static_cast<unsigned char>(character);
        if (!std::isxdigit(unsigned_character)) {
            return false;
        }
        result.push_back(static_cast<char>(std::tolower(unsigned_character)));
    }
    return true;
}

[[nodiscard]] std::optional<StockRuntimeCaptureDirection> parse_direction(
    const std::string_view value) noexcept
{
    if (value == "client_to_server") {
        return StockRuntimeCaptureDirection::client_to_server;
    }
    if (value == "server_to_client") {
        return StockRuntimeCaptureDirection::server_to_client;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<StockRuntimeTransportRole> parse_role(
    const std::string_view value) noexcept
{
    if (value == "research_client") {
        return StockRuntimeTransportRole::research_client;
    }
    if (value == "research_server") {
        return StockRuntimeTransportRole::research_server;
    }
    if (value == "unexpected_source") {
        return StockRuntimeTransportRole::unexpected_source;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<StockRuntimeCaptureAction> parse_action(
    const std::string_view value) noexcept
{
    if (value == "forward") return StockRuntimeCaptureAction::forward;
    if (value == "drop") return StockRuntimeCaptureAction::drop;
    if (value == "duplicate") return StockRuntimeCaptureAction::duplicate;
    if (value == "hold_for_delay") return StockRuntimeCaptureAction::hold_for_delay;
    if (value == "hold_for_reorder") return StockRuntimeCaptureAction::hold_for_reorder;
    return std::nullopt;
}

[[nodiscard]] std::optional<StockRuntimeTransportHoldState> parse_hold_state(
    const std::string_view value) noexcept
{
    if (value == "none") return StockRuntimeTransportHoldState::none;
    if (value == "held") return StockRuntimeTransportHoldState::held;
    if (value == "released") return StockRuntimeTransportHoldState::released;
    if (value == "unresolved") return StockRuntimeTransportHoldState::unresolved;
    return std::nullopt;
}

[[nodiscard]] std::string_view direction_text(
    const StockRuntimeCaptureDirection direction) noexcept
{
    return direction == StockRuntimeCaptureDirection::client_to_server
               ? "client_to_server"
               : "server_to_client";
}

[[nodiscard]] std::string_view action_text(
    const StockRuntimeCaptureAction action) noexcept
{
    switch (action) {
    case StockRuntimeCaptureAction::forward: return "forward";
    case StockRuntimeCaptureAction::drop: return "drop";
    case StockRuntimeCaptureAction::duplicate: return "duplicate";
    case StockRuntimeCaptureAction::hold_for_delay: return "hold_for_delay";
    case StockRuntimeCaptureAction::hold_for_reorder: return "hold_for_reorder";
    }
    return "unknown";
}

[[nodiscard]] bool exact_raw_filename(
    const StockRuntimeTransportJournalEntry& entry) noexcept
{
    if (entry.raw_filename.size() != 16U ||
        entry.raw_filename.substr(8U) !=
            (entry.direction == StockRuntimeCaptureDirection::client_to_server
                 ? "-c2s.bin"
                 : "-s2c.bin")) {
        return false;
    }
    for (std::size_t index = 0U; index < 8U; ++index) {
        if (entry.raw_filename[index] < '0' || entry.raw_filename[index] > '9') {
            return false;
        }
    }
    std::size_t ordinal = 0U;
    const auto converted = std::from_chars(
        entry.raw_filename.data(), entry.raw_filename.data() + 8U, ordinal, 10);
    return converted.ec == std::errc{} &&
           converted.ptr == entry.raw_filename.data() + 8U &&
           ordinal == entry.observed_ordinal;
}

[[nodiscard]] StockRuntimeTransportJournalValidation validation_failure(
    const StockRuntimeTransportJournalErrorCode code,
    const std::size_t ordinal,
    std::string context) noexcept
{
    return StockRuntimeTransportJournalValidation{
        StockRuntimeTransportJournalError{
            code, ordinal, 0U, std::move(context)},
    };
}

} // namespace

StockRuntimeTransportJournalEntryParseResult
parse_stock_runtime_transport_journal_entry(const std::string_view json_line)
{
    if (json_line.size() > kMaximumStockRuntimeJournalLineBytes) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::line_too_large,
            kMaximumStockRuntimeJournalLineBytes,
            "journal line exceeds the project bound");
    }

    std::vector<JsonProperty> properties;
    StockRuntimeTransportJournalError json_error;
    JournalJsonReader reader{json_line};
    if (!reader.read(properties, json_error)) {
        return {std::nullopt, std::move(json_error)};
    }

    constexpr std::array<std::string_view, 15U> expected{
        "schema", "observed_ordinal", "direction", "direction_ordinal",
        "relative_timestamp_us", "payload_byte_count", "raw_filename",
        "source_role", "destination_role", "action", "hold_state",
        "emitted_ordinals", "delivered", "wrong_source", "sha256",
    };
    if (properties.size() != expected.size() ||
        std::ranges::any_of(properties, [&expected](const auto& property) {
            return std::ranges::find(expected, property.name) == expected.end();
        })) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::unknown_property,
            0U,
            "journal properties do not match schema v1");
    }

    StockRuntimeTransportJournalEntry result;
    std::string_view value;
    if (!read_string(properties, "schema", value) ||
        value != kStockRuntimeTransportJournalSchema) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::wrong_schema, 0U,
            "journal schema is not v1");
    }
    if (!read_integer(properties, "observed_ordinal", result.observed_ordinal)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_ordinal, 0U,
            "observed ordinal is invalid");
    }
    if (!read_string(properties, "direction", value)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_direction, 0U,
            "direction is absent");
    }
    const auto direction = parse_direction(value);
    if (!direction) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_direction, 0U,
            "direction is invalid");
    }
    result.direction = *direction;
    if (!read_integer(properties, "direction_ordinal", result.direction_ordinal) ||
        result.direction_ordinal == 0U) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_direction_ordinal, 0U,
            "direction ordinal is invalid");
    }
    if (!read_integer(properties, "relative_timestamp_us",
                      result.relative_timestamp_us)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::timestamp_not_monotonic, 0U,
            "relative timestamp is invalid");
    }
    if (!read_integer(properties, "payload_byte_count",
                      result.payload_byte_count)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_payload_size, 0U,
            "payload byte count is invalid");
    }
    if (!read_string(properties, "raw_filename", value)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_raw_filename, 0U,
            "raw filename is absent");
    }
    result.raw_filename.assign(value);
    if (!exact_raw_filename(result)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_raw_filename, 0U,
            "raw filename does not match ordinal and direction");
    }
    if (!read_string(properties, "source_role", value)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_role, 0U,
            "source role is absent");
    }
    const auto source_role = parse_role(value);
    if (!source_role) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_role, 0U,
            "source role is invalid");
    }
    result.source_role = *source_role;
    if (!read_string(properties, "destination_role", value)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_role, 0U,
            "destination role is absent");
    }
    const auto destination_role = parse_role(value);
    if (!destination_role) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_role, 0U,
            "destination role is invalid");
    }
    result.destination_role = *destination_role;
    if (!read_string(properties, "action", value)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_action, 0U,
            "action is absent");
    }
    const auto action = parse_action(value);
    if (!action) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_action, 0U,
            "action is invalid");
    }
    result.action = *action;
    if (!read_string(properties, "hold_state", value)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_hold_state, 0U,
            "hold state is absent");
    }
    const auto hold_state = parse_hold_state(value);
    if (!hold_state) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_hold_state, 0U,
            "hold state is invalid");
    }
    result.hold_state = *hold_state;
    if (!read_array(properties, "emitted_ordinals", result.emitted_ordinals)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_emitted_ordinals, 0U,
            "emitted ordinals are invalid");
    }
    if (!read_bool(properties, "delivered", result.delivered)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_delivery_state, 0U,
            "delivered flag is invalid");
    }
    if (!read_bool(properties, "wrong_source", result.wrong_source)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_wrong_source_state, 0U,
            "wrong-source flag is invalid");
    }
    if (!read_string(properties, "sha256", value) ||
        !lower_hex_sha256(value, result.sha256)) {
        return parse_failure(
            StockRuntimeTransportJournalErrorCode::invalid_sha256, 0U,
            "SHA-256 is not 64 hexadecimal characters");
    }
    return {std::move(result), std::nullopt};
}

std::string serialize_stock_runtime_transport_journal_entry(
    const StockRuntimeTransportJournalEntry& entry)
{
    std::ostringstream stream;
    stream << "{\"schema\":\"" << kStockRuntimeTransportJournalSchema
           << "\",\"observed_ordinal\":" << entry.observed_ordinal
           << ",\"direction\":\"" << direction_text(entry.direction)
           << "\",\"direction_ordinal\":" << entry.direction_ordinal
           << ",\"relative_timestamp_us\":" << entry.relative_timestamp_us
           << ",\"payload_byte_count\":" << entry.payload_byte_count
           << ",\"raw_filename\":\"" << entry.raw_filename
           << "\",\"source_role\":\"" << to_string(entry.source_role)
           << "\",\"destination_role\":\"" << to_string(entry.destination_role)
           << "\",\"action\":\"" << action_text(entry.action)
           << "\",\"hold_state\":\"" << to_string(entry.hold_state)
           << "\",\"emitted_ordinals\":[";
    for (std::size_t index = 0U; index < entry.emitted_ordinals.size(); ++index) {
        if (index != 0U) stream << ',';
        stream << entry.emitted_ordinals[index];
    }
    stream << "],\"delivered\":" << (entry.delivered ? "true" : "false")
           << ",\"wrong_source\":" << (entry.wrong_source ? "true" : "false")
           << ",\"sha256\":\"" << entry.sha256 << "\"}";
    return stream.str();
}

StockRuntimeTransportJournalValidation validate_stock_runtime_transport_journal(
    const std::span<const StockRuntimeTransportJournalEntry> entries,
    const StockRuntimeTransportJournalLimits limits,
    const StockRuntimeTransportJournalValidationPolicy policy)
{
    if (limits.maximum_entries == 0U ||
        limits.maximum_entries > StockRuntimeCaptureHardCaps::maximum_datagrams ||
        limits.maximum_payload_bytes == 0U ||
        limits.maximum_payload_bytes > StockRuntimeCaptureHardCaps::maximum_payload_bytes ||
        limits.maximum_total_raw_bytes == 0U ||
        limits.maximum_total_raw_bytes > StockRuntimeCaptureHardCaps::maximum_total_raw_bytes ||
        limits.maximum_emitted_datagrams == 0U ||
        limits.maximum_emitted_datagrams >
            StockRuntimeCaptureHardCaps::maximum_datagrams * 2U ||
        limits.maximum_relative_timestamp_us == 0U) {
        return validation_failure(
            StockRuntimeTransportJournalErrorCode::invalid_configuration, 0U,
            "journal limits are invalid");
    }
    if (entries.empty() || entries.size() > limits.maximum_entries) {
        return validation_failure(
            StockRuntimeTransportJournalErrorCode::count_mismatch, entries.size(),
            "journal entry count is outside its configured bound");
    }

    std::vector<std::size_t> emission_owners;
    try {
        emission_owners.assign(limits.maximum_emitted_datagrams,
                               (std::numeric_limits<std::size_t>::max)());
    } catch (...) {
        return validation_failure(
            StockRuntimeTransportJournalErrorCode::invalid_configuration, 0U,
            "journal emission index allocation failed");
    }
    std::size_t c2s = 0U;
    std::size_t s2c = 0U;
    std::uint64_t raw_bytes = 0U;
    std::size_t emission_count = 0U;
    std::uint64_t previous_timestamp = 0U;
    bool transport_complete = true;

    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        if (entry.observed_ordinal != index) {
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::invalid_ordinal, index,
                "observed ordinals are not contiguous and zero based");
        }
        if (entry.relative_timestamp_us > limits.maximum_relative_timestamp_us ||
            (index != 0U && entry.relative_timestamp_us < previous_timestamp)) {
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::timestamp_not_monotonic, index,
                "relative timestamps are outside the monotonic bound");
        }
        previous_timestamp = entry.relative_timestamp_us;
        if (entry.payload_byte_count > limits.maximum_payload_bytes ||
            raw_bytes > limits.maximum_total_raw_bytes - entry.payload_byte_count) {
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::byte_limit_exceeded, index,
                "journal raw-byte accounting exceeds its bound");
        }
        raw_bytes += entry.payload_byte_count;
        if (!exact_raw_filename(entry)) {
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::invalid_raw_filename, index,
                "raw filename does not match ordinal and direction");
        }

        const bool client_direction =
            entry.direction == StockRuntimeCaptureDirection::client_to_server;
        const auto expected_direction_ordinal = client_direction ? ++c2s : ++s2c;
        if (entry.direction_ordinal != expected_direction_ordinal) {
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::invalid_direction_ordinal, index,
                "direction ordinals are not contiguous and one based");
        }
        const auto expected_source = client_direction
                                         ? StockRuntimeTransportRole::research_client
                                         : StockRuntimeTransportRole::research_server;
        const auto expected_destination = client_direction
                                              ? StockRuntimeTransportRole::research_server
                                              : StockRuntimeTransportRole::research_client;
        if (entry.destination_role != expected_destination ||
            (!entry.wrong_source && entry.source_role != expected_source) ||
            (entry.wrong_source &&
             entry.source_role != StockRuntimeTransportRole::unexpected_source)) {
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::role_direction_mismatch, index,
                "journal roles contradict the datagram direction");
        }
        if (entry.sha256.size() != 64U ||
            !std::ranges::all_of(entry.sha256, [](const char character) {
                return (character >= '0' && character <= '9') ||
                       (character >= 'a' && character <= 'f');
            })) {
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::invalid_sha256, index,
                "journal SHA-256 is not canonical lowercase hexadecimal");
        }
        if (entry.delivered != !entry.emitted_ordinals.empty()) {
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::invalid_delivery_state, index,
                "delivered flag contradicts emission references");
        }
        if (entry.wrong_source && entry.delivered) {
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::invalid_wrong_source_state, index,
                "wrong-source datagram cannot be delivered");
        }
        if (entry.wrong_source) {
            transport_complete = false;
            if (policy ==
                StockRuntimeTransportJournalValidationPolicy::complete_capture) {
                return validation_failure(
                    StockRuntimeTransportJournalErrorCode::invalid_wrong_source_state,
                    index,
                    "complete capture cannot contain an unexpected source");
            }
        }
        for (std::size_t emission_index = 1U;
             emission_index < entry.emitted_ordinals.size();
             ++emission_index) {
            if (entry.emitted_ordinals[emission_index] !=
                entry.emitted_ordinals[emission_index - 1U] + 1U) {
                return validation_failure(
                    StockRuntimeTransportJournalErrorCode::invalid_emitted_ordinals,
                    index,
                    "one datagram's duplicate emissions are not ascending and consecutive");
            }
        }

        std::size_t required_emissions = 0U;
        switch (entry.action) {
        case StockRuntimeCaptureAction::forward:
            if (entry.hold_state != StockRuntimeTransportHoldState::none) {
                return validation_failure(
                    StockRuntimeTransportJournalErrorCode::invalid_hold_state, index,
                    "forward action cannot have a hold state");
            }
            required_emissions = entry.wrong_source ? 0U : 1U;
            break;
        case StockRuntimeCaptureAction::drop:
            if (entry.hold_state != StockRuntimeTransportHoldState::none) {
                return validation_failure(
                    StockRuntimeTransportJournalErrorCode::invalid_hold_state, index,
                    "drop action cannot have a hold state");
            }
            required_emissions = 0U;
            break;
        case StockRuntimeCaptureAction::duplicate:
            if (entry.hold_state != StockRuntimeTransportHoldState::none ||
                entry.wrong_source) {
                return validation_failure(
                    StockRuntimeTransportJournalErrorCode::invalid_hold_state, index,
                    "duplicate action has an incompatible state");
            }
            required_emissions = 2U;
            break;
        case StockRuntimeCaptureAction::hold_for_delay:
        case StockRuntimeCaptureAction::hold_for_reorder:
            if (entry.hold_state == StockRuntimeTransportHoldState::held ||
                entry.hold_state == StockRuntimeTransportHoldState::unresolved) {
                transport_complete = false;
                if (policy ==
                    StockRuntimeTransportJournalValidationPolicy::complete_capture) {
                    return validation_failure(
                        StockRuntimeTransportJournalErrorCode::unresolved_hold, index,
                        "complete journal contains an unresolved held datagram");
                }
                if (entry.hold_state == StockRuntimeTransportHoldState::held) {
                    required_emissions = 0U;
                } else {
                    // A bounded-deadline flush may emit the held bytes once,
                    // but it cannot claim that the requested reorder occurred.
                    if (entry.emitted_ordinals.size() > 1U) {
                        return validation_failure(
                            StockRuntimeTransportJournalErrorCode::invalid_emitted_ordinals,
                            index,
                            "unresolved hold has more than one deadline emission");
                    }
                    required_emissions = entry.emitted_ordinals.size();
                }
                break;
            }
            if (entry.hold_state != StockRuntimeTransportHoldState::released ||
                entry.wrong_source) {
                return validation_failure(
                    StockRuntimeTransportJournalErrorCode::invalid_hold_state, index,
                    "held action was not released exactly once");
            }
            required_emissions = 1U;
            break;
        }
        if (entry.emitted_ordinals.size() != required_emissions) {
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::invalid_emitted_ordinals, index,
                "action does not match its emission cardinality");
        }
        for (const auto emission : entry.emitted_ordinals) {
            if (emission >= limits.maximum_emitted_datagrams ||
                emission_owners[emission] !=
                    (std::numeric_limits<std::size_t>::max)()) {
                return validation_failure(
                    StockRuntimeTransportJournalErrorCode::emission_reference_mismatch,
                    index,
                    "emission ordinal is duplicate or outside its bound");
            }
            emission_owners[emission] = index;
            emission_count = (std::max)(emission_count, emission + 1U);
        }
    }

    if (emission_count > limits.maximum_emitted_datagrams) {
        return validation_failure(
            StockRuntimeTransportJournalErrorCode::count_mismatch, entries.size(),
            "emitted datagram count exceeds its bound");
    }
    for (std::size_t ordinal = 0U; ordinal < emission_count; ++ordinal) {
        if (emission_owners[ordinal] ==
            (std::numeric_limits<std::size_t>::max)()) {
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::emission_reference_mismatch,
                emission_owners.size(),
                "emission ordinals are not contiguous and zero based");
        }
    }

    // A held datagram is released only by the immediately following observed
    // datagram in the same direction. Delay preserves peer order; reorder
    // emits the successor first. This validates action history without using
    // timestamps as a proxy for delivery order.
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const auto& held = entries[index];
        if (held.action != StockRuntimeCaptureAction::hold_for_delay &&
            held.action != StockRuntimeCaptureAction::hold_for_reorder) {
            continue;
        }
        if (held.hold_state != StockRuntimeTransportHoldState::released) {
            continue;
        }
        const auto successor = std::ranges::find_if(
            entries.subspan(index + 1U), [&held](const auto& candidate) {
                return candidate.direction == held.direction;
            });
        if (successor == entries.subspan(index + 1U).end()) {
            // A delayed datagram may be released exactly once at the bounded
            // capture deadline.  There is then no successor whose peer order
            // can be compared, but the requested delay was still completed.
            // Reorder, by contrast, is impossible without a successor and
            // must have been journalled as unresolved instead of released.
            if (held.action == StockRuntimeCaptureAction::hold_for_delay) {
                continue;
            }
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::unresolved_hold, index,
                "released reorder has no same-direction successor");
        }
        if (successor->action != StockRuntimeCaptureAction::forward ||
            successor->emitted_ordinals.size() != 1U ||
            held.emitted_ordinals.size() != 1U) {
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::unresolved_hold, index,
                "held datagram has no exact same-direction release successor");
        }
        const bool delay_order = held.emitted_ordinals.front() <
                                 successor->emitted_ordinals.front();
        if ((held.action == StockRuntimeCaptureAction::hold_for_delay && !delay_order) ||
            (held.action == StockRuntimeCaptureAction::hold_for_reorder && delay_order)) {
            return validation_failure(
                StockRuntimeTransportJournalErrorCode::emission_reference_mismatch,
                index,
                "held-datagram emission order contradicts the selected action");
        }
    }

    return StockRuntimeTransportJournalValidation{
        std::nullopt, emission_count, raw_bytes, c2s, s2c,
        transport_complete};
}

} // namespace hlclient::goldsrc
