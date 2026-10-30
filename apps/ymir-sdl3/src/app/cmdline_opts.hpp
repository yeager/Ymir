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
};

} // namespace app
