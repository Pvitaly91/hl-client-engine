#include "delta_test_fixture.hpp"
#include "move_vars_test_fixture.hpp"
#include "resource_client_response_test_fixture.hpp"
#include "resource_list_test_fixture.hpp"
#include "user_info_test_fixture.hpp"

#include <hlclient/goldsrc/move_vars.hpp>
#include <hlclient/goldsrc/netchan_packet.hpp>
#include <hlclient/goldsrc/stock_captured_signon_replay.hpp>

#include <catch2/catch_test_macros.hpp>

#include <bzlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace delta_fixture = hlclient::test::delta_fixture;
namespace move_fixture = hlclient::test::move_vars_fixture;
namespace response_fixture =
    hlclient::test::resource_client_response_fixture;
namespace resource_fixture = resource_list_test_fixture;
namespace user_fixture = hlclient::test::user_info_fixture;
namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] std::vector<std::byte> connectionless(
    const std::string_view text,
    const std::span<const std::byte> suffix = {})
{
    std::vector<std::byte> result{
        std::byte{0xffU}, std::byte{0xffU},
        std::byte{0xffU}, std::byte{0xffU}};
    const auto body = std::as_bytes(std::span{text.data(), text.size()});
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

[[nodiscard]] std::vector<std::byte> connect_request()
{
    const std::string protocol =
        "\\prot\\3\\unique\\-1\\raw\\steam\\cdkey\\" +
        std::string(32U, 'c');
    const std::string user =
        "\\bottomcolor\\6\\cl_autowepswitch\\1\\cl_dlmax\\1024"
        "\\cl_lc\\1\\cl_lw\\1\\cl_updaterate\\102"
        "\\hud_classautokill\\1\\model\\fixture_model"
        "\\name\\FixturePlayer\\topcolor\\30\\esevcmmx\\0\\_gm\\3154"
        "\\_vgui_menus\\0\\rate\\25000";
    const std::string body =
        "connect 48 9 \"" + protocol + "\" \"" + user + "\"";
    return connectionless(body, std::vector<std::byte>(213U, std::byte{0x6dU}));
}

[[nodiscard]] goldsrc::StockRuntimeTransportReplayDatagram datagram(
    const goldsrc::StockRuntimeCaptureDirection direction,
    const std::size_t ordinal,
    std::vector<std::byte> bytes)
{
    return {direction, ordinal, ordinal, ordinal + 1U,
            static_cast<std::uint64_t>(ordinal), std::move(bytes)};
}

[[nodiscard]] goldsrc::StockRuntimeTransportReplayResult accepted_transport()
{
    // Independent sequence-2 fixture decodes to eight 0x01 bytes.
    constexpr std::array<std::uint8_t, 16U> sequence_two{
        0x02U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U,
        0x59U, 0x19U, 0x01U, 0x03U,
        0x19U, 0x01U, 0x11U, 0x43U,
    };
    std::vector<std::byte> sequenced;
    std::ranges::transform(sequence_two, std::back_inserter(sequenced),
                           [](const auto value) { return std::byte{value}; });
    const std::vector delivered{
        datagram(goldsrc::StockRuntimeCaptureDirection::client_to_server, 0U,
                 connectionless("getchallenge steam\n")),
        datagram(goldsrc::StockRuntimeCaptureDirection::server_to_client, 1U,
                 nul_terminated_connectionless(
                     "A00000000 9 3 72057594037927936 0\n")),
        datagram(goldsrc::StockRuntimeCaptureDirection::client_to_server, 2U,
                 connect_request()),
        datagram(goldsrc::StockRuntimeCaptureDirection::server_to_client, 3U,
                 nul_terminated_connectionless(
                     "B 1 \"127.0.0.1:54456\" 0 10210")),
        datagram(goldsrc::StockRuntimeCaptureDirection::server_to_client, 4U,
                 std::move(sequenced)),
    };
    return goldsrc::StockRuntimeTransportReplay{}.replay(delivered);
}

[[nodiscard]] goldsrc::NetchanSequence sequence(const std::uint32_t value)
{
    const auto parsed = goldsrc::NetchanSequence::from_numeric(value);
    REQUIRE(parsed);
    return *parsed;
}

[[nodiscard]] std::vector<std::byte> encoded_payload(
    const goldsrc::StockRuntimeCaptureDirection direction,
    const std::uint32_t sequence_value,
    std::vector<std::byte> payload,
    const bool reliable = true)
{
    const goldsrc::NetchanHeader header{
        goldsrc::NetchanSequenceWord{
            sequence(sequence_value), {reliable, false}},
        goldsrc::NetchanAcknowledgementWord{sequence(0U), false}};
    if (direction ==
        goldsrc::StockRuntimeCaptureDirection::client_to_server) {
        const goldsrc::ClientToServerNetchanPacket packet{
            header, {}, std::move(payload), 0U};
        const auto encoded =
            goldsrc::encode_client_to_server_netchan_packet(packet);
        REQUIRE(encoded);
        REQUIRE(encoded.datagram);
        return *encoded.datagram;
    }
    const goldsrc::ServerToClientNetchanPacket packet{
        header, {}, std::move(payload), 0U};
    const auto encoded =
        goldsrc::encode_server_to_client_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return *encoded.datagram;
}

[[nodiscard]] std::vector<std::byte> encoded_client_fragment_carrier(
    const std::uint32_t sequence_value,
    std::vector<std::byte> fragment,
    const std::span<const std::byte> suffix)
{
    REQUIRE(fragment.size() <=
            (std::numeric_limits<std::uint16_t>::max)());
    goldsrc::NetchanFragmentSlots fragments{};
    fragments[0U] = goldsrc::NetchanFragmentDescriptor{
        0U, 0x0001'0001U, 0U,
        static_cast<std::uint16_t>(fragment.size()), 0U};
    const auto fragment_size = fragment.size();
    fragment.insert(fragment.end(), suffix.begin(), suffix.end());
    goldsrc::ClientToServerNetchanPacket packet{
        goldsrc::NetchanHeader{
            goldsrc::NetchanSequenceWord{
                sequence(sequence_value), {true, true}},
            goldsrc::NetchanAcknowledgementWord{sequence(0U), false}},
        fragments, std::move(fragment), fragment_size};
    const auto encoded = goldsrc::encode_client_to_server_netchan_packet(packet);
    REQUIRE(encoded);
    REQUIRE(encoded.datagram);
    return *encoded.datagram;
}

[[nodiscard]] std::vector<std::byte> compressed_service_envelope(
    const std::span<const std::byte> semantic)
{
    REQUIRE_FALSE(semantic.empty());
    REQUIRE(semantic.size() <=
            (std::numeric_limits<unsigned int>::max)());
    std::vector<char> source;
    source.reserve(semantic.size());
    std::ranges::transform(
        semantic, std::back_inserter(source), [](const std::byte value) {
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
        std::byte{0x42U}, std::byte{0x5aU},
        std::byte{0x32U}, std::byte{0x00U}};
    std::ranges::transform(
        compressed, std::back_inserter(envelope), [](const char value) {
            return static_cast<std::byte>(static_cast<unsigned char>(value));
        });
    return envelope;
}

[[nodiscard]] std::vector<std::byte> first_server_signon_payload()
{
    std::vector<std::byte> post_delta_body;
    post_delta_body.insert(
        post_delta_body.end(),
        move_fixture::kExactMoveVarsMessage.begin() + 1,
        move_fixture::kExactMoveVarsMessage.end());
    post_delta_body.insert(
        post_delta_body.end(),
        move_fixture::kExactPostMoveVarsStream.begin(),
        move_fixture::kExactPostMoveVarsStream.end() - 2);
    post_delta_body.insert(
        post_delta_body.end(), user_fixture::kExactUserInfoMessage.begin(),
        user_fixture::kExactUserInfoMessage.end());
    const std::vector<std::vector<std::byte>> schemas{
        delta_fixture::schema(
            "alpha_t", delta_fixture::kSchemaAlphaFields),
    };
    return delta_fixture::service_payload(
        schemas, goldsrc::kMoveVarsOpcode, post_delta_body);
}

[[nodiscard]] std::vector<std::byte> resource_list_server_payload()
{
    std::vector<std::byte> payload{
        std::byte{45U}, std::byte{1U}, std::byte{0U},
        std::byte{0U}, std::byte{0U}, std::byte{0U},
        std::byte{0U}, std::byte{0U}, std::byte{0U}};
    payload.insert(
        payload.end(), resource_fixture::kExactResourceListMessage.begin(),
        resource_fixture::kExactResourceListMessage.end());
    return payload;
}

enum class CapturedChainVariant {
    complete,
    missing_initial_request,
    missing_sendres,
    missing_resource_response,
    wrong_new,
    wrong_new_padding,
    wrong_sendres,
    sendres_with_suffix,
    wrong_resource_response,
    resource_response_with_suffix,
    client_fragment_before_response,
    zero_entry_resource_response,
};

[[nodiscard]] goldsrc::StockRuntimeTransportReplayResult captured_style_transport(
    const CapturedChainVariant variant = CapturedChainVariant::complete,
    const bool with_transport_padding_payload = false)
{
    std::vector<std::byte> initial_request{
        std::byte{3U}, std::byte{'n'}, std::byte{'e'},
        std::byte{'w'}, std::byte{0U},
        goldsrc::kStockProtocol48NetchanPaddingByte,
        goldsrc::kStockProtocol48NetchanPaddingByte,
        goldsrc::kStockProtocol48NetchanPaddingByte};
    std::vector<std::byte> sendres{
        std::byte{3U}, std::byte{'s'}, std::byte{'e'},
        std::byte{'n'}, std::byte{'d'}, std::byte{'r'},
        std::byte{'e'}, std::byte{'s'}, std::byte{0U}};
    std::vector<std::byte> resource_response{
        response_fixture::kExactSyntheticResponse.begin(),
        response_fixture::kExactSyntheticResponse.end()};
    if (variant == CapturedChainVariant::wrong_new) {
        initial_request[1U] = std::byte{'N'};
    }
    if (variant == CapturedChainVariant::wrong_new_padding) {
        initial_request.back() = std::byte{0U};
    }
    if (variant == CapturedChainVariant::wrong_sendres) {
        sendres[4U] = std::byte{'D'};
    }
    if (variant == CapturedChainVariant::sendres_with_suffix) {
        sendres.push_back(std::byte{0xa1U});
        sendres.push_back(std::byte{0xa2U});
    }
    if (variant == CapturedChainVariant::wrong_resource_response) {
        resource_response[0U] = std::byte{6U};
    }
    if (variant == CapturedChainVariant::zero_entry_resource_response) {
        resource_response = {
            std::byte{goldsrc::kOpcode5ResourceResponseOpcode},
            std::byte{0U}, std::byte{0U}};
    }

    auto delivered = std::vector{
        datagram(goldsrc::StockRuntimeCaptureDirection::client_to_server, 0U,
                 connectionless("getchallenge steam\n")),
        datagram(goldsrc::StockRuntimeCaptureDirection::server_to_client, 1U,
                 nul_terminated_connectionless(
                     "A00000000 9 3 72057594037927936 0\n")),
        datagram(goldsrc::StockRuntimeCaptureDirection::client_to_server, 2U,
                 connect_request()),
        datagram(goldsrc::StockRuntimeCaptureDirection::server_to_client, 3U,
                 nul_terminated_connectionless(
                     "B 1 \"127.0.0.1:54456\" 0 10210")),
    };
    const auto append = [&delivered](
                            const goldsrc::StockRuntimeCaptureDirection direction,
                            const std::uint32_t netchan_sequence,
                            std::vector<std::byte> semantic) {
        const auto delivery_ordinal = delivered.size();
        delivered.push_back(datagram(
            direction, delivery_ordinal,
            encoded_payload(
                direction, netchan_sequence, std::move(semantic))));
    };

    if (variant != CapturedChainVariant::missing_initial_request) {
        append(
            goldsrc::StockRuntimeCaptureDirection::client_to_server, 1U,
            std::move(initial_request));
    }
    const std::uint32_t server_sequence_offset =
        with_transport_padding_payload ? 1U : 0U;
    if (with_transport_padding_payload) {
        append(
            goldsrc::StockRuntimeCaptureDirection::server_to_client, 1U,
            std::vector<std::byte>(
                goldsrc::kStockProtocol48MinimumDecodedPayloadSize,
                goldsrc::kStockProtocol48NetchanPaddingByte));
    }
    append(
        goldsrc::StockRuntimeCaptureDirection::server_to_client,
        1U + server_sequence_offset,
        compressed_service_envelope(first_server_signon_payload()));
    if (variant != CapturedChainVariant::missing_sendres) {
        append(
            goldsrc::StockRuntimeCaptureDirection::client_to_server, 2U,
            std::move(sendres));
    }
    append(
        goldsrc::StockRuntimeCaptureDirection::server_to_client,
        2U + server_sequence_offset,
        compressed_service_envelope(resource_list_server_payload()));
    const std::array concurrent_suffix{
        std::byte{2U}, std::byte{0xb1U}, std::byte{0xb2U},
        std::byte{0xb3U}, std::byte{0xb4U}, std::byte{0xb5U},
        std::byte{0xb6U}, std::byte{0xb7U}, std::byte{0xb8U},
        std::byte{0xb9U}, std::byte{0xbaU}};
    if (variant == CapturedChainVariant::client_fragment_before_response) {
        const auto delivery_ordinal = delivered.size();
        delivered.push_back(datagram(
            goldsrc::StockRuntimeCaptureDirection::client_to_server,
            delivery_ordinal,
            encoded_client_fragment_carrier(
                3U,
                {std::byte{4U}, std::byte{0U}, std::byte{0U}},
                concurrent_suffix)));
    }
    if (variant != CapturedChainVariant::missing_resource_response) {
        if (variant == CapturedChainVariant::resource_response_with_suffix) {
            const auto delivery_ordinal = delivered.size();
            delivered.push_back(datagram(
                goldsrc::StockRuntimeCaptureDirection::client_to_server,
                delivery_ordinal,
                encoded_client_fragment_carrier(
                    3U, std::move(resource_response), concurrent_suffix)));
        } else {
            append(
                goldsrc::StockRuntimeCaptureDirection::client_to_server,
                variant == CapturedChainVariant::client_fragment_before_response
                    ? 4U
                    : 3U,
                std::move(resource_response));
        }
    }
    append(
        goldsrc::StockRuntimeCaptureDirection::server_to_client,
        3U + server_sequence_offset,
        {std::byte{0x21U}, std::byte{0xa1U}, std::byte{0xa2U}});
    return goldsrc::StockRuntimeTransportReplay{}.replay(delivered);
}

TEST_CASE("Captured signon adapter reconstructs an exact neutral boundary",
          "[goldsrc][stock-runtime][replay][signon-replay][boundary]")
{
    const auto transport = accepted_transport();
    REQUIRE(transport);
    REQUIRE(transport.state);

    const auto reconstructed =
        goldsrc::StockCapturedSignonReplay{}.
            reconstruct_post_resource_boundary(*transport.state, 0U, 3U, 3U);
    REQUIRE(reconstructed);
    REQUIRE(reconstructed.state);
    CHECK_FALSE(reconstructed.state->known_signon_validated());
    CHECK_FALSE(reconstructed.state->generated_ack());
    CHECK_FALSE(reconstructed.state->generated_client_request());
    CHECK_FALSE(reconstructed.state->observed_initial_new());
    CHECK_FALSE(reconstructed.state->observed_sendres());
    CHECK_FALSE(reconstructed.state->observed_opcode5_resource_response());
    CHECK_FALSE(reconstructed.state->server_info());
    CHECK_FALSE(reconstructed.state->delta_registry());
    CHECK(reconstructed.state->user_message_definitions().empty());
    CHECK(reconstructed.state->boundary().kind() ==
          goldsrc::PostResourceResponseBoundaryKind::opcode_at_payload_start);
    REQUIRE(reconstructed.state->boundary().opcode());
    CHECK(*reconstructed.state->boundary().opcode() == 1U);

    const auto& cursor = reconstructed.state->cursor();
    CHECK(cursor.replay_payload_ordinal == 0U);
    CHECK(cursor.corpus_observed_ordinal == 4U);
    CHECK(cursor.delivery_ordinal == 4U);
    CHECK(cursor.byte_offset == 0U);
    CHECK(cursor.bit_offset == 0U);
    CHECK(cursor.source_netchan_sequence == 2U);
    CHECK(cursor.source_payload_byte_count == 8U);
    CHECK(cursor.source_payload_bit_count == 64U);
    CHECK(cursor.next_unconsumed_bit_count == 64U);
    CHECK_FALSE(cursor.reassembled);
    CHECK_FALSE(cursor.decompressed);
}

TEST_CASE("Boundary adapter cannot promote a synthetic shortcut to known signon",
          "[goldsrc][stock-runtime][replay][signon-replay][fail-closed]")
{
    const auto transport = accepted_transport();
    REQUIRE(transport);
    REQUIRE(transport.state);

    const auto full =
        goldsrc::StockCapturedSignonReplay{}.replay(*transport.state);
    REQUIRE_FALSE(full);
    REQUIRE(full.error);
    CHECK(full.error->code ==
          goldsrc::StockCapturedSignonReplayErrorCode::
              initial_request_not_observed);

    const auto missing =
        goldsrc::StockCapturedSignonReplay{}.
            reconstruct_post_resource_boundary(*transport.state, 1U, 3U, 3U);
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error);
    CHECK(missing.error->code ==
          goldsrc::StockCapturedSignonReplayErrorCode::
              post_resource_cursor_unavailable);
}

TEST_CASE("Captured signon replay maps transport failures without raw diagnostics",
          "[goldsrc][stock-runtime][replay][signon-replay][transport-error]")
{
    const goldsrc::StockRuntimeTransportReplayResult transport{
        std::nullopt,
        goldsrc::StockRuntimeTransportReplayError{
            goldsrc::StockRuntimeTransportReplayErrorCode::decompression_failed,
            7U, "bounded BZip2 failure", std::nullopt, std::nullopt,
            goldsrc::ServicePayloadEnvelopeErrorCode::corrupt_compressed_stream}};
    const auto replayed = goldsrc::StockCapturedSignonReplay{}.replay(transport);
    REQUIRE_FALSE(replayed);
    REQUIRE(replayed.error);
    CHECK(replayed.error->code ==
          goldsrc::StockCapturedSignonReplayErrorCode::decompression_failed);
    REQUIRE(replayed.error->transport_code);
    CHECK(*replayed.error->transport_code ==
          goldsrc::StockRuntimeTransportReplayErrorCode::decompression_failed);
    CHECK(replayed.error->replay_payload_ordinal == 7U);
}

TEST_CASE("Captured-style new sendres and opcode-five chain reaches exact cursor",
          "[goldsrc][stock-runtime][replay][signon-replay][captured-chain]")
{
    const auto transport = captured_style_transport();
    REQUIRE(transport);
    REQUIRE(transport.state);
    REQUIRE(transport.state->payloads().size() == 6U);
    CHECK(transport.state->decompressed_payload_count() == 2U);

    const auto replayed =
        goldsrc::StockCapturedSignonReplay{}.replay(*transport.state);
    REQUIRE(replayed);
    REQUIRE(replayed.state);
    CHECK(replayed.state->known_signon_validated());
    REQUIRE(replayed.state->resources());
    CHECK_FALSE(replayed.state->resources()->entries().empty());
    CHECK(replayed.state->observed_initial_new());
    CHECK(replayed.state->observed_sendres());
    CHECK(replayed.state->observed_opcode5_resource_response());
    CHECK(replayed.state->observed_client_request_count() == 3U);
    CHECK(replayed.state->decoded_server_signon_payload_count() == 3U);
    CHECK_FALSE(replayed.state->generated_ack());
    CHECK_FALSE(replayed.state->generated_client_request());
    REQUIRE(replayed.state->server_info());
    CHECK(replayed.state->server_info()->maximum_clients().value() == 8U);
    REQUIRE(replayed.state->delta_registry());
    CHECK(replayed.state->delta_registry()->schema_count() == 1U);
    CHECK(replayed.state->delta_registry()->find_exact("alpha_t") != nullptr);
    REQUIRE(replayed.state->user_message_definitions().size() == 2U);
    CHECK(replayed.state->user_message_definitions()[0U].identifier == 64U);
    CHECK(replayed.state->user_message_definitions()[0U].declared_size == -1);
    CHECK(replayed.state->user_message_definitions()[0U].name == "HudText");
    CHECK(replayed.state->user_message_definitions()[1U].identifier == 65U);
    CHECK(replayed.state->user_message_definitions()[1U].declared_size == 9);
    CHECK(replayed.state->user_message_definitions()[1U].name == "ScoreInfo");

    const auto& boundary = replayed.state->boundary();
    CHECK(boundary.kind() ==
          goldsrc::PostResourceResponseBoundaryKind::opcode_at_payload_start);
    REQUIRE(boundary.opcode());
    CHECK(*boundary.opcode() == 0x21U);
    CHECK(boundary.byte_offset() == 0U);
    CHECK(boundary.bit_offset() == 0U);
    CHECK(boundary.remaining_byte_count() == 2U);

    const auto& cursor = replayed.state->cursor();
    CHECK(cursor.replay_payload_ordinal == 5U);
    CHECK(cursor.corpus_observed_ordinal == 9U);
    CHECK(cursor.delivery_ordinal == 9U);
    CHECK(cursor.byte_offset == 0U);
    CHECK(cursor.bit_offset == 0U);
    CHECK(cursor.source_netchan_sequence == 3U);
    CHECK(cursor.source_payload_byte_count == 3U);
    CHECK(cursor.source_payload_bit_count == 24U);
    CHECK(cursor.next_unconsumed_bit_count == 24U);
    CHECK_FALSE(cursor.reassembled);
    CHECK_FALSE(cursor.decompressed);
}

TEST_CASE("Captured signon skips only exact Protocol 48 padding payloads",
          "[goldsrc][stock-runtime][replay][signon-replay][captured-chain]"
          "[padding]")
{
    const auto transport = captured_style_transport(
        CapturedChainVariant::complete, true);
    REQUIRE(transport);
    REQUIRE(transport.state);
    REQUIRE(transport.state->payloads().size() == 7U);

    const auto replayed =
        goldsrc::StockCapturedSignonReplay{}.replay(*transport.state);
    REQUIRE(replayed);
    REQUIRE(replayed.state);
    CHECK(replayed.state->known_signon_validated());
    CHECK(replayed.state->cursor().replay_payload_ordinal == 6U);
    CHECK(replayed.state->cursor().source_netchan_sequence == 4U);
}

TEST_CASE("Captured signon consumes sendres only at its exact leading cursor",
          "[goldsrc][stock-runtime][replay][signon-replay][captured-chain]"
          "[cursor]")
{
    const auto transport = captured_style_transport(
        CapturedChainVariant::sendres_with_suffix);
    REQUIRE(transport);
    REQUIRE(transport.state);

    const auto replayed =
        goldsrc::StockCapturedSignonReplay{}.replay(*transport.state);
    REQUIRE(replayed);
    REQUIRE(replayed.state);
    CHECK(replayed.state->observed_sendres());
}

TEST_CASE("Captured signon selects a resource fragment before its opaque carrier suffix",
          "[goldsrc][stock-runtime][replay][signon-replay][captured-chain]"
          "[cursor]")
{
    const auto transport = captured_style_transport(
        CapturedChainVariant::resource_response_with_suffix);
    REQUIRE(transport);
    REQUIRE(transport.state);

    const auto replayed =
        goldsrc::StockCapturedSignonReplay{}.replay(*transport.state);
    REQUIRE(replayed);
    REQUIRE(replayed.state);
    CHECK(replayed.state->observed_opcode5_resource_response());
}

TEST_CASE("Captured signon advances only across typed payload boundaries before response",
          "[goldsrc][stock-runtime][replay][signon-replay][captured-chain]"
          "[cursor]")
{
    const auto transport = captured_style_transport(
        CapturedChainVariant::client_fragment_before_response);
    REQUIRE(transport);
    REQUIRE(transport.state);

    const auto replayed =
        goldsrc::StockCapturedSignonReplay{}.replay(*transport.state);
    REQUIRE(replayed);
    REQUIRE(replayed.state);
    CHECK(replayed.state->observed_opcode5_resource_response());
}

TEST_CASE("Captured signon accepts the exact zero-entry resource response",
          "[goldsrc][stock-runtime][replay][signon-replay][captured-chain]"
          "[zero-entry]")
{
    const auto transport = captured_style_transport(
        CapturedChainVariant::zero_entry_resource_response);
    REQUIRE(transport);
    REQUIRE(transport.state);

    const auto replayed =
        goldsrc::StockCapturedSignonReplay{}.replay(*transport.state);
    REQUIRE(replayed);
    REQUIRE(replayed.state);
    CHECK(replayed.state->observed_opcode5_resource_response());
}

TEST_CASE("Captured signon request identities fail closed when absent or changed",
          "[goldsrc][stock-runtime][replay][signon-replay][captured-chain][mutation]")
{
    const auto rejects = [](const CapturedChainVariant variant) {
        const auto transport = captured_style_transport(variant);
        REQUIRE(transport);
        REQUIRE(transport.state);
        const auto replayed =
            goldsrc::StockCapturedSignonReplay{}.replay(*transport.state);
        REQUIRE_FALSE(replayed);
        CHECK_FALSE(replayed.state);
        REQUIRE(replayed.error);
        return replayed.error->code;
    };

    SECTION("initial new is exact")
    {
        CHECK(rejects(CapturedChainVariant::wrong_new) ==
              goldsrc::StockCapturedSignonReplayErrorCode::
                  initial_request_not_observed);
    }
    SECTION("initial new is required")
    {
        CHECK(rejects(CapturedChainVariant::missing_initial_request) ==
              goldsrc::StockCapturedSignonReplayErrorCode::
                  initial_request_not_observed);
    }
    SECTION("initial new accepts only the exact Protocol 48 transport padding")
    {
        CHECK(rejects(CapturedChainVariant::wrong_new_padding) ==
              goldsrc::StockCapturedSignonReplayErrorCode::
                  initial_request_not_observed);
    }
    SECTION("sendres is required")
    {
        CHECK(rejects(CapturedChainVariant::missing_sendres) ==
              goldsrc::StockCapturedSignonReplayErrorCode::
                  resource_transition_request_not_observed);
    }
    SECTION("sendres spelling is exact")
    {
        CHECK(rejects(CapturedChainVariant::wrong_sendres) ==
              goldsrc::StockCapturedSignonReplayErrorCode::
                  resource_transition_request_not_observed);
    }
    SECTION("opcode-five response is required")
    {
        CHECK(rejects(CapturedChainVariant::missing_resource_response) ==
              goldsrc::StockCapturedSignonReplayErrorCode::
                  resource_response_not_observed);
    }
    SECTION("changed opcode-five response is not accepted")
    {
        CHECK(rejects(CapturedChainVariant::wrong_resource_response) ==
              goldsrc::StockCapturedSignonReplayErrorCode::
                  resource_response_not_observed);
    }
}

} // namespace
