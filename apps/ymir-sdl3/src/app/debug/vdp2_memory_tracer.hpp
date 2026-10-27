#pragma once

#include <ymir/debug/vdp2_memory_tracer_base.hpp>

#include <atomic>
#include <deque>
#include <mutex>

namespace app {

struct VDP2MemoryTracer final : ymir::debug::IVDP2MemoryTracer {
    struct Entry {
        uint64 vdp1Frame;
        ymir::debug::VDP2MemoryType type;
        uint32 address;
        uint8 width;
        uint32 value;
    };

    static constexpr size_t kDefaultCapacity = 250'000;

    std::atomic_bool enabled{false};
    std::atomic_bool traceVRAM{true};
    std::atomic_bool traceCRAM{true};
    std::atomic_uint32_t addressBegin{0};
    std::atomic_uint32_t addressEnd{0x80000};
    std::atomic_size_t capacity{kDefaultCapacity};
    std::atomic_bool traceVdp1FrameRange{false};
    std::atomic_uint64_t vdp1FrameBegin{0};
    std::atomic_uint64_t vdp1FrameEnd{0};
    std::atomic_uint64_t outOfFrameRange = 0;
    mutable std::mutex mutex;
    std::deque<Entry> entries;
    uint64 dropped = 0;

    bool IsEnabled() const final {
        return enabled.load(std::memory_order_relaxed);
    }

    void Reset() final {
        Clear();
    }

    void Write(uint64 vdp1Frame, ymir::debug::VDP2MemoryType type, uint32 address, uint8 width, uint32 value) final {
        if ((type == ymir::debug::VDP2MemoryType::VRAM && !traceVRAM.load(std::memory_order_relaxed)) ||
            (type == ymir::debug::VDP2MemoryType::CRAM && !traceCRAM.load(std::memory_order_relaxed))) {
            return;
        }
        const uint32 begin = addressBegin.load(std::memory_order_relaxed);
        const uint32 end = addressEnd.load(std::memory_order_relaxed);
        const uint64 writeEnd = static_cast<uint64>(address) + width;
        if (begin >= end || address >= end || writeEnd <= begin) {
            return;
        }
        if (traceVdp1FrameRange.load(std::memory_order_relaxed)) {
            const uint64 frameBegin = vdp1FrameBegin.load(std::memory_order_relaxed);
            const uint64 frameEnd = vdp1FrameEnd.load(std::memory_order_relaxed);
            if (vdp1Frame == ymir::debug::IVDP2MemoryTracer::kUnknownVdp1FrameIndex || vdp1Frame < frameBegin ||
                vdp1Frame >= frameEnd) {
                outOfFrameRange.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        std::lock_guard lock(mutex);
        if (entries.size() >= capacity.load(std::memory_order_relaxed)) {
            entries.pop_front();
            ++dropped;
        }
        entries.push_back({vdp1Frame, type, address, width, value});
    }

    void Clear() {
        std::lock_guard lock(mutex);
        entries.clear();
        dropped = 0;
        outOfFrameRange.store(0, std::memory_order_relaxed);
    }
};

} // namespace app
