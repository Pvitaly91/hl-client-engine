#include "delta_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "user_info_test_fixture.hpp"

#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/resource_list_stage.hpp>
#include <hlclient/network/datagram_transport.hpp>

#include <catch2/catch_test_macros.hpp>

#include <bzlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace delta_fixture = hlclient::test::delta_fixture;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace user_fixture = hlclient::test::user_info_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

struct SentDatagram {
    network::NetworkAddress destination;
    std::vector<std::byte> payload;
};

class FakeTransport final : public network::IDatagramTransport {
public:
    [[nodiscard]] network::DatagramLocalAddressResult local_address() const override
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

    network::NetworkAddress local{network::NetworkAddress::loopback(31'701U)};
    std::vector<SentDatagram> sent;
    std::deque<network::DatagramTransportReceiveResult> incoming;
};

class CountingLifetime final : public goldsrc::INetchanDriverLifetime {
public:
    explicit CountingLifetime(std::size_t& releases) noexcept : releases_{releases} {}
    ~CountingLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

struct ForbiddenResourceConsumerCallCounts {
    std::size_t raw_resource_name_inputs{0U};
    std::size_t path_joins{0U};
    std::size_t canonicalizations{0U};
    std::size_t file_opens{0U};
    std::size_t file_stats{0U};
    std::size_t file_reads{0U};
    std::size_t vfs_mounts{0U};
    std::size_t asset_loads{0U};
    std::size_t cache_writes{0U};
    std::size_t download_starts{0U};
    std::size_t renderer_mutations{0U};

    [[nodiscard]] std::size_t total() const noexcept
    {
        return raw_resource_name_inputs + path_joins + canonicalizations +
               file_opens + file_stats + file_reads + vfs_mounts +
               asset_loads + cache_writes + download_starts +
               renderer_mutations;
    }
};

// The resource-list trace is the only injected observation seam. It receives
// bounded metadata, while every forbidden downstream consumer remains a
// separately counted mock operation that the protocol stage cannot invoke.
class MetadataOnlyResourceIsolationMock final {
public:
    explicit MetadataOnlyResourceIsolationMock(
        ForbiddenResourceConsumerCallCounts& forbidden_calls) noexcept
        : forbidden_calls_{forbidden_calls}
    {
    }

    void operator()(const goldsrc::ResourceListTraceEvent& event) noexcept
    {
        ++trace_calls;
        if (event.classification ==
            goldsrc::ResourceListTraceClassification::resource_entry_metadata) {
            ++entry_metadata_calls;
            observed_name_byte_count += event.resource_name_byte_count;
        }
    }

    // These methods model the complete prohibited consumer surface. None is
    // passed to ResourceListStage; an accidental protocol dependency would
    // require an explicit API/dependency change instead of silently reaching
    // one of these consumers through the metadata trace seam.
    void accept_resource_name(const goldsrc::ResourceName&) noexcept
    {
        ++forbidden_calls_.raw_resource_name_inputs;
    }
    void join_path() noexcept { ++forbidden_calls_.path_joins; }
    void canonicalize_path() noexcept { ++forbidden_calls_.canonicalizations; }
    void open_file() noexcept { ++forbidden_calls_.file_opens; }
    void stat_file() noexcept { ++forbidden_calls_.file_stats; }
    void read_file() noexcept { ++forbidden_calls_.file_reads; }
    void mount_vfs() noexcept { ++forbidden_calls_.vfs_mounts; }
    void load_asset() noexcept { ++forbidden_calls_.asset_loads; }
    void write_cache() noexcept { ++forbidden_calls_.cache_writes; }
    void start_download() noexcept { ++forbidden_calls_.download_starts; }
    void mutate_renderer() noexcept { ++forbidden_calls_.renderer_mutations; }

    std::size_t trace_calls{0U};
    std::size_t entry_metadata_calls{0U};
    std::size_t observed_name_byte_count{0U};

private:
    ForbiddenResourceConsumerCallCounts& forbidden_calls_;
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
            return static_cast<std::byte>(static_cast<unsigned char>(value));
        });
    return envelope;
}

[[nodiscard]] std::vector<std::byte> server_packet(
    const std::uint32_t packet_sequence,
    const bool reliable,
    const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement,
    std::vector<std::byte> payload)
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

[[nodiscard]] std::vector<std::byte> secondary_fragment_packet(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement)
{
    goldsrc::NetchanFragmentSlots fragments;
    fragments[1U] = goldsrc::NetchanFragmentDescriptor{
        1U,
        (1U << 16U) | 1U,
        0U,
        1U,
        0U,
    };
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(packet_sequence),
                goldsrc::NetchanSequenceFlags{true, true},
            },
            goldsrc::NetchanAcknowledgementWord{
                sequence(acknowledgement),
                reliable_acknowledgement,
            },
        },
        std::move(fragments),
        std::vector<std::byte>{std::byte{0x41U}},
        1U,
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return std::move(*encoded.datagram);
}

[[nodiscard]] goldsrc::ResourceListStageConfig test_config()
{
    goldsrc::ResourceListStageConfig config;
    auto& transition = config.transition;
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
    config.maximum_stage_events = 64U;
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

void append_lsb_bits(
    std::vector<std::byte>& bytes,
    std::size_t& bit_cursor,
    const std::uint32_t value,
    const std::size_t width)
{
    REQUIRE(width <= 32U);
    for (std::size_t bit = 0U; bit < width; ++bit) {
        const auto byte_index = bit_cursor / 8U;
        if (byte_index == bytes.size()) {
            bytes.push_back(std::byte{0U});
        }
        REQUIRE(byte_index < bytes.size());
        if (((value >> bit) & 1U) != 0U) {
            bytes[byte_index] |=
                static_cast<std::byte>(1U << (bit_cursor & 7U));
        }
        ++bit_cursor;
    }
}

void append_resource_entry(
    std::vector<std::byte>& payload,
    std::size_t& bit_cursor,
    const std::uint8_t type,
    const std::string_view name,
    const std::uint16_t index,
    const std::uint32_t size_code,
    const std::uint8_t flags)
{
    append_lsb_bits(payload, bit_cursor, type, 4U);
    for (const auto character : name) {
        append_lsb_bits(
            payload,
            bit_cursor,
            static_cast<std::uint8_t>(character),
            8U);
    }
    append_lsb_bits(payload, bit_cursor, 0U, 8U);
    append_lsb_bits(payload, bit_cursor, index, 12U);
    append_lsb_bits(payload, bit_cursor, size_code, 24U);
    append_lsb_bits(payload, bit_cursor, flags, 4U);
}

[[nodiscard]] std::vector<std::byte> resource_semantic_payload(
    const std::uint8_t second_flags = 1U,
    const std::string_view first_name = "models/test_model.mdl")
{
    std::vector<std::byte> payload{
        std::byte{45U},
        std::byte{1U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{43U},
    };
    std::size_t bit_cursor = payload.size() * 8U;
    append_lsb_bits(payload, bit_cursor, 2U, 12U);
    append_resource_entry(
        payload, bit_cursor, 2U, first_name, 7U, 0x001234U, 0U);
    append_resource_entry(
        payload, bit_cursor, 0U, "sound/test_sound.wav", 8U, 0x00ffffffU,
        second_flags);
    append_lsb_bits(payload, bit_cursor, 0U, 8U - (bit_cursor & 7U));
    REQUIRE((bit_cursor & 7U) == 0U);
    REQUIRE(payload.size() == bit_cursor / 8U);
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

struct DrivenStage {
    goldsrc::ClientToServerNetchanPacket transition_request;
};

[[nodiscard]] DrivenStage drive_to_transition_request(
    goldsrc::ResourceListStage& stage,
    FakeTransport& transport,
    const network::NetworkAddress remote,
    const goldsrc::ResourceListStageTimePoint epoch,
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
            initial.header.sequence.flags.reliable,
            service_envelope(first_semantic_payload())));
    stage.update(epoch + 2ms);
    CHECK(stage.initial_request_queue_count() == 1U);
    CHECK(stage.transition_request_queue_count() == 1U);

    stage.update(epoch + 3ms);
    REQUIRE(transport.sent.size() >= 3U);
    const auto transition = decode_sent(transport.sent.back());
    const std::array exact{
        std::byte{0x03U}, std::byte{'s'}, std::byte{'e'}, std::byte{'n'},
        std::byte{'d'}, std::byte{'r'}, std::byte{'e'}, std::byte{'s'},
        std::byte{0U},
    };
    REQUIRE(std::ranges::equal(transition.payload, exact));
    return DrivenStage{transition};
}

void deliver_resource_payload(
    goldsrc::ResourceListStage& stage,
    FakeTransport& transport,
    const network::NetworkAddress remote,
    const DrivenStage& driven,
    const goldsrc::ResourceListStageTimePoint now,
    std::vector<std::byte> semantic_payload)
{
    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            driven.transition_request.header.sequence.sequence.value(),
            false,
            service_envelope(semantic_payload)));
    stage.update(now);
}

TEST_CASE("Resource-list stage event limit has a default hard cap and exact publication boundary",
          "[goldsrc][resource-list][stage][limits][events]")
{
    const goldsrc::ResourceListStageConfig defaults;
    CHECK(defaults.maximum_stage_events ==
          goldsrc::kDefaultMaximumResourceListEvents);
    CHECK(goldsrc::valid_resource_list_stage_configuration(defaults));

    auto hard_cap = defaults;
    hard_cap.maximum_stage_events = goldsrc::kMaximumResourceListEvents;
    CHECK(goldsrc::valid_resource_list_stage_configuration(hard_cap));

    auto above_hard_cap = hard_cap;
    above_hard_cap.maximum_stage_events =
        goldsrc::kMaximumResourceListEvents + 1U;
    CHECK_FALSE(goldsrc::valid_resource_list_stage_configuration(
        above_hard_cap));

    SECTION("exact event capacity publishes the complete atomic batch")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'800U);
        const auto epoch = goldsrc::ResourceListStageTimePoint{} + 1s;
        auto config = test_config();
        // list-ready + two entries + post-list boundary + response boundary.
        config.maximum_stage_events = 5U;
        goldsrc::ResourceListStage stage{transport, remote, config};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        deliver_resource_payload(
            stage,
            transport,
            remote,
            driven,
            epoch + 4ms,
            resource_semantic_payload());

        REQUIRE(stage.state() ==
                goldsrc::ResourceListStageState::client_response_required);
        REQUIRE(stage.result());
        CHECK(stage.pending_event_count() == 5U);
    }

    SECTION("one event beyond capacity fails before partial publication")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'799U);
        const auto epoch = goldsrc::ResourceListStageTimePoint{} + 1s;
        auto config = test_config();
        config.maximum_stage_events = 4U;
        goldsrc::ResourceListStage stage{transport, remote, config};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        deliver_resource_payload(
            stage,
            transport,
            remote,
            driven,
            epoch + 4ms,
            resource_semantic_payload());

        REQUIRE(stage.state() == goldsrc::ResourceListStageState::backpressure);
        CHECK_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::ResourceListStageErrorCode::event_backpressure);
        CHECK(stage.pending_event_count() == 1U);
        const auto event = stage.poll_event();
        REQUIRE(event);
        CHECK(event->type == goldsrc::ResourceListStageEventType::backpressure);
    }
}

TEST_CASE("Resource-list stage atomically publishes the list and response boundary",
          "[goldsrc][resource-list][stage][integration][security]")
{
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'801U);
    const auto epoch = goldsrc::ResourceListStageTimePoint{} + 1s;
    std::size_t releases = 0U;
    goldsrc::ResourceListStage stage{transport, remote, test_config()};
    const auto driven = drive_to_transition_request(
        stage,
        transport,
        remote,
        epoch,
        std::make_unique<CountingLifetime>(releases));
    REQUIRE(releases == 0U);

    deliver_resource_payload(
        stage,
        transport,
        remote,
        driven,
        epoch + 4ms,
        resource_semantic_payload());

    REQUIRE(stage.state() ==
            goldsrc::ResourceListStageState::client_response_required);
    REQUIRE(stage.terminal());
    REQUIRE_FALSE(stage.error());
    REQUIRE(stage.result());
    const auto& result = *stage.result();
    REQUIRE(result.resource_list().resource_count() == 2U);
    CHECK(result.resource_list().source_opcode_byte_offset() == 9U);
    CHECK(result.resource_list().next_byte_offset() ==
          result.transition().source_payload().decompressed_byte_count());
    CHECK(result.boundary().kind() ==
          goldsrc::PostResourceListBoundaryKind::exact_end_of_payload);
    CHECK(result.boundary().remaining_byte_count() == 0U);
    CHECK(result.client_response().opcode_candidate() == 5U);
    CHECK_FALSE(result.client_response().response_builder_available());
    CHECK_FALSE(result.client_response().response_queued());
    CHECK(stage.response_queue_count() == 0U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);

    std::vector<goldsrc::ResourceListStageEvent> events;
    while (auto event = stage.poll_event()) {
        events.push_back(std::move(*event));
    }
    REQUIRE(events.size() == 5U);
    CHECK(events[0U].type ==
          goldsrc::ResourceListStageEventType::resource_list_ready);
    CHECK(events[1U].type ==
          goldsrc::ResourceListStageEventType::resource_entry_metadata);
    CHECK(events[2U].type ==
          goldsrc::ResourceListStageEventType::resource_entry_metadata);
    CHECK(events[3U].type ==
          goldsrc::ResourceListStageEventType::post_resource_boundary);
    CHECK(events[4U].type ==
          goldsrc::ResourceListStageEventType::client_response_required);
    CHECK(events[1U].resource_name_byte_count == 21U);
    CHECK(events[1U].resource_type == goldsrc::ResourceType::model);
    CHECK_FALSE(events[4U].opcode);

    const auto sends_at_boundary = transport.sent.size();
    stage.update(epoch + 20ms);
    stage.cancel(epoch + 30ms);
    CHECK(transport.sent.size() == sends_at_boundary);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(releases == 1U);
}

TEST_CASE("Resource-list stage isolation seam exposes names as metadata only",
           "[goldsrc][resource-list][stage][filesystem][security]")
{
    ForbiddenResourceConsumerCallCounts forbidden_calls;
    MetadataOnlyResourceIsolationMock isolation_mock{forbidden_calls};
    constexpr std::array names{
        std::string_view{"maps/test.bsp"},
        std::string_view{"../evil"},
    };

    for (std::size_t run = 0U; run < names.size(); ++run) {
        INFO("resource metadata isolation run=" << run);
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(
            static_cast<std::uint16_t>(27'802U + run));
        const auto epoch = goldsrc::ResourceListStageTimePoint{} +
            std::chrono::seconds{1 + static_cast<std::int64_t>(run)};
        goldsrc::ResourceListStage stage{
            transport,
            remote,
            test_config(),
            {}, {}, {}, {}, {}, {},
            goldsrc::ResourceListTraceCallback{std::ref(isolation_mock)}};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        deliver_resource_payload(
            stage,
            transport,
            remote,
            driven,
            epoch + 4ms,
            resource_semantic_payload(1U, names[run]));

        REQUIRE(stage.result());
        REQUIRE(stage.result()->resource_list().entries().size() == 2U);
        CHECK(stage.result()->resource_list().entries()[0U].name().bytes() ==
              names[run]);
        CHECK(stage.response_queue_count() == 0U);
    }

    CHECK(isolation_mock.entry_metadata_calls == 4U);
    CHECK(isolation_mock.observed_name_byte_count ==
          names[0U].size() + names[1U].size() + 2U *
              std::string_view{"sound/test_sound.wav"}.size());
    CHECK(isolation_mock.trace_calls > isolation_mock.entry_metadata_calls);
    CHECK(forbidden_calls.raw_resource_name_inputs == 0U);
    CHECK(forbidden_calls.path_joins == 0U);
    CHECK(forbidden_calls.canonicalizations == 0U);
    CHECK(forbidden_calls.file_opens == 0U);
    CHECK(forbidden_calls.file_stats == 0U);
    CHECK(forbidden_calls.file_reads == 0U);
    CHECK(forbidden_calls.vfs_mounts == 0U);
    CHECK(forbidden_calls.asset_loads == 0U);
    CHECK(forbidden_calls.cache_writes == 0U);
    CHECK(forbidden_calls.download_starts == 0U);
    CHECK(forbidden_calls.renderer_mutations == 0U);
    CHECK(forbidden_calls.total() == 0U);
}

TEST_CASE("Resource-list stage failures are typed transactional and clean once",
          "[goldsrc][resource-list][stage][negative][security]")
{
    SECTION("truncated list")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'803U);
        const auto epoch = goldsrc::ResourceListStageTimePoint{} + 1s;
        std::size_t releases = 0U;
        goldsrc::ResourceListStage stage{transport, remote, test_config()};
        const auto driven = drive_to_transition_request(
            stage,
            transport,
            remote,
            epoch,
            std::make_unique<CountingLifetime>(releases));
        std::vector<std::byte> truncated{
            std::byte{45U},
            std::byte{1U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
            std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
            std::byte{43U},
            // M3.1.1 requires a non-empty neutral opcode-43 body. Eight bits
            // pass that boundary but remain short of the 12-bit list count.
            std::byte{0U},
        };
        deliver_resource_payload(
            stage, transport, remote, driven, epoch + 4ms, std::move(truncated));
        REQUIRE(stage.state() == goldsrc::ResourceListStageState::protocol_error);
        REQUIRE_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.error()->code ==
              goldsrc::ResourceListStageErrorCode::resource_list_decode_failed);
        CHECK(stage.error()->resource_list_code ==
              goldsrc::ResourceListErrorCode::truncated_count);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }

    SECTION("unobserved flags profile remains typed unsupported")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'804U);
        const auto epoch = goldsrc::ResourceListStageTimePoint{} + 1s;
        goldsrc::ResourceListStage stage{transport, remote, test_config()};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        deliver_resource_payload(
            stage,
            transport,
            remote,
            driven,
            epoch + 4ms,
            resource_semantic_payload(4U));
        REQUIRE(stage.state() ==
                goldsrc::ResourceListStageState::unsupported_resource_profile);
        REQUIRE(stage.error());
        CHECK(stage.error()->resource_list_code ==
              goldsrc::ResourceListErrorCode::unsupported_resource_profile);
        const auto event = stage.poll_event();
        REQUIRE(event);
        CHECK(event->type ==
              goldsrc::ResourceListStageEventType::unsupported_resource_profile);
    }

    SECTION("atomic event preflight")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'805U);
        const auto epoch = goldsrc::ResourceListStageTimePoint{} + 1s;
        auto config = test_config();
        config.maximum_stage_events = 2U;
        goldsrc::ResourceListStage stage{transport, remote, config};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        deliver_resource_payload(
            stage,
            transport,
            remote,
            driven,
            epoch + 4ms,
            resource_semantic_payload());
        REQUIRE(stage.state() == goldsrc::ResourceListStageState::backpressure);
        REQUIRE_FALSE(stage.result());
        REQUIRE(stage.error());
        CHECK(stage.pending_event_count() == 1U);
        const auto event = stage.poll_event();
        REQUIRE(event);
        CHECK(event->type == goldsrc::ResourceListStageEventType::backpressure);
    }

    SECTION("wrong opcode-43 transition cursor")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'806U);
        const auto epoch = goldsrc::ResourceListStageTimePoint{} + 1s;
        goldsrc::ResourceListStage stage{transport, remote, test_config()};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        auto malformed = resource_semantic_payload();
        malformed[9U] = std::byte{42U};
        deliver_resource_payload(
            stage, transport, remote, driven, epoch + 4ms, std::move(malformed));
        REQUIRE(stage.state() == goldsrc::ResourceListStageState::protocol_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->transition_code ==
              goldsrc::ResourceTransitionStageErrorCode::
                  transition_control_decode_failed);
    }
}

TEST_CASE("Resource-list stage propagates lifecycle and driver terminals",
          "[goldsrc][resource-list][stage][driver][security]")
{
    SECTION("timeout")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'807U);
        const auto epoch = goldsrc::ResourceListStageTimePoint{} + 1s;
        goldsrc::ResourceListStage stage{transport, remote, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        stage.update(epoch + 1ms);
        stage.update(epoch + 201ms);
        CHECK(stage.state() == goldsrc::ResourceListStageState::timed_out);
        const auto event = stage.poll_event();
        REQUIRE(event);
        CHECK(event->type == goldsrc::ResourceListStageEventType::timeout);
    }

    SECTION("cancellation releases authentication exactly once")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'808U);
        const auto epoch = goldsrc::ResourceListStageTimePoint{} + 1s;
        std::size_t releases = 0U;
        goldsrc::ResourceListStage stage{transport, remote, test_config()};
        REQUIRE(stage.start(
            epoch,
            transport.local,
            std::make_unique<CountingLifetime>(releases)));
        stage.cancel(epoch + 1ms);
        stage.cancel(epoch + 2ms);
        REQUIRE(stage.state() == goldsrc::ResourceListStageState::cancelled);
        CHECK(stage.cleanup_count() == 1U);
        CHECK(releases == 1U);
    }

    SECTION("secondary stream remains typed pending")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'809U);
        const auto epoch = goldsrc::ResourceListStageTimePoint{} + 1s;
        goldsrc::ResourceListStage stage{transport, remote, test_config()};
        const auto driven = drive_to_transition_request(
            stage, transport, remote, epoch);
        transport.queue(
            remote,
            secondary_fragment_packet(
                2U,
                driven.transition_request.header.sequence.sequence.value(),
                false));
        stage.update(epoch + 4ms);
        CHECK(stage.state() ==
              goldsrc::ResourceListStageState::secondary_stream_pending);
        REQUIRE(stage.error());
        CHECK(stage.error()->driver_code ==
              goldsrc::NetchanDriverErrorCode::secondary_stream_pending_m3);
    }

    SECTION("local endpoint drift remains a network error")
    {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'810U);
        const auto epoch = goldsrc::ResourceListStageTimePoint{} + 1s;
        goldsrc::ResourceListStage stage{transport, remote, test_config()};
        REQUIRE(stage.start(epoch, transport.local));
        transport.local = network::NetworkAddress::loopback(31'799U);
        stage.update(epoch + 1ms);
        CHECK(stage.state() == goldsrc::ResourceListStageState::network_error);
        REQUIRE(stage.error());
        CHECK(stage.error()->driver_code ==
              goldsrc::NetchanDriverErrorCode::local_endpoint_changed);
    }
}

} // namespace
