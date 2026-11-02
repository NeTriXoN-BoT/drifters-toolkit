#pragma once
#include <atomic>
#include <cstdint>
namespace drifter {
// Native-only. Renderer requests cannot choose an epoch or revive a stopped view.
class RendererEpoch {
    std::atomic<std::uint64_t> value_{1};
public:
    std::uint64_t current() const noexcept { return value_.load(std::memory_order_acquire); }
    std::uint64_t advance() noexcept { return value_.fetch_add(1, std::memory_order_acq_rel) + 1; }
    bool current(std::uint64_t expected) const noexcept { return expected != 0 && expected == current(); }
};
}
