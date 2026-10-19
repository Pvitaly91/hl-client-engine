#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace hlclient::app {

// A deliberately small success memo for application-side model preparation.
// It stores metadata only: callers still own asset lookup and binding, and may
// remember a key only after the complete ordered list resolved successfully.
class ModelPreparationCache final {
public:
  static constexpr std::size_t maximum_names = 8U;
  static constexpr std::size_t maximum_name_characters = 128U;

  [[nodiscard]] bool matches(
      const void* asset_owner, std::uint64_t generation,
      std::uint64_t reset_serial,
      std::span<const std::string_view> names) const noexcept {
    if(!valid_ || asset_owner_!=asset_owner || generation_!=generation ||
        reset_serial_!=reset_serial || count_!=names.size() ||
        !bounded(names)) return false;
    for(std::size_t i=0;i<count_;++i)
      if(names_[i]!=names[i]) return false;
    return true;
  }

  [[nodiscard]] bool remember(
      const void* asset_owner, std::uint64_t generation,
      std::uint64_t reset_serial,
      std::span<const std::string_view> names) {
    if(!bounded(names)) {
      invalidate();
      return false;
    }

    // Construct every owning string before changing the live key. An
    // allocation exception therefore leaves the previous successful key
    // intact; invalid input is intentionally a cache boundary.
    std::array<std::string,maximum_names> next_names;
    for(std::size_t i=0;i<names.size();++i)
      next_names[i]=names[i];

    names_=std::move(next_names);
    asset_owner_=asset_owner;
    generation_=generation;
    reset_serial_=reset_serial;
    count_=names.size();
    valid_=true;
    return true;
  }

  void invalidate() noexcept {
    valid_=false;
    asset_owner_=nullptr;
    generation_=reset_serial_=0U;
    count_=0U;
    for(auto& name:names_) name.clear();
  }

private:
  [[nodiscard]] static bool bounded(
      std::span<const std::string_view> names) noexcept {
    if(names.size()>maximum_names) return false;
    for(const auto name:names)
      if(name.size()>maximum_name_characters) return false;
    return true;
  }

  std::array<std::string,maximum_names> names_;
  const void* asset_owner_{};
  std::uint64_t generation_{},reset_serial_{};
  std::size_t count_{};
  bool valid_{};
};

} // namespace hlclient::app
