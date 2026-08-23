#include "delta_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "local_resource_test_fixture.hpp"
#include "resource_client_response_test_fixture.hpp"
#include "resource_list_test_fixture.hpp"
#include "user_info_test_fixture.hpp"

#include <hlclient/auth/authentication_provider.hpp>
#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/goldsrc/connect_request.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/local_resource_mapping.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/netchan_payload_transform.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/network/network_runtime.hpp>
#include <hlclient/network/udp_socket.hpp>
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
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace auth = hlclient::auth;
namespace consistency = hlclient::resource_consistency;
namespace delta_fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace network = hlclient::network;
namespace resource_fixture = resource_list_test_fixture;
namespace response_fixture =
    hlclient::test::resource_client_response_fixture;
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
