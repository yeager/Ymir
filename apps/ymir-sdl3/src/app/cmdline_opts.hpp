#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace app {

struct ScheduledControlPadInput {
    unsigned frame = 0;
    std::uint16_t buttonMask = 0;
    unsigned holdFrames = 0;
};

struct CommandLineOptions {
    std::filesystem::path gameDiscPath;
    std::filesystem::path profilePath;
    bool forceUserProfile;
    bool fullScreen;
    bool startPaused;
    bool startFastForward;
    bool enableDebugTracing;
    unsigned captureLoadStateSlot = 0;
    unsigned captureFramesAfterLoad = 0;
    unsigned captureFramesFromBoot = 0;
    bool captureControlPadStart = false;
    unsigned captureControlPadStartFrame = 0;
    unsigned captureControlPadStartHoldFrames = 0;
    bool captureControlPadStartRepeat = false;
    unsigned captureControlPadStartRepeatFrame = 0;
    unsigned captureControlPadStartRepeatHoldFrames = 0;
    bool captureControlPadA = false;
    unsigned captureControlPadAFrame = 0;
    unsigned captureControlPadAHoldFrames = 0;
    bool captureControlPadARepeat = false;
    unsigned captureControlPadARepeatFrame = 0;
    unsigned captureControlPadARepeatHoldFrames = 0;
    bool captureControlPadDown = false;
    unsigned captureControlPadDownFrame = 0;
    unsigned captureControlPadDownHoldFrames = 0;
    std::vector<ScheduledControlPadInput> captureControlPadInputs;
    bool captureControlPadC = false;
    unsigned captureControlPadCFrame = 0;
    unsigned captureControlPadCHoldFrames = 0;
    bool dumpMemoryAtStartup = false;
    bool captureScreenshotAtEnd = false;
    unsigned captureSaveStateSlot = 0;
    bool captureNoPresent = false;
    bool captureNoRender = false;
    bool captureVDP2MemoryWrites = false;
    bool captureSH2MemoryReads = false;
    unsigned captureSH2MemoryReadBegin = 0;
    unsigned captureSH2MemoryReadEnd = 0;
    bool captureSH2MemoryWrites = false;
    unsigned captureSH2MemoryWriteBegin = 0;
    unsigned captureSH2MemoryWriteEnd = 0;
};

} // namespace app
