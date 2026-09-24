#include <algorithm>
#include <cmath>
#include <hlclient/goldsrc/bit_reader.hpp>
#include <hlclient/goldsrc/bit_writer.hpp>
#include <hlclient/goldsrc/byte_reader.hpp>
#include <hlclient/goldsrc/client_message.hpp>
#include <hlclient/goldsrc/delta_value_decoder.hpp>
#include <hlclient/goldsrc/reference_client_move.hpp>
#include <hlclient/goldsrc/resource_client_response.hpp>
#include <limits>
#include <new>

namespace hlclient::goldsrc {
namespace {
constexpr auto delta_profile =
    DeltaValueCompatibilityProfile::public_goldsrc48_usercmd_delta_v1;
using Values = std::array<std::int32_t, kGoldSrcUserCmdSchemaFieldCount>;

Values values(const GoldSrcWireUserCmd &c) noexcept {
    return {c.lerp_msec,
            c.msec,
            c.angle_turns[1],
            c.angle_turns[0],
            c.buttons,
            c.forward,
            c.light_level,
            c.side,
            c.up,
            c.impulse,
            c.angle_turns[2],
            c.impact_index,
            c.impact_eighths[0],
            c.impact_eighths[1],
            c.impact_eighths[2]};
}

GoldSrcWireUserCmd project(const DeltaObjectState &state) {
    Values v{};
    for (const auto &field : goldsrc_usercmd_schema_binding_entries()) {
        const auto &scalar = state.fields()[field.wire_index].value();
        double number = 0;
        if (const auto *real = std::get_if<double>(&scalar)) {
            number = field.base_type == DeltaFieldBaseType::angle
                         ? *real * 65536.0 / 360.0
                         : *real * field.premultiply_wire_value /
                               field.postmultiply_wire_value;
        } else {
            number = std::get<std::uint32_t>(scalar);
        }
        v[field.wire_index] = static_cast<std::int32_t>(std::llround(number));
    }
    GoldSrcWireUserCmd c;
    c.lerp_msec = static_cast<std::uint16_t>(v[0]);
    c.msec = static_cast<std::uint8_t>(v[1]);
    c.angle_turns = {static_cast<std::uint16_t>(v[3]), static_cast<std::uint16_t>(v[2]),
                     static_cast<std::uint16_t>(v[10])};
    c.buttons = static_cast<std::uint16_t>(v[4]);
    c.forward = static_cast<std::int16_t>(v[5]);
    c.light_level = static_cast<std::uint8_t>(v[6]);
    c.side = static_cast<std::int16_t>(v[7]);
    c.up = static_cast<std::int16_t>(v[8]);
    c.impulse = static_cast<std::uint8_t>(v[9]);
    c.impact_index = static_cast<std::uint8_t>(v[11]);
    c.impact_eighths = {static_cast<std::int16_t>(v[12]),
                        static_cast<std::int16_t>(v[13]),
                        static_cast<std::int16_t>(v[14])};
    return c;
}

ReferenceMoveResult fail(ReferenceMoveErrorCode code, const ReferenceMoveSource &source,
                         std::size_t bit) {
    return {{}, ReferenceMoveError{code, source, bit, {}}};
}
bool valid_limits(const ReferenceMoveLimits &l) noexcept {
    return l.maximum_commands > 0 && l.maximum_commands <= 62 &&
           l.maximum_message_bytes >= 6 && l.maximum_message_bytes <= 8192 &&
           l.maximum_messages_per_payload > 0 &&
           l.maximum_messages_per_payload <= 65536 && l.maximum_retained_commands > 0 &&
           l.maximum_retained_commands <= 16384;
}
} // namespace

bool valid_wire_usercmd(const GoldSrcWireUserCmd &c) noexcept {
    return c.lerp_msec <= 511 && c.impact_index <= 63 &&
           std::abs(static_cast<int>(c.forward)) <= 2047 &&
           std::abs(static_cast<int>(c.side)) <= 2047 &&
           std::abs(static_cast<int>(c.up)) <= 2047 &&
           std::all_of(c.impact_eighths.begin(), c.impact_eighths.end(),
                       [](auto n) { return n != INT16_MIN; });
}
std::optional<std::uint16_t> quantize_wire_angle(double degrees) noexcept {
    if (!std::isfinite(degrees))
        return {};
    const auto ticks = std::trunc(std::fmod(degrees, 360.0) * 65536.0 / 360.0);
    const auto integer = static_cast<std::int32_t>(ticks);
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(integer) & 65535U);
}
std::optional<std::int16_t> quantize_wire_movement(double units) noexcept {
    if (!std::isfinite(units) || std::abs(std::trunc(units)) > 2047)
        return {};
    return static_cast<std::int16_t>(std::trunc(units));
}

bool transform_reference_move_body(std::span<std::byte> body, std::size_t length,
                                   std::uint32_t sequence, bool decode) noexcept {
    if (length > body.size() || length > 255 || sequence > 0x3fffffffU)
        return false;
    // Protocol data only: ReHLDS pinned COM_Munge table, see wire contract.
    constexpr std::array<unsigned, 16> table{0x7a, 0x64, 0x05, 0xf1, 0x1b, 0x9b,
                                             0xa0, 0xb5, 0xca, 0xed, 0x61, 0x0d,
                                             0x4a, 0xdf, 0x8e, 0xc7};
    for (std::size_t block = 0; block < length / 4; ++block) {
        const std::array<std::byte, 4> input{body[block * 4], body[block * 4 + 1],
                                             body[block * 4 + 2], body[block * 4 + 3]};
        for (unsigned j = 0; j < 4; ++j) {
            const auto key = static_cast<std::uint8_t>(
                (sequence >> (j * 8)) ^ ((~sequence) >> ((3 - j) * 8)) ^
                (0xa5U | (j << j) | j | table[(block + j) % 16]));
            body[block * 4 + (decode ? 3 - j : j)] =
                input[decode ? j : 3 - j] ^ std::byte{key};
        }
    }
    return true;
}

GoldSrcReferenceClientMoveCodec::GoldSrcReferenceClientMoveCodec(
    GoldSrcUserCmdSchemaBinding binding, std::uint64_t generation,
    ReferenceMoveLimits limits)
    : binding_(std::move(binding)), generation_(generation), limits_(limits) {}
bool GoldSrcReferenceClientMoveCodec::valid_configuration() const noexcept {
    return generation_ != 0 && valid_limits(limits_) &&
           binding_.profile() ==
               GoldSrcUserCmdSchemaBindingProfile::public_goldsrc48_usercmd_schema_v1;
}

bool GoldSrcReferenceClientMoveCodec::accepts_source(
    const ReferenceMoveSource &source) const noexcept {
    return source.generation == generation_ && source.sequence <= 0x3fffffffU;
}

ReferenceMoveResult
GoldSrcReferenceClientMoveCodec::decode(std::span<const std::byte> payload,
                                        std::size_t start,
                                        const ReferenceMoveSource &source) const {
    if (!valid_configuration())
        return fail(ReferenceMoveErrorCode::invalid_configuration, source, 0);
    if (source.generation != generation_ || source.sequence > 0x3fffffffU ||
        start > payload.size())
        return fail(ReferenceMoveErrorCode::invalid_context, source, 0);
    const auto bad = [&](ReferenceMoveErrorCode code, std::size_t byte) {
        return fail(code, source, byte * 8);
    };
    if (payload.size() - start < 6)
        return bad(ReferenceMoveErrorCode::truncated, start);
    ByteReader header{payload.subspan(start, 3)};
    if (*header.read_uint8() != 2)
        return bad(ReferenceMoveErrorCode::wrong_opcode, start);
    const auto length = *header.read_uint8();
    const auto recorded = *header.read_uint8();
    if (length < 3 || length > payload.size() - start - 3)
        return bad(ReferenceMoveErrorCode::invalid_length, start + 1);
    try {
        const auto span =
            payload.subspan(start + 3, std::min(payload.size() - start - 3,
                                                limits_.maximum_message_bytes - 3));
        std::vector<std::byte> body(span.begin(), span.end());
        if (!transform_reference_move_body(body, length, source.sequence, true))
            return bad(ReferenceMoveErrorCode::byte_limit, start + 1);
        ReferenceClientMoveMessage message;
        message.source = source;
        message.start_byte = start;
        message.transform_length = length;
        message.checksum = recorded;
        message.loss_metadata = std::to_integer<std::uint8_t>(body[0]);
        message.backup_count = std::to_integer<std::uint8_t>(body[1]);
        message.new_count = std::to_integer<std::uint8_t>(body[2]);
        const auto count =
            static_cast<std::size_t>(message.backup_count) + message.new_count;
        if (count > limits_.maximum_commands)
            return bad(ReferenceMoveErrorCode::count_limit, start + 4);
        auto base =
            DeltaObjectBuilder{{}, delta_profile}.build_default(binding_.schema());
        if (!base)
            return bad(ReferenceMoveErrorCode::invalid_configuration, start);
        std::size_t cursor = 24;
        for (std::size_t i = 0; i < count; ++i) {
            auto decoded = GoldSrcDeltaValueDecoder{{}, delta_profile}.decode_delta(
                binding_.schema(), &*base.state,
                {body, cursor, body.size() * 8 - cursor, {}, {}, false});
            if (!decoded)
                return fail(ReferenceMoveErrorCode::delta_failed, source,
                            (start + 3) * 8 +
                                (decoded.error ? decoded.error->bit_offset : cursor));
            BitReader padding{body, decoded.next_bit_offset};
            const auto pad = padding.align_to_byte_zero_padding();
            if (pad != BitReaderError::none)
                return fail(ReferenceMoveErrorCode::nonzero_padding, source,
                            (start + 3) * 8 + decoded.next_bit_offset);
            auto command = project(*decoded.state);
            if (!valid_wire_usercmd(command))
                return bad(ReferenceMoveErrorCode::invalid_command, start);
            // Retain representation metadata at the already validated command
            // boundary; scalar reconstruction is solely the shared decoder's.
            BitReader mask_reader{body, cursor};
            const auto mask_bytes =
                static_cast<std::uint8_t>(mask_reader.read_bits(3).value);
            std::uint16_t mask = 0;
            for (unsigned m = 0; m < mask_bytes; ++m)
                mask |= static_cast<std::uint16_t>(mask_reader.read_bits(8).value
                                                   << (m * 8));
            const auto previous_values = values(project(*base.state));
            const auto current_values = values(command);
            std::size_t redundant = 0;
            for (unsigned f = 0; f < 15; ++f)
                if ((mask & (1U << f)) != 0 && previous_values[f] == current_values[f])
                    ++redundant;
            message.commands.push_back({command, (start + 3) * 8 + cursor,
                                        (start + 3) * 8 + decoded.next_bit_offset,
                                        (start + 3) * 8 + padding.bit_offset(),
                                        i < message.backup_count, mask, mask_bytes,
                                        redundant});
            cursor = padding.bit_offset();
            base.state.emplace(std::move(*decoded.state));
        }
        const auto body_size = cursor / 8;
        if (std::min<std::size_t>(body_size, 255) != length)
            return bad(ReferenceMoveErrorCode::invalid_length, start + 1);
        const auto computed =
            GoldSrcMoveChecksum{
                GoldSrcMoveChecksumProfile::public_goldsrc48_crc32_low8_v1, 8192}
                .compute({source.sequence, body_size * 8},
                         std::span<const std::byte>{body}.first(body_size));
        if (!computed || *computed.checksum != recorded)
            return bad(ReferenceMoveErrorCode::checksum_mismatch, start + 2);
        message.checksum_matched = true;
        message.end_byte = start + 3 + body_size;
        message.bytes.assign(payload.begin() + start,
                             payload.begin() + message.end_byte);
        return {std::move(message), {}};
    } catch (const std::bad_alloc &) {
        return bad(ReferenceMoveErrorCode::allocation_failed, start);
    }
}

ReferenceMoveResult
GoldSrcReferenceClientMoveCodec::encode(std::span<const GoldSrcWireUserCmd> commands,
                                        std::size_t backups, std::uint8_t loss,
                                        const ReferenceMoveSource &source) const {
    if (!valid_configuration())
        return fail(ReferenceMoveErrorCode::invalid_configuration, source, 0);
    if (source.generation != generation_ || source.sequence > 0x3fffffffU)
        return fail(ReferenceMoveErrorCode::invalid_context, source, 0);
    if (commands.size() > limits_.maximum_commands || backups > commands.size())
        return fail(ReferenceMoveErrorCode::count_limit, source, 0);
    if (!std::all_of(commands.begin(), commands.end(), valid_wire_usercmd))
        return fail(ReferenceMoveErrorCode::invalid_command, source, 0);
    try {
        ReferenceClientMoveMessage message;
        message.source = source;
        message.loss_metadata = loss;
        message.backup_count = static_cast<std::uint8_t>(backups);
        message.new_count = static_cast<std::uint8_t>(commands.size() - backups);
        std::vector<std::byte> body(limits_.maximum_message_bytes - 3);
        body[0] = std::byte{loss};
        body[1] = std::byte{message.backup_count};
        body[2] = std::byte{message.new_count};
        BitWriter writer{body, 24};
        Values base{};
        for (std::size_t i = 0; i < commands.size(); ++i) {
            const auto current = values(commands[i]);
            std::array<std::uint8_t, 2> mask{};
            unsigned mask_bytes = 0;
            for (std::size_t f = 0; f < current.size(); ++f)
                if (current[f] != base[f]) {
                    mask[f / 8] |= static_cast<std::uint8_t>(1U << (f % 8));
                    mask_bytes = static_cast<unsigned>(f / 8 + 1);
                }
            const auto start = writer.bit_offset();
            if (!writer.write_bits(mask_bytes, 3))
                return fail(ReferenceMoveErrorCode::byte_limit, source,
                            writer.bit_offset() + 24);
            for (unsigned m = 0; m < mask_bytes; ++m)
                if (!writer.write_bits(mask[m], 8))
                    return fail(ReferenceMoveErrorCode::byte_limit, source,
                                writer.bit_offset() + 24);
            for (const auto &f : binding_.entries())
                if ((mask[f.wire_index / 8] & (1U << (f.wire_index % 8))) != 0) {
                    const auto value = current[f.wire_index];
                    const auto bits =
                        f.signed_value
                            ? (static_cast<std::uint32_t>(std::abs(value)) << 1U) |
                                  (value < 0 ? 1U : 0U)
                            : static_cast<std::uint32_t>(value);
                    if (!writer.write_bits(bits, f.significant_bits))
                        return fail(ReferenceMoveErrorCode::byte_limit, source,
                                    writer.bit_offset() + 24);
                }
            const auto meaningful = writer.bit_offset();
            if (writer.align_to_byte_zero_padding() != BitWriterError::none)
                return fail(ReferenceMoveErrorCode::byte_limit, source,
                            writer.bit_offset() + 24);
            message.commands.push_back(
                {commands[i], 24 + start, 24 + meaningful, 24 + writer.bit_offset(),
                 i < backups,
                 static_cast<std::uint16_t>(mask[0] |
                                            (static_cast<unsigned>(mask[1]) << 8)),
                 static_cast<std::uint8_t>(mask_bytes), 0});
            base = current;
        }
        body.resize(writer.bit_offset() / 8);
        message.transform_length =
            static_cast<std::uint8_t>(std::min<std::size_t>(body.size(), 255));
        const auto checksum =
            GoldSrcMoveChecksum{
                GoldSrcMoveChecksumProfile::public_goldsrc48_crc32_low8_v1, 8192}
                .compute({source.sequence, body.size() * 8}, body);
        if (!checksum)
            return fail(ReferenceMoveErrorCode::invalid_context, source, 0);
        message.checksum = *checksum.checksum;
        message.checksum_matched = true;
        if (!transform_reference_move_body(body, message.transform_length,
                                           source.sequence, false))
            return fail(ReferenceMoveErrorCode::invalid_context, source, 0);
        message.bytes = {std::byte{2}, std::byte{message.transform_length},
                         std::byte{message.checksum}};
        message.bytes.insert(message.bytes.end(), body.begin(), body.end());
        message.end_byte = message.bytes.size();
        return {std::move(message), {}};
    } catch (const std::bad_alloc &) {
        return fail(ReferenceMoveErrorCode::allocation_failed, source, 0);
    }
}

ReferenceClientPayloadResult decode_reference_client_payload(
    const GoldSrcReferenceClientMoveCodec &codec, std::span<const std::byte> payload,
    const ReferenceMoveSource &source, ReferenceMoveLimits limits) {
    ReferenceClientPayloadResult result;
    const auto fail_at = [&](ReferenceMoveErrorCode code, std::size_t offset,
                             std::uint8_t opcode) {
        result.error = ReferenceMoveError{code, source, offset * 8, opcode};
        result.moves.clear(); // One payload transaction; no partial commands.
    };
    if (!valid_limits(limits) || payload.size() > 1'048'576U) {
        fail_at(ReferenceMoveErrorCode::invalid_configuration, 0, 0);
        return result;
    }
    if (!codec.valid_configuration() || !codec.accepts_source(source)) {
        fail_at(ReferenceMoveErrorCode::invalid_context, 0, 0);
        return result;
    }
    try {
        std::size_t messages = 0;
        std::size_t retained_commands = 0;
        while (result.end_byte < payload.size()) {
            const auto start = result.end_byte;
            const auto opcode = std::to_integer<std::uint8_t>(payload[start]);
            if (++messages > limits.maximum_messages_per_payload) {
                fail_at(ReferenceMoveErrorCode::byte_limit, start, opcode);
                break;
            }
            if (opcode == 2) {
                if (!result.moves.empty()) {
                    fail_at(ReferenceMoveErrorCode::duplicate_move, start, opcode);
                    break;
                }
                auto move = codec.decode(payload, start, source);
                if (!move) {
                    result.error = move.error;
                    result.moves.clear();
                    break;
                }
                if (move.message->commands.size() >
                    limits.maximum_retained_commands - retained_commands) {
                    fail_at(ReferenceMoveErrorCode::count_limit, start, opcode);
                    break;
                }
                retained_commands += move.message->commands.size();
                result.end_byte = move.message->end_byte;
                result.moves.push_back(std::move(*move.message));
            } else if (opcode == 1) {
                ++result.end_byte;
            } else if (opcode == 4) {
                if (payload.size() - start < 2) {
                    fail_at(ReferenceMoveErrorCode::truncated, start, opcode);
                    break;
                }
                result.end_byte += 2;
            } else if (opcode == 3) {
                ByteReader reader{payload.subspan(
                    start + 1,
                    std::min<std::size_t>(payload.size() - start - 1, 2048))};
                auto command = reader.read_c_string();
                if (!command) {
                    fail_at(ReferenceMoveErrorCode::truncated, start, opcode);
                    break;
                }
                const auto consumed = reader.position() + 1;
                if (*command == "new" &&
                    !parse_initial_signon_request(payload.subspan(start, consumed))) {
                    fail_at(ReferenceMoveErrorCode::unsupported_boundary, start,
                            opcode);
                    break;
                }
                result.end_byte += consumed; // Never execute, retain or print strings.
            } else if (opcode == 5) {
                // Reuse the existing exact resource-response parser; zero-entry
                // form is the existing sign-on replay's explicitly framed case.
                if (payload.size() - start >= 3 && payload[start + 1] == std::byte{0} &&
                    payload[start + 2] == std::byte{0}) {
                    result.end_byte += 3;
                } else {
                    const auto bytes = payload.subspan(start);
                    const auto parsed = Opcode5ResourceResponseParser{}.parse(
                        bytes, {0, bytes.size(), bytes.size()},
                        Opcode5ResourceResponseSourceProfile::
                            captured_reliable_semantic_fragment);
                    if (!parsed) {
                        fail_at(ReferenceMoveErrorCode::unsupported_boundary, start,
                                opcode);
                        break;
                    }
                    result.end_byte += parsed.bytes_consumed;
                }
            } else {
                fail_at(ReferenceMoveErrorCode::unsupported_boundary, start, opcode);
                break;
            }
            ++result.opcode_counts[opcode];
        }
    } catch (const std::bad_alloc &) {
        fail_at(ReferenceMoveErrorCode::allocation_failed, result.end_byte, 0);
    }
    return result;
}

std::string_view to_string(ReferenceMoveErrorCode code) noexcept {
    switch (code) {
    case ReferenceMoveErrorCode::duplicate_move:
        return "duplicate_move";
    case ReferenceMoveErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ReferenceMoveErrorCode::invalid_context:
        return "invalid_context";
    case ReferenceMoveErrorCode::invalid_command:
        return "invalid_command";
    case ReferenceMoveErrorCode::truncated:
        return "truncated";
    case ReferenceMoveErrorCode::wrong_opcode:
        return "wrong_opcode";
    case ReferenceMoveErrorCode::invalid_length:
        return "invalid_length";
    case ReferenceMoveErrorCode::count_limit:
        return "count_limit";
    case ReferenceMoveErrorCode::byte_limit:
        return "byte_limit";
    case ReferenceMoveErrorCode::delta_failed:
        return "delta_failed";
    case ReferenceMoveErrorCode::nonzero_padding:
        return "nonzero_padding";
    case ReferenceMoveErrorCode::checksum_mismatch:
        return "checksum_mismatch";
    case ReferenceMoveErrorCode::unsupported_boundary:
        return "unsupported_boundary";
    case ReferenceMoveErrorCode::allocation_failed:
        return "allocation_failed";
    }
    return "unknown";
}
} // namespace hlclient::goldsrc
