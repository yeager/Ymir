#include "app/app.hpp"

#include <util/os_exception_handler.hpp>
#include <ymir/util/thread_name.hpp>

#include <cxxopts.hpp>
#include <fmt/format.h>

#if defined(__APPLE__)
    #include <objc/message.h>
#endif

#include <memory>

int main(int argc, char **argv) {
    constexpr unsigned kMaxCaptureFrames = 60000;
#if defined(_WIN32)
    // NOTE: Setting the main thread name on Linux and macOS replaces the process name displayed on tools like `top`.
    util::SetCurrentThreadName("Main thread");
#endif

    bool showHelp = false;
    bool enableAllExceptions = false;

    app::CommandLineOptions progOpts{};
    cxxopts::Options options("Ymir", "Ymir - Sega Saturn emulator");
    options.add_options()("d,disc", "Path to Saturn disc image (.ccd, .chd, .cue, .iso, .mds)",
                          cxxopts::value(progOpts.gameDiscPath));
    options.add_options()("p,profile", "Path to profile directory", cxxopts::value(progOpts.profilePath));
    options.add_options()("u,user", "Force user profile",
                          cxxopts::value(progOpts.forceUserProfile)->default_value("false"));
    options.add_options()("h,help", "Display help text", cxxopts::value(showHelp)->default_value("false"));
    options.add_options()("f,fullscreen", "Start in fullscreen mode",
                          cxxopts::value(progOpts.fullScreen)->default_value("false"));
    options.add_options()("P,paused", "Start paused", cxxopts::value(progOpts.startPaused)->default_value("false"));
    options.add_options()("F,fast-forward", "Start in fast-forward mode",
                          cxxopts::value(progOpts.startFastForward)->default_value("false"));
    options.add_options()("D,debug", "Start with debug tracing enabled",
                          cxxopts::value(progOpts.enableDebugTracing)->default_value("false"));
    options.add_options()("load-state", "Load save-state slot after media initialization",
                          cxxopts::value(progOpts.captureLoadStateSlot)->default_value("0"));
    options.add_options()("capture-frames", "Run this many frames after loading a state, then dump memory",
                          cxxopts::value(progOpts.captureFramesAfterLoad)->default_value("0"));
    options.add_options()("capture-boot-frames", "Run this many frames after normal disc boot, then dump memory",
                          cxxopts::value(progOpts.captureFramesFromBoot)->default_value("0"));
    options.add_options()("capture-start-frame", "Zero-based capture frame at which to assert Control Pad 1 Start",
                          cxxopts::value(progOpts.captureControlPadStartFrame));
    options.add_options()("capture-start-hold", "Number of capture frames to hold Control Pad 1 Start",
                          cxxopts::value(progOpts.captureControlPadStartHoldFrames));
    options.add_options()("capture-a-frame", "Zero-based capture frame at which to assert Control Pad 1 A",
                          cxxopts::value(progOpts.captureControlPadAFrame));
    options.add_options()("capture-a-hold", "Number of capture frames to hold Control Pad 1 A",
                          cxxopts::value(progOpts.captureControlPadAHoldFrames));
    options.add_options()("dump-memory", "Dump memory after optional state load",
                          cxxopts::value(progOpts.dumpMemoryAtStartup)->default_value("false"));
    options.add_options()("trace-vdp2-writes", "Record bounded VDP2 VRAM/CRAM writes during a frame capture",
                          cxxopts::value(progOpts.captureVDP2MemoryWrites)->default_value("false"));
    options.add_options()("E,exceptions", "Capture all unhandled exceptions",
                          cxxopts::value(enableAllExceptions)->default_value("false"));
    options.parse_positional({"disc"});
    options.positional_help("path to disc image");
    options.show_positional_help();

#if !Ymir_LOCAL_BUILD
    try {
#endif
        auto result = options.parse(argc, argv);
        progOpts.captureControlPadStart = result.count("capture-start-frame") != 0
                                           || result.count("capture-start-hold") != 0;
        progOpts.captureControlPadA = result.count("capture-a-frame") != 0 || result.count("capture-a-hold") != 0;
        if (progOpts.captureLoadStateSlot > 10) {
            fmt::println("--load-state must be between 1 and 10");
            return -1;
        }
        if (progOpts.captureFramesAfterLoad != 0
            && (progOpts.captureLoadStateSlot == 0 || !progOpts.dumpMemoryAtStartup)) {
            fmt::println("--capture-frames requires --load-state and --dump-memory");
            return -1;
        }
        if (progOpts.captureFramesAfterLoad != 0 && progOpts.captureFramesFromBoot != 0) {
            fmt::println("--capture-frames and --capture-boot-frames cannot be combined");
            return -1;
        }
        if (progOpts.captureFramesFromBoot != 0 && !progOpts.dumpMemoryAtStartup) {
            fmt::println("--capture-boot-frames requires --dump-memory");
            return -1;
        }
        if (progOpts.captureFramesAfterLoad > kMaxCaptureFrames || progOpts.captureFramesFromBoot > kMaxCaptureFrames) {
            fmt::println("frame capture must not exceed {} frames", kMaxCaptureFrames);
            return -1;
        }
        const unsigned captureFrameCount =
            progOpts.captureFramesAfterLoad != 0 ? progOpts.captureFramesAfterLoad : progOpts.captureFramesFromBoot;
        if (progOpts.captureControlPadStart
            && (captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup
                || progOpts.captureControlPadStartHoldFrames == 0
                || progOpts.captureControlPadStartFrame >= captureFrameCount)) {
            fmt::println("--capture-start-frame and --capture-start-hold require a frame capture and --dump-memory; "
                         "hold must be nonzero and start must be within the capture");
            return -1;
        }
        if (progOpts.captureControlPadA
            && (captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup || progOpts.captureControlPadAHoldFrames == 0
                || progOpts.captureControlPadAFrame >= captureFrameCount)) {
            fmt::println("--capture-a-frame and --capture-a-hold require a frame capture and --dump-memory; "
                         "hold must be nonzero and start must be within the capture");
            return -1;
        }
        if (progOpts.captureVDP2MemoryWrites
            && (!progOpts.enableDebugTracing || captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup)) {
            fmt::println("--trace-vdp2-writes requires --debug, a frame capture, and --dump-memory");
            return -1;
        }
        if (showHelp) {
            fmt::println("{}", options.help());
            return 0;
        }

        util::RegisterExceptionHandler(enableAllExceptions);

        auto app = std::make_unique<app::App>();
        return app->Run(progOpts);
#if !Ymir_LOCAL_BUILD
    } catch (const cxxopts::exceptions::exception &e) {
        std::string msg = fmt::format("Failed to parse arguments: {}", e.what());
        fmt::println("{}", msg);
        util::ShowFatalErrorDialog(msg.c_str());
        return -1;
    } catch (const std::system_error &e) {
        std::string msg = fmt::format("System error: {}", e.what());
        fmt::println("{}", msg);
        util::ShowFatalErrorDialog(msg.c_str());
        return e.code().value();
    } catch (const std::exception &e) {
        std::string msg = fmt::format("Unhandled exception: {}", e.what());
        fmt::println("{}", msg);
        util::ShowFatalErrorDialog(msg.c_str());
        return -1;
    #if defined(__APPLE__)
    } catch (id e) {
        SEL sel_reason = sel_registerName("reason");
        id reason = ((id (*)(id, SEL))objc_msgSend)(e, sel_reason); // NSString

        SEL sel_UTF8String = sel_registerName("UTF8String");
        const char *failureReason = ((const char *(*)(id, SEL))objc_msgSend)(reason, sel_UTF8String);
        util::ShowFatalErrorDialog(failureReason);
        return -1;
    #endif
    } catch (...) {
        std::string msg = "Unspecified exception";
        fmt::println("{}", msg);
        util::ShowFatalErrorDialog(msg.c_str());
        return -1;
    }
#endif

    return 0;
}
