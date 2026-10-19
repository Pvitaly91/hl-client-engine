#pragma once
#include <hlclient/gameplay_input/gameplay_input_bindings.hpp>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hlclient::console {
[[nodiscard]] bool valid_utf8(std::string_view text) noexcept;
[[nodiscard]] std::string utf8_prefix(std::string_view text, std::size_t bytes);
[[nodiscard]] std::string quote_argument(std::string_view text);
struct ConsoleLimits {
    std::size_t scrollback_lines{1024};
    std::size_t scrollback_bytes{256 * 1024};
    std::size_t editor_bytes{4096};
    std::size_t history_lines{128};
    std::size_t paste_lines{32};
};
class ConsoleModel final {
public:
    explicit ConsoleModel(ConsoleLimits limits = {});
    void append(std::string_view text);
    void clear() noexcept;
    [[nodiscard]] const std::deque<std::string>& lines() const noexcept;
    [[nodiscard]] std::size_t dropped_lines() const noexcept;
    [[nodiscard]] std::size_t scroll_offset() const noexcept;
    void scroll(int lines) noexcept;
    void bottom() noexcept;
    [[nodiscard]] std::vector<std::string> visible_lines(std::size_t columns, std::size_t rows) const;
    [[nodiscard]] const std::string& editor() const noexcept;
    [[nodiscard]] std::size_t cursor() const noexcept;
    [[nodiscard]] bool selected_all() const noexcept;
    bool set_editor(std::string_view text);
    bool insert(std::string_view text);
    void left() noexcept;
    void right() noexcept;
    void home() noexcept;
    void end() noexcept;
    void backspace();
    void erase_forward();
    void select_all() noexcept;
    [[nodiscard]] std::string copy_selection() const;
    void history_previous();
    void history_next();
    [[nodiscard]] std::string submit(bool retain_history = true);
    void complete(std::span<const std::string> candidates);
private:
    ConsoleLimits limits_;
    std::deque<std::string> lines_;
    std::size_t line_bytes_{};
    std::size_t dropped_{};
    // Scrollback is anchored to retained logical content, never to an index
    // into a previous framebuffer's wrapped rows. No anchor means follow tail.
    struct ScrollAnchor { std::uint64_t line{}; std::size_t byte_end{}; };
    struct ScrollWindow { std::size_t total{}, begin{}, end{}; };
    [[nodiscard]] ScrollWindow scroll_window() const noexcept;
    std::uint64_t first_line_id_{};
    std::optional<ScrollAnchor> scroll_anchor_;
    mutable std::size_t viewport_columns_{80};
    mutable std::size_t viewport_rows_{1};
    std::string editor_;
    std::size_t cursor_{};
    bool select_all_{};
    std::deque<std::string> history_;
    std::size_t history_offset_{};
    std::string history_draft_;
};
// Local-compatible 180ms slide, in monotonic seconds and viewport fraction.
class ConsoleController final {
public:
    void toggle(double now_seconds, bool repeated = false) noexcept;
    void set_open(bool open, double now_seconds) noexcept;
    void advance(double now_seconds) noexcept;
    [[nodiscard]] bool takes_input() const noexcept;
    [[nodiscard]] bool target_open() const noexcept;
    [[nodiscard]] double height_fraction() const noexcept;
private:
    double height_{};
    double last_time_{};
    bool has_time_{};
    bool open_{};
};
enum class ExecutionSource : std::uint8_t { local_console = 1, local_config = 2, local_binding = 4, server = 8 };
inline constexpr std::uint8_t local_execution_sources = 1 | 2 | 4;
enum class SessionState : std::uint8_t { idle = 1, loading = 2, in_game = 4, disconnecting = 8, error = 16 };
inline constexpr std::uint8_t all_session_states = 31;
enum class CommandSemantics : std::uint8_t { local, server_request, session_request, unsupported };
enum class ValueType : std::uint8_t { text, integer, real, boolean };
using ConsoleValue = std::variant<std::string, std::int64_t, double, bool>;
struct ArgumentSpec {
    std::string name;
    ValueType type{ValueType::text};
    bool optional{};
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::size_t maximum_bytes{1024};
};
struct ExecutionContext {
    ExecutionSource source{ExecutionSource::local_console};
    SessionState state{SessionState::idle};
    std::uint64_t session_generation{};
};
struct CommandResult { bool success{true}; std::string message; };
struct CommandDescriptor {
    std::string name;
    std::string description;
    std::string usage;
    std::string owner{"application"};
    std::vector<ArgumentSpec> arguments;
    bool variadic{};
    std::uint8_t allowed_sources{local_execution_sources};
    std::uint8_t allowed_states{all_session_states};
    CommandSemantics semantics{CommandSemantics::local};
    bool persistent{};
    bool restart_required{};
    bool sensitive{};
    std::function<std::vector<std::string>(std::string_view)> completion;
};
using CommandHandler = std::function<CommandResult(const ExecutionContext&, std::span<const ConsoleValue>)>;
class CommandRegistry final {
public:
    bool add(CommandDescriptor descriptor, CommandHandler handler);
    [[nodiscard]] const CommandDescriptor* find(std::string_view name) const;
    [[nodiscard]] std::vector<CommandDescriptor> descriptors() const;
    [[nodiscard]] std::vector<std::string> complete(std::string_view text) const;
    [[nodiscard]] CommandResult execute(std::string_view name, std::span<const std::string> arguments, const ExecutionContext& context) const;
private:
    struct Entry { CommandDescriptor descriptor; CommandHandler handler; };
    std::map<std::string, Entry, std::less<>> entries_;
};
struct CVarDescriptor { CommandDescriptor command; ArgumentSpec value; ConsoleValue default_value; };
struct CVarState { ConsoleValue requested; ConsoleValue applied; };
using CVarApply = std::function<CommandResult(const ConsoleValue&, ConsoleValue& actual)>;
class CVarRegistry final {
public:
    bool add(CVarDescriptor descriptor, ConsoleValue initial, CVarApply apply);
    [[nodiscard]] const CVarDescriptor* find(std::string_view name) const;
    [[nodiscard]] std::optional<CVarState> state(std::string_view name) const;
    bool observe_applied(std::string_view name,ConsoleValue value);
    [[nodiscard]] std::vector<CVarDescriptor> descriptors() const;
    [[nodiscard]] CommandResult execute(std::string_view name, std::span<const std::string> arguments, const ExecutionContext& context);
    [[nodiscard]] std::string persistent_config() const;
private:
    struct Entry { CVarDescriptor descriptor; CVarState state; CVarApply apply; };
    std::map<std::string, Entry, std::less<>> entries_;
};
[[nodiscard]] std::string value_text(const ConsoleValue& value);
struct ParsedCommand { std::vector<std::string> words; };
struct ParseResult {
    std::vector<ParsedCommand> commands;
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};
[[nodiscard]] ParseResult parse_commands(std::string_view text, std::size_t maximum_bytes = 65536, std::size_t maximum_commands = 256);
// Strict user-owned .cfg basenames. Never searches assets or server downloads.
class ConsoleConfigStore final {
public:
    explicit ConsoleConfigStore(std::filesystem::path root);
    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::optional<std::string> read(std::string_view name, std::string& error) const;
    [[nodiscard]] bool write(std::string_view name, std::string_view text, std::string& error) const;
private:
    [[nodiscard]] std::optional<std::filesystem::path> checked_path(std::string_view name, std::string& error) const;
    std::filesystem::path root_;
};
// Cumulative per-operation budgets survive update boundaries; source never changes.
class CommandExecutor final {
public:
    CommandExecutor(CommandRegistry& commands, CVarRegistry& cvars);
    void set_config_store(ConsoleConfigStore* store) noexcept;
    [[nodiscard]] CommandResult enqueue(std::string_view text, ExecutionSource source, std::uint64_t generation);
    [[nodiscard]] std::vector<std::string> process(SessionState state, std::uint64_t generation, std::size_t maximum_commands = 32,
        const std::function<void(std::string_view)>& output_sink={});
    [[nodiscard]] CommandResult define_alias(std::string_view name, std::string_view body);
    [[nodiscard]] std::vector<std::string> complete(std::string_view text) const;
    [[nodiscard]] std::string persistent_aliases() const;
    [[nodiscard]] bool safe_for_history(std::string_view text) const;
    void discard_pending() noexcept;
private:
    struct Pending {
        ParsedCommand command;
        ExecutionSource source;
        std::uint64_t generation;
        std::uint64_t operation;
        std::vector<std::string> ancestry;
    };
    struct Budget { std::size_t commands{}; std::size_t bytes{}; std::size_t expansions{}; };
    [[nodiscard]] CommandResult expand(const Pending& parent, std::string_view body, std::string key);
    CommandRegistry& commands_;
    CVarRegistry& cvars_;
    ConsoleConfigStore* config_{};
    std::map<std::string, std::string, std::less<>> aliases_;
    std::deque<Pending> pending_;
    std::map<std::uint64_t, Budget> budgets_;
    std::uint64_t next_operation_{};
};
class ConsoleBindings final {
public:
    using Action = gameplay_input::GameplayInputAction;
    using ActionSink = std::function<void(Action, bool)>;
    using CommandSink = std::function<void(std::string_view)>;
    void register_action(std::string name, Action action);
    [[nodiscard]] CommandResult bind(std::string_view key, std::string_view command);
    void unbind(std::string_view key);
    void unbind_all();
    [[nodiscard]] const std::map<std::string, std::string, std::less<>>& bindings() const noexcept;
    [[nodiscard]] bool key_event(std::string_view key, bool down, bool repeated, const ActionSink& action_sink, const CommandSink& command_sink);
    void release_all(const ActionSink& sink);
    [[nodiscard]] std::string persistent_config() const;
private:
    std::map<std::string, std::string, std::less<>> bindings_;
    std::map<std::string, Action, std::less<>> actions_;
    std::map<std::string, Action, std::less<>> held_;
};
} // namespace hlclient::console
