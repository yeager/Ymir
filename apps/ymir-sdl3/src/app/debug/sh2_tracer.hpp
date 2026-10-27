#pragma once

#include <ymir/debug/sh2_tracer_base.hpp>

#include <app/debug/sh2_exec_analyst.hpp>

#include <util/ring_buffer.hpp>

namespace app {

struct SH2Tracer final : ymir::debug::ISH2Tracer {
    void ResetInterruptCounter();
    void ResetDivisionCounter();
    void ResetDMACounter(uint32 channel);
    void ResetDMATraces();
    void ResetMemoryWriteTrace();
    void ResetMemoryReadTrace();

    bool traceInstructions = false;
    bool traceInterrupts = false;
    bool traceExceptions = false;
    bool traceDivisions = false;
    bool traceDMA = false;
    bool traceMemoryWrites = false;
    bool traceMemoryReads = false;
    uint32 memoryWriteAddressBegin = 0;
    uint32 memoryWriteAddressEnd = 0;
    bool traceMemoryWriteFrameRange = false;
    uint32 memoryWriteFrameBegin = 0;
    uint32 memoryWriteFrameEnd = 0;
    uint32 currentCaptureFrame = 0;
    bool captureFrameActive = false;
    uint32 memoryReadAddressBegin = 0;
    uint32 memoryReadAddressEnd = 0;

    struct InstructionInfo {
        uint32 pc;
        uint16 opcode;
        bool delaySlot;
    };

    struct InterruptInfo {
        uint8 vecNum;
        uint8 level;
        ymir::sh2::InterruptSource source;
        uint32 pc;
        uint32 counter;
    };

    struct ExceptionInfo {
        uint8 vecNum;
        uint32 pc;
        uint32 sr;
    };

    struct DivisionInfo {
        sint64 dividend;
        sint32 divisor;
        sint32 quotient;
        sint32 remainder;
        bool overflow;
        bool overflowIntrEnable;
        bool finished;
        bool div64;
        uint32 counter;
    };

    struct DMAInfo {
        uint32 srcAddress;
        uint32 dstAddress;
        uint32 count;
        uint32 unitSize;
        sint32 srcInc;
        sint32 dstInc;
        bool finished;
        bool irqRaised;
        uint32 counter;

        /*struct XferUnit {
            uint32 srcAddress;
            uint32 dstAddress;
            uint32 data;
            uint32 unitSize;
        };

        std::vector<XferUnit> units;*/
    };

    struct MemoryWriteInfo {
        uint32 captureFrame;
        bool captureFrameValid;
        uint32 pc;
        uint32 callerReturnAddress;
        uint32 callerTarget;
        uint32 address;
        uint32 value;
        uint8 width;
    };

    struct MemoryReadInfo {
        uint32 pc;
        uint32 callerReturnAddress;
        uint32 callerTarget;
        uint32 address;
        uint8 width;
    };

    util::RingBuffer<InstructionInfo, 16384> instructions;
    util::RingBuffer<InterruptInfo, 1024> interrupts;
    util::RingBuffer<ExceptionInfo, 1024> exceptions;
    util::RingBuffer<DivisionInfo, 1024> divisions;
    std::array<util::RingBuffer<DMAInfo, 1024>, 2> dmaTransfers;
    util::RingBuffer<MemoryWriteInfo, 250000> memoryWrites;
    uint64 memoryWritesDropped = 0;
    util::RingBuffer<MemoryReadInfo, 250000> memoryReads;
    uint64 memoryReadsDropped = 0;

    struct DivisionStatistics {
        uint64 div32s = 0;
        uint64 div64s = 0;
        uint64 overflows = 0;
        uint64 interrupts = 0;

        void Clear() {
            div32s = 0;
            div64s = 0;
            overflows = 0;
            interrupts = 0;
        }
    } divStats;

    struct DMAStatistics {
        uint64 numTransfers = 0;
        uint64 bytesTransferred = 0;
        uint64 interrupts = 0;

        void Clear() {
            numTransfers = 0;
            bytesTransferred = 0;
            interrupts = 0;
        }
    };

    std::array<DMAStatistics, 2> dmaStats;

    SH2ExecAnalyst execAnalyst;

    void SetCaptureFrameIndex(uint32 frameIndex) {
        currentCaptureFrame = frameIndex;
        captureFrameActive = true;
    }

    void ClearCaptureFrameIndex() {
        captureFrameActive = false;
    }

private:
    uint32 m_interruptCounter = 0;
    uint32 m_divisionCounter = 0;
    std::array<uint32, 2> m_dmaCounter = {0, 0};

    // -------------------------------------------------------------------------
    // ISH2Tracer implementation

    void Attached() final;
    void Detached() final;

    void Reset(uint32 pc, uint32 sp, bool watchdogInitiated) final;

    void ExecuteInstruction(uint32 pc, uint16 opcode, bool delaySlot) final;
    void MemoryWrite(uint32 pc, uint32 address, uint8 width, uint32 value) final;
    void MemoryRead(uint32 pc, uint32 address, uint8 width) final;
    bool IsMemoryReadTracingEnabled() const final { return traceMemoryReads; }
    bool IsMemoryWriteTracingEnabled() const final { return traceMemoryWrites; }
    void DelaySlot(uint32 pc, uint32 target) final;
    void Branch(uint32 pc, uint32 target) final;
    void BranchDelay(uint32 target) final;
    void Call(uint32 target) final;
    void Return(uint32 target) final;
    void ReturnFromException(uint32 target, uint32 newSP) final;
    void Interrupt(uint8 vecNum, uint8 level, ymir::sh2::InterruptSource source, uint32 pc) final;
    void Exception(uint8 vecNum, uint32 oldPC, uint32 oldSR, uint32 oldSP, uint32 newPC) final;
    void Trap(uint8 vecNum, uint32 oldPC, uint32 oldSP, uint32 newPC) final;
    void ChangeStack(uint32 newSP) final;
    void ResizeStack(uint32 oldSP, uint32 newSP) final;
    void PushRegisterToStack(uint8 rn, uint32 oldSP, uint32 newSP) final;
    void PushToStack(ymir::debug::SH2StackValueType type, uint32 newSP) final;
    void PopFromStack(uint32 newSP) final;

    void Begin32x32Division(sint32 dividend, sint32 divisor, bool overflowIntrEnable) final;
    void Begin64x32Division(sint64 dividend, sint32 divisor, bool overflowIntrEnable) final;
    void EndDivision(sint32 quotient, sint32 remainder, bool overflow) final;

    void DMAXferBegin(uint32 channel, uint32 srcAddress, uint32 dstAddress, uint32 count, uint32 unitSize,
                      sint32 srcInc, sint32 dstInc) final;
    void DMAXferData(uint32 channel, uint32 srcAddress, uint32 dstAddress, uint32 data, uint32 unitSize) final;
    void DMAXferEnd(uint32 channel, bool irqRaised) final;
};

} // namespace app
