#pragma once

#include <ymir/debug/vdp_tracer_base.hpp>

#include <array>
#include <atomic>
#include <deque>
#include <mutex>

namespace app {

struct VDPTracer final : ymir::debug::IVDPTracer {
    struct Entry {
        uint64 frame;
        uint32 index;
        uint32 address;
        std::array<uint16, 16> words;
    };

    static constexpr size_t kCapacity = 1'000'000;
    std::atomic_bool enabled{false};
    mutable std::mutex mutex;
    std::deque<Entry> entries;
    uint64 dropped = 0;

    bool IsEnabled() const final { return enabled.load(std::memory_order_relaxed); }
    void Command(uint64 frame, uint32 index, uint32 address, const std::array<uint16, 16> &words) final {
        std::lock_guard lock(mutex);
        if (entries.size() == kCapacity) {
            entries.pop_front();
            ++dropped;
        }
        entries.push_back({frame, index, address, words});
    }
    void Clear() {
        std::lock_guard lock(mutex);
        entries.clear();
        dropped = 0;
    }
};

} // namespace app
