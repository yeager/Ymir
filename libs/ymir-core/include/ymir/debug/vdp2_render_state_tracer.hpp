#pragma once

#include <ymir/core/types.hpp>

namespace ymir::debug {

struct VDP2RenderStateSample {
    uint64 frame;
    uint32 y;
    bool alternateField;
    uint32 scrollH;
    uint32 scrollV;
    uint32 scrollIncH;
    uint32 scrollIncV;
    uint32 fractionalScrollX;
    uint32 fractionalScrollY;
    uint8 priority;
    bool colorCalculation;
    uint8 colorCalculationRatio;
    bool bitmap;
    uint8 colorFormat;
    bool nbg0Enabled;
    bool layerEnabled;
    bool rbg1Enabled;
    bool mosaicReuse;
};

struct IVDP2RenderStateTracer {
    virtual ~IVDP2RenderStateTracer() = default;
    virtual bool ShouldTrace(uint64 frame, uint32 y) const = 0;
    virtual void Sample(const VDP2RenderStateSample &sample) = 0;
};

} // namespace ymir::debug
