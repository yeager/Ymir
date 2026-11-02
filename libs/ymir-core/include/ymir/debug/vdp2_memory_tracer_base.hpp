#pragma once

/**
@file
@brief Defines the VDP2 memory write tracing interface.
*/

#include <ymir/core/types.hpp>

#include <span>

namespace ymir::debug {

enum class VDP2MemoryType : uint8 {
    VRAM,
    CRAM,
};

/// Receives committed VDP2 VRAM and CRAM writes from the emulator thread.
/// The callback reports the mapped destination address; it does not identify
/// the CPU, DMA channel, or source address that initiated the write.
struct IVDP2MemoryTracer {
    static constexpr uint64 kUnknownVdp1FrameIndex = ~uint64{0};

    virtual ~IVDP2MemoryTracer() = default;

    virtual bool IsEnabled() const { return true; }
    /// Starts a trace history from this committed VDP2 memory baseline.
    virtual void BeginCapture(std::span<const uint8>, std::span<const uint8>) {}
    /// Filters a committed write before dispatching the full trace callback.
    /// The default keeps existing tracers enabled without additional policy.
    virtual bool ShouldTraceWrite(uint64 vdp1Frame, VDP2MemoryType type, uint32 address, uint8 width) const {
        (void)vdp1Frame;
        (void)type;
        (void)address;
        (void)width;
        return IsEnabled();
    }
    /// Invoked when the VDP resets, so frame indices from separate runs are not mixed.
    virtual void Reset() {}
    /// @param[in] vdp1Frame the current VDP1 frame index, or zero before its first frame
    virtual void Write(uint64, VDP2MemoryType, uint32, uint8, uint32) {}
    /// Sequence-aware overload. Existing tracers continue to receive the legacy callback.
    virtual void Write(uint64 vdp1Frame, uint64, VDP2MemoryType type, uint32 address, uint8 width, uint32 value) {
        Write(vdp1Frame, type, address, width, value);
    }
    /// Marks a renderer row boundary in the same ordered stream as memory writes.
    virtual void Scanline(uint64, uint64, uint32) {}
};

} // namespace ymir::debug
