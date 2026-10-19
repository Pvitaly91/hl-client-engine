#include <hlclient/app/external_diagnostics.hpp>
#include <hlclient/app/external_observation_gate.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <chrono>
#include <thread>
#include <stdexcept>

TEST_CASE("Runtime control diagnostics retain the typed error and message cursor",
          "[e31][public-server][diagnostics][no-stock]") {
    namespace app=hlclient::app;
    app::ExternalSessionDiagnostics diagnostics;
    const app::SessionReceiptIdentity identity{
        "0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,45,9876};
    diagnostics.reason="runtime_record_failed";
    diagnostics.wire_opcode=46U;diagnostics.source_sequence=21U;diagnostics.record_ordinal=3U;
    diagnostics.runtime_control_failure("unsupported_opcode",19U);
    const auto text=diagnostics.encode(identity);
    CHECK(text.find("\"reason\":\"runtime_record_failed\"")!=text.npos);
    CHECK(text.find("\"failure_phase\":\"runtime_control\"")!=text.npos);
    CHECK(text.find("\"failure_category\":\"protocol\"")!=text.npos);
    CHECK(text.find("\"protocol_code\":\"unsupported_opcode\"")!=text.npos);
    CHECK(text.find("\"protocol_byte_offset\":19")!=text.npos);
    CHECK(text.find("\"protocol_field\":\"service_message\"")!=text.npos);
    CHECK(text.find("\"wire_opcode\":46")!=text.npos);
    CHECK(text.find("\"source_sequence\":21")!=text.npos);
    CHECK(text.find("\"record_ordinal\":3")!=text.npos);
    diagnostics.runtime_control_failure("truncated_body",0U);
    CHECK(diagnostics.encode(identity).find("\"protocol_byte_offset\":0")!=text.npos);
    diagnostics.runtime_control_failure("private server text\nSECRET",std::nullopt);
    const auto rejected=diagnostics.encode(identity);
    CHECK(rejected.find("SECRET")==rejected.npos);
    CHECK(rejected.find("\"protocol_code\":\"rejected_diagnostic\"")!=rejected.npos);
    CHECK(rejected.find("\"protocol_byte_offset\":null")!=rejected.npos);
}

TEST_CASE("Reporting gate avoids unchanged slow sinks and never commits a failed observation",
          "[reporting-cache][e31][no-stock]") {
    hlclient::app::ExternalObservationGate<std::uint64_t> gate;
    std::size_t calls{};
    const auto slow=[&] {++calls;std::this_thread::sleep_for(std::chrono::milliseconds{10});return true;};
    REQUIRE(gate.publish(1U,slow));
    for(std::size_t i=0;i<10000U;++i)REQUIRE(gate.publish(1U,slow));
    CHECK(calls==1U); // No new work queue and no wait for the unchanged sink.
    CHECK_FALSE(gate.publish(2U,[&]{++calls;return false;}));
    CHECK_FALSE(gate.unchanged(2U));CHECK(gate.unchanged(1U));
    CHECK(gate.publish(2U,[&]{++calls;return true;}));CHECK(calls==3U);
    auto reconnected=hlclient::app::ExternalObservationGate<std::uint64_t>{};
    CHECK_FALSE(reconnected.unchanged(2U));
    CHECK(reconnected.publish(2U,[&]{++calls;return true;}));CHECK(calls==4U);
    CHECK_THROWS(gate.publish(3U,[]()->bool{throw std::runtime_error{"sink failed"};}));
    CHECK_FALSE(gate.unchanged(3U));CHECK(gate.unchanged(2U));
}

TEST_CASE("Initial capacity diagnostics retain their true owner and sanitized provenance",
          "[e31][diagnostics][signon-capacity][no-stock]") {
    namespace app=hlclient::app;
    app::ExternalSessionDiagnostics diagnostics;
    const app::SessionReceiptIdentity identity{
        "0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,45,9876};
    diagnostics.initial_capacity_failure("service_event_backpressure",0U,51U,1U);
    diagnostics.reason="response_stage_failed";
    const auto text=diagnostics.encode(identity);
    CHECK(text.find("\"reason\":\"response_stage_failed\"")!=text.npos);
    CHECK(text.find("\"failure_phase\":\"initial_signon\"")!=text.npos);
    CHECK(text.find("\"failure_category\":\"capacity\"")!=text.npos);
    CHECK(text.find("\"protocol_code\":\"service_event_backpressure\"")!=text.npos);
    CHECK(text.find("\"protocol_field\":\"event_or_retained_state\"")!=text.npos);
    CHECK(text.find("\"protocol_byte_offset\":0")!=text.npos);
    CHECK(text.find("\"wire_opcode\":51")!=text.npos);
    CHECK(text.find("\"source_sequence\":1")!=text.npos);
    CHECK(text.find("\"response_payload\":null")!=text.npos);
    diagnostics.initial_capacity_failure("server text\nSECRET",{},{},{});
    const auto rejected=diagnostics.encode(identity);
    CHECK(rejected.find("SECRET")==rejected.npos);
    CHECK(rejected.find("\"protocol_code\":\"rejected_diagnostic\"")!=rejected.npos);
    CHECK(rejected.find("\"wire_opcode\":null")!=rejected.npos);
}

TEST_CASE("Initial service diagnostics retain only sanitized parser metadata",
          "[e31][public-server][diagnostics][no-stock]") {
    namespace app=hlclient::app;
    app::ExternalSessionDiagnostics diagnostics;
    const app::SessionReceiptIdentity identity{
        "0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,45,9876};
    diagnostics.initial_service_failure("unsupported_service_opcode",3U,127U,7U);
    diagnostics.reason="response_stage_failed";
    const auto text=diagnostics.encode(identity);
    CHECK(text.find("\"reason\":\"response_stage_failed\"")!=text.npos);
    CHECK(text.find("\"failure_phase\":\"initial_signon\"")!=text.npos);
    CHECK(text.find("\"failure_category\":\"protocol\"")!=text.npos);
    CHECK(text.find("\"protocol_code\":\"unsupported_service_opcode\"")!=text.npos);
    CHECK(text.find("\"protocol_byte_offset\":3")!=text.npos);
    CHECK(text.find("\"protocol_field\":\"service_message\"")!=text.npos);
    CHECK(text.find("\"wire_opcode\":127")!=text.npos);
    CHECK(text.find("\"source_sequence\":7")!=text.npos);
    CHECK(text.find("\"record_ordinal\":null")!=text.npos);
    diagnostics.initial_service_failure("unsupported_service_opcode",0U,0U,0U);
    const auto zero=diagnostics.encode(identity);
    CHECK(zero.find("\"protocol_byte_offset\":0")!=zero.npos);
    CHECK(zero.find("\"wire_opcode\":0")!=zero.npos);
    CHECK(zero.find("\"source_sequence\":0")!=zero.npos);
    diagnostics.initial_service_failure("server text\nSECRET",{},{},{});
    const auto rejected=diagnostics.encode(identity);
    CHECK(rejected.find("SECRET")==rejected.npos);
    CHECK(rejected.find("\"protocol_code\":\"rejected_diagnostic\"")!=rejected.npos);
    CHECK(rejected.find("\"protocol_byte_offset\":null")!=rejected.npos);
}

TEST_CASE("Appearance reporting compares every encoded field before constructing JSON",
          "[reporting-cache][e31][no-stock]") {
    namespace app=hlclient::app;
    app::ReplayLocalVisualSummary::RemotePlayer original;
    original.entity=1U;original.model_slot=3U;original.network_generation=1U;
    original.map_generation=2U;original.entity_lifetime=4U;
    original.appearance.fields.model="gordon";original.appearance.model_name="gordon";
    original.resolved_visual_model="models/player/gordon/gordon.mdl";
    app::ExternalObservationGate<app::ExternalAppearanceKey> gate;
    std::size_t encodes{};
    REQUIRE(gate.publish(app::external_appearance_key_view(original),[&]{++encodes;return true;}));
    for(std::size_t i=0;i<10000U;++i)
        REQUIRE(gate.publish(app::external_appearance_key_view(original),[&]{++encodes;return true;}));
    CHECK(encodes==1U);
    const auto changed=[&](auto modify) {
        auto candidate=original;modify(candidate);
        CHECK_FALSE(gate.unchanged(app::external_appearance_key_view(candidate)));
    };
    changed([](auto& p){++p.entity;});changed([](auto& p){++p.model_slot;});
    changed([](auto& p){++p.network_generation;});changed([](auto& p){++p.map_generation;});
    changed([](auto& p){++p.entity_lifetime;});changed([](auto& p){++p.appearance.occupant_epoch;});
    changed([](auto& p){++p.appearance.appearance_revision;});
    changed([](auto& p){p.appearance.fields.model="barney";});
    changed([](auto& p){p.appearance.model_name="barney";});
    changed([](auto& p){p.resolved_visual_model="models/player.mdl";});
    changed([](auto& p){p.appearance.status=hlclient::game_api::PlayerAppearanceStatus::selected_model_ready;});
    changed([](auto& p){p.appearance.fields.top_color=std::uint16_t{0U};});
    changed([](auto& p){p.appearance.fields.bottom_color=std::uint16_t{0U};});
    changed([](auto& p){++p.visual_model_resource_id;});changed([](auto& p){p.palette_remapped=true;});
    auto pose_only=original;pose_only.origin[0]=300.0;pose_only.visible=true;
    CHECK(gate.unchanged(app::external_appearance_key_view(pose_only)));
    // Retained keys own their strings, not views into transient presentation.
    original.appearance.fields.model="different";
    CHECK_FALSE(gate.unchanged(app::external_appearance_key_view(original)));
}

TEST_CASE("Video report gate ignores changing kill text but observes HUD layout",
          "[reporting-cache][e31][no-stock]") {
    using Key=hlclient::app::ExternalHudObservationMetrics;
    hlclient::app::ExternalObservationGate<Key> gate;
    Key key{true,640U,1920U,1080U,1.F,1.F,1.F,16.F,1.F,100.F};
    std::size_t writes{};
    REQUIRE(gate.publish(key,[&]{++writes;return true;}));
    key.row_width=550.F;
    REQUIRE(gate.publish(key,[&]{++writes;return true;}));CHECK(writes==1U);
    key.font=18.F;
    REQUIRE(gate.publish(key,[&]{++writes;return true;}));CHECK(writes==2U);
    key.icon=1.5F;
    REQUIRE(gate.publish(key,[&]{++writes;return true;}));CHECK(writes==3U);
    key.width=1280U;
    REQUIRE(gate.publish(key,[&]{++writes;return true;}));CHECK(writes==4U);
}

TEST_CASE("E31 durable diagnostics retain safe resource and protocol context within bounds",
          "[e31][external-session][diagnostics][no-stock]") {
    namespace app=hlclient::app;
    app::ExternalSessionDiagnostics diagnostics;
    diagnostics.game="valve";diagnostics.map="maps/lab_missing.bsp";
    diagnostics.protocol=48;diagnostics.max_clients=24;
    diagnostics.reason="runtime_record_failed";diagnostics.protocol_code="unsupported_opcode";
    diagnostics.wire_opcode=222;diagnostics.source_sequence=901;diagnostics.record_ordinal=7;
    diagnostics.resource("models/missing.mdl","missing",5,2);
    diagnostics.resource("D:\\PRIVATE\\AUTH_MATERIAL", "untrusted status\nAUTH_MATERIAL",0,0);
    for(unsigned i=0;i<40;++i)diagnostics.resource("sprites/fixture.spr","open_failed",i,3);
    REQUIRE(diagnostics.resources.size()==32U);CHECK(diagnostics.omitted==10U);
    const auto text=diagnostics.encode({"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,45,9876});
    CHECK(text.size()<16384U);
    CHECK(text.find("hlclient.external-diagnostics.v1")!=text.npos);
    CHECK(text.find("maps/lab_missing.bsp")!=text.npos);
    CHECK(text.find("models/missing.mdl")!=text.npos);
    CHECK(text.find("\"protocol\":48")!=text.npos);
    CHECK(text.find("\"max_clients\":24")!=text.npos);
    CHECK(text.find("\"wire_opcode\":222")!=text.npos);
    CHECK(text.find("\"source_sequence\":901")!=text.npos);
    CHECK(text.find("\"record_ordinal\":7")!=text.npos);
    CHECK(text.find("\"omitted\":10")!=text.npos);
    CHECK(text.find("AUTH_MATERIAL")==text.npos);CHECK(text.find("PRIVATE")==text.npos);
    CHECK(text.find("rejected_virtual_name")!=text.npos);
    diagnostics.reason="user info password=SECRET";
    CHECK(diagnostics.encode({"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,45,9876}).find("SECRET")==text.npos);
    for(const auto invalid:{"../file.wav","/root/file.wav","C:/root/file.wav","\\\\host\\file","sound/a\n.wav"})
        CHECK_FALSE(app::external_virtual_name(invalid));
    diagnostics.early_failure("challenge","protocol","invalid_terminator",43,"terminator");
    const auto early=diagnostics.encode({"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,45,9876});
    CHECK(early.find("\"failure_phase\":\"challenge\"")!=early.npos);
    CHECK(early.find("\"protocol_field\":\"terminator\"")!=early.npos);
    CHECK(early.find("\"protocol_byte_offset\":43")!=early.npos);
    diagnostics.early_failure("challenge\nSECRET","RAW_USERINFO SECRET","RAW_AUTH SECRET",{},"RAW_PACKET SECRET");
    CHECK(diagnostics.encode({"0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,45,9876}).find("SECRET")==text.npos);
}

TEST_CASE("E31 rejection messages classify typed causes without persisting server text",
          "[e31][external-session][diagnostics][no-stock]") {
    using hlclient::app::external_rejection_category;
    CHECK(external_rejection_category("Server is full.\n")=="server_full");
    CHECK(external_rejection_category("SERVER FULL")=="server_full");
    CHECK(external_rejection_category("Bad password.")=="server_password_required_or_rejected");
    CHECK(external_rejection_category("Incompatible protocol")=="server_protocol_rejected");
    CHECK(external_rejection_category("Steam validation rejected")=="server_authentication_rejected");
    CHECK(external_rejection_category("authentication error")=="server_authentication_rejected");
    CHECK(external_rejection_category("arbitrary private server message")=="connection_rejected");
    CHECK(external_rejection_category(std::string(1024,'x'))=="connection_rejected");
}

TEST_CASE("Resource transition failure retains the exact parser offset and validated opcode",
          "[e31][resource-transition][fastdl][diagnostics][no-stock]") {
    namespace app=hlclient::app;
    const app::SessionReceiptIdentity identity{
        "0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,45,9876};
    app::ExternalSessionDiagnostics diagnostics;
    diagnostics.reason="response_stage_failed";
    diagnostics.protocol_code="resource_list_stage_failed";
    diagnostics.wire_opcode=45U;
    diagnostics.resource_transition_failure("wrong_next_boundary_opcode",27U,55U,13U);
    const auto text=diagnostics.encode(identity);
    CHECK(text.find("\"reason\":\"response_stage_failed\"")!=text.npos);
    CHECK(text.find("\"failure_phase\":\"resource_transition\"")!=text.npos);
    CHECK(text.find("\"protocol_code\":\"wrong_next_boundary_opcode\"")!=text.npos);
    CHECK(text.find("\"protocol_byte_offset\":27")!=text.npos);
    CHECK(text.find("\"protocol_field\":\"resource_transition_control\"")!=text.npos);
    CHECK(text.find("\"wire_opcode\":55")!=text.npos);
    CHECK(text.find("\"source_sequence\":13")!=text.npos);
    // An absent boundary does not leak a stale opcode from an earlier error.
    diagnostics.resource_transition_failure("missing_next_boundary",28U,{},13U);
    CHECK(diagnostics.encode(identity).find("\"wire_opcode\":null")!=text.npos);
    diagnostics.resource_transition_failure("wrong_opcode",0U,0U,0U);
    const auto zero=diagnostics.encode(identity);
    CHECK(zero.find("\"protocol_byte_offset\":0")!=zero.npos);
    CHECK(zero.find("\"wire_opcode\":0")!=zero.npos);
    CHECK(zero.find("\"source_sequence\":0")!=zero.npos);
    diagnostics.resource_transition_failure("https://private.invalid/TOKEN",{},{},{});
    const auto rejected=diagnostics.encode(identity);
    CHECK(rejected.find("TOKEN")==rejected.npos);
    CHECK(rejected.find("private.invalid")==rejected.npos);
    CHECK(rejected.find("\"protocol_code\":\"rejected_diagnostic\"")!=rejected.npos);
}

TEST_CASE("E31 usercmd failure enums survive bounded durable diagnostics without raw context",
          "[e31][e311][external-session][diagnostics][no-stock]") {
    namespace app=hlclient::app;
    app::ExternalSessionDiagnostics diagnostics;
    diagnostics.reason="usercmd_transmission_failed";
    diagnostics.usercmd_transmission_code="history_failed";
    diagnostics.usercmd_history_code="pending_submission";
    diagnostics.usercmd_diagnostic_code="transmit_retention_exhausted";
    const app::SessionReceiptIdentity identity{
        "0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,45,9876};
    const auto text=diagnostics.encode(identity);
    CHECK(text.find("\"usercmd_transmission_code\":\"history_failed\"")!=text.npos);
    CHECK(text.find("\"usercmd_history_code\":\"pending_submission\"")!=text.npos);
    CHECK(text.find("\"usercmd_diagnostic_code\":\"transmit_retention_exhausted\"")!=text.npos);
    CHECK(text.find("\"usercmd_scheduler_code\":\"not_observed\"")!=text.npos);
    CHECK(text.size()<2048U);
    diagnostics.usercmd_scheduler_code="raw context password=SECRET";
    diagnostics.usercmd_adapter_code=std::string(97U,'x');
    diagnostics.usercmd_transmission_code="D:\\PRIVATE\\AUTH_MATERIAL";
    diagnostics.usercmd_history_code="server payload\nSECRET";
    diagnostics.usercmd_diagnostic_code="raw context SECRET";
    const auto rejected=diagnostics.encode(identity);
    CHECK(rejected.find("SECRET")==rejected.npos);
    CHECK(rejected.find("PRIVATE")==rejected.npos);
    CHECK(rejected.find("AUTH_MATERIAL")==rejected.npos);
    CHECK(rejected.find("\"usercmd_history_code\":\"rejected_diagnostic\"")!=rejected.npos);
    CHECK(rejected.find("\"usercmd_diagnostic_code\":\"rejected_diagnostic\"")!=rejected.npos);
}

TEST_CASE("E31 driver failure diagnostics preserve typed packet and native causes without payload text",
          "[e31][e311][driver-failure][diagnostics][no-stock]") {
    hlclient::app::ExternalSessionDiagnostics diagnostics;
    diagnostics.reason="driver_failed";
    diagnostics.driver_code="invalid_acknowledgement";
    diagnostics.driver_session_code="future_acknowledgement";
    diagnostics.driver_datagram_classification="sequenced";
    diagnostics.driver_datagram_size=16U;
    diagnostics.driver_sequence=10U;
    diagnostics.driver_acknowledgement=42U;
    const hlclient::app::SessionReceiptIdentity identity{
        "0123456789abcdef0123456789abcdef",hlclient::app::SessionReceiptRole::A,45,9876};
    const auto packet=diagnostics.encode(identity);
    CHECK(packet.find("\"driver_code\":\"invalid_acknowledgement\"")!=packet.npos);
    CHECK(packet.find("\"driver_session_code\":\"future_acknowledgement\"")!=packet.npos);
    CHECK(packet.find("\"driver_sequence\":10")!=packet.npos);
    CHECK(packet.find("\"driver_acknowledgement\":42")!=packet.npos);
    CHECK(packet.find("\"driver_datagram_size\":16")!=packet.npos);
    diagnostics.driver_native_domain="winsock";
    diagnostics.driver_native_code=10054U;
    diagnostics.driver_datagram_size_is_lower_bound=true;
    diagnostics.driver_connectionless_opcode=66U;
    const auto native=diagnostics.encode(identity);
    CHECK(native.find("\"driver_native_code\":10054")!=native.npos);
    CHECK(native.find("\"driver_datagram_size_is_lower_bound\":true")!=native.npos);
    CHECK(native.find("\"driver_connectionless_opcode\":66")!=native.npos);
    diagnostics.driver_code="raw AUTH_MATERIAL";
    diagnostics.driver_native_domain="D:\\PRIVATE";
    diagnostics.driver_datagram_classification="packet bytes\nSECRET";
    const auto rejected=diagnostics.encode(identity);
    CHECK(rejected.find("AUTH_MATERIAL")==rejected.npos);
    CHECK(rejected.find("PRIVATE")==rejected.npos);
    CHECK(rejected.find("SECRET")==rejected.npos);
}

TEST_CASE("Startup response ACK diagnostics retain body-free cause and distinguish observed zero from absence",
          "[e31][startup-ack][diagnostics][no-stock]") {
    namespace app=hlclient::app;
    app::ExternalSessionDiagnostics diagnostics;
    const app::SessionReceiptIdentity identity{
        "0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,45,9876};
    const auto empty=diagnostics.encode(identity);
    CHECK(empty.find("\"response_payload\":null")!=empty.npos);
    CHECK(empty.find("\"source_acknowledgement\"")==empty.npos);
    CHECK(empty.size()<2048U);

    diagnostics.reason="resource_response_stage_failed";
    diagnostics.failure_phase="resource_response";
    diagnostics.failure_category="protocol";
    diagnostics.protocol_code="server_payload_acknowledgement_invalid";
    diagnostics.wire_opcode=1U;
    diagnostics.source_sequence=12U;
    auto& payload=diagnostics.response_payload.emplace();
    payload.classification="post_transmit_continuation_candidate";
    payload.receive_position="after_first_response_transmit";
    payload.wire_encoding="wire_uncompressed";
    payload.source_body_kind="direct_body";
    payload.source_sequence=12U;
    payload.source_acknowledgement=0U;
    payload.first_transmit_sequence=2U;
    payload.payload_ordinal=7U;
    payload.wire_byte_count=1U;
    payload.response_queued=true;
    payload.response_transmitted=true;
    const auto text=diagnostics.encode(identity);
    CHECK(text.find("\"failure_phase\":\"resource_response\"")!=text.npos);
    CHECK(text.find("\"failure_category\":\"protocol\"")!=text.npos);
    CHECK(text.find("\"protocol_code\":\"server_payload_acknowledgement_invalid\"")!=text.npos);
    CHECK(text.find("\"wire_opcode\":1")!=text.npos);
    CHECK(text.find("\"source_sequence\":12")!=text.npos);
    CHECK(text.find("\"source_acknowledgement\":0")!=text.npos);
    CHECK(text.find("\"first_transmit_sequence\":2")!=text.npos);
    CHECK(text.find("\"source_body_kind\":\"direct_body\"")!=text.npos);
    CHECK(text.find("\"wire_byte_count\":1")!=text.npos);
    CHECK(text.find("\"payload_ordinal\":7")!=text.npos);
    CHECK(text.find("\"source_reliable\":false")!=text.npos);
    CHECK(text.find("\"reassembled\":false")!=text.npos);
    CHECK(text.find("\"response_queued\":true")!=text.npos);
    CHECK(text.find("\"response_transmitted\":true")!=text.npos);
    CHECK(text.find("\"response_acknowledged\":false")!=text.npos);
    CHECK(text.size()<4096U);

    payload.source_acknowledgement.reset();
    payload.first_transmit_sequence.reset();
    const auto absent=diagnostics.encode(identity);
    CHECK(absent.find("\"source_acknowledgement\":null")!=absent.npos);
    CHECK(absent.find("\"first_transmit_sequence\":null")!=absent.npos);
    payload.source_sequence=std::numeric_limits<std::uint32_t>::max();
    payload.source_acknowledgement=std::numeric_limits<std::uint32_t>::max();
    payload.first_transmit_sequence=std::numeric_limits<std::uint32_t>::max();
    payload.wire_byte_count=std::numeric_limits<std::size_t>::max();
    CHECK(diagnostics.encode(identity).size()<4096U);
}

TEST_CASE("Startup response diagnostic provenance tokens remain bounded and cannot persist raw context",
          "[e31][startup-ack][diagnostics][no-stock]") {
    namespace app=hlclient::app;
    app::ExternalSessionDiagnostics diagnostics;
    const app::SessionReceiptIdentity identity{
        "0123456789abcdef0123456789abcdef",app::SessionReceiptRole::A,45,9876};
    auto& payload=diagnostics.response_payload.emplace();
    for(const auto kind:{"normal_fragment_completion","contemporaneous_fragment_suffix"}) {
        payload.source_body_kind=kind;
        payload.source_reliable=true;
        payload.reassembled=std::string_view{kind}=="normal_fragment_completion";
        const auto text=diagnostics.encode(identity);
        CHECK(text.find(std::string{"\"source_body_kind\":\""}+kind+"\"")!=text.npos);
        CHECK(text.find("\"source_reliable\":true")!=text.npos);
        CHECK(text.find(std::string{"\"reassembled\":"}+(payload.reassembled?"true":"false"))!=text.npos);
    }
    payload.classification="RAW_AUTH SECRET";
    payload.receive_position="private server payload\nSECRET";
    payload.source_body_kind="D:\\PRIVATE\\AUTH_MATERIAL";
    payload.wire_encoding=std::string(97U,'x');
    const auto rejected=diagnostics.encode(identity);
    CHECK(rejected.find("SECRET")==rejected.npos);
    CHECK(rejected.find("PRIVATE")==rejected.npos);
    CHECK(rejected.find("AUTH_MATERIAL")==rejected.npos);
    CHECK(rejected.find("\"source_body_kind\":\"rejected_diagnostic\"")!=rejected.npos);
    CHECK(rejected.find("\"wire_encoding\":\"rejected_diagnostic\"")!=rejected.npos);
    CHECK(rejected.size()<4096U);
    diagnostics.udp_split_parts=5U;diagnostics.udp_split_completed=2U;
    diagnostics.udp_split_duplicates=1U;diagnostics.udp_split_rejected=3U;
    diagnostics.udp_split_expired=4U;diagnostics.udp_split_evicted=6U;
    const auto split=diagnostics.encode(identity);
    CHECK(split.find("\"udp_split_parts\":5")!=split.npos);
    CHECK(split.find("\"udp_split_completed\":2")!=split.npos);
    CHECK(split.find("\"udp_split_duplicates\":1")!=split.npos);
    CHECK(split.find("\"udp_split_rejected\":3")!=split.npos);
    CHECK(split.find("\"udp_split_expired\":4")!=split.npos);
    CHECK(split.find("\"udp_split_evicted\":6")!=split.npos);
}
