#include "delta_test_fixture.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include "local_resource_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "resource_client_response_test_fixture.hpp"
#include "resource_list_test_fixture.hpp"
#include "user_info_test_fixture.hpp"

#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/precache_manifest_stage.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/resource_consistency/provider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <bzlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace consistency = hlclient::resource_consistency;
namespace delta_fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace local = hlclient::local_resources;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace network = hlclient::network;
namespace readiness_fixture = hlclient::tests::readiness_fixture;
namespace response_fixture =
    hlclient::test::resource_client_response_fixture;
namespace user_fixture = hlclient::test::user_info_fixture;

struct SentDatagram {
    network::NetworkAddress destination;
    std::vector<std::byte> payload;
};

class FakeTransport final : public network::IDatagramTransport {
public:
    [[nodiscard]] network::DatagramLocalAddressResult local_address()
        const override
    {
        return {local, {}};
    }

    [[nodiscard]] network::DatagramSendResult send_to(
        const network::NetworkAddress& destination,
        const std::span<const std::byte> payload) override
    {
        sent.push_back(SentDatagram{
            destination,
            std::vector<std::byte>{payload.begin(), payload.end()},
        });
        if (fail_sends) {
            return {network::DatagramSendStatus::error, "synthetic failure"};
        }
        return {network::DatagramSendStatus::sent, {}};
    }

    [[nodiscard]] network::DatagramTransportReceiveResult receive(
        std::size_t) override
    {
        if (incoming.empty()) {
            return {
                network::DatagramTransportReceiveStatus::would_block,
                std::nullopt,
                std::nullopt,
                0U,
                {},
            };
        }
        auto result = std::move(incoming.front());
        incoming.pop_front();
        return result;
    }

    void queue(
        const network::NetworkAddress source,
        std::vector<std::byte> payload)
    {
        const auto size = payload.size();
        incoming.push_back({
            network::DatagramTransportReceiveStatus::received,
            network::Datagram{source, std::move(payload)},
            source,
            size,
            {},
        });
    }

    network::NetworkAddress local{network::NetworkAddress::loopback(31'733U)};
    bool fail_sends{false};
    std::vector<SentDatagram> sent;
    std::deque<network::DatagramTransportReceiveResult> incoming;
};

class CountingConnectionLifetime final : public goldsrc::INetchanDriverLifetime {
public:
    explicit CountingConnectionLifetime(std::size_t& releases) noexcept
        : releases_{releases}
    {
    }

    ~CountingConnectionLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

class CountingConsistencyLifetime final
    : public consistency::IResourceConsistencySessionLifetime {
public:
    explicit CountingConsistencyLifetime(std::size_t& releases) noexcept
        : releases_{releases}
    {
    }

    ~CountingConsistencyLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

class ImmediateConsistencyOperation final
    : public consistency::ResourceConsistencyOperation {
public:
    ImmediateConsistencyOperation(
        std::size_t& update_count,
        std::size_t& cancel_count,
        std::size_t& lifetime_releases) noexcept
        : update_count_{update_count},
          cancel_count_{cancel_count},
          lifetime_releases_{lifetime_releases}
    {
    }

    [[nodiscard]] consistency::ResourceConsistencyUpdateResult update()
        override
    {
        ++update_count_;
        auto created = consistency::make_resource_consistency_material(
            0x01020304U,
            response_fixture::kSyntheticOpaqueMaterial);
        REQUIRE(created);
        return consistency::ResourceConsistencyUpdateResult::succeeded(
            consistency::ResourceConsistencySession{
                std::move(*created.material),
                std::make_unique<CountingConsistencyLifetime>(
                    lifetime_releases_),
            });
    }

    void cancel() noexcept override
    {
        if (!cancelled_) {
            cancelled_ = true;
            ++cancel_count_;
        }
    }

private:
    std::size_t& update_count_;
    std::size_t& cancel_count_;
    std::size_t& lifetime_releases_;
    bool cancelled_{false};
};

class ImmediateConsistencyProvider final
    : public consistency::IResourceConsistencyProvider {
public:
    [[nodiscard]] consistency::ResourceConsistencyBeginResult begin(
        const consistency::ResourceConsistencyRequirements&) override
    {
        ++begin_count;
        return consistency::ResourceConsistencyBeginResult::started(
            std::make_unique<ImmediateConsistencyOperation>(
                update_count,
                cancel_count,
                lifetime_releases));
    }

    std::size_t begin_count{0U};
    std::size_t update_count{0U};
    std::size_t cancel_count{0U};
    std::size_t lifetime_releases{0U};
};

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto parsed = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(parsed);
    return *parsed;
}

[[nodiscard]] std::vector<std::byte> service_envelope(
    const std::span<const std::byte> semantic_payload)
{
    REQUIRE_FALSE(semantic_payload.empty());
    REQUIRE(semantic_payload.size() <=
            (std::numeric_limits<unsigned int>::max)());

    std::vector<char> source;
    source.reserve(semantic_payload.size());
    std::ranges::transform(
        semantic_payload,
        std::back_inserter(source),
        [](const std::byte value) {
            return static_cast<char>(std::to_integer<std::uint8_t>(value));
        });
    const auto bound = source.size() + source.size() / 100U + 601U;
    REQUIRE(bound <= (std::numeric_limits<unsigned int>::max)());
    std::vector<char> compressed(bound);
    auto compressed_size = static_cast<unsigned int>(compressed.size());
    REQUIRE(BZ2_bzBuffToBuffCompress(
                compressed.data(),
                &compressed_size,
                source.data(),
                static_cast<unsigned int>(source.size()),
                9,
                0,
                30) == BZ_OK);
    compressed.resize(compressed_size);

    std::vector<std::byte> envelope{
        std::byte{0x42U}, std::byte{0x5aU},
        std::byte{0x32U}, std::byte{0x00U},
    };
    std::ranges::transform(
        compressed,
        std::back_inserter(envelope),
        [](const char value) {
            return static_cast<std::byte>(
                static_cast<unsigned char>(value));
        });
    return envelope;
}

[[nodiscard]] std::vector<std::byte> server_packet(
    const std::uint32_t packet_sequence,
    const bool reliable,
    const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement,
    std::vector<std::byte> payload = {})
{
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(packet_sequence),
                goldsrc::NetchanSequenceFlags{reliable, false},
            },
            goldsrc::NetchanAcknowledgementWord{
                sequence(acknowledgement),
                reliable_acknowledgement,
            },
        },
        {},
        std::move(payload),
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return std::move(*encoded.datagram);
}

[[nodiscard]] goldsrc::PrecacheManifestStageConfig test_config()
{
    goldsrc::PrecacheManifestStageConfig config;
    auto& transition = config.response.resource_list.transition;
    auto& driver = transition.user_info.movement_environment.delta.pre_resource
                       .initial_signon.driver;
    driver.channel_inactivity_timeout = 100ms;
    driver.fragment_transfer_timeout = 100ms;
    driver.maximum_datagrams_per_update = 16U;
    driver.maximum_outgoing_packets_per_update = 8U;
    driver.maximum_events = 64U;
    transition.user_info.movement_environment.delta.pre_resource.initial_signon
        .maximum_events = 64U;
    transition.user_info.movement_environment.delta.pre_resource.initial_signon
        .maximum_driver_events_per_update = 64U;
    transition.user_info.movement_environment.delta.pre_resource.maximum_events =
        64U;
    transition.user_info.movement_environment.delta.maximum_events = 64U;
    transition.user_info.movement_environment.maximum_events = 64U;
    transition.user_info.maximum_stage_events = 64U;
    transition.maximum_stage_events = 64U;
    transition.maximum_driver_events_per_update = 64U;
    config.response.resource_list.maximum_stage_events = 64U;
    config.response.maximum_driver_events_per_update = 64U;
    config.response.response.maximum_response_stage_events = 64U;
    config.manifest.maximum_manifest_events = 64U;
    return config;
}

[[nodiscard]] std::vector<std::vector<std::byte>> schemas()
{
    return {
        delta_fixture::schema("alpha_t", delta_fixture::kSchemaAlphaFields),
        delta_fixture::schema("bravo_t", delta_fixture::kSchemaBravoFields),
    };
}

[[nodiscard]] std::vector<std::byte> first_semantic_payload()
{
    std::vector<std::byte> post_delta;
    move_fixture::append_move_vars_body(post_delta);
    move_fixture::append_confirmed_controls(post_delta);
    post_delta.insert(
        post_delta.end(),
        user_fixture::kExactUserInfoMessage.begin(),
        user_fixture::kExactUserInfoMessage.end());
    return delta_fixture::service_payload(
        schemas(),
        goldsrc::kMoveVarsOpcode,
        post_delta);
}

[[nodiscard]] std::vector<std::byte> resource_semantic_payload(
    const std::span<const resource_list_test_fixture::EntrySpec> entries)
{
    constexpr std::array prefix{
        std::byte{45U},
        std::byte{1U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
    };
    const auto message = resource_list_test_fixture::make_message(entries);
    std::vector<std::byte> payload{prefix.begin(), prefix.end()};
    payload.insert(payload.end(), message.bytes.begin(), message.bytes.end());
    return payload;
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket decode_sent(
    const SentDatagram& datagram)
{
    const auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        datagram.payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    return *decoded.packet;
}

struct DrivenTransition {
    goldsrc::ClientToServerNetchanPacket request;
};

[[nodiscard]] DrivenTransition drive_to_transition_request(
    goldsrc::PrecacheManifestStage& stage,
    FakeTransport& transport,
    const network::NetworkAddress remote,
    const goldsrc::PrecacheManifestStageTimePoint epoch,
    std::unique_ptr<goldsrc::INetchanDriverLifetime> lifetime = {})
{
    REQUIRE(stage.start(epoch, transport.local, std::move(lifetime)));
    stage.update(epoch + 1ms);
    REQUIRE(transport.sent.size() == 1U);
    const auto initial = decode_sent(transport.sent.front());

    transport.queue(
        remote,
        server_packet(
            1U,
            true,
            initial.header.sequence.sequence.value(),
            true,
            service_envelope(first_semantic_payload())));
    stage.update(epoch + 2ms);
    CHECK(stage.initial_request_queue_count() == 1U);
    CHECK(stage.transition_request_queue_count() == 1U);

    stage.update(epoch + 3ms);
    REQUIRE(transport.sent.size() >= 3U);
    const auto transition = decode_sent(transport.sent.back());
    constexpr std::array exact_request{
        std::byte{0x03U}, std::byte{'s'}, std::byte{'e'}, std::byte{'n'},
        std::byte{'d'}, std::byte{'r'}, std::byte{'e'}, std::byte{'s'},
        std::byte{0U},
    };
    REQUIRE(std::ranges::equal(transition.payload, exact_request));
    return DrivenTransition{transition};
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket drive_to_response(
    goldsrc::PrecacheManifestStage& stage,
    FakeTransport& transport,
    const network::NetworkAddress remote,
    const DrivenTransition& driven,
    const std::span<const resource_list_test_fixture::EntrySpec> entries,
    const goldsrc::PrecacheManifestStageTimePoint now)
{
    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.request.header.sequence.sequence.value(),
            false,
            service_envelope(resource_semantic_payload(entries))));
    stage.update(now);
    REQUIRE_FALSE(transport.sent.empty());
    const auto response = decode_sent(transport.sent.back());
    REQUIRE(response.header.sequence.flags.reliable);
    REQUIRE(response.header.sequence.flags.fragmented);
    REQUIRE(response.fragments[0U]);
    REQUIRE_FALSE(response.fragments[1U]);
    CHECK(stage.response_queue_count() == 1U);
    return response;
}

void deliver_covering_continuation(
    goldsrc::PrecacheManifestStage& stage,
    FakeTransport& transport,
    const network::NetworkAddress remote,
    const goldsrc::ClientToServerNetchanPacket& response,
    const goldsrc::PrecacheManifestStageTimePoint now)
{
    constexpr std::array semantic{
        std::byte{0x03U},
        std::byte{'s'}, std::byte{'p'}, std::byte{'a'},
        std::byte{'w'}, std::byte{'n'}, std::byte{0U},
    };
    transport.queue(
        remote,
        server_packet(
            3U,
            false,
            response.header.sequence.sequence.value(),
            true,
            service_envelope(semantic)));
    stage.update(now);
}

[[nodiscard]] std::shared_ptr<const local::LocalResourceEnvironment>
shared_environment(const hlclient::tests::ScopedLocalResourceTestRoot& root)
{
    auto environment = readiness_fixture::make_environment(root);
    return std::shared_ptr<const local::LocalResourceEnvironment>{
        std::move(environment)};
}

const std::array kCompleteEntries{
    resource_list_test_fixture::EntrySpec{
        2U, "maps/test_alpha.bsp", 37U, 0x00ff'ffffU, 0U},
    resource_list_test_fixture::EntrySpec{
        2U, "models/test_model.mdl", 9U, 1U, 0U},
    resource_list_test_fixture::EntrySpec{
        0U, "test_sound.wav", 4U, 2U, 0U},
};

void run_repeated_stage_scenario(
    const std::shared_ptr<const local::LocalResourceEnvironment>& environment,
    const std::span<const resource_list_test_fixture::EntrySpec> entries,
    const goldsrc::PrecacheManifestStageState expected_state,
    const goldsrc::PrecacheManifestCompleteness expected_completeness,
    const bool expected_world_ready,
    const std::size_t run)
{
    INFO("repeated fake-HLDS run=" << run);
    FakeTransport transport;
    ImmediateConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(
        static_cast<std::uint16_t>(28'000U + run));
    const auto epoch = goldsrc::PrecacheManifestStageTimePoint{} +
                       std::chrono::seconds{static_cast<long long>(run + 1U)};
    std::size_t connection_releases = 0U;
    goldsrc::PrecacheManifestStage stage{
        transport,
        remote,
        environment,
        test_config(),
        &provider};

    const auto driven = drive_to_transition_request(
        stage,
        transport,
        remote,
        epoch,
        std::make_unique<CountingConnectionLifetime>(connection_releases));
    const auto response = drive_to_response(
        stage, transport, remote, driven, entries, epoch + 4ms);
    deliver_covering_continuation(
        stage, transport, remote, response, epoch + 5ms);
    REQUIRE(stage.state() ==
            goldsrc::PrecacheManifestStageState::building_local_inventory);
    const auto sends_at_boundary = transport.sent.size();

    stage.update(epoch + 6ms);
    REQUIRE(stage.state() ==
            goldsrc::PrecacheManifestStageState::building_precache_manifest);
    stage.update(epoch + 7ms);

    REQUIRE(stage.state() == expected_state);
    REQUIRE(stage.terminal());
    REQUIRE(stage.result());
    REQUIRE_FALSE(stage.error());
    CHECK(stage.result()->manifest().completeness() == expected_completeness);
    CHECK(stage.result()->manifest().world_geometry_ready() ==
          expected_world_ready);
    CHECK(stage.result()->environment().get() == environment.get());
    CHECK(stage.local_endpoint() == transport.local);
    CHECK(stage.initial_request_queue_count() == 1U);
    CHECK(stage.transition_request_queue_count() == 1U);
    CHECK(stage.response_queue_count() == 1U);
    CHECK(stage.manifest_publication_count() == 1U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(provider.begin_count == 1U);
    CHECK(provider.update_count == 1U);
    CHECK(provider.lifetime_releases == 1U);
    CHECK(connection_releases == 1U);
    CHECK(transport.sent.size() == sends_at_boundary);
    CHECK(std::ranges::all_of(
        transport.sent,
        [remote](const auto& sent) { return sent.destination == remote; }));

    stage.update(epoch + 8ms);
    stage.cancel(epoch + 9ms);
    CHECK(stage.manifest_publication_count() == 1U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(transport.sent.size() == sends_at_boundary);
    CHECK(connection_releases == 1U);
}

} // namespace

TEST_CASE("Precache-manifest stage retains one session through metadata publication",
          "[goldsrc][precache][stage][session][lifetime]")
{
    hlclient::tests::ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_alpha.bsp", "map");
    root.write("valve", "models/test_model.mdl", "model");
    root.write("valve", "sound/test_sound.wav", "sound");
    const auto environment = shared_environment(root);

    FakeTransport transport;
    ImmediateConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'860U);
    const auto epoch = goldsrc::PrecacheManifestStageTimePoint{} + 1s;
    std::size_t connection_releases = 0U;
    goldsrc::PrecacheManifestStage stage{
        transport,
        remote,
        environment,
        test_config(),
        &provider};

    const auto driven = drive_to_transition_request(
        stage,
        transport,
        remote,
        epoch,
        std::make_unique<CountingConnectionLifetime>(connection_releases));
    const auto response = drive_to_response(
        stage, transport, remote, driven, kCompleteEntries, epoch + 4ms);
    CHECK(provider.lifetime_releases == 0U);
    CHECK(connection_releases == 0U);

    deliver_covering_continuation(
        stage, transport, remote, response, epoch + 5ms);
    REQUIRE(stage.state() ==
            goldsrc::PrecacheManifestStageState::building_local_inventory);
    const auto sends_at_boundary = transport.sent.size();
    CHECK(stage.cleanup_count() == 0U);
    CHECK(connection_releases == 0U);

    stage.update(epoch + 6ms);
    REQUIRE(stage.state() ==
            goldsrc::PrecacheManifestStageState::building_precache_manifest);
    CHECK(transport.sent.size() == sends_at_boundary);
    CHECK(stage.cleanup_count() == 0U);

    stage.update(epoch + 7ms);
    REQUIRE(stage.state() ==
            goldsrc::PrecacheManifestStageState::precache_manifest_ready);
    REQUIRE(stage.terminal());
    REQUIRE_FALSE(stage.error());
    REQUIRE(stage.result());
    CHECK(stage.result()->environment().get() == environment.get());
    CHECK(stage.result()->inventory().entry_count() == kCompleteEntries.size());
    CHECK(stage.result()->manifest().entry_count() == kCompleteEntries.size());
    CHECK(stage.result()->manifest().complete_for_supported_local_profile());
    CHECK(stage.result()->manifest().world_geometry_ready());
    CHECK(stage.result()->manifest().world_selection().resource_index() == 37U);
    CHECK(stage.manifest_publication_count() == 1U);
    CHECK(stage.response_queue_count() == 1U);
    CHECK(transport.sent.size() == sends_at_boundary);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(provider.begin_count == 1U);
    CHECK(provider.update_count == 1U);
    CHECK(provider.lifetime_releases == 1U);
    CHECK(connection_releases == 1U);

    stage.update(epoch + 20ms);
    stage.cancel(epoch + 21ms);
    CHECK(stage.manifest_publication_count() == 1U);
    CHECK(transport.sent.size() == sends_at_boundary);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(provider.lifetime_releases == 1U);
    CHECK(connection_releases == 1U);
}

TEST_CASE("Precache-manifest stage publishes explicit nonfatal local outcomes",
          "[goldsrc][precache][stage][outcome]")
{
    hlclient::tests::ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_alpha.bsp", "map");
    const auto environment = shared_environment(root);
    const auto remote = network::NetworkAddress::loopback(27'861U);
    const auto epoch = goldsrc::PrecacheManifestStageTimePoint{} + 1s;

    const auto run = [&](const std::span<const resource_list_test_fixture::EntrySpec> entries,
                         const goldsrc::PrecacheManifestStageState expected) {
        FakeTransport transport;
        ImmediateConsistencyProvider provider;
        std::size_t connection_releases = 0U;
        goldsrc::PrecacheManifestStage stage{
            transport,
            remote,
            environment,
            test_config(),
            &provider};
        const auto driven = drive_to_transition_request(
            stage,
            transport,
            remote,
            epoch,
            std::make_unique<CountingConnectionLifetime>(
                connection_releases));
        const auto response = drive_to_response(
            stage, transport, remote, driven, entries, epoch + 4ms);
        deliver_covering_continuation(
            stage, transport, remote, response, epoch + 5ms);
        const auto sends_at_boundary = transport.sent.size();
        stage.update(epoch + 6ms);
        stage.update(epoch + 7ms);

        REQUIRE(stage.state() == expected);
        REQUIRE(stage.terminal());
        REQUIRE(stage.result());
        CHECK_FALSE(stage.error());
        CHECK(stage.manifest_publication_count() == 1U);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(connection_releases == 1U);
        CHECK(provider.lifetime_releases == 1U);
        CHECK(transport.sent.size() == sends_at_boundary);
        return stage.result()->manifest().completeness();
    };

    SECTION("world ready but model and sound are missing")
    {
        CHECK(run(kCompleteEntries,
                  goldsrc::PrecacheManifestStageState::
                      local_resources_incomplete) ==
              goldsrc::PrecacheManifestCompleteness::
                  world_ready_but_incomplete);
    }

    SECTION("unsafe entry blocks the local profile")
    {
        const std::array entries{
            resource_list_test_fixture::EntrySpec{
                2U, "maps/test_alpha.bsp", 37U, 0U, 0U},
            resource_list_test_fixture::EntrySpec{
                4U, "../outside.dat", 2U, 0U, 0U},
        };
        CHECK(run(entries,
                  goldsrc::PrecacheManifestStageState::
                      unsafe_local_resources) ==
              goldsrc::PrecacheManifestCompleteness::
                  blocked_unsafe_resources);
    }

    SECTION("unsupported name encoding remains explicit")
    {
        std::string non_ascii;
        non_ascii.push_back(static_cast<char>(0x80U));
        non_ascii.append(".mdl");
        const std::vector<resource_list_test_fixture::EntrySpec> entries{
            {2U, "maps/test_alpha.bsp", 37U, 0U, 0U},
            {2U, non_ascii, 2U, 0U, 0U},
        };
        CHECK(run(entries,
                  goldsrc::PrecacheManifestStageState::
                      unsupported_local_profile) ==
              goldsrc::PrecacheManifestCompleteness::unsupported_profile);
    }

    SECTION("directory target remains a local I/O outcome")
    {
        std::error_code error;
        REQUIRE(std::filesystem::create_directory(
            root.game_path("valve") / "directory-target", error));
        REQUIRE_FALSE(error);
        const std::array entries{
            resource_list_test_fixture::EntrySpec{
                2U, "maps/test_alpha.bsp", 37U, 0U, 0U},
            resource_list_test_fixture::EntrySpec{
                4U, "directory-target", 2U, 0U, 0U},
        };
        CHECK(run(entries,
                  goldsrc::PrecacheManifestStageState::
                      local_resource_io_error) ==
              goldsrc::PrecacheManifestCompleteness::local_io_failure);
    }
}

TEST_CASE("Precache-manifest stage fails transactionally on world mismatch",
          "[goldsrc][precache][stage][correlation]")
{
    hlclient::tests::ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_map.bsp", "map");
    const auto environment = shared_environment(root);
    FakeTransport transport;
    ImmediateConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'862U);
    const auto epoch = goldsrc::PrecacheManifestStageTimePoint{} + 1s;
    const std::array entries{
        resource_list_test_fixture::EntrySpec{
            2U, "maps/test_map.bsp", 91U, 0U, 0U},
    };
    std::size_t connection_releases = 0U;
    goldsrc::PrecacheManifestStage stage{
        transport,
        remote,
        environment,
        test_config(),
        &provider};
    const auto driven = drive_to_transition_request(
        stage,
        transport,
        remote,
        epoch,
        std::make_unique<CountingConnectionLifetime>(connection_releases));
    const auto response = drive_to_response(
        stage, transport, remote, driven, entries, epoch + 4ms);
    deliver_covering_continuation(
        stage, transport, remote, response, epoch + 5ms);
    const auto sends_at_boundary = transport.sent.size();
    stage.update(epoch + 6ms);
    stage.update(epoch + 7ms);

    REQUIRE(stage.state() ==
            goldsrc::PrecacheManifestStageState::protocol_error);
    REQUIRE(stage.terminal());
    REQUIRE_FALSE(stage.result());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::PrecacheManifestStageErrorCode::manifest_build_failed);
    REQUIRE(stage.error()->manifest_code);
    CHECK(*stage.error()->manifest_code ==
          goldsrc::PrecacheManifestErrorCode::readiness_build_failed);
    CHECK(stage.manifest_publication_count() == 0U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(connection_releases == 1U);
    CHECK(provider.lifetime_releases == 1U);
    CHECK(transport.sent.size() == sends_at_boundary);
}

TEST_CASE("Precache-manifest stage bounds events and cancels retained ownership once",
          "[goldsrc][precache][stage][backpressure][cancel]")
{
    hlclient::tests::ScopedLocalResourceTestRoot root;
    root.write("valve", "maps/test_alpha.bsp", "map");
    const auto environment = shared_environment(root);
    const auto remote = network::NetworkAddress::loopback(27'863U);
    const auto epoch = goldsrc::PrecacheManifestStageTimePoint{} + 1s;

    SECTION("unpolled boundary metadata fails closed at the next phase")
    {
        FakeTransport transport;
        ImmediateConsistencyProvider provider;
        auto config = test_config();
        config.manifest.maximum_manifest_events = 1U;
        std::size_t connection_releases = 0U;
        goldsrc::PrecacheManifestStage stage{
            transport,
            remote,
            environment,
            config,
            &provider};
        const auto driven = drive_to_transition_request(
            stage,
            transport,
            remote,
            epoch,
            std::make_unique<CountingConnectionLifetime>(
                connection_releases));
        const auto response = drive_to_response(
            stage,
            transport,
            remote,
            driven,
            kCompleteEntries,
            epoch + 4ms);
        deliver_covering_continuation(
            stage, transport, remote, response, epoch + 5ms);
        REQUIRE(stage.pending_event_count() == 1U);
        const auto sends_at_boundary = transport.sent.size();

        stage.update(epoch + 6ms);
        REQUIRE(stage.state() ==
                goldsrc::PrecacheManifestStageState::backpressure);
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::PrecacheManifestStageErrorCode::event_backpressure);
        CHECK_FALSE(stage.result());
        CHECK(stage.manifest_publication_count() == 0U);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(connection_releases == 1U);
        CHECK(provider.lifetime_releases == 1U);
        CHECK(transport.sent.size() == sends_at_boundary);
    }

    SECTION("cancellation after the response boundary is idempotent")
    {
        FakeTransport transport;
        ImmediateConsistencyProvider provider;
        std::size_t connection_releases = 0U;
        goldsrc::PrecacheManifestStage stage{
            transport,
            remote,
            environment,
            test_config(),
            &provider};
        const auto driven = drive_to_transition_request(
            stage,
            transport,
            remote,
            epoch,
            std::make_unique<CountingConnectionLifetime>(
                connection_releases));
        const auto response = drive_to_response(
            stage,
            transport,
            remote,
            driven,
            kCompleteEntries,
            epoch + 4ms);
        deliver_covering_continuation(
            stage, transport, remote, response, epoch + 5ms);
        const auto sends_at_boundary = transport.sent.size();

        stage.cancel(epoch + 6ms);
        stage.cancel(epoch + 7ms);
        stage.update(epoch + 8ms);
        REQUIRE(stage.state() ==
                goldsrc::PrecacheManifestStageState::cancelled);
        CHECK_FALSE(stage.result());
        CHECK_FALSE(stage.error());
        CHECK(stage.cleanup_count() == 1U);
        CHECK(connection_releases == 1U);
        CHECK(provider.lifetime_releases == 1U);
        CHECK(transport.sent.size() == sends_at_boundary);
    }
}

TEST_CASE("Precache-manifest stage inherits prerequisite network failure",
          "[goldsrc][precache][stage][network]")
{
    hlclient::tests::ScopedLocalResourceTestRoot root;
    const auto environment = shared_environment(root);
    FakeTransport transport;
    transport.fail_sends = true;
    ImmediateConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'864U);
    const auto epoch = goldsrc::PrecacheManifestStageTimePoint{} + 1s;
    std::size_t connection_releases = 0U;
    goldsrc::PrecacheManifestStage stage{
        transport,
        remote,
        environment,
        test_config(),
        &provider};

    REQUIRE(stage.start(
        epoch,
        transport.local,
        std::make_unique<CountingConnectionLifetime>(connection_releases)));
    stage.update(epoch + 1ms);

    REQUIRE(stage.state() ==
            goldsrc::PrecacheManifestStageState::network_error);
    REQUIRE(stage.terminal());
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::PrecacheManifestStageErrorCode::response_stage_failed);
    REQUIRE(stage.error()->response_code);
    CHECK(*stage.error()->response_code ==
          goldsrc::ResourceClientResponseStageErrorCode::
              resource_list_stage_failed);
    CHECK_FALSE(stage.result());
    CHECK(stage.cleanup_count() == 1U);
    CHECK(connection_releases == 1U);
    CHECK(provider.begin_count == 0U);
}

TEST_CASE("Precache-manifest same-session fake flow is deterministic across repeated profiles",
          "[goldsrc][precache][stage][integration][repeat]")
{
    SECTION("20 of 20 complete-manifest runs")
    {
        hlclient::tests::ScopedLocalResourceTestRoot root;
        root.write("valve", "maps/test_alpha.bsp", "map");
        root.write("valve", "models/test_model.mdl", "model");
        root.write("valve", "sound/test_sound.wav", "sound");
        const auto environment = shared_environment(root);
        for (std::size_t run = 0U; run < 20U; ++run) {
            run_repeated_stage_scenario(
                environment,
                kCompleteEntries,
                goldsrc::PrecacheManifestStageState::
                    precache_manifest_ready,
                goldsrc::PrecacheManifestCompleteness::
                    complete_for_supported_local_profile,
                true,
                run);
        }
    }

    SECTION("20 of 20 world-ready incomplete runs")
    {
        hlclient::tests::ScopedLocalResourceTestRoot root;
        root.write("valve", "maps/test_alpha.bsp", "map");
        const auto environment = shared_environment(root);
        const std::array entries{
            resource_list_test_fixture::EntrySpec{
                2U, "maps/test_alpha.bsp", 37U, 0U, 0U},
            resource_list_test_fixture::EntrySpec{
                0U, "missing.wav", 4U, 0U, 0U},
        };
        for (std::size_t run = 0U; run < 20U; ++run) {
            run_repeated_stage_scenario(
                environment,
                entries,
                goldsrc::PrecacheManifestStageState::
                    local_resources_incomplete,
                goldsrc::PrecacheManifestCompleteness::
                    world_ready_but_incomplete,
                true,
                run);
        }
    }

    SECTION("20 of 20 missing-map runs")
    {
        hlclient::tests::ScopedLocalResourceTestRoot root;
        root.write("valve", "models/test_model.mdl", "model");
        root.write("valve", "sound/test_sound.wav", "sound");
        const auto environment = shared_environment(root);
        for (std::size_t run = 0U; run < 20U; ++run) {
            run_repeated_stage_scenario(
                environment,
                kCompleteEntries,
                goldsrc::PrecacheManifestStageState::
                    local_resources_incomplete,
                goldsrc::PrecacheManifestCompleteness::
                    incomplete_missing_resources,
                false,
                run);
        }
    }

    SECTION("20 of 20 sparse-slot runs")
    {
        hlclient::tests::ScopedLocalResourceTestRoot root;
        root.write("valve", "maps/test_alpha.bsp", "map");
        root.write("valve", "models/test_model.mdl", "model");
        root.write("valve", "sound/test_sound.wav", "sound");
        root.write("valve", "generic/test.dat", "generic");
        root.write("valve", "events/test.sc", "event");
        const auto environment = shared_environment(root);
        const std::array entries{
            resource_list_test_fixture::EntrySpec{
                2U, "maps/test_alpha.bsp", 37U, 0U, 0U},
            resource_list_test_fixture::EntrySpec{
                2U, "models/test_model.mdl", 4'095U, 0U, 0U},
            resource_list_test_fixture::EntrySpec{
                0U, "test_sound.wav", 0U, 0U, 0U},
            resource_list_test_fixture::EntrySpec{
                4U, "generic/test.dat", 2'047U, 0U, 0U},
            resource_list_test_fixture::EntrySpec{
                5U, "events/test.sc", 1'023U, 0U, 0U},
            resource_list_test_fixture::EntrySpec{
                3U, "{metadata", 4'095U, 0U, 0U},
        };
        for (std::size_t run = 0U; run < 20U; ++run) {
            run_repeated_stage_scenario(
                environment,
                entries,
                goldsrc::PrecacheManifestStageState::
                    precache_manifest_ready,
                goldsrc::PrecacheManifestCompleteness::
                    complete_for_supported_local_profile,
                true,
                run);
        }
    }

    SECTION("20 of 20 malicious-name runs")
    {
        hlclient::tests::ScopedLocalResourceTestRoot root;
        root.write("valve", "maps/test_alpha.bsp", "map");
        const auto environment = shared_environment(root);
        const std::array entries{
            resource_list_test_fixture::EntrySpec{
                2U, "maps/test_alpha.bsp", 37U, 0U, 0U},
            resource_list_test_fixture::EntrySpec{
                4U, "../outside.dat", 2U, 0U, 0U},
        };
        for (std::size_t run = 0U; run < 20U; ++run) {
            run_repeated_stage_scenario(
                environment,
                entries,
                goldsrc::PrecacheManifestStageState::unsafe_local_resources,
                goldsrc::PrecacheManifestCompleteness::
                    blocked_unsafe_resources,
                true,
                run);
        }
    }
}
