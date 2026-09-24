#include <hlclient/goldsrc/runtime_control_decoder.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <variant>

namespace {

namespace goldsrc = hlclient::goldsrc;

[[nodiscard]] goldsrc::OwnedServicePayload literal_payload()
{
    // Literal Protocol 48 service messages, not encoder output:
    // svc_nop; svc_time(12.5f); svc_setview(42); svc_signonnum(1).
    constexpr std::array bytes{
        std::byte{0x01U}, std::byte{0x07U}, std::byte{0x00U}, std::byte{0x00U},
        std::byte{0x48U}, std::byte{0x41U}, std::byte{0x05U}, std::byte{0x2aU},
        std::byte{0x00U}, std::byte{0x19U}, std::byte{0x01U},
    };
    goldsrc::OwnedServicePayload payload;
    payload.bytes.assign(bytes.begin(), bytes.end());
    payload.source_sequence = 120U;
    payload.source_acknowledgement = 119U;
    payload.source_reliable = true;
    payload.reassembled = true;
    payload.decompressed = true;
    payload.acknowledgement_reliable = true;
    payload.direction = goldsrc::NetchanDirection::server_to_client;
    return payload;
}

} // namespace

int main()
{
    constexpr std::uint64_t generation = 7U;
    auto payload = literal_payload();
    const auto cursor = goldsrc::StockRuntimeSourceCursor::create(0U, 0U, payload.bytes.size());
    if (!cursor) {
        std::cerr << "error=size_overflow context=fixture_cursor\n";
        return 2;
    }

    goldsrc::RuntimeControlState state{generation};
    const goldsrc::RuntimeControlDecoder decoder;
    const auto result = decoder.decode_and_apply(
        goldsrc::RuntimeControlDecodeInput{payload, *cursor, generation, 0U}, state);
    if (!result) {
        const auto code = result.error ? goldsrc::to_string(result.error->code)
                                       : std::string_view{"missing_typed_error"};
        std::cerr << "error=" << code;
        if (result.error && result.error->cursor) {
            std::cerr << " cursor_bits=" << result.error->cursor->absolute_bit_offset();
        }
        if (result.error && result.error->wire_opcode) {
            std::cerr << " opcode=" << static_cast<unsigned>(*result.error->wire_opcode);
        }
        std::cerr << '\n';
        return 1;
    }

    const auto& batch = *result.batch;
    std::cout << "profile=" << goldsrc::to_string(batch.profile)
              << " source_category=" << goldsrc::to_string(batch.specification_source)
              << " stock_interoperability=" << goldsrc::to_string(batch.stock_verification) << '\n';
    for (const auto& event : batch.events) {
        std::cout << "message=" << goldsrc::to_string(event.opcode)
                  << " ordinal=" << event.provenance.message_ordinal
                  << " cursor_bits=" << event.provenance.start_cursor.absolute_bit_offset() << ".."
                  << event.provenance.end_cursor.absolute_bit_offset();
        switch (event.kind) {
        case goldsrc::RuntimeControlMessageKind::nop:
            break;
        case goldsrc::RuntimeControlMessageKind::server_time:
            std::cout << " seconds="
                      << std::get<goldsrc::RuntimeControlServerTime>(event.body).seconds;
            break;
        case goldsrc::RuntimeControlMessageKind::view_entity_reference:
            std::cout << " view_entity_reference="
                      << std::get<goldsrc::RuntimeControlViewEntityReference>(event.body)
                             .wire_entity_reference;
            break;
        case goldsrc::RuntimeControlMessageKind::signon_control:
            std::cout
                << " signon_number="
                << static_cast<unsigned>(
                       std::get<goldsrc::RuntimeControlSignonControl>(event.body).signon_number);
            break;
        }
        std::cout << '\n';
    }

    std::cout << "consumed_bytes=" << batch.consumed_byte_count
              << " consumed_bits=" << batch.consumed_bit_count
              << " generation=" << state.source_generation()
              << " committed_messages=" << state.committed_message_count() << '\n';
    std::cout << "state_server_time=" << state.server_time()->seconds
              << " state_view_entity_reference=" << state.view_entity()->wire_entity_reference
              << " state_signon_number="
              << static_cast<unsigned>(state.signon_control()->signon_number) << '\n';
    std::cout << "result=success\n";
    return 0;
}
