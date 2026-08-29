#include <hlclient/goldsrc/stock_runtime_capture.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] StockRuntimeCaptureLimitValidation limit_error(
    const StockRuntimeCaptureLimitErrorCode code,
    std::string field)
{
    return StockRuntimeCaptureLimitValidation{code, std::move(field)};
}

struct JsonProperty final {
    std::string name;
    std::string value;
    bool quoted{false};
};

class FlatJsonReader final {
public:
    explicit FlatJsonReader(const std::string_view input) noexcept : input_{input} {}

    [[nodiscard]] bool read(std::vector<JsonProperty>& properties, std::string& error)
    {
        skip_space();
        if (!consume('{')) {
            error = "metadata must begin with a JSON object";
            return false;
        }
        skip_space();
        if (consume('}')) {
            error = "metadata object is empty";
            return false;
        }
        while (properties.size() < 64U) {
            JsonProperty property;
            if (!read_string(property.name)) {
                error = "metadata property name is invalid";
                return false;
            }
            if (std::ranges::any_of(properties, [&property](const auto& existing) {
                    return existing.name == property.name;
                })) {
                error = "metadata contains a duplicate property";
                return false;
            }
            skip_space();
            if (!consume(':')) {
                error = "metadata property lacks a colon";
                return false;
            }
            skip_space();
            if (peek() == '"') {
                property.quoted = true;
                if (!read_string(property.value)) {
                    error = "metadata string value is invalid";
                    return false;
                }
            } else {
                const auto begin = offset_;
                while (offset_ < input_.size() &&
                       input_[offset_] != ',' && input_[offset_] != '}' &&
                       input_[offset_] != ' ' && input_[offset_] != '\t' &&
                       input_[offset_] != '\r' && input_[offset_] != '\n') {
                    ++offset_;
                }
                if (begin == offset_) {
                    error = "metadata scalar value is absent";
                    return false;
                }
                property.value.assign(input_.substr(begin, offset_ - begin));
            }
            properties.push_back(std::move(property));
            skip_space();
            if (consume('}')) {
                skip_space();
                if (offset_ != input_.size()) {
                    error = "metadata has bytes after the JSON object";
                    return false;
                }
                return true;
            }
            if (!consume(',')) {
                error = "metadata properties are not comma separated";
                return false;
            }
            skip_space();
        }
        error = "metadata property count exceeds its bound";
        return false;
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
            const unsigned char value = static_cast<unsigned char>(input_[offset_]);
            if (value < 0x20U || value > 0x7eU || input_[offset_] == '\\') {
                return false;
            }
            ++offset_;
        }
        if (offset_ >= input_.size()) {
            return false;
        }
        output.assign(input_.substr(begin, offset_ - begin));
        ++offset_;
        return true;
    }

    std::string_view input_;
    std::size_t offset_{0U};
};

[[nodiscard]] const JsonProperty* find_property(
    const std::vector<JsonProperty>& properties,
    const std::string_view name) noexcept
{
    const auto found = std::ranges::find_if(properties, [name](const auto& value) {
        return value.name == name;
    });
    return found == properties.end() ? nullptr : &*found;
}

[[nodiscard]] bool read_string_property(
    const std::vector<JsonProperty>& properties,
    const std::string_view name,
    std::string_view& value) noexcept
{
    const auto* property = find_property(properties, name);
    if (property == nullptr || !property->quoted) {
        return false;
    }
    value = property->value;
    return true;
}

[[nodiscard]] bool read_bool_property(
    const std::vector<JsonProperty>& properties,
    const std::string_view name,
    bool& value) noexcept
{
    const auto* property = find_property(properties, name);
    if (property == nullptr || property->quoted) {
        return false;
    }
    if (property->value == "true") {
        value = true;
        return true;
    }
    if (property->value == "false") {
        value = false;
        return true;
    }
    return false;
}

template<typename Integer>
[[nodiscard]] bool read_integer_property(
    const std::vector<JsonProperty>& properties,
    const std::string_view name,
    Integer& value) noexcept
{
    const auto* property = find_property(properties, name);
    if (property == nullptr || property->quoted || property->value.empty()) {
        return false;
    }
    Integer parsed{};
    const auto converted = std::from_chars(
        property->value.data(),
        property->value.data() + property->value.size(),
        parsed,
        10);
    if (converted.ec != std::errc{} ||
        converted.ptr != property->value.data() + property->value.size()) {
        return false;
    }
    value = parsed;
    return true;
}

template<typename Value>
void write_number(std::ostringstream& stream, const std::string_view name, const Value value)
{
    stream << "  \"" << name << "\": " << value << ",\n";
}

void write_boolean(
    std::ostringstream& stream,
    const std::string_view name,
    const bool value,
    const bool final = false)
{
    stream << "  \"" << name << "\": " << (value ? "true" : "false")
           << (final ? "\n" : ",\n");
}

} // namespace

StockRuntimeCaptureLimitValidation validate_stock_runtime_capture_limits(
    const StockRuntimeCaptureLimits& limits) noexcept
{
    if (limits.maximum_duration.count() <= 0) {
        return limit_error(StockRuntimeCaptureLimitErrorCode::zero_limit,
                           "maximum_duration");
    }
    const std::array<std::pair<std::size_t, std::string_view>, 7U> size_limits{{
        {limits.maximum_datagrams, "maximum_datagrams"},
        {limits.maximum_payload_bytes, "maximum_payload_bytes"},
        {limits.maximum_reassembled_bytes, "maximum_reassembled_bytes"},
        {limits.maximum_decompressed_bytes, "maximum_decompressed_bytes"},
        {limits.maximum_message_count, "maximum_message_count"},
        {limits.maximum_runtime_frames, "maximum_runtime_frames"},
        {limits.maximum_client_packets, "maximum_client_packets"},
    }};
    for (const auto& [value, name] : size_limits) {
        if (value == 0U) {
            return limit_error(StockRuntimeCaptureLimitErrorCode::zero_limit,
                               std::string{name});
        }
    }
    if (limits.maximum_server_packets == 0U ||
        limits.maximum_total_raw_bytes == 0U) {
        return limit_error(StockRuntimeCaptureLimitErrorCode::zero_limit,
                           limits.maximum_server_packets == 0U
                               ? "maximum_server_packets"
                               : "maximum_total_raw_bytes");
    }
    if (limits.maximum_duration > StockRuntimeCaptureHardCaps::maximum_duration ||
        limits.maximum_datagrams > StockRuntimeCaptureHardCaps::maximum_datagrams ||
        limits.maximum_total_raw_bytes >
            StockRuntimeCaptureHardCaps::maximum_total_raw_bytes ||
        limits.maximum_payload_bytes >
            StockRuntimeCaptureHardCaps::maximum_payload_bytes ||
        limits.maximum_reassembled_bytes >
            StockRuntimeCaptureHardCaps::maximum_reassembled_bytes ||
        limits.maximum_decompressed_bytes >
            StockRuntimeCaptureHardCaps::maximum_decompressed_bytes ||
        limits.maximum_message_count >
            StockRuntimeCaptureHardCaps::maximum_message_count ||
        limits.maximum_runtime_frames >
            StockRuntimeCaptureHardCaps::maximum_runtime_frames ||
        limits.maximum_client_packets >
            StockRuntimeCaptureHardCaps::maximum_client_packets ||
        limits.maximum_server_packets >
            StockRuntimeCaptureHardCaps::maximum_server_packets) {
        return limit_error(StockRuntimeCaptureLimitErrorCode::hard_cap_exceeded,
                           "capture_limits");
    }
    if (limits.maximum_payload_bytes > limits.maximum_total_raw_bytes) {
        return limit_error(
            StockRuntimeCaptureLimitErrorCode::payload_exceeds_total_raw_bytes,
            "maximum_payload_bytes");
    }
    if (limits.maximum_decompressed_bytes < limits.maximum_reassembled_bytes) {
        return limit_error(
            StockRuntimeCaptureLimitErrorCode::decompressed_smaller_than_reassembled,
            "maximum_decompressed_bytes");
    }
    if (limits.maximum_runtime_frames > limits.maximum_message_count) {
        return limit_error(StockRuntimeCaptureLimitErrorCode::frames_exceed_messages,
                           "maximum_runtime_frames");
    }
    if (limits.maximum_client_packets > limits.maximum_datagrams ||
        limits.maximum_server_packets > limits.maximum_datagrams) {
        return limit_error(
            StockRuntimeCaptureLimitErrorCode::direction_packets_exceed_total,
            "direction_packet_limit");
    }
    return {};
}

std::optional<StockRuntimeCaptureScenario> parse_stock_runtime_capture_scenario(
    const std::string_view value) noexcept
{
    constexpr std::array<StockRuntimeCaptureScenario, 29U> values{
        StockRuntimeCaptureScenario::baseline,
        StockRuntimeCaptureScenario::idle_runtime,
        StockRuntimeCaptureScenario::forward,
        StockRuntimeCaptureScenario::backward,
        StockRuntimeCaptureScenario::left,
        StockRuntimeCaptureScenario::right,
        StockRuntimeCaptureScenario::forward_right,
        StockRuntimeCaptureScenario::jump,
        StockRuntimeCaptureScenario::duck,
        StockRuntimeCaptureScenario::duck_stand,
        StockRuntimeCaptureScenario::yaw_positive,
        StockRuntimeCaptureScenario::yaw_negative,
        StockRuntimeCaptureScenario::pitch_positive,
        StockRuntimeCaptureScenario::pitch_negative,
        StockRuntimeCaptureScenario::second_client,
        StockRuntimeCaptureScenario::reconnect,
        StockRuntimeCaptureScenario::map_change,
        StockRuntimeCaptureScenario::server_restart,
        StockRuntimeCaptureScenario::respawn,
        StockRuntimeCaptureScenario::low_updaterate,
        StockRuntimeCaptureScenario::high_updaterate,
        StockRuntimeCaptureScenario::low_cmdrate,
        StockRuntimeCaptureScenario::high_cmdrate,
        StockRuntimeCaptureScenario::drop_server_runtime,
        StockRuntimeCaptureScenario::drop_two_server_runtime,
        StockRuntimeCaptureScenario::duplicate_server_runtime,
        StockRuntimeCaptureScenario::reorder_server_runtime,
        StockRuntimeCaptureScenario::drop_client_move,
        StockRuntimeCaptureScenario::delay_client_move,
    };
    for (const auto candidate : values) {
        if (to_string(candidate) == value) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::string_view to_string(const StockRuntimeCaptureScenario scenario) noexcept
{
    switch (scenario) {
    case StockRuntimeCaptureScenario::baseline: return "baseline";
    case StockRuntimeCaptureScenario::idle_runtime: return "idle-runtime";
    case StockRuntimeCaptureScenario::forward: return "forward";
    case StockRuntimeCaptureScenario::backward: return "backward";
    case StockRuntimeCaptureScenario::left: return "left";
    case StockRuntimeCaptureScenario::right: return "right";
    case StockRuntimeCaptureScenario::forward_right: return "forward-right";
    case StockRuntimeCaptureScenario::jump: return "jump";
    case StockRuntimeCaptureScenario::duck: return "duck";
    case StockRuntimeCaptureScenario::duck_stand: return "duck-stand";
    case StockRuntimeCaptureScenario::yaw_positive: return "yaw-positive";
    case StockRuntimeCaptureScenario::yaw_negative: return "yaw-negative";
    case StockRuntimeCaptureScenario::pitch_positive: return "pitch-positive";
    case StockRuntimeCaptureScenario::pitch_negative: return "pitch-negative";
    case StockRuntimeCaptureScenario::second_client: return "second-client";
    case StockRuntimeCaptureScenario::reconnect: return "reconnect";
    case StockRuntimeCaptureScenario::map_change: return "map-change";
    case StockRuntimeCaptureScenario::server_restart: return "server-restart";
    case StockRuntimeCaptureScenario::respawn: return "respawn";
    case StockRuntimeCaptureScenario::low_updaterate: return "low-updaterate";
    case StockRuntimeCaptureScenario::high_updaterate: return "high-updaterate";
    case StockRuntimeCaptureScenario::low_cmdrate: return "low-cmdrate";
    case StockRuntimeCaptureScenario::high_cmdrate: return "high-cmdrate";
    case StockRuntimeCaptureScenario::drop_server_runtime:
        return "drop-server-runtime";
    case StockRuntimeCaptureScenario::drop_two_server_runtime:
        return "drop-two-server-runtime";
    case StockRuntimeCaptureScenario::duplicate_server_runtime:
        return "duplicate-server-runtime";
    case StockRuntimeCaptureScenario::reorder_server_runtime:
        return "reorder-server-runtime";
    case StockRuntimeCaptureScenario::drop_client_move: return "drop-client-move";
    case StockRuntimeCaptureScenario::delay_client_move: return "delay-client-move";
    }
    return "unknown";
}

StockRuntimeCaptureAction stock_runtime_capture_action(
    const StockRuntimeCaptureScenario scenario,
    const StockRuntimeCaptureDirection direction,
    const std::size_t direction_packet_ordinal,
    const StockRuntimeCapturePerturbation perturbation) noexcept
{
    if (direction == StockRuntimeCaptureDirection::server_to_client) {
        if (scenario == StockRuntimeCaptureScenario::drop_server_runtime &&
            direction_packet_ordinal == perturbation.server_packet_ordinal) {
            return StockRuntimeCaptureAction::drop;
        }
        const bool immediately_after_perturbation =
            direction_packet_ordinal > perturbation.server_packet_ordinal &&
            direction_packet_ordinal - perturbation.server_packet_ordinal == 1U;
        if (scenario == StockRuntimeCaptureScenario::drop_two_server_runtime &&
            (direction_packet_ordinal == perturbation.server_packet_ordinal ||
             immediately_after_perturbation)) {
            return StockRuntimeCaptureAction::drop;
        }
        if (scenario == StockRuntimeCaptureScenario::duplicate_server_runtime &&
            direction_packet_ordinal == perturbation.server_packet_ordinal) {
            return StockRuntimeCaptureAction::duplicate;
        }
        if (scenario == StockRuntimeCaptureScenario::reorder_server_runtime &&
            direction_packet_ordinal == perturbation.server_packet_ordinal) {
            return StockRuntimeCaptureAction::hold_for_reorder;
        }
    } else {
        if (scenario == StockRuntimeCaptureScenario::drop_client_move &&
            direction_packet_ordinal == perturbation.client_packet_ordinal) {
            return StockRuntimeCaptureAction::drop;
        }
        if (scenario == StockRuntimeCaptureScenario::delay_client_move &&
            direction_packet_ordinal == perturbation.client_packet_ordinal) {
            return StockRuntimeCaptureAction::hold_for_delay;
        }
    }
    return StockRuntimeCaptureAction::forward;
}

StockRuntimeCaptureBudgetResult stock_runtime_capture_observe_datagram(
    StockRuntimeCaptureCounters& counters,
    const StockRuntimeCaptureLimits& limits,
    const StockRuntimeCaptureDirection direction,
    const std::size_t payload_bytes) noexcept
{
    if (payload_bytes > limits.maximum_payload_bytes) {
        return {StockRuntimeCaptureBudgetErrorCode::payload_limit};
    }
    if (counters.observed_datagrams >= limits.maximum_datagrams) {
        return {StockRuntimeCaptureBudgetErrorCode::datagram_limit};
    }
    if (counters.observed_raw_bytes > limits.maximum_total_raw_bytes ||
        payload_bytes > limits.maximum_total_raw_bytes -
                            counters.observed_raw_bytes) {
        return {StockRuntimeCaptureBudgetErrorCode::total_raw_byte_limit};
    }
    if (direction == StockRuntimeCaptureDirection::client_to_server &&
        counters.client_packets >= limits.maximum_client_packets) {
        return {StockRuntimeCaptureBudgetErrorCode::client_packet_limit};
    }
    if (direction == StockRuntimeCaptureDirection::server_to_client &&
        counters.server_packets >= limits.maximum_server_packets) {
        return {StockRuntimeCaptureBudgetErrorCode::server_packet_limit};
    }
    ++counters.observed_datagrams;
    counters.observed_raw_bytes += static_cast<std::uint64_t>(payload_bytes);
    if (direction == StockRuntimeCaptureDirection::client_to_server) {
        ++counters.client_packets;
    } else {
        ++counters.server_packets;
    }
    return {};
}

StockRuntimeCaptureBudgetResult stock_runtime_capture_record_emission(
    StockRuntimeCaptureCounters& counters,
    const std::size_t payload_bytes) noexcept
{
    if (counters.emitted_datagrams ==
            (std::numeric_limits<std::size_t>::max)() ||
        payload_bytes > (std::numeric_limits<std::uint64_t>::max)() -
                            counters.emitted_bytes) {
        return {StockRuntimeCaptureBudgetErrorCode::emitted_counter_overflow};
    }
    ++counters.emitted_datagrams;
    counters.emitted_bytes += static_cast<std::uint64_t>(payload_bytes);
    return {};
}

std::string serialize_stock_runtime_capture_metadata(
    const StockRuntimeCaptureMetadata& metadata)
{
    std::ostringstream stream;
    stream << "{\n"
           << "  \"schema\": \"" << kStockRuntimeCaptureMetadataSchema << "\",\n"
           << "  \"profile\": \"" << kStockRuntimePendingProfile << "\",\n"
           << "  \"scenario\": \"" << to_string(metadata.scenario) << "\",\n"
           << "  \"runtime_result\": \"evidence_pending\",\n"
           << "  \"runtime_ready\": \"evidence_pending\",\n"
           << "  \"stock_versions\": \"not_observed_by_capture_executable\",\n";
    write_number(stream, "maximum_duration_ms", metadata.limits.maximum_duration.count());
    write_number(stream, "maximum_datagrams", metadata.limits.maximum_datagrams);
    write_number(stream, "maximum_total_raw_bytes", metadata.limits.maximum_total_raw_bytes);
    write_number(stream, "maximum_payload_bytes", metadata.limits.maximum_payload_bytes);
    write_number(stream, "maximum_reassembled_bytes", metadata.limits.maximum_reassembled_bytes);
    write_number(stream, "maximum_decompressed_bytes", metadata.limits.maximum_decompressed_bytes);
    write_number(stream, "maximum_message_count", metadata.limits.maximum_message_count);
    write_number(stream, "maximum_runtime_frames", metadata.limits.maximum_runtime_frames);
    write_number(stream, "maximum_client_packets", metadata.limits.maximum_client_packets);
    write_number(stream, "maximum_server_packets", metadata.limits.maximum_server_packets);
    write_number(stream, "observed_datagrams", metadata.counters.observed_datagrams);
    write_number(stream, "observed_raw_bytes", metadata.counters.observed_raw_bytes);
    write_number(stream, "client_packets", metadata.counters.client_packets);
    write_number(stream, "server_packets", metadata.counters.server_packets);
    write_number(stream, "emitted_datagrams", metadata.counters.emitted_datagrams);
    write_number(stream, "emitted_bytes", metadata.counters.emitted_bytes);
    write_number(stream, "dropped_datagrams", metadata.counters.dropped_datagrams);
    write_number(stream, "duplicated_datagrams", metadata.counters.duplicated_datagrams);
    write_number(stream, "delayed_datagrams", metadata.counters.delayed_datagrams);
    write_number(stream, "ignored_wrong_source_datagrams",
                 metadata.counters.ignored_wrong_source_datagrams);
    write_number(stream, "perturbation_count", metadata.perturbation_count);
    write_boolean(stream, "bounded_transport_complete", metadata.bounded_transport_complete);
    write_boolean(stream, "byte_preserving", metadata.byte_preserving);
    write_boolean(stream, "private_ipv4_loopback", metadata.private_ipv4_loopback);
    write_boolean(stream, "one_learned_client_endpoint", metadata.one_learned_client_endpoint);
    write_boolean(stream, "one_upstream_socket", metadata.one_upstream_socket);
    write_boolean(stream, "exact_source_validation", metadata.exact_source_validation);
    write_boolean(stream, "payload_rewritten", metadata.payload_rewritten);
    write_boolean(stream, "raw_datagrams_stored", metadata.raw_datagrams_stored);
    write_boolean(stream, "accepted_evidence_run", metadata.accepted_evidence_run, true);
    stream << "}\n";
    return stream.str();
}

StockRuntimeCaptureMetadataParseResult parse_stock_runtime_capture_metadata(
    const std::string_view json)
{
    if (json.empty() || json.size() > 65'536U) {
        return {std::nullopt, "metadata byte length is outside its bound"};
    }
    std::vector<JsonProperty> properties;
    properties.reserve(42U);
    std::string error;
    if (!FlatJsonReader{json}.read(properties, error)) {
        return {std::nullopt, std::move(error)};
    }
    constexpr std::array<std::string_view, 36U> required{
        "schema", "profile", "scenario", "runtime_result", "runtime_ready",
        "stock_versions", "maximum_duration_ms", "maximum_datagrams",
        "maximum_total_raw_bytes", "maximum_payload_bytes",
        "maximum_reassembled_bytes", "maximum_decompressed_bytes",
        "maximum_message_count", "maximum_runtime_frames",
        "maximum_client_packets", "maximum_server_packets",
        "observed_datagrams", "observed_raw_bytes", "client_packets",
        "server_packets", "emitted_datagrams", "emitted_bytes",
        "dropped_datagrams", "duplicated_datagrams", "delayed_datagrams",
        "ignored_wrong_source_datagrams", "perturbation_count",
        "bounded_transport_complete", "byte_preserving",
        "private_ipv4_loopback", "one_learned_client_endpoint",
        "one_upstream_socket", "exact_source_validation", "payload_rewritten",
        "raw_datagrams_stored", "accepted_evidence_run",
    };
    if (properties.size() != required.size()) {
        return {std::nullopt, "metadata property count is not exact"};
    }
    for (const auto name : required) {
        if (find_property(properties, name) == nullptr) {
            return {std::nullopt, "metadata lacks an exact required property"};
        }
    }

    std::string_view schema;
    std::string_view profile;
    std::string_view scenario_text;
    std::string_view runtime_result;
    std::string_view runtime_ready;
    std::string_view versions;
    if (!read_string_property(properties, "schema", schema) ||
        !read_string_property(properties, "profile", profile) ||
        !read_string_property(properties, "scenario", scenario_text) ||
        !read_string_property(properties, "runtime_result", runtime_result) ||
        !read_string_property(properties, "runtime_ready", runtime_ready) ||
        !read_string_property(properties, "stock_versions", versions) ||
        schema != kStockRuntimeCaptureMetadataSchema ||
        profile != kStockRuntimePendingProfile ||
        runtime_result != "evidence_pending" || runtime_ready != "evidence_pending" ||
        versions != "not_observed_by_capture_executable") {
        return {std::nullopt, "metadata identity or evidence gate is invalid"};
    }
    const auto scenario = parse_stock_runtime_capture_scenario(scenario_text);
    if (!scenario) {
        return {std::nullopt, "metadata scenario is invalid"};
    }

    StockRuntimeCaptureMetadata value;
    value.scenario = *scenario;
    std::int64_t duration_ms{};
    if (!read_integer_property(properties, "maximum_duration_ms", duration_ms) ||
        duration_ms <= 0 ||
        !read_integer_property(properties, "maximum_datagrams",
                               value.limits.maximum_datagrams) ||
        !read_integer_property(properties, "maximum_total_raw_bytes",
                               value.limits.maximum_total_raw_bytes) ||
        !read_integer_property(properties, "maximum_payload_bytes",
                               value.limits.maximum_payload_bytes) ||
        !read_integer_property(properties, "maximum_reassembled_bytes",
                               value.limits.maximum_reassembled_bytes) ||
        !read_integer_property(properties, "maximum_decompressed_bytes",
                               value.limits.maximum_decompressed_bytes) ||
        !read_integer_property(properties, "maximum_message_count",
                               value.limits.maximum_message_count) ||
        !read_integer_property(properties, "maximum_runtime_frames",
                               value.limits.maximum_runtime_frames) ||
        !read_integer_property(properties, "maximum_client_packets",
                               value.limits.maximum_client_packets) ||
        !read_integer_property(properties, "maximum_server_packets",
                               value.limits.maximum_server_packets)) {
        return {std::nullopt, "metadata capture limits are invalid"};
    }
    value.limits.maximum_duration = std::chrono::milliseconds{duration_ms};
    if (!validate_stock_runtime_capture_limits(value.limits)) {
        return {std::nullopt, "metadata capture limits violate policy"};
    }
    if (!read_integer_property(properties, "observed_datagrams",
                               value.counters.observed_datagrams) ||
        !read_integer_property(properties, "observed_raw_bytes",
                               value.counters.observed_raw_bytes) ||
        !read_integer_property(properties, "client_packets",
                               value.counters.client_packets) ||
        !read_integer_property(properties, "server_packets",
                               value.counters.server_packets) ||
        !read_integer_property(properties, "emitted_datagrams",
                               value.counters.emitted_datagrams) ||
        !read_integer_property(properties, "emitted_bytes",
                               value.counters.emitted_bytes) ||
        !read_integer_property(properties, "dropped_datagrams",
                               value.counters.dropped_datagrams) ||
        !read_integer_property(properties, "duplicated_datagrams",
                               value.counters.duplicated_datagrams) ||
        !read_integer_property(properties, "delayed_datagrams",
                               value.counters.delayed_datagrams) ||
        !read_integer_property(properties, "ignored_wrong_source_datagrams",
                               value.counters.ignored_wrong_source_datagrams) ||
        !read_integer_property(properties, "perturbation_count",
                               value.perturbation_count)) {
        return {std::nullopt, "metadata counters are invalid"};
    }
    if (value.counters.observed_datagrams > value.limits.maximum_datagrams ||
        value.counters.observed_raw_bytes > value.limits.maximum_total_raw_bytes ||
        value.counters.client_packets > value.limits.maximum_client_packets ||
        value.counters.server_packets > value.limits.maximum_server_packets ||
        value.counters.client_packets > value.counters.observed_datagrams ||
        value.counters.server_packets !=
            value.counters.observed_datagrams - value.counters.client_packets) {
        return {std::nullopt, "metadata counters violate capture bounds"};
    }
    if (value.counters.dropped_datagrams > value.counters.observed_datagrams ||
        value.counters.duplicated_datagrams > value.counters.observed_datagrams ||
        value.counters.delayed_datagrams > value.counters.observed_datagrams ||
        value.counters.ignored_wrong_source_datagrams >
            value.limits.maximum_datagrams ||
        value.counters.emitted_datagrams !=
            value.counters.observed_datagrams -
                value.counters.dropped_datagrams +
                value.counters.duplicated_datagrams ||
        value.counters.emitted_bytes >
            value.limits.maximum_total_raw_bytes * 2U ||
        value.perturbation_count !=
            value.counters.dropped_datagrams +
                value.counters.duplicated_datagrams +
                value.counters.delayed_datagrams) {
        return {std::nullopt, "metadata mutation counters are inconsistent"};
    }
    if (!read_bool_property(properties, "bounded_transport_complete",
                            value.bounded_transport_complete) ||
        !read_bool_property(properties, "byte_preserving", value.byte_preserving) ||
        !read_bool_property(properties, "private_ipv4_loopback",
                            value.private_ipv4_loopback) ||
        !read_bool_property(properties, "one_learned_client_endpoint",
                            value.one_learned_client_endpoint) ||
        !read_bool_property(properties, "one_upstream_socket",
                            value.one_upstream_socket) ||
        !read_bool_property(properties, "exact_source_validation",
                            value.exact_source_validation) ||
        !read_bool_property(properties, "payload_rewritten", value.payload_rewritten) ||
        !read_bool_property(properties, "raw_datagrams_stored",
                            value.raw_datagrams_stored) ||
        !read_bool_property(properties, "accepted_evidence_run",
                            value.accepted_evidence_run)) {
        return {std::nullopt, "metadata Boolean policy is invalid"};
    }
    if (!value.byte_preserving || !value.private_ipv4_loopback ||
        !value.one_learned_client_endpoint || !value.one_upstream_socket ||
        !value.exact_source_validation || value.payload_rewritten ||
        !value.raw_datagrams_stored || value.accepted_evidence_run) {
        return {std::nullopt, "metadata attempts to bypass the pending evidence gate"};
    }
    if (value.bounded_transport_complete &&
        (value.counters.observed_datagrams == 0U ||
         value.counters.client_packets == 0U ||
         value.counters.server_packets == 0U ||
         value.counters.ignored_wrong_source_datagrams != 0U)) {
        return {std::nullopt, "complete transport metadata lacks both directions"};
    }
    return {std::move(value), {}};
}

std::string canonical_stock_runtime_capture_structure(
    const StockRuntimeCaptureMetadata& metadata)
{
    std::ostringstream stream;
    stream << kStockRuntimeCaptureMetadataSchema << '|'
           << kStockRuntimePendingProfile << '|'
           << to_string(metadata.scenario) << '|'
           << metadata.counters.observed_datagrams << '|'
           << metadata.counters.client_packets << '|'
           << metadata.counters.server_packets << '|'
           << metadata.counters.emitted_datagrams << '|'
           << metadata.counters.dropped_datagrams << '|'
           << metadata.counters.duplicated_datagrams << '|'
           << metadata.counters.delayed_datagrams << '|'
           << metadata.perturbation_count << '|'
           << (metadata.bounded_transport_complete ? 1 : 0) << '|'
           << "runtime=evidence_pending|authority=evidence_pending|ack=evidence_pending";
    return stream.str();
}

} // namespace hlclient::goldsrc
