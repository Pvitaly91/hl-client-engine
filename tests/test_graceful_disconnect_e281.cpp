#include <hlclient/goldsrc/disconnect_request.hpp>
#include <hlclient/goldsrc/netchan_driver.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/live_runtime_stage.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace std::chrono_literals;
namespace g = hlclient::goldsrc;
namespace n = hlclient::network;
// Independent literal oracle, not constructed with the production builder.
constexpr std::array<std::byte, 13> goodbye{
    std::byte{3}, std::byte{100}, std::byte{114}, std::byte{111},
    std::byte{112}, std::byte{99}, std::byte{108}, std::byte{105},
    std::byte{101}, std::byte{110}, std::byte{116}, std::byte{10}, std::byte{0}};
constexpr std::size_t full_reliable_body = g::kDefaultNetchanDatagramSize - g::kNetchanHeaderSize;
constexpr std::size_t fragmented_reliable_body = full_reliable_body + g::kStockProtocol48NormalFragmentChunkSize;

struct Lifetime final : g::INetchanDriverLifetime {
    explicit Lifetime(std::size_t& releases) : releases_(releases) {}
    ~Lifetime() override { ++releases_; }
    std::size_t& releases_;
};
struct Transport final : n::IDatagramTransport {
    std::optional<n::NetworkAddress> local{n::NetworkAddress::loopback(31000)};
    std::deque<n::DatagramTransportReceiveResult> incoming;
    std::deque<n::DatagramSendResult> results;
    std::vector<std::vector<std::byte>> sent;
    std::size_t attempts{}, polls{};
    std::size_t* releases{};
    bool send_after_teardown{};
    n::DatagramLocalAddressResult local_address() const override { return {local,{}}; }
    n::DatagramSendResult send_to(const n::NetworkAddress&, std::span<const std::byte> data) override {
        ++attempts;
        if (releases && *releases) send_after_teardown = true;
        if (!results.empty()) {
            auto result=results.front(); results.pop_front();
            if (!result) return result;
        }
        sent.emplace_back(data.begin(),data.end());
        return {n::DatagramSendStatus::sent,{}};
    }
    n::DatagramTransportReceiveResult receive(std::size_t) override {
        ++polls;
        if (incoming.empty()) return {};
        auto result=std::move(incoming.front()); incoming.pop_front(); return result;
    }
    void queue(const n::NetworkAddress peer, std::vector<std::byte> data) {
        const auto size=data.size();
        incoming.push_back({n::DatagramTransportReceiveStatus::received,
            n::Datagram{peer,std::move(data)},peer,size,{}});
    }
};
const auto peer=n::NetworkAddress::loopback(27015);
const auto epoch=g::NetchanDriverTimePoint{} + 1s;
g::NetchanSequence seq(std::uint32_t value) {
    const auto parsed=g::NetchanSequence::from_numeric(value);
    if (!parsed) throw std::runtime_error("invalid fixture sequence");
    return *parsed;
}
std::vector<std::byte> server(std::uint32_t sequence,std::uint32_t ack,
                            std::vector<std::byte> body={std::byte{1}},bool ack_reliable=false) {
    auto encoded=g::encode_server_to_client_netchan_packet({
        {{seq(sequence),{false,false}},{seq(ack),ack_reliable}}, {}, std::move(body)});
    REQUIRE(encoded.datagram); return std::move(*encoded.datagram);
}
void drain(g::NetchanDriver& driver) { while(driver.poll_event()) {} }
bool contains_goodbye(const std::vector<std::byte>& datagram) {
    const auto decoded=g::decode_client_to_server_netchan_packet(datagram);
    REQUIRE(decoded.packet);
    return std::search(decoded.packet->payload.begin(),decoded.packet->payload.end(),
                       goodbye.begin(),goodbye.end()) != decoded.packet->payload.end();
}
void finish_without_server(g::NetchanDriver& driver, const g::NetchanDriverTimePoint start=epoch) {
    for (int tick=0;tick<=30;++tick) { driver.update(start+tick*50ms); drain(driver); }
}
struct LiveFixture {
    Transport transport;
    hlclient::client::ClientWorldState world;
    std::shared_ptr<hlclient::game_api::GameClientHost> game{
        std::make_shared<hlclient::game_api::GameClientHost>(hlclient::games::halflife::make_half_life_client_module())};
    g::LiveRuntimeStageConfig config;
    std::unique_ptr<g::LiveRuntimeStage> stage;
    LiveFixture() {
        game->reset({1,1,1}); config.game_client=game;
        config.operation_mode=g::LiveRuntimeOperationMode::live_visual_control;
        config.live_visual_input_source=g::LiveVisualControlInputSource::keyboard_mouse;
        g::apply_live_runtime_compatibility_profile(config);
        stage=std::make_unique<g::LiveRuntimeStage>(transport,peer,world,config);
    }
};
}

TEST_CASE("E28.1 fixed disconnect bytes have exact opcode LF and NUL without text input",
          "[e281][graceful-disconnect][codec]") {
    const auto bytes=g::DisconnectRequestBuilder::build();
    CHECK(bytes.size()==13);
    CHECK(std::equal(bytes.begin(),bytes.end(),goodbye.begin(),goodbye.end()));
    CHECK(g::inspect_disconnect_notice({})==g::DisconnectNoticeStatus::not_notice);
    const std::array notice{std::byte{2},std::byte{'d'},std::byte{0}};
    CHECK(g::inspect_disconnect_notice(notice)==g::DisconnectNoticeStatus::observed);
    const std::array malformed{std::byte{2},std::byte{'d'}};
    CHECK(g::inspect_disconnect_notice(malformed)==g::DisconnectNoticeStatus::malformed);
    const std::array fake_text{std::byte{8},std::byte{2},std::byte{0}};
    CHECK(g::inspect_disconnect_notice(fake_text)==g::DisconnectNoticeStatus::not_notice);
}

TEST_CASE("E28.1 force cancellation retains the pre-fix no-terminal-send behavior",
          "[e281][graceful-disconnect][baseline]") {
    Transport transport; g::NetchanDriver driver(transport,peer);
    REQUIRE(driver.start(epoch,*transport.local));
    driver.cancel(epoch); driver.update(epoch+1ms);
    CHECK(transport.sent.empty()); CHECK_FALSE(driver.graceful_close_snapshot().requested);
    CHECK(driver.state()==g::NetchanDriverState::cancelled);
}

TEST_CASE("E28.1 normal close sends framed goodbye before lifetime teardown and remains bounded",
          "[e281][graceful-disconnect][netchan]") {
    Transport transport; std::size_t releases{}; transport.releases=&releases;
    g::NetchanDriver driver(transport,peer,{},std::make_unique<Lifetime>(releases));
    REQUIRE(driver.start(epoch,*transport.local));
    REQUIRE(driver.request_graceful_close(epoch));
    CHECK(releases==0); CHECK(transport.sent.empty());
    finish_without_server(driver);
    const auto summary=driver.graceful_close_snapshot();
    CHECK(summary.eligible); CHECK(summary.queued); CHECK(summary.send_attempt_count==3);
    CHECK(summary.successful_send_count==3); CHECK(summary.deadline_reached);
    CHECK(summary.first_send_at==epoch); CHECK(summary.last_send_at==epoch+200ms);
    CHECK(summary.netchan_closed_at==epoch+1500ms);
    CHECK(summary.outcome==g::GracefulCloseOutcome::sent_but_unconfirmed);
    CHECK_FALSE(summary.ack_observed); CHECK_FALSE(summary.server_disconnect_observed);
    REQUIRE(transport.sent.size()==3);
    for(const auto& datagram:transport.sent) CHECK(contains_goodbye(datagram));
    CHECK(releases==1); CHECK_FALSE(transport.send_after_teardown);
    driver.update(epoch+10s); driver.close(epoch+10s); driver.cancel(epoch+10s);
    CHECK(transport.sent.size()==3); CHECK(releases==1); CHECK(driver.cleanup_count()==1);
}

TEST_CASE("E28.1 repeated close keeps identity and deadline while late gameplay is rejected",
          "[e281][graceful-disconnect][idempotent]") {
    Transport transport; g::NetchanDriver driver(transport,peer);
    REQUIRE(driver.start(epoch,*transport.local)); REQUIRE(driver.request_graceful_close(epoch));
    const auto first=driver.graceful_close_snapshot();
    for(int i=1;i<20;++i) REQUIRE(driver.request_graceful_close(epoch+i*50ms));
    CHECK(driver.graceful_close_snapshot().operation_identity==first.operation_identity);
    CHECK(driver.graceful_close_snapshot().deadline_at==first.deadline_at);
    CHECK_FALSE(driver.queue_reliable(goodbye)); CHECK_FALSE(driver.submit_unreliable(goodbye));
    CHECK_FALSE(driver.prepare_unreliable_context());
    finish_without_server(driver); CHECK(driver.graceful_close_snapshot().send_attempt_count==3);
}

TEST_CASE("E28.1 would-block retains sequence and allows bounded retry without claiming sent",
          "[e281][graceful-disconnect][would-block]") {
    for(const bool all_blocked:{false,true}) {
        Transport transport;
        transport.results.push_back({n::DatagramSendStatus::would_block,{}});
        if(all_blocked) for(int i=0;i<2;++i) transport.results.push_back({n::DatagramSendStatus::would_block,{}});
        g::NetchanDriver driver(transport,peer);
        REQUIRE(driver.start(epoch,*transport.local)); REQUIRE(driver.request_graceful_close(epoch));
        finish_without_server(driver);
        const auto summary=driver.graceful_close_snapshot();
        CHECK(summary.send_attempt_count==3); CHECK(summary.would_block_count==(all_blocked?3U:1U));
        CHECK(summary.successful_send_count==(all_blocked?0U:2U));
        CHECK(summary.deadline_reached); CHECK_FALSE(summary.transport_error);
        if(!all_blocked) {
            auto first=g::decode_client_to_server_netchan_packet(transport.sent.front()); REQUIRE(first.packet);
            CHECK(first.packet->header.sequence.sequence.value()==1);
        } else CHECK(summary.outcome==g::GracefulCloseOutcome::send_failed);
    }
}

TEST_CASE("E28.1 send errors and fatal driver failures survive normal close intent",
          "[e281][graceful-disconnect][failure]") {
    Transport transport; transport.results.push_back({n::DatagramSendStatus::error,"project fixture error"});
    g::NetchanDriver driver(transport,peer);
    REQUIRE(driver.start(epoch,*transport.local)); REQUIRE(driver.request_graceful_close(epoch));
    driver.update(epoch);
    CHECK(driver.state()==g::NetchanDriverState::network_error);
    REQUIRE(driver.last_error()); CHECK(driver.last_error()->code==g::NetchanDriverErrorCode::send_failed);
    CHECK(driver.graceful_close_snapshot().outcome==g::GracefulCloseOutcome::send_failed);
    CHECK(driver.graceful_close_snapshot().successful_send_count==0);
    REQUIRE(driver.request_graceful_close(epoch+100ms));
    CHECK(driver.last_error()->code==g::NetchanDriverErrorCode::send_failed);
    Transport fatal_transport; g::NetchanDriver fatal(fatal_transport,peer);
    REQUIRE(fatal.start(epoch,*fatal_transport.local)); fatal.update(epoch-1ms);
    REQUIRE(fatal.request_graceful_close(epoch));
    CHECK(fatal.graceful_close_snapshot().outcome==g::GracefulCloseOutcome::fatal_failure);
    REQUIRE(fatal.last_error()); CHECK(fatal.last_error()->code==g::NetchanDriverErrorCode::time_moved_backwards);
    CHECK(fatal_transport.sent.empty());
}

TEST_CASE("E28.1 loss of first goodbye retries and exact covering ACK is not server confirmation",
          "[e281][graceful-disconnect][ack]") {
    Transport transport; g::NetchanDriver driver(transport,peer);
    REQUIRE(driver.start(epoch,*transport.local)); REQUIRE(driver.request_graceful_close(epoch));
    driver.update(epoch); drain(driver); // Mock peer discards the first packet.
    transport.queue(peer,server(1,0)); driver.update(epoch+50ms); drain(driver);
    CHECK_FALSE(driver.graceful_close_snapshot().ack_observed);
    driver.update(epoch+100ms); drain(driver); REQUIRE(transport.sent.size()==2);
    transport.queue(peer,server(2,2)); driver.update(epoch+150ms); drain(driver);
    const auto summary=driver.graceful_close_snapshot();
    CHECK(summary.ack_observed); CHECK_FALSE(summary.server_disconnect_observed);
    CHECK(summary.outcome==g::GracefulCloseOutcome::transport_acknowledged);
    CHECK(summary.successful_send_count==2); CHECK_FALSE(summary.deadline_reached);
    CHECK(driver.terminal());
}

TEST_CASE("E28.1 wrong endpoint traffic and stale ACK cannot confirm or extend close",
          "[e281][graceful-disconnect][negative]") {
    Transport transport; g::NetchanDriver driver(transport,peer);
    REQUIRE(driver.start(epoch,*transport.local)); REQUIRE(driver.request_graceful_close(epoch));
    for(int i=0;i<=30;++i) {
        transport.queue(n::NetworkAddress::loopback(27016),server(1,1));
        transport.queue(peer,server(static_cast<std::uint32_t>(i+1),0));
        driver.update(epoch+i*50ms); drain(driver);
    }
    CHECK(driver.graceful_close_snapshot().deadline_at==epoch+1500ms);
    CHECK(driver.graceful_close_snapshot().deadline_reached);
    CHECK_FALSE(driver.graceful_close_snapshot().ack_observed);
    CHECK(transport.sent.size()==3);
}

TEST_CASE("E28.1 pending reliable units and fragments are preserved through terminal framing",
          "[e281][graceful-disconnect][reliable]") {
    for(const std::size_t size:std::array<std::size_t,3>{5U,full_reliable_body,fragmented_reliable_body}) {
        Transport transport; g::NetchanDriver driver(transport,peer);
        REQUIRE(driver.start(epoch,*transport.local));
        REQUIRE(driver.session().limits().maximum_datagram_size-g::kNetchanHeaderSize==full_reliable_body);
        REQUIRE(driver.session().limits().maximum_unfragmented_reliable_payload==full_reliable_body);
        if(size==full_reliable_body) {
            REQUIRE(size+goodbye.size()>driver.session().limits().maximum_datagram_size-g::kNetchanHeaderSize);
        }
        if(size==fragmented_reliable_body) {
            REQUIRE(size>driver.session().limits().maximum_unfragmented_reliable_payload);
        }
        const std::vector<std::byte> pending(size,std::byte{1});
        REQUIRE(driver.queue_reliable(pending)); REQUIRE(driver.request_graceful_close(epoch));
        driver.update(epoch); drain(driver);
        REQUIRE(driver.session().in_flight_reliable_payload());
        CHECK(driver.session().in_flight_reliable_payload()->bytes.size()>0);
        if(size==full_reliable_body) {
            CHECK(driver.graceful_close_snapshot().successful_send_count==0);
            CHECK(driver.graceful_close_snapshot().successful_support_send_count==1);
            CHECK(driver.session().in_flight_reliable_payload()->bytes==pending);
        }
        driver.update(epoch+100ms); drain(driver);
        CHECK(std::any_of(transport.sent.begin(),transport.sent.end(),contains_goodbye));
        if(size==fragmented_reliable_body) REQUIRE(driver.session().outgoing_fragment_transfer());
        finish_without_server(driver,epoch+150ms);
        CHECK(driver.terminal());
    }
}

TEST_CASE("E28.1 existing in-flight reliable bytes are not replaced by goodbye",
          "[e281][graceful-disconnect][in-flight]") {
    Transport transport; g::NetchanDriver driver(transport,peer);
    REQUIRE(driver.start(epoch,*transport.local));
    const std::array pending{std::byte{1},std::byte{1},std::byte{1}};
    REQUIRE(driver.queue_reliable(pending)); driver.update(epoch); drain(driver);
    REQUIRE(driver.session().in_flight_reliable_payload());
    const auto retained=driver.session().in_flight_reliable_payload()->bytes;
    REQUIRE(driver.request_graceful_close(epoch+1ms)); driver.update(epoch+1ms); drain(driver);
    REQUIRE(driver.session().in_flight_reliable_payload());
    CHECK(driver.session().in_flight_reliable_payload()->bytes==retained);
    CHECK(contains_goodbye(transport.sent.back()));
}

TEST_CASE("E28.1 accepted unsent gameplay capability is abandoned without a final attack",
          "[e281][graceful-disconnect][gameplay-stop]") {
    Transport transport; g::NetchanDriver driver(transport,peer);
    REQUIRE(driver.start(epoch,*transport.local));
    transport.queue(peer,server(1,0)); driver.update(epoch); drain(driver);
    auto context=driver.prepare_unreliable_context(); REQUIRE(context.plan);
    const std::array old_action{std::byte{2},std::byte{99}};
    REQUIRE(driver.commit_unreliable(std::move(*context.plan),old_action));
    const auto old_sends=transport.sent.size();
    REQUIRE(driver.request_graceful_close(epoch+1ms)); driver.update(epoch+1ms); drain(driver);
    REQUIRE(transport.sent.size()==old_sends+1);
    const auto terminal=g::decode_client_to_server_netchan_packet(transport.sent.back());
    REQUIRE(terminal.packet); REQUIRE(terminal.packet->payload.size()>=goodbye.size());
    CHECK(std::equal(goodbye.begin(),goodbye.end(),terminal.packet->payload.begin()));
    CHECK_FALSE(driver.last_sent_unreliable_context_identity());
    CHECK_FALSE(driver.submit_unreliable(old_action));
}

TEST_CASE("E28.1 full reliable would-block support remains separately bounded and cannot claim terminal send",
          "[e281][graceful-disconnect][support-limit]") {
    Transport transport;
    for(int i=0;i<3;++i) transport.results.push_back({n::DatagramSendStatus::would_block,{}});
    g::NetchanDriver driver(transport,peer); REQUIRE(driver.start(epoch,*transport.local));
    REQUIRE(full_reliable_body==driver.session().limits().maximum_unfragmented_reliable_payload);
    REQUIRE(full_reliable_body+goodbye.size()>driver.session().limits().maximum_datagram_size-g::kNetchanHeaderSize);
    REQUIRE(driver.queue_reliable(std::vector<std::byte>(full_reliable_body,std::byte{1})));
    REQUIRE(driver.request_graceful_close(epoch)); finish_without_server(driver);
    const auto summary=driver.graceful_close_snapshot();
    CHECK(summary.support_send_attempt_count==3); CHECK(summary.successful_support_send_count==0);
    CHECK(summary.send_attempt_count==0); CHECK(summary.successful_send_count==0);
    CHECK(summary.would_block_count==3); CHECK(summary.deadline_reached);
    CHECK(summary.outcome==g::GracefulCloseOutcome::send_failed); CHECK(transport.sent.empty());
}

TEST_CASE("E28.1 forced cancellation wins an active close without extending or restarting it",
          "[e281][graceful-disconnect][cancel-race]") {
    Transport transport; g::NetchanDriver driver(transport,peer);
    REQUIRE(driver.start(epoch,*transport.local)); REQUIRE(driver.request_graceful_close(epoch));
    driver.update(epoch); drain(driver); driver.cancel(epoch+20ms);
    REQUIRE(driver.request_graceful_close(epoch+100ms)); driver.update(epoch+5s);
    CHECK(driver.state()==g::NetchanDriverState::cancelled);
    CHECK(driver.graceful_close_snapshot().outcome==g::GracefulCloseOutcome::cancelled);
    CHECK(driver.graceful_close_snapshot().deadline_at==epoch+1500ms);
    CHECK(driver.graceful_close_snapshot().netchan_closed_at==epoch+20ms);
    CHECK(transport.sent.size()==1); CHECK(driver.cleanup_count()==1);
}

TEST_CASE("E28.1 already closed server and invalid monotonic horizon cannot reopen or fake goodbye",
          "[e281][graceful-disconnect][terminal-negative]") {
    SECTION("Exact server notice is retained without a new terminal packet") {
        Transport transport; g::NetchanDriver driver(transport,peer);
        REQUIRE(driver.start(epoch,*transport.local)); driver.confirm_server_disconnect(epoch+1ms);
        REQUIRE(driver.request_graceful_close(epoch+2ms)); driver.update(epoch+3ms);
        CHECK(driver.graceful_close_snapshot().server_disconnect_observed);
        CHECK(driver.graceful_close_snapshot().outcome==g::GracefulCloseOutcome::server_disconnect_confirmed);
        CHECK_FALSE(driver.graceful_close_snapshot().eligible);
        CHECK(transport.sent.empty()); CHECK(driver.cleanup_count()==1);
    }
    SECTION("An overflowing deadline is typed failure not an unbounded close") {
        Transport transport; g::NetchanDriver driver(transport,peer);
        REQUIRE(driver.start(epoch,*transport.local));
        REQUIRE(driver.request_graceful_close(g::NetchanDriverTimePoint::max()));
        CHECK(driver.terminal()); REQUIRE(driver.last_error());
        CHECK(driver.last_error()->code==g::NetchanDriverErrorCode::invalid_configuration);
        CHECK(driver.graceful_close_snapshot().outcome==g::GracefulCloseOutcome::fatal_failure);
        CHECK(transport.sent.empty());
    }
}

TEST_CASE("E28.1 closing B does not cancel independent A and both may close concurrently",
          "[e281][graceful-disconnect][peer]") {
    Transport a_transport,b_transport; g::NetchanDriver a(a_transport,peer),b(b_transport,peer);
    REQUIRE(a.start(epoch,*a_transport.local)); REQUIRE(b.start(epoch,*b_transport.local));
    REQUIRE(b.request_graceful_close(epoch)); finish_without_server(b);
    CHECK(a.state()==g::NetchanDriverState::active); CHECK_FALSE(a.graceful_close_snapshot().requested);
    REQUIRE(a.queue_reliable(std::array{std::byte{1}})); a.update(epoch+1500ms);
    CHECK_FALSE(a_transport.sent.empty());
    REQUIRE(a.request_graceful_close(epoch+1500ms)); finish_without_server(a,epoch+1500ms);
    CHECK(a.terminal()); CHECK(b.terminal()); CHECK(a.cleanup_count()==1); CHECK(b.cleanup_count()==1);
}

TEST_CASE("E28.1 pre-connect close cancels without a guessed protocol endpoint",
          "[e281][graceful-disconnect][startup]") {
    Transport transport;
    g::GoldSrcHandshakeCoordinator coordinator(transport,peer,g::HandshakeStopPoint::challenge,{});
    REQUIRE(coordinator.request_graceful_close(epoch));
    CHECK(coordinator.graceful_close_snapshot().outcome==g::GracefulCloseOutcome::cancel_without_protocol_disconnect);
    CHECK_FALSE(coordinator.graceful_close_snapshot().eligible); CHECK(transport.sent.empty());
    CHECK(coordinator.terminal());
    coordinator.update(epoch+1s); CHECK(transport.sent.empty());
}

TEST_CASE("E28.1 early sign-on close uses the existing driver and suppresses new gameplay",
          "[e281][graceful-disconnect][live-stage]") {
    LiveFixture fixture; REQUIRE(fixture.stage->start(epoch,*fixture.transport.local));
    REQUIRE(fixture.stage->request_graceful_close(epoch));
    CHECK(fixture.stage->graceful_close_snapshot().eligible);
    g::LiveVisualControlInput input; input.generation=1; input.input_revision=1;
    input.focused=true; input.captured=true; input.held_buttons=0xffffU;
    CHECK_FALSE(fixture.stage->submit_live_visual_input(input,epoch));
    CHECK_FALSE(fixture.stage->activate_live_visual_control(epoch));
    CHECK_FALSE(fixture.stage->request_weapon_selection(2));
    for(int i=0;i<=30;++i) {fixture.stage->update(epoch+i*50ms);while(fixture.stage->poll_event()) {}}
    CHECK(fixture.stage->terminal()); CHECK_FALSE(fixture.stage->error());
    CHECK(fixture.stage->graceful_close_snapshot().successful_send_count>0);
    CHECK(fixture.stage->usercmd_transmit_count()==0);
    CHECK(std::any_of(fixture.transport.sent.begin(),fixture.transport.sent.end(),contains_goodbye));
}

TEST_CASE("E28.1 simultaneous server disconnect is typed confirmation without gameplay cleanup inference",
          "[e281][graceful-disconnect][server-close]") {
    LiveFixture fixture; REQUIRE(fixture.stage->start(epoch,*fixture.transport.local));
    REQUIRE(fixture.stage->request_graceful_close(epoch));
    fixture.transport.queue(peer,server(1,0,{std::byte{2},std::byte{'b'},std::byte{'y'},std::byte{'e'},std::byte{0}}));
    fixture.stage->update(epoch);
    CHECK(fixture.stage->terminal()); CHECK_FALSE(fixture.stage->error());
    CHECK(fixture.stage->graceful_close_snapshot().server_disconnect_observed);
    CHECK(fixture.stage->graceful_close_snapshot().outcome==g::GracefulCloseOutcome::server_disconnect_confirmed);
    CHECK_FALSE(fixture.stage->graceful_close_snapshot().ack_observed);
}

TEST_CASE("E28.1 malformed close notices and prior parser failures are not normal success",
          "[e281][graceful-disconnect][parser]") {
    LiveFixture fixture; REQUIRE(fixture.stage->start(epoch,*fixture.transport.local));
    REQUIRE(fixture.stage->request_graceful_close(epoch));
    fixture.transport.queue(peer,server(1,0,{std::byte{2},std::byte{'b'}}));
    fixture.stage->update(epoch); REQUIRE(fixture.stage->error());
    CHECK(fixture.stage->error()->code==g::LiveRuntimeStageErrorCode::runtime_record_failed);
    CHECK(fixture.stage->graceful_close_snapshot().outcome==g::GracefulCloseOutcome::fatal_failure);
    LiveFixture prior; REQUIRE(prior.stage->start(epoch,*prior.transport.local));
    prior.stage->update(epoch-1ms); REQUIRE(prior.stage->error());
    const auto cause=prior.stage->error()->code;
    REQUIRE(prior.stage->request_graceful_close(epoch));
    REQUIRE(prior.stage->error()); CHECK(prior.stage->error()->code==cause);
    CHECK(prior.stage->graceful_close_snapshot().outcome==g::GracefulCloseOutcome::fatal_failure);
}
