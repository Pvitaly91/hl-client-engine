#include <hlclient/goldsrc/hud_manifest.hpp>
#include <hlclient/hash/sha256.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <set>
#include <utility>

namespace hlclient::goldsrc {
namespace {
bool token_character(char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') || value == '_' || value == '-';
}
bool role_name(std::string_view value) noexcept {
    return !value.empty() && value.size() <= kMaximumHudManifestTokenBytes &&
        std::all_of(value.begin(), value.end(), token_character);
}
bool number(std::string_view value, std::uint32_t maximum, std::uint32_t& out) noexcept {
    if (value.empty() || value.size() > 10U ||
        !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), out);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && out <= maximum;
}
std::vector<std::string_view> tokens(std::string_view line) {
    std::vector<std::string_view> out;
    if (const auto comment = line.find("//"); comment != line.npos) line = line.substr(0, comment);
    while (!line.empty()) {
        const auto start = line.find_first_not_of(" \t\r");
        if (start == line.npos) break;
        line.remove_prefix(start);
        const auto end = line.find_first_of(" \t\r");
        out.push_back(line.substr(0, end));
        if (out.size() > 7U || end == line.npos) break;
        line.remove_prefix(end);
    }
    return out;
}
std::string folded(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    return value;
}
} // namespace

bool valid_hud_descriptor_name(std::string_view name) noexcept {
    if (!name.starts_with("sprites/") || !name.ends_with(".txt") || name.size() > 80U) return false;
    return role_name(name.substr(8U, name.size() - 12U));
}
bool valid_hud_sprite_basename(std::string_view name) noexcept {
    if (name.empty() || name.size() > kMaximumHudManifestTokenBytes ||
        name.front() == '/' || name.back() == '/' || name.find("//") != name.npos) return false;
    return std::all_of(name.begin(), name.end(), [](char c) { return token_character(c) || c == '/'; });
}
bool valid_hud_source_rectangle(game_api::HudSourceRectangle rect,
    std::uint32_t width, std::uint32_t height) noexcept {
    return rect.width && rect.height && rect.x <= width && rect.y <= height &&
        rect.width <= width - rect.x && rect.height <= height - rect.y;
}
HudManifestParseResult parse_hud_manifest(std::span<const std::byte> bytes,
    const std::optional<game_api::HudManifestCountCompatibility>& compatibility) {
    HudManifestParseResult result;
    const auto fail = [&](HudManifestError code, std::size_t line) {
        result.error = code; result.line = line; result.records.clear(); return result;
    };
    if (bytes.empty() || bytes.size() > kMaximumHudManifestBytes) return fail(HudManifestError::size_limit, 0U);
    for (const auto value : bytes) {
        const auto c = std::to_integer<unsigned char>(value);
        if (c > 126U || (c < 32U && c != '\r' && c != '\n' && c != '\t'))
            return fail(HudManifestError::invalid_text, 0U);
    }
    const auto digest = hash::sha256(bytes);
    if (!digest) return fail(HudManifestError::size_limit, 0U);
    result.source_sha256 = hash::sha256_hex(*digest);
    auto text = std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    std::set<std::pair<std::string, std::uint32_t>> seen;
    std::set<std::string> dependencies;
    bool first = true;
    std::size_t line_number = 0U;
    while (!text.empty()) {
        ++line_number;
        const auto end = text.find('\n');
        const auto row = tokens(text.substr(0, end));
        if (end == text.npos) text = {}; else text.remove_prefix(end + 1U);
        if (row.empty()) continue;
        if (first && row.size() == 1U) {
            std::uint32_t count{};
            if (!number(row.front(), static_cast<std::uint32_t>(game_api::kMaximumHudResourceEntries), count))
                return fail(HudManifestError::invalid_count, line_number);
            result.declared_count = count; first = false; continue;
        }
        first = false;
        if (row.size() != 7U) return fail(HudManifestError::invalid_record, line_number);
        if (result.records.size() == game_api::kMaximumHudResourceEntries)
            return fail(HudManifestError::entry_limit, line_number);
        for (const auto token : row) if (token.size() > kMaximumHudManifestTokenBytes)
            return fail(HudManifestError::invalid_name, line_number);
        if (!role_name(row[0]) || !valid_hud_sprite_basename(row[2]))
            return fail(HudManifestError::invalid_name, line_number);
        HudManifestRecord record;
        record.role = row[0]; record.sprite_basename = row[2];
        if (!number(row[1], 65'535U, record.resolution) || !record.resolution ||
            !number(row[3], kMaximumHudManifestDimension, record.source.x) ||
            !number(row[4], kMaximumHudManifestDimension, record.source.y) ||
            !number(row[5], kMaximumHudManifestDimension, record.source.width) || !record.source.width ||
            !number(row[6], kMaximumHudManifestDimension, record.source.height) || !record.source.height ||
            record.source.width > kMaximumHudManifestDimension - record.source.x ||
            record.source.height > kMaximumHudManifestDimension - record.source.y)
            return fail(HudManifestError::invalid_number, line_number);
        if (!seen.emplace(record.role, record.resolution).second)
            return fail(HudManifestError::duplicate_role_tier, line_number);
        dependencies.insert(record.sprite_basename);
        if (dependencies.size() > game_api::kMaximumHudResourceDependencies)
            return fail(HudManifestError::dependency_limit, line_number);
        result.records.push_back(std::move(record));
    }
    if (result.declared_count && *result.declared_count != result.records.size()) {
        if (!compatibility || compatibility->exact_source_sha256.size() != 64U ||
            folded(compatibility->exact_source_sha256) != result.source_sha256 ||
            compatibility->declared_count != *result.declared_count ||
            compatibility->parsed_count != result.records.size())
            return fail(HudManifestError::count_mismatch, line_number);
        result.exact_count_compatibility_applied = true;
    }
    return result;
}
} // namespace hlclient::goldsrc
