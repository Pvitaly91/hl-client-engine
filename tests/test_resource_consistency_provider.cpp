#include <hlclient/resource_consistency/provider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

namespace {

namespace consistency = hlclient::resource_consistency;

[[nodiscard]] std::array<std::byte, 16U> synthetic_material()
{
    return {
        std::byte{0xa0U}, std::byte{0xa1U}, std::byte{0xa2U}, std::byte{0xa3U},
        std::byte{0xa4U}, std::byte{0xa5U}, std::byte{0xa6U}, std::byte{0xa7U},
        std::byte{0xa8U}, std::byte{0xa9U}, std::byte{0xaaU}, std::byte{0xabU},
        std::byte{0xacU}, std::byte{0xadU}, std::byte{0xaeU}, std::byte{0xafU},
    };
}

class CountingLifetime final
    : public consistency::IResourceConsistencySessionLifetime {
public:
    explicit CountingLifetime(std::size_t& releases) noexcept
        : releases_{releases}
    {
    }

    ~CountingLifetime() override { ++releases_; }

private:
    std::size_t& releases_;
};

class FakeOperation final : public consistency::ResourceConsistencyOperation {
public:
    enum class Outcome { success, failure, remain_pending };

    FakeOperation(
        Outcome outcome,
        std::size_t pending_updates,
        std::size_t& cancellations,
        std::size_t& releases)
        : outcome_{outcome},
          pending_updates_{pending_updates},
          cancellations_{cancellations},
          releases_{releases}
    {
    }

    [[nodiscard]] consistency::ResourceConsistencyUpdateResult update() override
    {
        if (cancelled_) {
            return consistency::ResourceConsistencyUpdateResult::failed({
                consistency::ResourceConsistencyErrorCode::cancelled,
                "cancelled",
            });
        }
        if (pending_updates_ > 0U) {
            --pending_updates_;
            return consistency::ResourceConsistencyUpdateResult::pending();
        }
        if (outcome_ == Outcome::remain_pending) {
            return consistency::ResourceConsistencyUpdateResult::pending();
        }
        if (outcome_ == Outcome::failure) {
            return consistency::ResourceConsistencyUpdateResult::failed({
                consistency::ResourceConsistencyErrorCode::provider_error,
                "synthetic provider failure",
            });
        }
        auto bytes = synthetic_material();
        auto created = consistency::make_resource_consistency_material(
            0x01020304U,
            bytes);
        REQUIRE(created);
        return consistency::ResourceConsistencyUpdateResult::succeeded(
            consistency::ResourceConsistencySession{
                std::move(*created.material),
                std::make_unique<CountingLifetime>(releases_),
            });
    }

    void cancel() noexcept override
    {
        if (!cancelled_) {
            cancelled_ = true;
            ++cancellations_;
        }
    }

private:
    Outcome outcome_;
    std::size_t pending_updates_{0U};
    std::size_t& cancellations_;
    std::size_t& releases_;
    bool cancelled_{false};
};

class FakeProvider final : public consistency::IResourceConsistencyProvider {
public:
    explicit FakeProvider(FakeOperation::Outcome outcome) noexcept
        : outcome_{outcome}
    {
    }

    [[nodiscard]] consistency::ResourceConsistencyBeginResult begin(
        const consistency::ResourceConsistencyRequirements& requirements) override
    {
        ++begin_count;
        observed_material_count = requirements.material_count();
        observed_opaque_bytes = requirements.opaque_bytes_per_material();
        return consistency::ResourceConsistencyBeginResult::started(
            std::make_unique<FakeOperation>(
                outcome_, pending_updates, cancellations, releases));
    }

    FakeOperation::Outcome outcome_;
    std::size_t pending_updates{0U};
    std::size_t begin_count{0U};
    std::size_t cancellations{0U};
    std::size_t releases{0U};
    std::size_t observed_material_count{0U};
    std::size_t observed_opaque_bytes{0U};
};

TEST_CASE("Resource consistency provider boundary is path-free and bounded",
          "[goldsrc][resource-response][provider]")
{
    const auto requirements =
        consistency::ResourceConsistencyRequirements::stock_opcode5_single_resource();
    REQUIRE(requirements);
    CHECK(requirements->material_count() == 1U);
    CHECK(requirements->opaque_bytes_per_material() == 16U);
    CHECK(requirements->compatibility_profile() ==
          consistency::ResourceConsistencyCompatibilityProfile::
              stock_protocol_48_opcode5_single_resource);

    const auto bytes = synthetic_material();
    auto exact = consistency::make_resource_consistency_material(17U, bytes);
    REQUIRE(exact);
    CHECK(exact.material->byte_count() == 17U);
    CHECK(exact.material->opaque_byte_count() == bytes.size());

    std::array<std::byte, 17U> too_large{};
    auto rejected = consistency::make_resource_consistency_material(
        17U,
        too_large);
    CHECK_FALSE(rejected);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code ==
          consistency::ResourceConsistencyErrorCode::material_too_large);
    CHECK(rejected.error->context.find("a0") == std::string::npos);

    consistency::ResourceConsistencyLimits hard_cap;
    hard_cap.maximum_material_count =
        consistency::kMaximumResourceConsistencyMaterials;
    hard_cap.maximum_opaque_bytes_per_material =
        consistency::kMaximumResourceConsistencyOpaqueBytes;
    CHECK(consistency::valid_resource_consistency_limits(hard_cap));
    ++hard_cap.maximum_opaque_bytes_per_material;
    CHECK_FALSE(consistency::valid_resource_consistency_limits(hard_cap));
}

TEST_CASE("Resource consistency operation publishes one owning move-only session",
          "[goldsrc][resource-response][provider]")
{
    FakeProvider provider{FakeOperation::Outcome::success};
    provider.pending_updates = 1U;
    const auto requirements =
        consistency::ResourceConsistencyRequirements::stock_opcode5_single_resource();
    REQUIRE(requirements);

    auto begun = provider.begin(*requirements);
    REQUIRE(begun);
    REQUIRE(begun.operation);
    CHECK(provider.begin_count == 1U);
    CHECK(provider.observed_material_count == 1U);
    CHECK(provider.observed_opaque_bytes == 16U);

    auto pending = begun.operation->update();
    CHECK(pending.state == consistency::ResourceConsistencyUpdateState::pending);
    CHECK_FALSE(pending.session);
    CHECK_FALSE(pending.error);

    auto completed = begun.operation->update();
    CHECK(completed.state ==
          consistency::ResourceConsistencyUpdateState::succeeded);
    REQUIRE(completed.session);
    CHECK(completed.session->has_material());
    CHECK(completed.session->opaque_byte_count() == 16U);
    auto moved = std::move(*completed.session);
    CHECK(moved.has_material());
    CHECK_FALSE(completed.session->has_material());
    auto material = moved.take_material();
    REQUIRE(material);
    CHECK(material->byte_count() == 0x01020304U);
    CHECK_FALSE(moved.has_material());
    CHECK(provider.releases == 0U);
    material.reset();
    CHECK(provider.releases == 0U);
    moved = consistency::ResourceConsistencySession{
        std::move(*consistency::make_resource_consistency_material(
            1U, synthetic_material()).material)};
    CHECK(provider.releases == 1U);
}

TEST_CASE("Resource consistency failures, cancellation, and pending are typed",
          "[goldsrc][resource-response][provider]")
{
    const auto requirements =
        consistency::ResourceConsistencyRequirements::stock_opcode5_single_resource();
    REQUIRE(requirements);

    SECTION("provider failure")
    {
        FakeProvider provider{FakeOperation::Outcome::failure};
        auto begun = provider.begin(*requirements);
        REQUIRE(begun.operation);
        auto result = begun.operation->update();
        CHECK(result.state == consistency::ResourceConsistencyUpdateState::failed);
        REQUIRE(result.error);
        CHECK(result.error->code ==
              consistency::ResourceConsistencyErrorCode::provider_error);
        CHECK_FALSE(result.session);
    }

    SECTION("pending has no implicit production wait")
    {
        FakeProvider provider{FakeOperation::Outcome::remain_pending};
        auto begun = provider.begin(*requirements);
        REQUIRE(begun.operation);
        for (std::size_t iteration = 0U; iteration < 4U; ++iteration) {
            const auto result = begun.operation->update();
            CHECK(result.state ==
                  consistency::ResourceConsistencyUpdateState::pending);
        }
    }

    SECTION("cancellation is idempotent")
    {
        FakeProvider provider{FakeOperation::Outcome::remain_pending};
        auto begun = provider.begin(*requirements);
        REQUIRE(begun.operation);
        begun.operation->cancel();
        begun.operation->cancel();
        CHECK(provider.cancellations == 1U);
        auto result = begun.operation->update();
        REQUIRE(result.error);
        CHECK(result.error->code ==
              consistency::ResourceConsistencyErrorCode::cancelled);
    }

    SECTION("unavailable provider is explicit")
    {
        auto failed = consistency::ResourceConsistencyBeginResult::failed({
            consistency::ResourceConsistencyErrorCode::unavailable,
            "no production provider",
        });
        CHECK_FALSE(failed);
        CHECK_FALSE(failed.operation);
        REQUIRE(failed.error);
        CHECK(failed.error->code ==
              consistency::ResourceConsistencyErrorCode::unavailable);
    }
}

static_assert(!std::is_copy_constructible_v<
              consistency::ResourceConsistencySession>);
static_assert(std::is_move_constructible_v<
              consistency::ResourceConsistencySession>);

} // namespace
