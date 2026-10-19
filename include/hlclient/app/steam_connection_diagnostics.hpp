#pragma once

#include <hlclient/app/steam_authentication_provider.hpp>
#include <hlclient/app/session_receipt.hpp>
#include <hlclient/core/external_launch_config.hpp>
#include <hlclient/goldsrc/challenge_exchange.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/connect_response_wait.hpp>
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace hlclient::app {

// Deliberately accepts metadata events, never AuthenticationMaterial, packet
// bytes or a Steam/account ID. Each instance belongs to one console attempt.
class SteamConnectionDiagnostics final {
public:
    static constexpr std::size_t maximum_events=32U;
    void observe(const SteamAuthenticationTraceEvent& event,std::uint64_t at_ms) {
        observed_=true;
        if(event.request_context)request_=event.request_context;
        if(event.actual_app_id)app_id_=event.actual_app_id;
        if(event.logged_on)logged_on_=event.logged_on;
        callback_pumps_=(std::max)(callback_pumps_,event.callback_pump_count);
        if(event.initiate_returned_length)initiate_result_=event.initiate_returned_length;
        if(event.error)error_=event.error;
        using S=SteamAuthenticationTraceStatus;
        if(event.status==S::api_initialized)initialized_=true;
        if(event.status==S::material_acquired)material_size_=event.material_size;
        if(event.status==S::session_terminated)terminated_ms_=at_ms;
        if(event.status==S::api_shutdown)shutdown_ms_=at_ms;
        append(to_string(event.status),at_ms,event.phase,event.elapsed);
    }
    void observe(const goldsrc::ChallengeTraceEvent& event,std::uint64_t at_ms) {
        using C=goldsrc::ChallengeTraceClassification;
        if(event.classification==C::request_sent) {
            observed_=true;
            ++challenge_requests_;append("challenge_sent",at_ms);
        } else if(event.classification==C::challenge_accepted) {
            observed_=true;
            challenge_ms_=at_ms;challenge_bytes_=event.datagram_size;
            append("challenge_accepted",at_ms);
        }
        // escaped_preview/context can contain challenge/identity material.
    }
    void observe(const goldsrc::ConnectRequestTraceEvent& event,std::uint64_t at_ms) {
        if(event.state!=goldsrc::ConnectRequestStageState::request_sent)return;
        observed_=true;++connect_requests_;connect_endpoint_=event.endpoint;
        connect_protocol_=event.protocol;connect_bytes_=event.datagram_size;
        protocol_info_bytes_=event.protocol_info_size;user_info_bytes_=event.user_info_size;
        sent_material_size_=event.authentication_suffix_size;
        line_delimiter_=event.authentication_line_delimiter;connect_ms_=at_ms;
        append("connect_sent",at_ms);
        // No field values, challenge nonce or authentication bytes are copied.
    }
    void observe(const goldsrc::ConnectResponseTraceEvent& event,std::uint64_t at_ms) {
        using C=goldsrc::ConnectResponseTraceClassification;
        switch(event.classification) {
        case C::wrong_endpoint_ignored:++wrong_endpoint_responses_;return;
        case C::unrelated_connectionless_ignored:++unrelated_responses_;return;
        case C::connect_accepted:response_="accepted";break;
        case C::connect_rejected:response_="rejected";break;
        case C::wait_timed_out:response_="timeout";break;
        case C::wait_cancelled:response_="cancelled";break;
        case C::network_failure:response_="network_failure";break;
        case C::protocol_failure:response_="protocol_failure";break;
        default:return;
        }
        observed_=true;response_ms_=at_ms;response_bytes_=event.datagram_size;
        response_wait_ms_=event.elapsed.count();
        append(response_,at_ms);
    }
    void rejection(std::string_view text) {
        // Exact bounded known-message classification, not arbitrary server text.
        rejection_="unclassified";
        if(text.size()>512U)return;
        std::string normalized{text};
        for(auto& c:normalized)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
        const auto first=normalized.find_first_not_of(" \r\n\t");
        if(first==normalized.npos)return;
        const auto last=normalized.find_last_not_of(" \r\n\t");
        const auto value=std::string_view{normalized}.substr(first,last-first+1U);
        if(value=="steam validation rejected" || value=="steam validation rejected.")
            rejection_="steam_validation_rejected";
    }
    [[nodiscard]] bool observed() const noexcept {return observed_;}
    [[nodiscard]] std::string encode(const SessionReceiptIdentity& identity) const {
        const auto quote=[](std::string_view value){return core::external_json_string(value);};
        const auto number=[](const auto& value){return value?std::to_string(*value):std::string{"null"};};
        const auto boolean=[](std::optional<bool> value)->std::string_view {
            return value?*value?"true":"false":"null";
        };
        const auto endpoint=[&](const auto& value) {
            return value?quote(value->to_string()):std::string{"null"};
        };
        const std::optional<network::NetworkAddress> auth_endpoint=request_
            ?std::optional{request_->remote_endpoint}:std::nullopt;
        const std::optional<bool> endpoint_match=auth_endpoint && connect_endpoint_
            ?std::optional{*auth_endpoint==*connect_endpoint_}:std::nullopt;
        const std::optional<bool> size_match=material_size_ && sent_material_size_
            ?std::optional{*material_size_==*sent_material_size_}:std::nullopt;
        std::string out="{\"schema\":\"hlclient.steam-connection-diagnostics.v1\",\"run_id\":"+
            quote(identity.run_id)+",\"process_id\":"+std::to_string(identity.process_id)+
            ",\"session_role\":"+quote(identity.role==SessionReceiptRole::B?"B":"A")+
            ",\"process_creation\":"+std::to_string(identity.process_creation)+
            ",\"capture_boundary\":\"attempt_teardown\",\"api\":"+quote(kSteamLegacyAuthenticationApi)+
            ",\"steam_api_initialized\":"+(initialized_?"true":"false")+
            ",\"actual_app_id\":"+number(app_id_)+",\"logged_on\":"+std::string{boolean(logged_on_)}+
            ",\"authentication_endpoint\":"+endpoint(auth_endpoint)+
            ",\"connect_endpoint\":"+endpoint(connect_endpoint_)+
            ",\"endpoint_matches_authentication\":"+std::string{boolean(endpoint_match)}+
            ",\"request_context\":";
        if(request_) {
            const auto& r=*request_;
            out+="{\"protocol\":"+std::to_string(static_cast<std::uint32_t>(r.protocol))+
                ",\"server_id_present\":"+(r.server_id_present?"true":"false")+
                ",\"server_id_nonzero\":"+(r.server_id_nonzero?"true":"false")+
                ",\"server_id_universe\":"+number(r.server_id_universe)+
                ",\"server_id_account_type\":"+number(r.server_id_account_type)+
                ",\"server_id_instance\":"+number(r.server_id_instance)+
                ",\"secure\":"+std::string{boolean(r.secure)}+"}";
        } else out+="null";
        out+=",\"callback_pump_count_during_acquisition\":"+std::to_string(callback_pumps_)+
            ",\"initiate_returned_length\":"+number(initiate_result_)+
            ",\"material_bytes\":"+number(material_size_)+
            ",\"connect_material_bytes\":"+number(sent_material_size_)+
            ",\"material_size_matches_connect\":"+std::string{boolean(size_match)}+
            ",\"authentication_header_has_lf\":"+std::string{boolean(line_delimiter_)}+
            ",\"connect_protocol\":"+number(connect_protocol_)+
            ",\"connect_datagram_bytes\":"+number(connect_bytes_)+
            ",\"protocol_info_bytes\":"+number(protocol_info_bytes_)+
            ",\"user_info_bytes\":"+number(user_info_bytes_)+
            ",\"challenge_requests\":"+std::to_string(challenge_requests_)+
            ",\"challenge_datagram_bytes\":"+number(challenge_bytes_)+
            ",\"challenge_accepted_ms\":"+number(challenge_ms_)+
            ",\"connect_requests\":"+std::to_string(connect_requests_)+
            ",\"connect_sent_ms\":"+number(connect_ms_)+
            ",\"response\":"+quote(response_)+",\"response_ms\":"+number(response_ms_)+
            ",\"response_wait_ms\":"+number(response_wait_ms_)+
            ",\"response_datagram_bytes\":"+number(response_bytes_)+
            ",\"rejection_detail\":"+quote(rejection_)+
            ",\"provider_error\":"+(error_?quote(auth::to_string(*error_)):"null")+
            ",\"session_terminated_ms\":"+number(terminated_ms_)+
            ",\"api_shutdown_ms\":"+number(shutdown_ms_)+
            ",\"wrong_endpoint_responses\":"+std::to_string(wrong_endpoint_responses_)+
            ",\"unrelated_responses\":"+std::to_string(unrelated_responses_)+
            ",\"events_omitted\":"+std::to_string(omitted_)+",\"events\":[";
        for(std::size_t i=0;i<count_;++i) {
            if(i)out+=',';
            const auto& e=events_[i];
            out+="{\"status\":"+quote(e.label)+",\"observed_ms\":"+std::to_string(e.at_ms)+
                ",\"phase\":"+(e.phase?quote(to_string(*e.phase)):"null")+
                ",\"phase_elapsed_ms\":"+(e.phase?std::to_string(e.elapsed.count()):"null")+"}";
        }
        return out+"]}\n";
    }
private:
    void append(std::string_view label,std::uint64_t at_ms,
        std::optional<SteamAuthenticationPhase> phase={},std::chrono::milliseconds elapsed={}) {
        if(count_==events_.size()){++omitted_;return;}
        events_[count_++]={label,at_ms,phase,elapsed};
    }
    struct Event {std::string_view label;std::uint64_t at_ms{};
        std::optional<SteamAuthenticationPhase> phase;std::chrono::milliseconds elapsed{};};
    std::array<Event,maximum_events> events_{};
    std::size_t count_{},omitted_{},callback_pumps_{},challenge_requests_{},connect_requests_{};
    std::size_t wrong_endpoint_responses_{},unrelated_responses_{};
    bool observed_{},initialized_{};
    std::optional<SteamAuthenticationRequestSummary> request_;
    std::optional<network::NetworkAddress> connect_endpoint_;
    std::optional<std::uint32_t> app_id_,connect_protocol_;
    std::optional<bool> logged_on_,line_delimiter_;
    std::optional<int> initiate_result_;
    std::optional<std::size_t> material_size_,sent_material_size_,connect_bytes_,protocol_info_bytes_;
    std::optional<std::size_t> user_info_bytes_,challenge_bytes_,response_bytes_;
    std::optional<std::uint64_t> challenge_ms_,connect_ms_,response_ms_,terminated_ms_,shutdown_ms_;
    std::optional<std::int64_t> response_wait_ms_;
    std::optional<auth::AuthenticationErrorCode> error_;
    std::string_view response_{"not_observed"},rejection_{"not_observed"};
};
} // namespace hlclient::app
