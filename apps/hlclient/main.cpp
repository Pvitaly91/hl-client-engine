#include <hlclient/app/explicit_file_authentication_provider.hpp>
#include <hlclient/app/precache_manifest_exit_policy.hpp>
#include <hlclient/assets/asset_importer_registry.hpp>
#include <hlclient/assets/asset_manager.hpp>
#include <hlclient/client/client_scene_source.hpp>
#include <hlclient/client/client_world_state.hpp>
#include <hlclient/core/command_line.hpp>
#include <hlclient/core/log.hpp>
#include <hlclient/core/version.hpp>
#include <hlclient/filesystem/game_paths.hpp>
#include <hlclient/filesystem/rooted_file_system.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/local_resource_inventory.hpp>
#include <hlclient/goldsrc/local_resource_mapping.hpp>
#include <hlclient/goldsrc/precache_manifest.hpp>
#include <hlclient/goldsrc/precache_manifest_stage.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_resolver.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>
#include <hlclient/platform/sdl_runtime.hpp>
#include <hlclient/platform/sdl_window.hpp>
#include <hlclient/renderer/null/null_renderer.hpp>
#include <hlclient/renderer/opengl/opengl_renderer.hpp>
#include <hlclient/resource_consistency/prepared_local_resource_consistency_provider.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <variant>
#include <vector>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace {

using hlclient::core::LogLevel;

class BootstrapSceneSource final : public hlclient::client::IClientSceneSource {
public:
    [[nodiscard]] hlclient::client::SceneUpdateResult update(
        const hlclient::client::FrameTime elapsed) override
    {
        world_state_.advance(elapsed);
        return {};
    }

    [[nodiscard]] const hlclient::client::ClientWorldState& world_state() const noexcept override
    {
        return world_state_;
    }

    [[nodiscard]] hlclient::client::ClientWorldState& mutable_world_state() noexcept
    {
        return world_state_;
    }

private:
    hlclient::client::ClientWorldState world_state_;
};

[[nodiscard]] std::vector<std::string> command_line_arguments(
    const int argument_count,
#ifdef _WIN32
    wchar_t* arguments[])
#else
    char* arguments[])
#endif
{
    std::vector<std::string> result;
    if (argument_count > 1) {
        result.reserve(static_cast<std::size_t>(argument_count - 1));
    }
    for (int index = 1; index < argument_count; ++index) {
#ifdef _WIN32
        const std::wstring_view wide_argument{arguments[index]};
        if (wide_argument.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw std::length_error{"Command-line argument is too long"};
        }
        if (wide_argument.empty()) {
            result.emplace_back();
            continue;
        }

        const int wide_size = static_cast<int>(wide_argument.size());
        const int required_size = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide_argument.data(),
            wide_size,
            nullptr,
            0,
            nullptr,
            nullptr);
        if (required_size <= 0) {
            throw std::runtime_error{"Unable to convert a command-line argument to UTF-8"};
        }

        std::string converted(static_cast<std::size_t>(required_size), '\0');
        if (WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                wide_argument.data(),
                wide_size,
                converted.data(),
                required_size,
                nullptr,
                nullptr) != required_size) {
            throw std::runtime_error{"Unable to convert a command-line argument to UTF-8"};
        }
        result.push_back(std::move(converted));
#else
        result.emplace_back(arguments[index]);
#endif
    }
    return result;
}

[[nodiscard]] std::vector<std::string_view> argument_views(
    const std::vector<std::string>& arguments)
{
    std::vector<std::string_view> result;
    result.reserve(arguments.size());
    for (const auto& argument : arguments) {
        result.emplace_back(argument);
    }
    return result;
}

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view text)
{
#ifdef _WIN32
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error{"Filesystem path is too long"};
    }
    if (text.empty()) {
        return {};
    }

    const int utf8_size = static_cast<int>(text.size());
    const int required_size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        utf8_size,
        nullptr,
        0);
    if (required_size <= 0) {
        throw std::runtime_error{"Unable to convert a UTF-8 filesystem path"};
    }

    std::wstring converted(static_cast<std::size_t>(required_size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            utf8_size,
            converted.data(),
            required_size) != required_size) {
        throw std::runtime_error{"Unable to convert a UTF-8 filesystem path"};
    }
    return std::filesystem::path{std::move(converted)};
#else
    return std::filesystem::path{text};
#endif
}

[[nodiscard]] std::string path_as_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.u8string();
    return std::string{
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size(),
    };
}

void print_version()
{
    std::cout << hlclient::core::kApplicationName << '\n'
              << "Version: " << hlclient::core::kVersion << '\n'
              << "Platform: " << hlclient::core::build_platform() << '\n'
              << std::flush;
}

[[nodiscard]] std::optional<std::uint64_t> smoke_test_frame_limit()
{
    constexpr std::uint64_t maximum_frames = 1'000'000;
#if defined(_MSC_VER)
    char* environment_value = nullptr;
    std::size_t environment_value_size = 0;
    const auto environment_result = ::_dupenv_s(
        &environment_value,
        &environment_value_size,
        "HLCLIENT_SMOKE_TEST_FRAMES");
    if (environment_result != 0) {
        throw std::runtime_error{"Unable to read HLCLIENT_SMOKE_TEST_FRAMES"};
    }
    if (environment_value == nullptr) {
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> owned_environment_value{
        environment_value,
        &std::free};
    const std::string owned_value{owned_environment_value.get()};
    const std::string_view text{owned_value};
#else
    const char* environment_value = std::getenv("HLCLIENT_SMOKE_TEST_FRAMES");
    if (environment_value == nullptr) {
        return std::nullopt;
    }
    const std::string_view text{environment_value};
#endif

    std::uint64_t frames = 0;
    const auto conversion = std::from_chars(text.data(), text.data() + text.size(), frames, 10);
    if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size() || frames == 0 ||
        frames > maximum_frames) {
        throw std::invalid_argument{
            "Invalid HLCLIENT_SMOKE_TEST_FRAMES value (expected 1..1000000)"};
    }
    return frames;
}

void log_renderer_information(const hlclient::renderer::IRenderer& renderer)
{
    const auto& renderer_info = renderer.information();
    hlclient::core::log(LogLevel::info, "Renderer vendor: " + renderer_info.vendor);
    hlclient::core::log(LogLevel::info, "Renderer: " + renderer_info.device);
    hlclient::core::log(LogLevel::info, "Renderer version: " + renderer_info.version);
}

void log_challenge_trace(const hlclient::goldsrc::ChallengeTraceEvent& event)
{
    using Classification = hlclient::goldsrc::ChallengeTraceClassification;
    if (event.classification == Classification::exchange_started ||
        event.classification == Classification::request_send_started ||
        event.classification == Classification::receive_would_block) {
        return;
    }

    std::string direction;
    std::string classification;
    switch (event.classification) {
    case Classification::request_sent:
        direction = "TX";
        classification = "connectionless getchallenge";
        break;
    case Classification::wrong_endpoint_ignored:
        direction = "RX";
        classification = "wrong endpoint ignored";
        break;
    case Classification::response_truncated:
        direction = "RX";
        classification = "truncated connectionless response";
        break;
    case Classification::response_rejected:
        direction = "RX";
        classification = "rejected connectionless response";
        break;
    case Classification::challenge_accepted:
        direction = "RX";
        classification = "connectionless challenge";
        break;
    case Classification::exchange_timed_out:
        classification = "challenge exchange timed out";
        break;
    case Classification::exchange_cancelled:
        classification = "challenge exchange cancelled";
        break;
    case Classification::network_failure:
        classification = "network failure";
        break;
    case Classification::protocol_failure:
        classification = "protocol failure";
        break;
    case Classification::exchange_started:
    case Classification::request_send_started:
    case Classification::receive_would_block:
        return;
    }

    std::string message{"[net] "};
    if (!direction.empty()) {
        message += direction + ' ';
    }
    message += event.endpoint.to_string() + ' ' + classification;
    if (event.datagram_size != 0U) {
        message += ", " + std::to_string(event.datagram_size) + " bytes";
    }
    if (event.attempt != 0U) {
        message += ", attempt " + std::to_string(event.attempt);
    }
    message += ", elapsed " + std::to_string(event.elapsed.count()) + " ms";
    if (!event.escaped_preview.empty()) {
        message += ", preview=" + event.escaped_preview;
    }
    if (!event.context.empty()) {
        message += ", " + event.context;
    }
    hlclient::core::log(LogLevel::info, message);
}

void log_connect_trace(const hlclient::goldsrc::ConnectRequestTraceEvent& event)
{
    if (event.state != hlclient::goldsrc::ConnectRequestStageState::request_sent) {
        return;
    }

    std::string message = "[net] TX " + event.endpoint.to_string() +
                          " connectionless connect request, " +
                          std::to_string(event.datagram_size) + " bytes";
    hlclient::core::log(LogLevel::info, message);
    hlclient::core::log(
        LogLevel::info,
        "[net] protocol=" + std::to_string(event.protocol) +
            " challenge=" + std::to_string(event.challenge) +
            " protocol-info fields=" +
            std::to_string(event.protocol_info_field_names.size()) +
            " bytes=" + std::to_string(event.protocol_info_size) +
            " user-info fields=" + std::to_string(event.user_info_field_names.size()) +
            " bytes=" + std::to_string(event.user_info_size) +
            " authentication=" +
            hlclient::goldsrc::format_authentication_redaction(event.authentication_size));
}

void log_connect_response_trace(const hlclient::goldsrc::ConnectResponseTraceEvent& event)
{
    using Classification = hlclient::goldsrc::ConnectResponseTraceClassification;
    if (event.classification == Classification::wait_started ||
        event.classification == Classification::receive_would_block) {
        return;
    }

    std::string classification;
    switch (event.classification) {
    case Classification::wrong_endpoint_ignored:
        classification = "wrong endpoint ignored";
        break;
    case Classification::unrelated_connectionless_ignored:
        classification = "unrelated connectionless packet ignored";
        break;
    case Classification::connect_accepted:
        classification = "connectionless connect-accepted";
        break;
    case Classification::connect_rejected:
        classification = "connectionless connect-rejected";
        break;
    case Classification::response_truncated:
        classification = "truncated connect response";
        break;
    case Classification::unexpected_sequenced_packet_pending_m2_3:
        classification = "unexpected sequenced packet pending M2.3";
        break;
    case Classification::wait_timed_out:
        classification = "connect-response wait timed out";
        break;
    case Classification::wait_cancelled:
        classification = "connect-response wait cancelled";
        break;
    case Classification::network_failure:
        classification = "connect-response network failure";
        break;
    case Classification::protocol_failure:
        classification = "connect-response protocol failure";
        break;
    case Classification::wait_started:
    case Classification::receive_would_block:
        return;
    }

    std::string message = "[net] RX " + event.endpoint.to_string() + ' ' + classification;
    if (event.datagram_size != 0U) {
        message += ", " + std::to_string(event.datagram_size) + " bytes";
    }
    message += ", elapsed " + std::to_string(event.elapsed.count()) + " ms";
    hlclient::core::log(LogLevel::info, message);
}

void log_netchan_trace(const hlclient::goldsrc::NetchanBootstrapTraceEvent& event)
{
    using Classification = hlclient::goldsrc::NetchanBootstrapTraceClassification;
    if (event.classification == Classification::bootstrap_started ||
        event.classification == Classification::receive_would_block) {
        return;
    }

    const bool transmitted =
        event.classification == Classification::acknowledgement_sent;
    std::string classification;
    switch (event.classification) {
    case Classification::wrong_endpoint_ignored:
        classification = "wrong endpoint ignored";
        break;
    case Classification::sequenced_packet_received:
        classification = "sequenced";
        break;
    case Classification::fragment_received:
        classification = "fragment received";
        break;
    case Classification::normal_transfer_completed:
        classification = "normal fragment transfer complete";
        break;
    case Classification::duplicate_sequence_ignored:
        classification = "duplicate sequence ignored";
        break;
    case Classification::older_sequence_ignored:
        classification = "older sequence ignored";
        break;
    case Classification::payload_ready:
        classification = "opaque payload ready";
        break;
    case Classification::acknowledgement_sent:
        classification = event.fragmented ? "fragment packet" :
                                            "sequenced acknowledgement";
        break;
    case Classification::bootstrap_complete:
        classification = "netchan bootstrap complete";
        break;
    case Classification::datagram_truncated:
        classification = "truncated sequenced datagram";
        break;
    case Classification::normal_transfer_timed_out:
        classification = "normal fragment transfer timed out";
        break;
    case Classification::secondary_stream_pending_m3:
        classification = "unconfirmed secondary fragment stream rejected";
        break;
    case Classification::bootstrap_timed_out:
        classification = "netchan bootstrap timed out";
        break;
    case Classification::bootstrap_cancelled:
        classification = "netchan bootstrap cancelled";
        break;
    case Classification::network_failure:
        classification = "netchan network failure";
        break;
    case Classification::protocol_failure:
        classification = "netchan protocol failure";
        break;
    case Classification::bootstrap_started:
    case Classification::receive_would_block:
        return;
    }

    std::string message = std::string{"[net] "} + (transmitted ? "TX " : "RX ") +
                          event.endpoint.to_string() + ' ' + classification;
    if (event.sequence) {
        message += ", sequence=" + std::to_string(*event.sequence);
    }
    if (event.acknowledgement) {
        message += ", ack=" + std::to_string(*event.acknowledgement);
    }
    message += std::string{", reliable="} + (event.reliable ? "yes" : "no") +
               ", fragment=" + (event.fragmented ? "yes" : "no") +
               ", reliable-ack=" +
               (event.reliable_acknowledgement ? "yes" : "no");
    if (event.datagram_size != 0U) {
        message += ", datagram=" + std::to_string(event.datagram_size) + " bytes";
    }
    if (event.payload_size != 0U) {
        message += ", opaque-payload=" + std::to_string(event.payload_size) + " bytes";
    }
    if (event.fragment_stream) {
        message += std::string{", stream="} +
                   (*event.fragment_stream ==
                            hlclient::goldsrc::NetchanFragmentStream::normal
                        ? "normal"
                        : "unconfirmed-slot-1");
    }
    if (event.local_transfer_id) {
        message += ", transfer=<local:" +
                   std::to_string(*event.local_transfer_id) + '>';
    }
    if (event.fragment_length != 0U) {
        message += ", range=" + std::to_string(event.fragment_offset) + '+' +
                   std::to_string(event.fragment_length);
    }
    if (event.covered_size != 0U || event.transfer_size != 0U) {
        message += ", coverage=" + std::to_string(event.covered_size) + '/' +
                   std::to_string(event.transfer_size);
    }
    if (transmitted) {
        message += ", tx-count=" +
                   std::to_string(event.transmitted_packet_count);
    }
    hlclient::core::log(LogLevel::info, message);
}

void log_initial_signon_trace(
    const hlclient::goldsrc::InitialSignonTraceEvent& event)
{
    using Classification = hlclient::goldsrc::InitialSignonTraceClassification;
    std::string classification;
    switch (event.classification) {
    case Classification::stage_started:
        return;
    case Classification::initial_request_queued:
        classification = "initial request queued";
        break;
    case Classification::initial_request_transmitted:
        classification = "initial request transmitted";
        break;
    case Classification::initial_request_acknowledged:
        classification = "initial request acknowledged";
        break;
    case Classification::service_payload_received:
        classification = "service payload received";
        break;
    case Classification::service_message_decoded:
        classification = "service message decoded";
        break;
    case Classification::signon_boundary_reached:
        classification = "sign-on boundary reached";
        break;
    case Classification::stage_cancelled:
        classification = "stage cancelled";
        break;
    case Classification::stage_timed_out:
        classification = "stage timed out";
        break;
    case Classification::secondary_stream_pending_m3:
        classification = "secondary stream pending M3";
        break;
    case Classification::backpressure:
        classification = "event backpressure";
        break;
    case Classification::network_failure:
        classification = "network failure";
        break;
    case Classification::protocol_failure:
        classification = "protocol failure";
        break;
    }

    std::string message = "[signon] " + classification;
    if (event.request_size != 0U) {
        message += ", request=" + std::to_string(event.request_size) + " bytes";
    }
    if (event.payload_size != 0U) {
        message += ", payload=" + std::to_string(event.payload_size) + " bytes";
    }
    if (event.opcode) {
        message += ", opcode=" +
                   std::to_string(static_cast<unsigned int>(*event.opcode)) +
                   " (" + std::string{hlclient::goldsrc::to_string(*event.opcode)} + ')';
        message += ", offset=" + std::to_string(event.byte_offset);
    }
    if (event.byte_count != 0U) {
        message += ", bytes=" + std::to_string(event.byte_count);
    }
    if (event.service_payload_count != 0U) {
        message += ", payload-count=" +
                   std::to_string(event.service_payload_count);
    }
    if (event.transmitted_packet_count != 0U) {
        message += ", tx-count=" +
                   std::to_string(event.transmitted_packet_count);
    }
    hlclient::core::log(LogLevel::info, message);
}

void log_pre_resource_signon_trace(
    const hlclient::goldsrc::PreResourceSignonTraceEvent& event)
{
    using Classification =
        hlclient::goldsrc::PreResourceSignonTraceClassification;
    std::string classification;
    switch (event.classification) {
    case Classification::stage_started:
        return;
    case Classification::initial_boundary_reached:
        classification = "initial boundary retained";
        break;
    case Classification::server_info_ready:
        classification = "server-info decoded";
        break;
    case Classification::pre_resource_control:
        classification = "pre-resource control decoded";
        break;
    case Classification::pre_resource_boundary_reached:
        classification = "pre-resource boundary reached";
        break;
    case Classification::stage_cancelled:
        classification = "pre-resource stage cancelled";
        break;
    case Classification::stage_timed_out:
        classification = "pre-resource stage timed out";
        break;
    case Classification::secondary_stream_pending_m3:
        classification = "secondary stream pending M3";
        break;
    case Classification::unsupported_message:
        classification = "unsupported pre-resource message";
        break;
    case Classification::backpressure:
        classification = "pre-resource event backpressure";
        break;
    case Classification::network_failure:
        classification = "pre-resource network failure";
        break;
    case Classification::protocol_failure:
        classification = "pre-resource protocol failure";
        break;
    }

    std::string message = "[signon] " + classification;
    if (event.protocol_version) {
        message += ", protocol=" + std::to_string(*event.protocol_version);
    }
    if (event.maximum_clients) {
        message += ", max-clients=" + std::to_string(
            static_cast<unsigned int>(*event.maximum_clients));
    }
    if (event.multi_client_mode) {
        message += std::string{", multi-client="} +
                   (*event.multi_client_mode ? "yes" : "no");
    }
    if (event.opcode) {
        message += ", opcode=" + std::to_string(
            static_cast<unsigned int>(*event.opcode));
        message += ", offset=" + std::to_string(event.byte_offset);
    }
    if (event.byte_count != 0U) {
        message += ", bytes=" + std::to_string(event.byte_count);
    }
    if (event.string_length != 0U) {
        message += ", string-length=" +
                   std::to_string(event.string_length);
    }
    if (event.boundary_direction) {
        message += ", direction=" + std::string{
            hlclient::goldsrc::to_string(*event.boundary_direction)};
    }
    if (event.evidence_status) {
        message += ", evidence=" + std::string{
            hlclient::goldsrc::to_string(*event.evidence_status)};
    }
    if (event.transmitted_packet_count != 0U) {
        message += ", tx-count=" +
                   std::to_string(event.transmitted_packet_count);
    }
    hlclient::core::log(LogLevel::info, message);
}

void log_delta_description_trace(
    const hlclient::goldsrc::DeltaDescriptionTraceEvent& event)
{
    using Classification =
        hlclient::goldsrc::DeltaDescriptionTraceClassification;
    switch (event.classification) {
    case Classification::stage_started:
    case Classification::pre_resource_boundary_reached:
        return;
    case Classification::delta_schema_decoded:
        hlclient::core::log(
            LogLevel::info,
            "[signon] delta schema decoded: " +
                hlclient::goldsrc::sanitize_service_text_for_presentation(
                    event.schema_name) +
                ", index=" + std::to_string(event.schema_index) +
                ", fields=" + std::to_string(event.field_count) +
                ", bits=" + std::to_string(event.bits_consumed) +
                ", bytes=" + std::to_string(event.bytes_consumed));
        return;
    case Classification::delta_registry_ready:
        hlclient::core::log(
            LogLevel::info,
            "[signon] delta registry ready: schemas=" +
                std::to_string(event.schema_index) +
                ", fields=" + std::to_string(event.field_count) +
                ", bits=" + std::to_string(event.bits_consumed) +
                ", bytes=" + std::to_string(event.bytes_consumed));
        return;
    case Classification::post_delta_boundary_reached:
        hlclient::core::log(
            LogLevel::info,
            "[signon] next boundary opcode=" +
                std::to_string(static_cast<unsigned int>(
                    event.boundary_opcode.value_or(0U))) +
                " offset=" + std::to_string(event.byte_offset));
        return;
    case Classification::stage_cancelled:
        hlclient::core::log(LogLevel::error, "Delta-description stage cancelled");
        return;
    case Classification::stage_timed_out:
        hlclient::core::log(LogLevel::error, "Delta-description stage timed out");
        return;
    case Classification::unsupported_message:
        hlclient::core::log(LogLevel::error, "Unsupported delta-description message");
        return;
    case Classification::backpressure:
        hlclient::core::log(LogLevel::error, "Delta-description event backpressure");
        return;
    case Classification::secondary_stream_pending_m3:
        hlclient::core::log(LogLevel::error, "Secondary stream remains pending M3");
        return;
    case Classification::network_failure:
        hlclient::core::log(LogLevel::error, "Delta-description network failure");
        return;
    case Classification::protocol_failure:
        hlclient::core::log(LogLevel::error, "Delta-description protocol failure");
        return;
    }
}

void log_movement_environment_trace(
    const hlclient::goldsrc::MovementEnvironmentTraceEvent& event)
{
    using Classification =
        hlclient::goldsrc::MovementEnvironmentTraceClassification;
    switch (event.classification) {
    case Classification::stage_started:
    case Classification::delta_boundary_reached:
        return;
    case Classification::movement_environment_ready: {
        std::string message =
            "[signon] movement/environment state decoded";
        if (event.gravity) {
            message += ", gravity=" + std::to_string(*event.gravity);
        }
        if (event.maximum_speed) {
            message += ", max-speed=" + std::to_string(*event.maximum_speed);
        }
        if (event.footsteps) {
            message += std::string{", footsteps="} +
                       (*event.footsteps ? "yes" : "no");
        }
        message += ", sky-name=" +
                   hlclient::goldsrc::sanitize_service_text_for_presentation(
                       event.sky_name);
        message += ", bytes=" + std::to_string(event.byte_count);
        message += ", controls=" + std::to_string(event.control_count);
        hlclient::core::log(LogLevel::info, message);
        return;
    }
    case Classification::post_environment_control:
        hlclient::core::log(
            LogLevel::info,
            "[signon] post-movevars control opcode=" +
                std::to_string(static_cast<unsigned int>(
                    event.opcode.value_or(0U))) +
                " index=" + std::to_string(event.control_index) +
                " offset=" + std::to_string(event.byte_offset) +
                " bytes=" + std::to_string(event.byte_count) +
                " string-length=" + std::to_string(event.string_length));
        return;
    case Classification::post_environment_boundary_reached:
        hlclient::core::log(
            LogLevel::info,
            "[signon] post-movevars boundary opcode=" +
                std::to_string(static_cast<unsigned int>(
                    event.opcode.value_or(0U))) +
                " offset=" + std::to_string(event.byte_offset) +
                " unconsumed-body=" + std::to_string(event.byte_count) +
                " bytes");
        return;
    case Classification::stage_cancelled:
        hlclient::core::log(
            LogLevel::error,
            "Movement/environment stage cancelled");
        return;
    case Classification::stage_timed_out:
        hlclient::core::log(
            LogLevel::error,
            "Movement/environment stage timed out");
        return;
    case Classification::unsupported_message:
        hlclient::core::log(
            LogLevel::error,
            "Unsupported post-movevars service message");
        return;
    case Classification::backpressure:
        hlclient::core::log(
            LogLevel::error,
            "Movement/environment event backpressure");
        return;
    case Classification::secondary_stream_pending_m3:
        hlclient::core::log(
            LogLevel::error,
            "Secondary stream remains pending M3");
        return;
    case Classification::network_failure:
        hlclient::core::log(
            LogLevel::error,
            "Movement/environment network failure");
        return;
    case Classification::protocol_failure:
        hlclient::core::log(
            LogLevel::error,
            "Movement/environment protocol failure");
        return;
    }
}

void log_user_info_trace(
    const hlclient::goldsrc::UserInfoSignonTraceEvent& event)
{
    using Classification =
        hlclient::goldsrc::UserInfoSignonTraceClassification;
    switch (event.classification) {
    case Classification::stage_started:
    case Classification::movevars_boundary_reached:
        return;
    case Classification::user_info_message_decoded:
        hlclient::core::log(
            LogLevel::info,
            "[signon] user-info message decoded: message-index=" +
                std::to_string(event.message_index) +
                " info-bytes=" + std::to_string(event.info_string_length) +
                " entries=" + std::to_string(event.info_entry_count) +
                " player-name-length=" +
                (event.player_name_length
                     ? std::to_string(*event.player_name_length)
                     : std::string{"unavailable"}));
        return;
    case Classification::first_batch_complete:
        hlclient::core::log(
            LogLevel::info,
            "[signon] user-info messages decoded: count=" +
                std::to_string(event.message_count));
        return;
    case Classification::stage_cancelled:
        hlclient::core::log(LogLevel::error, "User-info stage cancelled");
        return;
    case Classification::stage_timed_out:
        hlclient::core::log(LogLevel::error, "User-info stage timed out");
        return;
    case Classification::unsupported_message:
        hlclient::core::log(LogLevel::error, "Unsupported user-info continuation");
        return;
    case Classification::backpressure:
        hlclient::core::log(LogLevel::error, "User-info event backpressure");
        return;
    case Classification::secondary_stream_pending:
        hlclient::core::log(LogLevel::error, "Secondary stream remains pending");
        return;
    case Classification::network_failure:
        hlclient::core::log(LogLevel::error, "User-info network failure");
        return;
    case Classification::protocol_failure:
        hlclient::core::log(LogLevel::error, "User-info protocol failure");
        return;
    }
}

void log_resource_transition_trace(
    const hlclient::goldsrc::ResourceTransitionTraceEvent& event)
{
    using Classification =
        hlclient::goldsrc::ResourceTransitionTraceClassification;
    switch (event.classification) {
    case Classification::stage_started:
    case Classification::user_info_ready:
        return;
    case Classification::transition_request_queued:
        hlclient::core::log(LogLevel::info, "[resource] transition request queued");
        return;
    case Classification::transition_request_transmitted:
        return;
    case Classification::transition_request_acknowledged:
        hlclient::core::log(
            LogLevel::info,
            "[resource] transition request acknowledged");
        return;
    case Classification::second_service_transfer_received:
        return;
    case Classification::transition_control_decoded:
        hlclient::core::log(
            LogLevel::info,
            "[resource] transition control decoded, bytes=" +
                std::to_string(event.byte_count));
        return;
    case Classification::neutral_opcode43_boundary_reached:
        hlclient::core::log(
            LogLevel::info,
            "[resource] neutral opcode-43 boundary opcode=" +
                std::to_string(static_cast<unsigned int>(
                    event.opcode.value_or(0U))) +
                " offset=" + std::to_string(event.byte_offset));
        return;
    case Classification::stage_cancelled:
        hlclient::core::log(LogLevel::error, "Resource-transition stage cancelled");
        return;
    case Classification::stage_timed_out:
        hlclient::core::log(LogLevel::error, "Resource-transition stage timed out");
        return;
    case Classification::unsupported_message:
        hlclient::core::log(LogLevel::error, "Unsupported resource-transition message");
        return;
    case Classification::backpressure:
        hlclient::core::log(LogLevel::error, "Resource-transition event backpressure");
        return;
    case Classification::secondary_stream_pending:
        hlclient::core::log(LogLevel::error, "Secondary resource stream remains pending");
        return;
    case Classification::network_failure:
        hlclient::core::log(LogLevel::error, "Resource-transition network failure");
        return;
    case Classification::protocol_failure:
        hlclient::core::log(LogLevel::error, "Resource-transition protocol failure");
        return;
    }
}

void log_resource_list_trace(
    const hlclient::goldsrc::ResourceListTraceEvent& event)
{
    using Classification = hlclient::goldsrc::ResourceListTraceClassification;
    switch (event.classification) {
    case Classification::stage_started:
    case Classification::transition_boundary_reached:
    case Classification::post_resource_control:
        return;
    case Classification::resource_list_decoded:
        hlclient::core::log(
            LogLevel::info,
            "[resource] list decoded: entries=" +
                std::to_string(event.resource_count));
        return;
    case Classification::resource_entry_metadata:
        hlclient::core::log(
            LogLevel::info,
            "[resource] entry=" + std::to_string(event.entry_ordinal) +
                " type=" +
                std::string{event.resource_type
                                ? hlclient::goldsrc::to_string(
                                      *event.resource_type)
                                : std::string_view{"unknown"}} +
                " index=" +
                (event.resource_index
                     ? std::to_string(*event.resource_index)
                     : std::string{"unavailable"}) +
                " name-bytes=" +
                std::to_string(event.resource_name_byte_count) +
                " size-code=" +
                (event.resource_size_code
                     ? std::to_string(*event.resource_size_code)
                     : std::string{"unavailable"}) +
                " flags=" +
                (event.resource_flags
                     ? std::to_string(static_cast<unsigned int>(
                           *event.resource_flags))
                     : std::string{"unavailable"}) +
                " offset=" + std::to_string(event.byte_offset) + ":" +
                std::to_string(event.bit_offset));
        return;
    case Classification::post_resource_boundary_reached:
        hlclient::core::log(
            LogLevel::info,
            "[resource] exact post-list boundary offset=" +
                std::to_string(event.byte_offset) + ":" +
                std::to_string(event.bit_offset));
        return;
    case Classification::client_response_required:
        hlclient::core::log(
            LogLevel::info,
            "[resource] stock client response required; metadata only, no response queued");
        return;
    case Classification::unsupported_resource_profile:
        hlclient::core::log(
            LogLevel::error,
            "Unobserved resource-list flags/profile slot is unsupported");
        return;
    case Classification::stage_timed_out:
        hlclient::core::log(LogLevel::error, "Resource-list stage timed out");
        return;
    case Classification::stage_cancelled:
        hlclient::core::log(LogLevel::error, "Resource-list stage cancelled");
        return;
    case Classification::backpressure:
        hlclient::core::log(LogLevel::error, "Resource-list event backpressure");
        return;
    case Classification::secondary_stream_pending:
        hlclient::core::log(LogLevel::error, "Secondary resource stream remains pending");
        return;
    case Classification::network_failure:
        hlclient::core::log(LogLevel::error, "Resource-list network failure");
        return;
    case Classification::protocol_failure:
        hlclient::core::log(LogLevel::error, "Resource-list protocol failure");
        return;
    }
}

void log_resource_client_response_trace(
    const hlclient::goldsrc::ResourceClientResponseTraceEvent& event)
{
    using Classification =
        hlclient::goldsrc::ResourceClientResponseTraceClassification;
    switch (event.classification) {
    case Classification::stage_started:
    case Classification::resource_list_ready:
        return;
    case Classification::resource_response_requirements_ready:
        hlclient::core::log(
            LogLevel::info,
            "[resource] client response requirements determined");
        return;
    case Classification::consistency_provider_required:
        hlclient::core::log(
            LogLevel::error,
            "[resource] a resource-consistency provider is required; response not sent");
        return;
    case Classification::resource_response_ready:
        hlclient::core::log(
            LogLevel::info,
            "[resource] client response ready, bytes=" +
                std::to_string(event.semantic_byte_count));
        return;
    case Classification::resource_response_queued:
        hlclient::core::log(
            LogLevel::info,
            "[resource] client response queued");
        return;
    case Classification::resource_response_transmitted:
        hlclient::core::log(
            LogLevel::debug,
            "[resource] client response transmitted, generation=" +
                (event.reliable_generation
                     ? std::to_string(*event.reliable_generation)
                     : std::string{"unavailable"}) +
                " sequence=" +
                (event.transmit_sequence
                     ? std::to_string(*event.transmit_sequence)
                     : std::string{"unavailable"}));
        return;
    case Classification::resource_response_acknowledged:
        hlclient::core::log(
            LogLevel::info,
            "[resource] client response acknowledged");
        return;
    case Classification::concurrent_tail_observed:
        hlclient::core::log(
            LogLevel::debug,
            "[resource] concurrent tail metadata observed, bytes=" +
                std::to_string(event.payload_byte_count));
        return;
    case Classification::server_continuation_received:
        hlclient::core::log(
            LogLevel::debug,
            "[resource] following server payload received, bytes=" +
                std::to_string(event.payload_byte_count));
        return;
    case Classification::next_server_boundary_reached:
        hlclient::core::log(
            LogLevel::info,
            "[resource] next server boundary opcode=" +
                (event.opcode
                     ? std::to_string(static_cast<unsigned int>(*event.opcode))
                     : std::string{"end-of-payload"}) +
                " offset=0");
        return;
    case Classification::unsupported_response_profile:
        hlclient::core::log(
            LogLevel::error,
            "Unsupported post-resource response profile");
        return;
    case Classification::stage_timed_out:
        hlclient::core::log(
            LogLevel::error,
            "Post-resource response stage timed out");
        return;
    case Classification::stage_cancelled:
        hlclient::core::log(
            LogLevel::error,
            "Post-resource response stage cancelled");
        return;
    case Classification::backpressure:
        hlclient::core::log(
            LogLevel::error,
            "Post-resource response event backpressure");
        return;
    case Classification::secondary_stream_pending:
        hlclient::core::log(
            LogLevel::error,
            "Secondary post-resource stream remains pending");
        return;
    case Classification::network_failure:
        hlclient::core::log(
            LogLevel::error,
            "Post-resource response network failure");
        return;
    case Classification::protocol_failure:
        hlclient::core::log(
            LogLevel::error,
            "Post-resource response protocol failure");
        return;
    }
}

void log_precache_manifest_trace(
    const hlclient::goldsrc::PrecacheManifestTraceEvent& event)
{
    using Classification =
        hlclient::goldsrc::PrecacheManifestTraceClassification;
    switch (event.classification) {
    case Classification::stage_started:
    case Classification::resource_response_boundary_reached:
        return;
    case Classification::local_inventory_ready:
        hlclient::core::log(
            LogLevel::debug,
            "[local-resource] inventory retained for manifest: entries=" +
                std::to_string(event.entry_count));
        return;
    case Classification::precache_manifest_ready:
    case Classification::local_resources_incomplete:
    case Classification::unsafe_local_resources:
    case Classification::unsupported_local_profile:
    case Classification::local_resource_io_error:
        hlclient::core::log(
            LogLevel::debug,
            "[precache] metadata snapshot: entries=" +
                std::to_string(event.entry_count) +
                ", ready=" + std::to_string(event.ready_count) +
                ", metadata-only=" +
                std::to_string(event.metadata_only_count) +
                ", missing=" + std::to_string(event.missing_count) +
                ", unsafe=" + std::to_string(event.unsafe_count) +
                ", unsupported=" +
                std::to_string(event.unsupported_count) +
                ", ambiguous=" + std::to_string(event.ambiguous_count) +
                ", io-error=" + std::to_string(event.io_error_count));
        return;
    case Classification::stage_timed_out:
    case Classification::stage_cancelled:
    case Classification::backpressure:
    case Classification::secondary_stream_pending:
    case Classification::network_failure:
    case Classification::protocol_failure:
        hlclient::core::log(
            LogLevel::error,
            "Precache-manifest stage ended before publication");
        return;
    }
}

[[nodiscard]] int report_handshake_result(
    const hlclient::goldsrc::GoldSrcHandshakeCoordinator& handshake)
{
    using State = hlclient::goldsrc::GoldSrcHandshakeState;
    switch (handshake.state()) {
    case State::challenge_received:
        if (!handshake.challenge()) {
            hlclient::core::log(
                LogLevel::error,
                "Challenge exchange completed without an owned challenge result");
            return 1;
        }
        hlclient::core::log(
            LogLevel::info,
            "Challenge received: " + std::to_string(handshake.challenge()->challenge));
        hlclient::core::log(LogLevel::info, "M1 challenge exchange completed");
        hlclient::core::log(
            LogLevel::info,
            "Connect and sign-on are not implemented yet in challenge-only mode");
        return 0;
    case State::request_sent:
        hlclient::core::log(LogLevel::info, "M2.1 connect request sent exactly once");
        hlclient::core::log(
            LogLevel::info,
            "Server acceptance was not determined; netchan and sign-on were not started");
        return 0;
    case State::accepted: {
        if (!handshake.connect_response() ||
            !std::holds_alternative<hlclient::goldsrc::ConnectAccepted>(
                *handshake.connect_response())) {
            hlclient::core::log(
                LogLevel::error,
                "Connect response completed without an accepted result");
            return 1;
        }
        const auto& accepted =
            std::get<hlclient::goldsrc::ConnectAccepted>(*handshake.connect_response());
        hlclient::core::log(LogLevel::info, "GoldSrc connect response accepted");
        hlclient::core::log(LogLevel::info, "User ID: " + std::to_string(accepted.user_id));
        hlclient::core::log(
            LogLevel::info,
            "Server build: " + std::to_string(accepted.server_build));
        hlclient::core::log(
            LogLevel::info,
            std::string{"Secure: "} + (accepted.secure ? "yes" : "no"));
        hlclient::core::log(
            LogLevel::info,
            "Immediate acceptance parsed; netchan and sign-on were not started");
        return 0;
    }
    case State::rejected: {
        if (!handshake.connect_response() ||
            !std::holds_alternative<hlclient::goldsrc::ConnectRejected>(
                *handshake.connect_response())) {
            hlclient::core::log(
                LogLevel::error,
                "Connect response completed without a rejection result");
            return 1;
        }
        const auto& rejected =
            std::get<hlclient::goldsrc::ConnectRejected>(*handshake.connect_response());
        hlclient::core::log(LogLevel::error, "GoldSrc connection rejected");
        hlclient::core::log(
            LogLevel::error,
            "Reason: " + hlclient::goldsrc::sanitize_connect_rejection_for_presentation(
                              rejected.message));
        return 1;
    }
    case State::connect_response_timed_out:
        hlclient::core::log(LogLevel::error, "GoldSrc connect-response wait timed out");
        return 1;
    case State::netchan_bootstrap_complete: {
        if (!handshake.netchan_bootstrap_result()) {
            hlclient::core::log(
                LogLevel::error,
                "Netchan bootstrap completed without an owned opaque payload");
            return 1;
        }
        const auto& payload = handshake.netchan_bootstrap_result()->payload;
        hlclient::core::log(LogLevel::info, "GoldSrc netchan bootstrap complete");
        hlclient::core::log(
            LogLevel::info,
            "Opaque transport payload: " + std::to_string(payload.bytes.size()) +
                " bytes from sequence " +
                std::to_string(payload.source_sequence.value()));
        hlclient::core::log(
            LogLevel::info,
            "No svc_* messages were interpreted at this stop; use the explicit "
            "signon-boundary mode for bounded M2.4.1 decoding");
        return 0;
    }
    case State::netchan_timed_out:
        hlclient::core::log(LogLevel::error, "GoldSrc netchan bootstrap timed out");
        return 1;
    case State::signon_boundary_reached: {
        if (!handshake.initial_signon_result()) {
            hlclient::core::log(
                LogLevel::error,
                "Initial sign-on completed without an owned boundary result");
            return 1;
        }
        const auto& result = *handshake.initial_signon_result();
        hlclient::core::log(LogLevel::info, "GoldSrc initial sign-on boundary reached");
        hlclient::core::log(
            LogLevel::info,
            "Decoded early service messages: " +
                std::to_string(result.messages.size()));
        hlclient::core::log(
            LogLevel::info,
            "Boundary opcode: " +
                std::to_string(static_cast<unsigned int>(result.boundary.opcode)) +
                " (" +
                std::string{hlclient::goldsrc::to_string(result.boundary.opcode)} +
                "), offset=" + std::to_string(result.boundary.byte_offset) +
                ", unconsumed-body=" +
                std::to_string(result.boundary.remaining_byte_count) + " bytes");
        hlclient::core::log(
            LogLevel::info,
            "No boundary body, resource list, or server command was executed");
        return 0;
    }
    case State::signon_timed_out:
        hlclient::core::log(LogLevel::error, "GoldSrc initial sign-on timed out");
        return 1;
    case State::signon_unsupported_service:
        hlclient::core::log(LogLevel::error, "Unsupported service opcode before sign-on boundary");
        return 1;
    case State::signon_backpressure:
        hlclient::core::log(LogLevel::error, "Initial sign-on event queue reached its hard bound");
        return 1;
    case State::signon_secondary_stream_pending_m3:
        hlclient::core::log(
            LogLevel::error,
            "Unconfirmed secondary netchan stream remains pending M3");
        return 1;
    case State::pre_resource_boundary_reached: {
        if (!handshake.pre_resource_result()) {
            hlclient::core::log(
                LogLevel::error,
                "Pre-resource sign-on completed without an owned typed result");
            return 1;
        }
        const auto& result = *handshake.pre_resource_result();
        const auto& server_info = result.server_info();
        const auto& boundary = result.boundary();
        hlclient::core::log(LogLevel::info, "[signon] server-info decoded");
        hlclient::core::log(
            LogLevel::info,
            "[signon] protocol=" +
                std::to_string(static_cast<std::uint32_t>(
                    server_info.protocol_version())));
        hlclient::core::log(
            LogLevel::info,
            "[signon] max-clients=" +
                std::to_string(static_cast<unsigned int>(
                    server_info.maximum_clients().value())));
        hlclient::core::log(
            LogLevel::info,
            std::string{"[signon] multi-client="} +
                (server_info.multi_client_mode() ? "yes" : "no"));
        hlclient::core::log(
            LogLevel::info,
            "[signon] game=" +
                hlclient::goldsrc::sanitize_service_text_for_presentation(
                    server_info.game_directory()));
        hlclient::core::log(
            LogLevel::info,
            "[signon] map=" +
                hlclient::goldsrc::sanitize_service_text_for_presentation(
                    server_info.map_file_path()));
        hlclient::core::log(
            LogLevel::info,
            "[signon] confirmed-pre-resource-controls=" +
                std::to_string(result.controls().size()));
        hlclient::core::log(
            LogLevel::info,
            "[signon] pre-resource boundary opcode=" +
                std::to_string(static_cast<unsigned int>(boundary.opcode())) +
                " offset=" + std::to_string(boundary.byte_offset()) +
                " unconsumed-body=" +
                std::to_string(boundary.remaining_byte_count()) +
                " bytes direction=" +
                std::string{hlclient::goldsrc::to_string(boundary.direction())} +
                " evidence=" +
                std::string{hlclient::goldsrc::to_string(
                    boundary.evidence_status())});
        hlclient::core::log(
            LogLevel::info,
            "No resource request was sent; the complex boundary body remains untouched");
        return 0;
    }
    case State::pre_resource_timed_out:
        hlclient::core::log(LogLevel::error, "GoldSrc pre-resource sign-on timed out");
        return 1;
    case State::pre_resource_unsupported_message:
        hlclient::core::log(
            LogLevel::error,
            "Unsupported service opcode before the pre-resource boundary");
        return 1;
    case State::pre_resource_backpressure:
        hlclient::core::log(
            LogLevel::error,
            "Pre-resource sign-on event queue reached its hard bound");
        return 1;
    case State::pre_resource_secondary_stream_pending_m3:
        hlclient::core::log(
            LogLevel::error,
            "Unconfirmed secondary netchan stream remains pending M3");
        return 1;
    case State::delta_schemas_ready:
    {
        if (!handshake.delta_description_result()) {
            hlclient::core::log(
                LogLevel::error,
                "Delta-description sign-on completed without an owning result");
            return 1;
        }
        const auto& result = *handshake.delta_description_result();
        const auto& registry = result.registry();
        const auto& boundary = result.boundary();
        for (const auto& schema : registry.schemas()) {
            hlclient::core::log(
                LogLevel::info,
                "[signon] delta schema decoded: " +
                    hlclient::goldsrc::sanitize_service_text_for_presentation(
                        schema.name()) +
                    ", fields=" + std::to_string(schema.field_count()));
        }
        hlclient::core::log(
            LogLevel::info,
            "[signon] delta registry ready: schemas=" +
                std::to_string(registry.schema_count()) +
                ", fields=" + std::to_string(registry.total_field_count()));
        hlclient::core::log(
            LogLevel::info,
            "[signon] next boundary opcode=" +
                std::to_string(static_cast<unsigned int>(boundary.opcode())) +
                " offset=" + std::to_string(boundary.byte_offset()) +
                " unconsumed-body=" +
                std::to_string(boundary.remaining_byte_count()) + " bytes");
        hlclient::core::log(
            LogLevel::info,
            "No post-delta body was parsed and no resource response was sent");
        return 0;
    }
    case State::delta_timed_out:
        hlclient::core::log(LogLevel::error, "GoldSrc delta-schema sign-on timed out");
        return 1;
    case State::delta_unsupported_message:
        hlclient::core::log(LogLevel::error, "Unsupported delta-description message");
        return 1;
    case State::delta_backpressure:
        hlclient::core::log(LogLevel::error, "Delta-description event queue reached its hard bound");
        return 1;
    case State::delta_secondary_stream_pending_m3:
        hlclient::core::log(LogLevel::error, "Unconfirmed secondary stream remains pending M3");
        return 1;
    case State::movement_environment_boundary_reached:
    {
        if (!handshake.movement_environment_result()) {
            hlclient::core::log(
                LogLevel::error,
                "Movement/environment sign-on completed without an owning result");
            return 1;
        }
        const auto& result = *handshake.movement_environment_result();
        const auto& move_vars = result.move_vars();
        const auto& boundary = result.boundary();
        hlclient::core::log(
            LogLevel::info,
            "[signon] movement/environment state decoded");
        hlclient::core::log(
            LogLevel::info,
            "[signon] gravity=" + std::to_string(move_vars.gravity()));
        hlclient::core::log(
            LogLevel::info,
            "[signon] max-speed=" +
                std::to_string(move_vars.maximum_speed()));
        hlclient::core::log(
            LogLevel::info,
            "[signon] acceleration=" +
                std::to_string(move_vars.acceleration()) +
                " air-acceleration=" +
                std::to_string(move_vars.air_acceleration()));
        hlclient::core::log(
            LogLevel::info,
            "[signon] friction=" + std::to_string(move_vars.friction()) +
                " step-size=" + std::to_string(move_vars.step_size()) +
                " max-velocity=" +
                std::to_string(move_vars.maximum_velocity()));
        hlclient::core::log(
            LogLevel::info,
            "[signon] footsteps=" +
                std::string{move_vars.footsteps() ? "yes" : "no"});
        hlclient::core::log(
            LogLevel::info,
            "[signon] sky-name=" +
                hlclient::goldsrc::sanitize_service_text_for_presentation(
                    move_vars.sky_name()));
        hlclient::core::log(
            LogLevel::info,
            "[signon] confirmed-post-movevars-controls=" +
                std::to_string(result.control_count()));
        hlclient::core::log(
            LogLevel::info,
            "[signon] next neutral boundary opcode=" +
                std::to_string(static_cast<unsigned int>(boundary.opcode())) +
                " offset=" + std::to_string(boundary.byte_offset()) +
                " unconsumed-body=" +
                std::to_string(boundary.remaining_byte_count()) + " bytes");
        hlclient::core::log(
            LogLevel::info,
            "Move variables were not applied; the boundary body remains untouched "
            "and no resource response was sent");
        return 0;
    }
    case State::movevars_timed_out:
        hlclient::core::log(
            LogLevel::error,
            "GoldSrc movement/environment sign-on timed out");
        return 1;
    case State::movevars_unsupported_message:
        hlclient::core::log(
            LogLevel::error,
            "Unsupported post-movevars service message");
        return 1;
    case State::movevars_backpressure:
        hlclient::core::log(
            LogLevel::error,
            "Movement/environment event queue reached its hard bound");
        return 1;
    case State::movevars_secondary_stream_pending_m3:
        hlclient::core::log(
            LogLevel::error,
            "Unconfirmed secondary stream remains pending M3");
        return 1;
    case State::user_info_complete:
    {
        if (!handshake.user_info_result()) {
            hlclient::core::log(
                LogLevel::error,
                "User-info sign-on completed without an owning result");
            return 1;
        }
        const auto& result = *handshake.user_info_result();
        hlclient::core::log(
            LogLevel::info,
            "[signon] user-info messages decoded: count=" +
                std::to_string(result.message_count()));
        for (const auto& message : result.messages()) {
            hlclient::core::log(
                LogLevel::info,
                "[signon] user-info info-bytes=" +
                    std::to_string(message.info_string_length()) +
                    " entries=" + std::to_string(message.info_entry_count()) +
                    " player-name-length=" +
                    (message.player_name_length()
                         ? std::to_string(*message.player_name_length())
                         : std::string{"unavailable"}));
        }
        hlclient::core::log(
            LogLevel::info,
            "[signon] first service batch complete at offset=" +
                std::to_string(result.completion().final_byte_offset()) +
                "; no resource-transition request was sent");
        return 0;
    }
    case State::user_info_timed_out:
        hlclient::core::log(LogLevel::error, "GoldSrc user-info stage timed out");
        return 1;
    case State::user_info_unsupported_message:
        hlclient::core::log(LogLevel::error, "Unsupported user-info continuation");
        return 1;
    case State::user_info_backpressure:
        hlclient::core::log(LogLevel::error, "User-info event queue reached its hard bound");
        return 1;
    case State::user_info_secondary_stream_pending:
        hlclient::core::log(LogLevel::error, "Secondary stream remains pending");
        return 1;
    case State::resource_transition_boundary_reached:
    {
        if (!handshake.resource_transition_result()) {
            hlclient::core::log(
                LogLevel::error,
                "Resource transition completed without an owning result");
            return 1;
        }
        const auto& result = *handshake.resource_transition_result();
        hlclient::core::log(
            LogLevel::info,
            "[resource] transition request acknowledged");
        hlclient::core::log(
            LogLevel::info,
            "[resource] transition control decoded, bytes=" +
                std::to_string(result.control().body_bytes()));
        hlclient::core::log(
            LogLevel::info,
            "[resource] neutral opcode-43 boundary opcode=" +
                std::to_string(static_cast<unsigned int>(
                    result.boundary().opcode())) +
                " offset=" + std::to_string(result.boundary().byte_offset()) +
                " unconsumed-body=" +
                std::to_string(result.boundary().remaining_byte_count()) +
                " bytes");
        hlclient::core::log(
            LogLevel::info,
            "Opcode-43 semantics remain evidence-gated; its body was not parsed "
            "and no resource response was sent");
        return 0;
    }
    case State::resource_transition_timed_out:
        hlclient::core::log(LogLevel::error, "GoldSrc resource transition timed out");
        return 1;
    case State::resource_transition_unsupported_message:
        hlclient::core::log(LogLevel::error, "Unsupported resource-transition message");
        return 1;
    case State::resource_transition_backpressure:
        hlclient::core::log(
            LogLevel::error,
            "Resource-transition event queue reached its hard bound");
        return 1;
    case State::resource_transition_secondary_stream_pending:
        hlclient::core::log(LogLevel::error, "Secondary resource stream remains pending");
        return 1;
    case State::resource_list_client_response_required:
    {
        if (!handshake.resource_list_result()) {
            hlclient::core::log(
                LogLevel::error,
                "Resource-list stage completed without an owning result");
            return 1;
        }
        const auto& result = *handshake.resource_list_result();
        const auto& list = result.resource_list();
        std::size_t sound_count = 0U;
        std::size_t model_count = 0U;
        std::size_t decal_count = 0U;
        std::size_t generic_count = 0U;
        std::size_t event_count = 0U;
        for (const auto& entry : list.entries()) {
            switch (entry.type()) {
            case hlclient::goldsrc::ResourceType::sound: ++sound_count; break;
            case hlclient::goldsrc::ResourceType::model: ++model_count; break;
            case hlclient::goldsrc::ResourceType::decal: ++decal_count; break;
            case hlclient::goldsrc::ResourceType::generic: ++generic_count; break;
            case hlclient::goldsrc::ResourceType::event_script: ++event_count; break;
            }
        }
        hlclient::core::log(
            LogLevel::info,
            "[resource] list decoded: entries=" +
                std::to_string(list.resource_count()));
        hlclient::core::log(
            LogLevel::info,
            "[resource] raw size-code sum=" +
                std::to_string(list.total_size_code_sum()));
        hlclient::core::log(
            LogLevel::info,
            "[resource] types=sound:" + std::to_string(sound_count) +
                ",model:" + std::to_string(model_count) +
                ",decal:" + std::to_string(decal_count) +
                ",generic:" + std::to_string(generic_count) +
                ",event_script:" + std::to_string(event_count));
        hlclient::core::log(
            LogLevel::info,
            "[resource] consumed bits=" +
                std::to_string(list.bits_consumed()) + " bytes=" +
                std::to_string(list.bytes_consumed()));
        hlclient::core::log(
            LogLevel::info,
            "[resource] next boundary=end-of-payload offset=" +
                std::to_string(result.boundary().byte_offset()) + ":" +
                std::to_string(result.boundary().bit_offset()));
        hlclient::core::log(
            LogLevel::info,
            "[resource] required stock client response recorded as metadata; "
            "no response was built, queued, or sent");
        return 0;
    }
    case State::resource_list_unsupported_profile:
        hlclient::core::log(
            LogLevel::error,
            "Unobserved resource-list flags/profile slot is unsupported");
        return 1;
    case State::resource_list_timed_out:
        hlclient::core::log(LogLevel::error, "GoldSrc resource-list stage timed out");
        return 1;
    case State::resource_list_backpressure:
        hlclient::core::log(
            LogLevel::error,
            "Resource-list event queue reached its hard bound");
        return 1;
    case State::resource_list_secondary_stream_pending:
        hlclient::core::log(
            LogLevel::error,
            "Secondary resource-list stream remains pending");
        return 1;
    case State::resource_response_boundary_reached:
    {
        if (!handshake.resource_client_response_result()) {
            hlclient::core::log(
                LogLevel::error,
                "Post-resource stage completed without an owning result");
            return 1;
        }
        const auto& result = *handshake.resource_client_response_result();
        const auto& boundary = result.boundary();
        hlclient::core::log(
            LogLevel::info,
            "[resource] neutral opcode-5 response lifecycle completed");
        hlclient::core::log(
            LogLevel::info,
            "[resource] semantic bytes=" +
                std::to_string(result.response().bytes_consumed()) +
                " reliable-generation=" +
                std::to_string(
                    result.reliable_lifecycle().reliable_generation()) +
                " transport-sends=" +
                std::to_string(result.reliable_lifecycle().transmit_count()));
        hlclient::core::log(
            LogLevel::info,
            "[resource] next server boundary opcode=" +
                (boundary.opcode()
                     ? std::to_string(
                           static_cast<unsigned int>(*boundary.opcode()))
                     : std::string{"end-of-payload"}) +
                " offset=" + std::to_string(boundary.byte_offset()) +
                " unconsumed-body=" +
                std::to_string(boundary.remaining_byte_count()) + " bytes");
        hlclient::core::log(
            LogLevel::info,
            "The next complex body remains unparsed; no download, cache, "
            "manifest, or asset-loading action was performed");
        return 0;
    }
    case State::resource_response_provider_required:
        hlclient::core::log(
            LogLevel::error,
            "The neutral opcode-5 response requires a configured path-free "
            "resource-consistency provider; no incomplete response was sent");
        return 1;
    case State::resource_response_unsupported_profile:
        hlclient::core::log(
            LogLevel::error,
            "Unsupported post-resource response profile");
        return 1;
    case State::resource_response_timed_out:
        hlclient::core::log(
            LogLevel::error,
            "Post-resource response stage timed out");
        return 1;
    case State::resource_response_backpressure:
        hlclient::core::log(
            LogLevel::error,
            "Post-resource response event queue reached its hard bound");
        return 1;
    case State::resource_response_secondary_stream_pending:
        hlclient::core::log(
            LogLevel::error,
            "Secondary post-resource response stream remains pending");
        return 1;
    case State::precache_manifest_ready:
        if (!handshake.precache_manifest_result()) {
            hlclient::core::log(
                LogLevel::error,
                "Precache-manifest stage completed without an owning result");
            return 1;
        }
        hlclient::core::log(
            LogLevel::info,
            "[precache] same-session metadata-only manifest ready");
        return 0;
    case State::local_resources_incomplete:
        hlclient::core::log(
            LogLevel::error,
            "[precache] manifest published with incomplete local candidates");
        return 1;
    case State::unsafe_local_resources:
        hlclient::core::log(
            LogLevel::error,
            "[precache] manifest published with security-blocked resources");
        return 1;
    case State::unsupported_local_profile:
        hlclient::core::log(
            LogLevel::error,
            "[precache] manifest published with an unsupported local profile");
        return 1;
    case State::local_resource_io_error:
        hlclient::core::log(
            LogLevel::error,
            "[precache] manifest published with local lookup failures");
        return 1;
    case State::timed_out:
        hlclient::core::log(LogLevel::error, "GoldSrc challenge exchange timed out");
        return 1;
    case State::cancelled:
        hlclient::core::log(LogLevel::error, "GoldSrc handshake was cancelled");
        return 1;
    case State::configuration_error:
    case State::network_error:
    case State::protocol_error:
        hlclient::core::log(
            LogLevel::error,
            handshake.error_context().empty()
                ? "GoldSrc handshake failed without diagnostic context"
                : std::string{handshake.error_context()});
        return 1;
    case State::idle:
    case State::waiting_for_challenge:
    case State::building_request:
    case State::request_ready:
    case State::sending_request:
    case State::waiting_for_connect_response:
    case State::waiting_for_netchan:
    case State::waiting_for_signon:
    case State::waiting_for_pre_resource:
    case State::waiting_for_delta_schemas:
    case State::waiting_for_movevars:
    case State::waiting_for_user_info:
    case State::waiting_for_resource_transition:
    case State::waiting_for_resource_list:
    case State::waiting_for_resource_response:
    case State::waiting_for_precache_manifest:
        hlclient::core::log(LogLevel::error, "GoldSrc handshake is not terminal");
        return 1;
    }
    return 1;
}

[[nodiscard]] int report_local_resource_inventory(
    const hlclient::goldsrc::ResourceClientResponseSignonState& response,
    const hlclient::local_resources::LocalResourceEnvironment& environment)
{
    auto built = hlclient::goldsrc::LocalResourceInventoryBuilder{}.build(
        response.resource_list().resource_list(),
        hlclient::goldsrc::GoldSrcResourceNameMapper{},
        environment.resolver());
    if (!built) {
        const auto code =
            built.error
                ? hlclient::goldsrc::to_string(built.error->code)
                : std::string_view{"unable_to_retain_inventory"};
        hlclient::core::log(
            LogLevel::error,
            "[local-resource] inventory build failed: " +
                std::string{code});
        return 1;
    }

    const auto& summary = built.state->summary();
    using Status = hlclient::goldsrc::LocalResourceInventoryStatus;
    const auto unsupported =
        summary.count(Status::unsupported_name_encoding) +
        summary.count(Status::unsupported_mapping);
    hlclient::core::log(
        LogLevel::info,
        "[local-resource] inventory: resolved=" +
            std::to_string(summary.count(Status::resolved)) +
            ", missing=" + std::to_string(summary.count(Status::missing)) +
            ", unsafe=" +
            std::to_string(summary.count(Status::unsafe_name)) +
            ", unsupported=" + std::to_string(unsupported) +
            ", ambiguous=" +
            std::to_string(summary.count(Status::ambiguous)) +
            ", io-error=" +
            std::to_string(summary.count(Status::io_error)));
    return 0;
}

[[nodiscard]] int report_precache_manifest(
    const hlclient::goldsrc::PrecacheManifestSignonState& result)
{
    const auto& manifest = result.manifest();
    const auto& summary = manifest.readiness_summary();
    using Status = hlclient::goldsrc::LocalResourceReadinessStatus;
    const auto unsupported =
        summary.count(Status::unsupported_name_encoding) +
        summary.count(Status::unsupported_mapping);

    hlclient::core::log(
        LogLevel::info,
        "[local-resource] readiness: ready=" +
            std::to_string(summary.count(Status::ready_local_file)) +
            ", metadata-only=" +
            std::to_string(summary.count(Status::metadata_only)) +
            ", missing=" +
            std::to_string(summary.count(Status::missing_local_file)) +
            ", unsafe=" + std::to_string(summary.count(Status::unsafe_name)) +
            ", unsupported=" + std::to_string(unsupported) +
            ", ambiguous=" +
            std::to_string(summary.count(Status::ambiguous_local_match)) +
            ", io-error=" +
            std::to_string(summary.count(Status::local_io_error)));
    hlclient::core::log(
        LogLevel::info,
        "[precache] manifest: entries=" +
            std::to_string(manifest.entry_count()) +
            ", sound-slots=" +
            std::to_string(manifest.sound_slots().slot_count()) +
            ", model-slots=" +
            std::to_string(manifest.model_slots().slot_count()) +
            ", generic-slots=" +
            std::to_string(manifest.generic_slots().slot_count()) +
            ", event-slots=" +
            std::to_string(manifest.event_script_slots().slot_count()) +
            ", decal-slots=" +
            std::to_string(manifest.decal_slots().slot_count()));
    hlclient::core::log(
        LogLevel::info,
        "[precache] world: status=" +
            std::string{hlclient::goldsrc::to_string(
                manifest.world_selection().status())} +
            ", resource-index=" +
            (manifest.world_selection().resource_index()
                 ? std::to_string(
                       *manifest.world_selection().resource_index())
                 : std::string{"unavailable"}));
    hlclient::core::log(
        LogLevel::info,
        "[precache] completeness=" +
            std::string{hlclient::goldsrc::to_string(
                manifest.completeness())});
    return hlclient::app::precache_manifest_exit_code(manifest.completeness());
}

[[nodiscard]] hlclient::network::UdpSocket open_challenge_socket(
    const hlclient::network::NetworkRuntime& runtime,
    const hlclient::network::NetworkAddress& remote_endpoint)
{
    if (!runtime.valid()) {
        throw std::runtime_error{
            "Network runtime initialization failed: " + runtime.error_message()};
    }

    std::string error;
    auto socket = hlclient::network::UdpSocket::open_ipv4(runtime, error);
    if (!socket) {
        throw std::runtime_error{
            error.empty() ? "Unable to open a nonblocking IPv4 UDP socket" : error};
    }

    const auto loopback_address = hlclient::network::NetworkAddress::loopback(0);
    const auto local_address =
        remote_endpoint.ipv4_host_order() == loopback_address.ipv4_host_order()
            ? loopback_address
            : hlclient::network::NetworkAddress{0U, 0U};
    if (!socket->bind(local_address, error)) {
        throw std::runtime_error{
            error.empty() ? "Unable to bind the challenge UDP socket" : error};
    }
    return std::move(*socket);
}

class HandshakeSession final {
public:
    HandshakeSession(
        const hlclient::network::NetworkAddress remote_endpoint,
        const hlclient::goldsrc::HandshakeStopPoint stop_point,
        std::optional<hlclient::goldsrc::PreparedConnectRequest> prepared_request,
        std::optional<hlclient::auth::AuthenticationSession> authentication_session,
        hlclient::resource_consistency::IResourceConsistencyProvider*
            resource_consistency_provider,
        std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>
            local_resource_environment,
        const bool net_trace)
        : local_resource_environment_{
              std::move(local_resource_environment)},
          network_runtime_{},
          transport_{open_challenge_socket(network_runtime_, remote_endpoint)},
          handshake_{
              transport_,
              remote_endpoint,
              stop_point,
              std::move(prepared_request),
              {},
              net_trace ? hlclient::goldsrc::ChallengeTraceCallback{&log_challenge_trace}
                        : hlclient::goldsrc::ChallengeTraceCallback{},
              net_trace ? hlclient::goldsrc::ConnectRequestTraceCallback{&log_connect_trace}
                        : hlclient::goldsrc::ConnectRequestTraceCallback{},
              {},
              net_trace
                  ? hlclient::goldsrc::ConnectResponseTraceCallback{&log_connect_response_trace}
                  : hlclient::goldsrc::ConnectResponseTraceCallback{},
              std::move(authentication_session),
              {},
               net_trace
                   ? hlclient::goldsrc::NetchanBootstrapTraceCallback{&log_netchan_trace}
                   : hlclient::goldsrc::NetchanBootstrapTraceCallback{},
               {},
               net_trace
                   ? hlclient::goldsrc::InitialSignonTraceCallback{&log_initial_signon_trace}
                   : hlclient::goldsrc::InitialSignonTraceCallback{},
               {},
               net_trace
                   ? hlclient::goldsrc::PreResourceSignonTraceCallback{
                         &log_pre_resource_signon_trace}
                   : hlclient::goldsrc::PreResourceSignonTraceCallback{},
               {},
                net_trace
                    ? hlclient::goldsrc::DeltaDescriptionTraceCallback{
                          &log_delta_description_trace}
                    : hlclient::goldsrc::DeltaDescriptionTraceCallback{},
                {},
                net_trace
                    ? hlclient::goldsrc::MovementEnvironmentTraceCallback{
                          &log_movement_environment_trace}
                    : hlclient::goldsrc::MovementEnvironmentTraceCallback{},
                {},
                net_trace
                    ? hlclient::goldsrc::UserInfoSignonTraceCallback{
                          &log_user_info_trace}
                    : hlclient::goldsrc::UserInfoSignonTraceCallback{},
                {},
                net_trace
                    ? hlclient::goldsrc::ResourceTransitionTraceCallback{
                          &log_resource_transition_trace}
                    : hlclient::goldsrc::ResourceTransitionTraceCallback{},
                {},
                net_trace
                    ? hlclient::goldsrc::ResourceListTraceCallback{
                          &log_resource_list_trace}
                    : hlclient::goldsrc::ResourceListTraceCallback{},
                {},
                 resource_consistency_provider,
                 hlclient::goldsrc::ResourceClientResponseTraceCallback{
                     &log_resource_client_response_trace},
                 local_resource_environment_,
                 {},
                 net_trace
                     ? hlclient::goldsrc::PrecacheManifestTraceCallback{
                           &log_precache_manifest_trace}
                     : hlclient::goldsrc::PrecacheManifestTraceCallback{}}
    {
        hlclient::core::log(LogLevel::info, "GoldSrc challenge exchange started");
        hlclient::core::log(LogLevel::info, "Server: " + remote_endpoint.to_string());

        static_cast<void>(handshake_.start(hlclient::goldsrc::ChallengeExchangeClock::now()));
        if (handshake_.local_endpoint()) {
            hlclient::core::log(
                LogLevel::info,
                "Local endpoint: " + handshake_.local_endpoint()->to_string());
        }
    }

    void update(const hlclient::goldsrc::ChallengeExchangeTimePoint now)
    {
        handshake_.update(now);
    }

    void cancel(const hlclient::goldsrc::ChallengeExchangeTimePoint now)
    {
        handshake_.cancel(now);
    }

    [[nodiscard]] bool terminal() const noexcept
    {
        return handshake_.terminal();
    }

    [[nodiscard]] int report_result() const
    {
        const int handshake_result = report_handshake_result(handshake_);
        if (handshake_.precache_manifest_result()) {
            const int manifest_result = report_precache_manifest(
                *handshake_.precache_manifest_result());
            return handshake_result != 0 ? handshake_result : manifest_result;
        }
        if (handshake_result != 0 || !local_resource_environment_ ||
            !handshake_.resource_client_response_result()) {
            return handshake_result;
        }
        return report_local_resource_inventory(
            *handshake_.resource_client_response_result(),
            *local_resource_environment_);
    }

private:
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>
        local_resource_environment_;
    hlclient::network::NetworkRuntime network_runtime_;
    hlclient::network::UdpDatagramTransport transport_;
    hlclient::goldsrc::GoldSrcHandshakeCoordinator handshake_;
};

struct RuntimeConnectPreparation {
    std::optional<hlclient::goldsrc::PreparedConnectRequest> request;
    std::optional<hlclient::auth::AuthenticationSession> authentication_session;
};

[[noreturn]] void throw_authentication_error(const hlclient::auth::AuthenticationError& error)
{
    const auto context = error.context.empty()
                             ? "Explicit authentication provider failed"
                             : error.context;
    switch (error.code) {
    case hlclient::auth::AuthenticationErrorCode::unavailable:
    case hlclient::auth::AuthenticationErrorCode::provider_error:
        throw std::runtime_error{context};
    case hlclient::auth::AuthenticationErrorCode::configuration_error:
    case hlclient::auth::AuthenticationErrorCode::invalid_material:
    case hlclient::auth::AuthenticationErrorCode::material_too_large:
    case hlclient::auth::AuthenticationErrorCode::cancelled:
        throw std::invalid_argument{context};
    }
    throw std::runtime_error{"Explicit authentication provider failed"};
}

[[nodiscard]] RuntimeConnectPreparation prepare_runtime_connect_request(
    const hlclient::core::CommandLineOptions& options,
    const hlclient::network::NetworkAddress& remote_endpoint)
{
    if (options.stop_after == hlclient::core::ConnectionStopPoint::challenge) {
        return {};
    }
    if (!options.authentication_material_file) {
        throw std::invalid_argument{
            "Connect request, response, netchan, and sign-on modes require a local "
            "authentication material file"};
    }

    const auto path = path_from_utf8(*options.authentication_material_file);
    hlclient::app::ExplicitFileAuthenticationProvider provider{path};
    hlclient::auth::AuthenticationRequestContext context;
    context.remote_endpoint = remote_endpoint;
    auto begun = provider.begin(context);
    if (!begun || !begun.operation) {
        throw_authentication_error(
            begun.error.value_or(hlclient::auth::AuthenticationError{
                hlclient::auth::AuthenticationErrorCode::provider_error,
                "Explicit authentication provider could not start",
            }));
    }
    auto update = begun.operation->update();
    if (update.state == hlclient::auth::AuthenticationUpdateState::pending) {
        throw std::runtime_error{
            "Explicit file authentication provider did not complete synchronously"};
    }
    if (update.state != hlclient::auth::AuthenticationUpdateState::succeeded ||
        !update.session) {
        throw_authentication_error(
            update.error.value_or(hlclient::auth::AuthenticationError{
                hlclient::auth::AuthenticationErrorCode::provider_error,
                "Explicit authentication provider returned no session",
            }));
    }

    auto authentication_session = std::move(*update.session);
    auto authentication = authentication_session.take_material();
    if (!authentication) {
        throw std::runtime_error{
            "Explicit authentication session returned no material"};
    }
    hlclient::goldsrc::ClientConnectionSettings settings;
    settings.display_name = options.player_name;
    settings.model = options.player_model;
    auto prepared = hlclient::goldsrc::prepare_connect_request(
        settings,
        std::move(*authentication));
    if (!prepared) {
        throw std::invalid_argument{
            prepared.error ? prepared.error->context
                           : "Unable to prepare the bounded connect request"};
    }
    return RuntimeConnectPreparation{
        std::move(*prepared.value),
        std::move(authentication_session),
    };
}

int run_null_renderer(
    hlclient::client::IClientSceneSource& scene_source,
    const std::optional<std::uint64_t> configured_frame_limit,
    HandshakeSession* const challenge_session)
{
    hlclient::renderer::null::NullRenderer renderer;
    renderer.initialize();
    log_renderer_information(renderer);
    hlclient::core::log(LogLevel::info, "Client bootstrap complete");

    const std::uint64_t frame_limit = configured_frame_limit.value_or(1);
    auto previous_time = std::chrono::steady_clock::now();
    std::uint64_t rendered_frames = 0;
    while ((challenge_session == nullptr && rendered_frames < frame_limit) ||
           (challenge_session != nullptr && !challenge_session->terminal())) {
        const auto current_time = std::chrono::steady_clock::now();
        if (challenge_session != nullptr) {
            challenge_session->update(current_time);
        }
        const auto update = scene_source.update(current_time - previous_time);
        if (!update) {
            throw std::runtime_error{"Scene update failed: " + update.error};
        }
        previous_time = current_time;
        renderer.render(hlclient::client::build_render_scene(scene_source.world_state()), {});
        ++rendered_frames;

        if (challenge_session != nullptr && !challenge_session->terminal()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    hlclient::core::log(
        LogLevel::info,
        "Null renderer completed " +
            std::to_string(renderer.statistics().rendered_frames) + " frame(s)");
    renderer.shutdown();
    if (!renderer.statistics().shutdown) {
        throw std::logic_error{"Null renderer failed to shut down"};
    }
    return challenge_session != nullptr ? challenge_session->report_result() : 0;
}

int run_opengl_renderer(
    hlclient::client::IClientSceneSource& scene_source,
    const std::optional<std::uint64_t> frame_limit,
    HandshakeSession* const challenge_session)
{
    [[maybe_unused]] hlclient::platform::SdlRuntime sdl_runtime;
    hlclient::core::log(LogLevel::info, "SDL initialized");

    hlclient::platform::SdlWindow window{
        hlclient::platform::SdlWindowConfig{std::string{hlclient::core::kApplicationName}, 1280, 720}};
    hlclient::core::log(LogLevel::info, "OpenGL context initialized");
    if (!window.vsync_enabled()) {
        hlclient::core::log(LogLevel::warning, "Vertical synchronization is unavailable");
    }

    // Declared after the window so its glad loader is released before the GL context is destroyed.
    hlclient::renderer::opengl::OpenGlRenderer renderer;
    log_renderer_information(renderer);
    hlclient::core::log(LogLevel::info, "Client bootstrap complete");

    auto previous_time = std::chrono::steady_clock::now();
    std::uint64_t rendered_frames = 0;
    bool running = true;
    while (running) {
        hlclient::platform::WindowEvent event;
        while (window.poll_event(event)) {
            if (event.type == hlclient::platform::WindowEventType::quit_requested) {
                running = false;
            }
        }
        if (!running) {
            break;
        }

        const auto current_time = std::chrono::steady_clock::now();
        if (challenge_session != nullptr) {
            challenge_session->update(current_time);
        }
        const auto update = scene_source.update(current_time - previous_time);
        if (!update) {
            throw std::runtime_error{"Scene update failed: " + update.error};
        }
        previous_time = current_time;

        const auto extent = window.pixel_extent();
        renderer.render(
            hlclient::client::build_render_scene(scene_source.world_state()),
            hlclient::renderer::RenderExtent{extent.width, extent.height});
        window.swap_buffers();

        ++rendered_frames;
        if ((challenge_session != nullptr && challenge_session->terminal()) ||
            (frame_limit && rendered_frames >= *frame_limit && challenge_session == nullptr)) {
            running = false;
        }
    }

    if (challenge_session != nullptr) {
        if (!challenge_session->terminal()) {
            challenge_session->cancel(hlclient::goldsrc::ChallengeExchangeClock::now());
        }
        return challenge_session->report_result();
    }
    return 0;
}

int run(const hlclient::core::CommandLineOptions& options)
{
    print_version();
    std::cout << '\n' << std::flush;

    hlclient::assets::AssetImporterRegistries asset_importers;
    std::unique_ptr<hlclient::filesystem::RootedFileSystem> asset_file_system;
    [[maybe_unused]] std::unique_ptr<hlclient::assets::AssetManager> asset_manager;
    if (options.base_directory && !options.resource_consistency_provider) {
        const auto paths = hlclient::filesystem::validate_game_paths(
            path_from_utf8(*options.base_directory),
            path_from_utf8(options.game_directory));
        if (!paths) {
            hlclient::core::log(LogLevel::error, paths.error);
            return 1;
        }
        hlclient::core::log(
            LogLevel::info,
            "Half-Life game directory: " + path_as_utf8(paths.paths->game_directory));

        auto file_system_result =
            hlclient::filesystem::RootedFileSystem::create(paths.paths->game_directory);
        if (!file_system_result) {
            hlclient::core::log(
                LogLevel::error,
                file_system_result.error ? file_system_result.error->context
                                         : "Unable to create the asset filesystem");
            return 1;
        }
        asset_file_system = std::move(file_system_result.file_system);
        asset_manager = std::make_unique<hlclient::assets::AssetManager>(
            *asset_file_system,
            asset_importers);
        hlclient::core::log(
            LogLevel::info,
            "Asset pipeline initialized; no production format importers are registered in M0.1");
    } else if (options.resource_consistency_provider) {
        hlclient::core::log(
            LogLevel::info,
            "Asset pipeline remains separate from local resource-consistency mode");
    } else {
        hlclient::core::log(
            LogLevel::info,
            "No Half-Life basedir selected; starting without game assets");
    }

    BootstrapSceneSource scene_source;
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>
        local_resource_environment;
    std::unique_ptr<
        hlclient::resource_consistency::PreparedLocalResourceConsistencyProvider>
        resource_consistency_provider;
    std::unique_ptr<HandshakeSession> challenge_session;
    if (options.connect_endpoint) {
        const auto address = hlclient::network::NetworkAddress::parse(*options.connect_endpoint);
        if (!address || address->port() == 0) {
            hlclient::core::log(
                LogLevel::error,
                "Invalid IPv4 endpoint for --connect: " + *options.connect_endpoint);
            return 1;
        }

        scene_source.mutable_world_state().set_connection_requested(true);
        if (hlclient::core::requires_local_resource_consistency_preparation(
                options)) {
            const auto base_directory =
                path_from_utf8(*options.base_directory);
            auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(
                base_directory,
                options.game_directory);
            if (!roots) {
                const auto code =
                    roots.error
                        ? hlclient::local_resources::to_string(roots.error->code)
                        : std::string_view{"io_error"};
                hlclient::core::log(
                    LogLevel::error,
                    "[local-resource] root validation failed: " +
                        std::string{code});
                return 1;
            }

            auto environment =
                hlclient::local_resources::LocalResourceEnvironment::create(
                    std::move(*roots.roots));
            if (!environment || !environment.environment) {
                const auto code =
                    environment.error
                        ? hlclient::local_resources::to_string(
                              environment.error->code)
                        : std::string_view{"unable_to_retain_environment"};
                hlclient::core::log(
                    LogLevel::error,
                    "[local-resource] environment creation failed: " +
                        std::string{code});
                return 1;
            }
            local_resource_environment = std::shared_ptr<
                const hlclient::local_resources::LocalResourceEnvironment>{
                std::move(environment.environment)};
            const auto root_count = local_resource_environment->root_count();
            auto provider = hlclient::resource_consistency::
                PreparedLocalResourceConsistencyProvider::prepare(
                    *local_resource_environment);
            if (!provider) {
                const auto code =
                    provider.error
                        ? hlclient::resource_consistency::to_string(
                              provider.error->code)
                        : std::string_view{"provider_error"};
                hlclient::core::log(
                    LogLevel::error,
                    "[local-resource] consistency provider preparation failed: " +
                        std::string{code});
                return 1;
            }

            resource_consistency_provider = std::move(provider.provider);
            hlclient::core::log(
                LogLevel::info,
                "[local-resource] roots validated: count=" +
                    std::to_string(root_count));
            hlclient::core::log(
                LogLevel::info,
                "[local-resource] consistency material ready: byte-count=" +
                    std::to_string(resource_consistency_provider->byte_count()) +
                    ", opaque-bytes=" +
                    std::to_string(
                        resource_consistency_provider->opaque_byte_count()));
        }

        auto preparation = prepare_runtime_connect_request(options, *address);
        hlclient::goldsrc::HandshakeStopPoint stop_point =
            hlclient::goldsrc::HandshakeStopPoint::challenge;
        switch (options.stop_after) {
        case hlclient::core::ConnectionStopPoint::challenge:
            break;
        case hlclient::core::ConnectionStopPoint::connect_request:
            stop_point = hlclient::goldsrc::HandshakeStopPoint::connect_request;
            break;
        case hlclient::core::ConnectionStopPoint::connect_response:
            stop_point = hlclient::goldsrc::HandshakeStopPoint::connect_response;
            break;
        case hlclient::core::ConnectionStopPoint::netchan_bootstrap:
            stop_point = hlclient::goldsrc::HandshakeStopPoint::netchan_bootstrap;
            break;
        case hlclient::core::ConnectionStopPoint::signon_boundary:
            stop_point = hlclient::goldsrc::HandshakeStopPoint::signon_boundary;
            break;
        case hlclient::core::ConnectionStopPoint::pre_resource:
            stop_point = hlclient::goldsrc::HandshakeStopPoint::pre_resource;
            break;
        case hlclient::core::ConnectionStopPoint::delta_schemas:
            stop_point = hlclient::goldsrc::HandshakeStopPoint::delta_schemas;
            break;
        case hlclient::core::ConnectionStopPoint::movevars:
            stop_point = hlclient::goldsrc::HandshakeStopPoint::movevars;
            break;
        case hlclient::core::ConnectionStopPoint::user_info:
            stop_point = hlclient::goldsrc::HandshakeStopPoint::user_info;
            break;
        case hlclient::core::ConnectionStopPoint::resource_list_boundary:
            stop_point =
                hlclient::goldsrc::HandshakeStopPoint::resource_list_boundary;
            break;
        case hlclient::core::ConnectionStopPoint::resource_list:
            stop_point = hlclient::goldsrc::HandshakeStopPoint::resource_list;
            break;
        case hlclient::core::ConnectionStopPoint::resource_response_boundary:
            stop_point =
                hlclient::goldsrc::HandshakeStopPoint::resource_response_boundary;
            break;
        case hlclient::core::ConnectionStopPoint::precache_manifest:
            stop_point = hlclient::goldsrc::HandshakeStopPoint::precache_manifest;
            break;
        }
        challenge_session = std::make_unique<HandshakeSession>(
            *address,
            stop_point,
            std::move(preparation.request),
            std::move(preparation.authentication_session),
            resource_consistency_provider.get(),
            local_resource_environment,
            options.net_trace);
    }

    const auto frame_limit = smoke_test_frame_limit();
    if (options.renderer == hlclient::core::RendererBackend::null) {
        return run_null_renderer(scene_source, frame_limit, challenge_session.get());
    }
    return run_opengl_renderer(scene_source, frame_limit, challenge_session.get());
}

int application_main(const int argument_count,
#ifdef _WIN32
                     wchar_t* arguments[])
#else
                     char* arguments[])
#endif
{
    hlclient::core::initialize_logging(
#if !defined(NDEBUG)
        LogLevel::debug
#else
        LogLevel::info
#endif
    );

    const auto owned_arguments = command_line_arguments(argument_count, arguments);
    const auto arguments_without_program = argument_views(owned_arguments);
    const auto parsed = hlclient::core::parse_command_line(
        std::span<const std::string_view>{arguments_without_program});
    if (!parsed) {
        hlclient::core::log(LogLevel::error, parsed.error);
        std::cerr << hlclient::core::command_line_help();
        return 2;
    }

    if (parsed.options->show_help) {
        std::cout << hlclient::core::command_line_help();
        return 0;
    }
    if (parsed.options->show_version) {
        print_version();
        return 0;
    }

    try {
        return run(*parsed.options);
    } catch (const std::exception& exception) {
        hlclient::core::log(LogLevel::fatal, exception.what());
        return 1;
    }
}

} // namespace

#ifdef _WIN32
int wmain(const int argument_count, wchar_t* arguments[])
#else
int main(const int argument_count, char* arguments[])
#endif
{
    try {
        return application_main(argument_count, arguments);
    } catch (const std::exception& exception) {
        std::cerr << "[fatal] " << exception.what() << '\n';
        return 1;
    }
}
