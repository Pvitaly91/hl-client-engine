#include "delta_test_fixture.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include "local_resource_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "resource_client_response_test_fixture.hpp"
#include "resource_list_test_fixture.hpp"
#include "synthetic_asset_importers.hpp"
#include "user_info_test_fixture.hpp"

#include <hlclient/auth/authentication_provider.hpp>
#include <hlclient/goldsrc/challenge_protocol.hpp>
#include <hlclient/goldsrc/connect_request_stage.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/precache_asset_dispatch_stage.hpp>
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
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace assets = hlclient::assets;
namespace auth = hlclient::auth;
namespace consistency = hlclient::resource_consistency;
namespace delta_fixture = hlclient::test::delta_fixture;
namespace goldsrc = hlclient::goldsrc;
namespace local = hlclient::local_resources;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace network = hlclient::network;
namespace readiness_fixture = hlclient::tests::readiness_fixture;
namespace response_fixture = hlclient::test::resource_client_response_fixture;
namespace synthetic = hlclient::tests::synthetic_assets;
namespace user_fixture = hlclient::test::user_info_fixture;

inline constexpr std::string_view kSyntheticAuthenticationMarker =
    "TEST_AUTH_MATERIAL";
inline constexpr std::string_view kSyntheticProtectedAuthentication =
    "TEST_AUTH_MATERIAL_TEST_AUTH_MAT";

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
    sent.push_back(SentDatagram{
        destination,
        std::vector<std::byte>{payload.begin(), payload.end()},
    });
    return {network::DatagramSendStatus::sent, {}};
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
    const auto byte_count = payload.size();
    incoming.push_back({
        network::DatagramTransportReceiveStatus::received,
        network::Datagram{source, std::move(payload)},
        source,
        byte_count,
        {},
    });
  }

  network::NetworkAddress local{network::NetworkAddress::loopback(31'734U)};
  std::vector<SentDatagram> sent;
  std::deque<network::DatagramTransportReceiveResult> incoming;
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

class CountingAuthenticationLifetime final
    : public auth::IAuthenticationSessionLifetime {
public:
  explicit CountingAuthenticationLifetime(std::size_t &releases) noexcept
      : releases_{releases} {}

  ~CountingAuthenticationLifetime() override { ++releases_; }

private:
  std::size_t &releases_;
};

[[nodiscard]] std::vector<std::byte> text_bytes(const std::string_view text) {
  const auto view = std::as_bytes(std::span{text.data(), text.size()});
  return {view.begin(), view.end()};
}

[[nodiscard]] std::vector<std::byte>
challenge_response(const std::uint32_t challenge) {
  std::string packet{"\xFF\xFF\xFF\xFF", 4U};
  packet +=
      "A00000000 " + std::to_string(challenge) + " 3 72057594037927936 0\n";
  packet.push_back('\0');
  return text_bytes(packet);
}

[[nodiscard]] std::vector<std::byte>
accept_response(const network::NetworkAddress client) {
  std::string packet{"\xFF\xFF\xFF\xFF", 4U};
  packet += "B 1 \"" + client.to_string() + "\" 0 10210";
  packet.push_back('\0');
  return text_bytes(packet);
}

struct PreparedWithAuthenticationSession {
  goldsrc::PreparedConnectRequest request;
  auth::AuthenticationSession session;
};

[[nodiscard]] PreparedWithAuthenticationSession
prepared_request_with_session(std::size_t &releases) {
  std::vector<std::byte> suffix(
      goldsrc::kObservedConnectAuthenticationSuffixSize);
  const auto marker = text_bytes(kSyntheticAuthenticationMarker);
  for (std::size_t index = 0U; index < suffix.size(); ++index) {
    suffix[index] = marker[index % marker.size()];
  }
  auto material = goldsrc::AuthenticationMaterial::create(
      text_bytes(kSyntheticProtectedAuthentication), suffix);
  REQUIRE(material);
  auth::AuthenticationSession session{
      std::move(*material.value),
      std::make_unique<CountingAuthenticationLifetime>(releases)};
  auto transferred = session.take_material();
  REQUIRE(transferred);
  auto profile = goldsrc::ConnectCompatibilityProfile{};
  profile.protected_authentication_is_ascii_hex = false;
  auto prepared =
      goldsrc::prepare_connect_request({}, std::move(*transferred), profile);
  REQUIRE(prepared);
  return {std::move(*prepared.value), std::move(session)};
}

class CountingConsistencyLifetime final
    : public consistency::IResourceConsistencySessionLifetime {
public:
  explicit CountingConsistencyLifetime(std::size_t &releases) noexcept
      : releases_{releases} {}

  ~CountingConsistencyLifetime() override { ++releases_; }

private:
  std::size_t &releases_;
};

class ImmediateConsistencyOperation final
    : public consistency::ResourceConsistencyOperation {
public:
  ImmediateConsistencyOperation(std::size_t &update_count,
                                std::size_t &cancel_count,
                                std::size_t &lifetime_releases) noexcept
      : update_count_{update_count}, cancel_count_{cancel_count},
        lifetime_releases_{lifetime_releases} {}

  [[nodiscard]] consistency::ResourceConsistencyUpdateResult update() override {
    ++update_count_;
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
  std::size_t &update_count_;
  std::size_t &cancel_count_;
  std::size_t &lifetime_releases_;
  bool cancelled_{false};
};

class ImmediateConsistencyProvider final
    : public consistency::IResourceConsistencyProvider {
public:
  [[nodiscard]] consistency::ResourceConsistencyBeginResult
  begin(const consistency::ResourceConsistencyRequirements &) override {
    ++begin_count;
    return consistency::ResourceConsistencyBeginResult::started(
        std::make_unique<ImmediateConsistencyOperation>(
            update_count, cancel_count, lifetime_releases));
  }

  std::size_t begin_count{0U};
  std::size_t update_count{0U};
  std::size_t cancel_count{0U};
  std::size_t lifetime_releases{0U};
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

[[nodiscard]] goldsrc::PrecacheAssetDispatchStageConfig test_config() {
  goldsrc::PrecacheAssetDispatchStageConfig config;
  auto &transition = config.manifest.response.resource_list.transition;
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
  config.manifest.response.resource_list.maximum_stage_events = 64U;
  config.manifest.response.maximum_driver_events_per_update = 64U;
  config.manifest.response.response.maximum_response_stage_events = 64U;
  config.manifest.manifest.maximum_manifest_events = 64U;
  config.maximum_stage_events = 128U;
  return config;
}

[[nodiscard]] goldsrc::ChallengeExchangeConfig coordinator_challenge_config() {
  goldsrc::ChallengeExchangeConfig config;
  config.retry_interval = 100ms;
  config.timeout = 1s;
  config.maximum_attempts = 2U;
  config.maximum_datagrams_per_update = 4U;
  return config;
}

[[nodiscard]] goldsrc::ConnectResponseWaitConfig coordinator_response_config() {
  goldsrc::ConnectResponseWaitConfig config;
  config.timeout = 1s;
  config.maximum_datagrams_per_update = 4U;
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

[[nodiscard]] std::vector<std::byte> resource_semantic_payload(
    const std::span<const resource_list_test_fixture::EntrySpec> entries) {
  constexpr std::array prefix{
      std::byte{45U}, std::byte{1U}, std::byte{0U},
      std::byte{0U},  std::byte{0U}, std::byte{0U},
      std::byte{0U},  std::byte{0U}, std::byte{0U},
  };
  const auto message = resource_list_test_fixture::make_message(entries);
  std::vector<std::byte> payload{prefix.begin(), prefix.end()};
  payload.insert(payload.end(), message.bytes.begin(), message.bytes.end());
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

struct ManifestBoundary {
  goldsrc::PrecacheAssetDispatchStageTimePoint next_update;
  std::size_t sent_datagram_count{0U};
  std::size_t resource_response_transmit_count{0U};
};

enum class ResponseLossMode {
  none,
  dropped_response,
  dropped_ack,
};

void drain_events(
    goldsrc::PrecacheAssetDispatchStage &stage,
    std::vector<goldsrc::PrecacheAssetDispatchStageEvent> &events) {
  while (auto event = stage.poll_event()) {
    events.push_back(std::move(*event));
  }
}

[[nodiscard]] ManifestBoundary drive_to_manifest_boundary(
    goldsrc::PrecacheAssetDispatchStage &stage, FakeTransport &transport,
    const network::NetworkAddress remote,
    const std::span<const resource_list_test_fixture::EntrySpec> entries,
    const goldsrc::PrecacheAssetDispatchStageTimePoint epoch,
    std::size_t &connection_releases,
    std::vector<goldsrc::PrecacheAssetDispatchStageEvent> *events = nullptr,
    const ResponseLossMode loss_mode = ResponseLossMode::none) {
  REQUIRE(stage.start(
      epoch, transport.local,
      std::make_unique<CountingConnectionLifetime>(connection_releases)));
  stage.update(epoch + 1ms);
  REQUIRE(transport.sent.size() == 1U);
  const auto initial = decode_sent(transport.sent.front());

  transport.queue(
      remote, server_packet(1U, true, initial.header.sequence.sequence.value(),
                            true, service_envelope(first_semantic_payload())));
  stage.update(epoch + 2ms);
  stage.update(epoch + 3ms);
  REQUIRE(transport.sent.size() >= 3U);
  const auto transition = decode_sent(transport.sent.back());
  constexpr std::array exact_request{
      std::byte{0x03U}, std::byte{'s'}, std::byte{'e'},
      std::byte{'n'},   std::byte{'d'}, std::byte{'r'},
      std::byte{'e'},   std::byte{'s'}, std::byte{0U},
  };
  REQUIRE(std::ranges::equal(transition.payload, exact_request));

  transport.queue(
      remote, server_packet(
                  2U, false, transition.header.sequence.sequence.value(), false,
                  service_envelope(resource_semantic_payload(entries))));
  stage.update(epoch + 4ms);
  REQUIRE_FALSE(transport.sent.empty());
  const auto response = decode_sent(transport.sent.back());
  REQUIRE(response.header.sequence.flags.reliable);
  REQUIRE(response.header.sequence.flags.fragmented);
  REQUIRE(response.fragments[0U]);
  REQUIRE_FALSE(response.fragments[1U]);

  constexpr std::array semantic{
      std::byte{0x03U}, std::byte{'s'}, std::byte{'p'}, std::byte{'a'},
      std::byte{'w'},   std::byte{'n'}, std::byte{0U},
  };
  auto continuation_time = epoch + 5ms;
  std::size_t response_transmit_count = 1U;
  switch (loss_mode) {
  case ResponseLossMode::none:
    transport.queue(remote,
                    server_packet(3U, false,
                                  response.header.sequence.sequence.value(),
                                  true, service_envelope(semantic)));
    stage.update(continuation_time);
    break;
  case ResponseLossMode::dropped_response: {
    transport.queue(remote,
                    server_packet(3U, true,
                                  response.header.sequence.sequence.value(),
                                  false));
    stage.update(epoch + 5ms);
    REQUIRE_FALSE(transport.sent.empty());
    const auto gap = decode_sent(transport.sent.back());
    REQUIRE_FALSE(gap.header.sequence.flags.reliable);

    transport.queue(
        remote,
        server_packet(4U, false, gap.header.sequence.sequence.value(), false));
    stage.update(epoch + 6ms);
    REQUIRE_FALSE(transport.sent.empty());
    const auto retry = decode_sent(transport.sent.back());
    REQUIRE(retry.header.sequence.flags.reliable);
    REQUIRE(retry.header.sequence.flags.fragmented);
    REQUIRE(retry.fragments[0U]);
    REQUIRE(response.fragments[0U]);
    CHECK(retry.header.sequence.sequence != response.header.sequence.sequence);
    CHECK(retry.fragments[0U]->packed_id() ==
          response.fragments[0U]->packed_id());
    CHECK(retry.payload == response.payload);
    response_transmit_count = 2U;
    continuation_time = epoch + 7ms;
    transport.queue(
        remote, server_packet(5U, false, retry.header.sequence.sequence.value(),
                              true, service_envelope(semantic)));
    stage.update(continuation_time);
    break;
  }
  case ResponseLossMode::dropped_ack: {
    const auto sends_after_response = transport.sent.size();
    stage.update(epoch + 5ms);
    CHECK(transport.sent.size() == sends_after_response);
    transport.queue(
        remote, server_packet(3U, false,
                              response.header.sequence.sequence.value(), true));
    stage.update(epoch + 6ms);
    continuation_time = epoch + 7ms;
    transport.queue(remote,
                    server_packet(4U, false,
                                  response.header.sequence.sequence.value(),
                                  true, service_envelope(semantic)));
    stage.update(continuation_time);
    break;
  }
  }
  stage.update(continuation_time + 1ms);
  stage.update(continuation_time + 2ms);
  REQUIRE(stage.state() ==
          goldsrc::PrecacheAssetDispatchStageState::selecting_world_entry);
  REQUIRE(stage.transmitted_packet_count_at_manifest_publication());
  if (events != nullptr) {
    drain_events(stage, *events);
  }
  return ManifestBoundary{continuation_time + 3ms, transport.sent.size(),
                          response_transmit_count};
}

void drive_to_terminal(
    goldsrc::PrecacheAssetDispatchStage &stage,
    goldsrc::PrecacheAssetDispatchStageTimePoint now,
    std::vector<goldsrc::PrecacheAssetDispatchStageEvent> &events) {
  for (std::size_t update = 0U; update < 256U && !stage.terminal(); ++update) {
    stage.update(now);
    drain_events(stage, events);
    now += 1ms;
  }
  REQUIRE(stage.terminal());
}

[[nodiscard]] std::shared_ptr<const local::LocalResourceEnvironment>
shared_environment(const hlclient::tests::ScopedLocalResourceTestRoot &root) {
  auto environment = readiness_fixture::make_environment(root);
  return std::shared_ptr<const local::LocalResourceEnvironment>{
      std::move(environment)};
}

[[nodiscard]] std::unique_ptr<goldsrc::GoldSrcHandshakeCoordinator>
historical_coordinator(
    FakeTransport &transport, const network::NetworkAddress remote,
    const goldsrc::HandshakeStopPoint stop_point,
    PreparedWithAuthenticationSession prepared,
    consistency::IResourceConsistencyProvider *provider,
    std::shared_ptr<const local::LocalResourceEnvironment> environment,
    const assets::AssetImporterRegistries &registries,
    std::size_t &source_open_starts) {
  auto asset_config = test_config();
  const auto resource_response_config = asset_config.manifest.response;
  return std::unique_ptr<goldsrc::GoldSrcHandshakeCoordinator>{
      new goldsrc::GoldSrcHandshakeCoordinator{
          transport,
          remote,
          stop_point,
          std::move(prepared.request),
          coordinator_challenge_config(),
          goldsrc::ChallengeTraceCallback{},
          goldsrc::ConnectRequestTraceCallback{},
          coordinator_response_config(),
          goldsrc::ConnectResponseTraceCallback{},
          std::move(prepared.session),
          goldsrc::NetchanBootstrapConfig{},
          goldsrc::NetchanBootstrapTraceCallback{},
          goldsrc::InitialSignonConfig{},
          goldsrc::InitialSignonTraceCallback{},
          goldsrc::PreResourceSignonConfig{},
          goldsrc::PreResourceSignonTraceCallback{},
          goldsrc::DeltaDescriptionStageConfig{},
          goldsrc::DeltaDescriptionTraceCallback{},
          goldsrc::MovementEnvironmentStageConfig{},
          goldsrc::MovementEnvironmentTraceCallback{},
          goldsrc::UserInfoSignonStageConfig{},
          goldsrc::UserInfoSignonTraceCallback{},
          goldsrc::ResourceTransitionStageConfig{},
          goldsrc::ResourceTransitionTraceCallback{},
          goldsrc::ResourceListStageConfig{},
          goldsrc::ResourceListTraceCallback{},
          resource_response_config,
          provider,
          goldsrc::ResourceClientResponseTraceCallback{},
          std::move(environment),
          goldsrc::PrecacheManifestStageConfig{},
          goldsrc::PrecacheManifestTraceCallback{},
          &registries,
          std::move(asset_config),
          [&source_open_starts](
              const goldsrc::PrecacheAssetDispatchTraceEvent &event) {
            if (event.classification ==
                goldsrc::PrecacheAssetDispatchTraceClassification::
                    asset_source_open_started) {
              ++source_open_starts;
            }
          }}};
}

[[nodiscard]] std::size_t
event_count(const std::vector<goldsrc::PrecacheAssetDispatchStageEvent> &events,
            const goldsrc::PrecacheAssetDispatchStageEventType type) {
  return static_cast<std::size_t>(std::ranges::count(
      events, type, &goldsrc::PrecacheAssetDispatchStageEvent::type));
}

class DispatchStageHarness final {
public:
  DispatchStageHarness(
      std::shared_ptr<const local::LocalResourceEnvironment> environment,
      const assets::AssetImporterRegistries &registries,
      goldsrc::PrecacheAssetDispatchStageConfig config = test_config())
      : stage{transport,         remote,   std::move(environment), registries,
              std::move(config), &provider} {}

  [[nodiscard]] ManifestBoundary reach_manifest(
      const std::span<const resource_list_test_fixture::EntrySpec> entries,
      const bool drain_outer_events = true,
      const ResponseLossMode loss_mode = ResponseLossMode::none) {
    return drive_to_manifest_boundary(
        stage, transport, remote, entries, epoch, connection_releases,
        drain_outer_events ? &events : nullptr, loss_mode);
  }

  void finish(const goldsrc::PrecacheAssetDispatchStageTimePoint now) {
    drive_to_terminal(stage, now, events);
  }

  FakeTransport transport;
  network::NetworkAddress remote{network::NetworkAddress::loopback(27'016U)};
  ImmediateConsistencyProvider provider;
  std::size_t connection_releases{0U};
  std::vector<goldsrc::PrecacheAssetDispatchStageEvent> events;
  const goldsrc::PrecacheAssetDispatchStageTimePoint epoch{};
  goldsrc::PrecacheAssetDispatchStage stage;
};

void write_complete_resources(
    const hlclient::tests::ScopedLocalResourceTestRoot &root,
    const std::span<const std::byte> world_bytes) {
  root.write("valve", "maps/test_alpha.bsp", world_bytes);
  root.write("valve", "models/test_model.mdl", "model");
  root.write("valve", "sound/test_sound.wav", "sound");
}

void check_post_manifest_terminal_invariants(
    const DispatchStageHarness &harness, const ManifestBoundary &boundary) {
  CHECK(harness.stage.cleanup_count() == 1U);
  CHECK(harness.connection_releases == 1U);
  CHECK(harness.provider.begin_count == 1U);
  CHECK(harness.provider.update_count == 1U);
  CHECK(harness.provider.cancel_count == 0U);
  CHECK(harness.provider.lifetime_releases == 1U);
  CHECK(harness.stage.remote_endpoint() == harness.remote);
  REQUIRE(harness.stage.local_endpoint());
  CHECK(*harness.stage.local_endpoint() == harness.transport.local);
  CHECK(harness.transport.sent.size() == boundary.sent_datagram_count);
  CHECK(std::ranges::all_of(harness.transport.sent,
                            [&harness](const SentDatagram &datagram) {
                              return datagram.destination == harness.remote;
                            }));
  REQUIRE(harness.stage.transmitted_packet_count_at_manifest_publication());
  CHECK(harness.stage.transmitted_packet_count() ==
        *harness.stage.transmitted_packet_count_at_manifest_publication());
  CHECK(harness.stage.transmitted_packet_count() ==
        boundary.sent_datagram_count);
}

const std::array kCompleteEntries{
    resource_list_test_fixture::EntrySpec{2U, "maps/test_alpha.bsp", 37U,
                                          0x00ff'ffffU, 0U},
    resource_list_test_fixture::EntrySpec{2U, "models/test_model.mdl", 9U, 1U,
                                          0U},
    resource_list_test_fixture::EntrySpec{0U, "test_sound.wav", 4U, 2U, 0U},
};

const std::array kWorldOnlyEntry{
    resource_list_test_fixture::EntrySpec{2U, "maps/test_alpha.bsp", 37U, 0U,
                                          0U},
};

TEST_CASE("Precache asset dispatch reaches the production importer boundary "
          "for a complete manifest",
          "[goldsrc][asset-dispatch][stage][boundary]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>(65U);
  write_complete_resources(root, world_bytes);
  assets::AssetImporterRegistries registries;
  DispatchStageHarness harness{shared_environment(root), registries};

  const auto boundary = harness.reach_manifest(kCompleteEntries);
  harness.finish(boundary.next_update);

  CHECK(harness.stage.state() ==
        goldsrc::PrecacheAssetDispatchStageState::importer_boundary_reached);
  REQUIRE(harness.stage.result());
  CHECK_FALSE(harness.stage.error());
  const auto &result = *harness.stage.result();
  CHECK(result.manifest().completeness() ==
        goldsrc::PrecacheManifestCompleteness::
            complete_for_supported_local_profile);
  CHECK(result.manifest().world_geometry_ready());
  CHECK(result.plan().selected_world());
  CHECK(result.plan().role() == assets::AssetDispatchRole::world);
  CHECK(result.plan().wire_ordinal() == 0U);
  CHECK(result.plan().resource_type() == goldsrc::ResourceType::model);
  CHECK(result.plan().resource_index() == 37U);
  CHECK(result.dispatch_result().state ==
        assets::AssetDispatchState::importer_not_registered);
  CHECK_FALSE(result.imported_asset());
  CHECK(result.source_byte_count() == world_bytes.size());
  CHECK(std::ranges::equal(result.source().source().bytes(), world_bytes));
  CHECK(result.source().source().virtual_path().generic_string() ==
        "maps/test_alpha.bsp");
  CHECK(harness.stage.manifest_publication_count() == 1U);
  CHECK(harness.stage.source_open_attempt_count() == 1U);
  CHECK(harness.stage.importer_dispatch_count() == 1U);
  CHECK(event_count(harness.events,
                    goldsrc::PrecacheAssetDispatchStageEventType::
                        precache_manifest_ready) == 1U);
  CHECK(
      event_count(
          harness.events,
          goldsrc::PrecacheAssetDispatchStageEventType::world_entry_selected) ==
      1U);
  CHECK(event_count(
            harness.events,
            goldsrc::PrecacheAssetDispatchStageEventType::asset_source_ready) ==
        1U);
  CHECK(event_count(harness.events,
                    goldsrc::PrecacheAssetDispatchStageEventType::
                        importer_boundary_reached) == 1U);
  check_post_manifest_terminal_invariants(harness, boundary);
}

TEST_CASE(
    "Precache asset dispatch accepts a ready world from an incomplete manifest",
    "[goldsrc][asset-dispatch][stage][incomplete]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>();
  root.write("valve", "maps/test_alpha.bsp", world_bytes);
  assets::AssetImporterRegistries registries;
  DispatchStageHarness harness{shared_environment(root), registries};

  const auto boundary = harness.reach_manifest(kCompleteEntries);
  harness.finish(boundary.next_update);

  CHECK(harness.stage.state() ==
        goldsrc::PrecacheAssetDispatchStageState::importer_boundary_reached);
  REQUIRE(harness.stage.result());
  CHECK(harness.stage.result()->manifest().completeness() ==
        goldsrc::PrecacheManifestCompleteness::world_ready_but_incomplete);
  CHECK(harness.stage.result()->manifest().world_geometry_ready());
  CHECK(harness.stage.result()->dispatch_result().state ==
        assets::AssetDispatchState::importer_not_registered);
  CHECK(harness.stage.source_open_attempt_count() == 1U);
  CHECK(harness.stage.importer_dispatch_count() == 1U);
  check_post_manifest_terminal_invariants(harness, boundary);
}

TEST_CASE(
    "Precache asset dispatch incrementally imports only the selected world",
    "[goldsrc][asset-dispatch][stage][import]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>(65U);
  write_complete_resources(root, world_bytes);
  synthetic::SyntheticImporterCounts counts;
  assets::AssetImporterRegistries registries;
  REQUIRE(registries.worlds.register_importer(
      std::make_unique<synthetic::SyntheticWorldImporter>("synthetic-world",
                                                          counts)));
  auto config = test_config();
  config.source_open.read_chunk_bytes = 4U;
  config.source_open.maximum_chunks_per_update = 1U;
  DispatchStageHarness harness{shared_environment(root), registries,
                               std::move(config)};

  const auto boundary = harness.reach_manifest(kCompleteEntries);
  harness.finish(boundary.next_update);

  CHECK(harness.stage.state() ==
        goldsrc::PrecacheAssetDispatchStageState::asset_imported);
  REQUIRE(harness.stage.result());
  CHECK_FALSE(harness.stage.error());
  const auto &result = *harness.stage.result();
  CHECK(result.dispatch_result().state == assets::AssetDispatchState::imported);
  CHECK(result.dispatch_result().selected_category ==
        assets::AssetImporterCategory::world);
  CHECK(result.dispatch_result().selected_importer_id ==
        "world:synthetic-world");
  REQUIRE(result.imported_asset());
  const auto *world =
      std::get_if<assets::WorldAsset>(&*result.imported_asset());
  REQUIRE(world);
  CHECK(world->identity.source_name == "synthetic-world");
  CHECK(world->vertices.size() == 3U);
  CHECK(counts.probe_count == 1U);
  CHECK(counts.import_count == 1U);
  CHECK(harness.stage.source_open_attempt_count() == 1U);
  CHECK(harness.stage.importer_dispatch_count() == 1U);
  CHECK(
      event_count(
          harness.events,
          goldsrc::PrecacheAssetDispatchStageEventType::asset_source_progress) >
      1U);
  CHECK(event_count(
            harness.events,
            goldsrc::PrecacheAssetDispatchStageEventType::asset_source_ready) ==
        1U);
  CHECK(event_count(harness.events,
                    goldsrc::PrecacheAssetDispatchStageEventType::
                        importer_probe_completed) == 1U);
  CHECK(event_count(
            harness.events,
            goldsrc::PrecacheAssetDispatchStageEventType::importer_selected) ==
        1U);
  CHECK(event_count(
            harness.events,
            goldsrc::PrecacheAssetDispatchStageEventType::asset_imported) ==
        1U);
  check_post_manifest_terminal_invariants(harness, boundary);

  const auto sent = harness.transport.sent.size();
  const auto cleanup = harness.stage.cleanup_count();
  const auto releases = harness.connection_releases;
  const auto consistency_releases = harness.provider.lifetime_releases;
  const auto source_opens = harness.stage.source_open_attempt_count();
  const auto dispatches = harness.stage.importer_dispatch_count();
  harness.stage.update(boundary.next_update + 5s);
  harness.stage.cancel(boundary.next_update + 6s);
  harness.stage.cancel(boundary.next_update + 7s);
  CHECK(harness.stage.state() ==
        goldsrc::PrecacheAssetDispatchStageState::asset_imported);
  CHECK(harness.transport.sent.size() == sent);
  CHECK(harness.stage.cleanup_count() == cleanup);
  CHECK(harness.connection_releases == releases);
  CHECK(harness.provider.lifetime_releases == consistency_releases);
  CHECK(harness.stage.source_open_attempt_count() == source_opens);
  CHECK(harness.stage.importer_dispatch_count() == dispatches);
  CHECK(counts.probe_count == 1U);
  CHECK(counts.import_count == 1U);
}

TEST_CASE("Importer callbacks cannot re-enter asset dispatch stage mutation",
          "[goldsrc][asset-dispatch][stage][reentry]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>(65U);
  write_complete_resources(root, world_bytes);
  synthetic::SyntheticImporterCounts counts;
  std::size_t probe_reentries = 0U;
  std::size_t import_reentries = 0U;
  goldsrc::PrecacheAssetDispatchStage *stage_under_test = nullptr;
  const auto reentry_time =
      goldsrc::PrecacheAssetDispatchStageTimePoint{} + 10s;
  const auto probe_reentry = [&] {
    ++probe_reentries;
    if (stage_under_test != nullptr) {
      stage_under_test->cancel(reentry_time);
      stage_under_test->update(reentry_time);
    }
  };
  const auto import_reentry = [&] {
    ++import_reentries;
    if (stage_under_test != nullptr) {
      stage_under_test->update(reentry_time);
      stage_under_test->cancel(reentry_time);
    }
  };
  assets::AssetImporterRegistries registries;
  REQUIRE(registries.worlds.register_importer(
      std::make_unique<synthetic::SyntheticWorldImporter>(
          "reentrant-world", counts, assets::AssetProbeConfidence{100U},
          synthetic::SyntheticImportBehavior::success, probe_reentry,
          import_reentry)));
  DispatchStageHarness harness{shared_environment(root), registries};
  stage_under_test = &harness.stage;

  const auto boundary = harness.reach_manifest(kCompleteEntries);
  harness.finish(boundary.next_update);

  REQUIRE(harness.stage.state() ==
          goldsrc::PrecacheAssetDispatchStageState::asset_imported);
  REQUIRE(harness.stage.result());
  CHECK_FALSE(harness.stage.error());
  CHECK(harness.stage.result()->dispatch_result().state ==
        assets::AssetDispatchState::imported);
  CHECK(probe_reentries == 1U);
  CHECK(import_reentries == 1U);
  CHECK(counts.probe_count == 1U);
  CHECK(counts.import_count == 1U);
  CHECK(harness.stage.source_open_attempt_count() == 1U);
  CHECK(harness.stage.importer_dispatch_count() == 1U);
  CHECK(event_count(harness.events,
                    goldsrc::PrecacheAssetDispatchStageEventType::cancelled) ==
        0U);
  CHECK(event_count(
            harness.events,
            goldsrc::PrecacheAssetDispatchStageEventType::asset_imported) ==
        1U);
  check_post_manifest_terminal_invariants(harness, boundary);
}

TEST_CASE("Precache asset dispatch rejects equally ranked world importers",
          "[goldsrc][asset-dispatch][stage][ambiguous]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>();
  write_complete_resources(root, world_bytes);
  synthetic::SyntheticImporterCounts first_counts;
  synthetic::SyntheticImporterCounts second_counts;
  assets::AssetImporterRegistries registries;
  REQUIRE(registries.worlds.register_importer(
      std::make_unique<synthetic::SyntheticWorldImporter>("world-alpha",
                                                          first_counts)));
  REQUIRE(registries.worlds.register_importer(
      std::make_unique<synthetic::SyntheticWorldImporter>("world-bravo",
                                                          second_counts)));
  DispatchStageHarness harness{shared_environment(root), registries};

  const auto boundary = harness.reach_manifest(kCompleteEntries);
  harness.finish(boundary.next_update);

  CHECK(harness.stage.state() ==
        goldsrc::PrecacheAssetDispatchStageState::ambiguous_importer);
  CHECK_FALSE(harness.stage.result());
  REQUIRE(harness.stage.error());
  CHECK(harness.stage.error()->code ==
        goldsrc::PrecacheAssetDispatchStageErrorCode::ambiguous_importer);
  CHECK(harness.stage.error()->dispatch_state ==
        assets::AssetDispatchState::ambiguous_importer);
  CHECK(harness.stage.error()->asset_code ==
        assets::AssetErrorCode::AmbiguousFormat);
  CHECK(first_counts.probe_count == 1U);
  CHECK(second_counts.probe_count == 1U);
  CHECK(first_counts.import_count == 0U);
  CHECK(second_counts.import_count == 0U);
  CHECK(harness.stage.importer_dispatch_count() == 1U);
  CHECK(event_count(
            harness.events,
            goldsrc::PrecacheAssetDispatchStageEventType::ambiguous_importer) ==
        1U);
  check_post_manifest_terminal_invariants(harness, boundary);
}

TEST_CASE("Precache asset dispatch contains malformed failures and importer "
          "exceptions",
          "[goldsrc][asset-dispatch][stage][failure]") {
  const std::array cases{
      std::pair{synthetic::SyntheticImportBehavior::malformed_data,
                assets::AssetErrorCode::MalformedData},
      std::pair{synthetic::SyntheticImportBehavior::import_failure,
                assets::AssetErrorCode::ImportFailed},
      std::pair{synthetic::SyntheticImportBehavior::standard_exception,
                assets::AssetErrorCode::ImportFailed},
      std::pair{synthetic::SyntheticImportBehavior::unknown_exception,
                assets::AssetErrorCode::ImportFailed},
  };

  for (const auto &[behavior, expected_error] : cases) {
    CAPTURE(behavior);
    hlclient::tests::ScopedLocalResourceTestRoot root;
    const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>();
    write_complete_resources(root, world_bytes);
    synthetic::SyntheticImporterCounts counts;
    assets::AssetImporterRegistries registries;
    REQUIRE(registries.worlds.register_importer(
        std::make_unique<synthetic::SyntheticWorldImporter>(
            "failing-world", counts, assets::AssetProbeConfidence{100U},
            behavior)));
    DispatchStageHarness harness{shared_environment(root), registries};

    const auto boundary = harness.reach_manifest(kCompleteEntries);
    harness.finish(boundary.next_update);

    CHECK(harness.stage.state() ==
          goldsrc::PrecacheAssetDispatchStageState::import_failed);
    CHECK_FALSE(harness.stage.result());
    REQUIRE(harness.stage.error());
    CHECK(harness.stage.error()->code ==
          goldsrc::PrecacheAssetDispatchStageErrorCode::import_failed);
    CHECK(harness.stage.error()->dispatch_state ==
          assets::AssetDispatchState::import_failed);
    CHECK(harness.stage.error()->asset_code == expected_error);
    CHECK(counts.probe_count == 1U);
    CHECK(counts.import_count == 1U);
    CHECK(harness.stage.importer_dispatch_count() == 1U);
    CHECK(event_count(
              harness.events,
              goldsrc::PrecacheAssetDispatchStageEventType::import_failed) ==
          1U);
    check_post_manifest_terminal_invariants(harness, boundary);
  }
}

void check_world_source_unavailable(const DispatchStageHarness &harness,
                                    const ManifestBoundary &boundary) {
  CHECK(harness.stage.state() ==
        goldsrc::PrecacheAssetDispatchStageState::world_source_unavailable);
  CHECK_FALSE(harness.stage.result());
  REQUIRE(harness.stage.error());
  CHECK(harness.stage.error()->code ==
        goldsrc::PrecacheAssetDispatchStageErrorCode::world_source_unavailable);
  CHECK(harness.stage.source_open_attempt_count() == 0U);
  CHECK(harness.stage.importer_dispatch_count() == 0U);
  CHECK(event_count(harness.events,
                    goldsrc::PrecacheAssetDispatchStageEventType::
                        world_source_unavailable) == 1U);
  check_post_manifest_terminal_invariants(harness, boundary);
}

TEST_CASE(
    "Precache asset dispatch stops before opening an unavailable world source",
    "[goldsrc][asset-dispatch][stage][unavailable]") {
  SECTION("missing world and absent locator") {
    hlclient::tests::ScopedLocalResourceTestRoot root;
    assets::AssetImporterRegistries registries;
    DispatchStageHarness harness{shared_environment(root), registries};

    const auto boundary = harness.reach_manifest(kWorldOnlyEntry);
    harness.finish(boundary.next_update);

    check_world_source_unavailable(harness, boundary);
  }

  SECTION("directory at the world path is a local I/O failure") {
    hlclient::tests::ScopedLocalResourceTestRoot root;
    REQUIRE(std::filesystem::create_directories(root.game_path("valve") /
                                                "maps/test_alpha.bsp"));
    assets::AssetImporterRegistries registries;
    DispatchStageHarness harness{shared_environment(root), registries};

    const auto boundary = harness.reach_manifest(kWorldOnlyEntry);
    harness.finish(boundary.next_update);

    check_world_source_unavailable(harness, boundary);
  }

  SECTION("reparse world is unsafe") {
    hlclient::tests::ScopedLocalResourceTestRoot root;
    const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>();
    root.write("valve", "maps/test_alpha.bsp", world_bytes);
    const auto world_path = root.game_path("valve") / "maps/test_alpha.bsp";
    const auto retained_path =
        root.game_path("valve") / "maps/retained_alpha.bsp";
    std::filesystem::rename(world_path, retained_path);
    std::error_code error;
    std::filesystem::create_symlink(retained_path, world_path, error);
    if (error) {
      SKIP("File symlinks are unavailable: " << error.message());
    }
    assets::AssetImporterRegistries registries;
    DispatchStageHarness harness{shared_environment(root), registries};

    const auto boundary = harness.reach_manifest(kWorldOnlyEntry);
    harness.finish(boundary.next_update);

    check_world_source_unavailable(harness, boundary);
  }

  SECTION("case-ambiguous world has no approved locator") {
    hlclient::tests::ScopedLocalResourceTestRoot root;
    const auto maps = root.game_path("valve") / "maps";
    REQUIRE(std::filesystem::create_directories(maps));
    if (!hlclient::tests::enable_case_sensitive_directory(maps)) {
      SKIP("Case-sensitive directory mode is unavailable");
    }
    const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>();
    root.write("valve", "maps/Test_Alpha.bsp", world_bytes);
    root.write("valve", "maps/TEST_ALPHA.BSP", world_bytes);
    const auto entry_count = static_cast<std::size_t>(
        std::ranges::distance(std::filesystem::directory_iterator{maps},
                              std::filesystem::directory_iterator{}));
    if (entry_count < 2U) {
      SKIP("Case-distinct directory entries are unavailable");
    }
    assets::AssetImporterRegistries registries;
    DispatchStageHarness harness{shared_environment(root), registries};

    const auto boundary = harness.reach_manifest(kWorldOnlyEntry);
    harness.finish(boundary.next_update);

    check_world_source_unavailable(harness, boundary);
  }
}

TEST_CASE("Precache asset dispatch rejects a stale selected-world locator",
          "[goldsrc][asset-dispatch][stage][stale-locator]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto original_bytes = synthetic::source_bytes<assets::WorldAsset>(32U);
  root.write("valve", "maps/test_alpha.bsp", original_bytes);
  assets::AssetImporterRegistries registries;
  DispatchStageHarness harness{shared_environment(root), registries};

  const auto boundary = harness.reach_manifest(kWorldOnlyEntry);
  const auto world_path = root.game_path("valve") / "maps/test_alpha.bsp";
  std::filesystem::rename(world_path,
                          root.game_path("valve") / "maps/original_alpha.bsp");
  const auto replacement_bytes =
      synthetic::source_bytes<assets::WorldAsset>(33U);
  root.write("valve", "maps/test_alpha.bsp", replacement_bytes);
  harness.finish(boundary.next_update);

  CHECK(harness.stage.state() ==
        goldsrc::PrecacheAssetDispatchStageState::source_open_failed);
  CHECK_FALSE(harness.stage.result());
  REQUIRE(harness.stage.error());
  CHECK(harness.stage.error()->code ==
        goldsrc::PrecacheAssetDispatchStageErrorCode::source_open_failed);
  CHECK(harness.stage.error()->source_open_code ==
        goldsrc::ApprovedAssetSourceOpenErrorCode::stale_locator);
  CHECK(harness.stage.error()->local_source_open_code ==
        hlclient::local_assets::LocalAssetSourceOpenErrorCode::stale_locator);
  CHECK(harness.stage.error()->locator_reopen_code ==
        local::LocalResourceLocatorReopenErrorCode::stale_locator);
  CHECK(harness.stage.source_open_attempt_count() == 1U);
  CHECK(harness.stage.importer_dispatch_count() == 0U);
  CHECK(event_count(
            harness.events,
            goldsrc::PrecacheAssetDispatchStageEventType::source_open_failed) ==
        1U);
  check_post_manifest_terminal_invariants(harness, boundary);
}

TEST_CASE("Precache asset dispatch cancellation is terminal and idempotent",
          "[goldsrc][asset-dispatch][stage][cancel]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>();
  root.write("valve", "maps/test_alpha.bsp", world_bytes);
  assets::AssetImporterRegistries registries;
  DispatchStageHarness harness{shared_environment(root), registries};

  const auto boundary = harness.reach_manifest(kWorldOnlyEntry);
  harness.stage.update(boundary.next_update);
  drain_events(harness.stage, harness.events);
  harness.stage.update(boundary.next_update + 1ms);
  drain_events(harness.stage, harness.events);
  REQUIRE(harness.stage.state() ==
          goldsrc::PrecacheAssetDispatchStageState::opening_asset_source);
  REQUIRE(harness.stage.source_open_attempt_count() == 1U);

  harness.stage.cancel(boundary.next_update + 2ms);
  drain_events(harness.stage, harness.events);
  harness.stage.cancel(boundary.next_update + 3ms);
  harness.stage.update(boundary.next_update + 4ms);

  CHECK(harness.stage.state() ==
        goldsrc::PrecacheAssetDispatchStageState::cancelled);
  CHECK_FALSE(harness.stage.result());
  CHECK_FALSE(harness.stage.error());
  CHECK(harness.stage.importer_dispatch_count() == 0U);
  CHECK(event_count(harness.events,
                    goldsrc::PrecacheAssetDispatchStageEventType::cancelled) ==
        1U);
  check_post_manifest_terminal_invariants(harness, boundary);
}

TEST_CASE("Precache asset dispatch bounds local source opening with a timeout",
          "[goldsrc][asset-dispatch][stage][timeout]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>(65U);
  root.write("valve", "maps/test_alpha.bsp", world_bytes);
  assets::AssetImporterRegistries registries;
  auto config = test_config();
  config.source_open.read_chunk_bytes = 1U;
  config.source_open.maximum_chunks_per_update = 1U;
  config.source_open.timeout = 1ms;
  DispatchStageHarness harness{shared_environment(root), registries,
                               std::move(config)};

  const auto boundary = harness.reach_manifest(kWorldOnlyEntry);
  harness.stage.update(boundary.next_update);
  drain_events(harness.stage, harness.events);
  harness.stage.update(boundary.next_update + 1ms);
  drain_events(harness.stage, harness.events);
  harness.stage.update(boundary.next_update + 2ms);
  drain_events(harness.stage, harness.events);
  harness.stage.update(boundary.next_update + 4ms);
  drain_events(harness.stage, harness.events);

  REQUIRE(harness.stage.terminal());
  CHECK(harness.stage.state() ==
        goldsrc::PrecacheAssetDispatchStageState::timed_out);
  CHECK_FALSE(harness.stage.result());
  REQUIRE(harness.stage.error());
  CHECK(harness.stage.error()->code ==
        goldsrc::PrecacheAssetDispatchStageErrorCode::source_open_failed);
  CHECK(harness.stage.error()->source_open_code ==
        goldsrc::ApprovedAssetSourceOpenErrorCode::timed_out);
  CHECK(harness.stage.error()->local_source_open_code ==
        hlclient::local_assets::LocalAssetSourceOpenErrorCode::timed_out);
  CHECK(harness.stage.source_open_attempt_count() == 1U);
  CHECK(harness.stage.importer_dispatch_count() == 0U);
  CHECK(event_count(harness.events,
                    goldsrc::PrecacheAssetDispatchStageEventType::timeout) ==
        1U);
  check_post_manifest_terminal_invariants(harness, boundary);
}

TEST_CASE("Precache asset dispatch rejects an oversized selected world before "
          "import",
          "[goldsrc][asset-dispatch][stage][source-limit]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>(32U);
  root.write("valve", "maps/test_alpha.bsp", world_bytes);
  assets::AssetImporterRegistries registries;
  auto config = test_config();
  config.source_open.maximum_source_bytes = 8U;
  DispatchStageHarness harness{shared_environment(root), registries,
                               std::move(config)};

  const auto boundary = harness.reach_manifest(kWorldOnlyEntry);
  harness.finish(boundary.next_update);

  REQUIRE(harness.stage.state() ==
          goldsrc::PrecacheAssetDispatchStageState::source_open_failed);
  REQUIRE(harness.stage.error());
  CHECK(harness.stage.error()->source_open_code ==
        goldsrc::ApprovedAssetSourceOpenErrorCode::source_too_large);
  CHECK(
      harness.stage.error()->local_source_open_code ==
      hlclient::local_assets::LocalAssetSourceOpenErrorCode::source_too_large);
  CHECK(harness.stage.source_open_attempt_count() == 1U);
  CHECK(harness.stage.importer_dispatch_count() == 0U);
  check_post_manifest_terminal_invariants(harness, boundary);
}

TEST_CASE("Precache asset dispatch applies bounded outer-event backpressure",
          "[goldsrc][asset-dispatch][stage][backpressure]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>();
  root.write("valve", "maps/test_alpha.bsp", world_bytes);
  assets::AssetImporterRegistries registries;
  auto config = test_config();
  config.maximum_stage_events = 3U;
  DispatchStageHarness harness{shared_environment(root), registries,
                               std::move(config)};

  const auto boundary = harness.reach_manifest(kWorldOnlyEntry, false);
  REQUIRE(harness.stage.pending_event_count() == 1U);
  auto now = boundary.next_update;
  for (std::size_t update = 0U; update < 8U && !harness.stage.terminal();
       ++update) {
    harness.stage.update(now);
    now += 1ms;
  }

  REQUIRE(harness.stage.terminal());
  CHECK(harness.stage.state() ==
        goldsrc::PrecacheAssetDispatchStageState::backpressure);
  CHECK_FALSE(harness.stage.result());
  REQUIRE(harness.stage.error());
  CHECK(harness.stage.error()->code ==
        goldsrc::PrecacheAssetDispatchStageErrorCode::event_backpressure);
  CHECK(harness.stage.source_open_attempt_count() == 1U);
  CHECK(harness.stage.importer_dispatch_count() == 0U);
  drain_events(harness.stage, harness.events);
  CHECK(event_count(harness.events,
                    goldsrc::PrecacheAssetDispatchStageEventType::
                        precache_manifest_ready) == 1U);
  CHECK(
      event_count(
          harness.events,
          goldsrc::PrecacheAssetDispatchStageEventType::world_entry_selected) ==
      1U);
  CHECK(event_count(harness.events,
                    goldsrc::PrecacheAssetDispatchStageEventType::
                        asset_source_open_started) == 1U);
  CHECK(event_count(
            harness.events,
            goldsrc::PrecacheAssetDispatchStageEventType::backpressure) == 0U);
  check_post_manifest_terminal_invariants(harness, boundary);
}

TEST_CASE("Synthetic world dispatch is stable across 20 fake-HLDS runs",
          "[goldsrc][asset-dispatch][stage][integration][repeat-20]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>(65U);
  write_complete_resources(root, world_bytes);
  const auto environment = shared_environment(root);
  synthetic::SyntheticImporterCounts counts;
  assets::AssetImporterRegistries registries;
  REQUIRE(registries.worlds.register_importer(
      std::make_unique<synthetic::SyntheticWorldImporter>("repeat-world",
                                                          counts)));

  for (std::size_t run = 0U; run < 20U; ++run) {
    INFO("synthetic world-import run=" << run);
    auto config = test_config();
    config.source_open.read_chunk_bytes = 4U;
    config.source_open.maximum_chunks_per_update = 1U;
    DispatchStageHarness harness{environment, registries, std::move(config)};
    const auto boundary = harness.reach_manifest(kCompleteEntries);
    harness.finish(boundary.next_update);

    REQUIRE(harness.stage.state() ==
            goldsrc::PrecacheAssetDispatchStageState::asset_imported);
    REQUIRE(harness.stage.result());
    REQUIRE(harness.stage.result()->imported_asset());
    CHECK(std::holds_alternative<assets::WorldAsset>(
        *harness.stage.result()->imported_asset()));
    CHECK(harness.stage.manifest_publication_count() == 1U);
    CHECK(harness.stage.source_open_attempt_count() == 1U);
    CHECK(harness.stage.importer_dispatch_count() == 1U);
    CHECK(counts.probe_count == run + 1U);
    CHECK(counts.import_count == run + 1U);
    CHECK(boundary.resource_response_transmit_count == 1U);
    check_post_manifest_terminal_invariants(harness, boundary);
  }
}

TEST_CASE("No-importer dispatch boundary is stable across 20 fake-HLDS runs",
          "[goldsrc][asset-dispatch][stage][integration][repeat-20]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>();
  write_complete_resources(root, world_bytes);
  const auto environment = shared_environment(root);
  assets::AssetImporterRegistries registries;

  for (std::size_t run = 0U; run < 20U; ++run) {
    INFO("no-importer boundary run=" << run);
    DispatchStageHarness harness{environment, registries};
    const auto boundary = harness.reach_manifest(kCompleteEntries);
    harness.finish(boundary.next_update);

    REQUIRE(
        harness.stage.state() ==
        goldsrc::PrecacheAssetDispatchStageState::importer_boundary_reached);
    REQUIRE(harness.stage.result());
    CHECK(harness.stage.result()->dispatch_result().state ==
          assets::AssetDispatchState::importer_not_registered);
    CHECK(harness.stage.manifest_publication_count() == 1U);
    CHECK(harness.stage.source_open_attempt_count() == 1U);
    CHECK(harness.stage.importer_dispatch_count() == 1U);
    CHECK(boundary.resource_response_transmit_count == 1U);
    check_post_manifest_terminal_invariants(harness, boundary);
  }
}

TEST_CASE("World-ready incomplete dispatch is stable across 20 fake-HLDS runs",
          "[goldsrc][asset-dispatch][stage][integration][repeat-20]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>();
  root.write("valve", "maps/test_alpha.bsp", world_bytes);
  const auto environment = shared_environment(root);
  assets::AssetImporterRegistries registries;

  for (std::size_t run = 0U; run < 20U; ++run) {
    INFO("world-ready incomplete run=" << run);
    DispatchStageHarness harness{environment, registries};
    const auto boundary = harness.reach_manifest(kCompleteEntries);
    harness.finish(boundary.next_update);

    REQUIRE(
        harness.stage.state() ==
        goldsrc::PrecacheAssetDispatchStageState::importer_boundary_reached);
    REQUIRE(harness.stage.result());
    CHECK(harness.stage.result()->manifest().completeness() ==
          goldsrc::PrecacheManifestCompleteness::world_ready_but_incomplete);
    CHECK(harness.stage.result()->manifest().world_geometry_ready());
    CHECK(harness.stage.manifest_publication_count() == 1U);
    CHECK(harness.stage.source_open_attempt_count() == 1U);
    CHECK(harness.stage.importer_dispatch_count() == 1U);
    CHECK(boundary.resource_response_transmit_count == 1U);
    check_post_manifest_terminal_invariants(harness, boundary);
  }
}

TEST_CASE("Stale replaced-world locators fail closed across 20 fake-HLDS runs",
          "[goldsrc][asset-dispatch][stage][integration][repeat-20]") {
  for (std::size_t run = 0U; run < 20U; ++run) {
    INFO("stale replaced-world run=" << run);
    hlclient::tests::ScopedLocalResourceTestRoot root;
    const auto original_bytes =
        synthetic::source_bytes<assets::WorldAsset>(32U + run);
    root.write("valve", "maps/test_alpha.bsp", original_bytes);
    assets::AssetImporterRegistries registries;
    DispatchStageHarness harness{shared_environment(root), registries};
    const auto boundary = harness.reach_manifest(kWorldOnlyEntry);

    const auto world_path = root.game_path("valve") / "maps/test_alpha.bsp";
    const auto replacement_bytes =
        synthetic::source_bytes<assets::WorldAsset>(65U + run);
    if (run % 2U == 0U) {
      std::filesystem::rename(world_path, root.game_path("valve") /
                                              "maps/original_alpha.bsp");
      root.write("valve", "maps/test_alpha.bsp", replacement_bytes);
    } else {
      root.write("valve", "maps/test_alpha.bsp", replacement_bytes);
    }
    harness.finish(boundary.next_update);

    REQUIRE(harness.stage.state() ==
            goldsrc::PrecacheAssetDispatchStageState::source_open_failed);
    REQUIRE(harness.stage.error());
    CHECK(harness.stage.error()->source_open_code ==
          goldsrc::ApprovedAssetSourceOpenErrorCode::stale_locator);
    CHECK(harness.stage.manifest_publication_count() == 1U);
    CHECK(harness.stage.source_open_attempt_count() == 1U);
    CHECK(harness.stage.importer_dispatch_count() == 0U);
    CHECK(boundary.resource_response_transmit_count == 1U);
    check_post_manifest_terminal_invariants(harness, boundary);
  }
}

TEST_CASE("Dropped response and ACK continuations remain one semantic dispatch "
          "across 20 runs each",
          "[goldsrc][asset-dispatch][stage][integration][loss][repeat-20]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>();
  write_complete_resources(root, world_bytes);
  const auto environment = shared_environment(root);
  assets::AssetImporterRegistries registries;

  const auto run_loss_mode = [&](const ResponseLossMode mode,
                                 const std::size_t expected_transmits) {
    for (std::size_t run = 0U; run < 20U; ++run) {
      INFO("loss continuation mode=" << static_cast<int>(mode)
                                     << " run=" << run);
      DispatchStageHarness harness{environment, registries};
      const auto boundary =
          harness.reach_manifest(kCompleteEntries, true, mode);
      harness.finish(boundary.next_update);

      REQUIRE(
          harness.stage.state() ==
          goldsrc::PrecacheAssetDispatchStageState::importer_boundary_reached);
      REQUIRE(harness.stage.result());
      CHECK(harness.stage.result()->dispatch_result().state ==
            assets::AssetDispatchState::importer_not_registered);
      CHECK(boundary.resource_response_transmit_count == expected_transmits);
      CHECK(harness.stage.manifest_publication_count() == 1U);
      CHECK(harness.stage.source_open_attempt_count() == 1U);
      CHECK(harness.stage.importer_dispatch_count() == 1U);
      check_post_manifest_terminal_invariants(harness, boundary);
    }
  };

  SECTION("dropped resource response retries one reliable generation") {
    run_loss_mode(ResponseLossMode::dropped_response, 2U);
  }
  SECTION("dropped ACK does not duplicate the semantic response") {
    run_loss_mode(ResponseLossMode::dropped_ack, 1U);
  }
}

TEST_CASE(
    "Historical coordinator stop points do not eagerly open asset sources",
    "[goldsrc][asset-dispatch][coordinator][historical-stops]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  auto environment = shared_environment(root);
  assets::AssetImporterRegistries registries;
  ImmediateConsistencyProvider provider;
  const auto remote = network::NetworkAddress::loopback(27'018U);

  SECTION("challenge-only") {
    FakeTransport transport;
    goldsrc::GoldSrcHandshakeCoordinator handshake{
        transport, remote, goldsrc::HandshakeStopPoint::challenge,
        std::nullopt};
    REQUIRE(handshake.start({}));
    CHECK_FALSE(handshake.asset_dispatch_result());
    CHECK_FALSE(handshake.asset_dispatch_error());
    handshake.cancel(goldsrc::ChallengeExchangeTimePoint{} + 1ms);
    CHECK(handshake.state() == goldsrc::GoldSrcHandshakeState::cancelled);
    CHECK(transport.sent.size() == 1U);
  }

  SECTION("all authenticated historical stops") {
    const std::array historical_stops{
        goldsrc::HandshakeStopPoint::connect_request,
        goldsrc::HandshakeStopPoint::connect_response,
        goldsrc::HandshakeStopPoint::netchan_bootstrap,
        goldsrc::HandshakeStopPoint::signon_boundary,
        goldsrc::HandshakeStopPoint::pre_resource,
        goldsrc::HandshakeStopPoint::delta_schemas,
        goldsrc::HandshakeStopPoint::movevars,
        goldsrc::HandshakeStopPoint::user_info,
        goldsrc::HandshakeStopPoint::resource_list_boundary,
        goldsrc::HandshakeStopPoint::resource_list,
        goldsrc::HandshakeStopPoint::resource_response_boundary,
        goldsrc::HandshakeStopPoint::precache_manifest,
    };

    for (const auto stop : historical_stops) {
      CAPTURE(stop);
      FakeTransport transport;
      std::size_t authentication_releases = 0U;
      std::size_t source_open_starts = 0U;
      auto handshake = historical_coordinator(
          transport, remote, stop,
          prepared_request_with_session(authentication_releases), &provider,
          environment, registries, source_open_starts);

      REQUIRE(handshake->start({}));
      CHECK(handshake->state() ==
            goldsrc::GoldSrcHandshakeState::waiting_for_challenge);
      CHECK(source_open_starts == 0U);
      CHECK_FALSE(handshake->asset_dispatch_result());
      CHECK_FALSE(handshake->asset_dispatch_error());
      handshake->cancel(goldsrc::ChallengeExchangeTimePoint{} + 1ms);
      CHECK(handshake->state() == goldsrc::GoldSrcHandshakeState::cancelled);
      CHECK(source_open_starts == 0U);
      CHECK(transport.sent.size() == 1U);
      CHECK(authentication_releases == 1U);
    }
    CHECK(provider.begin_count == 0U);
    CHECK(provider.update_count == 0U);
  }
}

TEST_CASE("Full coordinator maps asset outcomes from challenge through the "
          "dispatch boundary",
          "[goldsrc][asset-dispatch][coordinator][integration]") {
  hlclient::tests::ScopedLocalResourceTestRoot root;
  const auto world_bytes = synthetic::source_bytes<assets::WorldAsset>(65U);
  write_complete_resources(root, world_bytes);
  auto environment = shared_environment(root);
  synthetic::SyntheticImporterCounts importer_counts;
  assets::AssetImporterRegistries registries;
  REQUIRE(registries.worlds.register_importer(
      std::make_unique<synthetic::SyntheticWorldImporter>("coordinator-world",
                                                          importer_counts)));

  FakeTransport transport;
  const auto remote = network::NetworkAddress::loopback(27'017U);
  ImmediateConsistencyProvider provider;
  std::size_t authentication_releases = 0U;
  auto prepared = prepared_request_with_session(authentication_releases);
  auto asset_config = test_config();
  asset_config.source_open.read_chunk_bytes = 4U;
  asset_config.source_open.maximum_chunks_per_update = 1U;
  bool expect_timeout = false;
  SECTION("synthetic world import") {}
  SECTION("minimum event capacity drains one-byte chunk progress") {
    asset_config.maximum_stage_events = 3U;
    asset_config.source_open.read_chunk_bytes = 1U;
  }
  SECTION("local source timeout maps to the asset-dispatch timeout state") {
    expect_timeout = true;
    asset_config.source_open.timeout = 1ms;
  }
  const auto resource_response_config = asset_config.manifest.response;
  std::size_t response_queues = 0U;
  std::size_t manifest_publications = 0U;
  std::size_t source_open_starts = 0U;
  std::size_t source_ready_events = 0U;
  std::size_t importer_selected_events = 0U;
  std::size_t imported_events = 0U;
  std::size_t source_open_before_manifest = 0U;
  std::size_t endpoint_mismatches = 0U;
  std::optional<std::size_t> sent_at_manifest_publication;

  goldsrc::GoldSrcHandshakeCoordinator handshake{
      transport,
      remote,
      goldsrc::HandshakeStopPoint::asset_dispatch,
      std::move(prepared.request),
      coordinator_challenge_config(),
      goldsrc::ChallengeTraceCallback{},
      goldsrc::ConnectRequestTraceCallback{},
      coordinator_response_config(),
      goldsrc::ConnectResponseTraceCallback{},
      std::move(prepared.session),
      goldsrc::NetchanBootstrapConfig{},
      goldsrc::NetchanBootstrapTraceCallback{},
      goldsrc::InitialSignonConfig{},
      goldsrc::InitialSignonTraceCallback{},
      goldsrc::PreResourceSignonConfig{},
      goldsrc::PreResourceSignonTraceCallback{},
      goldsrc::DeltaDescriptionStageConfig{},
      goldsrc::DeltaDescriptionTraceCallback{},
      goldsrc::MovementEnvironmentStageConfig{},
      goldsrc::MovementEnvironmentTraceCallback{},
      goldsrc::UserInfoSignonStageConfig{},
      goldsrc::UserInfoSignonTraceCallback{},
      goldsrc::ResourceTransitionStageConfig{},
      goldsrc::ResourceTransitionTraceCallback{},
      goldsrc::ResourceListStageConfig{},
      goldsrc::ResourceListTraceCallback{},
      resource_response_config,
      &provider,
      [&response_queues](
          const goldsrc::ResourceClientResponseTraceEvent &event) {
        if (event.classification ==
            goldsrc::ResourceClientResponseTraceClassification::
                resource_response_queued) {
          ++response_queues;
        }
      },
      environment,
      goldsrc::PrecacheManifestStageConfig{},
      goldsrc::PrecacheManifestTraceCallback{},
      &registries,
      asset_config,
      [&](const goldsrc::PrecacheAssetDispatchTraceEvent &event) {
        if (event.endpoint != remote) {
          ++endpoint_mismatches;
        }
        using Classification =
            goldsrc::PrecacheAssetDispatchTraceClassification;
        if (event.classification == Classification::precache_manifest_ready) {
          ++manifest_publications;
          sent_at_manifest_publication = transport.sent.size();
        } else if (event.classification ==
                   Classification::asset_source_open_started) {
          ++source_open_starts;
          if (manifest_publications == 0U) {
            ++source_open_before_manifest;
          }
        } else if (event.classification == Classification::asset_source_ready) {
          ++source_ready_events;
        } else if (event.classification == Classification::importer_selected) {
          ++importer_selected_events;
        } else if (event.classification == Classification::asset_imported) {
          ++imported_events;
        }
      }};

  const goldsrc::ChallengeExchangeTimePoint epoch{};
  REQUIRE(handshake.start(epoch));
  REQUIRE(transport.sent.size() == 1U);
  const auto expected_challenge = goldsrc::build_getchallenge_request();
  REQUIRE(expected_challenge);
  CHECK(transport.sent.front().payload == *expected_challenge.datagram);

  constexpr std::uint32_t challenge = 0x7f00'3111U;
  transport.queue(remote, challenge_response(challenge));
  handshake.update(epoch + 1ms);
  REQUIRE(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::waiting_for_connect_response);
  REQUIRE(transport.sent.size() == 2U);

  transport.queue(remote, accept_response(transport.local));
  handshake.update(epoch + 2ms);
  REQUIRE(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::waiting_for_asset_dispatch);
  REQUIRE(handshake.local_endpoint());
  CHECK(*handshake.local_endpoint() == transport.local);
  CHECK(handshake.connect_send_attempts() == 1U);

  handshake.update(epoch + 3ms);
  REQUIRE(transport.sent.size() >= 3U);
  const auto initial = decode_sent(transport.sent.back());
  constexpr std::array exact_new{
      std::byte{0x03U}, std::byte{'n'}, std::byte{'e'},
      std::byte{'w'},   std::byte{0U},
  };
  REQUIRE(initial.payload.size() >= exact_new.size());
  CHECK(std::ranges::equal(
      std::span<const std::byte>{initial.payload}.first(exact_new.size()),
      exact_new));

  transport.queue(
      remote, server_packet(1U, true, initial.header.sequence.sequence.value(),
                            true, service_envelope(first_semantic_payload())));
  handshake.update(epoch + 4ms);
  handshake.update(epoch + 5ms);
  const auto transition = decode_sent(transport.sent.back());
  constexpr std::array exact_sendres{
      std::byte{0x03U}, std::byte{'s'}, std::byte{'e'},
      std::byte{'n'},   std::byte{'d'}, std::byte{'r'},
      std::byte{'e'},   std::byte{'s'}, std::byte{0U},
  };
  REQUIRE(std::ranges::equal(transition.payload, exact_sendres));

  transport.queue(
      remote,
      server_packet(
          2U, false, transition.header.sequence.sequence.value(), false,
          service_envelope(resource_semantic_payload(kCompleteEntries))));
  handshake.update(epoch + 6ms);
  const auto response = decode_sent(transport.sent.back());
  REQUIRE(response.header.sequence.flags.reliable);
  REQUIRE(response.header.sequence.flags.fragmented);
  REQUIRE(response.fragments[0U]);

  constexpr std::array continuation{
      std::byte{0x03U}, std::byte{'s'}, std::byte{'p'}, std::byte{'a'},
      std::byte{'w'},   std::byte{'n'}, std::byte{0U},
  };
  transport.queue(remote,
                  server_packet(3U, false,
                                response.header.sequence.sequence.value(), true,
                                service_envelope(continuation)));
  handshake.update(epoch + 7ms);
  handshake.update(epoch + 8ms);
  handshake.update(epoch + 9ms);

  auto now = epoch + 10ms;
  for (std::size_t update = 0U; update < 128U && !handshake.terminal();
       ++update) {
    handshake.update(now);
    now += 1ms;
  }

  REQUIRE(handshake.terminal());
  if (expect_timeout) {
    CHECK(handshake.state() ==
          goldsrc::GoldSrcHandshakeState::asset_dispatch_timed_out);
    CHECK_FALSE(handshake.asset_dispatch_result());
    REQUIRE(handshake.asset_dispatch_error());
    CHECK(handshake.asset_dispatch_error()->source_open_code ==
          goldsrc::ApprovedAssetSourceOpenErrorCode::timed_out);
  } else {
    CHECK(handshake.state() == goldsrc::GoldSrcHandshakeState::asset_imported);
    REQUIRE(handshake.asset_dispatch_result());
    CHECK_FALSE(handshake.asset_dispatch_error());
    REQUIRE(handshake.asset_dispatch_result()->imported_asset());
    CHECK(std::holds_alternative<assets::WorldAsset>(
        *handshake.asset_dispatch_result()->imported_asset()));
  }
  CHECK(response_queues == 1U);
  CHECK(manifest_publications == 1U);
  CHECK(source_open_starts == 1U);
  CHECK(source_open_before_manifest == 0U);
  CHECK(source_ready_events == (expect_timeout ? 0U : 1U));
  CHECK(importer_selected_events == (expect_timeout ? 0U : 1U));
  CHECK(imported_events == (expect_timeout ? 0U : 1U));
  CHECK(endpoint_mismatches == 0U);
  CHECK(importer_counts.probe_count == (expect_timeout ? 0U : 1U));
  CHECK(importer_counts.import_count == (expect_timeout ? 0U : 1U));
  CHECK(provider.begin_count == 1U);
  CHECK(provider.update_count == 1U);
  CHECK(provider.lifetime_releases == 1U);
  CHECK(authentication_releases == 1U);
  REQUIRE(sent_at_manifest_publication);
  CHECK(transport.sent.size() == *sent_at_manifest_publication);
  CHECK(std::ranges::all_of(transport.sent,
                            [remote](const SentDatagram &datagram) {
                              return datagram.destination == remote;
                            }));

  const auto terminal_sent = transport.sent.size();
  handshake.update(now + 1s);
  handshake.cancel(now + 2s);
  CHECK(handshake.state() ==
        (expect_timeout
             ? goldsrc::GoldSrcHandshakeState::asset_dispatch_timed_out
             : goldsrc::GoldSrcHandshakeState::asset_imported));
  CHECK(transport.sent.size() == terminal_sent);
  CHECK(authentication_releases == 1U);
  CHECK(importer_counts.import_count == (expect_timeout ? 0U : 1U));
}

} // namespace
