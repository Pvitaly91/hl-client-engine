#include "delta_test_fixture.hpp"
#include "local_resource_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "resource_client_response_test_fixture.hpp"
#include "resource_list_test_fixture.hpp"
#include "user_info_test_fixture.hpp"
#include "goldsrc_usercmd_test_fixture.hpp"
#include "collision_brush_test_fixture.hpp"

#include <hlclient/client/client_world_state.hpp>
#include <hlclient/goldsrc/live_runtime_stage.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
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
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
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

[[nodiscard]] std::vector<std::byte> resource_semantic_payload() {
  constexpr std::array prefix{
      std::byte{45U}, std::byte{1U}, std::byte{0U},
      std::byte{0U},  std::byte{0U}, std::byte{0U},
      std::byte{0U},  std::byte{0U}, std::byte{0U},
  };
  std::vector<std::byte> payload{prefix.begin(), prefix.end()};
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
};

constexpr delta_fixture::Field kLiveClientFields[]{
    {"health", 0x8000'0004U, 0U, 10U},
};

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

[[nodiscard]] std::vector<std::vector<std::byte>> live_schemas(
    const bool with_usercmd = false,
    const bool prediction_fields = false) {
  auto schemas = std::vector<std::vector<std::byte>>{
      delta_fixture::schema("entity_state_t", kLiveEntityFields),
      delta_fixture::schema("entity_state_player_t", prediction_fields
          ? std::span<const delta_fixture::Field>{kPredictionPlayerFields}
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
    const bool prediction_fields = false) {
  std::vector<std::byte> post_delta;
  move_fixture::append_move_vars_body(post_delta);
  move_fixture::append_confirmed_controls(post_delta);
  post_delta.insert(post_delta.end(),
                    user_fixture::kExactUserInfoMessage.begin(),
                    user_fixture::kExactUserInfoMessage.end());
  return delta_fixture::service_payload(live_schemas(with_usercmd, prediction_fields),
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
    const float server_time, const std::uint32_t origin_x) {
  delta_fixture::BitWriter writer;
  writer.write(7U, 8U);
  writer.write(std::bit_cast<std::uint32_t>(server_time), 32U);
  writer.write(goldsrc::kGoldSrcSvcClientDataOpcode, 8U);
  writer.write(0U, 1U);
  writer.write(2U, 3U);
  writer.write(0x1fffU, 16U);
  writer.write(goldsrc_signed_positive(100U), 10U);
  writer.write(0U, 16U);
  writer.write(0U, 16U);
  writer.write(0U, 16U);
  writer.write(goldsrc_signed_positive(112U), 10U);
  writer.write(goldsrc_signed_positive(origin_x * 8U), 16U);
  writer.write(0U, 16U);
  writer.write(goldsrc_signed_positive(512U), 16U);
  writer.write(0U, 32U);
  writer.write(3'200U, 16U);
  writer.write(0U, 1U);
  writer.write(0U, 2U);
  writer.write(0U, 3U);
  writer.write(0U, 1U);
  writer.align_zero();

  writer.write(goldsrc::kGoldSrcSvcPacketEntitiesOpcode, 8U);
  writer.write(1U, 16U);
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
                   const bool prediction_fields = false) {
  REQUIRE(stage.start(epoch, transport.local));
  stage.update(epoch + 1ms);
  REQUIRE_FALSE(transport.sent.empty());
  const auto initial_request = decode_sent(transport.sent.front());

  auto initial_payload = live_initial_semantic_payload(
      with_usercmd, prediction_fields);
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
  transport.queue(remote, server_packet(resource_sequence + 6U, false,
      first_sent.header.sequence.sequence.value(), false,
      service_envelope(live_prediction_air_record(102.1F, 10U))));
  stage.update(active_at + 60ms);
  const auto corrected = stage.live_reference_prediction_snapshot(active_at + 60ms);
  CAPTURE(corrected.reason, corrected.last_seed_status,
          corrected.last_ground_status, corrected.replayed_commands);
  REQUIRE_FALSE(stage.terminal());
  CHECK(corrected.accepted_corrections > 0U);
  CHECK(corrected.replayed_commands > 0U);
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
