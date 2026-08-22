#pragma once

#include <hlclient/goldsrc/delta_description.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hlclient::goldsrc {

inline constexpr std::uint8_t kMoveVarsOpcode = 44U;
inline constexpr std::uint8_t kStockPostMoveVarsBoundaryOpcode = 13U;
inline constexpr std::uint8_t kPostMoveVarsOpcode32 = 32U;
inline constexpr std::uint8_t kPostMoveVarsOpcode5 = 5U;
inline constexpr std::uint8_t kPostMoveVarsOpcode39 = 39U;
inline constexpr std::uint8_t kPostMoveVarsOpcode9 = 9U;

inline constexpr std::size_t kMoveVarsNumericFieldCount = 24U;
inline constexpr std::size_t kDefaultMaximumMoveVarsStringLength = 64U;
inline constexpr std::size_t kMaximumMoveVarsStringLength = 256U;
inline constexpr std::size_t kDefaultMaximumPostMoveVarsStringLength = 1'024U;
inline constexpr std::size_t kMaximumPostMoveVarsStringLength = 4'096U;
inline constexpr std::size_t kDefaultMaximumPostMoveVarsControls = 64U;
inline constexpr std::size_t kMaximumPostMoveVarsControls = 256U;
inline constexpr float kMaximumMoveVarsNumericMagnitude = 1'000'000.0F;
inline constexpr std::size_t kMoveVarsDiagnosticTextLimit = 256U;

struct MoveVarsLimits {
    std::size_t maximum_sky_name_length{
        kDefaultMaximumMoveVarsStringLength};
    std::size_t maximum_control_string_length{
        kDefaultMaximumPostMoveVarsStringLength};
    std::size_t maximum_post_movevars_controls{
        kDefaultMaximumPostMoveVarsControls};
};

[[nodiscard]] bool valid_move_vars_limits(
    const MoveVarsLimits& limits) noexcept;

enum class MoveVarsCompatibilityProfile {
    stock_protocol_48_build_10210,
};

enum class MoveVarsEvidenceProfile {
    stock_capture_and_public_valve_header,
};

// Owning immutable sign-on metadata. These values are deliberately not a
// movement runtime, renderer environment, filesystem path, or resource model.
class MoveVarsState final {
public:
    MoveVarsState(const MoveVarsState&) = default;
    MoveVarsState& operator=(const MoveVarsState&) = delete;
    MoveVarsState(MoveVarsState&&) noexcept = default;
    MoveVarsState& operator=(MoveVarsState&&) noexcept = delete;
    ~MoveVarsState() = default;

    [[nodiscard]] float gravity() const noexcept;
    [[nodiscard]] float stop_speed() const noexcept;
    [[nodiscard]] float maximum_speed() const noexcept;
    [[nodiscard]] float spectator_maximum_speed() const noexcept;
    [[nodiscard]] float acceleration() const noexcept;
    [[nodiscard]] float air_acceleration() const noexcept;
    [[nodiscard]] float water_acceleration() const noexcept;
    [[nodiscard]] float friction() const noexcept;
    [[nodiscard]] float edge_friction() const noexcept;
    [[nodiscard]] float water_friction() const noexcept;
    [[nodiscard]] float entity_gravity() const noexcept;
    [[nodiscard]] float bounce() const noexcept;
    [[nodiscard]] float step_size() const noexcept;
    [[nodiscard]] float maximum_velocity() const noexcept;
    [[nodiscard]] float z_maximum() const noexcept;
    [[nodiscard]] float wave_height() const noexcept;
    [[nodiscard]] bool footsteps() const noexcept;
    [[nodiscard]] float roll_angle() const noexcept;
    [[nodiscard]] float roll_speed() const noexcept;
    [[nodiscard]] float sky_color_red() const noexcept;
    [[nodiscard]] float sky_color_green() const noexcept;
    [[nodiscard]] float sky_color_blue() const noexcept;
    [[nodiscard]] float sky_vector_x() const noexcept;
    [[nodiscard]] float sky_vector_y() const noexcept;
    [[nodiscard]] float sky_vector_z() const noexcept;
    [[nodiscard]] const std::string& sky_name() const noexcept;

    [[nodiscard]] std::size_t source_message_offset() const noexcept;
    [[nodiscard]] std::size_t body_bytes() const noexcept;
    [[nodiscard]] std::size_t message_bytes() const noexcept;
    [[nodiscard]] MoveVarsCompatibilityProfile compatibility_profile() const noexcept;
    [[nodiscard]] MoveVarsEvidenceProfile evidence_profile() const noexcept;

private:
    friend class MoveVarsParser;

    MoveVarsState(
        std::array<float, kMoveVarsNumericFieldCount> numeric_fields,
        bool footsteps,
        std::string sky_name,
        std::size_t source_message_offset,
        std::size_t body_bytes,
        MoveVarsCompatibilityProfile compatibility_profile) noexcept;

    std::array<float, kMoveVarsNumericFieldCount> numeric_fields_{};
    bool footsteps_{false};
    std::string sky_name_;
    std::size_t source_message_offset_{0U};
    std::size_t body_bytes_{0U};
    MoveVarsCompatibilityProfile compatibility_profile_{
        MoveVarsCompatibilityProfile::stock_protocol_48_build_10210};
};

enum class MoveVarsErrorCode {
    invalid_configuration,
    invalid_input_geometry,
    wrong_opcode,
    truncated_numeric_field,
    non_finite_numeric_field,
    numeric_magnitude_exceeded,
    truncated_footsteps,
    invalid_footsteps,
    unterminated_sky_name,
    sky_name_too_long,
    size_overflow,
};

struct MoveVarsError {
    MoveVarsErrorCode code{MoveVarsErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::size_t> numeric_field_index;
    std::string context;
};

struct MoveVarsParseResult {
    std::optional<MoveVarsState> state;
    std::optional<MoveVarsError> error;
    // Includes opcode 44. Both counts are zero on failure.
    std::size_t bytes_consumed{0U};
    std::size_t next_byte_offset{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class MoveVarsParser final {
public:
    explicit MoveVarsParser(
        MoveVarsLimits limits = {},
        MoveVarsCompatibilityProfile profile =
            MoveVarsCompatibilityProfile::stock_protocol_48_build_10210) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const MoveVarsLimits& limits() const noexcept;
    [[nodiscard]] MoveVarsParseResult parse(
        std::span<const std::byte> service_payload,
        std::size_t opcode_byte_offset) const;

private:
    MoveVarsLimits limits_;
    MoveVarsCompatibilityProfile profile_;
};

enum class PostMoveVarsControlKind {
    opcode_32_two_byte,
    opcode_5_uint16_le,
    opcode_39_user_message_definition,
    opcode_9_nul_string,
};

struct PostMoveVarsOpcode32Control {
    std::uint8_t first_value{0U};
    std::uint8_t second_value{0U};
};

struct PostMoveVarsOpcode5Control {
    std::uint16_t value{0U};
};

struct PostMoveVarsUserMessageDefinition {
    std::uint8_t identifier{0U};
    std::int8_t declared_size{0};
    std::string name;
};

struct PostMoveVarsStringControl {
    std::string value;
};

using PostMoveVarsControlBody = std::variant<
    PostMoveVarsOpcode32Control,
    PostMoveVarsOpcode5Control,
    PostMoveVarsUserMessageDefinition,
    PostMoveVarsStringControl>;

class PostMoveVarsControl final {
public:
    PostMoveVarsControl(const PostMoveVarsControl&) = default;
    PostMoveVarsControl& operator=(const PostMoveVarsControl&) = delete;
    PostMoveVarsControl(PostMoveVarsControl&&) noexcept = default;
    PostMoveVarsControl& operator=(PostMoveVarsControl&&) noexcept = delete;
    ~PostMoveVarsControl() = default;

    [[nodiscard]] std::uint8_t opcode() const noexcept;
    [[nodiscard]] PostMoveVarsControlKind kind() const noexcept;
    [[nodiscard]] std::size_t byte_offset() const noexcept;
    [[nodiscard]] std::size_t byte_count() const noexcept;
    [[nodiscard]] const PostMoveVarsControlBody& body() const noexcept;

private:
    friend class MoveVarsStreamDecoder;

    PostMoveVarsControl(
        std::uint8_t opcode,
        PostMoveVarsControlKind kind,
        std::size_t byte_offset,
        std::size_t byte_count,
        PostMoveVarsControlBody body) noexcept;

    std::uint8_t opcode_{0U};
    PostMoveVarsControlKind kind_{
        PostMoveVarsControlKind::opcode_32_two_byte};
    std::size_t byte_offset_{0U};
    std::size_t byte_count_{0U};
    PostMoveVarsControlBody body_{PostMoveVarsOpcode32Control{}};
};

enum class PostMoveVarsBoundaryCategory {
    stock_observed_opcode_13,
};

enum class PostMoveVarsBoundaryEvidenceStatus {
    stock_confirmed_opcode_13_body_unconsumed,
};

class PostMoveVarsBoundary final {
public:
    [[nodiscard]] std::uint8_t opcode() const noexcept;
    [[nodiscard]] std::size_t byte_offset() const noexcept;
    [[nodiscard]] std::size_t remaining_byte_count() const noexcept;
    [[nodiscard]] PostMoveVarsBoundaryCategory category() const noexcept;
    [[nodiscard]] PostMoveVarsBoundaryEvidenceStatus evidence_status() const noexcept;

private:
    friend class MoveVarsStreamDecoder;

    PostMoveVarsBoundary(
        std::uint8_t opcode,
        std::size_t byte_offset,
        std::size_t remaining_byte_count) noexcept;

    std::uint8_t opcode_{0U};
    std::size_t byte_offset_{0U};
    std::size_t remaining_byte_count_{0U};
};

class MoveVarsStreamState final {
public:
    MoveVarsStreamState(const MoveVarsStreamState&) = default;
    MoveVarsStreamState& operator=(const MoveVarsStreamState&) = delete;
    MoveVarsStreamState(MoveVarsStreamState&&) noexcept = default;
    MoveVarsStreamState& operator=(MoveVarsStreamState&&) noexcept = delete;
    ~MoveVarsStreamState() = default;

    [[nodiscard]] const MoveVarsState& move_vars() const noexcept;
    [[nodiscard]] const std::vector<PostMoveVarsControl>& controls() const noexcept;
    [[nodiscard]] const PostMoveVarsBoundary& boundary() const noexcept;
    [[nodiscard]] std::size_t control_count() const noexcept;
    // From opcode 44 through all confirmed controls. Boundary opcode 13 and
    // its body are both unconsumed.
    [[nodiscard]] std::size_t bytes_consumed() const noexcept;

private:
    friend class MoveVarsStreamDecoder;

    MoveVarsStreamState(
        MoveVarsState move_vars,
        std::vector<PostMoveVarsControl> controls,
        PostMoveVarsBoundary boundary,
        std::size_t bytes_consumed) noexcept;

    MoveVarsState move_vars_;
    std::vector<PostMoveVarsControl> controls_;
    PostMoveVarsBoundary boundary_;
    std::size_t bytes_consumed_{0U};
};

enum class MoveVarsStreamErrorCode {
    invalid_configuration,
    invalid_boundary_geometry,
    wrong_initial_opcode,
    move_vars_parse_failed,
    missing_post_movevars_boundary,
    malformed_post_movevars_boundary,
    truncated_control,
    unterminated_control_string,
    control_string_too_long,
    invalid_control_value,
    control_limit_exceeded,
    duplicate_move_vars,
    unsupported_post_movevars_opcode,
    size_overflow,
};

struct MoveVarsStreamError {
    MoveVarsStreamErrorCode code{
        MoveVarsStreamErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::optional<std::uint8_t> wire_opcode;
    std::optional<MoveVarsErrorCode> parser_code;
    std::string context;
};

struct MoveVarsStreamDecodeResult {
    std::optional<MoveVarsStreamState> state;
    std::optional<MoveVarsStreamError> error;
    // One move-vars-ready event, one per confirmed control, and one boundary.
    // Zero on failure.
    std::size_t required_event_count{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value();
    }
};

class MoveVarsStreamDecoder final {
public:
    explicit MoveVarsStreamDecoder(
        MoveVarsLimits limits = {},
        MoveVarsCompatibilityProfile profile =
            MoveVarsCompatibilityProfile::stock_protocol_48_build_10210) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] MoveVarsStreamDecodeResult decode(
        std::span<const std::byte> service_payload,
        const PostDeltaBoundary& initial_boundary) const;

private:
    MoveVarsLimits limits_;
    MoveVarsCompatibilityProfile profile_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const MoveVarsErrorCode code) noexcept
{
    switch (code) {
    case MoveVarsErrorCode::invalid_configuration: return "invalid_configuration";
    case MoveVarsErrorCode::invalid_input_geometry: return "invalid_input_geometry";
    case MoveVarsErrorCode::wrong_opcode: return "wrong_opcode";
    case MoveVarsErrorCode::truncated_numeric_field: return "truncated_numeric_field";
    case MoveVarsErrorCode::non_finite_numeric_field: return "non_finite_numeric_field";
    case MoveVarsErrorCode::numeric_magnitude_exceeded: return "numeric_magnitude_exceeded";
    case MoveVarsErrorCode::truncated_footsteps: return "truncated_footsteps";
    case MoveVarsErrorCode::invalid_footsteps: return "invalid_footsteps";
    case MoveVarsErrorCode::unterminated_sky_name: return "unterminated_sky_name";
    case MoveVarsErrorCode::sky_name_too_long: return "sky_name_too_long";
    case MoveVarsErrorCode::size_overflow: return "size_overflow";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(
    const MoveVarsStreamErrorCode code) noexcept
{
    switch (code) {
    case MoveVarsStreamErrorCode::invalid_configuration: return "invalid_configuration";
    case MoveVarsStreamErrorCode::invalid_boundary_geometry: return "invalid_boundary_geometry";
    case MoveVarsStreamErrorCode::wrong_initial_opcode: return "wrong_initial_opcode";
    case MoveVarsStreamErrorCode::move_vars_parse_failed: return "move_vars_parse_failed";
    case MoveVarsStreamErrorCode::missing_post_movevars_boundary: return "missing_post_movevars_boundary";
    case MoveVarsStreamErrorCode::malformed_post_movevars_boundary: return "malformed_post_movevars_boundary";
    case MoveVarsStreamErrorCode::truncated_control: return "truncated_control";
    case MoveVarsStreamErrorCode::unterminated_control_string: return "unterminated_control_string";
    case MoveVarsStreamErrorCode::control_string_too_long: return "control_string_too_long";
    case MoveVarsStreamErrorCode::invalid_control_value: return "invalid_control_value";
    case MoveVarsStreamErrorCode::control_limit_exceeded: return "control_limit_exceeded";
    case MoveVarsStreamErrorCode::duplicate_move_vars: return "duplicate_move_vars";
    case MoveVarsStreamErrorCode::unsupported_post_movevars_opcode: return "unsupported_post_movevars_opcode";
    case MoveVarsStreamErrorCode::size_overflow: return "size_overflow";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
