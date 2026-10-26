#pragma once

#include <filesystem>

namespace app {

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
    bool captureControlPadDown = false;
    unsigned captureControlPadDownFrame = 0;
    unsigned captureControlPadDownHoldFrames = 0;
    bool captureControlPadC = false;
    unsigned captureControlPadCFrame = 0;
    unsigned captureControlPadCHoldFrames = 0;
    bool dumpMemoryAtStartup = false;
    bool captureScreenshotAtEnd = false;
    unsigned captureSaveStateSlot = 0;
    bool captureNoPresent = false;
    bool captureVDP2MemoryWrites = false;
};

} // namespace app
