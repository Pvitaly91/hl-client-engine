#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::goldsrc {

inline constexpr std::uint8_t kResourceTransitionControlOpcode = 45U;
inline constexpr std::size_t kResourceTransitionControlBodySize = 8U;
inline constexpr std::size_t kResourceTransitionControlMessageSize =
    1U + kResourceTransitionControlBodySize;
inline constexpr std::uint8_t kPostResourceTransitionControlBoundaryOpcode =
    43U;

// These are project safety bounds, not claims about stock engine maxima.
inline constexpr std::size_t kDefaultMaximumTransitionControlSize =
    kResourceTransitionControlMessageSize;
inline constexpr std::size_t kMaximumTransitionControlSize =
    kResourceTransitionControlMessageSize;
inline constexpr std::uint64_t kDefaultMaximumOpaqueCounterValue = 1'000'000U;
inline constexpr std::uint64_t kMaximumOpaqueCounterValue =
    static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)());
inline constexpr std::size_t kResourceTransitionControlDiagnosticTextLimit =
    256U;

struct ResourceTransitionControlLimits {
    std::size_t maximum_transition_control_size{
        kDefaultMaximumTransitionControlSize};
    // Project-only defensive bound for an evidence-incomplete opaque u32.
    std::uint64_t maximum_opaque_counter_value{
        kDefaultMaximumOpaqueCounterValue};
};

[[nodiscard]] bool valid_resource_transition_control_limits(
    const ResourceTransitionControlLimits& limits) noexcept;

enum class ResourceTransitionControlCompatibilityProfile {
    stock_protocol_48_build_10210,
};

enum class ResourceTransitionControlEvidenceProfile {
    repeated_signed_stock_capture_opaque_semantics,
};

// The two wire fields remain private: capture confirms width, byte order,
// variability, and the second field's zero invariant, but not a public
// semantic name for the first field.
class ResourceTransitionControlState final {
public:
    ResourceTransitionControlState(
        const ResourceTransitionControlState&) = default;
    ResourceTransitionControlState& operator=(
        const ResourceTransitionControlState&) = delete;
    ResourceTransitionControlState(
        ResourceTransitionControlState&&) noexcept = default;
    ResourceTransitionControlState& operator=(
        ResourceTransitionControlState&&) noexcept = delete;
    ~ResourceTransitionControlState() = default;

    [[nodiscard]] constexpr std::uint8_t opcode() const noexcept
    {
        return kResourceTransitionControlOpcode;
    }
    [[nodiscard]] std::size_t source_message_offset() const noexcept;
    [[nodiscard]] constexpr std::size_t body_bytes() const noexcept
    {
        return kResourceTransitionControlBodySize;
    }
    [[nodiscard]] constexpr std::size_t message_bytes() const noexcept
    {
        return kResourceTransitionControlMessageSize;
    }
    [[nodiscard]] ResourceTransitionControlCompatibilityProfile
        compatibility_profile() const noexcept;
    [[nodiscard]] ResourceTransitionControlEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class ResourceTransitionControlParser;

    ResourceTransitionControlState(
        std::uint32_t opaque_counter,
        std::uint32_t reserved_zero,
        std::size_t source_message_offset,
        ResourceTransitionControlCompatibilityProfile profile) noexcept;

    std::uint32_t opaque_counter_{0U};
    std::uint32_t reserved_zero_{0U};
    std::size_t source_message_offset_{0U};
    ResourceTransitionControlCompatibilityProfile profile_{
        ResourceTransitionControlCompatibilityProfile::
            stock_protocol_48_build_10210};
};

// Neutral boundary: capture confirms numeric opcode and exact cursor, while
// the public-header semantic gate for a resource-list name remains open.
class Opcode43Boundary final {
public:
    [[nodiscard]] constexpr std::uint8_t opcode() const noexcept
    {
        return kPostResourceTransitionControlBoundaryOpcode;
    }
    [[nodiscard]] std::size_t byte_offset() const noexcept;
    [[nodiscard]] std::size_t remaining_byte_count() const noexcept;
    [[nodiscard]] std::size_t source_payload_size() const noexcept;
    [[nodiscard]] ResourceTransitionControlCompatibilityProfile
        compatibility_profile() const noexcept;
    [[nodiscard]] ResourceTransitionControlEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class ResourceTransitionControlParser;

    Opcode43Boundary(
        std::size_t byte_offset,
        std::size_t remaining_byte_count,
        std::size_t source_payload_size,
        ResourceTransitionControlCompatibilityProfile profile) noexcept;

    std::size_t byte_offset_{0U};
    std::size_t remaining_byte_count_{0U};
    std::size_t source_payload_size_{0U};
    ResourceTransitionControlCompatibilityProfile profile_{
        ResourceTransitionControlCompatibilityProfile::
            stock_protocol_48_build_10210};
};

enum class ResourceTransitionControlErrorCode {
    invalid_configuration,
    invalid_input_geometry,
    wrong_opcode,
    truncated_body,
    opaque_counter_limit_exceeded,
    invalid_reserved_value,
    missing_next_boundary,
    wrong_next_boundary_opcode,
    truncated_next_boundary_body,
    size_overflow,
};

struct ResourceTransitionControlError {
    ResourceTransitionControlErrorCode code{
        ResourceTransitionControlErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::string context;
};

struct ResourceTransitionControlParseResult {
    std::optional<ResourceTransitionControlState> state;
    std::optional<Opcode43Boundary> boundary;
    std::optional<ResourceTransitionControlError> error;
    // Includes opcode 45 and its exact eight-byte body. Opcode 43 and every
    // following byte are unconsumed. Both values are zero on failure.
    std::size_t bytes_consumed{0U};
    std::size_t next_byte_offset{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value() && boundary.has_value();
    }
};

class ResourceTransitionControlParser final {
public:
    explicit ResourceTransitionControlParser(
        ResourceTransitionControlLimits limits = {},
        ResourceTransitionControlCompatibilityProfile profile =
            ResourceTransitionControlCompatibilityProfile::
                stock_protocol_48_build_10210) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const ResourceTransitionControlLimits& limits() const noexcept;
    [[nodiscard]] ResourceTransitionControlParseResult parse(
        std::span<const std::byte> second_service_payload,
        std::size_t opcode_byte_offset = 0U) const;

private:
    ResourceTransitionControlLimits limits_;
    ResourceTransitionControlCompatibilityProfile profile_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const ResourceTransitionControlErrorCode code) noexcept
{
    switch (code) {
    case ResourceTransitionControlErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ResourceTransitionControlErrorCode::invalid_input_geometry:
        return "invalid_input_geometry";
    case ResourceTransitionControlErrorCode::wrong_opcode:
        return "wrong_opcode";
    case ResourceTransitionControlErrorCode::truncated_body:
        return "truncated_body";
    case ResourceTransitionControlErrorCode::opaque_counter_limit_exceeded:
        return "opaque_counter_limit_exceeded";
    case ResourceTransitionControlErrorCode::invalid_reserved_value:
        return "invalid_reserved_value";
    case ResourceTransitionControlErrorCode::missing_next_boundary:
        return "missing_next_boundary";
    case ResourceTransitionControlErrorCode::wrong_next_boundary_opcode:
        return "wrong_next_boundary_opcode";
    case ResourceTransitionControlErrorCode::truncated_next_boundary_body:
        return "truncated_next_boundary_body";
    case ResourceTransitionControlErrorCode::size_overflow:
        return "size_overflow";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
