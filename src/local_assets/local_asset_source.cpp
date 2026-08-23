#include <hlclient/local_assets/local_asset_source.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace hlclient::local_assets {

namespace detail {
struct LocalAssetSourceOpenGate final {
    std::atomic_bool active{false};
};
} // namespace detail

namespace {

[[nodiscard]] std::string bounded_context(const std::string_view context)
{
    const auto size =
        (std::min)(context.size(), kLocalAssetSourceDiagnosticTextLimit);
    return std::string{context.data(), size};
}

[[nodiscard]] LocalAssetSourceOpenError make_error(
    const LocalAssetSourceOpenErrorCode code,
    const std::string_view context,
    const std::optional<
        local_resources::LocalResourceLocatorReopenErrorCode> reopen_code =
        std::nullopt,
    const std::optional<local_resources::LocalReadOnlyFileErrorCode>
        read_code = std::nullopt)
{
    return LocalAssetSourceOpenError{
        code, reopen_code, read_code, bounded_context(context)};
}

[[nodiscard]] LocalAssetSourceOpenError reopen_error(
    const local_resources::LocalResourceLocatorReopenError& error)
{
    using ReopenCode =
        local_resources::LocalResourceLocatorReopenErrorCode;
    LocalAssetSourceOpenErrorCode code{
        LocalAssetSourceOpenErrorCode::source_read_failed};
    switch (error.code) {
    case ReopenCode::invalid_locator:
        code = LocalAssetSourceOpenErrorCode::locator_invalid;
        break;
    case ReopenCode::locator_environment_mismatch:
        code = LocalAssetSourceOpenErrorCode::locator_environment_mismatch;
        break;
    case ReopenCode::locator_target_missing:
        code = LocalAssetSourceOpenErrorCode::locator_target_missing;
        break;
    case ReopenCode::stale_locator:
    case ReopenCode::ambiguous_case:
    case ReopenCode::not_regular_file:
    case ReopenCode::reparse_escape:
    case ReopenCode::remote_volume_unsupported:
        code = LocalAssetSourceOpenErrorCode::stale_locator;
        break;
    case ReopenCode::io_error:
        code = LocalAssetSourceOpenErrorCode::source_read_failed;
        break;
    }
    return make_error(code, error.context, error.code);
}

[[nodiscard]] LocalAssetSourceOpenError read_error(
    const local_resources::LocalReadOnlyFileError& error)
{
    const auto code =
        error.code == local_resources::LocalReadOnlyFileErrorCode::state_changed
            ? LocalAssetSourceOpenErrorCode::source_changed_during_read
            : LocalAssetSourceOpenErrorCode::source_read_failed;
    return make_error(code, error.context, std::nullopt, error.code);
}

[[nodiscard]] bool terminal(const LocalAssetSourceOpenState state) noexcept
{
    switch (state) {
    case LocalAssetSourceOpenState::source_ready:
    case LocalAssetSourceOpenState::cancelled:
    case LocalAssetSourceOpenState::timed_out:
    case LocalAssetSourceOpenState::failed: return true;
    case LocalAssetSourceOpenState::opening:
    case LocalAssetSourceOpenState::reading:
    case LocalAssetSourceOpenState::validating:
    case LocalAssetSourceOpenState::idle: return false;
    }
    return true;
}

[[nodiscard]] bool supported_locator_profile(
    const local_resources::LocalResourceLocatorCompatibilityProfile profile)
    noexcept
{
    return profile == local_resources::
                          LocalResourceLocatorCompatibilityProfile::
                              validated_fixed_local_volume_v1;
}

} // namespace

bool valid_local_asset_source_open_limits(
    const LocalAssetSourceOpenLimits& limits) noexcept
{
    const bool timeout_valid =
        !limits.timeout ||
        (*limits.timeout > std::chrono::milliseconds::zero() &&
         *limits.timeout <= kHardMaximumLocalAssetSourceTimeout);
    return limits.maximum_source_bytes > 0U &&
           limits.maximum_source_bytes <= kHardMaximumLocalAssetSourceBytes &&
           limits.read_chunk_bytes > 0U &&
           limits.read_chunk_bytes <=
               kHardMaximumLocalAssetSourceReadChunkBytes &&
           limits.maximum_chunks_per_update > 0U &&
           limits.maximum_chunks_per_update <=
               kHardMaximumLocalAssetSourceChunksPerUpdate &&
           limits.maximum_open_sources ==
               kMaximumSimultaneouslyOpenLocalAssetSources &&
           timeout_valid;
}

LocalAssetSource::LocalAssetSource(
    assets::AssetSource source,
    const local_resources::LocalResourceRootId root_id,
    const local_resources::LocalVirtualResourceId virtual_resource_id,
    const local_resources::LocalStableFileIdentity expected_identity,
    const std::uint64_t byte_count,
    const local_resources::LocalResourceLocatorCompatibilityProfile
        locator_compatibility_profile) noexcept
    : source_{std::move(source)},
      root_id_{root_id},
      virtual_resource_id_{virtual_resource_id},
      expected_identity_{expected_identity},
      byte_count_{byte_count},
      locator_compatibility_profile_{locator_compatibility_profile}
{
}

const assets::AssetSource& LocalAssetSource::source() const noexcept
{
    return source_;
}

local_resources::LocalResourceRootId LocalAssetSource::root_id() const noexcept
{
    return root_id_;
}

local_resources::LocalVirtualResourceId
LocalAssetSource::virtual_resource_id() const noexcept
{
    return virtual_resource_id_;
}

local_resources::LocalStableFileIdentity
LocalAssetSource::expected_identity() const noexcept
{
    return expected_identity_;
}

std::uint64_t LocalAssetSource::byte_count() const noexcept
{
    return byte_count_;
}

local_resources::LocalResourceLocatorCompatibilityProfile
LocalAssetSource::locator_compatibility_profile() const noexcept
{
    return locator_compatibility_profile_;
}

LocalAssetSourceOpenOperation::LocalAssetSourceOpenOperation(
    std::unique_ptr<local_resources::LocalResourceLocator> locator,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    const LocalAssetSourceOpenLimits limits,
    std::shared_ptr<detail::LocalAssetSourceOpenGate> gate) noexcept
    : locator_{std::move(locator)},
      environment_{std::move(environment)},
      limits_{limits},
      gate_{std::move(gate)}
{
}

LocalAssetSourceOpenOperation::~LocalAssetSourceOpenOperation()
{
    close_and_discard_partial_source();
    release_lease();
}

LocalAssetSourceOpenOperation::LocalAssetSourceOpenOperation(
    LocalAssetSourceOpenOperation&& other) noexcept
    : locator_{std::move(other.locator_)},
      environment_{std::move(other.environment_)},
      limits_{other.limits_},
      gate_{std::move(other.gate_)},
      owns_lease_{std::exchange(other.owns_lease_, false)},
      state_{other.state_},
      started_at_{other.started_at_},
      last_update_at_{other.last_update_at_},
      file_{std::move(other.file_)},
      initial_snapshot_{other.initial_snapshot_},
      initial_snapshot_valid_{other.initial_snapshot_valid_},
      source_bytes_{std::move(other.source_bytes_)},
      progress_bytes_{other.progress_bytes_},
      result_{std::move(other.result_)},
      error_{std::move(other.error_)}
{
    other.file_.reset();
    other.result_.reset();
    other.error_.reset();
    other.initial_snapshot_valid_ = false;
    other.progress_bytes_ = 0U;
    other.state_ = LocalAssetSourceOpenState::idle;
}

LocalAssetSourceOpenOperation& LocalAssetSourceOpenOperation::operator=(
    LocalAssetSourceOpenOperation&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    close_and_discard_partial_source();
    release_lease();
    locator_ = std::move(other.locator_);
    environment_ = std::move(other.environment_);
    limits_ = other.limits_;
    gate_ = std::move(other.gate_);
    owns_lease_ = std::exchange(other.owns_lease_, false);
    state_ = other.state_;
    started_at_ = other.started_at_;
    last_update_at_ = other.last_update_at_;
    file_ = std::move(other.file_);
    initial_snapshot_ = other.initial_snapshot_;
    initial_snapshot_valid_ = other.initial_snapshot_valid_;
    source_bytes_ = std::move(other.source_bytes_);
    progress_bytes_ = other.progress_bytes_;
    result_ = std::move(other.result_);
    error_ = std::move(other.error_);

    other.file_.reset();
    other.result_.reset();
    other.error_.reset();
    other.initial_snapshot_valid_ = false;
    other.progress_bytes_ = 0U;
    other.state_ = LocalAssetSourceOpenState::idle;
    return *this;
}

void LocalAssetSourceOpenOperation::release_lease() noexcept
{
    if (owns_lease_ && gate_) {
        gate_->active.store(false, std::memory_order_release);
        owns_lease_ = false;
    }
}

void LocalAssetSourceOpenOperation::close_and_discard_partial_source() noexcept
{
    if (file_) {
        file_->close();
        file_.reset();
    }
    std::vector<std::byte> empty;
    source_bytes_.swap(empty);
    initial_snapshot_valid_ = false;
}

void LocalAssetSourceOpenOperation::fail(LocalAssetSourceOpenError error) noexcept
{
    close_and_discard_partial_source();
    result_.reset();
    try {
        error_.emplace(std::move(error));
    } catch (...) {
        error_.reset();
    }
    state_ = LocalAssetSourceOpenState::failed;
    release_lease();
}

void LocalAssetSourceOpenOperation::fail_without_context(
    const LocalAssetSourceOpenErrorCode code) noexcept
{
    close_and_discard_partial_source();
    result_.reset();
    error_.reset();
    try {
        error_.emplace();
        error_->code = code;
    } catch (...) {
        error_.reset();
    }
    state_ = LocalAssetSourceOpenState::failed;
    release_lease();
}

void LocalAssetSourceOpenOperation::time_out() noexcept
{
    close_and_discard_partial_source();
    result_.reset();
    try {
        error_.emplace(make_error(
            LocalAssetSourceOpenErrorCode::timed_out,
            "Local asset-source opening exceeded its configured timeout"));
    } catch (...) {
        error_.reset();
    }
    state_ = LocalAssetSourceOpenState::timed_out;
    release_lease();
}

void LocalAssetSourceOpenOperation::update(
    const LocalAssetSourceOpenTimePoint now) noexcept
{
    if (terminal(state_)) {
        return;
    }

    try {
        if (!started_at_) {
            started_at_ = now;
            last_update_at_ = now;
        } else {
            if (last_update_at_ && now < *last_update_at_) {
                fail(make_error(
                    LocalAssetSourceOpenErrorCode::invalid_configuration,
                    "Local asset-source operation time moved backwards"));
                return;
            }
            last_update_at_ = now;
            if (limits_.timeout && now - *started_at_ >= *limits_.timeout) {
                time_out();
                return;
            }
        }

        switch (state_) {
        case LocalAssetSourceOpenState::idle:
            fail(make_error(
                LocalAssetSourceOpenErrorCode::invalid_configuration,
                "Idle local asset-source operation has no retained request"));
            break;
        case LocalAssetSourceOpenState::opening: update_opening(); break;
        case LocalAssetSourceOpenState::reading: update_reading(); break;
        case LocalAssetSourceOpenState::validating: update_validating(); break;
        case LocalAssetSourceOpenState::source_ready:
        case LocalAssetSourceOpenState::cancelled:
        case LocalAssetSourceOpenState::timed_out:
        case LocalAssetSourceOpenState::failed: break;
        }
    } catch (const std::bad_alloc&) {
        fail_without_context(
            LocalAssetSourceOpenErrorCode::source_creation_failed);
    } catch (...) {
        fail_without_context(
            LocalAssetSourceOpenErrorCode::source_creation_failed);
    }
}

void LocalAssetSourceOpenOperation::update_opening()
{
    if (!locator_ || !environment_) {
        fail(make_error(
            LocalAssetSourceOpenErrorCode::invalid_configuration,
            "Local asset-source operation prerequisites are unavailable"));
        return;
    }

    auto reopened = environment_->reopen_verified(*locator_);
    if (!reopened || !reopened.file) {
        fail(reopened.error
                 ? reopen_error(*reopened.error)
                 : make_error(
                       LocalAssetSourceOpenErrorCode::source_read_failed,
                       "Verified local asset-source reopen produced no handle"));
        return;
    }
    file_.emplace(std::move(*reopened.file));

    if (!file_->is_open() || !file_->is_regular_file() ||
        file_->bytes_consumed() != 0U ||
        file_->root_id() != locator_->root_id() ||
        file_->virtual_resource_id() != locator_->virtual_name().id() ||
        file_->identity() != locator_->expected_identity() ||
        file_->file_size() != locator_->expected_file_size()) {
        fail(make_error(
            LocalAssetSourceOpenErrorCode::stale_locator,
            "Verified local asset-source handle metadata is inconsistent"));
        return;
    }

    initial_snapshot_ = file_->initial_snapshot();
    initial_snapshot_valid_ = true;
    if (!initial_snapshot_.identity.valid() ||
        initial_snapshot_.identity != locator_->expected_identity() ||
        initial_snapshot_.file_size != locator_->expected_file_size()) {
        fail(make_error(
            LocalAssetSourceOpenErrorCode::stale_locator,
            "Initial local asset-source snapshot is inconsistent"));
        return;
    }

    const auto source_size = initial_snapshot_.file_size;
    if (source_size > limits_.maximum_source_bytes ||
        source_size > environment_->limits().maximum_file_size ||
        source_size > kHardMaximumLocalAssetSourceBytes ||
        source_size >
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::size_t>::max)())) {
        fail(make_error(
            LocalAssetSourceOpenErrorCode::source_too_large,
            "Local asset source exceeds its configured size bound"));
        return;
    }

    source_bytes_.resize(static_cast<std::size_t>(source_size));
    progress_bytes_ = 0U;
    state_ = source_size == 0U ? LocalAssetSourceOpenState::validating
                              : LocalAssetSourceOpenState::reading;
}

void LocalAssetSourceOpenOperation::update_reading()
{
    if (!file_ || !file_->is_open() || !initial_snapshot_valid_ ||
        source_bytes_.size() !=
            static_cast<std::size_t>(initial_snapshot_.file_size) ||
        progress_bytes_ > initial_snapshot_.file_size) {
        fail(make_error(
            LocalAssetSourceOpenErrorCode::source_read_failed,
            "Local asset-source read state is inconsistent"));
        return;
    }

    std::size_t chunks = 0U;
    while (chunks < limits_.maximum_chunks_per_update &&
           progress_bytes_ < initial_snapshot_.file_size) {
        const auto remaining = initial_snapshot_.file_size - progress_bytes_;
        const auto requested = static_cast<std::size_t>((std::min)(
            remaining, static_cast<std::uint64_t>(limits_.read_chunk_bytes)));
        auto destination = std::span<std::byte>{source_bytes_}.subspan(
            static_cast<std::size_t>(progress_bytes_), requested);
        const auto read = file_->read_next(destination);
        ++chunks;
        if (!read) {
            fail(read.error
                     ? read_error(*read.error)
                     : make_error(
                           LocalAssetSourceOpenErrorCode::source_read_failed,
                           "Local asset-source read failed without an error"));
            return;
        }
        if (read.bytes_read != requested || read.bytes_read == 0U ||
            progress_bytes_ >
                initial_snapshot_.file_size - read.bytes_read) {
            fail(make_error(
                LocalAssetSourceOpenErrorCode::source_read_failed,
                "Local asset source produced an invalid bounded read"));
            return;
        }
        progress_bytes_ += read.bytes_read;
    }

    if (progress_bytes_ == initial_snapshot_.file_size) {
        // The next update performs the mandatory physical EOF probe. The
        // tracked end-of-file flag returned with the final content read is not
        // accepted as proof that the underlying file did not grow.
        state_ = LocalAssetSourceOpenState::validating;
    }
}

void LocalAssetSourceOpenOperation::update_validating()
{
    if (!file_ || !file_->is_open() || !initial_snapshot_valid_ ||
        progress_bytes_ != initial_snapshot_.file_size ||
        source_bytes_.size() !=
            static_cast<std::size_t>(initial_snapshot_.file_size)) {
        fail(make_error(
            LocalAssetSourceOpenErrorCode::source_read_failed,
            "Local asset-source validation state is inconsistent"));
        return;
    }

    std::array<std::byte, 1U> end_probe{};
    const auto end = file_->read_next(end_probe);
    if (!end) {
        fail(end.error
                 ? read_error(*end.error)
                 : make_error(
                       LocalAssetSourceOpenErrorCode::source_read_failed,
                       "Local asset-source EOF probe failed without an error"));
        return;
    }
    if (!end.end_of_file || end.bytes_read != 0U) {
        fail(make_error(
            LocalAssetSourceOpenErrorCode::source_changed_during_read,
            "Local asset source grew during its bounded read"));
        return;
    }

    const auto final_snapshot = file_->metadata_snapshot();
    if (!final_snapshot || !final_snapshot.snapshot) {
        fail(final_snapshot.error
                 ? read_error(*final_snapshot.error)
                 : make_error(
                       LocalAssetSourceOpenErrorCode::source_read_failed,
                       "Final local asset-source snapshot is unavailable"));
        return;
    }
    if (*final_snapshot.snapshot != initial_snapshot_) {
        fail(make_error(
            LocalAssetSourceOpenErrorCode::source_changed_during_read,
            "Local asset-source metadata changed during its bounded read"));
        return;
    }

    assets::AssetSourceMetadata metadata;
    metadata.content_size = static_cast<std::uintmax_t>(progress_bytes_);
    auto created = assets::AssetSource::create(
        std::filesystem::path{std::string{locator_->virtual_name().value()}},
        std::move(source_bytes_),
        std::move(metadata));
    if (!created || !created.source) {
        fail(make_error(
            LocalAssetSourceOpenErrorCode::source_creation_failed,
            "Approved virtual metadata could not create an asset source"));
        return;
    }

    LocalAssetSource candidate{
        std::move(*created.source),
        locator_->root_id(),
        locator_->virtual_name().id(),
        locator_->expected_identity(),
        progress_bytes_,
        locator_->compatibility_profile()};
    file_->close();
    file_.reset();
    initial_snapshot_valid_ = false;
    result_.emplace(std::move(candidate));
    state_ = LocalAssetSourceOpenState::source_ready;
    release_lease();
}

void LocalAssetSourceOpenOperation::cancel() noexcept
{
    if (terminal(state_)) {
        return;
    }
    close_and_discard_partial_source();
    result_.reset();
    try {
        error_.emplace(make_error(
            LocalAssetSourceOpenErrorCode::cancelled,
            "Local asset-source opening was cancelled"));
    } catch (...) {
        error_.reset();
    }
    state_ = LocalAssetSourceOpenState::cancelled;
    release_lease();
}

LocalAssetSourceOpenState LocalAssetSourceOpenOperation::state() const noexcept
{
    return state_;
}

std::uint64_t LocalAssetSourceOpenOperation::progress_bytes() const noexcept
{
    return progress_bytes_;
}

const LocalAssetSource* LocalAssetSourceOpenOperation::result() const noexcept
{
    return result_ ? &*result_ : nullptr;
}

const LocalAssetSourceOpenError* LocalAssetSourceOpenOperation::error()
    const noexcept
{
    return error_ ? &*error_ : nullptr;
}

std::optional<LocalAssetSource>
LocalAssetSourceOpenOperation::take_result() noexcept
{
    if (state_ != LocalAssetSourceOpenState::source_ready || !result_) {
        return std::nullopt;
    }
    std::optional<LocalAssetSource> result{std::move(*result_)};
    result_.reset();
    return result;
}

LocalAssetSourceOpener::LocalAssetSourceOpener()
    : gate_{std::make_shared<detail::LocalAssetSourceOpenGate>()}
{
}

LocalAssetSourceOpenBeginResult LocalAssetSourceOpener::begin(
    const local_resources::LocalResourceLocator& locator,
    std::shared_ptr<const local_resources::LocalResourceEnvironment>
        environment,
    const LocalAssetSourceOpenLimits limits)
{
    if (!gate_ || !environment || environment->root_count() == 0U ||
        !valid_local_asset_source_open_limits(limits) ||
        !locator.root_id().valid() || !locator.expected_identity().valid() ||
        locator.virtual_name().value().empty() ||
        !supported_locator_profile(locator.compatibility_profile())) {
        return LocalAssetSourceOpenBeginResult{
            std::nullopt,
            make_error(
                LocalAssetSourceOpenErrorCode::invalid_configuration,
                "Local asset-source opener configuration is invalid")};
    }

    bool expected = false;
    if (!gate_->active.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return LocalAssetSourceOpenBeginResult{
            std::nullopt,
            make_error(
                LocalAssetSourceOpenErrorCode::open_source_limit_reached,
                "The local asset-source opener already has an active operation")};
    }

    try {
        auto retained_locator =
            std::make_unique<local_resources::LocalResourceLocator>(locator);
        return LocalAssetSourceOpenBeginResult{
            std::optional<LocalAssetSourceOpenOperation>{
                LocalAssetSourceOpenOperation{
                    std::move(retained_locator),
                    std::move(environment),
                    limits,
                    gate_}},
            std::nullopt};
    } catch (...) {
        gate_->active.store(false, std::memory_order_release);
        return LocalAssetSourceOpenBeginResult{
            std::nullopt,
            make_error(
                LocalAssetSourceOpenErrorCode::source_creation_failed,
                "Unable to retain bounded local asset-source operation state")};
    }
}

} // namespace hlclient::local_assets
