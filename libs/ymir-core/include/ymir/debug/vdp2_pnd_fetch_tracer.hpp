#pragma once

#include <ymir/core/types.hpp>

namespace ymir::debug {

// Context for one normal-scroll sample whose current character pattern name
// was fetched from VDP2 VRAM. The row is emitted at the actual PND read.
struct VDP2PNDFetchContext {
    uint64 frame;
    uint32 row;
    bool alternateField;
    uint8 layer; // NBG layer number: 1 for NBG1, 2 for NBG2.
    uint32 screenX;
    uint32 scrollX;
    uint32 scrollY;
};

struct VDP2PNDFetchSample {
    VDP2PNDFetchContext context;
    uint32 pndAddress;
    uint8 pndWidthBytes;
    uint32 rawPND;
};

// Opt-in observer for actual pattern-name reads used by visible normal-scroll
// fetches. Bitmap and rotation layers do not report through this interface.
struct IVDP2PNDFetchTracer {
    virtual ~IVDP2PNDFetchTracer() = default;
    virtual bool ShouldTrace(uint64 frame, uint32 row, uint8 layer) const = 0;
    virtual void Fetch(const VDP2PNDFetchSample &sample) = 0;
};

} // namespace ymir::debug
