#include <hlclient/goldsrc/move_vars.hpp>

#include <hlclient/goldsrc/byte_reader.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

inline constexpr std::size_t kPrefixNumericCount = 16U;
inline constexpr std::size_t kTailNumericCount = 8U;
inline constexpr std::size_t kFixedUserMessageNameBytes = 16U;

[[nodiscard]] MoveVarsParseResult parse_failure(
    const MoveVarsErrorCode code,
    const std::size_t byte_offset,
    const std::optional<std::size_t> numeric_field_index,
    std::string context)
{
    return MoveVarsParseResult{
        std::nullopt,
        MoveVarsError{
            code,
            byte_offset,
            numeric_field_index,
            std::move(context),
        },
        0U,
        0U,
    };
}

[[nodiscard]] MoveVarsStreamDecodeResult stream_failure(
    const MoveVarsStreamErrorCode code,
    const std::size_t byte_offset,
    const std::optional<std::uint8_t> wire_opcode,
    const std::optional<MoveVarsErrorCode> parser_code,
    std::string context)
{
    return MoveVarsStreamDecodeResult{
        std::nullopt,
        MoveVarsStreamError{
            code,
            byte_offset,
            wire_opcode,
            parser_code,
            std::move(context),
        },
        0U,
    };
}

[[nodiscard]] bool checked_add(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept
{
    if (right > (std::numeric_limits<std::size_t>::max)() - left) {
        return false;
    }
    output = left + right;
    return true;
}

[[nodiscard]] bool valid_numeric_value(const float value) noexcept
{
    return std::isfinite(value) &&
           std::fabs(value) <= kMaximumMoveVarsNumericMagnitude;
}

[[nodiscard]] bool valid_ascii_string(const std::string_view value) noexcept
{
    return std::all_of(
        value.begin(),
        value.end(),
        [](const char character) noexcept {
            const auto value = static_cast<unsigned char>(character);
            return value >= 0x20U && value <= 0x7eU;
        });
}

[[nodiscard]] bool supported_profile(
    const MoveVarsCompatibilityProfile profile) noexcept
{
    return profile ==
           MoveVarsCompatibilityProfile::stock_protocol_48_build_10210;
}

} // namespace

bool valid_move_vars_limits(const MoveVarsLimits& limits) noexcept
{
    return limits.maximum_sky_name_length > 0U &&
           limits.maximum_sky_name_length <= kMaximumMoveVarsStringLength &&
           limits.maximum_control_string_length > 0U &&
           limits.maximum_control_string_length <=
               kMaximumPostMoveVarsStringLength &&
           limits.maximum_post_movevars_controls > 0U &&
           limits.maximum_post_movevars_controls <=
               kMaximumPostMoveVarsControls;
}

MoveVarsState::MoveVarsState(
    std::array<float, kMoveVarsNumericFieldCount> numeric_fields,
    const bool footsteps,
    std::string sky_name,
    const std::size_t source_message_offset,
    const std::size_t body_bytes,
    const MoveVarsCompatibilityProfile compatibility_profile) noexcept
    : numeric_fields_{numeric_fields},
      footsteps_{footsteps},
      sky_name_{std::move(sky_name)},
      source_message_offset_{source_message_offset},
      body_bytes_{body_bytes},
      compatibility_profile_{compatibility_profile}
{
}

float MoveVarsState::gravity() const noexcept { return numeric_fields_[0U]; }
float MoveVarsState::stop_speed() const noexcept { return numeric_fields_[1U]; }
float MoveVarsState::maximum_speed() const noexcept { return numeric_fields_[2U]; }
float MoveVarsState::spectator_maximum_speed() const noexcept { return numeric_fields_[3U]; }
float MoveVarsState::acceleration() const noexcept { return numeric_fields_[4U]; }
float MoveVarsState::air_acceleration() const noexcept { return numeric_fields_[5U]; }
float MoveVarsState::water_acceleration() const noexcept { return numeric_fields_[6U]; }
float MoveVarsState::friction() const noexcept { return numeric_fields_[7U]; }
float MoveVarsState::edge_friction() const noexcept { return numeric_fields_[8U]; }
float MoveVarsState::water_friction() const noexcept { return numeric_fields_[9U]; }
float MoveVarsState::entity_gravity() const noexcept { return numeric_fields_[10U]; }
float MoveVarsState::bounce() const noexcept { return numeric_fields_[11U]; }
float MoveVarsState::step_size() const noexcept { return numeric_fields_[12U]; }
float MoveVarsState::maximum_velocity() const noexcept { return numeric_fields_[13U]; }
float MoveVarsState::z_maximum() const noexcept { return numeric_fields_[14U]; }
float MoveVarsState::wave_height() const noexcept { return numeric_fields_[15U]; }
bool MoveVarsState::footsteps() const noexcept { return footsteps_; }
float MoveVarsState::roll_angle() const noexcept { return numeric_fields_[16U]; }
float MoveVarsState::roll_speed() const noexcept { return numeric_fields_[17U]; }
float MoveVarsState::sky_color_red() const noexcept { return numeric_fields_[18U]; }
float MoveVarsState::sky_color_green() const noexcept { return numeric_fields_[19U]; }
float MoveVarsState::sky_color_blue() const noexcept { return numeric_fields_[20U]; }
float MoveVarsState::sky_vector_x() const noexcept { return numeric_fields_[21U]; }
float MoveVarsState::sky_vector_y() const noexcept { return numeric_fields_[22U]; }
float MoveVarsState::sky_vector_z() const noexcept { return numeric_fields_[23U]; }
const std::string& MoveVarsState::sky_name() const noexcept { return sky_name_; }
std::size_t MoveVarsState::source_message_offset() const noexcept { return source_message_offset_; }
std::size_t MoveVarsState::body_bytes() const noexcept { return body_bytes_; }
std::size_t MoveVarsState::message_bytes() const noexcept { return body_bytes_ + 1U; }
MoveVarsCompatibilityProfile MoveVarsState::compatibility_profile() const noexcept { return compatibility_profile_; }
MoveVarsEvidenceProfile MoveVarsState::evidence_profile() const noexcept { return MoveVarsEvidenceProfile::stock_capture_and_public_valve_header; }

MoveVarsParser::MoveVarsParser(
    MoveVarsLimits limits,
    const MoveVarsCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool MoveVarsParser::valid_configuration() const noexcept
{
    return valid_move_vars_limits(limits_) && supported_profile(profile_);
}

const MoveVarsLimits& MoveVarsParser::limits() const noexcept
{
    return limits_;
}

MoveVarsParseResult MoveVarsParser::parse(
    const std::span<const std::byte> service_payload,
    const std::size_t opcode_byte_offset) const
{
    if (!valid_configuration()) {
        return parse_failure(
            MoveVarsErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            "Move-vars limits or compatibility profile are unsupported");
    }
    if (opcode_byte_offset >= service_payload.size()) {
        return parse_failure(
            MoveVarsErrorCode::invalid_input_geometry,
            opcode_byte_offset,
            std::nullopt,
            "Move-vars opcode offset is outside the service payload");
    }
    if (std::to_integer<std::uint8_t>(service_payload[opcode_byte_offset]) !=
        kMoveVarsOpcode) {
        return parse_failure(
            MoveVarsErrorCode::wrong_opcode,
            opcode_byte_offset,
            std::nullopt,
            "Move-vars parser requires the exact opcode-44 cursor");
    }

    const auto body_offset = opcode_byte_offset + 1U;
    ByteReader reader{service_payload.subspan(body_offset)};
    std::array<float, kMoveVarsNumericFieldCount> numeric_fields{};

    const auto read_numeric = [&](const std::size_t index)
        -> std::optional<MoveVarsParseResult> {
        const auto field_offset = body_offset + reader.position();
        const auto value = reader.read_float32_le();
        if (!value) {
            return parse_failure(
                MoveVarsErrorCode::truncated_numeric_field,
                field_offset,
                index,
                "Move-vars float32 field is truncated");
        }
        if (!std::isfinite(*value)) {
            return parse_failure(
                MoveVarsErrorCode::non_finite_numeric_field,
                field_offset,
                index,
                "Move-vars float32 field must be finite");
        }
        if (!valid_numeric_value(*value)) {
            return parse_failure(
                MoveVarsErrorCode::numeric_magnitude_exceeded,
                field_offset,
                index,
                "Move-vars float32 field exceeds the project safety magnitude");
        }
        numeric_fields[index] = *value;
        return std::nullopt;
    };

    for (std::size_t index = 0U; index < kPrefixNumericCount; ++index) {
        if (const auto error = read_numeric(index)) {
            return *error;
        }
    }

    const auto footsteps_offset = body_offset + reader.position();
    const auto footsteps = reader.read_uint8();
    if (!footsteps) {
        return parse_failure(
            MoveVarsErrorCode::truncated_footsteps,
            footsteps_offset,
            std::nullopt,
            "Move-vars footsteps byte is truncated");
    }
    if (*footsteps > 1U) {
        return parse_failure(
            MoveVarsErrorCode::invalid_footsteps,
            footsteps_offset,
            std::nullopt,
            "Move-vars footsteps byte must equal zero or one");
    }

    for (std::size_t index = 0U; index < kTailNumericCount; ++index) {
        if (const auto error = read_numeric(kPrefixNumericCount + index)) {
            return *error;
        }
    }

    const auto string_offset = body_offset + reader.position();
    const auto available = reader.remaining();
    const auto scan_count = std::min(
        available,
        limits_.maximum_sky_name_length + 1U);
    std::optional<std::size_t> string_length;
    const auto body = service_payload.subspan(body_offset);
    for (std::size_t index = 0U; index < scan_count; ++index) {
        if (body[reader.position() + index] == std::byte{0U}) {
            string_length = index;
            break;
        }
    }
    if (!string_length) {
        return parse_failure(
            available > limits_.maximum_sky_name_length
                ? MoveVarsErrorCode::sky_name_too_long
                : MoveVarsErrorCode::unterminated_sky_name,
            available > limits_.maximum_sky_name_length
                ? string_offset + limits_.maximum_sky_name_length
                : service_payload.size(),
            std::nullopt,
            available > limits_.maximum_sky_name_length
                ? "Move-vars sky name exceeds the configured project bound"
                : "Move-vars sky name has no NUL terminator within the payload");
    }
    const auto wire_string = reader.read_bytes(*string_length + 1U);
    if (!wire_string) {
        return parse_failure(
            MoveVarsErrorCode::size_overflow,
            string_offset,
            std::nullopt,
            "Move-vars sky-name cursor arithmetic overflowed");
    }
    const auto* characters =
        reinterpret_cast<const char*>(wire_string->data());
    std::string sky_name{characters, *string_length};
    std::size_t bytes_consumed = 0U;
    if (!checked_add(1U, reader.position(), bytes_consumed)) {
        return parse_failure(
            MoveVarsErrorCode::size_overflow,
            opcode_byte_offset,
            std::nullopt,
            "Move-vars message size overflowed");
    }
    std::size_t next_byte_offset = 0U;
    if (!checked_add(opcode_byte_offset, bytes_consumed, next_byte_offset)) {
        return parse_failure(
            MoveVarsErrorCode::size_overflow,
            opcode_byte_offset,
            std::nullopt,
            "Move-vars next cursor overflowed");
    }

    return MoveVarsParseResult{
        MoveVarsState{
            numeric_fields,
            *footsteps != 0U,
            std::move(sky_name),
            opcode_byte_offset,
            reader.position(),
            profile_,
        },
        std::nullopt,
        bytes_consumed,
        next_byte_offset,
    };
}

PostMoveVarsControl::PostMoveVarsControl(
    const std::uint8_t opcode,
    const PostMoveVarsControlKind kind,
    const std::size_t byte_offset,
    const std::size_t byte_count,
    PostMoveVarsControlBody body) noexcept
    : opcode_{opcode},
      kind_{kind},
      byte_offset_{byte_offset},
      byte_count_{byte_count},
      body_{std::move(body)}
{
}

std::uint8_t PostMoveVarsControl::opcode() const noexcept { return opcode_; }
PostMoveVarsControlKind PostMoveVarsControl::kind() const noexcept { return kind_; }
std::size_t PostMoveVarsControl::byte_offset() const noexcept { return byte_offset_; }
std::size_t PostMoveVarsControl::byte_count() const noexcept { return byte_count_; }
const PostMoveVarsControlBody& PostMoveVarsControl::body() const noexcept { return body_; }

PostMoveVarsBoundary::PostMoveVarsBoundary(
    const std::uint8_t opcode,
    const std::size_t byte_offset,
    const std::size_t remaining_byte_count) noexcept
    : opcode_{opcode},
      byte_offset_{byte_offset},
      remaining_byte_count_{remaining_byte_count}
{
}

std::uint8_t PostMoveVarsBoundary::opcode() const noexcept { return opcode_; }
std::size_t PostMoveVarsBoundary::byte_offset() const noexcept { return byte_offset_; }
std::size_t PostMoveVarsBoundary::remaining_byte_count() const noexcept { return remaining_byte_count_; }
PostMoveVarsBoundaryCategory PostMoveVarsBoundary::category() const noexcept { return PostMoveVarsBoundaryCategory::stock_observed_opcode_13; }
PostMoveVarsBoundaryEvidenceStatus PostMoveVarsBoundary::evidence_status() const noexcept { return PostMoveVarsBoundaryEvidenceStatus::stock_confirmed_opcode_13_body_unconsumed; }

MoveVarsStreamState::MoveVarsStreamState(
    MoveVarsState move_vars,
    std::vector<PostMoveVarsControl> controls,
    PostMoveVarsBoundary boundary,
    const std::size_t bytes_consumed) noexcept
    : move_vars_{std::move(move_vars)},
      controls_{std::move(controls)},
      boundary_{std::move(boundary)},
      bytes_consumed_{bytes_consumed}
{
}

const MoveVarsState& MoveVarsStreamState::move_vars() const noexcept { return move_vars_; }
const std::vector<PostMoveVarsControl>& MoveVarsStreamState::controls() const noexcept { return controls_; }
const PostMoveVarsBoundary& MoveVarsStreamState::boundary() const noexcept { return boundary_; }
std::size_t MoveVarsStreamState::control_count() const noexcept { return controls_.size(); }
std::size_t MoveVarsStreamState::bytes_consumed() const noexcept { return bytes_consumed_; }

MoveVarsStreamDecoder::MoveVarsStreamDecoder(
    MoveVarsLimits limits,
    const MoveVarsCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool MoveVarsStreamDecoder::valid_configuration() const noexcept
{
    return valid_move_vars_limits(limits_) && supported_profile(profile_);
}

MoveVarsStreamDecodeResult MoveVarsStreamDecoder::decode(
    const std::span<const std::byte> service_payload,
    const PostDeltaBoundary& initial_boundary) const
{
    if (!valid_configuration()) {
        return stream_failure(
            MoveVarsStreamErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            std::nullopt,
            "Move-vars stream limits or compatibility profile are unsupported");
    }
    if (initial_boundary.opcode() != kMoveVarsOpcode) {
        return stream_failure(
            MoveVarsStreamErrorCode::wrong_initial_opcode,
            initial_boundary.byte_offset(),
            initial_boundary.opcode(),
            std::nullopt,
            "Post-delta continuation does not identify opcode 44");
    }
    if (initial_boundary.bit_offset() != 0U ||
        initial_boundary.byte_offset() >= service_payload.size()) {
        return stream_failure(
            MoveVarsStreamErrorCode::invalid_boundary_geometry,
            initial_boundary.byte_offset(),
            initial_boundary.opcode(),
            std::nullopt,
            "Post-delta move-vars cursor is outside the owning payload");
    }
    const auto expected_remaining =
        service_payload.size() - initial_boundary.byte_offset() - 1U;
    if (initial_boundary.remaining_byte_count() != expected_remaining) {
        return stream_failure(
            MoveVarsStreamErrorCode::invalid_boundary_geometry,
            initial_boundary.byte_offset(),
            initial_boundary.opcode(),
            std::nullopt,
            "Post-delta remaining-byte count does not match the owning payload");
    }

    const MoveVarsParser parser{limits_, profile_};
    auto parsed = parser.parse(service_payload, initial_boundary.byte_offset());
    if (!parsed || !parsed.state) {
        return stream_failure(
            MoveVarsStreamErrorCode::move_vars_parse_failed,
            parsed.error ? parsed.error->byte_offset
                         : initial_boundary.byte_offset(),
            kMoveVarsOpcode,
            parsed.error ? std::optional{parsed.error->code} : std::nullopt,
            parsed.error ? parsed.error->context
                         : "Move-vars parser returned no candidate or diagnostic");
    }

    auto cursor = parsed.next_byte_offset;
    std::vector<PostMoveVarsControl> controls;
    controls.reserve(std::min<std::size_t>(
        limits_.maximum_post_movevars_controls,
        64U));

    while (true) {
        if (cursor >= service_payload.size()) {
            return stream_failure(
                MoveVarsStreamErrorCode::missing_post_movevars_boundary,
                cursor,
                std::nullopt,
                std::nullopt,
                "Move-vars stream ends without the stock-observed opcode-13 boundary");
        }
        const auto opcode =
            std::to_integer<std::uint8_t>(service_payload[cursor]);
        if (opcode == kStockPostMoveVarsBoundaryOpcode) {
            if (service_payload.size() - cursor < 2U) {
                return stream_failure(
                    MoveVarsStreamErrorCode::malformed_post_movevars_boundary,
                    cursor,
                    opcode,
                    std::nullopt,
                    "Post-movevars opcode-13 boundary must retain an unconsumed body byte");
            }
            if (controls.size() >
                (std::numeric_limits<std::size_t>::max)() - 2U) {
                return stream_failure(
                    MoveVarsStreamErrorCode::size_overflow,
                    cursor,
                    opcode,
                    std::nullopt,
                    "Move-vars event count overflowed");
            }
            const auto bytes_consumed =
                cursor - initial_boundary.byte_offset();
            const auto required_event_count = controls.size() + 2U;
            return MoveVarsStreamDecodeResult{
                MoveVarsStreamState{
                    std::move(*parsed.state),
                    std::move(controls),
                    PostMoveVarsBoundary{
                        opcode,
                        cursor,
                        service_payload.size() - cursor - 1U,
                    },
                    bytes_consumed,
                },
                std::nullopt,
                required_event_count,
            };
        }
        if (opcode == kMoveVarsOpcode) {
            return stream_failure(
                MoveVarsStreamErrorCode::duplicate_move_vars,
                cursor,
                opcode,
                std::nullopt,
                "Move-vars stream contains a duplicate opcode-44 message");
        }
        if (controls.size() >= limits_.maximum_post_movevars_controls) {
            return stream_failure(
                MoveVarsStreamErrorCode::control_limit_exceeded,
                cursor,
                opcode,
                std::nullopt,
                "Post-movevars control count exceeds the configured bound");
        }

        const auto control_offset = cursor;
        if (opcode == kPostMoveVarsOpcode32) {
            if (service_payload.size() - cursor < 3U) {
                return stream_failure(
                    MoveVarsStreamErrorCode::truncated_control,
                    cursor,
                    opcode,
                    std::nullopt,
                    "Post-movevars opcode-32 fixed body is truncated");
            }
            controls.emplace_back(PostMoveVarsControl{
                opcode,
                PostMoveVarsControlKind::opcode_32_two_byte,
                control_offset,
                3U,
                PostMoveVarsOpcode32Control{
                    std::to_integer<std::uint8_t>(service_payload[cursor + 1U]),
                    std::to_integer<std::uint8_t>(service_payload[cursor + 2U]),
                },
            });
            cursor += 3U;
            continue;
        }
        if (opcode == kPostMoveVarsOpcode5) {
            if (service_payload.size() - cursor < 3U) {
                return stream_failure(
                    MoveVarsStreamErrorCode::truncated_control,
                    cursor,
                    opcode,
                    std::nullopt,
                    "Post-movevars opcode-5 uint16 body is truncated");
            }
            const auto value = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(
                    service_payload[cursor + 1U])) |
                static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(
                        service_payload[cursor + 2U]))
                    << 8U));
            controls.emplace_back(PostMoveVarsControl{
                opcode,
                PostMoveVarsControlKind::opcode_5_uint16_le,
                control_offset,
                3U,
                PostMoveVarsOpcode5Control{value},
            });
            cursor += 3U;
            continue;
        }
        if (opcode == kPostMoveVarsOpcode39) {
            constexpr std::size_t message_bytes =
                1U + 2U + kFixedUserMessageNameBytes;
            if (service_payload.size() - cursor < message_bytes) {
                return stream_failure(
                    MoveVarsStreamErrorCode::truncated_control,
                    cursor,
                    opcode,
                    std::nullopt,
                    "Post-movevars opcode-39 fixed body is truncated");
            }
            const auto name_bytes = service_payload.subspan(
                cursor + 3U,
                kFixedUserMessageNameBytes);
            const auto terminator = std::find(
                name_bytes.begin(),
                name_bytes.end(),
                std::byte{0U});
            if (terminator == name_bytes.end()) {
                return stream_failure(
                    MoveVarsStreamErrorCode::invalid_control_value,
                    cursor + 3U,
                    opcode,
                    std::nullopt,
                    "Post-movevars opcode-39 fixed name is not NUL terminated");
            }
            const auto name_length = static_cast<std::size_t>(
                std::distance(name_bytes.begin(), terminator));
            if (name_length == 0U ||
                !std::all_of(
                    terminator,
                    name_bytes.end(),
                    [](const std::byte value) noexcept {
                        return value == std::byte{0U};
                    })) {
                return stream_failure(
                    MoveVarsStreamErrorCode::invalid_control_value,
                    cursor + 3U + name_length,
                    opcode,
                    std::nullopt,
                    "Post-movevars opcode-39 name or zero padding is invalid");
            }
            const auto* characters =
                reinterpret_cast<const char*>(name_bytes.data());
            std::string name{characters, name_length};
            if (!valid_ascii_string(name)) {
                return stream_failure(
                    MoveVarsStreamErrorCode::invalid_control_value,
                    cursor + 3U,
                    opcode,
                    std::nullopt,
                    "Post-movevars opcode-39 name is outside the captured ASCII profile");
            }
            controls.emplace_back(PostMoveVarsControl{
                opcode,
                PostMoveVarsControlKind::opcode_39_user_message_definition,
                control_offset,
                message_bytes,
                PostMoveVarsUserMessageDefinition{
                    std::to_integer<std::uint8_t>(service_payload[cursor + 1U]),
                    std::bit_cast<std::int8_t>(
                        std::to_integer<std::uint8_t>(
                            service_payload[cursor + 2U])),
                    std::move(name),
                },
            });
            cursor += message_bytes;
            continue;
        }
        if (opcode == kPostMoveVarsOpcode9) {
            const auto string_offset = cursor + 1U;
            const auto available = service_payload.size() - string_offset;
            const auto scan_count = std::min(
                available,
                limits_.maximum_control_string_length + 1U);
            std::optional<std::size_t> length;
            for (std::size_t index = 0U; index < scan_count; ++index) {
                if (service_payload[string_offset + index] == std::byte{0U}) {
                    length = index;
                    break;
                }
            }
            if (!length) {
                return stream_failure(
                    available > limits_.maximum_control_string_length
                        ? MoveVarsStreamErrorCode::control_string_too_long
                        : MoveVarsStreamErrorCode::unterminated_control_string,
                    available > limits_.maximum_control_string_length
                        ? string_offset + limits_.maximum_control_string_length
                        : service_payload.size(),
                    opcode,
                    std::nullopt,
                    available > limits_.maximum_control_string_length
                        ? "Post-movevars opcode-9 string exceeds the configured bound"
                        : "Post-movevars opcode-9 string is unterminated");
            }
            std::size_t message_bytes = 0U;
            if (!checked_add(2U, *length, message_bytes)) {
                return stream_failure(
                    MoveVarsStreamErrorCode::size_overflow,
                    cursor,
                    opcode,
                    std::nullopt,
                    "Post-movevars opcode-9 message size overflowed");
            }
            const auto* characters = reinterpret_cast<const char*>(
                service_payload.data() + string_offset);
            controls.emplace_back(PostMoveVarsControl{
                opcode,
                PostMoveVarsControlKind::opcode_9_nul_string,
                control_offset,
                message_bytes,
                PostMoveVarsStringControl{
                    std::string{characters, *length},
                },
            });
            cursor += message_bytes;
            continue;
        }

        return stream_failure(
            MoveVarsStreamErrorCode::unsupported_post_movevars_opcode,
            cursor,
            opcode,
            std::nullopt,
            "Move-vars stream reached an unsupported exact service opcode");
    }
}

} // namespace hlclient::goldsrc
