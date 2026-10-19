#include <hlclient/goldsrc/resource_list.hpp>

#include <hlclient/goldsrc/bit_reader.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace hlclient::goldsrc {
namespace {

[[nodiscard]] bool supported_profile(
    const ResourceListCompatibilityProfile profile) noexcept
{
    return profile == ResourceListCompatibilityProfile::
                          stock_protocol_48_build_10210_standard;
}

[[nodiscard]] ResourceListParseResult failure(
    const ResourceListErrorCode code,
    const std::size_t absolute_bit_offset,
    const std::optional<std::size_t> entry_index,
    std::string context)
{
    return ResourceListParseResult{
        std::nullopt,
        ResourceListError{
            code,
            absolute_bit_offset / 8U,
            absolute_bit_offset,
            entry_index,
            std::move(context),
        },
        0U,
        0U,
        0U,
        0U,
    };
}

[[nodiscard]] PostResourceListStreamDecodeResult post_failure(
    const PostResourceListStreamErrorCode code,
    const std::size_t absolute_bit_offset,
    std::string context)
{
    return PostResourceListStreamDecodeResult{
        std::nullopt,
        PostResourceListStreamError{
            code,
            absolute_bit_offset / 8U,
            absolute_bit_offset,
            std::move(context),
        },
        0U,
    };
}

[[nodiscard]] bool checked_add_size(
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

[[nodiscard]] bool checked_add_u64(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& output) noexcept
{
    if (right > (std::numeric_limits<std::uint64_t>::max)() - left) {
        return false;
    }
    output = left + right;
    return true;
}

[[nodiscard]] std::optional<ResourceType> decode_resource_type(
    const std::uint32_t wire_value) noexcept
{
    switch (wire_value) {
    case 0U: return ResourceType::sound;
    case 2U: return ResourceType::model;
    case 3U: return ResourceType::decal;
    case 4U: return ResourceType::generic;
    case 5U: return ResourceType::event_script;
    default: return std::nullopt;
    }
}

[[nodiscard]] std::size_t resource_type_summary_index(
    const ResourceType type) noexcept
{
    switch (type) {
    case ResourceType::sound: return 0U;
    case ResourceType::model: return 1U;
    case ResourceType::decal: return 2U;
    case ResourceType::generic: return 3U;
    case ResourceType::event_script: return 4U;
    }
    return 0U;
}

} // namespace

bool valid_resource_list_limits(const ResourceListLimits& limits) noexcept
{
    return limits.maximum_resource_message_bits > 0U &&
           limits.maximum_resource_message_bits <= kMaximumResourceMessageBits &&
           limits.maximum_resource_message_bytes > 0U &&
           limits.maximum_resource_message_bytes <= kMaximumResourceMessageBytes &&
           limits.maximum_resource_message_bits <=
               limits.maximum_resource_message_bytes * 8U &&
           limits.maximum_resource_count > 0U &&
           limits.maximum_resource_count <= kMaximumResourceCount &&
           limits.maximum_resource_name_length > 0U &&
           limits.maximum_resource_name_length <= kMaximumResourceNameLength &&
           limits.maximum_resource_total_name_bytes >=
               limits.maximum_resource_name_length &&
           limits.maximum_resource_total_name_bytes <=
               kMaximumResourceTotalNameBytes &&
           limits.maximum_resource_declared_size <=
               kMaximumResourceDeclaredSize &&
           limits.maximum_resource_total_declared_size >=
               limits.maximum_resource_declared_size &&
           limits.maximum_resource_total_declared_size <=
               kMaximumResourceTotalDeclaredSize &&
           limits.maximum_resource_flags <= kMaximumResourceFlags;
}

ResourceName::ResourceName(std::string bytes) noexcept
    : bytes_{std::move(bytes)}
{
}

std::string_view ResourceName::bytes() const noexcept
{
    return bytes_;
}

std::size_t ResourceName::byte_length() const noexcept
{
    return bytes_.size();
}

ResourceIndex::ResourceIndex(const std::uint16_t value) noexcept
    : value_{value}
{
}

std::uint16_t ResourceIndex::value() const noexcept
{
    return value_;
}

ResourceByteSize::ResourceByteSize(const std::uint32_t value) noexcept
    : value_{value}
{
}

std::uint32_t ResourceByteSize::raw_code() const noexcept
{
    return value_;
}

ResourceFlags::ResourceFlags(const std::uint8_t wire_value) noexcept
    : wire_value_{wire_value}
{
}

std::uint8_t ResourceFlags::wire_value() const noexcept
{
    return wire_value_;
}

ResourceEntry::ResourceEntry(
    const ResourceType type,
    ResourceName name,
    const ResourceIndex index,
    const ResourceByteSize declared_size,
    const ResourceFlags flags,
    const std::size_t wire_ordinal,
    const std::size_t source_start_bit_offset,
    const std::size_t source_end_bit_offset) noexcept
    : type_{type},
      name_{std::move(name)},
      index_{index},
      declared_size_{declared_size},
      flags_{flags},
      wire_ordinal_{wire_ordinal},
      source_start_bit_offset_{source_start_bit_offset},
      source_end_bit_offset_{source_end_bit_offset}
{
}

ResourceType ResourceEntry::type() const noexcept
{
    return type_;
}

const ResourceName& ResourceEntry::name() const noexcept
{
    return name_;
}

const ResourceIndex& ResourceEntry::index() const noexcept
{
    return index_;
}

const ResourceByteSize& ResourceEntry::declared_size() const noexcept
{
    return declared_size_;
}

const ResourceFlags& ResourceEntry::flags() const noexcept
{
    return flags_;
}

std::size_t ResourceEntry::wire_ordinal() const noexcept
{
    return wire_ordinal_;
}

std::size_t ResourceEntry::source_start_bit_offset() const noexcept
{
    return source_start_bit_offset_;
}

std::size_t ResourceEntry::source_end_bit_offset() const noexcept
{
    return source_end_bit_offset_;
}

ResourceTypeCount::ResourceTypeCount(
    const ResourceType type,
    const std::size_t count) noexcept
    : type_{type}, count_{count}
{
}

ResourceType ResourceTypeCount::type() const noexcept
{
    return type_;
}

std::size_t ResourceTypeCount::count() const noexcept
{
    return count_;
}

ResourceTypeSummary::ResourceTypeSummary(
    const std::array<std::size_t, 5U>& counts) noexcept
    : ordered_counts_{
          ResourceTypeCount{ResourceType::sound, counts[0U]},
          ResourceTypeCount{ResourceType::model, counts[1U]},
          ResourceTypeCount{ResourceType::decal, counts[2U]},
          ResourceTypeCount{ResourceType::generic, counts[3U]},
          ResourceTypeCount{ResourceType::event_script, counts[4U]},
      }
{
}

std::span<const ResourceTypeCount>
ResourceTypeSummary::ordered_counts() const noexcept
{
    return ordered_counts_;
}

std::size_t ResourceTypeSummary::count(const ResourceType type) const noexcept
{
    const auto found = std::find_if(
        ordered_counts_.begin(),
        ordered_counts_.end(),
        [type](const ResourceTypeCount& entry) noexcept {
            return entry.type() == type;
        });
    return found != ordered_counts_.end() ? found->count() : 0U;
}

ResourceListState::ResourceListState(
    std::vector<ResourceEntry> entries,
    ResourceTypeSummary type_summary,
    const std::uint64_t total_size_code_sum,
    const std::size_t total_name_byte_count,
    const bool file_consistency_required,
    std::vector<std::uint16_t> file_consistency_ordinals,
    const std::size_t source_opcode_byte_offset,
    const std::size_t source_payload_bit_length,
    const std::size_t bits_consumed,
    const std::size_t bytes_consumed,
    const std::size_t next_byte_offset,
    const std::size_t next_bit_offset,
    const ResourceListCompatibilityProfile profile) noexcept
    : entries_{std::move(entries)},
      type_summary_{std::move(type_summary)},
      total_size_code_sum_{total_size_code_sum},
      total_name_byte_count_{total_name_byte_count},
      file_consistency_required_{file_consistency_required},
      file_consistency_ordinals_{std::move(file_consistency_ordinals)},
      source_opcode_byte_offset_{source_opcode_byte_offset},
      source_payload_bit_length_{source_payload_bit_length},
      bits_consumed_{bits_consumed},
      bytes_consumed_{bytes_consumed},
      next_byte_offset_{next_byte_offset},
      next_bit_offset_{next_bit_offset},
      profile_{profile}
{
}

const std::vector<ResourceEntry>& ResourceListState::entries() const noexcept
{
    return entries_;
}

std::size_t ResourceListState::resource_count() const noexcept
{
    return entries_.size();
}

const ResourceTypeSummary& ResourceListState::type_summary() const noexcept
{
    return type_summary_;
}

std::uint64_t ResourceListState::total_size_code_sum() const noexcept
{
    return total_size_code_sum_;
}

std::size_t ResourceListState::total_name_byte_count() const noexcept
{
    return total_name_byte_count_;
}

bool ResourceListState::file_consistency_required() const noexcept
{
    return file_consistency_required_;
}

const std::vector<std::uint16_t>&
ResourceListState::file_consistency_ordinals() const noexcept
{
    return file_consistency_ordinals_;
}

const ResourceEntry* ResourceListState::find_exact(
    const ResourceType type,
    const std::uint16_t index) const noexcept
{
    if (!resource_has_bindable_index(type, index)) {
        return nullptr;
    }
    const auto found = std::find_if(
        entries_.begin(),
        entries_.end(),
        [type, index](const ResourceEntry& entry) noexcept {
            return entry.type() == type && entry.index().value() == index;
        });
    return found != entries_.end() ? &*found : nullptr;
}

std::size_t ResourceListState::source_opcode_byte_offset() const noexcept
{
    return source_opcode_byte_offset_;
}

std::size_t ResourceListState::source_payload_bit_length() const noexcept
{
    return source_payload_bit_length_;
}

std::size_t ResourceListState::bits_consumed() const noexcept
{
    return bits_consumed_;
}

std::size_t ResourceListState::bytes_consumed() const noexcept
{
    return bytes_consumed_;
}

std::size_t ResourceListState::next_byte_offset() const noexcept
{
    return next_byte_offset_;
}

std::size_t ResourceListState::next_bit_offset() const noexcept
{
    return next_bit_offset_;
}

ResourceListCompatibilityProfile
ResourceListState::compatibility_profile() const noexcept
{
    return profile_;
}

ResourceListEvidenceProfile ResourceListState::evidence_profile() const noexcept
{
    return ResourceListEvidenceProfile::
        repeated_signed_stock_standard_resource_lists;
}

ResourceListParser::ResourceListParser(
    ResourceListLimits limits,
    const ResourceListCompatibilityProfile profile) noexcept
    : limits_{limits}, profile_{profile}
{
}

bool ResourceListParser::valid_configuration() const noexcept
{
    return valid_resource_list_limits(limits_) && supported_profile(profile_);
}

const ResourceListLimits& ResourceListParser::limits() const noexcept
{
    return limits_;
}

ResourceListParseResult ResourceListParser::parse(
    const std::span<const std::byte> service_payload,
    const std::size_t opcode_byte_offset,
    const std::size_t service_payload_bit_length) const
{
    if (!valid_configuration()) {
        return failure(
            ResourceListErrorCode::invalid_configuration,
            0U,
            std::nullopt,
            "Resource-list limits or compatibility profile are unsupported");
    }
    if (service_payload.size() >
        (std::numeric_limits<std::size_t>::max)() / 8U) {
        return failure(
            ResourceListErrorCode::invalid_input_geometry,
            0U,
            std::nullopt,
            "Resource-list source payload bit geometry overflowed");
    }

    const auto available_bits = service_payload.size() * 8U;
    const auto selected_bit_length =
        service_payload_bit_length == static_cast<std::size_t>(-1)
            ? available_bits
            : service_payload_bit_length;
    if (selected_bit_length > available_bits ||
        opcode_byte_offset >= service_payload.size() ||
        opcode_byte_offset >
            (std::numeric_limits<std::size_t>::max)() / 8U) {
        return failure(
            ResourceListErrorCode::invalid_input_geometry,
            0U,
            std::nullopt,
            "Resource-list opcode offset or bit limit is outside the source payload");
    }

    const auto message_start_bit = opcode_byte_offset * 8U;
    if (message_start_bit > selected_bit_length ||
        selected_bit_length - message_start_bit < 8U) {
        return failure(
            ResourceListErrorCode::invalid_input_geometry,
            message_start_bit,
            std::nullopt,
            "Resource-list bit limit truncates the supplied opcode cursor");
    }
    if (std::to_integer<std::uint8_t>(service_payload[opcode_byte_offset]) !=
        kResourceListOpcode) {
        return failure(
            ResourceListErrorCode::wrong_opcode,
            message_start_bit,
            std::nullopt,
            "Resource-list parser requires opcode 43 at the exact supplied cursor");
    }

    const auto message_bits = selected_bit_length - message_start_bit;
    const auto message_bytes = message_bits / 8U +
        ((message_bits & 7U) != 0U ? 1U : 0U);
    if (message_bits > limits_.maximum_resource_message_bits ||
        message_bytes > limits_.maximum_resource_message_bytes) {
        return failure(
            ResourceListErrorCode::message_too_large,
            message_start_bit,
            std::nullopt,
            "Resource-list message exceeds the configured project bound");
    }

    const auto body_start_bit = message_start_bit + 8U;
    BitReader reader{
        service_payload,
        body_start_bit,
        selected_bit_length - body_start_bit};
    if (!reader.valid()) {
        return failure(
            ResourceListErrorCode::invalid_input_geometry,
            body_start_bit,
            std::nullopt,
            "Unable to construct the bounded resource-list bit reader");
    }

    const auto count_read = reader.read_bits(kResourceListCountBitWidth);
    if (!count_read) {
        return failure(
            ResourceListErrorCode::truncated_count,
            reader.bit_offset(),
            std::nullopt,
            "Resource-list 12-bit entry count is truncated");
    }
    const auto resource_count = static_cast<std::size_t>(count_read.value);
    if (resource_count == 0U) {
        return failure(
            ResourceListErrorCode::zero_resource_count,
            body_start_bit,
            std::nullopt,
            "The confirmed standard stock profile has no empty resource list");
    }
    if (resource_count > limits_.maximum_resource_count) {
        return failure(
            ResourceListErrorCode::resource_count_limit_exceeded,
            body_start_bit,
            std::nullopt,
            "Resource-list entry count exceeds the configured project bound: count=" +
                std::to_string(resource_count) + " limit=" +
                std::to_string(limits_.maximum_resource_count));
    }

    std::vector<ResourceEntry> entries;
    entries.reserve(resource_count);
    std::array<std::size_t, 5U> type_counts{};
    std::size_t total_name_bytes = 0U;
    std::uint64_t total_size_code_sum = 0U;

    for (std::size_t entry_index = 0U; entry_index < resource_count;
         ++entry_index) {
        const auto entry_start_bit = reader.bit_offset();
        const auto type_read = reader.read_bits(kResourceTypeBitWidth);
        if (!type_read) {
            return failure(
                ResourceListErrorCode::truncated_entry,
                reader.bit_offset(),
                entry_index,
                "Resource-list entry type is truncated");
        }
        const auto type = decode_resource_type(type_read.value);
        if (!type) {
            return failure(
                ResourceListErrorCode::unsupported_resource_type,
                entry_start_bit,
                entry_index,
                "Resource-list entry type is outside the confirmed standard profile");
        }

        const auto name_start_bit = reader.bit_offset();
        std::string name;
        name.reserve((std::min)(
            limits_.maximum_resource_name_length,
            std::size_t{64U}));
        bool terminated = false;
        while (name.size() <= limits_.maximum_resource_name_length) {
            const auto character_start_bit = reader.bit_offset();
            const auto character = reader.read_bits(8U);
            if (!character) {
                return failure(
                    ResourceListErrorCode::unterminated_resource_name,
                    reader.bit_offset(),
                    entry_index,
                    "Resource-list entry name is truncated before its NUL terminator");
            }
            if (character.value == 0U) {
                terminated = true;
                break;
            }
            if (name.size() == limits_.maximum_resource_name_length) {
                return failure(
                    ResourceListErrorCode::resource_name_too_long,
                    character_start_bit,
                    entry_index,
                    "Resource-list entry name exceeds the configured byte bound");
            }
            name.push_back(static_cast<char>(character.value));
        }
        if (!terminated) {
            return failure(
                ResourceListErrorCode::unterminated_resource_name,
                reader.bit_offset(),
                entry_index,
                "Resource-list entry name has no bounded NUL terminator");
        }
        std::size_t candidate_name_bytes = 0U;
        if (!checked_add_size(
                total_name_bytes,
                name.size(),
                candidate_name_bytes)) {
            return failure(
                ResourceListErrorCode::size_overflow,
                name_start_bit,
                entry_index,
                "Resource-list cumulative name size overflowed");
        }
        if (candidate_name_bytes >
            limits_.maximum_resource_total_name_bytes) {
            return failure(
                ResourceListErrorCode::total_name_bytes_limit_exceeded,
                name_start_bit,
                entry_index,
                "Resource-list cumulative name bytes exceed the configured bound");
        }

        const auto index_start_bit = reader.bit_offset();
        const auto index_read = reader.read_bits(kResourceIndexBitWidth);
        const auto size_read = reader.read_bits(kResourceDeclaredSizeBitWidth);
        const auto flags_read = reader.read_bits(kResourceFlagsBitWidth);
        if (!index_read || !size_read || !flags_read) {
            return failure(
                ResourceListErrorCode::truncated_entry,
                index_start_bit,
                entry_index,
                "Resource-list entry index, declared size, or flags are truncated");
        }
        if (size_read.value > limits_.maximum_resource_declared_size) {
            return failure(
                ResourceListErrorCode::resource_declared_size_limit_exceeded,
                index_start_bit + kResourceIndexBitWidth,
                entry_index,
                "Resource-list declared-size metadata exceeds the configured bound");
        }

        const auto flags = static_cast<std::uint8_t>(flags_read.value);
        if ((flags &
             static_cast<std::uint8_t>(
                 ~kMaximumSupportedStandardResourceFlagsMask)) != 0U) {
            return failure(
                ResourceListErrorCode::unsupported_resource_profile,
                reader.bit_offset() - kResourceFlagsBitWidth,
                entry_index,
                "Resource-list flags/profile slot is outside the observed standard values; optional layout remains pending");
        }
        if (flags > limits_.maximum_resource_flags) {
            return failure(
                ResourceListErrorCode::unsupported_resource_flags,
                reader.bit_offset() - kResourceFlagsBitWidth,
                entry_index,
                "Resource-list flags are outside the confirmed raw masks 0 and 1");
        }

        const auto duplicate = std::find_if(
            entries.begin(),
            entries.end(),
            [type = *type, index = index_read.value](
                const ResourceEntry& entry) noexcept {
                return entry.type() == type &&
                       entry.index().value() == index;
            });
        if (duplicate != entries.end() &&
            resource_has_bindable_index(
                *type, static_cast<std::uint16_t>(index_read.value))) {
            return failure(
                ResourceListErrorCode::duplicate_resource_identity,
                index_start_bit,
                entry_index,
                "Resource-list repeats (type,index): type=" +
                    std::to_string(type_read.value) + " index=" +
                    std::to_string(index_read.value) + " first_ordinal=" +
                    std::to_string(static_cast<std::size_t>(
                        duplicate - entries.begin())) + " repeated_ordinal=" +
                    std::to_string(entry_index));
        }

        std::uint64_t candidate_size_code_sum = 0U;
        if (!checked_add_u64(
                total_size_code_sum,
                static_cast<std::uint64_t>(size_read.value),
                candidate_size_code_sum)) {
            return failure(
                ResourceListErrorCode::size_overflow,
                index_start_bit + kResourceIndexBitWidth,
                entry_index,
                "Resource-list cumulative declared-size metadata overflowed");
        }
        if (candidate_size_code_sum >
            limits_.maximum_resource_total_declared_size) {
            return failure(
                ResourceListErrorCode::total_declared_size_limit_exceeded,
                index_start_bit + kResourceIndexBitWidth,
                entry_index,
                "Resource-list cumulative declared-size metadata exceeds the configured bound");
        }

        entries.emplace_back(ResourceEntry{
            *type,
            ResourceName{std::move(name)},
            ResourceIndex{static_cast<std::uint16_t>(index_read.value)},
            ResourceByteSize{size_read.value},
            ResourceFlags{flags},
            entry_index,
            entry_start_bit,
            reader.bit_offset(),
        });
        ++type_counts[resource_type_summary_index(*type)];
        total_name_bytes = candidate_name_bytes;
        total_size_code_sum = candidate_size_code_sum;
    }

    // SV_SendConsistencyList / CL_ParseConsistencyInfo: an explicit enabled
    // bit precedes ordered full-list ordinals. A small delta uses five bits;
    // the absolute form uses ten bits (not the resource index's twelve).
    // The no-request form remains wire-identical to the old zero-fill fixture.
    const auto consistency_start_bit = reader.bit_offset();
    const auto consistency_enabled = reader.read_bits(1U);
    if (!consistency_enabled) {
        return failure(
            ResourceListErrorCode::truncated_file_consistency,
            consistency_start_bit,
            std::nullopt,
            "Resource-list file-consistency enabled bit is truncated");
    }
    std::vector<std::uint16_t> consistency_ordinals;
    if (consistency_enabled.value != 0U) {
        consistency_ordinals.reserve(resource_count);
        std::uint32_t last_ordinal = 0U;
        for (;;) {
            const auto request_start_bit = reader.bit_offset();
            const auto more = reader.read_bits(1U);
            if (!more) {
                return failure(
                    ResourceListErrorCode::truncated_file_consistency,
                    request_start_bit,
                    std::nullopt,
                    "Resource-list file-consistency terminator is truncated: requests=" +
                        std::to_string(consistency_ordinals.size()));
            }
            if (more.value == 0U) {
                break;
            }
            if (consistency_ordinals.size() >= resource_count) {
                return failure(
                    ResourceListErrorCode::file_consistency_request_limit_exceeded,
                    request_start_bit,
                    std::nullopt,
                    "Resource-list file-consistency request count exceeds resource count: requests=" +
                        std::to_string(consistency_ordinals.size()) + " count=" +
                        std::to_string(resource_count));
            }
            const auto is_delta = reader.read_bits(1U);
            if (!is_delta) {
                return failure(
                    ResourceListErrorCode::truncated_file_consistency,
                    reader.bit_offset(),
                    std::nullopt,
                    "Resource-list file-consistency ordinal mode is truncated");
            }
            const auto ordinal_start_bit = reader.bit_offset();
            const auto encoded_ordinal = reader.read_bits(
                is_delta.value != 0U ? 5U : 10U);
            if (!encoded_ordinal) {
                return failure(
                    ResourceListErrorCode::truncated_file_consistency,
                    ordinal_start_bit,
                    std::nullopt,
                    "Resource-list file-consistency ordinal is truncated: delta=" +
                        std::to_string(is_delta.value));
            }
            const auto ordinal = is_delta.value != 0U
                ? last_ordinal + encoded_ordinal.value
                : encoded_ordinal.value;
            if (ordinal >= resource_count) {
                return failure(
                    ResourceListErrorCode::invalid_file_consistency_ordinal,
                    ordinal_start_bit,
                    std::nullopt,
                    "Resource-list file-consistency ordinal is outside the list: ordinal=" +
                        std::to_string(ordinal) + " count=" +
                        std::to_string(resource_count));
            }
            if (!consistency_ordinals.empty() && ordinal <= last_ordinal) {
                return failure(
                    ResourceListErrorCode::unordered_file_consistency_ordinal,
                    ordinal_start_bit,
                    std::nullopt,
                    "Resource-list file-consistency ordinal repeats or moves backwards: previous=" +
                        std::to_string(last_ordinal) + " ordinal=" +
                        std::to_string(ordinal));
            }
            consistency_ordinals.push_back(static_cast<std::uint16_t>(ordinal));
            last_ordinal = ordinal;
        }
    }

    const auto mandatory_padding_width = (8U - (reader.bit_offset() & 7U)) & 7U;
    const auto padding_start_bit = reader.bit_offset();
    const auto padding = reader.read_bits(mandatory_padding_width);
    if (!padding) {
        return failure(
            ResourceListErrorCode::truncated_padding,
            padding_start_bit,
            std::nullopt,
            "Resource-list post-consistency byte-alignment padding is truncated");
    }
    if (padding.value != 0U) {
        return failure(
            ResourceListErrorCode::nonzero_padding,
            padding_start_bit,
            std::nullopt,
            "Resource-list terminal has non-zero data: count=" +
                std::to_string(resource_count) + " start_bit=" +
                std::to_string(padding_start_bit) + " width=" +
                std::to_string(mandatory_padding_width) + " value=" +
                std::to_string(padding.value) + " remaining_bits=" +
                std::to_string(reader.remaining_bits()));
    }
    if (!reader.byte_aligned()) {
        return failure(
            ResourceListErrorCode::size_overflow,
            reader.bit_offset(),
            std::nullopt,
            "Resource-list terminal cursor did not become byte aligned");
    }
    if (reader.remaining_bits() != 0U) {
        return failure(
            ResourceListErrorCode::unexpected_trailing_data,
            reader.bit_offset(),
            std::nullopt,
            "Bytes or bits follow the exact standard resource-list terminator");
    }

    const auto bits_consumed = reader.bit_offset() - message_start_bit;
    if ((bits_consumed & 7U) != 0U) {
        return failure(
            ResourceListErrorCode::size_overflow,
            reader.bit_offset(),
            std::nullopt,
            "Resource-list consumed-bit count is not byte aligned");
    }
    const auto bytes_consumed = bits_consumed / 8U;
    const auto next_byte_offset = reader.bit_offset() / 8U;

    return ResourceListParseResult{
        ResourceListState{
            std::move(entries),
            ResourceTypeSummary{type_counts},
            total_size_code_sum,
            total_name_bytes,
            consistency_enabled.value != 0U,
            std::move(consistency_ordinals),
            opcode_byte_offset,
            selected_bit_length,
            bits_consumed,
            bytes_consumed,
            next_byte_offset,
            0U,
            profile_,
        },
        std::nullopt,
        bits_consumed,
        bytes_consumed,
        next_byte_offset,
        0U,
    };
}

PostResourceListBoundary::PostResourceListBoundary(
    const std::size_t byte_offset,
    const std::size_t bit_offset,
    const std::size_t source_payload_bit_length,
    const std::size_t source_opcode_byte_offset) noexcept
    : byte_offset_{byte_offset},
      bit_offset_{bit_offset},
      source_payload_bit_length_{source_payload_bit_length},
      source_opcode_byte_offset_{source_opcode_byte_offset}
{
}

PostResourceListBoundaryKind PostResourceListBoundary::kind() const noexcept
{
    return PostResourceListBoundaryKind::exact_end_of_payload;
}

std::size_t PostResourceListBoundary::byte_offset() const noexcept
{
    return byte_offset_;
}

std::size_t PostResourceListBoundary::bit_offset() const noexcept
{
    return bit_offset_;
}

std::size_t PostResourceListBoundary::remaining_byte_count() const noexcept
{
    return 0U;
}

std::size_t PostResourceListBoundary::source_payload_bit_length() const noexcept
{
    return source_payload_bit_length_;
}

std::size_t PostResourceListBoundary::source_opcode_byte_offset() const noexcept
{
    return source_opcode_byte_offset_;
}

PostResourceListEvidenceStatus
PostResourceListBoundary::evidence_status() const noexcept
{
    return PostResourceListEvidenceStatus::repeated_stock_exact_end_of_payload;
}

ResourceClientResponseBoundary::ResourceClientResponseBoundary(
    const std::size_t trigger_byte_offset,
    const std::size_t trigger_bit_offset) noexcept
    : trigger_byte_offset_{trigger_byte_offset},
      trigger_bit_offset_{trigger_bit_offset}
{
}

ResourceClientResponseActionKind
ResourceClientResponseBoundary::action_kind() const noexcept
{
    return ResourceClientResponseActionKind::
        stock_response_required_semantics_pending;
}

std::uint8_t ResourceClientResponseBoundary::opcode_candidate() const noexcept
{
    return kResourceClientResponseOpcodeCandidate;
}

std::size_t ResourceClientResponseBoundary::trigger_byte_offset() const noexcept
{
    return trigger_byte_offset_;
}

std::size_t ResourceClientResponseBoundary::trigger_bit_offset() const noexcept
{
    return trigger_bit_offset_;
}

ResourceClientResponseEvidenceStatus
ResourceClientResponseBoundary::evidence_status() const noexcept
{
    return ResourceClientResponseEvidenceStatus::
        stock_fragmented_reliable_opcode_five_semantics_pending;
}

PostResourceListStreamState::PostResourceListStreamState(
    PostResourceListBoundary boundary,
    ResourceClientResponseBoundary client_response) noexcept
    : boundary_{std::move(boundary)},
      client_response_{std::move(client_response)}
{
}

const PostResourceListBoundary&
PostResourceListStreamState::boundary() const noexcept
{
    return boundary_;
}

const ResourceClientResponseBoundary&
PostResourceListStreamState::client_response() const noexcept
{
    return client_response_;
}

PostResourceListStreamDecodeResult PostResourceListStreamDecoder::decode(
    const std::span<const std::byte> service_payload,
    const ResourceListState& resource_list,
    const std::size_t service_payload_bit_length) const
{
    if (service_payload.size() >
        (std::numeric_limits<std::size_t>::max)() / 8U) {
        return post_failure(
            PostResourceListStreamErrorCode::invalid_input_geometry,
            0U,
            "Post-resource-list source payload bit geometry overflowed");
    }
    const auto available_bits = service_payload.size() * 8U;
    const auto selected_bit_length =
        service_payload_bit_length == static_cast<std::size_t>(-1)
            ? available_bits
            : service_payload_bit_length;
    if (selected_bit_length > available_bits) {
        return post_failure(
            PostResourceListStreamErrorCode::invalid_input_geometry,
            available_bits,
            "Post-resource-list bit limit is outside the source payload");
    }
    if (resource_list.compatibility_profile() !=
            ResourceListCompatibilityProfile::
                stock_protocol_48_build_10210_standard ||
        resource_list.source_payload_bit_length() != selected_bit_length) {
        return post_failure(
            PostResourceListStreamErrorCode::incompatible_resource_list_state,
            0U,
            "Post-list continuation does not match the parsed source geometry/profile");
    }
    if (resource_list.next_byte_offset() >
        (std::numeric_limits<std::size_t>::max)() / 8U) {
        return post_failure(
            PostResourceListStreamErrorCode::size_overflow,
            0U,
            "Post-resource-list cursor bit geometry overflowed");
    }
    const auto next_base_bit = resource_list.next_byte_offset() * 8U;
    std::size_t next_absolute_bit = 0U;
    if (!checked_add_size(
            next_base_bit,
            resource_list.next_bit_offset(),
            next_absolute_bit)) {
        return post_failure(
            PostResourceListStreamErrorCode::size_overflow,
            next_base_bit,
            "Post-resource-list cursor overflowed");
    }
    if (resource_list.next_bit_offset() != 0U ||
        next_absolute_bit != selected_bit_length) {
        return post_failure(
            PostResourceListStreamErrorCode::list_not_at_end_of_payload,
            next_absolute_bit,
            "Confirmed standard resource list does not end at the exact payload boundary");
    }

    return PostResourceListStreamDecodeResult{
        PostResourceListStreamState{
            PostResourceListBoundary{
                resource_list.next_byte_offset(),
                resource_list.next_bit_offset(),
                resource_list.source_payload_bit_length(),
                resource_list.source_opcode_byte_offset(),
            },
            ResourceClientResponseBoundary{
                resource_list.next_byte_offset(),
                resource_list.next_bit_offset(),
            },
        },
        std::nullopt,
        2U,
    };
}

} // namespace hlclient::goldsrc
