#include "delta_test_fixture.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include "local_resource_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "resource_client_response_test_fixture.hpp"
#include "resource_list_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "user_info_test_fixture.hpp"

#include <hlclient/assets/asset_importer_registry.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/world_render/world_render_package_stage.hpp>
#include <hlclient/network/datagram_transport.hpp>
#include <hlclient/resource_consistency/provider.hpp>
#include <hlclient/world_preview/world_preview_scene_source.hpp>

#include <catch2/catch_test_macros.hpp>

#include <bzlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
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
namespace assets = hlclient::assets;
namespace bsp = hlclient::goldsrc::bsp;
namespace consistency = hlclient::resource_consistency;
namespace delta_fixture = hlclient::test::delta_fixture;
namespace fixture = hlclient::tests;
namespace goldsrc = hlclient::goldsrc;
namespace local_resources = hlclient::local_resources;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace network = hlclient::network;
namespace readiness_fixture = hlclient::tests::readiness_fixture;
namespace response_fixture = hlclient::test::resource_client_response_fixture;
namespace user_fixture = hlclient::test::user_info_fixture;
namespace visibility = hlclient::world_visibility;
namespace world_preview = hlclient::world_preview;
namespace world_scene_render = hlclient::world_scene_render;

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
        destination, std::vector<std::byte>{payload.begin(), payload.end()}});
    return {network::DatagramSendStatus::sent, {}};
  }

  [[nodiscard]] network::DatagramTransportReceiveResult
  receive(std::size_t) override {
    if (incoming.empty()) {
      return {network::DatagramTransportReceiveStatus::would_block,
              std::nullopt,
              std::nullopt,
              0U,
              {}};
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

  network::NetworkAddress local{network::NetworkAddress::loopback(31'781U)};
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
  ImmediateConsistencyOperation(std::size_t &updates,
                                std::size_t &cancellations,
                                std::size_t &lifetime_releases) noexcept
      : updates_{updates}, cancellations_{cancellations},
        lifetime_releases_{lifetime_releases} {}

  [[nodiscard]] consistency::ResourceConsistencyUpdateResult update() override {
    ++updates_;
    auto material = consistency::make_resource_consistency_material(
        0x01020304U, response_fixture::kSyntheticOpaqueMaterial);
    REQUIRE(material);
    return consistency::ResourceConsistencyUpdateResult::succeeded(
        consistency::ResourceConsistencySession{
            std::move(*material.material),
            std::make_unique<CountingConsistencyLifetime>(lifetime_releases_)});
  }

  void cancel() noexcept override {
    if (!cancelled_) {
      cancelled_ = true;
      ++cancellations_;
    }
  }

private:
  std::size_t &updates_;
  std::size_t &cancellations_;
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

  std::vector<std::byte> envelope{std::byte{0x42U}, std::byte{0x5AU},
                                  std::byte{0x32U}, std::byte{0U}};
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
              goldsrc::NetchanSequenceFlags{reliable, false}},
          goldsrc::NetchanAcknowledgementWord{sequence(acknowledgement),
                                              reliable_acknowledgement}},
      {},
      std::move(payload)};
  auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
  REQUIRE(encoded);
  REQUIRE(encoded.datagram);
  return std::move(*encoded.datagram);
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket
decode_sent(const SentDatagram &datagram) {
  const auto decoded =
      goldsrc::decode_client_to_server_netchan_packet(datagram.payload);
  REQUIRE(decoded);
  REQUIRE(decoded.packet);
  return *decoded.packet;
}

[[nodiscard]] goldsrc::WorldRenderPackageStageConfig test_config() {
  goldsrc::WorldRenderPackageStageConfig config;
  auto &texture = config.world_textures;
  auto &asset = texture.asset_dispatch;
  auto &transition = asset.manifest.response.resource_list.transition;
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
  asset.manifest.response.resource_list.maximum_stage_events = 64U;
  asset.manifest.response.maximum_driver_events_per_update = 64U;
  asset.manifest.response.response.maximum_response_stage_events = 64U;
  asset.manifest.manifest.maximum_manifest_events = 64U;
  asset.maximum_stage_events = 128U;
  asset.source_open.read_chunk_bytes = 7U;
  asset.source_open.maximum_chunks_per_update = 1U;
  texture.texture_import.wad_source_open.read_chunk_bytes = 5U;
  texture.texture_import.wad_source_open.maximum_chunks_per_update = 1U;
  texture.texture_import.maximum_pixel_conversion_bytes_per_update = 4U;
  texture.maximum_stage_events = 128U;
  config.maximum_stage_events = 128U;
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
                                        post_delta, "maps/textured.bsp");
}

[[nodiscard]] std::vector<std::byte> resource_semantic_payload() {
  constexpr std::array prefix{std::byte{45U}, std::byte{1U}, std::byte{0U},
                              std::byte{0U},  std::byte{0U}, std::byte{0U},
                              std::byte{0U},  std::byte{0U}, std::byte{0U}};
  const std::array entries{
      resource_list_test_fixture::EntrySpec{2U, "maps/textured.bsp", 37U,
                                            0x00FF'FFFFU, 0U},
      resource_list_test_fixture::EntrySpec{2U, "models/test_model.mdl", 9U, 1U,
                                            0U},
      resource_list_test_fixture::EntrySpec{0U, "test_sound.wav", 4U, 2U, 0U}};
  const auto message = resource_list_test_fixture::make_message(entries);
  std::vector<std::byte> payload{prefix.begin(), prefix.end()};
  payload.insert(payload.end(), message.bytes.begin(), message.bytes.end());
  return payload;
}

[[nodiscard]] std::vector<std::byte> bytes_of(const std::string_view text) {
  const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
  return {bytes.begin(), bytes.end()};
}

void populate_embedded_palette(std::vector<std::byte> &bsp_bytes) {
  const auto texture_lump =
      static_cast<std::size_t>(fixture::synthetic_read_i32le(
          bsp_bytes, fixture::synthetic_lump_descriptor_offset(
                         fixture::SyntheticBspLumpId::textures)));
  const auto record_relative = static_cast<std::size_t>(
      fixture::synthetic_read_i32le(bsp_bytes, texture_lump + 4U));
  const auto record = texture_lump + record_relative;
  constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
  const auto count_offset = record + 40U + pixel_byte_count;
  fixture::synthetic_write_u16le(bsp_bytes, count_offset, 256U);
  for (std::size_t index = 0U; index < 256U; ++index) {
    bsp_bytes[count_offset + 2U + (index * 3U)] = static_cast<std::byte>(index);
    bsp_bytes[count_offset + 2U + (index * 3U) + 1U] =
        static_cast<std::byte>(255U - index);
    bsp_bytes[count_offset + 2U + (index * 3U) + 2U] =
        static_cast<std::byte>(index ^ 0x5AU);
  }
}

[[nodiscard]] std::vector<std::byte>
embedded_lightmapped_bsp(const std::size_t lighting_byte_count = 75U) {
  fixture::SyntheticBspBuilder builder;
  builder.lump(fixture::SyntheticBspLumpId::entities) =
      bytes_of("{\n\"classname\" \"worldspawn\"\n}\n");
  auto embedded = fixture::synthetic_embedded_texture("EMBEDDED", 16U, 16U);
  constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
  embedded.trailing_byte_count = pixel_byte_count + 2U + (256U * 3U);
  const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U> textures{
      embedded};
  builder.set_texture_directory(textures);

  fixture::SyntheticBspFace face;
  face.light_styles = {0U, 0xFFU, 0xFFU, 0xFFU};
  face.light_offset = 0;
  builder.set_faces(std::span{&face, 1U});
  auto &lighting = builder.lump(fixture::SyntheticBspLumpId::lighting);
  lighting.resize(lighting_byte_count);
  for (std::size_t index = 0U; index < lighting.size(); ++index) {
    lighting[index] = static_cast<std::byte>(index + 1U);
  }

  auto bytes = builder.build();
  populate_embedded_palette(bytes);
  return bytes;
}

[[nodiscard]] std::vector<std::byte> embedded_lightmapped_bsp_with_entity_tail(
    const std::string_view entity_tail) {
  fixture::SyntheticBspBuilder builder;
  auto entities = std::string{"{\n\"classname\" \"worldspawn\"\n}\n"};
  entities.append(entity_tail);
  builder.lump(fixture::SyntheticBspLumpId::entities) = bytes_of(entities);
  auto embedded = fixture::synthetic_embedded_texture("EMBEDDED", 16U, 16U);
  constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
  embedded.trailing_byte_count = pixel_byte_count + 2U + (256U * 3U);
  const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U> textures{
      embedded};
  builder.set_texture_directory(textures);

  fixture::SyntheticBspFace face;
  face.light_styles = {0U, 0xFFU, 0xFFU, 0xFFU};
  face.light_offset = 0;
  builder.set_faces(std::span{&face, 1U});
  auto &lighting = builder.lump(fixture::SyntheticBspLumpId::lighting);
  lighting.resize(75U, std::byte{0x40U});

  auto bytes = builder.build();
  populate_embedded_palette(bytes);
  return bytes;
}

[[nodiscard]] std::vector<std::byte> embedded_lightmapped_bsp_with_bad_pvs() {
  fixture::SyntheticBspBuilder builder;
  builder.lump(fixture::SyntheticBspLumpId::entities) =
      bytes_of("{\n\"classname\" \"worldspawn\"\n}\n");
  auto embedded = fixture::synthetic_embedded_texture("EMBEDDED", 16U, 16U);
  constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
  embedded.trailing_byte_count = pixel_byte_count + 2U + (256U * 3U);
  const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U> textures{
      embedded};
  builder.set_texture_directory(textures);

  fixture::SyntheticBspFace face;
  face.light_styles = {0U, 0xFFU, 0xFFU, 0xFFU};
  face.light_offset = 0;
  builder.set_faces(std::span{&face, 1U});
  auto &lighting = builder.lump(fixture::SyntheticBspLumpId::lighting);
  lighting.resize(75U, std::byte{0x40U});

  std::array<fixture::SyntheticBspLeaf, 2U> leaves{};
  leaves[0U].contents = -2;
  leaves[0U].visibility_offset = -1;
  leaves[0U].marksurface_count = 0U;
  leaves[1U].contents = -1;
  leaves[1U].visibility_offset = 0;
  leaves[1U].marksurface_count = 1U;
  builder.set_leaves(leaves);
  builder.lump(fixture::SyntheticBspLumpId::visibility) = {std::byte{0U}};

  auto bytes = builder.build();
  populate_embedded_palette(bytes);
  return bytes;
}

[[nodiscard]] std::vector<std::byte>
embedded_lightmapped_bsp_with_brush_scene() {
  fixture::SyntheticBspBuilder builder;
  builder.lump(fixture::SyntheticBspLumpId::entities) = bytes_of(R"({
"classname" "worldspawn"
}
{
"classname" "func_wall"
"model" "*1"
"origin" "0 0 -1"
}
{
"classname" "func_wall"
"model" "*1"
"origin" "8 0 -1"
"rendermode" "5"
}
{
"classname" "info_player_start"
"origin" "8 8 -1"
"angle" "90"
}
)");

  auto embedded = fixture::synthetic_embedded_texture("EMBEDDED", 16U, 16U);
  constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
  embedded.trailing_byte_count = pixel_byte_count + 2U + (256U * 3U);
  const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U> textures{
      embedded};
  builder.set_texture_directory(textures);

  constexpr std::size_t lightmap_bytes_per_quad = 5U * 5U * 3U;
  std::array faces{fixture::SyntheticBspFace{},
                   fixture::SyntheticBspFace{}};
  faces[0U].light_styles = {0U, 0xFFU, 0xFFU, 0xFFU};
  faces[0U].light_offset = 0;
  faces[1U].light_styles = {0U, 0xFFU, 0xFFU, 0xFFU};
  faces[1U].light_offset =
      static_cast<std::int32_t>(lightmap_bytes_per_quad);

  std::array models{fixture::SyntheticBspModel{},
                    fixture::SyntheticBspModel{}};
  models[0U].first_face = 0;
  models[0U].face_count = 1;
  models[0U].visibility_leaf_count = 9;
  models[1U].first_face = 1;
  models[1U].face_count = 1;
  models[1U].visibility_leaf_count = 0;

  std::array<fixture::SyntheticBspLeaf, 10U> leaves{};
  leaves[0U].contents = -2;
  leaves[0U].visibility_offset = -1;
  leaves[0U].marksurface_count = 0U;
  leaves[1U].contents = -1;
  leaves[1U].visibility_offset = 0;
  leaves[1U].marksurface_count = 1U;
  for (std::size_t leaf_index = 2U; leaf_index < leaves.size(); ++leaf_index) {
    leaves[leaf_index].contents = -1;
    leaves[leaf_index].visibility_offset = -1;
    leaves[leaf_index].marksurface_count = 0U;
  }
  constexpr std::array<std::uint16_t, 1U> marksurfaces{0U};

  builder.set_faces(faces)
      .set_leaves(leaves)
      .set_marksurfaces(marksurfaces)
      .set_models(models);
  // Leaf 1 uses a real GoldSrc zero-run compressed two-byte row. The other
  // addressable leaves retain the explicit all-visible row profile.
  builder.lump(fixture::SyntheticBspLumpId::visibility) = {
      std::byte{0x01U}, std::byte{0x00U}, std::byte{0x01U}};
  auto &lighting = builder.lump(fixture::SyntheticBspLumpId::lighting);
  lighting.resize(2U * lightmap_bytes_per_quad, std::byte{0x40U});

  auto bytes = builder.build();
  populate_embedded_palette(bytes);
  return bytes;
}

[[nodiscard]] std::vector<std::byte>
embedded_lightmapped_bsp_with_malformed_brush_geometry() {
  auto bytes = embedded_lightmapped_bsp_with_brush_scene();
  // Flip only model 1's face plane side. All references remain structurally
  // valid, model 0 still materializes, and the brush winding is rejected by
  // the shared geometry codec.
  return fixture::SyntheticBspCorruptor{std::move(bytes)}
      .write_i16(fixture::SyntheticBspLumpId::faces,
                 bsp::kGoldSrcBspFaceWireSize + 2U, 1)
      .take();
}

[[nodiscard]] std::vector<std::byte> unresolved_external_bsp() {
  fixture::SyntheticBspBuilder builder;
  builder.lump(fixture::SyntheticBspLumpId::entities) =
      bytes_of("{\n\"classname\" \"worldspawn\"\n"
               "\"_wad\" \"D:\\\\compiler\\\\absent.wad;\"\n}\n");
  const auto external = fixture::synthetic_external_texture("ABSENT");
  const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U> textures{
      external};
  builder.set_texture_directory(textures);
  return builder.build();
}

[[nodiscard]] std::shared_ptr<const local_resources::LocalResourceEnvironment>
shared_environment(const fixture::ScopedLocalResourceTestRoot &root) {
  auto environment = readiness_fixture::make_environment(root);
  return std::shared_ptr<const local_resources::LocalResourceEnvironment>{
      std::move(environment)};
}

void write_stage_prerequisites(const fixture::ScopedLocalResourceTestRoot &root,
                               const std::span<const std::byte> bsp_bytes) {
  root.write("valve", "maps/textured.bsp", bsp_bytes);
  root.write("valve", "models/test_model.mdl", "model");
  root.write("valve", "sound/test_sound.wav", "sound");
}

void drain_events(goldsrc::WorldRenderPackageStage &stage,
                  std::vector<goldsrc::WorldRenderPackageStageEvent> &events) {
  while (auto event = stage.poll_event()) {
    events.push_back(std::move(*event));
  }
}

template <class Type>
concept HasRendererHandle =
    requires(const Type &value) { value.renderer_handle(); };

class WorldRenderPackageStageHarness final {
public:
  WorldRenderPackageStageHarness(
      const fixture::ScopedLocalResourceTestRoot &root,
      const assets::AssetImporterRegistries &registries,
      goldsrc::WorldRenderPackageStageConfig config = test_config())
      : environment{shared_environment(root)},
        stage{transport,
              remote,
              environment,
              registries,
              std::move(config),
              &provider,
              {},
              {},
              {},
              {},
              {},
              {},
              {},
              {},
              {},
              {},
              {},
              [this](const goldsrc::WorldRenderPackageTraceEvent &event) {
                traces.push_back(event);
              }} {}

  void begin_protocol() {
    REQUIRE(stage.start(
        epoch, transport.local,
        std::make_unique<CountingConnectionLifetime>(connection_releases)));
    update_at(epoch + 1ms, false);
    REQUIRE(transport.sent.size() == 1U);
    const auto initial = decode_sent(transport.sent.front());

    transport.queue(
        remote,
        server_packet(1U, true, initial.header.sequence.sequence.value(), true,
                      service_envelope(first_semantic_payload())));
    update_at(epoch + 2ms, false);
    update_at(epoch + 3ms, false);
    REQUIRE(transport.sent.size() >= 3U);
    const auto transition = decode_sent(transport.sent.back());

    transport.queue(
        remote,
        server_packet(2U, false, transition.header.sequence.sequence.value(),
                      false, service_envelope(resource_semantic_payload())));
    update_at(epoch + 4ms, false);
    REQUIRE_FALSE(transport.sent.empty());
    const auto response = decode_sent(transport.sent.back());
    REQUIRE(response.header.sequence.flags.reliable);
    REQUIRE(response.header.sequence.flags.fragmented);

    constexpr std::array spawn{std::byte{0x03U}, std::byte{'s'}, std::byte{'p'},
                               std::byte{'a'},   std::byte{'w'}, std::byte{'n'},
                               std::byte{0U}};
    transport.queue(remote,
                    server_packet(3U, false,
                                  response.header.sequence.sequence.value(),
                                  true, service_envelope(spawn)));
    update_at(epoch + 5ms, false);
    update_at(epoch + 6ms, false);
    update_at(epoch + 7ms, false);
    next_update = epoch + 8ms;

    REQUIRE(stage.manifest_publication_count() == 1U);
    REQUIRE(stage.transmitted_packet_count_at_manifest_publication());
    sent_at_manifest =
        *stage.transmitted_packet_count_at_manifest_publication();
    REQUIRE(transport.sent.size() == sent_at_manifest);
  }

  void update_once(const bool drain = true) {
    update_at(next_update, drain);
    next_update += 1ms;
  }

  void finish(const bool drain = true) {
    for (std::size_t update = 0U; update < 8'192U && !stage.terminal();
         ++update) {
      update_once(drain);
    }
    REQUIRE(stage.terminal());
    if (drain) {
      drain_events(stage, events);
    }
  }

  [[nodiscard]] std::size_t
  event_count(const goldsrc::WorldRenderPackageStageEventType type) const {
    return static_cast<std::size_t>(std::ranges::count(
        events, type, &goldsrc::WorldRenderPackageStageEvent::type));
  }

  [[nodiscard]] std::size_t trace_count(
      const goldsrc::WorldRenderPackageTraceClassification classification)
      const {
    return static_cast<std::size_t>(std::ranges::count(
        traces, classification,
        &goldsrc::WorldRenderPackageTraceEvent::classification));
  }

  FakeTransport transport;
  network::NetworkAddress remote{network::NetworkAddress::loopback(27'043U)};
  std::shared_ptr<const local_resources::LocalResourceEnvironment> environment;
  ImmediateConsistencyProvider provider;
  std::size_t connection_releases{0U};
  std::vector<goldsrc::WorldRenderPackageStageEvent> events;
  std::vector<goldsrc::WorldRenderPackageTraceEvent> traces;
  std::size_t sent_at_manifest{0U};
  const goldsrc::WorldRenderPackageStageTimePoint epoch{};
  goldsrc::WorldRenderPackageStageTimePoint next_update{};
  goldsrc::WorldRenderPackageStage stage;

private:
  void update_at(const goldsrc::WorldRenderPackageStageTimePoint now,
                 const bool drain) {
    stage.update(now);
    if (drain) {
      drain_events(stage, events);
    }
  }
};

void check_terminal_ownership_and_transport(
    const WorldRenderPackageStageHarness &harness) {
  CHECK(harness.stage.cleanup_count() == 1U);
  CHECK(harness.connection_releases == 1U);
  CHECK(harness.provider.begin_count == 1U);
  CHECK(harness.provider.update_count == 1U);
  CHECK(harness.provider.cancel_count == 0U);
  CHECK(harness.provider.lifetime_releases == 1U);
  CHECK(harness.stage.manifest_publication_count() == 1U);
  CHECK(harness.stage.transmitted_packet_count() == harness.sent_at_manifest);
  CHECK(harness.transport.sent.size() == harness.sent_at_manifest);
  CHECK(std::ranges::all_of(harness.transport.sent,
                            [&harness](const SentDatagram &datagram) {
                              return datagram.destination == harness.remote;
                            }));
  REQUIRE(harness.stage.local_endpoint());
  CHECK(*harness.stage.local_endpoint() == harness.transport.local);
  CHECK(harness.stage.remote_endpoint() == harness.remote);
}

TEST_CASE("World render package stage publishes one complete CPU package",
          "[world-render-package][stage][success]") {
  STATIC_REQUIRE_FALSE(HasRendererHandle<goldsrc::WorldRenderPackageStage>);
  STATIC_REQUIRE_FALSE(
      HasRendererHandle<hlclient::world_render::WorldRenderPackage>);

  fixture::ScopedLocalResourceTestRoot root;
  write_stage_prerequisites(root, embedded_lightmapped_bsp());
  assets::AssetImporterRegistries registries;
  REQUIRE(bsp::register_builtin_asset_importers(registries));
  WorldRenderPackageStageHarness harness{root, registries};
  harness.begin_protocol();
  harness.finish();

  REQUIRE(harness.stage.state() ==
          goldsrc::WorldRenderPackageStageState::world_render_package_ready);
  REQUIRE(harness.stage.result());
  CHECK_FALSE(harness.stage.error());
  const auto &package = *harness.stage.result();
  CHECK(package.statistics().source_surface_count == 1U);
  CHECK(package.statistics().vertex_count == 4U);
  CHECK(package.statistics().index_count == 6U);
  CHECK(package.statistics().batch_count == 1U);
  CHECK(package.lightmaps().binding_count() == 1U);
  CHECK(package.lightmaps().page_count() == 1U);
  CHECK(harness.stage.lightmap_import_count() == 1U);
  CHECK(harness.stage.lightmap_set_publication_count() == 1U);
  CHECK(harness.stage.render_package_publication_count() == 1U);
  CHECK(harness.event_count(goldsrc::WorldRenderPackageStageEventType::
                                world_render_package_ready) == 1U);
  CHECK(harness.trace_count(goldsrc::WorldRenderPackageTraceClassification::
                                world_render_package_ready) == 1U);
  check_terminal_ownership_and_transport(harness);

  const auto terminal_state = harness.stage.state();
  const auto terminal_sent = harness.transport.sent.size();
  const auto terminal_cleanup = harness.stage.cleanup_count();
  const auto terminal_releases = harness.connection_releases;
  harness.stage.update(harness.next_update + 5s);
  harness.stage.cancel(harness.next_update + 6s);
  harness.stage.cancel(harness.next_update + 7s);
  CHECK(harness.stage.state() == terminal_state);
  CHECK(harness.transport.sent.size() == terminal_sent);
  CHECK(harness.stage.cleanup_count() == terminal_cleanup);
  CHECK(harness.connection_releases == terminal_releases);
  CHECK(harness.stage.lightmap_import_count() == 1U);
  CHECK(harness.stage.lightmap_set_publication_count() == 1U);
  CHECK(harness.stage.render_package_publication_count() == 1U);
}

TEST_CASE("World render package stage publishes one M4.4 CPU spatial scene",
          "[world-render-package][stage][world-spatial-scene][success]") {
  STATIC_REQUIRE_FALSE(HasRendererHandle<goldsrc::WorldRenderPackageStage>);
  STATIC_REQUIRE_FALSE(
      HasRendererHandle<hlclient::world_scene_render::WorldSceneRenderPackage>);

  fixture::ScopedLocalResourceTestRoot root;
  write_stage_prerequisites(root, embedded_lightmapped_bsp());
  assets::AssetImporterRegistries registries;
  REQUIRE(bsp::register_builtin_asset_importers(registries));
  auto config = test_config();
  config.build_world_spatial_scene = true;
  WorldRenderPackageStageHarness harness{root, registries, config};
  harness.begin_protocol();
  harness.finish();

  REQUIRE(harness.stage.state() ==
          goldsrc::WorldRenderPackageStageState::world_render_package_ready);
  REQUIRE(harness.stage.result());
  REQUIRE(harness.stage.scene_result());
  CHECK_FALSE(harness.stage.error());
  const auto &scene = *harness.stage.scene_result();
  CHECK(scene.world_package() == harness.stage.result());
  CHECK(scene.statistics().world_surface_count == 1U);
  CHECK(scene.statistics().brush_model_count == 0U);
  CHECK(scene.statistics().brush_instance_count == 0U);
  CHECK(scene.spatial_package().statistics().plane_count == 1U);
  CHECK(scene.spatial_package().statistics().node_count == 1U);
  CHECK(scene.spatial_package().statistics().leaf_count == 2U);
  CHECK(harness.stage.bsp_scene_parse_count() == 1U);
  CHECK(harness.stage.brush_library_build_count() == 0U);
  CHECK(harness.stage.world_scene_publication_count() == 1U);
  CHECK(harness.stage.render_package_publication_count() == 1U);
  CHECK_FALSE(harness.stage.spawn_camera_result());
  check_terminal_ownership_and_transport(harness);

  const auto scene_identity = scene.resource_identity();
  const auto sent = harness.transport.sent.size();
  harness.stage.update(harness.next_update + 5s);
  CHECK(harness.stage.scene_result()->resource_identity() == scene_identity);
  CHECK(harness.stage.world_scene_publication_count() == 1U);
  CHECK(harness.stage.bsp_scene_parse_count() == 1U);
  CHECK(harness.transport.sent.size() == sent);
}

TEST_CASE("World render package stage composes brushes spawn and CPU visibility",
          "[world-render-package][stage][world-spatial-scene][brush]"
          "[visibility][spawn]") {
  STATIC_REQUIRE_FALSE(HasRendererHandle<goldsrc::WorldRenderPackageStage>);
  STATIC_REQUIRE_FALSE(
      HasRendererHandle<world_scene_render::WorldSceneRenderPackage>);
  STATIC_REQUIRE_FALSE(
      HasRendererHandle<world_preview::WorldPreviewSceneSource>);

  fixture::ScopedLocalResourceTestRoot root;
  write_stage_prerequisites(root,
                            embedded_lightmapped_bsp_with_brush_scene());
  assets::AssetImporterRegistries registries;
  REQUIRE(bsp::register_builtin_asset_importers(registries));
  auto config = test_config();
  config.build_world_spatial_scene = true;
  config.world_scene.brushes =
      goldsrc::brush_models::GoldSrcWorldSceneBrushMode::static_initial;
  config.world_scene.extract_spawn = true;
  WorldRenderPackageStageHarness harness{root, registries, config};
  harness.begin_protocol();
  harness.finish();

  REQUIRE(harness.stage.state() ==
          goldsrc::WorldRenderPackageStageState::world_render_package_ready);
  REQUIRE(harness.stage.result());
  REQUIRE(harness.stage.scene_result());
  CHECK_FALSE(harness.stage.error());
  const auto &scene = *harness.stage.scene_result();
  CHECK(scene.world_package() == harness.stage.result());
  CHECK(scene.statistics().world_surface_count == 1U);
  CHECK(scene.statistics().brush_model_count == 1U);
  CHECK(scene.statistics().brush_surface_count == 1U);
  CHECK(scene.statistics().brush_instance_count == 2U);
  CHECK(scene.statistics().supported_brush_instance_count == 1U);
  CHECK(scene.statistics().unsupported_brush_instance_count == 1U);
  REQUIRE(scene.brush_instances().size() == 2U);
  CHECK(scene.brush_instances()[0U].support_status ==
        world_scene_render::BrushSubmodelRenderSupportStatus::
            supported_static_opaque);
  CHECK(scene.brush_instances()[1U].support_status ==
        world_scene_render::BrushSubmodelRenderSupportStatus::
            unsupported_rendermode);
  CHECK(scene.spatial_package().statistics().unique_pvs_row_count == 2U);
  const auto retained_pvs_row =
      scene.spatial_package().pvs_table().row_for_leaf(1U);
  REQUIRE(retained_pvs_row);
  REQUIRE(retained_pvs_row->size() == 2U);
  CHECK((*retained_pvs_row)[0U] == std::byte{0x01U});
  CHECK((*retained_pvs_row)[1U] == std::byte{0x00U});
  const auto all_visible_pvs_row =
      scene.spatial_package().pvs_table().row_for_leaf(2U);
  REQUIRE(all_visible_pvs_row);
  REQUIRE(all_visible_pvs_row->size() == 2U);
  CHECK((*all_visible_pvs_row)[0U] == std::byte{0xFFU});
  CHECK((*all_visible_pvs_row)[1U] == std::byte{0x01U});

  CHECK(harness.stage.bsp_scene_parse_count() == 1U);
  CHECK(harness.stage.brush_library_build_count() == 1U);
  CHECK(harness.stage.world_scene_publication_count() == 1U);
  CHECK(harness.stage.render_package_publication_count() == 1U);
  REQUIRE(harness.stage.spawn_camera_result());
  const auto &spawn = *harness.stage.spawn_camera_result();
  REQUIRE(spawn.descriptor);
  CHECK(spawn.descriptor->source_class == goldsrc::brush_models::
        GoldSrcSpawnCameraSourceClass::info_player_start);
  CHECK(spawn.descriptor->source_entity_ordinal == 3U);
  CHECK(spawn.descriptor->position.x == 8.0F);
  CHECK(spawn.descriptor->position.y == 8.0F);
  CHECK(spawn.descriptor->position.z == -1.0F);

  world_preview::WorldPreviewSceneOptions preview_options;
  preview_options.camera_mode = world_preview::WorldPreviewCameraMode::spawn;
  preview_options.visibility_mode = visibility::WorldVisibilityMode::pvs_only;
  preview_options.brush_submodels =
      world_preview::WorldPreviewBrushSubmodelsMode::static_instances;
  preview_options.spawn_camera =
      world_preview::WorldPreviewSpawnCameraDescriptor{
          spawn.descriptor->position,
          spawn.descriptor->forward,
          spawn.descriptor->up,
      };
  world_preview::WorldPreviewSceneSource preview{
      harness.stage.scene_result(), preview_options};
  CHECK(preview.spawn_camera_applied());
  REQUIRE(preview.world_state().world_visibility());
  const auto &visible = *preview.world_state().world_visibility();
  CHECK(visible.requested_mode() == visibility::WorldVisibilityMode::pvs_only);
  CHECK(visible.applied_mode() == visibility::WorldVisibilityMode::pvs_only);
  CHECK(visible.fallback_reason() == visibility::WorldPvsFallbackReason::none);
  REQUIRE(visible.camera_leaf_index());
  CHECK(*visible.camera_leaf_index() == 1U);
  REQUIRE(visible.visible_leaf_indices().size() == 1U);
  CHECK(visible.visible_leaf_indices()[0U] == 1U);
  REQUIRE(visible.visible_world_surface_indices().size() == 1U);
  CHECK(visible.visible_world_surface_indices()[0U] == 0U);
  REQUIRE(visible.visible_brush_instance_indices().size() == 1U);
  CHECK(visible.visible_brush_instance_indices()[0U] == 0U);
  CHECK(visible.statistics().total_brush_instance_count == 2U);
  CHECK(visible.statistics().supported_brush_instance_count == 1U);
  CHECK(visible.statistics().visible_brush_instance_count == 1U);
  REQUIRE(preview.world_state().visible_draw_list());
  const auto &draw_list = *preview.world_state().visible_draw_list();
  CHECK(draw_list.statistics().world_command_count == 1U);
  CHECK(draw_list.statistics().brush_command_count == 1U);
  CHECK(draw_list.statistics().command_count == 2U);
  REQUIRE(draw_list.commands().size() == 2U);
  CHECK(draw_list.commands()[1U].object_kind ==
        visibility::WorldVisibleObjectKind::brush_instance_surface);
  REQUIRE(draw_list.commands()[1U].source_instance_index);
  CHECK(*draw_list.commands()[1U].source_instance_index == 0U);

  check_terminal_ownership_and_transport(harness);
  const auto terminal_state = harness.stage.state();
  const auto terminal_sent = harness.transport.sent.size();
  const auto terminal_cleanup = harness.stage.cleanup_count();
  const auto terminal_releases = harness.connection_releases;
  const auto scene_identity = harness.stage.scene_result()->resource_identity();
  harness.stage.update(harness.next_update + 5s);
  harness.stage.cancel(harness.next_update + 6s);
  REQUIRE(preview.update(std::chrono::duration<double>{0.0}));
  CHECK(harness.stage.state() == terminal_state);
  CHECK(harness.stage.scene_result()->resource_identity() == scene_identity);
  CHECK(harness.transport.sent.size() == terminal_sent);
  CHECK(harness.stage.cleanup_count() == terminal_cleanup);
  CHECK(harness.connection_releases == terminal_releases);
  CHECK(harness.stage.world_scene_publication_count() == 1U);
}

TEST_CASE("World render package stage translates submodel parse limits to the brush boundary",
          "[world-render-package][stage][world-spatial-scene][brush]"
          "[failure][regression]") {
  enum class Scenario {
    invalid_geometry,
    aggregate_limit,
  };

  Scenario scenario{Scenario::invalid_geometry};
  fixture::ScopedLocalResourceTestRoot root;
  auto config = test_config();
  config.build_world_spatial_scene = true;
  config.world_scene.brushes =
      goldsrc::brush_models::GoldSrcWorldSceneBrushMode::static_initial;
  SECTION("malformed model 1 geometry is a typed brush failure") {
    scenario = Scenario::invalid_geometry;
    write_stage_prerequisites(
        root, embedded_lightmapped_bsp_with_malformed_brush_geometry());
  }
  SECTION("aggregate retained geometry is a typed brush limit failure") {
    scenario = Scenario::aggregate_limit;
    write_stage_prerequisites(root,
                              embedded_lightmapped_bsp_with_brush_scene());
    // Model 0 retains four vertices. The brush adds four more, so seven is a
    // valid per-model limit but rejects the aggregate at source model 1.
    config.bsp.maximum_output_vertices = 7U;
  }
  assets::AssetImporterRegistries registries;
  REQUIRE(bsp::register_builtin_asset_importers(registries));
  WorldRenderPackageStageHarness harness{root, registries, config};
  harness.begin_protocol();
  harness.finish();

  CHECK(harness.stage.state() ==
        goldsrc::WorldRenderPackageStageState::render_package_failed);
  CHECK_FALSE(harness.stage.result());
  CHECK_FALSE(harness.stage.scene_result());
  REQUIRE(harness.stage.error());
  CHECK(harness.stage.error()->code ==
        goldsrc::WorldRenderPackageStageErrorCode::
            brush_render_library_build_failed);
  CHECK_FALSE(harness.stage.error()->bsp_code);
  REQUIRE(harness.stage.error()->brush_library_code);
  CHECK(*harness.stage.error()->brush_library_code ==
        (scenario == Scenario::aggregate_limit
             ? goldsrc::brush_models::GoldSrcBrushRenderLibraryErrorCode::
                   aggregate_limit_exceeded
             : goldsrc::brush_models::GoldSrcBrushRenderLibraryErrorCode::
                   invalid_model_geometry));
  CHECK(harness.stage.bsp_scene_parse_count() == 1U);
  CHECK(harness.stage.brush_library_build_count() == 1U);
  CHECK(harness.stage.world_scene_publication_count() == 0U);
  CHECK(harness.stage.render_package_publication_count() == 0U);
  check_terminal_ownership_and_transport(harness);
}

TEST_CASE("World render package stage keeps model zero parse failures at the BSP boundary",
          "[world-render-package][stage][world-spatial-scene][failure]"
          "[regression]") {
  fixture::ScopedLocalResourceTestRoot root;
  write_stage_prerequisites(root,
                            embedded_lightmapped_bsp_with_brush_scene());
  assets::AssetImporterRegistries registries;
  REQUIRE(bsp::register_builtin_asset_importers(registries));
  auto config = test_config();
  config.build_world_spatial_scene = true;
  config.world_scene.brushes =
      goldsrc::brush_models::GoldSrcWorldSceneBrushMode::static_initial;
  config.bsp.maximum_output_vertices = 3U;
  WorldRenderPackageStageHarness harness{root, registries, config};
  harness.begin_protocol();
  harness.finish();

  CHECK(harness.stage.state() ==
        goldsrc::WorldRenderPackageStageState::render_package_failed);
  CHECK_FALSE(harness.stage.result());
  CHECK_FALSE(harness.stage.scene_result());
  REQUIRE(harness.stage.error());
  CHECK(harness.stage.error()->code ==
        goldsrc::WorldRenderPackageStageErrorCode::
            world_scene_bsp_parse_failed);
  REQUIRE(harness.stage.error()->bsp_code);
  CHECK(*harness.stage.error()->bsp_code ==
        bsp::GoldSrcBspErrorCode::geometry_limit_exceeded);
  CHECK_FALSE(harness.stage.error()->brush_library_code);
  CHECK(harness.stage.bsp_scene_parse_count() == 1U);
  CHECK(harness.stage.brush_library_build_count() == 0U);
  CHECK(harness.stage.world_scene_publication_count() == 0U);
  CHECK(harness.stage.render_package_publication_count() == 0U);
  check_terminal_ownership_and_transport(harness);
}

TEST_CASE("World render package stage retains typed M4.4 scene failures",
          "[world-render-package][stage][world-spatial-scene][failure]") {
  enum class Scenario {
    malformed_pvs,
    malformed_entity_tail,
  };

  Scenario scenario{Scenario::malformed_pvs};
  fixture::ScopedLocalResourceTestRoot root;
  auto config = test_config();
  config.build_world_spatial_scene = true;

  SECTION("malformed PVS fails the spatial scene after M4.3 package build") {
    scenario = Scenario::malformed_pvs;
    write_stage_prerequisites(root, embedded_lightmapped_bsp_with_bad_pvs());
  }
  SECTION("malformed inert entity tail fails canonical scene composition") {
    scenario = Scenario::malformed_entity_tail;
    config.world_scene.brushes =
        goldsrc::brush_models::GoldSrcWorldSceneBrushMode::static_initial;
    write_stage_prerequisites(
        root,
        embedded_lightmapped_bsp_with_entity_tail(
            "{\n\"classname\" \"func_wall\"\n\"model\" \"*1\"\n"));
  }

  assets::AssetImporterRegistries registries;
  REQUIRE(bsp::register_builtin_asset_importers(registries));
  WorldRenderPackageStageHarness harness{root, registries, config};
  harness.begin_protocol();
  harness.finish();

  CHECK(harness.stage.state() ==
        goldsrc::WorldRenderPackageStageState::render_package_failed);
  CHECK_FALSE(harness.stage.result());
  CHECK_FALSE(harness.stage.scene_result());
  REQUIRE(harness.stage.error());
  CHECK(harness.stage.error()->code ==
        goldsrc::WorldRenderPackageStageErrorCode::world_scene_build_failed);
  REQUIRE(harness.stage.error()->world_scene_code);
  CHECK(*harness.stage.error()->world_scene_code ==
        (scenario == Scenario::malformed_pvs
             ? goldsrc::brush_models::GoldSrcWorldSceneBuildErrorCode::
                   spatial_package_build_failed
             : goldsrc::brush_models::GoldSrcWorldSceneBuildErrorCode::
                   entity_document_parse_failed));
  CHECK(harness.stage.bsp_scene_parse_count() == 1U);
  CHECK(harness.stage.brush_library_build_count() ==
        (scenario == Scenario::malformed_entity_tail ? 1U : 0U));
  CHECK(harness.stage.world_scene_publication_count() == 0U);
  CHECK(harness.stage.render_package_publication_count() == 0U);
  check_terminal_ownership_and_transport(harness);
}

TEST_CASE("World render package stage preserves typed prerequisite failures",
          "[world-render-package][stage][failure]") {
  enum class Scenario {
    incomplete_texture_set,
    lightmap_range_failure,
    lightmap_atlas_failure,
  };

  Scenario scenario{Scenario::incomplete_texture_set};
  fixture::ScopedLocalResourceTestRoot root;
  auto config = test_config();

  SECTION("incomplete texture set stops before lightmap import") {
    scenario = Scenario::incomplete_texture_set;
    write_stage_prerequisites(root, unresolved_external_bsp());
  }
  SECTION("truncated retained lighting range is rejected transactionally") {
    scenario = Scenario::lightmap_range_failure;
    write_stage_prerequisites(root, embedded_lightmapped_bsp(74U));
  }
  SECTION("a valid but undersized atlas rejects the padded rectangle") {
    scenario = Scenario::lightmap_atlas_failure;
    write_stage_prerequisites(root, embedded_lightmapped_bsp());
    config.lightmaps.atlas_width = 4U;
    config.lightmaps.maximum_atlas_dimension = 4U;
  }

  assets::AssetImporterRegistries registries;
  REQUIRE(bsp::register_builtin_asset_importers(registries));
  WorldRenderPackageStageHarness harness{root, registries, config};
  harness.begin_protocol();
  harness.finish();

  CHECK_FALSE(harness.stage.result());
  REQUIRE(harness.stage.error());
  if (scenario == Scenario::incomplete_texture_set) {
    CHECK(harness.stage.state() ==
          goldsrc::WorldRenderPackageStageState::world_textures_incomplete);
    CHECK(harness.stage.error()->code ==
          goldsrc::WorldRenderPackageStageErrorCode::world_textures_incomplete);
    CHECK_FALSE(harness.stage.error()->world_texture_code);
    CHECK(harness.stage.lightmap_import_count() == 0U);
    CHECK(harness.stage.lightmap_set_publication_count() == 0U);
  } else {
    CHECK(harness.stage.state() ==
          goldsrc::WorldRenderPackageStageState::lightmap_import_failed);
    CHECK(harness.stage.error()->code ==
          goldsrc::WorldRenderPackageStageErrorCode::lightmap_import_failed);
    REQUIRE(harness.stage.error()->lightmap_code);
    CHECK(*harness.stage.error()->lightmap_code ==
          (scenario == Scenario::lightmap_range_failure
               ? goldsrc::lightmaps::GoldSrcWorldLightmapImportErrorCode::
                     lightmap_range_out_of_bounds
               : goldsrc::lightmaps::GoldSrcWorldLightmapImportErrorCode::
                      atlas_rectangle_limit_exceeded));
    CHECK(harness.stage.lightmap_import_count() == 1U);
    CHECK(harness.stage.lightmap_set_publication_count() == 0U);
  }
  CHECK(harness.stage.render_package_publication_count() == 0U);
  check_terminal_ownership_and_transport(harness);
}

TEST_CASE(
    "World render package stage has deterministic outer event backpressure",
    "[world-render-package][stage][backpressure]") {
  fixture::ScopedLocalResourceTestRoot root;
  write_stage_prerequisites(root, embedded_lightmapped_bsp());
  assets::AssetImporterRegistries registries;
  REQUIRE(bsp::register_builtin_asset_importers(registries));
  auto config = test_config();
  config.maximum_stage_events = 4U;
  SECTION("historical M4.3 package boundary") {}
  SECTION("M4.4 spatial-scene continuation requested") {
    config.build_world_spatial_scene = true;
    config.world_scene.brushes =
        goldsrc::brush_models::GoldSrcWorldSceneBrushMode::static_initial;
    config.world_scene.extract_spawn = true;
  }
  WorldRenderPackageStageHarness harness{root, registries, config};
  harness.begin_protocol();
  harness.finish(false);

  CHECK(harness.stage.state() ==
        goldsrc::WorldRenderPackageStageState::backpressure);
  CHECK_FALSE(harness.stage.result());
  REQUIRE(harness.stage.error());
  CHECK(harness.stage.error()->code ==
        goldsrc::WorldRenderPackageStageErrorCode::event_backpressure);
  CHECK(harness.stage.pending_event_count() == 1U);
  CHECK(harness.stage.lightmap_import_count() == 0U);
  CHECK(harness.stage.lightmap_set_publication_count() == 0U);
  CHECK(harness.stage.render_package_publication_count() == 0U);
  CHECK_FALSE(harness.stage.scene_result());
  CHECK_FALSE(harness.stage.spawn_camera_result());
  CHECK(harness.stage.bsp_scene_parse_count() == 0U);
  CHECK(harness.stage.brush_library_build_count() == 0U);
  CHECK(harness.stage.world_scene_publication_count() == 0U);
  check_terminal_ownership_and_transport(harness);
}

TEST_CASE("World render package stage cancellation and timeout are idempotent",
          "[world-render-package][stage][cancel][timeout]") {
  enum class Scenario {
    cancellation,
    timeout,
  };

  Scenario scenario{Scenario::cancellation};
  fixture::ScopedLocalResourceTestRoot root;
  write_stage_prerequisites(root, embedded_lightmapped_bsp());
  auto config = test_config();
  SECTION("historical M4.3 package boundary") {
    SECTION("cooperative cancellation") {
      scenario = Scenario::cancellation;
    }
    SECTION("manual-clock local texture timeout") {
      scenario = Scenario::timeout;
      config.world_textures.texture_import.timeout = 1ms;
    }
  }
  SECTION("M4.4 spatial-scene continuation requested") {
    config.build_world_spatial_scene = true;
    config.world_scene.brushes =
        goldsrc::brush_models::GoldSrcWorldSceneBrushMode::static_initial;
    config.world_scene.extract_spawn = true;
    SECTION("cooperative cancellation") {
      scenario = Scenario::cancellation;
    }
    SECTION("manual-clock local texture timeout") {
      scenario = Scenario::timeout;
      config.world_textures.texture_import.timeout = 1ms;
    }
  }

  assets::AssetImporterRegistries registries;
  REQUIRE(bsp::register_builtin_asset_importers(registries));
  WorldRenderPackageStageHarness harness{root, registries, config};
  harness.begin_protocol();
  if (scenario == Scenario::cancellation) {
    REQUIRE_FALSE(harness.stage.terminal());
    harness.stage.cancel(harness.next_update);
    drain_events(harness.stage, harness.events);
  } else {
    harness.finish();
  }

  REQUIRE(harness.stage.terminal());
  CHECK(harness.stage.state() ==
        (scenario == Scenario::cancellation
             ? goldsrc::WorldRenderPackageStageState::cancelled
             : goldsrc::WorldRenderPackageStageState::timed_out));
  CHECK_FALSE(harness.stage.result());
  CHECK_FALSE(harness.stage.scene_result());
  CHECK_FALSE(harness.stage.spawn_camera_result());
  CHECK(harness.stage.lightmap_set_publication_count() == 0U);
  CHECK(harness.stage.render_package_publication_count() == 0U);
  CHECK(harness.stage.bsp_scene_parse_count() == 0U);
  CHECK(harness.stage.brush_library_build_count() == 0U);
  CHECK(harness.stage.world_scene_publication_count() == 0U);
  if (scenario == Scenario::cancellation) {
    CHECK_FALSE(harness.stage.error());
    CHECK(harness.event_count(
              goldsrc::WorldRenderPackageStageEventType::cancelled) == 1U);
  } else {
    REQUIRE(harness.stage.error());
    REQUIRE(harness.stage.error()->world_texture_code);
    CHECK(*harness.stage.error()->world_texture_code ==
          goldsrc::WorldTextureImportStageErrorCode::texture_import_failed);
  }
  check_terminal_ownership_and_transport(harness);

  const auto state = harness.stage.state();
  const auto cleanup = harness.stage.cleanup_count();
  const auto releases = harness.connection_releases;
  const auto sent = harness.transport.sent.size();
  harness.stage.cancel(harness.next_update + 1s);
  harness.stage.update(harness.next_update + 2s);
  CHECK(harness.stage.state() == state);
  CHECK(harness.stage.cleanup_count() == cleanup);
  CHECK(harness.connection_releases == releases);
  CHECK(harness.transport.sent.size() == sent);
}

} // namespace
