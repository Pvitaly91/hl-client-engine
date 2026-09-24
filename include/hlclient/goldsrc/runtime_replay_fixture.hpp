#pragma once

#include <hlclient/goldsrc/runtime_replay_session.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

// Project-owned deterministic fixture catalog. The contained service payloads
// use the public GoldSrc 48 layouts exercised by A/B/C/D; they are not stock
// captures and this catalog is not a DEM, PCAP, or generic file format.
enum class RuntimeReplayFixtureKind : std::uint8_t {
    basic_mixed,
    missing_entity_base,
    reference_checker,
    visual_entities,
};

struct RuntimeReplayFixtureRequest final {
    RuntimeReplayFixtureKind kind{RuntimeReplayFixtureKind::basic_mixed};
    std::uint64_t generation{1U};
    std::uint32_t source_sequence_base{100U};
    std::uint64_t record_identity_base{1'000U};
};

struct RuntimeReplayFixture final {
    RuntimeReplayFixtureKind kind{RuntimeReplayFixtureKind::basic_mixed};
    RuntimeReplayInitialization initialization;
    std::vector<RuntimeReplayRecord> records;
    // Application presentation metadata only. Empty retains the original
    // immediate bounded-drain behavior; otherwise each entry gates the
    // matching record by elapsed replay seconds without changing wire time.
    std::vector<double> presentation_offsets_seconds;
    std::size_t baseline_consumed_bits{0U};
};

enum class RuntimeReplayFixtureErrorCode : std::uint8_t {
    invalid_request,
    schema_decode_failed,
    baseline_decode_failed,
    unable_to_retain_fixture,
};

struct RuntimeReplayFixtureError final {
    RuntimeReplayFixtureErrorCode code{
        RuntimeReplayFixtureErrorCode::invalid_request};
    std::string context;
};

struct RuntimeReplayFixtureResult final {
    std::optional<RuntimeReplayFixture> fixture;
    std::optional<RuntimeReplayFixtureError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return fixture.has_value() && !error.has_value();
    }
};

[[nodiscard]] RuntimeReplayFixtureResult make_runtime_replay_fixture(
    const RuntimeReplayFixtureRequest& request);
[[nodiscard]] std::string_view to_string(RuntimeReplayFixtureKind kind) noexcept;
[[nodiscard]] std::string_view to_string(
    RuntimeReplayFixtureErrorCode code) noexcept;

} // namespace hlclient::goldsrc
