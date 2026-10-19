#pragma once

#include <hlclient/core/command_line.hpp>
#include <hlclient/core/external_launch_config.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/app/session_receipt.hpp>

namespace hlclient::app {

// Maps a validated launcher contract to the SAME production gameplay path.
// There is no server path, map override, script, RCON or second-client option.
[[nodiscard]] inline bool apply_external_launch_configuration(
        const core::ExternalLaunchConfiguration& config,
        core::CommandLineOptions& options) {
    if(!options.external_server_client || !core::validate_external_launch_configuration(config).empty()) return false;
    options.connect_endpoint=config.endpoint;
    options.player_name=config.name;
    options.base_directory=config.asset_root;
    options.game_directory="valve";
    options.authentication_provider=core::AuthenticationProviderKind::steam;
    options.authentication_material_file.reset();
    options.steam_api_runtime=config.steam_api_runtime;
    options.resource_consistency_provider.reset(); // normal live path prepares its own provider
    options.stop_after=core::ConnectionStopPoint::live_visual_control;
    options.live_input=core::LiveInputMode::keyboard_mouse;
    options.reference_prediction=true;
    options.live_session_seconds.reset();
    options.live_session_unlimited=true;
    options.renderer=core::RendererBackend::opengl;
    options.video=config.video;
    options.image=config.image;
    options.hud_scale=config.hud_scale;
    options.audio_volume=config.volume;
    options.movement_diagnostics=config.movement_diagnostics;
    options.net_trace=true; // bounded diagnostics, redacted by existing callbacks
    return true;
}

[[nodiscard]] inline constexpr network::NetworkAddress client_bind_endpoint(
        const network::NetworkAddress remote) noexcept {
    return remote.ipv4_host_order()==network::NetworkAddress::loopback(0).ipv4_host_order()
        ? network::NetworkAddress::loopback(0) : network::NetworkAddress{0,0};
}

// One endpoint capability layered on the existing session transport. It never
// opens/closes another socket or changes global firewall/research state.
class EndpointBoundTransport final : public network::IDatagramTransport {
public:
    EndpointBoundTransport(network::IDatagramTransport& transport,
        std::optional<network::NetworkAddress> endpoint) noexcept
        : transport_(transport),endpoint_(endpoint) {}
    [[nodiscard]] network::DatagramLocalAddressResult local_address() const override {
        return transport_.local_address();
    }
    [[nodiscard]] network::DatagramSendResult send_to(const network::NetworkAddress& destination,
        std::span<const std::byte> bytes) override {
        if(endpoint_ && destination!=*endpoint_)
            return {network::DatagramSendStatus::error,"external_endpoint_policy_denied"};
        return transport_.send_to(destination,bytes);
    }
    [[nodiscard]] network::DatagramTransportReceiveResult receive(std::size_t maximum_size) override {
        auto result=transport_.receive(maximum_size);
        const auto source=result.datagram ? std::optional{result.datagram->source} : result.source;
        if(endpoint_ && source && *source!=*endpoint_) return {};
        return result;
    }
private:
    network::IDatagramTransport& transport_;
    std::optional<network::NetworkAddress> endpoint_;
};

[[nodiscard]] inline bool external_half_life_profile(std::string_view game) noexcept {
    return game=="valve";
}

// A failed handshake is the primary failure, not a failed goodbye. The
// receipt still records fatal shutdown/transport cleanup separately. Never
// replace the original diagnostic merely because fatal shutdown is non-success.
[[nodiscard]] inline std::string_view external_terminal_failure_reason(
        std::string_view primary_error, bool close_failed) noexcept {
    if(!primary_error.empty() && primary_error!="none") return primary_error;
    return close_failed ? "disconnect_failed" : "gameplay_receipt_incomplete";
}

[[nodiscard]] inline bool external_gameplay_receipt_ready(const SessionReceiptSnapshot& receipt) noexcept {
    const auto required=kSessionReceiptSignonFacts |
        receipt_fact(SessionReceiptFact::keyboard_mouse) |
        receipt_fact(SessionReceiptFact::usercmd_transmitted) |
        receipt_fact(SessionReceiptFact::world_published) |
        receipt_fact(SessionReceiptFact::gameplay_committed);
    return valid_session_receipt(receipt) && (receipt.fact_mask&required)==required &&
        receipt.milestones.back().has_value() &&
        receipt.generation.map_identifier!="not_observed" &&
        !receipt.counters.usercmd_zero_observed &&
        receipt.counters.new_usercmd_count.value_or(0U)>0U &&
        receipt.counters.transmitted_usercmd_packet_count.value_or(0U)>0U &&
        receipt.counters.server_sample_count.value_or(0U)>0U;
}

// UI status is not acceptance. Durable positive readiness remains separate
// from a healthy user's early cancellation and a normal established close.
[[nodiscard]] inline bool external_close_succeeded(const SessionReceiptSnapshot* receipt,
        bool fatal_error,bool close_failed) noexcept {
    if(fatal_error || close_failed || !receipt || !valid_session_receipt(*receipt) ||
        receipt->stage!=SessionReceiptStage::terminal) return false;
    const auto& close=receipt->close;
    const bool graceful=close.outcome==SessionCloseOutcome::sent_but_unconfirmed ||
        close.outcome==SessionCloseOutcome::transport_acknowledged ||
        close.outcome==SessionCloseOutcome::server_disconnect_confirmed;
    // A user can leave a still-loading session from the console without
    // quitting the application. Clean close is not proof of gameplay readiness.
    if(close.reason==SessionCloseReason::startup_cancel ||
        close.reason==SessionCloseReason::console_disconnect)
        return graceful || close.outcome==SessionCloseOutcome::cancel_without_protocol_disconnect;
    return close.reason==SessionCloseReason::window_close && graceful &&
        external_gameplay_receipt_ready(*receipt);
}

} // namespace hlclient::app
