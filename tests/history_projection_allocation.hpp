#pragma once
#include <cstdint>
namespace history_allocation {
struct Count { std::uint64_t calls{},bytes{}; };
void start() noexcept;
Count stop() noexcept;
}
