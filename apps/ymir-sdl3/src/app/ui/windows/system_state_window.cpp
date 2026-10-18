#include "system_state_window.hpp"

#include <ymir/sys/saturn.hpp>

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <app/ui/widgets/cartridge_widgets.hpp>
#include <app/ui/widgets/common_widgets.hpp>
#include <app/ui/widgets/system_widgets.hpp>

#include <app/imgui_data.hpp>
#include <app/ui/i18n.hpp>

#include <SDL3/SDL_clipboard.h>

#include <fmt/std.h>

#include <cinttypes>

using namespace ymir;

namespace app::ui {

#define TR(text) i18n::tr(text)

inline constexpr float kWindowWidth = 350.0f;

SystemStateWindow::SystemStateWindow(SharedContext &context)
    : WindowBase(context) {

    m_windowConfig.name = TR("System state");
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void SystemStateWindow::PrepareWindow() {
    /*ImGui::SetNextWindowSizeConstraints(ImVec2(kWindowWidth * m_context.displayScale, 0),
                                        ImVec2(kWindowWidth * m_context.displayScale, FLT_MAX));*/
}

void SystemStateWindow::DrawContents() {
    const auto &settings = m_context.serviceLocator.GetRequired<Settings>();

    ImGui::BeginGroup();

    ImGui::SeparatorText(TR("SMPC Parameters"));
    DrawSMPCParameters();

    ImGui::SeparatorText(TR("State"));
    DrawScreen();
    DrawRealTimeClock();
    DrawClocks();

    ImGui::SeparatorText(TR("CD drive"));
    if (settings.cdblock.useLLE) {
        DrawCDDrive();
    } else {
        DrawCDBlock();
    }

    ImGui::SeparatorText(TR("Backup memory"));
    DrawBackupMemory();

    ImGui::SeparatorText(TR("Cartridge"));
    DrawCartridge();

    ImGui::SeparatorText(TR("Peripherals"));
    DrawPeripherals();

    ImGui::SeparatorText(TR("Actions"));
    DrawActions();

    ImGui::EndGroup();
}

void SystemStateWindow::DrawSMPCParameters() {
    sys::ClockSpeed clockSpeed = m_context.saturn.instance->GetClockSpeed();

    if (ImGui::BeginTable("sys_params", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("Clock speed"));
            widgets::ExplanationTooltip(TR("Select the divider for system clock rates.\nAutomatically adjusted by games.\nOn a real Saturn, games must use the faster clock setting to use 352 pixels-wide resolution modes."));
        }
        if (ImGui::TableNextColumn()) {
            if (ImGui::RadioButton(TR("Slow"), clockSpeed == sys::ClockSpeed::_320)) {
                m_context.EnqueueEvent(events::emu::SetClockSpeed(sys::ClockSpeed::_320));
            }
            widgets::ExplanationTooltip(TR("320 pixels"));
            ImGui::SameLine();
            if (ImGui::RadioButton(TR("Fast"), clockSpeed == sys::ClockSpeed::_352)) {
                m_context.EnqueueEvent(events::emu::SetClockSpeed(sys::ClockSpeed::_352));
            }
            widgets::ExplanationTooltip(TR("352 pixels"));
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("Video standard"));
        }
        if (ImGui::TableNextColumn()) {
            ui::widgets::VideoStandardSelector(m_context);
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("Region"));
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted(TR("Changing this option will cause a hard reset"));
                ImGui::EndTooltip();
            }
        }
        if (ImGui::TableNextColumn()) {
            ui::widgets::RegionSelector(m_context);
        }

        ImGui::EndTable();
    }
}

void SystemStateWindow::DrawScreen() {
    auto &probe = m_context.saturn.instance->VDP.GetProbe();
    auto [width, height] = probe.GetResolution();
    auto interlaceMode = probe.GetInterlaceMode();

    const char *const kInterlaceNames[]{TR("progressive"), TR("(invalid)"), TR("single-density interlace"),
                                         TR("double-density interlace")};

    ImGui::TextUnformatted(TR("Resolution:"));
    ImGui::SameLine();
    ImGui::Text("%ux%u %s", width, height, kInterlaceNames[static_cast<uint8>(interlaceMode)]);
}

void SystemStateWindow::DrawRealTimeClock() {
    const auto dt = m_context.saturn.instance->SMPC.GetProbe().GetRTCDateTime();

    const char *const kWeekdays[] = {TR("Sun"), TR("Mon"), TR("Tue"), TR("Wed"), TR("Thu"), TR("Fri"), TR("Sat")};

    ImGui::Text(TR("Current date/time: %s %04u/%02u/%02u %02u:%02u:%02u"), kWeekdays[dt.weekday], dt.year, dt.month, dt.day,
                dt.hour, dt.minute, dt.second);
    // TODO: make it adjustable, sync to host
}

void SystemStateWindow::DrawClocks() {
    if (ImGui::BeginTable("sys_clocks", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn(TR("Components"));
        ImGui::TableSetupColumn(TR("Clock"));
        ImGui::TableSetupColumn(TR("Ratio"));
        ImGui::TableHeadersRow();

        const Saturn &saturn = *m_context.saturn.instance;

        const sys::ClockRatios clockRatios = saturn.GetClockRatios();

        const double clockScale = (double)saturn.configuration.system.sh2ClockFactor.Get().AsDouble();
        const double baseMasterClock =
            ((double)clockRatios.masterClock * clockRatios.masterClockNum / clockRatios.masterClockDen / 1000000.0);
        const double masterClock = baseMasterClock * clockScale;

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted(TR("SH-2, SCU and VDPs"));
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%.5lf MHz", masterClock);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("1:1");
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("SCU DSP");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%.5lf MHz", masterClock * 0.5);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("1:2");
        }

        // Account for double-resolution
        const bool doubleWidth = saturn.VDP.GetProbe().GetResolution().width >= 640;
        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted(TR("Pixel clock"));
        }
        if (ImGui::TableNextColumn()) {
            const double factor = doubleWidth ? 0.5 : 0.25;
            ImGui::Text("%.5lf MHz", baseMasterClock * factor);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("1:%u", doubleWidth ? 2u : 4u);
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("SCSP");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%.5lf MHz", masterClock * clockRatios.SCSPNum / clockRatios.SCSPDen);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%" PRIu64 ":%" PRIu64, clockRatios.SCSPNum, clockRatios.SCSPDen);
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("MC68EC000");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%.5lf MHz", masterClock * clockRatios.SCSPNum / clockRatios.SCSPDen * 0.5);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%" PRIu64 ":%" PRIu64, clockRatios.SCSPNum, clockRatios.SCSPDen * 2u);
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("CD Block SH-1");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%.5lf MHz", masterClock * clockRatios.CDBlockNum / clockRatios.CDBlockDen);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%" PRIu64 ":%" PRIu64, clockRatios.CDBlockNum, clockRatios.CDBlockDen);
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("SMPC MCU");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%.5lf MHz", masterClock * clockRatios.SMPCNum / clockRatios.SMPCDen);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%" PRIu64 ":%" PRIu64, clockRatios.SMPCNum, clockRatios.SMPCDen);
        }

        ImGui::EndTable();
    }
}

void SystemStateWindow::DrawCDBlock() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &probe = m_context.saturn.instance->CDBlock.GetProbe();

    const uint8 status = probe.GetCurrentStatusCode();

    if (ImGui::Button(m_context.saturn.instance->CDBlock.IsTrayOpen() ? TR("Close tray") : TR("Open tray"))) {
        m_context.EnqueueEvent(events::emu::OpenCloseTray());
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Load disc..."))) {
        m_context.EnqueueEvent(events::gui::LoadDisc());
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Eject disc"))) {
        m_context.EnqueueEvent(events::emu::EjectDisc());
    }

    DrawDiscImage();

    switch (status) {
    case cdblock::kStatusCodeBusy: ImGui::TextUnformatted(TR("Busy")); break;
    case cdblock::kStatusCodePause: ImGui::TextUnformatted(TR("Paused")); break;
    case cdblock::kStatusCodeStandby: ImGui::TextUnformatted(TR("Standby")); break;
    case cdblock::kStatusCodePlay:
        ImGui::Text(TR("Playing track %u, index %u (%s)"), probe.GetCurrentTrack(), probe.GetCurrentIndex(),
                    (probe.GetCurrentControlADRBits() == 0x01 ? "CDDA" : "Data"));
        break;
    case cdblock::kStatusCodeSeek: ImGui::TextUnformatted(TR("Seeking")); break;
    case cdblock::kStatusCodeScan:
        ImGui::Text(TR("Scanning track %u, index %u (%s)"), probe.GetCurrentTrack(), probe.GetCurrentIndex(),
                    (probe.GetCurrentControlADRBits() == 0x01 ? "CDDA" : "Data"));
        break;
    case cdblock::kStatusCodeOpen: ImGui::TextUnformatted(TR("Tray open")); break;
    case cdblock::kStatusCodeNoDisc: ImGui::TextUnformatted(TR("No disc")); break;
    case cdblock::kStatusCodeRetry: ImGui::TextUnformatted(TR("Retrying")); break;
    case cdblock::kStatusCodeError: ImGui::TextUnformatted(TR("Error")); break;
    case cdblock::kStatusCodeFatal: ImGui::TextUnformatted(TR("Fatal error")); break;
    }

    ImGui::Text(TR("Read speed: %ux"), probe.GetReadSpeed());

    const uint32 fad = probe.GetCurrentFrameAddress();
    const uint8 repeat = probe.GetCurrentRepeatCount();
    const uint8 maxRepeat = probe.GetMaxRepeatCount();
    const media::MSF msf = media::FADToMSF(fad);

    if (status == cdblock::kStatusCodePlay || status == cdblock::kStatusCodeScan) {
        ImGui::BeginGroup();
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        if (msf.m == 0) {
            ImGui::TextDisabled("00");
        } else {
            if (msf.m < 10) {
                ImGui::TextDisabled("0");
                ImGui::SameLine(0, 0);
            }
            ImGui::Text("%u", msf.m);
        }
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(":");
        ImGui::SameLine(0, 0);
        if (msf.m == 0 && msf.s == 0) {
            ImGui::TextDisabled("00");
        } else {
            if (msf.m == 0 && msf.s < 10) {
                ImGui::TextDisabled("0");
                ImGui::SameLine(0, 0);
                ImGui::Text("%u", msf.s);
            } else {
                ImGui::Text("%02u", msf.s);
            }
        }
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(".");
        ImGui::SameLine(0, 0);
        if (msf.m == 0 && msf.s == 0 && msf.f == 0) {
            ImGui::TextDisabled("00");
        } else {
            if (msf.m == 0 && msf.s == 0 && msf.f < 10) {
                ImGui::TextDisabled("0");
                ImGui::SameLine(0, 0);
                ImGui::Text("%u", msf.f);
            } else {
                ImGui::Text("%02u", msf.f);
            }
        }
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetItemTooltip("%s", TR("MM:SS.FF\nMinutes, seconds and frames\n(75 frames per second)"));

        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(" :: ");
        ImGui::SameLine(0, 0);

        ImGui::BeginGroup();
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        const uint32 numZeros = std::countl_zero(fad) / 4 - 2; // FAD is 24 bits
        ImGui::TextDisabled("%0*u", numZeros, 0);
        ImGui::SameLine(0, 0);
        ImGui::Text("%X", fad);
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetItemTooltip("%s", TR("Frame address (FAD)"));
    } else {
        ImGui::BeginGroup();
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        ImGui::TextDisabled("--");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(":");
        ImGui::SameLine(0, 0);
        ImGui::TextDisabled("--");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(".");
        ImGui::SameLine(0, 0);
        ImGui::TextDisabled("--");
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetItemTooltip("%s", TR("MM:SS.FF\nMinutes, seconds and frames\n(75 frames per second)"));

        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(" :: ");
        ImGui::SameLine(0, 0);

        ImGui::BeginGroup();
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        ImGui::TextDisabled("------");
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetItemTooltip("%s", TR("Frame address (FAD)"));
    }

    ImGui::SameLine(0, 0);
    ImGui::TextUnformatted(" :: ");
    ImGui::SameLine(0, 0);

    if (maxRepeat == 0xF) {
        ImGui::TextUnformatted(TR("Repeat forever"));
    } else if (maxRepeat > 0) {
        ImGui::Text(TR("Repeat %u of %u"), repeat, maxRepeat);
    } else {
        ImGui::TextUnformatted(TR("No repeat"));
    }

    if (status == cdblock::kStatusCodePlay) {
        std::string file = probe.GetPathAtFrameAddress(fad);
        if (!file.empty()) {
            ImGui::Text(TR("Reading file %s"), file.c_str());
        } else {
            ImGui::TextUnformatted(TR("Not a file"));
        }
    } else {
        ImGui::TextUnformatted("");
    }
}

void SystemStateWindow::DrawCDDrive() {
    const YmirImGuiData *imguiData = GetYmirImGuiData();
    auto &probe = m_context.saturn.instance->CDDrive.GetProbe();

    using CDOp = cdblock::CDDrive::Operation;

    const auto &status = probe.GetStatus();

    if (ImGui::Button(status.operation == CDOp::TrayOpen ? TR("Close tray") : TR("Open tray"))) {
        m_context.EnqueueEvent(events::emu::OpenCloseTray());
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Load disc..."))) {
        m_context.EnqueueEvent(events::gui::LoadDisc());
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Eject disc"))) {
        m_context.EnqueueEvent(events::emu::EjectDisc());
    }

    DrawDiscImage();

    switch (status.operation) {
    case CDOp::Reset: ImGui::TextUnformatted(TR("Reset")); break;
    case CDOp::Idle: ImGui::TextUnformatted(TR("Idle")); break;
    case CDOp::Stopped: ImGui::TextUnformatted(TR("Stopped")); break;
    case CDOp::TrayOpen: ImGui::TextUnformatted(TR("Tray open")); break;
    case CDOp::NoDisc: ImGui::TextUnformatted(TR("No disc")); break;
    case CDOp::ReadTOC: ImGui::TextUnformatted(TR("Reading TOC")); break;
    case CDOp::DiscChanged: ImGui::TextUnformatted(TR("Disc changed")); break;
    case CDOp::ReadDataSector:
        ImGui::Text(TR("Reading track %u, index %u (Data)"), util::from_bcd(status.trackNum),
                    util::from_bcd(status.indexNum));
        break;
    case CDOp::ReadAudioSector:
        ImGui::Text(TR("Playing track %u, index %u (CDDA)"), util::from_bcd(status.trackNum),
                    util::from_bcd(status.indexNum));
        break;
    case CDOp::ScanAudioSector:
        ImGui::Text(TR("Scanning track %u, index %u (CDDA)"), util::from_bcd(status.trackNum),
                    util::from_bcd(status.indexNum));
        break;
    case CDOp::Seek: ImGui::TextUnformatted("Seeking"); break;
    case CDOp::SeekSecurityRingB2: [[fallthrough]];
    case CDOp::SeekSecurityRingB6: ImGui::TextUnformatted(TR("Seeking security ring")); break;
    default: ImGui::Text(TR("Unknown (%02X)"), static_cast<uint8>(status.operation)); break;
    }

    ImGui::Text(TR("Read speed: %ux"), probe.GetReadSpeed());

    const bool isReading = status.operation == CDOp::ReadDataSector || status.operation == CDOp::ReadAudioSector ||
                           status.operation == CDOp::ScanAudioSector;
    const bool isSeeking = status.operation == CDOp::Seek || status.operation == CDOp::SeekSecurityRingB2 ||
                           status.operation == CDOp::SeekSecurityRingB6;

    const uint32 fad = isSeeking ? probe.GetTargetFrameAddress() : probe.GetCurrentFrameAddress();
    const media::MSF msf = media::FADToMSF(fad);

    if (isReading || isSeeking) {
        ImGui::BeginGroup();
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        if (msf.m == 0) {
            ImGui::TextDisabled("00");
        } else {
            if (msf.m < 10) {
                ImGui::TextDisabled("0");
                ImGui::SameLine(0, 0);
            }
            ImGui::Text("%u", msf.m);
        }
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(":");
        ImGui::SameLine(0, 0);
        if (msf.m == 0 && msf.s == 0) {
            ImGui::TextDisabled("00");
        } else {
            if (msf.m == 0 && msf.s < 10) {
                ImGui::TextDisabled("0");
                ImGui::SameLine(0, 0);
                ImGui::Text("%u", msf.s);
            } else {
                ImGui::Text("%02u", msf.s);
            }
        }
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(".");
        ImGui::SameLine(0, 0);
        if (msf.m == 0 && msf.s == 0 && msf.f == 0) {
            ImGui::TextDisabled("00");
        } else {
            if (msf.m == 0 && msf.s == 0 && msf.f < 10) {
                ImGui::TextDisabled("0");
                ImGui::SameLine(0, 0);
                ImGui::Text("%u", msf.f);
            } else {
                ImGui::Text("%02u", msf.f);
            }
        }
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetItemTooltip("%s", TR("MM:SS.FF\nMinutes, seconds and frames\n(75 frames per second)"));

        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(" :: ");
        ImGui::SameLine(0, 0);

        ImGui::BeginGroup();
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        const uint32 numZeros = std::countl_zero(fad) / 4 - 2; // FAD is 24 bits
        ImGui::TextDisabled("%0*u", numZeros, 0);
        ImGui::SameLine(0, 0);
        ImGui::Text("%X", fad);
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetItemTooltip("%s", TR("Frame address (FAD)"));
    } else {
        ImGui::BeginGroup();
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        ImGui::TextDisabled("--");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(":");
        ImGui::SameLine(0, 0);
        ImGui::TextDisabled("--");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(".");
        ImGui::SameLine(0, 0);
        ImGui::TextDisabled("--");
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetItemTooltip("%s", TR("MM:SS.FF\nMinutes, seconds and frames\n(75 frames per second)"));

        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(" :: ");
        ImGui::SameLine(0, 0);

        ImGui::BeginGroup();
        ImGui::PushFont(imguiData->fonts.monospace.regular, imguiData->fontSizes.medium);
        ImGui::TextDisabled("------");
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetItemTooltip("%s", TR("Frame address (FAD)"));
    }

    if (status.operation == CDOp::ReadDataSector) {
        std::string file = probe.GetPathAtFrameAddress(fad);
        if (!file.empty()) {
            ImGui::Text(TR("Reading file %s"), file.c_str());
        } else {
            ImGui::TextUnformatted(TR("Not a file"));
        }
    } else {
        ImGui::TextUnformatted("");
    }
}

void SystemStateWindow::DrawDiscImage() {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    if (m_context.state.loadedDiscImagePath.empty() && m_context.state.loadedDiscDrivePath.empty()) {
        ImGui::TextUnformatted(TR("No disc loaded"));
    } else {
        if (!m_context.state.loadedDiscImagePath.empty()) {
            ImGui::Text(TR("Disc image from %s"), fmt::format("{}", m_context.state.loadedDiscImagePath).c_str());
        } else {
            ImGui::Text(TR("Disc from drive %s"), fmt::format("{}", m_context.state.loadedDiscDrivePath).c_str());
        }
        std::string hash{};
        std::string serial{};
        {
            std::unique_lock lock{m_context.locks.disc};
            hash = ToString(m_context.saturn.GetDiscHash());
            serial = m_context.saturn.GetDiscHeader().productNumber;
        }

        auto draw = [&](const char *name, std::string_view value) {
            if (value.empty()) {
                ImGui::Text(TR("%s: <blank>"), name);
            } else {
                ImGui::Text("%s: %s", name, value.data());
                ImGui::PushID(name);
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Copy"))) {
                    SDL_SetClipboardText(value.data());
                }
                ImGui::PopID();
            }
        };

        draw("Serial", serial.c_str());
        draw("Hash", hash.c_str());
    }
    ImGui::PopTextWrapPos();
}

void SystemStateWindow::DrawBackupMemory() {
    if (ImGui::Button(TR("Open backup memory manager"))) {
        m_context.EnqueueEvent(events::gui::OpenBackupMemoryManager());
    }

    if (ImGui::BeginTable("bup_info", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn(TR("Device"));
        ImGui::TableSetupColumn(TR("Capacity"));
        ImGui::TableSetupColumn(TR("Blocks used"));
        ImGui::TableHeadersRow();

        auto drawBup = [&](const char *name, bup::IBackupMemory *bup) {
            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::TextUnformatted(name);
            }
            if (bup) {
                if (ImGui::TableNextColumn()) {
                    ImGui::Text("%u KiB", bup->Size() / 1024u);
                }
                if (ImGui::TableNextColumn()) {
                    if (bup->IsHeaderValid()) {
                        ImGui::Text(TR("%u of %u"), bup->GetUsedBlocks(), bup->GetTotalBlocks());
                    } else {
                        ImGui::TextUnformatted(TR("Invalid"));
                    }
                }
            } else {
                if (ImGui::TableNextColumn()) {
                    ImGui::TextUnformatted("-");
                }
                if (ImGui::TableNextColumn()) {
                    ImGui::TextUnformatted("-");
                }
            }
        };

        drawBup(TR("Internal"), &m_context.saturn.instance->mem.GetInternalBackupRAM());
        {
            std::unique_lock lock{m_context.locks.cart};
            if (auto *bupCart = m_context.saturn.instance->GetCartridge().As<cart::CartType::BackupMemory>()) {
                drawBup(TR("External"), &bupCart->GetBackupMemory());
            } else {
                drawBup(TR("External"), nullptr);
            }
        }

        ImGui::EndTable();
    }
}

void SystemStateWindow::DrawCartridge() {
    ImGui::Button(TR("Insert..."));
    if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
        if (ImGui::MenuItem(TR("Backup RAM"))) {
            m_context.EnqueueEvent(events::gui::OpenBackupMemoryCartFileDialog());
        }
        if (ImGui::MenuItem(TR("8 Mbit DRAM"))) {
            m_context.EnqueueEvent(events::emu::Insert8MbitDRAMCartridge());
        }
        if (ImGui::MenuItem(TR("32 Mbit DRAM"))) {
            m_context.EnqueueEvent(events::emu::Insert32MbitDRAMCartridge());
        }
        if (ImGui::MenuItem(TR("48 Mbit DRAM"))) {
            m_context.EnqueueEvent(events::emu::Insert48MbitDRAMCartridge());
        }
        if (ImGui::MenuItem(TR("16 Mbit ROM"))) {
            m_context.EnqueueEvent(events::gui::OpenROMCartFileDialog());
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Remove"))) {
        m_context.EnqueueEvent(events::emu::RemoveCartridge());
    }
    ImGui::SameLine();

    uint8 cartID;
    {
        std::unique_lock lock{m_context.locks.cart};
        auto &cart = m_context.saturn.instance->GetCartridge();
        cartID = cart.GetID();
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("[ID %02X] ", cartID);
    ImGui::SameLine(0, 0);
    widgets::CartridgeInfo(m_context);
}

void SystemStateWindow::DrawPeripherals() {
    if (ImGui::Button(TR("Configure##peripherals"))) {
        m_context.EnqueueEvent(events::gui::OpenSettings(SettingsTab::Input));
    }

    if (ImGui::BeginTable("sys_peripherals", 2, ImGuiTableFlags_SizingFixedFit)) {
        auto &port1 = m_context.saturn.instance->SMPC.GetPeripheralPort1();
        auto type1 = port1.GetPeripheral().GetType();

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            // ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("Port 1:"));
        }
        if (ImGui::TableNextColumn()) {
            // ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", peripheral::GetPeripheralName(type1).data());
        }
        /*if (ImGui::TableNextColumn()) {
            if (ImGui::Button("Configure...##port_1")) {
                // TODO: send GUI event to invoke peripheral configuration
            }
        }*/

        auto &port2 = m_context.saturn.instance->SMPC.GetPeripheralPort2();
        auto type2 = port2.GetPeripheral().GetType();

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            // ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("Port 2:"));
        }
        if (ImGui::TableNextColumn()) {
            // ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", peripheral::GetPeripheralName(type2).data());
        }
        /*if (ImGui::TableNextColumn()) {
            if (ImGui::Button("Configure...##port_2")) {
                // TODO: send GUI event to invoke peripheral configuration
            }
        }*/

        ImGui::EndTable();
    }
}

void SystemStateWindow::DrawActions() {
    if (ImGui::Button(TR("Hard reset"))) {
        m_context.EnqueueEvent(events::emu::HardReset());
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Soft reset"))) {
        m_context.EnqueueEvent(events::emu::SoftReset());
    }
    // TODO: Let's not make it that easy to accidentally wipe system settings
    /*ImGui::SameLine();
    if (ImGui::Button("Factory reset")) {
        m_context.EnqueueEvent(events::emu::FactoryReset());
    }*/
}

} // namespace app::ui
