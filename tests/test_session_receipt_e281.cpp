#include <hlclient/app/session_receipt.hpp>
#include <hlclient/app/session_receipt_commit.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <hlclient/platform/windows/session_receipt_file.hpp>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
namespace app = hlclient::app;

[[nodiscard]] app::SessionReceiptIdentity identity()
{
    return {"0123456789abcdef0123456789abcdef", app::SessionReceiptRole::A,
        41U, 987654321U};
}

[[nodiscard]] app::SessionCloseReceipt sent_close()
{
    app::SessionCloseReceipt close;
    close.reason = app::SessionCloseReason::window_close;
    close.outcome = app::SessionCloseOutcome::sent_but_unconfirmed;
    close.operation_identity = 1U;
    close.session_generation = 1U;
    close.requested = true;
    close.eligible = true;
    close.queued = true;
    close.send_attempt_count = 3U;
    close.successful_send_count = 1U;
    close.support_send_attempt_count = 2U;
    close.successful_support_send_count = 1U;
    close.deadline_reached = true;
    close.requested_process_ms = 100U;
    close.deadline_process_ms = 1'600U;
    close.first_send_process_ms = 101U;
    close.last_send_process_ms = 101U;
    close.transport_closed_process_ms = 1'600U;
    close.requested_host_qpc_ticks = 1'000'000U;
    close.transport_closed_host_qpc_ticks = 2'500'000U;
    return close;
}

TEST_CASE("E28.1 early-channel generation publishes immutable start before connection", "[e281][session-receipt][no-stock]") {
    app::SessionReceiptJournal journal{identity(),1'000'000U};
    std::vector<app::SessionReceiptStage> leaves;
    const auto publish=[&](const app::SessionReceiptSnapshot& snapshot) {
        const auto duplicate=std::ranges::find(leaves,snapshot.stage)!=leaves.end();
        CHECK_FALSE(duplicate);leaves.push_back(snapshot.stage);return !duplicate;
    };
    REQUIRE(app::begin_published_session_receipt_generation(journal,{1U,0U,"not_observed"},publish));
    REQUIRE(journal.observe_facts(app::kSessionReceiptConnectionFacts));
    REQUIRE(journal.record(app::SessionReceiptStage::connection_established,{10U,{},{},10'000U}));
    REQUIRE(publish(*journal.snapshot()));
    REQUIRE(journal.finish(sent_close()));REQUIRE(publish(*journal.snapshot()));
    CHECK(leaves==std::vector<app::SessionReceiptStage>{app::SessionReceiptStage::generation_started,
        app::SessionReceiptStage::connection_established,app::SessionReceiptStage::terminal});
    CHECK_FALSE(app::session_receipt_keyboard_ready(*journal.snapshot()));
    app::SessionReceiptJournal interrupted{identity()};
    CHECK_FALSE(app::begin_published_session_receipt_generation(interrupted,{1U,0U,"not_observed"},
        [](const auto&){return false;}));
    CHECK_FALSE(app::session_receipt_keyboard_ready(*interrupted.snapshot()));
}

[[nodiscard]] app::SessionReceiptJournal ready_journal(const bool terminal = false)
{
    app::SessionReceiptJournal journal{identity(), 1'000'000U};
    REQUIRE(journal.begin_generation({1U, 1U, "maps/crossfire.bsp"}));
    REQUIRE(journal.observe_receiving_entity(2U)); // actual slot, unrelated to role A
    REQUIRE(journal.observe_facts(app::kSessionReceiptKnownFacts,
        {144U, 96U, 12U, false}));
    for (std::uint32_t index = 1U; index <= 5U; ++index) {
        REQUIRE(journal.record(static_cast<app::SessionReceiptStage>(index),
            {index * 10U, index * 20U, index * 30U, index * 100'000U}));
    }
    if (terminal) REQUIRE(journal.finish(sent_close()));
    return journal;
}

[[nodiscard]] std::string changed(std::string text,
    const std::string_view from, const std::string_view replacement)
{
    const auto position = text.find(from);
    REQUIRE(position != std::string::npos);
    text.replace(position, from.size(), replacement);
    return text;
}

#ifdef _WIN32
namespace windows = hlclient::platform::windows;
class ReceiptTemporaryDirectory final {
public:
    ReceiptTemporaryDirectory()
    {
        std::wstring buffer(32'768U, L'\0');
        const auto size = ::GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (size == 0U || size >= buffer.size()) return;
        buffer.resize(size);
        std::error_code error;
        parent_ = std::filesystem::canonical(std::filesystem::path{buffer}, error);
        if (error || !parent_.is_absolute()) return;
        static std::atomic_uint32_t ordinal{};
        for (std::size_t attempt = 0U; attempt < 32U; ++attempt) {
            const auto candidate = parent_ / (L"hlclient-session-receipt-e281-" +
                std::to_wstring(::GetCurrentProcessId()) + L"-" +
                std::to_wstring(::GetTickCount64()) + L"-" +
                std::to_wstring(ordinal.fetch_add(1U)));
            if (::CreateDirectoryW(candidate.c_str(), nullptr)) {
                path_ = candidate;
                break;
            }
        }
    }
    ~ReceiptTemporaryDirectory()
    {
        if (path_.is_absolute() && path_.parent_path() == parent_ &&
            path_.filename().wstring().starts_with(L"hlclient-session-receipt-e281-")) {
            std::error_code error;
            static_cast<void>(std::filesystem::remove_all(path_, error));
        }
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path parent_, path_;
};

void publish_bytes(const windows::SecureOutputDirectory& directory,
    const std::wstring_view leaf, const std::string_view text)
{
    REQUIRE(windows::secure_atomic_write_new(directory, leaf,
        std::as_bytes(std::span{text.data(), text.size()})));
}
#endif
} // namespace

TEST_CASE("E28.1 durable readiness survives arbitrary bounded log eviction",
    "[e281][session-receipt][receipt][offline]")
{
    auto journal = ready_journal();
    const auto original = *journal.snapshot();
    std::string rolling_log{"spawn_tx=1 signon_ack=1 map entered\n"};
    constexpr std::size_t maximum_tail = 4'096U;
    for (std::size_t index = 0U; index < 100'000U; ++index) {
        rolling_log += "ordinary server text and runtime diagnostics\n";
        if (rolling_log.size() > maximum_tail)
            rolling_log.erase(0U, rolling_log.size() - maximum_tail);
    }
    CHECK(rolling_log.find("spawn_tx") == std::string::npos);
    CHECK(rolling_log.find("map entered") == std::string::npos);
    CHECK(*journal.snapshot() == original);
    CHECK(app::session_receipt_keyboard_ready(*journal.snapshot()));
    REQUIRE(journal.observe_facts(0U, {})); // coordinator cancel must not erase receipts
    REQUIRE(journal.finish(sent_close()));
    const auto encoded = app::encode_session_receipt(*journal.snapshot());
    REQUIRE(encoded);
    CHECK(encoded->size() <= app::kSessionReceiptMaximumBytes);
    const auto decoded = app::decode_session_receipt(*encoded);
    REQUIRE(decoded);
    CHECK(*decoded == *journal.snapshot());
    CHECK(app::session_receipt_keyboard_ready(*decoded));
    CHECK_FALSE(decoded->close.ack_observed);
    CHECK_FALSE(decoded->close.server_disconnect_observed);
    CHECK(decoded->close.outcome == app::SessionCloseOutcome::sent_but_unconfirmed);
}

TEST_CASE("E28.1 connection-only and rollback observations cannot create map readiness",
    "[e281][session-receipt][receipt][offline]")
{
    app::SessionReceiptJournal journal{identity()};
    REQUIRE(journal.begin_generation({1U, 1U, "maps/crossfire.bsp"}));
    REQUIRE(journal.observe_facts(app::kSessionReceiptConnectionFacts |
        app::receipt_fact(app::SessionReceiptFact::serverinfo_received)));
    REQUIRE(journal.record(app::SessionReceiptStage::connection_established, {1U, {}, {}, {}}));
    const auto committed = *journal.snapshot();
    CHECK_FALSE(journal.record(app::SessionReceiptStage::map_entry_committed,
        {2U, 5U, 5U, {}}));
    CHECK_FALSE(journal.record(app::SessionReceiptStage::signon_complete, {2U, {}, {}, {}}));
    CHECK(*journal.snapshot() == committed);
    CHECK_FALSE(app::session_receipt_keyboard_ready(*journal.snapshot()));
    // A rolled-back owner does not call record; text resembling an internal
    // marker is not accepted by this typed component's decoder.
    CHECK_FALSE(app::decode_session_receipt("server says: map entered\n"));
    app::SessionCloseReceipt close;
    close.reason = app::SessionCloseReason::startup_cancel;
    close.outcome = app::SessionCloseOutcome::cancel_without_protocol_disconnect;
    close.operation_identity = 1U;
    close.requested = true;
    close.requested_process_ms = 3U;
    REQUIRE(journal.finish(close));
    CHECK_FALSE(app::session_receipt_keyboard_ready(*journal.snapshot()));
    CHECK_FALSE(journal.observe_facts(app::kSessionReceiptKnownFacts));
    CHECK_FALSE(journal.finish(close));
}

TEST_CASE("E28.1 generation changes invalidate old readiness and remain bounded",
    "[e281][session-receipt][receipt][offline]")
{
    auto journal = ready_journal();
    CHECK_FALSE(journal.begin_generation({1U, 1U, "maps/crossfire.bsp"}));
    CHECK_FALSE(journal.begin_generation({1U, 1U, "maps/other.bsp"}));
    REQUIRE(journal.begin_generation({1U, 2U, "maps/crossfire.bsp"}));
    CHECK(journal.snapshot()->fact_mask == 0U);
    CHECK_FALSE(journal.snapshot()->receiving_entity);
    REQUIRE(journal.observe_receiving_entity(7U));
    CHECK(journal.snapshot()->receiving_entity == 7U);
    CHECK_FALSE(app::session_receipt_keyboard_ready(*journal.snapshot()));
    CHECK_FALSE(journal.snapshot()->counters.new_usercmd_count);
    for (std::uint64_t map = 3U; map <= app::kSessionReceiptMaximumGenerations; ++map)
        REQUIRE(journal.begin_generation({1U, map, "maps/crossfire.bsp"}));
    CHECK_FALSE(journal.begin_generation({2U, 1U, "maps/crossfire.bsp"}));
    CHECK(journal.snapshot()->generation_ordinal == app::kSessionReceiptMaximumGenerations);
    app::SessionReceiptJournal unknown{identity()};
    REQUIRE(unknown.begin_generation({0U, 0U, "not_observed"}));
    CHECK_FALSE(unknown.record(app::SessionReceiptStage::connection_established, {1U, {}, {}, {}}));
    REQUIRE(unknown.begin_generation({1U, 1U, "maps/crossfire.bsp"}));
    CHECK_FALSE(unknown.begin_generation({0U, 0U, "not_observed"}));
}

TEST_CASE("E28.1 closed receipt codec rejects corruption and impossible pass metadata",
    "[e281][session-receipt][receipt][offline]")
{
    const auto journal = ready_journal(true);
    const auto encoded = app::encode_session_receipt(*journal.snapshot());
    REQUIRE(encoded);
    for (std::size_t size = 0U; size < encoded->size(); ++size)
        CHECK_FALSE(app::decode_session_receipt(std::string_view{*encoded}.substr(0U, size)));
    CHECK_FALSE(app::decode_session_receipt(*encoded + "serverinfo_received=1\n"));
    CHECK_FALSE(app::decode_session_receipt(changed(*encoded, "schema=hlclient.session-receipt.v1",
        "schema=hlclient.session-receipt.v2")));
    CHECK_FALSE(app::decode_session_receipt(changed(*encoded, "fact_mask=131071", "fact_mask=262143")));
    CHECK_FALSE(app::decode_session_receipt(changed(*encoded, "process_id=41", "process_id=041")));
    CHECK_FALSE(app::decode_session_receipt(changed(*encoded, "role=A", "role=%n")));
    CHECK_FALSE(app::decode_session_receipt(changed(*encoded, "receiving_entity=2", "receiving_entity=0")));
    CHECK_FALSE(app::decode_session_receipt(changed(*encoded, "receiving_entity=2", "receiving_entity=33")));
    CHECK_FALSE(app::decode_session_receipt(changed(*encoded, "receiving_entity=2", "receiving_entity=258")));
    CHECK_FALSE(app::decode_session_receipt(changed(*encoded, "maps/crossfire.bsp", "maps/../crossfire.bsp")));
    auto impossible = *journal.snapshot();
    impossible.close.outcome = app::SessionCloseOutcome::server_disconnect_confirmed;
    CHECK_FALSE(app::encode_session_receipt(impossible));
    impossible = *journal.snapshot();
    impossible.close.send_attempt_count = 4U;
    CHECK_FALSE(app::encode_session_receipt(impossible));
    impossible = *journal.snapshot();
    impossible.close.session_generation = 2U;
    CHECK_FALSE(app::encode_session_receipt(impossible));
    impossible = *journal.snapshot();
    impossible.counters.usercmd_zero_observed = true;
    CHECK_FALSE(app::session_receipt_keyboard_ready(impossible));
}

TEST_CASE("E28.1 actual counters survive reset without invented nonzero counts",
    "[e281][session-receipt][receipt][offline]")
{
    auto journal = ready_journal();
    CHECK(journal.snapshot()->receiving_entity == 2U);
    REQUIRE(journal.observe_receiving_entity(2U));
    CHECK_FALSE(journal.observe_receiving_entity(1U));
    CHECK_FALSE(journal.observe_receiving_entity(0U));
    CHECK_FALSE(journal.observe_receiving_entity(33U));
    REQUIRE(journal.observe_facts(0U, {0U, 0U, 0U, false}));
    CHECK(journal.snapshot()->counters.new_usercmd_count == 144U);
    CHECK(journal.snapshot()->counters.transmitted_usercmd_packet_count == 96U);
    CHECK(journal.snapshot()->counters.server_sample_count == 12U);
    REQUIRE(journal.observe_facts(0U, {200U, 110U, 15U, true}));
    CHECK(journal.snapshot()->counters.new_usercmd_count == 200U);
    CHECK_FALSE(app::session_receipt_keyboard_ready(*journal.snapshot()));
    CHECK(app::normalized_session_map_identifier("crossfire") == "maps/crossfire.bsp");
    CHECK_FALSE(app::normalized_session_map_identifier("../crossfire"));
    CHECK_FALSE(app::normalized_session_map_identifier("C:/maps/crossfire.bsp"));
}

TEST_CASE("E28.1 committed receipt projection excludes rollback partial origins and unready gameplay",
    "[e281][session-receipt][receipt][offline]")
{
    app::SessionReceiptCommitInput input{true, true, 9U, true, true,
        {144U, 96U, 12U, false}};
    const auto positive = app::project_session_receipt_commit(input);
    CHECK(positive.map_entry_committed);
    CHECK(positive.world_ready);
    CHECK(positive.first_gameplay_update);
    CHECK(positive.fact_mask == (app::receipt_fact(app::SessionReceiptFact::world_published) |
        app::receipt_fact(app::SessionReceiptFact::usercmd_transmitted) |
        app::receipt_fact(app::SessionReceiptFact::gameplay_committed)));
    input.owning_commit = false;
    const auto rollback = app::project_session_receipt_commit(input);
    CHECK(rollback.fact_mask == 0U);
    CHECK_FALSE(rollback.map_entry_committed);
    CHECK_FALSE(rollback.world_ready);
    CHECK_FALSE(rollback.first_gameplay_update);
    input.owning_commit = true;
    input.publication_ordinal = 0U;
    CHECK(app::project_session_receipt_commit(input).fact_mask == 0U);
    input.publication_ordinal = 9U;
    input.receiving_origin_complete = false;
    const auto partial = app::project_session_receipt_commit(input);
    CHECK_FALSE(partial.map_entry_committed);
    CHECK_FALSE(partial.world_ready);
    CHECK_FALSE(partial.first_gameplay_update);
    CHECK(partial.fact_mask == app::receipt_fact(app::SessionReceiptFact::usercmd_transmitted));
    input.receiving_origin_complete = true;
    input.counters = {};
    CHECK_FALSE(app::project_session_receipt_commit(input).first_gameplay_update);
    input.counters = {1U, 1U, 1U, false};
    input.control_activated = false;
    CHECK_FALSE(app::project_session_receipt_commit(input).first_gameplay_update);
    input.control_activated = true;
    input.world_assets_ready_and_presented = false;
    CHECK_FALSE(app::project_session_receipt_commit(input).world_ready);
    CHECK_FALSE(app::project_session_receipt_commit(input).first_gameplay_update);
    input.world_assets_ready_and_presented = true;
    input.counters.usercmd_zero_observed = true;
    CHECK_FALSE(app::project_session_receipt_commit(input).first_gameplay_update);
}

TEST_CASE("E28.1 early eligible netchan close retains network identity without fabricated map readiness",
    "[e281][session-receipt][receipt][offline]")
{
    app::SessionReceiptJournal journal{identity(), 1'000'000U};
    REQUIRE(journal.begin_generation({}));
    REQUIRE(journal.begin_generation({1U, 0U, "not_observed"}));
    REQUIRE(journal.observe_facts(app::kSessionReceiptSignonFacts));
    REQUIRE(journal.record(app::SessionReceiptStage::connection_established, {1U, {}, {}, 1'000U}));
    CHECK_FALSE(journal.record(app::SessionReceiptStage::signon_complete, {2U, {}, {}, 2'000U}));
    CHECK_FALSE(journal.observe_receiving_entity(0U));
    REQUIRE(journal.finish(sent_close()));
    const auto encoded = app::encode_session_receipt(*journal.snapshot());
    REQUIRE(encoded);
    const auto decoded = app::decode_session_receipt(*encoded);
    REQUIRE(decoded);
    CHECK(decoded->generation.network_generation == 1U);
    CHECK(decoded->generation.map_generation == 0U);
    CHECK(decoded->generation.map_identifier == "not_observed");
    CHECK(decoded->close.eligible);
    CHECK_FALSE(app::session_receipt_keyboard_ready(*decoded));
    CHECK_FALSE(decoded->milestones[2U]);
}

TEST_CASE("E28.1 peer removal receipts are numeric bounded and generation owned",
    "[e281][session-receipt][receipt][offline]")
{
    auto journal = ready_journal();
    CHECK_FALSE(journal.observe_peer_removal({0U, 1U, 1U, 50U}));
    CHECK_FALSE(journal.observe_peer_removal({1U, 0U, 1U, 50U}));
    for (std::uint8_t entity = 1U; entity <= 32U; ++entity)
        REQUIRE(journal.observe_peer_removal({entity, 10U + entity, 1'000'000U + entity, 60U}));
    CHECK(journal.snapshot()->peer_removal_count == 32U);
    CHECK_FALSE(journal.observe_peer_removal({33U, 50U, 1'000'050U, 60U}));
    const auto first_removal = journal.snapshot()->peer_removals[0U];
    REQUIRE(journal.observe_peer_removal({1U, 11U, 1'100'000U, 61U}));
    CHECK(journal.snapshot()->peer_removals[0U] == first_removal);
    CHECK(journal.snapshot()->peer_removal_count == 32U);
    CHECK_FALSE(journal.observe_peer_removal({1U, 10U, 1'100'000U, 61U}));
    CHECK(journal.snapshot()->peer_removals[0U] == first_removal);
    REQUIRE(journal.observe_peer_removal({1U, 200U, 1'100'000U, 61U}));
    REQUIRE(journal.finish(sent_close()));
    const auto encoded = app::encode_session_receipt(*journal.snapshot());
    REQUIRE(encoded);
    CHECK(encoded->size() <= app::kSessionReceiptMaximumBytes);
    const auto decoded = app::decode_session_receipt(*encoded);
    REQUIRE(decoded);
    CHECK(decoded->peer_removal_count == 32U);
    CHECK(decoded->peer_removals[0U].source_ordinal == 200U);
    CHECK(*decoded == *journal.snapshot());
}

#ifdef _WIN32
TEST_CASE("E28.1 secure atomic receipt publication rejects stale identity and partial final evidence",
    "[e281][session-receipt][receipt][windows][offline]")
{
    ReceiptTemporaryDirectory fixture;
    REQUIRE_FALSE(fixture.path().empty());
    auto opened = windows::open_secure_output_directory(fixture.path());
    REQUIRE(opened);
    const auto& directory = *opened.directory;
    auto journal = ready_journal();
    REQUIRE(windows::publish_session_receipt(directory, *journal.snapshot()));
    auto observed = windows::read_latest_session_receipt(directory, identity(), "maps/crossfire.bsp");
    REQUIRE(observed);
    CHECK(app::session_receipt_keyboard_ready(*observed.snapshot));
    auto final_missing = windows::read_latest_session_receipt(directory, identity(), "maps/crossfire.bsp", true);
    CHECK(final_missing.status == windows::SessionReceiptFileStatus::terminal_missing);
    CHECK_FALSE(final_missing);
    REQUIRE(final_missing.snapshot); // diagnostic proof survives a later crash
    CHECK(app::session_receipt_keyboard_ready(*final_missing.snapshot));
    CHECK_FALSE(windows::publish_session_receipt(directory, *journal.snapshot()));
    REQUIRE(journal.finish(sent_close()));
    REQUIRE(windows::publish_session_receipt(directory, *journal.snapshot()));
    REQUIRE(windows::read_latest_session_receipt(directory, identity(), "maps/crossfire.bsp", true, 1U, 1U));
    auto wrong = identity();
    wrong.run_id[0U] = 'f';
    CHECK(windows::read_latest_session_receipt(directory, wrong, "maps/crossfire.bsp", true).status ==
        windows::SessionReceiptFileStatus::invalid_identity);
    wrong = identity();
    ++wrong.process_id;
    CHECK_FALSE(windows::read_latest_session_receipt(directory, wrong, "maps/crossfire.bsp", true));
    wrong = identity();
    ++wrong.process_creation;
    CHECK_FALSE(windows::read_latest_session_receipt(directory, wrong, "maps/crossfire.bsp", true));
    CHECK_FALSE(windows::read_latest_session_receipt(directory, identity(), "maps/other.bsp", true));
    const auto external=windows::read_latest_external_session_receipt(directory,identity(),true);
    REQUIRE(external);CHECK(external.snapshot->generation.map_identifier=="maps/crossfire.bsp");
    CHECK_FALSE(windows::read_latest_external_session_receipt(directory,wrong,true));
    CHECK_FALSE(windows::read_latest_session_receipt(directory, identity(), "maps/crossfire.bsp", true, 2U, 1U));
    CHECK_FALSE(windows::read_latest_session_receipt(directory, identity(), "maps/crossfire.bsp", true, 1U, 2U));
    // Put an A receipt into the deterministic B leaf; exact role matching,
    // not filenames or server text, owns the acceptance decision.
    const auto encoded = app::encode_session_receipt(*journal.snapshot());
    REQUIRE(encoded);
    publish_bytes(directory, app::session_receipt_leaf(app::SessionReceiptRole::B, 1U,
        app::SessionReceiptStage::terminal), *encoded);
    wrong = identity(); wrong.role = app::SessionReceiptRole::B;
    CHECK(windows::read_latest_session_receipt(directory, wrong, "maps/crossfire.bsp", true).status ==
        windows::SessionReceiptFileStatus::invalid_identity);
    CHECK(windows::query_current_session_receipt_process_identity());
    CHECK(windows::query_session_receipt_clock());
}

TEST_CASE("E28.1 interrupted write and new generation cannot borrow an old ready snapshot",
    "[e281][session-receipt][receipt][windows][offline]")
{
    ReceiptTemporaryDirectory fixture;
    auto opened = windows::open_secure_output_directory(fixture.path());
    REQUIRE(opened);
    const auto& directory = *opened.directory;
    auto journal = ready_journal();
    REQUIRE(windows::publish_session_receipt(directory, *journal.snapshot()));
    // Unpublished temporary bytes are not among the fixed filename whitelist.
    publish_bytes(directory, L".hlclient-stock-runtime-fixture.tmp", "map entered\n");
    REQUIRE(windows::read_latest_session_receipt(directory, identity(), "maps/crossfire.bsp"));
    REQUIRE(journal.begin_generation({1U, 2U, "maps/crossfire.bsp"}));
    CHECK_FALSE(journal.snapshot()->receiving_entity);
    REQUIRE(journal.observe_receiving_entity(1U));
    REQUIRE(windows::publish_session_receipt(directory, *journal.snapshot()));
    const auto new_generation = windows::read_latest_session_receipt(directory, identity(), "maps/crossfire.bsp");
    REQUIRE(new_generation);
    CHECK(new_generation.snapshot->generation.map_generation == 2U);
    CHECK_FALSE(app::session_receipt_keyboard_ready(*new_generation.snapshot));
    app::SessionCloseReceipt cancelled;
    cancelled.reason = app::SessionCloseReason::fatal_failure;
    cancelled.outcome = app::SessionCloseOutcome::fatal_failure;
    REQUIRE(journal.finish(cancelled));
    const auto encoded = app::encode_session_receipt(*journal.snapshot());
    REQUIRE(encoded);
    // Even a published valid prefix is not a receipt. No fallback to the old
    // successful generation or exit0 is possible for this final file.
    publish_bytes(directory, app::session_receipt_leaf(app::SessionReceiptRole::A, 1U,
        app::SessionReceiptStage::terminal), encoded->substr(0U, encoded->size() - 10U));
    CHECK(windows::read_latest_session_receipt(directory, identity(), "maps/crossfire.bsp", true).status ==
        windows::SessionReceiptFileStatus::invalid_receipt);
}

TEST_CASE("E28.1 secure receipt read fails closed for hardlinks and oversized records",
    "[e281][session-receipt][receipt][windows][offline]")
{
    ReceiptTemporaryDirectory fixture;
    auto opened = windows::open_secure_output_directory(fixture.path());
    REQUIRE(opened);
    const auto& directory = *opened.directory;
    const auto leaf = app::session_receipt_leaf(app::SessionReceiptRole::A, 1U,
        app::SessionReceiptStage::generation_started);
    SECTION("oversized") {
        publish_bytes(directory, leaf, std::string(app::kSessionReceiptMaximumBytes + 1U, 'x'));
        CHECK(windows::read_latest_session_receipt(directory, identity(), "maps/crossfire.bsp").status ==
            windows::SessionReceiptFileStatus::size_limit);
    }
    SECTION("hardlinked") {
        auto journal = ready_journal();
        const auto encoded = app::encode_session_receipt(*journal.snapshot());
        REQUIRE(encoded);
        publish_bytes(directory, L"owning-fixture-source.txt", *encoded);
        REQUIRE(::CreateHardLinkW((fixture.path() / leaf).c_str(),
            (fixture.path() / L"owning-fixture-source.txt").c_str(), nullptr));
        CHECK(windows::read_latest_session_receipt(directory, identity(), "maps/crossfire.bsp").status ==
            windows::SessionReceiptFileStatus::not_regular_file);
    }
}
#endif
