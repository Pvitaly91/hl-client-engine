#pragma once

#include <hlclient/goldsrc/entity_baseline_decoder.hpp>
#include <hlclient/goldsrc/runtime_replay_session.hpp>
#include <hlclient/goldsrc/stock_captured_signon_replay.hpp>
#include <hlclient/goldsrc/stock_runtime_capture_corpus.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hlclient::goldsrc {

struct RuntimeReplayCaptureSummary final {
    std::string run_id;
    std::string corpus_structural_sha256;
    std::size_t delivered_datagram_count{0U};
    std::size_t replayed_payload_count{0U};
    std::size_t reassembled_payload_count{0U};
    std::size_t decompressed_payload_count{0U};
    std::size_t decoded_server_signon_payload_count{0U};
    std::size_t schema_count{0U};
    std::size_t baseline_entity_count{0U};
    std::size_t baseline_instanced_count{0U};
    std::size_t runtime_record_count{0U};
    StockPostResourceResponseCursor signon_cursor{};
    StockRuntimeSourceCursor baseline_end_cursor{};
    std::size_t baseline_payload_ordinal{0U};
    std::uint32_t baseline_source_sequence{0U};
};

class RuntimeReplayCaptureState final {
public:
    RuntimeReplayCaptureState(const RuntimeReplayCaptureState&) = default;
    RuntimeReplayCaptureState& operator=(const RuntimeReplayCaptureState&) = delete;
    RuntimeReplayCaptureState(RuntimeReplayCaptureState&&) noexcept = default;
    RuntimeReplayCaptureState& operator=(RuntimeReplayCaptureState&&) noexcept =
        delete;
    ~RuntimeReplayCaptureState() = default;

    [[nodiscard]] const RuntimeReplayInitialization& initialization()
        const noexcept;
    [[nodiscard]] const std::vector<RuntimeReplayRecord>& records()
        const noexcept;
    [[nodiscard]] std::size_t baseline_consumed_bits() const noexcept;
    [[nodiscard]] const RuntimeReplayCaptureSummary& summary() const noexcept;
    [[nodiscard]] const ServerInfoState& server_info() const noexcept { return *server_info_; }
    [[nodiscard]] const ResourceListState& resources() const noexcept { return *resources_; }
    [[nodiscard]] const std::vector<double>& presentation_offsets_seconds() const noexcept { return presentation_offsets_; }

private:
    friend class RuntimeReplayCaptureLoader;

    RuntimeReplayCaptureState(
        RuntimeReplayInitialization initialization,
        std::vector<RuntimeReplayRecord> records,
        std::size_t baseline_consumed_bits,
        RuntimeReplayCaptureSummary summary,
        std::shared_ptr<const ServerInfoState> server_info,
        std::shared_ptr<const ResourceListState> resources,
        std::vector<double> presentation_offsets) noexcept;

    RuntimeReplayInitialization initialization_;
    std::vector<RuntimeReplayRecord> records_;
    std::size_t baseline_consumed_bits_{0U};
    RuntimeReplayCaptureSummary summary_;
    std::shared_ptr<const ServerInfoState> server_info_;
    std::shared_ptr<const ResourceListState> resources_;
    std::vector<double> presentation_offsets_;
};

enum class RuntimeReplayCaptureErrorCode : std::uint8_t {
    corpus_load_failed,
    transport_replay_failed,
    signon_replay_failed,
    initialization_context_missing,
    unsupported_initialization_message,
    baseline_decode_failed,
    baseline_missing,
    runtime_payload_missing,
    allocation_failed,
};

struct RuntimeReplayCaptureError final {
    RuntimeReplayCaptureErrorCode code{
        RuntimeReplayCaptureErrorCode::corpus_load_failed};
    std::optional<StockRuntimeCaptureCorpusErrorCode> corpus_error;
    std::optional<StockRuntimeTransportReplayErrorCode> transport_error;
    std::optional<StockCapturedSignonReplayErrorCode> signon_error;
    std::optional<EntityBaselineDecodeErrorCode> baseline_error;
    std::optional<RuntimeControlDecodeErrorCode> control_error;
    std::optional<std::uint8_t> unsupported_opcode;
    std::size_t replay_payload_ordinal{0U};
    StockRuntimeSourceCursor cursor{};
    std::string context;
};

struct RuntimeReplayCaptureLoadResult final {
    std::optional<RuntimeReplayCaptureState> state;
    std::optional<RuntimeReplayCaptureError> error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state.has_value() && !error.has_value();
    }
};

class RuntimeReplayCaptureLoader final {
public:
    [[nodiscard]] RuntimeReplayCaptureLoadResult load(
        const std::filesystem::path& exact_functional_run_directory) const;
};

[[nodiscard]] constexpr std::string_view to_string(
    const RuntimeReplayCaptureErrorCode code) noexcept
{
    switch (code) {
    case RuntimeReplayCaptureErrorCode::corpus_load_failed:
        return "corpus_load_failed";
    case RuntimeReplayCaptureErrorCode::transport_replay_failed:
        return "transport_replay_failed";
    case RuntimeReplayCaptureErrorCode::signon_replay_failed:
        return "signon_replay_failed";
    case RuntimeReplayCaptureErrorCode::initialization_context_missing:
        return "initialization_context_missing";
    case RuntimeReplayCaptureErrorCode::unsupported_initialization_message:
        return "unsupported_initialization_message";
    case RuntimeReplayCaptureErrorCode::baseline_decode_failed:
        return "baseline_decode_failed";
    case RuntimeReplayCaptureErrorCode::baseline_missing:
        return "baseline_missing";
    case RuntimeReplayCaptureErrorCode::runtime_payload_missing:
        return "runtime_payload_missing";
    case RuntimeReplayCaptureErrorCode::allocation_failed:
        return "allocation_failed";
    }
    return "unknown";
}

} // namespace hlclient::goldsrc
