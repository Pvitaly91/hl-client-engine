#include <hlclient/app/model_preparation_cache.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace {

using hlclient::app::ModelPreparationCache;

TEST_CASE("model preparation cache matches only the complete ordered key",
    "[b080-cpu-workload][model-preparation-cache]") {
  ModelPreparationCache cache;
  int first_owner{},second_owner{};
  const std::array names{std::string_view{"models/v_9mmhandgun.mdl"},
                         std::string_view{"models/p_9mmhandgun.mdl"}};

  CHECK_FALSE(cache.matches(&first_owner,7U,3U,names));
  REQUIRE(cache.remember(&first_owner,7U,3U,names));
  CHECK(cache.matches(&first_owner,7U,3U,names));
  CHECK_FALSE(cache.matches(&second_owner,7U,3U,names));
  CHECK_FALSE(cache.matches(&first_owner,8U,3U,names));
  CHECK_FALSE(cache.matches(&first_owner,7U,4U,names));

  const std::array reversed{names[1],names[0]};
  const std::array shorter{names[0]};
  CHECK_FALSE(cache.matches(&first_owner,7U,3U,reversed));
  CHECK_FALSE(cache.matches(&first_owner,7U,3U,shorter));

  cache.invalidate();
  CHECK_FALSE(cache.matches(&first_owner,7U,3U,names));
}

TEST_CASE("model preparation cache owns names and supports an empty resolved list",
    "[b080-cpu-workload][model-preparation-cache]") {
  ModelPreparationCache cache;
  int owner{};
  std::string source="models/original.mdl";
  const std::array borrowed{std::string_view{source}};
  REQUIRE(cache.remember(&owner,1U,2U,borrowed));

  // Mutate in place so the borrowed view remains valid and demonstrably
  // differs from the cache's owning copy.
  source[7]='X';
  const std::array original{std::string_view{"models/original.mdl"}};
  CHECK(cache.matches(&owner,1U,2U,original));
  CHECK_FALSE(cache.matches(&owner,1U,2U,borrowed));

  const std::span<const std::string_view> empty;
  REQUIRE(cache.remember(&owner,1U,3U,empty));
  CHECK(cache.matches(&owner,1U,3U,empty));
}

TEST_CASE("invalid model preparation keys clear a previous success",
    "[b080-cpu-workload][model-preparation-cache]") {
  ModelPreparationCache cache;
  int owner{};
  const std::array valid{std::string_view{"models/valid.mdl"}};
  REQUIRE(cache.remember(&owner,4U,5U,valid));

  std::array<std::string_view,ModelPreparationCache::maximum_names+1U> too_many{};
  CHECK_FALSE(cache.remember(&owner,4U,5U,too_many));
  CHECK_FALSE(cache.matches(&owner,4U,5U,valid));

  REQUIRE(cache.remember(&owner,4U,5U,valid));
  const std::string oversized(ModelPreparationCache::maximum_name_characters+1U,'x');
  const std::array too_long{std::string_view{oversized}};
  CHECK_FALSE(cache.remember(&owner,4U,5U,too_long));
  CHECK_FALSE(cache.matches(&owner,4U,5U,valid));

  // Oversize probes never match and do not revive or replace invalid state.
  CHECK_FALSE(cache.matches(&owner,4U,5U,too_many));
  CHECK_FALSE(cache.matches(&owner,4U,5U,too_long));
}

} // namespace
