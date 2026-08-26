#include "delta_test_fixture.hpp"
#include "entity_snapshot_fake_hlds_test_support.hpp"
#include "handshake_coordinator_test_access.hpp"
#include "move_vars_test_fixture.hpp"
#include "resource_client_response_test_fixture.hpp"
#include "resource_list_test_fixture.hpp"
#include "user_info_test_fixture.hpp"

#include <hlclient/auth/authentication_provider.hpp>
#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/delta_value_decoder.hpp>
#include <hlclient/goldsrc/entity_snapshot.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
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
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace auth = hlclient::auth;
namespace consistency = hlclient::resource_consistency;
namespace delta_fixture = hlclient::test::delta_fixture;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace response_fixture =
    hlclient::test::resource_client_response_fixture;
namespace user_fixture = hlclient::test::user_info_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace network = hlclient::network;

inline constexpr std::string_view kAuthenticationMarker =
    "SYNTHETIC_ENTITY_AUTH";
inline constexpr std::string_view kProtectedAuthentication =
    "SYNTHETIC_ENTITY_AUTH_MATERIAL__";

// Independently authored expected stage response. It is intentionally a
// literal, not output from the production response builder.
inline constexpr std::array<std::byte, 41U> kExactTempdecalResponse{
    std::byte{0x05U},
    std::byte{0x01U}, std::byte{0x00U},
    std::byte{'t'}, std::byte{'e'}, std::byte{'m'}, std::byte{'p'},
    std::byte{'d'}, std::byte{'e'}, std::byte{'c'}, std::byte{'a'},
    std::byte{'l'}, std::byte{'.'}, std::byte{'w'}, std::byte{'a'},
    std::byte{'d'}, std::byte{0x00U},
    std::byte{0x03U},
    std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x04U}, std::byte{0x03U}, std::byte{0x02U}, std::byte{0x01U},
    std::byte{0x04U},
    std::byte{0xa0U}, std::byte{0xa1U}, std::byte{0xa2U}, std::byte{0xa3U},
    std::byte{0xa4U}, std::byte{0xa5U}, std::byte{0xa6U}, std::byte{0xa7U},
    std::byte{0xa8U}, std::byte{0xa9U}, std::byte{0xaaU}, std::byte{0xabU},
    std::byte{0xacU}, std::byte{0xadU}, std::byte{0xaeU}, std::byte{0xafU},
};

inline constexpr std::array<std::byte, 8U> kExactInitialWireRequest{
    std::byte{0x03U}, std::byte{'n'}, std::byte{'e'}, std::byte{'w'},
    std::byte{0x00U}, std::byte{0x01U}, std::byte{0x01U},
    std::byte{0x01U}};

inline constexpr std::array<std::byte, 9U> kExactTransitionRequest{
    std::byte{0x03U}, std::byte{'s'}, std::byte{'e'}, std::byte{'n'},
    std::byte{'d'}, std::byte{'r'}, std::byte{'e'}, std::byte{'s'},
    std::byte{0x00U}};

inline constexpr std::array<std::byte, 8U>
    kExactSyntheticContinuationWire{
        std::byte{0xfdU}, std::byte{0x45U}, std::byte{0x01U},
        std::byte{0x01U}, std::byte{0x01U}, std::byte{0x01U},
        std::byte{0x01U}, std::byte{0x01U}};

inline constexpr delta_fixture::Field kEntityStateFields[]{
    {"origin[0]", 0x8000'0004U, 0U, 16U, 32'000U, 4'000U},
    {"origin[1]", 0x8000'0004U, 4U, 16U, 32'000U, 4'000U},
    {"origin[2]", 0x8000'0004U, 8U, 16U, 32'000U, 4'000U},
    {"angles[0]", 0x0000'0010U, 12U, 8U, 400U, 4'000U},
    {"angles[1]", 0x0000'0010U, 16U, 8U, 400U, 4'000U},
    {"angles[2]", 0x0000'0010U, 20U, 8U, 400U, 4'000U},
};

struct SentDatagram {
    network::NetworkAddress destination;
    std::vector<std::byte> payload;
};

class FakeHldsTransport final : public network::IDatagramTransport {
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
        sent.push_back({
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

    network::NetworkAddress local{
        network::NetworkAddress::loopback(31'791U)};
    std::vector<SentDatagram> sent;
    std::deque<network::DatagramTransportReceiveResult> incoming;
};

class CountingAuthenticationLifetime final
    : public auth::IAuthenticationSessionLifetime {
public:
    explicit CountingAuthenticationLifetime(std::size_t& releases) noexcept
        : releases_{releases}
    {
    }

    ~CountingAuthenticationLifetime() override { ++releases_; }

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

class SyntheticConsistencyOperation final
    : public consistency::ResourceConsistencyOperation {
public:
    SyntheticConsistencyOperation(
        std::size_t& updates,
        std::size_t& cancels,
        std::size_t& releases) noexcept
        : updates_{updates}, cancels_{cancels}, releases_{releases}
    {
    }

    [[nodiscard]] consistency::ResourceConsistencyUpdateResult update()
        override
    {
        ++updates_;
        auto material = consistency::make_resource_consistency_material(
            0x0102'0304U,
            response_fixture::kSyntheticOpaqueMaterial);
        REQUIRE(material);
        return consistency::ResourceConsistencyUpdateResult::succeeded(
            consistency::ResourceConsistencySession{
                std::move(*material.material),
                std::make_unique<CountingConsistencyLifetime>(releases_)});
    }

    void cancel() noexcept override
    {
        if (!cancelled_) {
            cancelled_ = true;
            ++cancels_;
        }
    }

private:
    std::size_t& updates_;
    std::size_t& cancels_;
    std::size_t& releases_;
    bool cancelled_{false};
};

class SyntheticConsistencyProvider final
    : public consistency::IResourceConsistencyProvider {
public:
    [[nodiscard]] consistency::ResourceConsistencyBeginResult begin(
        const consistency::ResourceConsistencyRequirements& requirements)
        override
    {
        ++begins;
        material_count = requirements.material_count();
        opaque_byte_count = requirements.opaque_bytes_per_material();
        return consistency::ResourceConsistencyBeginResult::started(
            std::make_unique<SyntheticConsistencyOperation>(
                updates, cancels, lifetime_releases));
    }

    std::size_t begins{0U};
    std::size_t updates{0U};
    std::size_t cancels{0U};
    std::size_t lifetime_releases{0U};
    std::size_t material_count{0U};
    std::size_t opaque_byte_count{0U};
};

struct TraceCounts {
    std::size_t request_ready{0U};
    std::size_t request_queued{0U};
    std::size_t request_acknowledged{0U};
    std::size_t baselines_ready{0U};
    std::size_t full_snapshots_ready{0U};
    std::size_t delta_snapshots_ready{0U};
    std::size_t removals{0U};
    std::size_t history_updates{0U};
    std::size_t endpoint_mismatches{0U};
    std::size_t publication_state_mismatches{0U};
    std::size_t request_semantic_bytes{0U};
};

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text)
{
    const auto raw = std::as_bytes(std::span{text.data(), text.size()});
    return {raw.begin(), raw.end()};
}

[[nodiscard]] goldsrc::ConnectCompatibilityProfile connect_profile()
{
    auto profile = goldsrc::ConnectCompatibilityProfile{};
    profile.protected_authentication_is_ascii_hex = false;
    return profile;
}

struct PreparedWithSession {
    goldsrc::PreparedConnectRequest request;
    auth::AuthenticationSession session;
};

[[nodiscard]] PreparedWithSession prepared_request_with_session(
    std::size_t& releases)
{
    std::vector<std::byte> suffix(
        goldsrc::kObservedConnectAuthenticationSuffixSize);
    const auto marker = bytes(kAuthenticationMarker);
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        suffix[index] = marker[index % marker.size()];
    }
    auto material = goldsrc::AuthenticationMaterial::create(
        bytes(kProtectedAuthentication), suffix);
    REQUIRE(material);
    auth::AuthenticationSession session{
        std::move(*material.value),
        std::make_unique<CountingAuthenticationLifetime>(releases)};
    auto transferred = session.take_material();
    REQUIRE(transferred);
    auto prepared = goldsrc::prepare_connect_request(
        {}, std::move(*transferred), connect_profile());
    REQUIRE(prepared);
    return {std::move(*prepared.value), std::move(session)};
}

[[nodiscard]] std::vector<std::byte> challenge_response(
    const std::uint32_t challenge)
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "A00000000 " + std::to_string(challenge) +
              " 3 72057594037927936 0\n";
    packet.push_back('\0');
    return bytes(packet);
}

[[nodiscard]] std::vector<std::byte> accepted_response(
    const network::NetworkAddress client)
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "B 1 \"" + client.to_string() + "\" 0 10210";
    packet.push_back('\0');
    return bytes(packet);
}

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
        std::byte{0x32U}, std::byte{0x00U}};
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
                goldsrc::NetchanSequenceFlags{reliable, false}},
            goldsrc::NetchanAcknowledgementWord{
                sequence(acknowledgement), reliable_acknowledgement}},
        {},
        std::move(payload),
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return std::move(*encoded.datagram);
}

[[nodiscard]] std::vector<std::byte> server_single_fragment_packet(
    const std::uint32_t packet_sequence,
    const bool reliable,
    const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement,
    std::vector<std::byte> payload)
{
    REQUIRE_FALSE(payload.empty());
    REQUIRE(payload.size() <=
            (std::numeric_limits<std::uint16_t>::max)());
    const auto payload_size = payload.size();
    goldsrc::NetchanFragmentSlots fragments;
    fragments[0U] = goldsrc::NetchanFragmentDescriptor{
        0U,
        (1U << 16U) | 1U,
        0U,
        static_cast<std::uint16_t>(payload_size),
        0U,
    };
    const goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(packet_sequence),
                goldsrc::NetchanSequenceFlags{reliable, true}},
            goldsrc::NetchanAcknowledgementWord{
                sequence(acknowledgement), reliable_acknowledgement}},
        std::move(fragments),
        std::move(payload),
        payload_size,
    };
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return std::move(*encoded.datagram);
}

[[nodiscard]] std::vector<std::vector<std::byte>> schemas(
    const bool include_required_entity_schema = true)
{
    std::vector<std::vector<std::byte>> result;
    result.reserve(4U);
    if (include_required_entity_schema) {
        result.push_back(delta_fixture::schema(
            "entity_state_t", kEntityStateFields));
    } else {
        result.push_back(delta_fixture::schema(
            "synthetic_foreign_entity_t", kEntityStateFields));
    }
    result.push_back(delta_fixture::schema(
        "entity_state_player_t", kEntityStateFields));
    result.push_back(delta_fixture::schema(
        "custom_entity_state_t", kEntityStateFields));
    result.push_back(delta_fixture::schema(
        "bravo_t", delta_fixture::kSchemaBravoFields));
    return result;
}

[[nodiscard]] std::vector<std::byte> first_semantic_payload(
    const bool include_required_entity_schema = true)
{
    std::vector<std::byte> post_delta;
    move_fixture::append_move_vars_body(post_delta);
    move_fixture::append_confirmed_controls(post_delta);
    post_delta.insert(
        post_delta.end(),
        user_fixture::kExactUserInfoMessage.begin(),
        user_fixture::kExactUserInfoMessage.end());
    return delta_fixture::service_payload(
        schemas(include_required_entity_schema),
        goldsrc::kMoveVarsOpcode,
        post_delta);
}

[[nodiscard]] std::vector<std::byte> resource_semantic_payload()
{
    constexpr std::array prefix{
        std::byte{45U},
        std::byte{1U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U}};
    std::vector<std::byte> payload{prefix.begin(), prefix.end()};
    payload.insert(
        payload.end(),
        resource_list_test_fixture::kExactResourceListMessage.begin(),
        resource_list_test_fixture::kExactResourceListMessage.end());
    return payload;
}

[[nodiscard]] goldsrc::ResourceClientResponseStageConfig response_config()
{
    goldsrc::ResourceClientResponseStageConfig config;
    auto& transition = config.resource_list.transition;
    auto& driver = transition.user_info.movement_environment.delta.pre_resource
                       .initial_signon.driver;
    driver.channel_inactivity_timeout = 250ms;
    driver.fragment_transfer_timeout = 250ms;
    driver.maximum_datagrams_per_update = 16U;
    driver.maximum_outgoing_packets_per_update = 8U;
    driver.maximum_events = 64U;
    transition.user_info.movement_environment.delta.pre_resource.initial_signon
        .maximum_events = 64U;
    transition.user_info.movement_environment.delta.pre_resource.initial_signon
        .maximum_driver_events_per_update = 64U;
    transition.user_info.movement_environment.delta.pre_resource
        .maximum_events = 64U;
    transition.user_info.movement_environment.delta.maximum_events = 64U;
    transition.user_info.movement_environment.maximum_events = 64U;
    transition.user_info.maximum_stage_events = 64U;
    transition.maximum_stage_events = 64U;
    transition.maximum_driver_events_per_update = 64U;
    config.resource_list.maximum_stage_events = 64U;
    config.maximum_driver_events_per_update = 64U;
    config.response.maximum_response_stage_events = 64U;
    return config;
}

[[nodiscard]] goldsrc::ChallengeExchangeConfig challenge_config()
{
    goldsrc::ChallengeExchangeConfig config;
    config.retry_interval = 100ms;
    config.timeout = 350ms;
    config.maximum_attempts = 2U;
    config.maximum_datagrams_per_update = 8U;
    return config;
}

[[nodiscard]] goldsrc::ConnectResponseWaitConfig connect_response_config()
{
    goldsrc::ConnectResponseWaitConfig config;
    config.timeout = 250ms;
    config.maximum_datagrams_per_update = 8U;
    return config;
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket decode_client_packet(
    const SentDatagram& datagram)
{
    const auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        datagram.payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    return *decoded.packet;
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket find_exact_payload(
    const FakeHldsTransport& transport,
    const std::size_t first,
    const std::span<const std::byte> expected)
{
    CAPTURE(first, transport.sent.size(), expected.size());
    std::optional<goldsrc::ClientToServerNetchanPacket> found;
    for (std::size_t index = first; index < transport.sent.size(); ++index) {
        const auto decoded = goldsrc::decode_client_to_server_netchan_packet(
            transport.sent[index].payload);
        if (decoded && decoded.packet &&
            std::ranges::equal(decoded.packet->payload, expected)) {
            found.emplace(std::move(*decoded.packet));
            break;
        }
    }
    REQUIRE(found);
    return std::move(*found);
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket latest_client_packet(
    const FakeHldsTransport& transport,
    const std::size_t netchan_start)
{
    REQUIRE(transport.sent.size() > netchan_start);
    std::optional<goldsrc::ClientToServerNetchanPacket> found;
    for (std::size_t offset = 0U;
         offset < transport.sent.size() - netchan_start;
         ++offset) {
        const auto index = transport.sent.size() - 1U - offset;
        const auto decoded = goldsrc::decode_client_to_server_netchan_packet(
            transport.sent[index].payload);
        if (decoded && decoded.packet) {
            found.emplace(std::move(*decoded.packet));
            break;
        }
    }
    REQUIRE(found);
    return std::move(*found);
}

[[nodiscard]] std::size_t exact_payload_count(
    const FakeHldsTransport& transport,
    const std::size_t first,
    const std::span<const std::byte> expected)
{
    std::size_t count = 0U;
    for (std::size_t index = first; index < transport.sent.size(); ++index) {
        const auto decoded = goldsrc::decode_client_to_server_netchan_packet(
            transport.sent[index].payload);
        if (decoded && decoded.packet &&
            std::ranges::equal(decoded.packet->payload, expected)) {
            ++count;
        }
    }
    return count;
}

constexpr auto kSyntheticDeltaValueProfile =
    goldsrc::DeltaValueCompatibilityProfile::synthetic_neutral_v1;
constexpr auto kSyntheticEntitySnapshotProfile =
    goldsrc::EntitySnapshotCompatibilityProfile::synthetic_neutral_v1;

[[nodiscard]] goldsrc::DeltaObjectState default_delta_object(
    const goldsrc::DeltaSchema& schema)
{
    std::vector<goldsrc::DeltaScalarValue> values(
        schema.fields().size(), double{0.0});
    auto built = goldsrc::DeltaObjectBuilder{
        {}, kSyntheticDeltaValueProfile};
    auto result = built.build(schema, values);
    REQUIRE(result);
    REQUIRE(result.state);
    return std::move(*result.state);
}

void require_missing_delta_base_rejected(
    const goldsrc::PostResourceSignonState& result)
{
    REQUIRE(result.baseline_registry());
    goldsrc::EntitySnapshotHistoryBuilder empty_history_builder{
        {}, kSyntheticEntitySnapshotProfile};
    const auto empty_history = empty_history_builder.publish();
    REQUIRE(empty_history);
    REQUIRE(empty_history.state);

    const goldsrc::EntityDeltaSnapshotBuilder delta_builder{
        {}, kSyntheticEntitySnapshotProfile};
    const auto missing = delta_builder.build(
        goldsrc::EntitySnapshotReference::synthetic(11U),
        goldsrc::EntityServerTime::synthetic_raw(11),
        goldsrc::EntitySnapshotReference::synthetic(10U),
        *empty_history.state,
        *result.baseline_registry(),
        std::span<const goldsrc::EntitySnapshotEntityInput>{},
        std::span<const std::uint32_t>{});
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error);
    CHECK(missing.error->code ==
          goldsrc::EntitySnapshotErrorCode::missing_delta_snapshot_base);
    CHECK_FALSE(missing.state);
}

void require_malformed_runtime_mask_rejected(
    const goldsrc::PostResourceSignonState& result)
{
    const auto* const schema =
        result.delta_registry().find_exact("entity_state_t");
    REQUIRE(schema != nullptr);
    // Literal synthetic runtime-mask bytes. This is not a post-resource wire
    // frame: the two-byte mask length cannot fit this six-field schema.
    constexpr std::array malformed_mask{
        std::byte{0x02U}, std::byte{0x00U}, std::byte{0x00U}};
    const goldsrc::GoldSrcDeltaValueDecoder decoder{
        {}, kSyntheticDeltaValueProfile};
    const auto decoded = decoder.decode_delta(
        *schema,
        nullptr,
        goldsrc::DeltaValueDecodeContext{
            std::span<const std::byte>{malformed_mask},
            0U,
            malformed_mask.size() * 8U,
            std::nullopt});
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error);
    CHECK(decoded.error->code ==
          goldsrc::DeltaValueErrorCode::mask_length_exceeds_schema);
    CHECK_FALSE(decoded.state);
    CHECK(decoded.bits_consumed == 0U);
}

void require_invalid_typed_baseline_rejected(
    const goldsrc::PostResourceSignonState& result)
{
    REQUIRE(result.baseline_registry());
    const auto* const source = result.baseline_registry()->find_exact(
        goldsrc::EntityBaselineKey::for_entity(1U));
    REQUIRE(source != nullptr);
    goldsrc::EntityBaselineRegistryBuilder builder{
        result.delta_registry(), {}, kSyntheticEntitySnapshotProfile};
    const auto malformed = builder.insert(
        goldsrc::EntityBaselineKey::for_entity(50U),
        goldsrc::EntitySchemaCategory::ordinary_entity,
        source->object(),
        goldsrc::EntitySourceGeometry{0U, 1U, 7U, 2U});
    REQUIRE_FALSE(malformed);
    REQUIRE(malformed.error);
    CHECK(malformed.error->code ==
          goldsrc::EntityBaselineErrorCode::invalid_source_geometry);
    CHECK(builder.candidate_baselines().empty());
}

void require_player_and_custom_typed_variants(
    const goldsrc::PostResourceSignonState& result)
{
    const auto* const player_schema =
        result.delta_registry().find_exact("entity_state_player_t");
    const auto* const custom_schema =
        result.delta_registry().find_exact("custom_entity_state_t");
    REQUIRE(player_schema != nullptr);
    REQUIRE(custom_schema != nullptr);
    const auto player_object = default_delta_object(*player_schema);
    const auto custom_object = default_delta_object(*custom_schema);

    const auto player_key = goldsrc::EntityBaselineKey::for_entity(10U);
    const auto custom_key =
        goldsrc::EntityBaselineKey::for_alternate_slot(4U);
    goldsrc::EntityBaselineRegistryBuilder baseline_builder{
        result.delta_registry(), {}, kSyntheticEntitySnapshotProfile};
    REQUIRE(baseline_builder.insert(
        player_key,
        goldsrc::EntitySchemaCategory::player_entity,
        player_object,
        goldsrc::EntitySourceGeometry{10U, 2U, 0U, 8U}));
    REQUIRE(baseline_builder.insert(
        custom_key,
        goldsrc::EntitySchemaCategory::custom_entity,
        custom_object,
        goldsrc::EntitySourceGeometry{10U, 2U, 8U, 8U}));
    auto baselines = std::move(baseline_builder).publish();
    REQUIRE(baselines);
    REQUIRE(baselines.state);

    const std::array full_inputs{
        goldsrc::EntitySnapshotEntityInput::from_baseline(10U, player_key),
        goldsrc::EntitySnapshotEntityInput::from_baseline(11U, custom_key),
    };
    const goldsrc::EntityFullSnapshotBuilder full_builder{
        {}, kSyntheticEntitySnapshotProfile};
    auto full = full_builder.build(
        goldsrc::EntitySnapshotReference::synthetic(50U),
        goldsrc::EntityServerTime::synthetic_raw(50),
        *baselines.state,
        full_inputs,
        goldsrc::EntitySourceGeometry{11U, 2U, 0U, 16U});
    REQUIRE(full);
    REQUIRE(full.state);
    REQUIRE(full.state->find_exact(10U));
    REQUIRE(full.state->find_exact(11U));
    CHECK(full.state->find_exact(10U)->schema_category() ==
          goldsrc::EntitySchemaCategory::player_entity);
    CHECK(full.state->find_exact(10U)->object().schema_name() ==
          "entity_state_player_t");
    CHECK(full.state->find_exact(11U)->schema_category() ==
          goldsrc::EntitySchemaCategory::custom_entity);
    CHECK(full.state->find_exact(11U)->object().schema_name() ==
          "custom_entity_state_t");

    goldsrc::EntitySnapshotHistoryBuilder history_builder{
        {}, kSyntheticEntitySnapshotProfile};
    REQUIRE(history_builder.insert(*full.state));
    const auto history = history_builder.publish();
    REQUIRE(history);
    REQUIRE(history.state);
    const std::array delta_updates{
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            10U, player_key, player_object),
        goldsrc::EntitySnapshotEntityInput::with_decoded_state(
            11U, custom_key, custom_object),
    };
    const goldsrc::EntityDeltaSnapshotBuilder delta_builder{
        {}, kSyntheticEntitySnapshotProfile};
    auto delta = delta_builder.build(
        goldsrc::EntitySnapshotReference::synthetic(51U),
        goldsrc::EntityServerTime::synthetic_raw(51),
        goldsrc::EntitySnapshotReference::synthetic(50U),
        *history.state,
        *baselines.state,
        delta_updates,
        std::span<const std::uint32_t>{},
        goldsrc::EntitySourceGeometry{12U, 2U, 0U, 16U});
    REQUIRE(delta);
    REQUIRE(delta.state);
    REQUIRE(delta.state->find_exact(10U));
    REQUIRE(delta.state->find_exact(11U));
    CHECK(delta.state->find_exact(10U)->schema_category() ==
          goldsrc::EntitySchemaCategory::player_entity);
    CHECK(delta.state->find_exact(11U)->schema_category() ==
          goldsrc::EntitySchemaCategory::custom_entity);
}

void require_stage_delta_classified_old(
    const goldsrc::PostResourceSignonState& result)
{
    REQUIRE(result.baseline_registry());
    REQUIRE(result.snapshot_history());
    const auto* const full = result.snapshot_history()->find_exact(
        goldsrc::EntitySnapshotReference::synthetic(1U));
    const auto* const stage_delta = result.snapshot_history()->find_exact(
        goldsrc::EntitySnapshotReference::synthetic(2U));
    REQUIRE(full != nullptr);
    REQUIRE(stage_delta != nullptr);
    REQUIRE(stage_delta->kind() == goldsrc::EntitySnapshotKind::delta);

    const std::array newer_inputs{
        goldsrc::EntitySnapshotEntityInput::from_baseline(
            1U, goldsrc::EntityBaselineKey::for_entity(1U)),
    };
    const goldsrc::EntityFullSnapshotBuilder full_builder{
        {}, kSyntheticEntitySnapshotProfile};
    auto newer = full_builder.build(
        goldsrc::EntitySnapshotReference::synthetic(3U),
        goldsrc::EntityServerTime::synthetic_raw(3),
        *result.baseline_registry(),
        newer_inputs,
        goldsrc::EntitySourceGeometry{20U, 1U, 0U, 8U});
    REQUIRE(newer);
    REQUIRE(newer.state);

    goldsrc::EntitySnapshotHistoryBuilder history_builder{
        {}, kSyntheticEntitySnapshotProfile};
    REQUIRE(history_builder.insert(*full));
    REQUIRE(history_builder.insert(*newer.state));
    CHECK(history_builder.candidate_snapshot_count() == 2U);
    const auto classified = history_builder.insert(*stage_delta);
    REQUIRE_FALSE(classified);
    REQUIRE(classified.error);
    CHECK(classified.error->code ==
          goldsrc::EntitySnapshotHistoryErrorCode::old_snapshot);
    CHECK(history_builder.candidate_snapshot_count() == 2U);
}

[[nodiscard]] goldsrc::GoldSrcHandshakeCoordinator make_coordinator(
    FakeHldsTransport& transport,
    const network::NetworkAddress remote,
    PreparedWithSession prepared,
    SyntheticConsistencyProvider& provider,
    TraceCounts& traces,
    const std::size_t maximum_post_resource_messages =
        goldsrc::kDefaultMaximumPostResourceMessages,
    const goldsrc::HandshakeStopPoint stop_point =
        goldsrc::HandshakeStopPoint::entity_snapshot,
    const std::size_t maximum_stage_events =
        goldsrc::kDefaultMaximumPostResourceStageEvents)
{
    auto nested_response = response_config();
    auto post = goldsrc::PostResourceEntitySnapshotStageConfig{};
    post.profile =
        goldsrc::PostResourceSignonCompatibilityProfile::synthetic_neutral_v1;
    post.post_resource.maximum_post_resource_messages =
        maximum_post_resource_messages;
    post.timeout = 250ms;
    post.maximum_stage_events = maximum_stage_events;
    post.maximum_driver_events_per_update = 128U;

    return goldsrc::GoldSrcHandshakeCoordinator{
        transport,
        remote,
        stop_point,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        connect_response_config(),
        {},
        std::move(prepared.session),
        {}, {},
        {}, {},
        {}, {},
        {}, {},
        {}, {},
        {}, {},
        {}, {},
        nested_response.resource_list,
        {},
        std::move(nested_response),
        &provider,
        {},
        {},
        {}, {},
        nullptr,
        {}, {},
        {}, {},
        {}, {},
        std::move(post),
        [&traces, remote](
            const goldsrc::PostResourceEntitySnapshotTraceEvent& event) {
            if (event.endpoint != remote) {
                ++traces.endpoint_mismatches;
            }
            using Type = goldsrc::PostResourceEntitySnapshotStageEventType;
            switch (event.metadata.type) {
            case Type::client_signon_request_ready:
                ++traces.request_ready;
                traces.request_semantic_bytes =
                    event.metadata.semantic_byte_count;
                break;
            case Type::client_signon_request_queued:
                ++traces.request_queued;
                break;
            case Type::client_signon_request_acknowledged:
                ++traces.request_acknowledged;
                break;
            case Type::baseline_registry_ready:
                ++traces.baselines_ready;
                if (event.state != goldsrc::
                        PostResourceEntitySnapshotStageState::
                            baseline_registry_ready) {
                    ++traces.publication_state_mismatches;
                }
                break;
            case Type::full_entity_snapshot_ready:
                ++traces.full_snapshots_ready;
                if (event.state != goldsrc::
                        PostResourceEntitySnapshotStageState::
                            full_snapshot_ready) {
                    ++traces.publication_state_mismatches;
                }
                break;
            case Type::delta_entity_snapshot_ready:
                ++traces.delta_snapshots_ready;
                if (event.state != goldsrc::
                        PostResourceEntitySnapshotStageState::
                            entity_snapshot_ready) {
                    ++traces.publication_state_mismatches;
                }
                break;
            case Type::entity_removed:
                ++traces.removals;
                break;
            case Type::snapshot_history_updated:
                ++traces.history_updates;
                break;
            default:
                break;
            }
        }};
}

enum class TransportScenario {
    baseline,
    dropped_request,
    dropped_acknowledgement,
};

enum class PublicationScenario {
    complete,
    duplicate_baseline_datagram,
    wrong_endpoint_before_baseline,
    replay_delta_after_terminal,
    dropped_full_snapshot,
    dropped_delta_snapshot,
    aggregate_message_limit_one,
    cancelled_after_request_ack,
    server_baselines_stop,
    event_backpressure,
    acknowledgement_event_backpressure,
    unsupported_entity_schema,
    missing_delta_base,
    malformed_runtime_mask,
    invalid_typed_baseline,
    player_custom_schema_variants,
    fragmented_full_snapshot,
    old_delta_classification,
};

void require_endpoint_stability(
    const FakeHldsTransport& transport,
    const network::NetworkAddress remote)
{
    REQUIRE_FALSE(transport.sent.empty());
    CHECK(std::ranges::all_of(
        transport.sent,
        [remote](const SentDatagram& datagram) {
            return datagram.destination == remote;
        }));
}

void run_complete_route(
    const std::size_t run,
    const TransportScenario scenario,
    const PublicationScenario publication_scenario =
        PublicationScenario::complete,
    hlclient::test_support::EntitySnapshotHappyRouteProof* const proof =
        nullptr)
{
    FakeHldsTransport transport;
    const auto remote = network::NetworkAddress::loopback(
        static_cast<std::uint16_t>(28'000U + run));
    std::size_t authentication_releases = 0U;
    SyntheticConsistencyProvider consistency_provider;
    TraceCounts traces;
    auto prepared = prepared_request_with_session(authentication_releases);
    auto handshake = make_coordinator(
        transport,
        remote,
        std::move(prepared),
        consistency_provider,
        traces,
        publication_scenario ==
                PublicationScenario::aggregate_message_limit_one
            ? 1U
            : goldsrc::kDefaultMaximumPostResourceMessages,
        publication_scenario == PublicationScenario::server_baselines_stop
            ? goldsrc::HandshakeStopPoint::server_baselines
            : goldsrc::HandshakeStopPoint::entity_snapshot,
        publication_scenario == PublicationScenario::event_backpressure
            ? 1U
            : publication_scenario ==
                      PublicationScenario::acknowledgement_event_backpressure
                ? 3U
                : goldsrc::kDefaultMaximumPostResourceStageEvents);
    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} +
        std::chrono::milliseconds{1'000 + static_cast<std::int64_t>(run)};

    REQUIRE(handshake.start(epoch));
    REQUIRE(transport.sent.size() == 1U);
    const auto expected_challenge = goldsrc::build_getchallenge_request();
    REQUIRE(expected_challenge);
    CHECK(transport.sent.front().payload == *expected_challenge.datagram);

    constexpr std::uint32_t challenge = 0x7f00'4511U;
    transport.queue(remote, challenge_response(challenge));
    handshake.update(epoch + 1ms);
    REQUIRE(handshake.state() ==
            goldsrc::GoldSrcHandshakeState::waiting_for_connect_response);
    REQUIRE(transport.sent.size() == 2U);
    const auto parsed_connect = goldsrc::parse_connect_request(
        transport.sent.back().payload, connect_profile());
    REQUIRE(parsed_connect);
    REQUIRE(parsed_connect.request);
    CHECK(parsed_connect.request->challenge() == challenge);
    CHECK(handshake.connect_send_attempts() == 1U);

    transport.queue(remote, accepted_response(transport.local));
    handshake.update(epoch + 2ms);
    INFO(handshake.error_context());
    REQUIRE(handshake.state() ==
            goldsrc::GoldSrcHandshakeState::
                waiting_for_post_resource_entity_snapshot);
    REQUIRE(handshake.local_endpoint());
    CHECK(*handshake.local_endpoint() == transport.local);
    CHECK(authentication_releases == 0U);
    const std::size_t netchan_start = transport.sent.size();

    handshake.update(epoch + 3ms);
    const auto initial = find_exact_payload(
        transport, netchan_start, kExactInitialWireRequest);
    REQUIRE(initial.header.sequence.flags.reliable);
    CHECK(initial.header.sequence.sequence.value() == 1U);

    transport.queue(
        remote,
        server_packet(
            1U,
            true,
            initial.header.sequence.sequence.value(),
            true,
            service_envelope(first_semantic_payload(
                publication_scenario !=
                PublicationScenario::unsupported_entity_schema))));
    handshake.update(epoch + 4ms);
    REQUIRE_FALSE(handshake.terminal());

    const auto sends_before_transition = transport.sent.size();
    handshake.update(epoch + 5ms);
    const auto transition = find_exact_payload(
        transport, sends_before_transition, kExactTransitionRequest);
    REQUIRE(transition.header.sequence.flags.reliable);

    transport.queue(
        remote,
        server_packet(
            2U,
            false,
            transition.header.sequence.sequence.value(),
            false,
            service_envelope(resource_semantic_payload())));
    const auto sends_before_response = transport.sent.size();
    handshake.update(epoch + 6ms);
    const auto response = find_exact_payload(
        transport, sends_before_response, kExactTempdecalResponse);
    REQUIRE(response.header.sequence.flags.reliable);
    REQUIRE(response.header.sequence.flags.fragmented);
    CHECK(response.payload.front() == std::byte{0x05U});
    CHECK(consistency_provider.begins == 1U);
    CHECK(consistency_provider.updates == 1U);
    CHECK(consistency_provider.material_count == 1U);
    CHECK(consistency_provider.opaque_byte_count == 16U);

    transport.queue(
        remote,
        server_packet(
            3U,
            false,
            response.header.sequence.sequence.value(),
            true,
            service_envelope(
                goldsrc::kSyntheticPostResourceRequestTrigger)));

    if (publication_scenario ==
        PublicationScenario::acknowledgement_event_backpressure) {
        auto* const saturated_stage =
            goldsrc::detail::GoldSrcHandshakeCoordinatorTestAccess::
                post_resource_stage(handshake);
        REQUIRE(saturated_stage != nullptr);

        // Drive the nested stage directly so its three trigger-publication
        // events remain queued; the coordinator normally drains them after
        // each update because it exposes aggregate state rather than events.
        saturated_stage->update(epoch + 7ms);
        CHECK(saturated_stage->pending_event_count() == 3U);
        CHECK(saturated_stage->request_queue_count() == 1U);

        const auto sends_before_request = transport.sent.size();
        saturated_stage->update(epoch + 8ms);
        const auto request = find_exact_payload(
            transport,
            sends_before_request,
            kExactSyntheticContinuationWire);
        CHECK(saturated_stage->request_transmitted());
        CHECK_FALSE(saturated_stage->request_acknowledged());

        transport.queue(
            remote,
            server_packet(
                4U,
                false,
                request.header.sequence.sequence.value(),
                false));
        saturated_stage->update(epoch + 9ms);

        REQUIRE(saturated_stage->terminal());
        CHECK(saturated_stage->state() ==
              goldsrc::PostResourceEntitySnapshotStageState::backpressure);
        REQUIRE(saturated_stage->error());
        CHECK(saturated_stage->error()->code ==
              goldsrc::PostResourceEntitySnapshotStageErrorCode::
                  event_backpressure);
        CHECK_FALSE(saturated_stage->request_acknowledged());
        CHECK(saturated_stage->pending_event_count() == 3U);
        CHECK(saturated_stage->cleanup_count() == 1U);
        CHECK(traces.request_acknowledged == 0U);
        CHECK(authentication_releases == 1U);
        CHECK(consistency_provider.lifetime_releases == 1U);
        return;
    }

    handshake.update(epoch + 7ms);

    if (publication_scenario == PublicationScenario::event_backpressure) {
        REQUIRE(handshake.terminal());
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::post_resource_backpressure);
        REQUIRE(handshake.post_resource_error());
        CHECK(handshake.post_resource_error()->code ==
              goldsrc::PostResourceEntitySnapshotStageErrorCode::
                  event_backpressure);
        CHECK_FALSE(handshake.post_resource_result());
        const auto* backpressured_stage =
            goldsrc::detail::GoldSrcHandshakeCoordinatorTestAccess::
                post_resource_stage(handshake);
        REQUIRE(backpressured_stage != nullptr);
        CHECK(backpressured_stage->request_queue_count() == 0U);
        CHECK(backpressured_stage->cleanup_count() == 1U);
        CHECK(authentication_releases == 1U);
        CHECK(consistency_provider.lifetime_releases == 1U);
        return;
    }

    REQUIRE_FALSE(handshake.terminal());

    const auto sends_before_request = transport.sent.size();
    handshake.update(epoch + 8ms);
    const auto first_request = find_exact_payload(
        transport,
        sends_before_request,
        kExactSyntheticContinuationWire);
    REQUIRE(first_request.header.sequence.flags.reliable);
    auto covering_request = first_request;
    auto server_sequence = 4U;
    auto now = epoch + 9ms;

    const auto* stage =
        goldsrc::detail::GoldSrcHandshakeCoordinatorTestAccess::
            post_resource_stage(handshake);
    REQUIRE(stage != nullptr);
    CHECK(stage->request_queue_count() == 1U);
    CHECK(stage->request_transmitted());
    CHECK_FALSE(stage->request_acknowledged());

    if (scenario == TransportScenario::dropped_acknowledgement) {
        const auto sends_while_ack_is_dropped = transport.sent.size();
        now += 50ms;
        handshake.update(now);
        CHECK(transport.sent.size() == sends_while_ack_is_dropped);
        CHECK(stage->request_queue_count() == 1U);
        CHECK_FALSE(stage->request_acknowledged());
        now += 1ms;
    }
    if (scenario == TransportScenario::dropped_request ||
        scenario == TransportScenario::dropped_acknowledgement) {
        const auto sends_before_gap = transport.sent.size();
        transport.queue(
            remote,
            server_packet(
                server_sequence++,
                true,
                first_request.header.sequence.sequence.value(),
                true));
        handshake.update(now);
        REQUIRE(transport.sent.size() > sends_before_gap);
        const auto gap = latest_client_packet(transport, netchan_start);
        CHECK_FALSE(gap.header.sequence.flags.reliable);
        CHECK(stage->request_queue_count() == 1U);
        CHECK_FALSE(stage->request_acknowledged());
        now += 1ms;

        const auto sends_before_retry = transport.sent.size();
        transport.queue(
            remote,
            server_packet(
                server_sequence++,
                false,
                gap.header.sequence.sequence.value(),
                true));
        handshake.update(now);
        covering_request = find_exact_payload(
            transport,
            sends_before_retry,
            kExactSyntheticContinuationWire);
        REQUIRE(covering_request.header.sequence.flags.reliable);
        CHECK(covering_request.header.sequence.sequence !=
              first_request.header.sequence.sequence);
        CHECK(stage->request_queue_count() == 1U);
        CHECK_FALSE(stage->request_acknowledged());
        now += 1ms;
    }

    transport.queue(
        remote,
        server_packet(
            server_sequence++,
            false,
            covering_request.header.sequence.sequence.value(),
            false));
    handshake.update(now);
    CHECK(stage->request_acknowledged());
    CHECK(stage->request_queue_count() == 1U);
    CHECK(exact_payload_count(
              transport,
              netchan_start,
              kExactSyntheticContinuationWire) ==
          (scenario == TransportScenario::baseline ? 1U : 2U));
    REQUIRE_FALSE(handshake.terminal());
    now += 1ms;

    if (publication_scenario ==
        PublicationScenario::cancelled_after_request_ack) {
        handshake.cancel(now);
        REQUIRE(handshake.terminal());
        CHECK(handshake.state() == goldsrc::GoldSrcHandshakeState::cancelled);
        CHECK_FALSE(handshake.post_resource_result());
        CHECK_FALSE(handshake.post_resource_error());
        CHECK(stage->request_queue_count() == 1U);
        CHECK(stage->request_acknowledged());
        CHECK(stage->cleanup_count() == 1U);
        CHECK(authentication_releases == 1U);
        CHECK(consistency_provider.lifetime_releases == 1U);
        CHECK(traces.baselines_ready == 0U);
        CHECK(traces.full_snapshots_ready == 0U);
        CHECK(traces.delta_snapshots_ready == 0U);
        return;
    }

    const auto make_publication =
        [&](const std::span<const std::byte> semantic,
            const bool fragmented = false) {
        const auto client = latest_client_packet(transport, netchan_start);
        if (fragmented) {
            return server_single_fragment_packet(
                server_sequence++,
                true,
                client.header.sequence.sequence.value(),
                false,
                service_envelope(semantic));
        }
        return server_packet(
            server_sequence++,
            false,
            client.header.sequence.sequence.value(),
            false,
            service_envelope(semantic));
    };
    const auto deliver_publication =
        [&](const std::span<const std::byte> semantic,
            const bool fragmented = false) {
        auto datagram = make_publication(semantic, fragmented);
        transport.queue(remote, datagram);
        handshake.update(now);
        now += 1ms;
        return datagram;
    };

    std::vector<std::byte> baseline_datagram;
    if (publication_scenario ==
        PublicationScenario::wrong_endpoint_before_baseline) {
        baseline_datagram = make_publication(
            goldsrc::kSyntheticPostResourceBaselinePublication);
        const auto wrong_endpoint = network::NetworkAddress::loopback(
            static_cast<std::uint16_t>(remote.port() + 1U));
        const auto sends_before_wrong_endpoint = transport.sent.size();
        transport.queue(wrong_endpoint, baseline_datagram);
        handshake.update(now);
        CHECK(transport.sent.size() == sends_before_wrong_endpoint);
        CHECK_FALSE(handshake.terminal());
        REQUIRE(handshake.post_resource_result());
        CHECK_FALSE(handshake.post_resource_result()->baseline_registry());
        now += 1ms;
        transport.queue(remote, baseline_datagram);
        handshake.update(now);
        now += 1ms;
    } else {
        baseline_datagram = deliver_publication(
            goldsrc::kSyntheticPostResourceBaselinePublication);
    }

    if (publication_scenario ==
        PublicationScenario::unsupported_entity_schema) {
        REQUIRE(handshake.terminal());
        CHECK(handshake.state() == goldsrc::GoldSrcHandshakeState::protocol_error);
        REQUIRE(handshake.post_resource_error());
        CHECK(handshake.post_resource_error()->code ==
              goldsrc::PostResourceEntitySnapshotStageErrorCode::
                  entity_publication_failed);
        CHECK_FALSE(handshake.post_resource_result());
        CHECK(stage->request_queue_count() == 1U);
        CHECK(stage->request_acknowledged());
        CHECK(stage->cleanup_count() == 1U);
        CHECK(traces.baselines_ready == 0U);
        CHECK(traces.full_snapshots_ready == 0U);
        CHECK(traces.delta_snapshots_ready == 0U);
        CHECK(authentication_releases == 1U);
        CHECK(consistency_provider.lifetime_releases == 1U);
        return;
    }

    if (publication_scenario == PublicationScenario::server_baselines_stop) {
        REQUIRE(handshake.terminal());
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::server_baselines_ready);
        REQUIRE_FALSE(handshake.post_resource_error());
        REQUIRE(handshake.post_resource_result());
        REQUIRE(handshake.post_resource_result()->baseline_registry());
        CHECK(handshake.post_resource_result()
                  ->baseline_registry()
                  ->baseline_count() == 3U);
        CHECK_FALSE(handshake.post_resource_result()->snapshot_history());
        CHECK(handshake.post_resource_result()
                  ->boundary_state()
                  .progress()
                  .progress() ==
              goldsrc::PostResourceSignonProgress::baseline_registry_ready);
        const auto& transcript =
            handshake.post_resource_result()->boundary_state().transcript();
        REQUIRE(transcript.server_messages().size() == 2U);
        CHECK(transcript.server_messages()[0].opcode == 0xfeU);
        CHECK(transcript.server_messages()[1].opcode == 0xfcU);
        REQUIRE(transcript.client_requests().size() == 1U);
        CHECK(stage->request_queue_count() == 1U);
        CHECK(stage->request_acknowledged());
        CHECK(stage->cleanup_count() == 1U);
        CHECK(authentication_releases == 1U);
        CHECK(consistency_provider.lifetime_releases == 1U);
        CHECK(traces.baselines_ready == 1U);
        CHECK(traces.full_snapshots_ready == 0U);
        CHECK(traces.delta_snapshots_ready == 0U);
        require_endpoint_stability(transport, remote);
        return;
    }

    if (publication_scenario ==
        PublicationScenario::aggregate_message_limit_one) {
        REQUIRE(handshake.terminal());
        CHECK(handshake.state() == goldsrc::GoldSrcHandshakeState::protocol_error);
        REQUIRE(handshake.post_resource_error());
        CHECK(handshake.post_resource_error()->code ==
              goldsrc::PostResourceEntitySnapshotStageErrorCode::
                  stream_decode_failed);
        REQUIRE(handshake.post_resource_error()->stream_code);
        CHECK(*handshake.post_resource_error()->stream_code ==
              goldsrc::PostResourceSignonStreamErrorCode::
                  message_limit_exceeded);
        CHECK_FALSE(handshake.post_resource_result());
        CHECK(traces.baselines_ready == 0U);
        CHECK(traces.full_snapshots_ready == 0U);
        CHECK(traces.delta_snapshots_ready == 0U);
        CHECK(stage->request_queue_count() == 1U);
        CHECK(stage->request_acknowledged());
        CHECK(stage->cleanup_count() == 1U);
        CHECK(authentication_releases == 1U);
        CHECK(consistency_provider.lifetime_releases == 1U);
        return;
    }

    REQUIRE_FALSE(handshake.terminal());
    REQUIRE(handshake.post_resource_result());
    REQUIRE(handshake.post_resource_result()->baseline_registry());
    CHECK(handshake.post_resource_result()
              ->baseline_registry()
              ->baseline_count() == 3U);
    for (std::uint32_t entity_number = 1U;
         entity_number <= 3U;
         ++entity_number) {
        REQUIRE(handshake.post_resource_result()
                    ->baseline_registry()
                    ->find_exact(
                        goldsrc::EntityBaselineKey::for_entity(
                            entity_number)));
    }
    CHECK(handshake.post_resource_result()
              ->baseline_registry()
              ->find_exact(goldsrc::EntityBaselineKey::for_entity(1U))
              ->object()
              .schema_name() == "entity_state_t");

    if (publication_scenario ==
        PublicationScenario::duplicate_baseline_datagram) {
        const auto sends_before_duplicate = transport.sent.size();
        transport.queue(remote, baseline_datagram);
        handshake.update(now);
        now += 1ms;
        CHECK(transport.sent.size() == sends_before_duplicate);
        REQUIRE_FALSE(handshake.terminal());
        REQUIRE(handshake.post_resource_result());
        REQUIRE(handshake.post_resource_result()->baseline_registry());
        CHECK(handshake.post_resource_result()
                  ->baseline_registry()
                  ->baseline_count() == 3U);
        CHECK(stage->request_queue_count() == 1U);
        CHECK(traces.baselines_ready == 1U);
    }

    if (publication_scenario ==
        PublicationScenario::dropped_full_snapshot) {
        handshake.update(epoch + 300ms);
        REQUIRE(handshake.terminal());
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::post_resource_timed_out);
        REQUIRE(handshake.post_resource_error());
        CHECK(handshake.post_resource_error()->code ==
              goldsrc::PostResourceEntitySnapshotStageErrorCode::
                  stage_timed_out);
        CHECK_FALSE(handshake.post_resource_result());
        CHECK(traces.baselines_ready == 1U);
        CHECK(traces.full_snapshots_ready == 0U);
        CHECK(traces.delta_snapshots_ready == 0U);
        CHECK(stage->cleanup_count() == 1U);
        CHECK(authentication_releases == 1U);
        CHECK(consistency_provider.lifetime_releases == 1U);
        return;
    }

    static_cast<void>(deliver_publication(
        goldsrc::kSyntheticPostResourceFullSnapshotPublication,
        publication_scenario ==
            PublicationScenario::fragmented_full_snapshot));
    INFO(handshake.error_context());
    CAPTURE(static_cast<int>(handshake.state()));
    REQUIRE_FALSE(handshake.terminal());
    REQUIRE(handshake.post_resource_result());
    REQUIRE(handshake.post_resource_result()->snapshot_history());
    CHECK(handshake.post_resource_result()
              ->snapshot_history()
              ->snapshot_count() == 1U);
    REQUIRE(handshake.post_resource_result()->latest_snapshot());
    CHECK(handshake.post_resource_result()->latest_snapshot()->kind() ==
          goldsrc::EntitySnapshotKind::full);
    CHECK(handshake.post_resource_result()
              ->boundary_state()
              .progress()
              .progress() ==
          goldsrc::PostResourceSignonProgress::full_snapshot_ready);
    CHECK(handshake.post_resource_result()->latest_snapshot()->entity_count() ==
          2U);

    if (publication_scenario ==
        PublicationScenario::dropped_delta_snapshot) {
        handshake.update(epoch + 300ms);
        REQUIRE(handshake.terminal());
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::post_resource_timed_out);
        REQUIRE(handshake.post_resource_error());
        CHECK(handshake.post_resource_error()->code ==
              goldsrc::PostResourceEntitySnapshotStageErrorCode::
                  stage_timed_out);
        CHECK_FALSE(handshake.post_resource_result());
        CHECK(traces.baselines_ready == 1U);
        CHECK(traces.full_snapshots_ready == 1U);
        CHECK(traces.delta_snapshots_ready == 0U);
        CHECK(traces.history_updates == 1U);
        CHECK(stage->cleanup_count() == 1U);
        CHECK(authentication_releases == 1U);
        CHECK(consistency_provider.lifetime_releases == 1U);
        return;
    }

    const auto delta_datagram = deliver_publication(
        goldsrc::kSyntheticPostResourceDeltaSnapshotPublication);
    REQUIRE(handshake.terminal());
    CHECK(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::entity_snapshot_ready);
    REQUIRE_FALSE(handshake.post_resource_error());
    REQUIRE(handshake.post_resource_result());
    const auto& result = *handshake.post_resource_result();
    CHECK(result.profile() ==
          goldsrc::PostResourceSignonCompatibilityProfile::
              synthetic_neutral_v1);
    CHECK(result.delta_registry().schema_count() == 4U);
    REQUIRE(result.baseline_registry());
    REQUIRE(result.baseline_registry()->find_exact(
        goldsrc::EntityBaselineKey::for_entity(1U)));
    CHECK(result.baseline_registry()
              ->find_exact(goldsrc::EntityBaselineKey::for_entity(1U))
              ->object()
              .schema_name() == "entity_state_t");
    REQUIRE(result.snapshot_history());
    CHECK(result.snapshot_history()->snapshot_count() == 2U);
    REQUIRE(result.latest_snapshot());
    const auto& snapshot = *result.latest_snapshot();
    CHECK(snapshot.kind() == goldsrc::EntitySnapshotKind::delta);
    CHECK(snapshot.reference().value() == 2U);
    REQUIRE(snapshot.base_reference());
    CHECK(snapshot.base_reference()->value() == 1U);
    CHECK(snapshot.entity_count() == 2U);
    CHECK(snapshot.statistics().changed_count == 1U);
    CHECK(snapshot.statistics().added_count == 1U);
    CHECK(snapshot.statistics().removed_count == 1U);
    CHECK(snapshot.find_exact(1U) != nullptr);
    CHECK(snapshot.find_exact(2U) == nullptr);
    CHECK(snapshot.find_exact(3U) != nullptr);
    REQUIRE(snapshot.find_exact(1U));
    REQUIRE(snapshot.find_exact(1U)->object().find_exact("origin[0]"));
    CHECK(std::get<double>(
              snapshot.find_exact(1U)
                  ->object()
                  .find_exact("origin[0]")
                  ->value()) == 1.0);
    REQUIRE(snapshot.find_exact(1U)->object().find_exact("angles[1]"));
    CHECK(std::get<double>(
              snapshot.find_exact(1U)
                  ->object()
                  .find_exact("angles[1]")
                  ->value()) == 1.0);
    REQUIRE(snapshot.removed_entity_numbers().size() == 1U);
    CHECK(snapshot.removed_entity_numbers().front() == 2U);
    CHECK(result.boundary_state().progress().completion().completed());
    CHECK(result.boundary_state().progress().progress() ==
          goldsrc::PostResourceSignonProgress::server_signon_completed);
    CHECK(result.boundary_state().progress().completion().condition() ==
          goldsrc::ServerSignonCompletionCondition::
              synthetic_full_and_delta_published);
    CHECK(result.boundary_state().progress().signon_generation() == 1U);
    const auto& transcript = result.boundary_state().transcript();
    REQUIRE(transcript.server_messages().size() == 4U);
    constexpr std::array<std::uint8_t, 4U> expected_opcodes{
        0xfeU, 0xfcU, 0xfbU, 0xfaU};
    for (std::size_t index = 0U; index < expected_opcodes.size(); ++index) {
        CHECK(transcript.server_messages()[index].opcode ==
              expected_opcodes[index]);
        CHECK(transcript.server_messages()[index].ordinal == index);
        CHECK(transcript.server_messages()[index]
                  .decompressed_payload_ordinal == index);
        CHECK(transcript.server_messages()[index].evidence_status ==
              goldsrc::PostResourceSignonEvidenceStatus::
                  independently_authored_synthetic_fixture);
        CHECK(transcript.server_messages()[index].decompressed);
    }
    CHECK(transcript.server_messages()[2U].fragmented ==
          (publication_scenario ==
           PublicationScenario::fragmented_full_snapshot));
    REQUIRE(transcript.client_requests().size() == 1U);
    CHECK(transcript.client_requests().front().semantic_byte_count ==
          goldsrc::kSyntheticPostResourceClientContinuation.size());
    CHECK(transcript.client_requests().front().trigger_message_ordinal == 0U);
    CHECK(transcript.client_requests().front().evidence_status ==
          goldsrc::PostResourceSignonEvidenceStatus::
              independently_authored_synthetic_fixture);

    stage = goldsrc::detail::GoldSrcHandshakeCoordinatorTestAccess::
        post_resource_stage(handshake);
    REQUIRE(stage != nullptr);
    CHECK(stage->request_queue_count() == 1U);
    CHECK(stage->request_transmitted());
    CHECK(stage->request_acknowledged());
    CHECK(stage->cleanup_count() == 1U);
    CHECK(authentication_releases == 1U);
    CHECK(consistency_provider.lifetime_releases == 1U);
    CHECK(consistency_provider.cancels == 0U);
    CHECK(goldsrc::detail::GoldSrcHandshakeCoordinatorTestAccess::
              asset_dispatch_stage(handshake) == nullptr);
    CHECK(goldsrc::detail::GoldSrcHandshakeCoordinatorTestAccess::
              world_render_package_stage(handshake) == nullptr);
    CHECK(traces.request_ready == 1U);
    CHECK(traces.request_queued == 1U);
    CHECK(traces.request_acknowledged == 1U);
    CHECK(traces.request_semantic_bytes ==
          goldsrc::kSyntheticPostResourceClientContinuation.size());
    CHECK(traces.baselines_ready == 1U);
    CHECK(traces.full_snapshots_ready == 1U);
    CHECK(traces.delta_snapshots_ready == 1U);
    CHECK(traces.removals == 1U);
    CHECK(traces.history_updates == 2U);
    CHECK(traces.endpoint_mismatches == 0U);
    CHECK(traces.publication_state_mismatches == 0U);
    require_endpoint_stability(transport, remote);

    if (publication_scenario == PublicationScenario::missing_delta_base) {
        require_missing_delta_base_rejected(result);
    }
    if (publication_scenario ==
        PublicationScenario::malformed_runtime_mask) {
        require_malformed_runtime_mask_rejected(result);
    }
    if (publication_scenario ==
        PublicationScenario::invalid_typed_baseline) {
        require_invalid_typed_baseline_rejected(result);
    }
    if (publication_scenario ==
        PublicationScenario::player_custom_schema_variants) {
        require_player_and_custom_typed_variants(result);
    }
    if (publication_scenario ==
        PublicationScenario::old_delta_classification) {
        require_stage_delta_classified_old(result);
    }

    if (publication_scenario ==
        PublicationScenario::replay_delta_after_terminal) {
        const auto sends_before_replay = transport.sent.size();
        transport.queue(remote, delta_datagram);
        handshake.update(now);
        CHECK(transport.sent.size() == sends_before_replay);
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::entity_snapshot_ready);
        REQUIRE(handshake.post_resource_result());
        REQUIRE(handshake.post_resource_result()->snapshot_history());
        CHECK(handshake.post_resource_result()
                  ->snapshot_history()
                  ->snapshot_count() == 2U);
        CHECK(traces.delta_snapshots_ready == 1U);
        CHECK(traces.history_updates == 2U);
        CHECK(stage->cleanup_count() == 1U);
        CHECK(authentication_releases == 1U);
        CHECK(consistency_provider.lifetime_releases == 1U);
        now += 1ms;
    }

    const auto send_count_at_terminal = transport.sent.size();
    handshake.update(now + 1ms);
    handshake.cancel(now + 2ms);
    CHECK(transport.sent.size() == send_count_at_terminal);
    CHECK(stage->cleanup_count() == 1U);
    CHECK(authentication_releases == 1U);
    CHECK(consistency_provider.lifetime_releases == 1U);
    if (proof != nullptr) {
        proof->snapshot_history =
            std::make_shared<const goldsrc::EntitySnapshotHistoryState>(
                *result.snapshot_history());
        proof->network_endpoint_count = 1U;
        proof->transmitted_packet_count_at_success = send_count_at_terminal;
        proof->transmitted_packet_count_after_cleanup_checks =
            transport.sent.size();
        proof->semantic_entity_request_count = exact_payload_count(
            transport, netchan_start, kExactSyntheticContinuationWire);
        proof->cleanup_count = stage->cleanup_count();
        proof->authentication_release_count = authentication_releases;
        proof->consistency_release_count =
            consistency_provider.lifetime_releases;
    }
}

TEST_CASE(
    "Synthetic-neutral fake HLDS publishes entity snapshots 20 of 20",
    "[goldsrc][entity-snapshot][fake-hlds][integration][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        INFO("synthetic entity-snapshot route " << run + 1U << "/20");
        run_complete_route(run, TransportScenario::baseline);
    }
}

TEST_CASE(
    "Synthetic-neutral fake HLDS retries a dropped typed request 20 of 20",
    "[goldsrc][entity-snapshot][fake-hlds][integration][drop-request][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        INFO("synthetic dropped-request route " << run + 1U << "/20");
        run_complete_route(100U + run, TransportScenario::dropped_request);
    }
}

TEST_CASE(
    "Synthetic-neutral fake HLDS survives a dropped typed-request ACK 20 of 20",
    "[goldsrc][entity-snapshot][fake-hlds][integration][drop-ack][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        INFO("synthetic dropped-ACK route " << run + 1U << "/20");
        run_complete_route(
            200U + run,
            TransportScenario::dropped_acknowledgement);
    }
}

TEST_CASE(
    "Synthetic-neutral fake HLDS enforces the aggregate post-resource message limit",
    "[goldsrc][entity-snapshot][fake-hlds][integration][message-limit]")
{
    run_complete_route(
        300U,
        TransportScenario::baseline,
        PublicationScenario::aggregate_message_limit_one);
}

TEST_CASE(
    "Synthetic-neutral fake HLDS ignores a duplicate baseline datagram",
    "[goldsrc][entity-snapshot][fake-hlds][integration][duplicate]")
{
    run_complete_route(
        301U,
        TransportScenario::baseline,
        PublicationScenario::duplicate_baseline_datagram);
}

TEST_CASE(
    "Synthetic-neutral fake HLDS ignores a publication from the wrong endpoint",
    "[goldsrc][entity-snapshot][fake-hlds][integration][wrong-endpoint]")
{
    run_complete_route(
        302U,
        TransportScenario::baseline,
        PublicationScenario::wrong_endpoint_before_baseline);
}

TEST_CASE(
    "Synthetic-neutral fake HLDS times out when the full snapshot is dropped",
    "[goldsrc][entity-snapshot][fake-hlds][integration][drop-full]")
{
    run_complete_route(
        303U,
        TransportScenario::baseline,
        PublicationScenario::dropped_full_snapshot);
}

TEST_CASE(
    "Synthetic-neutral fake HLDS times out when the delta snapshot is dropped",
    "[goldsrc][entity-snapshot][fake-hlds][integration][drop-delta]")
{
    run_complete_route(
        304U,
        TransportScenario::baseline,
        PublicationScenario::dropped_delta_snapshot);
}

TEST_CASE(
    "Synthetic-neutral fake HLDS keeps sealed history immutable after delta replay",
    "[goldsrc][entity-snapshot][fake-hlds][integration][replay]")
{
    run_complete_route(
        305U,
        TransportScenario::baseline,
        PublicationScenario::replay_delta_after_terminal);
}

TEST_CASE(
    "Synthetic-neutral fake HLDS cancellation releases post-resource lifetime once",
    "[goldsrc][entity-snapshot][fake-hlds][integration][cancel]")
{
    run_complete_route(
        306U,
        TransportScenario::baseline,
        PublicationScenario::cancelled_after_request_ack);
}

TEST_CASE(
    "Synthetic-neutral fake HLDS stops successfully after server baselines",
    "[goldsrc][entity-snapshot][fake-hlds][integration][server-baselines]")
{
    run_complete_route(
        307U,
        TransportScenario::baseline,
        PublicationScenario::server_baselines_stop);
}

TEST_CASE(
    "Synthetic-neutral fake HLDS reports bounded stage-event backpressure",
    "[goldsrc][entity-snapshot][fake-hlds][integration][backpressure]")
{
    run_complete_route(
        308U,
        TransportScenario::baseline,
        PublicationScenario::event_backpressure);
}

TEST_CASE(
    "Synthetic-neutral fake HLDS rejects an ACK when its event queue is full",
    "[goldsrc][entity-snapshot][fake-hlds][integration][backpressure][ack]")
{
    run_complete_route(
        309U,
        TransportScenario::baseline,
        PublicationScenario::acknowledgement_event_backpressure);
}

TEST_CASE(
    "Synthetic-neutral fake HLDS fails closed when entity_state_t is absent",
    "[goldsrc][entity-snapshot][fake-hlds][integration][unsupported-schema]")
{
    run_complete_route(
        310U,
        TransportScenario::baseline,
        PublicationScenario::unsupported_entity_schema);
}

TEST_CASE(
    "Synthetic-neutral fake route rejects a typed delta with a missing base",
    "[goldsrc][entity-snapshot][fake-hlds][integration][missing-base]")
{
    run_complete_route(
        311U,
        TransportScenario::baseline,
        PublicationScenario::missing_delta_base);
}

TEST_CASE(
    "Synthetic-neutral fake route rejects a malformed typed runtime mask",
    "[goldsrc][entity-snapshot][fake-hlds][integration][malformed-mask]")
{
    run_complete_route(
        312U,
        TransportScenario::baseline,
        PublicationScenario::malformed_runtime_mask);
}

TEST_CASE(
    "Synthetic-neutral fake route rejects invalid typed baseline geometry",
    "[goldsrc][entity-snapshot][fake-hlds][integration][malformed-baseline]")
{
    run_complete_route(
        313U,
        TransportScenario::baseline,
        PublicationScenario::invalid_typed_baseline);
}

TEST_CASE(
    "Synthetic-neutral fake route publishes typed player and custom variants",
    "[goldsrc][entity-snapshot][fake-hlds][integration][player][custom]")
{
    run_complete_route(
        314U,
        TransportScenario::baseline,
        PublicationScenario::player_custom_schema_variants);
}

TEST_CASE(
    "Synthetic-neutral fake HLDS reassembles a fragmented full publication",
    "[goldsrc][entity-snapshot][fake-hlds][integration][fragmented]")
{
    run_complete_route(
        315U,
        TransportScenario::baseline,
        PublicationScenario::fragmented_full_snapshot);
}

TEST_CASE(
    "Synthetic-neutral fake route classifies the stage delta as old",
    "[goldsrc][entity-snapshot][fake-hlds][integration][old-delta]")
{
    run_complete_route(
        316U,
        TransportScenario::baseline,
        PublicationScenario::old_delta_classification);
}

} // namespace

namespace hlclient::test_support {

EntitySnapshotHappyRouteProof acquire_entity_snapshot_happy_route_proof()
{
    EntitySnapshotHappyRouteProof proof;
    run_complete_route(
        407U, TransportScenario::baseline, PublicationScenario::complete,
        &proof);
    return proof;
}

EntitySnapshotHappyRouteProof
acquire_entity_snapshot_dropped_request_route_proof(const std::size_t run)
{
    EntitySnapshotHappyRouteProof proof;
    run_complete_route(
        500U + run,
        TransportScenario::dropped_request,
        PublicationScenario::complete,
        &proof);
    return proof;
}

EntitySnapshotHappyRouteProof
acquire_entity_snapshot_dropped_acknowledgement_route_proof(
    const std::size_t run)
{
    EntitySnapshotHappyRouteProof proof;
    run_complete_route(
        600U + run,
        TransportScenario::dropped_acknowledgement,
        PublicationScenario::complete,
        &proof);
    return proof;
}

void require_entity_snapshot_happy_route()
{
    run_complete_route(400U, TransportScenario::baseline);
}

void require_entity_snapshot_duplicate_and_wrong_endpoint_routes()
{
    run_complete_route(
        401U,
        TransportScenario::baseline,
        PublicationScenario::duplicate_baseline_datagram);
    run_complete_route(
        402U,
        TransportScenario::baseline,
        PublicationScenario::wrong_endpoint_before_baseline);
}

void require_entity_snapshot_timeout_routes()
{
    run_complete_route(
        403U,
        TransportScenario::baseline,
        PublicationScenario::dropped_full_snapshot);
    run_complete_route(
        404U,
        TransportScenario::baseline,
        PublicationScenario::dropped_delta_snapshot);
}

void require_entity_snapshot_replay_route()
{
    run_complete_route(
        405U,
        TransportScenario::baseline,
        PublicationScenario::replay_delta_after_terminal);
}

void require_entity_snapshot_cancellation_route()
{
    run_complete_route(
        406U,
        TransportScenario::baseline,
        PublicationScenario::cancelled_after_request_ack);
}

} // namespace hlclient::test_support
