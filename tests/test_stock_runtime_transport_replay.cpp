#include <hlclient/goldsrc/netchan_payload_transform.hpp>
#include <hlclient/goldsrc/stock_runtime_transport_replay.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] std::vector<std::byte> text_bytes(const std::string_view text)
{
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

[[nodiscard]] std::vector<std::byte> connectionless(
    const std::string_view text,
    const std::span<const std::byte> suffix = {})
{
    std::vector<std::byte> result{
        std::byte{0xffU}, std::byte{0xffU},
        std::byte{0xffU}, std::byte{0xffU}};
    const auto body = text_bytes(text);
    result.insert(result.end(), body.begin(), body.end());
    result.insert(result.end(), suffix.begin(), suffix.end());
    return result;
}

[[nodiscard]] std::vector<std::byte> nul_terminated_connectionless(
    std::string text)
{
    text.push_back('\0');
    return connectionless(std::string_view{text.data(), text.size()});
}

[[nodiscard]] std::vector<std::byte> independent_connect_request(
    const std::string_view challenge = "7")
{
    const std::string protected_auth(32U, 'a');
    const std::string protocol =
        "\\prot\\3\\unique\\-1\\raw\\steam\\cdkey\\" +
        protected_auth;
    const std::string user =
        "\\bottomcolor\\6\\cl_autowepswitch\\1\\cl_dlmax\\1024"
        "\\cl_lc\\1\\cl_lw\\1\\cl_updaterate\\102"
        "\\hud_classautokill\\1\\model\\fixture_model"
        "\\name\\FixturePlayer\\topcolor\\30\\esevcmmx\\0\\_gm\\3154"
        "\\_vgui_menus\\0\\rate\\25000";
    const std::string body =
        "connect 48 " + std::string{challenge} + " \"" + protocol +
        "\" \"" + user + "\"";
    const std::vector<std::byte> opaque_suffix(213U, std::byte{0xa5U});
    return connectionless(body, opaque_suffix);
}

[[nodiscard]] goldsrc::StockRuntimeTransportReplayDatagram datagram(
    const goldsrc::StockRuntimeCaptureDirection direction,
    const std::size_t delivery_ordinal,
    std::vector<std::byte> bytes)
{
    return {
        direction, delivery_ordinal, delivery_ordinal,
        delivery_ordinal + 1U, static_cast<std::uint64_t>(delivery_ordinal * 10U),
        std::move(bytes),
    };
}

[[nodiscard]] std::vector<goldsrc::StockRuntimeTransportReplayDatagram>
handshake()
{
    return {
        datagram(goldsrc::StockRuntimeCaptureDirection::client_to_server, 0U,
                 connectionless("getchallenge steam\n")),
        datagram(goldsrc::StockRuntimeCaptureDirection::server_to_client, 1U,
                 nul_terminated_connectionless(
                     "A00000000 7 3 72057594037927936 0\n")),
        datagram(goldsrc::StockRuntimeCaptureDirection::client_to_server, 2U,
                 independent_connect_request()),
        datagram(goldsrc::StockRuntimeCaptureDirection::server_to_client, 3U,
                 nul_terminated_connectionless(
                     "B 1 \"127.0.0.1:54456\" 0 10210")),
    };
}

[[nodiscard]] std::vector<std::byte> ordinary_sequence_two_fixture()
{
    // Independent transformed fixture: sequence=2, acknowledgement=0,
    // followed by eight decoded 0x01 bytes under the stock transform.
    constexpr std::array<std::uint8_t, 16U> values{
        0x02U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U,
        0x59U, 0x19U, 0x01U, 0x03U,
        0x19U, 0x01U, 0x11U, 0x43U,
    };
    std::vector<std::byte> result;
    std::ranges::transform(values, std::back_inserter(result),
                           [](const auto value) { return std::byte{value}; });
    return result;
}

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto parsed = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(parsed);
    return *parsed;
}

[[nodiscard]] std::vector<std::byte> encoded_server_payload(
    const std::uint32_t sequence_value,
    std::vector<std::byte> payload)
{
    goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(sequence_value), {false, false}},
            goldsrc::NetchanAcknowledgementWord{sequence(0U), false}},
        {}, std::move(payload), 0U};
    const auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return *encoded.datagram;
}

[[nodiscard]] std::vector<std::byte> encoded_server_fragment(
    const std::uint32_t sequence_value,
    const std::uint16_t fragment_index,
    const std::uint16_t fragment_count,
    std::vector<std::byte> payload,
    const std::uint8_t slot = 0U)
{
    REQUIRE(payload.size() <=
            (std::numeric_limits<std::uint16_t>::max)());
    goldsrc::NetchanFragmentSlots fragments{};
    fragments[slot] = goldsrc::NetchanFragmentDescriptor{
        slot,
        (static_cast<std::uint32_t>(fragment_index) << 16U) |
            static_cast<std::uint32_t>(fragment_count),
        0U,
        static_cast<std::uint16_t>(payload.size()),
        0U,
    };
    goldsrc::ServerToClientNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(sequence_value), {true, true}},
            goldsrc::NetchanAcknowledgementWord{sequence(0U), false}},
        fragments, std::move(payload), 0U};
    packet.fragment_payload_size = packet.payload.size();
    const auto encoded = goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return *encoded.datagram;
}

[[nodiscard]] std::vector<std::byte> independent_bzip2_envelope()
{
    // Independently generated bzip2 level-9 stream for
    // "SYNTHETIC_SERVICE_PAYLOAD", prefixed by BZ2-NUL.
    constexpr std::array<std::uint8_t, 63U> values{
        0x42U, 0x5AU, 0x32U, 0x00U, 0x42U, 0x5AU, 0x68U, 0x39U,
        0x31U, 0x41U, 0x59U, 0x26U, 0x53U, 0x59U, 0x50U, 0x7AU,
        0x53U, 0x30U, 0x00U, 0x00U, 0x08U, 0x86U, 0x00U, 0x2EU,
        0x65U, 0xDDU, 0x20U, 0xA0U, 0x00U, 0x31U, 0x4CU, 0x00U,
        0x13U, 0x42U, 0x26U, 0x9EU, 0xA6U, 0x8DU, 0x1BU, 0x42U,
        0x3FU, 0x78U, 0x82U, 0x1FU, 0x66U, 0x56U, 0x90U, 0xDCU,
        0xACU, 0xD0U, 0xB2U, 0x44U, 0x7CU, 0x5DU, 0xC9U, 0x14U,
        0xE1U, 0x42U, 0x41U, 0x41U, 0xE9U, 0x4CU, 0xC0U,
    };
    std::vector<std::byte> result;
    std::ranges::transform(values, std::back_inserter(result),
                           [](const auto value) { return std::byte{value}; });
    return result;
}

TEST_CASE("Offline replay consumes only exact delivered order",
          "[goldsrc][stock-runtime][replay][transport-replay][delivery]")
{
    auto delivered = handshake();
    delivered.push_back(datagram(
        goldsrc::StockRuntimeCaptureDirection::server_to_client, 4U,
        ordinary_sequence_two_fixture()));
    delivered.push_back(datagram(
        goldsrc::StockRuntimeCaptureDirection::server_to_client, 5U,
        ordinary_sequence_two_fixture()));

    const auto replayed =
        goldsrc::StockRuntimeTransportReplay{}.replay(delivered, 3U);
    REQUIRE(replayed);
    REQUIRE(replayed.state);
    CHECK(replayed.state->connection_state() ==
          goldsrc::StockRuntimeConnectionReplayState::accepted);
    CHECK(replayed.state->connectionless_datagram_count() == 4U);
    CHECK(replayed.state->sequenced_server_to_client_count() == 1U);
    CHECK(replayed.state->duplicate_packet_count() == 1U);
    CHECK(replayed.state->dropped_observation_count() == 3U);
    REQUIRE(replayed.state->payloads().size() == 1U);
    CHECK(replayed.state->payloads()[0].delivery_ordinal() == 4U);
    CHECK(std::ranges::equal(
        replayed.state->payloads()[0].bytes(),
        std::vector<std::byte>(8U, std::byte{0x01U})));
}

TEST_CASE("Offline replay reuses existing BZip2 and fragment codecs",
          "[goldsrc][stock-runtime][replay][transport-replay][codec]")
{
    SECTION("in-memory BZip2 envelope")
    {
        auto delivered = handshake();
        delivered.push_back(datagram(
            goldsrc::StockRuntimeCaptureDirection::server_to_client, 4U,
            encoded_server_payload(1U, independent_bzip2_envelope())));
        const auto replayed =
            goldsrc::StockRuntimeTransportReplay{}.replay(delivered);
        REQUIRE(replayed);
        REQUIRE(replayed.state);
        REQUIRE(replayed.state->payloads().size() == 1U);
        CHECK(replayed.state->decompressed_payload_count() == 1U);
        CHECK(replayed.state->payloads()[0].decompressed());
        CHECK(std::ranges::equal(
            replayed.state->payloads()[0].bytes(),
            text_bytes("SYNTHETIC_SERVICE_PAYLOAD")));
    }

    SECTION("independent one-fragment normal-stream fixture")
    {
        constexpr std::array<std::uint8_t, 19U> values{
            0x01U, 0x00U, 0x00U, 0xc0U,
            0x00U, 0x00U, 0x00U, 0x00U,
            0x5aU, 0x18U, 0x01U, 0x00U,
            0x1aU, 0x00U, 0x10U, 0x41U,
            0x00U, 0x00U, 0xabU,
        };
        std::vector<std::byte> fixture;
        std::ranges::transform(values, std::back_inserter(fixture),
                               [](const auto value) { return std::byte{value}; });
        auto delivered = handshake();
        delivered.push_back(datagram(
            goldsrc::StockRuntimeCaptureDirection::server_to_client, 4U,
            std::move(fixture)));
        const auto replayed =
            goldsrc::StockRuntimeTransportReplay{}.replay(delivered);
        REQUIRE(replayed);
        REQUIRE(replayed.state);
        CHECK(replayed.state->fragment_packet_count() == 1U);
        CHECK(replayed.state->reassembled_payload_count() == 1U);
        REQUIRE(replayed.state->payloads().size() == 1U);
        CHECK(replayed.state->payloads()[0].reassembled());
        CHECK(std::ranges::equal(
            replayed.state->payloads()[0].bytes(),
            std::array{std::byte{0xabU}}));
    }
}

TEST_CASE("Offline replay enforces an aggregate retained-payload byte budget",
          "[goldsrc][stock-runtime][replay][transport-replay][bounds]")
{
    SECTION("ordinary payloads cannot accumulate past the aggregate budget")
    {
        auto delivered = handshake();
        delivered.push_back(datagram(
            goldsrc::StockRuntimeCaptureDirection::server_to_client, 4U,
            encoded_server_payload(
                1U, {std::byte{0x11U}, std::byte{0x12U}})));
        delivered.push_back(datagram(
            goldsrc::StockRuntimeCaptureDirection::server_to_client, 5U,
            encoded_server_payload(
                2U, {std::byte{0x21U}, std::byte{0x22U}})));

        goldsrc::StockRuntimeTransportReplayLimits limits;
        limits.maximum_total_replayed_payload_bytes = 3U;
        const auto rejected =
            goldsrc::StockRuntimeTransportReplay{limits}.replay(delivered);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeTransportReplayErrorCode::
                  replay_payload_limit_exceeded);
        CHECK(rejected.error->delivery_ordinal == 5U);
    }

    SECTION("decompression expansion is charged before publication")
    {
        auto delivered = handshake();
        delivered.push_back(datagram(
            goldsrc::StockRuntimeCaptureDirection::server_to_client, 4U,
            encoded_server_payload(1U, independent_bzip2_envelope())));

        goldsrc::StockRuntimeTransportReplayLimits limits;
        limits.maximum_total_replayed_payload_bytes = 8U;
        const auto rejected =
            goldsrc::StockRuntimeTransportReplay{limits}.replay(delivered);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeTransportReplayErrorCode::
                  replay_payload_limit_exceeded);
        CHECK(rejected.error->delivery_ordinal == 4U);
    }

    SECTION("a duplicate emission cannot exceed the delivered raw-byte budget")
    {
        auto delivered = handshake();
        delivered.push_back(datagram(
            goldsrc::StockRuntimeCaptureDirection::server_to_client, 4U,
            ordinary_sequence_two_fixture()));
        delivered.push_back(datagram(
            goldsrc::StockRuntimeCaptureDirection::server_to_client, 5U,
            ordinary_sequence_two_fixture()));
        std::size_t total_bytes = 0U;
        for (const auto& item : delivered) total_bytes += item.bytes.size();
        REQUIRE(total_bytes > delivered.back().bytes.size());

        goldsrc::StockRuntimeTransportReplayLimits limits;
        limits.maximum_total_delivered_datagram_bytes = total_bytes - 1U;
        const auto rejected =
            goldsrc::StockRuntimeTransportReplay{limits}.replay(delivered);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeTransportReplayErrorCode::
                  delivered_datagram_budget_exceeded);
        CHECK(rejected.error->delivery_ordinal == 5U);
    }
}

TEST_CASE("Transport mutations return typed failures without partial state",
          "[goldsrc][stock-runtime][replay][transport-replay][mutation]")
{
    SECTION("challenge mismatch keeps authentication opaque")
    {
        auto delivered = handshake();
        delivered[2U].bytes = independent_connect_request("8");
        const auto rejected =
            goldsrc::StockRuntimeTransportReplay{}.replay(delivered);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeTransportReplayErrorCode::challenge_mismatch);
        CHECK(rejected.error->context.find("a5") == std::string::npos);
    }

    SECTION("half-range sequence is neither guessed old nor new")
    {
        auto delivered = handshake();
        delivered.push_back(datagram(
            goldsrc::StockRuntimeCaptureDirection::server_to_client, 4U,
            ordinary_sequence_two_fixture()));
        auto ambiguous = ordinary_sequence_two_fixture();
        ambiguous[0U] = std::byte{0x02U};
        ambiguous[1U] = std::byte{0x00U};
        ambiguous[2U] = std::byte{0x00U};
        ambiguous[3U] = std::byte{0x20U};
        delivered.push_back(datagram(
            goldsrc::StockRuntimeCaptureDirection::server_to_client, 5U,
            std::move(ambiguous)));
        const auto rejected =
            goldsrc::StockRuntimeTransportReplay{}.replay(delivered);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeTransportReplayErrorCode::
                  sequence_half_range_ambiguous);
    }

    SECTION("malformed BZip2 is typed")
    {
        auto delivered = handshake();
        auto envelope = independent_bzip2_envelope();
        envelope[4U] = std::byte{'X'};
        delivered.push_back(datagram(
            goldsrc::StockRuntimeCaptureDirection::server_to_client, 4U,
            encoded_server_payload(1U, std::move(envelope))));
        const auto rejected =
            goldsrc::StockRuntimeTransportReplay{}.replay(delivered);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeTransportReplayErrorCode::
                  decompression_failed);
        REQUIRE(rejected.error->envelope_code);
    }
}

TEST_CASE("Offline replay completes a two-fragment transfer with exact provenance",
          "[goldsrc][stock-runtime][replay][transport-replay][fragment][provenance]")
{
    auto delivered = handshake();
    auto first = datagram(
        goldsrc::StockRuntimeCaptureDirection::server_to_client, 4U,
        encoded_server_fragment(
            1U, 1U, 2U,
            std::vector<std::byte>(
                goldsrc::kStockProtocol48NormalFragmentChunkSize,
                std::byte{0x6aU})));
    first.observed_ordinal = 91U;
    first.direction_ordinal = 17U;
    first.observed_relative_timestamp_us = 7'000U;
    auto second = datagram(
        goldsrc::StockRuntimeCaptureDirection::server_to_client, 5U,
        encoded_server_fragment(
            2U, 2U, 2U,
            {std::byte{0x7bU}, std::byte{0x7cU}, std::byte{0x7dU}}));
    second.observed_ordinal = 44U;
    second.direction_ordinal = 18U;
    second.observed_relative_timestamp_us = 7'100U;
    delivered.push_back(std::move(first));
    delivered.push_back(std::move(second));

    const auto replayed =
        goldsrc::StockRuntimeTransportReplay{}.replay(delivered);
    REQUIRE(replayed);
    REQUIRE(replayed.state);
    CHECK(replayed.state->fragment_packet_count() == 2U);
    CHECK(replayed.state->reassembled_payload_count() == 1U);
    CHECK(replayed.state->sequenced_server_to_client_count() == 2U);
    REQUIRE(replayed.state->payloads().size() == 1U);
    const auto& payload = replayed.state->payloads().front();
    CHECK(payload.kind() ==
          goldsrc::StockRuntimeReplayedPayloadKind::
              completed_normal_fragment_transfer);
    CHECK(payload.fragmented());
    CHECK(payload.reassembled());
    CHECK_FALSE(payload.decompressed());
    CHECK(payload.source_fragment_count() == 2U);
    CHECK(payload.source_sequence() == 2U);
    CHECK(payload.corpus_observed_ordinal() == 44U);
    CHECK(payload.delivery_ordinal() == 5U);
    REQUIRE(payload.bytes().size() ==
            goldsrc::kStockProtocol48NormalFragmentChunkSize + 3U);
    CHECK(std::ranges::all_of(
        payload.bytes().first(
            goldsrc::kStockProtocol48NormalFragmentChunkSize),
        [](const std::byte value) { return value == std::byte{0x6aU}; }));
    CHECK(std::ranges::equal(
        payload.bytes().last(3U),
        std::array{
            std::byte{0x7bU}, std::byte{0x7cU}, std::byte{0x7dU}}));
}

TEST_CASE("Offline replay drops old sequence bodies but preserves reordered provenance",
          "[goldsrc][stock-runtime][replay][transport-replay][old][reorder][provenance]")
{
    auto delivered = handshake();
    auto first = datagram(
        goldsrc::StockRuntimeCaptureDirection::server_to_client, 4U,
        encoded_server_payload(5U, {std::byte{0x51U}}));
    first.observed_ordinal = 30U;
    first.direction_ordinal = 9U;
    auto old = datagram(
        goldsrc::StockRuntimeCaptureDirection::server_to_client, 5U,
        encoded_server_payload(3U, {std::byte{0x31U}}));
    old.observed_ordinal = 31U;
    old.direction_ordinal = 10U;
    auto reordered_new = datagram(
        goldsrc::StockRuntimeCaptureDirection::server_to_client, 6U,
        encoded_server_payload(6U, {std::byte{0x61U}}));
    reordered_new.observed_ordinal = 12U;
    reordered_new.direction_ordinal = 7U;
    delivered.push_back(std::move(first));
    delivered.push_back(std::move(old));
    delivered.push_back(std::move(reordered_new));

    const auto replayed =
        goldsrc::StockRuntimeTransportReplay{}.replay(delivered);
    REQUIRE(replayed);
    REQUIRE(replayed.state);
    CHECK(replayed.state->old_packet_count() == 1U);
    CHECK(replayed.state->duplicate_packet_count() == 0U);
    CHECK(replayed.state->sequenced_server_to_client_count() == 2U);
    REQUIRE(replayed.state->payloads().size() == 2U);
    CHECK(replayed.state->payloads()[0U].source_sequence() == 5U);
    CHECK(replayed.state->payloads()[0U].corpus_observed_ordinal() == 30U);
    CHECK(replayed.state->payloads()[0U].delivery_ordinal() == 4U);
    CHECK(replayed.state->payloads()[1U].source_sequence() == 6U);
    CHECK(replayed.state->payloads()[1U].corpus_observed_ordinal() == 12U);
    CHECK(replayed.state->payloads()[1U].delivery_ordinal() == 6U);
    CHECK(std::ranges::equal(
        replayed.state->payloads()[0U].bytes(),
        std::array{std::byte{0x51U}}));
    CHECK(std::ranges::equal(
        replayed.state->payloads()[1U].bytes(),
        std::array{std::byte{0x61U}}));
}

TEST_CASE("Incomplete normal and pending secondary fragment streams fail closed",
          "[goldsrc][stock-runtime][replay][transport-replay][fragment][fail-closed]")
{
    SECTION("missing final normal fragment")
    {
        auto delivered = handshake();
        delivered.push_back(datagram(
            goldsrc::StockRuntimeCaptureDirection::server_to_client, 4U,
            encoded_server_fragment(
                1U, 1U, 2U,
                std::vector<std::byte>(
                    goldsrc::kStockProtocol48NormalFragmentChunkSize,
                    std::byte{0x41U}))));

        const auto rejected =
            goldsrc::StockRuntimeTransportReplay{}.replay(delivered);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeTransportReplayErrorCode::
                  fragment_reassembly_failed);
        CHECK(rejected.error->delivery_ordinal == delivered.size());
    }

    SECTION("slot one remains explicitly pending")
    {
        auto delivered = handshake();
        delivered.push_back(datagram(
            goldsrc::StockRuntimeCaptureDirection::server_to_client, 4U,
            encoded_server_fragment(
                1U, 1U, 1U, {std::byte{0x55U}}, 1U)));

        const auto rejected =
            goldsrc::StockRuntimeTransportReplay{}.replay(delivered);
        REQUIRE_FALSE(rejected);
        CHECK_FALSE(rejected.state);
        REQUIRE(rejected.error);
        CHECK(rejected.error->code ==
              goldsrc::StockRuntimeTransportReplayErrorCode::
                  unsupported_secondary_stream);
        CHECK(rejected.error->delivery_ordinal == 4U);
    }
}

} // namespace
