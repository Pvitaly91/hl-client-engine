#pragma once
#include <optional>
#include <cstdint>
namespace hlclient::app {
// Read-only harness condition. Never writes world, HUD, prediction or network.
// Caller continues its ordinary pump while pending. A new committed clientdata
// source is required; retained/render frames cannot satisfy the condition.
class TestStartHealthGate final {
public:
  explicit TestStartHealthGate(bool enabled) : ready_(!enabled) {}
  void observe(std::optional<double> health, bool fresh, std::uint64_t revision) {
    if(ready_ || !fresh || !health || revision==0 || revision==last_) return;
    last_=revision; observed_=health; if(*health==50.0) ready_=true;
  }
  [[nodiscard]] bool ready() const noexcept { return ready_; }
  [[nodiscard]] bool expired(double elapsed) const noexcept { return !ready_ && elapsed>=15.0; }
  [[nodiscard]] std::optional<double> observed() const noexcept { return observed_; }
private:
  bool ready_{};
  std::uint64_t last_{};
  std::optional<double> observed_;
};
}
