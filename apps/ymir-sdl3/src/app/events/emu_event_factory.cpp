#include "emu_event_factory.hpp"

#include "gui_event_factory.hpp"

#include <app/shared_context.hpp>

#include <app/services/graphics_service.hpp>
#include <app/services/save_state_service.hpp>

#include <app/services/gfx/gfx_context_impls.hpp>

#include <ymir/sys/saturn.hpp>

#include <ymir/util/dev_log.hpp>
#include <ymir/util/scope_guard.hpp>

#include <util/file_loader.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

using namespace ymir;

namespace app::events::emu {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "Emulator";
    };

} // namespace grp

EmuEvent SetClockSpeed(sys::ClockSpeed clockSpeed) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.instance->SetClockSpeed(clockSpeed); });
}

EmuEvent SetVideoStandard(core::config::sys::VideoStandard videoStandard) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.instance->SetVideoStandard(videoStandard); });
}

EmuEvent SetAreaCode(uint8 areaCode) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.instance->SMPC.SetAreaCode(areaCode); });
}

EmuEvent SetDeinterlace(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        ctx.saturn.instance->VDP.ModifyEnhancements(
            [&](vdp::config::Enhancements &enhancements) { enhancements.deinterlace = enable; });
    });
}

EmuEvent SetTransparentMeshes(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        ctx.saturn.instance->VDP.ModifyEnhancements(
            [&](vdp::config::Enhancements &enhancements) { enhancements.transparentMeshes = enable; });
    });
}

EmuEvent UseNullVDPRenderer(util::Event &event) {
    return RunFunction([&event](SharedContext &ctx) {
        auto &vdp = ctx.saturn.instance->VDP;
        vdp.UseNullRenderer();
        event.Set();
    });
}

EmuEvent SwitchVDPRenderer(bool verbose) {
    return RunFunction([=](SharedContext &ctx) {
        auto notifySuccess = [&](std::string message) {
            if (verbose) {
                ctx.DisplayMessage(message);
            } else {
                devlog::info<grp::base>("{}", message);
            }
        };

        auto &svc = ctx.serviceLocator.GetRequired<services::GraphicsService>();
        gfx::IGraphicsContext &gfxCtx = svc.GetGraphicsContext();

        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        auto &vdp = ctx.saturn.instance->VDP;
        if (settings.video.useHardwareAcceleration) {
            const gfx::Backend backend = gfxCtx.GetBackend();
            switch (backend) {
#if YMIR_PLATFORM_HAS_DIRECT3D
            case gfx::Backend::Direct3D12: //
            {
                if (auto *dx12Ctx = gfxCtx.As<gfx::Direct3D12GraphicsContext>()) {
                    auto result = vdp.UseDirect3D12Renderer(dx12Ctx->GetDevice());
                    if (result) {
                        ctx.screen.frameReadyEvent.Set(); // unblock main thread in case video sync is enabled
                        notifySuccess("Direct3D 12 renderer initialized successfully");
                        return;
                    }
                    ctx.DisplayMessage(
                        fmt::format("Failed to create Direct3D 12 VDP renderer: {}", result.Error().message));
                } else {
                    // This should never happen
                    ctx.DisplayMessage("An unexpected error occured trying to access the Direct3D 12 graphics context");
                }
                break;
            }
#endif
            case gfx::Backend::SDLRenderer:
                // Silently revert to software renderer
                break;
            default:
                ctx.DisplayMessage(
                    fmt::format("Hardware acceleration is not implemented for {}", gfx::GraphicsBackendName(backend)));
                break;
            }

            // Hardware renderer instantiation failed; revert setting
            settings.video.useHardwareAcceleration = false;
        }

        // Fall back to software renderer if not using GPU acceleration or the hardware renderer failed to initialize
        if (vdp.GetRenderer().GetType() != vdp::VDPRendererType::Software) {
            vdp.UseSoftwareRenderer();
            gfxCtx.ResetDisplayOutputTextures();
            notifySuccess("Software renderer initialized successfully");
        }
    });
}

EmuEvent SetDebugTrace(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        ctx.saturn.instance->EnableDebugTracing(enable);
        ctx.saturn.instance->VDP.UseTracer(enable ? &ctx.tracers.VDP : nullptr);
        ctx.saturn.instance->VDP.UseVDP2MemoryTracer(
            ctx.tracers.VDP2Memory.enabled.load(std::memory_order_relaxed) ? &ctx.tracers.VDP2Memory : nullptr);
        if (enable) {
            ctx.saturn.instance->masterSH2.UseTracer(&ctx.tracers.masterSH2);
            ctx.saturn.instance->slaveSH2.UseTracer(&ctx.tracers.slaveSH2);
            ctx.saturn.instance->SCU.UseTracer(&ctx.tracers.SCU);
            ctx.saturn.instance->SCSP.UseTracer(&ctx.tracers.SCSP);
            ctx.saturn.instance->CDBlock.UseTracer(&ctx.tracers.CDBlock);
            ctx.saturn.instance->CDDrive.UseTracer(&ctx.tracers.CDDrive);
            ctx.saturn.instance->YGR.UseTracer(&ctx.tracers.YGR);
        }
        ctx.DisplayMessage(fmt::format("Debug tracing {}", (enable ? "enabled" : "disabled")));
    });
}

EmuEvent SetVDPTrace(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        ctx.saturn.instance->VDP.UseTracer(enable ? &ctx.tracers.VDP : nullptr);
    });
}

EmuEvent SetVDP2MemoryTrace(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        auto &saturn = *ctx.saturn.instance;
        if (enable) {
            saturn.EnableDebugTracing(true);
            saturn.VDP.UseVDP2MemoryTracer(&ctx.tracers.VDP2Memory);
            ctx.DisplayMessage("VDP2 memory write tracing enabled");
        } else {
            saturn.VDP.UseVDP2MemoryTracer(nullptr);
            ctx.DisplayMessage("VDP2 memory write tracing disabled");
        }
    });
}

EmuEvent ExportVDP2MemoryTrace() {
    return RunFunction([](SharedContext &ctx) {
        auto &saturn = *ctx.saturn.instance;
        auto &tracer = ctx.tracers.VDP2Memory;
        const uint64 captureFrame = saturn.VDP.GetVDP2MemoryTraceFrameIndex();
        std::vector<VDP2MemoryTracer::Entry> traceSnapshot;
        uint64 dropped = 0;
        {
            std::lock_guard lock(tracer.mutex);
            traceSnapshot.assign(tracer.entries.begin(), tracer.entries.end());
            dropped = tracer.dropped;
        }
        const bool frameRangeEnabled = tracer.traceVdp1FrameRange.load(std::memory_order_relaxed);
        const uint64 frameRangeBegin = tracer.vdp1FrameBegin.load(std::memory_order_relaxed);
        const uint64 frameRangeEnd = tracer.vdp1FrameEnd.load(std::memory_order_relaxed);
        const uint64 outOfFrameRange = tracer.outOfFrameRange.load(std::memory_order_relaxed);
        const auto dumpPath = ctx.profile.GetPath(ProfilePath::Dumps);
        std::error_code error{};
        std::filesystem::create_directories(dumpPath, error);
        if (error) {
            devlog::warn<grp::base>("Could not create dump directory {}: {}", dumpPath, error.message());
            return;
        }
        auto writeFile = [&](const std::filesystem::path &path, auto dump) {
            std::ofstream out(path, std::ios::binary);
            if (!out) {
                return false;
            }
            dump(out);
            out.close();
            return static_cast<bool>(out);
        };
        const bool vramWritten = writeFile(dumpPath / "vdp2-vram.bin", [&](auto &out) {
            saturn.VDP.DumpVDP2VRAM(out);
        });
        const bool cramWritten = writeFile(dumpPath / "vdp2-cram.bin", [&](auto &out) {
            saturn.VDP.DumpVDP2CRAM(out);
        });
        const bool registersWritten = writeFile(dumpPath / "vdp2-registers.bin", [&](auto &out) {
            saturn.VDP.DumpVDP2Registers(out);
        });
        const bool traceWritten = writeFile(dumpPath / "vdp2-memory-write-trace.csv", [&](auto &out) {
            out << "vdp1_frame,memory,address,width,value\n";
            for (const auto &entry : traceSnapshot) {
                if (entry.vdp1Frame == ymir::debug::IVDP2MemoryTracer::kUnknownVdp1FrameIndex) {
                    out << "unknown";
                } else {
                    out << entry.vdp1Frame;
                }
                out << ',' << (entry.type == ymir::debug::VDP2MemoryType::VRAM ? "VRAM" : "CRAM") << ",0x"
                    << std::hex << entry.address << std::dec << ',' << static_cast<unsigned>(entry.width) << ",0x"
                    << std::hex << entry.value << std::dec << '\n';
            }
        });
        std::ofstream manifest(dumpPath / "vdp2-memory-write-bundle.txt");
        if (!manifest) {
            devlog::warn<grp::base>("Could not write VDP2 memory trace receipt in {}", dumpPath);
            return;
        }
        manifest << "format=ymir-vdp2-memory-write-bundle-v1\n"
                 << "current_vdp1_frame_index=";
        if (captureFrame == ymir::debug::IVDP2MemoryTracer::kUnknownVdp1FrameIndex) {
            manifest << "unknown\n";
        } else {
            manifest << captureFrame << '\n';
        }
        manifest << "address_begin=0x" << std::hex << tracer.addressBegin.load(std::memory_order_relaxed) << '\n'
                 << "address_end_exclusive=0x" << tracer.addressEnd.load(std::memory_order_relaxed) << '\n'
                 << std::dec << "trace_vram=" << (tracer.traceVRAM.load(std::memory_order_relaxed) ? 1 : 0) << '\n'
                 << "trace_cram=" << (tracer.traceCRAM.load(std::memory_order_relaxed) ? 1 : 0) << '\n'
                 << "vdp2_vram_file_complete=" << (vramWritten ? 1 : 0) << '\n'
                 << "vdp2_cram_file_complete=" << (cramWritten ? 1 : 0) << '\n'
                 << "trace_file_complete=" << (traceWritten ? 1 : 0) << '\n'
                 << "trace_rows=" << traceSnapshot.size() << '\n'
                 << "trace_capacity_rows=" << tracer.capacity.load(std::memory_order_relaxed) << '\n'
                 << "trace_dropped_total=" << dropped << '\n'
                 << "trace_vdp1_frame_range_enabled=" << (frameRangeEnabled ? 1 : 0) << '\n'
                 << "trace_vdp1_frame_begin=";
        if (frameRangeEnabled) {
            manifest << frameRangeBegin;
        } else {
            manifest << "not_set";
        }
        manifest << '\n' << "trace_vdp1_frame_end_exclusive=";
        if (frameRangeEnabled) {
            manifest << frameRangeEnd;
        } else {
            manifest << "not_set";
        }
        manifest << '\n' << "trace_rows_outside_vdp1_frame_range=" << outOfFrameRange << '\n';
        if (!traceSnapshot.empty()) {
            const auto writeFrame = [&](std::string_view key, uint64 frame) {
                manifest << key << '=';
                if (frame == ymir::debug::IVDP2MemoryTracer::kUnknownVdp1FrameIndex) {
                    manifest << "unknown\n";
                } else {
                    manifest << frame << '\n';
                }
            };
            writeFrame("first_vdp1_frame_index", traceSnapshot.front().vdp1Frame);
            writeFrame("last_vdp1_frame_index", traceSnapshot.back().vdp1Frame);
        }
        manifest << "bundle_complete=" << (vramWritten && cramWritten && registersWritten && traceWritten ? 1 : 0)
                 << '\n';
        manifest.close();
        if (!vramWritten || !cramWritten || !registersWritten || !traceWritten || !manifest) {
            devlog::warn<grp::base>("VDP2 memory trace bundle is incomplete in {}", dumpPath);
        } else {
            ctx.DisplayMessage("Exported synchronized VDP2 memory trace bundle");
        }
    });
}

namespace {

std::pair<uint64, uint64> SH2WriteFrameBounds(const SH2Tracer &master, const SH2Tracer &slave) {
    uint64 minimum = ~uint64{0};
    uint64 maximum = 0;
    for (const SH2Tracer *tracer : {&master, &slave}) {
        for (const auto &entry : tracer->memoryWrites) {
            if (entry.vdp1Frame != ~uint64{0}) {
                minimum = std::min(minimum, entry.vdp1Frame);
                maximum = std::max(maximum, entry.vdp1Frame);
            }
        }
    }
    if (minimum == ~uint64{0}) {
        maximum = ~uint64{0};
    }
    return {minimum, maximum};
}

void WriteSH2FrameReceipt(std::ostream &out, const SH2Tracer &master, const SH2Tracer &slave) {
    const auto [minimum, maximum] = SH2WriteFrameBounds(master, slave);
    const auto writeFrame = [&](std::string_view key, uint64 frame) {
        out << key << '=';
        if (frame == ~uint64{0}) {
            out << "unknown\n";
        } else {
            out << frame << '\n';
        }
    };
    writeFrame("sh2_ram_write_frame_min", minimum);
    writeFrame("sh2_ram_write_frame_max", maximum);
    out << "sh2_ram_write_frame_range_enabled=" << (master.memoryWriteFrameRangeEnabled ? 1 : 0) << '\n';
    if (master.memoryWriteFrameRangeEnabled) {
        out << "sh2_ram_write_frame_begin=" << master.memoryWriteFrameBegin << '\n'
            << "sh2_ram_write_frame_end_exclusive=" << master.memoryWriteFrameEnd << '\n';
    }
}

void DumpAllMemory(SharedContext &ctx) {
    // Sample the VDP cursor and trace on the emulation thread before dumping
    // memory, so the bundle identifies the exact captured frame.
    const auto &vdp = ctx.saturn.instance->VDP;
    const auto &vdp1Regs = vdp.GetProbe().GetVDP1Regs();
    const auto &vdp1State = vdp.GetProbe().GetVDP1State();
    const uint64 captureFrame = vdp.GetVDP1FrameIndex();
    const uint32 captureCommandIndex = vdp.GetVDP1CommandIndex();
    const uint32 captureCommandAddress = vdp1Regs.currCommandAddress;
    const sint32 captureLocalX = vdp1State.localCoordX;
    const sint32 captureLocalY = vdp1State.localCoordY;
    const auto &tracer = ctx.tracers.VDP;
    std::vector<VDPTracer::Entry> traceSnapshot;
    uint64 traceDropped = 0;
    const bool traceEnabled = tracer.enabled.load(std::memory_order_relaxed);
    {
        std::lock_guard lock(tracer.mutex);
        traceSnapshot.assign(tracer.entries.begin(), tracer.entries.end());
        traceDropped = tracer.dropped;
    }
    const auto &vdp2MemoryTracer = ctx.tracers.VDP2Memory;
    std::vector<VDP2MemoryTracer::Entry> vdp2MemoryTraceSnapshot;
    uint64 vdp2MemoryTraceDropped = 0;
    const bool vdp2MemoryTraceEnabled = vdp2MemoryTracer.enabled.load(std::memory_order_relaxed);
    const bool vdp2FrameRangeEnabled = vdp2MemoryTracer.traceVdp1FrameRange.load(std::memory_order_relaxed);
    const uint64 vdp2FrameRangeBegin = vdp2MemoryTracer.vdp1FrameBegin.load(std::memory_order_relaxed);
    const uint64 vdp2FrameRangeEnd = vdp2MemoryTracer.vdp1FrameEnd.load(std::memory_order_relaxed);
    const uint64 vdp2RowsOutsideFrameRange = vdp2MemoryTracer.outOfFrameRange.load(std::memory_order_relaxed);
    const uint64 vdp2MemoryTraceFrame = vdp.GetVDP2MemoryTraceFrameIndex();
    if (vdp2MemoryTraceEnabled) {
        std::lock_guard lock(vdp2MemoryTracer.mutex);
        vdp2MemoryTraceSnapshot.assign(vdp2MemoryTracer.entries.begin(), vdp2MemoryTracer.entries.end());
        vdp2MemoryTraceDropped = vdp2MemoryTracer.dropped;
    }

    auto dumpPath = ctx.profile.GetPath(ProfilePath::Dumps);
    std::error_code error{};
    std::filesystem::create_directories(dumpPath, error);
    if (error) {
        devlog::warn<grp::base>("Could not create dump directory {}: {}", dumpPath, error.message());
        return;
    }

    devlog::info<grp::base>("Dumping all memory to {}...", dumpPath);
    {
        std::ofstream out{dumpPath / "msh2-cache-data.bin", std::ios::binary};
        ctx.saturn.instance->masterSH2.DumpCacheData(out);
    }
    {
        std::ofstream out{dumpPath / "msh2-cache-addrtag.bin", std::ios::binary};
        ctx.saturn.instance->masterSH2.DumpCacheAddressTag(out);
    }
    {
        std::ofstream out{dumpPath / "ssh2-cache-data.bin", std::ios::binary};
        ctx.saturn.instance->slaveSH2.DumpCacheData(out);
    }
    {
        std::ofstream out{dumpPath / "ssh2-cache-addrtag.bin", std::ios::binary};
        ctx.saturn.instance->slaveSH2.DumpCacheAddressTag(out);
    }
    {
        std::ofstream out{dumpPath / "wram-lo.bin", std::ios::binary};
        ctx.saturn.instance->mem.DumpWRAMLow(out);
    }
    {
        std::ofstream out{dumpPath / "wram-hi.bin", std::ios::binary};
        ctx.saturn.instance->mem.DumpWRAMHigh(out);
    }
    {
        std::ofstream out{dumpPath / "vdp1-vram.bin", std::ios::binary};
        ctx.saturn.instance->VDP.DumpVDP1VRAM(out);
    }
    {
        std::ofstream out{dumpPath / "vdp1-registers.bin", std::ios::binary};
        ctx.saturn.instance->VDP.DumpVDP1Registers(out);
    }
    {
        std::ofstream out{dumpPath / "vdp1-fbs.bin", std::ios::binary};
        ctx.saturn.instance->VDP.DumpVDP1Framebuffers(out);
    }
    bool vdp2VRAMWritten = false;
    {
        std::ofstream out{dumpPath / "vdp2-vram.bin", std::ios::binary};
        if (out) {
            ctx.saturn.instance->VDP.DumpVDP2VRAM(out);
            out.close();
            vdp2VRAMWritten = static_cast<bool>(out);
        }
    }
    bool vdp2CRAMWritten = false;
    {
        std::ofstream out{dumpPath / "vdp2-cram.bin", std::ios::binary};
        if (out) {
            ctx.saturn.instance->VDP.DumpVDP2CRAM(out);
            out.close();
            vdp2CRAMWritten = static_cast<bool>(out);
        }
    }
    bool vdp2RegistersWritten = false;
    {
        std::ofstream out{dumpPath / "vdp2-registers.bin", std::ios::binary};
        if (out) {
            ctx.saturn.instance->VDP.DumpVDP2Registers(out);
            out.close();
            vdp2RegistersWritten = static_cast<bool>(out);
        }
    }
    if (vdp2MemoryTraceEnabled || ctx.tracers.SCU.traceDMA) {
        std::ofstream traceOut{dumpPath / "vdp2-memory-write-trace.csv"};
        bool traceWritten = false;
        if (!traceOut) {
            devlog::warn<grp::base>("Could not write VDP2 memory trace in {}", dumpPath);
        } else {
            traceOut << "vdp1_frame,memory,address,width,value\n";
            for (const auto &entry : vdp2MemoryTraceSnapshot) {
                if (entry.vdp1Frame == ymir::debug::IVDP2MemoryTracer::kUnknownVdp1FrameIndex) {
                    traceOut << "unknown";
                } else {
                    traceOut << entry.vdp1Frame;
                }
                traceOut << ',' << (entry.type == ymir::debug::VDP2MemoryType::VRAM ? "VRAM" : "CRAM")
                         << ",0x" << std::hex << entry.address << std::dec << ','
                         << static_cast<unsigned>(entry.width) << ",0x" << std::hex << entry.value << std::dec
                         << '\n';
            }
            traceOut.close();
            traceWritten = static_cast<bool>(traceOut);
        }
        std::ofstream manifest{dumpPath / "vdp2-memory-write-bundle.txt"};
        manifest << "format=ymir-vdp2-memory-write-bundle-v1\n"
                 << "current_vdp1_frame_index=" << vdp2MemoryTraceFrame << '\n'
                 << "address_begin=0x" << std::hex
                 << vdp2MemoryTracer.addressBegin.load(std::memory_order_relaxed) << '\n'
                 << "address_end_exclusive=0x" << vdp2MemoryTracer.addressEnd.load(std::memory_order_relaxed) << '\n'
                 << std::dec << "trace_vram="
                 << (vdp2MemoryTracer.traceVRAM.load(std::memory_order_relaxed) ? 1 : 0) << '\n'
                 << "trace_cram=" << (vdp2MemoryTracer.traceCRAM.load(std::memory_order_relaxed) ? 1 : 0) << '\n'
                 << "trace_file_complete=" << (traceWritten ? 1 : 0) << '\n'
                 << "vdp2_vram_file_complete=" << (vdp2VRAMWritten ? 1 : 0) << '\n'
                 << "vdp2_cram_file_complete=" << (vdp2CRAMWritten ? 1 : 0) << '\n'
                 << "vdp2_register_file_complete=" << (vdp2RegistersWritten ? 1 : 0) << '\n'
                 << "trace_rows=" << vdp2MemoryTraceSnapshot.size() << '\n'
                 << "trace_capacity_rows=" << vdp2MemoryTracer.capacity.load(std::memory_order_relaxed) << '\n'
                 << "trace_dropped_total=" << vdp2MemoryTraceDropped << '\n'
                 << "trace_vdp1_frame_range_enabled=" << (vdp2FrameRangeEnabled ? 1 : 0) << '\n'
                 << "trace_vdp1_frame_begin=";
        if (vdp2FrameRangeEnabled) {
            manifest << vdp2FrameRangeBegin;
        } else {
            manifest << "not_set";
        }
        manifest << '\n' << "trace_vdp1_frame_end_exclusive=";
        if (vdp2FrameRangeEnabled) {
            manifest << vdp2FrameRangeEnd;
        } else {
            manifest << "not_set";
        }
        manifest << '\n' << "trace_rows_outside_vdp1_frame_range=" << vdp2RowsOutsideFrameRange << '\n';
        if (!vdp2MemoryTraceSnapshot.empty()) {
            const auto writeFrame = [&](std::string_view key, uint64 frameIndex) {
                manifest << key << '=';
                if (frameIndex == ymir::debug::IVDP2MemoryTracer::kUnknownVdp1FrameIndex) {
                    manifest << "unknown\n";
                } else {
                    manifest << frameIndex << '\n';
                }
            };
            writeFrame("first_vdp1_frame_index", vdp2MemoryTraceSnapshot.front().vdp1Frame);
            writeFrame("last_vdp1_frame_index", vdp2MemoryTraceSnapshot.back().vdp1Frame);
        }
        manifest << "bundle_complete="
                 << (traceWritten && vdp2VRAMWritten && vdp2CRAMWritten && vdp2RegistersWritten ? 1 : 0) << '\n';

        std::ofstream dmaOut{dumpPath / "sh2-dma-trace.csv"};
        bool dmaWritten = false;
        size_t dmaRows = 0;
        if (!dmaOut) {
            devlog::warn<grp::base>("Could not write SH-2 DMA trace in {}", dumpPath);
        } else {
            dmaOut << "sh2,channel,transfer,source_address,destination_address,count,unit_size,source_increment,"
                      "destination_increment,finished,irq_raised\n";
            const auto writeDMATracer = [&](std::string_view name, const SH2Tracer &tracer) {
                for (uint32 channel = 0; channel < tracer.dmaTransfers.size(); channel++) {
                    const auto &transfers = tracer.dmaTransfers[channel];
                    for (size_t i = 0; i < transfers.Count(); i++) {
                        const auto entry = transfers.Read(i);
                        dmaOut << name << ',' << channel << ',' << entry.counter << ",0x" << std::hex
                               << entry.srcAddress << ",0x" << entry.dstAddress << std::dec << ',' << entry.count
                               << ',' << entry.unitSize << ',' << entry.srcInc << ',' << entry.dstInc << ','
                               << (entry.finished ? 1 : 0) << ',' << (entry.irqRaised ? 1 : 0) << '\n';
                        dmaRows++;
                    }
                }
            };
            writeDMATracer("master", ctx.tracers.masterSH2);
            writeDMATracer("slave", ctx.tracers.slaveSH2);
            dmaOut.close();
            dmaWritten = static_cast<bool>(dmaOut);
        }
        std::ofstream dmaManifest{dumpPath / "sh2-dma-trace-bundle.txt"};
        dmaManifest << "format=ymir-sh2-dma-trace-bundle-v1\n"
                    << "trace_file=sh2-dma-trace.csv\n"
                    << "trace_file_complete=" << (dmaWritten ? 1 : 0) << '\n'
                    << "trace_rows=" << dmaRows << '\n'
                    << "bundle_complete=" << (dmaWritten ? 1 : 0) << '\n';
        const auto writeDMAStats = [&](std::string_view name, const SH2Tracer &tracer) {
            for (uint32 channel = 0; channel < tracer.dmaTransfers.size(); channel++) {
                const auto &stats = tracer.dmaStats[channel];
                dmaManifest << name << "_channel_" << channel << "_retained="
                            << tracer.dmaTransfers[channel].Count() << '\n'
                            << name << "_channel_" << channel << "_total=" << stats.numTransfers << '\n'
                            << name << "_channel_" << channel << "_bytes=" << stats.bytesTransferred << '\n'
                            << name << "_channel_" << channel << "_interrupts=" << stats.interrupts << '\n';
            }
        };
        writeDMAStats("master", ctx.tracers.masterSH2);
        writeDMAStats("slave", ctx.tracers.slaveSH2);

        std::ofstream scuDMAOut{dumpPath / "scu-dma-trace.csv"};
        bool scuDMAWritten = false;
        if (!scuDMAOut) {
            devlog::warn<grp::base>("Could not write SCU DMA trace in {}", dumpPath);
        } else {
            scuDMAOut << "transfer,channel,source_address,destination_address,count,source_increment,"
                         "destination_increment,indirect,indirect_address\n";
            const auto &transfers = ctx.tracers.SCU.dmaTransfers;
            const auto &transferHistory = ctx.tracers.SCU.dmaTransferHistory;
            const size_t transferCount = ctx.tracers.SCU.traceDMAData ? transferHistory.size() : transfers.Count();
            for (size_t i = 0; i < transferCount; i++) {
                const auto entry = ctx.tracers.SCU.traceDMAData ? transferHistory[i] : transfers.Read(i);
                scuDMAOut << entry.counter << ',' << static_cast<unsigned>(entry.channel) << ",0x" << std::hex
                          << entry.srcAddr << ",0x" << entry.dstAddr << std::dec << ',' << entry.xferCount << ','
                          << entry.srcAddrInc << ',' << entry.dstAddrInc << ',' << (entry.indirect ? 1 : 0) << ",0x"
                          << std::hex << entry.indirectAddr << std::dec << '\n';
            }
            scuDMAOut.close();
            scuDMAWritten = static_cast<bool>(scuDMAOut);
        }
        std::ofstream scuDMAManifest{dumpPath / "scu-dma-trace-bundle.txt"};
        const bool scuDMAFullHistory = ctx.tracers.SCU.traceDMAData;
        const size_t scuDMARetainedRows =
            scuDMAFullHistory ? ctx.tracers.SCU.dmaTransferHistory.size() : ctx.tracers.SCU.dmaTransfers.Count();
        const size_t scuDMADroppedRows = scuDMAFullHistory ? ctx.tracers.SCU.dmaTransferHistoryDropped : 0;
        scuDMAManifest << "format=ymir-scu-dma-trace-bundle-v1\n"
                       << "trace_file=scu-dma-trace.csv\n"
                       << "trace_file_complete=" << (scuDMAWritten ? 1 : 0) << '\n'
                       << "capture_mode=" << (scuDMAFullHistory ? "full_history" : "ring_buffer") << '\n'
                       << "retained_rows=" << scuDMARetainedRows << '\n'
                       << "retention_capacity="
                       << (scuDMAFullHistory ? app::SCUTracer::kMaxDMATransferHistory
                                             : ctx.tracers.SCU.dmaTransfers.Capacity())
                       << '\n';
        if (scuDMAFullHistory) {
            scuDMAManifest << "dropped_rows=" << scuDMADroppedRows << '\n'
                           << "trace_complete=" << (scuDMADroppedRows == 0 ? 1 : 0) << '\n'
                           << "overflow=" << (scuDMADroppedRows == 0 ? "none" : "known") << '\n';
        } else {
            scuDMAManifest << "dropped_rows=unknown\ntrace_complete=unknown\noverflow=unknown\n";
        }
        scuDMAManifest << "bundle_complete=" << (scuDMAWritten ? 1 : 0) << '\n';

        if (ctx.tracers.SCU.traceDMAData) {
            std::ofstream payloadOut{dumpPath / "scu-dma-payload-trace.csv"};
            bool payloadWritten = false;
            if (!payloadOut) {
                devlog::warn<grp::base>("Could not write SCU DMA payload trace in {}", dumpPath);
            } else {
                payloadOut << "transfer,channel,destination_address,width,value\n";
                for (const auto &entry : ctx.tracers.SCU.dmaDataWrites) {
                    payloadOut << entry.transfer << ',' << static_cast<unsigned>(entry.channel) << ",0x" << std::hex
                               << entry.address << std::dec << ',' << static_cast<unsigned>(entry.width) << ",0x"
                               << std::hex << entry.value << std::dec << '\n';
                }
                payloadOut.close();
                payloadWritten = static_cast<bool>(payloadOut);
            }
            std::ofstream payloadManifest{dumpPath / "scu-dma-payload-trace-bundle.txt"};
            payloadManifest << "format=ymir-scu-dma-payload-trace-bundle-v1\n"
                            << "trace_file=scu-dma-payload-trace.csv\n"
                            << "trace_file_complete=" << (payloadWritten ? 1 : 0) << '\n'
                            << "address_begin=0x" << std::hex << ctx.tracers.SCU.dmaDataAddressBegin << '\n'
                            << "address_end_exclusive=0x" << ctx.tracers.SCU.dmaDataAddressEnd << std::dec << '\n'
                            << "retained_rows=" << ctx.tracers.SCU.dmaDataWrites.size() << '\n'
                            << "retention_capacity=" << ctx.tracers.SCU.maxDMADataWrites << '\n'
                            << "dropped_rows=" << ctx.tracers.SCU.dmaDataWritesDropped << '\n'
                            << "trace_complete=" << (ctx.tracers.SCU.dmaDataWritesDropped == 0 ? 1 : 0) << '\n'
                            << "transfer_trace_complete=" << (scuDMAFullHistory && scuDMADroppedRows == 0 ? 1 : 0)
                            << '\n'
                            << "bundle_complete=" << (payloadWritten ? 1 : 0) << '\n';
        }

        std::ofstream sh2MemoryOut{dumpPath / "sh2-vdp2-write-trace.csv"};
        bool sh2MemoryWritten = false;
        size_t sh2MemoryRows = 0;
        if (!sh2MemoryOut) {
            devlog::warn<grp::base>("Could not write SH-2 VDP2 write trace in {}", dumpPath);
        } else {
            sh2MemoryOut << "sh2,vdp1_frame,pc,caller_return_address,caller_target,address,width,value,caller_stack_targets\n";
            const auto writeMemoryTracer = [&](std::string_view name, const SH2Tracer &tracer) {
                for (const auto &entry : tracer.memoryWrites) {
                    sh2MemoryOut << name << ',';
                    if (entry.vdp1Frame == ~uint64{0}) {
                        sh2MemoryOut << "unknown";
                    } else {
                        sh2MemoryOut << entry.vdp1Frame;
                    }
                    sh2MemoryOut << ",0x" << std::hex << entry.pc << ",0x" << entry.callerReturnAddress
                                 << ",0x" << entry.callerTarget << ",0x" << entry.address << std::dec << ','
                                 << static_cast<unsigned>(entry.width) << ",0x" << std::hex << entry.value << std::dec
                                 << ',';
                    for (uint8 i = 0; i < entry.callerTargetCount; i++) {
                        if (i != 0) {
                            sh2MemoryOut << '|';
                        }
                        sh2MemoryOut << "0x" << std::hex << entry.callerTargets[i] << std::dec;
                    }
                    sh2MemoryOut << '\n';
                    sh2MemoryRows++;
                }
            };
            writeMemoryTracer("master", ctx.tracers.masterSH2);
            writeMemoryTracer("slave", ctx.tracers.slaveSH2);
            sh2MemoryOut.close();
            sh2MemoryWritten = static_cast<bool>(sh2MemoryOut);
        }
        std::ofstream sh2MemoryManifest{dumpPath / "sh2-vdp2-write-trace-bundle.txt"};
        sh2MemoryManifest << "format=ymir-sh2-vdp2-write-trace-bundle-v1\n"
                          << "trace_file=sh2-vdp2-write-trace.csv\n"
                          << "trace_file_complete=" << (sh2MemoryWritten ? 1 : 0) << '\n'
                          << "trace_rows=" << sh2MemoryRows << '\n'
                          << "capacity_rows=" << ctx.tracers.masterSH2.memoryWriteMaxRows << '\n'
                          << "address_begin=0x" << std::hex << ctx.tracers.masterSH2.memoryWriteAddressBegin << '\n'
                          << "address_end_exclusive=0x" << ctx.tracers.masterSH2.memoryWriteAddressEnd << std::dec << '\n'
                          << "master_dropped=" << ctx.tracers.masterSH2.memoryWritesDropped << '\n'
                          << "slave_dropped=" << ctx.tracers.slaveSH2.memoryWritesDropped << '\n'
                          << "bundle_complete=" << (sh2MemoryWritten ? 1 : 0) << '\n';
        WriteSH2FrameReceipt(sh2MemoryManifest, ctx.tracers.masterSH2, ctx.tracers.slaveSH2);
    }
    const bool sh2MemoryReadTraceEnabled = ctx.tracers.masterSH2.traceMemoryReads
                                           || ctx.tracers.slaveSH2.traceMemoryReads;
    const bool sh2MemoryWriteTraceEnabled = ctx.tracers.masterSH2.traceMemoryWrites
                                            || ctx.tracers.slaveSH2.traceMemoryWrites;
    if (sh2MemoryWriteTraceEnabled) {
        std::ofstream out{dumpPath / "sh2-memory-write-trace.csv"};
        bool written = false;
        size_t rows = 0;
        if (!out) {
            devlog::warn<grp::base>("Could not write SH-2 memory write trace in {}", dumpPath);
        } else {
            out << "sh2,vdp1_frame,pc,caller_return_address,caller_target,address,width,value,caller_stack_targets\n";
            const auto writeTracer = [&](std::string_view name, const SH2Tracer &tracer) {
                for (const auto &entry : tracer.memoryWrites) {
                    out << name << ',';
                    if (entry.vdp1Frame == ~uint64{0}) {
                        out << "unknown";
                    } else {
                        out << entry.vdp1Frame;
                    }
                    out << ",0x" << std::hex << entry.pc << ",0x" << entry.callerReturnAddress << ",0x"
                        << entry.callerTarget << ",0x" << entry.address << std::dec << ','
                        << static_cast<unsigned>(entry.width) << ",0x" << std::hex << entry.value << std::dec << ',';
                    for (uint8 i = 0; i < entry.callerTargetCount; i++) {
                        if (i != 0) {
                            out << '|';
                        }
                        out << "0x" << std::hex << entry.callerTargets[i] << std::dec;
                    }
                    out << '\n';
                    rows++;
                }
            };
            writeTracer("master", ctx.tracers.masterSH2);
            writeTracer("slave", ctx.tracers.slaveSH2);
            out.close();
            written = static_cast<bool>(out);
        }
        std::ofstream manifest{dumpPath / "sh2-memory-write-trace-bundle.txt"};
        manifest << "format=ymir-sh2-memory-write-trace-bundle-v1\n"
                 << "trace_file=sh2-memory-write-trace.csv\n"
                 << "trace_file_complete=" << (written ? 1 : 0) << '\n'
                 << "address_begin=0x" << std::hex << ctx.tracers.masterSH2.memoryWriteAddressBegin << '\n'
                 << "address_end_exclusive=0x" << ctx.tracers.masterSH2.memoryWriteAddressEnd << std::dec << '\n'
                 << "trace_rows=" << rows << '\n'
                 << "capacity_rows=" << ctx.tracers.masterSH2.memoryWriteMaxRows << '\n'
                 << "master_dropped=" << ctx.tracers.masterSH2.memoryWritesDropped << '\n'
                 << "slave_dropped=" << ctx.tracers.slaveSH2.memoryWritesDropped << '\n'
                 << "bundle_complete=" << (written ? 1 : 0) << '\n';
        WriteSH2FrameReceipt(manifest, ctx.tracers.masterSH2, ctx.tracers.slaveSH2);
    }
    if (sh2MemoryReadTraceEnabled) {
        std::ofstream out{dumpPath / "sh2-memory-read-trace.csv"};
        bool written = false;
        size_t rows = 0;
        if (!out) {
            devlog::warn<grp::base>("Could not write SH-2 memory read trace in {}", dumpPath);
        } else {
            out << "sh2,pc,caller_return_address,caller_target,address,width\n";
            const auto writeTracer = [&](std::string_view name, const SH2Tracer &tracer) {
                for (size_t i = 0; i < tracer.memoryReads.Count(); i++) {
                    const auto entry = tracer.memoryReads.Read(i);
                    out << name << ",0x" << std::hex << entry.pc << ",0x" << entry.callerReturnAddress << ",0x"
                        << entry.callerTarget << ",0x" << entry.address << std::dec << ','
                        << static_cast<unsigned>(entry.width) << '\n';
                    rows++;
                }
            };
            writeTracer("master", ctx.tracers.masterSH2);
            writeTracer("slave", ctx.tracers.slaveSH2);
            out.close();
            written = static_cast<bool>(out);
        }
        std::ofstream manifest{dumpPath / "sh2-memory-read-trace-bundle.txt"};
        manifest << "format=ymir-sh2-memory-read-trace-bundle-v1\n"
                 << "trace_file=sh2-memory-read-trace.csv\n"
                 << "trace_file_complete=" << (written ? 1 : 0) << '\n'
                 << "address_begin=0x" << std::hex << ctx.tracers.masterSH2.memoryReadAddressBegin << '\n'
                 << "address_end_exclusive=0x" << ctx.tracers.masterSH2.memoryReadAddressEnd << std::dec << '\n'
                 << "trace_rows=" << rows << '\n'
                 << "master_dropped=" << ctx.tracers.masterSH2.memoryReadsDropped << '\n'
                 << "slave_dropped=" << ctx.tracers.slaveSH2.memoryReadsDropped << '\n'
                 << "bundle_complete=" << (written ? 1 : 0) << '\n';
    }
    {
        std::ofstream out{dumpPath / "vdp-register-snapshot.txt"};
        out << "format=ymir-vdp-register-snapshot-v1\n"
            << "register_byte_order=big_endian\n"
            << "register_word_bytes=2\n"
            << "vdp1_register_address_base=0x00000000\n"
            << "vdp1_register_bytes=32\n"
            << "vdp2_register_address_base=0x00000000\n"
            << "vdp2_register_bytes=512\n"
            << "vdp1_frame_index=" << captureFrame << '\n'
            << "vdp1_next_command_index=" << captureCommandIndex << '\n'
            << "vdp1_current_command_address=" << captureCommandAddress << '\n'
            << "vdp1_local_x=" << captureLocalX << '\n'
            << "vdp1_local_y=" << captureLocalY << '\n';
    }
    {
        std::ofstream out{dumpPath / "vdp1-command-trace.csv"};
        bool traceWritten = false;
        size_t traceRowsForCaptureFrame = 0;
        if (!out) {
            devlog::warn<grp::base>("Could not write VDP1 command trace in {}", dumpPath);
        } else {
            out << "frame,index,address";
            for (size_t i = 0; i < 16; ++i) {
                out << ",word" << i;
            }
            out << ",frame_root,local_x,local_y\n";
            for (const auto &entry : traceSnapshot) {
                if (entry.frame == captureFrame) {
                    ++traceRowsForCaptureFrame;
                }
                out << entry.frame << ',' << entry.index << ",0x" << std::hex << entry.address << std::dec;
                for (uint16 word : entry.words) {
                    out << ",0x" << std::hex << word << std::dec;
                }
                out << ",0x" << std::hex << entry.rootAddress << std::dec << ',' << entry.localX << ','
                    << entry.localY << '\n';
            }
            out.close();
            traceWritten = static_cast<bool>(out);
        }
        std::ofstream manifest{dumpPath / "vdp1-capture-bundle.txt"};
        manifest << "format=ymir-vdp1-capture-bundle-v1\n"
                 << "capture_frame_index=" << captureFrame << '\n'
                 << "capture_next_command_index=" << captureCommandIndex << '\n'
                 << "capture_current_command_address=" << captureCommandAddress << '\n'
                 << "capture_local_x=" << captureLocalX << '\n'
                 << "capture_local_y=" << captureLocalY << '\n'
                 << "trace_enabled=" << (traceEnabled ? 1 : 0) << '\n'
                 << "trace_file=vdp1-command-trace.csv\n"
                 << "trace_file_complete=" << (traceWritten ? 1 : 0) << '\n'
                 << "trace_rows=" << traceSnapshot.size() << '\n'
                 << "trace_rows_for_capture_frame=" << traceRowsForCaptureFrame << '\n'
                 << "trace_dropped_total=" << traceDropped << '\n';
        if (!traceSnapshot.empty()) {
            manifest << "trace_first_frame=" << traceSnapshot.front().frame << '\n'
                     << "trace_last_frame=" << traceSnapshot.back().frame << '\n';
        }
    }
    {
        std::ofstream out{dumpPath / "scu-dsp-prog.bin", std::ios::binary};
        ctx.saturn.instance->SCU.DumpDSPProgramRAM(out);
    }
    {
        std::ofstream out{dumpPath / "scu-dsp-data.bin", std::ios::binary};
        ctx.saturn.instance->SCU.DumpDSPDataRAM(out);
    }
    {
        std::ofstream out{dumpPath / "scu-dsp-regs.bin", std::ios::binary};
        ctx.saturn.instance->SCU.DumpDSPRegs(out);
    }
    {
        std::ofstream out{dumpPath / "scsp-wram.bin", std::ios::binary};
        ctx.saturn.instance->SCSP.DumpWRAM(out);
    }
    {
        std::ofstream out{dumpPath / "scsp-dsp-mpro.bin", std::ios::binary};
        ctx.saturn.instance->SCSP.DumpDSP_MPRO(out);
    }
    {
        std::ofstream out{dumpPath / "scsp-dsp-temp.bin", std::ios::binary};
        ctx.saturn.instance->SCSP.DumpDSP_TEMP(out);
    }
    {
        std::ofstream out{dumpPath / "scsp-dsp-mems.bin", std::ios::binary};
        ctx.saturn.instance->SCSP.DumpDSP_MEMS(out);
    }
    {
        std::ofstream out{dumpPath / "scsp-dsp-coef.bin", std::ios::binary};
        ctx.saturn.instance->SCSP.DumpDSP_COEF(out);
    }
    {
        std::ofstream out{dumpPath / "scsp-dsp-madrs.bin", std::ios::binary};
        ctx.saturn.instance->SCSP.DumpDSP_MADRS(out);
    }
    {
        std::ofstream out{dumpPath / "scsp-dsp-mixs.bin", std::ios::binary};
        ctx.saturn.instance->SCSP.DumpDSP_MIXS(out);
    }
    {
        std::ofstream out{dumpPath / "scsp-dsp-efreg.bin", std::ios::binary};
        ctx.saturn.instance->SCSP.DumpDSP_EFREG(out);
    }
    {
        std::ofstream out{dumpPath / "scsp-dsp-exts.bin", std::ios::binary};
        ctx.saturn.instance->SCSP.DumpDSP_EXTS(out);
    }
    {
        std::ofstream out{dumpPath / "scsp-dsp-regs.bin", std::ios::binary};
        ctx.saturn.instance->SCSP.DumpDSPRegs(out);
    }
    {
        std::ofstream out{dumpPath / "sh1-ram.bin", std::ios::binary};
        ctx.saturn.instance->SH1.DumpRAM(out);
    }
    {
        std::ofstream out{dumpPath / "cdb-dram.bin", std::ios::binary};
        ctx.saturn.instance->DumpCDBlockDRAM(out);
    }
    devlog::info<grp::base>("Dump complete");
}

} // namespace

EmuEvent DumpMemory() {
    return RunFunction(DumpAllMemory);
}

EmuEvent RunFramesAndDump(uint32 frameCount, uint32 controlPadStartFrame, uint32 controlPadStartHoldFrames,
                          uint32 controlPadStartRepeatFrame, uint32 controlPadStartRepeatHoldFrames,
                          uint32 controlPadAFrame, uint32 controlPadAHoldFrames, uint32 controlPadCFrame,
                          uint32 controlPadCHoldFrames, uint32 controlPadARepeatFrame,
                          uint32 controlPadARepeatHoldFrames, uint32 controlPadDownFrame,
                          uint32 controlPadDownHoldFrames,
                          std::vector<ScheduledControlPadInput> controlPadInputs, bool captureScreenshot, bool quitWhenComplete,
                          uint32 saveStateSlot, bool captureNoRender) {
    return RunFunction([=](SharedContext &ctx) {
        // Loading a state rebuilds VDP and clears its optional tracer pointer.
        // Reattach it immediately before the CLI-owned capture frames.
        if (ctx.saturn.instance->IsDebugTracingEnabled()) {
            ctx.tracers.VDP.enabled.store(true, std::memory_order_relaxed);
            ctx.saturn.instance->VDP.UseTracer(&ctx.tracers.VDP);
            for (auto *tracer : {&ctx.tracers.masterSH2, &ctx.tracers.slaveSH2}) {
                tracer->currentVDP1FrameIndex = &ctx.tracers.VDP.currentFrame;
            }
            if (ctx.tracers.VDP2Memory.enabled.load(std::memory_order_relaxed)) {
                ctx.saturn.instance->VDP.UseVDP2MemoryTracer(&ctx.tracers.VDP2Memory);
                ctx.tracers.masterSH2.traceDMA = true;
                ctx.tracers.slaveSH2.traceDMA = true;
                ctx.tracers.masterSH2.ResetDMATraces();
                ctx.tracers.slaveSH2.ResetDMATraces();
                for (auto *tracer : {&ctx.tracers.masterSH2, &ctx.tracers.slaveSH2}) {
                    // VDP2 tracing supplies a useful default SH-2 range, but
                    // retain any explicit range configured by the CLI.
                    if (!tracer->traceMemoryWrites) {
                        tracer->traceMemoryWrites = true;
                        tracer->memoryWriteAddressBegin = 0x05E00000;
                        tracer->memoryWriteAddressEnd = 0x05F01000;
                    }
                    tracer->ResetMemoryWriteTrace();
                }
                // This legacy VDP2 route configures its tracer after debug
                // tracing attached them, so refresh optional categories
                // without resetting execution analysis.
                ctx.saturn.instance->masterSH2.RefreshTracerConfiguration();
                ctx.saturn.instance->slaveSH2.RefreshTracerConfiguration();
                ctx.tracers.SCU.traceDMA = true;
                ctx.tracers.SCU.ClearDMATransfers();
            }
        }
        if (ctx.tracers.SCU.traceDMAData) {
            ctx.tracers.SCU.traceDMA = true;
            ctx.tracers.SCU.ClearDMATransfers();
        }

        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        const bool threadedVDP1 = settings.video.swRenderer.threadedVDP1.Get();
        const bool threadedVDP2 = settings.video.swRenderer.threadedVDP2.Get();
        const bool threadedDeinterlacer = settings.video.swRenderer.threadedDeinterlacer.Get();
        auto &audio = ctx.audioSystem;
        const bool audioSync = audio.IsSync();
        const bool audioSilent = audio.IsSilent();

        // Each capture step must observe the renderer's completed state. The
        // software renderer provides that guarantee only while it is local to
        // this emulator thread.
        settings.video.swRenderer.threadedVDP1 = false;
        settings.video.swRenderer.threadedVDP2 = false;
        settings.video.swRenderer.threadedDeinterlacer = false;
        // Capture owns this synchronous frame interval.  Audio pacing would
        // otherwise limit a no-present capture to the host playback clock.
        audio.SetSync(false);
        audio.SetSilent(true);
        util::ScopeGuard restoreAudio{[&] {
            audio.SetSilent(audioSilent);
            audio.SetSync(audioSync);
        }};
        auto &vdp = ctx.saturn.instance->VDP;
        const auto rendererType = vdp.GetRenderer().GetType();
        if (captureNoRender) {
            auto result = vdp.UseNullRenderer();
            if (!result) {
                ctx.DisplayMessage(fmt::format("Could not enable null VDP renderer: {}", result.Error().message));
                return;
            }
        }
        util::ScopeGuard restoreRenderer{[&] {
            if (captureNoRender && rendererType == ymir::vdp::VDPRendererType::Software) {
                if (auto result = vdp.UseSoftwareRenderer(); !result) {
                    devlog::error<grp::base>("Could not restore software VDP renderer: {}", result.Error().message);
                }
            }
        }};
        using Button = ymir::peripheral::Button;
        auto &controlPad = ctx.controlPadInputs[0];
        auto scheduledInputs = controlPadInputs;
        auto addScheduledInput = [&](uint32 frame, uint32 holdFrames, Button button) {
            if (holdFrames != 0) {
                scheduledInputs.push_back({frame, static_cast<std::uint16_t>(button), holdFrames});
            }
        };
        addScheduledInput(controlPadStartFrame, controlPadStartHoldFrames, Button::Start);
        addScheduledInput(controlPadStartRepeatFrame, controlPadStartRepeatHoldFrames, Button::Start);
        addScheduledInput(controlPadAFrame, controlPadAHoldFrames, Button::A);
        addScheduledInput(controlPadARepeatFrame, controlPadARepeatHoldFrames, Button::A);
        addScheduledInput(controlPadCFrame, controlPadCHoldFrames, Button::C);
        addScheduledInput(controlPadDownFrame, controlPadDownHoldFrames, Button::Down);
        std::uint16_t managedButtons = 0;
        for (const auto &input : scheduledInputs) managedButtons |= input.buttonMask;
        const std::uint16_t initialManagedButtons = static_cast<std::uint16_t>(controlPad.buttons) & managedButtons;
        for (uint32 frame = 0; frame < frameCount; frame++) {
            std::uint16_t pressedButtons = 0;
            for (const auto &input : scheduledInputs) {
                if (frame >= input.frame && frame - input.frame < input.holdFrames) {
                    pressedButtons |= input.buttonMask;
                }
            }
            const std::uint16_t buttons = static_cast<std::uint16_t>(controlPad.buttons);
            controlPad.buttons = static_cast<Button>((buttons & ~managedButtons) | (managedButtons & ~pressedButtons));
            ctx.saturn.instance->RunFrame();
        }
        const std::uint16_t buttons = static_cast<std::uint16_t>(controlPad.buttons);
        controlPad.buttons = static_cast<Button>((buttons & ~managedButtons) | initialManagedButtons);
        DumpAllMemory(ctx);
        if (saveStateSlot != 0) {
            const uint32 slotIndex = saveStateSlot - 1;
            auto &saves = ctx.serviceLocator.GetRequired<services::SaveStateService>();
            {
                auto lock = std::unique_lock{saves.SlotMutex(slotIndex)};
                auto *slot = saves.Push(slotIndex);
                assert(slot != nullptr);
                ctx.saturn.instance->SaveState(*slot->state);
                slot->timestamp = std::chrono::system_clock::now();
            }
            saves.PersistSaveState(slotIndex);
        }
        if (captureScreenshot) {
            ctx.EnqueueEvent(events::gui::TakeScreenshot());
        }
        if (quitWhenComplete) {
            ctx.EnqueueEvent(events::gui::Quit());
        }
        settings.video.swRenderer.threadedVDP1 = threadedVDP1;
        settings.video.swRenderer.threadedVDP2 = threadedVDP2;
        settings.video.swRenderer.threadedDeinterlacer = threadedDeinterlacer;
    });
}

// handler to dump a specified memory region from the mem viewer
EmuEvent DumpMemRegion(const ui::mem_view::MemoryViewerState &memView) {
    return RunFunction([memView](const SharedContext &ctx) {
        // get path from ctx, setup errors, try dir creation
        const auto dumpPath = ctx.profile.GetPath(ProfilePath::Dumps);
        std::error_code ec{};
        std::filesystem::create_directories(dumpPath, ec);
        if (ec) {
            devlog::warn<grp::base>("Could not create dump directory {}: {}", dumpPath, ec.message());
            return;
        }

        // get size from region, check readFn
        const auto *region = memView.selectedRegion;
        if (!region || region->size == 0 || !region->readFn) {
            // could happen for invalid region or if no readFn exists
            devlog::warn<grp::base>("DumpMemRegion: invalid region/readFn/size");
            return;
        }
        const uint32_t size = region->size;

        devlog::info<grp::base>("Dumping memory region {}...", region->name);

        // fill buffer /w same size as mem region
        std::vector<std::uint8_t> buf(size);
        void *userData = memView.memoryEditor.UserData;
        for (uint32_t i = 0; i < size; ++i) {
            buf[i] = region->readFn(nullptr, i, userData);
        }

        // filename sanitizer to be safe
        auto sanitize = [](std::string s) {
            for (char &c : s) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') {
                    c = '_';
                }
            }
            return s;
        };

        // get game product num, setup path
        const auto &productNumber = ctx.saturn.GetDiscHeader().productNumber;
        const auto outPath = dumpPath / fmt::format("{}_{}_{:08X}_{}B.bin", productNumber, sanitize(region->name),
                                                    region->baseAddress, size);

        // write to dump path
        std::ofstream out{outPath, std::ios::binary | std::ios::trunc};
        if (!out) {
            devlog::warn<grp::base>("DumpMemRegion: failed to open {}", outPath);
            return;
        }
        out.write(reinterpret_cast<const char *>(buf.data()), static_cast<std::streamsize>(buf.size()));

        ctx.DisplayMessage(fmt::format("Dumped {} bytes from [{}:{:08X}..{:08X}] to {}", size, region->addressBlockName,
                                       region->baseAddress, region->baseAddress + size - 1, outPath));
    });
}

static void InsertPeripheral(peripheral::PeripheralType type, peripheral::PeripheralPort &port) {
    switch (type) {
    case ymir::peripheral::PeripheralType::None: port.DisconnectPeripherals(); break;
    case ymir::peripheral::PeripheralType::ControlPad: port.ConnectControlPad(); break;
    case ymir::peripheral::PeripheralType::AnalogPad: port.ConnectAnalogPad(); break;
    case ymir::peripheral::PeripheralType::ArcadeRacer: port.ConnectArcadeRacer(); break;
    case ymir::peripheral::PeripheralType::MissionStick: port.ConnectMissionStick(); break;
    case ymir::peripheral::PeripheralType::VirtuaGun: port.ConnectVirtuaGun(); break;
    case ymir::peripheral::PeripheralType::ShuttleMouse: port.ConnectShuttleMouse(); break;
    }
}

EmuEvent InsertPeripheral(uint32 port, ymir::peripheral::PeripheralType type) {
    return RunFunction([=](SharedContext &ctx) {
        std::unique_lock lock{ctx.locks.peripherals};
        InsertPeripheral(type, port == 0 ? ctx.saturn.instance->SMPC.GetPeripheralPort1()
                                         : ctx.saturn.instance->SMPC.GetPeripheralPort2());
    });
}

EmuEvent InsertBackupMemoryCartridge(std::filesystem::path path) {
    return RunFunction([=](SharedContext &ctx) {
        // Prevent loading the internal backup RAM file as backup memory cartridge
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        if (std::filesystem::absolute(path) == std::filesystem::absolute(settings.system.internalBackupRAMImagePath)) {
            ctx.EnqueueEvent(events::gui::ShowError(fmt::format(
                "Failed to load external backup memory: file {} is already in use as internal backup memory", path)));
            return;
        }

        std::error_code error{};
        bup::BackupMemory bupMem{};
        const auto result = bupMem.LoadFrom(path, false, error);
        switch (result) {
        case bup::BackupMemoryImageLoadResult::Success: //
        {
            auto *cart = ctx.saturn.instance->InsertCartridge<cart::BackupMemoryCartridge>(std::move(bupMem));
            settings.cartridge.backupRAM.capacity = SizeToCapacity(cart->GetBackupMemory().Size());
            settings.cartridge.backupRAM.imagePath = path;
            devlog::info<grp::base>("External backup memory cartridge loaded from {}", path);
            break;
        }
        case bup::BackupMemoryImageLoadResult::FilesystemError:
            if (error) {
                ctx.EnqueueEvent(
                    events::gui::ShowError(fmt::format("Failed to load external backup memory: {}", error.message())));
            } else {
                ctx.EnqueueEvent(
                    events::gui::ShowError("Failed to load external backup memory: Unspecified file system error"));
            }
            break;
        case bup::BackupMemoryImageLoadResult::InvalidSize:
            ctx.EnqueueEvent(events::gui::ShowError("Failed to load external backup memory: Invalid image size"));
            break;
        default:
            ctx.EnqueueEvent(events::gui::ShowError("Failed to load external backup memory: Unexpected error"));
            break;
        };
    });
}

EmuEvent Insert8MbitDRAMCartridge() {
    return RunFunction([](SharedContext &ctx) { ctx.saturn.instance->InsertCartridge<cart::DRAM8MbitCartridge>(); });
}

EmuEvent Insert32MbitDRAMCartridge() {
    return RunFunction([](SharedContext &ctx) { ctx.saturn.instance->InsertCartridge<cart::DRAM32MbitCartridge>(); });
}

EmuEvent Insert48MbitDRAMCartridge() {
    return RunFunction([](SharedContext &ctx) { ctx.saturn.instance->InsertCartridge<cart::DRAM48MbitCartridge>(); });
}

EmuEvent InsertROMCartridge(std::filesystem::path path) {
    return RunFunction([=](SharedContext &ctx) {
        // TODO: deduplicate code

        // Don't even bother if no path was specified
        if (path.empty()) {
            return;
        }

        std::error_code error{};
        std::vector<uint8> rom = util::LoadFile(path, error);

        // Check for file system errors
        if (error) {
            ctx.EnqueueEvent(
                events::gui::ShowError(fmt::format("Could not load ROM cartridge image: {}", error.message())));
            return;
        }

        // Check that the file has contents
        if (rom.empty()) {
            ctx.EnqueueEvent(
                events::gui::ShowError("Could not load ROM cartridge image: file is empty or could not be read."));
            return;
        }

        // Check that the image is not larger than the ROM cartridge capacity
        if (rom.size() > cart::kROMCartSize) {
            ctx.EnqueueEvent(events::gui::ShowError(fmt::format(
                "Could not load ROM cartridge image: file is too large ({} > {})", rom.size(), cart::kROMCartSize)));
            return;
        }

        // TODO: Check that the image is a proper Sega Saturn cartridge (headers)

        // Insert cartridge
        cart::ROMCartridge *cart = ctx.saturn.instance->InsertCartridge<cart::ROMCartridge>();
        if (cart != nullptr) {
            devlog::info<grp::base>("16 Mbit ROM cartridge inserted with image from {}", path);
            cart->LoadROM(rom);
        }
    });
}

EmuEvent InsertCartridgeFromSettings() {
    return RunFunction([](SharedContext &ctx) {
        std::unique_lock lock{ctx.locks.cart};

        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        auto &cartSettings = settings.cartridge;

        switch (cartSettings.type) {
        case Settings::Cartridge::Type::None:
            ctx.saturn.instance->RemoveCartridge();
            devlog::info<grp::base>("Cartridge removed");
            break;

        case Settings::Cartridge::Type::BackupRAM: {
            // Use default path for specified size
            if (cartSettings.backupRAM.imagePath.empty()) {
                cartSettings.backupRAM.imagePath =
                    ctx.profile.GetPath(ProfilePath::PersistentState) /
                    fmt::format("bup-ext-{}M.bin", CapacityToSize(cartSettings.backupRAM.capacity) * 8 / 1024 / 1024);
            }

            // Prevent loading the internal backup RAM file as backup memory cartridge
            if (std::filesystem::absolute(cartSettings.backupRAM.imagePath) ==
                std::filesystem::absolute(settings.system.internalBackupRAMImagePath)) {
                ctx.EnqueueEvent(events::gui::ShowError(fmt::format(
                    "Failed to load external backup memory: file {} is already in use as internal backup memory",
                    cartSettings.backupRAM.imagePath)));
                return;
            }

            // If a backup RAM cartridge is inserted, remove it first to unlock the file and reinsert the previous
            // cartridge in case of failure
            std::filesystem::path prevPath = "";
            util::ScopeGuard sgReinsertOnFailure{[&] {
                if (prevPath.empty()) {
                    return;
                }

                std::error_code error{};
                bup::BackupMemory bupMem{};
                auto result = bupMem.LoadFrom(prevPath, false, error);
                if (result == bup::BackupMemoryImageLoadResult::Success) {
                    ctx.saturn.instance->InsertCartridge<cart::BackupMemoryCartridge>(std::move(bupMem));
                }
            }};
            if (auto *cart = ctx.saturn.instance->GetCartridge().As<cart::CartType::BackupMemory>()) {
                prevPath = cart->GetBackupMemory().GetPath();
                if (prevPath == cartSettings.backupRAM.imagePath) {
                    ctx.saturn.instance->RemoveCartridge();
                } else {
                    sgReinsertOnFailure.Cancel();
                }
            } else {
                sgReinsertOnFailure.Cancel();
            }

            std::error_code error{};
            bup::BackupMemory bupMem{};
            bupMem.CreateFrom(cartSettings.backupRAM.imagePath, false, error,
                              CapacityToBupSize(cartSettings.backupRAM.capacity));
            if (error) {
                devlog::info<grp::base>("Failed to insert {} backup RAM cartridge from {}: {}",
                                        BupCapacityShortName(cartSettings.backupRAM.capacity),
                                        cartSettings.backupRAM.imagePath, error.message());
            } else {
                devlog::info<grp::base>("{} backup RAM cartridge inserted with image from {}",
                                        BupCapacityShortName(cartSettings.backupRAM.capacity),
                                        cartSettings.backupRAM.imagePath);
                ctx.saturn.instance->InsertCartridge<cart::BackupMemoryCartridge>(std::move(bupMem));

                // If the cartridge was successfully inserted, we don't need to reinsert the previous cartridge
                sgReinsertOnFailure.Cancel();
            }
            break;
        }
        case Settings::Cartridge::Type::DRAM:
            switch (cartSettings.dram.capacity) {
            case Settings::Cartridge::DRAM::Capacity::_48Mbit:
                ctx.saturn.instance->InsertCartridge<cart::DRAM48MbitCartridge>();
                devlog::info<grp::base>("48 Mbit DRAM dev cartridge inserted");
                break;
            case Settings::Cartridge::DRAM::Capacity::_32Mbit:
                ctx.saturn.instance->InsertCartridge<cart::DRAM32MbitCartridge>();
                devlog::info<grp::base>("32 Mbit DRAM cartridge inserted");
                break;
            case Settings::Cartridge::DRAM::Capacity::_8Mbit:
                ctx.saturn.instance->InsertCartridge<cart::DRAM8MbitCartridge>();
                devlog::info<grp::base>("8 Mbit DRAM cartridge inserted");
                break;
            }
            break;
        case Settings::Cartridge::Type::ROM: //
        {
            // TODO: deduplicate code

            // Don't even bother if no path was specified
            if (cartSettings.rom.imagePath.empty()) {
                break;
            }

            std::error_code error{};
            std::vector<uint8> rom = util::LoadFile(cartSettings.rom.imagePath, error);

            // Check for file system errors
            if (error) {
                ctx.EnqueueEvent(
                    events::gui::ShowError(fmt::format("Could not load ROM cartridge image: {}", error.message())));
                return;
            }

            // Check that the file has contents
            if (rom.empty()) {
                ctx.EnqueueEvent(
                    events::gui::ShowError("Could not load ROM cartridge image: file is empty or could not be read."));
                return;
            }

            // Check that the image is not larger than the ROM cartridge capacity
            if (rom.size() > cart::kROMCartSize) {
                ctx.EnqueueEvent(events::gui::ShowError(
                    fmt::format("Could not load ROM cartridge image: file is too large ({} > {})", rom.size(),
                                cart::kROMCartSize)));
                return;
            }

            // TODO: Check that the image is a proper Sega Saturn cartridge (headers)

            // Insert cartridge
            cart::ROMCartridge *cart = ctx.saturn.instance->InsertCartridge<cart::ROMCartridge>();
            if (cart != nullptr) {
                devlog::info<grp::base>("16 Mbit ROM cartridge inserted with image from {}",
                                        cartSettings.rom.imagePath);
                cart->LoadROM(rom);
            }
            break;
        }
        }
    });
}

EmuEvent DeleteBackupFile(std::string filename, bool external) {
    if (external) {
        return RunFunction([=](SharedContext &ctx) {
            if (auto *cart = ctx.saturn.instance->GetCartridge().As<cart::CartType::BackupMemory>()) {
                cart->GetBackupMemory().Delete(filename);
            }
        });
    } else {
        return RunFunction(
            [=](SharedContext &ctx) { ctx.saturn.instance->mem.GetInternalBackupRAM().Delete(filename); });
    }
}

EmuEvent FormatBackupMemory(bool external) {
    if (external) {
        return RunFunction([](SharedContext &ctx) {
            if (auto *cart = ctx.saturn.instance->GetCartridge().As<cart::CartType::BackupMemory>()) {
                cart->GetBackupMemory().Format();
            }
        });
    } else {
        return RunFunction([](SharedContext &ctx) { ctx.saturn.instance->mem.GetInternalBackupRAM().Format(); });
    }
}

EmuEvent LoadInternalBackupMemory() {
    return RunFunction([](SharedContext &ctx) {
        std::filesystem::path path = ctx.GetInternalBackupRAMPath();

        std::error_code error{};
        if (ctx.saturn.instance->LoadInternalBackupMemoryImage(path, false, error); error) {
            devlog::warn<grp::base>("Failed to load internal backup memory from {}: {}", path, error.message());
        } else {
            devlog::info<grp::base>("Internal backup memory image loaded from {}", path);
        }
    });
}

EmuEvent SetEmulateSH2Cache(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        const bool currEnable = ctx.saturn.instance->IsSH2CacheEmulationEnabled();
        if (currEnable != enable) {
            ctx.saturn.instance->EnableSH2CacheEmulation(enable);
            devlog::info<grp::base>("SH2 cache emulation {}", (enable ? "enabled" : "disabled"));
        }
    });
}

EmuEvent SetSH2ClockFactor(uint32 factor) {
    return RunFunction(
        [=](SharedContext &ctx) { ctx.saturn.instance->SetSH2ClockFactor(Ratio::FromPercentage(factor)); });
}

EmuEvent SetCDBlockLLE(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        ctx.saturn.instance->configuration.cdblock.useLLE = enable;
        ctx.rewindBuffer.Reset();
    });
}

EmuEvent EnableThreadedVDP1(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        settings.video.swRenderer.threadedVDP1 = enable;
    });
}

EmuEvent EnableThreadedVDP2(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        settings.video.swRenderer.threadedVDP2 = enable;
    });
}

EmuEvent EnableThreadedDeinterlacer(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        settings.video.swRenderer.threadedDeinterlacer = enable;
    });
}

EmuEvent EnableThreadedSCSP(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        auto &settings = ctx.serviceLocator.GetRequired<Settings>();
        settings.audio.threadedSCSP = enable;
    });
}

EmuEvent SetSCSPStepGranularity(uint32 granularity) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.instance->SCSP.SetStepGranularity(granularity); });
}

EmuEvent LoadState(uint32 slotIndex, bool dumpAfterLoad, uint32 framesAfterLoad, uint32 controlPadStartFrame,
                   uint32 controlPadStartHoldFrames, uint32 controlPadStartRepeatFrame,
                   uint32 controlPadStartRepeatHoldFrames, uint32 controlPadAFrame, uint32 controlPadAHoldFrames,
                   uint32 controlPadCFrame, uint32 controlPadCHoldFrames, uint32 controlPadARepeatFrame,
                   uint32 controlPadARepeatHoldFrames, uint32 controlPadDownFrame,
                   uint32 controlPadDownHoldFrames,
                   std::vector<ScheduledControlPadInput> controlPadInputs, bool captureScreenshot,
                   bool quitWhenComplete, uint32 saveStateSlot, bool captureNoRender) {
    return RunFunction([=](SharedContext &ctx) {
        // Grab the service and check for bounds
        auto &saves = ctx.serviceLocator.GetRequired<services::SaveStateService>();
        if (!saves.IsValidIndex(slotIndex)) {
            return;
        }

        // Sanity check: do entry and underlying state exist?
        auto lock = std::unique_lock{saves.SlotMutex(slotIndex)};
        const auto *slot = saves.Peek(slotIndex);
        assert(slot != nullptr); // slot index out of range
        if (!slot->IsValid()) {
            ctx.DisplayMessage(fmt::format("Save state slot {} selected", slotIndex + 1));
            return;
        }

        // Grab the savestate
        const auto &state = *slot->primary.state;

        // Sanity check: ensure that the disc hash matches
        if (!state.ValidateDiscHash(ctx.saturn.GetDiscHash())) {
            devlog::warn<grp::base>("Save state disc hash mismatch; refusing to load save state");
            return;
        }

        // Check for IPL and CD block ROM mismatches and locate and load matching ROMs if possible.
        // Refuse to load the save state otherwise.

        std::filesystem::path candidateIPLROMPath{};
        std::filesystem::path candidateCDBROMPath{};

        // Locate ROMs
        if (!state.ValidateIPLROMHash(ctx.saturn.instance->GetIPLHash())) {
            devlog::warn<grp::base>("Save state IPL ROM hash mismatch; locating IPL ROM with hash {}",
                                    ToString(state.system.iplRomHash));

            std::unique_lock lock{ctx.locks.romManager};
            for (auto &[path, info] : ctx.romManager.GetIPLROMs()) {
                if (info.hash == state.system.iplRomHash) {
                    candidateIPLROMPath = path;
                    devlog::info<grp::base>("Found matching IPL ROM at {}", path);
                    break;
                }
            }
            if (candidateIPLROMPath.empty()) {
                devlog::warn<grp::base>("Could not find matching IPL ROM. Refusing to load save state");
                return;
            }
        }

        if (!state.ValidateCDBlockROMHash(ctx.saturn.instance->SH1.GetROMHash())) {
            devlog::warn<grp::base>("Save state CD block ROM hash mismatch; locating CD block ROM with hash {}",
                                    ToString(state.sh1.romHash));

            std::unique_lock lock{ctx.locks.romManager};
            for (auto &[path, info] : ctx.romManager.GetCDBlockROMs()) {
                if (info.hash == state.sh1.romHash) {
                    candidateCDBROMPath = path;
                    devlog::info<grp::base>("Found matching CD block ROM at {}", path);
                    break;
                }
            }
            if (candidateCDBROMPath.empty()) {
                devlog::warn<grp::base>("Could not find matching CD block ROM. Refusing to load save state");
                return;
            }
        }

        // ROMs to load, if possible, into the Saturn instance.
        // Empty optional means "don't load, the Saturn instance already contains the correct ROM."
        std::optional<std::vector<uint8>> iplROMData{};
        std::optional<std::vector<uint8>> cdbROMData{};

        // Load ROMs if needed
        if (!candidateIPLROMPath.empty()) {
            std::error_code error{};
            iplROMData = util::LoadFile(candidateIPLROMPath, error);
            if (error) {
                devlog::warn<grp::base>("Could not load IPL ROM: {}. Refusing to load save state", error.message());
                return;
            }
            if (iplROMData->size() != ymir::sys::kIPLSize) {
                devlog::warn<grp::base>(
                    "Could not load IPL ROM: size mismatch - must be {} bytes. Refusing to load save state",
                    ymir::sys::kIPLSize);
                return;
            }
        }
        if (!candidateCDBROMPath.empty()) {
            std::error_code error{};
            cdbROMData = util::LoadFile(candidateCDBROMPath, error);
            if (error) {
                devlog::warn<grp::base>("Could not load CD block ROM: {}. Refusing to load save state",
                                        error.message());
                return;
            }
            if (cdbROMData->size() != ymir::sh1::kROMSize) {
                devlog::warn<grp::base>(
                    "Could not load CD block ROM: size mismatch - must be {} bytes. Refusing to load save state",
                    ymir::sh1::kROMSize);
                return;
            }
        }

        // At this point the ROMs have been loaded and validated

        // Cache current emulator state for undo before loading
        auto undoLoadState = std::make_unique<savestate::SaveState>();
        ctx.saturn.instance->SaveState(*undoLoadState);

        if (ctx.saturn.instance->LoadState(state, true)) {
            // Now that the save state has been succesfully loaded, load the ROMs
            if (iplROMData) {
                ctx.saturn.instance->LoadIPL(std::span<uint8, sys::kIPLSize>(*iplROMData));
                ctx.iplRomPath = candidateIPLROMPath;
                ctx.DisplayMessage(fmt::format("IPL ROM used by save state loaded from {}", ctx.iplRomPath));
            }
            if (cdbROMData) {
                ctx.saturn.instance->LoadCDBlockROM(std::span<uint8, sh1::kROMSize>(*cdbROMData));
                ctx.cdbRomPath = candidateCDBROMPath;
                ctx.DisplayMessage(fmt::format("CD block ROM used by save state loaded from {}", ctx.cdbRomPath));
            }
            auto &settings = ctx.serviceLocator.GetRequired<Settings>();
            if (settings.cdblock.useLLE != ctx.saturn.instance->configuration.cdblock.useLLE) {
                settings.cdblock.useLLE = ctx.saturn.instance->configuration.cdblock.useLLE;
                settings.MakeDirty();
                ctx.DisplayMessage(fmt::format("CD block emulation mode switched to {}",
                                               (settings.cdblock.useLLE ? "low-level" : "high-level")));
            }

            // Update the undo load state
            saves.PushUndoLoadState(std::move(undoLoadState));

            ctx.EnqueueEvent(events::gui::StateLoaded(slotIndex));
            if (dumpAfterLoad) {
                if (framesAfterLoad != 0) {
                    ctx.EnqueueEvent(RunFramesAndDump(framesAfterLoad, controlPadStartFrame,
                                                       controlPadStartHoldFrames, controlPadStartRepeatFrame,
                                                       controlPadStartRepeatHoldFrames, controlPadAFrame,
                                                       controlPadAHoldFrames, controlPadCFrame,
                                                       controlPadCHoldFrames, controlPadARepeatFrame,
                                                       controlPadARepeatHoldFrames, controlPadDownFrame,
                                                       controlPadDownHoldFrames, controlPadInputs, captureScreenshot, quitWhenComplete,
                                                       saveStateSlot, captureNoRender));
                } else {
                    ctx.EnqueueEvent(DumpMemory());
                }
            }
        } else {
            devlog::warn<grp::base>("Failed to load save state");
        }
    });
}

EmuEvent SaveState(uint32 slotIndex) {
    return RunFunction([=](SharedContext &ctx) {
        // Grab the service and check bounds
        auto &saves = ctx.serviceLocator.GetRequired<services::SaveStateService>();
        if (!saves.IsValidIndex(slotIndex)) {
            return;
        }

        {
            auto lock = std::unique_lock{saves.SlotMutex(slotIndex)};

            // Make room in save state slot
            auto *slot = saves.Push(slotIndex);
            assert(slot != nullptr); // slot index out of range

            // Save to selected slot and set timestamp
            ctx.saturn.instance->SaveState(*slot->state);
            slot->timestamp = std::chrono::system_clock::now();
        }

        ctx.EnqueueEvent(events::gui::StateSaved(slotIndex));
    });
}

EmuEvent UndoSaveState() {
    return RunFunction([](SharedContext &ctx) {
        auto &saves = ctx.serviceLocator.GetRequired<services::SaveStateService>();

        const auto slotIndex = saves.CurrentSlot();
        auto lock = std::unique_lock{saves.SlotMutex(slotIndex)};
        if (saves.Pop(slotIndex)) {
            ctx.DisplayMessage(fmt::format("Undid save state in slot {}", slotIndex + 1));
            ctx.EnqueueEvent(events::gui::StateSaved(static_cast<uint32>(slotIndex)));
        } else {
            ctx.DisplayMessage("No save state to undo");
        }
    });
}

EmuEvent UndoLoadState() {
    return RunFunction([](SharedContext &ctx) {
        auto &saves = ctx.serviceLocator.GetRequired<services::SaveStateService>();
        if (!saves.CanUndoLoadState()) {
            ctx.DisplayMessage("No load state to undo");
            return;
        }

        if (ctx.saturn.instance->LoadState(*saves.PopUndoLoadState(), true)) {
            ctx.DisplayMessage("Undid load state");
        } else {
            devlog::warn<grp::base>("Failed to undo load state");
        }
    });
}

} // namespace app::events::emu
