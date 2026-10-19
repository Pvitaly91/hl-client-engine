#include <hlclient/goldsrc/file_consistency_response.hpp>

#include <hlclient/goldsrc/bit_writer.hpp>
#include <hlclient/goldsrc/local_resource_mapping.hpp>
#include <hlclient/hash/md5.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <span>
#include <utility>

namespace hlclient::goldsrc {
namespace {

using ErrorCode = FileConsistencyResponseErrorCode;
using Clock = std::chrono::steady_clock;
namespace local = local_resources;

[[nodiscard]] FileConsistencyResponseBuildResult failure(
    const ErrorCode code, const std::optional<std::uint16_t> ordinal = {})
{
    return {{}, FileConsistencyResponseError{code, ordinal,
        ordinal ? "ordinal=" + std::to_string(*ordinal) : std::string{}}};
}

[[nodiscard]] std::optional<ErrorCode> interrupted(
    const std::stop_token stop, const Clock::time_point deadline) noexcept
{
    if (stop.stop_requested()) return ErrorCode::cancelled;
    if (Clock::now() >= deadline) return ErrorCode::timed_out;
    return {};
}

[[nodiscard]] bool supported_asset(
    const ResourceType type, const std::string_view name)
{
    std::string lower{name};
    for (auto& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    const std::string_view path{lower};
    const bool model = path.starts_with("models/") && path.ends_with(".mdl");
    const bool map = path.starts_with("maps/") && path.ends_with(".bsp");
    const bool sprite = path.starts_with("sprites/") && path.ends_with(".spr");
    const bool sound = path.starts_with("sound/") && path.ends_with(".wav");
    const bool event = path.starts_with("events/") && path.ends_with(".sc");
    const bool image = path.starts_with("gfx/") &&
        (path.ends_with(".tga") || path.ends_with(".bmp"));
    const bool wad = path.find('/') == std::string_view::npos && path.ends_with(".wad");
    switch (type) {
    case ResourceType::model: return model || map || sprite;
    case ResourceType::sound: return sound;
    case ResourceType::event_script: return event;
    case ResourceType::generic: return model || map || sprite || sound || event || image || wad;
    case ResourceType::decal: return false;
    }
    return false;
}

[[nodiscard]] bool schema_name(const std::string_view name) noexcept
{
    constexpr std::string_view expected{"delta.lst"};
    if (name.size() != expected.size()) return false;
    for (std::size_t i = 0U; i < name.size(); ++i) {
        const auto c = name[i] >= 'A' && name[i] <= 'Z'
            ? static_cast<char>(name[i] + ('a' - 'A')) : name[i];
        if (c != expected[i]) return false;
    }
    return true;
}

[[nodiscard]] bool rechecker_profile(const ResourceListState& list) noexcept
{
    // ReChecker places its fixed schema baseline at full-list ordinal zero.
    // A similarly named later row or ordinary precache entry is not this profile.
    return !list.entries().empty() && !list.file_consistency_ordinals().empty() &&
        list.file_consistency_ordinals().front() == 0U &&
        list.entries().front().type() == ResourceType::decal &&
        list.entries().front().index().value() == kMaximumResourceIndexWireValue &&
        list.entries().front().name().bytes() == "delta.lst";
}

[[nodiscard]] bool parent_component(const std::string_view name) noexcept
{
    std::size_t start = 0U;
    while (start < name.size()) {
        const auto end = name.find_first_of("/\\", start);
        if (name.substr(start, end == std::string_view::npos ? end : end - start) == "..")
            return true;
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return false;
}

// Classification only: no file lookup and no widening of the approved read
// policy. A decal row can describe an asset-shaped request while still being
// rejected by the actual consistency mapper below.
[[nodiscard]] std::string request_summary(const ResourceListState& list)
{
    std::size_t parent = 0U, absolute = 0U, schema = 0U, assets = 0U, other = 0U;
    const auto& ordinals = list.file_consistency_ordinals();
    const bool first_schema = !ordinals.empty() && ordinals.front() < list.entries().size() &&
        schema_name(list.entries()[ordinals.front()].name().bytes());
    const GoldSrcResourceNameMapper mapper;
    const auto count = (std::min)(ordinals.size(), kMaximumFileConsistencyRequests);
    for (std::size_t i = 0U; i < count; ++i) {
        if (ordinals[i] >= list.entries().size()) { ++other; continue; }
        const auto& entry = list.entries()[ordinals[i]];
        const auto name = entry.name().bytes();
        if (parent_component(name)) { ++parent; continue; }
        if ((!name.empty() && (name.front() == '/' || name.front() == '\\')) ||
            (name.size() >= 2U && name[1U] == ':')) { ++absolute; continue; }
        if (schema_name(name)) { ++schema; continue; }
        const auto type = entry.type() == ResourceType::decal ? ResourceType::generic : entry.type();
        const auto mapped = mapper.classify(type, entry.name());
        if (mapped.file_backed() && mapped.safe_virtual_name() &&
            supported_asset(type, mapped.safe_virtual_name()->value())) ++assets;
        else ++other;
    }
    // Compact names leave space for the escaped failed request within the live
    // stage's 256-byte diagnostic, including its error-code prefix.
    return " count=" + std::to_string(ordinals.size()) +
        " delta0=" + (first_schema ? "1" : "0") +
        " parent=" + std::to_string(parent) +
        " abs=" + std::to_string(absolute) +
        " schema=" + std::to_string(schema) +
        " assets=" + std::to_string(assets) +
        " other=" + std::to_string(other) +
        " rechecker=" + (rechecker_profile(list) ? "1" : "0");
}

[[nodiscard]] std::string escaped_request_name(
    const std::string_view name, const std::size_t limit)
{
    constexpr char hex[]{"0123456789ABCDEF"};
    std::string escaped;
    escaped.reserve(limit);
    std::size_t consumed = 0U;
    for (const char character : name) {
        const auto c = static_cast<unsigned char>(character);
        const auto width = (c < 0x20U || c >= 0x7fU) ? 4U : (c == '\\' || c == '"' ? 2U : 1U);
        if (width > limit - escaped.size()) break;
        if (width == 4U) {
            escaped.append("\\x");
            escaped.push_back(hex[c >> 4U]);
            escaped.push_back(hex[c & 15U]);
        } else {
            if (width == 2U) escaped.push_back('\\');
            escaped.push_back(character);
        }
        ++consumed;
    }
    return " name=\"" + escaped + "\" cut=" + (consumed < name.size() ? "1" : "0");
}

// COM_Munge (not netchan's COM_Munge2), as used by Xash3D
// CL_SendConsistencyInfo and ReHLDS SV_ParseConsistencyResponse. The key is
// the full server spawn count. Only complete 32-bit words are transformed.
void munge_consistency_body(
    const std::span<std::byte> body, const std::uint32_t server_count) noexcept
{
    constexpr std::array<std::uint8_t, 16U> table{
        0x7aU, 0x64U, 0x05U, 0xf1U, 0x1bU, 0x9bU, 0xa0U, 0xb5U,
        0xcaU, 0xedU, 0x61U, 0x0dU, 0x4aU, 0xdfU, 0x8eU, 0xc7U};
    for (std::size_t block = 0U; block < body.size() / 4U; ++block) {
        const std::array<std::byte, 4U> input{
            body[block * 4U], body[block * 4U + 1U],
            body[block * 4U + 2U], body[block * 4U + 3U]};
        for (unsigned j = 0U; j < 4U; ++j) {
            const auto key = static_cast<std::uint8_t>(
                (server_count >> (j * 8U)) ^ ((~server_count) >> ((3U - j) * 8U)) ^
                (0xa5U | (j << j) | j | table[(block + j) % table.size()]));
            body[block * 4U + j] = input[3U - j] ^ std::byte{key};
        }
    }
}

struct HashResult {
    std::optional<std::uint32_t> prefix;
    std::optional<ErrorCode> error;
};

// Same stable-handle/EOF invariant as inspect_local_resource_file, with
// per-chunk deadline and cooperative cancellation for the network worker.
[[nodiscard]] HashResult hash_file(
    local::LocalReadOnlyFile& file, const std::stop_token stop,
    const Clock::time_point deadline)
{
    if (!file.is_open() || !file.is_regular_file() || file.bytes_consumed() != 0U)
        return {{}, ErrorCode::local_read_failed};
    const auto initial = file.initial_snapshot();
    if (initial.file_size > kMaximumFileConsistencyFileBytes)
        return {{}, ErrorCode::local_file_too_large};
    std::array<std::byte, 64U * 1024U> chunk{};
    hash::Md5Hasher hasher;
    std::uint64_t consumed = 0U;
    while (consumed < initial.file_size) {
        if (const auto error = interrupted(stop, deadline)) return {{}, error};
        const auto remaining = initial.file_size - consumed;
        const auto count = static_cast<std::size_t>(
            (std::min)(remaining, static_cast<std::uint64_t>(chunk.size())));
        const auto read = file.read_next(std::span{chunk}.first(count));
        if (!read) return {{}, read.error &&
            read.error->code == local::LocalReadOnlyFileErrorCode::state_changed
                ? ErrorCode::local_file_changed : ErrorCode::local_read_failed};
        if (read.bytes_read == 0U || read.bytes_read > count)
            return {{}, ErrorCode::local_read_failed};
        if (!hasher.update(std::span<const std::byte>{chunk}.first(read.bytes_read)))
            return {{}, ErrorCode::hash_failed};
        consumed += read.bytes_read;
    }
    if (const auto error = interrupted(stop, deadline)) return {{}, error};
    std::array<std::byte, 1U> probe{};
    const auto end = file.read_next(probe);
    if (!end) return {{}, end.error &&
        end.error->code == local::LocalReadOnlyFileErrorCode::state_changed
            ? ErrorCode::local_file_changed : ErrorCode::local_read_failed};
    if (end.bytes_read != 0U || !end.end_of_file)
        return {{}, ErrorCode::local_file_changed};
    const auto final = file.metadata_snapshot();
    if (!final) return {{}, ErrorCode::local_read_failed};
    if (*final.snapshot != initial) return {{}, ErrorCode::local_file_changed};
    if (const auto error = interrupted(stop, deadline)) return {{}, error};
    const auto digest = hasher.finalize();
    if (!digest) return {{}, ErrorCode::hash_failed};
    std::uint32_t prefix = 0U;
    for (unsigned i = 0U; i < 4U; ++i)
        prefix |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>((*digest)[i])) << (i * 8U);
    return {prefix, {}};
}

} // namespace

FileConsistencyResponseBuildResult build_file_consistency_response(
    const ResourceListState& list, const local::LocalResourceEnvironment& environment,
    const std::uint32_t server_count, const std::stop_token stop,
    const FileConsistencyReadPolicy policy)
{
    const auto fail = [&list](const ErrorCode code,
                            const std::optional<std::uint16_t> ordinal = {}) {
        auto result = failure(code, ordinal);
        if (ordinal && *ordinal < list.entries().size())
            result.error->context += " type=" + std::to_string(
                static_cast<unsigned>(list.entries()[*ordinal].type()));
        const auto summary = request_summary(list);
        if (ordinal && *ordinal < list.entries().size()) {
            constexpr std::size_t context_limit = 200U;
            constexpr std::size_t name_wrapper_size = 14U;
            const auto reserved = result.error->context.size() + summary.size() + name_wrapper_size;
            const auto name_limit = reserved < context_limit ? context_limit - reserved : 0U;
            result.error->context += escaped_request_name(
                list.entries()[*ordinal].name().bytes(), name_limit);
        }
        result.error->context += summary;
        return result;
    };
    if (!list.file_consistency_required()) {
        if (!list.file_consistency_ordinals().empty()) return fail(ErrorCode::invalid_request);
        return {};
    }
    const auto deadline = Clock::now() + std::chrono::seconds{kFileConsistencyDeadlineSeconds};
    if (const auto error = interrupted(stop, deadline)) return fail(*error);
    if (policy != FileConsistencyReadPolicy::game_assets_only &&
        policy != FileConsistencyReadPolicy::approved_installation_files)
        return fail(ErrorCode::invalid_request);
    const auto& ordinals = list.file_consistency_ordinals();
    if (ordinals.size() > kMaximumFileConsistencyRequests || ordinals.size() > list.entries().size())
        return fail(ErrorCode::invalid_request);
    std::optional<std::uint16_t> current;
    try {
        // Each exact-file row is one continuation bit + ordinal12 + MD5low32.
        const auto bit_count = ordinals.size() * 45U + 1U;
        const auto byte_count = (bit_count + 7U) / 8U;
        static_assert((kMaximumFileConsistencyRequests * 45U + 8U) / 8U <=
            (std::numeric_limits<std::int16_t>::max)());
        std::vector<std::byte> body(byte_count, std::byte{0U});
        BitWriter writer{body};
        GoldSrcResourceNameMapper mapper;
        std::uint64_t total_bytes = 0U;
        std::optional<std::uint16_t> previous;
        std::optional<std::uint32_t> previous_response_prefix;
        const bool checker = rechecker_profile(list);
        for (const auto ordinal : ordinals) {
            current = ordinal;
            if (const auto error = interrupted(stop, deadline)) return fail(*error, current);
            if (ordinal >= list.entries().size() ||
                (previous && ordinal <= *previous) ||
                list.entries()[ordinal].wire_ordinal() != ordinal)
                return fail(ErrorCode::invalid_request, current);
            previous = ordinal;
            const auto& entry = list.entries()[ordinal];
            const bool checker_row = checker && entry.type() == ResourceType::decal &&
                entry.index().value() == kMaximumResourceIndexWireValue;
            const bool schema_baseline = checker_row && ordinal == 0U;
            local::LocalResourceResolutionResult resolved;
            if (checker_row && policy == FileConsistencyReadPolicy::approved_installation_files) {
                // This separate API requires the explicit installation-scoped
                // policy. Its own strict validation allows at most one leading
                // ../ to the pinned base, not traversal or alternate roots.
                resolved = environment.resolver().resolve_installation_check(entry.name().bytes());
            } else {
                const auto mapped_type = checker_row ? ResourceType::generic : entry.type();
                const auto mapping = mapper.classify(mapped_type, entry.name());
                if (!mapping.file_backed() || !mapping.safe_virtual_name()) {
                    const bool unsafe = mapping.kind() == GoldSrcResourceNameClassificationKind::unsafe_name ||
                        mapping.kind() == GoldSrcResourceNameClassificationKind::unsupported_name_encoding;
                    return fail(unsafe ? ErrorCode::unsafe_name : ErrorCode::unsupported_asset, current);
                }
                const auto name = mapping.safe_virtual_name()->value();
                // Only the fixed profile head admits the game-root schema.
                // Normal resources and the default policy keep the allowlist.
                if (!schema_baseline && !supported_asset(mapped_type, name))
                    return fail(ErrorCode::unsupported_asset, current);
                resolved = environment.resolver().resolve(name);
            }
            if (const auto error = interrupted(stop, deadline)) return fail(*error, current);
            std::optional<std::uint32_t> prefix;
            if (!resolved) {
                // ReChecker FileConsistencyResponse defines equality with the
                // preceding response as MISSING. Use that encoding only after
                // a real baseline hash and a verified allowed-file not_found;
                // denial, unsafe names, unreadable files and missing heads fail.
                if (checker_row && !schema_baseline && previous_response_prefix &&
                    resolved.code == local::LocalResourceResolutionCode::not_found)
                    prefix = previous_response_prefix;
                else {
                    const auto code = resolved.code == local::LocalResourceResolutionCode::too_large
                        ? ErrorCode::local_file_too_large
                        : resolved.code == local::LocalResourceResolutionCode::unsafe_name ||
                          resolved.code == local::LocalResourceResolutionCode::unsupported_name_encoding
                            ? ErrorCode::unsafe_name : ErrorCode::local_file_unavailable;
                    return fail(code, current);
                }
            } else {
                const auto size = resolved.file->file_size();
                if (size > kMaximumFileConsistencyFileBytes)
                    return fail(ErrorCode::local_file_too_large, current);
                if (size > kMaximumFileConsistencyTotalBytes - total_bytes)
                    return fail(ErrorCode::total_bytes_exceeded, current);
                total_bytes += size;
                const auto hashed = hash_file(*resolved.file, stop, deadline);
                if (hashed.error || !hashed.prefix)
                    return fail(hashed.error.value_or(ErrorCode::hash_failed), current);
                prefix = hashed.prefix;
            }
            if (!writer.write_bits(1U, 1U) || !writer.write_bits(ordinal, 12U) ||
                !writer.write_bits(*prefix, 32U))
                return fail(ErrorCode::encoding_failed, current);
            previous_response_prefix = prefix;
        }
        if (!writer.write_bits(0U, 1U) ||
            writer.align_to_byte_zero_padding() != BitWriterError::none ||
            writer.written_bits() != byte_count * 8U)
            return fail(ErrorCode::encoding_failed);
        if (const auto error = interrupted(stop, deadline)) return fail(*error, current);
        munge_consistency_body(body, server_count);
        std::vector<std::byte> result;
        result.reserve(body.size() + 3U);
        result.push_back(std::byte{kFileConsistencyResponseOpcode});
        result.push_back(static_cast<std::byte>(body.size() & 0xffU));
        result.push_back(static_cast<std::byte>((body.size() >> 8U) & 0xffU));
        result.insert(result.end(), body.begin(), body.end());
        return {std::move(result), {}};
    } catch (...) {
        return fail(ErrorCode::allocation_failed, current);
    }
}

} // namespace hlclient::goldsrc
