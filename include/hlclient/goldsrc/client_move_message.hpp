#pragma once

#include <hlclient/goldsrc/move_checksum.hpp>
#include <hlclient/goldsrc/usercmd_delta_codec.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::uint8_t kSyntheticClientMoveOpcode = 0xE1U;

enum class GoldSrcClientMoveCompatibilityProfile : std::uint8_t {
    synthetic_client_move_v1,
    stock_protocol_48_build_10210_evidence_pending,
};

enum class GoldSrcClientMoveEndPolicy : std::uint8_t {
    leave_trailing_bytes,
    require_exact_end,
};

struct GoldSrcClientMoveEncodeContext {
    std::uint32_t outgoing_netchan_sequence{0U};
    std::uint8_t synthetic_loss_metadata{0U};
    std::size_t backup_command_count{0U};
    std::size_t new_command_count{0U};
};

struct GoldSrcClientMoveDecodeContext {
    std::uint32_t outgoing_netchan_sequence{0U};
    GoldSrcUserCmdSequence first_command_sequence{};
    std::int64_t first_sample_time_nanoseconds{0};
    GoldSrcClientMoveEndPolicy end_policy{
        GoldSrcClientMoveEndPolicy::leave_trailing_bytes};
};

enum class GoldSrcClientMoveErrorCode : std::uint8_t {
    invalid_configuration,
    stock_evidence_pending,
    invalid_context,
    count_limit_exceeded,
    count_mismatch,
    command_order_invalid,
    packet_size_exceeded,
    delta_encode_failed,
    delta_decode_failed,
    checksum_failed,
    wrong_opcode,
    truncated_header,
    truncated_delta_length,
    truncated_delta,
    checksum_mismatch,
    unexpected_trailing_bytes,
    sequence_exhausted,
};

struct GoldSrcClientMoveError {
    GoldSrcClientMoveErrorCode code{
        GoldSrcClientMoveErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::size_t> command_index;
    std::optional<GoldSrcUserCmdDeltaErrorCode> delta_code;
    std::optional<GoldSrcMoveChecksumErrorCode> checksum_code;
    std::string_view context;
};

class GoldSrcClientMoveMessage final {
public:
    GoldSrcClientMoveMessage(const GoldSrcClientMoveMessage&) = default;
    GoldSrcClientMoveMessage(GoldSrcClientMoveMessage&&) noexcept = default;
    GoldSrcClientMoveMessage& operator=(const GoldSrcClientMoveMessage&) = delete;
    GoldSrcClientMoveMessage& operator=(GoldSrcClientMoveMessage&&) = delete;
    ~GoldSrcClientMoveMessage() = default;

    [[nodiscard]] std::uint8_t opcode() const noexcept;
    [[nodiscard]] std::uint8_t checksum() const noexcept;
    [[nodiscard]] std::uint8_t synthetic_loss_metadata() const noexcept;
    [[nodiscard]] std::size_t backup_command_count() const noexcept;
    [[nodiscard]] std::size_t new_command_count() const noexcept;
    [[nodiscard]] const std::vector<GoldSrcUserCmdState>& commands() const noexcept;
    [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept;
    [[nodiscard]] std::size_t bit_length() const noexcept;
    [[nodiscard]] std::size_t changed_field_count() const noexcept;
    [[nodiscard]] GoldSrcClientMoveCompatibilityProfile profile() const noexcept;

private:
    friend class GoldSrcClientMoveMessageCodec;

    GoldSrcClientMoveMessage(
        std::uint8_t checksum,
        std::uint8_t synthetic_loss_metadata,
        std::size_t backup_command_count,
        std::size_t new_command_count,
        std::vector<GoldSrcUserCmdState> commands,
        std::vector<std::byte> bytes,
        std::size_t changed_field_count,
        GoldSrcClientMoveCompatibilityProfile profile) noexcept;

    std::uint8_t checksum_{0U};
    std::uint8_t synthetic_loss_metadata_{0U};
    std::size_t backup_command_count_{0U};
    std::size_t new_command_count_{0U};
    std::vector<GoldSrcUserCmdState> commands_;
    std::vector<std::byte> bytes_;
    std::size_t changed_field_count_{0U};
    GoldSrcClientMoveCompatibilityProfile profile_{
        GoldSrcClientMoveCompatibilityProfile::synthetic_client_move_v1};
};

struct GoldSrcClientMoveEncodeResult {
    std::optional<GoldSrcClientMoveMessage> message;
    std::optional<GoldSrcClientMoveError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return message.has_value();
    }
};

struct GoldSrcClientMoveDecodeResult {
    std::optional<GoldSrcClientMoveMessage> message;
    std::optional<GoldSrcClientMoveError> error;
    std::size_t bytes_consumed{0U};
    std::size_t next_byte_offset{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return message.has_value();
    }
};

class GoldSrcClientMoveMessageCodec final {
public:
    explicit GoldSrcClientMoveMessageCodec(
        GoldSrcUserCmdLimits limits = {},
        GoldSrcClientMoveCompatibilityProfile profile =
            GoldSrcClientMoveCompatibilityProfile::
                stock_protocol_48_build_10210_evidence_pending) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] GoldSrcClientMoveCompatibilityProfile profile() const noexcept;
    [[nodiscard]] GoldSrcClientMoveEncodeResult encode(
        std::span<const std::shared_ptr<const GoldSrcUserCmdState>> commands,
        const GoldSrcUserCmdSchemaBinding& binding,
        const GoldSrcClientMoveEncodeContext& context) const;
    [[nodiscard]] GoldSrcClientMoveDecodeResult decode(
        std::span<const std::byte> payload,
        const GoldSrcUserCmdSchemaBinding& binding,
        const GoldSrcClientMoveDecodeContext& context) const;

private:
    GoldSrcUserCmdLimits limits_;
    GoldSrcClientMoveCompatibilityProfile profile_;
    bool valid_configuration_{false};
};

} // namespace hlclient::goldsrc
