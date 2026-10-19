#pragma once

#include <hlclient/app/session_receipt.hpp>
#include <hlclient/core/external_launch_config.hpp>
#include <algorithm>
#include <optional>
#include <vector>

namespace hlclient::app {
[[nodiscard]] inline bool external_diagnostic_token(std::string_view text) noexcept {
    return !text.empty() && text.size()<=96U && std::ranges::all_of(text,[](unsigned char c) {
        return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_' || c=='-';
    });
}
[[nodiscard]] inline bool external_virtual_name(std::string_view text) noexcept {
    return !text.empty() && text.size()<=256U && text.front()!='/' && text.find("..") == text.npos &&
        std::ranges::all_of(text,[](unsigned char c) {
            return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') ||
                c=='_' || c=='-' || c=='.' || c=='/' || c=='{' || c=='}' || c=='+' || c=='~';
        });
}
// Classifies only known reject semantics; never persists arbitrary server text.
[[nodiscard]] inline std::string_view external_rejection_category(std::string_view text) {
    if(text.size()>512U)return "connection_rejected";
    std::string lower{text};for(auto& c:lower)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    if(lower.find("server is full")!=lower.npos || lower.find("server full")!=lower.npos)return "server_full";
    if(lower.find("password")!=lower.npos)return "server_password_required_or_rejected";
    if(lower.find("protocol")!=lower.npos)return "server_protocol_rejected";
    if(lower.find("steam")!=lower.npos || lower.find("authentication")!=lower.npos)return "server_authentication_rejected";
    return "connection_rejected";
}
struct ExternalSessionDiagnostics {
    struct ResourceIssue {std::string virtual_name,reason;unsigned index{},type{};};
    // Copied only from the owning response-stage diagnostic. No packet bytes,
    // server text or resource-root locators cross this durable boundary.
    struct ResponsePayload {
        std::string classification{"not_observed"},receive_position{"not_observed"};
        std::string wire_encoding{"not_observed"},source_body_kind{"not_observed"};
        std::optional<std::uint32_t> source_sequence,source_acknowledgement,first_transmit_sequence;
        std::size_t payload_ordinal{},wire_byte_count{};
        bool source_reliable{},reassembled{},response_queued{},response_transmitted{},response_acknowledged{};
    };
    std::string game{"not_observed"},map{"not_observed"},reason{"none"};
    std::string protocol_code{"not_observed"};
    std::string usercmd_scheduler_code{"not_observed"},usercmd_adapter_code{"not_observed"};
    std::string usercmd_transmission_code{"not_observed"},usercmd_history_code{"not_observed"};
    std::string usercmd_diagnostic_code{"not_observed"};
    std::string driver_code{"not_observed"},driver_packet_code{"not_observed"};
    std::string driver_session_code{"not_observed"},driver_reassembly_code{"not_observed"};
    std::string driver_native_domain{"not_observed"},driver_datagram_classification{"not_observed"};
    std::optional<std::uint32_t> driver_native_code,driver_sequence,driver_acknowledgement;
    std::optional<std::size_t> driver_datagram_size;
    std::optional<std::size_t> udp_split_parts,udp_split_completed,udp_split_duplicates;
    std::optional<std::size_t> udp_split_rejected,udp_split_expired,udp_split_evicted;
    std::optional<unsigned> driver_connectionless_opcode;
    bool driver_datagram_size_is_lower_bound{};
    std::string failure_phase{"not_observed"},failure_category{"not_observed"},protocol_field{"not_observed"};
    std::optional<std::size_t> protocol_byte_offset;
    std::optional<unsigned> protocol,max_clients,wire_opcode;
    std::optional<std::uint64_t> source_sequence,record_ordinal;
    std::optional<ResponsePayload> response_payload;
    std::vector<ResourceIssue> resources;
    std::size_t omitted{},missing_texture_placeholders{};
    void early_failure(std::string_view phase,std::string_view category,std::string_view code,
        std::optional<std::size_t> offset={},std::string_view field="not_observed") {
        const auto safe=[](std::string_view value) {
            return external_diagnostic_token(value)?std::string{value}:std::string{"rejected_diagnostic"};
        };
        failure_phase=safe(phase);failure_category=safe(category);protocol_code=safe(code);
        protocol_field=safe(field);protocol_byte_offset=offset;
        reason=failure_phase+"_"+failure_category+"_error";
    }
    void resource(std::string_view name,std::string_view status,unsigned index=0,unsigned type=0) {
        if(resources.size()>=32U){++omitted;return;}
        resources.push_back({external_virtual_name(name)?std::string{name}:"rejected_virtual_name",
            external_diagnostic_token(status)?std::string{status}:"rejected_status",index,type});
    }
    // Only typed numeric metadata and enum labels; never a resource URL or
    // a byte read from an unvalidated string/body position.
    void resource_transition_failure(std::string_view code,
        std::optional<std::size_t> error_offset, std::optional<unsigned> opcode,
        std::optional<std::uint64_t> sequence) {
        failure_phase="resource_transition";failure_category="protocol";
        protocol_code=external_diagnostic_token(code)?std::string{code}:"rejected_diagnostic";
        protocol_field="resource_transition_control";protocol_byte_offset=error_offset;
        wire_opcode=opcode;source_sequence=sequence;
    }
    // Numeric decoder cursor only, never payload previews or server text.
    void initial_service_failure(std::string_view code,
        std::optional<std::size_t> error_offset, std::optional<unsigned> opcode,
        std::optional<std::uint64_t> sequence) {
        failure_phase="initial_signon";failure_category="protocol";
        protocol_code=external_diagnostic_token(code)?std::string{code}:"rejected_diagnostic";
        protocol_field="service_message";protocol_byte_offset=error_offset;
        wire_opcode=opcode;source_sequence=sequence;
    }
    // A local bounded-consumer/state limit is not a remote protocol-parser
    // failure. Preserve the actual owner through cascading stage wrappers.
    void initial_capacity_failure(std::string_view code,
        std::optional<std::size_t> error_offset, std::optional<unsigned> opcode,
        std::optional<std::uint64_t> sequence) {
        initial_service_failure(code,error_offset,opcode,sequence);
        failure_category="capacity";protocol_field="event_or_retained_state";
    }
    // Numeric decoder cursor only, never payload previews or server text.
    // Preserve the outer runtime_record_failed reason and wire provenance.
    void runtime_control_failure(std::string_view code,
        std::optional<std::size_t> message_offset) {
        failure_phase="runtime_control";failure_category="protocol";
        protocol_code=external_diagnostic_token(code)?std::string{code}:"rejected_diagnostic";
        protocol_field="service_message";protocol_byte_offset=message_offset;
    }
    [[nodiscard]] std::string encode(const SessionReceiptIdentity& identity) const {
        const auto quote=[](std::string_view token){return core::external_json_string(
            external_diagnostic_token(token)?token:std::string_view{"rejected_diagnostic"});};
        const auto number=[](const auto& value){return value?std::to_string(*value):"null";};
        std::string text="{\"schema\":\"hlclient.external-diagnostics.v1\",\"run_id\":"+
            core::external_json_string(identity.run_id)+",\"process_id\":"+std::to_string(identity.process_id)+
            ",\"process_creation\":"+std::to_string(identity.process_creation)+",\"game\":"+quote(game)+
            ",\"map\":"+core::external_json_string(external_virtual_name(map)?map:"rejected_virtual_name")+
            ",\"protocol\":"+number(protocol)+",\"max_clients\":"+number(max_clients)+
            ",\"reason\":"+quote(reason)+",\"protocol_code\":"+quote(protocol_code)+
            ",\"usercmd_scheduler_code\":"+quote(usercmd_scheduler_code)+
            ",\"usercmd_adapter_code\":"+quote(usercmd_adapter_code)+
            ",\"usercmd_transmission_code\":"+quote(usercmd_transmission_code)+
            ",\"usercmd_history_code\":"+quote(usercmd_history_code)+
            ",\"usercmd_diagnostic_code\":"+quote(usercmd_diagnostic_code)+
            ",\"driver_code\":"+quote(driver_code)+",\"driver_packet_code\":"+quote(driver_packet_code)+
            ",\"driver_session_code\":"+quote(driver_session_code)+",\"driver_reassembly_code\":"+quote(driver_reassembly_code)+
            ",\"driver_native_domain\":"+quote(driver_native_domain)+",\"driver_native_code\":"+number(driver_native_code)+
            ",\"driver_datagram_classification\":"+quote(driver_datagram_classification)+
            ",\"driver_datagram_size\":"+number(driver_datagram_size)+
            ",\"driver_datagram_size_is_lower_bound\":"+(driver_datagram_size_is_lower_bound?"true":"false")+
            ",\"driver_connectionless_opcode\":"+number(driver_connectionless_opcode)+
            ",\"driver_sequence\":"+number(driver_sequence)+",\"driver_acknowledgement\":"+number(driver_acknowledgement)+
            ",\"udp_split_parts\":"+number(udp_split_parts)+",\"udp_split_completed\":"+number(udp_split_completed)+
            ",\"udp_split_duplicates\":"+number(udp_split_duplicates)+",\"udp_split_rejected\":"+number(udp_split_rejected)+
            ",\"udp_split_expired\":"+number(udp_split_expired)+",\"udp_split_evicted\":"+number(udp_split_evicted)+
            ",\"failure_phase\":"+quote(failure_phase)+",\"failure_category\":"+quote(failure_category)+
            ",\"protocol_byte_offset\":"+number(protocol_byte_offset)+",\"protocol_field\":"+quote(protocol_field)+
            ",\"wire_opcode\":"+number(wire_opcode)+",\"source_sequence\":"+number(source_sequence)+
            ",\"record_ordinal\":"+number(record_ordinal)+",\"response_payload\":";
        if(response_payload) {
            const auto& payload=*response_payload;
            text+="{\"classification\":"+quote(payload.classification)+
                ",\"receive_position\":"+quote(payload.receive_position)+
                ",\"wire_encoding\":"+quote(payload.wire_encoding)+
                ",\"source_body_kind\":"+quote(payload.source_body_kind)+
                ",\"source_sequence\":"+number(payload.source_sequence)+
                ",\"source_acknowledgement\":"+number(payload.source_acknowledgement)+
                ",\"first_transmit_sequence\":"+number(payload.first_transmit_sequence)+
                ",\"payload_ordinal\":"+std::to_string(payload.payload_ordinal)+
                ",\"wire_byte_count\":"+std::to_string(payload.wire_byte_count)+
                ",\"source_reliable\":"+(payload.source_reliable?"true":"false")+
                ",\"reassembled\":"+(payload.reassembled?"true":"false")+
                ",\"response_queued\":"+(payload.response_queued?"true":"false")+
                ",\"response_transmitted\":"+(payload.response_transmitted?"true":"false")+
                ",\"response_acknowledged\":"+(payload.response_acknowledged?"true":"false")+"}";
        } else text+="null";
        text+=",\"resources\":[";
        for(std::size_t i=0;i<resources.size() && i<32U;++i) {
            if(i)text+=',';const auto& issue=resources[i];
            text+="{\"virtual_name\":"+core::external_json_string(external_virtual_name(issue.virtual_name)?
                issue.virtual_name:"rejected_virtual_name")+",\"status\":"+quote(issue.reason)+
                ",\"index\":"+std::to_string(issue.index)+",\"type\":"+std::to_string(issue.type)+"}";
        }
        return text+"],\"omitted\":"+std::to_string(omitted)+",\"missing_texture_placeholders\":"+
            std::to_string(missing_texture_placeholders)+"}\n";
    }
};
} // namespace hlclient::app
