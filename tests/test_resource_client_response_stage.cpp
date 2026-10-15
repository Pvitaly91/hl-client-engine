#include "delta_test_fixture.hpp"
#include "local_resource_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "resource_client_response_test_fixture.hpp"
#include "resource_list_test_fixture.hpp"
#include "user_info_test_fixture.hpp"
#include "goldsrc_usercmd_test_fixture.hpp"
#include "collision_brush_test_fixture.hpp"
#include "vertical_lift_test_fixture.hpp"
#include "lift_top_e32_1_test_fixture.hpp"
#include "hand_grenade_test_fixture.hpp"
#include "literal_movement_bsp_fixture.hpp"
#include "convex_corner_test_fixture.hpp"
#include <hlclient/goldsrc/bsp/goldsrc_bsp_parser.hpp>
#include <hlclient/goldsrc/collision/goldsrc_collision_world_builder.hpp>
#include <catch2/catch_approx.hpp>

#include <hlclient/client/client_world_state.hpp>
#include <hlclient/game_api/game_client_host.hpp>
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <hlclient/games/halflife/client_module.hpp>
#include <hlclient/goldsrc/live_runtime_stage.hpp>
#include <hlclient/goldsrc/disconnect_request.hpp>
#include <hlclient/goldsrc/local_audio.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/reference_client_move.hpp>
#include <hlclient/goldsrc/resource_client_response_stage.hpp>
#include <hlclient/goldsrc/stock_spawn_request.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/resource_consistency/prepared_local_resource_consistency_provider.hpp>
#include <hlclient/resource_consistency/provider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <bzlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace consistency = hlclient::resource_consistency;
namespace delta_fixture = hlclient::test::delta_fixture;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace response_fixture = hlclient::test::resource_client_response_fixture;
namespace user_fixture = hlclient::test::user_info_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace local = hlclient::local_resources;
namespace network = hlclient::network;

// Project-owned alternate module used only to prove the neutral host -> live
// command cache -> prediction path.  The second accepted command receives one
// post-step impulse; no Half-Life implementation or asset is involved.
struct PostMovementEffectTestProbe {
  std::size_t queries{};
  std::size_t emissions{};
};

class PostMovementEffectTestModule final
    : public hlclient::game_api::IGameClientModule {
public:
  explicit PostMovementEffectTestModule(
      const hlclient::assets::AssetVector3 impulse = {100.0F, 0.0F, 0.0F},
      std::shared_ptr<PostMovementEffectTestProbe> probe = {})
      : impulse_{impulse}, probe_{std::move(probe)} {
    auto bindings = hlclient::gameplay_input::GameplayInputBindings::project_default_v1();
    if (!bindings) throw std::logic_error{"invalid post-effect test bindings"};
    policy_.bindings = std::make_shared<hlclient::gameplay_input::GameplayInputBindings>(
        std::move(*bindings.bindings));
    policy_.environment_profile = goldsrc::movement::
        GoldSrcMovementEnvironmentProfile::movevars_dry_walk_subset_v1;
    policy_.movement_speeds = {1.0F, 1.0F, 1.0F};
    policy_.speed_key_multiplier = 1.0F;
    policy_.button_policy = goldsrc::GoldSrcReferenceButtonPolicy::
        jump_duck_primary_secondary_reload_use;
  }
  void reset(hlclient::game_api::GameSessionIdentity identity) noexcept override {
    generation_ = identity.network_generation; effect_calls_ = 0U;
  }
  void teardown() noexcept override { generation_ = 0U; effect_calls_ = 0U; }
  const hlclient::game_api::GameMovementPolicy& movement_policy() const noexcept override {
    return policy_;
  }
  hlclient::game_api::GameRecordResult stage_record(
      const hlclient::game_api::GameRecordInput&) const override {
    return {hlclient::game_api::GameRecordState{}, {}};
  }
  void commit_record(hlclient::game_api::GameRecordState&&) noexcept override {}
  void bind_model(std::optional<hlclient::game_api::LocalWeaponModelMetadata>) override {}
  void observe(const hlclient::client::RuntimeClientObservationState&, double) override {}
  void submit(const hlclient::game_api::LocalWeaponSubmittedCommand&, double) override {}
  std::optional<hlclient::assets::AssetVector3> command_movement_effect(
      const hlclient::game_api::LocalWeaponSubmittedCommand& command) noexcept override {
    ++effect_calls_;
    if (probe_) ++probe_->queries;
    if (command.generation == generation_ && effect_calls_ == 2U) {
      if (probe_) ++probe_->emissions;
      return impulse_;
    }
    return {};
  }
  hlclient::game_api::CommandMovementEffectPhase
  command_movement_effect_phase() const noexcept override {
    return hlclient::game_api::CommandMovementEffectPhase::after_movement;
  }
  void cancel_uncommitted() noexcept override {}
  hlclient::game_api::LocalWeaponPresentationSnapshot sample(double) noexcept override {
    return {};
  }
  hlclient::game_api::LocalAudioBatch drain_audio() noexcept override { return {}; }
  hlclient::game_api::ViewmodelIntent viewmodel(
      const hlclient::client::RuntimeClientObservationState&,
      std::optional<hlclient::game_api::LocalWeaponModelMetadata>, double,
      std::optional<hlclient::game_api::LocalWeaponVisual>) override { return {}; }
  hlclient::game_api::HudState hud(
      const hlclient::client::RuntimeClientObservationState&, double) override { return {}; }
  hlclient::game_api::CameraIntent camera(
      const hlclient::client::RuntimeClientObservationState&, double) const noexcept override {
    return {};
  }
  void observe_action_evidence(const hlclient::client::RuntimeClientObservationState&,
      const std::optional<hlclient::game_api::GameActionTraffic>&) noexcept override {}
  hlclient::game_api::GameActionEvidenceSnapshot action_evidence() const noexcept override {
    return {};
  }
  std::optional<hlclient::game_api::GameCommandRequest> inventory_selection(
      const hlclient::client::RuntimeClientObservationState&, std::uint8_t) const override {
    return {};
  }
  std::optional<std::uint8_t> select_group(
      const hlclient::client::RuntimeClientObservationState&, std::uint8_t,
      std::optional<std::uint8_t>) const override { return {}; }
  std::optional<std::uint8_t> cycle_inventory(
      const hlclient::client::RuntimeClientObservationState&,
      std::optional<std::uint8_t>, int) const override { return {}; }
  hlclient::game_api::GameCommandRequest self_kill_request() const override { return {}; }
  std::optional<std::uint8_t> scenario_inventory_target(
      const hlclient::client::RuntimeClientObservationState&, std::size_t) const override {
    return {};
  }
  hlclient::game_api::GameScenarioDirective scenario_command(
      hlclient::game_api::GameScenario,
      const hlclient::client::RuntimeClientObservationState*, std::size_t,
      double, double) noexcept override { return {}; }
  hlclient::game_api::DamageRespawnScriptSnapshot
  damage_respawn_snapshot() const noexcept override { return {}; }

private:
  hlclient::game_api::GameMovementPolicy policy_{};
  hlclient::assets::AssetVector3 impulse_{};
  std::shared_ptr<PostMovementEffectTestProbe> probe_;
  std::uint64_t generation_{};
  unsigned effect_calls_{};
};

// Independently authored stage fixture. It deliberately names the stage's
// evidence-gated tempdecal profile rather than asking the production builder
// to produce the expected bytes.
inline constexpr std::array<std::byte, 41U> kExactTempdecalResponse{
    std::byte{0x05U}, std::byte{0x01U}, std::byte{0x00U}, std::byte{'t'},
    std::byte{'e'},   std::byte{'m'},   std::byte{'p'},   std::byte{'d'},
    std::byte{'e'},   std::byte{'c'},   std::byte{'a'},   std::byte{'l'},
    std::byte{'.'},   std::byte{'w'},   std::byte{'a'},   std::byte{'d'},
    std::byte{0x00U}, std::byte{0x03U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x04U}, std::byte{0x03U}, std::byte{0x02U}, std::byte{0x01U},
    std::byte{0x04U}, std::byte{0xa0U}, std::byte{0xa1U}, std::byte{0xa2U},
    std::byte{0xa3U}, std::byte{0xa4U}, std::byte{0xa5U}, std::byte{0xa6U},
    std::byte{0xa7U}, std::byte{0xa8U}, std::byte{0xa9U}, std::byte{0xaaU},
    std::byte{0xabU}, std::byte{0xacU}, std::byte{0xadU}, std::byte{0xaeU},
    std::byte{0xafU},
};

inline constexpr std::array<std::byte, 41U> kExactPreparedLocalResponse{
    std::byte{0x05U}, std::byte{0x01U}, std::byte{0x00U}, std::byte{'t'},
    std::byte{'e'},   std::byte{'m'},   std::byte{'p'},   std::byte{'d'},
    std::byte{'e'},   std::byte{'c'},   std::byte{'a'},   std::byte{'l'},
    std::byte{'.'},   std::byte{'w'},   std::byte{'a'},   std::byte{'d'},
    std::byte{0x00U}, std::byte{0x03U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x03U}, std::byte{0x00U}, std::byte{0x00U}, std::byte{0x00U},
    std::byte{0x04U}, std::byte{0x90U}, std::byte{0x01U}, std::byte{0x50U},
    std::byte{0x98U}, std::byte{0x3cU}, std::byte{0xd2U}, std::byte{0x4fU},
    std::byte{0xb0U}, std::byte{0xd6U}, std::byte{0x96U}, std::byte{0x3fU},
    std::byte{0x7dU}, std::byte{0x28U}, std::byte{0xe1U}, std::byte{0x7fU},
    std::byte{0x72U},
};

struct SentDatagram {
  network::NetworkAddress destination;
  std::vector<std::byte> payload;
};

class FakeTransport final : public network::IDatagramTransport {
public:
  [[nodiscard]] network::DatagramLocalAddressResult
  local_address() const override {
    return {local, {}};
  }

  [[nodiscard]] network::DatagramSendResult
  send_to(const network::NetworkAddress &destination,
          const std::span<const std::byte> payload) override {
    ++send_attempt_count;
    if (!send_statuses.empty()) {
      const auto status = send_statuses.front();
      send_statuses.pop_front();
      if (status != network::DatagramSendStatus::sent) {
        return {status, {}};
      }
    }
    sent.push_back(SentDatagram{
        destination,
        std::vector<std::byte>{payload.begin(), payload.end()},
    });
    return {network::DatagramSendStatus::sent, {}};
  }

  void queue_send_status(const network::DatagramSendStatus status) {
    send_statuses.push_back(status);
  }

  [[nodiscard]] network::DatagramTransportReceiveResult
  receive(std::size_t) override {
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

  void queue(const network::NetworkAddress source,
             std::vector<std::byte> payload) {
    const auto size = payload.size();
    incoming.push_back({
        network::DatagramTransportReceiveStatus::received,
        network::Datagram{source, std::move(payload)},
        source,
        size,
        {},
    });
  }

  network::NetworkAddress local{network::NetworkAddress::loopback(31'703U)};
  std::vector<SentDatagram> sent;
  std::deque<network::DatagramTransportReceiveResult> incoming;
  std::deque<network::DatagramSendStatus> send_statuses;
  std::size_t send_attempt_count{0U};
};

class CountingConnectionLifetime final
    : public goldsrc::INetchanDriverLifetime {
public:
  explicit CountingConnectionLifetime(std::size_t &releases) noexcept
      : releases_{releases} {}

  ~CountingConnectionLifetime() override { ++releases_; }

private:
  std::size_t &releases_;
};

class CountingConsistencyLifetime final
    : public consistency::IResourceConsistencySessionLifetime {
public:
  explicit CountingConsistencyLifetime(std::size_t &releases) noexcept
      : releases_{releases} {}

  ~CountingConsistencyLifetime() override { ++releases_; }

private:
  std::size_t &releases_;
};

class FakeConsistencyOperation final
    : public consistency::ResourceConsistencyOperation {
public:
  FakeConsistencyOperation(const bool pending_forever,
                           std::size_t &update_count, std::size_t &cancel_count,
                           std::size_t &lifetime_releases) noexcept
      : pending_forever_{pending_forever}, update_count_{update_count},
        cancel_count_{cancel_count}, lifetime_releases_{lifetime_releases} {}

  [[nodiscard]] consistency::ResourceConsistencyUpdateResult update() override {
    ++update_count_;
    if (pending_forever_) {
      return consistency::ResourceConsistencyUpdateResult::pending();
    }

    auto created = consistency::make_resource_consistency_material(
        0x01020304U, response_fixture::kSyntheticOpaqueMaterial);
    REQUIRE(created);
    return consistency::ResourceConsistencyUpdateResult::succeeded(
        consistency::ResourceConsistencySession{
            std::move(*created.material),
            std::make_unique<CountingConsistencyLifetime>(lifetime_releases_),
        });
  }

  void cancel() noexcept override {
    if (!cancelled_) {
      cancelled_ = true;
      ++cancel_count_;
    }
  }

private:
  bool pending_forever_{false};
  std::size_t &update_count_;
  std::size_t &cancel_count_;
  std::size_t &lifetime_releases_;
  bool cancelled_{false};
};

class FakeConsistencyProvider final
    : public consistency::IResourceConsistencyProvider {
public:
  explicit FakeConsistencyProvider(const bool pending_forever = false) noexcept
      : pending_forever_{pending_forever} {}

  [[nodiscard]] consistency::ResourceConsistencyBeginResult
  begin(const consistency::ResourceConsistencyRequirements &requirements)
      override {
    ++begin_count;
    observed_material_count = requirements.material_count();
    observed_opaque_byte_count = requirements.opaque_bytes_per_material();
    return consistency::ResourceConsistencyBeginResult::started(
        std::make_unique<FakeConsistencyOperation>(
            pending_forever_, update_count, cancel_count, lifetime_releases));
  }

  bool pending_forever_{false};
  std::size_t begin_count{0U};
  std::size_t update_count{0U};
  std::size_t cancel_count{0U};
  std::size_t lifetime_releases{0U};
  std::size_t observed_material_count{0U};
  std::size_t observed_opaque_byte_count{0U};
};

class SensitiveFailureProvider final
    : public consistency::IResourceConsistencyProvider {
public:
  [[nodiscard]] consistency::ResourceConsistencyBeginResult
  begin(const consistency::ResourceConsistencyRequirements &) override {
    ++begin_count;
    return consistency::ResourceConsistencyBeginResult::failed({
        consistency::ResourceConsistencyErrorCode::provider_error,
        R"(C:\private\tempdecal.wad material=a0a1)",
    });
  }

  std::size_t begin_count{0U};
};

class InvalidUpdateOperation final
    : public consistency::ResourceConsistencyOperation {
public:
  explicit InvalidUpdateOperation(std::size_t &update_count,
                                  std::size_t &cancel_count) noexcept
      : update_count_{update_count}, cancel_count_{cancel_count} {}

  [[nodiscard]] consistency::ResourceConsistencyUpdateResult update() override {
    ++update_count_;
    return {
        static_cast<consistency::ResourceConsistencyUpdateState>(0xffU),
        std::nullopt,
        std::nullopt,
    };
  }

  void cancel() noexcept override {
    if (!cancelled_) {
      cancelled_ = true;
      ++cancel_count_;
    }
  }

private:
  std::size_t &update_count_;
  std::size_t &cancel_count_;
  bool cancelled_{false};
};

class InvalidUpdateProvider final
    : public consistency::IResourceConsistencyProvider {
public:
  [[nodiscard]] consistency::ResourceConsistencyBeginResult
  begin(const consistency::ResourceConsistencyRequirements &) override {
    ++begin_count;
    return consistency::ResourceConsistencyBeginResult::started(
        std::make_unique<InvalidUpdateOperation>(update_count, cancel_count));
  }

  std::size_t begin_count{0U};
  std::size_t update_count{0U};
  std::size_t cancel_count{0U};
};

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value) {
  const auto parsed = goldsrc::NetchanSequence::from_numeric(value);
  REQUIRE(parsed);
  return *parsed;
}

[[nodiscard]] std::vector<std::byte>
service_envelope(const std::span<const std::byte> semantic_payload) {
  REQUIRE_FALSE(semantic_payload.empty());
  REQUIRE(semantic_payload.size() <=
          (std::numeric_limits<unsigned int>::max)());

  std::vector<char> source;
  source.reserve(semantic_payload.size());
  std::ranges::transform(
      semantic_payload, std::back_inserter(source), [](const std::byte value) {
        return static_cast<char>(std::to_integer<std::uint8_t>(value));
      });
  const auto bound = source.size() + source.size() / 100U + 601U;
  REQUIRE(bound <= (std::numeric_limits<unsigned int>::max)());
  std::vector<char> compressed(bound);
  auto compressed_size = static_cast<unsigned int>(compressed.size());
  REQUIRE(BZ2_bzBuffToBuffCompress(
              compressed.data(), &compressed_size, source.data(),
              static_cast<unsigned int>(source.size()), 9, 0, 30) == BZ_OK);
  compressed.resize(compressed_size);

  std::vector<std::byte> envelope{
      std::byte{0x42U},
      std::byte{0x5aU},
      std::byte{0x32U},
      std::byte{0x00U},
  };
  std::ranges::transform(
      compressed, std::back_inserter(envelope), [](const char value) {
        return static_cast<std::byte>(static_cast<unsigned char>(value));
      });
  return envelope;
}

[[nodiscard]] std::vector<std::byte>
server_packet(const std::uint32_t packet_sequence, const bool reliable,
              const std::uint32_t acknowledgement,
              const bool reliable_acknowledgement,
              std::vector<std::byte> payload = {}) {
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

[[nodiscard]] goldsrc::ResourceClientResponseStageConfig test_config() {
  goldsrc::ResourceClientResponseStageConfig config;
  auto &transition = config.resource_list.transition;
  auto &driver = transition.user_info.movement_environment.delta.pre_resource
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
  config.resource_list.maximum_stage_events = 64U;
  config.maximum_driver_events_per_update = 64U;
  config.response.maximum_response_stage_events = 64U;
  return config;
}

[[nodiscard]] goldsrc::ResourceClientResponseStageConfig
empty_response_config() {
  auto config = test_config();
  config.advertisement_profile =
      goldsrc::ClientResourceAdvertisementProfile::no_custom_resources;
  config.resource_list.transition.user_info.movement_environment.delta
      .pre_resource.initial_signon.driver
      .maximum_unfragmented_reliable_payload =
      goldsrc::kOpcode5EmptyResourceResponseSemanticSize - 1U;
  return config;
}

[[nodiscard]] goldsrc::ResourceClientResponseStageConfig
live_response_config() {
  auto config = empty_response_config();
  config.pre_transmit_payload_policy =
      goldsrc::ResourceResponsePreTransmitPayloadPolicy::decode_nop_control;
  config.completion_policy =
      goldsrc::ResourceResponseCompletionPolicy::covering_acknowledgement;
  config.post_response_payload_compression =
      goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed;
  return config;
}

[[nodiscard]] std::vector<std::vector<std::byte>> schemas() {
  return {
      delta_fixture::schema("alpha_t", delta_fixture::kSchemaAlphaFields),
      delta_fixture::schema("bravo_t", delta_fixture::kSchemaBravoFields),
  };
}

[[nodiscard]] std::vector<std::byte> first_semantic_payload() {
  std::vector<std::byte> post_delta;
  move_fixture::append_move_vars_body(post_delta);
  move_fixture::append_confirmed_controls(post_delta);
  post_delta.insert(post_delta.end(),
                    user_fixture::kExactUserInfoMessage.begin(),
                    user_fixture::kExactUserInfoMessage.end());
  return delta_fixture::service_payload(schemas(), goldsrc::kMoveVarsOpcode,
                                        post_delta);
}

[[nodiscard]] std::vector<std::byte> resource_semantic_payload(const bool brush = false) {
  constexpr std::array prefix{
      std::byte{45U}, std::byte{1U}, std::byte{0U},
      std::byte{0U},  std::byte{0U}, std::byte{0U},
      std::byte{0U},  std::byte{0U}, std::byte{0U},
  };
  std::vector<std::byte> payload{prefix.begin(), prefix.end()};
  if (brush) {
    const auto list = resource_list_test_fixture::make_message({
        {2U,"maps/test_map.bsp",7U,1U,0U},{2U,"*1",27U,0U,0U}});
    payload.insert(payload.end(),list.bytes.begin(),list.bytes.end());
    return payload;
  }
  payload.insert(payload.end(),
                 resource_list_test_fixture::kExactResourceListMessage.begin(),
                 resource_list_test_fixture::kExactResourceListMessage.end());
  return payload;
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket
decode_sent(const SentDatagram &datagram) {
  const auto decoded =
      goldsrc::decode_client_to_server_netchan_packet(datagram.payload);
  REQUIRE(decoded);
  REQUIRE(decoded.packet);
  return *decoded.packet;
}

struct DrivenTransition {
  goldsrc::ClientToServerNetchanPacket request;
};

template <typename Stage>
[[nodiscard]] DrivenTransition drive_to_transition_request(
    Stage &stage, FakeTransport &transport,
    const network::NetworkAddress remote,
    const goldsrc::ResourceClientResponseStageTimePoint epoch,
    std::unique_ptr<goldsrc::INetchanDriverLifetime> lifetime = {}) {
  REQUIRE(stage.start(epoch, transport.local, std::move(lifetime)));
  stage.update(epoch + 1ms);
  REQUIRE(transport.sent.size() == 1U);
  const auto initial = decode_sent(transport.sent.front());

  transport.queue(
      remote, server_packet(1U, true, initial.header.sequence.sequence.value(),
                            true, service_envelope(first_semantic_payload())));
  stage.update(epoch + 2ms);
  CHECK(stage.initial_request_queue_count() == 1U);
  CHECK(stage.transition_request_queue_count() == 1U);

  stage.update(epoch + 3ms);
  REQUIRE(transport.sent.size() >= 3U);
  const auto transition = decode_sent(transport.sent.back());
  constexpr std::array exact_request{
      std::byte{0x03U}, std::byte{'s'}, std::byte{'e'},
      std::byte{'n'},   std::byte{'d'}, std::byte{'r'},
      std::byte{'e'},   std::byte{'s'}, std::byte{0U},
  };
  REQUIRE(std::ranges::equal(transition.payload, exact_request));
  return DrivenTransition{transition};
}

template <typename Stage>
void deliver_resource_payload(
    Stage &stage, FakeTransport &transport,
    const network::NetworkAddress remote, const DrivenTransition &driven,
    const goldsrc::ResourceClientResponseStageTimePoint now) {
  transport.queue(
      remote,
      server_packet(2U, false, driven.request.header.sequence.sequence.value(),
                    false, service_envelope(resource_semantic_payload())));
  stage.update(now);
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket
require_response_packet(const FakeTransport &transport,
                        const std::span<const std::byte> expected) {
  REQUIRE_FALSE(transport.sent.empty());
  const auto response = decode_sent(transport.sent.back());
  REQUIRE(response.header.sequence.flags.reliable);
  REQUIRE(response.header.sequence.flags.fragmented);
  REQUIRE(response.fragments[0U]);
  REQUIRE_FALSE(response.fragments[1U]);
  REQUIRE(response.fragments[0U]->packed_id());
  CHECK(response.fragments[0U]->packed_id()->fragment_index() == 1U);
  CHECK(response.fragments[0U]->packed_id()->fragment_count() == 1U);
  CHECK(response.fragments[0U]->length == expected.size());
  CHECK(response.fragment_payload_size == expected.size());
  CHECK(std::ranges::equal(response.payload, expected));
  return response;
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket
require_response_packet(const FakeTransport &transport) {
  return require_response_packet(transport, kExactTempdecalResponse);
}

[[nodiscard]] bool can_open_local_writer(const std::filesystem::path &path) {
  const HANDLE writer =
      ::CreateFileW(path.c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (writer == INVALID_HANDLE_VALUE) {
    return false;
  }
  static_cast<void>(::CloseHandle(writer));
  return true;
}

[[nodiscard]] std::vector<goldsrc::ResourceClientResponseStageEvent>
drain_events(goldsrc::ResourceClientResponseStage &stage) {
  std::vector<goldsrc::ResourceClientResponseStageEvent> events;
  while (auto event = stage.poll_event()) {
    events.push_back(std::move(*event));
  }
  return events;
}

[[nodiscard]] std::size_t event_count(
    const std::span<const goldsrc::ResourceClientResponseStageEvent> events,
    const goldsrc::ResourceClientResponseStageEventType type) {
  return static_cast<std::size_t>(std::ranges::count_if(
      events, [type](const auto &event) { return event.type == type; }));
}

[[nodiscard]] constexpr std::array<std::byte, 7U>
next_server_semantic_payload() {
  return {
      std::byte{0x03U}, std::byte{'s'}, std::byte{'p'}, std::byte{'a'},
      std::byte{'w'},   std::byte{'n'}, std::byte{0U},
  };
}

void deliver_covering_continuation(
    goldsrc::ResourceClientResponseStage &stage, FakeTransport &transport,
    const network::NetworkAddress remote, const std::uint32_t incoming_sequence,
    const goldsrc::ClientToServerNetchanPacket &response,
    const goldsrc::ResourceClientResponseStageTimePoint now) {
  transport.queue(
      remote, server_packet(incoming_sequence, false,
                            response.header.sequence.sequence.value(), true,
                            service_envelope(next_server_semantic_payload())));
  stage.update(now);
}

constexpr delta_fixture::Field kLiveEntityFields[]{
    {"origin[0]", 0x8000'0004U, 0U, 16U, 32'000U, 4'000U},
    {"origin[1]", 0x8000'0004U, 4U, 16U, 32'000U, 4'000U},
    {"origin[2]", 0x8000'0004U, 8U, 16U, 32'000U, 4'000U},
    {"angles[0]", 0x0000'0010U, 12U, 16U},
    {"angles[1]", 0x0000'0010U, 16U, 16U},
    {"angles[2]", 0x0000'0010U, 20U, 16U},
    {"modelindex", 0x0000'0008U, 24U, 16U},
    {"solid", 0x0000'0002U, 28U, 3U},
    {"movetype", 0x0000'0008U, 32U, 4U},
};

constexpr delta_fixture::Field kLiveClientFields[]{
    {"health", 0x8000'0004U, 0U, 10U},
};

const std::vector<delta_fixture::Field> kBoxEntityFields=[] {
  std::vector<delta_fixture::Field> fields{std::begin(kLiveEntityFields),std::end(kLiveEntityFields)};
  fields.push_back({"owner",8U,36U,8U});
  for(const auto name:{"mins[0]","mins[1]","mins[2]","maxs[0]","maxs[1]","maxs[2]"})
    fields.push_back({name,0x80000004U,static_cast<std::uint16_t>(40+fields.size()*4),16U,32000U,4000U});
  return fields;
}();

constexpr delta_fixture::Field kLiveWeaponFields[]{
    {"m_iClip", 0x8000'0008U, 0U, 10U},
};

constexpr delta_fixture::Field kPredictionClientFields[]{
    {"health", 0x8000'0004U, 0U, 10U},
    {"velocity[0]", 0x8000'0004U, 4U, 16U, 32'000U, 4'000U},
    {"velocity[1]", 0x8000'0004U, 8U, 16U, 32'000U, 4'000U},
    {"velocity[2]", 0x8000'0004U, 12U, 16U, 32'000U, 4'000U},
    {"view_ofs[2]", 0x8000'0004U, 16U, 10U, 16'000U, 4'000U},
    {"origin[0]", 0x8000'0004U, 20U, 16U, 32'000U, 4'000U},
    {"origin[1]", 0x8000'0004U, 24U, 16U, 32'000U, 4'000U},
    {"origin[2]", 0x8000'0004U, 28U, 16U, 32'000U, 4'000U},
    {"flags", 0x0000'0008U, 32U, 32U},
    {"maxspeed", 0x0000'0004U, 36U, 16U, 10U, 1U},
    {"bInDuck", 0x0000'0008U, 44U, 1U},
    {"waterlevel", 0x0000'0008U, 48U, 2U},
    {"deadflag", 0x0000'0008U, 52U, 3U},
};

constexpr delta_fixture::Field kPredictionPlayerFields[]{
    {"origin[0]", 0x8000'0004U, 0U, 16U, 32'000U, 4'000U},
    {"origin[1]", 0x8000'0004U, 4U, 16U, 32'000U, 4'000U},
    {"origin[2]", 0x8000'0004U, 8U, 16U, 32'000U, 4'000U},
    {"angles[0]", 0x0000'0010U, 12U, 16U},
    {"angles[1]", 0x0000'0010U, 16U, 16U},
    {"angles[2]", 0x0000'0010U, 20U, 16U},
    {"modelindex", 0x0000'0008U, 24U, 16U},
    {"movetype", 0x0000'0008U, 28U, 4U},
    {"friction", 0x8000'0004U, 32U, 16U, 8U, 1U},
    {"usehull", 0x0000'0008U, 36U, 1U},
    {"gravity", 0x8000'0004U, 40U, 16U, 32U, 1U},
    {"basevelocity[0]", 0x8000'0004U, 44U, 16U, 8U, 1U},
    {"basevelocity[1]", 0x8000'0004U, 48U, 16U, 8U, 1U},
    {"basevelocity[2]", 0x8000'0004U, 52U, 16U, 8U, 1U},
    {"spectator", 0x0000'0008U, 56U, 1U},
};

const std::vector<delta_fixture::Field> kPredictionEffectsPlayerFields = [] {
  std::vector<delta_fixture::Field> fields{std::begin(kPredictionPlayerFields), std::end(kPredictionPlayerFields)};
  fields.push_back({"effects", 0x0000'0008U, 60U, 8U});
  return fields;
}();

[[nodiscard]] std::vector<std::vector<std::byte>> live_schemas(
    const bool with_usercmd = false,
    const bool prediction_fields = false, const bool boxes = false, const bool player_effects = false) {
  auto schemas = std::vector<std::vector<std::byte>>{
      delta_fixture::schema("entity_state_t", boxes ? std::span<const delta_fixture::Field>{kBoxEntityFields} : std::span<const delta_fixture::Field>{kLiveEntityFields}),
      delta_fixture::schema("entity_state_player_t", prediction_fields
          ? (player_effects ? std::span<const delta_fixture::Field>{kPredictionEffectsPlayerFields}
                            : std::span<const delta_fixture::Field>{kPredictionPlayerFields})
          : std::span<const delta_fixture::Field>{kLiveEntityFields}),
      delta_fixture::schema("custom_entity_state_t", kLiveEntityFields),
      delta_fixture::schema("clientdata_t", prediction_fields
          ? std::span<const delta_fixture::Field>{kPredictionClientFields}
          : std::span<const delta_fixture::Field>{kLiveClientFields}),
      delta_fixture::schema("weapon_data_t", kLiveWeaponFields),
  };
  if (with_usercmd)
    schemas.push_back(delta_fixture::schema(
        "usercmd_t", hlclient::test::usercmd_fixture::kExactFields));
  return schemas;
}

[[nodiscard]] std::vector<std::byte> live_initial_semantic_payload(
    const bool with_usercmd = false,
    const bool prediction_fields = false, const bool boxes = false, const bool damage_message = false,
    const bool player_effects = false) {
  std::vector<std::byte> post_delta;
  move_fixture::append_move_vars_body(post_delta);
  move_fixture::append_confirmed_controls(post_delta);
  if(damage_message) {
    post_delta.insert(post_delta.end(),{std::byte{39U},std::byte{66U},std::byte{12U}});
    constexpr std::string_view name{"Damage"};
    for(std::size_t i=0;i<16U;++i)post_delta.push_back(i<name.size() ? static_cast<std::byte>(name[i]) : std::byte{0U});
  }
  post_delta.insert(post_delta.end(),
                    user_fixture::kExactUserInfoMessage.begin(),
                    user_fixture::kExactUserInfoMessage.end());
  return delta_fixture::service_payload(live_schemas(with_usercmd, prediction_fields,boxes,player_effects),
                                        goldsrc::kMoveVarsOpcode, post_delta);
}

void write_live_delta(
    delta_fixture::BitWriter &writer, const std::uint8_t mask,
    const std::span<const std::pair<std::uint32_t, std::size_t>> values = {}) {
  writer.write(mask == 0U ? 0U : 1U, 3U);
  if (mask == 0U) {
    return;
  }
  writer.write(mask, 8U);
  for (const auto [value, width] : values) {
    writer.write(value, width);
  }
}

[[nodiscard]] constexpr std::uint32_t goldsrc_signed_positive(
    const std::uint32_t magnitude) noexcept {
  return magnitude << 1U;
}

[[nodiscard]] std::vector<std::byte>
live_runtime_record(const float server_time, const std::uint32_t health,
                    const std::uint32_t origin_x) {
  delta_fixture::BitWriter writer;
  // Exercise the post-spawn controls that stock can place ahead of the
  // canonical time/clientdata/entity sequence. The independent literal
  // svc_sound fixture lives in test_runtime_control_decoder.cpp.
  writer.write(
      static_cast<std::uint8_t>(goldsrc::RuntimeControlOpcode::svc_weaponanim),
      8U);
  writer.write(7U, 8U);
  writer.write(2U, 8U);
  writer.write(
      static_cast<std::uint8_t>(goldsrc::RuntimeControlOpcode::svc_sound), 8U);
  writer.write(0U, 9U);  // field mask
  writer.write(0U, 3U);  // channel
  writer.write(0U, 11U); // entity
  writer.write(0U, 8U);  // sound index
  writer.write(0U, 3U);  // origin-component presence
  writer.align_zero();

  writer.write(7U, 8U);
  writer.write(std::bit_cast<std::uint32_t>(server_time), 32U);

  writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
  writer.write(0U, 1U);
  const std::pair<std::uint32_t, std::size_t> client_values[]{
      {goldsrc_signed_positive(health), 10U},
  };
  write_live_delta(writer, 0x01U, client_values);
  writer.write(0U, 1U);
  writer.align_zero();

  writer.write(goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 8U);
  writer.write(1U, 16U);
  writer.write(0U, 1U);
  writer.write(0U, 1U);
  writer.write(2U, 6U);
  writer.write(0U, 1U);
  writer.write(0U, 1U);
  const std::pair<std::uint32_t, std::size_t> entity_values[]{
      {goldsrc_signed_positive(origin_x), 16U},
  };
  write_live_delta(writer, 0x01U, entity_values);
  writer.write(0U, 16U);
  writer.align_zero();
  return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> live_entity_only_record(
    const std::uint32_t origin_x) {
  delta_fixture::BitWriter writer;
  writer.write(goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 8U);
  writer.write(1U, 16U);
  writer.write(0U, 1U);
  writer.write(0U, 1U);
  writer.write(2U, 6U);
  writer.write(0U, 1U);
  writer.write(0U, 1U);
  const std::pair<std::uint32_t, std::size_t> entity_values[]{
      {goldsrc_signed_positive(origin_x), 16U},
  };
  write_live_delta(writer, 0x01U, entity_values);
  writer.write(0U, 16U);
  writer.align_zero();
  return writer.bytes();
}

[[nodiscard]] std::vector<std::byte> live_clientdata_zero_change_record(
    const float server_time) {
  delta_fixture::BitWriter writer;
  writer.write(7U, 8U);
  writer.write(std::bit_cast<std::uint32_t>(server_time), 32U);
  writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
  writer.write(0U, 1U);
  write_live_delta(writer, 0U);
  writer.write(0U, 1U);
  writer.align_zero();
  return writer.bytes();
}

// An independently packed coherent airborne player record. The world-only
// collision fixture is empty, so flags=0 is an explicit airborne contract.
[[nodiscard]] std::vector<std::byte> live_prediction_air_record(
    const float server_time, const std::uint32_t origin_x,
    const std::uint32_t origin_eighths = 0U,
    const std::uint32_t origin_z_eighths = 512U,
    const std::uint32_t health = 100U,
    const std::uint32_t dead_flag = 0U,
    const bool brush = false, const std::optional<unsigned> lift_z_eighths = {}, const bool boxes=false,
    const std::optional<unsigned> unrelated_box_x_eighths = {}, const bool include_clientdata = true,
    const std::optional<unsigned> velocity_z_eighths = {},
    const bool explicitly_grounded = false) {
  delta_fixture::BitWriter writer;
  writer.write(7U, 8U);
  writer.write(std::bit_cast<std::uint32_t>(server_time), 32U);
  if (include_clientdata) {
  writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
  writer.write(0U, 1U);
  writer.write(2U, 3U);
  writer.write(0x1fffU, 16U);
  writer.write(goldsrc_signed_positive(health), 10U);
  writer.write(0U, 16U);
  writer.write(0U, 16U);
  writer.write(velocity_z_eighths
      ? goldsrc_signed_positive(*velocity_z_eighths) : 0U, 16U);
  writer.write(goldsrc_signed_positive(112U), 10U);
  writer.write(goldsrc_signed_positive(origin_x * 8U + origin_eighths), 16U);
  writer.write(0U, 16U);
  writer.write(goldsrc_signed_positive(origin_z_eighths), 16U);
  writer.write(lift_z_eighths || boxes || explicitly_grounded
      ? 1U<<9U : 0U, 32U);
  writer.write(3'200U, 16U);
  writer.write(0U, 1U);
  writer.write(0U, 2U);
  writer.write(dead_flag, 3U);
  writer.write(0U, 1U);
  writer.align_zero();
  }

  writer.write(goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 8U);
  writer.write((boxes ? 4U : brush ? 2U : 1U) + static_cast<unsigned>(unrelated_box_x_eighths.has_value()), 16U);
  writer.write(0U, 1U);
  writer.write(0U, 1U);
  writer.write(1U, 6U);
  writer.write(0U, 1U);
  writer.write(0U, 1U);
  writer.write(2U, 3U);
  writer.write(0x7f80U, 16U);
  writer.write(3U, 4U);
  writer.write(goldsrc_signed_positive(8U), 16U);
  writer.write(0U, 1U);
  writer.write(goldsrc_signed_positive(32U), 16U);
  writer.write(0U, 16U);
  writer.write(0U, 16U);
  writer.write(0U, 16U);
  writer.write(0U, 1U);
  if (brush) {
    // Keep the brush outside the server's reserved player entity range.
    writer.write(0U,1U); writer.write(0U,1U); writer.write(41U,6U);
    writer.write(0U,1U); writer.write(0U,1U);
    writer.write(2U,3U); writer.write(0x01ffU,16U);
    for (unsigned component=0; component<6; ++component)
      writer.write(component==2 && lift_z_eighths ? goldsrc_signed_positive(*lift_z_eighths) : 0U,16U);
    writer.write(27U,16U); writer.write(4U,3U); writer.write(7U,4U);
  }
  if(boxes) for(unsigned box=0;box<3;++box) {
    if(box)writer.write(1,1);else{writer.write(0,1);writer.write(0,1);writer.write(41,6);}
    writer.write(0,1);writer.write(0,1);writer.write(2,3);writer.write(0xffff,16);
    writer.write(goldsrc_signed_positive((40+box*4)*8),16);writer.write(0,16);
    writer.write(goldsrc_signed_positive(32),16);for(unsigned i=0;i<3;++i)writer.write(0,16);
    writer.write(0,16);writer.write(2,3);writer.write(10,4);writer.write(2,8);
    for(unsigned i=0;i<3;++i)writer.write(goldsrc_signed_positive(32)|1U,16);
    for(unsigned i=0;i<3;++i)writer.write(goldsrc_signed_positive(32),16);
  }
  if (unrelated_box_x_eighths) {
    // Entity 43 after the existing lift 42, from the independently authored
    // ordinary BBOX schema. It is not a player or a rendered-model dependency.
    writer.write(1U,1U); writer.write(0U,1U); writer.write(0U,1U);
    writer.write(2U,3U); writer.write(0xffffU,16U);
    writer.write(goldsrc_signed_positive(*unrelated_box_x_eighths),16U);
    writer.write(0U,16U); writer.write(goldsrc_signed_positive(60U*8U),16U);
    for(unsigned i=0;i<3;++i) writer.write(0U,16U);
    writer.write(0U,16U); writer.write(2U,3U); writer.write(3U,4U); writer.write(0U,8U);
    for(unsigned i=0;i<3;++i) writer.write(goldsrc_signed_positive(4U*8U)|1U,16U);
    for(unsigned i=0;i<3;++i) writer.write(goldsrc_signed_positive(4U*8U),16U);
  }
  writer.write(0U, 16U);
  writer.align_zero();
  return writer.bytes();
}

[[nodiscard]] std::vector<std::byte>
live_baseline_and_runtime_payload(const float server_time,
                                  const std::uint32_t health,
                                  const std::uint32_t origin_x,
                                  const bool with_player_baseline = false) {
  delta_fixture::BitWriter baseline;
  if (with_player_baseline) {
    baseline.write(1U, 11U);
    baseline.write(0U, 2U);
    write_live_delta(baseline, 0U);
  }
  baseline.write(2U, 11U);
  baseline.write(0U, 2U);
  write_live_delta(baseline, 0U);
  if (with_player_baseline) {
    baseline.write(42U,11U); baseline.write(0U,2U);
    write_live_delta(baseline,0U);
  }
  baseline.write(0xffffU, 16U);
  baseline.write(0U, 6U);
  baseline.align_zero();

  std::vector<std::byte> payload{
      static_cast<std::byte>(goldsrc::kGoldSrcSvcSpawnBaselineOpcode),
  };
  payload.insert(payload.end(), baseline.bytes().begin(),
                 baseline.bytes().end());
  const auto runtime = live_runtime_record(server_time, health, origin_x);
  payload.insert(payload.end(), runtime.begin(), runtime.end());
  payload.push_back(
      static_cast<std::byte>(goldsrc::RuntimeControlOpcode::svc_signonnum));
  payload.push_back(std::byte{1U});
  return payload;
}

[[nodiscard]] std::vector<std::byte> normal_fragment_packet(
    const std::uint32_t packet_sequence, const std::uint32_t acknowledgement,
    const bool reliable_acknowledgement, const std::uint16_t fragment_index,
    const std::uint16_t fragment_count,
    std::vector<std::byte> fragment_payload) {
  REQUIRE_FALSE(fragment_payload.empty());
  REQUIRE(fragment_payload.size() <=
          goldsrc::kStockProtocol48NormalFragmentChunkSize);
  const auto fragment_size = fragment_payload.size();
  goldsrc::NetchanFragmentSlots fragments;
  fragments[0U] = goldsrc::NetchanFragmentDescriptor{
      0U, (static_cast<std::uint32_t>(fragment_index) << 16U) | fragment_count,
      0U, static_cast<std::uint16_t>(fragment_size),
      0U,
  };
  goldsrc::ServerToClientNetchanPacket packet{
      goldsrc::NetchanHeader{
          goldsrc::NetchanSequenceWord{
              sequence(packet_sequence),
              goldsrc::NetchanSequenceFlags{true, true},
          },
          goldsrc::NetchanAcknowledgementWord{sequence(acknowledgement),
                                              reliable_acknowledgement},
      },
      std::move(fragments),
      std::move(fragment_payload),
      fragment_size,
  };
  auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
  REQUIRE(encoded);
  REQUIRE(encoded.datagram);
  return std::move(*encoded.datagram);
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket
require_latest_payload(const FakeTransport &transport,
                       const std::span<const std::byte> expected) {
  for (auto item = transport.sent.rbegin(); item != transport.sent.rend();
       ++item) {
    const auto packet = decode_sent(*item);
    if (std::ranges::equal(packet.payload, expected)) {
      return packet;
    }
  }
  FAIL("Expected client semantic payload was not transmitted");
}

struct LiveInitialDrive {
  goldsrc::ClientToServerNetchanPacket transition_request;
  std::uint32_t last_server_sequence{0U};
  goldsrc::LiveRuntimeStageTimePoint now{};
};

[[nodiscard]] LiveInitialDrive
drive_live_initial(goldsrc::LiveRuntimeStage &stage, FakeTransport &transport,
                   const network::NetworkAddress remote,
                   const goldsrc::LiveRuntimeStageTimePoint epoch,
                   const bool compressed, const bool fragmented,
                   const bool with_usercmd = false,
                   const bool prediction_fields = false, const bool boxes=false, const bool damage_message=false,
                   const bool player_effects=false) {
  REQUIRE(stage.start(epoch, transport.local));
  stage.update(epoch + 1ms);
  REQUIRE_FALSE(transport.sent.empty());
  const auto initial_request = decode_sent(transport.sent.front());

  auto initial_payload = live_initial_semantic_payload(
      with_usercmd, prediction_fields,boxes,damage_message,player_effects);
  if (compressed) {
    initial_payload = service_envelope(initial_payload);
  }

  std::uint32_t server_sequence = 1U;
  auto now = epoch + 2ms;
  if (!fragmented) {
    transport.queue(
        remote, server_packet(server_sequence, true,
                              initial_request.header.sequence.sequence.value(),
                              initial_request.header.sequence.flags.reliable,
                              std::move(initial_payload)));
    stage.update(now);
  } else {
    const auto chunk = goldsrc::kStockProtocol48NormalFragmentChunkSize;
    REQUIRE(initial_payload.size() > chunk);
    const auto count_value = (initial_payload.size() + chunk - 1U) / chunk;
    REQUIRE(count_value <= (std::numeric_limits<std::uint16_t>::max)());
    const auto count = static_cast<std::uint16_t>(count_value);
    for (std::uint16_t index = 1U; index <= count; ++index) {
      const auto offset = static_cast<std::size_t>(index - 1U) * chunk;
      const auto length = (std::min)(chunk, initial_payload.size() - offset);
      transport.queue(
          remote,
          normal_fragment_packet(
              server_sequence++,
              initial_request.header.sequence.sequence.value(),
              initial_request.header.sequence.flags.reliable, index, count,
              std::vector<std::byte>{
                  initial_payload.begin() + static_cast<std::ptrdiff_t>(offset),
                  initial_payload.begin() +
                      static_cast<std::ptrdiff_t>(offset + length)}));
      stage.update(now++);
    }
    --server_sequence;
  }

  constexpr std::array transition_semantic{
      std::byte{0x03U}, std::byte{'s'}, std::byte{'e'},
      std::byte{'n'},   std::byte{'d'}, std::byte{'r'},
      std::byte{'e'},   std::byte{'s'}, std::byte{0U},
  };
  stage.update(++now);
  const auto transition =
      require_latest_payload(transport, transition_semantic);
  return {transition, server_sequence, now};
}

[[nodiscard]] goldsrc::LiveRuntimeStageConfig live_test_config() {
  goldsrc::LiveRuntimeStageConfig config;
  config.game_client = std::make_shared<hlclient::game_api::GameClientHost>(
      hlclient::games::halflife::make_half_life_client_module());
  // Reproduce the production coordinator replacement that previously
  // discarded the nested live-session compatibility policy.
  config.resource_response = test_config();
  goldsrc::apply_live_runtime_compatibility_profile(config);
  auto &driver =
      config.resource_response.resource_list.transition.user_info
          .movement_environment.delta.pre_resource.initial_signon.driver;
  driver.channel_inactivity_timeout = 10s;
  driver.fragment_transfer_timeout = 5s;
  config.stable_interval = 2s;
  config.timeout = 10s;
  config.maximum_driver_events_per_update = 128U;
  config.maximum_events = 256U;
  return config;
}

struct LiveVisualReadyDrive final {
  std::uint32_t next_server_sequence{};
  goldsrc::LiveRuntimeStageTimePoint ready_at{};
};

// Full production owner, including the typed resource response, spawn and
// sendents handoff. Failure tests below inject only transport observations;
// they never manufacture a retained-driver pointer or a ready usercmd stage.
[[nodiscard]] LiveVisualReadyDrive drive_live_visual_ready(
    goldsrc::LiveRuntimeStage& stage, FakeTransport& transport,
    const network::NetworkAddress remote,
    const goldsrc::LiveRuntimeStageTimePoint epoch) {
  const auto initial = drive_live_initial(
      stage, transport, remote, epoch, false, false, true);
  const auto resource_sequence = initial.last_server_sequence + 1U;
  transport.queue(remote, server_packet(resource_sequence, false,
      initial.transition_request.header.sequence.sequence.value(), false,
      service_envelope(resource_semantic_payload())));
  transport.queue(remote, server_packet(resource_sequence + 1U, false,
      initial.transition_request.header.sequence.sequence.value(), false,
      {std::byte{1U}}));
  for (std::size_t step = 1U; step <= 8U; ++step)
    stage.update(initial.now + std::chrono::milliseconds{step});
  constexpr std::array response_bytes{
      std::byte{5U}, std::byte{0U}, std::byte{0U}};
  const auto response = require_latest_payload(transport, response_bytes);
  const auto runtime_at = initial.now + 9ms;
  transport.queue(remote, server_packet(resource_sequence + 2U, false,
      response.header.sequence.sequence.value(),
      response.header.sequence.flags.reliable));
  stage.update(runtime_at);
  const auto spawn_request = goldsrc::StockSpawnRequestBuilder::build(
      0x1234'5678U,
      goldsrc::decode_stock_server_world_map_crc(0xdead'beefU, 0U));
  REQUIRE(spawn_request);
  REQUIRE(spawn_request.encoding);
  const auto spawn = require_latest_payload(
      transport, spawn_request.encoding->semantic_bytes());
  transport.queue(remote, server_packet(resource_sequence + 3U, true,
      spawn.header.sequence.sequence.value(), false,
      live_baseline_and_runtime_payload(100.0F, 100U, 80U, true)));
  stage.update(runtime_at + 1ms);
  stage.update(runtime_at + 2ms);
  const auto sendents = require_latest_payload(transport,
      goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
  transport.queue(remote, server_packet(resource_sequence + 4U, false,
      sendents.header.sequence.sequence.value(),
      sendents.header.sequence.flags.reliable,
      service_envelope(live_runtime_record(101.0F, 90U, 96U))));
  stage.update(runtime_at + 3ms);
  const auto ready_at = runtime_at + 2s + 2ms;
  stage.update(ready_at);
  REQUIRE(stage.live_visual_input_ready());
  REQUIRE_FALSE(stage.terminal());
  while (stage.poll_event()) {}
  return {resource_sequence + 5U, ready_at};
}

TEST_CASE("Live owner retains exact terminal driver cause across teardown",
          "[e311][driver-failure][goldsrc][live-runtime][integration][diagnostics]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'866U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 80s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source =
      goldsrc::LiveVisualControlInputSource::scripted_speed_check;
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  const auto ready_drive = drive_live_visual_ready(stage, transport, remote, epoch);
  const auto latest_sent = decode_sent(transport.sent.back());
  auto failure_at = ready_drive.ready_at + 1ms;
  auto expected_state = goldsrc::LiveRuntimeStageState::protocol_error;
  auto expected_event = goldsrc::LiveRuntimeStageEventType::protocol_error;
  auto expected_driver = goldsrc::NetchanDriverErrorCode::malformed_packet;
  std::optional<goldsrc::NetchanPacketErrorCode> expected_packet;
  std::optional<goldsrc::NetchanSessionErrorCode> expected_session;
  std::optional<network::SocketNativeError> expected_native;
  std::optional<goldsrc::NetchanFailurePacketMetadata> expected_metadata;

  SECTION("Malformed exact endpoint packet retains codec rejection") {
    transport.queue(remote,
        std::vector<std::byte>(goldsrc::kNetchanHeaderSize - 1U));
    expected_packet = goldsrc::NetchanPacketErrorCode::datagram_too_short;
    expected_metadata.emplace();
    expected_metadata->datagram_size = goldsrc::kNetchanHeaderSize - 1U;
    expected_metadata->classification = goldsrc::NetchanDatagramClassification::malformed;
  }
  SECTION("Connectionless traffic remains rejected in the active channel") {
    transport.queue(remote, {std::byte{0xffU}, std::byte{0xffU},
        std::byte{0xffU}, std::byte{0xffU}, std::byte{'A'}, std::byte{0U}});
    expected_driver = goldsrc::NetchanDriverErrorCode::unexpected_connectionless_packet;
    expected_packet = goldsrc::NetchanPacketErrorCode::connectionless_packet;
    expected_metadata.emplace();
    expected_metadata->datagram_size = 6U;
    expected_metadata->classification = goldsrc::NetchanDatagramClassification::connectionless;
    expected_metadata->connectionless_opcode = static_cast<std::uint8_t>('A');
  }
  SECTION("Receive failure retains transport ownership and network classification") {
    expected_native = network::SocketNativeError{
        network::SocketNativeErrorDomain::winsock, 10054U};
    transport.incoming.push_back({network::DatagramTransportReceiveStatus::error,
        std::nullopt, std::nullopt, 0U, "synthetic_live_receive_failure",
        *expected_native});
    expected_driver = goldsrc::NetchanDriverErrorCode::receive_failed;
    expected_state = goldsrc::LiveRuntimeStageState::network_error;
    expected_event = goldsrc::LiveRuntimeStageEventType::network_error;
  }
  SECTION("Required acknowledgement send failure remains a network error") {
    transport.queue_send_status(network::DatagramSendStatus::error);
    auto acknowledgement_trigger = server_packet(ready_drive.next_server_sequence, true,
        latest_sent.header.sequence.sequence.value(),
        latest_sent.header.sequence.flags.reliable, {std::byte{1U}});
    // This valid RX packet only triggers the required acknowledgement. The
    // failure is TX, so it must not inherit an RX-rejection packet snapshot.
    transport.queue(remote, std::move(acknowledgement_trigger));
    expected_driver = goldsrc::NetchanDriverErrorCode::send_failed;
    expected_state = goldsrc::LiveRuntimeStageState::network_error;
    expected_event = goldsrc::LiveRuntimeStageEventType::network_error;
  }
  SECTION("Future acknowledgement retains the nested session rejection") {
    auto invalid_acknowledgement = server_packet(ready_drive.next_server_sequence, false,
        latest_sent.header.sequence.sequence.value() + 100U,
        latest_sent.header.sequence.flags.reliable, {std::byte{1U}});
    expected_metadata.emplace();
    expected_metadata->datagram_size = invalid_acknowledgement.size();
    expected_metadata->classification = goldsrc::NetchanDatagramClassification::sequenced;
    expected_metadata->sequence = ready_drive.next_server_sequence;
    expected_metadata->acknowledgement = latest_sent.header.sequence.sequence.value() + 100U;
    transport.queue(remote, std::move(invalid_acknowledgement));
    expected_driver = goldsrc::NetchanDriverErrorCode::invalid_acknowledgement;
    expected_session = goldsrc::NetchanSessionErrorCode::future_acknowledgement;
  }
  SECTION("Incomplete normal transfer retains its real deadline classification") {
    transport.queue(remote, normal_fragment_packet(ready_drive.next_server_sequence,
        latest_sent.header.sequence.sequence.value(),
        latest_sent.header.sequence.flags.reliable, 1U, 2U,
        std::vector<std::byte>(goldsrc::kStockProtocol48NormalFragmentChunkSize,
                              std::byte{1U})));
    stage.update(failure_at);
    REQUIRE_FALSE(stage.terminal());
    while (stage.poll_event()) {}
    failure_at += 5s;
    expected_driver = goldsrc::NetchanDriverErrorCode::fragment_transfer_timed_out;
    expected_state = goldsrc::LiveRuntimeStageState::timed_out;
    expected_event = goldsrc::LiveRuntimeStageEventType::timeout;
  }

  stage.update(failure_at);
  REQUIRE(stage.terminal());
  REQUIRE(stage.error());
  CHECK(stage.error()->code == goldsrc::LiveRuntimeStageErrorCode::driver_failed);
  CHECK(stage.state() == expected_state);
  REQUIRE(stage.error()->driver_code);
  REQUIRE(stage.error()->driver_failure);
  CHECK(*stage.error()->driver_code == expected_driver);
  CHECK(stage.error()->driver_failure->code == expected_driver);
  CHECK(stage.error()->driver_failure->packet_code == expected_packet);
  CHECK(stage.error()->driver_failure->session_code == expected_session);
  CHECK(stage.error()->driver_failure->native_error.has_value() == expected_native.has_value());
  if (expected_native) {
    REQUIRE(stage.error()->driver_failure->native_error);
    CHECK(stage.error()->driver_failure->native_error->domain == expected_native->domain);
    CHECK(stage.error()->driver_failure->native_error->code == expected_native->code);
  }
  CHECK(stage.error()->driver_failure->packet_metadata.has_value() == expected_metadata.has_value());
  if (expected_metadata) {
    REQUIRE(stage.error()->driver_failure->packet_metadata);
    const auto& actual = *stage.error()->driver_failure->packet_metadata;
    CHECK(actual.datagram_size == expected_metadata->datagram_size);
    CHECK(actual.datagram_size_is_lower_bound == expected_metadata->datagram_size_is_lower_bound);
    CHECK(actual.classification == expected_metadata->classification);
    CHECK(actual.connectionless_opcode == expected_metadata->connectionless_opcode);
    CHECK(actual.sequence == expected_metadata->sequence);
    CHECK(actual.acknowledgement == expected_metadata->acknowledgement);
  }
  CHECK_FALSE(stage.error()->driver_failure->context.empty());
  CHECK(stage.usercmd_transmit_count() == 0U);
  CHECK(stage.cleanup_count() == 1U);
  const auto terminal_event = stage.poll_event();
  REQUIRE(terminal_event);
  CHECK(terminal_event->type == expected_event);
  CHECK_FALSE(stage.poll_event());
  const auto first_failure = *stage.error()->driver_failure;
  const auto sends_after_failure = transport.sent.size();
  stage.update(failure_at + 1ms);
  stage.cancel(failure_at + 2ms);
  CHECK(stage.cleanup_count() == 1U);
  CHECK(transport.sent.size() == sends_after_failure);
  REQUIRE(stage.error()->driver_failure);
  CHECK(stage.error()->driver_failure->code == first_failure.code);
  CHECK(stage.error()->driver_failure->context == first_failure.context);
  CHECK(stage.error()->driver_failure->packet_code == first_failure.packet_code);
  CHECK(stage.error()->driver_failure->session_code == first_failure.session_code);
  CHECK(stage.error()->driver_failure->reassembly_code == first_failure.reassembly_code);
  CHECK(stage.error()->driver_failure->native_error.has_value() == first_failure.native_error.has_value());
  CHECK(stage.error()->driver_failure->packet_metadata.has_value() == first_failure.packet_metadata.has_value());
}

TEST_CASE("Live owner retains driver cause before first map and signon publication",
          "[e311][driver-failure][goldsrc][live-runtime][integration][pre-signon]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'868U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 85s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source =
      goldsrc::LiveVisualControlInputSource::scripted_speed_check;
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  const auto initial = drive_live_initial(
      stage, transport, remote, epoch, false, false, true);
  const auto resource_sequence = initial.last_server_sequence + 1U;
  transport.queue(remote, server_packet(resource_sequence, false,
      initial.transition_request.header.sequence.sequence.value(), false,
      service_envelope(resource_semantic_payload())));
  transport.queue(remote, server_packet(resource_sequence + 1U, false,
      initial.transition_request.header.sequence.sequence.value(), false,
      {std::byte{1U}}));
  for (std::size_t step = 1U; step <= 8U; ++step)
    stage.update(initial.now + std::chrono::milliseconds{step});
  constexpr std::array response_bytes{
      std::byte{5U}, std::byte{0U}, std::byte{0U}};
  const auto response = require_latest_payload(transport, response_bytes);
  transport.queue(remote, server_packet(resource_sequence + 2U, false,
      response.header.sequence.sequence.value(),
      response.header.sequence.flags.reliable));
  const auto runtime_at = initial.now + 9ms;
  stage.update(runtime_at);
  REQUIRE_FALSE(stage.terminal());
  REQUIRE_FALSE(world.runtime_observation());
  while (stage.poll_event()) {}
  auto expected_driver = goldsrc::NetchanDriverErrorCode::malformed_packet;
  auto expected_packet = goldsrc::NetchanPacketErrorCode::datagram_too_short;
  SECTION("Short packet in the first continuation update") {
    transport.queue(remote,
        std::vector<std::byte>(goldsrc::kNetchanHeaderSize - 1U));
  }
  SECTION("Unexpected connectionless packet in the first continuation update") {
    transport.queue(remote, {std::byte{0xffU}, std::byte{0xffU},
        std::byte{0xffU}, std::byte{0xffU}, std::byte{'A'}, std::byte{0U}});
    expected_driver = goldsrc::NetchanDriverErrorCode::unexpected_connectionless_packet;
    expected_packet = goldsrc::NetchanPacketErrorCode::connectionless_packet;
  }
  stage.update(runtime_at + 1ms);
  REQUIRE(stage.error());
  CHECK(stage.error()->code == goldsrc::LiveRuntimeStageErrorCode::driver_failed);
  CHECK(stage.state() == goldsrc::LiveRuntimeStageState::protocol_error);
  REQUIRE(stage.error()->driver_code);
  REQUIRE(stage.error()->driver_failure);
  CHECK(*stage.error()->driver_code == expected_driver);
  CHECK(stage.error()->driver_failure->code == expected_driver);
  CHECK(stage.error()->driver_failure->packet_code == expected_packet);
  REQUIRE(stage.error()->driver_failure->packet_metadata);
  CHECK(stage.error()->driver_failure->packet_metadata->datagram_size ==
        (expected_packet == goldsrc::NetchanPacketErrorCode::datagram_too_short
             ? goldsrc::kNetchanHeaderSize - 1U : 6U));
  CHECK(stage.usercmd_transmit_count() == 0U);
  CHECK_FALSE(world.runtime_observation());
  CHECK(stage.cleanup_count() == 1U);
}

TEST_CASE("Live owner preserves sent move receipt before same pump RX rejection",
          "[e311][driver-failure][goldsrc][live-runtime][integration][usercmd][shared-driver]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'867U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 90s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source =
      goldsrc::LiveVisualControlInputSource::scripted_speed_check;
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  const auto ready_drive = drive_live_visual_ready(stage, transport, remote, epoch);
  const auto active_at = ready_drive.ready_at + 1ms;
  REQUIRE(stage.activate_live_visual_control(active_at));
  while (stage.poll_event()) {}
  const auto sends_before_move = transport.sent.size();
  auto expected_driver = goldsrc::NetchanDriverErrorCode::malformed_packet;
  auto expected_state = goldsrc::LiveRuntimeStageState::protocol_error;
  auto expected_move_receipts = std::size_t{1U};
  auto expected_transmission =
      goldsrc::GoldSrcUserCmdTransmissionErrorCode::driver_send_failed;
  SECTION("A successful move remains accounted when later RX is malformed") {
    transport.queue(remote,
        std::vector<std::byte>(goldsrc::kNetchanHeaderSize - 1U));
  }
  SECTION("A failed move send never manufactures a successful receipt") {
    transport.queue_send_status(network::DatagramSendStatus::error);
    expected_driver = goldsrc::NetchanDriverErrorCode::send_failed;
    expected_state = goldsrc::LiveRuntimeStageState::network_error;
    expected_move_receipts = 0U;
    // With no receipt, reference_ready() observes the terminal driver before
    // commit_prepared_move(). The owning driver still determines the primary
    // cause; retain this secondary prerequisite rejection accurately as well.
    expected_transmission =
        goldsrc::GoldSrcUserCmdTransmissionErrorCode::runtime_signon_evidence_pending;
  }
  stage.update(active_at + 20ms);
  REQUIRE(stage.terminal());
  REQUIRE(stage.error());
  CHECK(stage.error()->code == goldsrc::LiveRuntimeStageErrorCode::driver_failed);
  CHECK(stage.state() == expected_state);
  REQUIRE(stage.error()->driver_code);
  REQUIRE(stage.error()->driver_failure);
  CHECK(*stage.error()->driver_code == expected_driver);
  CHECK(stage.error()->driver_failure->code == expected_driver);
  CHECK(stage.error()->usercmd_transmission_code == expected_transmission);
  CHECK(stage.usercmd_transmit_count() == expected_move_receipts);
  CHECK(transport.sent.size() == sends_before_move + expected_move_receipts);
  const auto snapshot = stage.live_usercmd_snapshot();
  REQUIRE(snapshot);
  CHECK(snapshot->generated_command_count == 1U);
  CHECK(snapshot->new_command_submission_count == expected_move_receipts);
  CHECK(snapshot->transmitted_packet_count == expected_move_receipts);
  stage.update(active_at + 40ms);
  CHECK(stage.usercmd_transmit_count() == expected_move_receipts);
  CHECK(stage.cleanup_count() == 1U);
}

TEST_CASE("Resource response requires an explicit provider and sends no "
          "guessed bytes",
          "[goldsrc][resource-response][stage][provider][security]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'820U);
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
  std::size_t connection_releases = 0U;
  goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                             nullptr};
  const auto driven = drive_to_transition_request(
      stage, transport, remote, epoch,
      std::make_unique<CountingConnectionLifetime>(connection_releases));
  const auto sends_before_boundary = transport.sent.size();

  deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);

  REQUIRE(
      stage.state() ==
      goldsrc::ResourceClientResponseStageState::consistency_provider_required);
  REQUIRE(stage.terminal());
  REQUIRE(stage.error());
  CHECK(stage.error()->code ==
        goldsrc::ResourceClientResponseStageErrorCode::provider_required);
  REQUIRE(stage.error()->consistency_code);
  CHECK(*stage.error()->consistency_code ==
        consistency::ResourceConsistencyErrorCode::unavailable);
  CHECK_FALSE(stage.result());
  CHECK(stage.requirements_derivation_count() == 1U);
  CHECK(stage.provider_begin_count() == 0U);
  CHECK(stage.response_build_count() == 0U);
  CHECK(stage.response_queue_count() == 0U);
  CHECK_FALSE(stage.response_transmitted());
  CHECK(transport.sent.size() == sends_before_boundary);
  CHECK(stage.cleanup_count() == 1U);
  CHECK(connection_releases == 1U);

  const auto events = drain_events(stage);
  CHECK(event_count(events, goldsrc::ResourceClientResponseStageEventType::
                                resource_response_requirements_ready) == 1U);
  CHECK(event_count(events, goldsrc::ResourceClientResponseStageEventType::
                                consistency_provider_required) == 1U);
}

TEST_CASE(
    "Resource response builds and queues the independent semantic fixture once",
    "[goldsrc][resource-response][stage][provider][semantic-once]") {
  FakeTransport transport;
  FakeConsistencyProvider provider;
  const auto remote = network::NetworkAddress::loopback(27'821U);
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
  goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                             &provider};
  const auto driven =
      drive_to_transition_request(stage, transport, remote, epoch);

  deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
  const auto response = require_response_packet(transport);

  CHECK(stage.state() ==
        goldsrc::ResourceClientResponseStageState::waiting_for_response_ack);
  CHECK(stage.requirements_derivation_count() == 1U);
  CHECK(stage.provider_begin_count() == 1U);
  CHECK(stage.response_build_count() == 1U);
  CHECK(stage.response_queue_count() == 1U);
  CHECK(stage.response_transmitted());
  CHECK_FALSE(stage.response_acknowledged());
  CHECK(provider.begin_count == 1U);
  CHECK(provider.update_count == 1U);
  CHECK(provider.observed_material_count == 1U);
  CHECK(provider.observed_opaque_byte_count == 16U);
  CHECK(provider.lifetime_releases == 0U);

  const auto events = drain_events(stage);
  CHECK(event_count(events, goldsrc::ResourceClientResponseStageEventType::
                                resource_response_requirements_ready) == 1U);
  CHECK(event_count(events, goldsrc::ResourceClientResponseStageEventType::
                                resource_response_ready) == 1U);
  CHECK(event_count(events, goldsrc::ResourceClientResponseStageEventType::
                                resource_response_queued) == 1U);
  CHECK(event_count(events, goldsrc::ResourceClientResponseStageEventType::
                                resource_response_transmitted) == 1U);

  const auto sends_after_response = transport.sent.size();
  stage.update(epoch + 5ms);
  stage.update(epoch + 6ms);
  CHECK(transport.sent.size() == sends_after_response);
  CHECK(stage.response_build_count() == 1U);
  CHECK(stage.response_queue_count() == 1U);
  CHECK(response.fragment_payload_size == kExactTempdecalResponse.size());
}

TEST_CASE("Empty client-resource advertisement queues without a provider",
          "[goldsrc][resource-response][stage][empty][no-provider]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'831U);
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
  goldsrc::ResourceClientResponseStage stage{transport, remote,
                                             empty_response_config(), nullptr};
  const auto driven =
      drive_to_transition_request(stage, transport, remote, epoch);

  deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
  constexpr std::array expected{
      std::byte{goldsrc::kOpcode5ResourceResponseOpcode}, std::byte{0U},
      std::byte{0U}};
  const auto response = require_response_packet(transport, expected);

  CHECK(stage.state() ==
        goldsrc::ResourceClientResponseStageState::waiting_for_response_ack);
  CHECK(stage.requirements_derivation_count() == 1U);
  CHECK(stage.provider_begin_count() == 0U);
  CHECK(stage.response_build_count() == 1U);
  CHECK(stage.response_queue_count() == 1U);
  CHECK(stage.response_transmitted());
  CHECK(response.fragment_payload_size == expected.size());
}

TEST_CASE("Dropped resource response retries the same generation without "
          "semantic requeue",
          "[goldsrc][resource-response][stage][loss][retransmission]") {
  FakeTransport transport;
  FakeConsistencyProvider provider;
  const auto remote = network::NetworkAddress::loopback(27'822U);
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
  goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                             &provider};
  const auto driven =
      drive_to_transition_request(stage, transport, remote, epoch);
  deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
  const auto first_response = require_response_packet(transport);

  // The first response is intentionally not acknowledged. A reliable server
  // packet creates the ordinary outgoing ACK gap, while its wrong-generation
  // ACK cannot release or retry an equal-sequence response.
  transport.queue(remote,
                  server_packet(3U, true,
                                first_response.header.sequence.sequence.value(),
                                false));
  stage.update(epoch + 5ms);
  REQUIRE(transport.sent.size() >= 5U);
  const auto gap_packet = decode_sent(transport.sent.back());
  REQUIRE_FALSE(gap_packet.header.sequence.flags.reliable);
  CHECK(stage.response_queue_count() == 1U);
  CHECK_FALSE(stage.response_acknowledged());

  // Advancing the wrong-generation ACK past the latest response send asks
  // the persistent driver for a transport retry, not a semantic requeue.
  transport.queue(remote,
                  server_packet(4U, false,
                                gap_packet.header.sequence.sequence.value(),
                                false));
  stage.update(epoch + 6ms);
  const auto retry = require_response_packet(transport);
  CHECK(retry.header.sequence.sequence !=
        first_response.header.sequence.sequence);
  REQUIRE(retry.fragments[0U]);
  REQUIRE(first_response.fragments[0U]);
  CHECK(retry.fragments[0U]->packed_id() ==
        first_response.fragments[0U]->packed_id());
  CHECK(retry.payload == first_response.payload);
  CHECK(stage.response_build_count() == 1U);
  CHECK(stage.response_queue_count() == 1U);
  CHECK(stage.response_transmitted());
  CHECK_FALSE(stage.response_acknowledged());

  deliver_covering_continuation(stage, transport, remote, 5U, retry,
                                epoch + 7ms);
  REQUIRE(stage.result());
  const auto &lifecycle = stage.result()->reliable_lifecycle();
  CHECK(lifecycle.first_transmit_sequence() ==
        first_response.header.sequence.sequence.value());
  CHECK(lifecycle.most_recent_transmit_sequence() ==
        retry.header.sequence.sequence.value());
  CHECK(lifecycle.transmit_count() == 2U);
  CHECK(lifecycle.reliable_generation() != 0U);
}

TEST_CASE(
    "Dropped ACK waits without duplicate send then accepts one covering ACK",
    "[goldsrc][resource-response][stage][loss][ack]") {
  FakeTransport transport;
  FakeConsistencyProvider provider;
  const auto remote = network::NetworkAddress::loopback(27'823U);
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
  goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                             &provider};
  const auto driven =
      drive_to_transition_request(stage, transport, remote, epoch);
  deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
  const auto response = require_response_packet(transport);
  const auto sends_after_response = transport.sent.size();

  // Model a lost server ACK by providing no datagram at all.
  stage.update(epoch + 5ms);
  CHECK(stage.state() ==
        goldsrc::ResourceClientResponseStageState::waiting_for_response_ack);
  CHECK(transport.sent.size() == sends_after_response);
  CHECK(stage.response_queue_count() == 1U);

  transport.queue(
      remote, server_packet(3U, false,
                            response.header.sequence.sequence.value(), true));
  stage.update(epoch + 6ms);
  CHECK(stage.state() == goldsrc::ResourceClientResponseStageState::
                             waiting_for_server_continuation);
  CHECK(stage.response_acknowledged());
  CHECK(stage.response_queue_count() == 1U);
  CHECK_FALSE(stage.result());

  const auto next = next_server_semantic_payload();
  transport.queue(remote,
                  server_packet(4U, false,
                                response.header.sequence.sequence.value(), true,
                                service_envelope(next)));
  stage.update(epoch + 7ms);
  REQUIRE(stage.result());
  CHECK(stage.result()->reliable_lifecycle().transmit_count() == 1U);
  CHECK(stage.result()->reliable_lifecycle().acknowledgement().sequence ==
        response.header.sequence.sequence);
}

TEST_CASE("Stage leaves stale ACKs in flight and fails closed on future ACKs",
          "[goldsrc][resource-response][stage][ack][negative]") {
  SECTION("stale ACK is admitted by transport but cannot release response") {
    FakeTransport transport;
    FakeConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'824U);
    const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
    goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                               &provider};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
    const auto response = require_response_packet(transport);

    transport.queue(remote, server_packet(3U, false, 2U, true));
    stage.update(epoch + 5ms);
    CHECK(stage.state() ==
          goldsrc::ResourceClientResponseStageState::waiting_for_response_ack);
    CHECK_FALSE(stage.response_acknowledged());
    CHECK(stage.response_queue_count() == 1U);

    deliver_covering_continuation(stage, transport, remote, 4U, response,
                                  epoch + 6ms);
    REQUIRE(stage.result());
    CHECK(stage.result()->reliable_lifecycle().acknowledgement().disposition ==
          goldsrc::NetchanAcknowledgementDisposition::advanced);
  }

  SECTION("future ACK terminates through the driver without publication") {
    FakeTransport transport;
    FakeConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'825U);
    const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
    goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                               &provider};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
    const auto response = require_response_packet(transport);

    transport.queue(
        remote,
        server_packet(3U, false, response.header.sequence.sequence.value() + 1U,
                      true));
    stage.update(epoch + 5ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceClientResponseStageState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceClientResponseStageErrorCode::driver_failed);
    REQUIRE(stage.error()->driver_code);
    CHECK(*stage.error()->driver_code ==
          goldsrc::NetchanDriverErrorCode::invalid_acknowledgement);
    CHECK_FALSE(stage.response_acknowledged());
    CHECK_FALSE(stage.result());
    CHECK(stage.cleanup_count() == 1U);
  }
}

TEST_CASE("Covering ACK and BZ2 continuation publish the next server boundary",
          "[goldsrc][resource-response][stage][boundary][bz2]") {
  FakeTransport transport;
  FakeConsistencyProvider provider;
  const auto remote = network::NetworkAddress::loopback(27'826U);
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
  std::size_t connection_releases = 0U;
  goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                             &provider};
  const auto driven = drive_to_transition_request(
      stage, transport, remote, epoch,
      std::make_unique<CountingConnectionLifetime>(connection_releases));
  deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
  const auto response = require_response_packet(transport);
  CHECK(provider.lifetime_releases == 0U);
  CHECK(connection_releases == 0U);

  deliver_covering_continuation(stage, transport, remote, 3U, response,
                                epoch + 5ms);

  REQUIRE(
      stage.state() ==
      goldsrc::ResourceClientResponseStageState::next_server_boundary_reached);
  REQUIRE(stage.terminal());
  REQUIRE_FALSE(stage.error());
  REQUIRE(stage.result());
  const auto &result = *stage.result();
  CHECK(result.response().wire_name() == "tempdecal.wad");
  CHECK(result.response().byte_count() == 0x01020304U);
  CHECK_FALSE(result.source_carrier_geometry());
  CHECK_FALSE(result.concurrent_tail());
  CHECK(result.boundary().kind() ==
        goldsrc::PostResourceResponseBoundaryKind::opcode_at_payload_start);
  REQUIRE(result.boundary().opcode());
  CHECK(*result.boundary().opcode() == 3U);
  CHECK(result.boundary().byte_offset() == 0U);
  CHECK(result.boundary().remaining_byte_count() == 6U);
  CHECK(result.boundary().source_payload().direction ==
        goldsrc::NetchanDirection::server_to_client);
  CHECK(result.boundary().source_payload().source_sequence == 3U);
  CHECK(result.boundary().source_payload().decompressed);
  CHECK(result.boundary().source_payload().decoded_payload_byte_count == 7U);
  const auto &lifecycle = result.reliable_lifecycle();
  CHECK(lifecycle.fragmented());
  CHECK(lifecycle.fragment_count() == 1U);
  CHECK(lifecycle.transmit_count() == 1U);
  CHECK(lifecycle.first_transmit_sequence() ==
        response.header.sequence.sequence.value());
  CHECK(lifecycle.most_recent_transmit_sequence() ==
        response.header.sequence.sequence.value());
  CHECK(lifecycle.acknowledgement().sequence ==
        response.header.sequence.sequence);
  CHECK(lifecycle.acknowledgement().reliable);
  CHECK(stage.response_acknowledged());
  CHECK(stage.cleanup_count() == 1U);
  CHECK(provider.lifetime_releases == 1U);
  CHECK(connection_releases == 1U);

  const auto events = drain_events(stage);
  CHECK(event_count(events, goldsrc::ResourceClientResponseStageEventType::
                                resource_response_acknowledged) == 1U);
  CHECK(event_count(events, goldsrc::ResourceClientResponseStageEventType::
                                server_continuation_received) == 1U);
  CHECK(event_count(events, goldsrc::ResourceClientResponseStageEventType::
                                next_server_boundary_reached) == 1U);

  const auto sends_at_terminal = transport.sent.size();
  stage.update(epoch + 20ms);
  stage.cancel(epoch + 21ms);
  CHECK(transport.sent.size() == sends_at_terminal);
  CHECK(stage.cleanup_count() == 1U);
  CHECK(provider.lifetime_releases == 1U);
  CHECK(connection_releases == 1U);
}

TEST_CASE(
    "Cancellation is idempotent across provider and owning-session states",
    "[goldsrc][resource-response][stage][cancel][lifetime]") {
  SECTION("pending provider operation is cancelled exactly once") {
    FakeTransport transport;
    FakeConsistencyProvider provider{true};
    const auto remote = network::NetworkAddress::loopback(27'827U);
    const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
    std::size_t connection_releases = 0U;
    goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                               &provider};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch,
        std::make_unique<CountingConnectionLifetime>(connection_releases));
    deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
    REQUIRE(stage.state() == goldsrc::ResourceClientResponseStageState::
                                 waiting_for_consistency_provider);
    CHECK(provider.begin_count == 1U);
    CHECK(provider.update_count == 1U);
    CHECK(provider.cancel_count == 0U);
    CHECK(stage.response_queue_count() == 0U);

    stage.cancel(epoch + 5ms);
    stage.cancel(epoch + 6ms);
    stage.update(epoch + 7ms);
    CHECK(stage.state() ==
          goldsrc::ResourceClientResponseStageState::cancelled);
    CHECK(provider.cancel_count == 1U);
    CHECK(provider.lifetime_releases == 0U);
    CHECK(connection_releases == 1U);
    CHECK(stage.cleanup_count() == 1U);
  }

  SECTION("completed provider session is held until stage cancellation") {
    FakeTransport transport;
    FakeConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'828U);
    const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 1s;
    std::size_t connection_releases = 0U;
    goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                               &provider};
    const auto driven = drive_to_transition_request(
        stage, transport, remote, epoch,
        std::make_unique<CountingConnectionLifetime>(connection_releases));
    deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
    static_cast<void>(require_response_packet(transport));
    CHECK(provider.lifetime_releases == 0U);
    CHECK(connection_releases == 0U);

    const auto sends_before_cancel = transport.sent.size();
    stage.cancel(epoch + 5ms);
    stage.cancel(epoch + 6ms);
    CHECK(stage.state() ==
          goldsrc::ResourceClientResponseStageState::cancelled);
    CHECK(transport.sent.size() == sends_before_cancel);
    CHECK(provider.lifetime_releases == 1U);
    CHECK(connection_releases == 1U);
    CHECK(stage.cleanup_count() == 1U);
    CHECK(stage.response_build_count() == 1U);
    CHECK(stage.response_queue_count() == 1U);
  }
}

TEST_CASE(
    "Prepared local provider releases its real handle on stage cancellation",
    "[goldsrc][resource-response][stage][local-provider][cancel][lifetime]") {
  hlclient::tests::ScopedLocalResourceTestRoot temporary;
  const auto target = temporary.game_path("valve") / "tempdecal.wad";
  temporary.write("valve", "tempdecal.wad", "stale");
  temporary.write("valve", "tempdecal.wad", "abc");
  auto roots =
      local::LocalResourceSearchRoots::create(temporary.path(), "valve");
  REQUIRE(roots);
  auto prepared =
      consistency::PreparedLocalResourceConsistencyProvider::prepare(
          std::move(*roots.roots));
  REQUIRE(prepared);
  CHECK_FALSE(can_open_local_writer(target));

  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'837U);
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 13s;
  goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                             prepared.provider.get()};
  const auto driven =
      drive_to_transition_request(stage, transport, remote, epoch);
  deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
  static_cast<void>(
      require_response_packet(transport, kExactPreparedLocalResponse));

  CHECK(prepared.provider->consumed());
  CHECK_FALSE(can_open_local_writer(target));
  CHECK(stage.response_build_count() == 1U);
  CHECK(stage.response_queue_count() == 1U);

  stage.cancel(epoch + 5ms);
  stage.cancel(epoch + 6ms);
  CHECK(stage.state() == goldsrc::ResourceClientResponseStageState::cancelled);
  CHECK(stage.cleanup_count() == 1U);
  CHECK(can_open_local_writer(target));

  const auto requirements = consistency::ResourceConsistencyRequirements::
      stock_opcode5_single_resource();
  REQUIRE(requirements);
  const auto second = prepared.provider->begin(*requirements);
  REQUIRE_FALSE(second);
  REQUIRE(second.error);
  CHECK(second.error->code ==
        consistency::ResourceConsistencyErrorCode::unavailable);
}

TEST_CASE("Provider wait and synchronous failure remain bounded and redacted",
          "[goldsrc][resource-response][stage][provider][security]") {
  const auto remote = network::NetworkAddress::loopback(27'828U);
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 8s;

  SECTION("pending provider reaches an independent manual-clock deadline") {
    FakeTransport transport;
    FakeConsistencyProvider provider{true};
    auto config = test_config();
    config.consistency_provider_timeout = 5ms;
    goldsrc::ResourceClientResponseStage stage{transport, remote, config,
                                               &provider};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
    REQUIRE(stage.state() == goldsrc::ResourceClientResponseStageState::
                                 waiting_for_consistency_provider);

    stage.update(epoch + 9ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceClientResponseStageState::timed_out);
    REQUIRE(stage.error());
    CHECK(stage.error()->code == goldsrc::ResourceClientResponseStageErrorCode::
                                     consistency_provider_failed);
    REQUIRE(stage.error()->consistency_code);
    CHECK(*stage.error()->consistency_code ==
          consistency::ResourceConsistencyErrorCode::timed_out);
    CHECK(provider.cancel_count == 1U);
    CHECK(stage.response_build_count() == 0U);
    CHECK(stage.response_queue_count() == 0U);
  }

  SECTION("provider diagnostics cannot cross into the CLI-visible error") {
    FakeTransport transport;
    SensitiveFailureProvider provider;
    goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                               &provider};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);

    REQUIRE(stage.error());
    CHECK(provider.begin_count == 1U);
    CHECK(stage.error()->context.find("tempdecal") == std::string::npos);
    CHECK(stage.error()->context.find("a0a1") == std::string::npos);
    CHECK(stage.response_queue_count() == 0U);
  }
}

TEST_CASE(
    "Response-stage bounds reject partial publication and stale continuation",
    "[goldsrc][resource-response][stage][bounds][security]") {
  SECTION("provider deadline validates its exact hard cap") {
    auto config = test_config();
    config.consistency_provider_timeout =
        goldsrc::kMaximumResourceConsistencyProviderTimeout;
    CHECK(goldsrc::valid_resource_client_response_stage_configuration(config));
    config.consistency_provider_timeout =
        goldsrc::kMaximumResourceConsistencyProviderTimeout + 1ms;
    CHECK_FALSE(
        goldsrc::valid_resource_client_response_stage_configuration(config));
    config.consistency_provider_timeout = 0ms;
    CHECK_FALSE(
        goldsrc::valid_resource_client_response_stage_configuration(config));
  }

  SECTION(
      "response ACK and post-ACK deadlines validate their exact hard caps") {
    auto config = test_config();
    config.response_acknowledgement_timeout =
        goldsrc::kMaximumResourceResponseAcknowledgementTimeout;
    config.post_ack_boundary_timeout =
        goldsrc::kMaximumPostResourceResponseBoundaryTimeout;
    CHECK(goldsrc::valid_resource_client_response_stage_configuration(config));

    config.response_acknowledgement_timeout =
        goldsrc::kMaximumResourceResponseAcknowledgementTimeout + 1ms;
    CHECK_FALSE(
        goldsrc::valid_resource_client_response_stage_configuration(config));
    config.response_acknowledgement_timeout =
        goldsrc::kMaximumResourceResponseAcknowledgementTimeout;
    config.post_ack_boundary_timeout =
        goldsrc::kMaximumPostResourceResponseBoundaryTimeout + 1ms;
    CHECK_FALSE(
        goldsrc::valid_resource_client_response_stage_configuration(config));

    config.post_ack_boundary_timeout =
        goldsrc::kMaximumPostResourceResponseBoundaryTimeout;
    config.response_acknowledgement_timeout = 0ms;
    CHECK_FALSE(
        goldsrc::valid_resource_client_response_stage_configuration(config));
    config.response_acknowledgement_timeout = 1ms;
    config.post_ack_boundary_timeout = 0ms;
    CHECK_FALSE(
        goldsrc::valid_resource_client_response_stage_configuration(config));
  }

  SECTION(
      "one and three event slots retain terminal backpressure publication") {
    for (const auto capacity : {1U, 3U}) {
      FakeTransport transport;
      FakeConsistencyProvider provider;
      const auto remote = network::NetworkAddress::loopback(
          static_cast<std::uint16_t>(27'829U + capacity));
      const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 9s;
      auto config = test_config();
      config.response.maximum_response_stage_events = capacity;
      goldsrc::ResourceClientResponseStage stage{transport, remote, config,
                                                 &provider};
      const auto driven =
          drive_to_transition_request(stage, transport, remote, epoch);
      deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);

      REQUIRE(stage.state() ==
              goldsrc::ResourceClientResponseStageState::backpressure);
      REQUIRE(stage.error());
      CHECK(stage.error()->code ==
            goldsrc::ResourceClientResponseStageErrorCode::event_backpressure);
      const auto events = drain_events(stage);
      CHECK(event_count(
                events,
                goldsrc::ResourceClientResponseStageEventType::backpressure) ==
            1U);
      CHECK(event_count(events, goldsrc::ResourceClientResponseStageEventType::
                                    resource_response_requirements_ready) ==
            (capacity == 1U ? 0U : 1U));
      CHECK(stage.response_build_count() == 0U);
      CHECK(stage.response_queue_count() == 0U);
    }
  }

  SECTION("unknown provider update state fails closed") {
    FakeTransport transport;
    InvalidUpdateProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'833U);
    const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 9s;
    goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                               &provider};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceClientResponseStageState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code == goldsrc::ResourceClientResponseStageErrorCode::
                                     consistency_provider_result_invalid);
    CHECK(provider.begin_count == 1U);
    CHECK(provider.update_count == 1U);
    CHECK(provider.cancel_count == 1U);
    CHECK(stage.response_build_count() == 0U);
    CHECK(stage.response_queue_count() == 0U);
  }

  SECTION("payload already queued before first response TX is rejected") {
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'830U);
    const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 10s;
    FakeConsistencyProvider provider;
    goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                               &provider};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    transport.queue(
        remote, server_packet(
                    2U, false, driven.request.header.sequence.sequence.value(),
                    false, service_envelope(resource_semantic_payload())));
    transport.queue(
        remote, server_packet(
                    3U, false, driven.request.header.sequence.sequence.value(),
                    false, service_envelope(next_server_semantic_payload())));

    stage.update(epoch + 4ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceClientResponseStageState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code == goldsrc::ResourceClientResponseStageErrorCode::
                                     server_payload_before_response_transmit);
    CHECK(stage.response_queue_count() == 1U);
    CHECK_FALSE(stage.response_transmitted());
  }
}

TEST_CASE(
    "Live response ordering consumes only decoder-proven pre-TX NOP and "
    "completes on covering ACK",
    "[goldsrc][resource-response][stage][ordering][live-policy][regression]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'846U);
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 10s;
  goldsrc::ResourceClientResponseStage stage{transport, remote,
                                             live_response_config()};
  const auto driven =
      drive_to_transition_request(stage, transport, remote, epoch);

  transport.queue(
      remote,
      server_packet(2U, false, driven.request.header.sequence.sequence.value(),
                    false, service_envelope(resource_semantic_payload())));
  transport.queue(remote,
                  server_packet(3U, false,
                                driven.request.header.sequence.sequence.value(),
                                false, {std::byte{1U}}));

  // RX and the first response TX intentionally share the same stage time.
  stage.update(epoch + 4ms);

  REQUIRE_FALSE(stage.terminal());
  CHECK(stage.response_queue_count() == 1U);
  CHECK(stage.response_transmitted());
  CHECK_FALSE(stage.response_acknowledged());
  REQUIRE(stage.payload_diagnostic());
  const auto &diagnostic = *stage.payload_diagnostic();
  CHECK(diagnostic.classification ==
        goldsrc::ResourceResponsePayloadClassification::pre_transmit_control);
  CHECK(
      diagnostic.receive_position ==
      goldsrc::ResourceResponseReceivePosition::before_first_response_transmit);
  CHECK(diagnostic.actual_opcode == 1U);
  CHECK(diagnostic.validated_message_boundary);
  CHECK(diagnostic.consumed_control_message_count == 1U);
  CHECK(diagnostic.last_successful_handoff_cursor == 1U);
  CHECK(diagnostic.response_transmitted == false);

  constexpr std::array empty_response{std::byte{0x05U}, std::byte{0x00U},
                                      std::byte{0x00U}};
  const auto response = require_response_packet(transport, empty_response);
  transport.queue(remote,
                  server_packet(4U, false,
                                response.header.sequence.sequence.value(),
                                response.header.sequence.flags.reliable));
  stage.update(epoch + 5ms);

  REQUIRE(stage.state() ==
          goldsrc::ResourceClientResponseStageState::response_completion_ready);
  REQUIRE(stage.response_acknowledged());
  REQUIRE(stage.result());
  CHECK_FALSE(stage.result()->boundary_optional());
  const auto events = drain_events(stage);
  CHECK(event_count(events, goldsrc::ResourceClientResponseStageEventType::
                                pre_transmit_control_consumed) == 1U);
  CHECK(event_count(events, goldsrc::ResourceClientResponseStageEventType::
                                response_completion_ready) == 1U);
}

TEST_CASE(
    "Live response ordering preserves would-block history and rejects unknown "
    "pre-TX framing",
    "[goldsrc][resource-response][stage][ordering][backpressure][security]") {
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 11s;

  SECTION(
      "control traffic during would-block does not fabricate transmission") {
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'847U);
    auto config = live_response_config();
    config.response_acknowledgement_timeout = 50ms;
    goldsrc::ResourceClientResponseStage stage{transport, remote, config};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);

    transport.queue_send_status(network::DatagramSendStatus::would_block);
    transport.queue(
        remote, server_packet(
                    2U, false, driven.request.header.sequence.sequence.value(),
                    false, service_envelope(resource_semantic_payload())));
    transport.queue(
        remote, server_packet(3U, false,
                              driven.request.header.sequence.sequence.value(),
                              false, {std::byte{1U}}));
    stage.update(epoch + 4ms);
    CHECK(stage.response_queue_count() == 1U);
    CHECK_FALSE(stage.response_transmitted());
    CHECK_FALSE(stage.terminal());

    transport.queue_send_status(network::DatagramSendStatus::would_block);
    transport.queue(
        remote, server_packet(4U, false,
                              driven.request.header.sequence.sequence.value(),
                              false, {std::byte{1U}}));
    stage.update(epoch + 5ms);
    CHECK(stage.response_queue_count() == 1U);
    CHECK_FALSE(stage.response_transmitted());
    REQUIRE(stage.payload_diagnostic());
    CHECK(stage.payload_diagnostic()->consumed_control_message_count == 2U);

    stage.update(epoch + 6ms);
    CHECK(stage.response_queue_count() == 1U);
    CHECK(stage.response_transmitted());
    CHECK_FALSE(stage.response_acknowledged());
  }

  SECTION("NOP followed by unknown opcode fails at the exact proven cursor") {
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'848U);
    goldsrc::ResourceClientResponseStage stage{transport, remote,
                                               live_response_config()};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    transport.queue(
        remote, server_packet(
                    2U, false, driven.request.header.sequence.sequence.value(),
                    false, service_envelope(resource_semantic_payload())));
    transport.queue(
        remote, server_packet(
                    3U, false, driven.request.header.sequence.sequence.value(),
                    false, {std::byte{1U}, std::byte{44U}, std::byte{45U}}));
    stage.update(epoch + 4ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceClientResponseStageState::protocol_error);
    REQUIRE(stage.error());
    CHECK(stage.error()->code == goldsrc::ResourceClientResponseStageErrorCode::
                                     pre_transmit_control_decode_failed);
    REQUIRE(stage.error()->payload_diagnostic);
    const auto &diagnostic = *stage.error()->payload_diagnostic;
    CHECK(diagnostic.actual_opcode == 44U);
    CHECK(diagnostic.cursor_byte_offset == 1U);
    CHECK(diagnostic.validated_message_boundary);
    CHECK(diagnostic.control_code ==
          goldsrc::RuntimeControlDecodeErrorCode::unsupported_opcode);
    CHECK(diagnostic.consumed_control_message_count == 1U);
    CHECK(diagnostic.last_successful_handoff_cursor == 1U);
    CHECK_FALSE(stage.response_transmitted());
  }

  SECTION("truncated known control fails without searching a later byte") {
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'849U);
    goldsrc::ResourceClientResponseStage stage{transport, remote,
                                               live_response_config()};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    transport.queue(
        remote, server_packet(
                    2U, false, driven.request.header.sequence.sequence.value(),
                    false, service_envelope(resource_semantic_payload())));
    transport.queue(
        remote, server_packet(3U, false,
                              driven.request.header.sequence.sequence.value(),
                              false, {std::byte{7U}}));
    stage.update(epoch + 4ms);

    REQUIRE(stage.error());
    CHECK(stage.error()->code == goldsrc::ResourceClientResponseStageErrorCode::
                                     pre_transmit_control_decode_failed);
    REQUIRE(stage.error()->payload_diagnostic);
    CHECK(stage.error()->payload_diagnostic->actual_opcode == 7U);
    CHECK(stage.error()->payload_diagnostic->cursor_byte_offset == 0U);
    CHECK(stage.error()->payload_diagnostic->control_code ==
          goldsrc::RuntimeControlDecodeErrorCode::truncated_body);
    CHECK_FALSE(stage.response_transmitted());
  }

  SECTION("bounded NOP count fails closed without response completion") {
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'850U);
    auto config = live_response_config();
    config.maximum_pre_transmit_control_payloads = 1U;
    goldsrc::ResourceClientResponseStage stage{transport, remote, config};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    transport.queue_send_status(network::DatagramSendStatus::would_block);
    transport.queue(
        remote, server_packet(
                    2U, false, driven.request.header.sequence.sequence.value(),
                    false, service_envelope(resource_semantic_payload())));
    transport.queue(
        remote, server_packet(3U, false,
                              driven.request.header.sequence.sequence.value(),
                              false, {std::byte{1U}}));
    stage.update(epoch + 4ms);
    REQUIRE_FALSE(stage.terminal());
    REQUIRE_FALSE(stage.response_transmitted());

    transport.queue(
        remote, server_packet(4U, false,
                              driven.request.header.sequence.sequence.value(),
                              false, {std::byte{1U}}));
    stage.update(epoch + 5ms);
    REQUIRE(stage.error());
    CHECK(stage.error()->code == goldsrc::ResourceClientResponseStageErrorCode::
                                     pre_transmit_control_overflow);
    CHECK_FALSE(stage.response_acknowledged());
    CHECK(stage.response_queue_count() == 1U);
  }
}

TEST_CASE(
    "Live response NOPs stay noncausal after the first response transmission",
    "[goldsrc][resource-response][stage][ordering][live-policy][startup-nop][regression]") {
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 10s;
  constexpr std::array empty_response{std::byte{5U}, std::byte{0U},
                                      std::byte{0U}};
  constexpr std::array nops{std::byte{1U}, std::byte{1U}};
  for (const bool compressed : {false, true}) {
    for (const bool numeric_covering_ack : {false, true}) {
      DYNAMIC_SECTION("compressed=" << compressed
                                    << " numeric_covering_ack="
                                    << numeric_covering_ack) {
        FakeTransport transport;
        const auto remote = network::NetworkAddress::loopback(27'860U);
        auto config = live_response_config();
        goldsrc::ResourceClientResponseStage stage{transport, remote, config};
        const auto driven =
            drive_to_transition_request(stage, transport, remote, epoch);
        deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
        REQUIRE_FALSE(stage.terminal());
        REQUIRE(stage.response_transmitted());
        const auto response = require_response_packet(transport, empty_response);
        const auto first_tx = response.header.sequence.sequence.value();
        const auto old_ack = driven.request.header.sequence.sequence.value();
        REQUIRE(first_tx > old_ack);
        const auto sends_before_controls = transport.sent.size();
        const auto ack = numeric_covering_ack ? first_tx : old_ack;
        // Numeric coverage alone does not acknowledge the reliable generation.
        const bool wrong_toggle = !response.header.sequence.flags.reliable;
        const auto wire = compressed
                              ? service_envelope(nops)
                              : std::vector<std::byte>{nops.begin(), nops.end()};
        static_cast<void>(drain_events(stage));

        for (std::uint32_t index = 0U; index < 3U; ++index) {
          // One reliable NOP produces a real later transport ACK packet.
          // The remaining controls are ordinary unreliable in-flight NOPs.
          transport.queue(remote, server_packet(12U + index, index == 0U, ack,
                                                 wrong_toggle, wire));
          stage.update(epoch + std::chrono::milliseconds{5U + index});
          REQUIRE_FALSE(stage.terminal());
          REQUIRE_FALSE(stage.error());
          CHECK(stage.state() == goldsrc::ResourceClientResponseStageState::
                                     waiting_for_response_ack);
          CHECK_FALSE(stage.response_acknowledged());
          CHECK_FALSE(stage.result());
          REQUIRE(stage.payload_diagnostic());
          const auto &diagnostic = *stage.payload_diagnostic();
          CHECK(diagnostic.classification ==
                goldsrc::ResourceResponsePayloadClassification::
                    post_transmit_control);
          CHECK(diagnostic.receive_position ==
                goldsrc::ResourceResponseReceivePosition::
                    after_first_response_transmit);
          CHECK(diagnostic.source_sequence == 12U + index);
          CHECK(diagnostic.source_acknowledgement == ack);
          CHECK(diagnostic.first_transmit_sequence == first_tx);
          CHECK(diagnostic.source_body_kind ==
                goldsrc::NetchanPayloadBodyKind::direct_body);
          CHECK(diagnostic.response_transmitted);
          CHECK_FALSE(diagnostic.response_acknowledged);
          CHECK(diagnostic.wire_encoding ==
                (compressed ? goldsrc::ResourceResponseWireEncodingKind::bzip2
                            : goldsrc::ResourceResponseWireEncodingKind::
                                  wire_uncompressed));
          CHECK(diagnostic.wire_byte_count == wire.size());
          CHECK(diagnostic.decoded_byte_count == nops.size());
          CHECK(diagnostic.actual_opcode == 1U);
          CHECK(diagnostic.validated_message_boundary);
          CHECK(diagnostic.last_successful_handoff_cursor == nops.size());
          CHECK(diagnostic.consumed_control_message_count == 2U * (index + 1U));
          CHECK(diagnostic.pending_payload_count == 0U);
          CHECK(diagnostic.pending_byte_count == 0U);
        }

        // A repeat update does not replay the previously consumed controls.
        stage.update(epoch + 7ms);
        REQUIRE_FALSE(stage.terminal());
        REQUIRE(stage.payload_diagnostic());
        CHECK(stage.payload_diagnostic()->consumed_control_message_count == 6U);
        const auto controls = drain_events(stage);
        CHECK(event_count(controls,
                          goldsrc::ResourceClientResponseStageEventType::
                              post_transmit_control_consumed) == 3U);
        CHECK(event_count(controls,
                          goldsrc::ResourceClientResponseStageEventType::
                              pre_transmit_control_consumed) == 0U);
        CHECK(event_count(controls,
                          goldsrc::ResourceClientResponseStageEventType::
                              server_continuation_received) == 0U);
        CHECK(event_count(controls,
                          goldsrc::ResourceClientResponseStageEventType::
                              resource_response_acknowledged) == 0U);
        CHECK(event_count(controls,
                          goldsrc::ResourceClientResponseStageEventType::
                              response_completion_ready) == 0U);

        // A genuine, advanced ACK with the correct toggle releases the same
        // generation. No NOP occupies pre_ack_payload_ or blocks ACK-only finish.
        // The first reliable NOP really emitted this later transport sequence;
        // do not invent a future ACK or change only a duplicate's reliable bit.
        REQUIRE(transport.sent.size() == sends_before_controls + 1U);
        const auto latest_tx =
            decode_sent(transport.sent.back()).header.sequence.sequence.value();
        INFO("first response TX=" << first_tx << " latest actual TX="
                                  << latest_tx << " numeric NOP ACK=" << ack);
        REQUIRE(latest_tx > first_tx);
        transport.queue(remote, server_packet(15U, false, latest_tx,
                                               response.header.sequence.flags.reliable));
        stage.update(epoch + 8ms);
        REQUIRE_FALSE(stage.error());
        REQUIRE(stage.state() ==
                goldsrc::ResourceClientResponseStageState::
                    response_completion_ready);
        REQUIRE(stage.response_acknowledged());
        REQUIRE(stage.result());
        CHECK_FALSE(stage.result()->boundary_optional());
        CHECK(stage.result()->reliable_lifecycle().first_transmit_sequence() ==
              first_tx);
        CHECK(stage.result()->reliable_lifecycle().acknowledgement().sequence ==
              sequence(latest_tx));
        CHECK(stage.response_build_count() == 1U);
        CHECK(stage.response_queue_count() == 1U);
        CHECK(stage.cleanup_count() == 1U);
        const auto completion = drain_events(stage);
        CHECK(event_count(completion,
                          goldsrc::ResourceClientResponseStageEventType::
                              resource_response_acknowledged) == 1U);
        CHECK(event_count(completion,
                          goldsrc::ResourceClientResponseStageEventType::
                              response_completion_ready) == 1U);
        CHECK(event_count(completion,
                          goldsrc::ResourceClientResponseStageEventType::
                              server_continuation_received) == 0U);
      }
    }
  }
}

TEST_CASE(
    "Delayed live NOP controls share bounded budgets and cannot extend ACK deadline",
    "[goldsrc][resource-response][stage][ordering][startup-nop][bounds][timeout]") {
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 11s;

  SECTION("pre and post transmission controls share the payload-count budget") {
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'861U);
    auto config = live_response_config();
    config.maximum_pre_transmit_control_payloads = 2U;
    goldsrc::ResourceClientResponseStage stage{transport, remote, config};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    const auto old_ack = driven.request.header.sequence.sequence.value();
    transport.queue(remote, server_packet(2U, false, old_ack, false,
        service_envelope(resource_semantic_payload())));
    transport.queue(remote, server_packet(3U, false, old_ack, false,
                                            {std::byte{1U}}));
    stage.update(epoch + 4ms);
    REQUIRE_FALSE(stage.terminal());
    REQUIRE(stage.response_transmitted());
    REQUIRE(stage.payload_diagnostic());
    CHECK(stage.payload_diagnostic()->consumed_control_message_count == 1U);

    transport.queue(remote, server_packet(12U, false, old_ack, false,
                                            {std::byte{1U}}));
    stage.update(epoch + 5ms);
    REQUIRE_FALSE(stage.terminal());
    REQUIRE(stage.payload_diagnostic());
    CHECK(stage.payload_diagnostic()->consumed_control_message_count == 2U);
    const auto accepted = drain_events(stage);
    CHECK(event_count(accepted,
                      goldsrc::ResourceClientResponseStageEventType::
                          pre_transmit_control_consumed) == 1U);
    CHECK(event_count(accepted,
                      goldsrc::ResourceClientResponseStageEventType::
                          post_transmit_control_consumed) == 1U);

    transport.queue(remote, server_packet(13U, false, old_ack, false,
                                            {std::byte{1U}}));
    stage.update(epoch + 6ms);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceClientResponseStageErrorCode::
              post_transmit_control_overflow);
    REQUIRE(stage.payload_diagnostic());
    CHECK(stage.payload_diagnostic()->consumed_control_message_count == 2U);
    CHECK_FALSE(stage.response_acknowledged());
    CHECK_FALSE(stage.result());
    const auto rejected = drain_events(stage);
    CHECK(event_count(rejected,
                      goldsrc::ResourceClientResponseStageEventType::
                          post_transmit_control_consumed) == 0U);
  }

  SECTION("wire-byte budget is shared without accepting the excess payload") {
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'862U);
    auto config = live_response_config();
    config.maximum_pre_transmit_control_bytes = 3U;
    goldsrc::ResourceClientResponseStage stage{transport, remote, config};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    const auto old_ack = driven.request.header.sequence.sequence.value();
    transport.queue(remote, server_packet(2U, false, old_ack, false,
        service_envelope(resource_semantic_payload())));
    transport.queue(remote, server_packet(3U, false, old_ack, false,
                                            {std::byte{1U}}));
    stage.update(epoch + 4ms);
    REQUIRE_FALSE(stage.terminal());
    transport.queue(remote, server_packet(12U, false, old_ack, false,
                                            {std::byte{1U}, std::byte{1U}}));
    stage.update(epoch + 5ms);
    REQUIRE_FALSE(stage.terminal());
    REQUIRE(stage.payload_diagnostic());
    CHECK(stage.payload_diagnostic()->consumed_control_message_count == 3U);
    static_cast<void>(drain_events(stage));
    transport.queue(remote, server_packet(13U, false, old_ack, false,
                                            {std::byte{1U}}));
    stage.update(epoch + 6ms);
    REQUIRE(stage.error());
    CHECK(stage.error()->code ==
          goldsrc::ResourceClientResponseStageErrorCode::
              post_transmit_control_overflow);
    REQUIRE(stage.payload_diagnostic());
    CHECK(stage.payload_diagnostic()->consumed_control_message_count == 3U);
    CHECK_FALSE(stage.response_acknowledged());
    CHECK_FALSE(stage.result());
    const auto rejected = drain_events(stage);
    CHECK(event_count(rejected,
                      goldsrc::ResourceClientResponseStageEventType::
                          post_transmit_control_consumed) == 0U);
  }

  SECTION("NOP traffic does not move the original resource-response ACK deadline") {
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27'863U);
    auto config = live_response_config();
    config.response_acknowledgement_timeout = 5ms;
    goldsrc::ResourceClientResponseStage stage{transport, remote, config};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
    REQUIRE(stage.response_transmitted());
    for (std::uint32_t index = 0U; index < 4U; ++index) {
      transport.queue(remote, server_packet(12U + index, false,
          driven.request.header.sequence.sequence.value(), false,
          {std::byte{1U}}));
      stage.update(epoch + std::chrono::milliseconds{5U + index});
      REQUIRE_FALSE(stage.terminal());
      CHECK_FALSE(stage.response_acknowledged());
    }
    stage.update(epoch + 9ms);
    REQUIRE(stage.error());
    CHECK(stage.state() ==
          goldsrc::ResourceClientResponseStageState::timed_out);
    CHECK(stage.error()->code ==
          goldsrc::ResourceClientResponseStageErrorCode::
              response_acknowledgement_timed_out);
    CHECK_FALSE(stage.result());
    CHECK(stage.cleanup_count() == 1U);
  }
}

TEST_CASE(
    "Delayed control proof cannot bypass semantic ACK or envelope validation",
    "[goldsrc][resource-response][stage][ordering][startup-nop][security]") {
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 12s;
  for (const unsigned int kind : {0U, 1U, 2U, 3U, 4U}) {
    DYNAMIC_SECTION("payload kind=" << kind) {
      FakeTransport transport;
      const auto remote = network::NetworkAddress::loopback(27'864U);
      goldsrc::ResourceClientResponseStage stage{transport, remote,
                                                  live_response_config()};
      const auto driven =
          drive_to_transition_request(stage, transport, remote, epoch);
      deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
      REQUIRE_FALSE(stage.terminal());
      REQUIRE(stage.response_transmitted());
      std::vector<std::byte> body;
      if (kind == 0U) {
        body = service_envelope(next_server_semantic_payload());
      } else if (kind == 1U) {
        // The prefix NOP cannot hide a following semantic spawn boundary.
        body = {std::byte{1U}};
        const auto semantic = next_server_semantic_payload();
        body.insert(body.end(), semantic.begin(), semantic.end());
      } else if (kind == 2U) {
        body = {std::byte{1U}, std::byte{7U}}; // truncated known control
      } else if (kind == 3U) {
        body = service_envelope(std::array{std::byte{1U}});
        body.pop_back(); // no complete BZ_STREAM_END
      } else {
        body = service_envelope(std::array{std::byte{1U}});
        body.push_back(std::byte{1U}); // trailing data after compressed stream
      }
      static_cast<void>(drain_events(stage));
      transport.queue(remote, server_packet(12U, false,
          driven.request.header.sequence.sequence.value(), false, std::move(body)));
      stage.update(epoch + 5ms);
      REQUIRE(stage.terminal());
      REQUIRE(stage.error());
      if (kind == 0U || kind == 1U)
        CHECK(stage.error()->code ==
              goldsrc::ResourceClientResponseStageErrorCode::
                  server_payload_acknowledgement_invalid);
      CHECK_FALSE(stage.response_acknowledged());
      CHECK_FALSE(stage.result());
      REQUIRE(stage.payload_diagnostic());
      CHECK(stage.payload_diagnostic()->consumed_control_message_count == 0U);
      const auto rejected = drain_events(stage);
      CHECK(event_count(rejected,
                        goldsrc::ResourceClientResponseStageEventType::
                            post_transmit_control_consumed) == 0U);
      CHECK(event_count(rejected,
                        goldsrc::ResourceClientResponseStageEventType::
                            response_completion_ready) == 0U);
    }
  }
}

TEST_CASE(
    "Covering numeric ACK cannot turn mixed or malformed bodies into NOP controls",
    "[goldsrc][resource-response][stage][ordering][startup-nop][security]") {
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 12s;
  constexpr std::array empty_response{std::byte{5U}, std::byte{0U},
                                      std::byte{0U}};
  for (const unsigned int kind : {0U, 1U, 2U, 3U}) {
    DYNAMIC_SECTION("covering payload kind=" << kind) {
      FakeTransport transport;
      const auto remote = network::NetworkAddress::loopback(27'865U);
        auto config = live_response_config();
      goldsrc::ResourceClientResponseStage stage{transport, remote, config};
      const auto driven =
          drive_to_transition_request(stage, transport, remote, epoch);
      deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
      REQUIRE(stage.response_transmitted());
      const auto response = require_response_packet(transport, empty_response);
      std::vector<std::byte> body;
      if (kind == 0U) {
        body = {std::byte{1U}};
        const auto semantic = next_server_semantic_payload();
        body.insert(body.end(), semantic.begin(), semantic.end());
      } else if (kind == 1U) {
        body = {std::byte{1U}, std::byte{7U}};
      } else if (kind == 2U) {
        body = service_envelope(std::array{std::byte{1U}});
        body.pop_back();
      } else {
        body = service_envelope(std::array{std::byte{1U}});
        body.push_back(std::byte{1U});
      }
      static_cast<void>(drain_events(stage));
      const auto sends_before_candidate = transport.sent.size();
      // A reliable carrier generates a real later client ACK sequence, without
      // changing the semantic body's strict treatment or transport timers.
      transport.queue(remote, server_packet(12U, true,
          response.header.sequence.sequence.value(),
          !response.header.sequence.flags.reliable, std::move(body)));
      stage.update(epoch + 5ms);
      CHECK_FALSE(stage.response_acknowledged());
      CHECK_FALSE(stage.result());
      REQUIRE(stage.payload_diagnostic());
      CHECK(stage.payload_diagnostic()->consumed_control_message_count == 0U);
      const auto candidate_events = drain_events(stage);
      CHECK(event_count(candidate_events,
                        goldsrc::ResourceClientResponseStageEventType::
                            post_transmit_control_consumed) == 0U);
      CHECK(event_count(candidate_events,
                        goldsrc::ResourceClientResponseStageEventType::
                            response_completion_ready) == 0U);

      // The original strict semantic path may retain its one bounded payload
      // until the genuine ACK. It must never silently discard it as a NOP or
      // manufacture an ACK-only result, even with covering numeric metadata.
      if (!stage.terminal()) {
        REQUIRE(transport.sent.size() == sends_before_candidate + 1U);
        const auto latest_tx =
            decode_sent(transport.sent.back()).header.sequence.sequence.value();
        INFO("first response TX=" << response.header.sequence.sequence.value()
                                  << " latest actual TX=" << latest_tx
                                  << " payload kind=" << kind);
        REQUIRE(latest_tx > response.header.sequence.sequence.value());
        transport.queue(remote, server_packet(13U, false, latest_tx,
            response.header.sequence.flags.reliable));
        stage.update(epoch + 6ms);
      }
      REQUIRE(stage.terminal());
      REQUIRE(stage.error());
      CHECK_FALSE(stage.result());
      const auto rejection_events = drain_events(stage);
      CHECK(event_count(rejection_events,
                        goldsrc::ResourceClientResponseStageEventType::
                            post_transmit_control_consumed) == 0U);
      CHECK(event_count(rejection_events,
                        goldsrc::ResourceClientResponseStageEventType::
                            response_completion_ready) == 0U);
    }
  }
}

TEST_CASE("Delayed NOP exemption remains gated by the explicit live-control policy",
          "[goldsrc][resource-response][stage][startup-nop][security][regression]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'866U);
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 12s;
  auto config = live_response_config();
  config.pre_transmit_payload_policy =
      goldsrc::ResourceResponsePreTransmitPayloadPolicy::reject;
  goldsrc::ResourceClientResponseStage stage{transport, remote, config};
  const auto driven =
      drive_to_transition_request(stage, transport, remote, epoch);
  deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
  REQUIRE(stage.response_transmitted());
  static_cast<void>(drain_events(stage));
  transport.queue(remote, server_packet(12U, false,
      driven.request.header.sequence.sequence.value(), false, {std::byte{1U}}));
  stage.update(epoch + 5ms);
  REQUIRE(stage.error());
  CHECK(stage.error()->code ==
        goldsrc::ResourceClientResponseStageErrorCode::
            server_payload_acknowledgement_invalid);
  CHECK_FALSE(stage.response_acknowledged());
  CHECK_FALSE(stage.result());
  const auto rejected = drain_events(stage);
  CHECK(event_count(rejected,
                    goldsrc::ResourceClientResponseStageEventType::
                        post_transmit_control_consumed) == 0U);
}

TEST_CASE("Independent response-boundary deadlines ignore header-only traffic",
          "[goldsrc][resource-response][stage][timeout][manual-clock]") {
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 11s;

  SECTION("header-only traffic cannot extend response ACK deadline") {
    FakeTransport transport;
    FakeConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'834U);
    auto config = test_config();
    config.response_acknowledgement_timeout = 5ms;
    goldsrc::ResourceClientResponseStage stage{transport, remote, config,
                                               &provider};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
    const auto response = require_response_packet(transport);
    REQUIRE(response.header.sequence.sequence.value() > 2U);

    for (std::uint32_t incoming_sequence = 3U; incoming_sequence <= 6U;
         ++incoming_sequence) {
      transport.queue(remote,
                      server_packet(incoming_sequence, false, 2U, true));
      stage.update(epoch + std::chrono::milliseconds{incoming_sequence + 2U});
      REQUIRE(
          stage.state() ==
          goldsrc::ResourceClientResponseStageState::waiting_for_response_ack);
    }

    stage.update(epoch + 9ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceClientResponseStageState::timed_out);
    REQUIRE(stage.error());
    CHECK(stage.error()->code == goldsrc::ResourceClientResponseStageErrorCode::
                                     response_acknowledgement_timed_out);
    CHECK_FALSE(stage.response_acknowledged());
    CHECK(stage.response_queue_count() == 1U);
  }

  SECTION("header-only traffic cannot extend post-ACK boundary deadline") {
    FakeTransport transport;
    FakeConsistencyProvider provider;
    const auto remote = network::NetworkAddress::loopback(27'835U);
    auto config = test_config();
    config.post_ack_boundary_timeout = 5ms;
    goldsrc::ResourceClientResponseStage stage{transport, remote, config,
                                               &provider};
    const auto driven =
        drive_to_transition_request(stage, transport, remote, epoch);
    deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
    const auto response = require_response_packet(transport);

    transport.queue(
        remote, server_packet(3U, false,
                              response.header.sequence.sequence.value(), true));
    stage.update(epoch + 5ms);
    REQUIRE(stage.state() == goldsrc::ResourceClientResponseStageState::
                                 waiting_for_server_continuation);

    for (std::uint32_t incoming_sequence = 4U; incoming_sequence <= 7U;
         ++incoming_sequence) {
      transport.queue(remote,
                      server_packet(incoming_sequence, false,
                                    response.header.sequence.sequence.value(),
                                    true));
      stage.update(epoch + std::chrono::milliseconds{incoming_sequence + 2U});
      REQUIRE(stage.state() == goldsrc::ResourceClientResponseStageState::
                                   waiting_for_server_continuation);
    }

    stage.update(epoch + 10ms);

    REQUIRE(stage.state() ==
            goldsrc::ResourceClientResponseStageState::timed_out);
    REQUIRE(stage.error());
    CHECK(stage.error()->code == goldsrc::ResourceClientResponseStageErrorCode::
                                     post_response_boundary_timed_out);
    CHECK(stage.response_acknowledged());
    CHECK_FALSE(stage.result());
  }
}

TEST_CASE(
    "Delayed continuation must acknowledge at least the first response TX",
    "[goldsrc][resource-response][stage][ack][security]") {
  FakeTransport transport;
  FakeConsistencyProvider provider;
  const auto remote = network::NetworkAddress::loopback(27'836U);
  const auto epoch = goldsrc::ResourceClientResponseStageTimePoint{} + 12s;
  goldsrc::ResourceClientResponseStage stage{transport, remote, test_config(),
                                             &provider};
  const auto driven =
      drive_to_transition_request(stage, transport, remote, epoch);
  deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
  const auto response = require_response_packet(transport);
  REQUIRE(response.header.sequence.sequence.value() > 0U);

  transport.queue(
      remote,
      server_packet(3U, false, response.header.sequence.sequence.value() - 1U,
                    true, service_envelope(next_server_semantic_payload())));
  stage.update(epoch + 5ms);

  REQUIRE(stage.state() ==
          goldsrc::ResourceClientResponseStageState::protocol_error);
  REQUIRE(stage.error());
  CHECK(stage.error()->code == goldsrc::ResourceClientResponseStageErrorCode::
                                   server_payload_acknowledgement_invalid);
  CHECK_FALSE(stage.response_acknowledged());
  CHECK_FALSE(stage.result());
  CHECK(stage.response_queue_count() == 1U);
}

TEST_CASE("Historical resource-list stop remains zero-queue and zero-send at "
          "its boundary",
          "[goldsrc][resource-list][stage][regression][zero-send]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'829U);
  const auto epoch = goldsrc::ResourceListStageTimePoint{} + 1s;
  std::size_t connection_releases = 0U;
  goldsrc::ResourceListStage stage{transport, remote,
                                   test_config().resource_list};
  const auto driven = drive_to_transition_request(
      stage, transport, remote, epoch,
      std::make_unique<CountingConnectionLifetime>(connection_releases));
  const auto sends_before_boundary = transport.sent.size();

  deliver_resource_payload(stage, transport, remote, driven, epoch + 4ms);
  REQUIRE(stage.state() ==
          goldsrc::ResourceListStageState::client_response_required);
  REQUIRE(stage.result());
  CHECK(stage.response_queue_count() == 0U);
  CHECK(transport.sent.size() == sends_before_boundary);
  CHECK(stage.cleanup_count() == 1U);
  CHECK(connection_releases == 1U);

  stage.update(epoch + 5ms);
  stage.cancel(epoch + 6ms);
  CHECK(stage.response_queue_count() == 0U);
  CHECK(transport.sent.size() == sends_before_boundary);
  CHECK(stage.cleanup_count() == 1U);
  CHECK(connection_releases == 1U);
}

TEST_CASE("Live runtime composition preserves mixed wire encodings through "
          "ClientWorldState",
          "[goldsrc][live-runtime][integration][uncompressed][compressed]["
          "world-state]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'842U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 20s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};

  const auto driven =
      drive_live_initial(stage, transport, remote, epoch, false, false);

  const auto resource_sequence = driven.last_server_sequence + 1U;
  // The normal-fragment header's reliable marker is not the semantic
  // reliable-generation ACK.  The four queued client semantics use the
  // session generations true, false, true, false in this fixture.
  transport.queue(
      remote,
      server_packet(resource_sequence, false,
                    driven.transition_request.header.sequence.sequence.value(),
                    false, service_envelope(resource_semantic_payload())));
  transport.queue(
      remote,
      server_packet(resource_sequence + 1U, false,
                    driven.transition_request.header.sequence.sequence.value(),
                    false, {std::byte{1U}}));
  for (std::size_t step = 1U; step <= 8U; ++step) {
    stage.update(driven.now + std::chrono::milliseconds{step});
  }

  constexpr std::array empty_response{
      std::byte{0x05U},
      std::byte{0x00U},
      std::byte{0x00U},
  };
  CAPTURE(static_cast<int>(stage.state()));
  CAPTURE(stage.error() ? stage.error()->context : std::string{});
  CAPTURE(driven.transition_request.header.sequence.sequence.value(),
          driven.transition_request.header.sequence.flags.reliable,
          resource_sequence);
  std::vector<std::size_t> client_payload_sizes;
  std::vector<std::uint32_t> client_sequences;
  for (const auto &datagram : transport.sent) {
    const auto packet = decode_sent(datagram);
    client_payload_sizes.push_back(packet.payload.size());
    client_sequences.push_back(packet.header.sequence.sequence.value());
  }
  CAPTURE(client_payload_sizes, client_sequences);
  INFO("expected typed zero-entry resource response");
  const auto response = require_latest_payload(transport, empty_response);
  CHECK(response.header.sequence.flags.fragmented);

  const auto first_runtime_at = driven.now + 9ms;
  // Resource response completion is transport-only. The server sign-on
  // continuation is not fabricated before the client sends typed spawn.
  transport.queue(remote,
                  server_packet(resource_sequence + 2U, false,
                                response.header.sequence.sequence.value(),
                                response.header.sequence.flags.reliable));
  stage.update(first_runtime_at);
  CAPTURE(static_cast<int>(stage.state()));
  CAPTURE(stage.error() ? stage.error()->context : std::string{});
  REQUIRE_FALSE(stage.terminal());
  CHECK_FALSE(world.runtime_observation());
  const auto expected_spawn = goldsrc::StockSpawnRequestBuilder::build(
      0x1234'5678U,
      goldsrc::decode_stock_server_world_map_crc(0xdead'beefU, 0U));
  REQUIRE(expected_spawn);
  REQUIRE(expected_spawn.encoding);
  CAPTURE(static_cast<int>(stage.state()));
  CAPTURE(stage.error() ? stage.error()->context : std::string{});
  INFO("expected typed stock spawn request");
  const auto spawn = require_latest_payload(
      transport, expected_spawn.encoding->semantic_bytes());
  CAPTURE(spawn.header.sequence.sequence.value(),
          spawn.header.sequence.flags.reliable,
          response.header.sequence.sequence.value(),
          response.header.sequence.flags.reliable);

  transport.queue(
      remote,
      server_packet(resource_sequence + 3U, true,
                    spawn.header.sequence.sequence.value(), false,
                    live_baseline_and_runtime_payload(100.0F, 100U, 80U)));
  stage.update(first_runtime_at + 1ms);
  CAPTURE(static_cast<int>(stage.state()));
  CAPTURE(stage.error() ? stage.error()->context : std::string{});
  REQUIRE_FALSE(stage.terminal());
  REQUIRE(world.runtime_observation());
  CHECK(world.runtime_publication_revision() == 2U);

  stage.update(first_runtime_at + 2ms);
  const auto expected_signon_reply =
      goldsrc::StockSendEntitiesRequestBuilder::build();
  INFO("expected typed sendents reply to svc_signonnum 1");
  const auto signon_reply =
      require_latest_payload(transport, expected_signon_reply.semantic_bytes());

  transport.queue(
      remote,
      server_packet(resource_sequence + 4U, false,
                    signon_reply.header.sequence.sequence.value(),
                    signon_reply.header.sequence.flags.reliable,
                    service_envelope(live_runtime_record(101.0F, 90U, 96U))));
  stage.update(first_runtime_at + 3ms);
  CAPTURE(static_cast<int>(stage.state()));
  CAPTURE(stage.error() ? stage.error()->context : std::string{});
  REQUIRE_FALSE(stage.terminal());
  REQUIRE(world.runtime_observation());
  CHECK(world.runtime_publication_revision() == 3U);
  REQUIRE(world.runtime_observation()->receiving_client);
  REQUIRE(world.runtime_observation()->receiving_client->health);
  CHECK(*world.runtime_observation()->receiving_client->health == 90.0);
  REQUIRE(world.runtime_observation()->packet_entities.size() == 1U);
  REQUIRE(world.runtime_observation()->packet_entities.front().origin.x);
  CHECK(*world.runtime_observation()->packet_entities.front().origin.x == 12.0);

  stage.update(first_runtime_at + 2s + 2ms);
  CAPTURE(stage.error() ? stage.error()->context : std::string{});
  REQUIRE(stage.state() ==
          goldsrc::LiveRuntimeStageState::stable_runtime_state_ready);
  REQUIRE(stage.result());
  const auto &result = *stage.result();
  CHECK(result.protocol == 48U);
  CHECK(result.schema_count == 5U);
  CHECK(result.baseline_entity_count == 1U);
  CHECK(result.baseline_instanced_count == 0U);
  CHECK(result.received_service_payload_count == 2U);
  CHECK(result.applied_runtime_record_count == 2U);
  CHECK(result.clientdata_record_count == 2U);
  CHECK(result.entity_record_count == 2U);
  CHECK(result.compressed_service_payload_count == 2U);
  CHECK(result.wire_uncompressed_service_payload_count == 2U);
  CHECK(result.initial_service_wire_uncompressed);
  CHECK_FALSE(result.transition_service_wire_uncompressed);
  CHECK(result.spawn_request_queue_count == 1U);
  CHECK(result.spawn_request_transmitted);
  CHECK(result.spawn_request_acknowledged);
  CHECK(result.signon_reply_queue_count == 1U);
  CHECK(result.signon_reply_transmitted);
  CHECK(result.signon_reply_acknowledged);
  CHECK(result.server_time_observed);
  CHECK(result.clientdata_observed);
  CHECK(result.entities_observed);
  CHECK(result.stable_progress_observed);
  CHECK(result.stable_interval >= 2s);
  CHECK(result.publication_revision == 3U);
  CHECK(result.entity_count == 1U);
  CHECK(stage.usercmd_transmit_count() == 0U);
  REQUIRE(stage.live_resource_list() != nullptr);
  REQUIRE(stage.live_server_info() != nullptr);
  CHECK(stage.live_server_info()->protocol_version() ==
        goldsrc::ProtocolVersion::goldsrc_48);
  CHECK(stage.live_server_info()->map_file_path() == "maps/test_alpha.bsp");
  CHECK(stage.live_sky_name() == "desert");
}

TEST_CASE("Live owner retains pre-baseline spawn angles across initialization and later RX",
          "[e14][spawn-orientation][goldsrc][live-runtime][integration]") {
  for (const bool reference : {false,true}) {
    CAPTURE(reference);
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27847);
    const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 50s;
    hlclient::client::ClientWorldState world;
    auto config = live_test_config();
    config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
    config.live_visual_input_source = goldsrc::LiveVisualControlInputSource::keyboard_mouse;
    config.reference_prediction = reference;
    goldsrc::LiveRuntimeStage stage{transport,remote,world,config};
    const auto driven = drive_live_initial(stage,transport,remote,epoch,false,false,true);
    const auto sequence = driven.last_server_sequence + 1;
    transport.queue(remote,server_packet(sequence,false,
        driven.transition_request.header.sequence.sequence.value(),false,
        service_envelope(resource_semantic_payload())));
    transport.queue(remote,server_packet(sequence+1,false,
        driven.transition_request.header.sequence.sequence.value(),false,{std::byte{1}}));
    for (unsigned step=1;step<=8;++step) stage.update(driven.now+std::chrono::milliseconds{step});
    constexpr std::array empty_response{std::byte{5},std::byte{0},std::byte{0}};
    const auto response = require_latest_payload(transport,empty_response);
    const auto now = driven.now + 9ms;
    transport.queue(remote,server_packet(sequence+2,false,
        response.header.sequence.sequence.value(),response.header.sequence.flags.reliable));
    stage.update(now);
    const auto spawn = require_latest_payload(transport,
        goldsrc::StockSpawnRequestBuilder::build(0x12345678,
            goldsrc::decode_stock_server_world_map_crc(0xdeadbeef,0)).encoding->semantic_bytes());
    // Separate pre-baseline correction is consumed before async assets/camera.
    std::vector<std::byte> angle{
        std::byte{10},std::byte{0},std::byte{0},std::byte{0},
        std::byte{0x80},std::byte{0},std::byte{0}};
    if (!reference) {
      auto info=std::vector<std::byte>{user_fixture::kExactUserInfoMessage.begin(),user_fixture::kExactUserInfoMessage.end()};
      const std::array original{std::byte{'S'},std::byte{'y'},std::byte{'n'},std::byte{'t'},std::byte{'h'},std::byte{'e'},std::byte{'t'},std::byte{'i'},std::byte{'c'}};
      const auto name=std::search(info.begin(),info.end(),original.begin(),original.end());REQUIRE(name!=info.end());
      const std::string replacement="Pending__";std::transform(replacement.begin(),replacement.end(),name,[](char c){return static_cast<std::byte>(c);});
      angle.insert(angle.end(),info.begin(),info.end());
    }
    transport.queue(remote,server_packet(sequence+3,true,
        spawn.header.sequence.sequence.value(),false,angle));
    stage.update(now+1ms);
    transport.queue(remote,server_packet(sequence+4,false,
        spawn.header.sequence.sequence.value(),false,
        live_baseline_and_runtime_payload(100.0F,100,80)));
    stage.update(now+2ms);
    REQUIRE_FALSE(stage.error());
    REQUIRE(world.runtime_observation());
    config.game_client->set_hud_context({640U,480U,true});
    const auto board=config.game_client->hud(*world.runtime_observation(),0.0);
    const std::string expected_name=reference ? "Synthetic" : "Pending__";
    CHECK(std::any_of(board.draw.texts.begin(),board.draw.texts.end(),[&](const auto& text){return text.text==expected_name;}));
    REQUIRE(world.runtime_observation()->view_angle_correction);
    const auto correction = *world.runtime_observation()->view_angle_correction;
    CHECK(correction.yaw_degrees == -180.0);
    CHECK(correction.source.source_transport_sequence == sequence+3);
    stage.update(now+3ms);
    const auto sendents = require_latest_payload(transport,
        goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
    transport.queue(remote,server_packet(sequence+5,false,
        sendents.header.sequence.sequence.value(),sendents.header.sequence.flags.reliable,
        service_envelope(live_runtime_record(101.0F,90,96))));
    stage.update(now+4ms);
    REQUIRE_FALSE(stage.error());
    CHECK(world.runtime_observation()->view_angle_correction == correction);
  }
}

TEST_CASE("Reference waiting mode delivers each committed clientdata before an entity-only final record",
          "[goldsrc][live-runtime][integration][reference-rx-routing]") {
  std::optional<std::uint64_t> off_revision;
  std::optional<double> off_health;
  for (const bool reference_prediction : {false, true}) {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'847U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 50s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source =
      goldsrc::LiveVisualControlInputSource::scripted_speed_check;
  config.reference_prediction = reference_prediction;
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  const auto driven = drive_live_initial(stage, transport, remote, epoch,
                                         false, false, true);
  const auto resource_sequence = driven.last_server_sequence + 1U;
  transport.queue(remote, server_packet(resource_sequence, false,
      driven.transition_request.header.sequence.sequence.value(), false,
      service_envelope(resource_semantic_payload())));
  transport.queue(remote, server_packet(resource_sequence + 1U, false,
      driven.transition_request.header.sequence.sequence.value(), false,
      {std::byte{1U}}));
  for (std::size_t step = 1U; step <= 8U; ++step)
    stage.update(driven.now + std::chrono::milliseconds{step});
  constexpr std::array empty_response{std::byte{0x05U}, std::byte{0U},
                                      std::byte{0U}};
  const auto response = require_latest_payload(transport, empty_response);
  const auto first_runtime_at = driven.now + 9ms;
  transport.queue(remote, server_packet(resource_sequence + 2U, false,
      response.header.sequence.sequence.value(),
      response.header.sequence.flags.reliable));
  stage.update(first_runtime_at);
  const auto spawn = require_latest_payload(transport,
      goldsrc::StockSpawnRequestBuilder::build(
          0x1234'5678U,
          goldsrc::decode_stock_server_world_map_crc(0xdead'beefU, 0U))
          .encoding->semantic_bytes());
  transport.queue(remote, server_packet(resource_sequence + 3U, true,
      spawn.header.sequence.sequence.value(), false,
      live_baseline_and_runtime_payload(100.0F, 100U, 80U)));
  stage.update(first_runtime_at + 1ms);
  stage.update(first_runtime_at + 2ms);
  const auto sendents = require_latest_payload(transport,
      goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
  transport.queue(remote, server_packet(resource_sequence + 4U, false,
      sendents.header.sequence.sequence.value(),
      sendents.header.sequence.flags.reliable,
      service_envelope(live_runtime_record(101.0F, 90U, 96U))));
  stage.update(first_runtime_at + 3ms);
  const auto revision_before = world.runtime_publication_revision();
  stage.update(first_runtime_at + 2s + 2ms);
  REQUIRE(stage.live_visual_input_ready());
  CAPTURE(static_cast<int>(stage.state()));
  CAPTURE(stage.error() ? stage.error()->context : std::string{});
  const auto activated = stage.activate_live_visual_control(first_runtime_at + 2s + 3ms);
  CAPTURE(static_cast<int>(stage.state()));
  CAPTURE(stage.error() ? stage.error()->context : std::string{});
  REQUIRE(activated);
  const auto active_at = first_runtime_at + 2s + 23ms;
  stage.update(active_at);
  REQUIRE(stage.usercmd_transmit_count() > 0U);
  const auto last_sent = decode_sent(transport.sent.back());

  // These two records arrive during one owning driver update. The second
  // projection retains clientdata; it must not erase the first fresh sample.
  transport.queue(remote, server_packet(resource_sequence + 5U, false,
      last_sent.header.sequence.sequence.value(), false,
      service_envelope(live_runtime_record(102.0F, 90U, 96U))));
  transport.queue(remote, server_packet(resource_sequence + 6U, false,
      last_sent.header.sequence.sequence.value(), false,
      service_envelope(live_entity_only_record(96U))));
  stage.update(active_at + 20ms);
  REQUIRE_FALSE(stage.terminal());
  REQUIRE(world.runtime_observation());
  CHECK(world.runtime_publication_revision() == revision_before + 2U);
  CHECK(world.runtime_observation()->client_metadata.freshness ==
      hlclient::client::RuntimeObservationFreshness::retained);
  const auto commands = stage.live_usercmd_snapshot();
  REQUIRE(commands);
  REQUIRE(commands->server_samples.size() == 1U);
  CHECK(commands->server_samples.front().source.source_transport_sequence ==
      resource_sequence + 5U);
  CHECK(commands->driver_rx_total.owning_datagrams -
            commands->driver_rx_at_input_activation.owning_datagrams == 2U);
  CHECK(commands->driver_rx_total.updates >
        commands->driver_rx_at_input_activation.updates);
  CHECK(commands->driver_rx_total.payloads_created -
            commands->driver_rx_at_input_activation.payloads_created == 2U);
  CHECK(commands->payload_events_consumed_total -
            commands->payload_events_consumed_at_input_activation == 2U);
  CHECK(commands->runtime_records_committed_total -
            commands->runtime_records_committed_at_input_activation == 2U);
  CHECK(commands->clientdata_records_committed_total -
            commands->clientdata_records_committed_at_input_activation == 1U);
  transport.queue(remote, server_packet(resource_sequence + 7U, false,
      last_sent.header.sequence.sequence.value(), false,
      service_envelope(live_clientdata_zero_change_record(102.5F))));
  stage.update(active_at + 40ms);
  REQUIRE_FALSE(stage.terminal());
  const auto after_zero_change = stage.live_usercmd_snapshot();
  REQUIRE(after_zero_change);
  REQUIRE(after_zero_change->server_samples.size() == 2U);
  CHECK(after_zero_change->server_samples.back().source.source_transport_sequence ==
      resource_sequence + 7U);
  CHECK(after_zero_change->clientdata_records_committed_total -
            after_zero_change->clientdata_records_committed_at_input_activation == 2U);
  if (reference_prediction)
    CHECK(stage.live_reference_prediction_snapshot(active_at + 40ms).state ==
        goldsrc::LiveReferencePredictionState::waiting_for_seed);
  REQUIRE(world.runtime_observation());
  REQUIRE(world.runtime_observation()->receiving_client);
  REQUIRE(world.runtime_observation()->receiving_client->health);
  if (!reference_prediction) {
    off_revision = world.runtime_publication_revision();
    off_health = *world.runtime_observation()->receiving_client->health;
  } else {
    CHECK(world.runtime_publication_revision() == off_revision);
    CHECK(*world.runtime_observation()->receiving_client->health == off_health);
  }
  }
}

TEST_CASE("Reference live stage keeps action commands in active history",
          "[e321][e321-b][e32-lift][d4][goldsrc][live-runtime][integration][reference-actions]") {
  bool brush=false, lift=false, same_time_box=false, lift_top=false;
  bool quantized_boundary=false, post_effect=false;
  bool lift_ack_error=false, lift_ack_reversal=false, lift_ack_negative=false;
  float upward_post_effect_z=0.0F;
  std::shared_ptr<PostMovementEffectTestProbe> post_effect_probe;
  SECTION("unchanged world-only host") {}
  SECTION("same host with committed server brush context") { brush=true; }
  SECTION("vertical lift uses source time for carry replay and camera") { brush=true; lift=true; }
  SECTION("E32 same-time unrelated collider retains lift carry and current membership") {
    brush=true; lift=true; same_time_box=true;
  }
  SECTION("E32.1 observed upper stop preserves the same-time camera and then stands") {
    brush=true; lift=true; lift_top=true;
  }
  SECTION("E32 grounded quantized lift boundary stays active through live anchor rebase") {
    brush=true; lift=true; quantized_boundary=true;
  }
  SECTION("E32 corrected lift anchor keeps its checked presentation predecessor") {
    brush=true; lift=true; lift_ack_error=true;
  }
  SECTION("E32 opposite observed lift motion does not retain an ascending predecessor") {
    brush=true; lift=true; lift_ack_error=true; lift_ack_reversal=true;
  }
  SECTION("E32 a negative correction on an ascending lift retains its ascending contact pair") {
    brush=true; lift=true; lift_ack_error=true; lift_ack_negative=true;
  }
  SECTION("E32 neutral post movement effect traverses the ordinary live command owner") {
    post_effect=true;
  }
  SECTION("E32 grounded canonical echo accepts a 400 ups post movement impulse once") {
    post_effect=true; upward_post_effect_z=400.0F;
  }
  SECTION("E32 grounded canonical echo accepts a 1000 ups post movement impulse once") {
    post_effect=true; upward_post_effect_z=1000.0F;
  }
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'850U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 60s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  if (post_effect) {
    post_effect_probe=std::make_shared<PostMovementEffectTestProbe>();
    config.game_client = std::make_shared<hlclient::game_api::GameClientHost>(
        std::make_unique<PostMovementEffectTestModule>(
            upward_post_effect_z > 0.0F
                ? hlclient::assets::AssetVector3{0.0F,0.0F,upward_post_effect_z}
                : hlclient::assets::AssetVector3{100.0F,0.0F,0.0F},
            post_effect_probe));
    config.game_client->reset({1U,1U,std::nullopt});
    config.game_client->set_movement_audio_time_origin(
        std::chrono::duration<double>(epoch.time_since_epoch()).count());
  }
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source = goldsrc::
      LiveVisualControlInputSource::scripted_jump_duck_check;
  config.reference_prediction = true;
  if (lift_top || lift_ack_error) config.movement_diagnostics = true;
  config.usercmd_scenario.durations = {20ms, 40ms, 20ms, 80ms, 20ms};
  if (post_effect)
    config.usercmd_scenario.durations={2000ms,2000ms,2000ms,2000ms,2000ms};
  if (lift) {
    config.live_visual_input_source=goldsrc::LiveVisualControlInputSource::scripted_speed_check;
    config.usercmd_scenario.durations={2000ms,2000ms,2000ms,2000ms,2000ms};
  }
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  const auto driven = drive_live_initial(stage, transport, remote, epoch,
                                         false, false, true, true, same_time_box);
  const auto resource_sequence = driven.last_server_sequence + 1U;
  transport.queue(remote, server_packet(resource_sequence, false,
      driven.transition_request.header.sequence.sequence.value(), false,
      service_envelope(resource_semantic_payload(brush))));
  transport.queue(remote, server_packet(resource_sequence + 1U, false,
      driven.transition_request.header.sequence.sequence.value(), false,
      {std::byte{1U}}));
  for (std::size_t step = 1U; step <= 8U; ++step)
    stage.update(driven.now + std::chrono::milliseconds{step});
  constexpr std::array empty_response{std::byte{0x05U}, std::byte{0U},
                                      std::byte{0U}};
  const auto response = require_latest_payload(transport, empty_response);
  const auto first_runtime_at = driven.now + 9ms;
  transport.queue(remote, server_packet(resource_sequence + 2U, false,
      response.header.sequence.sequence.value(),
      response.header.sequence.flags.reliable));
  stage.update(first_runtime_at);
  const auto spawn = require_latest_payload(transport,
      goldsrc::StockSpawnRequestBuilder::build(
          0x1234'5678U,
          goldsrc::decode_stock_server_world_map_crc(0xdead'beefU, 0U))
          .encoding->semantic_bytes());
  transport.queue(remote, server_packet(resource_sequence + 3U, true,
      spawn.header.sequence.sequence.value(), false,
      live_baseline_and_runtime_payload(100.0F, 100U, 80U, true)));
  stage.update(first_runtime_at + 1ms);
  stage.update(first_runtime_at + 2ms);
  const auto sendents = require_latest_payload(transport,
      goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
  transport.queue(remote, server_packet(resource_sequence + 4U, false,
      sendents.header.sequence.sequence.value(),
      sendents.header.sequence.flags.reliable,
      service_envelope(live_runtime_record(101.0F, 90U, 96U))));
  stage.update(first_runtime_at + 3ms);
  stage.update(first_runtime_at + 2s + 2ms);
  REQUIRE(stage.live_visual_input_ready());
  REQUIRE(stage.attach_reference_prediction_collision(
      lift_top ? hlclient::tests::lift_top_e32_1::package() :
      lift ? hlclient::tests::vertical_lift::package() :
      upward_post_effect_z > 0.0F
          ? hlclient::tests::convex_corner::package(false,64.0F)
          : hlclient::tests::collision_brush_fixture::package(false)));
  REQUIRE(stage.activate_live_visual_control(first_runtime_at + 2s + 3ms));
  const auto active_at = first_runtime_at + 2s + 23ms;
  stage.update(active_at);
  const auto sent = decode_sent(transport.sent.back());
  if (lift) {
    auto now=active_at;
    std::uint32_t server_sequence=resource_sequence+4;
    auto receive=[&](float time,unsigned height,std::chrono::milliseconds delay,
                     std::optional<unsigned> player_z_eighths = {},
                     std::optional<std::uint32_t> acknowledged_packet = {}) {
      const auto ack=acknowledged_packet.value_or(
          decode_sent(transport.sent.back()).header.sequence.sequence.value());
      transport.queue(remote,server_packet(++server_sequence,false,ack,false,
          service_envelope(live_prediction_air_record(time,0,0,
              player_z_eighths.value_or((height+36)*8),100,0,true,height*8,false,
              same_time_box ? std::optional<unsigned>{256U*8U} : std::nullopt))));
      now+=delay; stage.update(now);
      const auto result=stage.live_reference_prediction_snapshot(now);
      INFO(result.reason); REQUIRE(result.state==goldsrc::LiveReferencePredictionState::active);
      return result;
    };
    receive(102.F,50,20ms,quantized_boundary ? std::optional<unsigned>{(50U+36U)*8U-1U} : std::nullopt);
    auto moving=receive(102.1F,60,20ms,
        quantized_boundary ? std::optional<unsigned>{(60U+36U)*8U-1U} : std::nullopt);
    REQUIRE(moving.presented_brush_scene);
    REQUIRE(moving.predicted_origin);
    CHECK(moving.support_policy=="derived_vertical_support_bounded_250ms_v1");
    if (quantized_boundary) {
      REQUIRE(moving.predicted_origin);
      // The 102.0 -> 102.1 support trajectory has already contributed one
      // 20ms carry command after the recovered top96 anchor.
      CHECK(moving.predicted_origin->z==Catch::Approx(98.F).margin(.002));
      const auto corrections=moving.accepted_corrections;
      const auto fallback=moving.fallback_count;
      const auto acknowledged=decode_sent(transport.sent.back()).header.sequence.sequence.value();
      now+=20ms;stage.update(now);
      const auto before=stage.live_reference_prediction_snapshot(now);
      REQUIRE(before.state==goldsrc::LiveReferencePredictionState::active);
      REQUIRE(before.presented_origin);
      // The older acknowledged boundary is the carried z98 state.  Advance
      // the authoritative carrier by the same 20ms (top62), retain the
      // deliberate 1/8-unit overlap, then replay exactly the one saved tail.
      const auto corrected=receive(102.12F,62U,0ms,
          (62U+36U)*8U-1U,acknowledged);
      INFO(corrected.reason);
      REQUIRE(corrected.state==goldsrc::LiveReferencePredictionState::active);
      CHECK(corrected.accepted_corrections>corrections);
      REQUIRE(corrected.replayed_commands>=before.replayed_commands);
      CHECK(corrected.replayed_commands-before.replayed_commands==1U);
      CHECK(corrected.fallback_count==fallback);
      REQUIRE(corrected.last_raw_position_error);
      CHECK(*corrected.last_raw_position_error<=.1251);
      REQUIRE(corrected.presented_origin);
      CHECK(std::abs(corrected.presented_origin->z-before.presented_origin->z)<=.1251F);
      REQUIRE(corrected.last_camera_correction_jump);
      CHECK(*corrected.last_camera_correction_jump<=.1251);
      return;
    }
    if (same_time_box) {
      // No new clientdata correction, command or lift transform is present.
      // Only the unrelated box changed; svc_time still dates the same lift.
      const auto before=stage.live_reference_prediction_snapshot(now);
      const auto ack=decode_sent(transport.sent.back()).header.sequence.sequence.value();
      transport.queue(remote,server_packet(++server_sequence,false,ack,false,
          service_envelope(live_prediction_air_record(102.1F,0,0,96U*8U,100,0,
              true,60U*8U,false,512U*8U,false))));
      stage.update(now);
      const auto unchanged=stage.live_reference_prediction_snapshot(now);
      INFO(unchanged.reason);
      REQUIRE(unchanged.state==goldsrc::LiveReferencePredictionState::active);
      CHECK(unchanged.support_policy=="derived_vertical_support_bounded_250ms_v1");
      REQUIRE(unchanged.predicted_origin); REQUIRE(before.predicted_origin);
      CHECK(unchanged.predicted_origin->z==before.predicted_origin->z);
      REQUIRE(unchanged.presented_origin); REQUIRE(before.presented_origin);
      CHECK(unchanged.presented_origin->z==Catch::Approx(before.presented_origin->z).margin(.002));
      CHECK(unchanged.local_steps==before.local_steps);
      REQUIRE(world.runtime_observation());
      const auto box=std::find_if(world.runtime_observation()->packet_entities.begin(),
          world.runtime_observation()->packet_entities.end(),[](const auto& item) { return item.entity_number==43U; });
      REQUIRE(box!=world.runtime_observation()->packet_entities.end());
      REQUIRE(box->origin.x); CHECK(*box->origin.x==512.0);
      now+=20ms; stage.update(now);
      const auto next=stage.live_reference_prediction_snapshot(now);
      INFO(next.reason); REQUIRE(next.state==goldsrc::LiveReferencePredictionState::active);
      REQUIRE(next.predicted_origin);
      CHECK(next.predicted_origin->z==Catch::Approx(before.predicted_origin->z+2.F).margin(.002));
      CHECK(next.local_steps==before.local_steps+1U);
      REQUIRE(next.presented_origin); REQUIRE(next.presented_brush_scene);
      CHECK(next.presented_origin->z-next.presented_brush_scene->instances().front().transform.translation.z==
          Catch::Approx(36.F).margin(.002));
      CHECK(next.fallback_count==before.fallback_count);
      return;
    }
    const auto initial_steps=moving.local_steps;
    const auto initial_z=moving.predicted_origin->z;
    const auto initial_fallback=moving.fallback_count;
    now+=80ms; stage.update(now); // four commands in one application update
    auto batch=stage.live_reference_prediction_snapshot(now);
    INFO(batch.reason); REQUIRE(batch.state==goldsrc::LiveReferencePredictionState::active);
    CHECK(batch.local_steps==initial_steps+4);
    REQUIRE(batch.predicted_origin);
    CHECK(batch.predicted_origin->z==Catch::Approx(initial_z+8).margin(.002));
    const auto tx=transport.sent.size();
    for (auto frame=now-19ms;frame<=now;frame+=1ms) {
      auto sample=stage.live_reference_prediction_snapshot(frame);
      REQUIRE(sample.presented_origin); REQUIRE(sample.presented_brush_scene);
      CHECK(sample.presented_origin->z-sample.presented_brush_scene->instances().front().transform.translation.z==Catch::Approx(36).margin(.002));
      CHECK(sample.fallback_count==initial_fallback);
      CHECK(sample.local_steps==batch.local_steps);
    }
    CHECK(transport.sent.size()==tx);
    // Ack the last generated command at its independently expected source
    // height; latest-command correction must retain its presentation predecessor.
    const auto before=stage.live_reference_prediction_snapshot(now);
    const auto target_height=static_cast<unsigned>(std::lround(before.predicted_origin->z-36.F));
    const float target_time=102.1F+static_cast<float>(target_height-60)*.01F;
    if (lift_ack_error) {
      // The ACK consumes the newest command while rendering still samples its
      // predecessor. A source-time advancement accounts for four more units
      // on the same ascending support; it must not discard the render pair.
      const unsigned corrected_height=lift_ack_reversal ? 58U : lift_ack_negative ? target_height-1U : target_height+4U;
      const float corrected_time=lift_ack_reversal ? target_time+.1F : lift_ack_negative ? target_time-.01F : target_time+.04F;
      const auto corrected=receive(corrected_time,corrected_height,0ms);
      REQUIRE(corrected.last_raw_position_error);
      CHECK(*corrected.last_raw_position_error==Catch::Approx(lift_ack_reversal ? 12. : lift_ack_negative ? 1. : 4.).margin(.002));
      CHECK(corrected.fallback_count==initial_fallback);
      CHECK(corrected.local_steps==batch.local_steps);
      REQUIRE(corrected.predicted_origin);
      CHECK(corrected.predicted_origin->z==Catch::Approx(corrected_height+36.F).margin(.002));
      REQUIRE(corrected.support_velocity_z);
      REQUIRE(corrected.presented_origin);
      if (lift_ack_reversal) {
        CHECK(*corrected.support_velocity_z<0.);
        CHECK(corrected.presentation_reason=="anchor_before_history");
        CHECK(corrected.presented_origin->z==corrected.predicted_origin->z);
        return;
      }
      CHECK(*corrected.support_velocity_z>0.);
      CHECK(corrected.presentation_reason=="pair_endpoint");
      REQUIRE(before.presented_origin);
      CHECK(corrected.presented_origin->z==Catch::Approx(before.presented_origin->z).margin(.002));
      REQUIRE(corrected.last_camera_correction_jump);
      CHECK(*corrected.last_camera_correction_jump<.002);
      const auto transmissions=transport.sent.size();
      float previous_z=corrected.presented_origin->z;
      for(unsigned millisecond=1;millisecond<=20;++millisecond) {
        const auto frame=stage.live_reference_prediction_snapshot(now+std::chrono::milliseconds{millisecond});
        REQUIRE(frame.presented_origin);REQUIRE(frame.presented_brush_scene);
        CHECK(frame.presented_origin->z>=previous_z);
        CHECK(frame.presented_origin->z-previous_z<=.301F);
        CHECK(frame.presented_origin->z-frame.presented_brush_scene->instances().front().transform.translation.z==
            Catch::Approx(36.F).margin(.002));
        CHECK(frame.local_steps==corrected.local_steps);
        CHECK(frame.fallback_count==initial_fallback);
        previous_z=frame.presented_origin->z;
      }
      CHECK(previous_z==Catch::Approx(corrected.predicted_origin->z).margin(.002));
      CHECK(transport.sent.size()==transmissions);
      const auto repeated=receive(corrected_time,corrected_height,0ms);
      REQUIRE(repeated.presented_origin);
      CHECK(repeated.presented_origin->z==Catch::Approx(corrected.presented_origin->z).margin(.002));
      CHECK(repeated.fallback_count==initial_fallback);
      CHECK(repeated.local_steps==corrected.local_steps);
      return;
    }
    auto corrected=receive(target_time,target_height,0ms);
    REQUIRE(corrected.last_raw_position_error);
    CHECK(*corrected.last_raw_position_error<.002);
    REQUIRE(corrected.presented_origin);
    CHECK(corrected.presented_origin->z==Catch::Approx(before.presented_origin->z).margin(.002));
    CHECK(corrected.fallback_count==initial_fallback);
    CHECK(corrected.local_steps==batch.local_steps);
    if (lift_top) {
      // Last actual moving observation: top70 and player106. The source time
      // advances on the first observed stopped record; no command is generated
      // during these5ms, and raw movement is unchanged at the same carrier.
      CHECK(initial_z==Catch::Approx(98.F).margin(.002));
      CHECK(target_height==70U);
      CHECK(target_time==Catch::Approx(102.2F).margin(.0001));
      const auto sample_time=now+5ms;
      const auto before_stop=stage.live_reference_prediction_snapshot(sample_time);
      REQUIRE(before_stop.presented_origin); REQUIRE(before_stop.presented_brush_scene);
      const auto stopped=receive(target_time+.02F,70U,5ms);
      REQUIRE(stopped.last_raw_position_error);
      CHECK(*stopped.last_raw_position_error<.002);
      CHECK(stopped.local_steps==corrected.local_steps);
      REQUIRE(stopped.predicted_origin);
      CHECK(stopped.predicted_origin->z==Catch::Approx(106.F).margin(.002));
      REQUIRE(stopped.support_velocity_z); CHECK(*stopped.support_velocity_z==0.0);
      REQUIRE(stopped.presented_origin); REQUIRE(stopped.presented_brush_scene);
      CHECK(stopped.presented_origin->z==Catch::Approx(before_stop.presented_origin->z).margin(.002));
      CHECK(stopped.presentation_reason=="interpolated");
      CHECK(stopped.presented_origin->z-stopped.presented_brush_scene->instances().front().transform.translation.z==
          Catch::Approx(36.F).margin(.002));
      CHECK(stopped.fallback_count==initial_fallback);
      // Confirmed stop stays still across neutral commands and repeated source
      // records. This does not assert prediction of an unseen future stop.
      for(unsigned update=1U;update<=4U;++update) {
        now+=20ms; stage.update(now);
        const auto standing=stage.live_reference_prediction_snapshot(now);
        REQUIRE(standing.state==goldsrc::LiveReferencePredictionState::active);
        REQUIRE(standing.predicted_origin);
        REQUIRE(standing.predicted_velocity);
        CHECK(standing.predicted_origin->z==Catch::Approx(106.F).margin(.002));
        CHECK(standing.predicted_velocity->z==0.F);
        CHECK(standing.fallback_count==initial_fallback);
      }
      return;
    }
    auto repeated=receive(target_time,target_height,0ms);
    REQUIRE(repeated.presented_origin); REQUIRE(repeated.presented_brush_scene);
    CHECK(repeated.presented_origin->z==Catch::Approx(corrected.presented_origin->z).margin(.002));
    CHECK(repeated.fallback_count==initial_fallback);
    CHECK(repeated.local_steps==batch.local_steps);
    return;
  }
  transport.queue(remote, server_packet(resource_sequence + 5U, false,
      sent.header.sequence.sequence.value(), false,
      service_envelope(live_prediction_air_record(102.0F, 10U,0U,
          upward_post_effect_z>0.0F ? 36U*8U : 512U,100U,0U,brush,
          {},false,{},true,{},upward_post_effect_z>0.0F))));
  stage.update(active_at + 20ms);
  const auto seeded=stage.live_reference_prediction_snapshot(active_at+20ms);
  INFO("brush=" << brush << " reason=" << seeded.reason);
  INFO((stage.error() ? stage.error()->context : "no stage error"));
  REQUIRE(seeded.state==goldsrc::LiveReferencePredictionState::active);
  if(brush) {
    // Stationary doors still participate in presentation ray collision even
    // when there is no derived lift velocity to produce a sampled scene.
    CHECK(seeded.support_policy!="derived_vertical_support_bounded_250ms_v1");
    REQUIRE(seeded.presented_brush_scene);
    CHECK_FALSE(seeded.presented_brush_scene->instances().empty());
    CHECK(seeded.presented_brush_scene->instances().front().identity.source_model_index==1U);
  }
  if (post_effect) {
    REQUIRE(seeded.predicted_origin);REQUIRE(seeded.predicted_velocity);
    REQUIRE(seeded.canonical_origin);
    CHECK(seeded.predicted_origin->x==seeded.canonical_origin->x);
    if (upward_post_effect_z > 0.0F) {
      REQUIRE(post_effect_probe);
      CHECK(post_effect_probe->emissions==1U);
      CHECK(seeded.predicted_origin->x==seeded.canonical_origin->x);
      CHECK(seeded.predicted_origin->y==seeded.canonical_origin->y);
      CHECK(seeded.predicted_origin->z==seeded.canonical_origin->z);
      CHECK(seeded.predicted_velocity->z==
          Catch::Approx(upward_post_effect_z).margin(.001));
      REQUIRE(seeded.local_grounded);
      CHECK(*seeded.local_grounded);

      const auto effect_boundary=decode_sent(transport.sent.back());
      const auto queries_after_effect=post_effect_probe->queries;
      const auto fallback=seeded.fallback_count;
      const auto canonical_echo=[&](const std::uint32_t sequence,
                                    const float source_time) {
        transport.queue(remote,server_packet(sequence,false,
            effect_boundary.header.sequence.sequence.value(),false,
            service_envelope(live_prediction_air_record(source_time,10U,0U,36U*8U,
                100U,0U,false,{},false,{},true,
                static_cast<unsigned>(upward_post_effect_z*8.0F),true))));
        stage.update(active_at+30ms);
        const auto value=stage.live_reference_prediction_snapshot(active_at+30ms);
        INFO(value.reason);
        REQUIRE(value.state==goldsrc::LiveReferencePredictionState::active);
        REQUIRE(value.predicted_origin);REQUIRE(value.predicted_velocity);
        CHECK(value.predicted_origin->z==seeded.predicted_origin->z);
        CHECK(value.predicted_velocity->z==
            Catch::Approx(upward_post_effect_z).margin(.001));
        REQUIRE(value.server_grounded); CHECK(*value.server_grounded);
        REQUIRE(value.local_grounded); CHECK(*value.local_grounded);
        CHECK(post_effect_probe->queries==queries_after_effect);
        CHECK(post_effect_probe->emissions==1U);
        return value;
      };
      const auto corrected=canonical_echo(resource_sequence+6U,102.1F);
      CHECK(corrected.accepted_corrections>seeded.accepted_corrections);
      CHECK(corrected.fallback_count==fallback);
      REQUIRE(corrected.last_camera_correction_jump);
      CHECK(*corrected.last_camera_correction_jump<.001);

      // The exact command has moved into the anchor. A fresh duplicate echo
      // must use the bounded command-effect sidecar, without replaying or
      // querying the alternate game module again.
      const auto repeated=canonical_echo(resource_sequence+7U,102.2F);
      CHECK(repeated.accepted_corrections>corrected.accepted_corrections);
      CHECK(repeated.fallback_count==fallback);
      REQUIRE(repeated.presented_origin);REQUIRE(corrected.presented_origin);
      CHECK(repeated.predicted_origin->x==corrected.predicted_origin->x);
      CHECK(repeated.predicted_origin->y==corrected.predicted_origin->y);
      CHECK(repeated.predicted_origin->z==corrected.predicted_origin->z);
      CHECK(repeated.predicted_velocity->x==corrected.predicted_velocity->x);
      CHECK(repeated.predicted_velocity->y==corrected.predicted_velocity->y);
      CHECK(repeated.predicted_velocity->z==corrected.predicted_velocity->z);
      CHECK(repeated.presented_origin->x==corrected.presented_origin->x);
      CHECK(repeated.presented_origin->y==corrected.presented_origin->y);
      CHECK(repeated.presented_origin->z==corrected.presented_origin->z);
      REQUIRE(repeated.last_camera_correction_jump);
      CHECK(*repeated.last_camera_correction_jump<.001);

      stage.update(active_at+40ms);
      const auto airborne=stage.live_reference_prediction_snapshot(active_at+40ms);
      INFO(airborne.reason);
      REQUIRE(airborne.state==goldsrc::LiveReferencePredictionState::active);
      REQUIRE(airborne.predicted_origin);REQUIRE(airborne.predicted_velocity);
      REQUIRE(airborne.local_grounded);CHECK_FALSE(*airborne.local_grounded);
      CHECK(airborne.predicted_origin->z>repeated.predicted_origin->z);
      CHECK(airborne.predicted_velocity->z<upward_post_effect_z);
      CHECK(post_effect_probe->queries==queries_after_effect+1U);
      CHECK(post_effect_probe->emissions==1U);
      return;
    }
    CHECK(seeded.predicted_velocity->x==Catch::Approx(100.F).margin(.001));
    const auto fallback=seeded.fallback_count;
    transport.queue(remote,server_packet(resource_sequence+6U,false,
        sent.header.sequence.sequence.value(),false,
        service_envelope(live_prediction_air_record(102.1F,10U,0U,512U,100U,0U,false))));
    stage.update(active_at+30ms);
    const auto replayed=stage.live_reference_prediction_snapshot(active_at+30ms);
    INFO(replayed.reason);
    REQUIRE(replayed.state==goldsrc::LiveReferencePredictionState::active);
    CHECK(replayed.accepted_corrections>seeded.accepted_corrections);
    CHECK(replayed.replayed_commands==1U);
    CHECK(replayed.fallback_count==fallback);
    REQUIRE(replayed.predicted_origin);REQUIRE(replayed.predicted_velocity);
    CHECK(replayed.predicted_origin->x==seeded.predicted_origin->x);
    CHECK(replayed.predicted_velocity->x==Catch::Approx(100.F).margin(.001));
    REQUIRE(replayed.last_camera_correction_jump);
    CHECK(*replayed.last_camera_correction_jump<.001);
    return;
  }
  stage.update(active_at + 40ms);
  const auto during_jump = stage.live_reference_prediction_snapshot(active_at + 40ms);
  CHECK(during_jump.state == goldsrc::LiveReferencePredictionState::active);
  CHECK(during_jump.collision_brush_count == (brush ? 1U : 0U));
  if (brush) {
    REQUIRE(during_jump.collision_identity);
    CHECK(during_jump.collision_identity->profile == goldsrc::movement::LocalMovementCollisionProfile::reference_brush_scene_v1);
    CHECK(during_jump.collision_context_revision > 0U);
  }
  REQUIRE(during_jump.predicted_old_buttons);
  CHECK((*during_jump.predicted_old_buttons &
         goldsrc::kReferenceGoldSrcButtonJump) != 0U);
  const auto jump_sent = decode_sent(transport.sent.back());
  transport.queue(remote, server_packet(resource_sequence + 6U, false,
      jump_sent.header.sequence.sequence.value(), false,
      service_envelope(live_prediction_air_record(102.1F, 10U,0U,512U,100U,0U,brush))));
  stage.update(active_at + 50ms);
  CHECK(stage.live_reference_prediction_snapshot(active_at + 50ms).state ==
        goldsrc::LiveReferencePredictionState::active);
  stage.update(active_at + 140ms);
  const auto crouched = stage.live_reference_prediction_snapshot(active_at + 140ms);
  CAPTURE(crouched.reason, crouched.last_seed_status,
          crouched.last_ground_status);
  CHECK(crouched.state == goldsrc::LiveReferencePredictionState::active);
  CHECK(crouched.predicted_hull ==
        hlclient::movement::PlayerMovementHull::ducked);
  REQUIRE(crouched.predicted_old_buttons);
  CHECK((*crouched.predicted_old_buttons &
         goldsrc::kReferenceGoldSrcButtonDuck) != 0U);
  CHECK(crouched.local_steps > during_jump.local_steps);
  REQUIRE(crouched.predicted_origin);
  const auto committed_origin = *crouched.predicted_origin;
  const auto committed_steps = crouched.local_steps;
  for (const auto frame_step : {std::chrono::microseconds{33'333},
                                std::chrono::microseconds{16'667},
                                std::chrono::microseconds{6'944},
                                std::chrono::microseconds{11'000}}) {
    for (auto sample_at = active_at + 100ms;
         sample_at <= active_at + 140ms; sample_at += frame_step) {
      const auto sample = stage.live_reference_prediction_snapshot(sample_at);
      CHECK(sample.state == goldsrc::LiveReferencePredictionState::active);
      CHECK(sample.local_steps == committed_steps);
      REQUIRE(sample.predicted_origin);
      CHECK(sample.predicted_origin->x == committed_origin.x);
      CHECK(sample.predicted_origin->y == committed_origin.y);
      CHECK(sample.predicted_origin->z == committed_origin.z);
    }
  }
  stage.update(active_at + 180ms);
  const auto released = stage.live_reference_prediction_snapshot(active_at + 180ms);
  CHECK(released.state == goldsrc::LiveReferencePredictionState::active);
  CHECK(released.predicted_hull ==
        hlclient::movement::PlayerMovementHull::standing);
  REQUIRE(released.predicted_old_buttons);
  CHECK((*released.predicted_old_buttons &
         goldsrc::kReferenceGoldSrcButtonDuck) == 0U);
}

TEST_CASE("E25 clustered server charges retain continuous command-time camera presentation",
          "[e25][cluster-step-camera][goldsrc][live-runtime][reference-positive]") {
  FakeTransport transport;const auto remote=network::NetworkAddress::loopback(27852);
  const auto epoch=goldsrc::LiveRuntimeStageTimePoint{}+70s;hlclient::client::ClientWorldState world;
  auto config=live_test_config();config.operation_mode=goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source=goldsrc::LiveVisualControlInputSource::scripted_speed_check;
  config.reference_prediction=true;config.usercmd_scenario.forward_amplitude=100;
  config.usercmd_scenario.durations={20ms,2000ms,2000ms,2000ms,2000ms};
  goldsrc::LiveRuntimeStage stage{transport,remote,world,config};
  const auto driven=drive_live_initial(stage,transport,remote,epoch,false,false,true,true,true);
  const auto sequence=driven.last_server_sequence+1;
  transport.queue(remote,server_packet(sequence,false,driven.transition_request.header.sequence.sequence.value(),false,service_envelope(resource_semantic_payload())));
  transport.queue(remote,server_packet(sequence+1,false,driven.transition_request.header.sequence.sequence.value(),false,{std::byte{1}}));
  for(unsigned i=1;i<=8;++i)stage.update(driven.now+std::chrono::milliseconds{i});
  constexpr std::array empty_response{std::byte{5},std::byte{0},std::byte{0}};
  const auto response=require_latest_payload(transport,empty_response);const auto runtime_at=driven.now+9ms;
  transport.queue(remote,server_packet(sequence+2,false,response.header.sequence.sequence.value(),response.header.sequence.flags.reliable));stage.update(runtime_at);
  const auto spawn=require_latest_payload(transport,goldsrc::StockSpawnRequestBuilder::build(0x12345678,
      goldsrc::decode_stock_server_world_map_crc(0xdeadbeef,0)).encoding->semantic_bytes());
  transport.queue(remote,server_packet(sequence+3,true,spawn.header.sequence.sequence.value(),false,
      live_baseline_and_runtime_payload(100,100,80,true)));stage.update(runtime_at+1ms);stage.update(runtime_at+2ms);
  const auto sendents=require_latest_payload(transport,goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
  transport.queue(remote,server_packet(sequence+4,false,sendents.header.sequence.sequence.value(),sendents.header.sequence.flags.reliable,
      service_envelope(live_runtime_record(101,90,96))));stage.update(runtime_at+3ms);stage.update(runtime_at+2s+2ms);
  REQUIRE(stage.live_visual_input_ready());
  const auto bsp=goldsrc::bsp::GoldSrcBspParser::parse(hlclient::tests::literal_movement_bsp::make_slope_bsp_v30(0));REQUIRE(bsp);
  const auto collision=goldsrc::collision::GoldSrcCollisionWorldBuilder::build(bsp.document->collision_source);REQUIRE(collision);
  REQUIRE(stage.attach_reference_prediction_collision(collision.package));
  REQUIRE(stage.activate_live_visual_control(runtime_at+2s+3ms));const auto active_at=runtime_at+2s+23ms;
  stage.update(active_at);const auto sent=decode_sent(transport.sent.back());
  transport.queue(remote,server_packet(sequence+5,false,sent.header.sequence.sequence.value(),false,
      service_envelope(live_prediction_air_record(102,10,0,288,100,0,false,{},true))));stage.update(active_at+20ms);
  auto now=active_at+20ms;bool found_step=false;float maximum_physical_z=36.F;unsigned blocked_frames=0;std::string diagnostic;
  for(unsigned command=0;command<11;++command) {
    now+=20ms;stage.update(now);const auto endpoint=stage.live_reference_prediction_snapshot(now);
    INFO(endpoint.reason);REQUIRE(endpoint.state==goldsrc::LiveReferencePredictionState::active);REQUIRE(endpoint.predicted_origin);
    maximum_physical_z=std::max(maximum_physical_z,endpoint.predicted_origin->z);
    const auto immutable=world.runtime_observation();const auto tx=transport.sent.size();const auto steps=endpoint.local_steps;
    const auto early=stage.live_reference_prediction_snapshot(now-19ms);
    const auto middle=stage.live_reference_prediction_snapshot(now-10ms);
    const auto late=stage.live_reference_prediction_snapshot(now-1ms);
    REQUIRE(early.presented_origin);REQUIRE(middle.presented_origin);REQUIRE(late.presented_origin);
    REQUIRE(early.presented_view_offset);REQUIRE(middle.presented_view_offset);REQUIRE(late.presented_view_offset);
    blocked_frames+=middle.presentation_reason=="collision_blocked";
    const auto eye=[](const auto& p){return p.presented_origin->z+p.presented_view_offset->z;};
    diagnostic+="\n"+std::to_string(command)+": phys="+std::to_string(endpoint.predicted_origin->x)+","+std::to_string(endpoint.predicted_origin->z)+
      " reason="+std::string{middle.presentation_reason}+" eyes="+std::to_string(eye(early))+","+std::to_string(eye(middle))+","+std::to_string(eye(late));
    INFO(diagnostic);
    CHECK(eye(middle)>=eye(early));CHECK(eye(late)>=eye(middle));
    CHECK(middle.presented_origin->x>=early.presented_origin->x);
    CHECK(late.presented_origin->x>=middle.presented_origin->x);
    CHECK(late.presentation_reason!="collision_blocked");
    if(eye(late)>eye(early)+.5F) {
      found_step=true;
      CHECK(eye(middle)>eye(early));CHECK(eye(middle)<eye(late));
      CHECK(middle.presentation_reason!="collision_blocked");
    }
    CHECK(early.local_steps==steps);CHECK(middle.local_steps==steps);CHECK(late.local_steps==steps);
    CHECK(transport.sent.size()==tx);CHECK(world.runtime_observation()==immutable);
  }
  INFO("physical max z=" << maximum_physical_z << "; collision-blocked samples=" << blocked_frames);
  INFO(diagnostic);
  CHECK(maximum_physical_z==Catch::Approx(44).margin(.002));CHECK(found_step);
}

TEST_CASE("Reference live stage activates from owning RX and replays a later committed command",
          "[goldsrc][live-runtime][integration][reference-positive]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'849U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 50s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source =
      goldsrc::LiveVisualControlInputSource::scripted_speed_check;
  config.reference_prediction = true;
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  const auto driven = drive_live_initial(stage, transport, remote, epoch,
                                         false, false, true, true);
  const auto resource_sequence = driven.last_server_sequence + 1U;
  transport.queue(remote, server_packet(resource_sequence, false,
      driven.transition_request.header.sequence.sequence.value(), false,
      service_envelope(resource_semantic_payload())));
  transport.queue(remote, server_packet(resource_sequence + 1U, false,
      driven.transition_request.header.sequence.sequence.value(), false,
      {std::byte{1U}}));
  for (std::size_t step = 1U; step <= 8U; ++step)
    stage.update(driven.now + std::chrono::milliseconds{step});
  constexpr std::array empty_response{std::byte{0x05U}, std::byte{0U},
                                      std::byte{0U}};
  const auto response = require_latest_payload(transport, empty_response);
  const auto first_runtime_at = driven.now + 9ms;
  transport.queue(remote, server_packet(resource_sequence + 2U, false,
      response.header.sequence.sequence.value(),
      response.header.sequence.flags.reliable));
  stage.update(first_runtime_at);
  const auto spawn = require_latest_payload(transport,
      goldsrc::StockSpawnRequestBuilder::build(
          0x1234'5678U,
          goldsrc::decode_stock_server_world_map_crc(0xdead'beefU, 0U))
          .encoding->semantic_bytes());
  transport.queue(remote, server_packet(resource_sequence + 3U, true,
      spawn.header.sequence.sequence.value(), false,
      live_baseline_and_runtime_payload(100.0F, 100U, 80U, true)));
  stage.update(first_runtime_at + 1ms);
  stage.update(first_runtime_at + 2ms);
  const auto sendents = require_latest_payload(transport,
      goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
  transport.queue(remote, server_packet(resource_sequence + 4U, false,
      sendents.header.sequence.sequence.value(),
      sendents.header.sequence.flags.reliable,
      service_envelope(live_runtime_record(101.0F, 90U, 96U))));
  stage.update(first_runtime_at + 3ms);
  stage.update(first_runtime_at + 2s + 2ms);
  REQUIRE(stage.live_visual_input_ready());
  REQUIRE(stage.attach_reference_prediction_collision(
      hlclient::tests::collision_brush_fixture::package(false)));
  REQUIRE(stage.activate_live_visual_control(first_runtime_at + 2s + 3ms));
  const auto active_at = first_runtime_at + 2s + 23ms;
  stage.update(active_at);
  REQUIRE(stage.usercmd_transmit_count() > 0U);
  const auto first_sent = decode_sent(transport.sent.back());
  transport.queue(remote, server_packet(resource_sequence + 5U, false,
      first_sent.header.sequence.sequence.value(), false,
      service_envelope(live_prediction_air_record(102.0F, 10U))));
  stage.update(active_at + 20ms);
  const auto initial = stage.live_reference_prediction_snapshot(active_at + 20ms);
  CAPTURE(initial.reason, initial.last_seed_status, initial.last_seed_field,
          initial.last_ground_status);
  CAPTURE(static_cast<int>(stage.state()));
  CAPTURE(stage.error() ? stage.error()->context : std::string{});
  REQUIRE_FALSE(stage.terminal());
  REQUIRE(initial.state == goldsrc::LiveReferencePredictionState::active);
  REQUIRE(initial.predicted_origin);
  stage.update(active_at + 40ms);
  const auto advanced = stage.live_reference_prediction_snapshot(active_at + 40ms);
  CHECK(advanced.local_steps > 0U);
  REQUIRE(advanced.canonical_origin);
  REQUIRE(advanced.predicted_origin);
  CHECK(advanced.predicted_origin->z < advanced.canonical_origin->z);
  const auto between = stage.live_reference_prediction_snapshot(active_at + 50ms);
  REQUIRE(between.presented_origin);
  CHECK(between.local_steps == advanced.local_steps);
  CHECK(between.presented_origin->z > advanced.predicted_origin->z);
  CHECK(between.presented_origin->z < initial.predicted_origin->z);
  // A second record at the same render time corrects the first command
  // boundary exactly. It must not turn the in-flight local interpolation into
  // an immediate publication of the later predicted endpoint.
  const auto before_rebase = stage.live_reference_prediction_snapshot(active_at + 50ms);
  REQUIRE(before_rebase.presented_origin);
  transport.queue(remote, server_packet(resource_sequence + 6U, false,
      first_sent.header.sequence.sequence.value(), false,
      service_envelope(live_prediction_air_record(102.1F, 10U))));
  stage.update(active_at + 50ms);
  const auto corrected = stage.live_reference_prediction_snapshot(active_at + 50ms);
  CAPTURE(corrected.reason, corrected.last_seed_status,
          corrected.last_ground_status, corrected.replayed_commands);
  REQUIRE_FALSE(stage.terminal());
  CHECK(corrected.accepted_corrections > 0U);
  CHECK(corrected.replayed_commands > 0U);
  REQUIRE(corrected.presented_origin);
  CHECK(corrected.last_raw_position_error.value_or(100.0) < 0.05);
  REQUIRE(before_rebase.presentation_alpha);
  REQUIRE(corrected.presentation_alpha);
  CHECK(before_rebase.presentation_reason == "interpolated");
  CHECK(corrected.presentation_reason == "interpolated");
  CHECK(before_rebase.presentation_from_command ==
        corrected.presentation_from_command);
  CHECK(before_rebase.presentation_to_command ==
        corrected.presentation_to_command);
  CHECK(before_rebase.presentation_from_time_ns ==
        corrected.presentation_from_time_ns);
  CHECK(before_rebase.presentation_to_time_ns ==
        corrected.presentation_to_time_ns);
  CHECK(std::abs(*corrected.presentation_alpha - 0.5) < 0.000001);
  const auto correction_jump = std::abs(corrected.presented_origin->z -
                                        before_rebase.presented_origin->z);
  CAPTURE(correction_jump);
  CHECK(std::abs(corrected.presented_origin->z -
                 before_rebase.presented_origin->z) < 0.05F);
  REQUIRE(corrected.last_camera_correction_jump);
  CHECK(*corrected.last_camera_correction_jump < 0.05);
  const auto repeated = stage.live_reference_prediction_snapshot(active_at + 50ms);
  CHECK(repeated.presented_origin->z == corrected.presented_origin->z);
  CHECK(repeated.presentation_scratch_bytes ==
        corrected.presentation_scratch_bytes);
  CHECK(repeated.presentation_trace_queries_total ==
        corrected.presentation_trace_queries_total);
  const auto next_same_history =
      stage.live_reference_prediction_snapshot(active_at + 51ms);
  CHECK(next_same_history.presentation_trace_queries_total ==
        corrected.presentation_trace_queries_total + 1U);
  CHECK(next_same_history.presentation_scratch_growths_total ==
        corrected.presentation_scratch_growths_total);
  CHECK(next_same_history.presentation_scratch_bytes ==
        corrected.presentation_scratch_bytes);
  const auto steps_before_catchup = corrected.local_steps;
  stage.update(active_at + 100ms);
  const auto caught_up = stage.live_reference_prediction_snapshot(active_at + 90ms);
  CHECK(caught_up.local_steps >= steps_before_catchup + 2U);
  REQUIRE(caught_up.presentation_from_time_ns);
  REQUIRE(caught_up.presentation_to_time_ns);
  CHECK(*caught_up.presentation_to_time_ns -
        *caught_up.presentation_from_time_ns == 20'000'000);
  REQUIRE(caught_up.presentation_alpha);
  CHECK(std::abs(*caught_up.presentation_alpha - 0.5) < 0.000001);
  const auto next_render_sample =
      stage.live_reference_prediction_snapshot(active_at + 91ms);
  REQUIRE(next_render_sample.presentation_alpha);
  CHECK(*next_render_sample.presentation_alpha >
        *caught_up.presentation_alpha);
  const auto committed_steps = caught_up.local_steps;
  const auto committed_origin = caught_up.predicted_origin;
  REQUIRE(committed_origin);
  const auto canonical_revision = world.runtime_publication_revision();
  const auto committed_commands = stage.live_usercmd_snapshot();
  REQUIRE(committed_commands);
  for (const auto spacing_us : std::array{33'333, 16'667, 6'944}) {
    for (auto elapsed_us = 0; elapsed_us <= 50'000;
         elapsed_us += spacing_us) {
      const auto render = stage.live_reference_prediction_snapshot(
          active_at + 50ms + std::chrono::microseconds{elapsed_us});
      CHECK(render.local_steps == committed_steps);
      REQUIRE(render.predicted_origin);
      CHECK(render.predicted_origin->x == committed_origin->x);
      CHECK(render.predicted_origin->y == committed_origin->y);
      CHECK(render.predicted_origin->z == committed_origin->z);
      if (render.presentation_alpha) {
        CHECK(std::isfinite(*render.presentation_alpha));
        CHECK(*render.presentation_alpha >= 0.0);
        CHECK(*render.presentation_alpha <= 1.0);
      }
    }
  }
  for (const auto elapsed_us : std::array{1'000, 9'700, 22'400, 45'300}) {
    const auto render = stage.live_reference_prediction_snapshot(
        active_at + 50ms + std::chrono::microseconds{elapsed_us});
    CHECK(render.local_steps == committed_steps);
    REQUIRE(render.predicted_origin);
    CHECK(render.predicted_origin->x == committed_origin->x);
    CHECK(render.predicted_origin->y == committed_origin->y);
    CHECK(render.predicted_origin->z == committed_origin->z);
  }
  CHECK(world.runtime_publication_revision() == canonical_revision);
  const auto after_render_sampling = stage.live_usercmd_snapshot();
  REQUIRE(after_render_sampling);
  CHECK(after_render_sampling->generated_command_count ==
        committed_commands->generated_command_count);
  const auto before_repeated_anchor =
      stage.live_reference_prediction_snapshot(active_at + 110ms);
  REQUIRE(before_repeated_anchor.presented_origin);
  transport.queue(remote, server_packet(resource_sequence + 7U, false,
      first_sent.header.sequence.sequence.value(), false,
      service_envelope(live_prediction_air_record(102.2F, 10U))));
  stage.update(active_at + 110ms);
  const auto repeated_anchor =
      stage.live_reference_prediction_snapshot(active_at + 110ms);
  CHECK(repeated_anchor.accepted_corrections ==
        corrected.accepted_corrections + 1U);
  REQUIRE(repeated_anchor.last_camera_correction_jump);
  CHECK(*repeated_anchor.last_camera_correction_jump < 0.05);
  CHECK(repeated_anchor.local_steps == committed_steps);
  transport.queue(remote, server_packet(resource_sequence + 8U, false,
      first_sent.header.sequence.sequence.value(), false,
      service_envelope(live_prediction_air_record(102.3F, 10U, 1U))));
  stage.update(active_at + 120ms);
  const auto newest_sent = decode_sent(transport.sent.back());
  const auto small_correction =
      stage.live_reference_prediction_snapshot(active_at + 120ms);
  CHECK(small_correction.accepted_corrections ==
        repeated_anchor.accepted_corrections + 1U);
  REQUIRE(small_correction.last_raw_position_error);
  CHECK(*small_correction.last_raw_position_error > 0.1);
  CHECK(*small_correction.last_raw_position_error < 0.2);
  REQUIRE(small_correction.last_camera_correction_jump);
  CHECK(*small_correction.last_camera_correction_jump < 0.25);
  transport.queue(remote, server_packet(resource_sequence + 8U, false,
      first_sent.header.sequence.sequence.value(), false,
      service_envelope(live_prediction_air_record(102.3F, 10U, 1U))));
  stage.update(active_at + 125ms);
  CHECK(stage.live_reference_prediction_snapshot(active_at + 125ms)
            .accepted_corrections == small_correction.accepted_corrections);
  const auto before_latest_anchor =
      stage.live_reference_prediction_snapshot(active_at + 130ms);
  REQUIRE(before_latest_anchor.presented_origin);
  REQUIRE(before_latest_anchor.predicted_origin);
  const auto matched_z_eighths = static_cast<std::uint32_t>(std::lround(
      before_latest_anchor.predicted_origin->z * 8.0F));
  transport.queue(remote, server_packet(resource_sequence + 9U, false,
      newest_sent.header.sequence.sequence.value(), false,
      service_envelope(live_prediction_air_record(
          102.4F, 10U, 1U, matched_z_eighths))));
  stage.update(active_at + 130ms);
  const auto latest_anchor =
      stage.live_reference_prediction_snapshot(active_at + 130ms);
  CHECK(latest_anchor.accepted_corrections ==
        small_correction.accepted_corrections + 1U);
  REQUIRE(latest_anchor.last_raw_position_error);
  CHECK(*latest_anchor.last_raw_position_error < 0.1);
  REQUIRE(latest_anchor.last_camera_correction_jump);
  CAPTURE(*latest_anchor.last_camera_correction_jump);
  CHECK(*latest_anchor.last_camera_correction_jump < 0.1);
  CHECK(latest_anchor.presentation_reason == "visual_correction");
  const auto before_visual_repeat =
      stage.live_reference_prediction_snapshot(active_at + 135ms);
  REQUIRE(before_visual_repeat.presented_origin);
  transport.queue(remote, server_packet(resource_sequence + 10U, false,
      newest_sent.header.sequence.sequence.value(), false,
      service_envelope(live_prediction_air_record(
          102.5F, 10U, 1U, matched_z_eighths))));
  stage.update(active_at + 135ms);
  const auto visual_repeat =
      stage.live_reference_prediction_snapshot(active_at + 135ms);
  CHECK(visual_repeat.accepted_corrections ==
        latest_anchor.accepted_corrections + 1U);
  REQUIRE(visual_repeat.last_camera_correction_jump);
  CHECK(*visual_repeat.last_camera_correction_jump < 0.1);
  const auto halfway_visual =
      stage.live_reference_prediction_snapshot(active_at + 140ms);
  const auto completed_visual =
      stage.live_reference_prediction_snapshot(active_at + 151ms);
  REQUIRE(halfway_visual.presented_origin);
  REQUIRE(completed_visual.presented_origin);
  REQUIRE(completed_visual.predicted_origin);
  CHECK(halfway_visual.presented_origin->z !=
        completed_visual.presented_origin->z);
  CHECK(std::abs(completed_visual.presented_origin->z -
                 completed_visual.predicted_origin->z) < 0.001F);
  CHECK(world.runtime_observation());
}

TEST_CASE("Live runtime profile reaches the resource request for alternate "
          "initial encodings",
          "[goldsrc][live-runtime][integration][fragment][compressed]") {
  const auto remote = network::NetworkAddress::loopback(27'843U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 30s;

  SECTION("compressed unfragmented") {
    FakeTransport transport;
    hlclient::client::ClientWorldState world;
    goldsrc::LiveRuntimeStage stage{transport, remote, world,
                                    live_test_config()};
    const auto driven =
        drive_live_initial(stage, transport, remote, epoch, true, false);
    CHECK(driven.last_server_sequence == 1U);
    CHECK_FALSE(stage.terminal());
  }

  SECTION("uncompressed fragmented and reassembled") {
    FakeTransport transport;
    hlclient::client::ClientWorldState world;
    goldsrc::LiveRuntimeStage stage{transport, remote, world,
                                    live_test_config()};
    const auto driven =
        drive_live_initial(stage, transport, remote, epoch, false, true);
    CHECK(driven.last_server_sequence > 1U);
    CHECK_FALSE(stage.terminal());
  }
}

TEST_CASE("E31 application game profile rejects incompatible signon before runtime publication",
          "[e31][goldsrc][live-runtime][no-stock]") {
  FakeTransport transport;
  const network::NetworkAddress remote{0xc0a80066U,27016};
  const auto epoch=goldsrc::LiveRuntimeStageTimePoint{}+30s;
  hlclient::client::ClientWorldState world;
  auto config=live_test_config();config.required_game_directory="incompatible_fixture";
  REQUIRE(goldsrc::valid_live_runtime_stage_configuration(config));
  goldsrc::LiveRuntimeStage stage{transport,remote,world,config};
  const auto driven=drive_live_initial(stage,transport,remote,epoch,false,false);
  const auto resource=driven.last_server_sequence+1U;
  transport.queue(remote,server_packet(resource,false,
      driven.transition_request.header.sequence.sequence.value(),false,
      service_envelope(resource_semantic_payload())));
  transport.queue(remote,server_packet(resource+1U,false,
      driven.transition_request.header.sequence.sequence.value(),false,{std::byte{1}}));
  for(std::size_t step=1;step<=8;++step)stage.update(driven.now+std::chrono::milliseconds{step});
  constexpr std::array empty_response{std::byte{5},std::byte{0},std::byte{0}};
  const auto response=require_latest_payload(transport,empty_response);
  transport.queue(remote,server_packet(resource+2U,false,response.header.sequence.sequence.value(),
      response.header.sequence.flags.reliable));
  stage.update(driven.now+9ms);
  REQUIRE(stage.error());CHECK(stage.error()->code==goldsrc::LiveRuntimeStageErrorCode::unsupported_game_profile);
  CHECK_FALSE(world.runtime_observation());
  config.required_game_directory="../valve";CHECK_FALSE(goldsrc::valid_live_runtime_stage_configuration(config));
  config.required_game_directory="valve";CHECK(goldsrc::valid_live_runtime_stage_configuration(config));
}

TEST_CASE(
    "Live runtime preserves nested progress and bounded transition failure "
    "metadata",
    "[goldsrc][live-runtime][integration][progress][diagnostic][regression]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'845U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 35s;
  hlclient::client::ClientWorldState world;
  bool serverinfo_ready = false;
  bool registry_ready = false;
  bool movevars_ready = false;
  bool user_info_ready = false;
  bool request_queued = false;
  bool request_transmitted = false;
  bool request_acknowledged = false;
  std::optional<goldsrc::ResourceTransitionFailureMetadata> retained_failure;

  goldsrc::LiveRuntimeStage stage{
      transport,
      remote,
      world,
      live_test_config(),
      nullptr,
      {},
      {},
      [&serverinfo_ready](const auto &event) {
        serverinfo_ready = serverinfo_ready ||
                           event.classification ==
                               goldsrc::PreResourceSignonTraceClassification::
                                   server_info_ready;
      },
      [&registry_ready](const auto &event) {
        registry_ready =
            registry_ready || event.classification ==
                                  goldsrc::DeltaDescriptionTraceClassification::
                                      delta_registry_ready;
      },
      [&movevars_ready](const auto &event) {
        movevars_ready = movevars_ready ||
                         event.classification ==
                             goldsrc::MovementEnvironmentTraceClassification::
                                 movement_environment_ready;
      },
      [&user_info_ready](const auto &event) {
        user_info_ready =
            user_info_ready || event.classification ==
                                   goldsrc::UserInfoSignonTraceClassification::
                                       first_batch_complete;
      },
      [&](const goldsrc::ResourceTransitionTraceEvent &event) {
        request_queued = request_queued ||
                         event.classification ==
                             goldsrc::ResourceTransitionTraceClassification::
                                 transition_request_queued;
        request_transmitted =
            request_transmitted ||
            event.classification ==
                goldsrc::ResourceTransitionTraceClassification::
                    transition_request_transmitted;
        request_acknowledged =
            request_acknowledged ||
            event.classification ==
                goldsrc::ResourceTransitionTraceClassification::
                    transition_request_acknowledged;
        if (event.failure_metadata) {
          retained_failure = event.failure_metadata;
        }
      },
      {},
      {}};

  const auto driven =
      drive_live_initial(stage, transport, remote, epoch, false, false);
  const std::vector malformed{std::byte{44U}, std::byte{1U}, std::byte{45U},
                              std::byte{2U},  std::byte{3U}, std::byte{43U}};
  transport.queue(
      remote,
      server_packet(driven.last_server_sequence + 1U, false,
                    driven.transition_request.header.sequence.sequence.value(),
                    false, malformed));
  stage.update(driven.now + 1ms);

  REQUIRE(stage.terminal());
  REQUIRE(stage.error());
  CHECK(stage.error()->code ==
        goldsrc::LiveRuntimeStageErrorCode::response_stage_failed);
  CHECK(stage.error()->resource_list_code ==
        goldsrc::ResourceListStageErrorCode::transition_stage_failed);
  CHECK(stage.error()->transition_stage_code ==
        goldsrc::ResourceTransitionStageErrorCode::
            intermediate_message_decode_failed);
  CHECK_FALSE(stage.error()->transition_control_code);
  REQUIRE(stage.error()->transition_failure_metadata);
  CHECK(stage.error()->transition_failure_metadata->cursor_byte_value == 44U);
  CHECK(stage.error()->transition_failure_metadata->actual_opcode == 44U);
  CHECK(serverinfo_ready);
  CHECK(registry_ready);
  CHECK(movevars_ready);
  CHECK(user_info_ready);
  CHECK(request_queued);
  CHECK(request_transmitted);
  CHECK(request_acknowledged);
  REQUIRE(retained_failure);
  CHECK(retained_failure->expected_opcode == 45U);
  CHECK(retained_failure->actual_opcode == 44U);
  CHECK(retained_failure->cursor_byte_value == 44U);
  CHECK(retained_failure->cursor_boundary_kind ==
        goldsrc::ResourceTransitionCursorBoundaryKind::
            validated_message_boundary);
  CHECK(retained_failure->payload_ordinal == 1U);
  CHECK(retained_failure->wire_encoding ==
        goldsrc::ResourceTransitionWireEncodingKind::wire_uncompressed);
  CHECK(retained_failure->decoded_byte_count == malformed.size());
  CHECK(retained_failure->intermediate_parser_error ==
        goldsrc::RuntimeControlDecodeErrorCode::unsupported_opcode);
  CHECK_FALSE(retained_failure->parser_error);
  CHECK_FALSE(world.runtime_observation());
  CHECK(world.runtime_publication_revision() == 0U);
}

TEST_CASE(
    "Live runtime exact BZ2 marker corruption fails without raw publication",
    "[goldsrc][live-runtime][integration][envelope][security]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'844U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 40s;
  hlclient::client::ClientWorldState world;
  goldsrc::LiveRuntimeStage stage{transport, remote, world, live_test_config()};
  REQUIRE(stage.start(epoch, transport.local));
  stage.update(epoch + 1ms);
  REQUIRE_FALSE(transport.sent.empty());
  const auto request = decode_sent(transport.sent.front());

  transport.queue(
      remote,
      server_packet(1U, true, request.header.sequence.sequence.value(),
                    request.header.sequence.flags.reliable,
                    {std::byte{0x42U}, std::byte{0x5aU}, std::byte{0x32U},
                     std::byte{0x00U}, std::byte{0x42U}, std::byte{0x5aU}}));
  stage.update(epoch + 2ms);

  REQUIRE(stage.terminal());
  REQUIRE(stage.error());
  CHECK(stage.error()->code ==
        goldsrc::LiveRuntimeStageErrorCode::response_stage_failed);
  CHECK_FALSE(stage.result());
  CHECK_FALSE(world.runtime_observation());
  CHECK(world.runtime_publication_revision() == 0U);
}

TEST_CASE("Live runtime selector is explicit and leaves historical strict "
          "defaults unchanged",
          "[goldsrc][live-runtime][profile][regression]") {
  goldsrc::ResourceClientResponseStageConfig historical;
  CHECK(historical.resource_list.transition.user_info.movement_environment.delta
            .pre_resource.initial_signon.driver.idle_poll_interval == 0ms);
  CHECK(historical.post_response_payload_compression ==
        goldsrc::ServicePayloadCompressionPolicy::require_bzip2_envelope);
  CHECK(
      historical.resource_list.transition.second_service_payload_compression ==
      goldsrc::ServicePayloadCompressionPolicy::require_bzip2_envelope);
  CHECK(historical.pre_transmit_payload_policy ==
        goldsrc::ResourceResponsePreTransmitPayloadPolicy::reject);
  CHECK(historical.completion_policy ==
        goldsrc::ResourceResponseCompletionPolicy::
            require_post_response_boundary);
  CHECK(historical.resource_list.transition.user_info.movement_environment.delta
            .pre_resource.initial_signon.service_payload_envelope
            .compression_policy ==
        goldsrc::ServicePayloadCompressionPolicy::require_bzip2_envelope);

  auto live = live_test_config();
  CHECK(live.resource_response.resource_list.transition.user_info.movement_environment.delta
            .pre_resource.initial_signon.driver.idle_poll_interval == 200ms);
  CHECK(live.resource_response.pre_transmit_payload_policy ==
        goldsrc::ResourceResponsePreTransmitPayloadPolicy::decode_nop_control);
  CHECK(live.resource_response.completion_policy ==
        goldsrc::ResourceResponseCompletionPolicy::covering_acknowledgement);
  CHECK(live.envelope.compression_policy ==
        goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed);
  CHECK(live.resource_response.post_response_payload_compression ==
        goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed);
  CHECK(live.resource_response.resource_list.transition
            .second_service_payload_compression ==
        goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed);
  CHECK(live.resource_response.resource_list.transition.user_info
            .movement_environment.delta.pre_resource.initial_signon
            .service_payload_envelope.compression_policy ==
        goldsrc::ServicePayloadCompressionPolicy::accept_bzip2_or_uncompressed);
}

TEST_CASE("Live F input cannot activate before the same-session production handoff",
           "[goldsrc][live-runtime][live-visual][readiness]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'846U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 45s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source =
      goldsrc::LiveVisualControlInputSource::keyboard_mouse;
  REQUIRE(goldsrc::valid_live_runtime_stage_configuration(config));
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  const goldsrc::LiveVisualControlInput input{
      1U, 1U, 1.0F, 0.0F, 0.0, 0.0, true, true};

  CHECK(stage.live_resource_list() == nullptr);
  CHECK(stage.live_server_info() == nullptr);
  CHECK(stage.live_sky_name().empty());
  CHECK_FALSE(stage.submit_live_visual_input(input, epoch));
  CHECK_FALSE(stage.activate_live_visual_control(epoch));
  REQUIRE(stage.start(epoch, transport.local));
  CHECK_FALSE(stage.live_visual_input_ready());
  CHECK_FALSE(stage.submit_live_visual_input(input, epoch + 1ms));
  CHECK_FALSE(stage.activate_live_visual_control(epoch + 1ms));
  CHECK(stage.usercmd_transmit_count() == 0U);
  stage.cancel(epoch + 2ms);
  CHECK(stage.terminal());
}

TEST_CASE("Both bounded live F scripts enter the scenario completion gate",
          "[goldsrc][live-runtime][live-visual][completion]") {
  using goldsrc::LiveVisualControlInputSource;
  CHECK(goldsrc::bounded_live_visual_scenario(
      LiveVisualControlInputSource::scripted_check));
  CHECK(goldsrc::bounded_live_visual_scenario(
      LiveVisualControlInputSource::scripted_side_check));
  CHECK(goldsrc::bounded_live_visual_scenario(
      LiveVisualControlInputSource::scripted_fire_reload_presentation_check));
  CHECK_FALSE(goldsrc::bounded_live_visual_scenario(
      LiveVisualControlInputSource::keyboard_mouse));
}

TEST_CASE("Live keyboard diagnostic limits cover the full CLI session",
          "[goldsrc][live-runtime][live-visual][capacity]") {
  // The 513th packet in a physical-input session must not exhaust a
  // diagnostic limit while the user still has time to press A/D.
  constexpr std::size_t maximum_commands = 300'000U / 20U;
  CHECK(goldsrc::kMaximumLiveUserCmdTransmitRanges > maximum_commands);
  CHECK(goldsrc::kMaximumLiveUserCmdSamples >= 300U * 100U);
  auto config = live_test_config();
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source =
      goldsrc::LiveVisualControlInputSource::keyboard_mouse;
  CHECK(goldsrc::valid_live_runtime_stage_configuration(config));
  config.timeout = std::chrono::seconds{90};
  CHECK_FALSE(goldsrc::valid_live_runtime_stage_configuration(config));
  config.live_visual_input_source =
      goldsrc::LiveVisualControlInputSource::scripted_fire_reload_presentation_check;
  CHECK(goldsrc::valid_live_runtime_stage_configuration(config));
  config.timeout = std::chrono::seconds{91};
  CHECK_FALSE(goldsrc::valid_live_runtime_stage_configuration(config));
}

TEST_CASE("C fake peer keeps RX TX command identity and reseeds only after a fresh new-life carrier",
          "[goldsrc][live-runtime][damage-respawn][integration][use][weapon-stall-time]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27851U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 60s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source = goldsrc::LiveVisualControlInputSource::keyboard_mouse;
  config.reference_prediction = true;
  goldsrc::LiveRuntimeStage stage{transport,remote,world,config};
  const auto driven = drive_live_initial(stage,transport,remote,epoch,false,false,true,true);
  const auto resource = driven.last_server_sequence + 1U;
  transport.queue(remote,server_packet(resource,false,
      driven.transition_request.header.sequence.sequence.value(),false,
      service_envelope(resource_semantic_payload())));
  transport.queue(remote,server_packet(resource+1U,false,
      driven.transition_request.header.sequence.sequence.value(),false,{std::byte{1}}));
  for (std::size_t step=1; step<=8; ++step) stage.update(driven.now+std::chrono::milliseconds{step});
  constexpr std::array empty_response{std::byte{5},std::byte{0},std::byte{0}};
  const auto response = require_latest_payload(transport,empty_response);
  const auto first = driven.now+9ms;
  transport.queue(remote,server_packet(resource+2U,false,response.header.sequence.sequence.value(),
      response.header.sequence.flags.reliable));
  stage.update(first);
  const auto spawn = require_latest_payload(transport,goldsrc::StockSpawnRequestBuilder::build(
      0x12345678U,goldsrc::decode_stock_server_world_map_crc(0xdeadbeefU,0U)).encoding->semantic_bytes());
  transport.queue(remote,server_packet(resource+3U,true,spawn.header.sequence.sequence.value(),
      false,live_baseline_and_runtime_payload(100.0F,100U,80U,true)));
  stage.update(first+1ms); stage.update(first+2ms);
  const auto sendents = require_latest_payload(transport,goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
  transport.queue(remote,server_packet(resource+4U,false,sendents.header.sequence.sequence.value(),
      sendents.header.sequence.flags.reliable,service_envelope(live_prediction_air_record(101.0F,10U))));
  stage.update(first+3ms); stage.update(first+2s+2ms);
  REQUIRE(stage.live_visual_input_ready());
  REQUIRE(stage.attach_reference_prediction_collision(hlclient::tests::collision_brush_fixture::package(false)));
  auto now = first+2s+3ms;
  goldsrc::LiveVisualControlInput input{1,1,0,0,0,0,true,true};
  REQUIRE(stage.submit_live_visual_input(input,now));
  REQUIRE(stage.activate_live_visual_control(now));
  now += 20ms;
  stage.update(now);
  std::uint32_t server_sequence = resource+4U;
  auto last_transport = decode_sent(transport.sent.back()).header.sequence.sequence.value();
  const auto feed = [&](std::uint32_t hp,std::uint32_t flag,std::uint32_t x,
                        std::uint32_t ack) {
    transport.queue(remote,server_packet(++server_sequence,false,ack,false,
        service_envelope(live_prediction_air_record(102.0F,x,0,512,hp,flag))));
    now += 20ms;
    stage.update(now);
    REQUIRE_FALSE(stage.terminal());
    const auto transmitted = decode_sent(transport.sent.back()).header.sequence.sequence.value();
    CHECK(transmitted > last_transport);
    last_transport = transmitted;
  };
  feed(100,0,10,last_transport);
  REQUIRE(stage.live_reference_prediction_snapshot(now).state == goldsrc::LiveReferencePredictionState::active);
  for (std::uint64_t cycle=1; cycle<=3; ++cycle) {
    const auto use_action = hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::use);
    ++input.input_revision; input.held_buttons=use_action; input.pressed_buttons=use_action;
    REQUIRE(stage.submit_live_visual_input(input,now));
    now += 20ms; stage.update(now);
    const auto use_count = stage.live_usercmd_snapshot()->use_new_submission_count;
    CHECK(use_count > 0U);
    CHECK(stage.live_usercmd_snapshot()->command_interval == 20ms);
    CHECK(stage.live_reference_prediction_snapshot(now).state == goldsrc::LiveReferencePredictionState::active);
    const auto old_carrier = last_transport;
    feed(0,2,10,last_transport);
    CHECK(world.runtime_observation()->lifecycle.dead());
    auto suspended = stage.live_reference_prediction_snapshot(now);
    CHECK(suspended.reason == "local_player_dead");
    CHECK(suspended.history_depth == 0);
    CHECK_FALSE(suspended.presented_origin);
    ++input.input_revision; input.pressed_buttons=0U; // stale held E after death
    REQUIRE(stage.submit_live_visual_input(input,now));
    const auto at_death = stage.live_usercmd_snapshot()->use_new_submission_count;
    now += 20ms; stage.update(now);
    CHECK(stage.live_usercmd_snapshot()->use_new_submission_count == at_death);
    CHECK(stage.live_usercmd_snapshot()->use_clear_after_release_count > 0U);
    // Release then normal LMB request uses unchanged production usercmd path.
    ++input.input_revision; input.held_buttons = 0; input.pressed_buttons = 0;
    REQUIRE(stage.submit_live_visual_input(input,now));
    now += 20ms; stage.update(now);
    const auto before_press = stage.live_usercmd_snapshot()->attack_new_submission_count;
    ++input.input_revision;
    input.held_buttons = hlclient::gameplay_input::gameplay_button_mask(
        hlclient::gameplay_input::GameplayButton::attack_primary);
    input.pressed_buttons = input.held_buttons;
    REQUIRE(stage.submit_live_visual_input(input,now));
    now += 20ms; stage.update(now);
    CHECK(stage.live_usercmd_snapshot()->attack_new_submission_count > before_press);
    feed(0,3,10,last_transport);
    CHECK(world.runtime_observation()->lifecycle.state == hlclient::client::LocalPlayerLifeState::awaiting_respawn);
    ++input.input_revision; input.held_buttons = 0; input.pressed_buttons = 0;
    REQUIRE(stage.submit_live_visual_input(input,now));
    // New alive location with old carrier cannot seed the new life.
    feed(100,0,100,old_carrier);
    CHECK(world.runtime_observation()->lifecycle.respawns == cycle);
    CHECK(stage.live_reference_prediction_snapshot(now).state != goldsrc::LiveReferencePredictionState::active);
    now += 20ms; stage.update(now); // strictly post-boundary neutral carrier
    const auto new_carrier = decode_sent(transport.sent.back()).header.sequence.sequence.value();
    feed(100,0,100,new_carrier);
    const auto reseeded = stage.live_reference_prediction_snapshot(now);
    CAPTURE(reseeded.reason, reseeded.last_seed_status);
    REQUIRE(reseeded.state == goldsrc::LiveReferencePredictionState::active);
    REQUIRE(reseeded.presented_origin);
    CHECK(reseeded.presented_origin->x > 99.0F); // no corpse-to-spawn interpolation
    CHECK(world.runtime_observation()->generation == 1);
    CHECK(world.runtime_observation()->lifecycle.life_epoch == cycle+1);
  }
  const auto accounting = stage.live_usercmd_snapshot();
  REQUIRE(accounting);
  CHECK(accounting->server_samples.size() >= 13);
  CHECK(accounting->driver_rx_total.owning_datagrams > accounting->driver_rx_at_input_activation.owning_datagrams);
  // Same production owner/driver/history: a real long frame must not cause
  // exit 2 or synthesize attacks/Use over the missing wall-clock samples.
  while (stage.poll_weapon_command_submission()) {}
  ++input.input_revision;
  input.forward_axis = 1.0F;
  input.held_buttons = input.pressed_buttons =
      hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::move_forward) |
      hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::attack_primary) |
      hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::use);
  now += 601ms;
  REQUIRE(stage.submit_live_visual_input(input, now));
  transport.queue(remote,server_packet(++server_sequence,false,last_transport,false,
      service_envelope(live_prediction_air_record(103.0F,100,0,512,100,0))));
  stage.update(now);
  REQUIRE_FALSE(stage.terminal());
  const auto recovered = stage.live_usercmd_snapshot();
  REQUIRE(recovered);
  CHECK(recovered->scheduler_stall_recoveries == 1U);
  CHECK(recovered->scheduler_discarded_wall_time_samples == 29U);
  CHECK(recovered->generated_command_count == accounting->generated_command_count + 1U);
  CHECK(recovered->attack_new_submission_count == accounting->attack_new_submission_count);
  CHECK(recovered->use_new_submission_count == accounting->use_new_submission_count);
  CHECK(recovered->last_sampled_forward_axis == 0.0F);
  CHECK(recovered->scheduler_maximum_commands_per_update == 8U);
  CHECK(recovered->runtime_records_committed_total > accounting->runtime_records_committed_total);
  // The existing presentation stream also carries neutral releases. Keep
  // exactly that notification, never a retroactive shot/Use or duplicate.
  const auto neutral_submission = stage.poll_weapon_command_submission();
  REQUIRE(neutral_submission);
  CHECK(neutral_submission->buttons == 0U);
  CHECK(neutral_submission->command_sequence == recovered->transmit_ranges.back().last_new_command_sequence);
  CHECK(neutral_submission->command_end_nanoseconds ==
        recovered->scheduler_next_sample_time_nanoseconds - 20'000'000);
  CHECK_FALSE(stage.poll_weapon_command_submission());
  REQUIRE(recovered->transmit_ranges.back().last_new_command_sequence ==
          accounting->transmit_ranges.back().last_new_command_sequence + 1U);
  // Release attack/Use while W remains physically held; the numeric axis and
  // directional button state must describe the same input snapshot.
  ++input.input_revision;
  input.held_buttons = hlclient::gameplay_input::gameplay_button_mask(
      hlclient::gameplay_input::GameplayButton::move_forward);
  input.pressed_buttons = 0U;
  REQUIRE(stage.submit_live_visual_input(input,now));
  now += 20ms; stage.update(now);
  REQUIRE_FALSE(stage.terminal());
  CHECK(stage.live_usercmd_snapshot()->last_sampled_forward_axis == 1.0F);
  CHECK(stage.live_usercmd_snapshot()->scheduler_stall_recoveries == 1U);
  CHECK(world.runtime_observation()->generation == 1U);
  while (stage.poll_weapon_command_submission()) {}

  // A freshly sampled attack after recovery keeps its real monotonic sample
  // time. Contiguous command IDs deliberately do NOT include discarded wall
  // samples. Reconstructing time from activation+ID*20ms ages every later
  // local effect by 580ms, although its immutable wire command is fresh.
  ++input.input_revision;
  input.forward_axis = 0.0F; // release W in both the axis and button views
  input.held_buttons = input.pressed_buttons =
      hlclient::gameplay_input::gameplay_button_mask(
          hlclient::gameplay_input::GameplayButton::attack_primary);
  REQUIRE(stage.submit_live_visual_input(input,now));
  now += 20ms; stage.update(now);
  REQUIRE_FALSE(stage.terminal());
  const auto shot = stage.poll_weapon_command_submission();
  REQUIRE(shot);
  CHECK_FALSE(stage.poll_weapon_command_submission());
  CHECK(shot->buttons == goldsrc::kReferenceGoldSrcButtonAttack);
  const auto after_shot = stage.live_usercmd_snapshot();
  REQUIRE(after_shot);
  CHECK(shot->command_end_nanoseconds ==
        after_shot->scheduler_next_sample_time_nanoseconds - 20'000'000);
  CHECK(shot->command_end_nanoseconds ==
        after_shot->scheduler_activation_time_nanoseconds +
            (std::int64_t{shot->command_sequence} + 29) * 20'000'000);
  CHECK(after_shot->command_interval == 20ms);
  CHECK(after_shot->attack_new_submission_count ==
        recovered->attack_new_submission_count + 1U);

  // Feed the actual production TX receipt through the normal game host and
  // local mixer. An independent model/observation/PCM fixture needs no game
  // installation or audio device. Keep all existing cue expiry guards active.
  namespace api = hlclient::game_api;
  api::GameClientHost effects{hlclient::games::halflife::make_half_life_client_module()};
  effects.reset({1,1,{1}});
  api::LocalWeaponModelMetadata model;
  model.generation=1; model.model_index=59; model.resource_revision=1;
  model.resource_name="models/v_9mmhandgun.mdl";
  model.supported_bodies.fill(true); model.selectable_bodies.fill(true);
  model.sequences.resize(10,{30,46,false,{}});
  hlclient::assets::ModelSequenceEvent marker;
  marker.frame=0; marker.event_number=5001;
  marker.options={std::byte{'1'},std::byte{'1'}};
  model.sequences[3].events.push_back(marker);
  effects.bind_model(model);
  hlclient::client::RuntimeClientObservationState observed;
  observed.generation=1; observed.publication_revision=1;
  observed.lifecycle.life_epoch=1;
  observed.lifecycle.state=hlclient::client::LocalPlayerLifeState::alive;
  observed.client_metadata.generation=1;
  observed.client_metadata.freshness=
      hlclient::client::RuntimeObservationFreshness::observed_in_record;
  observed.client_metadata.source=hlclient::client::RuntimeObservationSource{
      .record_identity=1,.record_ordinal=1};
  observed.receiving_client.emplace();
  observed.receiving_client->viewmodel_index=59;
  observed.receiving_client->health=100;
  observed.weapon_hud.active_weapon_id=2;
  observed.weapon_slots.push_back({.wire_slot=2,.clip=8,.in_reload=false,
      .next_primary_attack=0.0,.weapon_id=2});
  const auto presentation_now=std::chrono::duration<double>{now-epoch}.count();
  effects.observe(observed,presentation_now);
  const auto sample_time=std::chrono::duration<double>{
      std::chrono::nanoseconds{shot->command_end_nanoseconds}-
      epoch.time_since_epoch()}.count();
  api::LocalWeaponSubmittedCommand submitted{shot->generation,shot->command_sequence,
      shot->buttons,sample_time,api::LocalWeaponSubmittedCommand::ShotContext{
          {0,0,28},{1,0,0}}};
  effects.submit(submitted,presentation_now);
  const auto presentation=effects.sample(presentation_now);
  CHECK(presentation.primary_fire_starts==1U);
  const api::LocalVisualContext camera{{0,0,28},{1,0,0},{0,-1,0},{0,0,1},{},presentation_now};
  const auto visual=effects.local_visuals(camera);
  CHECK(visual.flash.has_value()); CHECK(visual.light.has_value());
  CHECK(visual.shell.has_value()); CHECK(visual.world_impact.has_value());
  CHECK(visual.statistics.late_cues_dropped==0U);
  const auto cues=effects.drain_audio();
  REQUIRE(cues.count==1U);
  CHECK(cues.cues[0].kind==api::LocalSoundKind::fire);
  struct ReadyAudio final : goldsrc::SoundAssets {
    std::shared_ptr<const hlclient::assets::AudioAsset> pcm;
    ReadyAudio() {
      auto data=std::make_shared<hlclient::assets::AudioAsset>();
      data->sample_rate=48000; data->channel_count=1;
      data->interleaved_samples.assign(4800,0.125F); pcm=std::move(data);
    }
    goldsrc::SoundAssetResult request(std::uint16_t) noexcept override {return {goldsrc::SoundAssetStatus::ready,pcm};}
    goldsrc::SoundAssetResult request_local(const api::LocalSoundReference&) noexcept override {return {goldsrc::SoundAssetStatus::ready,pcm};}
  } sounds;
  hlclient::audio::OfflineOutput output;
  output.set_listener({{0,0,28},{0,1,0},1,false});
  goldsrc::LocalAudio playback(output);
  playback.update(cues,&sounds,presentation_now,true);
  CHECK(playback.statistics().late==0U);
  CHECK(output.mixer.statistics().presentation_started==1U);
  std::array<float,256> mixed{};
  output.mixer.render(mixed);
  CHECK(std::any_of(mixed.begin(),mixed.end(),[](float v){return v!=0.0F;}));
  effects.submit(submitted,presentation_now); // exact replay cannot emit again
  (void)effects.sample(presentation_now);
  CHECK_FALSE(effects.local_visuals(camera).shell);
  CHECK_FALSE(effects.local_visuals(camera).world_impact);
  CHECK(effects.drain_audio().count==0U);
  playback.update(cues,&sounds,presentation_now,true); // repeated batch too
  CHECK(output.mixer.statistics().presentation_started==1U);
  stage.cancel(now+1ms);
}

TEST_CASE(
    "Live E motion criterion rejects stationary vertical and unusable fake "
    "responses",
    "[goldsrc][live-runtime][live-usercmd][motion-criterion][fake-server]") {
  const std::array<std::size_t, goldsrc::kLiveUserCmdPhaseCount> sent{
      1U, 1U, 1U, 1U, 1U};
  const auto sample = [](const goldsrc::LiveUserCmdInputPhase phase,
                         const double x, const double y, const double z,
                         const double velocity_x, const double velocity_y,
                         const double velocity_z,
                         const std::uint64_t identity) {
    goldsrc::LiveUserCmdServerSample result;
    result.generation = 1U;
    result.publication_revision = identity;
    result.source.record_identity = identity;
    result.source.source_transport_sequence =
        static_cast<std::uint32_t>(identity);
    result.health = 100.0;
    result.phase = phase;
    result.origin = {x, y, z};
    result.velocity = {velocity_x, velocity_y, velocity_z};
    return result;
  };

  std::vector<goldsrc::LiveUserCmdServerSample> responsive{
      sample(goldsrc::LiveUserCmdInputPhase::neutral_before, 0.0, 0.0, 64.0,
             0.0, 0.0, -4.0, 1U),
      sample(goldsrc::LiveUserCmdInputPhase::forward, 0.0, 0.0, 63.0, 5.0, 0.0,
             8.0, 2U),
      sample(goldsrc::LiveUserCmdInputPhase::forward, 1.0, 0.0, 70.0, 25.0, 0.0,
             16.0, 3U),
      sample(goldsrc::LiveUserCmdInputPhase::neutral_middle, 2.0, 0.0, 72.0,
             8.0, 0.0, 0.0, 4U),
      sample(goldsrc::LiveUserCmdInputPhase::backward, 2.0, 0.0, 71.0, -6.0,
             0.0, -8.0, 5U),
      sample(goldsrc::LiveUserCmdInputPhase::neutral_tail, 1.0, 0.0, 65.0, -2.0,
             0.0, -4.0, 6U)};
  CHECK(goldsrc::evaluate_live_usercmd_motion(responsive, sent, 0.0, 1.0,
                                               0.125) ==
        goldsrc::LiveUserCmdMotionOutcome::verified);

  // The retained A/D live session reported these lateral positions and
  // velocities. The side script projects them onto yaw 90 for its motion gate.
  const std::vector<goldsrc::LiveUserCmdServerSample> lateral{
      sample(goldsrc::LiveUserCmdInputPhase::neutral_before, -256.0, 656.0,
             292.031, 0.0, 0.0, 0.0, 10U),
      sample(goldsrc::LiveUserCmdInputPhase::forward, -256.0, 656.0,
             292.031, 0.0, 0.0, 0.0, 11U),
      sample(goldsrc::LiveUserCmdInputPhase::forward, -256.0, 745.836,
             292.031, 0.0, 100.0, 0.0, 12U),
      sample(goldsrc::LiveUserCmdInputPhase::neutral_middle, -256.0, 761.359,
             292.031, 0.0, 0.0, 0.0, 13U),
      sample(goldsrc::LiveUserCmdInputPhase::backward, -256.0, 761.359,
             292.031, 0.0, 0.0, 0.0, 14U),
      sample(goldsrc::LiveUserCmdInputPhase::backward, -256.0, 669.516,
             292.031, 0.0, -100.0, 0.0, 15U),
      sample(goldsrc::LiveUserCmdInputPhase::neutral_tail, -256.0, 656.0,
             292.031, 0.0, 0.0, 0.0, 16U)};
  CHECK(goldsrc::evaluate_live_usercmd_motion(lateral, sent, 90.0, 1.0,
                                               0.125) ==
        goldsrc::LiveUserCmdMotionOutcome::verified);

  auto stationary = responsive;
  for (auto &value : stationary) {
    value.origin.x = 0.0;
    value.origin.y = 0.0;
    value.velocity.x = 0.0;
    value.velocity.y = 0.0;
  }
  CHECK(goldsrc::evaluate_live_usercmd_motion(stationary, sent, 0.0, 1.0,
                                              0.125) ==
        goldsrc::LiveUserCmdMotionOutcome::server_motion_unverified);

  auto vertical_only = stationary;
  for (std::size_t index = 0U; index < vertical_only.size(); ++index) {
    vertical_only[index].origin.z = static_cast<double>(index) * 10.0;
    vertical_only[index].velocity.z = static_cast<double>(index) * 20.0;
  }
  CHECK(goldsrc::evaluate_live_usercmd_motion(vertical_only, sent, 0.0, 1.0,
                                              0.125) ==
        goldsrc::LiveUserCmdMotionOutcome::server_motion_unverified);

  auto unavailable = responsive;
  for (auto &value : unavailable)
    value.origin.x.reset();
  CHECK(goldsrc::evaluate_live_usercmd_motion(unavailable, sent, 0.0, 1.0,
                                              0.125) ==
        goldsrc::LiveUserCmdMotionOutcome::observation_context_blocked);
}

} // namespace
namespace {

enum class E321MovementBodyOrder {
  current_time_first,
  current_time_after_movement,
  inherited_time,
  repeated_time,
};

// An independently authored server impulse. This modifies the encoded server
// state, not a prediction snapshot, so a copied client result cannot satisfy
// the correction tests accidentally. Defaults preserve the original oracle.
struct E32ServerCorrectionFixture {
  hlclient::assets::AssetVector3 position_delta{}, velocity_delta{};
  float additional_server_seconds{};
  unsigned health{100U}, dead_flag{};
  bool explicit_setangle{};
  bool grounded{};
  std::optional<unsigned> effects;
};

// Server-side fixture oracle, not a sample of LiveRuntimeStage's predicted
// position. From command 1's independently authored seed, an empty-world
// forward airborne command travels 0.6 units and gravity is 800 units/s^2.
// The wire quantizes each authored position once to the advertised 1/8 unit.
[[nodiscard]] std::vector<std::byte> e321_advancing_air_record(
    const std::uint32_t boundary,
    const E321MovementBodyOrder order = E321MovementBodyOrder::current_time_first,
    const E32ServerCorrectionFixture correction = {}) {
  REQUIRE(boundary >= 1U);
  const double steps = static_cast<double>(boundary - 1U);
  const auto signed_eighths = [](const double value) {
    const auto quantized = std::lround(value * 8.0);
    const auto magnitude = static_cast<std::uint32_t>(quantized < 0 ? -quantized : quantized);
    REQUIRE(magnitude < 0x8000U);
    return (magnitude << 1U) | (quantized < 0 ? 1U : 0U);
  };
  const auto x = signed_eighths(10.0 + .6 * steps + correction.position_delta.x);
  const auto y = signed_eighths(correction.position_delta.y);
  const auto z = signed_eighths(1024.0 - .16 * steps * steps + correction.position_delta.z);
  delta_fixture::BitWriter time;
  time.write(7U, 8U);
  const float seconds = order == E321MovementBodyOrder::repeated_time ? 102.F :
      102.F + static_cast<float>(steps) * .02F + correction.additional_server_seconds;
  time.write(std::bit_cast<std::uint32_t>(seconds), 32U);

  delta_fixture::BitWriter movement;
  movement.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
  movement.write(0U, 1U); // full clientdata, not an inherited delta base
  movement.write(2U, 3U); movement.write(0x1fffU, 16U);
  movement.write(goldsrc_signed_positive(correction.health), 10U);
  movement.write(signed_eighths((boundary == 1U ? 0.0 : 30.0) + correction.velocity_delta.x), 16U);
  movement.write(signed_eighths(correction.velocity_delta.y), 16U);
  movement.write(signed_eighths(-16.0 * steps + correction.velocity_delta.z), 16U);
  movement.write(goldsrc_signed_positive(112U), 10U);
  movement.write(x, 16U); movement.write(y, 16U); movement.write(z, 16U);
  movement.write(correction.grounded ? 1U << 9U : 0U, 32U);
  movement.write(3200U, 16U); movement.write(0U, 1U);
  movement.write(0U, 2U); movement.write(correction.dead_flag, 3U);
  movement.write(0U, 1U); movement.align_zero();
  movement.write(goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 8U);
  movement.write(1U, 16U);
  movement.write(0U, 1U); movement.write(0U, 1U); movement.write(1U, 6U);
  movement.write(0U, 1U); movement.write(0U, 1U);
  movement.write(2U, 3U); movement.write(correction.effects ? 0xffffU : 0x7fffU, 16U);
  movement.write(x, 16U); movement.write(y, 16U); movement.write(z, 16U);
  for (unsigned component = 0U; component < 3U; ++component) movement.write(0U, 16U);
  movement.write(0U, 16U); movement.write(3U, 4U);
  movement.write(goldsrc_signed_positive(8U), 16U); movement.write(0U, 1U);
  movement.write(goldsrc_signed_positive(32U), 16U);
  for (unsigned component = 0U; component < 3U; ++component) movement.write(0U, 16U);
  movement.write(0U, 1U);
  if (correction.effects) movement.write(*correction.effects, 8U);
  movement.write(0U, 16U); movement.align_zero();

  // Benign reliable-prefix-shaped control bytes precede ordinary current
  // movement in both controls. The test does not pretend this proves a wire
  // reliable/unreliable byte boundary: ordinary packets do not encode one.
  std::vector<std::byte> body{static_cast<std::byte>(goldsrc::RuntimeControlOpcode::svc_nop)};
  const auto append = [&](const auto& part) { body.insert(body.end(), part.begin(), part.end()); };
  if (order != E321MovementBodyOrder::inherited_time &&
      order != E321MovementBodyOrder::current_time_after_movement) append(time.bytes());
  if (correction.explicit_setangle) {
    body.push_back(static_cast<std::byte>(goldsrc::RuntimeControlOpcode::svc_setangle));
    for (unsigned component = 0U; component < 6U; ++component) body.push_back(std::byte{});
  }
  append(movement.bytes());
  if (order == E321MovementBodyOrder::current_time_after_movement) append(time.bytes());
  return body;
}

struct E321AdvancingAirStart {
  goldsrc::LiveRuntimeStageTimePoint now;
  std::uint32_t server_sequence{};
};

[[nodiscard]] E321AdvancingAirStart seed_e321_advancing_air(
    goldsrc::LiveRuntimeStage& stage, FakeTransport& transport,
    const network::NetworkAddress& remote, const goldsrc::LiveRuntimeStageTimePoint epoch,
    std::shared_ptr<const hlclient::collision::CollisionWorldPackage> collision = {},
    const E32ServerCorrectionFixture initial = {}) {
  const auto driven = drive_live_initial(stage, transport, remote, epoch, false, false, true, true,
      false, false, initial.effects.has_value());
  const auto resource = driven.last_server_sequence + 1U;
  transport.queue(remote, server_packet(resource, false,
      driven.transition_request.header.sequence.sequence.value(), false,
      service_envelope(resource_semantic_payload())));
  transport.queue(remote, server_packet(resource + 1U, false,
      driven.transition_request.header.sequence.sequence.value(), false, {std::byte{1U}}));
  for (unsigned step = 1U; step <= 8U; ++step) stage.update(driven.now + std::chrono::milliseconds{step});
  constexpr std::array empty_response{std::byte{5U}, std::byte{0U}, std::byte{0U}};
  const auto response = require_latest_payload(transport, empty_response);
  const auto first = driven.now + 9ms;
  transport.queue(remote, server_packet(resource + 2U, false, response.header.sequence.sequence.value(),
      response.header.sequence.flags.reliable));
  stage.update(first);
  const auto spawn = require_latest_payload(transport, goldsrc::StockSpawnRequestBuilder::build(0x12345678U,
      goldsrc::decode_stock_server_world_map_crc(0xdeadbeefU, 0U)).encoding->semantic_bytes());
  transport.queue(remote, server_packet(resource + 3U, true, spawn.header.sequence.sequence.value(), false,
      live_baseline_and_runtime_payload(100.F, 100U, 80U, true)));
  stage.update(first + 1ms); stage.update(first + 2ms);
  const auto sendents = require_latest_payload(transport, goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
  transport.queue(remote, server_packet(resource + 4U, false, sendents.header.sequence.sequence.value(),
      sendents.header.sequence.flags.reliable, live_prediction_air_record(101.F, 10U)));
  stage.update(first + 3ms); stage.update(first + 2s + 2ms);
  REQUIRE(stage.live_visual_input_ready());
  REQUIRE(stage.attach_reference_prediction_collision(collision ? std::move(collision) :
      hlclient::tests::collision_brush_fixture::package(false)));
  auto now = first + 2s + 3ms;
  goldsrc::LiveVisualControlInput input{1U, 1U, 1.F, 0, 0, 0, true, true};
  input.held_buttons = hlclient::gameplay_input::gameplay_button_mask(
      hlclient::gameplay_input::GameplayButton::move_forward);
  REQUIRE(stage.submit_live_visual_input(input, now)); REQUIRE(stage.activate_live_visual_control(now));
  now += 20ms; stage.update(now);
  const auto accounting = stage.live_usercmd_snapshot(); REQUIRE(accounting);
  REQUIRE_FALSE(accounting->transmit_ranges.empty());
  const auto& carrier = accounting->transmit_ranges.back();
  REQUIRE(carrier.last_new_command_sequence == 1U);
  const auto server_sequence = resource + 5U;
  transport.queue(remote, server_packet(server_sequence, false, carrier.outgoing_netchan_sequence, false,
      e321_advancing_air_record(1U, E321MovementBodyOrder::current_time_first, initial)));
  now += 20ms; stage.update(now);
  REQUIRE_FALSE(stage.terminal());
  const auto prediction = stage.live_reference_prediction_snapshot(now);
  REQUIRE(prediction.state == goldsrc::LiveReferencePredictionState::active);
  REQUIRE(prediction.anchor_command == 1U);
  while (stage.poll_event()) {}
  while (stage.poll_weapon_command_submission()) {}
  return {now, server_sequence};
}

[[nodiscard]] goldsrc::LiveRuntimeStageConfig e321_advancing_air_config() {
  auto config = live_test_config(); // the real default Half-Life module, not a permissive test module
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source = goldsrc::LiveVisualControlInputSource::keyboard_mouse;
  config.reference_prediction = true;
  config.movement_diagnostics = true;
  return config;
}

[[nodiscard]] std::shared_ptr<const hlclient::collision::CollisionWorldPackage>
e32_correction_collision(const bool floor) {
  namespace fixture = hlclient::tests;
  namespace literal = fixture::literal_movement_bsp;
  // Both (10.6,0,1023.84) and (-7.4,18,1023.84) are outside this finite
  // solid, but their straight body sweep crosses its expanded convex corner.
  const std::vector<literal::ConvexVolume> volumes{floor ?
      literal::box({-256,-256,900}, {256,256,988}) :
      literal::box({20,20,900}, {128,128,1100})};
  std::vector<fixture::SyntheticBspPlane> planes{fixture::SyntheticBspPlane{{0,0,1},0,2}};
  std::vector<fixture::SyntheticBspNode> nodes;
  std::vector<fixture::SyntheticBspClipnode> clipnodes;
  const auto point = literal::compile_point_tree(volumes, planes, nodes);
  const auto standing = literal::compile_clip_tree(volumes, {16,16,36}, planes, clipnodes);
  const auto duck = literal::compile_clip_tree(volumes, {16,16,18}, planes, clipnodes);
  const std::array leaves{
      fixture::SyntheticBspLeaf{-1,-1,{-256,-256,0},{256,256,1200},0,0,{}},
      fixture::SyntheticBspLeaf{-2,-1,{-256,-256,0},{256,256,1200},0,0,{}}};
  fixture::SyntheticBspModel model;
  model.minimum = {-256,-256,0}; model.maximum = {256,256,1200};
  model.headnodes = {point,standing,-1,duck};
  model.visibility_leaf_count = 1; model.first_face = 0; model.face_count = 1;
  fixture::SyntheticBspBuilder builder;
  const auto bytes = builder.set_planes(planes).set_nodes(nodes).set_leaves(leaves)
      .set_clipnodes(clipnodes).set_models(std::span{&model,1U}).build();
  const auto parsed = goldsrc::bsp::GoldSrcBspParser::parse(bytes);
  REQUIRE(parsed); REQUIRE(parsed.document);
  const auto built = goldsrc::collision::GoldSrcCollisionWorldBuilder::build(parsed.document->collision_source);
  REQUIRE(built); REQUIRE(built.package);
  return built.package;
}

} // namespace

TEST_CASE("E32 a validated large correction keeps exact jump history without waiting for release",
          "[e32][contact-stale-recovery][goldsrc][live-runtime][integration][reference-positive]") {
  for (const bool diagnostics : {false, true}) {
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27878U);
    hlclient::client::ClientWorldState world;
    auto config = e321_advancing_air_config(); config.movement_diagnostics = diagnostics;
    goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
    auto route = seed_e321_advancing_air(stage, transport, remote,
        goldsrc::LiveRuntimeStageTimePoint{} + 150s);
    goldsrc::LiveVisualControlInput input{1U, 2U, 1.F, 0, 0, 0, true, true};
    input.held_buttons = hlclient::gameplay_input::gameplay_button_mask(
        hlclient::gameplay_input::GameplayButton::move_forward) |
        hlclient::gameplay_input::gameplay_button_mask(
            hlclient::gameplay_input::GameplayButton::jump);
    REQUIRE(stage.submit_live_visual_input(input, route.now + 1ms));
    route.now += 20ms; stage.update(route.now);
    while (stage.poll_weapon_command_submission()) {}
    const auto carrier = stage.live_usercmd_snapshot()->transmit_ranges.back();
    REQUIRE(carrier.last_new_command_sequence == 3U);
    const auto before = stage.live_reference_prediction_snapshot(route.now);
    REQUIRE(before.state == goldsrc::LiveReferencePredictionState::active);
    REQUIRE(before.predicted_old_buttons);
    CHECK((*before.predicted_old_buttons & goldsrc::kReferenceGoldSrcButtonJump) != 0U);
    E32ServerCorrectionFixture correction;
    correction.position_delta.x = 16.25F; // no velocity evidence: snap, never impulse smoothing
    route.now += 10ms;
    transport.queue(remote, server_packet(++route.server_sequence, false,
        carrier.outgoing_netchan_sequence, false,
        e321_advancing_air_record(3U, E321MovementBodyOrder::current_time_first, correction)));
    stage.update(route.now);
    const auto after = stage.live_reference_prediction_snapshot(route.now);
    CAPTURE(diagnostics, after.reason, after.presentation_reason);
    REQUIRE(after.state == goldsrc::LiveReferencePredictionState::active);
    CHECK(after.fallback_count == before.fallback_count);
    REQUIRE(after.predicted_origin); REQUIRE(after.canonical_origin); REQUIRE(after.presented_origin);
    CHECK(after.canonical_origin->x == Catch::Approx(27.5).margin(.001));
    CHECK(after.predicted_origin->x == after.canonical_origin->x);
    CHECK(after.presented_origin->x == after.predicted_origin->x);
    CHECK(after.presentation_reason != "visual_server_velocity_correction");
    REQUIRE(after.predicted_old_buttons);
    CHECK((*after.predicted_old_buttons & goldsrc::kReferenceGoldSrcButtonJump) != 0U);
    // A fresh observation for that exact held-jump boundary needs its retained
    // slot. Clearing history turns this into matching_prediction_slot_required.
    correction.additional_server_seconds = .001F;
    route.now += 1ms;
    transport.queue(remote, server_packet(++route.server_sequence, false,
        carrier.outgoing_netchan_sequence, false,
        e321_advancing_air_record(3U, E321MovementBodyOrder::current_time_first, correction)));
    stage.update(route.now);
    const auto confirmed = stage.live_reference_prediction_snapshot(route.now);
    REQUIRE(confirmed.state == goldsrc::LiveReferencePredictionState::active);
    CHECK(confirmed.fallback_count == before.fallback_count);
    CHECK(confirmed.accepted_corrections == before.accepted_corrections + 2U);
    const auto command_count = stage.live_usercmd_snapshot()->generated_command_count;
    stage.update(route.now);
    CHECK(stage.live_usercmd_snapshot()->generated_command_count == command_count);
    CHECK(stage.live_reference_prediction_snapshot(route.now).accepted_corrections == confirmed.accepted_corrections);
    CHECK_FALSE(stage.poll_weapon_command_submission());
  }
}

TEST_CASE("E32 stale carrier backlog commits authority without replaying expired command suffixes",
          "[e32][contact-stale-recovery][goldsrc][live-runtime][integration][reference-positive]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27879U);
  hlclient::client::ClientWorldState world;
  auto config = e321_advancing_air_config();
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  auto route = seed_e321_advancing_air(stage, transport, remote,
      goldsrc::LiveRuntimeStageTimePoint{} + 150s);
  std::vector<goldsrc::LiveUserCmdTransmitRange> old_carriers{
      stage.live_usercmd_snapshot()->transmit_ranges.back()};
  for (unsigned tick = 0; tick < 20; ++tick) {
    route.now += 20ms; stage.update(route.now);
    if (tick < 3U) old_carriers.push_back(stage.live_usercmd_snapshot()->transmit_ranges.back());
  }
  const auto suspended = stage.live_reference_prediction_snapshot(route.now);
  REQUIRE(suspended.state != goldsrc::LiveReferencePredictionState::active);
  REQUIRE(suspended.reason == "reference_correction_horizon_exceeded");
  const auto before = stage.live_usercmd_snapshot(); REQUIRE(before);
  while (stage.poll_weapon_command_submission()) {}
  const auto fresh_carrier = before->transmit_ranges.back();
  REQUIRE(fresh_carrier.last_new_command_sequence == 22U);
  for (const auto& carrier : old_carriers)
    transport.queue(remote, server_packet(++route.server_sequence, false,
        carrier.outgoing_netchan_sequence, false,
        e321_advancing_air_record(carrier.last_new_command_sequence)));
  route.now += 1ms; stage.update(route.now);
  REQUIRE_FALSE(stage.terminal());
  const auto stale = stage.live_reference_prediction_snapshot(route.now);
  const auto cost = stage.movement_diagnostics_snapshot();
  CHECK(cost.committed_records == old_carriers.size());
  CHECK(cost.replayed_commands == 0U);
  REQUIRE(cost.replayed_simulation_ms); CHECK(*cost.replayed_simulation_ms == 0.0);
  CHECK(stale.state != goldsrc::LiveReferencePredictionState::active);
  CHECK(stale.reason == "reference_seed_horizon_exceeded");
  CHECK(stale.accepted_corrections == suspended.accepted_corrections);
  REQUIRE(stale.canonical_origin);
  CHECK(stale.canonical_origin->x == Catch::Approx(12.375).margin(.001)); // boundary5 was still committed
  CHECK(stage.live_usercmd_snapshot()->server_sample_count == before->server_sample_count + old_carriers.size());
  CHECK(stage.live_usercmd_snapshot()->generated_command_count == before->generated_command_count);
  // The latest exact carrier is fresh: resume immediately from the server's
  // independently authored origin, not from the expired client path.
  transport.queue(remote, server_packet(++route.server_sequence, false,
      fresh_carrier.outgoing_netchan_sequence, false,
      e321_advancing_air_record(fresh_carrier.last_new_command_sequence)));
  route.now += 1ms; stage.update(route.now);
  const auto recovered = stage.live_reference_prediction_snapshot(route.now);
  CAPTURE(recovered.reason);
  REQUIRE(recovered.state == goldsrc::LiveReferencePredictionState::active);
  CHECK(recovered.anchor_command == 22U);
  CHECK(recovered.history_depth == 0U);
  REQUIRE(recovered.predicted_origin); REQUIRE(recovered.canonical_origin);
  CHECK(recovered.predicted_origin->x == Catch::Approx(22.625).margin(.001));
  CHECK(recovered.predicted_origin->x == recovered.canonical_origin->x);
  CHECK(stage.movement_diagnostics_snapshot().replayed_commands == 0U);
  CHECK_FALSE(stage.poll_weapon_command_submission());
}

TEST_CASE("E32 velocity-backed server correction preserves the camera while authoritative replay advances",
          "[e32][server-correction-camera][goldsrc][live-runtime][integration][reference-positive]") {
  for (const bool diagnostics : {false, true}) for (const unsigned suffix : {0U, 2U})
  for (const float displacement : {6.5F, 18.0F, 18.625F}) {
    CAPTURE(diagnostics, suffix, displacement);
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27871U);
    hlclient::client::ClientWorldState world;
    auto config = e321_advancing_air_config(); config.movement_diagnostics = diagnostics;
    goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
    auto route = seed_e321_advancing_air(stage, transport, remote,
        goldsrc::LiveRuntimeStageTimePoint{} + 150s);
    const auto commands_at_anchor = stage.live_usercmd_snapshot(); REQUIRE(commands_at_anchor);
    const auto carrier = commands_at_anchor->transmit_ranges.back();
    REQUIRE(carrier.last_new_command_sequence == 2U);
    if (suffix) { route.now += std::chrono::milliseconds{20U * suffix}; stage.update(route.now); }
    route.now += 10ms;
    const auto before = stage.live_reference_prediction_snapshot(route.now);
    REQUIRE(before.state == goldsrc::LiveReferencePredictionState::active);
    REQUIRE(before.presented_origin); REQUIRE(before.predicted_origin);
    while (stage.poll_weapon_command_submission()) {}
    const auto commands_before = stage.live_usercmd_snapshot(); REQUIRE(commands_before);
    E32ServerCorrectionFixture correction;
    correction.position_delta.x = displacement;
    correction.velocity_delta.x = displacement * 12.5F; // independently authored 80ms impulse
    transport.queue(remote, server_packet(++route.server_sequence, false,
        carrier.outgoing_netchan_sequence, false,
        e321_advancing_air_record(carrier.last_new_command_sequence,
            E321MovementBodyOrder::current_time_first, correction)));
    stage.update(route.now);
    const auto after = stage.live_reference_prediction_snapshot(route.now);
    CAPTURE(after.reason, after.presentation_reason);
    REQUIRE_FALSE(stage.terminal());
    REQUIRE(after.state == goldsrc::LiveReferencePredictionState::active);
    REQUIRE(after.presented_origin); REQUIRE(after.predicted_origin); REQUIRE(after.canonical_origin);
    REQUIRE(after.predicted_velocity); REQUIRE(after.last_raw_position_error);
    CHECK(*after.last_raw_position_error == Catch::Approx(displacement).margin(.064));
    CHECK(after.canonical_origin->x == Catch::Approx(10.6 + displacement).margin(.064));
    CHECK(after.predicted_velocity->x == Catch::Approx(30.0 + correction.velocity_delta.x).margin(.064));
    CHECK(after.predicted_origin->x >= after.canonical_origin->x);
    CHECK(after.accepted_corrections == before.accepted_corrections + 1U);
    CHECK(after.fallback_count == before.fallback_count);
    CHECK(after.replayed_commands == before.replayed_commands + suffix);
    CHECK(after.anchor_command == carrier.last_new_command_sequence);
    const auto distance = [](const auto& a, const auto& b) {
      return std::hypot(double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z);
    };
    CHECK(distance(*after.presented_origin, *before.presented_origin) < .001);
    REQUIRE(after.last_camera_correction_jump); CHECK(*after.last_camera_correction_jump < .001);
    CHECK(after.presentation_reason == "visual_server_velocity_correction");
    CHECK_FALSE(stage.poll_weapon_command_submission());
    CHECK(stage.live_usercmd_snapshot()->generated_command_count == commands_before->generated_command_count);
    const auto revision = world.runtime_publication_revision();
    const auto sends = transport.sent.size();
    auto previous = *after.presented_origin;
    for (const unsigned elapsed : {0U, 1U, 7U, 16U, 33U, 60U, 99U, 101U}) {
      const auto at = route.now + std::chrono::milliseconds{elapsed};
      const auto sample = stage.live_reference_prediction_snapshot(at);
      REQUIRE(sample.presented_origin); REQUIRE(sample.predicted_origin);
      CHECK(sample.predicted_origin->x == after.predicted_origin->x);
      CHECK(sample.local_steps == after.local_steps);
      CHECK(sample.replayed_commands == after.replayed_commands);
      CHECK(sample.fallback_count == after.fallback_count);
      CHECK(sample.presented_origin->x >= previous.x - .001F);
      CHECK(sample.presented_origin->x <= sample.predicted_origin->x + .001F);
      const auto repeat = stage.live_reference_prediction_snapshot(at);
      REQUIRE(repeat.presented_origin);
      CHECK(distance(*repeat.presented_origin, *sample.presented_origin) == 0.0);
      CHECK(repeat.presentation_trace_queries_total == sample.presentation_trace_queries_total);
      previous = *sample.presented_origin;
      if (elapsed == 101U) CHECK(distance(*sample.presented_origin, *sample.predicted_origin) < .001);
    }
    CHECK(world.runtime_publication_revision() == revision);
    CHECK(transport.sent.size() == sends);
    CHECK_FALSE(stage.poll_weapon_command_submission());
    std::cout << "[server-correction-camera] diagnostics=" << diagnostics << " suffix=" << suffix
        << " raw_error=" << *after.last_raw_position_error << " same_time_jump="
        << *after.last_camera_correction_jump << " fallback_delta="
        << after.fallback_count - before.fallback_count << '\n';
  }
}

TEST_CASE("E32 fresh ACKs preserve one bounded server correction fade without accumulating offsets",
          "[e32][server-correction-camera][goldsrc][live-runtime][integration][reference-positive]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27872U);
  hlclient::client::ClientWorldState world;
  auto config = e321_advancing_air_config();
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  auto route = seed_e321_advancing_air(stage, transport, remote,
      goldsrc::LiveRuntimeStageTimePoint{} + 150s);
  const auto carrier = stage.live_usercmd_snapshot()->transmit_ranges.back();
  E32ServerCorrectionFixture correction;
  correction.position_delta.x = 18.F; correction.velocity_delta.x = 225.F;
  route.now += 10ms;
  const auto started = route.now;
  transport.queue(remote, server_packet(++route.server_sequence, false, carrier.outgoing_netchan_sequence,
      false, e321_advancing_air_record(2U, E321MovementBodyOrder::current_time_first, correction)));
  stage.update(route.now);
  const auto initial = stage.live_reference_prediction_snapshot(route.now);
  REQUIRE(initial.state == goldsrc::LiveReferencePredictionState::active);
  REQUIRE(initial.presented_origin); REQUIRE(initial.predicted_origin);
  CHECK(initial.presentation_reason == "visual_server_velocity_correction");
  const auto commands = stage.live_usercmd_snapshot(); REQUIRE(commands);
  for (const unsigned elapsed : {5U, 8U, 9U}) {
    route.now = started + std::chrono::milliseconds{elapsed};
    const auto before = stage.live_reference_prediction_snapshot(route.now); REQUIRE(before.presented_origin);
    // The first two records confirm the same state; the last is a second
    // independently authored small impulse. None may add the old residual
    // twice or renew the initial100ms convergence deadline.
    correction.additional_server_seconds = static_cast<float>(elapsed) / 1000.F;
    if (elapsed == 9U) { correction.position_delta.x = 20.F; correction.velocity_delta.x = 275.F; }
    transport.queue(remote, server_packet(++route.server_sequence, false, carrier.outgoing_netchan_sequence,
        false, e321_advancing_air_record(2U, E321MovementBodyOrder::current_time_first, correction)));
    stage.update(route.now);
    const auto after = stage.live_reference_prediction_snapshot(route.now);
    CAPTURE(elapsed, after.reason, after.presentation_reason);
    REQUIRE(after.state == goldsrc::LiveReferencePredictionState::active);
    REQUIRE(after.presented_origin); REQUIRE(after.predicted_origin);
    CHECK(std::abs(after.presented_origin->x - before.presented_origin->x) < .001F);
    CHECK(std::abs(after.presented_origin->z - before.presented_origin->z) < .001F);
    CHECK(after.fallback_count == initial.fallback_count);
    CHECK(after.presentation_reason == "visual_server_velocity_correction");
    CHECK(std::abs(after.presented_origin->x - after.predicted_origin->x) <= 32.F);
    stage.update(route.now);
    const auto repeat = stage.live_reference_prediction_snapshot(route.now);
    CHECK(repeat.accepted_corrections == after.accepted_corrections);
    CHECK(repeat.local_steps == after.local_steps);
    CHECK(repeat.replayed_commands == after.replayed_commands);
    REQUIRE(repeat.presented_origin); CHECK(repeat.presented_origin->x == after.presented_origin->x);
    CHECK(repeat.presentation_trace_queries_total == after.presentation_trace_queries_total);
  }
  const auto done = stage.live_reference_prediction_snapshot(started + 101ms);
  REQUIRE(done.presented_origin); REQUIRE(done.predicted_origin);
  CHECK(std::abs(done.presented_origin->x - done.predicted_origin->x) < .001F);
  CHECK(std::abs(done.presented_origin->z - done.predicted_origin->z) < .001F);
  CHECK(done.accepted_corrections == initial.accepted_corrections + 3U);
  CHECK(stage.live_usercmd_snapshot()->generated_command_count == commands->generated_command_count);
  CHECK_FALSE(stage.poll_weapon_command_submission());
}

TEST_CASE("E32 ordinary position refinements retain an active velocity correction fade deadline",
          "[e32][server-correction-camera][goldsrc][live-runtime][integration][reference-positive]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27875U);
  hlclient::client::ClientWorldState world;
  auto config = e321_advancing_air_config();
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  auto route = seed_e321_advancing_air(stage, transport, remote,
      goldsrc::LiveRuntimeStageTimePoint{} + 150s);
  const auto carrier = stage.live_usercmd_snapshot()->transmit_ranges.back();
  E32ServerCorrectionFixture correction;
  correction.position_delta.x = 18.F; correction.velocity_delta.x = 225.F;
  route.now += 10ms;
  const auto started = route.now;
  transport.queue(remote, server_packet(++route.server_sequence, false, carrier.outgoing_netchan_sequence,
      false, e321_advancing_air_record(2U, E321MovementBodyOrder::current_time_first, correction)));
  stage.update(route.now);
  const auto initial = stage.live_reference_prediction_snapshot(route.now);
  REQUIRE(initial.state == goldsrc::LiveReferencePredictionState::active);
  REQUIRE(initial.presentation_reason == "visual_server_velocity_correction");
  // Generate command3 before the next same-time RX comparison. This ensures
  // normal input motion is not mistaken for a correction-induced camera jump.
  stage.update(started + 10ms);
  route.now = started + 20ms;
  const auto before = stage.live_reference_prediction_snapshot(route.now);
  REQUIRE(before.presented_origin);
  correction.position_delta.x = 19.F; // ordinary1u refinement, unchanged velocity
  correction.additional_server_seconds = .02F;
  transport.queue(remote, server_packet(++route.server_sequence, false, carrier.outgoing_netchan_sequence,
      false, e321_advancing_air_record(2U, E321MovementBodyOrder::current_time_first, correction)));
  stage.update(route.now);
  const auto after = stage.live_reference_prediction_snapshot(route.now);
  CAPTURE(after.reason, after.presentation_reason);
  REQUIRE(after.state == goldsrc::LiveReferencePredictionState::active);
  REQUIRE(after.presented_origin); REQUIRE(after.last_raw_position_error);
  CHECK(*after.last_raw_position_error == Catch::Approx(1.0).margin(.001));
  CHECK(std::hypot(double(after.presented_origin->x) - before.presented_origin->x,
      double(after.presented_origin->y) - before.presented_origin->y,
      double(after.presented_origin->z) - before.presented_origin->z) < .001);
  CHECK(after.presentation_reason == "visual_server_velocity_correction");
  CHECK(after.fallback_count == initial.fallback_count);
  const auto done = stage.live_reference_prediction_snapshot(started + 101ms);
  REQUIRE(done.presented_origin); REQUIRE(done.predicted_origin);
  CHECK(std::hypot(double(done.presented_origin->x) - done.predicted_origin->x,
      double(done.presented_origin->y) - done.predicted_origin->y,
      double(done.presented_origin->z) - done.predicted_origin->z) < .001);
}

TEST_CASE("E32 observed three-axis impulse bridges grounded to airborne without a jump command",
          "[e32][server-correction-camera][goldsrc][live-runtime][integration][reference-positive]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27876U);
  hlclient::client::ClientWorldState world;
  auto config = e321_advancing_air_config();
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  E32ServerCorrectionFixture grounded; grounded.grounded = true;
  auto route = seed_e321_advancing_air(stage, transport, remote,
      goldsrc::LiveRuntimeStageTimePoint{} + 150s, e32_correction_collision(true), grounded);
  const auto carrier = stage.live_usercmd_snapshot()->transmit_ranges.back();
  route.now += 10ms;
  const auto before = stage.live_reference_prediction_snapshot(route.now);
  REQUIRE(before.state == goldsrc::LiveReferencePredictionState::active);
  REQUIRE(before.predicted_origin); REQUIRE(before.predicted_velocity); REQUIRE(before.presented_origin);
  REQUIRE(before.predicted_mode == hlclient::movement::PlayerMovementMode::walking);
  // Independent flat-ground command oracle: from rest, accel10 * maxspeed320
  // *20ms =64ups, and position10 +64*.020 =11.28. The correction is the
  // reported session's dp/dv, translated to this owned fixture's coordinates.
  REQUIRE(before.predicted_origin->x == Catch::Approx(11.28).margin(.064));
  REQUIRE(before.predicted_origin->z == Catch::Approx(1024.0).margin(.064));
  REQUIRE(before.predicted_velocity->x == Catch::Approx(64.0).margin(.064));
  REQUIRE(before.predicted_velocity->z == Catch::Approx(0.0).margin(.064));
  E32ServerCorrectionFixture correction;
  correction.position_delta = {11.28F - 5.077F - 10.6F, 15.979F,
      1024.F + 6.648F - 1023.84F};
  correction.velocity_delta = {64.F - 118.404F - 30.F, 368.506F,149.375F + 16.F};
  transport.queue(remote, server_packet(++route.server_sequence, false, carrier.outgoing_netchan_sequence,
      false, e321_advancing_air_record(2U, E321MovementBodyOrder::current_time_first, correction)));
  stage.update(route.now);
  const auto after = stage.live_reference_prediction_snapshot(route.now);
  CAPTURE(after.reason, after.presentation_reason);
  REQUIRE(after.state == goldsrc::LiveReferencePredictionState::active);
  REQUIRE(after.predicted_origin); REQUIRE(after.predicted_velocity); REQUIRE(after.presented_origin);
  CHECK(after.predicted_mode == hlclient::movement::PlayerMovementMode::airborne);
  CHECK(after.predicted_origin->x == Catch::Approx(6.203).margin(.064));
  CHECK(after.predicted_origin->y == Catch::Approx(15.979).margin(.064));
  CHECK(after.predicted_origin->z == Catch::Approx(1030.648).margin(.064));
  CHECK(after.predicted_velocity->x == Catch::Approx(-54.404).margin(.064));
  CHECK(after.predicted_velocity->y == Catch::Approx(368.506).margin(.064));
  CHECK(after.predicted_velocity->z == Catch::Approx(149.375).margin(.064));
  REQUIRE(after.last_raw_position_error);
  CHECK(*after.last_raw_position_error == Catch::Approx(18.035).margin(.15));
  CHECK(after.fallback_count == before.fallback_count);
  CHECK(after.presentation_reason == "visual_server_velocity_correction");
  CHECK(std::hypot(double(after.presented_origin->x) - before.presented_origin->x,
      double(after.presented_origin->y) - before.presented_origin->y,
      double(after.presented_origin->z) - before.presented_origin->z) < .001);
  CHECK(stage.live_usercmd_snapshot()->jump_new_submission_count == 0U);
}

TEST_CASE("E32 unclassified discontinuities never receive the server impulse camera bridge",
          "[e32][server-correction-camera][goldsrc][live-runtime][integration][reference-positive]") {
  enum class InvalidCorrection { position_only, over_bound, sideways_velocity, opposite_velocity,
      perpendicular_miss, insufficient_velocity, stale_anchor, blocked_corner, setangle, nointerp };
  for (const auto kind : {InvalidCorrection::position_only, InvalidCorrection::over_bound,
      InvalidCorrection::sideways_velocity, InvalidCorrection::opposite_velocity,
      InvalidCorrection::perpendicular_miss, InvalidCorrection::insufficient_velocity,
      InvalidCorrection::stale_anchor, InvalidCorrection::blocked_corner, InvalidCorrection::setangle,
      InvalidCorrection::nointerp}) {
    CAPTURE(static_cast<unsigned>(kind));
    E32ServerCorrectionFixture correction;
    correction.position_delta.x = 18.F; correction.velocity_delta.x = 225.F;
    if (kind == InvalidCorrection::position_only) correction.velocity_delta = {};
    if (kind == InvalidCorrection::over_bound) { correction.position_delta.x = 33.F; correction.velocity_delta.x = 412.5F; }
    if (kind == InvalidCorrection::sideways_velocity) correction.velocity_delta = {0,225,0};
    if (kind == InvalidCorrection::opposite_velocity) correction.velocity_delta.x = -225.F;
    if (kind == InvalidCorrection::perpendicular_miss) correction.position_delta.y = 4.F;
    if (kind == InvalidCorrection::insufficient_velocity) correction.velocity_delta.x = 16.F;
    if (kind == InvalidCorrection::blocked_corner) {
      correction.position_delta = {-18,18,0}; correction.velocity_delta = {-225,225,0};
    }
    correction.explicit_setangle = kind == InvalidCorrection::setangle;
    if (kind == InvalidCorrection::nointerp) correction.effects = 32U;
    const auto package = kind == InvalidCorrection::blocked_corner ? e32_correction_collision(false) :
        hlclient::tests::collision_brush_fixture::package(false);
    if (kind == InvalidCorrection::blocked_corner) {
      goldsrc::movement::WorldOnlyMovementCollision collision{package};
      hlclient::collision::CollisionQueryScratch scratch;
      constexpr hlclient::assets::AssetVector3 from{10.625F,0,1023.875F}, to{-7.375F,18,1023.875F};
      for (const auto& point : {from,to}) {
        const auto occupied = collision.test_position(point, hlclient::movement::PlayerMovementHull::standing, scratch);
        REQUIRE(occupied); CHECK(occupied.result->status == goldsrc::movement::LocalMovementPositionStatus::free);
      }
      const auto trace = collision.trace_hull(from, to, hlclient::movement::PlayerMovementHull::standing, scratch);
      REQUIRE(trace); CHECK_FALSE(trace.result->start_solid); CHECK(trace.result->fraction < 1.0);
    }
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27873U);
    hlclient::client::ClientWorldState world;
    auto config = e321_advancing_air_config();
    goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
    E32ServerCorrectionFixture initial;
    if (correction.effects) initial.effects = 0U;
    auto route = seed_e321_advancing_air(stage, transport, remote,
        goldsrc::LiveRuntimeStageTimePoint{} + 150s, package, initial);
    const auto carrier = stage.live_usercmd_snapshot()->transmit_ranges.back();
    const auto before = stage.live_reference_prediction_snapshot(route.now);
    REQUIRE(before.state == goldsrc::LiveReferencePredictionState::active);
    route.now += kind == InvalidCorrection::stale_anchor ? 101ms : 10ms;
    transport.queue(remote, server_packet(++route.server_sequence, false, carrier.outgoing_netchan_sequence,
        false, e321_advancing_air_record(2U, E321MovementBodyOrder::current_time_first, correction)));
    stage.update(route.now);
    const auto rejected = stage.live_reference_prediction_snapshot(route.now);
    CAPTURE(rejected.reason, rejected.presentation_reason);
    REQUIRE_FALSE(stage.terminal());
    // The visual rejection remains strict. A successful physical rebase is
    // now retained as a hard snap instead of destroying exact command slots.
    REQUIRE(rejected.state == goldsrc::LiveReferencePredictionState::active);
    CHECK(rejected.reason == "active");
    CHECK(rejected.rebase_reason == "ready_hard_correction_snap");
    CHECK(rejected.fallback_count == before.fallback_count);
    CHECK(rejected.presentation_reason != "visual_server_velocity_correction");
    CHECK(rejected.presentation_reason != "visual_correction");
    REQUIRE(rejected.presented_origin); REQUIRE(rejected.predicted_origin);
    if (rejected.presentation_from_command) CHECK(*rejected.presentation_from_command >= 2U);
    if (kind != InvalidCorrection::stale_anchor) {
      CHECK(rejected.presented_origin->x == rejected.predicted_origin->x);
      CHECK(rejected.presented_origin->y == rejected.predicted_origin->y);
      CHECK(rejected.presented_origin->z == rejected.predicted_origin->z);
    }
    REQUIRE(world.runtime_observation());
    CHECK(world.runtime_observation()->receiving_client->origin.x ==
        Catch::Approx(10.6 + correction.position_delta.x).margin(.064));
  }
}

TEST_CASE("E32 recovery retains the existing exact 250ms horizon and rejects occupied authority",
          "[e32][contact-stale-recovery][goldsrc][live-runtime][integration][reference-positive]") {
  SECTION("inactive recovery accepts 250ms and rejects 251ms independently") {
    for (const auto age : {250ms, 251ms}) {
    CAPTURE(age.count());
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27880U);
    hlclient::client::ClientWorldState world;
    goldsrc::LiveRuntimeStage stage{transport, remote, world, e321_advancing_air_config()};
    auto route = seed_e321_advancing_air(stage, transport, remote,
        goldsrc::LiveRuntimeStageTimePoint{} + 150s);
    // Retain command four's immutable carrier, then let the last accepted
    // authoritative state expire. Each boundary check starts inactive: an
    // active stream of fresh authority may legitimately repeat an older ACK.
    for (unsigned tick = 0; tick < 2; ++tick) { route.now += 20ms; stage.update(route.now); }
    const auto carrier = stage.live_usercmd_snapshot()->transmit_ranges.back();
    REQUIRE(carrier.last_new_command_sequence == 4U);
    const auto sampled = route.now;
    for (unsigned tick = 0; tick < 12; ++tick) { route.now += 20ms; stage.update(route.now); }
    REQUIRE(stage.live_reference_prediction_snapshot(route.now).state !=
        goldsrc::LiveReferencePredictionState::active);
    const auto before = stage.live_reference_prediction_snapshot(route.now);
    route.now = sampled + age;
    transport.queue(remote, server_packet(++route.server_sequence, false,
        carrier.outgoing_netchan_sequence, false, e321_advancing_air_record(4U)));
    stage.update(route.now);
    const auto result = stage.live_reference_prediction_snapshot(route.now);
    CAPTURE(result.reason);
    if (age == 250ms) {
      REQUIRE(result.state == goldsrc::LiveReferencePredictionState::active);
      CHECK(result.anchor_command == 4U);
      CHECK(stage.movement_diagnostics_snapshot().replayed_commands == 12U);
      REQUIRE(stage.movement_diagnostics_snapshot().replayed_simulation_ms);
      CHECK(*stage.movement_diagnostics_snapshot().replayed_simulation_ms == 240.0);
    } else {
      CHECK(result.state != goldsrc::LiveReferencePredictionState::active);
      CHECK(result.reason == "reference_seed_horizon_exceeded");
      CHECK(stage.movement_diagnostics_snapshot().replayed_commands == 0U);
      CHECK(result.accepted_corrections == before.accepted_corrections);
    }
    CHECK_FALSE(stage.terminal());
    }
  }
  SECTION("physical seed rejection is not converted into a hard visual snap") {
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27881U);
    hlclient::client::ClientWorldState world;
    goldsrc::LiveRuntimeStage stage{transport, remote, world, e321_advancing_air_config()};
    E32ServerCorrectionFixture ground; ground.grounded = true;
    auto route = seed_e321_advancing_air(stage, transport, remote,
        goldsrc::LiveRuntimeStageTimePoint{} + 150s, e32_correction_collision(true), ground);
    const auto carrier = stage.live_usercmd_snapshot()->transmit_ranges.back();
    const auto before = stage.live_reference_prediction_snapshot(route.now);
    E32ServerCorrectionFixture blocked; blocked.position_delta.z = -100.F;
    route.now += 10ms;
    transport.queue(remote, server_packet(++route.server_sequence, false,
        carrier.outgoing_netchan_sequence, false,
        e321_advancing_air_record(2U, E321MovementBodyOrder::current_time_first, blocked)));
    stage.update(route.now);
    const auto after = stage.live_reference_prediction_snapshot(route.now);
    CHECK(after.state != goldsrc::LiveReferencePredictionState::active);
    CHECK_FALSE(after.presented_origin);
    CHECK(after.rebase_reason != "ready_hard_correction_snap");
    CHECK(after.accepted_corrections == before.accepted_corrections);
    CHECK(after.fallback_count == before.fallback_count + 1U);
    CHECK(stage.movement_diagnostics_snapshot().replayed_commands == 0U);
    REQUIRE(after.canonical_origin); CHECK(after.canonical_origin->z < 925.F);
    CHECK_FALSE(stage.terminal());
  }
}

TEST_CASE("E32 pose and view discontinuities cancel a camera fade between authoritative movement records",
          "[e32][server-correction-camera][goldsrc][live-runtime][integration][reference-positive]") {
  bool duck = false;
  SECTION("new setangle alone cancels while the old spawn setangle does not") {}
  SECTION("local duck command cancels the standing hull fade") { duck = true; }
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27877U);
  hlclient::client::ClientWorldState world;
  auto config = e321_advancing_air_config();
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  E32ServerCorrectionFixture initial; initial.explicit_setangle = true;
  auto route = seed_e321_advancing_air(stage, transport, remote,
      goldsrc::LiveRuntimeStageTimePoint{} + 150s, {}, initial);
  REQUIRE(world.runtime_observation()->view_angle_correction);
  const auto spawn_setangle = world.runtime_observation()->view_angle_correction->source;
  const auto carrier = stage.live_usercmd_snapshot()->transmit_ranges.back();
  E32ServerCorrectionFixture correction;
  correction.position_delta.x = 18.F; correction.velocity_delta.x = 225.F;
  route.now += 10ms;
  transport.queue(remote, server_packet(++route.server_sequence, false, carrier.outgoing_netchan_sequence,
      false, e321_advancing_air_record(2U, E321MovementBodyOrder::current_time_first, correction)));
  stage.update(route.now);
  const auto active = stage.live_reference_prediction_snapshot(route.now);
  REQUIRE(active.state == goldsrc::LiveReferencePredictionState::active);
  REQUIRE(active.presentation_reason == "visual_server_velocity_correction");
  REQUIRE(active.presented_origin); REQUIRE(active.predicted_origin);
  CHECK(std::abs(active.presented_origin->x - active.predicted_origin->x) > 16.F);
  CHECK(world.runtime_observation()->view_angle_correction->source == spawn_setangle);
  const auto clientdata_source = world.runtime_observation()->client_metadata.source;
  if (duck) {
    goldsrc::LiveVisualControlInput input{1U,2U,1.F,0,0,0,true,true};
    input.held_buttons = hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::move_forward) |
        hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::duck);
    input.pressed_buttons = hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::duck);
    REQUIRE(stage.submit_live_visual_input(input, route.now + 5ms));
    route.now += 10ms; stage.update(route.now);
  } else {
    // A genuine owning control record, with no replacement clientdata and no
    // prediction ACK. Its source identity alone invalidates the old bridge.
    std::vector<std::byte> body{static_cast<std::byte>(goldsrc::RuntimeControlOpcode::svc_setangle)};
    for (unsigned component = 0U; component < 6U; ++component) body.push_back(std::byte{});
    transport.queue(remote, server_packet(++route.server_sequence, false, carrier.outgoing_netchan_sequence,
        false, std::move(body)));
    route.now += 5ms; stage.update(route.now);
    REQUIRE(world.runtime_observation()->view_angle_correction);
    CHECK(world.runtime_observation()->view_angle_correction->source != spawn_setangle);
  }
  const auto after = stage.live_reference_prediction_snapshot(route.now);
  CAPTURE(duck, after.reason, after.presentation_reason);
  REQUIRE(after.state == goldsrc::LiveReferencePredictionState::active);
  CHECK(world.runtime_observation()->client_metadata.source == clientdata_source);
  CHECK(after.accepted_corrections == active.accepted_corrections);
  CHECK(after.fallback_count == active.fallback_count);
  CHECK(after.presentation_reason != "visual_server_velocity_correction");
  REQUIRE(after.presented_origin); REQUIRE(after.predicted_origin);
  if (duck) {
    REQUIRE(after.predicted_hull);
    CHECK(*after.predicted_hull == hlclient::movement::PlayerMovementHull::ducked);
  } else {
    CHECK(after.local_steps == active.local_steps);
    CHECK(std::abs(after.presented_origin->x - after.predicted_origin->x) < .001F);
  }
  const auto later = stage.live_reference_prediction_snapshot(route.now + 5ms);
  CHECK(later.presentation_reason != "visual_server_velocity_correction");
}

TEST_CASE("E32 death clears an active server impulse camera fade before a new life",
          "[e32][server-correction-camera][goldsrc][live-runtime][damage-respawn][integration][reference-positive]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27874U);
  hlclient::client::ClientWorldState world;
  auto config = e321_advancing_air_config();
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  auto route = seed_e321_advancing_air(stage, transport, remote,
      goldsrc::LiveRuntimeStageTimePoint{} + 150s);
  const auto carrier = stage.live_usercmd_snapshot()->transmit_ranges.back();
  E32ServerCorrectionFixture correction;
  correction.position_delta.x = 18.F; correction.velocity_delta.x = 225.F;
  route.now += 10ms;
  transport.queue(remote, server_packet(++route.server_sequence, false, carrier.outgoing_netchan_sequence,
      false, e321_advancing_air_record(2U, E321MovementBodyOrder::current_time_first, correction)));
  stage.update(route.now);
  const auto alive = stage.live_reference_prediction_snapshot(route.now);
  REQUIRE(alive.state == goldsrc::LiveReferencePredictionState::active);
  REQUIRE(alive.presented_origin); REQUIRE(alive.predicted_origin);
  REQUIRE(std::abs(alive.presented_origin->x - alive.predicted_origin->x) > 16.F);
  correction.health = 0U; correction.dead_flag = 2U; correction.additional_server_seconds = .005F;
  route.now += 5ms;
  transport.queue(remote, server_packet(++route.server_sequence, false, carrier.outgoing_netchan_sequence,
      false, e321_advancing_air_record(2U, E321MovementBodyOrder::current_time_first, correction)));
  stage.update(route.now);
  REQUIRE(world.runtime_observation()->lifecycle.dead());
  const auto dead = stage.live_reference_prediction_snapshot(route.now);
  CHECK(dead.reason == "local_player_dead"); CHECK_FALSE(dead.presented_origin); CHECK(dead.history_depth == 0U);
  const auto later = stage.live_reference_prediction_snapshot(route.now + 40ms);
  CHECK_FALSE(later.presented_origin); CHECK(later.history_depth == 0U);
  // Existing life tests cover reseeding with a strictly post-respawn carrier;
  // here even an early respawn must not revive the old residual or history.
  correction.health = 100U; correction.dead_flag = 0U;
  correction.position_delta.x = 100.F; correction.additional_server_seconds = .01F;
  route.now += 5ms;
  transport.queue(remote, server_packet(++route.server_sequence, false, carrier.outgoing_netchan_sequence,
      false, e321_advancing_air_record(2U, E321MovementBodyOrder::current_time_first, correction)));
  stage.update(route.now);
  const auto respawning = stage.live_reference_prediction_snapshot(route.now);
  CHECK(world.runtime_observation()->lifecycle.respawns == 1U);
  CHECK(respawning.state != goldsrc::LiveReferencePredictionState::active);
  CHECK_FALSE(respawning.presented_origin);
}

TEST_CASE("Normal keyboard sessions roll diagnostic windows without losing exact ACK replay",
          "[e32][usercmd-retention][goldsrc][live-runtime][integration][reference-positive]") {
  for (const std::size_t capacity : {1U, 3U}) {
    CAPTURE(capacity);
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27864U);
    const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 130s;
    hlclient::client::ClientWorldState world;
    auto config = e321_advancing_air_config();
    config.usercmd_scenario.maximum_transmit_ranges = capacity;
    config.usercmd_scenario.maximum_samples = capacity;
    goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
    auto route = seed_e321_advancing_air(stage, transport, remote, epoch);
    const auto initial = stage.live_reference_prediction_snapshot(route.now);
    for (unsigned tick = 0U; tick < 16U; ++tick) {
      const auto before = stage.live_usercmd_snapshot(); REQUIRE(before);
      const auto carrier = before->transmit_ranges.back();
      const auto boundary = carrier.last_new_command_sequence;
      transport.queue(remote, server_packet(++route.server_sequence, false,
          carrier.outgoing_netchan_sequence, false, e321_advancing_air_record(boundary)));
      route.now += 20ms; stage.update(route.now);
      CAPTURE(tick, stage.error() ? stage.error()->context : std::string{});
      REQUIRE_FALSE(stage.terminal());
      const auto prediction = stage.live_reference_prediction_snapshot(route.now);
      REQUIRE(prediction.state == goldsrc::LiveReferencePredictionState::active);
      CHECK(prediction.anchor_command == boundary);
      CHECK(prediction.accepted_corrections == initial.accepted_corrections + tick + 1U);
      CHECK(prediction.fallback_count == initial.fallback_count);
      const auto accounting = stage.live_usercmd_snapshot(); REQUIRE(accounting);
      CHECK(accounting->transmitted_packet_count == boundary + 1U);
      CHECK(accounting->new_command_submission_count == boundary + 1U);
      CHECK(accounting->first_new_command_sequence == 1U);
      CHECK(accounting->last_new_command_sequence == boundary + 1U);
      CHECK(accounting->server_sample_count == tick + 2U);
      CHECK(accounting->transmit_ranges.size() ==
          (std::min)(capacity, accounting->transmitted_packet_count));
      CHECK(accounting->server_samples.size() ==
          (std::min)(capacity, accounting->server_sample_count));
      CHECK(accounting->transmit_ranges_evicted + accounting->transmit_ranges.size() ==
          accounting->transmitted_packet_count);
      CHECK(accounting->server_samples_evicted + accounting->server_samples.size() ==
          accounting->server_sample_count);
      for (std::size_t index = 1U; index < accounting->transmit_ranges.size(); ++index) {
        CHECK(accounting->transmit_ranges[index - 1U].last_new_command_sequence + 1U ==
            accounting->transmit_ranges[index].first_new_command_sequence);
      }
      for (std::size_t index = 1U; index < accounting->server_samples.size(); ++index) {
        CHECK(accounting->server_samples[index - 1U].source.source_transport_sequence <
            accounting->server_samples[index].source.source_transport_sequence);
      }
      const auto steps = static_cast<double>(boundary);
      REQUIRE(prediction.predicted_origin); REQUIRE(prediction.predicted_velocity);
      CHECK(prediction.predicted_origin->x == Catch::Approx(10.0 + .6 * steps).margin(.064));
      CHECK(prediction.predicted_origin->z == Catch::Approx(1024.0 - .16 * steps * steps).margin(.064));
      CHECK(prediction.predicted_velocity->z == Catch::Approx(-16.0 * steps).margin(.001));
      stage.update(route.now); // No new sample, send, eviction or correction on repeated update.
      const auto repeated = stage.live_usercmd_snapshot(); REQUIRE(repeated);
      CHECK(repeated->transmitted_packet_count == accounting->transmitted_packet_count);
      CHECK(repeated->server_sample_count == accounting->server_sample_count);
      CHECK(repeated->transmit_ranges_evicted == accounting->transmit_ranges_evicted);
      CHECK(repeated->server_samples_evicted == accounting->server_samples_evicted);
      CHECK(repeated->first_new_command_sequence == accounting->first_new_command_sequence);
      CHECK(repeated->last_new_command_sequence == accounting->last_new_command_sequence);
      while (stage.poll_event()) {}
      while (stage.poll_weapon_command_submission()) {}
    }
    // Diagnostic eviction is not prediction-authority eviction: keep an exact
    // carrier, then generate enough newer sends to remove it from this window.
    const auto old = stage.live_usercmd_snapshot()->transmit_ranges.back();
    for (std::size_t index = 0U; index <= capacity; ++index) {
      route.now += 20ms; stage.update(route.now);
      REQUIRE_FALSE(stage.terminal());
      while (stage.poll_event()) {}
      while (stage.poll_weapon_command_submission()) {}
    }
    const auto before_ack = stage.live_usercmd_snapshot(); REQUIRE(before_ack);
    CHECK(std::none_of(before_ack->transmit_ranges.begin(), before_ack->transmit_ranges.end(),
        [&](const auto& retained) { return retained.outgoing_netchan_sequence == old.outgoing_netchan_sequence; }));
    const auto incoming = ++route.server_sequence;
    const auto body = e321_advancing_air_record(old.last_new_command_sequence);
    transport.queue(remote, server_packet(incoming, false, old.outgoing_netchan_sequence, false, body));
    route.now += 20ms; stage.update(route.now);
    REQUIRE_FALSE(stage.terminal());
    const auto after_ack = stage.live_reference_prediction_snapshot(route.now);
    CHECK(after_ack.state == goldsrc::LiveReferencePredictionState::active);
    CHECK(after_ack.anchor_command == old.last_new_command_sequence);
    CHECK(after_ack.history_depth == capacity + 2U);
    CHECK(after_ack.fallback_count == initial.fallback_count);
    const auto count = stage.live_usercmd_snapshot()->server_sample_count;
    transport.queue(remote, server_packet(incoming, false, old.outgoing_netchan_sequence, false, body));
    stage.update(route.now);
    CHECK(stage.live_usercmd_snapshot()->server_sample_count == count);
    CHECK(stage.live_reference_prediction_snapshot(route.now).accepted_corrections == after_ack.accepted_corrections);
  }
}

TEST_CASE("Scripted checks keep complete evidence and report typed retention exhaustion",
          "[e32][usercmd-retention][goldsrc][live-runtime][integration][reference-negative]") {
  bool sample_bound = false;
  SECTION("sent diagnostic ranges") {}
  SECTION("fresh server samples") { sample_bound = true; }
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27865U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 130s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source = goldsrc::LiveVisualControlInputSource::scripted_speed_check;
  if (sample_bound) config.usercmd_scenario.maximum_samples = 1U;
  else config.usercmd_scenario.maximum_transmit_ranges = 1U;
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  auto ready = drive_live_visual_ready(stage, transport, remote, epoch);
  auto now = ready.ready_at + 1ms;
  REQUIRE(stage.activate_live_visual_control(now));
  now += 20ms; stage.update(now);
  REQUIRE_FALSE(stage.terminal());
  if (sample_bound) {
    for (unsigned tick = 0U; tick < 2U; ++tick) {
      const auto sent = decode_sent(transport.sent.back());
      transport.queue(remote, server_packet(ready.next_server_sequence++, false,
          sent.header.sequence.sequence.value(), false,
          service_envelope(live_runtime_record(102.F + static_cast<float>(tick), 90U, 96U))));
      now += 20ms; stage.update(now);
      if (tick == 0U) REQUIRE_FALSE(stage.terminal());
    }
  } else {
    now += 20ms; stage.update(now);
  }
  REQUIRE(stage.terminal()); REQUIRE(stage.error());
  CHECK(stage.error()->code == (sample_bound ? goldsrc::LiveRuntimeStageErrorCode::usercmd_observation_failed
      : goldsrc::LiveRuntimeStageErrorCode::usercmd_transmission_failed));
  REQUIRE(stage.error()->usercmd_diagnostic_code);
  CHECK(*stage.error()->usercmd_diagnostic_code == (sample_bound
      ? goldsrc::LiveUserCmdDiagnosticErrorCode::server_sample_retention_exhausted
      : goldsrc::LiveUserCmdDiagnosticErrorCode::transmit_retention_exhausted));
  const auto accounting = stage.live_usercmd_snapshot(); REQUIRE(accounting);
  CHECK(accounting->transmit_ranges_evicted == 0U);
  CHECK(accounting->server_samples_evicted == 0U);
  if (sample_bound) { CHECK(accounting->server_sample_count == 1U); CHECK(accounting->server_samples.size() == 1U); }
  else { CHECK(accounting->transmitted_packet_count == 2U); CHECK(accounting->transmit_ranges.size() == 1U); }
}

TEST_CASE("Rolling diagnostics do not accept malformed successful-send metadata",
          "[e32][usercmd-retention][goldsrc][live-runtime][metadata][reference-negative]") {
  goldsrc::GoldSrcUserCmdTransmissionEvent event;
  event.type = goldsrc::GoldSrcUserCmdTransmissionEventType::move_packet_submitted;
  event.outgoing_netchan_sequence = 11U;
  event.first_new_command_sequence = 101U;
  event.last_new_command_sequence = 103U;
  event.new_command_count = 3U;
  event.backup_command_count = 2U;
  REQUIRE(goldsrc::inspect_live_usercmd_transmit_range(event));
  const auto valid = event;
  SECTION("missing carrier") { event.outgoing_netchan_sequence.reset(); }
  SECTION("missing first command") { event.first_new_command_sequence.reset(); }
  SECTION("missing last command") { event.last_new_command_sequence.reset(); }
  SECTION("zero first command") { event.first_new_command_sequence = 0U; }
  SECTION("no new commands") { event.new_command_count = 0U; }
  SECTION("reversed interval") { event.last_new_command_sequence = 100U; }
  SECTION("wrong command count") { event.new_command_count = 2U; }
  SECTION("not a successful send") { event.type = goldsrc::GoldSrcUserCmdTransmissionEventType::move_packet_encoded; }
  CHECK_FALSE(goldsrc::inspect_live_usercmd_transmit_range(event));
  const auto range = goldsrc::inspect_live_usercmd_transmit_range(valid); REQUIRE(range);
  CHECK(range->outgoing_netchan_sequence == 11U);
  CHECK(range->first_new_command_sequence == 101U);
  CHECK(range->last_new_command_sequence == 103U);
  CHECK(range->new_command_count == 3U);
  CHECK(range->backup_command_count == 2U);
}

TEST_CASE("E32.1 fresh movement in every reliable carrier advances anchors beyond the expiry horizon",
          "[e321][e321-reliable-movement][goldsrc][live-runtime][integration][reference-positive]") {
  bool reliable = false;
  SECTION("ordinary unreliable control follows the independent falling route") {}
  SECTION("reliable benign prefix with coherent current movement follows the same route") { reliable = true; }
  for (const bool intermediate_updates : {false, true}) {
  CAPTURE(intermediate_updates);
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27858U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 130s;
  hlclient::client::ClientWorldState world;
  const auto config = e321_advancing_air_config();
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  auto route = seed_e321_advancing_air(stage, transport, remote, epoch);
  const auto initial = stage.live_reference_prediction_snapshot(route.now);
  const auto started = route.now;
  for (unsigned tick = 0U; tick < 20U; ++tick) {
    const auto before = stage.live_usercmd_snapshot(); REQUIRE(before);
    REQUIRE_FALSE(before->transmit_ranges.empty());
    const auto carrier = before->transmit_ranges.back();
    const auto boundary = carrier.last_new_command_sequence;
    REQUIRE(boundary == tick + 2U);
    const auto before_prediction = stage.live_reference_prediction_snapshot(route.now);
    if (intermediate_updates) for (const auto offset : {7ms, 13ms}) {
      // Additional host/render updates must not change the 20ms command or
      // receive timeline. Queue the next authoritative record only afterwards.
      stage.update(route.now + offset);
      REQUIRE_FALSE(stage.terminal());
      const auto sampled = stage.live_reference_prediction_snapshot(route.now + offset);
      const auto sampled_commands = stage.live_usercmd_snapshot(); REQUIRE(sampled_commands);
      CHECK(sampled_commands->generated_command_count == before->generated_command_count);
      CHECK(sampled_commands->new_command_submission_count == before->new_command_submission_count);
      CHECK(sampled_commands->transmit_ranges.size() == before->transmit_ranges.size());
      CHECK(sampled.anchor_command == before_prediction.anchor_command);
      CHECK(sampled.accepted_corrections == before_prediction.accepted_corrections);
      CHECK(sampled.last_anchor_status == before_prediction.last_anchor_status);
      CHECK(sampled.anchor_coherent_reliable_frame == before_prediction.anchor_coherent_reliable_frame);
      CHECK(sampled.fallback_count == initial.fallback_count);
      CHECK(sampled.state == goldsrc::LiveReferencePredictionState::active);
      REQUIRE(sampled.presented_origin);
      CHECK(std::isfinite(sampled.presented_origin->x));
      CHECK(std::isfinite(sampled.presented_origin->y));
      CHECK(std::isfinite(sampled.presented_origin->z));
    }
    transport.queue(remote, server_packet(++route.server_sequence, reliable,
        carrier.outgoing_netchan_sequence, false, e321_advancing_air_record(boundary)));
    route.now += 20ms; stage.update(route.now);
    CAPTURE(reliable, tick, boundary);
    REQUIRE_FALSE(stage.terminal());
    const auto prediction = stage.live_reference_prediction_snapshot(route.now);
    const auto accounting = stage.live_usercmd_snapshot(); REQUIRE(accounting);
    CHECK(accounting->generated_command_count == boundary + 1U);
    CHECK(prediction.canonical_source_sequence == route.server_sequence);
    CHECK(prediction.canonical_body_kind == (reliable ? "reliable_body" : "direct_body"));
    REQUIRE(world.runtime_observation()); REQUIRE(world.runtime_observation()->client_metadata.source);
    const auto& raw_source = *world.runtime_observation()->client_metadata.source;
    CHECK(raw_source.source_reliable == reliable);
    CHECK_FALSE(raw_source.reassembled);
    CHECK_FALSE(raw_source.contemporaneous_fragment_suffix);
    CHECK(prediction.anchor_coherent_reliable_frame == reliable);
    CHECK(prediction.last_anchor_status == goldsrc::ReferencePredictionAnchorStatus::bound);
    CHECK(accounting->reference_prediction_last_anchor_status == prediction.last_anchor_status);
    CHECK(prediction.state == goldsrc::LiveReferencePredictionState::active);
    CHECK(prediction.anchor_command == boundary);
    CHECK(prediction.accepted_corrections == initial.accepted_corrections + tick + 1U);
    CHECK(prediction.fallback_count == initial.fallback_count);
    REQUIRE(prediction.canonical_origin);
    const double server_steps = static_cast<double>(boundary - 1U);
    CHECK(prediction.canonical_origin->x == Catch::Approx(std::round((10.0 + .6 * server_steps) * 8.0) / 8.0));
    CHECK(prediction.canonical_origin->z == Catch::Approx(std::round((1024.0 - .16 * server_steps * server_steps) * 8.0) / 8.0));
    // One immutable command is newer than each exact ACK. Compare its replay
    // with a closed-form oracle, not the predicted state used as server input.
    if (prediction.predicted_origin && prediction.predicted_velocity) {
      const double steps = static_cast<double>(boundary);
      CHECK(prediction.predicted_origin->x == Catch::Approx(10.0 + .6 * steps).margin(.064));
      CHECK(prediction.predicted_origin->y == Catch::Approx(0.0).margin(.001));
      CHECK(prediction.predicted_origin->z == Catch::Approx(1024.0 - .16 * steps * steps).margin(.064));
      CHECK(prediction.predicted_velocity->x == Catch::Approx(30.0).margin(.001));
      CHECK(prediction.predicted_velocity->z == Catch::Approx(-16.0 * steps).margin(.001));
    } else {
      CHECK(prediction.predicted_origin.has_value()); CHECK(prediction.predicted_velocity.has_value());
    }
    const auto sent_count = transport.sent.size();
    stage.update(route.now);
    const auto repeated = stage.live_reference_prediction_snapshot(route.now);
    CHECK(repeated.accepted_corrections == prediction.accepted_corrections);
    CHECK(repeated.anchor_command == prediction.anchor_command);
    CHECK(stage.live_usercmd_snapshot()->generated_command_count == accounting->generated_command_count);
    CHECK(transport.sent.size() == sent_count);
    while (stage.poll_event()) {}
    while (stage.poll_weapon_command_submission()) {}
  }
  CHECK(route.now - started == 400ms); // longer than the retained 250ms anchor deadline
  }
}

TEST_CASE("E32.1 reliable movement cannot borrow later inherited repeated time or a reassembly ACK",
          "[e321][e321-reliable-movement][goldsrc][live-runtime][integration][reference-negative]") {
  auto order = E321MovementBodyOrder::current_time_after_movement;
  bool reassembled = false;
  SECTION("a later svc_time cannot freshen earlier clientdata and entities") {}
  SECTION("retained svc_time from another record does not prove a current reliable movement body") {
    order = E321MovementBodyOrder::inherited_time;
  }
  SECTION("equal server time does not prove a newly produced reliable movement body") {
    order = E321MovementBodyOrder::repeated_time;
  }
  SECTION("even coherent movement in a real completed reliable fragment keeps the old-body guard") {
    order = E321MovementBodyOrder::current_time_first; reassembled = true;
  }
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27859U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 140s;
  hlclient::client::ClientWorldState world;
  const auto config = e321_advancing_air_config();
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  auto route = seed_e321_advancing_air(stage, transport, remote, epoch);
  const auto initial = stage.live_reference_prediction_snapshot(route.now);
  const auto before = stage.live_usercmd_snapshot(); REQUIRE(before);
  REQUIRE_FALSE(before->transmit_ranges.empty());
  const auto carrier = before->transmit_ranges.back();
  const auto body = e321_advancing_air_record(carrier.last_new_command_sequence, order);
  const auto incoming = ++route.server_sequence;
  transport.queue(remote, reassembled ? normal_fragment_packet(incoming, carrier.outgoing_netchan_sequence,
      false, 1U, 1U, body) : server_packet(incoming, true, carrier.outgoing_netchan_sequence, false, body));
  route.now += 20ms; stage.update(route.now);
  REQUIRE_FALSE(stage.terminal());
  const auto rejected = stage.live_reference_prediction_snapshot(route.now);
  // Network publication is still allowed. Only the temporal authority needed
  // to attach these movement bytes to this carrier's command ACK is denied.
  CHECK(rejected.canonical_source_sequence == incoming);
  CHECK(rejected.anchor_command == initial.anchor_command);
  CHECK(rejected.accepted_corrections == initial.accepted_corrections);
  CHECK(rejected.fallback_count == initial.fallback_count);
  CHECK(rejected.state == goldsrc::LiveReferencePredictionState::active);
  REQUIRE(rejected.last_anchor_status);
  CHECK(rejected.last_anchor_status != goldsrc::ReferencePredictionAnchorStatus::bound);
  CHECK_FALSE(rejected.anchor_coherent_reliable_frame);

  // A later actual outgoing command must not overwrite the latest incoming
  // rejection with record_sent's successful status. This was an independent
  // diagnostic blind spot in the user's incident capture.
  const auto rejected_commands = stage.live_usercmd_snapshot(); REQUIRE(rejected_commands);
  CHECK(rejected_commands->reference_prediction_last_anchor_status == rejected.last_anchor_status);
  route.now += 20ms; stage.update(route.now);
  REQUIRE_FALSE(stage.terminal());
  const auto outgoing_only = stage.live_reference_prediction_snapshot(route.now);
  const auto outgoing_commands = stage.live_usercmd_snapshot(); REQUIRE(outgoing_commands);
  CHECK(outgoing_commands->generated_command_count == rejected_commands->generated_command_count + 1U);
  CHECK(outgoing_commands->new_command_submission_count == rejected_commands->new_command_submission_count + 1U);
  CHECK(outgoing_commands->reference_prediction_last_anchor_status == rejected.last_anchor_status);
  CHECK(outgoing_only.last_anchor_status == rejected.last_anchor_status);
  CHECK(outgoing_only.anchor_command == initial.anchor_command);
  CHECK(outgoing_only.accepted_corrections == initial.accepted_corrections);
  CHECK(outgoing_only.fallback_count == initial.fallback_count);
  CHECK(outgoing_only.state == goldsrc::LiveReferencePredictionState::active);

  // Rejection does not reserve a boundary or poison the next genuine current
  // carrier. Recovery uses newer exact ACK/time and the independently authored
  // same route, with no new signon or prediction reset.
  const auto next = stage.live_usercmd_snapshot(); REQUIRE(next);
  REQUIRE_FALSE(next->transmit_ranges.empty());
  const auto current = next->transmit_ranges.back();
  transport.queue(remote, server_packet(++route.server_sequence, false, current.outgoing_netchan_sequence,
      false, e321_advancing_air_record(current.last_new_command_sequence)));
  route.now += 20ms; stage.update(route.now);
  REQUIRE_FALSE(stage.terminal());
  const auto recovered = stage.live_reference_prediction_snapshot(route.now);
  CHECK(recovered.anchor_command == current.last_new_command_sequence);
  CHECK(recovered.accepted_corrections == initial.accepted_corrections + 1U);
  CHECK(recovered.fallback_count == initial.fallback_count);
  CHECK(recovered.state == goldsrc::LiveReferencePredictionState::active);
}

TEST_CASE("E21 stock vertical-only eye reaches Gauss before TX dedup in reference and off",
          "[e21-1][e21][goldsrc][live-runtime][integration][no-stock]") {
  namespace api=hlclient::game_api;
  for(const bool prediction:{false,true}) {
    CAPTURE(prediction);
    FakeTransport transport;
    const auto remote=network::NetworkAddress::loopback(27857U);
    const auto epoch=goldsrc::LiveRuntimeStageTimePoint{}+60s;
    hlclient::client::ClientWorldState world;
    auto config=live_test_config();
    config.operation_mode=goldsrc::LiveRuntimeOperationMode::live_visual_control;
    config.live_visual_input_source=goldsrc::LiveVisualControlInputSource::keyboard_mouse;
    config.reference_prediction=prediction;
    config.game_client->set_movement_audio_time_origin(60.0);
    goldsrc::LiveRuntimeStage stage{transport,remote,world,config};
    const auto driven=drive_live_initial(stage,transport,remote,epoch,false,false,true,true);
    const auto resource=driven.last_server_sequence+1U;
    transport.queue(remote,server_packet(resource,false,
        driven.transition_request.header.sequence.sequence.value(),false,
        service_envelope(resource_semantic_payload())));
    transport.queue(remote,server_packet(resource+1U,false,
        driven.transition_request.header.sequence.sequence.value(),false,{std::byte{1}}));
    for(std::size_t step=1;step<=8;++step)stage.update(driven.now+std::chrono::milliseconds{step});
    constexpr std::array empty_response{std::byte{5},std::byte{0},std::byte{0}};
    const auto response=require_latest_payload(transport,empty_response);
    const auto first=driven.now+9ms;
    transport.queue(remote,server_packet(resource+2U,false,response.header.sequence.sequence.value(),
        response.header.sequence.flags.reliable));
    stage.update(first);
    const auto spawn=require_latest_payload(transport,goldsrc::StockSpawnRequestBuilder::build(
        0x12345678U,goldsrc::decode_stock_server_world_map_crc(0xdeadbeefU,0U)).encoding->semantic_bytes());
    transport.queue(remote,server_packet(resource+3U,true,spawn.header.sequence.sequence.value(),false,
        live_baseline_and_runtime_payload(100.F,100U,80U,true)));
    stage.update(first+1ms);stage.update(first+2ms);
    const auto sendents=require_latest_payload(transport,goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
    transport.queue(remote,server_packet(resource+4U,false,sendents.header.sequence.sequence.value(),
        sendents.header.sequence.flags.reliable,service_envelope(live_prediction_air_record(101.F,10U))));
    stage.update(first+3ms);stage.update(first+2s+2ms);
    REQUIRE(stage.live_visual_input_ready());
    if(prediction)REQUIRE(stage.attach_reference_prediction_collision(hlclient::tests::collision_brush_fixture::package(false)));
    auto now=first+2s+3ms;
    goldsrc::LiveVisualControlInput input{1,1,0,0,0,0,true,true};
    REQUIRE(stage.submit_live_visual_input(input,now));REQUIRE(stage.activate_live_visual_control(now));
    REQUIRE(world.runtime_observation());REQUIRE(world.runtime_observation()->receiving_client);
    const auto& wire_eye=world.runtime_observation()->receiving_client->view_offset;
    CHECK_FALSE(wire_eye.x);CHECK_FALSE(wire_eye.y);REQUIRE(wire_eye.z);CHECK(*wire_eye.z==28);

    // Bind independent Gauss semantic observations to the same real host.
    // The stage still constructs command eye/direction from its decoded stock-
    // shaped frame, which deliberately has no view_ofs[0]/[1] descriptors.
    auto state=*world.runtime_observation();auto& client=*state.receiving_client;
    client.viewmodel_index=73;client.owned_weapon_bits=1U<<9;client.next_weapon_attack=0;
    client.auxiliary_float2=0;client.auxiliary_float3=0;
    state.weapon_hud.active_weapon_id=9;state.weapon_hud.reserve_ammo[1]=50;
    state.weapon_hud.catalogue={{.id=9,.command_name="weapon_gauss",.primary_ammo_type=1,
        .slot=3,.position=1}};
    state.weapon_slots={{.wire_slot=9,.clip=-1,.in_reload=false,.next_primary_attack=0,
        .weapon_id=9,.next_secondary_attack=0,.auxiliary_integer2=0}};
    api::LocalWeaponModelMetadata model;model.generation=state.generation;model.model_index=73;
    model.resource_revision=1;model.resource_name="models/v_gauss.mdl";
    model.supported_bodies[2]=model.selectable_bodies[2]=true;
    for(const auto label:{"idle","idle2","fidget","spinup","spin","fire","fire2","holster","draw"})
      model.sequences.push_back({30,31,std::string_view{label}=="spin",{},label});
    config.game_client->bind_model(model);
    config.game_client->observe(state,std::chrono::duration<double>(now-epoch).count());
    struct Wall final:api::IStaticEffectTrace {
      using V=hlclient::assets::AssetVector3;
      api::StaticEffectTrace trace(V start,V) const noexcept override {
        return {true,false,false,true,false,{start.x+16,start.y,start.z},{-1,0,0}};
      }
    } wall;
    const auto attack=hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::attack_primary);
    ++input.input_revision;input.held_buttons=input.pressed_buttons=attack;
    REQUIRE(stage.submit_live_visual_input(input,now));now+=20ms;stage.update(now);
    REQUIRE_FALSE(stage.terminal());
    // Render/audio delivery follows the command sample, as in the application.
    // Avoid subtracting large absolute clocks at an exact future boundary.
    const auto seconds=std::chrono::duration<double>(now-epoch).count()+.001;
    const auto shot=config.game_client->sample(seconds);REQUIRE(shot.identity);
    CHECK(shot.actions_started==1);REQUIRE(shot.visual);CHECK(shot.visual->sequence==6);
    const auto audio=config.game_client->drain_audio();REQUIRE(audio.count==1);
    CHECK(audio.cues[0].reference.name()=="weapons/gauss2.wav");CHECK(audio.cues[0].serial>0);
    const auto beams=config.game_client->beam_effects(wall,seconds,{});
    CAPTURE(seconds,shot.identity->started_at_seconds,beams.shots,beams.count,beams.rejected,beams.impact_count);
    CHECK(beams.shots==1);REQUIRE(beams.count>1);CHECK(beams.sprites[0].ribbon);
    CHECK(beams.sprites[0].start.x==Catch::Approx(10));CHECK(beams.sprites[0].start.y==0);
    CHECK(beams.sprites[0].start.z==Catch::Approx(92));CHECK(beams.impact_count==1);
    const auto submitted=stage.poll_weapon_command_submission();REQUIRE(submitted);
    CHECK(submitted->command_sequence==shot.identity->command_sequence);
    api::LocalWeaponSubmittedCommand duplicate{state.generation,submitted->command_sequence,submitted->buttons,
        std::chrono::duration<double>(std::chrono::nanoseconds{submitted->command_end_nanoseconds}-epoch.time_since_epoch()).count()};
    duplicate.shot_context=api::LocalWeaponSubmittedCommand::ShotContext{{999,999,999},{1,0,0},{0,-1,0},{0,0,1}};
    config.game_client->submit(duplicate,seconds);stage.update(now);
    CHECK(config.game_client->sample(seconds).actions_started==1);
    CHECK(config.game_client->drain_audio().count==0);
    CHECK(config.game_client->beam_effects(wall,seconds,{}).impact_count==0);
    const auto step=[&](unsigned held,unsigned pressed) {
      ++input.input_revision;input.released_buttons=input.held_buttons&~held;
      input.held_buttons=held;input.pressed_buttons=pressed;
      REQUIRE(stage.submit_live_visual_input(input,now));now+=20ms;stage.update(now);
      REQUIRE_FALSE(stage.terminal());return config.game_client->drain_audio();
    };
    for(unsigned i=0;i<16;++i)(void)step(0,0);
    const auto secondary=hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::attack_secondary);
    const auto spin=step(secondary,secondary);REQUIRE(spin.control_count==1);
    CHECK(spin.controls[0].operation==api::LocalVoiceControl::Operation::start);
    for(unsigned i=0;i<80;++i)(void)step(secondary,0);
    const auto discharge=step(0,0);REQUIRE(discharge.count==1);REQUIRE(discharge.control_count==1);
    CHECK(discharge.cues[0].reference.name()=="weapons/gauss2.wav");CHECK(discharge.cues[0].volume==1);
    CHECK(discharge.controls[0].operation==api::LocalVoiceControl::Operation::stop);
    const auto released=std::chrono::duration<double>(now-epoch).count()+.001;
    CHECK(config.game_client->sample(released).actions_started==2);
    const auto charged=config.game_client->beam_effects(wall,released,{});
    CHECK(charged.shots==2);REQUIRE(charged.count>1);CHECK(charged.sprites[0].width==Catch::Approx(3.5));
    CHECK(charged.impact_count>0);CHECK(config.game_client->drain_audio().count==0);
  }
}
TEST_CASE("D2 real live owner clears Use on capture loss latches short tap and retains RX with contextual TX",
          "[use][goldsrc][live-runtime][integration]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27851U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 60s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source = goldsrc::LiveVisualControlInputSource::keyboard_mouse;
  config.reference_prediction = true;
  goldsrc::LiveRuntimeStage stage{transport,remote,world,config};
  const auto driven = drive_live_initial(stage,transport,remote,epoch,false,false,true,true);
  const auto resource = driven.last_server_sequence + 1U;
  transport.queue(remote,server_packet(resource,false,
      driven.transition_request.header.sequence.sequence.value(),false,
      service_envelope(resource_semantic_payload())));
  transport.queue(remote,server_packet(resource+1U,false,
      driven.transition_request.header.sequence.sequence.value(),false,{std::byte{1}}));
  for (std::size_t step=1; step<=8; ++step) stage.update(driven.now+std::chrono::milliseconds{step});
  constexpr std::array empty_response{std::byte{5},std::byte{0},std::byte{0}};
  const auto response = require_latest_payload(transport,empty_response);
  const auto first = driven.now+9ms;
  transport.queue(remote,server_packet(resource+2U,false,response.header.sequence.sequence.value(),
      response.header.sequence.flags.reliable));
  stage.update(first);
  const auto spawn = require_latest_payload(transport,goldsrc::StockSpawnRequestBuilder::build(
      0x12345678U,goldsrc::decode_stock_server_world_map_crc(0xdeadbeefU,0U)).encoding->semantic_bytes());
  transport.queue(remote,server_packet(resource+3U,true,spawn.header.sequence.sequence.value(),
      false,live_baseline_and_runtime_payload(100.0F,100U,80U,true)));
  stage.update(first+1ms); stage.update(first+2ms);
  const auto sendents = require_latest_payload(transport,goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
  transport.queue(remote,server_packet(resource+4U,false,sendents.header.sequence.sequence.value(),
      sendents.header.sequence.flags.reliable,service_envelope(live_prediction_air_record(101.0F,10U))));
  stage.update(first+3ms); stage.update(first+2s+2ms);
  REQUIRE(stage.live_visual_input_ready());
  REQUIRE(stage.attach_reference_prediction_collision(hlclient::tests::collision_brush_fixture::package(false)));

  auto now = first+2s+3ms;
  goldsrc::LiveVisualControlInput input{1,1,0,0,0,0,true,true};
  REQUIRE(stage.submit_live_visual_input(input,now));
  REQUIRE(stage.activate_live_visual_control(now));
  const auto use = hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::use);
  auto sample = [&](bool capture, unsigned held, unsigned pressed, unsigned released = 0U) {
    ++input.input_revision; input.captured=capture;
    input.held_buttons=held; input.pressed_buttons=pressed; input.released_buttons=released;
    REQUIRE(stage.submit_live_visual_input(input,now));
    now += 20ms; stage.update(now);
    REQUIRE_FALSE(stage.terminal());
    return *stage.live_usercmd_snapshot();
  };
  const auto initial = sample(true,use,use);
  CHECK(initial.use_generated_count == 1U);
  const auto held = sample(true,use,0U);
  CHECK(held.use_generated_count == 2U);
  CHECK(held.use_command_press_count == 1U);
  const auto loss = sample(false,use,0U);
  CHECK(loss.use_generated_count == 2U);
  CHECK(loss.use_command_release_count == 1U);
  CHECK(loss.use_clear_after_release_count == 1U);
  const auto stale = sample(true,use,0U);
  CHECK(stale.use_generated_count == 2U);
  sample(true,0U,0U,use);
  const auto fresh = sample(true,use,use);
  CHECK(fresh.use_generated_count == 3U);
  CHECK(fresh.use_command_press_count == 2U);
  sample(true,0U,0U,use);
  // Short tap arrives between command samples, then another render input
  // arrives before the command boundary. Pending is consumed by history only.
  ++input.input_revision; input.held_buttons=0U; input.pressed_buttons=use; input.released_buttons=use;
  REQUIRE(stage.submit_live_visual_input(input,now));
  ++input.input_revision; input.pressed_buttons=0U; input.released_buttons=0U;
  REQUIRE(stage.submit_live_visual_input(input,now+1ms));
  now += 20ms; stage.update(now);
  const auto tap = *stage.live_usercmd_snapshot();
  CHECK(tap.use_generated_count == 4U);
  CHECK(tap.use_command_press_count == 3U);
  const auto clear = sample(true,0U,0U);
  CHECK(clear.use_generated_count == 4U);
  CHECK(clear.use_clear_after_release_count == 3U);
  CHECK(clear.command_interval == 20ms);
  CHECK(clear.generated_command_count == clear.new_command_submission_count);
  // RX remains active in the update that transmits held Use.
  const auto sent = decode_sent(transport.sent.back());
  transport.queue(remote,server_packet(resource+5U,false,sent.header.sequence.sequence.value(),false,
      service_envelope(live_prediction_air_record(102.0F,10U))));
  const auto rx_before = clear.driver_rx_total.owning_datagrams;
  const auto with_rx = sample(true,use,use);
  CHECK(with_rx.driver_rx_total.owning_datagrams == rx_before+1U);
  CHECK(with_rx.runtime_records_committed_total == clear.runtime_records_committed_total+1U);
  const auto stable = *stage.live_usercmd_snapshot();
  const auto canonical = hlclient::client::runtime_observation_canonical_hash(*world.runtime_observation());
  for (unsigned frame=0; frame<20; ++frame) static_cast<void>(stage.live_reference_prediction_snapshot(now+std::chrono::microseconds{frame}));
  CHECK(stage.live_usercmd_snapshot()->use_generated_count == stable.use_generated_count);
  CHECK(stage.live_usercmd_snapshot()->use_new_submission_count == stable.use_new_submission_count);
  CHECK(hlclient::client::runtime_observation_canonical_hash(*world.runtime_observation()) == canonical);
  input.focused=false; input.captured=false; input.held_buttons=0U; input.pressed_buttons=0U;
  ++input.input_revision; REQUIRE(stage.submit_live_visual_input(input,now));
  now += 20ms; stage.update(now);
  CHECK(stage.live_usercmd_snapshot()->use_generated_count == stable.use_generated_count);
}

TEST_CASE("E16 normal live owner transmits held secondary and publishes only accepted new TX commands",
          "[e16][e21][e24][secondary][goldsrc][live-runtime][integration][normal-input]") {
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27851U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 60s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source = goldsrc::LiveVisualControlInputSource::keyboard_mouse;
  config.reference_prediction = true;
  REQUIRE(config.game_client->movement_policy().button_policy ==
      goldsrc::GoldSrcReferenceButtonPolicy::jump_duck_primary_secondary_reload_use);
  goldsrc::LiveRuntimeStage stage{transport,remote,world,config};
  const auto driven = drive_live_initial(stage,transport,remote,epoch,false,false,true,true);
  const auto resource = driven.last_server_sequence + 1U;
  transport.queue(remote,server_packet(resource,false,
      driven.transition_request.header.sequence.sequence.value(),false,
      service_envelope(resource_semantic_payload())));
  transport.queue(remote,server_packet(resource+1U,false,
      driven.transition_request.header.sequence.sequence.value(),false,{std::byte{1}}));
  for (std::size_t step=1; step<=8; ++step) stage.update(driven.now+std::chrono::milliseconds{step});
  constexpr std::array empty_response{std::byte{5},std::byte{0},std::byte{0}};
  const auto response = require_latest_payload(transport,empty_response);
  const auto first = driven.now+9ms;
  transport.queue(remote,server_packet(resource+2U,false,response.header.sequence.sequence.value(),
      response.header.sequence.flags.reliable));
  stage.update(first);
  const auto spawn = require_latest_payload(transport,goldsrc::StockSpawnRequestBuilder::build(
      0x12345678U,goldsrc::decode_stock_server_world_map_crc(0xdeadbeefU,0U)).encoding->semantic_bytes());
  transport.queue(remote,server_packet(resource+3U,true,spawn.header.sequence.sequence.value(),
      false,live_baseline_and_runtime_payload(100.0F,100U,80U,true)));
  stage.update(first+1ms); stage.update(first+2ms);
  const auto sendents = require_latest_payload(transport,goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
  transport.queue(remote,server_packet(resource+4U,false,sendents.header.sequence.sequence.value(),
      sendents.header.sequence.flags.reliable,service_envelope(live_prediction_air_record(101.0F,10U))));
  stage.update(first+3ms); stage.update(first+2s+2ms);
  REQUIRE(stage.live_visual_input_ready());
  REQUIRE(stage.attach_reference_prediction_collision(hlclient::tests::collision_brush_fixture::package(false)));

  auto now = first+2s+3ms;
  goldsrc::LiveVisualControlInput input{1,1,0,0,0,0,true,true};
  REQUIRE(stage.submit_live_visual_input(input,now));
  REQUIRE(stage.activate_live_visual_control(now));
  const auto secondary = hlclient::gameplay_input::gameplay_button_mask(
      hlclient::gameplay_input::GameplayButton::attack_secondary);
  // Weapon buttons deliberately do not use the short movement/Use edge latch.
  // This must pass through the live owner's held-action reconstruction.
  CHECK((goldsrc::LiveVisualButtonLatch::allowed() & secondary) == 0U);
  auto registry = hlclient::test::usercmd_fixture::exact_registry();
  const auto bound = goldsrc::bind_goldsrc_usercmd_schema(registry,
      goldsrc::GoldSrcUserCmdSchemaBindingProfile::public_goldsrc48_usercmd_schema_v1);
  REQUIRE(bound);
  goldsrc::GoldSrcReferenceClientMoveCodec codec{*bound.binding,1U};
  std::uint32_t last_notified_sequence{};
  const auto sample = [&](bool focused,bool captured,unsigned held,unsigned released,
                          std::uint16_t expected) {
    ++input.input_revision;
    CAPTURE(input.input_revision,focused,captured,held,released,expected);
    input.focused=focused; input.captured=captured;
    input.held_buttons=held; input.pressed_buttons=0U; input.released_buttons=released;
    REQUIRE(stage.submit_live_visual_input(input,now));
    CHECK_FALSE(stage.submit_live_visual_input(input,now)); // duplicate input revision is not another command.
    now += 20ms;
    stage.update(now);
    REQUIRE_FALSE(stage.terminal());
    const auto sent = decode_sent(transport.sent.back());
    goldsrc::ReferenceMoveSource source;
    source.generation=1U; source.sequence=sent.header.sequence.sequence.value();
    const auto decoded = goldsrc::decode_reference_client_payload(codec,sent.payload,source);
    REQUIRE_FALSE(decoded.error);
    REQUIRE(decoded.moves.size()==1U);
    const auto& move=decoded.moves[0];
    REQUIRE(move.new_count==1U);
    REQUIRE_FALSE(move.commands.empty());
    CHECK(move.commands.back().value.buttons==expected);
    CHECK(move.commands.back().value.msec==20U);
    const auto state=stage.live_usercmd_snapshot();
    REQUIRE(state);
    CHECK(state->command_interval==20ms);
    CHECK(state->generated_command_count==state->new_command_submission_count);
    const auto notification=stage.poll_weapon_command_submission();
    // E24 requires every accepted immutable command, including a neutral tick
    // after release. Exactly one delivery, unchanged wire bytes and cadence.
    REQUIRE(notification);
    CHECK(notification->buttons==expected);
    CHECK(notification->command_sequence==state->transmit_ranges.back().last_new_command_sequence);
    CHECK(notification->command_sequence>last_notified_sequence);
    CHECK(notification->command_end_nanoseconds==state->scheduler_next_sample_time_nanoseconds-20'000'000);
    last_notified_sequence=notification->command_sequence;
    CHECK_FALSE(stage.poll_weapon_command_submission());
    stage.update(now); // Rendering/repeated update at the same clock cannot re-notify TX.
    CHECK_FALSE(stage.poll_weapon_command_submission());
    return *state;
  };
  // Initial sign-on life publication invalidates scoped actions. A stale held
  // button cannot become an attack until a physical release/press is observed.
  sample(true,true,secondary,0U,0U);
  sample(true,true,0U,secondary,0U);
  sample(true,true,secondary,0U,2048U);
  const auto held=sample(true,true,secondary,0U,2048U); // No fresh press/latch is required.
  const auto before_rx=decode_sent(transport.sent.back());
  transport.queue(remote,server_packet(resource+5U,false,before_rx.header.sequence.sequence.value(),false,
      service_envelope(live_prediction_air_record(102.0F,10U))));
  const auto with_rx=sample(true,true,secondary,0U,2048U);
  CHECK(with_rx.driver_rx_total.owning_datagrams==held.driver_rx_total.owning_datagrams+1U);
  CHECK(with_rx.runtime_records_committed_total==held.runtime_records_committed_total+1U);
  sample(true,true,0U,secondary,0U);
  sample(true,true,secondary,0U,2048U);
  sample(true,false,secondary,0U,0U); // Loss of capture clears and invalidates held secondary.
  sample(true,true,secondary,0U,0U); // Recapture cannot replay stale held RMB.
  sample(true,true,0U,secondary,0U);
  sample(true,true,secondary,0U,2048U);
  sample(false,false,0U,secondary,0U);
  sample(true,true,secondary,0U,0U); // Focus recovery also requires a real release.
  sample(true,true,0U,secondary,0U);
  const auto final=sample(true,true,secondary,0U,2048U);
  const auto sent_before_replay=transport.sent.size();
  for(unsigned frame=0;frame<20;++frame)
    static_cast<void>(stage.live_reference_prediction_snapshot(now+std::chrono::microseconds{frame}));
  CHECK(transport.sent.size()==sent_before_replay);
  CHECK(stage.live_usercmd_snapshot()->new_command_submission_count==final.new_command_submission_count);
  CHECK_FALSE(stage.poll_weapon_command_submission());
}
TEST_CASE("Fullscreen frame gaps preserve pending movement ownership and resume contiguous commands",
          "[e311][fullscreen-usercmd][goldsrc][live-runtime][integration][normal-input][no-stock]") {
  bool stale_press = false;
  bool focus_loss = false;
  bool cancel_pending = false;
  bool close_pending = false;
  SECTION("A blocked send survives later 20 and 60 millisecond frames") {}
  SECTION("Long backpressure does not replay a stale one-shot press") {
    stale_press = true;
  }
  SECTION("Focus loss clears an unsampled press while movement is pending") { focus_loss = true; }
  SECTION("Cancellation abandons the pending movement without late commands") { cancel_pending = true; }
  SECTION("Graceful close abandons movement before the disconnect packet") { close_pending = true; }

  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27861U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 60s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source = goldsrc::LiveVisualControlInputSource::keyboard_mouse;
  auto registry = hlclient::test::usercmd_fixture::exact_registry();
  const auto bound = goldsrc::bind_goldsrc_usercmd_schema(registry,
      goldsrc::GoldSrcUserCmdSchemaBindingProfile::public_goldsrc48_usercmd_schema_v1);
  REQUIRE(bound);
  goldsrc::GoldSrcReferenceClientMoveCodec codec{*bound.binding, 1U};
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  const auto driven = drive_live_initial(stage, transport, remote, epoch,
      false, false, true, true);
  const auto resource = driven.last_server_sequence + 1U;
  transport.queue(remote, server_packet(resource, false,
      driven.transition_request.header.sequence.sequence.value(), false,
      service_envelope(resource_semantic_payload())));
  transport.queue(remote, server_packet(resource + 1U, false,
      driven.transition_request.header.sequence.sequence.value(), false, {std::byte{1}}));
  for (std::size_t step = 1U; step <= 8U; ++step)
    stage.update(driven.now + std::chrono::milliseconds{step});
  constexpr std::array empty_response{std::byte{5}, std::byte{0}, std::byte{0}};
  const auto response = require_latest_payload(transport, empty_response);
  const auto first = driven.now + 9ms;
  transport.queue(remote, server_packet(resource + 2U, false,
      response.header.sequence.sequence.value(), response.header.sequence.flags.reliable));
  stage.update(first);
  const auto spawn = require_latest_payload(transport,
      goldsrc::StockSpawnRequestBuilder::build(0x12345678U,
          goldsrc::decode_stock_server_world_map_crc(0xdeadbeefU, 0U)).encoding->semantic_bytes());
  transport.queue(remote, server_packet(resource + 3U, true,
      spawn.header.sequence.sequence.value(), false,
      live_baseline_and_runtime_payload(100.0F, 100U, 80U, true)));
  stage.update(first + 1ms);
  stage.update(first + 2ms);
  const auto sendents = require_latest_payload(transport,
      goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
  transport.queue(remote, server_packet(resource + 4U, false,
      sendents.header.sequence.sequence.value(), sendents.header.sequence.flags.reliable,
      service_envelope(live_prediction_air_record(101.0F, 10U))));
  stage.update(first + 3ms);
  stage.update(first + 2s + 2ms);
  REQUIRE(stage.live_visual_input_ready());
  auto now = first + 2s + 3ms;
  goldsrc::LiveVisualControlInput input{1U, 1U, 0, 0, 0, 0, true, true};
  REQUIRE(stage.submit_live_visual_input(input, now));
  REQUIRE(stage.activate_live_visual_control(now));
  const auto sent_at_activation = transport.sent.size();
  transport.queue_send_status(network::DatagramSendStatus::would_block);
  now += 20ms;
  stage.update(now);
  REQUIRE_FALSE(stage.terminal());
  const auto pending = stage.live_usercmd_snapshot();
  REQUIRE(pending);
  constexpr std::size_t originally_generated = 1U;
  REQUIRE(pending->generated_command_count == originally_generated);
  CHECK(pending->new_command_submission_count == 0U);
  CHECK(transport.sent.size() == sent_at_activation);
  if (cancel_pending || close_pending) {
    if (cancel_pending) stage.cancel(now);
    else REQUIRE(stage.request_graceful_close(now));
    ++input.input_revision;
    CHECK_FALSE(stage.submit_live_visual_input(input, now));
    now += 20ms;
    stage.update(now);
    if (close_pending) {
      REQUIRE(transport.sent.size() > sent_at_activation);
      const auto goodbye = decode_sent(transport.sent.back());
      const auto expected_goodbye = goldsrc::DisconnectRequestBuilder::build();
      CHECK(std::equal(goodbye.payload.begin(), goodbye.payload.end(),
          expected_goodbye.begin(), expected_goodbye.end()));
      now += 2s;
      stage.update(now);
    }
    REQUIRE(stage.terminal());
    const auto sent_before_repeated = transport.sent.size();
    const auto attempts_before_repeated = transport.send_attempt_count;
    stage.update(now + 20ms);
    CHECK(transport.sent.size() == sent_before_repeated);
    CHECK(transport.send_attempt_count == attempts_before_repeated);
    CHECK_FALSE(stage.poll_weapon_command_submission());
    return;
  }

  const auto jump = hlclient::gameplay_input::gameplay_button_mask(
      hlclient::gameplay_input::GameplayButton::jump);
  ++input.input_revision;
  input.held_buttons = input.pressed_buttons = jump;
  REQUIRE(stage.submit_live_visual_input(input, now));
  ++input.input_revision;
  input.held_buttons = input.pressed_buttons = 0U;
  input.released_buttons = jump;
  if (focus_loss) input.focused = input.captured = false;
  REQUIRE(stage.submit_live_visual_input(input, now));
  for (const auto frame_gap : {20ms, 60ms, 20ms}) {
    transport.queue_send_status(network::DatagramSendStatus::would_block);
    now += frame_gap;
    stage.update(now);
    CAPTURE(stage.error() ? stage.error()->context : std::string{});
    REQUIRE_FALSE(stage.terminal());
    const auto blocked = stage.live_usercmd_snapshot();
    REQUIRE(blocked);
    CHECK(blocked->generated_command_count == originally_generated);
    CHECK(blocked->scheduler_last_due_command_count == 0U);
    CHECK(blocked->jump_new_submission_count == 0U);
    CHECK(blocked->new_command_submission_count == 0U);
  }
  if (stale_press) {
    transport.queue_send_status(network::DatagramSendStatus::would_block);
    now += 300ms;
    stage.update(now);
    REQUIRE_FALSE(stage.terminal());
    CHECK(stage.live_usercmd_snapshot()->generated_command_count == originally_generated);
  }
  // Drain all originally prepared/remaining packets. Each update still owns
  // exactly one driver pump; no command is appended to an owned transaction.
  for (unsigned frame = 0U; frame < 10U &&
      stage.live_usercmd_snapshot()->new_command_submission_count < originally_generated; ++frame) {
    now += 20ms;
    stage.update(now);
    REQUIRE_FALSE(stage.terminal());
    CHECK(stage.live_usercmd_snapshot()->generated_command_count == originally_generated);
  }
  REQUIRE(stage.live_usercmd_snapshot()->new_command_submission_count == originally_generated);
  now += 20ms;
  stage.update(now);
  REQUIRE_FALSE(stage.terminal());
  const auto resumed = stage.live_usercmd_snapshot();
  REQUIRE(resumed);
  CHECK(resumed->generated_command_count > originally_generated);
  const auto expected_jumps = !stale_press && !focus_loss ? 1U : 0U;
  CHECK(resumed->jump_new_submission_count == expected_jumps);
  if (stale_press) {
    CHECK(resumed->scheduler_stall_recoveries == 1U);
    CHECK(resumed->last_sampled_forward_axis == 0.0F);
  }
  std::uint32_t next_sequence = 1U;
  std::size_t notification_count = 0U;
  while (const auto notification = stage.poll_weapon_command_submission()) {
    CHECK(notification->command_sequence == next_sequence++);
    CHECK(notification->generation == 1U);
    ++notification_count;
  }
  CHECK(notification_count == resumed->new_command_submission_count);
  std::uint32_t next_range = 1U;
  for (const auto& range : resumed->transmit_ranges) {
    CHECK(range.first_new_command_sequence == next_range);
    CHECK(range.last_new_command_sequence - range.first_new_command_sequence + 1U == range.new_command_count);
    next_range = range.last_new_command_sequence + 1U;
  }
  std::size_t wire_new_commands = 0U;
  std::size_t wire_jumps = 0U;
  for (std::size_t packet_index = sent_at_activation; packet_index < transport.sent.size(); ++packet_index) {
    const auto sent_packet = decode_sent(transport.sent[packet_index]);
    goldsrc::ReferenceMoveSource source;
    source.generation = 1U;
    source.sequence = sent_packet.header.sequence.sequence.value();
    const auto decoded = goldsrc::decode_reference_client_payload(codec, sent_packet.payload, source);
    REQUIRE_FALSE(decoded.error);
    for (const auto& move : decoded.moves) {
      REQUIRE(move.checksum_matched);
      REQUIRE(move.commands.size() >= move.new_count);
      for (std::size_t command_index = move.commands.size() - move.new_count;
           command_index < move.commands.size(); ++command_index) {
        const auto& command = move.commands[command_index].value;
        CHECK(command.msec == 20U);
        wire_jumps += (command.buttons & goldsrc::kReferenceGoldSrcButtonJump) != 0U;
        ++wire_new_commands;
      }
    }
  }
  CHECK(wire_new_commands == resumed->new_command_submission_count);
  CHECK(wire_jumps == expected_jumps);
  stage.update(now);
  CHECK_FALSE(stage.poll_weapon_command_submission());
  CHECK(stage.live_usercmd_snapshot()->new_command_submission_count == resumed->new_command_submission_count);
}

TEST_CASE("E321 newest correction near a finite outside corner preserves the same-time camera",
          "[e321][convex-corner][goldsrc][live-runtime][reference-positive][presentation]") {
  unsigned render_hz=0;
  bool repeated_rebase=false;
  SECTION("complete fade at every millisecond") {}
  SECTION("fresh exact correction cannot restart or reject the active corner fade") {repeated_rebase=true;}
  SECTION("30 Hz render samples the same retained commands") {render_hz=30;}
  SECTION("60 Hz render samples the same retained commands") {render_hz=60;}
  SECTION("100 Hz render samples the same retained commands") {render_hz=100;}
  SECTION("144 Hz render samples the same retained commands") {render_hz=144;}
  SECTION("240 Hz render samples the same retained commands") {render_hz=240;}
  SECTION("irregular render samples the same retained commands") {render_hz=1;}
  const auto corner_record=[](float server_time,unsigned x_eighths,unsigned y_eighths) {
    delta_fixture::BitWriter writer;writer.write(7,8);writer.write(std::bit_cast<std::uint32_t>(server_time),32);
    writer.write(goldsrc::kGoldSrcSvcClientDataOpcode,8);writer.write(0,1);writer.write(2,3);writer.write(0x1fff,16);
    writer.write(goldsrc_signed_positive(100),10);writer.write(0,16);
    writer.write(goldsrc_signed_positive(800)|1U,16);writer.write(0,16);
    writer.write(goldsrc_signed_positive(112),10);
    writer.write(goldsrc_signed_positive(x_eighths),16);writer.write(goldsrc_signed_positive(y_eighths),16);
    writer.write(goldsrc_signed_positive(288),16);writer.write(1U<<9U,32);writer.write(3200,16);
    writer.write(0,1);writer.write(0,2);writer.write(0,3);writer.write(0,1);writer.align_zero();
    writer.write(goldsrc::kGoldSrcSvcPacketEntitiesOpcode,8);writer.write(1,16);
    writer.write(0,1);writer.write(0,1);writer.write(1,6);writer.write(0,1);writer.write(0,1);
    writer.write(2,3);writer.write(0x7f80,16);writer.write(3,4);writer.write(goldsrc_signed_positive(8),16);
    writer.write(0,1);writer.write(goldsrc_signed_positive(32),16);writer.write(0,16);writer.write(0,16);writer.write(0,16);writer.write(0,1);
    writer.write(0,16);writer.align_zero();return writer.bytes();
  };
  FakeTransport transport;const auto remote=network::NetworkAddress::loopback(27858);
  const auto epoch=goldsrc::LiveRuntimeStageTimePoint{}+100s;hlclient::client::ClientWorldState world;
  auto config=live_test_config();config.operation_mode=goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source=goldsrc::LiveVisualControlInputSource::keyboard_mouse;config.reference_prediction=true;
  config.movement_diagnostics=true;
  goldsrc::LiveRuntimeStage stage{transport,remote,world,config};
  const auto driven=drive_live_initial(stage,transport,remote,epoch,false,false,true,true);
  auto sequence=driven.last_server_sequence+1;
  transport.queue(remote,server_packet(sequence,false,driven.transition_request.header.sequence.sequence.value(),false,
      service_envelope(resource_semantic_payload())));
  transport.queue(remote,server_packet(++sequence,false,driven.transition_request.header.sequence.sequence.value(),false,{std::byte{1}}));
  for(unsigned step=1;step<=8;++step)stage.update(driven.now+std::chrono::milliseconds{step});
  constexpr std::array empty_response{std::byte{5},std::byte{0},std::byte{0}};
  const auto response=require_latest_payload(transport,empty_response);const auto first=driven.now+9ms;
  transport.queue(remote,server_packet(++sequence,false,response.header.sequence.sequence.value(),response.header.sequence.flags.reliable));stage.update(first);
  const auto spawn=require_latest_payload(transport,goldsrc::StockSpawnRequestBuilder::build(0x12345678,
      goldsrc::decode_stock_server_world_map_crc(0xdeadbeef,0)).encoding->semantic_bytes());
  transport.queue(remote,server_packet(++sequence,true,spawn.header.sequence.sequence.value(),false,
      live_baseline_and_runtime_payload(100,100,80,true)));stage.update(first+1ms);stage.update(first+2ms);
  const auto sendents=require_latest_payload(transport,goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
  transport.queue(remote,server_packet(++sequence,false,sendents.header.sequence.sequence.value(),sendents.header.sequence.flags.reliable,
      service_envelope(corner_record(101,896,904))));stage.update(first+3ms);stage.update(first+2s+2ms);
  REQUIRE(stage.live_visual_input_ready());const auto package=hlclient::tests::convex_corner::package(false,128);REQUIRE(package);
  REQUIRE(stage.attach_reference_prediction_collision(package));
  const auto activated=first+2s+3ms;
  goldsrc::LiveVisualControlInput input{1,1,0,0,270,0,true,true};
  REQUIRE(stage.submit_live_visual_input(input,activated));REQUIRE(stage.activate_live_visual_control(activated));
  stage.update(activated+20ms);const auto first_carrier=decode_sent(transport.sent.back()).header.sequence.sequence.value();
  transport.queue(remote,server_packet(++sequence,false,first_carrier,false,service_envelope(corner_record(102,896,904))));
  stage.update(activated+40ms);const auto latest_carrier=decode_sent(transport.sent.back()).header.sequence.sequence.value();
  const auto render_at=activated+50ms;const auto before=stage.live_reference_prediction_snapshot(render_at);
  INFO(before.reason);REQUIRE(before.state==goldsrc::LiveReferencePredictionState::active);
  REQUIRE(before.presented_origin);REQUIRE(before.predicted_origin);
  const auto y_eighths=static_cast<unsigned>(std::lround(before.predicted_origin->y*8));
  REQUIRE(y_eighths<896); // The actual command has gone past the exterior vertex.
  transport.queue(remote,server_packet(++sequence,false,latest_carrier,false,service_envelope(corner_record(102.02F,897,y_eighths))));
  stage.update(render_at);const auto after=stage.live_reference_prediction_snapshot(render_at);
  INFO(after.reason);INFO(after.presentation_reason);REQUIRE(after.state==goldsrc::LiveReferencePredictionState::active);
  REQUIRE(after.last_raw_position_error);CHECK(*after.last_raw_position_error<=.25);
  REQUIRE(after.presented_origin);REQUIRE(after.canonical_origin);
  const auto jump=std::hypot(static_cast<double>(after.presented_origin->x-before.presented_origin->x),
      static_cast<double>(after.presented_origin->y-before.presented_origin->y),
      static_cast<double>(after.presented_origin->z-before.presented_origin->z));
  CAPTURE(jump,*after.last_raw_position_error,before.presentation_reason,after.presentation_reason);
  CHECK(jump<=.001); // Same render timestamp; unrelated camera time cannot hide the rebase jump.
  CHECK(after.presentation_reason=="visual_correction_exterior_corner");
  REQUIRE(after.collision_trace_evidence);CHECK(after.collision_trace_evidence->fraction<1);
  CHECK(after.collision_query_purpose=="present_linear_body");
  REQUIRE(after.collision_query_start);REQUIRE(after.collision_query_end);REQUIRE(after.collision_query_hull);
  hlclient::goldsrc::movement::WorldOnlyMovementCollision collision{package};hlclient::collision::CollisionQueryScratch scratch;
  for(const auto& position:{*before.presented_origin,*after.presented_origin,*after.canonical_origin}) {
    const auto clear=collision.test_position(position,hlclient::movement::PlayerMovementHull::standing,scratch);REQUIRE(clear);
    CHECK(clear.result->status==hlclient::goldsrc::movement::LocalMovementPositionStatus::free);
  }
  const auto repeated=stage.live_reference_prediction_snapshot(render_at);REQUIRE(repeated.presented_origin);
  CHECK(repeated.presented_origin->x==after.presented_origin->x);CHECK(repeated.presented_origin->y==after.presented_origin->y);
  CHECK(repeated.local_steps==after.local_steps);
  CHECK(repeated.presentation_trace_queries_total==after.presentation_trace_queries_total);
  auto expected=after;
  double repeated_rebase_jump=0;
  if(repeated_rebase) {
    const auto extra_at=render_at+1ms;
    const auto old=stage.live_reference_prediction_snapshot(extra_at);REQUIRE(old.presented_origin);
    transport.queue(remote,server_packet(++sequence,false,latest_carrier,false,service_envelope(corner_record(102.025F,897,y_eighths))));
    stage.update(extra_at);expected=stage.live_reference_prediction_snapshot(extra_at);REQUIRE(expected.presented_origin);
    REQUIRE(expected.state==goldsrc::LiveReferencePredictionState::active);
    CHECK(expected.prediction_epoch>after.prediction_epoch);CHECK(expected.accepted_corrections==after.accepted_corrections+1);
    CHECK(expected.presentation_reason=="visual_correction_exterior_corner");
    repeated_rebase_jump=std::hypot(static_cast<double>(expected.presented_origin->x-old.presented_origin->x),
        static_cast<double>(expected.presented_origin->y-old.presented_origin->y),static_cast<double>(expected.presented_origin->z-old.presented_origin->z));
    CHECK(repeated_rebase_jump<=.001);
  }
  std::vector<unsigned> sample_ms;
  if(render_hz==0)for(unsigned ms=1;ms<=25;++ms)sample_ms.push_back(ms);
  else if(render_hz==1)sample_ms={1,3,4,9,14,19,20,25};
  else for(unsigned frame=1;;++frame) {
    const auto ms=static_cast<unsigned>(std::ceil(1000.0*frame/render_hz));
    if(ms>=25)break;sample_ms.push_back(ms);
  }
  if(sample_ms.empty()||sample_ms.back()!=25)sample_ms.push_back(25);
  auto last_trace_queries=expected.presentation_trace_queries_total;
  for(const auto ms:sample_ms) {
    const auto later=stage.live_reference_prediction_snapshot(render_at+std::chrono::milliseconds{ms});
    CAPTURE(render_hz,ms,later.presentation_reason);REQUIRE(later.presented_origin);REQUIRE(later.predicted_origin);
    CHECK(later.local_steps==expected.local_steps);CHECK(later.accepted_corrections==expected.accepted_corrections);
    CHECK(later.replayed_commands==expected.replayed_commands);CHECK(later.last_raw_position_error==expected.last_raw_position_error);
    CHECK(later.predicted_origin->x==expected.predicted_origin->x);CHECK(later.predicted_origin->y==expected.predicted_origin->y);
    const auto clear=collision.test_position(*later.presented_origin,hlclient::movement::PlayerMovementHull::standing,scratch);REQUIRE(clear);
    CHECK(clear.result->status==hlclient::goldsrc::movement::LocalMovementPositionStatus::free);
    const auto same=stage.live_reference_prediction_snapshot(render_at+std::chrono::milliseconds{ms});REQUIRE(same.presented_origin);
    CHECK(same.presented_origin->x==later.presented_origin->x);CHECK(same.presented_origin->y==later.presented_origin->y);
    CHECK(same.presentation_trace_queries_total==later.presentation_trace_queries_total);
    last_trace_queries=later.presentation_trace_queries_total;
    if(ms==25) {
      CHECK(later.presented_origin->x==later.predicted_origin->x);CHECK(later.presented_origin->y==later.predicted_origin->y);
    }
  }
  std::cout<<"[e321-corner-camera] render_hz="<<render_hz<<" repeated_rebase="<<repeated_rebase
      <<" raw_error="<<*after.last_raw_position_error<<" same_time_jump="<<jump
      <<" repeat_rebase_jump="<<repeated_rebase_jump<<" presentation="<<after.presentation_reason
      <<" source_record="<<after.last_record_identity.value_or(0)<<" carrier_boundary="<<after.anchor_command.value_or(0)
      <<" epoch="<<after.prediction_epoch<<" final_epoch="<<expected.prediction_epoch
      <<" first_query="<<after.collision_query_purpose<<" blocked_fraction="<<after.collision_trace_evidence->fraction
      <<" samples="<<sample_ms.size()<<" cached_repeats="<<sample_ms.size()+1
      <<" trace_queries="<<last_trace_queries<<" unchanged_steps="<<expected.local_steps<<'\n';
}

TEST_CASE("E32 production prediction retains exact wall deadlines and ignores obsolete carrier boundaries",
          "[e32][goldsrc][live-runtime][integration][reference-positive][prediction-timeline]") {
  bool recovered_gap = false;
  bool diagnostic_only = false;
  SECTION("unsampled wall slots do not rewrite retained command deadlines") { recovered_gap = true; }
  SECTION("a fresh packet with an older carrier cannot rewind the accepted command anchor") {}
  SECTION("opt-in clocks date actual socket read parse commit and prediction") { diagnostic_only = true; }
  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27854U);
  const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 90s;
  hlclient::client::ClientWorldState world;
  auto config = live_test_config();
  config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
  config.live_visual_input_source = goldsrc::LiveVisualControlInputSource::keyboard_mouse;
  config.reference_prediction = true;
  config.movement_diagnostics = diagnostic_only;
  goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
  const auto driven = drive_live_initial(stage, transport, remote, epoch, false, false, true, true);
  const auto resource = driven.last_server_sequence + 1U;
  transport.queue(remote, server_packet(resource, false,
      driven.transition_request.header.sequence.sequence.value(), false,
      service_envelope(resource_semantic_payload())));
  transport.queue(remote, server_packet(resource + 1U, false,
      driven.transition_request.header.sequence.sequence.value(), false, {std::byte{1}}));
  for (std::size_t step = 1U; step <= 8U; ++step)
    stage.update(driven.now + std::chrono::milliseconds{step});
  constexpr std::array empty_response{std::byte{5}, std::byte{0}, std::byte{0}};
  const auto response = require_latest_payload(transport, empty_response);
  const auto first = driven.now + 9ms;
  transport.queue(remote, server_packet(resource + 2U, false,
      response.header.sequence.sequence.value(), response.header.sequence.flags.reliable));
  stage.update(first);
  const auto spawn = require_latest_payload(transport,
      goldsrc::StockSpawnRequestBuilder::build(0x12345678U,
          goldsrc::decode_stock_server_world_map_crc(0xdeadbeefU, 0U)).encoding->semantic_bytes());
  transport.queue(remote, server_packet(resource + 3U, true,
      spawn.header.sequence.sequence.value(), false,
      live_baseline_and_runtime_payload(100.F, 100U, 80U, true)));
  stage.update(first + 1ms);
  stage.update(first + 2ms);
  const auto sendents = require_latest_payload(transport,
      goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
  transport.queue(remote, server_packet(resource + 4U, false,
      sendents.header.sequence.sequence.value(), sendents.header.sequence.flags.reliable,
      service_envelope(live_prediction_air_record(101.F, 10U))));
  stage.update(first + 3ms);
  stage.update(first + 2s + 2ms);
  REQUIRE(stage.live_visual_input_ready());
  REQUIRE(stage.attach_reference_prediction_collision(hlclient::tests::collision_brush_fixture::package(false)));
  const auto activated = first + 2s + 3ms;
  auto now = activated;
  goldsrc::LiveVisualControlInput input{1U, 1U, 0, 0, 0, 0, true, true};
  REQUIRE(stage.submit_live_visual_input(input, now));
  REQUIRE(stage.activate_live_visual_control(now));
  now += 20ms;
  stage.update(now);
  const auto first_carrier = decode_sent(transport.sent.back()).header.sequence.sequence.value();
  std::uint32_t server_sequence = resource + 4U;
  transport.queue(remote, server_packet(++server_sequence, false, first_carrier, false,
      service_envelope(live_prediction_air_record(102.F, 10U))));
  now += 20ms;
  stage.update(now);
  REQUIRE_FALSE(stage.terminal());
  REQUIRE(stage.live_reference_prediction_snapshot(now).state == goldsrc::LiveReferencePredictionState::active);
  const auto seeded_metrics = stage.movement_diagnostics_snapshot();
  now += 20ms;
  stage.update(now);
  const auto before = stage.live_reference_prediction_snapshot(now);
  REQUIRE(before.state == goldsrc::LiveReferencePredictionState::active);
  REQUIRE(before.predicted_origin);
  const auto accounting = stage.live_usercmd_snapshot();
  REQUIRE(accounting);
  REQUIRE(accounting->generated_command_count == 3U);

  if (diagnostic_only) {
    REQUIRE(seeded_metrics.enabled);
    REQUIRE(seeded_metrics.socket_read_begin_ns);
    REQUIRE(seeded_metrics.socket_read_end_ns);
    REQUIRE(seeded_metrics.parse_begin_ns);
    REQUIRE(seeded_metrics.atomic_commit_ns);
    REQUIRE(seeded_metrics.prediction_use_ns);
    CHECK(*seeded_metrics.socket_read_begin_ns <= *seeded_metrics.socket_read_end_ns);
    CHECK(*seeded_metrics.socket_read_end_ns <= *seeded_metrics.parse_begin_ns);
    CHECK(*seeded_metrics.parse_begin_ns <= *seeded_metrics.atomic_commit_ns);
    CHECK(*seeded_metrics.atomic_commit_ns <= *seeded_metrics.prediction_use_ns);
    CHECK(seeded_metrics.latest_state_commit_ns == seeded_metrics.atomic_commit_ns);
    CHECK(seeded_metrics.committed_records == 1U);
    CHECK(seeded_metrics.committed_bytes > 0U);
    CHECK(seeded_metrics.source_transport_sequence == server_sequence);
    CHECK(seeded_metrics.entity_count == 1U);
    CHECK(seeded_metrics.observed_player_count == 1U);
    CHECK(seeded_metrics.driver_queue_after == 0U);
    CHECK(seeded_metrics.processed_driver_events > 0U);
    for (const auto measured : {seeded_metrics.network_driver_ms,
         seeded_metrics.network_parse_commit_ms, seeded_metrics.collision_scene_ms,
         seeded_metrics.prediction_command_ms, seeded_metrics.prediction_replay_ms}) {
      CHECK(std::isfinite(measured));
      CHECK(measured >= 0.0);
    }
    const auto no_new_record = stage.movement_diagnostics_snapshot();
    CHECK_FALSE(no_new_record.atomic_commit_ns);
    CHECK_FALSE(no_new_record.socket_read_end_ns);
    CHECK(no_new_record.latest_state_commit_ns == seeded_metrics.atomic_commit_ns);
    CHECK(no_new_record.committed_records == 0U);
    CHECK(no_new_record.committed_bytes == 0U);
    CHECK(no_new_record.replayed_commands == 0U);
    CHECK(no_new_record.replayed_simulation_ms == 0.0);
    CHECK_FALSE(no_new_record.parse_begin_ns);
    CHECK(no_new_record.network_parse_commit_ms == 0.0);
    CHECK(no_new_record.prediction_replay_ms == 0.0);
    for (const auto measured : {no_new_record.network_driver_ms,
         no_new_record.collision_scene_ms, no_new_record.prediction_command_ms}) {
      CHECK(std::isfinite(measured));
      CHECK(measured >= 0.0);
    }
    REQUIRE(before.command_simulation_seconds);
    REQUIRE(before.render_sample_time_ns);
    CHECK(*before.render_sample_time_ns == std::chrono::duration_cast<std::chrono::nanoseconds>(
        (now - 20ms).time_since_epoch()).count());
    REQUIRE(before.collision_query_hull);
    CHECK(*before.collision_query_hull == hlclient::movement::PlayerMovementHull::standing);
    REQUIRE(before.collision_position_evidence);
    CHECK(before.collision_position_evidence->status == goldsrc::movement::LocalMovementPositionStatus::free);
    REQUIRE(before.collision_trace_evidence);
    CHECK(before.collision_trace_evidence->fraction == 1.0);
    CHECK_FALSE(before.collision_trace_evidence->hit);
    return;
  }
  CHECK_FALSE(seeded_metrics.enabled);
  CHECK_FALSE(seeded_metrics.socket_read_end_ns);
  CHECK_FALSE(seeded_metrics.latest_state_commit_ns);

  if (recovered_gap) {
    // The scheduler recovers at240ms with command4, skipping eight wall
    // slots. Command3 still ended at60ms: a new scheduler deadline cannot
    // retroactively translate an already retained command to220ms.
    while (stage.poll_weapon_command_submission()) {}
    now += 180ms;
    ++input.input_revision;
    REQUIRE(stage.submit_live_visual_input(input, now));
    stage.update(now);
    REQUIRE_FALSE(stage.terminal());
    const auto recovered = stage.live_usercmd_snapshot();
    REQUIRE(recovered);
    CHECK(recovered->scheduler_stall_recoveries == 1U);
    CHECK(recovered->scheduler_discarded_wall_time_samples == 8U);
    CHECK(recovered->generated_command_count == 4U);
    const auto exact_submission = stage.poll_weapon_command_submission();
    REQUIRE(exact_submission);
    CHECK(exact_submission->command_sequence == 4U);
    const auto exact_time = [](const auto at) {
      return std::chrono::duration_cast<std::chrono::nanoseconds>(at.time_since_epoch()).count();
    };
    CHECK(exact_submission->command_end_nanoseconds == exact_time(activated + 240ms));
    const auto gap = stage.live_reference_prediction_snapshot(now + 10ms);
    REQUIRE(gap.state == goldsrc::LiveReferencePredictionState::active);
    CHECK(gap.fallback_count == before.fallback_count);
    CHECK(gap.presentation_reason == "command_wall_time_gap");
    CHECK(gap.presentation_from_command == 3U);
    CHECK(gap.presentation_to_command == 4U);
    CHECK(gap.presentation_from_time_ns == exact_time(activated + 60ms));
    CHECK(gap.presentation_to_time_ns == exact_time(activated + 240ms));
    CHECK_FALSE(gap.presentation_alpha);
    REQUIRE(gap.presented_origin);
    REQUIRE(gap.predicted_origin);
    CHECK(gap.presented_origin->z == gap.predicted_origin->z);
    // The following ordinary pair again has a genuine20ms interval. Repeated
    // camera sampling cannot alter raw physics, wire count or canonical state.
    now += 20ms;
    stage.update(now);
    const auto regular = stage.live_reference_prediction_snapshot(now + 10ms);
    REQUIRE(regular.presentation_alpha);
    CHECK(*regular.presentation_alpha == Catch::Approx(.5).margin(1e-9));
    CHECK(regular.presentation_from_time_ns == exact_time(activated + 240ms));
    CHECK(regular.presentation_to_time_ns == exact_time(activated + 260ms));
    const auto revision = world.runtime_publication_revision();
    const auto sent_count = transport.sent.size();
    for (const auto offset : {1ms, 3ms, 7ms, 10ms, 13ms, 17ms}) {
      const auto sampled = stage.live_reference_prediction_snapshot(now + offset);
      REQUIRE(sampled.predicted_origin);
      CHECK(sampled.predicted_origin->z == regular.predicted_origin->z);
      CHECK(sampled.local_steps == regular.local_steps);
    }
    CHECK(world.runtime_publication_revision() == revision);
    CHECK(transport.sent.size() == sent_count);
    return;
  }

  const auto newest_carrier = decode_sent(transport.sent.back()).header.sequence.sequence.value();
  const auto corrected_z = static_cast<std::uint32_t>(std::lround(before.predicted_origin->z * 8.F));
  transport.queue(remote, server_packet(++server_sequence, false, newest_carrier, false,
      service_envelope(live_prediction_air_record(102.06F, 10U, 0U, corrected_z))));
  stage.update(now);
  const auto accepted = stage.live_reference_prediction_snapshot(now);
  REQUIRE(accepted.state == goldsrc::LiveReferencePredictionState::active);
  REQUIRE(accepted.anchor_command == 3U);
  REQUIRE(accepted.last_raw_position_error);
  CHECK(*accepted.last_raw_position_error < .063);
  const auto committed_before_stale = world.runtime_publication_revision();
  // A higher incoming packet sequence with an olderACK is valid transport
  // input. Its canonical state commits, but command1 is older than accepted3
  // and cannot establish another prediction correction.
  transport.queue(remote, server_packet(++server_sequence, false, first_carrier, false,
      service_envelope(live_prediction_air_record(102.065F, 20U))));
  now += 5ms;
  stage.update(now);
  REQUIRE_FALSE(stage.terminal());
  CHECK(world.runtime_publication_revision() > committed_before_stale);
  REQUIRE(world.runtime_observation());
  CHECK(world.runtime_observation()->receiving_client->origin.x == 20.0);
  const auto stale = stage.live_reference_prediction_snapshot(now);
  CHECK(stale.state == goldsrc::LiveReferencePredictionState::active);
  CHECK(stale.anchor_command == accepted.anchor_command);
  CHECK(stale.accepted_corrections == accepted.accepted_corrections);
  CHECK(stale.fallback_count == accepted.fallback_count);
  CHECK(stage.live_usercmd_snapshot()->reference_prediction_last_anchor_status !=
        goldsrc::ReferencePredictionAnchorStatus::bound);
  CHECK(stage.live_usercmd_snapshot()->reference_prediction_last_anchor_status ==
        goldsrc::ReferencePredictionAnchorStatus::stale_command_boundary);
  // Equal command boundary with a genuinely newer record is still a valid
  // repeated correction, rather than a global packet/record suppression.
  transport.queue(remote, server_packet(++server_sequence, false, newest_carrier, false,
      service_envelope(live_prediction_air_record(102.07F, 10U, 0U, corrected_z))));
  now += 5ms;
  stage.update(now);
  const auto repeated = stage.live_reference_prediction_snapshot(now);
  REQUIRE(repeated.state == goldsrc::LiveReferencePredictionState::active);
  CHECK(repeated.anchor_command == 3U);
  CHECK(repeated.accepted_corrections == accepted.accepted_corrections + 1U);
  CHECK(repeated.fallback_count == accepted.fallback_count);
}

TEST_CASE("E32 encoded actor load drains ordered backlog without changing immutable commands",
          "[e32][goldsrc][live-runtime][integration][load][backlog][prediction-timeline]") {
  const auto load_record = [](const float seconds, const unsigned actors) {
    delta_fixture::BitWriter writer;
    writer.write(7U, 8U);
    writer.write(std::bit_cast<std::uint32_t>(seconds), 32U);
    writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
    writer.write(0U, 1U);
    writer.write(2U, 3U); writer.write(0x1fffU, 16U);
    writer.write(goldsrc_signed_positive(100U), 10U);
    for (unsigned component = 0U; component < 3U; ++component) writer.write(0U, 16U);
    writer.write(goldsrc_signed_positive(112U), 10U);
    writer.write(goldsrc_signed_positive(80U), 16U); // receiving origin X10
    writer.write(0U, 16U); writer.write(goldsrc_signed_positive(512U), 16U);
    writer.write(0U, 32U); writer.write(3200U, 16U);
    writer.write(0U, 1U); writer.write(0U, 2U); writer.write(0U, 3U);
    writer.write(0U, 1U); writer.align_zero();
    writer.write(goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 8U);
    writer.write(actors + 128U, 16U);
    for (unsigned entity = 1U; entity <= actors; ++entity) {
      if (entity == 1U) {
        writer.write(0U, 1U); writer.write(0U, 1U); writer.write(1U, 6U);
      } else writer.write(1U, 1U);
      writer.write(0U, 1U); writer.write(0U, 1U);
      writer.write(2U, 3U); writer.write(0x7fffU, 16U);
      writer.write(goldsrc_signed_positive((entity == 1U ? 10U : 512U + entity * 16U) * 8U), 16U);
      writer.write(goldsrc_signed_positive(entity == 1U ? 0U : 2048U), 16U);
      writer.write(goldsrc_signed_positive(512U), 16U);
      for (unsigned component = 0U; component < 3U; ++component) writer.write(0U, 16U);
      writer.write(0U, 16U); writer.write(3U, 4U); // no render asset in this transport fixture
      writer.write(goldsrc_signed_positive(8U), 16U); writer.write(0U, 1U);
      writer.write(goldsrc_signed_positive(32U), 16U);
      for (unsigned component = 0U; component < 3U; ++component) writer.write(0U, 16U);
      writer.write(0U, 1U);
    }
    for (unsigned entity = 64U; entity < 192U; ++entity) {
      if (entity == 64U) {
        writer.write(0U, 1U); writer.write(0U, 1U); writer.write(entity - actors, 6U);
      } else writer.write(1U, 1U);
      writer.write(0U, 1U); writer.write(0U, 1U);
      writer.write(2U, 3U); writer.write(0x01ffU, 16U);
      writer.write(goldsrc_signed_positive((2048U + (entity - 64U) * 4U) * 8U), 16U);
      writer.write(goldsrc_signed_positive(4096U), 16U);
      writer.write(goldsrc_signed_positive(512U), 16U);
      for (unsigned component = 0U; component < 3U; ++component) writer.write(0U, 16U);
      writer.write(0U, 16U); writer.write(0U, 3U); writer.write(0U, 4U);
    }
    writer.write(0U, 16U); writer.align_zero();
    return writer.bytes();
  };
  for (const unsigned actors : {2U, 8U, 16U, 32U}) {
    CAPTURE(actors);
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27855U);
    const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 100s;
    hlclient::client::ClientWorldState world;
    auto config = live_test_config();
    config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
    config.live_visual_input_source = goldsrc::LiveVisualControlInputSource::keyboard_mouse;
    config.reference_prediction = true;
    config.movement_diagnostics = true;
    // The shared test_config deliberately permits16 reads/update. This
    // backlog control selects an explicit smaller test budget; production
    // driver limits and policy are unchanged.
    constexpr std::size_t fixture_receive_budget = 4U;
    config.resource_response.resource_list.transition.user_info.movement_environment.delta
        .pre_resource.initial_signon.driver.maximum_datagrams_per_update = fixture_receive_budget;
    goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
    REQUIRE(stage.start(epoch, transport.local)); stage.update(epoch + 1ms);
    const auto initial_request = decode_sent(transport.sent.front());
    auto initial_payload = live_initial_semantic_payload(true, true);
    // Independent literal framing: print(3), serverinfo opcode(1), three
    // little-endian u32 fields(12),16 opaque fixture bytes, then maxclients.
    REQUIRE(initial_payload.at(3U) == std::byte{11U});
    REQUIRE(initial_payload.at(32U) == std::byte{8U});
    initial_payload[32U] = std::byte{32U};
    transport.queue(remote, server_packet(1U, true,
        initial_request.header.sequence.sequence.value(), initial_request.header.sequence.flags.reliable,
        std::move(initial_payload)));
    stage.update(epoch + 2ms); stage.update(epoch + 3ms);
    constexpr std::array sendres{std::byte{3U}, std::byte{'s'}, std::byte{'e'}, std::byte{'n'},
        std::byte{'d'}, std::byte{'r'}, std::byte{'e'}, std::byte{'s'}, std::byte{0U}};
    const auto transition = require_latest_payload(transport, sendres);
    constexpr std::uint32_t resource = 2U;
    transport.queue(remote, server_packet(resource, false, transition.header.sequence.sequence.value(), false,
        service_envelope(resource_semantic_payload())));
    transport.queue(remote, server_packet(resource + 1U, false, transition.header.sequence.sequence.value(), false,
        {std::byte{1U}}));
    for (unsigned step = 1U; step <= 8U; ++step) stage.update(epoch + 3ms + std::chrono::milliseconds{step});
    constexpr std::array empty_response{std::byte{5U}, std::byte{0U}, std::byte{0U}};
    const auto response = require_latest_payload(transport, empty_response);
    const auto first = epoch + 12ms;
    transport.queue(remote, server_packet(resource + 2U, false, response.header.sequence.sequence.value(),
        response.header.sequence.flags.reliable)); stage.update(first);
    const auto spawn = require_latest_payload(transport, goldsrc::StockSpawnRequestBuilder::build(0x12345678U,
        goldsrc::decode_stock_server_world_map_crc(0xdeadbeefU, 0U)).encoding->semantic_bytes());
    transport.queue(remote, server_packet(resource + 3U, true, spawn.header.sequence.sequence.value(), false,
        live_baseline_and_runtime_payload(100.F, 100U, 80U, true)));
    stage.update(first + 1ms); stage.update(first + 2ms);
    const auto sendents = require_latest_payload(transport, goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
    transport.queue(remote, server_packet(resource + 4U, false, sendents.header.sequence.sequence.value(),
        sendents.header.sequence.flags.reliable, load_record(101.F, actors)));
    stage.update(first + 3ms); stage.update(first + 2s + 2ms);
    REQUIRE(stage.live_visual_input_ready()); REQUIRE(stage.live_server_info());
    CHECK(stage.live_server_info()->maximum_clients().value() == 32U);
    REQUIRE(stage.attach_reference_prediction_collision(hlclient::tests::collision_brush_fixture::package(false)));
    auto now = first + 2s + 3ms;
    goldsrc::LiveVisualControlInput input{1U, 1U, 0, 0, 0, 0, true, true};
    REQUIRE(stage.submit_live_visual_input(input, now)); REQUIRE(stage.activate_live_visual_control(now));
    now += 20ms; stage.update(now);
    const auto first_carrier = decode_sent(transport.sent.back()).header.sequence.sequence.value();
    std::uint32_t server_sequence = resource + 4U;
    const auto large_body = load_record(102.F, actors);
    REQUIRE(large_body.size() > 2048U); REQUIRE(large_body.size() < 4000U);
    transport.queue(remote, server_packet(++server_sequence, false, first_carrier, false, large_body));
    now += 20ms; stage.update(now);
    REQUIRE(stage.live_reference_prediction_snapshot(now).state == goldsrc::LiveReferencePredictionState::active);
    ++input.input_revision;
    input.held_buttons = input.pressed_buttons = hlclient::gameplay_input::gameplay_button_mask(
        hlclient::gameplay_input::GameplayButton::attack_primary);
    REQUIRE(stage.submit_live_visual_input(input, now)); now += 20ms; stage.update(now);
    const auto before = stage.live_usercmd_snapshot(); REQUIRE(before);
    REQUIRE(before->generated_command_count == 3U); CHECK(before->attack_new_submission_count == 1U);
    const auto base_sequence = server_sequence;
    for (unsigned packet = 1U; packet <= 12U; ++packet) {
      if (packet == 4U) continue; // one intentionally absent sequence, no fabricated body
      const float time = 102.F + static_cast<float>(packet) * .01F;
      transport.queue(remote, server_packet(base_sequence + packet, false, first_carrier, false, load_record(time, actors)));
      if (packet == 2U)
        transport.queue(remote, server_packet(base_sequence + packet, false, first_carrier, false, load_record(199.F, actors)));
      if (packet == 6U)
        transport.queue(remote, server_packet(base_sequence + 1U, false, first_carrier, false, load_record(198.F, actors)));
    }
    REQUIRE(transport.incoming.size() == 13U);
    REQUIRE(transport.incoming.size() > fixture_receive_budget);
    ++input.input_revision; input.held_buttons = input.pressed_buttons = 0U;
    input.released_buttons = hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::attack_primary);
    now += 180ms; REQUIRE(stage.submit_live_visual_input(input, now)); stage.update(now);
    REQUIRE_FALSE(stage.terminal());
    CHECK_FALSE(transport.incoming.empty()); // actual bounded RX queue, not a made-up counter
    CHECK(transport.incoming.size() == 13U - fixture_receive_budget);
    CHECK(world.runtime_observation()->client_metadata.source->source_transport_sequence == base_sequence + 3U);
    unsigned drain_updates = 0U;
    while (!transport.incoming.empty() && drain_updates < 16U) {
      const auto depth = transport.incoming.size(); stage.update(now); ++drain_updates;
      REQUIRE_FALSE(stage.terminal()); CHECK(transport.incoming.size() < depth);
    }
    REQUIRE(transport.incoming.empty()); CHECK(drain_updates <= 13U);
    const auto final_commands = stage.live_usercmd_snapshot(); REQUIRE(final_commands);
    CHECK(final_commands->generated_command_count == 4U);
    CHECK(final_commands->new_command_submission_count == 4U);
    CHECK(final_commands->attack_new_submission_count == 1U);
    CHECK(final_commands->attack_command_release_count == 1U);
    CHECK(final_commands->scheduler_stall_recoveries == 1U);
    CHECK(final_commands->scheduler_discarded_wall_time_samples == 8U);
    CHECK(final_commands->runtime_records_committed_total == before->runtime_records_committed_total + 11U);
    CHECK(final_commands->driver_rx_total.accepted_sequences == before->driver_rx_total.accepted_sequences + 11U);
    CHECK(final_commands->driver_rx_total.rejected_sequences == before->driver_rx_total.rejected_sequences + 2U);
    REQUIRE(world.runtime_observation());
    CHECK(world.runtime_observation()->packet_entities.size() == actors + 128U);
    CHECK(world.runtime_observation()->client_metadata.source->source_transport_sequence == base_sequence + 12U);
    CHECK(world.runtime_observation()->server_time_seconds == static_cast<double>(102.12F));
    const auto raw = stage.live_reference_prediction_snapshot(now);
    REQUIRE(raw.state == goldsrc::LiveReferencePredictionState::active);
    REQUIRE(raw.predicted_origin); REQUIRE(raw.predicted_velocity);
    CHECK(raw.predicted_origin->x == Catch::Approx(10.F));
    CHECK(raw.predicted_origin->y == 0.F);
    // Independent constant-gravity oracle: three retained20ms air commands
    // from rest atZ64, acceleration800, trapezoidal gravity half-steps.
    CHECK(raw.predicted_origin->z == Catch::Approx(64.0 - .5 * 800.0 * .06 * .06).margin(.001));
    CHECK(raw.predicted_velocity->z == Catch::Approx(-800.0 * .06).margin(.001));
    CHECK(raw.history_depth == 3U); CHECK(raw.anchor_command == 1U);
    CHECK(raw.fallback_count == 0U); CHECK(raw.last_raw_position_error == 0.0);
    const auto revision = world.runtime_publication_revision();
    const auto sent_count = transport.sent.size();
    for (const int spacing_us : {33333, 16667, 10000, 6944, 4167}) {
      for (int sample = 0; sample <= 20000; sample += spacing_us) {
        const auto camera = stage.live_reference_prediction_snapshot(now + std::chrono::microseconds{sample});
        REQUIRE(camera.predicted_origin); CHECK(camera.predicted_origin->z == raw.predicted_origin->z);
        CHECK(camera.local_steps == raw.local_steps); CHECK(camera.fallback_count == 0U);
      }
    }
    for (const int sample : {317, 4103, 12491, 17933}) {
      const auto camera = stage.live_reference_prediction_snapshot(now + std::chrono::microseconds{sample});
      REQUIRE(camera.predicted_origin); CHECK(camera.predicted_origin->z == raw.predicted_origin->z);
    }
    CHECK(world.runtime_publication_revision() == revision); CHECK(transport.sent.size() == sent_count);
    std::uint32_t command_sequence = 0U; unsigned primary_notifications = 0U;
    while (const auto command = stage.poll_weapon_command_submission()) {
      CHECK(command->command_sequence == ++command_sequence);
      primary_notifications += (command->buttons & goldsrc::kReferenceGoldSrcButtonAttack) != 0U;
    }
    CHECK(command_sequence == 4U); CHECK(primary_notifications == 1U);
    CHECK(stage.movement_diagnostics_snapshot().observed_player_count == actors);
  }
}

TEST_CASE("E32.1 run fire feedback keeps contemporary fragment suffix movement eligible",
          "[e321][e321-a][goldsrc][live-runtime][integration][fragment][suffix][weapon-presentation]") {
  bool suffix_carrier = false;
  bool old_completion = false;
  SECTION("ordinary direct state control") {}
  SECTION("exact contemporary bytes after a reliable fragment prefix") { suffix_carrier = true; }
  SECTION("an old coherent completion cannot date or reserve its contemporary suffix") { suffix_carrier = true; old_completion = true; }
  namespace api = hlclient::game_api;
  const auto fragment_with_suffix = [](const std::uint32_t sequence_number, const std::uint32_t ack,
                                      std::vector<std::byte> suffix, const bool old_prefix) {
    const auto prefix=old_prefix ? live_prediction_air_record(101.F,250U) :
        std::vector<std::byte>{static_cast<std::byte>(goldsrc::RuntimeControlOpcode::svc_nop)};
    std::vector<std::byte> body(prefix.begin(), prefix.end());
    body.insert(body.end(), suffix.begin(), suffix.end());
    goldsrc::NetchanFragmentSlots fragments;
    fragments[0] = goldsrc::NetchanFragmentDescriptor{0U, (1U << 16U) | 1U, 0U,
        static_cast<std::uint16_t>(prefix.size()), 0U};
    const auto encoded = goldsrc::encode_server_to_client_netchan_packet({
        { {::sequence(sequence_number), {true, true}}, {::sequence(ack), false} },
        fragments, std::move(body), prefix.size()});
    REQUIRE(encoded); REQUIRE(encoded.datagram);
    return *encoded.datagram;
  };
  const auto feedback_record = [](const float seconds, const std::uint32_t health, const bool blood, const bool damage) {
    delta_fixture::BitWriter wire;
    wire.write(7U,8U); wire.write(std::bit_cast<std::uint32_t>(seconds),32U);
    wire.write(goldsrc::kGoldSrcSvcClientDataOpcode,8U); wire.write(0U,1U);
    wire.write(2U,3U); wire.write(0x1fffU,16U);
    wire.write(goldsrc_signed_positive(health),10U);
    for(unsigned component=0;component<3;++component)wire.write(0U,16U);
    wire.write(goldsrc_signed_positive(112U),10U);
    wire.write(goldsrc_signed_positive(80U),16U);wire.write(0U,16U);wire.write(goldsrc_signed_positive(512U),16U);
    wire.write(0U,32U);wire.write(3200U,16U);wire.write(0U,1U);wire.write(0U,2U);wire.write(0U,3U);
    wire.write(0U,1U);wire.align_zero();
    wire.write(goldsrc::kGoldSrcSvcPacketEntitiesOpcode,8U);wire.write(2U,16U);
    for(unsigned entity=1;entity<=2;++entity) {
      if(entity==1){wire.write(0U,1U);wire.write(0U,1U);wire.write(1U,6U);}else wire.write(1U,1U);
      wire.write(0U,1U);wire.write(0U,1U);wire.write(2U,3U);wire.write(0x7fffU,16U);
      wire.write(goldsrc_signed_positive((entity==1 ? 10U : 128U)*8U),16U);
      wire.write(0U,16U);wire.write(goldsrc_signed_positive(512U),16U);
      for(unsigned component=0;component<3;++component)wire.write(0U,16U);
      wire.write(0U,16U);wire.write(3U,4U);wire.write(goldsrc_signed_positive(8U),16U);wire.write(0U,1U);
      wire.write(goldsrc_signed_positive(32U),16U);
      for(unsigned component=0;component<3;++component)wire.write(0U,16U);
      wire.write(0U,1U);
    }
    wire.write(0U,16U);wire.align_zero();
    auto record=wire.bytes();
    if (blood) {
      delta_fixture::BitWriter effect;
      effect.write(static_cast<std::uint8_t>(goldsrc::RuntimeControlOpcode::svc_temp_entity), 8U);
      effect.write(115U, 8U); // exact project-owned TE_BLOODSPRITE fields
      effect.write(128U * 8U, 16U); effect.write(0U, 16U); effect.write(64U * 8U, 16U);
      effect.write(10U, 16U); effect.write(11U, 16U); effect.write(247U, 8U); effect.write(8U, 8U);
      const auto& bytes = effect.bytes(); record.insert(record.end(), bytes.begin(), bytes.end());
    }
    if(damage) {
      delta_fixture::BitWriter effect;
      effect.write(66U,8U);effect.write(0U,8U);effect.write(25U,8U);effect.write(4U,32U);
      effect.write(128U*8U,16U);effect.write(0U,16U);effect.write(64U*8U,16U);
      const auto& bytes=effect.bytes();record.insert(record.end(),bytes.begin(),bytes.end());
    }
    return record;
  };
  // The same committed movement timeline drives A0 run, A1 air shots, A2 wall
  // shots, A3 target feedback (with/without return damage), and A4 stationary
  // target feedback. Model/ammo are explicit project-owned game API fixtures;
  // movement/health/feedback are encoded and committed by the real live owner.
  for (const auto render_fps : {30.0,60.0,100.0,144.0,240.0,0.0})
  for (const auto weapon : {2U, 4U}) for (unsigned scenario = 0U; scenario < 6U; ++scenario) {
    CAPTURE(suffix_carrier, weapon, scenario, render_fps);
    FakeTransport transport;
    const auto remote = network::NetworkAddress::loopback(27856U);
    const auto epoch = goldsrc::LiveRuntimeStageTimePoint{} + 110s;
    hlclient::client::ClientWorldState world;
    auto config = live_test_config();
    config.operation_mode = goldsrc::LiveRuntimeOperationMode::live_visual_control;
    config.live_visual_input_source = goldsrc::LiveVisualControlInputSource::keyboard_mouse;
    config.reference_prediction = true; config.movement_diagnostics = true;
    goldsrc::LiveRuntimeStage stage{transport, remote, world, config};
    const auto driven = drive_live_initial(stage, transport, remote, epoch, false, false, true, true, false, true);
    const auto resource = driven.last_server_sequence + 1U;
    transport.queue(remote, server_packet(resource, false,
        driven.transition_request.header.sequence.sequence.value(), false, service_envelope(resource_semantic_payload())));
    transport.queue(remote, server_packet(resource + 1U, false,
        driven.transition_request.header.sequence.sequence.value(), false, {std::byte{1U}}));
    for (unsigned step = 1U; step <= 8U; ++step) stage.update(driven.now + std::chrono::milliseconds{step});
    constexpr std::array empty_response{std::byte{5U}, std::byte{0U}, std::byte{0U}};
    const auto response = require_latest_payload(transport, empty_response);
    const auto first = driven.now + 9ms;
    transport.queue(remote, server_packet(resource + 2U, false, response.header.sequence.sequence.value(),
        response.header.sequence.flags.reliable)); stage.update(first);
    const auto spawn = require_latest_payload(transport, goldsrc::StockSpawnRequestBuilder::build(0x12345678U,
        goldsrc::decode_stock_server_world_map_crc(0xdeadbeefU, 0U)).encoding->semantic_bytes());
    transport.queue(remote, server_packet(resource + 3U, true, spawn.header.sequence.sequence.value(), false,
        live_baseline_and_runtime_payload(100.F, 100U, 80U, true)));
    stage.update(first + 1ms); stage.update(first + 2ms);
    const auto sendents = require_latest_payload(transport, goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
    transport.queue(remote, server_packet(resource + 4U, false, sendents.header.sequence.sequence.value(),
        sendents.header.sequence.flags.reliable, live_prediction_air_record(101.F, 10U)));
    stage.update(first + 3ms); stage.update(first + 2s + 2ms);
    REQUIRE(stage.live_visual_input_ready());
    const auto movement_package=hlclient::tests::collision_brush_fixture::package(true,0.0,0U);
    REQUIRE(stage.attach_reference_prediction_collision(movement_package));
    auto now = first + 2s + 3ms;
    goldsrc::LiveVisualControlInput input{1U, 1U, scenario == 5U ? 0.F : 1.F, 0, 0, 0, true, true};
    const auto forward_button=scenario==5U ? hlclient::gameplay_input::GameplayButtonMask{0U} :
        hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::move_forward);
    input.held_buttons=forward_button;
    REQUIRE(stage.submit_live_visual_input(input, now)); REQUIRE(stage.activate_live_visual_control(now));
    now += 20ms; stage.update(now);
    const auto anchor_carrier = decode_sent(transport.sent.back()).header.sequence.sequence.value();
    std::uint32_t server_sequence = resource + 4U;
    transport.queue(remote, server_packet(++server_sequence, false, anchor_carrier, false,
        live_prediction_air_record(102.F, 10U)));
    now += 20ms; stage.update(now);
    const auto initial_prediction = stage.live_reference_prediction_snapshot(now);
    REQUIRE(initial_prediction.state == goldsrc::LiveReferencePredictionState::active);
    while (stage.poll_weapon_command_submission()) {}
    auto next_render=now+20ms;
    std::size_t render_reads=0U,irregular_render_index=0U;
    constexpr std::array irregular_render_intervals{1ms,47ms,3ms,19ms,2ms,41ms,11ms,5ms};

    api::LocalWeaponModelMetadata model;
    model.generation = 1U; model.model_index = 100U + weapon; model.resource_revision = 1U;
    model.resource_name = weapon == 2U ? "models/v_9mmhandgun.mdl" : "models/v_9mmAR.mdl";
    model.supported_bodies[0] = model.selectable_bodies[0] = true;
    model.supported_bodies[2] = model.selectable_bodies[2] = true;
    const auto labels = weapon == 2U ? std::vector<std::string_view>{"idle1", "idle2", "idle3", "shoot", "shoot_empty",
        "reload_empty", "reload", "draw", "holster", "add_silencer"} :
        std::vector<std::string_view>{"longidle", "idle1", "grenade", "reload", "deploy", "shoot", "shoot", "shoot"};
    for (const auto label : labels) model.sequences.push_back({30,31,false,{},std::string(label)});
    for (const auto index : weapon == 2U ? std::vector<unsigned>{3U,4U} : std::vector<unsigned>{5U,6U,7U})
      model.sequences[index].events.push_back({0,5001,0,{std::byte{'1'},std::byte{'1'}}});
    config.game_client->bind_model(model);
    config.game_client->set_movement_audio_time_origin(std::chrono::duration<double>(epoch.time_since_epoch()).count());
    api::EffectPaletteMetadata palette; palette.status = api::EffectPaletteStatus::ready;
    palette.colors[247U] = {192U, 0U, 0U}; config.game_client->bind_effect_palette(palette);
    config.game_client->bind_effect_resource({api::EffectResourceBinding::Kind::model,10U,"sprites/bloodspray.spr"});
    config.game_client->bind_effect_resource({api::EffectResourceBinding::Kind::model,11U,"sprites/blood.spr"});
    std::size_t accepted_actions = 0U, blood_intents = 0U, attack_notifications = 0U;
    const auto prime_weapon = [&](double at, bool spent) {
      auto observed = *world.runtime_observation();
      observed.receiving_client->viewmodel_index = model.model_index;
      observed.receiving_client->owned_weapon_bits = 1U << weapon;
      observed.receiving_client->next_weapon_attack = 0.0;
      observed.weapon_hud.active_weapon_id = static_cast<std::uint8_t>(weapon);
      observed.weapon_hud.catalogue = {{.id=static_cast<std::uint8_t>(weapon),
          .command_name=weapon == 2U ? "weapon_9mmhandgun" : "weapon_9mmAR", .primary_ammo_type=1}};
      observed.weapon_hud.reserve_ammo[1U] = std::uint8_t{100U};
      const auto clip = static_cast<std::int16_t>((weapon == 2U ? 17 : 50) - (spent ? 1 : 0));
      observed.weapon_hud.clips[weapon] = clip;
      observed.weapon_slots = {{.wire_slot=static_cast<std::uint8_t>(weapon), .clip=clip, .in_reload=false,
          .next_primary_attack=0.0, .weapon_id=static_cast<std::uint8_t>(weapon)}};
      config.game_client->observe(observed, at);
    };
    prime_weapon(std::chrono::duration<double>(now-epoch).count(), false);
    goldsrc::movement::WorldOnlyMovementCollision independent_movement{movement_package};
    hlclient::collision::CollisionQueryScratch movement_query_scratch,effect_query_scratch;
    const auto query_before=independent_movement.trace_hull({10,0,64},{20,0,64},
        hlclient::movement::PlayerMovementHull::standing,movement_query_scratch);
    REQUIRE(query_before); REQUIRE(query_before.result); CHECK(query_before.result->fraction==1.0);
    for (unsigned step = 0U; step < 16U; ++step) {
      ++input.input_revision;
      input.pressed_buttons = scenario != 0U && step == 1U ?
          hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::attack_primary) : 0U;
      input.held_buttons=forward_button|input.pressed_buttons;
      input.released_buttons = scenario != 0U && step == 2U ?
          hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::attack_primary) : 0U;
      REQUIRE(stage.submit_live_visual_input(input, now));
      const bool hit_feedback = scenario >= 3U && step == 2U;
      const std::uint32_t health = scenario == 4U && step >= 2U ? 75U : 100U;
      const auto body = feedback_record(102.F + static_cast<float>(step + 1U) * .02F, health, hit_feedback,
          scenario==4U && step==2U);
      const auto incoming = ++server_sequence;
      transport.queue(remote, suffix_carrier ? fragment_with_suffix(incoming, anchor_carrier, body, old_completion) :
          server_packet(incoming, false, anchor_carrier, false, body));
      now += 20ms; stage.update(now);
      INFO("A envelope step=" << step << " old_completion=" << old_completion << " stage=" << static_cast<unsigned>(stage.state()));
      if (stage.error()) {
        INFO("A stage_error=" << goldsrc::to_string(stage.error()->code) << " context=" << stage.error()->context);
        if (stage.error()->runtime_code) INFO("A runtime_error=" << goldsrc::to_string(*stage.error()->runtime_code));
        if (stage.error()->driver_code) INFO("A driver_error=" << goldsrc::to_string(*stage.error()->driver_code));
        if (stage.error()->runtime_failure) INFO("A runtime_detail=" << stage.error()->runtime_failure->context);
        REQUIRE_FALSE(stage.terminal()); // Keep owning error context alive on failure.
      }
      REQUIRE_FALSE(stage.terminal());
      const auto at = std::chrono::duration<double>(now-epoch).count() + .001;
      prime_weapon(at, scenario != 0U && step >= 2U);
      while (const auto submitted = stage.poll_weapon_command_submission()) {
        api::LocalWeaponSubmittedCommand command{submitted->generation, submitted->command_sequence,
            submitted->buttons, std::chrono::duration<double>(
                std::chrono::nanoseconds{submitted->command_end_nanoseconds} - epoch.time_since_epoch()).count()};
        command.shot_context = api::LocalWeaponSubmittedCommand::ShotContext{
            {10,0,92},{1,0,0},{0,-1,0},{0,0,1}};
        config.game_client->submit(command, at);
        attack_notifications += (submitted->buttons & goldsrc::kReferenceGoldSrcButtonAttack) != 0U;
      }
      const auto weapon_frame = config.game_client->sample(at);
      accepted_actions = std::max(accepted_actions, weapon_frame.actions_started);
      const auto visual = config.game_client->local_visuals({{10,0,92},{1,0,0},{0,-1,0},{0,0,1},{},at});
      INFO("sample_at=" << at << " actions=" << weapon_frame.actions_started << " action_error=" <<
          static_cast<int>(weapon_frame.error) << " action_time=" <<
          (weapon_frame.identity ? weapon_frame.identity->started_at_seconds : -1.0) <<
          " unsupported_marker=" << visual.statistics.unsupported_metadata);
      if (scenario != 0U && step == 1U) { REQUIRE(visual.flash); REQUIRE(visual.light); REQUIRE(visual.shell); }
      if(scenario!=0U) {
        hlclient::collision::CollisionTraceRequest effect_query;
        effect_query.start={10,0,92};effect_query.end=scenario==2U ? hlclient::assets::AssetVector3{-256,0,92} :
            hlclient::assets::AssetVector3{256,0,92};
        const auto effect_hit=hlclient::collision::CollisionWorldQuery{movement_package}.trace_line(effect_query,effect_query_scratch);
        REQUIRE(effect_hit); REQUIRE(effect_hit.result);
        CHECK(effect_hit.result->hit.has_value()==(scenario==2U));
      }
      const auto query_after=independent_movement.trace_hull({10,0,64},{20,0,64},
          hlclient::movement::PlayerMovementHull::standing,movement_query_scratch);
      REQUIRE(query_after); REQUIRE(query_after.result);
      CHECK(query_after.result->fraction==query_before.result->fraction);
      CHECK(query_after.result->start_solid==query_before.result->start_solid);
      CHECK(query_after.result->all_solid==query_before.result->all_solid);
      CHECK(query_after.result->hit==query_before.result->hit);
      CHECK(query_after.result->end_position.x==query_before.result->end_position.x);
      CHECK(query_after.result->end_position.y==query_before.result->end_position.y);
      CHECK(query_after.result->end_position.z==query_before.result->end_position.z);
      const auto blood = config.game_client->drain_temporary_effects(at); blood_intents += blood.count;
      const auto prediction = stage.live_reference_prediction_snapshot(now);
      INFO("step=" << step << " state=" << goldsrc::to_string(prediction.state) << " reason=" << prediction.reason
          << " fallback=" << prediction.fallback_count << " anchor_status=" <<
          static_cast<int>(stage.live_usercmd_snapshot()->reference_prediction_last_anchor_status.value_or(
              goldsrc::ReferencePredictionAnchorStatus::invalid_record)));
      CHECK(prediction.state == goldsrc::LiveReferencePredictionState::active);
      CHECK(prediction.fallback_count == initial_prediction.fallback_count);
      REQUIRE(prediction.canonical_velocity);
      CHECK(prediction.canonical_velocity->x == 0.F);
      CHECK(prediction.canonical_velocity->y == 0.F);
      CHECK(prediction.canonical_velocity->z == 0.F);
      REQUIRE(prediction.canonical_source_record_identity);
      CHECK(*prediction.canonical_source_record_identity > 0U);
      CHECK(prediction.canonical_source_sequence == incoming);
      CHECK(prediction.canonical_body_kind == (suffix_carrier ? "fragment_suffix" : "direct_body"));
      CHECK(prediction.last_anchor_status == goldsrc::ReferencePredictionAnchorStatus::bound);
      REQUIRE(prediction.matched_origin_before_correction);
      REQUIRE(prediction.matched_velocity_before_correction);
      CHECK(prediction.matched_velocity_before_correction->x == 0.F);
      CHECK(prediction.matched_velocity_before_correction->y == 0.F);
      CHECK(prediction.matched_velocity_before_correction->z == 0.F);
      const auto metrics = stage.movement_diagnostics_snapshot();
      REQUIRE(metrics.latest_command);
      CHECK(metrics.latest_command->msec == 20U);
      CHECK(metrics.latest_command->side == 0);
      CHECK(metrics.latest_command->up == 0);
      CHECK(metrics.latest_command->buttons == ((scenario == 5U ? 0U : goldsrc::kReferenceGoldSrcButtonForward) |
          (scenario != 0U && step == 1U ? goldsrc::kReferenceGoldSrcButtonAttack : 0U)));
      REQUIRE(metrics.latest_command_sequence);
      CHECK(*metrics.latest_command_sequence == step + 3U);
      CHECK(metrics.latest_command_sample_time_ns == std::chrono::duration_cast<std::chrono::nanoseconds>(
          now.time_since_epoch()).count());
      REQUIRE(prediction.predicted_origin);
      // Independent dry-air cap30: the seed replays exactly(step+2)20ms
      // commands after command1, irrespective of effects or packet envelope.
      CHECK(prediction.predicted_origin->x == Catch::Approx(10.0 +
          (scenario == 5U ? 0.0 : .6 * static_cast<double>(step + 2U))).margin(.002));
      CHECK(prediction.predicted_origin->z == Catch::Approx(64.0 - .5 * 800.0 *
          std::pow(.02 * static_cast<double>(step + 2U),2)).margin(.002));
      // Fixed command/RX/effect ticks remain 20ms. Only render snapshot reads
      // follow this independent global cadence, always in ordered deadlines
      // between command updates. Presentation may move; kernel/history may not.
      const auto immutable_commands=stage.live_usercmd_snapshot();REQUIRE(immutable_commands);
      const auto immutable_observation=world.runtime_observation();
      const auto same_vector=[](const auto& actual,const auto& expected) {
        REQUIRE(actual.has_value()==expected.has_value());
        if(actual) {CHECK(actual->x==expected->x);CHECK(actual->y==expected->y);CHECK(actual->z==expected->z);}
      };
      while(next_render<now+20ms) {
        REQUIRE(next_render>=now);
        const auto rendered=stage.live_reference_prediction_snapshot(next_render);++render_reads;
        CHECK(rendered.state==prediction.state);
        same_vector(rendered.canonical_origin,prediction.canonical_origin);
        same_vector(rendered.canonical_velocity,prediction.canonical_velocity);
        same_vector(rendered.predicted_origin,prediction.predicted_origin);
        same_vector(rendered.predicted_velocity,prediction.predicted_velocity);
        CHECK(rendered.local_steps==prediction.local_steps);
        CHECK(rendered.accepted_corrections==prediction.accepted_corrections);
        CHECK(rendered.replayed_commands==prediction.replayed_commands);
        CHECK(rendered.fallback_count==prediction.fallback_count);
        CHECK(rendered.history_depth==prediction.history_depth);
        CHECK(rendered.last_raw_position_error==prediction.last_raw_position_error);
        CHECK(rendered.maximum_raw_position_error==prediction.maximum_raw_position_error);
        CHECK(rendered.anchor_command==prediction.anchor_command);
        CHECK(rendered.canonical_source_record_identity==prediction.canonical_source_record_identity);
        CHECK(rendered.canonical_source_sequence==prediction.canonical_source_sequence);
        CHECK(world.runtime_observation()==immutable_observation);
        const auto after_read=stage.live_usercmd_snapshot();REQUIRE(after_read);
        CHECK(after_read->generated_command_count==immutable_commands->generated_command_count);
        CHECK(after_read->new_command_submission_count==immutable_commands->new_command_submission_count);
        next_render+=render_fps>0.0 ? std::chrono::nanoseconds{static_cast<std::int64_t>(std::llround(1e9/render_fps))} :
            std::chrono::duration_cast<std::chrono::nanoseconds>(irregular_render_intervals[
                irregular_render_index++%irregular_render_intervals.size()]);
      }
    }
    CHECK(render_reads>0U);
    CHECK(accepted_actions == (scenario == 0U ? 0U : 1U));
    CHECK(attack_notifications == (scenario == 0U ? 0U : 1U));
    CHECK(blood_intents == (scenario >= 3U ? 1U : 0U));
    CHECK(world.runtime_observation()->receiving_client->health == (scenario == 4U ? 75.0 : 100.0));
    REQUIRE(world.runtime_observation()->packet_entities.size()==2U);
    CHECK(world.runtime_observation()->packet_entities[1U].entity_number==2U);
    CHECK(world.runtime_observation()->packet_entities[1U].player_movement_schema);
    if (scenario == 4U) CHECK(world.runtime_observation()->lifecycle.damage_events > 0U);
    const auto accounting = stage.live_usercmd_snapshot(); REQUIRE(accounting);
    CHECK(accounting->generated_command_count == 18U);
    CHECK(accounting->new_command_submission_count == 18U);
    CHECK(accounting->last_sampled_forward_axis == (scenario == 5U ? 0.F : 1.F));
    CHECK(accounting->reference_prediction_last_anchor_status == goldsrc::ReferencePredictionAnchorStatus::bound);
  }
}

TEST_CASE("E24 normal accepted TX stream delivers quick LMB release and delayed neutral throw ticks",
          "[e24][goldsrc][live-runtime][integration][normal-input][no-stock]") {
  namespace grenade_fixture=hlclient::tests::hand_grenade_fixture;
  namespace api=hlclient::game_api;
  for(const bool prediction:{false,true}) {
    CAPTURE(prediction);FakeTransport transport;
    const auto remote=network::NetworkAddress::loopback(27859U);
    const auto epoch=goldsrc::LiveRuntimeStageTimePoint{}+60s;
    hlclient::client::ClientWorldState world;auto config=live_test_config();
    config.operation_mode=goldsrc::LiveRuntimeOperationMode::live_visual_control;
    config.live_visual_input_source=goldsrc::LiveVisualControlInputSource::keyboard_mouse;
    config.reference_prediction=prediction;config.game_client->set_movement_audio_time_origin(60.0);
    goldsrc::LiveRuntimeStage stage{transport,remote,world,config};
    const auto driven=drive_live_initial(stage,transport,remote,epoch,false,false,true,true);
    const auto resource=driven.last_server_sequence+1U;
    transport.queue(remote,server_packet(resource,false,
        driven.transition_request.header.sequence.sequence.value(),false,service_envelope(resource_semantic_payload())));
    transport.queue(remote,server_packet(resource+1U,false,
        driven.transition_request.header.sequence.sequence.value(),false,{std::byte{1}}));
    for(std::size_t step=1;step<=8;++step)stage.update(driven.now+std::chrono::milliseconds{step});
    constexpr std::array empty_response{std::byte{5},std::byte{0},std::byte{0}};
    const auto response=require_latest_payload(transport,empty_response);const auto first=driven.now+9ms;
    transport.queue(remote,server_packet(resource+2U,false,response.header.sequence.sequence.value(),response.header.sequence.flags.reliable));
    stage.update(first);
    const auto spawn=require_latest_payload(transport,goldsrc::StockSpawnRequestBuilder::build(
        0x12345678U,goldsrc::decode_stock_server_world_map_crc(0xdeadbeefU,0U)).encoding->semantic_bytes());
    transport.queue(remote,server_packet(resource+3U,true,spawn.header.sequence.sequence.value(),false,
        live_baseline_and_runtime_payload(100.F,100U,80U,true)));
    stage.update(first+1ms);stage.update(first+2ms);
    const auto sendents=require_latest_payload(transport,goldsrc::StockSendEntitiesRequestBuilder::build().semantic_bytes());
    transport.queue(remote,server_packet(resource+4U,false,sendents.header.sequence.sequence.value(),
        sendents.header.sequence.flags.reliable,service_envelope(live_prediction_air_record(101.F,10U))));
    stage.update(first+3ms);stage.update(first+2s+2ms);REQUIRE(stage.live_visual_input_ready());
    if(prediction)REQUIRE(stage.attach_reference_prediction_collision(hlclient::tests::collision_brush_fixture::package(false)));
    auto now=first+2s+3ms;goldsrc::LiveVisualControlInput input{1,1,0,0,0,0,true,true};
    REQUIRE(stage.submit_live_visual_input(input,now));REQUIRE(stage.activate_live_visual_control(now));
    while(stage.poll_weapon_command_submission()) {} // only pre-scenario accepted startup commands
    auto observed=grenade_fixture::state();config.game_client->bind_model(grenade_fixture::model());
    config.game_client->observe(observed,std::chrono::duration<double>(now-epoch).count());
    const auto attack=hlclient::gameplay_input::gameplay_button_mask(hlclient::gameplay_input::GameplayButton::attack_primary);
    auto registry=hlclient::test::usercmd_fixture::exact_registry();
    const auto bound=goldsrc::bind_goldsrc_usercmd_schema(registry,goldsrc::GoldSrcUserCmdSchemaBindingProfile::public_goldsrc48_usercmd_schema_v1);
    REQUIRE(bound);goldsrc::GoldSrcReferenceClientMoveCodec codec{*bound.binding,1U};
    unsigned delivered=0,pose_activation_handoffs=0;std::uint32_t last_sequence=0;double prime_sample=0;
    const auto tick=[&](unsigned held) {
      const auto before_command_pose=stage.live_reference_prediction_snapshot(now);
      const auto before_command_origin=world.runtime_observation()->receiving_client->origin;
      ++input.input_revision;input.released_buttons=input.held_buttons&~held;
      input.pressed_buttons=held&~input.held_buttons;input.held_buttons=held;
      REQUIRE(stage.submit_live_visual_input(input,now));now+=20ms;stage.update(now);REQUIRE_FALSE(stage.terminal());
      const auto sent=decode_sent(transport.sent.back());goldsrc::ReferenceMoveSource source;
      source.generation=1;source.sequence=sent.header.sequence.sequence.value();
      const auto decoded=goldsrc::decode_reference_client_payload(codec,sent.payload,source);
      REQUIRE_FALSE(decoded.error);REQUIRE(decoded.moves.size()==1);REQUIRE(decoded.moves[0].new_count==1);
      const auto notification=stage.poll_weapon_command_submission();REQUIRE(notification);++delivered;
      CHECK(notification->buttons==decoded.moves[0].commands.back().value.buttons);
      // E27 neutral command-pose handoff is captured for this immutable TX,
      // not reconstructed from the later rendered camera or a weapon ID.
      const auto command_pose=stage.live_reference_prediction_snapshot(now);
      if(before_command_pose.state==goldsrc::LiveReferencePredictionState::active &&
          command_pose.state==goldsrc::LiveReferencePredictionState::active) {
        REQUIRE(command_pose.predicted_origin);REQUIRE(command_pose.predicted_hull);
        REQUIRE(notification->player_origin);REQUIRE(notification->player_ducked);
        CHECK(notification->player_origin->x==command_pose.predicted_origin->x);
        CHECK(notification->player_origin->y==command_pose.predicted_origin->y);
        CHECK(notification->player_origin->z==command_pose.predicted_origin->z);
        CHECK(*notification->player_ducked==(*command_pose.predicted_hull==hlclient::movement::PlayerMovementHull::ducked));
      } else if(before_command_origin.complete()) {
        REQUIRE(notification->player_origin);
        CHECK(notification->player_origin->x==Catch::Approx(*before_command_origin.x));
        CHECK(notification->player_origin->y==Catch::Approx(*before_command_origin.y));
        CHECK(notification->player_origin->z==Catch::Approx(*before_command_origin.z));
        // RX can activate prediction AFTER TX notification is frozen in the
        // same update. Its first airborne step must not replace that pose.
        if(command_pose.state==goldsrc::LiveReferencePredictionState::active) {
          REQUIRE(command_pose.predicted_origin);++pose_activation_handoffs;
          CHECK(notification->player_origin->z!=command_pose.predicted_origin->z);
        }
      }
      CHECK(notification->buttons==(held ? 1U : 0U));CHECK(notification->command_sequence>last_sequence);
      last_sequence=notification->command_sequence;CHECK_FALSE(stage.poll_weapon_command_submission());
      const auto seconds=std::chrono::duration<double>(std::chrono::nanoseconds{notification->command_end_nanoseconds}-epoch.time_since_epoch()).count();
      auto command=grenade_fixture::command(notification->command_sequence,seconds,notification->buttons);
      for(std::size_t axis=0;axis<3;++axis)(*command.shot_context->view_angles_degrees)[axis]=
          static_cast<float>(std::remainder(notification->angle_turns[axis]*(360.0/65536.0),360.0));
      const auto sample_time=std::chrono::duration<double>(now-epoch).count()+.001;
      config.game_client->submit(command,sample_time);const auto result=config.game_client->sample(sample_time);
      const auto accounting=stage.live_usercmd_snapshot();REQUIRE(accounting);
      CHECK(accounting->generated_command_count==accounting->new_command_submission_count);
      CHECK(accounting->command_interval==20ms);
      stage.update(now);CHECK_FALSE(stage.poll_weapon_command_submission());
      if(held)prime_sample=seconds;return result;
    };
    (void)tick(0);const auto primed=tick(attack);REQUIRE(primed.visual);CHECK(primed.visual->sequence==2);
    const auto before=stage.live_usercmd_snapshot();REQUIRE(before);
    const auto carrier=decode_sent(transport.sent.back());
    transport.queue(remote,server_packet(resource+5U,false,carrier.header.sequence.sequence.value(),false,
        service_envelope(live_prediction_air_record(102.F,10U))));
    const auto released=tick(0);REQUIRE(released.visual);CHECK(released.visual->sequence==2);
    CHECK(released.actions_started==1);CHECK(stage.live_usercmd_snapshot()->driver_rx_total.owning_datagrams==before->driver_rx_total.owning_datagrams+1);
    CHECK(stage.live_usercmd_snapshot()->runtime_records_committed_total==before->runtime_records_committed_total+1);
    api::LocalWeaponPresentationSnapshot thrown;
    for(unsigned i=0;i<25;++i) {thrown=tick(0);if(i<23)CHECK(thrown.actions_started==1);}
    REQUIRE(thrown.identity);REQUIRE(thrown.visual);CHECK(thrown.action==api::LocalWeaponAction::throw_release);
    CHECK(thrown.visual->sequence==4);CHECK(thrown.actions_started==2);
    CHECK(thrown.identity->started_at_seconds==Catch::Approx(prime_sample+.5));CHECK(delivered==28);
    CHECK(pose_activation_handoffs==(prediction ? 1U : 0U));
    CHECK(config.game_client->hud(observed,std::chrono::duration<double>(now-epoch).count()+.001).primary_reserve==5);
    CHECK(config.game_client->drain_audio().count==0);CHECK(observed.packet_entities.empty());
    const auto sent_count=transport.sent.size();const auto commands=stage.live_usercmd_snapshot()->new_command_submission_count;
    for(unsigned frame=0;frame<20;++frame)(void)stage.live_reference_prediction_snapshot(now+std::chrono::microseconds{frame});
    CHECK(transport.sent.size()==sent_count);CHECK(stage.live_usercmd_snapshot()->new_command_submission_count==commands);
    CHECK_FALSE(stage.poll_weapon_command_submission());
  }
}
