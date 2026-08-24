#include "delta_test_fixture.hpp"
#include "local_resource_readiness_test_fixture.hpp"
#include "local_resource_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "resource_client_response_test_fixture.hpp"
#include "resource_list_test_fixture.hpp"
#include "synthetic_goldsrc_bsp_fixture.hpp"
#include "synthetic_goldsrc_wad3_fixture.hpp"
#include "user_info_test_fixture.hpp"

#include <hlclient/assets/asset_importer_registry.hpp>
#include <hlclient/goldsrc/bsp/goldsrc_bsp_world_importer.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/world_textures/world_texture_import_stage.hpp>
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
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
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
            std::vector<std::byte>{payload.begin(), payload.end()}});
        return {network::DatagramSendStatus::sent, {}};
    }

    [[nodiscard]] network::DatagramTransportReceiveResult receive(
        std::size_t) override
    {
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

    void queue(
        const network::NetworkAddress source,
        std::vector<std::byte> payload)
    {
        const auto byte_count = payload.size();
        incoming.push_back({
            network::DatagramTransportReceiveStatus::received,
            network::Datagram{source, std::move(payload)},
            source,
            byte_count,
            {},
        });
    }

    network::NetworkAddress local{network::NetworkAddress::loopback(31'780U)};
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
        std::size_t& updates,
        std::size_t& cancellations,
        std::size_t& lifetime_releases) noexcept
        : updates_{updates},
          cancellations_{cancellations},
          lifetime_releases_{lifetime_releases}
    {
    }

    [[nodiscard]] consistency::ResourceConsistencyUpdateResult update() override
    {
        ++updates_;
        auto material = consistency::make_resource_consistency_material(
            0x01020304U, response_fixture::kSyntheticOpaqueMaterial);
        REQUIRE(material);
        return consistency::ResourceConsistencyUpdateResult::succeeded(
            consistency::ResourceConsistencySession{
                std::move(*material.material),
                std::make_unique<CountingConsistencyLifetime>(
                    lifetime_releases_)});
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

class ImmediateConsistencyProvider final
    : public consistency::IResourceConsistencyProvider {
public:
    [[nodiscard]] consistency::ResourceConsistencyBeginResult begin(
        const consistency::ResourceConsistencyRequirements&) override
    {
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
    std::ranges::transform(semantic_payload,
        std::back_inserter(source),
        [](const std::byte value) {
            return static_cast<char>(std::to_integer<std::uint8_t>(value));
        });
    const auto bound = source.size() + source.size() / 100U + 601U;
    REQUIRE(bound <= (std::numeric_limits<unsigned int>::max)());
    std::vector<char> compressed(bound);
    auto compressed_size = static_cast<unsigned int>(compressed.size());
    REQUIRE(BZ2_bzBuffToBuffCompress(compressed.data(),
                &compressed_size,
                source.data(),
                static_cast<unsigned int>(source.size()),
                9,
                0,
                30) == BZ_OK);
    compressed.resize(compressed_size);

    std::vector<std::byte> envelope{
        std::byte{0x42U},
        std::byte{0x5AU},
        std::byte{0x32U},
        std::byte{0U}};
    std::ranges::transform(compressed,
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
        std::move(payload)};
    auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return std::move(*encoded.datagram);
}

[[nodiscard]] goldsrc::ClientToServerNetchanPacket decode_sent(
    const SentDatagram& datagram)
{
    const auto decoded =
        goldsrc::decode_client_to_server_netchan_packet(datagram.payload);
    REQUIRE(decoded);
    REQUIRE(decoded.packet);
    return *decoded.packet;
}

[[nodiscard]] goldsrc::WorldTextureImportStageConfig test_config()
{
    goldsrc::WorldTextureImportStageConfig config;
    auto& asset = config.asset_dispatch;
    auto& transition = asset.manifest.response.resource_list.transition;
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
    asset.manifest.response.resource_list.maximum_stage_events = 64U;
    asset.manifest.response.maximum_driver_events_per_update = 64U;
    asset.manifest.response.response.maximum_response_stage_events = 64U;
    asset.manifest.manifest.maximum_manifest_events = 64U;
    asset.maximum_stage_events = 128U;
    asset.source_open.read_chunk_bytes = 7U;
    asset.source_open.maximum_chunks_per_update = 1U;
    config.texture_import.wad_source_open.read_chunk_bytes = 5U;
    config.texture_import.wad_source_open.maximum_chunks_per_update = 1U;
    config.texture_import.maximum_pixel_conversion_bytes_per_update = 4U;
    config.maximum_stage_events = 128U;
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
    post_delta.insert(post_delta.end(),
        user_fixture::kExactUserInfoMessage.begin(),
        user_fixture::kExactUserInfoMessage.end());
    return delta_fixture::service_payload(
        schemas(), goldsrc::kMoveVarsOpcode, post_delta, "maps/textured.bsp");
}

[[nodiscard]] std::vector<std::byte> resource_semantic_payload()
{
    constexpr std::array prefix{
        std::byte{45U},
        std::byte{1U},
        std::byte{0U},
        std::byte{0U},
        std::byte{0U},
        std::byte{0U},
        std::byte{0U},
        std::byte{0U},
        std::byte{0U}};
    const std::array entries{
        resource_list_test_fixture::EntrySpec{
            2U, "maps/textured.bsp", 37U, 0x00FF'FFFFU, 0U},
        resource_list_test_fixture::EntrySpec{
            2U, "models/test_model.mdl", 9U, 1U, 0U},
        resource_list_test_fixture::EntrySpec{
            0U, "test_sound.wav", 4U, 2U, 0U}};
    const auto message = resource_list_test_fixture::make_message(entries);
    std::vector<std::byte> payload{prefix.begin(), prefix.end()};
    payload.insert(payload.end(), message.bytes.begin(), message.bytes.end());
    return payload;
}

[[nodiscard]] std::vector<std::byte> bytes_of(const std::string_view text)
{
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

[[nodiscard]] std::vector<std::byte> external_textured_bsp(
    const std::string_view texture_name = "STAGE_WAD",
    const std::string_view wad_declaration =
        R"(Q:\compiler\private\stage.wad;)")
{
    fixture::SyntheticBspBuilder builder;
    std::string entities{"{\n\"classname\" \"worldspawn\"\n"};
    if (!wad_declaration.empty()) {
        entities += "\"_wad\" \"";
        entities.append(wad_declaration);
        entities += "\"\n";
    }
    entities += "}\n";
    builder.lump(fixture::SyntheticBspLumpId::entities) = bytes_of(entities);
    auto texture = fixture::synthetic_external_texture(texture_name);
    texture.width = 16U;
    texture.height = 16U;
    const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U>
        textures{texture};
    builder.set_texture_directory(textures);
    return builder.build();
}

void configure_two_material_geometry(fixture::SyntheticBspBuilder& builder)
{
    constexpr std::array vertices{
        fixture::SyntheticBspVector3{0.0F, 0.0F, 0.0F},
        fixture::SyntheticBspVector3{64.0F, 0.0F, 0.0F},
        fixture::SyntheticBspVector3{64.0F, 64.0F, 0.0F},
        fixture::SyntheticBspVector3{0.0F, 64.0F, 0.0F},
    };
    constexpr std::array edges{
        fixture::SyntheticBspEdge{0U, 0U},
        fixture::SyntheticBspEdge{0U, 1U},
        fixture::SyntheticBspEdge{1U, 2U},
        fixture::SyntheticBspEdge{2U, 0U},
        fixture::SyntheticBspEdge{0U, 2U},
        fixture::SyntheticBspEdge{2U, 3U},
        fixture::SyntheticBspEdge{3U, 0U},
    };
    constexpr std::array<std::int32_t, 6U> surfedges{1, 2, 3, 4, 5, 6};
    std::array faces{
        fixture::SyntheticBspFace{},
        fixture::SyntheticBspFace{},
    };
    faces[0U].surfedge_count = 3;
    faces[1U].first_surfedge = 3;
    faces[1U].surfedge_count = 3;
    faces[1U].texinfo_index = 1;
    faces[0U].light_styles[0U] = 0U;
    faces[1U].light_styles[0U] = 0U;
    faces[0U].light_offset = 0;
    faces[1U].light_offset = 0;
    auto node = fixture::SyntheticBspNode{};
    node.face_count = 2U;
    std::array leaves{
        fixture::SyntheticBspLeaf{},
        fixture::SyntheticBspLeaf{},
    };
    leaves[0U].contents = -2;
    leaves[0U].marksurface_count = 0U;
    leaves[1U].marksurface_count = 2U;
    constexpr std::array<std::uint16_t, 2U> marksurfaces{0U, 1U};
    auto model = fixture::SyntheticBspModel{};
    model.face_count = 2;
    std::array texinfo{
        fixture::SyntheticBspTexinfo{},
        fixture::SyntheticBspTexinfo{},
    };
    texinfo[1U].miptex_index = 1;
    builder.lump(fixture::SyntheticBspLumpId::lighting) = {
        std::byte{0x11U}, std::byte{0x22U}, std::byte{0x33U}};

    builder.set_vertices(vertices)
        .set_edges(edges)
        .set_surfedges(surfedges)
        .set_faces(faces)
        .set_nodes(std::span{&node, 1U})
        .set_leaves(leaves)
        .set_marksurfaces(marksurfaces)
        .set_models(std::span{&model, 1U})
        .set_texinfo(texinfo);
}

void populate_embedded_palette(
    std::vector<std::byte>& bsp_bytes,
    const std::size_t texture_ordinal,
    const std::uint16_t palette_count = 256U)
{
    const auto texture_lump = static_cast<std::size_t>(
        fixture::synthetic_read_i32le(
            bsp_bytes,
            fixture::synthetic_lump_descriptor_offset(
                fixture::SyntheticBspLumpId::textures)));
    const auto record_relative = static_cast<std::size_t>(
        fixture::synthetic_read_i32le(
            bsp_bytes,
            texture_lump + 4U + (texture_ordinal * 4U)));
    const auto record = texture_lump + record_relative;
    constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
    const auto count_offset = record + 40U + pixel_byte_count;
    fixture::synthetic_write_u16le(bsp_bytes, count_offset, palette_count);
    for (std::size_t index = 0U; index < 256U; ++index) {
        bsp_bytes[count_offset + 2U + (index * 3U)] =
            static_cast<std::byte>(index);
        bsp_bytes[count_offset + 2U + (index * 3U) + 1U] =
            static_cast<std::byte>(255U - index);
        bsp_bytes[count_offset + 2U + (index * 3U) + 2U] =
            static_cast<std::byte>(index ^ 0x5AU);
    }
}

[[nodiscard]] std::vector<std::byte> embedded_textured_bsp(
    const bool malformed_palette = false,
    const std::string_view wad_declaration = {})
{
    fixture::SyntheticBspBuilder builder;
    std::string entities{"{\n\"classname\" \"worldspawn\"\n"};
    if (!wad_declaration.empty()) {
        entities += "\"_wad\" \"";
        entities.append(wad_declaration);
        entities += "\"\n";
    }
    entities += "}\n";
    builder.lump(fixture::SyntheticBspLumpId::entities) = bytes_of(entities);
    auto embedded =
        fixture::synthetic_embedded_texture("EMBEDDED", 16U, 16U);
    constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
    embedded.trailing_byte_count = pixel_byte_count + 2U + (256U * 3U);
    const std::array<std::optional<fixture::SyntheticBspMipTexture>, 1U>
        textures{embedded};
    builder.set_texture_directory(textures);
    auto bytes = builder.build();
    populate_embedded_palette(bytes, 0U, malformed_palette ? 255U : 256U);
    return bytes;
}

[[nodiscard]] std::vector<std::byte> mixed_textured_bsp()
{
    fixture::SyntheticBspBuilder builder;
    configure_two_material_geometry(builder);
    builder.lump(fixture::SyntheticBspLumpId::entities) = bytes_of(
        "{\n\"classname\" \"worldspawn\"\n"
        "\"_wad\" \"C:\\compiler\\stage.wad;\"\n}\n");
    auto embedded =
        fixture::synthetic_embedded_texture("EMBEDDED", 16U, 16U);
    constexpr std::size_t pixel_byte_count = 256U + 64U + 16U + 4U;
    embedded.trailing_byte_count = pixel_byte_count + 2U + (256U * 3U);
    auto external = fixture::synthetic_external_texture("STAGE_WAD");
    external.width = 16U;
    external.height = 16U;
    const std::array<std::optional<fixture::SyntheticBspMipTexture>, 2U>
        textures{embedded, external};
    builder.set_texture_directory(textures);
    auto bytes = builder.build();
    populate_embedded_palette(bytes, 0U);
    return bytes;
}

[[nodiscard]] std::shared_ptr<const local_resources::LocalResourceEnvironment>
shared_environment(const fixture::ScopedLocalResourceTestRoot& root)
{
    auto environment = readiness_fixture::make_environment(root);
    return std::shared_ptr<const local_resources::LocalResourceEnvironment>{
        std::move(environment)};
}

void drain_events(
    goldsrc::WorldTextureImportStage& stage,
    std::vector<goldsrc::WorldTextureImportStageEvent>& events)
{
    while (auto event = stage.poll_event()) {
        events.push_back(std::move(*event));
    }
}

class AlwaysModelImporter final : public assets::IModelImporter {
public:
    [[nodiscard]] std::string_view id() const noexcept override
    {
        return "stage-wrong-model";
    }

    [[nodiscard]] assets::AssetProbeConfidence probe(
        const assets::AssetProbe& probe) const noexcept override
    {
        return probe.structural_bytes.empty() ? assets::kAssetProbeNoMatch
                                              : 1'000U;
    }

    [[nodiscard]] assets::ModelAssetResult import(
        const assets::AssetSource&) const override
    {
        assets::ModelAsset model;
        model.identity.source_name = "stage-wrong-model";
        model.vertices.resize(3U);
        model.indices = {0U, 1U, 2U};
        return assets::ModelAssetResult::success(std::move(model));
    }
};

struct RootSnapshotEntry {
    std::string relative_name;
    std::uintmax_t byte_count{0U};
    std::filesystem::file_time_type write_time{};
    std::vector<std::byte> contents;
    bool directory{false};

    friend bool operator==(
        const RootSnapshotEntry&,
        const RootSnapshotEntry&) = default;
};

[[nodiscard]] std::vector<RootSnapshotEntry> snapshot_root(
    const std::filesystem::path& root)
{
    std::vector<RootSnapshotEntry> snapshot;
    for (const auto& entry :
        std::filesystem::recursive_directory_iterator{root}) {
        std::error_code error;
        const bool directory = entry.is_directory(error);
        REQUIRE_FALSE(error);
        const bool regular = entry.is_regular_file(error);
        REQUIRE_FALSE(error);

        RootSnapshotEntry item;
        item.relative_name =
            std::filesystem::relative(entry.path(), root).generic_string();
        item.directory = directory;
        if (regular) {
            item.byte_count = entry.file_size(error);
            REQUIRE_FALSE(error);
            item.write_time = entry.last_write_time(error);
            REQUIRE_FALSE(error);
            std::ifstream stream{entry.path(), std::ios::binary};
            REQUIRE(stream);
            const std::vector<char> characters{
                std::istreambuf_iterator<char>{stream},
                std::istreambuf_iterator<char>{}};
            REQUIRE_FALSE(stream.bad());
            item.contents.reserve(characters.size());
            std::ranges::transform(
                characters,
                std::back_inserter(item.contents),
                [](const char value) {
                    return static_cast<std::byte>(
                        static_cast<unsigned char>(value));
                });
        }
        snapshot.push_back(std::move(item));
    }
    std::ranges::sort(snapshot, {}, &RootSnapshotEntry::relative_name);
    return snapshot;
}

struct RecordedTextureTrace {
    goldsrc::WorldTextureImportTraceEvent event;
    std::size_t update_ordinal{0U};
};

class WorldTextureStageHarness final {
public:
    WorldTextureStageHarness(
        const fixture::ScopedLocalResourceTestRoot& root,
        const assets::AssetImporterRegistries& registries,
        goldsrc::WorldTextureImportStageConfig config = test_config())
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
              [this](const goldsrc::WorldTextureImportTraceEvent& event) {
                  traces.push_back(RecordedTextureTrace{
                      event, current_update_ordinal});
              }}
    {
    }

    void begin_protocol()
    {
        REQUIRE(stage.start(epoch,
            transport.local,
            std::make_unique<CountingConnectionLifetime>(
                connection_releases)));
        update_at(epoch + 1ms, false);
        REQUIRE(transport.sent.size() == 1U);
        const auto initial = decode_sent(transport.sent.front());

        transport.queue(remote,
            server_packet(1U,
                true,
                initial.header.sequence.sequence.value(),
                true,
                service_envelope(first_semantic_payload())));
        update_at(epoch + 2ms, false);
        update_at(epoch + 3ms, false);
        REQUIRE(transport.sent.size() >= 3U);
        const auto transition = decode_sent(transport.sent.back());

        transport.queue(remote,
            server_packet(2U,
                false,
                transition.header.sequence.sequence.value(),
                false,
                service_envelope(resource_semantic_payload())));
        update_at(epoch + 4ms, false);
        REQUIRE_FALSE(transport.sent.empty());
        const auto response = decode_sent(transport.sent.back());
        REQUIRE(response.header.sequence.flags.reliable);
        REQUIRE(response.header.sequence.flags.fragmented);

        constexpr std::array spawn{
            std::byte{0x03U},
            std::byte{'s'},
            std::byte{'p'},
            std::byte{'a'},
            std::byte{'w'},
            std::byte{'n'},
            std::byte{0U}};
        transport.queue(remote,
            server_packet(3U,
                false,
                response.header.sequence.sequence.value(),
                true,
                service_envelope(spawn)));
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

    void update_once(const bool drain = true)
    {
        update_at(next_update, drain);
        next_update += 1ms;
    }

    void reach_texture_import()
    {
        for (std::size_t update = 0U;
             update < 4'096U &&
             stage.state() ==
                 goldsrc::WorldTextureImportStageState::
                     waiting_for_world_geometry;
             ++update) {
            update_once();
        }
        REQUIRE(stage.state() !=
            goldsrc::WorldTextureImportStageState::waiting_for_world_geometry);
    }

    void finish(const bool drain = true)
    {
        for (std::size_t update = 0U;
             update < 8'192U && !stage.terminal();
             ++update) {
            update_once(drain);
        }
        REQUIRE(stage.terminal());
        if (drain) {
            drain_events(stage, events);
        }
    }

    [[nodiscard]] std::size_t trace_count(
        const goldsrc::WorldTextureImportTraceClassification classification)
        const
    {
        return static_cast<std::size_t>(std::ranges::count_if(
            traces,
            [classification](const RecordedTextureTrace& trace) {
                return trace.event.classification == classification;
            }));
    }

    [[nodiscard]] const RecordedTextureTrace* first_trace(
        const goldsrc::WorldTextureImportTraceClassification classification)
        const
    {
        const auto found = std::ranges::find_if(
            traces,
            [classification](const RecordedTextureTrace& trace) {
                return trace.event.classification == classification;
            });
        return found == traces.end() ? nullptr : &*found;
    }

    FakeTransport transport;
    network::NetworkAddress remote{network::NetworkAddress::loopback(27'042U)};
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment;
    ImmediateConsistencyProvider provider;
    std::size_t connection_releases{0U};
    std::vector<goldsrc::WorldTextureImportStageEvent> events;
    std::vector<RecordedTextureTrace> traces;
    std::size_t current_update_ordinal{0U};
    std::size_t sent_at_manifest{0U};
    const goldsrc::WorldTextureImportStageTimePoint epoch{};
    goldsrc::WorldTextureImportStageTimePoint next_update{};
    goldsrc::WorldTextureImportStage stage;

private:
    void update_at(
        const goldsrc::WorldTextureImportStageTimePoint now,
        const bool drain)
    {
        stage.update(now);
        ++current_update_ordinal;
        if (drain) {
            drain_events(stage, events);
        }
    }
};

void write_stage_prerequisites(
    const fixture::ScopedLocalResourceTestRoot& root,
    const std::span<const std::byte> bsp_bytes)
{
    root.write("valve", "maps/textured.bsp", bsp_bytes);
    root.write("valve", "models/test_model.mdl", "model");
    root.write("valve", "sound/test_sound.wav", "sound");
}

void check_terminal_ownership_and_transport(
    const WorldTextureStageHarness& harness)
{
    CHECK(harness.stage.cleanup_count() == 1U);
    CHECK(harness.connection_releases == 1U);
    CHECK(harness.provider.begin_count == 1U);
    CHECK(harness.provider.update_count == 1U);
    CHECK(harness.provider.cancel_count == 0U);
    CHECK(harness.provider.lifetime_releases == 1U);
    CHECK(harness.stage.manifest_publication_count() == 1U);
    CHECK(harness.stage.transmitted_packet_count() ==
        harness.sent_at_manifest);
    CHECK(harness.transport.sent.size() == harness.sent_at_manifest);
    CHECK(std::ranges::all_of(harness.transport.sent,
        [&harness](const SentDatagram& datagram) {
            return datagram.destination == harness.remote;
        }));
    REQUIRE(harness.stage.local_endpoint());
    CHECK(*harness.stage.local_endpoint() == harness.transport.local);
    CHECK(harness.stage.remote_endpoint() == harness.remote);
}

template<class Type>
concept HasRendererHandle = requires(const Type& value) {
    value.renderer_handle();
};

template<class Type>
concept HasDecodedLightmaps = requires(const Type& value) {
    value.lightmaps();
};

TEST_CASE("World texture stage retains one session and releases its upstream "
          "authentication owner once",
    "[world-textures][stage][lifecycle]")
{
    fixture::ScopedLocalResourceTestRoot root;
    write_stage_prerequisites(root, external_textured_bsp());
    root.write("valve", "stage.wad",
        fixture::synthetic_valid_wad3("STAGE_WAD").bytes);

    assets::AssetImporterRegistries registries;
    REQUIRE(bsp::register_builtin_asset_importers(registries));
    WorldTextureStageHarness harness{root, registries};
    harness.begin_protocol();
    bool observed_local_continuation = false;
    for (std::size_t update = 0U;
         update < 8'192U && !harness.stage.terminal();
         ++update) {
        harness.update_once();
        if (harness.stage.state() !=
                goldsrc::WorldTextureImportStageState::
                    waiting_for_world_geometry &&
            !harness.stage.terminal()) {
            observed_local_continuation = true;
            CHECK(harness.connection_releases == 0U);
            CHECK(harness.stage.transmitted_packet_count() ==
                harness.sent_at_manifest);
            CHECK(harness.transport.sent.size() == harness.sent_at_manifest);
        }
    }

    REQUIRE(harness.stage.terminal());
    REQUIRE(harness.stage.state() ==
        goldsrc::WorldTextureImportStageState::world_textures_ready);
    REQUIRE(harness.stage.result());
    CHECK_FALSE(harness.stage.error());
    CHECK(observed_local_continuation);
    CHECK(harness.stage.bsp_source_open_attempt_count() == 1U);
    CHECK(harness.stage.importer_dispatch_count() == 1U);
    CHECK(harness.stage.wad_source_open_attempt_count() == 1U);
    CHECK(harness.stage.texture_set_publication_count() == 1U);
    CHECK(harness.trace_count(
              goldsrc::WorldTextureImportTraceClassification::
                  texture_import_progress) > 1U);

    const auto geometry_event = std::ranges::find_if(harness.events,
        [](const goldsrc::WorldTextureImportStageEvent& event) {
            return event.type == goldsrc::WorldTextureImportStageEventType::
                                     world_geometry_ready;
        });
    REQUIRE(geometry_event != harness.events.end());
    CHECK(geometry_event->material_count == 1U);
    CHECK(std::ranges::count_if(harness.events,
              [](const goldsrc::WorldTextureImportStageEvent& event) {
                  return event.type == goldsrc::
                                           WorldTextureImportStageEventType::
                                               wad_source_open_started;
              }) == 1);
    CHECK(std::ranges::count_if(harness.events,
              [](const goldsrc::WorldTextureImportStageEvent& event) {
                  return event.type == goldsrc::
                                           WorldTextureImportStageEventType::
                                               wad_source_ready;
              }) == 1);

    const auto& result = *harness.stage.result();
    CHECK(result.environment() != nullptr);
    CHECK(result.world().source_profile ==
        assets::WorldGeometrySourceProfile::goldsrc_bsp_v30);
    REQUIRE(result.textures().complete_for_world_materials());
    REQUIRE(result.textures().texture_count() == 1U);
    REQUIRE(result.textures().binding_count() == 1U);
    CHECK(result.textures().bindings()[0].status ==
        assets::WorldMaterialTextureBindingStatus::resolved_wad3);
    CHECK(result.textures().textures()[0].source_kind ==
        assets::WorldTextureSourceKind::external_wad3);
    CHECK(result.textures().textures()[0].mip_levels.size() == 4U);
    check_terminal_ownership_and_transport(harness);

    const auto terminal_sent = harness.transport.sent.size();
    const auto terminal_cleanup = harness.stage.cleanup_count();
    const auto terminal_releases = harness.connection_releases;
    const auto terminal_wad_opens =
        harness.stage.wad_source_open_attempt_count();
    harness.stage.update(harness.next_update + 5s);
    harness.stage.cancel(harness.next_update + 6s);
    harness.stage.cancel(harness.next_update + 7s);
    CHECK(harness.stage.state() ==
        goldsrc::WorldTextureImportStageState::world_textures_ready);
    CHECK(harness.transport.sent.size() == terminal_sent);
    CHECK(harness.stage.cleanup_count() == terminal_cleanup);
    CHECK(harness.connection_releases == terminal_releases);
    CHECK(harness.stage.wad_source_open_attempt_count() == terminal_wad_opens);
    CHECK(harness.stage.texture_set_publication_count() == 1U);
}

TEST_CASE("World texture stage validates the owning imported-world prerequisite",
    "[world-textures][stage][prerequisite]")
{
    enum class Scenario {
        valid_world,
        wrong_asset_variant,
        missing_bsp_source,
    };

    Scenario scenario{Scenario::valid_world};
    fixture::ScopedLocalResourceTestRoot root;
    assets::AssetImporterRegistries registries;

    SECTION("valid imported WorldAsset and approved BSP source")
    {
        scenario = Scenario::valid_world;
        write_stage_prerequisites(root, embedded_textured_bsp());
        REQUIRE(bsp::register_builtin_asset_importers(registries));
    }
    SECTION("wrong imported asset variant is rejected")
    {
        scenario = Scenario::wrong_asset_variant;
        write_stage_prerequisites(root, embedded_textured_bsp());
        REQUIRE(registries.models.register_importer(
            std::make_unique<AlwaysModelImporter>()));
    }
    SECTION("missing approved BSP source is rejected before import")
    {
        scenario = Scenario::missing_bsp_source;
        root.write("valve", "models/test_model.mdl", "model");
        root.write("valve", "sound/test_sound.wav", "sound");
        REQUIRE(bsp::register_builtin_asset_importers(registries));
    }

    WorldTextureStageHarness harness{root, registries};
    harness.begin_protocol();
    harness.finish();

    if (scenario == Scenario::valid_world) {
        REQUIRE(harness.stage.state() ==
            goldsrc::WorldTextureImportStageState::world_textures_ready);
        REQUIRE(harness.stage.result());
        const auto& textured = *harness.stage.result();
        REQUIRE(textured.dispatch_state().imported_asset());
        CHECK(std::holds_alternative<assets::WorldAsset>(
            *textured.dispatch_state().imported_asset()));
        CHECK(textured.dispatch_state().source_byte_count() > 0U);
        CHECK(textured.environment().get() == harness.environment.get());
        CHECK(harness.stage.bsp_source_open_attempt_count() == 1U);
        CHECK(harness.stage.importer_dispatch_count() == 1U);
        CHECK(harness.stage.texture_set_publication_count() == 1U);
    } else {
        CHECK(harness.stage.state() ==
            goldsrc::WorldTextureImportStageState::
                world_geometry_unavailable);
        CHECK_FALSE(harness.stage.result());
        REQUIRE(harness.stage.error());
        CHECK(harness.stage.error()->code ==
            goldsrc::WorldTextureImportStageErrorCode::
                asset_dispatch_failed);
        if (scenario == Scenario::missing_bsp_source) {
            REQUIRE(harness.stage.error()->asset_dispatch_code);
            CHECK(*harness.stage.error()->asset_dispatch_code ==
                goldsrc::PrecacheAssetDispatchStageErrorCode::
                    world_source_unavailable);
        }
        CHECK(harness.stage.texture_set_publication_count() == 0U);
        CHECK(harness.stage.bsp_source_open_attempt_count() ==
            (scenario == Scenario::wrong_asset_variant ? 1U : 0U));
    }
    check_terminal_ownership_and_transport(harness);
}

TEST_CASE("World texture stage publishes complete embedded external and mixed sets",
    "[world-textures][stage][complete]")
{
    enum class Scenario {
        embedded_only,
        external_only,
        mixed,
    };

    Scenario scenario{Scenario::embedded_only};
    fixture::ScopedLocalResourceTestRoot root;
    std::vector<std::byte> bsp_bytes;

    SECTION("embedded-only texture set")
    {
        scenario = Scenario::embedded_only;
        bsp_bytes = embedded_textured_bsp(
            false, R"(C:\compiler\unused.wad;)");
        root.write("valve", "unused.wad", "not opened");
    }
    SECTION("external WAD3 texture set")
    {
        scenario = Scenario::external_only;
        bsp_bytes = external_textured_bsp();
        root.write("valve", "stage.wad",
            fixture::synthetic_valid_wad3("STAGE_WAD").bytes);
    }
    SECTION("mixed embedded and external texture set")
    {
        scenario = Scenario::mixed;
        bsp_bytes = mixed_textured_bsp();
        root.write("valve", "stage.wad",
            fixture::synthetic_valid_wad3("STAGE_WAD").bytes);
    }

    write_stage_prerequisites(root, bsp_bytes);
    assets::AssetImporterRegistries registries;
    REQUIRE(bsp::register_builtin_asset_importers(registries));
    WorldTextureStageHarness harness{root, registries};
    harness.begin_protocol();
    harness.finish();

    REQUIRE(harness.stage.state() ==
        goldsrc::WorldTextureImportStageState::world_textures_ready);
    REQUIRE(harness.stage.result());
    CHECK_FALSE(harness.stage.error());
    const auto& textures = harness.stage.result()->textures();
    REQUIRE(textures.complete_for_world_materials());
    const auto expected_count = scenario == Scenario::mixed ? 2U : 1U;
    CHECK(textures.texture_count() == expected_count);
    CHECK(textures.binding_count() == expected_count);
    CHECK(textures.statistics().unresolved_material_count == 0U);
    CHECK(harness.stage.texture_set_publication_count() == 1U);

    REQUIRE_FALSE(textures.bindings().empty());
    if (scenario == Scenario::external_only) {
        CHECK(textures.bindings()[0U].status ==
            assets::WorldMaterialTextureBindingStatus::resolved_wad3);
        CHECK(textures.textures()[0U].source_kind ==
            assets::WorldTextureSourceKind::external_wad3);
    } else {
        CHECK(textures.bindings()[0U].status ==
            assets::WorldMaterialTextureBindingStatus::resolved_embedded);
        CHECK(textures.textures()[0U].source_kind ==
            assets::WorldTextureSourceKind::embedded_bsp);
    }
    if (scenario == Scenario::mixed) {
        REQUIRE(textures.bindings().size() == 2U);
        CHECK(textures.bindings()[1U].status ==
            assets::WorldMaterialTextureBindingStatus::resolved_wad3);
        CHECK(textures.textures()[1U].source_kind ==
            assets::WorldTextureSourceKind::external_wad3);
    }
    CHECK(harness.stage.wad_source_open_attempt_count() ==
        (scenario == Scenario::embedded_only ? 0U : 1U));
    check_terminal_ownership_and_transport(harness);
}

TEST_CASE("World texture stage publishes typed incomplete texture sets",
    "[world-textures][stage][incomplete]")
{
    enum class Scenario {
        missing_wad,
        missing_texture,
        dimension_mismatch,
    };

    Scenario scenario{Scenario::missing_wad};
    auto expected_status = assets::WorldMaterialTextureBindingStatus::
        external_wad_archive_missing;
    fixture::ScopedLocalResourceTestRoot root;
    std::vector<std::byte> bsp_bytes;

    SECTION("missing WAD publishes an incomplete set")
    {
        scenario = Scenario::missing_wad;
        expected_status = assets::WorldMaterialTextureBindingStatus::
            external_wad_archive_missing;
        bsp_bytes = external_textured_bsp(
            "ABSENT", R"(D:\compiler\absent.wad;)");
    }
    SECTION("missing texture in a valid WAD publishes an incomplete set")
    {
        scenario = Scenario::missing_texture;
        expected_status = assets::WorldMaterialTextureBindingStatus::
            external_texture_not_found;
        bsp_bytes = external_textured_bsp(
            "ABSENT", R"(D:\compiler\valid.wad;)");
        root.write("valve", "valid.wad",
            fixture::synthetic_valid_wad3("OTHER").bytes);
    }
    SECTION("external dimension mismatch publishes an incomplete set")
    {
        scenario = Scenario::dimension_mismatch;
        expected_status = assets::WorldMaterialTextureBindingStatus::
            external_texture_dimension_mismatch;
        bsp_bytes = external_textured_bsp(
            "SIZED", R"(D:\compiler\sized.wad;)");
        fixture::SyntheticWad3Entry entry;
        entry.name = "SIZED";
        entry.payload =
            fixture::synthetic_goldsrc_miptex("SIZED", 32U, 16U);
        root.write("valve", "sized.wad",
            fixture::synthetic_wad3({std::move(entry)}).bytes);
    }

    write_stage_prerequisites(root, bsp_bytes);
    assets::AssetImporterRegistries registries;
    REQUIRE(bsp::register_builtin_asset_importers(registries));
    WorldTextureStageHarness harness{root, registries};
    harness.begin_protocol();
    harness.finish();

    REQUIRE(harness.stage.state() ==
        goldsrc::WorldTextureImportStageState::world_textures_incomplete);
    REQUIRE(harness.stage.result());
    CHECK_FALSE(harness.stage.error());
    const auto& textures = harness.stage.result()->textures();
    CHECK_FALSE(textures.complete_for_world_materials());
    CHECK(textures.texture_count() == 0U);
    REQUIRE(textures.binding_count() == 1U);
    CHECK(textures.bindings()[0U].status == expected_status);
    CHECK(textures.statistics().unresolved_material_count == 1U);
    CHECK(textures.statistics().dimension_mismatch_count ==
        (scenario == Scenario::dimension_mismatch ? 1U : 0U));
    CHECK(harness.stage.wad_source_open_attempt_count() ==
        (scenario == Scenario::missing_wad ? 0U : 1U));
    CHECK(harness.stage.texture_set_publication_count() == 1U);
    check_terminal_ownership_and_transport(harness);
}

TEST_CASE("World texture stage rejects malformed texture inputs transactionally",
    "[world-textures][stage][fatal]")
{
    enum class Scenario {
        malformed_embedded,
        malformed_wad_catalog,
    };

    Scenario scenario{Scenario::malformed_embedded};
    fixture::ScopedLocalResourceTestRoot root;
    std::vector<std::byte> bsp_bytes;

    SECTION("malformed embedded miptex")
    {
        scenario = Scenario::malformed_embedded;
        bsp_bytes = embedded_textured_bsp(true);
    }
    SECTION("malformed WAD3 catalog")
    {
        scenario = Scenario::malformed_wad_catalog;
        bsp_bytes = external_textured_bsp(
            "BROKEN", R"(C:\compiler\broken.wad;)");
        auto malformed = fixture::synthetic_valid_wad3("BROKEN").bytes;
        REQUIRE_FALSE(malformed.empty());
        malformed[0U] = std::byte{'X'};
        root.write("valve", "broken.wad", malformed);
    }

    write_stage_prerequisites(root, bsp_bytes);
    assets::AssetImporterRegistries registries;
    REQUIRE(bsp::register_builtin_asset_importers(registries));
    WorldTextureStageHarness harness{root, registries};
    harness.begin_protocol();
    harness.finish();

    CHECK(harness.stage.state() ==
        (scenario == Scenario::malformed_embedded
                ? goldsrc::WorldTextureImportStageState::texture_decode_failed
                : goldsrc::WorldTextureImportStageState::wad_catalog_failed));
    CHECK_FALSE(harness.stage.result());
    REQUIRE(harness.stage.error());
    CHECK(harness.stage.error()->code ==
        goldsrc::WorldTextureImportStageErrorCode::texture_import_failed);
    REQUIRE(harness.stage.error()->texture_import_code);
    CHECK(*harness.stage.error()->texture_import_code ==
        (scenario == Scenario::malformed_embedded
                ? goldsrc::WorldTextureImportErrorCode::
                      bsp_texture_source_parse_failed
                : goldsrc::WorldTextureImportErrorCode::wad_catalog_failed));
    CHECK(harness.stage.texture_set_publication_count() == 0U);
    check_terminal_ownership_and_transport(harness);
}

TEST_CASE("World texture stage exposes bounded incremental WAD and pixel progress",
    "[world-textures][stage][incremental]")
{
    fixture::ScopedLocalResourceTestRoot root;
    assets::AssetImporterRegistries registries;
    REQUIRE(bsp::register_builtin_asset_importers(registries));
    auto config = test_config();

    SECTION("WAD source is read over multiple caller updates")
    {
        const auto wad = fixture::synthetic_valid_wad3("STAGE_WAD");
        write_stage_prerequisites(root, external_textured_bsp());
        root.write("valve", "stage.wad", wad.bytes);
        config.texture_import.wad_source_open.read_chunk_bytes = 5U;
        config.texture_import.wad_source_open.maximum_chunks_per_update = 1U;
        WorldTextureStageHarness harness{root, registries, config};
        harness.begin_protocol();
        harness.finish();

        const auto* started = harness.first_trace(
            goldsrc::WorldTextureImportTraceClassification::
                wad_source_open_started);
        const auto* ready = harness.first_trace(
            goldsrc::WorldTextureImportTraceClassification::wad_source_ready);
        REQUIRE(started != nullptr);
        REQUIRE(ready != nullptr);
        CHECK(ready->update_ordinal > started->update_ordinal + 1U);
        CHECK(harness.trace_count(
                  goldsrc::WorldTextureImportTraceClassification::
                      wad_source_open_started) == 1U);
        CHECK(harness.trace_count(
                  goldsrc::WorldTextureImportTraceClassification::
                      wad_source_ready) == 1U);
        CHECK(harness.stage.wad_source_open_attempt_count() == 1U);
        check_terminal_ownership_and_transport(harness);
    }

    SECTION("pixel conversion emits cumulative progress within its byte budget")
    {
        write_stage_prerequisites(root, embedded_textured_bsp());
        config.texture_import.maximum_pixel_conversion_bytes_per_update = 4U;
        WorldTextureStageHarness harness{root, registries, config};
        harness.begin_protocol();
        harness.finish();

        std::size_t previous = 0U;
        std::size_t positive_progress_events = 0U;
        for (const auto& trace : harness.traces) {
            if (trace.event.classification !=
                    goldsrc::WorldTextureImportTraceClassification::
                        texture_import_progress ||
                trace.event.pixel_conversion_bytes == previous) {
                continue;
            }
            REQUIRE(trace.event.pixel_conversion_bytes > previous);
            CHECK(trace.event.pixel_conversion_bytes - previous <= 4U);
            previous = trace.event.pixel_conversion_bytes;
            ++positive_progress_events;
        }
        CHECK(positive_progress_events > 100U);
        REQUIRE(harness.stage.result());
        CHECK(previous == harness.stage.result()
                              ->textures()
                              .statistics()
                              .total_rgba_byte_count);
        check_terminal_ownership_and_transport(harness);
    }
}

TEST_CASE("World texture stage lifecycle controls are bounded and terminal",
    "[world-textures][stage][backpressure][cancel][timeout]")
{
    enum class Scenario {
        backpressure,
        cancellation,
        timeout,
    };

    Scenario scenario{Scenario::backpressure};
    fixture::ScopedLocalResourceTestRoot root;
    write_stage_prerequisites(root, embedded_textured_bsp());
    assets::AssetImporterRegistries registries;
    REQUIRE(bsp::register_builtin_asset_importers(registries));
    auto config = test_config();

    SECTION("outer event backpressure fails before publication")
    {
        scenario = Scenario::backpressure;
        config.maximum_stage_events =
            goldsrc::kMinimumWorldTextureImportStageEvents;
    }
    SECTION("cooperative cancellation is idempotent")
    {
        scenario = Scenario::cancellation;
    }
    SECTION("manual-clock texture timeout")
    {
        scenario = Scenario::timeout;
        config.texture_import.timeout = 1ms;
    }

    WorldTextureStageHarness harness{root, registries, config};
    harness.begin_protocol();
    if (scenario == Scenario::cancellation) {
        harness.reach_texture_import();
        REQUIRE_FALSE(harness.stage.terminal());
        harness.stage.cancel(harness.next_update);
        drain_events(harness.stage, harness.events);
        const auto state = harness.stage.state();
        const auto cleanup = harness.stage.cleanup_count();
        const auto releases = harness.connection_releases;
        const auto sent = harness.transport.sent.size();
        harness.stage.cancel(harness.next_update + 1ms);
        harness.stage.update(harness.next_update + 2ms);
        CHECK(harness.stage.state() == state);
        CHECK(harness.stage.cleanup_count() == cleanup);
        CHECK(harness.connection_releases == releases);
        CHECK(harness.transport.sent.size() == sent);
    } else {
        harness.finish(scenario != Scenario::backpressure);
    }

    REQUIRE(harness.stage.terminal());
    CHECK(harness.stage.state() ==
        (scenario == Scenario::backpressure
                ? goldsrc::WorldTextureImportStageState::backpressure
                : (scenario == Scenario::cancellation
                          ? goldsrc::WorldTextureImportStageState::cancelled
                          : goldsrc::WorldTextureImportStageState::timed_out)));
    CHECK_FALSE(harness.stage.result());
    CHECK(harness.stage.texture_set_publication_count() == 0U);
    if (scenario == Scenario::backpressure) {
        REQUIRE(harness.stage.error());
        CHECK(harness.stage.error()->code ==
            goldsrc::WorldTextureImportStageErrorCode::event_backpressure);
        CHECK(harness.stage.pending_event_count() ==
            goldsrc::kMinimumWorldTextureImportStageEvents);
    } else if (scenario == Scenario::timeout) {
        REQUIRE(harness.stage.error());
        REQUIRE(harness.stage.error()->texture_import_code);
        CHECK(*harness.stage.error()->texture_import_code ==
            goldsrc::WorldTextureImportErrorCode::timed_out);
    } else {
        CHECK(std::ranges::count(
                  harness.events,
                  goldsrc::WorldTextureImportStageEventType::cancelled,
                  &goldsrc::WorldTextureImportStageEvent::type) == 1);
    }
    check_terminal_ownership_and_transport(harness);
}

TEST_CASE("World texture stage is read-only and renderer-lightmap neutral",
    "[world-textures][stage][read-only][boundary]")
{
    STATIC_REQUIRE_FALSE(HasRendererHandle<goldsrc::TexturedWorldAssetState>);
    STATIC_REQUIRE_FALSE(HasDecodedLightmaps<goldsrc::TexturedWorldAssetState>);
    STATIC_REQUIRE_FALSE(HasRendererHandle<assets::WorldTextureSet>);
    STATIC_REQUIRE_FALSE(HasDecodedLightmaps<assets::WorldTextureSet>);

    fixture::ScopedLocalResourceTestRoot root;
    write_stage_prerequisites(root, mixed_textured_bsp());
    root.write("valve", "stage.wad",
        fixture::synthetic_valid_wad3("STAGE_WAD").bytes);
    const auto before = snapshot_root(root.path());

    assets::AssetImporterRegistries registries;
    REQUIRE(bsp::register_builtin_asset_importers(registries));
    WorldTextureStageHarness harness{root, registries};
    harness.begin_protocol();
    harness.finish();

    REQUIRE(harness.stage.result());
    const auto& textured = *harness.stage.result();
    CHECK(textured.textures().statistics().total_mip_level_count == 8U);
    REQUIRE(textured.world().surfaces.size() == 2U);
    CHECK(std::ranges::all_of(textured.world().surfaces,
        [](const assets::WorldSurface& surface) {
            return surface.lightmap_offset == 0U &&
                surface.light_styles ==
                    std::array<std::uint8_t, 4U>{
                        0U, 0xFFU, 0xFFU, 0xFFU};
        }));
    CHECK(snapshot_root(root.path()) == before);
    check_terminal_ownership_and_transport(harness);
}

} // namespace
