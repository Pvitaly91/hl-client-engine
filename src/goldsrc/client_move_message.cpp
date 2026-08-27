#include <hlclient/goldsrc/client_move_message.hpp>

#include <algorithm>
#include <limits>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] constexpr bool valid_profile(
    const GoldSrcClientMoveCompatibilityProfile profile) noexcept
{
    return profile ==
               GoldSrcClientMoveCompatibilityProfile::synthetic_client_move_v1 ||
        profile == GoldSrcClientMoveCompatibilityProfile::
                       stock_protocol_48_build_10210_evidence_pending;
}

[[nodiscard]] constexpr bool valid_end_policy(
    const GoldSrcClientMoveEndPolicy policy) noexcept
{
    return policy == GoldSrcClientMoveEndPolicy::leave_trailing_bytes ||
        policy == GoldSrcClientMoveEndPolicy::require_exact_end;
}

constexpr std::size_t kHeaderBytes = 4U;
constexpr std::size_t kDeltaLengthBytes = 2U;

[[nodiscard]] std::size_t effective_packet_byte_limit(
    const GoldSrcUserCmdLimits& limits) noexcept
{
    return std::min(
        limits.maximum_encoded_bytes, limits.maximum_encoded_bits / 8U);
}

[[nodiscard]] GoldSrcClientMoveEncodeResult encode_failure(
    const GoldSrcClientMoveErrorCode code,
    const std::string_view context,
    const std::size_t byte_offset = 0U,
    const std::optional<std::size_t> command_index = std::nullopt,
    const std::optional<GoldSrcUserCmdDeltaErrorCode> delta_code = std::nullopt,
    const std::optional<GoldSrcMoveChecksumErrorCode> checksum_code =
        std::nullopt)
{
    return {std::nullopt,
            GoldSrcClientMoveError{
                code,
                byte_offset,
                command_index,
                delta_code,
                checksum_code,
                context}};
}

[[nodiscard]] GoldSrcClientMoveDecodeResult decode_failure(
    const GoldSrcClientMoveErrorCode code,
    const std::string_view context,
    const std::size_t byte_offset = 0U,
    const std::optional<std::size_t> command_index = std::nullopt,
    const std::optional<GoldSrcUserCmdDeltaErrorCode> delta_code = std::nullopt,
    const std::optional<GoldSrcMoveChecksumErrorCode> checksum_code =
        std::nullopt)
{
    return {std::nullopt,
            GoldSrcClientMoveError{
                code,
                byte_offset,
                command_index,
                delta_code,
                checksum_code,
                context},
            0U,
            0U};
}

[[nodiscard]] std::optional<GoldSrcUserCmdState> default_base_for(
    const GoldSrcUserCmdSequence sequence,
    const GoldSrcUserCmdLimits& limits)
{
    auto info = goldsrc_usercmd_default_create_info(sequence);
    const auto result = GoldSrcUserCmdState::create(info, limits);
    if (!result || !result.state) {
        return std::nullopt;
    }
    return std::move(*result.state);
}

[[nodiscard]] std::uint16_t read_u16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 1U]))
         << 8U));
}

} // namespace

GoldSrcClientMoveMessage::GoldSrcClientMoveMessage(
    const std::uint8_t checksum,
    const std::uint8_t synthetic_loss_metadata,
    const std::size_t backup_command_count,
    const std::size_t new_command_count,
    std::vector<GoldSrcUserCmdState> commands,
    std::vector<std::byte> bytes,
    const std::size_t changed_field_count,
    const GoldSrcClientMoveCompatibilityProfile profile) noexcept
    : checksum_{checksum},
      synthetic_loss_metadata_{synthetic_loss_metadata},
      backup_command_count_{backup_command_count},
      new_command_count_{new_command_count},
      commands_{std::move(commands)},
      bytes_{std::move(bytes)},
      changed_field_count_{changed_field_count},
      profile_{profile}
{
}

std::uint8_t GoldSrcClientMoveMessage::opcode() const noexcept
{
    return kSyntheticClientMoveOpcode;
}
std::uint8_t GoldSrcClientMoveMessage::checksum() const noexcept
{
    return checksum_;
}
std::uint8_t GoldSrcClientMoveMessage::synthetic_loss_metadata() const noexcept
{
    return synthetic_loss_metadata_;
}
std::size_t GoldSrcClientMoveMessage::backup_command_count() const noexcept
{
    return backup_command_count_;
}
std::size_t GoldSrcClientMoveMessage::new_command_count() const noexcept
{
    return new_command_count_;
}
const std::vector<GoldSrcUserCmdState>&
GoldSrcClientMoveMessage::commands() const noexcept
{
    return commands_;
}
const std::vector<std::byte>& GoldSrcClientMoveMessage::bytes() const noexcept
{
    return bytes_;
}
std::size_t GoldSrcClientMoveMessage::bit_length() const noexcept
{
    return bytes_.size() * 8U;
}
std::size_t GoldSrcClientMoveMessage::changed_field_count() const noexcept
{
    return changed_field_count_;
}
GoldSrcClientMoveCompatibilityProfile
GoldSrcClientMoveMessage::profile() const noexcept
{
    return profile_;
}

GoldSrcClientMoveMessageCodec::GoldSrcClientMoveMessageCodec(
    GoldSrcUserCmdLimits limits,
    const GoldSrcClientMoveCompatibilityProfile profile) noexcept
    : limits_{limits},
      profile_{profile},
      valid_configuration_{
          valid_goldsrc_usercmd_limits(limits_) && valid_profile(profile_)}
{
}

bool GoldSrcClientMoveMessageCodec::valid_configuration() const noexcept
{
    return valid_configuration_;
}

GoldSrcClientMoveCompatibilityProfile
GoldSrcClientMoveMessageCodec::profile() const noexcept
{
    return profile_;
}

GoldSrcClientMoveEncodeResult GoldSrcClientMoveMessageCodec::encode(
    const std::span<const std::shared_ptr<const GoldSrcUserCmdState>> commands,
    const GoldSrcUserCmdSchemaBinding& binding,
    const GoldSrcClientMoveEncodeContext& context) const
{
    if (!valid_configuration_) {
        return encode_failure(
            GoldSrcClientMoveErrorCode::invalid_configuration,
            "Client-move codec configuration is invalid");
    }
    if (profile_ !=
        GoldSrcClientMoveCompatibilityProfile::synthetic_client_move_v1) {
        return encode_failure(
            GoldSrcClientMoveErrorCode::stock_evidence_pending,
            "Stock client-move opcode, envelope, and checksum remain pending");
    }
    const auto packet_byte_limit = effective_packet_byte_limit(limits_);
    if (packet_byte_limit < kHeaderBytes) {
        return encode_failure(
            GoldSrcClientMoveErrorCode::packet_size_exceeded,
            "Client-move header does not fit the configured byte/bit budget");
    }
    if (context.backup_command_count >
        std::numeric_limits<std::size_t>::max() - context.new_command_count) {
        return encode_failure(
            GoldSrcClientMoveErrorCode::count_limit_exceeded,
            "Client-move command count sum overflowed");
    }
    const auto total_count = context.backup_command_count +
        context.new_command_count;
    if (context.new_command_count == 0U ||
        context.backup_command_count > limits_.maximum_backup_commands ||
        context.new_command_count > limits_.maximum_new_commands ||
        total_count > limits_.maximum_commands_per_packet) {
        return encode_failure(
            GoldSrcClientMoveErrorCode::count_limit_exceeded,
            "Client-move command counts exceed their configured bounds");
    }
    if (total_count != commands.size()) {
        return encode_failure(
            GoldSrcClientMoveErrorCode::count_mismatch,
            "Client-move counts do not match the ordered command collection");
    }
    if (context.backup_command_count > 15U ||
        context.new_command_count > 15U) {
        return encode_failure(
            GoldSrcClientMoveErrorCode::count_limit_exceeded,
            "Synthetic client-move count nibbles overflow");
    }
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        if (!commands[index] ||
            (index != 0U &&
             commands[index - 1U]->command_sequence().value() >=
                 commands[index]->command_sequence().value())) {
            return encode_failure(
                GoldSrcClientMoveErrorCode::command_order_invalid,
                "Client-move commands must be non-null and strictly oldest-first",
                0U,
                index);
        }
    }

    auto default_base = default_base_for(
        commands.front()->command_sequence(), limits_);
    if (!default_base) {
        return encode_failure(
            GoldSrcClientMoveErrorCode::invalid_context,
            "Synthetic default usercmd base could not be created");
    }
    GoldSrcUserCmdDeltaCodec delta_codec{
        limits_,
        GoldSrcUserCmdDeltaCompatibilityProfile::synthetic_usercmd_delta_v1};
    std::vector<GoldSrcUserCmdEncodedDelta> encoded_deltas;
    encoded_deltas.reserve(commands.size());
    std::size_t packet_bytes = kHeaderBytes;
    std::size_t changed_fields = 0U;
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        const auto& base = index == 0U ? *default_base : *commands[index - 1U];
        const auto encoded = delta_codec.encode_delta(
            base,
            *commands[index],
            binding,
            GoldSrcUserCmdDeltaEncodeContext{
                GoldSrcUserCmdDeltaBasePolicy::explicit_command});
        if (!encoded || !encoded.delta) {
            return encode_failure(
                GoldSrcClientMoveErrorCode::delta_encode_failed,
                "A client-move command delta could not be encoded",
                packet_bytes,
                index,
                encoded.error ? std::optional{encoded.error->code}
                              : std::nullopt);
        }
        if (encoded.delta->bit_length > UINT16_MAX ||
            packet_bytes > packet_byte_limit - kDeltaLengthBytes ||
            encoded.delta->bytes.size() >
                packet_byte_limit - packet_bytes - kDeltaLengthBytes) {
            return encode_failure(
                GoldSrcClientMoveErrorCode::packet_size_exceeded,
                "Client-move delta does not fit the bounded packet budget",
                packet_bytes,
                index);
        }
        packet_bytes += kDeltaLengthBytes + encoded.delta->bytes.size();
        changed_fields += encoded.delta->changed_field_count;
        encoded_deltas.push_back(std::move(*encoded.delta));
    }

    std::vector<std::byte> bytes(packet_bytes, std::byte{0U});
    bytes[0U] = static_cast<std::byte>(kSyntheticClientMoveOpcode);
    bytes[2U] = static_cast<std::byte>(context.synthetic_loss_metadata);
    bytes[3U] = static_cast<std::byte>(
        static_cast<std::uint8_t>(context.backup_command_count) |
        static_cast<std::uint8_t>(context.new_command_count << 4U));
    auto cursor = kHeaderBytes;
    for (const auto& delta : encoded_deltas) {
        const auto bit_length = static_cast<std::uint16_t>(delta.bit_length);
        bytes[cursor] = static_cast<std::byte>(bit_length & 0xffU);
        bytes[cursor + 1U] = static_cast<std::byte>(bit_length >> 8U);
        cursor += kDeltaLengthBytes;
        std::ranges::copy(delta.bytes, bytes.begin() +
            static_cast<std::ptrdiff_t>(cursor));
        cursor += delta.bytes.size();
    }
    const auto checksum_body = std::span<const std::byte>{bytes}.subspan(2U);
    const auto checksum = GoldSrcMoveChecksum{
        GoldSrcMoveChecksumProfile::synthetic_crc8_v1,
        limits_.maximum_encoded_bytes}
                              .compute(
                                  GoldSrcMoveChecksumContext{
                                      context.outgoing_netchan_sequence,
                                      checksum_body.size() * 8U},
                                  checksum_body);
    if (!checksum || !checksum.checksum) {
        return encode_failure(
            GoldSrcClientMoveErrorCode::checksum_failed,
            "Synthetic client-move checksum could not be computed",
            1U,
            std::nullopt,
            std::nullopt,
            checksum.error ? std::optional{checksum.error->code} : std::nullopt);
    }
    bytes[1U] = static_cast<std::byte>(*checksum.checksum);

    std::vector<GoldSrcUserCmdState> owned_commands;
    owned_commands.reserve(commands.size());
    for (const auto& command : commands) {
        owned_commands.push_back(*command);
    }
    return {GoldSrcClientMoveMessage{
                *checksum.checksum,
                context.synthetic_loss_metadata,
                context.backup_command_count,
                context.new_command_count,
                std::move(owned_commands),
                std::move(bytes),
                changed_fields,
                profile_},
            std::nullopt};
}

GoldSrcClientMoveDecodeResult GoldSrcClientMoveMessageCodec::decode(
    const std::span<const std::byte> payload,
    const GoldSrcUserCmdSchemaBinding& binding,
    const GoldSrcClientMoveDecodeContext& context) const
{
    if (!valid_configuration_) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::invalid_configuration,
            "Client-move codec configuration is invalid");
    }
    if (profile_ !=
        GoldSrcClientMoveCompatibilityProfile::synthetic_client_move_v1) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::stock_evidence_pending,
            "Stock client-move opcode, envelope, and checksum remain pending");
    }
    if (!valid_end_policy(context.end_policy)) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::invalid_context,
            "Client-move end policy is invalid");
    }
    const auto packet_byte_limit = effective_packet_byte_limit(limits_);
    if (packet_byte_limit < kHeaderBytes) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::packet_size_exceeded,
            "Client-move header exceeds the configured byte/bit budget");
    }
    if (!context.first_command_sequence.valid()) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::invalid_context,
            "Client-move decoder requires an explicit project-local first identity");
    }
    if (payload.size() < kHeaderBytes) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::truncated_header,
            "Synthetic client-move header is truncated",
            payload.size());
    }
    if (std::to_integer<std::uint8_t>(payload[0U]) !=
        kSyntheticClientMoveOpcode) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::wrong_opcode,
            "Synthetic client-move opcode does not match",
            0U);
    }
    const auto count_byte = std::to_integer<std::uint8_t>(payload[3U]);
    const auto backup_count = static_cast<std::size_t>(count_byte & 0x0fU);
    const auto new_count = static_cast<std::size_t>(count_byte >> 4U);
    const auto total_count = backup_count + new_count;
    if (new_count == 0U || backup_count > limits_.maximum_backup_commands ||
        new_count > limits_.maximum_new_commands ||
        total_count > limits_.maximum_commands_per_packet) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::count_limit_exceeded,
            "Decoded client-move counts exceed their configured bounds",
            3U);
    }
    if (context.first_command_sequence.value() >
            limits_.maximum_command_sequence ||
        total_count - 1U > limits_.maximum_command_sequence -
            context.first_command_sequence.value()) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::sequence_exhausted,
            "Decoded command identities exceed the project-local sequence domain",
            3U);
    }
    const auto final_sample_offset = static_cast<std::int64_t>(
        (total_count - 1U) * 1'000'000U);
    if (context.first_sample_time_nanoseconds >
        std::numeric_limits<std::int64_t>::max() - final_sample_offset) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::invalid_context,
            "Decoded command sample-time metadata would overflow",
            3U);
    }

    auto default_base = default_base_for(context.first_command_sequence, limits_);
    if (!default_base) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::invalid_context,
            "Synthetic default usercmd base could not be created");
    }
    GoldSrcUserCmdDeltaCodec delta_codec{
        limits_,
        GoldSrcUserCmdDeltaCompatibilityProfile::synthetic_usercmd_delta_v1};
    std::vector<GoldSrcUserCmdState> commands;
    commands.reserve(total_count);
    std::size_t cursor = kHeaderBytes;
    std::size_t changed_fields = 0U;
    for (std::size_t index = 0U; index < total_count; ++index) {
        if (cursor > packet_byte_limit - kDeltaLengthBytes) {
            return decode_failure(
                GoldSrcClientMoveErrorCode::packet_size_exceeded,
                "Client-move delta prefix exceeds the configured packet budget",
                cursor,
                index);
        }
        if (payload.size() - cursor < kDeltaLengthBytes) {
            return decode_failure(
                GoldSrcClientMoveErrorCode::truncated_delta_length,
                "Client-move delta bit-length prefix is truncated",
                cursor,
                index);
        }
        const auto delta_bit_length = read_u16(payload, cursor);
        cursor += kDeltaLengthBytes;
        const auto delta_byte_length =
            (static_cast<std::size_t>(delta_bit_length) + 7U) / 8U;
        if (delta_bit_length == 0U || delta_byte_length > payload.size() - cursor) {
            return decode_failure(
                GoldSrcClientMoveErrorCode::truncated_delta,
                "Client-move command delta is truncated or empty",
                cursor,
                index);
        }
        if (delta_byte_length > packet_byte_limit - cursor) {
            return decode_failure(
                GoldSrcClientMoveErrorCode::packet_size_exceeded,
                "Client-move delta exceeds the configured packet budget",
                cursor,
                index);
        }
        const auto sequence_value =
            context.first_command_sequence.value() +
            static_cast<std::uint32_t>(index);
        const auto sequence = GoldSrcUserCmdSequence::create(
            sequence_value, limits_.maximum_command_sequence);
        if (!sequence) {
            return decode_failure(
                GoldSrcClientMoveErrorCode::sequence_exhausted,
                "Decoded project-local command identity is exhausted",
                cursor,
                index);
        }
        const auto& base = index == 0U ? *default_base : commands.back();
        const auto decoded = delta_codec.decode_delta(
            base,
            binding,
            GoldSrcUserCmdDeltaDecodeContext{
                payload.subspan(cursor, delta_byte_length),
                0U,
                delta_bit_length,
                GoldSrcUserCmdDeltaEndPolicy::require_exact_end,
                GoldSrcUserCmdDeltaBasePolicy::explicit_command,
                *sequence,
                0U,
                context.first_sample_time_nanoseconds +
                    static_cast<std::int64_t>(index) * 1'000'000,
                std::nullopt,
            });
        if (!decoded || !decoded.state) {
            return decode_failure(
                GoldSrcClientMoveErrorCode::delta_decode_failed,
                "Client-move command delta could not be decoded",
                cursor,
                index,
                decoded.error ? std::optional{decoded.error->code}
                              : std::nullopt);
        }
        changed_fields += decoded.changed_field_count;
        commands.push_back(std::move(*decoded.state));
        cursor += delta_byte_length;
    }
    if (context.end_policy == GoldSrcClientMoveEndPolicy::require_exact_end &&
        cursor != payload.size()) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::unexpected_trailing_bytes,
            "Client-move profile requires the message to end with the payload",
            cursor);
    }

    const auto checksum_body = payload.subspan(2U, cursor - 2U);
    const auto checksum = GoldSrcMoveChecksum{
        GoldSrcMoveChecksumProfile::synthetic_crc8_v1,
        limits_.maximum_encoded_bytes}
                              .compute(
                                  GoldSrcMoveChecksumContext{
                                      context.outgoing_netchan_sequence,
                                      checksum_body.size() * 8U},
                                  checksum_body);
    if (!checksum || !checksum.checksum) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::checksum_failed,
            "Synthetic client-move checksum could not be recomputed",
            1U,
            std::nullopt,
            std::nullopt,
            checksum.error ? std::optional{checksum.error->code} : std::nullopt);
    }
    const auto encoded_checksum = std::to_integer<std::uint8_t>(payload[1U]);
    if (*checksum.checksum != encoded_checksum) {
        return decode_failure(
            GoldSrcClientMoveErrorCode::checksum_mismatch,
            "Synthetic client-move checksum does not match its body and sequence",
            1U);
    }

    std::vector<std::byte> owned_bytes(payload.begin(), payload.begin() +
        static_cast<std::ptrdiff_t>(cursor));
    return {GoldSrcClientMoveMessage{
                encoded_checksum,
                std::to_integer<std::uint8_t>(payload[2U]),
                backup_count,
                new_count,
                std::move(commands),
                std::move(owned_bytes),
                changed_fields,
                profile_},
            std::nullopt,
            cursor,
            cursor};
}

} // namespace hlclient::goldsrc
