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
    std::filesystem::path pndFetchTracePath;
    uint64_t pndFetchFirstFrame = 0;
    uint64_t pndFetchEndFrame = 64;
    uint32_t pndFetchFirstRow = 0;
    uint32_t pndFetchEndRow = 512;
    std::filesystem::path vdp2PixelFetchTracePath;
    uint64_t vdp2PixelFetchFirstFrame = 0;
    uint64_t vdp2PixelFetchEndFrame = 1;
    uint32_t vdp2PixelFetchFirstRow = 0;
    uint32_t vdp2PixelFetchEndRow = 224;
    uint32_t vdp2PixelFetchFirstX = 0;
    uint32_t vdp2PixelFetchEndX = 320;
    unsigned captureLoadStateSlot = 0;
    unsigned captureFramesAfterLoad = 0;
    bool dumpMemoryAtStartup = false;
};

} // namespace app
