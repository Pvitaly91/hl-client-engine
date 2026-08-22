#pragma once

#include <hlclient/goldsrc/client_message.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hlclient::goldsrc {

inline constexpr std::size_t kResourceTransitionRequestCommandLength = 7U;
inline constexpr std::size_t kResourceTransitionRequestSize =
    1U + kResourceTransitionRequestCommandLength + 1U;

// These are project safety bounds, not claims about a stock engine buffer.
// The confirmed request is fixed-size, so both the default and hard cap are
// its exact semantic message size.
inline constexpr std::size_t kDefaultMaximumResourceTransitionRequestSize =
    kResourceTransitionRequestSize;
inline constexpr std::size_t kMaximumResourceTransitionRequestSize =
    kResourceTransitionRequestSize;
inline constexpr std::size_t kResourceTransitionRequestDiagnosticTextLimit =
    256U;

struct ResourceTransitionRequestLimits {
    std::size_t maximum_resource_transition_request_size{
        kDefaultMaximumResourceTransitionRequestSize};
};

[[nodiscard]] bool valid_resource_transition_request_limits(
    const ResourceTransitionRequestLimits& limits) noexcept;

enum class ResourceTransitionRequestCompatibilityProfile {
    stock_protocol_48_build_10210,
};

enum class ResourceTransitionRequestEvidenceProfile {
    repeated_signed_stock_capture,
};

// Immutable owning wire message. It intentionally exposes neither a command
// string nor an arbitrary-command construction surface.
class ResourceTransitionRequest final {
public:
    ResourceTransitionRequest(const ResourceTransitionRequest&) = default;
    ResourceTransitionRequest& operator=(const ResourceTransitionRequest&) =
        delete;
    ResourceTransitionRequest(ResourceTransitionRequest&&) noexcept = default;
    ResourceTransitionRequest& operator=(ResourceTransitionRequest&&) noexcept =
        delete;
    ~ResourceTransitionRequest() = default;

    [[nodiscard]] constexpr ClientMessageOpcode opcode() const noexcept
    {
        return ClientMessageOpcode::string_command;
    }

    [[nodiscard]] std::span<
        const std::byte,
        kResourceTransitionRequestSize> bytes() const noexcept;
    [[nodiscard]] std::size_t source_message_offset() const noexcept;
    [[nodiscard]] constexpr std::size_t message_bytes() const noexcept
    {
        return kResourceTransitionRequestSize;
    }
    [[nodiscard]] ResourceTransitionRequestCompatibilityProfile
        compatibility_profile() const noexcept;
    [[nodiscard]] ResourceTransitionRequestEvidenceProfile evidence_profile()
        const noexcept;

private:
    friend class ResourceTransitionRequestBuilder;
    friend class ResourceTransitionRequestParser;

    ResourceTransitionRequest(
        std::array<std::byte, kResourceTransitionRequestSize> bytes,
        std::size_t source_message_offset,
        ResourceTransitionRequestCompatibilityProfile profile) noexcept;

    std::array<std::byte, kResourceTransitionRequestSize> bytes_{};
    std::size_t source_message_offset_{0U};
    ResourceTransitionRequestCompatibilityProfile profile_{
        ResourceTransitionRequestCompatibilityProfile::
            stock_protocol_48_build_10210};
};

enum class ResourceTransitionRequestErrorCode {
    invalid_configuration,
    invalid_input_geometry,
    wrong_opcode,
    truncated_request,
    unexpected_terminator,
    unsupported_command_variant,
    missing_terminator,
    size_overflow,
};

struct ResourceTransitionRequestError {
    ResourceTransitionRequestErrorCode code{
        ResourceTransitionRequestErrorCode::invalid_configuration};
    std::size_t byte_offset{0U};
    std::string context;
};

struct ResourceTransitionRequestBuildResult {
    std::optional<ResourceTransitionRequest> request;
    std::optional<ResourceTransitionRequestError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return request.has_value();
    }
};

struct ResourceTransitionRequestParseResult {
    std::optional<ResourceTransitionRequest> request;
    std::optional<ResourceTransitionRequestError> error;
    // Includes the opcode. Both values are zero on failure. Bytes after this
    // exact message belong to the surrounding reliable message stream.
    std::size_t bytes_consumed{0U};
    std::size_t next_byte_offset{0U};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return request.has_value();
    }
};

class ResourceTransitionRequestBuilder final {
public:
    explicit ResourceTransitionRequestBuilder(
        ResourceTransitionRequestLimits limits = {},
        ResourceTransitionRequestCompatibilityProfile profile =
            ResourceTransitionRequestCompatibilityProfile::
                stock_protocol_48_build_10210) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const ResourceTransitionRequestLimits& limits() const noexcept;
    // This intentionally has no command or raw-string parameter.
    [[nodiscard]] ResourceTransitionRequestBuildResult build() const;

private:
    ResourceTransitionRequestLimits limits_;
    ResourceTransitionRequestCompatibilityProfile profile_;
};

class ResourceTransitionRequestParser final {
public:
    explicit ResourceTransitionRequestParser(
        ResourceTransitionRequestLimits limits = {},
        ResourceTransitionRequestCompatibilityProfile profile =
            ResourceTransitionRequestCompatibilityProfile::
                stock_protocol_48_build_10210) noexcept;

    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] const ResourceTransitionRequestLimits& limits() const noexcept;
    [[nodiscard]] ResourceTransitionRequestParseResult parse(
        std::span<const std::byte> reliable_message_stream,
        std::size_t opcode_byte_offset = 0U) const;

private:
    ResourceTransitionRequestLimits limits_;
    ResourceTransitionRequestCompatibilityProfile profile_;
};

[[nodiscard]] constexpr std::string_view to_string(
    const ResourceTransitionRequestErrorCode code) noexcept
{
    switch (code) {
    case ResourceTransitionRequestErrorCode::invalid_configuration:
        return "invalid_configuration";
    case ResourceTransitionRequestErrorCode::invalid_input_geometry:
        return "invalid_input_geometry";
    case ResourceTransitionRequestErrorCode::wrong_opcode:
        return "wrong_opcode";
    case ResourceTransitionRequestErrorCode::truncated_request:
        return "truncated_request";
    case ResourceTransitionRequestErrorCode::unexpected_terminator:
        return "unexpected_terminator";
    case ResourceTransitionRequestErrorCode::unsupported_command_variant:
        return "unsupported_command_variant";
    case ResourceTransitionRequestErrorCode::missing_terminator:
        return "missing_terminator";
    case ResourceTransitionRequestErrorCode::size_overflow:
        return "size_overflow";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
