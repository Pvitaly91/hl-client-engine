#pragma once

#include <hlclient/goldsrc/delta_description.hpp>
#include <hlclient/goldsrc/service_message_stream.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace hlclient::test::delta_fixture {

namespace goldsrc = hlclient::goldsrc;

class BitWriter final {
public:
    void write(const std::uint32_t value, const std::size_t width)
    {
        for (std::size_t index = 0U; index < width; ++index) {
            const auto byte_index = bit_offset_ >> 3U;
            if (byte_index == bytes_.size()) {
                bytes_.push_back(std::byte{0U});
            }
            if (((value >> index) & 1U) != 0U) {
                bytes_[byte_index] |= static_cast<std::byte>(
                    1U << (bit_offset_ & 7U));
            }
            ++bit_offset_;
        }
    }

    void string(const std::string_view value)
    {
        for (const auto character : value) {
            write(static_cast<std::uint8_t>(character), 8U);
        }
        write(0U, 8U);
    }

    void align_zero()
    {
        while ((bit_offset_ & 7U) != 0U) {
            write(0U, 1U);
        }
    }

    [[nodiscard]] std::size_t bit_offset() const noexcept { return bit_offset_; }
    [[nodiscard]] std::vector<std::byte>& bytes() noexcept { return bytes_; }

private:
    std::vector<std::byte> bytes_;
    std::size_t bit_offset_{0U};
};

struct Field {
    std::string_view name;
    std::uint32_t type;
    std::uint16_t offset;
    std::uint8_t significant_bits;
    std::uint32_t premultiply{4'000U};
    std::uint32_t postmultiply{4'000U};
};

inline constexpr Field kSchemaAlphaFields[]{
    {"alpha", 0x0000'0001U, 0U, 8U},
    {"origin[0]", 0x8000'0004U, 4U, 16U, 32'000U, 4'000U},
};

inline constexpr Field kSchemaBravoFields[]{
    {"bravo", 0x0000'0002U, 0U, 11U},
    {"angles[1]", 0x0000'0010U, 8U, 8U, 400U, 4'000U},
};

[[nodiscard]] inline std::vector<std::byte> schema(
    const std::string_view name,
    const std::span<const Field> fields)
{
    BitWriter writer;
    writer.write(goldsrc::kDeltaDescriptionOpcode, 8U);
    writer.string(name);
    writer.write(static_cast<std::uint32_t>(fields.size()), 16U);
    for (const auto& field : fields) {
        writer.write(1U, 3U);
        writer.write(field.offset == 0U ? 0x7bU : 0x7fU, 8U);
        writer.write(field.type, 32U);
        writer.string(field.name);
        if (field.offset != 0U) {
            writer.write(field.offset, 16U);
        }
        writer.write(1U, 8U);
        writer.write(field.significant_bits, 8U);
        writer.write(field.premultiply, 32U);
        writer.write(field.postmultiply, 32U);
    }
    writer.align_zero();
    return writer.bytes();
}

inline void append_u32_le(
    std::vector<std::byte>& bytes,
    const std::uint32_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

inline void append_string(
    std::vector<std::byte>& bytes,
    const std::string_view value)
{
    const auto source = std::as_bytes(std::span{value.data(), value.size()});
    bytes.insert(bytes.end(), source.begin(), source.end());
    bytes.push_back(std::byte{0U});
}

[[nodiscard]] inline std::vector<std::byte> server_info_body()
{
    std::vector<std::byte> bytes;
    append_u32_le(bytes, 48U);
    append_u32_le(bytes, 0x1234'5678U);
    append_u32_le(bytes, 0xdead'beefU);
    for (std::uint8_t value = 0U; value < 16U; ++value) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    bytes.push_back(std::byte{8U});
    bytes.push_back(std::byte{0U});
    bytes.push_back(std::byte{1U});
    append_string(bytes, "sample");
    append_string(bytes, "Local Test");
    append_string(bytes, "maps/test_alpha.bsp");
    append_string(bytes, "alpha beta");
    bytes.push_back(std::byte{0U});
    return bytes;
}

[[nodiscard]] inline std::vector<std::byte> service_payload(
    const std::span<const std::vector<std::byte>> schemas,
    const std::uint8_t boundary_opcode = goldsrc::kStockPostDeltaBoundaryOpcode,
    const std::span<const std::byte> boundary_body = {})
{
    std::vector<std::byte> bytes{
        std::byte{8U},
        std::byte{'x'},
        std::byte{0U},
        std::byte{11U},
    };
    const auto info = server_info_body();
    bytes.insert(bytes.end(), info.begin(), info.end());
    bytes.push_back(
        static_cast<std::byte>(goldsrc::kPreResourceSimpleControlOpcode));
    bytes.push_back(std::byte{0U});
    bytes.push_back(std::byte{0U});
    for (const auto& encoded : schemas) {
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
    }
    bytes.push_back(static_cast<std::byte>(boundary_opcode));
    if (boundary_body.empty()) {
        bytes.push_back(std::byte{0xa5U});
    } else {
        bytes.insert(bytes.end(), boundary_body.begin(), boundary_body.end());
    }
    return bytes;
}

[[nodiscard]] inline goldsrc::OwnedServicePayload owning_payload(
    std::vector<std::byte> bytes)
{
    goldsrc::OwnedServicePayload payload;
    payload.bytes = std::move(bytes);
    payload.source_sequence = 31U;
    payload.source_acknowledgement = 17U;
    payload.source_reliable = true;
    payload.reassembled = true;
    payload.decompressed = true;
    payload.acknowledgement_reliable = true;
    payload.direction = goldsrc::NetchanDirection::server_to_client;
    return payload;
}

struct PreResourceInput {
    goldsrc::OwnedServicePayload payload;
    goldsrc::PreResourceSignonState state;
};

[[nodiscard]] inline PreResourceInput decode_pre_resource(
    std::vector<std::byte> bytes)
{
    const goldsrc::ServiceMessageStreamDecoder decoder;
    auto initial = decoder.decode(owning_payload(std::move(bytes)));
    REQUIRE(initial);
    REQUIRE(initial.stream);
    REQUIRE(initial.stream->boundary);
    auto payload = std::move(initial.stream->payload);
    const auto continued = decoder.continue_to_pre_resource(
        payload,
        *initial.stream->boundary);
    REQUIRE(continued);
    REQUIRE(continued.state);
    return PreResourceInput{std::move(payload), std::move(*continued.state)};
}

} // namespace hlclient::test::delta_fixture
