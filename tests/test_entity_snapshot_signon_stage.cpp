#include <hlclient/goldsrc/post_resource_entity_snapshot_stage.hpp>

#include "entity_snapshot_fake_hlds_test_support.hpp"

#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_address.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

class FakeTransport final : public network::IDatagramTransport {
public:
    [[nodiscard]] network::DatagramLocalAddressResult local_address()
        const override
    {
        if (throw_on_local_address) {
            throw std::runtime_error{"synthetic local-address failure"};
        }
        return {local, {}};
    }

    [[nodiscard]] network::DatagramSendResult send_to(
        const network::NetworkAddress&,
        const std::span<const std::byte> payload) override
    {
        sent.emplace_back(payload.begin(), payload.end());
        return {network::DatagramSendStatus::sent, {}};
    }

    [[nodiscard]] network::DatagramTransportReceiveResult receive(
        std::size_t) override
    {
        return {
            network::DatagramTransportReceiveStatus::would_block,
            std::nullopt,
            std::nullopt,
            0U,
            {}};
    }

    network::NetworkAddress local{network::NetworkAddress::loopback(31'799U)};
    std::vector<std::vector<std::byte>> sent;
    bool throw_on_local_address{false};
};

class CountingLifetime final : public goldsrc::INetchanDriverLifetime {
public:
    explicit CountingLifetime(std::size_t& releases) noexcept
        : releases_{releases}
    {
    }

    ~CountingLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

TEST_CASE("Post-resource entity stage validates every project safety cap",
          "[goldsrc][entity-snapshot][stage]")
{
    goldsrc::PostResourceEntitySnapshotStageConfig config;
    CHECK(goldsrc::valid_post_resource_entity_snapshot_stage_configuration(
        config));

    config.maximum_stage_events = goldsrc::kMaximumPostResourceStageEvents;
    CHECK(goldsrc::valid_post_resource_entity_snapshot_stage_configuration(
        config));
    ++config.maximum_stage_events;
    CHECK_FALSE(
        goldsrc::valid_post_resource_entity_snapshot_stage_configuration(
            config));
    config.maximum_stage_events =
        goldsrc::kDefaultMaximumPostResourceStageEvents;
    config.maximum_driver_events_per_update =
        goldsrc::kMaximumPostResourceDriverEventsPerUpdate;
    CHECK(goldsrc::valid_post_resource_entity_snapshot_stage_configuration(
        config));
    ++config.maximum_driver_events_per_update;
    CHECK_FALSE(
        goldsrc::valid_post_resource_entity_snapshot_stage_configuration(
            config));
    config.maximum_driver_events_per_update =
        goldsrc::kDefaultMaximumPostResourceDriverEventsPerUpdate;
    config.timeout = goldsrc::kMaximumPostResourceSignonTimeout;
    CHECK(goldsrc::valid_post_resource_entity_snapshot_stage_configuration(
        config));
    config.timeout += std::chrono::milliseconds{1};
    CHECK_FALSE(
        goldsrc::valid_post_resource_entity_snapshot_stage_configuration(
            config));

    config.timeout = goldsrc::kDefaultPostResourceSignonTimeout;
    config.profile =
        static_cast<goldsrc::PostResourceSignonCompatibilityProfile>(0xffU);
    CHECK_FALSE(
        goldsrc::valid_post_resource_entity_snapshot_stage_configuration(
            config));
    config.profile = goldsrc::PostResourceSignonCompatibilityProfile::
        stock_protocol_48_build_10210_evidence_pending;
    config.stop_condition =
        static_cast<goldsrc::EntitySnapshotStageStopCondition>(0xffU);
    CHECK_FALSE(
        goldsrc::valid_post_resource_entity_snapshot_stage_configuration(
            config));
    config.stop_condition =
        goldsrc::EntitySnapshotStageStopCondition::first_applied_delta;
    config.entity_snapshots.maximum_snapshot_history = 0U;
    CHECK_FALSE(
        goldsrc::valid_post_resource_entity_snapshot_stage_configuration(
            config));
    config.entity_snapshots.maximum_snapshot_history = 1U;
    CHECK_FALSE(
        goldsrc::valid_post_resource_entity_snapshot_stage_configuration(
            config));
    config.stop_condition =
        goldsrc::EntitySnapshotStageStopCondition::first_full_snapshot;
    CHECK(goldsrc::valid_post_resource_entity_snapshot_stage_configuration(
        config));
}

TEST_CASE("Invalid post-resource stage fails before transport or lifetime use",
          "[goldsrc][entity-snapshot][stage]")
{
    FakeTransport transport;
    auto config = goldsrc::PostResourceEntitySnapshotStageConfig{};
    config.maximum_stage_events = 0U;
    goldsrc::PostResourceEntitySnapshotStage stage{
        transport,
        network::NetworkAddress::loopback(31'798U),
        config};
    std::size_t releases = 0U;
    auto lifetime = std::make_unique<CountingLifetime>(releases);
    const auto now = goldsrc::PostResourceEntitySnapshotStageTimePoint{};

    CHECK_FALSE(stage.start(now, transport.local, std::move(lifetime)));
    CHECK(stage.terminal());
    CHECK(stage.state() ==
          goldsrc::PostResourceEntitySnapshotStageState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::PostResourceEntitySnapshotStageErrorCode::
              invalid_configuration);
    CHECK(transport.sent.empty());
    CHECK(releases == 1U);
    CHECK(stage.request_queue_count() == 0U);
    CHECK_FALSE(stage.request_transmitted());
    CHECK_FALSE(stage.request_acknowledged());
}

TEST_CASE("Post-resource stage cancellation is terminal and idempotent",
          "[goldsrc][entity-snapshot][stage]")
{
    FakeTransport transport;
    goldsrc::PostResourceEntitySnapshotStage stage{
        transport, network::NetworkAddress::loopback(31'798U)};
    std::size_t releases = 0U;
    const auto now = goldsrc::PostResourceEntitySnapshotStageTimePoint{};
    REQUIRE(stage.start(
        now,
        transport.local,
        std::make_unique<CountingLifetime>(releases)));
    CHECK(stage.state() ==
          goldsrc::PostResourceEntitySnapshotStageState::
              waiting_for_resource_response);

    stage.cancel(now + std::chrono::milliseconds{1});
    stage.cancel(now + std::chrono::milliseconds{2});
    stage.update(now + std::chrono::milliseconds{3});
    CHECK(stage.terminal());
    CHECK(stage.state() ==
          goldsrc::PostResourceEntitySnapshotStageState::cancelled);
    CHECK(stage.request_queue_count() == 0U);
    CHECK(releases == 1U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK_FALSE(stage.result());
    CHECK_FALSE(stage.error());
    REQUIRE(stage.poll_event());
}

TEST_CASE("Post-resource stage rejects backwards time and cleans up once",
          "[goldsrc][entity-snapshot][stage][cleanup][time]")
{
    FakeTransport transport;
    goldsrc::PostResourceEntitySnapshotStage stage{
        transport, network::NetworkAddress::loopback(31'798U)};
    std::size_t releases = 0U;
    const auto now = goldsrc::PostResourceEntitySnapshotStageTimePoint{} +
        std::chrono::milliseconds{10};
    REQUIRE(stage.start(
        now,
        transport.local,
        std::make_unique<CountingLifetime>(releases)));

    stage.update(now - std::chrono::milliseconds{1});
    REQUIRE(stage.terminal());
    CHECK(stage.state() ==
          goldsrc::PostResourceEntitySnapshotStageState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::PostResourceEntitySnapshotStageErrorCode::
              time_moved_backwards);
    CHECK_FALSE(stage.result());
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);

    stage.update(now + std::chrono::milliseconds{1});
    stage.cancel(now + std::chrono::milliseconds{2});
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Post-resource stage catches transport exceptions during start",
          "[goldsrc][entity-snapshot][stage][exception]")
{
    FakeTransport transport;
    transport.throw_on_local_address = true;
    goldsrc::PostResourceEntitySnapshotStage stage{
        transport, network::NetworkAddress::loopback(31'798U)};
    std::size_t releases = 0U;
    const auto now = goldsrc::PostResourceEntitySnapshotStageTimePoint{};

    CHECK_FALSE(stage.start(
        now,
        transport.local,
        std::make_unique<CountingLifetime>(releases)));
    CHECK(stage.terminal());
    CHECK(stage.state() ==
          goldsrc::PostResourceEntitySnapshotStageState::protocol_error);
    REQUIRE(stage.error());
    CHECK(releases == 1U);
}

TEST_CASE("Destroying an active post-resource stage releases one lifetime",
          "[goldsrc][entity-snapshot][stage][cleanup]")
{
    FakeTransport transport;
    std::size_t releases = 0U;
    const auto now = goldsrc::PostResourceEntitySnapshotStageTimePoint{};
    {
        goldsrc::PostResourceEntitySnapshotStage stage{
            transport, network::NetworkAddress::loopback(31'798U)};
        REQUIRE(stage.start(
            now,
            transport.local,
            std::make_unique<CountingLifetime>(releases)));
        CHECK(releases == 0U);
    }
    CHECK(releases == 1U);
}

TEST_CASE(
    "Post-resource entity stage publishes the sealed baseline full and delta route",
    "[goldsrc][entity-snapshot][stage][integration][synthetic]")
{
    hlclient::test_support::require_entity_snapshot_happy_route();
}

TEST_CASE(
    "Post-resource entity stage ignores duplicate and wrong-endpoint publications",
    "[goldsrc][entity-snapshot][stage][integration][duplicate][wrong-endpoint]")
{
    hlclient::test_support::
        require_entity_snapshot_duplicate_and_wrong_endpoint_routes();
}

TEST_CASE(
    "Post-resource entity stage times out while awaiting full or delta publication",
    "[goldsrc][entity-snapshot][stage][integration][timeout]")
{
    hlclient::test_support::require_entity_snapshot_timeout_routes();
}

TEST_CASE(
    "Post-resource entity stage ignores an old delta after terminal publication",
    "[goldsrc][entity-snapshot][stage][integration][replay]")
{
    hlclient::test_support::require_entity_snapshot_replay_route();
}

TEST_CASE(
    "Post-resource entity stage cancellation releases authentication lifetime once",
    "[goldsrc][entity-snapshot][stage][integration][cleanup]")
{
    hlclient::test_support::require_entity_snapshot_cancellation_route();
}

} // namespace
