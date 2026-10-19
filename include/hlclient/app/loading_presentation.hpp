#pragma once
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace hlclient::app {
// Independent automatic visibility owner. A full panel travels at the same
// viewport-units/second speed as the 180 ms half-console; no scene/FOV scaling.
class LoadingCurtain final {
public:
    void reset(double now) noexcept { height_=1;last_=now;ready_=false; }
    void presented(double now) noexcept { if(!ready_){last_=now;ready_=true;} }
    void advance(double now) noexcept {
        if(!std::isfinite(now)||now<last_)return;
        if(ready_)height_=std::max(0.0,height_-(now-last_)*(.5/.18));
        last_=now;
    }
    [[nodiscard]] double fraction() const noexcept { return height_; }
private:
    double height_{1},last_{};
    bool ready_{};
};
enum class LoadingStage : std::uint8_t { idle, connecting, authenticating, signon, resources, map, textures, models, scene, ready, disconnecting, failed };
// Shared only as a bounded mailbox. Worker results remain owned by their future;
// cancellation never transfers a partially built resource to the GL thread.
struct LoadingWork final {
    explicit LoadingWork(std::uint64_t value) : generation(value) {}
    const std::uint64_t generation;
    std::atomic<bool> cancelled{};
    std::atomic<LoadingStage> stage{LoadingStage::resources};
    std::atomic<std::uint32_t> done{}, total{};
    std::atomic<std::uint64_t> revision{};
    bool progress(LoadingStage next, std::uint32_t completed=0, std::uint32_t count=0) noexcept {
        if(cancelled.load())return false;
        revision.fetch_add(1);done.store(completed);stage.store(next);total.store(count);revision.fetch_add(1);
        return true;
    }
};
struct LoadingPresentation final {
    std::uint64_t generation{};
    LoadingStage stage{LoadingStage::idle};
    std::string error;
    std::optional<unsigned> percent;
    void begin(std::uint64_t next) {generation=next;stage=LoadingStage::connecting;error.clear();percent.reset();}
    bool observe(const LoadingWork& work) noexcept {
        if(work.generation!=generation||work.cancelled.load())return false;
        const auto version=work.revision.load();if(version&1U)return false;
        const auto next_stage=work.stage.load();const auto total=work.total.load(),done=work.done.load();
        if(work.revision.load()!=version||work.cancelled.load())return false;
        stage=next_stage;
        percent=total&&done<=total ? std::optional{static_cast<unsigned>(100ULL*done/total)}:std::nullopt;
        if(percent&&*percent==100U&&stage!=LoadingStage::ready)percent=99U;
        return true;
    }
};
[[nodiscard]] inline const char* loading_stage_label(LoadingStage stage) noexcept {
    switch(stage) {
    case LoadingStage::idle:return "Ready to connect";
    case LoadingStage::connecting:return "Connecting...";
    case LoadingStage::authenticating:return "Authenticating...";
    case LoadingStage::signon:return "Receiving server state...";
    case LoadingStage::resources:return "Checking resources...";
    case LoadingStage::map:return "Loading map...";
    case LoadingStage::textures:return "Loading textures...";
    case LoadingStage::models:return "Loading models...";
    case LoadingStage::scene:return "Preparing scene...";
    case LoadingStage::ready:return "Scene ready";
    case LoadingStage::disconnecting:return "Disconnecting...";
    case LoadingStage::failed:return "Connection failed";
    }
    return "";
}
}
