#pragma once

#include <ymir/debug/vdp2_memory_tracer_base.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <span>
#include <vector>

namespace app {

struct VDP2MemoryTracer final : ymir::debug::IVDP2MemoryTracer {
    struct Entry {
        uint64 vdp1Frame;
        uint64 sequence;
        bool isScanline;
        uint32 row;
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
    std::atomic_uint64_t filterRevision{0};
    std::atomic_uint64_t vdp1FrameBegin{0};
    std::atomic_uint64_t vdp1FrameEnd{0};
    mutable std::atomic_uint64_t outOfFrameRange = 0;
    mutable std::mutex mutex;
    std::vector<Entry> entries;
    std::vector<uint8> initialVRAM;
    std::vector<uint8> initialCRAM;
    bool initialSnapshotValid = false;
    uint64 captureFilterRevision = 0;
    size_t nextEntry = 0;
    size_t entryCount = 0;
    uint64 dropped = 0;

    // Resize only when the configured limit changes. Retain the newest rows
    // if a live configuration lowers the limit.
    void EnsureCapacityLocked(size_t requested) {
        if (entries.size() == requested) {
            return;
        }
        const size_t previousCount = entryCount;
        const size_t retainedCount = std::min(previousCount, requested);
        std::vector<Entry> retained;
        retained.reserve(retainedCount);
        if (retainedCount != 0 && !entries.empty()) {
            const size_t oldest = (nextEntry + entries.size() - entryCount) % entries.size();
            const size_t firstRetained = previousCount - retainedCount;
            for (size_t i = firstRetained; i < previousCount; ++i) {
                retained.push_back(entries[(oldest + i) % entries.size()]);
            }
        }
        entries.assign(requested, Entry{});
        for (size_t i = 0; i < retained.size(); ++i) {
            entries[i] = retained[i];
        }
        entryCount = retained.size();
        nextEntry = requested == 0 ? 0 : entryCount % requested;
        dropped += previousCount - entryCount;
    }

    void CopySnapshot(std::vector<Entry> &out, uint64 &outDropped) const {
        std::lock_guard lock(mutex);
        out.clear();
        out.reserve(entryCount);
        if (entryCount != 0 && !entries.empty()) {
            const size_t oldest = (nextEntry + entries.size() - entryCount) % entries.size();
            for (size_t i = 0; i < entryCount; ++i) {
                out.push_back(entries[(oldest + i) % entries.size()]);
            }
        }
        outDropped = dropped;
    }

    bool CopyCaptureSnapshot(std::vector<Entry> &out, uint64 &outDropped, std::vector<uint8> &vram,
                             std::vector<uint8> &cram, bool &filtersUnchanged) const {
        std::lock_guard lock(mutex);
        out.clear();
        out.reserve(entryCount);
        if (entryCount != 0 && !entries.empty()) {
            const size_t oldest = (nextEntry + entries.size() - entryCount) % entries.size();
            for (size_t i = 0; i < entryCount; ++i) {
                out.push_back(entries[(oldest + i) % entries.size()]);
            }
        }
        outDropped = dropped;
        vram = initialVRAM;
        cram = initialCRAM;
        filtersUnchanged = captureFilterRevision == filterRevision.load(std::memory_order_relaxed);
        return initialSnapshotValid;
    }

    void BeginCapture(std::span<const uint8> vram, std::span<const uint8> cram) final {
        std::lock_guard lock(mutex);
        initialVRAM.assign(vram.begin(), vram.end());
        initialCRAM.assign(cram.begin(), cram.end());
        initialSnapshotValid = !initialVRAM.empty() && !initialCRAM.empty();
        captureFilterRevision = filterRevision.load(std::memory_order_relaxed);
        nextEntry = 0;
        entryCount = 0;
        dropped = 0;
        outOfFrameRange.store(0, std::memory_order_relaxed);
    }

    void GetStats(size_t &outCount, uint64 &outDropped) const {
        std::lock_guard lock(mutex);
        outCount = entryCount;
        outDropped = dropped;
    }

    bool IsEnabled() const final {
        return enabled.load(std::memory_order_relaxed);
    }

    bool ShouldTraceWrite(uint64 vdp1Frame, ymir::debug::VDP2MemoryType type, uint32 address, uint8 width) const final {
        if (!enabled.load(std::memory_order_relaxed)) {
            return false;
        }
        if ((type == ymir::debug::VDP2MemoryType::VRAM && !traceVRAM.load(std::memory_order_relaxed)) ||
            (type == ymir::debug::VDP2MemoryType::CRAM && !traceCRAM.load(std::memory_order_relaxed))) {
            return false;
        }
        const uint32 begin = addressBegin.load(std::memory_order_relaxed);
        const uint32 end = addressEnd.load(std::memory_order_relaxed);
        const uint64 writeEnd = static_cast<uint64>(address) + width;
        if (begin >= end || address >= end || writeEnd <= begin) {
            return false;
        }
        if (traceVdp1FrameRange.load(std::memory_order_relaxed)) {
            const uint64 frameBegin = vdp1FrameBegin.load(std::memory_order_relaxed);
            const uint64 frameEnd = vdp1FrameEnd.load(std::memory_order_relaxed);
            if (vdp1Frame == ymir::debug::IVDP2MemoryTracer::kUnknownVdp1FrameIndex ||
                vdp1Frame < frameBegin || vdp1Frame >= frameEnd) {
                outOfFrameRange.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        }
        return true;
    }

    void Reset() final {
        Clear();
    }

    void Write(uint64 vdp1Frame, uint64 sequence, ymir::debug::VDP2MemoryType type, uint32 address, uint8 width,
               uint32 value) final {
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
        const size_t maxRows = capacity.load(std::memory_order_relaxed);
        EnsureCapacityLocked(maxRows);
        if (maxRows == 0) {
            ++dropped;
            return;
        }
        entries[nextEntry] = {vdp1Frame, sequence, false, 0, type, address, width, value};
        nextEntry = (nextEntry + 1) % maxRows;
        if (entryCount < maxRows) {
            ++entryCount;
        } else {
            ++dropped;
        }
    }

    void Scanline(uint64 vdp1Frame, uint64 sequence, uint32 row) final {
        if (!enabled.load(std::memory_order_relaxed)) {
            return;
        }
        if (traceVdp1FrameRange.load(std::memory_order_relaxed)) {
            const uint64 frameBegin = vdp1FrameBegin.load(std::memory_order_relaxed);
            const uint64 frameEnd = vdp1FrameEnd.load(std::memory_order_relaxed);
            if (vdp1Frame == ymir::debug::IVDP2MemoryTracer::kUnknownVdp1FrameIndex || vdp1Frame < frameBegin ||
                vdp1Frame >= frameEnd) {
                return;
            }
        }
        std::lock_guard lock(mutex);
        const size_t maxRows = capacity.load(std::memory_order_relaxed);
        EnsureCapacityLocked(maxRows);
        if (maxRows == 0) {
            ++dropped;
            return;
        }
        entries[nextEntry] = {vdp1Frame, sequence, true, row, ymir::debug::VDP2MemoryType::VRAM, 0, 0, 0};
        nextEntry = (nextEntry + 1) % maxRows;
        if (entryCount < maxRows) {
            ++entryCount;
        } else {
            ++dropped;
        }
    }

    void Clear() {
        std::lock_guard lock(mutex);
        initialVRAM.clear();
        initialCRAM.clear();
        initialSnapshotValid = false;
        nextEntry = 0;
        entryCount = 0;
        dropped = 0;
        outOfFrameRange.store(0, std::memory_order_relaxed);
    }
};

} // namespace app
