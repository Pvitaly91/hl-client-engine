#pragma once

#include <array>
#include <cstdint>
#include <hlclient/goldsrc/client_move_message.hpp>
#include <hlclient/goldsrc/stock_runtime_transport_replay.hpp>
#include <optional>
#include <vector>

namespace hlclient::goldsrc {

// Valve common/in_buttons.h, pinned b1b5cf5892918535619b2937bb927e46cb097ba1.
inline constexpr std::uint16_t kReferenceGoldSrcButtonJump = 1U << 1U;
inline constexpr std::uint16_t kReferenceGoldSrcButtonDuck = 1U << 2U;

// Quantized network values, deliberately not a simulation/history command.
// No fabricated local identity, sample clock, SDK pointer or weaponselect.
struct GoldSrcWireUserCmd final {
    std::uint16_t lerp_msec{};
    std::uint8_t msec{};
    std::array<std::uint16_t, 3> angle_turns{}; // pitch/yaw/roll, 65536/revolution
    std::int16_t forward{}, side{}, up{};       // whole movement units
    std::uint16_t buttons{};
    std::uint8_t light_level{}, impulse{}, impact_index{};
    std::array<std::int16_t, 3> impact_eighths{};
    friend bool operator==(const GoldSrcWireUserCmd &,
                           const GoldSrcWireUserCmd &) = default;
};

[[nodiscard]] bool valid_wire_usercmd(const GoldSrcWireUserCmd &) noexcept;
[[nodiscard]] std::optional<std::uint16_t> quantize_wire_angle(double degrees) noexcept;
[[nodiscard]] std::optional<std::int16_t> quantize_wire_movement(double units) noexcept;

struct ReferenceMoveSource final {
    std::uint64_t generation{1};
    std::size_t payload_ordinal{}, delivery_ordinal{}, observed_ordinal{};
    std::uint32_t sequence{};
    bool reliable{}, fragmented{}, reassembled{};
    std::size_t fragment_count{};
    StockRuntimeReplayedPayloadKind payload_kind{
        StockRuntimeReplayedPayloadKind::ordinary};
    friend bool operator==(const ReferenceMoveSource &,
                           const ReferenceMoveSource &) = default;
};

struct ReferenceMoveLimits final {
    std::size_t maximum_commands{62};
    std::size_t maximum_message_bytes{2048};
    std::size_t maximum_messages_per_payload{1024};
    std::size_t maximum_retained_commands{4096};
};

enum class ReferenceMoveErrorCode {
    duplicate_move,
    invalid_configuration,
    invalid_context,
    invalid_command,
    truncated,
    wrong_opcode,
    invalid_length,
    count_limit,
    byte_limit,
    delta_failed,
    nonzero_padding,
    checksum_mismatch,
    unsupported_boundary,
    allocation_failed
};
[[nodiscard]] std::string_view to_string(ReferenceMoveErrorCode) noexcept;

struct ReferenceMoveError final {
    ReferenceMoveErrorCode code{};
    ReferenceMoveSource source;
    std::size_t bit_offset{};
    std::optional<std::uint8_t> opcode;
};

struct ReferenceUserCmdRecord final {
    GoldSrcWireUserCmd value;
    std::size_t start_bit{}, meaningful_end_bit{}, end_bit{};
    bool backup{};
    std::uint16_t field_mask{};
    std::uint8_t mask_bytes{};
    std::size_t redundant_fields{};
};

struct ReferenceClientMoveMessage final {
    ReferenceMoveSource source;
    std::size_t start_byte{}, end_byte{};
    std::uint8_t transform_length{}, checksum{}, loss_metadata{}, backup_count{},
        new_count{};
    bool checksum_matched{};
    std::vector<ReferenceUserCmdRecord> commands;
    std::vector<std::byte> bytes;
};

struct ReferenceMoveResult final {
    std::optional<ReferenceClientMoveMessage> message;
    std::optional<ReferenceMoveError> error;
    explicit operator bool() const noexcept { return message.has_value(); }
};

// In-place only on caller-owned memory, never corpus files. Trailing bytes stay
// untouched. Sequence is the full masked transport sequence, not its ACK.
[[nodiscard]] bool transform_reference_move_body(std::span<std::byte> body,
                                                 std::size_t prefix_length,
                                                 std::uint32_t sequence,
                                                 bool decode) noexcept;

class GoldSrcReferenceClientMoveCodec final {
  public:
    GoldSrcReferenceClientMoveCodec(GoldSrcUserCmdSchemaBinding binding,
                                    std::uint64_t generation,
                                    ReferenceMoveLimits limits = {});
    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] bool accepts_source(const ReferenceMoveSource &) const noexcept;
    [[nodiscard]] ReferenceMoveResult decode(std::span<const std::byte> payload,
                                             std::size_t start_byte,
                                             const ReferenceMoveSource &source) const;
    [[nodiscard]] ReferenceMoveResult
    encode(std::span<const GoldSrcWireUserCmd> commands, std::size_t backups,
           std::uint8_t loss_metadata, const ReferenceMoveSource &source) const;
    [[nodiscard]] static constexpr auto profile() noexcept {
        return GoldSrcClientMoveCompatibilityProfile::public_goldsrc48_client_move_v1;
    }

  private:
    GoldSrcUserCmdSchemaBinding binding_;
    std::uint64_t generation_;
    ReferenceMoveLimits limits_;
};

// A narrow client-message walker over an already decoded transport payload.
// Known controls stay inert; unknown framing stops without byte scanning.
struct ReferenceClientPayloadResult final {
    std::vector<ReferenceClientMoveMessage> moves;
    std::array<std::size_t, 12> opcode_counts{};
    std::size_t end_byte{};
    std::optional<ReferenceMoveError> error;
};
[[nodiscard]] ReferenceClientPayloadResult decode_reference_client_payload(
    const GoldSrcReferenceClientMoveCodec &codec, std::span<const std::byte> payload,
    const ReferenceMoveSource &source, ReferenceMoveLimits limits = {});

} // namespace hlclient::goldsrc
