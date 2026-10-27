#include "app/app.hpp"

#include <util/os_exception_handler.hpp>
#include <ymir/util/thread_name.hpp>
#include <ymir/hw/smpc/peripheral/peripheral_state_common.hpp>

#include <cxxopts.hpp>
#include <fmt/format.h>

#if defined(__APPLE__)
    #include <objc/message.h>
#endif

#include <charconv>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

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
    options.add_options()("capture-start-repeat-frame", "Zero-based capture frame for a second Control Pad 1 Start press",
                          cxxopts::value(progOpts.captureControlPadStartRepeatFrame));
    options.add_options()("capture-start-repeat-hold", "Number of capture frames to hold the second Control Pad 1 Start press",
                          cxxopts::value(progOpts.captureControlPadStartRepeatHoldFrames));
    options.add_options()("capture-a-frame", "Zero-based capture frame at which to assert Control Pad 1 A",
                          cxxopts::value(progOpts.captureControlPadAFrame));
    options.add_options()("capture-a-hold", "Number of capture frames to hold Control Pad 1 A",
                          cxxopts::value(progOpts.captureControlPadAHoldFrames));
    options.add_options()("capture-a-repeat-frame", "Zero-based capture frame for a second Control Pad 1 A press",
                          cxxopts::value(progOpts.captureControlPadARepeatFrame));
    options.add_options()("capture-a-repeat-hold", "Number of capture frames to hold the second Control Pad 1 A press",
                          cxxopts::value(progOpts.captureControlPadARepeatHoldFrames));
    options.add_options()("capture-down-frame", "Zero-based capture frame at which to assert Control Pad 1 Down",
                          cxxopts::value(progOpts.captureControlPadDownFrame));
    options.add_options()("capture-down-hold", "Number of capture frames to hold Control Pad 1 Down",
                          cxxopts::value(progOpts.captureControlPadDownHoldFrames));
    options.add_options()("capture-pad-input",
                          "Schedule Control Pad 1 input as FRAME:BUTTON:HOLD (repeatable; up/down/left/right/start/a/b/c)",
                          cxxopts::value<std::vector<std::string>>());
    options.add_options()("capture-c-frame", "Zero-based capture frame at which to assert Control Pad 1 C",
                          cxxopts::value(progOpts.captureControlPadCFrame));
    options.add_options()("capture-c-hold", "Number of capture frames to hold Control Pad 1 C",
                          cxxopts::value(progOpts.captureControlPadCHoldFrames));
    options.add_options()("dump-memory", "Dump memory after optional state load",
                          cxxopts::value(progOpts.dumpMemoryAtStartup)->default_value("false"));
    options.add_options()("capture-screenshot", "Save a screenshot after a deterministic frame capture",
                          cxxopts::value(progOpts.captureScreenshotAtEnd)->default_value("false"));
    options.add_options()("capture-save-state", "Save this slot after a deterministic frame capture",
                          cxxopts::value(progOpts.captureSaveStateSlot)->default_value("0"));
    options.add_options()("capture-no-present", "Run a deterministic frame capture without presenting GUI frames",
                          cxxopts::value(progOpts.captureNoPresent)->default_value("false"));
    options.add_options()("capture-no-render", "Run a memory/trace-only capture with the VDP null renderer",
                          cxxopts::value(progOpts.captureNoRender)->default_value("false"));
    options.add_options()("trace-vdp2-writes", "Record bounded VDP2 VRAM/CRAM writes during a frame capture",
                          cxxopts::value(progOpts.captureVDP2MemoryWrites)->default_value("false"));
    options.add_options()("trace-scu-dma", "Record SCU DMA transfer starts during a frame capture",
                          cxxopts::value(progOpts.captureSCUDMATrace)->default_value("false"));
    options.add_options()("trace-sh2-reads-begin", "First physical bus address for a bounded SH-2 data-read trace",
                          cxxopts::value(progOpts.captureSH2MemoryReadBegin));
    options.add_options()("trace-sh2-reads-end", "Exclusive physical bus address for a bounded SH-2 data-read trace",
                          cxxopts::value(progOpts.captureSH2MemoryReadEnd));
    options.add_options()("trace-sh2-writes-begin", "First physical bus address for a bounded SH-2 data-write trace",
                          cxxopts::value(progOpts.captureSH2MemoryWriteBegin));
    options.add_options()("trace-sh2-writes-end", "Exclusive physical bus address for a bounded SH-2 data-write trace",
                          cxxopts::value(progOpts.captureSH2MemoryWriteEnd));
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
        progOpts.captureControlPadStartRepeat = result.count("capture-start-repeat-frame") != 0
                                                 || result.count("capture-start-repeat-hold") != 0;
        progOpts.captureControlPadA = result.count("capture-a-frame") != 0 || result.count("capture-a-hold") != 0;
        progOpts.captureControlPadARepeat = result.count("capture-a-repeat-frame") != 0
                                             || result.count("capture-a-repeat-hold") != 0;
        progOpts.captureControlPadDown = result.count("capture-down-frame") != 0
                                         || result.count("capture-down-hold") != 0;
        progOpts.captureControlPadC = result.count("capture-c-frame") != 0 || result.count("capture-c-hold") != 0;
        if (result.count("capture-pad-input") != 0) {
            using Button = ymir::peripheral::Button;
            const auto parseFrame = [](std::string_view text, unsigned &value) {
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
                return error == std::errc{} && end == text.data() + text.size();
            };
            const auto buttonMask = [](std::string_view name) -> std::uint16_t {
                if (name == "up") return static_cast<std::uint16_t>(Button::Up);
                if (name == "down") return static_cast<std::uint16_t>(Button::Down);
                if (name == "left") return static_cast<std::uint16_t>(Button::Left);
                if (name == "right") return static_cast<std::uint16_t>(Button::Right);
                if (name == "start") return static_cast<std::uint16_t>(Button::Start);
                if (name == "a") return static_cast<std::uint16_t>(Button::A);
                if (name == "b") return static_cast<std::uint16_t>(Button::B);
                if (name == "c") return static_cast<std::uint16_t>(Button::C);
                return 0;
            };
            for (const std::string &spec : result["capture-pad-input"].as<std::vector<std::string>>()) {
                const size_t first = spec.find(':');
                const size_t second = first == std::string::npos ? first : spec.find(':', first + 1);
                app::ScheduledControlPadInput input{};
                const std::uint16_t mask = second == std::string::npos
                                               ? 0
                                               : buttonMask(std::string_view(spec).substr(first + 1, second - first - 1));
                if (first == std::string::npos || second == std::string::npos
                    || !parseFrame(std::string_view(spec).substr(0, first), input.frame)
                    || !parseFrame(std::string_view(spec).substr(second + 1), input.holdFrames) || mask == 0
                    || input.holdFrames == 0) {
                    fmt::println("invalid --capture-pad-input '{}'; expected FRAME:BUTTON:HOLD with a supported "
                                 "button and nonzero hold",
                                 spec);
                    return -1;
                }
                input.buttonMask = mask;
                progOpts.captureControlPadInputs.push_back(input);
            }
        }
        progOpts.captureSH2MemoryReads = result.count("trace-sh2-reads-begin") != 0
                                         || result.count("trace-sh2-reads-end") != 0;
        progOpts.captureSH2MemoryWrites = result.count("trace-sh2-writes-begin") != 0
                                          || result.count("trace-sh2-writes-end") != 0;
        if (progOpts.captureLoadStateSlot > 10) {
            fmt::println("--load-state must be between 1 and 10");
            return -1;
        }
        if (progOpts.captureSaveStateSlot > 10) {
            fmt::println("--capture-save-state must be between 1 and 10");
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
        if (progOpts.captureScreenshotAtEnd && (captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup)) {
            fmt::println("--capture-screenshot requires a frame capture and --dump-memory");
            return -1;
        }
        if (progOpts.captureSaveStateSlot != 0 && (captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup)) {
            fmt::println("--capture-save-state requires a frame capture and --dump-memory");
            return -1;
        }
        if (progOpts.captureNoPresent && (captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup)) {
            fmt::println("--capture-no-present requires a frame capture and --dump-memory");
            return -1;
        }
        if (progOpts.captureNoRender
            && (captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup || !progOpts.captureNoPresent
                || progOpts.captureScreenshotAtEnd || progOpts.captureSaveStateSlot != 0)) {
            fmt::println("--capture-no-render requires --capture-no-present, a frame capture, and --dump-memory; "
                         "it cannot be combined with screenshots or save states");
            return -1;
        }
        if (progOpts.captureControlPadStart
            && (captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup
                || progOpts.captureControlPadStartHoldFrames == 0
                || progOpts.captureControlPadStartFrame >= captureFrameCount)) {
            fmt::println("--capture-start-frame and --capture-start-hold require a frame capture and --dump-memory; "
                         "hold must be nonzero and start must be within the capture");
            return -1;
        }
        if (progOpts.captureControlPadStartRepeat
            && (captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup
                || progOpts.captureControlPadStartRepeatHoldFrames == 0
                || progOpts.captureControlPadStartRepeatFrame >= captureFrameCount)) {
            fmt::println("--capture-start-repeat-frame and --capture-start-repeat-hold require a frame capture and "
                         "--dump-memory; hold must be nonzero and start must be within the capture");
            return -1;
        }
        if (progOpts.captureControlPadA
            && (captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup || progOpts.captureControlPadAHoldFrames == 0
                || progOpts.captureControlPadAFrame >= captureFrameCount)) {
            fmt::println("--capture-a-frame and --capture-a-hold require a frame capture and --dump-memory; "
                         "hold must be nonzero and start must be within the capture");
            return -1;
        }
        if (progOpts.captureControlPadARepeat
            && (captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup
                || progOpts.captureControlPadARepeatHoldFrames == 0
                || progOpts.captureControlPadARepeatFrame >= captureFrameCount)) {
            fmt::println("--capture-a-repeat-frame and --capture-a-repeat-hold require a frame capture and "
                         "--dump-memory; hold must be nonzero and start must be within the capture");
            return -1;
        }
        if (progOpts.captureControlPadDown
            && (captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup
                || progOpts.captureControlPadDownHoldFrames == 0
                || progOpts.captureControlPadDownFrame >= captureFrameCount)) {
            fmt::println("--capture-down-frame and --capture-down-hold require a frame capture and --dump-memory; "
                         "hold must be nonzero and start must be within the capture");
            return -1;
        }
        if (progOpts.captureControlPadC
            && (captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup || progOpts.captureControlPadCHoldFrames == 0
                || progOpts.captureControlPadCFrame >= captureFrameCount)) {
            fmt::println("--capture-c-frame and --capture-c-hold require a frame capture and --dump-memory; "
                         "hold must be nonzero and start must be within the capture");
            return -1;
        }
        if (!progOpts.captureControlPadInputs.empty()) {
            if (captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup) {
                fmt::println("--capture-pad-input requires --capture-frames or --capture-boot-frames and --dump-memory");
                return -1;
            }
            for (const auto &input : progOpts.captureControlPadInputs) {
                if (input.frame >= captureFrameCount || input.holdFrames > captureFrameCount - input.frame) {
                    fmt::println("--capture-pad-input frame and hold must fit within the capture");
                    return -1;
                }
            }
        }
        if (progOpts.captureSCUDMATrace
            && (!progOpts.enableDebugTracing || captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup)) {
            fmt::println("--trace-scu-dma requires --debug, a frame capture, and --dump-memory");
            return -1;
        }
        if (progOpts.captureVDP2MemoryWrites
            && (!progOpts.enableDebugTracing || captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup)) {
            fmt::println("--trace-vdp2-writes requires --debug, a frame capture, and --dump-memory");
            return -1;
        }
        if (progOpts.captureSH2MemoryReads
            && (!progOpts.enableDebugTracing || captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup
                || progOpts.captureSH2MemoryReadBegin >= progOpts.captureSH2MemoryReadEnd)) {
            fmt::println("--trace-sh2-reads-begin/end require --debug, a frame capture, --dump-memory, and a nonempty range");
            return -1;
        }
        if (progOpts.captureSH2MemoryWrites
            && (!progOpts.enableDebugTracing || captureFrameCount == 0 || !progOpts.dumpMemoryAtStartup
                || progOpts.captureSH2MemoryWriteBegin >= progOpts.captureSH2MemoryWriteEnd)) {
            fmt::println("--trace-sh2-writes-begin/end require --debug, a frame capture, --dump-memory, and a nonempty range");
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
