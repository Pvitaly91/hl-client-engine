#include <hlclient/goldsrc/entity_baseline_decoder.hpp>
#include <hlclient/goldsrc/delta_description.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {
using namespace hlclient::goldsrc;

struct Bits {
    std::vector<std::byte> bytes;
    std::size_t bit{0};
    void put(std::uint32_t value, std::size_t width) {
        for (std::size_t i=0;i<width;++i,++bit) {
            if ((value & (1U<<i)) != 0U) {
                if ((bit>>3U) == bytes.size()) bytes.push_back(std::byte{0});
                bytes[bit>>3U] |= static_cast<std::byte>(1U << (bit&7U));
            } else if ((bit>>3U) == bytes.size()) bytes.push_back(std::byte{0});
        }
    }
    void text(std::string_view s) { for (auto c:s) put(static_cast<std::uint8_t>(c),8); put(0,8); }
    void align() { while ((bit&7U)!=0U) put(0,1); }
};

std::vector<std::byte> schema_bytes(std::string_view name) {
    (void)name;
    return {std::byte{0x0e},std::byte{0x78},std::byte{0x00},std::byte{0x01},
            std::byte{0x00},std::byte{0xd9},std::byte{0x0b},std::byte{0x00},
            std::byte{0x00},std::byte{0x00},std::byte{0xc8},std::byte{0x03},
            std::byte{0x08},std::byte{0x40},std::byte{0x00},std::byte{0x7d},
            std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x7d},
            std::byte{0x00},std::byte{0x00},std::byte{0x00}};
}

} // namespace

int main() {
    DeltaSchemaRegistryBuilder rb;
    for (auto name : {"x"}) {
        const auto bytes = schema_bytes(name);
        const auto parsed = DeltaDescriptionParser{}.parse(bytes, 0U);
        if (!parsed || !rb.insert(*parsed.schema)) {
            std::cout << "result=failure schema bytes=" << bytes.size() << " code=" << (parsed.error ? static_cast<int>(parsed.error->code) : -1) << " bit=" << (parsed.error ? parsed.error->bit_offset : 0) << " ctx=" << (parsed.error ? parsed.error->context : "") << "\n"; return 1; }
    }
    const auto schemas = std::move(rb).publish();
    Bits b;
    b.put(2,11); b.put(0,2); b.put(1,3); b.put(1,8); b.put(42,8);
    b.put(20,11); b.put(0,2); b.put(1,3); b.put(1,8); b.put(7,8);
    b.put(kGoldSrcBaselineTerminator,kGoldSrcBaselineTerminatorBits); b.put(0,6); b.align();
    OwnedServicePayload payload; payload.bytes = b.bytes; payload.decompressed = true;
    payload.direction = NetchanDirection::server_to_client; payload.source_sequence = 17;
    const auto cursor = StockRuntimeSourceCursor::create(0,0,payload.bytes.size());
    if (!cursor) { std::cout << "result=failure cursor\n"; return 1; }
    EntityBaselineDecodeInput input{&payload,*cursor,4,1,1,"x","x","x",b.bit};
    const auto decoded = GoldSrcEntityBaselineDecoder{}.decode(input, schemas);
    if (!decoded || decoded.registry->baseline_count() != 2U) {
        std::cout << "result=failure baseline\n"; return 1;
    }
    std::cout << "profile=public_goldsrc48_entity_delta_v1 baselines="
              << decoded.registry->baseline_count() << " entities="
              << decoded.entity_count << " consumed_bits=" << decoded.bits_consumed
              << " end_byte=" << decoded.end_cursor.byte_offset() << " result=success\n";
    return 0;
}
