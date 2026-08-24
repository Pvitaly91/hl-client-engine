#include "delta_test_fixture.hpp"
#include "handshake_coordinator_test_access.hpp"
#include "local_asset_source_test_access.hpp"
#include "move_vars_test_fixture.hpp"
#include "local_resource_test_fixture.hpp"
#include "resource_client_response_test_fixture.hpp"
#include "resource_list_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "user_info_test_fixture.hpp"

#include "../src/goldsrc/precache_asset_dispatch_stage_test_access.hpp"

#include <hlclient/auth/authentication_provider.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/local_resource_mapping.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/netchan_payload_transform.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>
#include <hlclient/local_resources/local_resource_environment.hpp>
#include <hlclient/local_resources/local_resource_search_roots.hpp>
#include <hlclient/resource_consistency/prepared_local_resource_consistency_provider.hpp>
#include <hlclient/resource_consistency/provider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <bzlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace assets = hlclient::assets;
namespace auth = hlclient::auth;
namespace bsp = hlclient::goldsrc::bsp;
namespace consistency = hlclient::resource_consistency;
namespace delta_fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace network = hlclient::network;
namespace resource_fixture = resource_list_test_fixture;
namespace response_fixture =
    hlclient::test::resource_client_response_fixture;
namespace synthetic_bsp = hlclient::tests;
namespace user_fixture = hlclient::test::user_info_fixture;

inline constexpr std::string_view kAuthenticationMarker =
    "TRANSITION_TEST_AUTH";
inline constexpr std::string_view kProtectedAuthentication =
    "TRANSITION_TEST_AUTH_TRANSITION_";
inline constexpr std::array kExactTransitionRequest{
    std::byte{0x03U}, std::byte{'s'}, std::byte{'e'}, std::byte{'n'},
    std::byte{'d'}, std::byte{'r'}, std::byte{'e'}, std::byte{'s'},
    std::byte{0U},
};

// Independently authored expected semantic response for the coordinator
// integration. No production encoder contributes bytes to this fixture.
inline constexpr std::array<std::byte, 41U> kExactResourceResponse{
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

// Independently authored from the standard MD5("abc") vector. Production
// encoder and production hasher code do not contribute bytes to this fixture.
inline constexpr std::array<std::byte, 41U> kExactLocalProviderResponse{
    std::byte{0x05U},
    std::byte{0x01U}, std::byte{0x00U},
    std::byte{'t'}, std::byte{'e'}, std::byte{'m'}, std::byte{'p'},
    std::byte{'d'}, std::byte{'e'}, std::byte{'c'}, std::byte{'a'},
    std::byte{'l'}, std::byte{'.'}, std::byte{'w'}, std::byte{'a'},
    std::byte{'d'}, std::byte{0x00U},
    std::byte{0x03U},
    std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x03U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x04U},
    std::byte{0x90U}, std::byte{0x01U}, std::byte{0x50U}, std::byte{0x98U},
    std::byte{0x3cU}, std::byte{0xd2U}, std::byte{0x4fU}, std::byte{0xb0U},
    std::byte{0xd6U}, std::byte{0x96U}, std::byte{0x3fU}, std::byte{0x7dU},
    std::byte{0x28U}, std::byte{0xe1U}, std::byte{0x7fU}, std::byte{0x72U},
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

class CountingResourceConsistencyLifetime final
    : public consistency::IResourceConsistencySessionLifetime {
public:
    explicit CountingResourceConsistencyLifetime(
        std::size_t& releases) noexcept
        : releases_{releases}
    {
    }

    ~CountingResourceConsistencyLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

class IntegrationResourceConsistencyOperation final
    : public consistency::ResourceConsistencyOperation {
public:
    IntegrationResourceConsistencyOperation(
        std::size_t& updates,
        std::size_t& cancellations,
        std::size_t& lifetime_releases) noexcept
        : updates_{updates},
          cancellations_{cancellations},
          lifetime_releases_{lifetime_releases}
    {
    }

    [[nodiscard]] consistency::ResourceConsistencyUpdateResult update()
        override
    {
        ++updates_;
        auto material = consistency::make_resource_consistency_material(
            0x01020304U,
            response_fixture::kSyntheticOpaqueMaterial);
        REQUIRE(material);
        return consistency::ResourceConsistencyUpdateResult::succeeded(
            consistency::ResourceConsistencySession{
                std::move(*material.material),
                std::make_unique<CountingResourceConsistencyLifetime>(
                    lifetime_releases_),
            });
    }

    void cancel() noexcept override
    {
        if (!cancelled_) {
            cancelled_ = true;
            ++cancellations_;
        }
    }

private:
    std::size_t& updates_;
    std::size_t& cancellations_;
    std::size_t& lifetime_releases_;
    bool cancelled_{false};
};

class IntegrationResourceConsistencyProvider final
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
            std::make_unique<IntegrationResourceConsistencyOperation>(
                updates,
                cancellations,
                lifetime_releases));
    }

    std::size_t begins{0U};
    std::size_t updates{0U};
    std::size_t cancellations{0U};
    std::size_t lifetime_releases{0U};
    std::size_t material_count{0U};
    std::size_t opaque_byte_count{0U};
    std::size_t filesystem_calls{0U};
};

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text)
{
    const auto raw = std::as_bytes(std::span{text.data(), text.size()});
    return {raw.begin(), raw.end()};
}

void append_u16_le(std::vector<std::byte>& output, const std::uint16_t value)
{
    output.push_back(static_cast<std::byte>(value & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void append_u32_le(std::vector<std::byte>& output, const std::uint32_t value)
{
    output.push_back(static_cast<std::byte>(value & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto parsed = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(parsed);
    return *parsed;
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

[[nodiscard]] std::vector<std::byte> accept_response(
    const network::NetworkAddress client)
{
    std::string packet{"\xFF\xFF\xFF\xFF", 4U};
    packet += "B 1 \"" + client.to_string() + "\" 0 10210";
    packet.push_back('\0');
    return bytes(packet);
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
        bytes(kProtectedAuthentication),
        suffix);
    REQUIRE(material);
    auth::AuthenticationSession session{
        std::move(*material.value),
        std::make_unique<CountingAuthenticationLifetime>(releases),
    };
    auto transferred = session.take_material();
    REQUIRE(transferred);
    auto profile = goldsrc::ConnectCompatibilityProfile{};
    profile.protected_authentication_is_ascii_hex = false;
    auto prepared = goldsrc::prepare_connect_request(
        {},
        std::move(*transferred),
        profile);
    REQUIRE(prepared);
    return {std::move(*prepared.value), std::move(session)};
}

[[nodiscard]] network::Datagram receive_bounded(
    network::UdpSocket& socket,
    const std::size_t maximum_size)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        auto received = socket.receive(maximum_size);
        if (received.status == network::ReceiveStatus::received) {
            REQUIRE(received.datagram);
            return std::move(*received.datagram);
        }
        if (received.status == network::ReceiveStatus::error ||
            received.status == network::ReceiveStatus::truncated) {
            FAIL(received.error);
        }
        std::this_thread::yield();
    }
    throw std::runtime_error{
        "Timed out waiting for bounded fake-HLDS transition traffic"};
}

void require_no_datagram(network::UdpSocket& socket)
{
    const auto received = socket.receive(goldsrc::kMaximumNetchanDatagramSize);
    if (received.status == network::ReceiveStatus::received) {
        FAIL("Unexpected post-boundary fake-HLDS transition datagram");
    }
    if (received.status == network::ReceiveStatus::error ||
        received.status == network::ReceiveStatus::truncated) {
        FAIL(received.error);
    }
    CHECK(received.status == network::ReceiveStatus::would_block);
}

void send_server_datagram(
    network::UdpSocket& server,
    const network::NetworkAddress client,
    const std::span<const std::byte> payload,
    std::string& error)
{
    REQUIRE(server.send_to(client, payload, error));
}

[[nodiscard]] goldsrc::ChallengeExchangeConfig challenge_config()
{
    goldsrc::ChallengeExchangeConfig config;
    config.retry_interval = 100ms;
    config.timeout = 1s;
    config.maximum_attempts = 2U;
    config.maximum_datagrams_per_update = 4U;
    return config;
}

[[nodiscard]] goldsrc::ConnectResponseWaitConfig response_config()
{
    goldsrc::ConnectResponseWaitConfig config;
    config.timeout = 1s;
    config.maximum_datagrams_per_update = 4U;
    return config;
}

[[nodiscard]] goldsrc::UserInfoSignonStageConfig user_info_config()
{
    goldsrc::UserInfoSignonStageConfig config;
    auto& driver = config.movement_environment.delta.pre_resource
                       .initial_signon.driver;
    driver.channel_inactivity_timeout = 1s;
    driver.fragment_transfer_timeout = 500ms;
    driver.maximum_datagram_size = 1'100U;
    driver.maximum_fragment_datagram_size = 1'100U;
    driver.maximum_unreliable_payload_size =
        driver.maximum_datagram_size - goldsrc::kNetchanHeaderSize;
    driver.maximum_datagrams_per_update = 8U;
    driver.maximum_outgoing_packets_per_update = 8U;
    driver.maximum_events = 64U;
    config.movement_environment.delta.pre_resource.initial_signon.maximum_events =
        64U;
    config.movement_environment.delta.pre_resource.initial_signon
        .maximum_driver_events_per_update = 64U;
    config.movement_environment.delta.pre_resource.maximum_events = 64U;
    config.movement_environment.delta.maximum_events = 64U;
    config.movement_environment.maximum_events = 64U;
    config.maximum_stage_events = 64U;
    return config;
}

[[nodiscard]] goldsrc::ResourceTransitionStageConfig transition_config()
{
    goldsrc::ResourceTransitionStageConfig config;
    config.user_info = user_info_config();
    config.maximum_stage_events = 64U;
    config.maximum_driver_events_per_update = 64U;
    return config;
}

[[nodiscard]] goldsrc::ResourceListStageConfig resource_list_config()
{
    goldsrc::ResourceListStageConfig config;
    config.transition = transition_config();
    config.maximum_stage_events = 512U;
    return config;
}

[[nodiscard]] goldsrc::ResourceClientResponseStageConfig
resource_client_response_config()
{
    goldsrc::ResourceClientResponseStageConfig config;
    config.resource_list = resource_list_config();
    config.resource_list.transition.user_info.movement_environment.delta
        .pre_resource.initial_signon.driver
        .maximum_unfragmented_reliable_payload =
        goldsrc::kOpcode5ResourceResponseSemanticSize - 1U;
    config.maximum_driver_events_per_update = 64U;
    config.response.maximum_response_stage_events = 64U;
    return config;
}

[[nodiscard]] std::vector<std::vector<std::byte>> delta_schemas()
{
    return {
        delta_fixture::schema("alpha_t", delta_fixture::kSchemaAlphaFields),
        delta_fixture::schema("bravo_t", delta_fixture::kSchemaBravoFields),
        delta_fixture::schema("charlie_t", delta_fixture::kSchemaAlphaFields),
        delta_fixture::schema("delta_t", delta_fixture::kSchemaBravoFields),
        delta_fixture::schema("echo_t", delta_fixture::kSchemaAlphaFields),
        delta_fixture::schema("foxtrot_t", delta_fixture::kSchemaBravoFields),
        delta_fixture::schema("golf_t", delta_fixture::kSchemaAlphaFields),
    };
}

[[nodiscard]] std::vector<std::byte> fragmented_batch_user_info_message()
{
    constexpr std::string_view alphabet =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string private_info{"\\name\\Synthetic\\model\\scientist"};
    std::uint32_t state = 0xa341'316cU;
    for (std::size_t entry = 0U; entry < 4U; ++entry) {
        private_info += "\\x";
        private_info.push_back(static_cast<char>('0' + entry));
        private_info.push_back('\\');
        for (std::size_t index = 0U; index < 230U; ++index) {
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            private_info.push_back(alphabet[state % alphabet.size()]);
        }
    }
    REQUIRE(private_info.size() <= goldsrc::kDefaultMaximumUserInfoStringSize);
    return user_fixture::make_message(2U, 0x1234'5678U, private_info);
}

enum class MalformedUserInfoScenario {
    wrong_opcode,
    duplicate_client_index,
    unterminated_info_string,
    oversized_info_string,
    missing_opaque_suffix,
};

[[nodiscard]] std::vector<std::byte> first_semantic_payload_with_user_info(
    const std::span<const std::byte> user_info_messages)
{
    REQUIRE_FALSE(user_info_messages.empty());
    std::vector<std::byte> post_delta;
    move_fixture::append_move_vars_body(post_delta);
    move_fixture::append_confirmed_controls(post_delta);
    post_delta.insert(
        post_delta.end(),
        user_info_messages.begin(),
        user_info_messages.end());
    return delta_fixture::service_payload(
        delta_schemas(),
        goldsrc::kMoveVarsOpcode,
        post_delta);
}

[[nodiscard]] std::vector<std::byte> first_semantic_payload(
    const bool multiple_user_info_messages)
{
    auto user_info_messages = fragmented_batch_user_info_message();
    if (multiple_user_info_messages) {
        const auto second_user_info = user_fixture::make_message(
            3U,
            0x2345'6789U,
            "\\name\\SyntheticTwo\\model\\scientist");
        user_info_messages.insert(
            user_info_messages.end(),
            second_user_info.begin(),
            second_user_info.end());
    }
    return first_semantic_payload_with_user_info(user_info_messages);
}

[[nodiscard]] std::vector<std::byte> malformed_first_semantic_payload(
    const MalformedUserInfoScenario scenario)
{
    std::vector<std::byte> user_info_messages;
    switch (scenario) {
    case MalformedUserInfoScenario::wrong_opcode:
        user_info_messages = fragmented_batch_user_info_message();
        user_info_messages.front() = std::byte{99U};
        break;
    case MalformedUserInfoScenario::duplicate_client_index: {
        user_info_messages = fragmented_batch_user_info_message();
        const auto duplicate = user_fixture::make_message(
            2U,
            0x2345'6789U,
            "\\name\\SecondSynthetic\\model\\scientist");
        user_info_messages.insert(
            user_info_messages.end(),
            duplicate.begin(),
            duplicate.end());
        break;
    }
    case MalformedUserInfoScenario::unterminated_info_string: {
        user_info_messages = fragmented_batch_user_info_message();
        const auto terminator = std::ranges::find(
            user_info_messages.begin() +
                static_cast<std::ptrdiff_t>(user_fixture::kInfoStringOffset),
            user_info_messages.end(),
            std::byte{0U});
        REQUIRE(terminator != user_info_messages.end());
        user_info_messages.erase(terminator);
        break;
    }
    case MalformedUserInfoScenario::oversized_info_string: {
        constexpr std::string_view alphabet =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::string private_info{"\\field\\"};
        std::uint32_t state = 0xc801'3ea4U;
        while (private_info.size() <
               goldsrc::kDefaultMaximumUserInfoStringSize + 1U) {
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            private_info.push_back(alphabet[state % alphabet.size()]);
        }
        REQUIRE(private_info.size() ==
                goldsrc::kDefaultMaximumUserInfoStringSize + 1U);
        user_info_messages = user_fixture::make_message(
            2U,
            0x1234'5678U,
            private_info);
        break;
    }
    case MalformedUserInfoScenario::missing_opaque_suffix: {
        user_info_messages = fragmented_batch_user_info_message();
        const auto terminator = std::ranges::find(
            user_info_messages.begin() +
                static_cast<std::ptrdiff_t>(user_fixture::kInfoStringOffset),
            user_info_messages.end(),
            std::byte{0U});
        REQUIRE(terminator != user_info_messages.end());
        user_info_messages.resize(
            static_cast<std::size_t>(std::distance(
                user_info_messages.begin(),
                terminator)) + 1U);
        break;
    }
    }
    return first_semantic_payload_with_user_info(user_info_messages);
}

[[nodiscard]] std::vector<std::byte> second_semantic_payload()
{
    std::vector<std::byte> output{
        std::byte{45U},
        std::byte{1U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{43U},
    };
    std::uint32_t state = 0x243f6a88U;
    for (std::size_t index = 0U; index < 4'096U; ++index) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        output.push_back(static_cast<std::byte>(state & 0xffU));
    }
    return output;
}

enum class ResourceListIntegrationScenario {
    baseline,
    reordered_fragments,
    differential_map,
    malicious_names,
    wrong_endpoint,
    duplicate_completed_batch,
    invalid_type,
    excessive_count,
    duplicate_identity,
    unobserved_flags_profile,
    nonzero_padding,
    trailing_data,
    truncated_entry,
    truncated_count,
    unterminated_name,
    resource_size_limit,
    resource_total_size_limit,
    missing_fragment,
    malformed_bzip2,
    timeout,
    cancellation,
    event_backpressure,
};

enum class ResourceResponseIntegrationScenario {
    baseline,
    dropped_response,
    dropped_acknowledgement,
    coalesced_tail,
    differential_map,
    malicious_resource_names,
};

enum class PrecacheManifestIntegrationScenario {
    complete,
    world_ready_missing_sound,
    local_map_missing,
    sparse_slots,
    malicious_name,
    missing_model,
    unsupported_non_ascii,
    ambiguous_sound,
    duplicate_map_match,
};

enum class PrecacheManifestTransportScenario {
    baseline,
    dropped_response,
    dropped_acknowledgement,
};

enum class PrecacheManifestCompletionMode {
    manifest_only,
    production_bsp_dispatch,
    production_bsp_malformed_rejection,
};

enum class ProductionBspIntegrationScenario {
    valid_quad,
    valid_triangle,
    valid_multi_face,
    missing_texture_metadata,
    malformed_header,
    malformed_texinfo,
    geometry_output_limit,
    stale_selected_locator,
    source_changed_during_read,
};

[[nodiscard]] std::vector<std::byte> manifest_first_semantic_payload()
{
    auto output = first_semantic_payload(false);
    const auto old_map = bytes("maps/test_alpha.bsp");
    const auto new_map = bytes("maps/test_map.bsp");
    const auto match = std::search(
        output.begin(),
        output.end(),
        old_map.begin(),
        old_map.end());
    REQUIRE(match != output.end());
    const auto offset = static_cast<std::size_t>(
        std::distance(output.begin(), match));
    output.erase(
        output.begin() + static_cast<std::ptrdiff_t>(offset),
        output.begin() + static_cast<std::ptrdiff_t>(
                             offset + old_map.size()));
    output.insert(
        output.begin() + static_cast<std::ptrdiff_t>(offset),
        new_map.begin(),
        new_map.end());
    return output;
}

[[nodiscard]] std::vector<resource_fixture::EntrySpec>
manifest_resource_entries(const PrecacheManifestIntegrationScenario scenario)
{
    const bool sparse =
        scenario == PrecacheManifestIntegrationScenario::sparse_slots;
    const auto shared_sparse_index = static_cast<std::uint16_t>(
        sparse ? 4'095U : 0U);

    std::vector<resource_fixture::EntrySpec> entries{
        resource_fixture::EntrySpec{
            4U,
            "generic/test.dat",
            sparse ? shared_sparse_index : std::uint16_t{7U},
            101U,
            0U,
        },
        resource_fixture::EntrySpec{
            0U,
            "test_sound.wav",
            sparse ? shared_sparse_index : std::uint16_t{41U},
            202U,
            0U,
        },
        resource_fixture::EntrySpec{
            2U,
            "models/test_model.mdl",
            17U,
            303U,
            0U,
        },
        resource_fixture::EntrySpec{
            5U,
            "events/test_event.sc",
            sparse ? shared_sparse_index : std::uint16_t{19U},
            404U,
            0U,
        },
        resource_fixture::EntrySpec{
            3U,
            "decals/test_decal.wad",
            sparse ? shared_sparse_index : std::uint16_t{3U},
            505U,
            0U,
        },
        resource_fixture::EntrySpec{
            2U,
            "maps/test_map.bsp",
            sparse ? shared_sparse_index : std::uint16_t{137U},
            606U,
            0U,
        },
    };
    if (scenario == PrecacheManifestIntegrationScenario::malicious_name) {
        entries.push_back(resource_fixture::EntrySpec{
            4U,
            "../evil.dat",
            73U,
            707U,
            0U,
        });
    }
    if (scenario ==
        PrecacheManifestIntegrationScenario::unsupported_non_ascii) {
        std::string name{"unsupported_"};
        name.push_back(static_cast<char>(0x80U));
        name.append(".dat");
        entries.push_back(resource_fixture::EntrySpec{
            4U,
            std::move(name),
            73U,
            707U,
            0U,
        });
    }
    if (scenario == PrecacheManifestIntegrationScenario::ambiguous_sound) {
        entries[1U].name = "Test_Sound.wav";
    }
    if (scenario ==
        PrecacheManifestIntegrationScenario::duplicate_map_match) {
        entries.push_back(resource_fixture::EntrySpec{
            2U,
            "maps/test_map.bsp",
            138U,
            808U,
            0U,
        });
    }
    return entries;
}

[[nodiscard]] std::vector<std::byte> manifest_resource_list_payload(
    const PrecacheManifestIntegrationScenario scenario)
{
    const auto entries = manifest_resource_entries(scenario);
    const auto message = resource_fixture::make_message(entries);
    std::vector<std::byte> output{
        std::byte{45U},
        std::byte{1U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
    };
    output.insert(output.end(), message.bytes.begin(), message.bytes.end());
    return output;
}

[[nodiscard]] std::vector<std::byte> second_resource_list_payload(
    const ResourceListIntegrationScenario scenario,
    const std::size_t run)
{
    constexpr std::string_view alphabet =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::vector<resource_fixture::EntrySpec> entries;
    entries.reserve(128U);
    std::array<std::uint16_t, 16U> next_index{};
    next_index[0U] = 1U;
    next_index[2U] = 1U;
    next_index[3U] = 0U;
    next_index[4U] = 1U;
    next_index[5U] = 1U;
    constexpr std::array<std::uint8_t, 5U> types{0U, 2U, 3U, 4U, 5U};
    std::uint32_t random = 0x9e37'79b9U;
    for (std::size_t ordinal = 0U; ordinal < 128U; ++ordinal) {
        const auto type = types[ordinal % types.size()];
        std::string name;
        switch (type) {
        case 0U: name = "sound/synthetic_"; break;
        case 2U: name = "models/synthetic_"; break;
        case 3U: name = "decals/synthetic_"; break;
        case 4U: name = "generic/synthetic_"; break;
        case 5U: name = "events/synthetic_"; break;
        default: FAIL("Unexpected synthetic resource type");
        }
        name += std::to_string(ordinal);
        name.push_back('_');
        for (std::size_t character = 0U; character < 40U; ++character) {
            random ^= random << 13U;
            random ^= random >> 17U;
            random ^= random << 5U;
            name.push_back(alphabet[random % alphabet.size()]);
        }
        switch (type) {
        case 0U: name += ".wav"; break;
        case 2U: name += ".mdl"; break;
        case 3U: name += ".wad"; break;
        case 4U: name += ".dat"; break;
        case 5U: name += ".sc"; break;
        default: FAIL("Unexpected synthetic resource type");
        }
        entries.push_back(resource_fixture::EntrySpec{
            type,
            std::move(name),
            next_index[type]++,
            static_cast<std::uint32_t>(1'000U + ordinal * 37U),
            static_cast<std::uint8_t>(ordinal % 7U == 0U ? 1U : 0U),
        });
    }

    if (scenario == ResourceListIntegrationScenario::duplicate_identity) {
        entries[1U].type = entries[0U].type;
        entries[1U].index = entries[0U].index;
    } else if (scenario == ResourceListIntegrationScenario::invalid_type) {
        entries[0U].type = 1U;
    } else if (scenario ==
               ResourceListIntegrationScenario::unobserved_flags_profile) {
        entries[0U].flags = 4U;
    } else if (scenario == ResourceListIntegrationScenario::differential_map) {
        constexpr std::array<std::string_view, 3U> map_names{
            "maps/differential_alpha.bsp",
            "maps/differential_bravo.bsp",
            "maps/differential_charlie.bsp",
        };
        const auto variant = run % map_names.size();
        entries[1U].name = map_names[variant];
        entries[1U].size_code =
            static_cast<std::uint32_t>(98'765U + variant);
    } else if (scenario == ResourceListIntegrationScenario::malicious_names) {
        const std::array<std::string, 10U> names{
            "../evil",
            "..\\evil",
            "C:\\evil",
            "\\\\server\\share",
            "/absolute",
            "maps//double.bsp",
            "maps/./dot.bsp",
            "maps/%2e%2e/encoded",
            std::string{"control_\x1b", 9U},
            std::string{"non_ascii_\x80", 11U},
        };
        for (std::size_t index = 0U; index < names.size(); ++index) {
            entries[index].name = names[index];
        }
    }

    auto message = resource_fixture::make_message(entries);
    if (scenario == ResourceListIntegrationScenario::excessive_count) {
        message.bytes[1U] = std::byte{0x01U};
        const auto type_nibble =
            std::to_integer<std::uint8_t>(message.bytes[2U]) & 0xf0U;
        message.bytes[2U] = static_cast<std::byte>(type_nibble | 0x04U);
    } else if (scenario == ResourceListIntegrationScenario::nonzero_padding) {
        resource_fixture::set_bit(
            message.bytes,
            message.padding_start_bit,
            true);
    } else if (scenario == ResourceListIntegrationScenario::trailing_data) {
        message.bytes.push_back(std::byte{5U});
    } else if (scenario == ResourceListIntegrationScenario::truncated_entry) {
        REQUIRE(message.bytes.size() > 3U);
        message.bytes.resize(message.bytes.size() - 3U);
    } else if (scenario == ResourceListIntegrationScenario::truncated_count) {
        // Retain opcode 43 and one body byte: the transition boundary is valid,
        // while the resource parser has only 8 of the required 12 count bits.
        message.bytes.resize(2U);
    } else if (scenario == ResourceListIntegrationScenario::unterminated_name) {
        // Opcode, count, type, and five name bytes with no NUL terminator.
        message.bytes.resize(8U);
    }
    std::vector<std::byte> output{
        std::byte{45U},
        std::byte{1U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{0U},
    };
    output.insert(output.end(), message.bytes.begin(), message.bytes.end());
    return output;
}

[[nodiscard]] std::optional<goldsrc::ResourceListErrorCode>
expected_resource_list_error(const ResourceListIntegrationScenario scenario)
{
    switch (scenario) {
    case ResourceListIntegrationScenario::baseline:
    case ResourceListIntegrationScenario::reordered_fragments:
    case ResourceListIntegrationScenario::differential_map:
    case ResourceListIntegrationScenario::malicious_names:
    case ResourceListIntegrationScenario::wrong_endpoint:
    case ResourceListIntegrationScenario::duplicate_completed_batch:
    case ResourceListIntegrationScenario::missing_fragment:
    case ResourceListIntegrationScenario::malformed_bzip2:
    case ResourceListIntegrationScenario::timeout:
    case ResourceListIntegrationScenario::cancellation:
    case ResourceListIntegrationScenario::event_backpressure:
        return std::nullopt;
    case ResourceListIntegrationScenario::invalid_type:
        return goldsrc::ResourceListErrorCode::unsupported_resource_type;
    case ResourceListIntegrationScenario::excessive_count:
        return goldsrc::ResourceListErrorCode::resource_count_limit_exceeded;
    case ResourceListIntegrationScenario::duplicate_identity:
        return goldsrc::ResourceListErrorCode::duplicate_resource_identity;
    case ResourceListIntegrationScenario::unobserved_flags_profile:
        return goldsrc::ResourceListErrorCode::unsupported_resource_profile;
    case ResourceListIntegrationScenario::nonzero_padding:
        return goldsrc::ResourceListErrorCode::nonzero_padding;
    case ResourceListIntegrationScenario::trailing_data:
        return goldsrc::ResourceListErrorCode::unexpected_trailing_data;
    case ResourceListIntegrationScenario::truncated_entry:
        return goldsrc::ResourceListErrorCode::truncated_entry;
    case ResourceListIntegrationScenario::truncated_count:
        return goldsrc::ResourceListErrorCode::truncated_count;
    case ResourceListIntegrationScenario::unterminated_name:
        return goldsrc::ResourceListErrorCode::unterminated_resource_name;
    case ResourceListIntegrationScenario::resource_size_limit:
        return goldsrc::ResourceListErrorCode::
            resource_declared_size_limit_exceeded;
    case ResourceListIntegrationScenario::resource_total_size_limit:
        return goldsrc::ResourceListErrorCode::
            total_declared_size_limit_exceeded;
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<std::byte> service_envelope(
    const std::span<const std::byte> semantic)
{
    REQUIRE_FALSE(semantic.empty());
    REQUIRE(semantic.size() <= (std::numeric_limits<unsigned int>::max)());
    std::vector<char> source;
    source.reserve(semantic.size());
    std::ranges::transform(
        semantic,
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
    std::vector<std::byte> output{
        std::byte{0x42U}, std::byte{0x5aU}, std::byte{0x32U}, std::byte{0U}};
    std::ranges::transform(
        compressed,
        std::back_inserter(output),
        [](const char value) {
            return static_cast<std::byte>(static_cast<unsigned char>(value));
        });
    return output;
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

// The fake server authors descriptor bytes independently. Production packet
// encoding is not used to choose the fragment index/count or payload ranges.
[[nodiscard]] std::vector<std::byte> server_fragment(
    const std::uint32_t packet_sequence,
    const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement,
    const std::uint16_t fragment_index,
    const std::uint16_t fragment_count,
    const std::span<const std::byte> fragment)
{
    REQUIRE_FALSE(fragment.empty());
    REQUIRE(fragment.size() <= goldsrc::kStockProtocol48NormalFragmentChunkSize);
    std::vector<std::byte> body;
    body.push_back(std::byte{1U});
    append_u32_le(
        body,
        (static_cast<std::uint32_t>(fragment_index) << 16U) |
            static_cast<std::uint32_t>(fragment_count));
    append_u16_le(body, 0U);
    append_u16_le(body, static_cast<std::uint16_t>(fragment.size()));
    body.push_back(std::byte{0U});
    body.insert(body.end(), fragment.begin(), fragment.end());
    goldsrc::encode_netchan_payload(body, sequence(packet_sequence));

    std::vector<std::byte> datagram;
    append_u32_le(
        datagram,
        packet_sequence | goldsrc::kNetchanReliableSequenceFlag |
            goldsrc::kNetchanFragmentSequenceFlag);
    append_u32_le(
        datagram,
        acknowledgement |
            (reliable_acknowledgement
                 ? goldsrc::kNetchanReliableSequenceFlag
                 : 0U));
    datagram.insert(datagram.end(), body.begin(), body.end());
    return datagram;
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket decode_client_packet(
    const network::Datagram& datagram,
    const network::NetworkAddress expected_source)
{
    CHECK(datagram.source == expected_source);
    const auto decoded = goldsrc::decode_client_to_server_netchan_packet(
        datagram.payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    return *decoded.packet;
}

[[nodiscard]] bool is_exact_transition_request(
    const goldsrc::ClientToServerNetchanPacket& packet)
{
    return std::ranges::equal(packet.payload, kExactTransitionRequest);
}

void check_transport_only_packet(
    const goldsrc::ClientToServerNetchanPacket& packet)
{
    CHECK_FALSE(packet.header.sequence.flags.fragmented);
    CHECK_FALSE(packet.header.sequence.flags.reliable);
    CHECK(std::ranges::all_of(
        packet.payload,
        [](const std::byte value) {
            return value == goldsrc::kStockProtocol48NetchanPaddingByte;
        }));
}

void check_exact_resource_response_packet(
    const goldsrc::ClientToServerNetchanPacket& packet,
    const std::span<const std::byte, 41U> expected = kExactResourceResponse)
{
    REQUIRE(packet.header.sequence.flags.reliable);
    REQUIRE(packet.header.sequence.flags.fragmented);
    REQUIRE(packet.fragments[0U]);
    REQUIRE_FALSE(packet.fragments[1U]);
    REQUIRE(packet.fragments[0U]->packed_id());
    CHECK(packet.fragments[0U]->packed_id()->fragment_index() == 1U);
    CHECK(packet.fragments[0U]->packed_id()->fragment_count() == 1U);
    CHECK(packet.fragments[0U]->offset == 0U);
    CHECK(packet.fragments[0U]->length == expected.size());
    CHECK(packet.fragment_payload_size == expected.size());
    CHECK(std::ranges::equal(packet.payload, expected));
}

void check_coalesced_response_carrier(
    const goldsrc::ClientToServerNetchanPacket& response,
    const std::size_t run)
{
    constexpr std::array<std::size_t, 3U> tail_sizes{13U, 15U, 17U};
    constexpr std::array<
        goldsrc::ResourceResponseConcurrentTailProfile,
        3U> tail_profiles{
        goldsrc::ResourceResponseConcurrentTailProfile::
            stock_coalesced_opaque_length_13,
        goldsrc::ResourceResponseConcurrentTailProfile::
            stock_coalesced_opaque_length_15,
        goldsrc::ResourceResponseConcurrentTailProfile::
            stock_coalesced_opaque_length_17,
    };
    const auto variant = run % tail_sizes.size();
    const auto tail_size = tail_sizes[variant];
    std::vector<std::byte> decoded_body{
        std::byte{0x01U},
        std::byte{0x01U}, std::byte{0x00U},
        std::byte{0x01U}, std::byte{0x00U},
        std::byte{0x00U}, std::byte{0x00U},
        std::byte{0x29U}, std::byte{0x00U},
        std::byte{0x00U},
    };
    decoded_body.insert(
        decoded_body.end(),
        response.payload.begin(),
        response.payload.end());
    for (std::size_t index = 0U; index < tail_size; ++index) {
        decoded_body.push_back(static_cast<std::byte>(
            (run * 29U + index * 17U + 2U) & 0xffU));
    }

    const auto parsed = goldsrc::Opcode5ResourceResponseCarrierParser{}.parse(
        response.header,
        decoded_body,
        static_cast<std::uint64_t>(run + 1U));
    REQUIRE(parsed.state);
    REQUIRE_FALSE(parsed.error);
    CHECK(parsed.state->response().wire_name() == "tempdecal.wad");
    CHECK(parsed.state->geometry().semantic_reliable_range().byte_offset() ==
          10U);
    CHECK(parsed.state->geometry().semantic_reliable_range().byte_count() ==
          kExactResourceResponse.size());
    CHECK(parsed.state->geometry().tail_range().byte_offset() == 51U);
    CHECK(parsed.state->geometry().tail_range().byte_count() == tail_size);
    CHECK(parsed.state->geometry().full_decoded_body_size() ==
          51U + tail_size);
    CHECK(parsed.state->concurrent_tail().byte_count() == tail_size);
    CHECK(parsed.state->concurrent_tail().profile() ==
          tail_profiles[variant]);
}

[[nodiscard]] constexpr std::array<std::byte, 7U>
post_response_semantic_payload()
{
    return {
        std::byte{0x03U},
        std::byte{'s'}, std::byte{'p'}, std::byte{'a'},
        std::byte{'w'}, std::byte{'n'}, std::byte{0U},
    };
}

struct TraceCounts {
    std::size_t user_info_messages{0U};
    std::size_t first_batch_completions{0U};
    std::size_t requests_queued{0U};
    std::size_t requests_acknowledged{0U};
    std::size_t controls{0U};
    std::size_t boundaries{0U};
    std::size_t resource_lists{0U};
    std::size_t resource_entries{0U};
    std::size_t post_list_boundaries{0U};
    std::size_t client_response_boundaries{0U};
    std::size_t response_requirements{0U};
    std::size_t responses_ready{0U};
    std::size_t responses_queued{0U};
    std::size_t responses_transmitted{0U};
    std::size_t responses_acknowledged{0U};
    std::size_t response_boundaries{0U};
};

enum class IntegrationScenario {
    baseline,
    dropped_request,
    dropped_acknowledgement,
    multiple_user_info,
};

struct StartedSession {
    network::NetworkAddress client_endpoint;
    goldsrc::ClientToServerNetchanPacket initial_request;
};

[[nodiscard]] StartedSession reach_first_service_request(
    network::UdpSocket& server,
    goldsrc::GoldSrcHandshakeCoordinator& handshake,
    const goldsrc::ChallengeExchangeTimePoint epoch,
    std::string& error)
{
    REQUIRE(handshake.start(epoch));
    const auto challenge_request = receive_bounded(
        server,
        goldsrc::kMaximumConnectionlessChallengeDatagramSize);
    const auto expected_challenge = goldsrc::build_getchallenge_request();
    REQUIRE(expected_challenge);
    CHECK(challenge_request.payload == *expected_challenge.datagram);
    const auto client_endpoint = challenge_request.source;

    constexpr std::uint32_t challenge = 0x7f00'3111U;
    send_server_datagram(
        server,
        client_endpoint,
        challenge_response(challenge),
        error);
    handshake.update(epoch + 1ms);
    const auto connect = receive_bounded(server, goldsrc::kMaximumConnectDatagramSize);
    CHECK(connect.source == client_endpoint);
    auto profile = goldsrc::ConnectCompatibilityProfile{};
    profile.protected_authentication_is_ascii_hex = false;
    const auto parsed = goldsrc::parse_connect_request(connect.payload, profile);
    REQUIRE(parsed);
    REQUIRE(parsed.request);
    CHECK(parsed.request->challenge() == challenge);

    send_server_datagram(
        server,
        client_endpoint,
        accept_response(client_endpoint),
        error);
    handshake.update(epoch + 2ms);
    REQUIRE_FALSE(handshake.terminal());
    REQUIRE(handshake.local_endpoint());
    CHECK(*handshake.local_endpoint() == client_endpoint);
    CHECK(handshake.connect_send_attempts() == 1U);

    handshake.update(epoch + 3ms);
    const auto initial_datagram = receive_bounded(
        server,
        goldsrc::kMaximumNetchanDatagramSize);
    auto initial = decode_client_packet(initial_datagram, client_endpoint);
    const std::array exact_new{
        std::byte{0x03U}, std::byte{'n'}, std::byte{'e'}, std::byte{'w'},
        std::byte{0U}};
    REQUIRE(initial.payload.size() >= exact_new.size());
    CHECK(std::ranges::equal(
        std::span<const std::byte>{initial.payload}.first(exact_new.size()),
        exact_new));
    CHECK(initial.header.sequence.flags.reliable);
    return {client_endpoint, std::move(initial)};
}

struct FirstServiceBatchExchange {
    std::uint32_t next_server_sequence{1U};
    goldsrc::ChallengeExchangeTimePoint next_update{};
};

[[nodiscard]] FirstServiceBatchExchange send_first_service_batch(
    network::UdpSocket& server,
    goldsrc::GoldSrcHandshakeCoordinator& handshake,
    const StartedSession& started,
    const goldsrc::ChallengeExchangeTimePoint now,
    const std::span<const std::byte> semantic_payload,
    std::string& error)
{
    const auto envelope = service_envelope(semantic_payload);
    const auto fragment_count_size =
        (envelope.size() + goldsrc::kStockProtocol48NormalFragmentChunkSize - 1U) /
        goldsrc::kStockProtocol48NormalFragmentChunkSize;
    REQUIRE(fragment_count_size >= 2U);
    REQUIRE(fragment_count_size <= (std::numeric_limits<std::uint16_t>::max)());
    const auto fragment_count = static_cast<std::uint16_t>(fragment_count_size);

    auto client_sequence =
        started.initial_request.header.sequence.sequence.value();
    auto update_time = now;
    std::uint32_t server_sequence = 1U;
    for (std::uint16_t index = 1U; index <= fragment_count; ++index) {
        const auto offset = static_cast<std::size_t>(index - 1U) *
            goldsrc::kStockProtocol48NormalFragmentChunkSize;
        const auto length = (std::min)(
            goldsrc::kStockProtocol48NormalFragmentChunkSize,
            envelope.size() - offset);
        send_server_datagram(
            server,
            started.client_endpoint,
            server_fragment(
                server_sequence++,
                client_sequence,
                true,
                index,
                fragment_count,
                std::span<const std::byte>{envelope}.subspan(offset, length)),
            error);
        handshake.update(update_time);
        const auto acknowledgement = decode_client_packet(
            receive_bounded(server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        check_transport_only_packet(acknowledgement);
        client_sequence = acknowledgement.header.sequence.sequence.value();
        update_time += 1ms;
    }

    return {server_sequence, update_time};
}

[[nodiscard]] FirstServiceBatchExchange send_first_service_batch(
    network::UdpSocket& server,
    goldsrc::GoldSrcHandshakeCoordinator& handshake,
    const StartedSession& started,
    const goldsrc::ChallengeExchangeTimePoint now,
    const bool multiple_user_info_messages,
    std::string& error)
{
    const auto semantic_payload = first_semantic_payload(
        multiple_user_info_messages);
    return send_first_service_batch(
        server,
        handshake,
        started,
        now,
        semantic_payload,
        error);
}

void run_user_info_stop_point()
{
    network::NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());
    std::string error;
    auto server = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(server);
    REQUIRE(server->bind(network::NetworkAddress::loopback(0U), error));
    const auto server_endpoint = server->local_address(error);
    REQUIRE(server_endpoint);
    auto client = network::UdpSocket::open_ipv4(runtime, error);
    REQUIRE(client);
    REQUIRE(client->bind(network::NetworkAddress::loopback(0U), error));
    network::UdpDatagramTransport transport{std::move(*client)};

    std::size_t releases = 0U;
    TraceCounts traces;
    auto prepared = prepared_request_with_session(releases);
    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        goldsrc::HandshakeStopPoint::user_info,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        response_config(),
        {},
        std::move(prepared.session),
        {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        user_info_config(),
        [&traces](const goldsrc::UserInfoSignonTraceEvent& event) {
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    user_info_message_decoded) {
                ++traces.user_info_messages;
            }
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    first_batch_complete) {
                ++traces.first_batch_completions;
            }
        }};
    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} + 7s;
    const auto started = reach_first_service_request(
        *server, handshake, epoch, error);
    CHECK(handshake.state() == goldsrc::GoldSrcHandshakeState::waiting_for_user_info);
    (void)send_first_service_batch(
        *server,
        handshake,
        started,
        epoch + 4ms,
        false,
        error);

    CHECK(handshake.state() == goldsrc::GoldSrcHandshakeState::user_info_complete);
    REQUIRE(handshake.user_info_result());
    CHECK(handshake.user_info_result()->message_count() == 1U);
    CHECK(handshake.user_info_result()->completion().remaining_byte_count() == 0U);
    CHECK(handshake.user_info_result()->source_payload().reassembled());
    CHECK(handshake.user_info_result()->source_payload().decompressed());
    CHECK_FALSE(handshake.resource_transition_result());
    CHECK(traces.user_info_messages == 1U);
    CHECK(traces.first_batch_completions == 1U);
    CHECK(releases == 1U);
    require_no_datagram(*server);
}

void run_malformed_user_info(
    const MalformedUserInfoScenario scenario)
{
    network::NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());
    std::string error;
    auto server = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(server);
    REQUIRE(server->bind(network::NetworkAddress::loopback(0U), error));
    const auto server_endpoint = server->local_address(error);
    REQUIRE(server_endpoint);
    auto client = network::UdpSocket::open_ipv4(runtime, error);
    REQUIRE(client);
    REQUIRE(client->bind(network::NetworkAddress::loopback(0U), error));
    network::UdpDatagramTransport transport{std::move(*client)};

    std::size_t releases = 0U;
    TraceCounts traces;
    auto prepared = prepared_request_with_session(releases);
    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        goldsrc::HandshakeStopPoint::resource_list_boundary,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        response_config(),
        {},
        std::move(prepared.session),
        {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        [&traces](const goldsrc::UserInfoSignonTraceEvent& event) {
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    user_info_message_decoded) {
                ++traces.user_info_messages;
            }
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    first_batch_complete) {
                ++traces.first_batch_completions;
            }
        },
        transition_config(),
        [&traces](const goldsrc::ResourceTransitionTraceEvent& event) {
            using Classification =
                goldsrc::ResourceTransitionTraceClassification;
            if (event.classification ==
                Classification::transition_request_queued) {
                ++traces.requests_queued;
            }
            if (event.classification ==
                Classification::transition_request_acknowledged) {
                ++traces.requests_acknowledged;
            }
            if (event.classification ==
                Classification::transition_control_decoded) {
                ++traces.controls;
            }
            if (event.classification ==
                Classification::neutral_opcode43_boundary_reached) {
                ++traces.boundaries;
            }
        }};
    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} +
        std::chrono::milliseconds{
            9'000 + static_cast<std::int64_t>(scenario)};
    const auto started = reach_first_service_request(
        *server,
        handshake,
        epoch,
        error);
    CHECK(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::waiting_for_resource_transition);

    const auto semantic_payload = malformed_first_semantic_payload(scenario);
    const auto first_batch = send_first_service_batch(
        *server,
        handshake,
        started,
        epoch + 4ms,
        semantic_payload,
        error);
    for (std::size_t attempt = 0U;
         attempt < 4U && !handshake.terminal();
         ++attempt) {
        handshake.update(
            first_batch.next_update +
            std::chrono::milliseconds{static_cast<std::int64_t>(attempt)});
    }

    // Each fragment acknowledgement was already consumed and checked as
    // transport-only. Anything remaining would be a forbidden transition TX.
    require_no_datagram(*server);
    REQUIRE(handshake.terminal());
    const auto expected_state = scenario == MalformedUserInfoScenario::wrong_opcode
        ? goldsrc::GoldSrcHandshakeState::
              resource_transition_unsupported_message
        : goldsrc::GoldSrcHandshakeState::protocol_error;
    CHECK(handshake.state() == expected_state);
    CHECK_FALSE(handshake.user_info_result());
    CHECK_FALSE(handshake.resource_transition_result());
    REQUIRE(handshake.resource_transition_error());
    CHECK(handshake.resource_transition_error()->code ==
          goldsrc::ResourceTransitionStageErrorCode::user_info_stage_failed);
    REQUIRE(handshake.resource_transition_error()->user_info_code);
    const auto expected_user_info_code =
        scenario == MalformedUserInfoScenario::wrong_opcode
        ? goldsrc::UserInfoSignonStageErrorCode::movement_stage_failed
        : goldsrc::UserInfoSignonStageErrorCode::
              user_info_stream_decode_failed;
    CHECK(*handshake.resource_transition_error()->user_info_code ==
          expected_user_info_code);
    CHECK_FALSE(handshake.error_context().empty());
    CHECK(traces.user_info_messages == 0U);
    CHECK(traces.first_batch_completions == 0U);
    CHECK(traces.requests_queued == 0U);
    CHECK(traces.requests_acknowledged == 0U);
    CHECK(traces.controls == 0U);
    CHECK(traces.boundaries == 0U);
    CHECK(releases == 1U);

    handshake.update(first_batch.next_update + 100ms);
    handshake.cancel(first_batch.next_update + 200ms);
    CHECK(releases == 1U);
    require_no_datagram(*server);
}

void run_resource_transition(
    const std::size_t run,
    const IntegrationScenario scenario)
{
    INFO("fake-HLDS resource-transition run " << run + 1U);
    network::NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());
    std::string error;
    auto server = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(server);
    REQUIRE(server->bind(network::NetworkAddress::loopback(0U), error));
    const auto server_endpoint = server->local_address(error);
    REQUIRE(server_endpoint);
    auto client = network::UdpSocket::open_ipv4(runtime, error);
    REQUIRE(client);
    REQUIRE(client->bind(network::NetworkAddress::loopback(0U), error));
    network::UdpDatagramTransport transport{std::move(*client)};

    std::size_t releases = 0U;
    TraceCounts traces;
    auto prepared = prepared_request_with_session(releases);
    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        goldsrc::HandshakeStopPoint::resource_list_boundary,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        response_config(),
        {},
        std::move(prepared.session),
        {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        [&traces](const goldsrc::UserInfoSignonTraceEvent& event) {
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    user_info_message_decoded) {
                ++traces.user_info_messages;
            }
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    first_batch_complete) {
                ++traces.first_batch_completions;
            }
        },
        transition_config(),
        [&traces](const goldsrc::ResourceTransitionTraceEvent& event) {
            using Classification =
                goldsrc::ResourceTransitionTraceClassification;
            if (event.classification == Classification::transition_request_queued) {
                ++traces.requests_queued;
            }
            if (event.classification ==
                Classification::transition_request_acknowledged) {
                ++traces.requests_acknowledged;
            }
            if (event.classification == Classification::transition_control_decoded) {
                ++traces.controls;
            }
            if (event.classification ==
                Classification::neutral_opcode43_boundary_reached) {
                ++traces.boundaries;
            }
        }};
    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} +
        std::chrono::milliseconds{8'000 + static_cast<std::int64_t>(run)};
    const auto started = reach_first_service_request(
        *server, handshake, epoch, error);
    CHECK(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::waiting_for_resource_transition);
    const bool multiple_user_info_messages =
        scenario == IntegrationScenario::multiple_user_info;
    const auto first_batch = send_first_service_batch(
        *server,
        handshake,
        started,
        epoch + 4ms,
        multiple_user_info_messages,
        error);
    CHECK_FALSE(handshake.terminal());
    CHECK(traces.first_batch_completions == 1U);

    handshake.update(first_batch.next_update);
    auto transition = decode_client_packet(
        receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
        started.client_endpoint);
    REQUIRE(is_exact_transition_request(transition));
    CHECK(transition.header.sequence.flags.reliable);
    std::size_t transition_datagrams = 1U;
    std::uint32_t last_client_sequence =
        transition.header.sequence.sequence.value();
    std::uint32_t server_sequence = first_batch.next_server_sequence;

    if (scenario == IntegrationScenario::baseline ||
        scenario == IntegrationScenario::multiple_user_info) {
        send_server_datagram(
            *server,
            started.client_endpoint,
            server_packet(
                server_sequence++,
                false,
                last_client_sequence,
                false,
                {}),
            error);
        handshake.update(first_batch.next_update + 1ms);
        CHECK_FALSE(handshake.terminal());
        CHECK(traces.requests_acknowledged == 1U);
    } else {
        send_server_datagram(
            *server,
            started.client_endpoint,
            server_packet(
                server_sequence++,
                true,
                last_client_sequence,
                true,
                {}),
            error);
        handshake.update(first_batch.next_update + 1ms);
        auto first_probe_ack = decode_client_packet(
            receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        check_transport_only_packet(first_probe_ack);
        last_client_sequence =
            first_probe_ack.header.sequence.sequence.value();

        send_server_datagram(
            *server,
            started.client_endpoint,
            server_packet(
                server_sequence++,
                true,
                last_client_sequence,
                true,
                {}),
            error);
        handshake.update(first_batch.next_update + 2ms);
        auto retry = decode_client_packet(
            receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        REQUIRE(is_exact_transition_request(retry));
        ++transition_datagrams;
        last_client_sequence = retry.header.sequence.sequence.value();
        CHECK_FALSE(handshake.terminal());
    }

    const auto envelope = service_envelope(second_semantic_payload());
    REQUIRE(envelope.size() > goldsrc::kStockProtocol48NormalFragmentChunkSize);
    const auto count_size =
        (envelope.size() + goldsrc::kStockProtocol48NormalFragmentChunkSize - 1U) /
        goldsrc::kStockProtocol48NormalFragmentChunkSize;
    REQUIRE(count_size >= 3U);
    REQUIRE(count_size <= (std::numeric_limits<std::uint16_t>::max)());
    const auto fragment_count = static_cast<std::uint16_t>(count_size);

    auto now = first_batch.next_update + 3ms;
    for (std::uint16_t index = 1U; index <= fragment_count; ++index) {
        const auto offset = static_cast<std::size_t>(index - 1U) *
            goldsrc::kStockProtocol48NormalFragmentChunkSize;
        const auto length = (std::min)(
            goldsrc::kStockProtocol48NormalFragmentChunkSize,
            envelope.size() - offset);
        const bool old_generation =
            scenario == IntegrationScenario::dropped_acknowledgement;
        send_server_datagram(
            *server,
            started.client_endpoint,
            server_fragment(
                server_sequence++,
                last_client_sequence,
                old_generation,
                index,
                fragment_count,
                std::span<const std::byte>{envelope}.subspan(offset, length)),
            error);
        handshake.update(now);
        auto response = decode_client_packet(
            receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        if (is_exact_transition_request(response)) {
            ++transition_datagrams;
        } else {
            check_transport_only_packet(response);
        }
        last_client_sequence = response.header.sequence.sequence.value();
        now += 1ms;
    }

    if (scenario == IntegrationScenario::dropped_acknowledgement) {
        CHECK_FALSE(handshake.terminal());
        CHECK_FALSE(handshake.resource_transition_result());
        send_server_datagram(
            *server,
            started.client_endpoint,
            server_packet(
                server_sequence,
                false,
                last_client_sequence,
                false,
                {}),
            error);
        handshake.update(now);
    }

    CHECK(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::resource_transition_boundary_reached);
    REQUIRE(handshake.terminal());
    REQUIRE(handshake.resource_transition_result());
    const auto& result = *handshake.resource_transition_result();
    const std::size_t expected_user_info_messages =
        multiple_user_info_messages ? 2U : 1U;
    CHECK(result.user_info().message_count() == expected_user_info_messages);
    CHECK(result.user_info().source_payload().reassembled());
    CHECK(result.user_info().source_payload().decompressed());
    CHECK(result.request().message_bytes() == kExactTransitionRequest.size());
    CHECK(result.control().body_bytes() == 8U);
    CHECK(result.boundary().opcode() == 43U);
    CHECK(result.boundary().byte_offset() == 9U);
    CHECK(result.boundary().remaining_byte_count() == 4'096U);
    CHECK(result.source_payload().reassembled());
    CHECK(result.source_payload().decompressed());
    CHECK(traces.user_info_messages == expected_user_info_messages);
    CHECK(traces.first_batch_completions == 1U);
    CHECK(traces.requests_queued == 1U);
    CHECK(traces.requests_acknowledged == 1U);
    CHECK(traces.controls == 1U);
    CHECK(traces.boundaries == 1U);
    if (scenario == IntegrationScenario::baseline ||
        scenario == IntegrationScenario::multiple_user_info) {
        CHECK(transition_datagrams == 1U);
    } else if (scenario == IntegrationScenario::dropped_request) {
        CHECK(transition_datagrams == 2U);
    } else {
        CHECK(transition_datagrams >= 2U);
    }
    CHECK(handshake.error_context().empty());
    CHECK(releases == 1U);
    require_no_datagram(*server);
    handshake.update(now + 100ms);
    handshake.cancel(now + 200ms);
    CHECK(releases == 1U);
    require_no_datagram(*server);
}

void run_resource_list(
    const std::size_t run,
    const ResourceListIntegrationScenario scenario)
{
    INFO("fake-HLDS resource-list run " << run + 1U);
    network::NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());
    std::string error;
    auto server = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(server);
    REQUIRE(server->bind(network::NetworkAddress::loopback(0U), error));
    const auto server_endpoint = server->local_address(error);
    REQUIRE(server_endpoint);
    auto client = network::UdpSocket::open_ipv4(runtime, error);
    REQUIRE(client);
    REQUIRE(client->bind(network::NetworkAddress::loopback(0U), error));
    network::UdpDatagramTransport transport{std::move(*client)};

    std::size_t releases = 0U;
    TraceCounts traces;
    auto prepared = prepared_request_with_session(releases);
    auto list_config = resource_list_config();
    if (scenario == ResourceListIntegrationScenario::resource_size_limit) {
        list_config.resource_list.maximum_resource_declared_size = 999U;
    } else if (scenario ==
               ResourceListIntegrationScenario::resource_total_size_limit) {
        list_config.resource_list.maximum_resource_declared_size = 6'000U;
        list_config.resource_list.maximum_resource_total_declared_size = 6'000U;
    } else if (scenario == ResourceListIntegrationScenario::missing_fragment) {
        list_config.transition.user_info.movement_environment.delta.pre_resource
            .initial_signon.driver.fragment_transfer_timeout = 20ms;
    } else if (scenario == ResourceListIntegrationScenario::timeout) {
        list_config.transition.user_info.movement_environment.delta.pre_resource
            .initial_signon.driver.channel_inactivity_timeout = 20ms;
    } else if (scenario == ResourceListIntegrationScenario::event_backpressure) {
        list_config.maximum_stage_events = 2U;
    }
    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        goldsrc::HandshakeStopPoint::resource_list,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        response_config(),
        {},
        std::move(prepared.session),
        {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        [&traces](const goldsrc::UserInfoSignonTraceEvent& event) {
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    user_info_message_decoded) {
                ++traces.user_info_messages;
            }
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    first_batch_complete) {
                ++traces.first_batch_completions;
            }
        },
        transition_config(),
        [&traces](const goldsrc::ResourceTransitionTraceEvent& event) {
            using Classification =
                goldsrc::ResourceTransitionTraceClassification;
            if (event.classification ==
                Classification::transition_request_queued) {
                ++traces.requests_queued;
            }
            if (event.classification ==
                Classification::transition_request_acknowledged) {
                ++traces.requests_acknowledged;
            }
            if (event.classification ==
                Classification::transition_control_decoded) {
                ++traces.controls;
            }
            if (event.classification ==
                Classification::neutral_opcode43_boundary_reached) {
                ++traces.boundaries;
            }
        },
        list_config,
        [&traces](const goldsrc::ResourceListTraceEvent& event) {
            using Classification = goldsrc::ResourceListTraceClassification;
            if (event.classification == Classification::resource_list_decoded) {
                ++traces.resource_lists;
            }
            if (event.classification ==
                Classification::resource_entry_metadata) {
                ++traces.resource_entries;
            }
            if (event.classification ==
                Classification::post_resource_boundary_reached) {
                ++traces.post_list_boundaries;
            }
            if (event.classification ==
                Classification::client_response_required) {
                ++traces.client_response_boundaries;
            }
        }};

    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} +
        std::chrono::milliseconds{30'000 + static_cast<std::int64_t>(run)};
    const auto started = reach_first_service_request(
        *server, handshake, epoch, error);
    CHECK(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::waiting_for_resource_list);
    const auto first_batch = send_first_service_batch(
        *server,
        handshake,
        started,
        epoch + 4ms,
        false,
        error);
    CHECK_FALSE(handshake.terminal());
    CHECK(traces.first_batch_completions == 1U);

    handshake.update(first_batch.next_update);
    const auto transition = decode_client_packet(
        receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
        started.client_endpoint);
    REQUIRE(is_exact_transition_request(transition));
    CHECK(transition.header.sequence.flags.reliable);
    auto last_client_sequence = transition.header.sequence.sequence.value();
    auto server_sequence = first_batch.next_server_sequence;
    send_server_datagram(
        *server,
        started.client_endpoint,
        server_packet(
            server_sequence++,
            false,
            last_client_sequence,
            false,
            {}),
        error);
    handshake.update(first_batch.next_update + 1ms);
    CHECK_FALSE(handshake.terminal());
    CHECK(traces.requests_acknowledged == 1U);

    if (scenario == ResourceListIntegrationScenario::timeout) {
        handshake.update(first_batch.next_update + 25ms);
        REQUIRE(handshake.terminal());
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::resource_list_timed_out);
        CHECK_FALSE(handshake.resource_list_result());
        REQUIRE(handshake.resource_list_error());
        CHECK(handshake.resource_list_error()->code ==
              goldsrc::ResourceListStageErrorCode::transition_stage_failed);
        CHECK(handshake.resource_list_error()->driver_code ==
              goldsrc::NetchanDriverErrorCode::channel_inactivity_timed_out);
        CHECK(traces.resource_lists == 0U);
        CHECK(releases == 1U);
        require_no_datagram(*server);
        handshake.update(first_batch.next_update + 30ms);
        handshake.cancel(first_batch.next_update + 31ms);
        CHECK(releases == 1U);
        return;
    }

    if (scenario == ResourceListIntegrationScenario::cancellation) {
        handshake.cancel(first_batch.next_update + 2ms);
        REQUIRE(handshake.terminal());
        CHECK(handshake.state() == goldsrc::GoldSrcHandshakeState::cancelled);
        CHECK_FALSE(handshake.resource_list_result());
        CHECK_FALSE(handshake.resource_list_error());
        CHECK(traces.resource_lists == 0U);
        CHECK(releases == 1U);
        require_no_datagram(*server);
        handshake.cancel(first_batch.next_update + 3ms);
        CHECK(releases == 1U);
        return;
    }

    const auto semantic = second_resource_list_payload(scenario, run);
    auto envelope = service_envelope(semantic);
    if (scenario == ResourceListIntegrationScenario::malformed_bzip2) {
        REQUIRE(envelope.size() > goldsrc::kServicePayloadEnvelopeHeaderSize);
        std::fill(
            envelope.begin() + static_cast<std::ptrdiff_t>(
                goldsrc::kServicePayloadEnvelopeHeaderSize),
            envelope.end(),
            std::byte{0U});
    }
    const auto count_size =
        (envelope.size() + goldsrc::kStockProtocol48NormalFragmentChunkSize - 1U) /
        goldsrc::kStockProtocol48NormalFragmentChunkSize;
    REQUIRE(count_size >= 1U);
    if (scenario != ResourceListIntegrationScenario::truncated_count &&
        scenario != ResourceListIntegrationScenario::unterminated_name) {
        REQUIRE(count_size >= 3U);
    }
    REQUIRE(count_size <= (std::numeric_limits<std::uint16_t>::max)());
    const auto fragment_count = static_cast<std::uint16_t>(count_size);
    std::vector<std::uint16_t> fragment_order;
    fragment_order.reserve(fragment_count);
    for (std::uint16_t index = 1U; index <= fragment_count; ++index) {
        fragment_order.push_back(index);
    }
    if (scenario ==
        ResourceListIntegrationScenario::reordered_fragments) {
        std::reverse(fragment_order.begin() + 1, fragment_order.end());
    }

    if (scenario == ResourceListIntegrationScenario::missing_fragment) {
        REQUIRE(fragment_order.size() >= 2U);
        fragment_order.pop_back();
    }

    auto now = first_batch.next_update + 2ms;
    if (scenario == ResourceListIntegrationScenario::wrong_endpoint) {
        auto intruder = network::UdpSocket::open_ipv4(runtime, error);
        REQUIRE(intruder);
        REQUIRE(intruder->bind(network::NetworkAddress::loopback(0U), error));
        const auto first_length = (std::min)(
            goldsrc::kStockProtocol48NormalFragmentChunkSize,
            envelope.size());
        send_server_datagram(
            *intruder,
            started.client_endpoint,
            server_fragment(
                server_sequence + 100U,
                last_client_sequence,
                false,
                1U,
                fragment_count,
                std::span<const std::byte>{envelope}.first(first_length)),
            error);
        handshake.update(now);
        CHECK_FALSE(handshake.terminal());
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::waiting_for_resource_list);
        require_no_datagram(*server);
        require_no_datagram(*intruder);
        now += 1ms;
    }
    for (const auto index : fragment_order) {
        const auto offset = static_cast<std::size_t>(index - 1U) *
            goldsrc::kStockProtocol48NormalFragmentChunkSize;
        const auto length = (std::min)(
            goldsrc::kStockProtocol48NormalFragmentChunkSize,
            envelope.size() - offset);
        send_server_datagram(
            *server,
            started.client_endpoint,
            server_fragment(
                server_sequence++,
                last_client_sequence,
                false,
                index,
                fragment_count,
                std::span<const std::byte>{envelope}.subspan(offset, length)),
            error);
        handshake.update(now);
        const auto acknowledgement = decode_client_packet(
            receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        check_transport_only_packet(acknowledgement);
        last_client_sequence = acknowledgement.header.sequence.sequence.value();
        now += 1ms;
    }

    if (scenario == ResourceListIntegrationScenario::missing_fragment) {
        REQUIRE_FALSE(handshake.terminal());
        handshake.update(first_batch.next_update + 30ms);
        REQUIRE(handshake.terminal());
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::resource_list_timed_out);
        CHECK_FALSE(handshake.resource_list_result());
        REQUIRE(handshake.resource_list_error());
        CHECK(handshake.resource_list_error()->code ==
              goldsrc::ResourceListStageErrorCode::transition_stage_failed);
        CHECK(handshake.resource_list_error()->driver_code ==
              goldsrc::NetchanDriverErrorCode::fragment_transfer_timed_out);
        CHECK(traces.resource_lists == 0U);
        CHECK(traces.resource_entries == 0U);
        CHECK(releases == 1U);
        require_no_datagram(*server);
        handshake.update(first_batch.next_update + 31ms);
        handshake.cancel(first_batch.next_update + 32ms);
        CHECK(releases == 1U);
        return;
    }

    if (scenario == ResourceListIntegrationScenario::malformed_bzip2) {
        REQUIRE(handshake.terminal());
        CHECK(handshake.state() == goldsrc::GoldSrcHandshakeState::protocol_error);
        CHECK_FALSE(handshake.resource_list_result());
        REQUIRE(handshake.resource_list_error());
        CHECK(handshake.resource_list_error()->code ==
              goldsrc::ResourceListStageErrorCode::transition_stage_failed);
        CHECK(handshake.resource_list_error()->transition_code ==
              goldsrc::ResourceTransitionStageErrorCode::
                  second_payload_envelope_decode_failed);
        CHECK(traces.resource_lists == 0U);
        CHECK(traces.resource_entries == 0U);
        CHECK(releases == 1U);
        require_no_datagram(*server);
        handshake.update(now + 1ms);
        handshake.cancel(now + 2ms);
        CHECK(releases == 1U);
        return;
    }

    if (scenario == ResourceListIntegrationScenario::event_backpressure) {
        REQUIRE(handshake.terminal());
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::resource_list_backpressure);
        CHECK_FALSE(handshake.resource_list_result());
        REQUIRE(handshake.resource_list_error());
        CHECK(handshake.resource_list_error()->code ==
              goldsrc::ResourceListStageErrorCode::event_backpressure);
        CHECK_FALSE(handshake.resource_list_error()->resource_list_code);
        CHECK(traces.resource_lists == 0U);
        CHECK(traces.resource_entries == 0U);
        CHECK(traces.post_list_boundaries == 0U);
        CHECK(traces.client_response_boundaries == 0U);
        CHECK(releases == 1U);
        require_no_datagram(*server);
        handshake.update(now + 1ms);
        handshake.cancel(now + 2ms);
        CHECK(releases == 1U);
        return;
    }

    const auto expected_error = expected_resource_list_error(scenario);
    if (expected_error) {
        REQUIRE(handshake.terminal());
        const auto expected_state =
            *expected_error ==
                    goldsrc::ResourceListErrorCode::unsupported_resource_profile
                ? goldsrc::GoldSrcHandshakeState::
                      resource_list_unsupported_profile
                : goldsrc::GoldSrcHandshakeState::protocol_error;
        CHECK(handshake.state() == expected_state);
        CHECK_FALSE(handshake.resource_list_result());
        REQUIRE(handshake.resource_list_error());
        CHECK(handshake.resource_list_error()->code ==
              goldsrc::ResourceListStageErrorCode::resource_list_decode_failed);
        CHECK(handshake.resource_list_error()->resource_list_code ==
              expected_error);
        CHECK(traces.user_info_messages == 1U);
        CHECK(traces.first_batch_completions == 1U);
        CHECK(traces.requests_queued == 1U);
        CHECK(traces.requests_acknowledged == 1U);
        CHECK(traces.controls == 1U);
        CHECK(traces.boundaries == 1U);
        CHECK(traces.resource_lists == 0U);
        CHECK(traces.resource_entries == 0U);
        CHECK(traces.post_list_boundaries == 0U);
        CHECK(traces.client_response_boundaries == 0U);
        CHECK_FALSE(handshake.error_context().empty());
        CHECK(releases == 1U);
        require_no_datagram(*server);
        handshake.update(now + 100ms);
        handshake.cancel(now + 200ms);
        CHECK(releases == 1U);
        require_no_datagram(*server);
        return;
    }

    REQUIRE(handshake.terminal());
    CHECK(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::
              resource_list_client_response_required);
    REQUIRE(handshake.resource_list_result());
    const auto& result = *handshake.resource_list_result();
    const auto& list = result.resource_list();
    CHECK(result.transition().request().message_bytes() ==
          kExactTransitionRequest.size());
    CHECK(result.transition().control().body_bytes() == 8U);
    CHECK(result.transition().boundary().opcode() == 43U);
    CHECK(result.transition().boundary().byte_offset() == 9U);
    CHECK(result.transition().source_payload().reassembled());
    CHECK(result.transition().source_payload().decompressed());
    CHECK(list.resource_count() == 128U);
    CHECK(list.source_opcode_byte_offset() == 9U);
    CHECK(list.source_payload_bit_length() == semantic.size() * 8U);
    CHECK(list.bits_consumed() == (semantic.size() - 9U) * 8U);
    CHECK(list.bytes_consumed() == semantic.size() - 9U);
    CHECK(list.next_byte_offset() == semantic.size());
    CHECK(list.next_bit_offset() == 0U);
    CHECK(result.boundary().byte_offset() == semantic.size());
    CHECK(result.boundary().bit_offset() == 0U);
    CHECK(result.boundary().remaining_byte_count() == 0U);
    CHECK_FALSE(result.client_response().response_builder_available());
    CHECK_FALSE(result.client_response().response_queued());
    if (scenario == ResourceListIntegrationScenario::differential_map) {
        constexpr std::array<std::string_view, 3U> map_names{
            "maps/differential_alpha.bsp",
            "maps/differential_bravo.bsp",
            "maps/differential_charlie.bsp",
        };
        const auto variant = run % map_names.size();
        CHECK(list.entries()[1U].name().bytes() ==
              map_names[variant]);
        CHECK(list.entries()[1U].type() == goldsrc::ResourceType::model);
        CHECK(list.entries()[1U].declared_size().raw_code() ==
              98'765U + variant);
    }
    if (scenario == ResourceListIntegrationScenario::malicious_names) {
        CHECK(list.entries()[0U].name().bytes() == "../evil");
        CHECK(list.entries()[1U].name().bytes() == "..\\evil");
        CHECK(list.entries()[2U].name().bytes() == "C:\\evil");
        CHECK(list.entries()[3U].name().bytes() == "\\\\server\\share");
        CHECK(list.entries()[4U].name().bytes() == "/absolute");
    }
    CHECK(traces.user_info_messages == 1U);
    CHECK(traces.first_batch_completions == 1U);
    CHECK(traces.requests_queued == 1U);
    CHECK(traces.requests_acknowledged == 1U);
    CHECK(traces.controls == 1U);
    CHECK(traces.boundaries == 1U);
    CHECK(traces.resource_lists == 1U);
    CHECK(traces.resource_entries == 128U);
    CHECK(traces.post_list_boundaries == 1U);
    CHECK(traces.client_response_boundaries == 1U);
    CHECK(handshake.error_context().empty());
    CHECK(releases == 1U);

    if (scenario ==
        ResourceListIntegrationScenario::duplicate_completed_batch) {
        const auto published_count = list.resource_count();
        for (std::uint16_t index = 1U; index <= fragment_count; ++index) {
            const auto offset = static_cast<std::size_t>(index - 1U) *
                goldsrc::kStockProtocol48NormalFragmentChunkSize;
            const auto length = (std::min)(
                goldsrc::kStockProtocol48NormalFragmentChunkSize,
                envelope.size() - offset);
            send_server_datagram(
                *server,
                started.client_endpoint,
                server_fragment(
                    server_sequence++,
                    last_client_sequence,
                    false,
                    index,
                    fragment_count,
                    std::span<const std::byte>{envelope}.subspan(offset, length)),
                error);
        }
        handshake.update(now + 1ms);
        REQUIRE(handshake.resource_list_result());
        CHECK(handshake.resource_list_result()->resource_list().resource_count() ==
              published_count);
        CHECK(traces.resource_lists == 1U);
        CHECK(traces.resource_entries == 128U);
        CHECK(traces.post_list_boundaries == 1U);
        CHECK(traces.client_response_boundaries == 1U);
    }

    require_no_datagram(*server);
    handshake.update(now + 100ms);
    handshake.cancel(now + 200ms);
    CHECK(releases == 1U);
    require_no_datagram(*server);
}

void run_resource_response_with_provider(
    const std::size_t run,
    const ResourceResponseIntegrationScenario scenario,
    consistency::IResourceConsistencyProvider& provider,
    const std::span<const std::byte, 41U> expected_response,
    const std::uint32_t expected_byte_count,
    IntegrationResourceConsistencyProvider* synthetic_provider)
{
    INFO("fake-HLDS resource-response run " << run + 1U);
    network::NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());
    std::string error;
    auto server = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(server);
    REQUIRE(server->bind(network::NetworkAddress::loopback(0U), error));
    const auto server_endpoint = server->local_address(error);
    REQUIRE(server_endpoint);
    auto client = network::UdpSocket::open_ipv4(runtime, error);
    REQUIRE(client);
    REQUIRE(client->bind(network::NetworkAddress::loopback(0U), error));
    network::UdpDatagramTransport transport{std::move(*client)};

    std::size_t authentication_releases = 0U;
    TraceCounts traces;
    auto prepared = prepared_request_with_session(authentication_releases);
    auto stage_config = resource_client_response_config();
    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        goldsrc::HandshakeStopPoint::resource_response_boundary,
        std::move(prepared.request),
        challenge_config(),
        {},
        {},
        response_config(),
        {},
        std::move(prepared.session),
        {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        [&traces](const goldsrc::UserInfoSignonTraceEvent& event) {
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    user_info_message_decoded) {
                ++traces.user_info_messages;
            }
            if (event.classification ==
                goldsrc::UserInfoSignonTraceClassification::
                    first_batch_complete) {
                ++traces.first_batch_completions;
            }
        },
        transition_config(),
        [&traces](const goldsrc::ResourceTransitionTraceEvent& event) {
            using Classification =
                goldsrc::ResourceTransitionTraceClassification;
            if (event.classification ==
                Classification::transition_request_queued) {
                ++traces.requests_queued;
            }
            if (event.classification ==
                Classification::transition_request_acknowledged) {
                ++traces.requests_acknowledged;
            }
            if (event.classification ==
                Classification::transition_control_decoded) {
                ++traces.controls;
            }
            if (event.classification ==
                Classification::neutral_opcode43_boundary_reached) {
                ++traces.boundaries;
            }
        },
        stage_config.resource_list,
        [&traces](const goldsrc::ResourceListTraceEvent& event) {
            using Classification = goldsrc::ResourceListTraceClassification;
            if (event.classification == Classification::resource_list_decoded) {
                ++traces.resource_lists;
            }
            if (event.classification ==
                Classification::resource_entry_metadata) {
                ++traces.resource_entries;
            }
            if (event.classification ==
                Classification::post_resource_boundary_reached) {
                ++traces.post_list_boundaries;
            }
            if (event.classification ==
                Classification::client_response_required) {
                ++traces.client_response_boundaries;
            }
        },
        stage_config,
        &provider,
        [&traces](const goldsrc::ResourceClientResponseTraceEvent& event) {
            using Classification =
                goldsrc::ResourceClientResponseTraceClassification;
            if (event.classification ==
                Classification::resource_response_requirements_ready) {
                ++traces.response_requirements;
            }
            if (event.classification ==
                Classification::resource_response_ready) {
                ++traces.responses_ready;
            }
            if (event.classification ==
                Classification::resource_response_queued) {
                ++traces.responses_queued;
            }
            if (event.classification ==
                Classification::resource_response_transmitted) {
                ++traces.responses_transmitted;
            }
            if (event.classification ==
                Classification::resource_response_acknowledged) {
                ++traces.responses_acknowledged;
            }
            if (event.classification ==
                Classification::next_server_boundary_reached) {
                ++traces.response_boundaries;
            }
        }};

    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} +
        std::chrono::milliseconds{50'000 + static_cast<std::int64_t>(run)};
    const auto started = reach_first_service_request(
        *server, handshake, epoch, error);
    REQUIRE(handshake.local_endpoint());
    CHECK(*handshake.local_endpoint() == started.client_endpoint);
    CHECK(handshake.connect_send_attempts() == 1U);
    CHECK(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::waiting_for_resource_response);

    const auto first_batch = send_first_service_batch(
        *server,
        handshake,
        started,
        epoch + 4ms,
        false,
        error);
    CHECK_FALSE(handshake.terminal());
    CHECK(traces.first_batch_completions == 1U);

    handshake.update(first_batch.next_update);
    const auto transition = decode_client_packet(
        receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
        started.client_endpoint);
    REQUIRE(is_exact_transition_request(transition));
    REQUIRE(transition.header.sequence.flags.reliable);
    auto last_client_sequence = transition.header.sequence.sequence.value();
    auto server_sequence = first_batch.next_server_sequence;
    send_server_datagram(
        *server,
        started.client_endpoint,
        server_packet(
            server_sequence++,
            false,
            last_client_sequence,
            false,
            {}),
        error);
    handshake.update(first_batch.next_update + 1ms);
    CHECK_FALSE(handshake.terminal());
    CHECK(traces.requests_acknowledged == 1U);

    const auto list_scenario =
        scenario == ResourceResponseIntegrationScenario::differential_map
            ? ResourceListIntegrationScenario::differential_map
        : scenario ==
                ResourceResponseIntegrationScenario::malicious_resource_names
            ? ResourceListIntegrationScenario::malicious_names
            : ResourceListIntegrationScenario::baseline;
    const auto semantic = second_resource_list_payload(list_scenario, run);
    const auto envelope = service_envelope(semantic);
    const auto count_size =
        (envelope.size() + goldsrc::kStockProtocol48NormalFragmentChunkSize -
         1U) /
        goldsrc::kStockProtocol48NormalFragmentChunkSize;
    REQUIRE(count_size >= 3U);
    REQUIRE(count_size <= (std::numeric_limits<std::uint16_t>::max)());
    const auto fragment_count = static_cast<std::uint16_t>(count_size);

    auto now = first_batch.next_update + 2ms;
    for (std::uint16_t index = 1U; index <= fragment_count; ++index) {
        const auto offset = static_cast<std::size_t>(index - 1U) *
            goldsrc::kStockProtocol48NormalFragmentChunkSize;
        const auto length = (std::min)(
            goldsrc::kStockProtocol48NormalFragmentChunkSize,
            envelope.size() - offset);
        send_server_datagram(
            *server,
            started.client_endpoint,
            server_fragment(
                server_sequence++,
                last_client_sequence,
                false,
                index,
                fragment_count,
                std::span<const std::byte>{envelope}.subspan(offset, length)),
            error);
        handshake.update(now);
        const auto acknowledgement = decode_client_packet(
            receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        check_transport_only_packet(acknowledgement);
        last_client_sequence =
            acknowledgement.header.sequence.sequence.value();
        now += 1ms;
    }

    // The response stage is allowed to publish after the final resource-list
    // transport ACK in the same update. This extra bounded update also covers
    // implementations that defer the client-first reliable send once.
    handshake.update(now);
    auto response = decode_client_packet(
        receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
        started.client_endpoint);
    check_exact_resource_response_packet(response, expected_response);
    std::size_t response_datagrams = 1U;
    CHECK_FALSE(handshake.terminal());
    if (synthetic_provider != nullptr) {
        CHECK(synthetic_provider->lifetime_releases == 0U);
    }

    if (scenario == ResourceResponseIntegrationScenario::coalesced_tail) {
        check_coalesced_response_carrier(response, run);
    }

    if (scenario ==
        ResourceResponseIntegrationScenario::dropped_acknowledgement) {
        const auto deliberately_dropped_ack = server_packet(
            server_sequence,
            false,
            response.header.sequence.sequence.value(),
            true,
            {});
        CHECK_FALSE(deliberately_dropped_ack.empty());
        handshake.update(now + 1ms);
        require_no_datagram(*server);
        now += 1ms;
    }

    if (scenario == ResourceResponseIntegrationScenario::dropped_response ||
        scenario ==
            ResourceResponseIntegrationScenario::dropped_acknowledgement) {
        // A newer reliable server unit first creates an ordinary outgoing ACK
        // gap. Its equal-sequence wrong-generation ACK cannot release the
        // response. Advancing that wrong generation past the response then
        // requests a transport retry of the same canonical fragment.
        send_server_datagram(
            *server,
            started.client_endpoint,
            server_packet(
                server_sequence++,
                true,
                response.header.sequence.sequence.value(),
                false,
                {}),
            error);
        handshake.update(now + 1ms);
        const auto gap_packet = decode_client_packet(
            receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        check_transport_only_packet(gap_packet);

        send_server_datagram(
            *server,
            started.client_endpoint,
            server_packet(
                server_sequence++,
                false,
                gap_packet.header.sequence.sequence.value(),
                false,
                {}),
            error);
        handshake.update(now + 2ms);
        auto retry = decode_client_packet(
            receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        check_exact_resource_response_packet(retry, expected_response);
        REQUIRE(response.fragments[0U]);
        REQUIRE(retry.fragments[0U]);
        CHECK(retry.fragments[0U]->packed_id() ==
              response.fragments[0U]->packed_id());
        CHECK(retry.payload == response.payload);
        CHECK(retry.header.sequence.sequence !=
              response.header.sequence.sequence);
        response = std::move(retry);
        ++response_datagrams;
        now += 2ms;
    }

    const auto next_payload = post_response_semantic_payload();
    send_server_datagram(
        *server,
        started.client_endpoint,
        server_packet(
            server_sequence++,
            false,
            response.header.sequence.sequence.value(),
            true,
            service_envelope(next_payload)),
        error);
    handshake.update(now + 1ms);
    now += 1ms;

    REQUIRE(handshake.terminal());
    REQUIRE(handshake.state() ==
            goldsrc::GoldSrcHandshakeState::
                resource_response_boundary_reached);
    REQUIRE_FALSE(handshake.resource_client_response_error());
    REQUIRE(handshake.resource_client_response_result());
    const auto& result = *handshake.resource_client_response_result();
    const auto& list = result.resource_list().resource_list();
    CHECK(list.resource_count() == 128U);
    CHECK(result.response().wire_name() == "tempdecal.wad");
    CHECK(result.response().byte_count() == expected_byte_count);
    CHECK_FALSE(result.source_carrier_geometry());
    CHECK_FALSE(result.concurrent_tail());
    CHECK(result.reliable_lifecycle().fragmented());
    CHECK(result.reliable_lifecycle().fragment_count() == 1U);
    CHECK(result.reliable_lifecycle().transmit_count() ==
          response_datagrams);
    CHECK(result.reliable_lifecycle().acknowledgement().sequence ==
          response.header.sequence.sequence);
    CHECK(result.reliable_lifecycle().acknowledgement().reliable);
    REQUIRE(result.boundary().opcode());
    CHECK(*result.boundary().opcode() == 3U);
    CHECK(result.boundary().remaining_byte_count() ==
          next_payload.size() - 1U);
    CHECK(result.boundary().source_payload().decompressed);
    if (scenario == ResourceResponseIntegrationScenario::differential_map) {
        constexpr std::array<std::string_view, 3U> map_names{
            "maps/differential_alpha.bsp",
            "maps/differential_bravo.bsp",
            "maps/differential_charlie.bsp",
        };
        const auto variant = run % map_names.size();
        CHECK(list.entries()[1U].name().bytes() == map_names[variant]);
        CHECK(list.entries()[1U].declared_size().raw_code() ==
              98'765U + variant);
    }
    if (scenario ==
        ResourceResponseIntegrationScenario::malicious_resource_names) {
        CHECK(list.entries()[0U].name().bytes() == "../evil");
        CHECK(list.entries()[1U].name().bytes() == "..\\evil");
        CHECK(list.entries()[2U].name().bytes() == "C:\\evil");
        CHECK(list.entries()[3U].name().bytes() == "\\\\server\\share");
        CHECK(list.entries()[4U].name().bytes() == "/absolute");
        const goldsrc::GoldSrcResourceNameMapper mapper;
        for (std::size_t index = 0U; index < 5U; ++index) {
            const auto classification = mapper.classify(
                list.entries()[index].type(),
                list.entries()[index].name());
            CHECK(classification.kind() ==
                  goldsrc::GoldSrcResourceNameClassificationKind::unsafe_name);
            CHECK_FALSE(classification.safe_virtual_name());
        }
    }

    CHECK(traces.user_info_messages == 1U);
    CHECK(traces.first_batch_completions == 1U);
    CHECK(traces.requests_queued == 1U);
    CHECK(traces.requests_acknowledged == 1U);
    CHECK(traces.controls == 1U);
    CHECK(traces.boundaries == 1U);
    CHECK(traces.resource_lists == 1U);
    CHECK(traces.resource_entries == 128U);
    CHECK(traces.post_list_boundaries == 1U);
    CHECK(traces.client_response_boundaries == 1U);
    CHECK(traces.response_requirements == 1U);
    CHECK(traces.responses_ready == 1U);
    CHECK(traces.responses_queued == 1U);
    CHECK(traces.responses_transmitted == 1U);
    CHECK(traces.responses_acknowledged == 1U);
    CHECK(traces.response_boundaries == 1U);
    if (synthetic_provider != nullptr) {
        CHECK(synthetic_provider->begins == 1U);
        CHECK(synthetic_provider->updates == 1U);
        CHECK(synthetic_provider->cancellations == 0U);
        CHECK(synthetic_provider->material_count == 1U);
        CHECK(synthetic_provider->opaque_byte_count == 16U);
        CHECK(synthetic_provider->filesystem_calls == 0U);
        CHECK(synthetic_provider->lifetime_releases == 1U);
    }
    CHECK(authentication_releases == 1U);
    CHECK(handshake.error_context().empty());
    REQUIRE(handshake.local_endpoint());
    CHECK(*handshake.local_endpoint() == started.client_endpoint);

    require_no_datagram(*server);
    handshake.update(now + 100ms);
    handshake.cancel(now + 200ms);
    if (synthetic_provider != nullptr) {
        CHECK(synthetic_provider->lifetime_releases == 1U);
    }
    CHECK(authentication_releases == 1U);
    require_no_datagram(*server);
}

void run_resource_response(
    const std::size_t run,
    const ResourceResponseIntegrationScenario scenario)
{
    IntegrationResourceConsistencyProvider provider;
    run_resource_response_with_provider(
        run,
        scenario,
        provider,
        kExactResourceResponse,
        0x01020304U,
        &provider);
}

struct SyntheticRootSnapshotEntry {
    std::string relative_name;
    std::uintmax_t byte_count{0U};
    std::filesystem::file_time_type write_time{};
    std::string contents;
    bool directory{false};

    friend bool operator==(
        const SyntheticRootSnapshotEntry&,
        const SyntheticRootSnapshotEntry&) = default;
};

[[nodiscard]] std::vector<SyntheticRootSnapshotEntry> snapshot_synthetic_root(
    const std::filesystem::path& root)
{
    std::vector<SyntheticRootSnapshotEntry> snapshot;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
        std::error_code error;
        const bool directory = entry.is_directory(error);
        REQUIRE_FALSE(error);
        const bool regular = entry.is_regular_file(error);
        REQUIRE_FALSE(error);

        SyntheticRootSnapshotEntry item;
        item.relative_name =
            std::filesystem::relative(entry.path(), root).generic_string();
        item.directory = directory;
        if (regular) {
            item.write_time = entry.last_write_time(error);
            REQUIRE_FALSE(error);
            item.byte_count = entry.file_size(error);
            REQUIRE_FALSE(error);
            std::ifstream stream{entry.path(), std::ios::binary};
            REQUIRE(stream);
            item.contents.assign(
                std::istreambuf_iterator<char>{stream},
                std::istreambuf_iterator<char>{});
            REQUIRE_FALSE(stream.bad());
        }
        snapshot.push_back(std::move(item));
    }
    std::ranges::sort(
        snapshot,
        {},
        &SyntheticRootSnapshotEntry::relative_name);
    return snapshot;
}

[[nodiscard]] bool can_open_synthetic_writer(
    const std::filesystem::path& path)
{
    const HANDLE writer = ::CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (writer == INVALID_HANDLE_VALUE) {
        return false;
    }
    static_cast<void>(::CloseHandle(writer));
    return true;
}

struct PrecacheManifestIntegrationTraceCounts {
    std::size_t initial_requests_queued{0U};
    std::size_t transition_requests_queued{0U};
    std::size_t resource_lists_decoded{0U};
    std::size_t resource_responses_queued{0U};
    std::size_t response_boundaries{0U};
    std::size_t manifest_publications{0U};
    std::size_t manifest_terminal_outcomes{0U};
    std::size_t world_entries_selected{0U};
    std::size_t asset_source_open_starts{0U};
    std::size_t asset_source_progress_events{0U};
    std::size_t asset_source_open_failures{0U};
    std::size_t importer_probes_completed{0U};
    std::size_t importers_selected{0U};
    std::size_t assets_imported{0U};
    std::size_t asset_import_failures{0U};
    std::size_t endpoint_mismatches{0U};
    std::optional<std::size_t> transmitted_at_response_boundary;
    std::optional<std::size_t> transmitted_at_manifest_publication;
    std::optional<std::size_t> transmitted_at_manifest_terminal;
    std::optional<std::size_t> transmitted_at_asset_terminal;
    std::optional<std::size_t> authentication_releases_at_manifest_terminal;
};

void write_manifest_integration_files(
    const hlclient::tests::ScopedLocalResourceTestRoot& root,
    const std::string_view game,
    const PrecacheManifestIntegrationScenario scenario)
{
    root.write(game, "tempdecal.wad", "abc");
    if (scenario !=
        PrecacheManifestIntegrationScenario::local_map_missing) {
        root.write(game, "maps/test_map.bsp", "synthetic bsp metadata");
    }
    if (scenario != PrecacheManifestIntegrationScenario::missing_model) {
        root.write(game, "models/test_model.mdl", "synthetic model metadata");
    }
    if (scenario !=
            PrecacheManifestIntegrationScenario::world_ready_missing_sound &&
        scenario != PrecacheManifestIntegrationScenario::ambiguous_sound) {
        root.write(game, "sound/test_sound.wav", "synthetic sound metadata");
    }
    if (scenario == PrecacheManifestIntegrationScenario::ambiguous_sound) {
        const auto sound_directory = root.game_path(game) / "sound";
        std::error_code error;
        std::filesystem::create_directories(sound_directory, error);
        REQUIRE_FALSE(error);
        if (!hlclient::tests::enable_case_sensitive_directory(
                sound_directory)) {
            SKIP("Case-sensitive directories are unavailable for the bounded ambiguous-path integration");
        }
        root.write(game, "sound/test_sound.wav", "lower candidate");
        root.write(game, "sound/TEST_SOUND.WAV", "upper candidate");
    }
    root.write(game, "events/test_event.sc", "synthetic event metadata");
    root.write(game, "generic/test.dat", "synthetic generic metadata");
}

[[nodiscard]] std::vector<std::byte> make_two_face_production_bsp()
{
    synthetic_bsp::SyntheticBspBuilder builder;
    constexpr std::array vertices{
        synthetic_bsp::SyntheticBspVector3{0.0F, 0.0F, 0.0F},
        synthetic_bsp::SyntheticBspVector3{64.0F, 0.0F, 0.0F},
        synthetic_bsp::SyntheticBspVector3{64.0F, 64.0F, 0.0F},
        synthetic_bsp::SyntheticBspVector3{0.0F, 64.0F, 0.0F},
    };
    constexpr std::array edges{
        synthetic_bsp::SyntheticBspEdge{0U, 0U},
        synthetic_bsp::SyntheticBspEdge{0U, 1U},
        synthetic_bsp::SyntheticBspEdge{1U, 2U},
        synthetic_bsp::SyntheticBspEdge{2U, 0U},
        synthetic_bsp::SyntheticBspEdge{0U, 2U},
        synthetic_bsp::SyntheticBspEdge{2U, 3U},
        synthetic_bsp::SyntheticBspEdge{3U, 0U},
    };
    constexpr std::array<std::int32_t, 6U> surfedges{1, 2, 3, 4, 5, 6};
    std::array faces{
        synthetic_bsp::SyntheticBspFace{},
        synthetic_bsp::SyntheticBspFace{},
    };
    faces[0].surfedge_count = 3;
    faces[1].first_surfedge = 3;
    faces[1].surfedge_count = 3;
    auto node = synthetic_bsp::SyntheticBspNode{};
    node.face_count = 2U;
    std::array leaves{
        synthetic_bsp::SyntheticBspLeaf{},
        synthetic_bsp::SyntheticBspLeaf{},
    };
    leaves[0].contents = -2;
    leaves[0].marksurface_count = 0U;
    leaves[1].marksurface_count = 2U;
    constexpr std::array<std::uint16_t, 2U> marksurfaces{0U, 1U};
    auto model = synthetic_bsp::SyntheticBspModel{};
    model.face_count = 2;

    builder.set_vertices(vertices)
        .set_edges(edges)
        .set_surfedges(surfedges)
        .set_faces(faces)
        .set_nodes(std::span{&node, 1U})
        .set_leaves(leaves)
        .set_marksurfaces(marksurfaces)
        .set_models(std::span{&model, 1U});
    return builder.build();
}

[[nodiscard]] std::vector<std::byte> production_bsp_fixture(
    const ProductionBspIntegrationScenario scenario)
{
    switch (scenario) {
    case ProductionBspIntegrationScenario::valid_triangle: {
        synthetic_bsp::SyntheticBspBuilder builder;
        constexpr auto vertices = synthetic_bsp::synthetic_triangle_vertices();
        builder.set_convex_polygon(vertices);
        return builder.build();
    }
    case ProductionBspIntegrationScenario::valid_multi_face:
        return make_two_face_production_bsp();
    case ProductionBspIntegrationScenario::missing_texture_metadata: {
        synthetic_bsp::SyntheticBspBuilder builder;
        const std::array<std::optional<synthetic_bsp::SyntheticBspMipTexture>, 1U>
            textures{std::nullopt};
        builder.set_texture_directory(textures);
        return builder.build();
    }
    case ProductionBspIntegrationScenario::malformed_header: {
        auto bytes = synthetic_bsp::literal_minimal_goldsrc_bsp_v30();
        bytes.resize(synthetic_bsp::kSyntheticBspHeaderSize - 1U);
        return bytes;
    }
    case ProductionBspIntegrationScenario::malformed_texinfo:
        return std::move(synthetic_bsp::SyntheticBspCorruptor{
                             synthetic_bsp::literal_minimal_goldsrc_bsp_v30()})
            .write_u32(synthetic_bsp::SyntheticBspLumpId::texinfo,
                       0U,
                       0x7fc0'0000U)
            .take();
    case ProductionBspIntegrationScenario::valid_quad:
    case ProductionBspIntegrationScenario::geometry_output_limit:
    case ProductionBspIntegrationScenario::stale_selected_locator:
    case ProductionBspIntegrationScenario::source_changed_during_read:
        return synthetic_bsp::literal_minimal_goldsrc_bsp_v30();
    }
    return synthetic_bsp::literal_minimal_goldsrc_bsp_v30();
}

[[nodiscard]] bsp::GoldSrcBspImportLimits production_bsp_limits(
    const ProductionBspIntegrationScenario scenario)
{
    bsp::GoldSrcBspImportLimits limits;
    if (scenario ==
        ProductionBspIntegrationScenario::geometry_output_limit) {
        limits.maximum_output_vertices = 3U;
    }
    return limits;
}

[[nodiscard]] bool expects_production_source_failure(
    const ProductionBspIntegrationScenario scenario) noexcept
{
    return scenario ==
               ProductionBspIntegrationScenario::stale_selected_locator ||
           scenario ==
               ProductionBspIntegrationScenario::source_changed_during_read;
}

[[nodiscard]] bool expects_production_import_failure(
    const ProductionBspIntegrationScenario scenario) noexcept
{
    return scenario == ProductionBspIntegrationScenario::malformed_header ||
           scenario == ProductionBspIntegrationScenario::malformed_texinfo ||
           scenario ==
               ProductionBspIntegrationScenario::geometry_output_limit;
}

void check_literal_bsp_world_from_production_flow(
    const assets::WorldAsset& world,
    const ProductionBspIntegrationScenario scenario =
        ProductionBspIntegrationScenario::valid_quad)
{
    CHECK(world.identity.source_name == "maps/test_map.bsp");
    CHECK(world.coordinate_space ==
          assets::WorldCoordinateSpace::source_native_goldsrc_z_up);
    CHECK(world.texture_coordinate_space ==
          assets::WorldTextureCoordinateSpace::texel_units);
    CHECK(world.source_profile ==
          assets::WorldGeometrySourceProfile::goldsrc_bsp_v30);

    std::vector<synthetic_bsp::SyntheticBspVector3> expected_positions;
    std::vector<std::uint32_t> expected_indices;
    std::vector<std::uint32_t> expected_surface_index_counts;
    if (scenario == ProductionBspIntegrationScenario::valid_triangle) {
        constexpr auto triangle = synthetic_bsp::synthetic_triangle_vertices();
        expected_positions.assign(triangle.begin(), triangle.end());
        expected_indices = {0U, 1U, 2U};
        expected_surface_index_counts = {3U};
    } else if (scenario ==
               ProductionBspIntegrationScenario::valid_multi_face) {
        constexpr auto quad = synthetic_bsp::synthetic_quad_vertices();
        expected_positions = {
            quad[0U], quad[1U], quad[2U],
            quad[0U], quad[2U], quad[3U],
        };
        expected_indices = {0U, 1U, 2U, 3U, 4U, 5U};
        expected_surface_index_counts = {3U, 3U};
    } else {
        constexpr auto quad = synthetic_bsp::synthetic_quad_vertices();
        expected_positions.assign(quad.begin(), quad.end());
        expected_indices = {0U, 1U, 2U, 0U, 2U, 3U};
        expected_surface_index_counts = {6U};
    }

    REQUIRE(world.vertices.size() == expected_positions.size());
    REQUIRE(world.indices.size() == expected_indices.size());
    REQUIRE(world.surfaces.size() == expected_surface_index_counts.size());
    REQUIRE(world.materials.size() == 1U);

    for (std::size_t index = 0U; index < expected_positions.size(); ++index) {
        const auto& vertex = world.vertices[index];
        CHECK(vertex.position.x == expected_positions[index].x);
        CHECK(vertex.position.y == expected_positions[index].y);
        CHECK(vertex.position.z == expected_positions[index].z);
        CHECK(vertex.normal.x == 0.0F);
        CHECK(vertex.normal.y == 0.0F);
        CHECK(vertex.normal.z == 1.0F);
        CHECK(vertex.texture_coordinate.x == vertex.position.x);
        CHECK(vertex.texture_coordinate.y == vertex.position.y);
    }
    CHECK(std::ranges::equal(world.indices, expected_indices));

    CHECK(world.bounds.minimum.x == 0.0F);
    CHECK(world.bounds.minimum.y == 0.0F);
    CHECK(world.bounds.minimum.z == 0.0F);
    CHECK(world.bounds.maximum.x == 64.0F);
    CHECK(world.bounds.maximum.y == 64.0F);
    CHECK(world.bounds.maximum.z == 0.0F);
    REQUIRE(world.source_model_bounds);
    CHECK(world.source_model_bounds->minimum.x == -1.0F);
    CHECK(world.source_model_bounds->minimum.y == -1.0F);
    CHECK(world.source_model_bounds->minimum.z == -1.0F);
    CHECK(world.source_model_bounds->maximum.x == 65.0F);
    CHECK(world.source_model_bounds->maximum.y == 65.0F);
    CHECK(world.source_model_bounds->maximum.z == 1.0F);

    std::uint32_t first_index = 0U;
    for (std::size_t index = 0U;
         index < expected_surface_index_counts.size();
         ++index) {
        const auto& surface = world.surfaces[index];
        CHECK(surface.first_index == first_index);
        CHECK(surface.index_count == expected_surface_index_counts[index]);
        CHECK(surface.material_index == 0U);
        CHECK(surface.source_surface_ordinal ==
              static_cast<std::uint32_t>(index));
        CHECK_FALSE(surface.lightmap_offset);
        CHECK(surface.light_styles ==
              std::array<std::uint8_t, 4U>{0xffU, 0xffU, 0xffU, 0xffU});
        CHECK_FALSE(surface.special_surface);
        first_index += expected_surface_index_counts[index];
    }

    const auto& material = world.materials.front();
    const bool missing_texture =
        scenario ==
        ProductionBspIntegrationScenario::missing_texture_metadata;
    if (missing_texture) {
        CHECK_FALSE(material.texture_name);
        CHECK_FALSE(material.width);
        CHECK_FALSE(material.height);
        CHECK(material.texture_storage == assets::WorldTextureStorage::missing);
    } else {
        CHECK(material.texture_name == "TEST_QUAD");
        CHECK(material.width == 64U);
        CHECK(material.height == 64U);
        CHECK(material.texture_storage ==
              assets::WorldTextureStorage::external_reference);
    }
    CHECK(material.source_texture_flags == 0);
    CHECK(material.source_texinfo_index == 0U);
    CHECK(material.compatibility_profile ==
          assets::WorldMaterialCompatibilityProfile::
              source_texture_reference_v1);
    CHECK(material.evidence_profile ==
          assets::WorldMaterialEvidenceProfile::validated_source_metadata);

    CHECK(world.statistics.source_version == 30);
    CHECK(world.statistics.source_model_count == 1U);
    CHECK(world.statistics.source_face_count ==
          expected_surface_index_counts.size());
    CHECK(world.statistics.world_model_source_face_count ==
          expected_surface_index_counts.size());
    CHECK(world.statistics.skipped_submodel_face_count == 0U);
    CHECK(world.statistics.emitted_surface_count ==
          expected_surface_index_counts.size());
    CHECK(world.statistics.emitted_vertex_count == expected_positions.size());
    CHECK(world.statistics.emitted_triangle_count ==
          expected_indices.size() / 3U);
    CHECK(world.statistics.material_count == 1U);
    CHECK(world.statistics.missing_texture_reference_count ==
          (missing_texture ? 1U : 0U));
    CHECK(world.statistics.external_texture_reference_count ==
          (missing_texture ? 0U : 1U));
    CHECK(world.statistics.embedded_texture_reference_count == 0U);
}

void run_precache_manifest_integration(
    const std::size_t run,
    const PrecacheManifestIntegrationScenario scenario,
    const PrecacheManifestTransportScenario transport_scenario =
        PrecacheManifestTransportScenario::baseline,
    const bool verify_stale_locator = false,
    const PrecacheManifestCompletionMode completion_mode =
        PrecacheManifestCompletionMode::manifest_only,
    const std::size_t source_read_chunk_bytes = 7U,
    const ProductionBspIntegrationScenario production_bsp_scenario =
        ProductionBspIntegrationScenario::valid_quad)
{
    INFO("fake-HLDS precache-manifest run " << run + 1U);
    const bool production_bsp_dispatch =
        completion_mode != PrecacheManifestCompletionMode::manifest_only;
    const bool legacy_malformed_face_rejection =
        completion_mode ==
        PrecacheManifestCompletionMode::production_bsp_malformed_rejection;
    const bool expect_import_failure =
        legacy_malformed_face_rejection ||
        expects_production_import_failure(production_bsp_scenario);
    const bool expect_source_failure =
        expects_production_source_failure(production_bsp_scenario);
    REQUIRE_FALSE((production_bsp_dispatch &&
                   scenario != PrecacheManifestIntegrationScenario::complete &&
                   scenario != PrecacheManifestIntegrationScenario::
                                   world_ready_missing_sound));
    REQUIRE_FALSE((legacy_malformed_face_rejection &&
                   scenario != PrecacheManifestIntegrationScenario::complete));
    REQUIRE_FALSE((production_bsp_dispatch && verify_stale_locator));
    REQUIRE_FALSE((production_bsp_dispatch && source_read_chunk_bytes == 0U));
    REQUIRE_FALSE((!production_bsp_dispatch &&
                   production_bsp_scenario !=
                       ProductionBspIntegrationScenario::valid_quad));
    REQUIRE_FALSE((legacy_malformed_face_rejection &&
                   production_bsp_scenario !=
                       ProductionBspIntegrationScenario::valid_quad));

    // Alternate exact game-root selection and valve fallback while retaining
    // one validated environment for both consistency preparation and the
    // post-response inventory/manifest publication.
    hlclient::tests::ScopedLocalResourceTestRoot root;
    constexpr std::string_view game = "manifestmod";
    root.create_game(game);
    const bool use_game_root = (run % 2U) == 0U;
    const std::string_view selected_game = use_game_root ? game : "valve";
    const std::uint32_t expected_root_id = use_game_root ? 0U : 1U;
    write_manifest_integration_files(root, selected_game, scenario);
    std::vector<std::byte> production_world_bytes;
    if (production_bsp_dispatch) {
        production_world_bytes = production_bsp_fixture(
            production_bsp_scenario);
        if (legacy_malformed_face_rejection) {
            const auto face_descriptor =
                hlclient::tests::synthetic_lump_descriptor_offset(
                    hlclient::tests::SyntheticBspLumpId::faces);
            const auto face_offset = static_cast<std::size_t>(
                hlclient::tests::synthetic_read_i32le(
                    production_world_bytes,
                    face_descriptor));
            hlclient::tests::synthetic_write_i16le(
                std::span<std::byte>{production_world_bytes},
                face_offset + 8U,
                2);
        }
        root.write(
            selected_game,
            "maps/test_map.bsp",
            production_world_bytes);
    }
    const auto before = snapshot_synthetic_root(root.path());

    auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(
        root.path(),
        game);
    INFO((roots.error ? roots.error->context : std::string{}));
    REQUIRE(roots);
    REQUIRE(roots.roots);
    auto created_environment =
        hlclient::local_resources::LocalResourceEnvironment::create(
            std::move(*roots.roots));
    INFO((created_environment.error
              ? created_environment.error->context
              : std::string{}));
    REQUIRE(created_environment);
    REQUIRE(created_environment.environment);
    std::shared_ptr<hlclient::local_resources::LocalResourceEnvironment>
        mutable_environment{std::move(created_environment.environment)};
    std::shared_ptr<const hlclient::local_resources::LocalResourceEnvironment>
        environment = mutable_environment;
    REQUIRE(environment);
    CHECK(environment->root_count() == 2U);

    auto prepared_provider =
        consistency::PreparedLocalResourceConsistencyProvider::prepare(
            *environment);
    INFO((prepared_provider.error
              ? prepared_provider.error->context
              : std::string{}));
    REQUIRE(prepared_provider);
    REQUIRE(prepared_provider.provider);
    CHECK(prepared_provider.provider->validated_root_count() == 2U);
    CHECK(prepared_provider.provider->selected_root_id().value() ==
          expected_root_id);
    CHECK_FALSE(prepared_provider.provider->consumed());
    if (production_bsp_dispatch) {
        CHECK(prepared_provider.provider->byte_count() == 3U);
        CHECK(prepared_provider.provider->opaque_byte_count() == 16U);
        CHECK_FALSE(can_open_synthetic_writer(
            root.game_path(selected_game) / "tempdecal.wad"));
    }
    CHECK(snapshot_synthetic_root(root.path()) == before);

    network::NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());
    std::string error;
    auto server = network::UdpSocket::open_ipv4(runtime, error);
    INFO(error);
    REQUIRE(server);
    REQUIRE(server->bind(network::NetworkAddress::loopback(0U), error));
    const auto server_endpoint = server->local_address(error);
    REQUIRE(server_endpoint);
    auto client = network::UdpSocket::open_ipv4(runtime, error);
    REQUIRE(client);
    REQUIRE(client->bind(network::NetworkAddress::loopback(0U), error));
    network::UdpDatagramTransport transport{std::move(*client)};

    std::size_t authentication_releases = 0U;
    PrecacheManifestIntegrationTraceCounts traces;
    auto prepared_request =
        prepared_request_with_session(authentication_releases);
    auto response_stage_config = resource_client_response_config();
    goldsrc::PrecacheManifestStageConfig manifest_stage_config;
    manifest_stage_config.response = response_stage_config;
    assets::AssetImporterRegistries registries;
    if (production_bsp_dispatch) {
        REQUIRE(bsp::register_builtin_asset_importers(
            registries,
            production_bsp_limits(production_bsp_scenario)));
        CHECK(registries.worlds.size() == 1U);
        CHECK(registries.models.size() == 0U);
        CHECK(registries.sprites.size() == 0U);
        CHECK(registries.images.size() == 0U);
        CHECK(registries.audio.size() == 0U);
    }
    goldsrc::PrecacheAssetDispatchStageConfig asset_dispatch_config;
    asset_dispatch_config.source_open.read_chunk_bytes =
        source_read_chunk_bytes;
    asset_dispatch_config.source_open.maximum_chunks_per_update = 1U;
    const auto expected_remote = *server_endpoint;

    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport,
        *server_endpoint,
        production_bsp_dispatch
            ? goldsrc::HandshakeStopPoint::asset_dispatch
            : goldsrc::HandshakeStopPoint::precache_manifest,
        std::move(prepared_request.request),
        challenge_config(),
        {},
        {},
        response_config(),
        {},
        std::move(prepared_request.session),
        {},
        {},
        {},
        [&traces, expected_remote](
            const goldsrc::InitialSignonTraceEvent& event) {
            if (event.endpoint != expected_remote) {
                ++traces.endpoint_mismatches;
            }
            if (event.classification ==
                goldsrc::InitialSignonTraceClassification::
                    initial_request_queued) {
                ++traces.initial_requests_queued;
            }
        },
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        transition_config(),
        [&traces, expected_remote](
            const goldsrc::ResourceTransitionTraceEvent& event) {
            if (event.endpoint != expected_remote) {
                ++traces.endpoint_mismatches;
            }
            if (event.classification ==
                goldsrc::ResourceTransitionTraceClassification::
                    transition_request_queued) {
                ++traces.transition_requests_queued;
            }
        },
        response_stage_config.resource_list,
        [&traces, expected_remote](
            const goldsrc::ResourceListTraceEvent& event) {
            if (event.endpoint != expected_remote) {
                ++traces.endpoint_mismatches;
            }
            if (event.classification ==
                goldsrc::ResourceListTraceClassification::
                    resource_list_decoded) {
                ++traces.resource_lists_decoded;
            }
        },
        response_stage_config,
        prepared_provider.provider.get(),
        [&traces, expected_remote](
            const goldsrc::ResourceClientResponseTraceEvent& event) {
            if (event.endpoint != expected_remote) {
                ++traces.endpoint_mismatches;
            }
            if (event.classification ==
                goldsrc::ResourceClientResponseTraceClassification::
                    resource_response_queued) {
                ++traces.resource_responses_queued;
            }
            if (event.classification ==
                goldsrc::ResourceClientResponseTraceClassification::
                    next_server_boundary_reached) {
                ++traces.response_boundaries;
            }
        },
        environment,
        manifest_stage_config,
        [&traces, &authentication_releases, expected_remote](
            const goldsrc::PrecacheManifestTraceEvent& event) {
            if (event.endpoint != expected_remote) {
                ++traces.endpoint_mismatches;
            }
            if (event.classification ==
                goldsrc::PrecacheManifestTraceClassification::
                    resource_response_boundary_reached) {
                traces.transmitted_at_response_boundary =
                    event.transmitted_packet_count;
            }
            using Classification =
                goldsrc::PrecacheManifestTraceClassification;
            if (event.classification ==
                    Classification::precache_manifest_ready ||
                event.classification ==
                    Classification::local_resources_incomplete ||
                event.classification ==
                    Classification::unsafe_local_resources ||
                event.classification ==
                    Classification::unsupported_local_profile ||
                event.classification ==
                    Classification::local_resource_io_error) {
                ++traces.manifest_publications;
                traces.transmitted_at_manifest_publication =
                    event.transmitted_packet_count;
            }
            if (event.classification ==
                    Classification::precache_manifest_ready ||
                event.classification ==
                    Classification::local_resources_incomplete ||
                event.classification ==
                    Classification::unsafe_local_resources ||
                event.classification ==
                    Classification::unsupported_local_profile ||
                event.classification ==
                    Classification::local_resource_io_error ||
                event.classification == Classification::protocol_failure) {
                ++traces.manifest_terminal_outcomes;
                traces.transmitted_at_manifest_terminal =
                    event.transmitted_packet_count;
                traces.authentication_releases_at_manifest_terminal =
                    authentication_releases;
            }
        },
        production_bsp_dispatch ? &registries : nullptr,
        asset_dispatch_config,
        [&traces, expected_remote](
            const goldsrc::PrecacheAssetDispatchTraceEvent& event) {
            if (event.endpoint != expected_remote) {
                ++traces.endpoint_mismatches;
            }
            using Classification =
                goldsrc::PrecacheAssetDispatchTraceClassification;
            switch (event.classification) {
            case Classification::world_entry_selected:
                ++traces.world_entries_selected;
                break;
            case Classification::asset_source_open_started:
                ++traces.asset_source_open_starts;
                break;
            case Classification::asset_source_progress:
                ++traces.asset_source_progress_events;
                break;
            case Classification::source_open_failed:
                ++traces.asset_source_open_failures;
                traces.transmitted_at_asset_terminal =
                    event.transmitted_packet_count;
                break;
            case Classification::importer_probe_completed:
                ++traces.importer_probes_completed;
                break;
            case Classification::importer_selected:
                ++traces.importers_selected;
                break;
            case Classification::asset_imported:
                ++traces.assets_imported;
                traces.transmitted_at_asset_terminal =
                    event.transmitted_packet_count;
                break;
            case Classification::import_failed:
                ++traces.asset_import_failures;
                traces.transmitted_at_asset_terminal =
                    event.transmitted_packet_count;
                break;
            default:
                break;
            }
        }};

    const auto epoch = goldsrc::ChallengeExchangeTimePoint{} +
        std::chrono::milliseconds{90'000 + static_cast<std::int64_t>(run)};
    const auto started = reach_first_service_request(
        *server, handshake, epoch, error);
    REQUIRE(handshake.local_endpoint());
    CHECK(*handshake.local_endpoint() == started.client_endpoint);
    CHECK(handshake.connect_send_attempts() == 1U);
    CHECK_FALSE(handshake.terminal());
    require_no_datagram(*server);

    const auto first_batch = send_first_service_batch(
        *server,
        handshake,
        started,
        epoch + 4ms,
        manifest_first_semantic_payload(),
        error);
    CHECK_FALSE(handshake.terminal());

    handshake.update(first_batch.next_update);
    const auto transition = decode_client_packet(
        receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
        started.client_endpoint);
    REQUIRE(is_exact_transition_request(transition));
    REQUIRE(transition.header.sequence.flags.reliable);
    auto last_client_sequence =
        transition.header.sequence.sequence.value();
    auto server_sequence = first_batch.next_server_sequence;
    require_no_datagram(*server);

    send_server_datagram(
        *server,
        started.client_endpoint,
        server_packet(
            server_sequence++,
            false,
            last_client_sequence,
            false,
            {}),
        error);
    handshake.update(first_batch.next_update + 1ms);
    CHECK_FALSE(handshake.terminal());

    const auto semantic = manifest_resource_list_payload(scenario);
    const auto envelope = service_envelope(semantic);
    const auto fragment_count_size =
        (envelope.size() + goldsrc::kStockProtocol48NormalFragmentChunkSize -
         1U) /
        goldsrc::kStockProtocol48NormalFragmentChunkSize;
    REQUIRE(fragment_count_size > 0U);
    REQUIRE(fragment_count_size <=
            (std::numeric_limits<std::uint16_t>::max)());
    const auto fragment_count =
        static_cast<std::uint16_t>(fragment_count_size);

    auto now = first_batch.next_update + 2ms;
    for (std::uint16_t index = 1U; index <= fragment_count; ++index) {
        const auto offset = static_cast<std::size_t>(index - 1U) *
            goldsrc::kStockProtocol48NormalFragmentChunkSize;
        const auto length = (std::min)(
            goldsrc::kStockProtocol48NormalFragmentChunkSize,
            envelope.size() - offset);
        send_server_datagram(
            *server,
            started.client_endpoint,
            server_fragment(
                server_sequence++,
                last_client_sequence,
                false,
                index,
                fragment_count,
                std::span<const std::byte>{envelope}.subspan(offset, length)),
            error);
        handshake.update(now);
        const auto acknowledgement = decode_client_packet(
            receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        check_transport_only_packet(acknowledgement);
        last_client_sequence =
            acknowledgement.header.sequence.sequence.value();
        now += 1ms;
    }

    handshake.update(now);
    auto response = decode_client_packet(
        receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
        started.client_endpoint);
    check_exact_resource_response_packet(
        response,
        kExactLocalProviderResponse);
    CHECK_FALSE(handshake.terminal());
    std::size_t response_datagrams = 1U;

    if (transport_scenario ==
        PrecacheManifestTransportScenario::dropped_acknowledgement) {
        // Withhold the covering ACK for one bounded update. The semantic unit
        // must remain queued exactly once and no premature retry is emitted.
        handshake.update(now + 1ms);
        require_no_datagram(*server);
        now += 1ms;
    }

    if (transport_scenario ==
            PrecacheManifestTransportScenario::dropped_response ||
        transport_scenario ==
            PrecacheManifestTransportScenario::dropped_acknowledgement) {
        // Create the same reliable-generation gap used by the historical
        // response integration. It requests a transport retry without a
        // second semantic queue.
        send_server_datagram(
            *server,
            started.client_endpoint,
            server_packet(
                server_sequence++,
                true,
                response.header.sequence.sequence.value(),
                false,
                {}),
            error);
        handshake.update(now + 1ms);
        const auto gap_packet = decode_client_packet(
            receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        check_transport_only_packet(gap_packet);

        send_server_datagram(
            *server,
            started.client_endpoint,
            server_packet(
                server_sequence++,
                false,
                gap_packet.header.sequence.sequence.value(),
                false,
                {}),
            error);
        handshake.update(now + 2ms);
        auto retry = decode_client_packet(
            receive_bounded(*server, goldsrc::kMaximumNetchanDatagramSize),
            started.client_endpoint);
        check_exact_resource_response_packet(
            retry,
            kExactLocalProviderResponse);
        REQUIRE(response.fragments[0U]);
        REQUIRE(retry.fragments[0U]);
        CHECK(retry.fragments[0U]->packed_id() ==
              response.fragments[0U]->packed_id());
        CHECK(retry.payload == response.payload);
        CHECK(retry.header.sequence.sequence !=
              response.header.sequence.sequence);
        response = std::move(retry);
        ++response_datagrams;
        now += 2ms;
    }

    const auto next_payload = post_response_semantic_payload();
    send_server_datagram(
        *server,
        started.client_endpoint,
        server_packet(
            server_sequence++,
            false,
            response.header.sequence.sequence.value(),
            true,
            service_envelope(next_payload)),
        error);
    handshake.update(now + 1ms);
    CHECK_FALSE(handshake.terminal());
    if (production_bsp_dispatch) {
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::waiting_for_asset_dispatch);
    } else {
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::waiting_for_precache_manifest);
    }
    require_no_datagram(*server);

    const auto production_map_path =
        root.game_path(selected_game) / "maps/test_map.bsp";
    const auto stale_original_path =
        root.game_path(selected_game) / "maps/test_map.stale-original.bsp";
    bool production_mutation_applied = false;
    auto terminal_time = now + 3ms;
    if (production_bsp_dispatch) {
        // All work after the covering ACK is bounded local metadata/source
        // processing on the same retained coordinator and transport.
        auto local_update_time = now + 2ms;
        for (std::size_t update = 0U;
             update < 256U && !handshake.terminal();
             ++update) {
            if (production_bsp_scenario ==
                    ProductionBspIntegrationScenario::stale_selected_locator &&
                !production_mutation_applied &&
                traces.world_entries_selected == 1U &&
                traces.asset_source_open_starts == 0U) {
                std::filesystem::rename(
                    production_map_path,
                    stale_original_path);
                root.write(
                    selected_game,
                    "maps/test_map.bsp",
                    "stale replacement");
                production_mutation_applied = true;
            }

            handshake.update(local_update_time);
            require_no_datagram(*server);

            if (production_bsp_scenario ==
                    ProductionBspIntegrationScenario::
                        source_changed_during_read &&
                !production_mutation_applied &&
                traces.asset_source_progress_events > 0U) {
                auto* stage = hlclient::goldsrc::detail::
                    GoldSrcHandshakeCoordinatorTestAccess::
                        asset_dispatch_stage(handshake);
                REQUIRE(stage != nullptr);
                auto* operation = hlclient::goldsrc::detail::
                    PrecacheAssetDispatchStageTestAccess::
                        source_open_operation(*stage);
                REQUIRE(operation != nullptr);
                REQUIRE(operation->progress_bytes() > 0U);
                hlclient::local_assets::detail::
                    LocalAssetSourceOpenOperationTestAccess::
                        simulate_final_change_metadata(*operation);
                production_mutation_applied = true;
            }

            terminal_time = local_update_time;
            local_update_time += 1ms;
        }
        REQUIRE(handshake.terminal());
        if (expects_production_source_failure(production_bsp_scenario)) {
            REQUIRE(production_mutation_applied);
        }
        if (production_bsp_scenario ==
            ProductionBspIntegrationScenario::stale_selected_locator) {
            REQUIRE(std::filesystem::remove(production_map_path));
            std::filesystem::rename(
                stale_original_path,
                production_map_path);
            CHECK_FALSE(std::filesystem::exists(stale_original_path));
        }
    } else {
        // These two deterministic metadata-only updates build the inventory
        // and manifest without driving the retained network session.
        handshake.update(now + 2ms);
        CHECK_FALSE(handshake.terminal());
        require_no_datagram(*server);
        handshake.update(terminal_time);
        REQUIRE(handshake.terminal());
        require_no_datagram(*server);
    }

    if (production_bsp_dispatch) {
        const auto expected_terminal_state =
            expect_source_failure
                ? goldsrc::GoldSrcHandshakeState::asset_source_open_failed
                : (expect_import_failure
                       ? goldsrc::GoldSrcHandshakeState::asset_import_failed
                       : goldsrc::GoldSrcHandshakeState::asset_imported);
        const bool expect_import_success =
            !expect_source_failure && !expect_import_failure;
        const auto expected_progress_events =
            production_bsp_scenario ==
                    ProductionBspIntegrationScenario::stale_selected_locator
                ? 0U
                : (production_world_bytes.size() + source_read_chunk_bytes -
                   1U) /
                      source_read_chunk_bytes;
        CHECK(handshake.state() == expected_terminal_state);

        auto* asset_dispatch_stage = hlclient::goldsrc::detail::
            GoldSrcHandshakeCoordinatorTestAccess::asset_dispatch_stage(
                handshake);
        REQUIRE(asset_dispatch_stage != nullptr);
        CHECK(asset_dispatch_stage->manifest_publication_count() == 1U);
        CHECK(asset_dispatch_stage->source_open_attempt_count() == 1U);
        CHECK(asset_dispatch_stage->importer_dispatch_count() ==
              (expect_source_failure ? 0U : 1U));

        if (expect_source_failure) {
            CHECK_FALSE(handshake.asset_dispatch_result());
            REQUIRE(handshake.asset_dispatch_error());
            const auto& asset_error = *handshake.asset_dispatch_error();
            CHECK(asset_error.code ==
                  goldsrc::PrecacheAssetDispatchStageErrorCode::
                      source_open_failed);
            if (production_bsp_scenario ==
                ProductionBspIntegrationScenario::stale_selected_locator) {
                CHECK(asset_error.source_open_code ==
                      goldsrc::ApprovedAssetSourceOpenErrorCode::stale_locator);
                CHECK(asset_error.local_source_open_code ==
                      hlclient::local_assets::
                          LocalAssetSourceOpenErrorCode::stale_locator);
                CHECK(asset_error.locator_reopen_code ==
                      hlclient::local_resources::
                          LocalResourceLocatorReopenErrorCode::stale_locator);
            } else {
                CHECK(asset_error.source_open_code ==
                      goldsrc::ApprovedAssetSourceOpenErrorCode::
                          source_changed_during_read);
                CHECK(asset_error.local_source_open_code ==
                      hlclient::local_assets::
                          LocalAssetSourceOpenErrorCode::
                              source_changed_during_read);
            }
        } else if (expect_import_failure) {
            CHECK_FALSE(handshake.asset_dispatch_result());
            REQUIRE(handshake.asset_dispatch_error());
            const auto& asset_error = *handshake.asset_dispatch_error();
            CHECK(asset_error.code ==
                  goldsrc::PrecacheAssetDispatchStageErrorCode::import_failed);
            CHECK(asset_error.dispatch_state ==
                  assets::AssetDispatchState::import_failed);
            CHECK(asset_error.asset_code ==
                  assets::AssetErrorCode::MalformedData);
        } else {
            CHECK_FALSE(handshake.asset_dispatch_error());
            REQUIRE(handshake.asset_dispatch_result());
            const auto& result = *handshake.asset_dispatch_result();
            CHECK(result.environment().get() == environment.get());
            CHECK(result.source_byte_count() == production_world_bytes.size());

            const auto& manifest = result.manifest();
            CHECK(manifest.entry_count() == 6U);
            const bool incomplete =
                scenario == PrecacheManifestIntegrationScenario::
                                world_ready_missing_sound;
            if (incomplete) {
                CHECK(manifest.completeness() ==
                      goldsrc::PrecacheManifestCompleteness::
                          world_ready_but_incomplete);
                CHECK_FALSE(manifest.complete_for_supported_local_profile());
                CHECK(manifest.readiness_summary().resolved_mapped_file_count() ==
                      4U);
                CHECK(manifest.readiness_summary().missing_count() == 1U);
            } else {
                CHECK(manifest.completeness() ==
                      goldsrc::PrecacheManifestCompleteness::
                          complete_for_supported_local_profile);
                CHECK(manifest.complete_for_supported_local_profile());
                CHECK(manifest.readiness_summary().resolved_mapped_file_count() ==
                      5U);
                CHECK(manifest.readiness_summary().missing_count() == 0U);
            }
            CHECK(manifest.readiness_summary().metadata_only_count() == 1U);
            CHECK(manifest.world_geometry_ready());
            CHECK(manifest.world_selection().wire_ordinal() == 5U);
            CHECK(manifest.world_selection().entry_offset() == 5U);
            CHECK(manifest.world_selection().resource_index() == 137U);
            CHECK(manifest.world_selection().status() ==
                  goldsrc::WorldResourceReadiness::ready);
            REQUIRE(manifest.world_selection().locator());
            CHECK(manifest.world_selection().locator()->root_id().value() ==
                  expected_root_id);

            const auto& dispatch = result.dispatch_result();
            CHECK(dispatch.state == assets::AssetDispatchState::imported);
            CHECK(dispatch.selected_category ==
                  assets::AssetImporterCategory::world);
            CHECK(dispatch.selected_importer_id == "world:goldsrc-bsp-v30");
            REQUIRE(dispatch.top_candidates.size() == 1U);
            CHECK(dispatch.top_candidates.front().importer_id ==
                  "world:goldsrc-bsp-v30");
            REQUIRE(result.imported_asset());
            const auto* world =
                std::get_if<assets::WorldAsset>(&*result.imported_asset());
            REQUIRE(world);
            check_literal_bsp_world_from_production_flow(
                *world,
                production_bsp_scenario);
        }

        CHECK(traces.initial_requests_queued == 1U);
        CHECK(traces.transition_requests_queued == 1U);
        CHECK(traces.resource_lists_decoded == 1U);
        CHECK(traces.resource_responses_queued == 1U);
        CHECK(traces.response_boundaries == 1U);
        CHECK(traces.manifest_publications == 1U);
        CHECK(traces.manifest_terminal_outcomes == 1U);
        CHECK(traces.world_entries_selected == 1U);
        CHECK(traces.asset_source_open_starts == 1U);
        CHECK(traces.asset_source_progress_events == expected_progress_events);
        CHECK(traces.asset_source_open_failures ==
              (expect_source_failure ? 1U : 0U));
        CHECK(traces.importer_probes_completed ==
              (expect_source_failure ? 0U : 1U));
        CHECK(traces.importers_selected ==
              (expect_source_failure ? 0U : 1U));
        CHECK(traces.assets_imported == (expect_import_success ? 1U : 0U));
        CHECK(traces.asset_import_failures ==
              (expect_import_failure ? 1U : 0U));
        CHECK(traces.endpoint_mismatches == 0U);
        CHECK(response_datagrams ==
              (transport_scenario == PrecacheManifestTransportScenario::baseline
                   ? 1U
                   : 2U));
        REQUIRE(traces.transmitted_at_response_boundary);
        REQUIRE(traces.transmitted_at_manifest_publication);
        REQUIRE(traces.transmitted_at_manifest_terminal);
        REQUIRE(traces.transmitted_at_asset_terminal);
        REQUIRE(traces.authentication_releases_at_manifest_terminal);
        CHECK(*traces.transmitted_at_manifest_publication ==
              *traces.transmitted_at_response_boundary);
        CHECK(*traces.transmitted_at_manifest_terminal ==
              *traces.transmitted_at_response_boundary);
        CHECK(*traces.transmitted_at_asset_terminal ==
              *traces.transmitted_at_manifest_publication);
        CHECK(*traces.authentication_releases_at_manifest_terminal == 0U);
        CHECK(prepared_provider.provider->consumed());
        CHECK(authentication_releases == 1U);
        CHECK(snapshot_synthetic_root(root.path()) == before);
        CHECK(can_open_synthetic_writer(
            root.game_path(selected_game) / "tempdecal.wad"));

        handshake.update(terminal_time + 100ms);
        handshake.cancel(terminal_time + 200ms);
        CHECK(handshake.state() == expected_terminal_state);
        CHECK(authentication_releases == 1U);
        CHECK(traces.world_entries_selected == 1U);
        CHECK(traces.asset_source_open_starts == 1U);
        CHECK(traces.asset_source_progress_events == expected_progress_events);
        CHECK(traces.asset_source_open_failures ==
              (expect_source_failure ? 1U : 0U));
        CHECK(traces.importer_probes_completed ==
              (expect_source_failure ? 0U : 1U));
        CHECK(traces.importers_selected ==
              (expect_source_failure ? 0U : 1U));
        CHECK(traces.assets_imported == (expect_import_success ? 1U : 0U));
        CHECK(traces.asset_import_failures ==
              (expect_import_failure ? 1U : 0U));
        CHECK(asset_dispatch_stage->manifest_publication_count() == 1U);
        CHECK(asset_dispatch_stage->source_open_attempt_count() == 1U);
        CHECK(asset_dispatch_stage->importer_dispatch_count() ==
              (expect_source_failure ? 0U : 1U));
        require_no_datagram(*server);
        CHECK(snapshot_synthetic_root(root.path()) == before);
        return;
    }

    if (scenario ==
        PrecacheManifestIntegrationScenario::duplicate_map_match) {
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::protocol_error);
        CHECK_FALSE(handshake.precache_manifest_result());
        REQUIRE(handshake.precache_manifest_error());
        CHECK(handshake.precache_manifest_error()->code ==
              goldsrc::PrecacheManifestStageErrorCode::manifest_build_failed);
        CHECK(handshake.precache_manifest_error()->manifest_code ==
              goldsrc::PrecacheManifestErrorCode::readiness_build_failed);
        CHECK(traces.initial_requests_queued == 1U);
        CHECK(traces.transition_requests_queued == 1U);
        CHECK(traces.resource_lists_decoded == 1U);
        CHECK(traces.resource_responses_queued == 1U);
        CHECK(traces.response_boundaries == 1U);
        CHECK(traces.manifest_publications == 0U);
        CHECK(traces.manifest_terminal_outcomes == 1U);
        CHECK(traces.endpoint_mismatches == 0U);
        REQUIRE(traces.transmitted_at_response_boundary);
        REQUIRE(traces.transmitted_at_manifest_terminal);
        REQUIRE(traces.authentication_releases_at_manifest_terminal);
        CHECK(*traces.transmitted_at_manifest_terminal ==
              *traces.transmitted_at_response_boundary);
        CHECK(*traces.authentication_releases_at_manifest_terminal == 1U);
        CHECK(prepared_provider.provider->consumed());
        CHECK(authentication_releases == 1U);
        CHECK(snapshot_synthetic_root(root.path()) == before);
        CHECK(can_open_synthetic_writer(
            root.game_path(selected_game) / "tempdecal.wad"));
        handshake.update(now + 100ms);
        handshake.cancel(now + 200ms);
        CHECK(authentication_releases == 1U);
        require_no_datagram(*server);
        return;
    }

    REQUIRE_FALSE(handshake.precache_manifest_error());
    REQUIRE(handshake.precache_manifest_result());
    const auto& publication = *handshake.precache_manifest_result();
    const auto& manifest = publication.manifest();
    const auto expected_entries = manifest_resource_entries(scenario);
    const auto expected_map_index = static_cast<std::uint16_t>(
        scenario == PrecacheManifestIntegrationScenario::sparse_slots
            ? 4'095U
            : 137U);
    CHECK(publication.environment().get() == environment.get());
    CHECK(publication.inventory().entry_count() == expected_entries.size());
    CHECK(manifest.entry_count() == expected_entries.size());
    CHECK(manifest.world_selection().wire_ordinal() == 5U);
    CHECK(manifest.world_selection().entry_offset() == 5U);
    CHECK(manifest.world_selection().resource_index() == expected_map_index);
    REQUIRE(manifest.world_entry());
    CHECK(manifest.world_entry() ==
          manifest.find(goldsrc::ResourceType::model, expected_map_index));
    CHECK(manifest.model_slots().entry_offset(expected_map_index) == 5U);
    const auto& response_result = publication.response();
    CHECK(response_result.response().wire_name() == "tempdecal.wad");
    CHECK(response_result.response().byte_count() == 3U);
    CHECK(response_result.reliable_lifecycle().fragmented());
    CHECK(response_result.reliable_lifecycle().fragment_count() == 1U);
    CHECK(response_result.reliable_lifecycle().transmit_count() ==
          response_datagrams);
    CHECK(response_result.reliable_lifecycle().acknowledgement().sequence ==
          response.header.sequence.sequence);
    CHECK(response_result.reliable_lifecycle().acknowledgement().reliable);

    const bool missing_map =
        scenario == PrecacheManifestIntegrationScenario::local_map_missing;
    CHECK(manifest.world_selection().status() ==
          (missing_map
               ? goldsrc::WorldResourceReadiness::local_map_missing
               : goldsrc::WorldResourceReadiness::ready));
    CHECK(manifest.world_geometry_ready() == !missing_map);
    if (missing_map) {
        CHECK_FALSE(manifest.world_selection().locator());
    } else {
        REQUIRE(manifest.world_selection().locator());
        CHECK(manifest.world_selection().locator()->root_id().value() ==
              expected_root_id);
    }

    const auto& summary = manifest.readiness_summary();
    CHECK(summary.total_entry_count() == expected_entries.size());
    CHECK(summary.metadata_only_count() == 1U);
    switch (scenario) {
    case PrecacheManifestIntegrationScenario::complete:
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::precache_manifest_ready);
        CHECK(manifest.completeness() ==
              goldsrc::PrecacheManifestCompleteness::
                  complete_for_supported_local_profile);
        CHECK(manifest.complete_for_supported_local_profile());
        CHECK(summary.resolved_mapped_file_count() == 5U);
        CHECK(summary.missing_count() == 0U);
        break;
    case PrecacheManifestIntegrationScenario::world_ready_missing_sound:
    case PrecacheManifestIntegrationScenario::missing_model:
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::local_resources_incomplete);
        CHECK(manifest.completeness() ==
              goldsrc::PrecacheManifestCompleteness::
                  world_ready_but_incomplete);
        CHECK_FALSE(manifest.complete_for_supported_local_profile());
        CHECK(summary.resolved_mapped_file_count() == 4U);
        CHECK(summary.missing_count() == 1U);
        break;
    case PrecacheManifestIntegrationScenario::local_map_missing:
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::local_resources_incomplete);
        CHECK(manifest.completeness() ==
              goldsrc::PrecacheManifestCompleteness::
                  incomplete_missing_resources);
        CHECK_FALSE(manifest.complete_for_supported_local_profile());
        CHECK(summary.resolved_mapped_file_count() == 4U);
        CHECK(summary.missing_count() == 1U);
        break;
    case PrecacheManifestIntegrationScenario::sparse_slots:
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::precache_manifest_ready);
        CHECK(manifest.completeness() ==
              goldsrc::PrecacheManifestCompleteness::
                  complete_for_supported_local_profile);
        CHECK(manifest.sound_slots().slot_count() == 4'096U);
        CHECK(manifest.model_slots().slot_count() == 4'096U);
        CHECK(manifest.generic_slots().slot_count() == 4'096U);
        CHECK(manifest.event_script_slots().slot_count() == 4'096U);
        CHECK(manifest.decal_slots().slot_count() == 4'096U);
        CHECK_FALSE(manifest.sound_slots().entry_offset(4'094U));
        CHECK(manifest.sound_slots().entry_offset(4'095U) == 1U);
        CHECK(manifest.generic_slots().entry_offset(4'095U) == 0U);
        CHECK(manifest.event_script_slots().entry_offset(4'095U) == 3U);
        CHECK(manifest.decal_slots().entry_offset(4'095U) == 4U);
        break;
    case PrecacheManifestIntegrationScenario::malicious_name:
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::unsafe_local_resources);
        CHECK(manifest.completeness() ==
              goldsrc::PrecacheManifestCompleteness::
                  blocked_unsafe_resources);
        CHECK(summary.security_blocked_count() == 1U);
        REQUIRE(manifest.find(goldsrc::ResourceType::generic, 73U));
        CHECK(manifest.find(goldsrc::ResourceType::generic, 73U)
                  ->readiness_status() ==
              goldsrc::LocalResourceReadinessStatus::unsafe_name);
        break;
    case PrecacheManifestIntegrationScenario::unsupported_non_ascii:
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::unsupported_local_profile);
        CHECK(manifest.completeness() ==
              goldsrc::PrecacheManifestCompleteness::unsupported_profile);
        CHECK(summary.unsupported_count() == 1U);
        break;
    case PrecacheManifestIntegrationScenario::ambiguous_sound:
        CHECK(handshake.state() ==
              goldsrc::GoldSrcHandshakeState::local_resource_io_error);
        CHECK(manifest.completeness() ==
              goldsrc::PrecacheManifestCompleteness::local_io_failure);
        CHECK(summary.ambiguous_count() == 1U);
        break;
    case PrecacheManifestIntegrationScenario::duplicate_map_match:
        FAIL("Duplicate map match must have returned through the transactional failure branch");
    }

    CHECK(traces.initial_requests_queued == 1U);
    CHECK(traces.transition_requests_queued == 1U);
    CHECK(traces.resource_lists_decoded == 1U);
    CHECK(traces.resource_responses_queued == 1U);
    CHECK(traces.response_boundaries == 1U);
    CHECK(traces.manifest_publications == 1U);
    CHECK(traces.manifest_terminal_outcomes == 1U);
    CHECK(traces.endpoint_mismatches == 0U);
    REQUIRE(traces.transmitted_at_response_boundary);
    REQUIRE(traces.transmitted_at_manifest_publication);
    REQUIRE(traces.transmitted_at_manifest_terminal);
    REQUIRE(traces.authentication_releases_at_manifest_terminal);
    CHECK(*traces.transmitted_at_manifest_publication ==
          *traces.transmitted_at_response_boundary);
    CHECK(*traces.transmitted_at_manifest_terminal ==
          *traces.transmitted_at_response_boundary);
    CHECK(*traces.authentication_releases_at_manifest_terminal == 0U);
    CHECK(prepared_provider.provider->consumed());
    CHECK(authentication_releases == 1U);
    CHECK(snapshot_synthetic_root(root.path()) == before);
    CHECK(can_open_synthetic_writer(
        root.game_path(selected_game) / "tempdecal.wad"));

    if (verify_stale_locator) {
        REQUIRE(manifest.world_selection().locator());
        const auto locator = *manifest.world_selection().locator();
        root.write(
            selected_game,
            "maps/test_map.bsp",
            "synthetic replacement with changed size");
        const auto reopened = environment->reopen_verified(locator);
        REQUIRE_FALSE(reopened);
        REQUIRE(reopened.error);
        CHECK(reopened.error->code ==
              hlclient::local_resources::
                  LocalResourceLocatorReopenErrorCode::stale_locator);
    }

    handshake.update(now + 100ms);
    handshake.cancel(now + 200ms);
    CHECK(authentication_releases == 1U);
    require_no_datagram(*server);
    if (!verify_stale_locator) {
        CHECK(snapshot_synthetic_root(root.path()) == before);
    }
}

class ScopedChildProcess final {
public:
    ~ScopedChildProcess()
    {
        if (information_.hProcess != nullptr) {
            if (::WaitForSingleObject(information_.hProcess, 0U) == WAIT_TIMEOUT) {
                static_cast<void>(::TerminateProcess(information_.hProcess, 255U));
                static_cast<void>(::WaitForSingleObject(
                    information_.hProcess,
                    5'000U));
            }
            static_cast<void>(::CloseHandle(information_.hProcess));
        }
        if (information_.hThread != nullptr) {
            static_cast<void>(::CloseHandle(information_.hThread));
        }
    }

    ScopedChildProcess(const ScopedChildProcess&) = delete;
    ScopedChildProcess& operator=(const ScopedChildProcess&) = delete;
    ScopedChildProcess() = default;

    [[nodiscard]] PROCESS_INFORMATION* information() noexcept
    {
        return &information_;
    }

private:
    PROCESS_INFORMATION information_{};
};

[[nodiscard]] std::filesystem::path sibling_hlclient_executable()
{
    std::wstring module(32'768U, L'\0');
    const DWORD length = ::GetModuleFileNameW(
        nullptr,
        module.data(),
        static_cast<DWORD>(module.size()));
    REQUIRE(length > 0U);
    REQUIRE(length < module.size());
    module.resize(length);
    return std::filesystem::path{module}.parent_path() / L"hlclient.exe";
}

[[nodiscard]] DWORD run_local_provider_client(
    const std::filesystem::path& root,
    const std::string_view game,
    const std::uint16_t server_port)
{
    const auto executable = sibling_hlclient_executable();
    REQUIRE(std::filesystem::is_regular_file(executable));
    const auto unused_auth = root / L"absent-auth-material.bin";
    std::wstring command =
        L"\"" + executable.wstring() +
        L"\" --renderer null --connect 127.0.0.1:" +
        std::to_wstring(server_port) +
        L" --stop-after resource-response-boundary --auth-provider file "
        L"--auth-material-file \"" + unused_auth.wstring() +
        L"\" --resource-consistency-provider local --basedir \"" +
        root.wstring() + L"\" --game " +
        std::wstring{game.begin(), game.end()};
    std::vector<wchar_t> mutable_command{command.begin(), command.end()};
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    ScopedChildProcess child;
    REQUIRE(::CreateProcessW(
                executable.c_str(),
                mutable_command.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                nullptr,
                &startup,
                child.information()) != FALSE);
    REQUIRE(::WaitForSingleObject(
                child.information()->hProcess,
                5'000U) == WAIT_OBJECT_0);
    DWORD exit_code = STILL_ACTIVE;
    REQUIRE(::GetExitCodeProcess(
                child.information()->hProcess,
                &exit_code) != FALSE);
    return exit_code;
}

void run_local_provider_resource_response(
    const std::size_t run,
    const ResourceResponseIntegrationScenario scenario,
    const bool exercise_game_and_fallback)
{
    INFO("production local-provider fake-HLDS run " << run + 1U);
    hlclient::tests::ScopedLocalResourceTestRoot root;

    std::string game{"valve"};
    std::uint32_t expected_root_id = 0U;
    if (exercise_game_and_fallback) {
        game = "mymod";
        root.create_game(game);
        if ((run % 2U) == 0U) {
            root.write("valve", "tempdecal.wad", "decoy");
            root.write(game, "tempdecal.wad", "abc");
        } else {
            root.write("valve", "tempdecal.wad", "abc");
            expected_root_id = 1U;
        }
    } else {
        root.write("valve", "tempdecal.wad", "abc");
    }

    const auto before = snapshot_synthetic_root(root.path());
    auto roots = hlclient::local_resources::LocalResourceSearchRoots::create(
        root.path(),
        game);
    REQUIRE(roots);
    const auto expected_root_count = exercise_game_and_fallback ? 2U : 1U;
    CHECK(roots.roots->size() == expected_root_count);

    auto prepared = consistency::PreparedLocalResourceConsistencyProvider::prepare(
        std::move(*roots.roots));
    REQUIRE(prepared);
    CHECK(prepared.provider->validated_root_count() == expected_root_count);
    CHECK(prepared.provider->selected_root_id().value() == expected_root_id);
    CHECK(prepared.provider->byte_count() == 3U);
    CHECK(prepared.provider->opaque_byte_count() == 16U);
    CHECK_FALSE(prepared.provider->consumed());
    const auto selected_target =
        root.game_path(expected_root_id == 0U ? game : "valve") /
        "tempdecal.wad";
    CHECK_FALSE(can_open_synthetic_writer(selected_target));

    run_resource_response_with_provider(
        run,
        scenario,
        *prepared.provider,
        kExactLocalProviderResponse,
        3U,
        nullptr);

    CHECK(prepared.provider->consumed());
    CHECK(can_open_synthetic_writer(selected_target));
    const auto after = snapshot_synthetic_root(root.path());
    CHECK(after == before);
}

} // namespace

TEST_CASE("Fake HLDS user-info stop point sends no transition request",
          "[goldsrc][userinfo][udp][stop-point][security][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        INFO("fake-HLDS user-info stop run " << run + 1U);
        run_user_info_stop_point();
    }
}

TEST_CASE("Fake HLDS malformed inbound user-info fails atomically before sendres",
          "[goldsrc][userinfo][resource-transition][udp][negative][malformed-userinfo]")
{
    SECTION("wrong opcode at the exact opcode-13 boundary") {
        run_malformed_user_info(MalformedUserInfoScenario::wrong_opcode);
    }
    SECTION("duplicate client index") {
        run_malformed_user_info(
            MalformedUserInfoScenario::duplicate_client_index);
    }
    SECTION("unterminated info string") {
        run_malformed_user_info(
            MalformedUserInfoScenario::unterminated_info_string);
    }
    SECTION("info string exceeds the default project limit") {
        run_malformed_user_info(
            MalformedUserInfoScenario::oversized_info_string);
    }
    SECTION("fixed opaque suffix is missing") {
        run_malformed_user_info(
            MalformedUserInfoScenario::missing_opaque_suffix);
    }
}

TEST_CASE("Fake HLDS full resource-transition boundary repeats 20 of 20",
          "[goldsrc][resource-transition][udp][baseline][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_transition(run, IntegrationScenario::baseline);
    }
}

TEST_CASE("Fake HLDS dropped transition request repeats 20 of 20 without semantic requeue",
          "[goldsrc][resource-transition][udp][drop-request][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_transition(run, IntegrationScenario::dropped_request);
    }
}

TEST_CASE("Fake HLDS dropped transition ACK repeats 20 of 20 with bounded pre-ACK payload",
          "[goldsrc][resource-transition][udp][drop-ack][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_transition(
            run,
            IntegrationScenario::dropped_acknowledgement);
    }
}

TEST_CASE("Fake HLDS multi-userinfo differential repeats 20 of 20",
          "[goldsrc][resource-transition][udp][multi-userinfo][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_transition(run, IntegrationScenario::multiple_user_info);
    }
}

TEST_CASE("Fake HLDS full resource-list baseline repeats 20 of 20",
          "[goldsrc][resource-list][udp][baseline][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_list(run, ResourceListIntegrationScenario::baseline);
    }
}

TEST_CASE("Fake HLDS reordered resource-list fragments repeat 20 of 20",
          "[goldsrc][resource-list][udp][fragment][reordered][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_list(
            run,
            ResourceListIntegrationScenario::reordered_fragments);
    }
}

TEST_CASE("Fake HLDS resource-list map differential repeats 20 of 20",
          "[goldsrc][resource-list][udp][differential][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_list(
            run,
            ResourceListIntegrationScenario::differential_map);
    }
}

TEST_CASE("Fake HLDS malicious resource names remain metadata for 20 of 20",
          "[goldsrc][resource-list][udp][security][no-filesystem][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_list(
            run,
            ResourceListIntegrationScenario::malicious_names);
    }
}

TEST_CASE("Fake HLDS resource-response baseline repeats 20 of 20",
          "[goldsrc][resource-response][udp][baseline][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_response(
            run,
            ResourceResponseIntegrationScenario::baseline);
    }
}

TEST_CASE("Fake HLDS dropped resource response repeats 20 of 20 without semantic requeue",
          "[goldsrc][resource-response][udp][drop-response][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_response(
            run,
            ResourceResponseIntegrationScenario::dropped_response);
    }
}

TEST_CASE("Fake HLDS dropped response ACK repeats 20 of 20 with transport-only retry",
          "[goldsrc][resource-response][udp][drop-ack][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_response(
            run,
            ResourceResponseIntegrationScenario::dropped_acknowledgement);
    }
}

TEST_CASE("Fake HLDS coalesced response tails repeat 20 of 20 outside reliable semantics",
          "[goldsrc][resource-response][udp][coalesced-tail][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_response(
            run,
            ResourceResponseIntegrationScenario::coalesced_tail);
    }
}

TEST_CASE("Fake HLDS map and resource-list differentials preserve response for 20 of 20",
          "[goldsrc][resource-response][udp][differential][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_resource_response(
            run,
            ResourceResponseIntegrationScenario::differential_map);
    }
}

TEST_CASE("Production local provider fake HLDS baseline repeats 20 of 20",
          "[goldsrc][resource-response][local-provider][udp][baseline][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_local_provider_resource_response(
            run,
            ResourceResponseIntegrationScenario::baseline,
            false);
    }
}

TEST_CASE("Production local provider game priority and fallback repeat 20 of 20",
          "[goldsrc][resource-response][local-provider][roots][udp][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_local_provider_resource_response(
            run,
            ResourceResponseIntegrationScenario::baseline,
            true);
    }
}

TEST_CASE("Production local provider dropped response repeats 20 of 20",
          "[goldsrc][resource-response][local-provider][drop-response][udp][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_local_provider_resource_response(
            run,
            ResourceResponseIntegrationScenario::dropped_response,
            false);
    }
}

TEST_CASE("Production local provider dropped ACK repeats 20 of 20",
          "[goldsrc][resource-response][local-provider][drop-ack][udp][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_local_provider_resource_response(
            run,
            ResourceResponseIntegrationScenario::dropped_acknowledgement,
            false);
    }
}

TEST_CASE("Production local provider rejects malicious server paths for 20 of 20",
          "[goldsrc][resource-response][local-provider][security][udp][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_local_provider_resource_response(
            run,
            ResourceResponseIntegrationScenario::malicious_resource_names,
            false);
    }
}

TEST_CASE("Production local provider imports a valid literal BSP 20 of 20",
          "[goldsrc][bsp][asset-dispatch][local-provider][udp][production][full-flow][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        INFO("production literal BSP full-flow run " << run + 1U << "/20");
        run_precache_manifest_integration(
            run,
            PrecacheManifestIntegrationScenario::complete,
            PrecacheManifestTransportScenario::baseline,
            false,
            PrecacheManifestCompletionMode::production_bsp_dispatch);
    }
}

TEST_CASE("Production local provider imports a world-ready incomplete BSP 20 of 20",
          "[goldsrc][bsp][asset-dispatch][local-provider][udp][production][incomplete][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        INFO("production incomplete-manifest BSP run " << run + 1U << "/20");
        run_precache_manifest_integration(
            run,
            PrecacheManifestIntegrationScenario::world_ready_missing_sound,
            PrecacheManifestTransportScenario::baseline,
            false,
            PrecacheManifestCompletionMode::production_bsp_dispatch);
    }
}

TEST_CASE("Production local provider rejects a malformed BSP 20 of 20",
          "[goldsrc][bsp][asset-dispatch][local-provider][udp][production][malformed][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        INFO("production malformed BSP full-flow run " << run + 1U << "/20");
        run_precache_manifest_integration(
            run,
            PrecacheManifestIntegrationScenario::complete,
            PrecacheManifestTransportScenario::baseline,
            false,
            PrecacheManifestCompletionMode::
                production_bsp_malformed_rejection);
    }
}

TEST_CASE("Production local provider imports a chunked BSP source 20 of 20",
          "[goldsrc][bsp][asset-dispatch][local-provider][udp][production][chunked][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        INFO("production three-byte-chunk BSP run " << run + 1U << "/20");
        run_precache_manifest_integration(
            run,
            PrecacheManifestIntegrationScenario::complete,
            PrecacheManifestTransportScenario::baseline,
            false,
            PrecacheManifestCompletionMode::production_bsp_dispatch,
            3U);
    }
}

TEST_CASE("Production BSP dispatch survives a dropped resource response 20 of 20",
          "[goldsrc][bsp][asset-dispatch][local-provider][udp][production][drop-response][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        INFO("production dropped-response BSP run " << run + 1U << "/20");
        run_precache_manifest_integration(
            run,
            PrecacheManifestIntegrationScenario::complete,
            PrecacheManifestTransportScenario::dropped_response,
            false,
            PrecacheManifestCompletionMode::production_bsp_dispatch);
    }
}

TEST_CASE("Production BSP dispatch survives a dropped covering ACK 20 of 20",
          "[goldsrc][bsp][asset-dispatch][local-provider][udp][production][drop-ack][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        INFO("production dropped-ACK BSP run " << run + 1U << "/20");
        run_precache_manifest_integration(
            run,
            PrecacheManifestIntegrationScenario::complete,
            PrecacheManifestTransportScenario::dropped_acknowledgement,
            false,
            PrecacheManifestCompletionMode::production_bsp_dispatch);
    }
}

TEST_CASE("Production BSP fake HLDS covers required one-shot import variants",
          "[goldsrc][bsp][asset-dispatch][local-provider][udp][production][variants]")
{
    const auto run_variant = [](const ProductionBspIntegrationScenario scenario,
                                const std::size_t chunk_bytes = 7U) {
        run_precache_manifest_integration(
            0U,
            PrecacheManifestIntegrationScenario::complete,
            PrecacheManifestTransportScenario::baseline,
            false,
            PrecacheManifestCompletionMode::production_bsp_dispatch,
            chunk_bytes,
            scenario);
    };

    SECTION("valid triangle BSP") {
        run_variant(ProductionBspIntegrationScenario::valid_triangle);
    }
    SECTION("valid multi-face BSP") {
        run_variant(ProductionBspIntegrationScenario::valid_multi_face);
    }
    SECTION("missing texture metadata") {
        run_variant(
            ProductionBspIntegrationScenario::missing_texture_metadata);
    }
    SECTION("malformed BSP header") {
        run_variant(ProductionBspIntegrationScenario::malformed_header);
    }
    SECTION("malformed texinfo") {
        run_variant(ProductionBspIntegrationScenario::malformed_texinfo);
    }
    SECTION("configured geometry output limit") {
        run_variant(ProductionBspIntegrationScenario::geometry_output_limit);
    }
    SECTION("stale selected map locator before source open") {
        run_variant(
            ProductionBspIntegrationScenario::stale_selected_locator);
    }
    SECTION("selected map changes during incremental same-handle read") {
        run_variant(
            ProductionBspIntegrationScenario::source_changed_during_read,
            8U);
    }
}

TEST_CASE("Fake HLDS complete precache manifest repeats 20 of 20",
          "[goldsrc][precache-manifest][udp][complete][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_precache_manifest_integration(
            run,
            PrecacheManifestIntegrationScenario::complete);
    }
}

TEST_CASE("Fake HLDS world-ready incomplete precache manifest repeats 20 of 20",
          "[goldsrc][precache-manifest][udp][incomplete][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_precache_manifest_integration(
            run,
            PrecacheManifestIntegrationScenario::world_ready_missing_sound);
    }
}

TEST_CASE("Fake HLDS locally missing map precache manifest repeats 20 of 20",
          "[goldsrc][precache-manifest][udp][missing-map][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_precache_manifest_integration(
            run,
            PrecacheManifestIntegrationScenario::local_map_missing);
    }
}

TEST_CASE("Fake HLDS sparse maximum-index precache manifest repeats 20 of 20",
          "[goldsrc][precache-manifest][udp][sparse-slots][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_precache_manifest_integration(
            run,
            PrecacheManifestIntegrationScenario::sparse_slots);
    }
}

TEST_CASE("Fake HLDS malicious-name precache manifest repeats 20 of 20",
          "[goldsrc][precache-manifest][udp][security][repeat-20]")
{
    for (std::size_t run = 0U; run < 20U; ++run) {
        run_precache_manifest_integration(
            run,
            PrecacheManifestIntegrationScenario::malicious_name);
    }
}

TEST_CASE("Fake HLDS precache manifest covers bounded secondary variants",
          "[goldsrc][precache-manifest][udp][variants]")
{
    SECTION("missing non-world model") {
        run_precache_manifest_integration(
            100U,
            PrecacheManifestIntegrationScenario::missing_model);
    }

    SECTION("unsupported non-ASCII server name") {
        run_precache_manifest_integration(
            101U,
            PrecacheManifestIntegrationScenario::unsupported_non_ascii);
    }

    SECTION("duplicate exact ServerInfo map match") {
        run_precache_manifest_integration(
            102U,
            PrecacheManifestIntegrationScenario::duplicate_map_match);
    }

    SECTION("published locator becomes stale after synthetic replacement") {
        run_precache_manifest_integration(
            103U,
            PrecacheManifestIntegrationScenario::complete,
            PrecacheManifestTransportScenario::baseline,
            true);
    }

    SECTION("dropped resource response retries transport only") {
        run_precache_manifest_integration(
            105U,
            PrecacheManifestIntegrationScenario::complete,
            PrecacheManifestTransportScenario::dropped_response);
    }

    SECTION("dropped response ACK retries transport only") {
        run_precache_manifest_integration(
            106U,
            PrecacheManifestIntegrationScenario::complete,
            PrecacheManifestTransportScenario::dropped_acknowledgement);
    }
}

TEST_CASE("Fake HLDS case-ambiguous precache manifest runs when supported",
          "[goldsrc][precache-manifest][udp][ambiguous][capability]")
{
    run_precache_manifest_integration(
        104U,
        PrecacheManifestIntegrationScenario::ambiguous_sound);
}

TEST_CASE("Local provider preparation failures emit zero UDP packets",
          "[goldsrc][resource-response][local-provider][pre-network][negative]")
{
    network::NetworkRuntime runtime;
    INFO(runtime.error_message());
    REQUIRE(runtime.valid());
    std::string error;
    auto server = network::UdpSocket::open_ipv4(runtime, error);
    REQUIRE(server);
    REQUIRE(server->bind(network::NetworkAddress::loopback(0U), error));
    const auto server_endpoint = server->local_address(error);
    REQUIRE(server_endpoint);

    const auto require_pre_network_failure = [&](const auto& root,
                                                 const std::string_view game) {
        const auto before = snapshot_synthetic_root(root.path());
        CHECK(run_local_provider_client(
                  root.path(), game, server_endpoint->port()) == 1U);
        require_no_datagram(*server);
        CHECK(snapshot_synthetic_root(root.path()) == before);
    };

    SECTION("missing target")
    {
        hlclient::tests::ScopedLocalResourceTestRoot root;
        require_pre_network_failure(root, "valve");
    }

    SECTION("missing explicit game root")
    {
        hlclient::tests::ScopedLocalResourceTestRoot root;
        require_pre_network_failure(root, "missingmod");
    }

    SECTION("empty target")
    {
        hlclient::tests::ScopedLocalResourceTestRoot root;
        root.write("valve", "tempdecal.wad", "");
        require_pre_network_failure(root, "valve");
    }

    SECTION("target exceeds the default consistency limit")
    {
        hlclient::tests::ScopedLocalResourceTestRoot root;
        root.write_repeated(
            "valve",
            "tempdecal.wad",
            static_cast<std::size_t>(
                hlclient::local_resources::
                    kDefaultMaximumLocalResourceFileSize +
                1U));
        require_pre_network_failure(root, "valve");
    }

    SECTION("final reparse target")
    {
        hlclient::tests::ScopedLocalResourceTestRoot root;
        root.write("valve", "real.wad", "real");
        std::error_code link_error;
        std::filesystem::create_symlink(
            root.game_path("valve") / "real.wad",
            root.game_path("valve") / "tempdecal.wad",
            link_error);
        if (link_error) {
            SKIP("File symlinks are unavailable: " << link_error.message());
        }
        require_pre_network_failure(root, "valve");
    }

    SECTION("ambiguous ASCII case")
    {
        hlclient::tests::ScopedLocalResourceTestRoot root;
        if (!hlclient::tests::enable_case_sensitive_directory(
                root.game_path("valve"))) {
            SKIP("Case-sensitive directory mode is unavailable");
        }
        root.write("valve", "TempDecal.wad", "one");
        root.write("valve", "TEMPDECAL.WAD", "two");
        require_pre_network_failure(root, "valve");
    }
}

TEST_CASE("Fake HLDS malformed resource lists fail atomically after transition",
          "[goldsrc][resource-list][udp][negative][transactional]")
{
    SECTION("invalid type") {
        run_resource_list(0U, ResourceListIntegrationScenario::invalid_type);
    }
    SECTION("excessive count") {
        run_resource_list(0U, ResourceListIntegrationScenario::excessive_count);
    }
    SECTION("duplicate identity") {
        run_resource_list(
            0U,
            ResourceListIntegrationScenario::duplicate_identity);
    }
    SECTION("unobserved flags profile") {
        run_resource_list(
            0U,
            ResourceListIntegrationScenario::unobserved_flags_profile);
    }
    SECTION("nonzero terminal fill") {
        run_resource_list(
            0U,
            ResourceListIntegrationScenario::nonzero_padding);
    }
    SECTION("trailing message is never scanned") {
        run_resource_list(0U, ResourceListIntegrationScenario::trailing_data);
    }
    SECTION("truncated final entry") {
        run_resource_list(
            0U,
            ResourceListIntegrationScenario::truncated_entry);
    }
    SECTION("truncated count") {
        run_resource_list(
            0U,
            ResourceListIntegrationScenario::truncated_count);
    }
    SECTION("unterminated name") {
        run_resource_list(
            0U,
            ResourceListIntegrationScenario::unterminated_name);
    }
    SECTION("per-entry raw size-code limit") {
        run_resource_list(
            0U,
            ResourceListIntegrationScenario::resource_size_limit);
    }
    SECTION("checked total raw size-code bound") {
        run_resource_list(
            0U,
            ResourceListIntegrationScenario::resource_total_size_limit);
    }

    // A uint64_t arithmetic overflow is not representable with a u12 count
    // and u24 size codes; the configured total bound exercises the same
    // checked, fail-closed accumulation path at production-flow level.
}

TEST_CASE("Fake HLDS resource-list transport and lifecycle negatives remain atomic",
          "[goldsrc][resource-list][udp][negative][transport][lifecycle]")
{
    SECTION("wrong endpoint is ignored before the valid transfer") {
        run_resource_list(
            0U,
            ResourceListIntegrationScenario::wrong_endpoint);
    }
    SECTION("missing fragment reaches the fixed transfer timeout") {
        run_resource_list(
            0U,
            ResourceListIntegrationScenario::missing_fragment);
    }
    SECTION("malformed BZip2 completes transport but publishes no list") {
        run_resource_list(
            0U,
            ResourceListIntegrationScenario::malformed_bzip2);
    }
    SECTION("duplicate completed batch cannot republish terminal state") {
        run_resource_list(
            0U,
            ResourceListIntegrationScenario::duplicate_completed_batch);
    }
    SECTION("channel timeout uses the manual clock") {
        run_resource_list(0U, ResourceListIntegrationScenario::timeout);
    }
    SECTION("cancellation is terminal and idempotent") {
        run_resource_list(0U, ResourceListIntegrationScenario::cancellation);
    }
    SECTION("resource-list event backpressure publishes no candidate") {
        run_resource_list(
            0U,
            ResourceListIntegrationScenario::event_backpressure);
    }

    // A u12 index has no out-of-range wire encoding. Optional/profile layout
    // remains typed unsupported above; this integration does not fabricate it.
}
