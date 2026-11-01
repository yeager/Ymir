#pragma once

#include <ymir/core/types.hpp>

namespace ymir::debug {

struct VDP2PixelFetchSample {
    uint64 frame;
    uint64 vdp2FrameSerial;
    uint32 row;
    uint32 screenX;
    bool alternateField;
    uint8 layer;
    uint32 scrollX;
    uint32 scrollY;
    bool windowBlocked;
    bool mosaicReused;
    uint32 mosaicSourceX;
    bool pndSourceValid;
    bool pndAccessAllowed;
    uint32 pndAddress;
    uint8 pndWidthBytes;
    uint32 rawPND;
    uint32 charNum;
    uint32 charAddress;
    bool charDataCacheHit;
    bool charPatternAccessAllowed;
    bool charDataValid;
    uint32 dotAddress;
    uint8 dotValue;
    bool dotValueValid;
    uint8 priority;
};

// Observes selected pixels from normal-scroll VDP2 background layers.
// Bitmap and rotation backgrounds are not reported by this interface.
struct IVDP2PixelFetchTracer {
    virtual ~IVDP2PixelFetchTracer() = default;
    virtual bool ShouldTrace(uint64 frame, uint32 row, uint32 screenX, uint8 layer) const = 0;
    virtual void Fetch(const VDP2PixelFetchSample &sample) = 0;
};

} // namespace ymir::debug
