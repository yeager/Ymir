#pragma once

#include <ymir/debug/vdp2_render_state_tracer.hpp>

#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace app {

struct VDP2RenderStateTracer final : ymir::debug::IVDP2RenderStateTracer {
    static constexpr uint64 kMaxRenderedFrameBytes = 256 * 1024 * 1024;

    std::filesystem::path csvPath;
    std::filesystem::path renderedFramesPath;
    std::filesystem::path renderedFramesIndexPath;
    std::filesystem::path vdpFrameSnapshotsPath;
    std::ofstream vdpFrameSnapshotsIndex;
    unsigned vdpFrameSnapshotsBegin = 0, vdpFrameSnapshotsEnd = 0;
    uint64 vdpFrameSnapshots = 0;
    bool vdpFrameSnapshotsFailed = false;
    unsigned frameBegin = 0, frameEnd = 0, rowBegin = 0, rowEnd = 0;
    std::ofstream csv;
    std::ofstream renderedFramesIndex;
    uint64 rows = 0;
    uint64 renderedFrames = 0;
    uint64 renderedFrameBytes = 0;
    bool renderedFrameFailed = false;
    bool renderedFrameLimitExceeded = false;
    bool failed = false;
    uint64 mainSamples = 0;
    uint64 alternateSamples = 0;
    std::set<std::pair<uint64, uint32>> mainCoverage;
    std::set<std::pair<uint64, uint32>> mainVdp2FrameCoverage;
    std::set<uint64> observedMainFrames;
    std::set<uint64> observedVdp2FrameSerials;
    std::set<uint32> observedMainRows;
    std::string captureStatus = "pending";

    bool ConfigureVDPFrameSnapshots(const std::filesystem::path &path, unsigned begin, unsigned end) {
        vdpFrameSnapshotsPath = path;
        vdpFrameSnapshotsBegin = begin;
        vdpFrameSnapshotsEnd = end;
        if (path.empty()) return true;
        std::error_code error;
        if (std::filesystem::exists(path, error)) {
            if (error || !std::filesystem::is_directory(path, error) || error ||
                !std::filesystem::is_empty(path, error) || error) return false;
        } else if (error || !std::filesystem::create_directories(path, error) || error) {
            return false;
        }
        vdpFrameSnapshotsIndex.open(path / "index.csv", std::ios::trunc);
        if (!vdpFrameSnapshotsIndex) return false;
        vdpFrameSnapshotsIndex << "vdp1_frame,vdp2_frame_serial,file_prefix\n";
        vdpFrameSnapshotsIndex.flush();
        return static_cast<bool>(vdpFrameSnapshotsIndex);
    }

    template <typename VDPType> bool CaptureVDPFrameSnapshot(uint64 frame, uint64 vdp2FrameSerial,
                                                              const VDPType &vdp) {
        if (!vdpFrameSnapshotsIndex.is_open() || frame < vdpFrameSnapshotsBegin || frame >= vdpFrameSnapshotsEnd)
            return true;
        const std::string prefix = "vdp1-" + std::to_string(frame) + "-vdp2-" + std::to_string(vdp2FrameSerial);
        auto write = [&](const char *suffix, auto dump) {
            std::ofstream out(vdpFrameSnapshotsPath / (prefix + suffix), std::ios::binary | std::ios::trunc);
            if (!out) return false;
            dump(out);
            out.close();
            return static_cast<bool>(out);
        };
        const bool okay = write("-vdp1-vram.bin", [&](auto &out) { vdp.DumpVDP1VRAM(out); }) &&
                          write("-vdp1-registers.bin", [&](auto &out) { vdp.DumpVDP1Registers(out); }) &&
                          write("-vdp1-fbs.bin", [&](auto &out) { vdp.DumpVDP1Framebuffers(out); }) &&
                          write("-vdp2-vram.bin", [&](auto &out) { vdp.DumpVDP2VRAM(out); }) &&
                          write("-vdp2-cram.bin", [&](auto &out) { vdp.DumpVDP2CRAM(out); }) &&
                          write("-vdp2-registers.bin", [&](auto &out) { vdp.DumpVDP2Registers(out); });
        if (!okay) {
            vdpFrameSnapshotsFailed = true;
            return false;
        }
        vdpFrameSnapshotsIndex << frame << ',' << vdp2FrameSerial << ',' << prefix << '\n';
        vdpFrameSnapshotsIndex.flush();
        if (!vdpFrameSnapshotsIndex.good()) {
            vdpFrameSnapshotsFailed = true;
            return false;
        }
        ++vdpFrameSnapshots;
        return true;
    }

    bool Configure(const std::filesystem::path &path, unsigned fb, unsigned fe, unsigned rb, unsigned re,
                   const std::filesystem::path &framesPath = {}) {
        csvPath = path;
        renderedFramesPath = framesPath;
        frameBegin = fb; frameEnd = fe; rowBegin = rb; rowEnd = re;
        rows = 0; renderedFrames = 0; renderedFrameBytes = 0; renderedFrameFailed = false;
        renderedFrameLimitExceeded = false;
        failed = false; mainSamples = 0; alternateSamples = 0;
        captureStatus = "pending";
        mainCoverage.clear(); mainVdp2FrameCoverage.clear(); observedMainFrames.clear();
        observedVdp2FrameSerials.clear(); observedMainRows.clear();

        if (!renderedFramesPath.empty()) {
            std::error_code error;
            if (std::filesystem::exists(renderedFramesPath, error)) {
                if (error || !std::filesystem::is_directory(renderedFramesPath, error) || error ||
                    !std::filesystem::is_empty(renderedFramesPath, error) || error) {
                    return false;
                }
            } else if (error || !std::filesystem::create_directories(renderedFramesPath, error) || error) {
                return false;
            }
            renderedFramesIndexPath = renderedFramesPath / "index.csv";
            renderedFramesIndex.open(renderedFramesIndexPath, std::ios::trunc);
            if (!renderedFramesIndex) return false;
            renderedFramesIndex << "frame_index,vdp2_frame_serial,width,height,pixel_format,file\n";
            renderedFramesIndex.flush();
            if (!renderedFramesIndex.good()) {
                renderedFramesIndex.close();
                return false;
            }
        }

        csv.open(csvPath, std::ios::trunc);
        if (!csv) return false;
        csv << "frame_index,vdp2_frame_serial,line_call_ordinal,row_vcnt,alternate_field,scroll_h_11_8,scroll_v_11_8,scroll_inc_h_11_8,scroll_inc_v_11_8,"
               "fractional_scroll_x_11_8,fractional_scroll_y_11_8,priority,color_calculation,color_calculation_ratio,"
               "bitmap,color_format,nbg0_enabled,layer_enabled,rbg1_enabled,mosaic_reuse\n";
        csv.flush();
        if (!csv.good()) {
            failed = true;
            csv.close();
            return false;
        }
        return true;
    }

    bool ShouldTrace(uint64 frame, uint32 y) const final {
        return csv.is_open() && frame >= frameBegin && frame < frameEnd && y >= rowBegin && y < rowEnd;
    }

    bool ShouldCaptureFrame(uint64 frame) const final {
        return renderedFramesIndex.is_open() && frame >= frameBegin && frame < frameEnd && !renderedFrameFailed &&
               renderedFrameBytes < kMaxRenderedFrameBytes;
    }

    void CompletedFrame(uint64 frame, uint64 vdp2FrameSerial, uint32 width, uint32 height,
                        const uint32 *pixels) final {
        if (!ShouldCaptureFrame(frame)) return;

        std::ostringstream filename;
        filename << "vdp1-" << frame << "-vdp2-" << vdp2FrameSerial << "-" << width << "x" << height
                 << ".rgbx8888le";
        const auto framePath = renderedFramesPath / filename.str();
        if (pixels == nullptr || width == 0 || height == 0 ||
            static_cast<uint64>(width) > std::numeric_limits<uint64>::max() / height / sizeof(uint32)) {
            failed = true;
            renderedFrameFailed = true;
            return;
        }
        const uint64 byteCount = static_cast<uint64>(width) * height * sizeof(uint32);
        if (byteCount > kMaxRenderedFrameBytes - renderedFrameBytes) {
            failed = true;
            renderedFrameFailed = true;
            renderedFrameLimitExceeded = true;
            return;
        }
        std::ofstream output(framePath, std::ios::binary | std::ios::trunc);
        bool fileOk = static_cast<bool>(output);
        if (fileOk) {
            for (uint64 i = 0; i < byteCount / sizeof(uint32); ++i) {
                const uint32 pixel = pixels[i];
                const char bytes[4] = {static_cast<char>(pixel & 0xFF), static_cast<char>((pixel >> 8) & 0xFF),
                                       static_cast<char>((pixel >> 16) & 0xFF), static_cast<char>((pixel >> 24) & 0xFF)};
                output.write(bytes, sizeof(bytes));
                if (!output) { fileOk = false; break; }
            }
            output.flush();
            fileOk = fileOk && static_cast<bool>(output);
        }
        output.close();
        fileOk = fileOk && !output.fail();
        if (!fileOk) {
            failed = true;
            renderedFrameFailed = true;
            std::error_code error;
            std::filesystem::remove(framePath, error);
            return;
        }

        renderedFramesIndex << frame << ',' << vdp2FrameSerial << ',' << width << ',' << height
                            << ",rgbx8888le," << filename.str() << '\n';
        renderedFramesIndex.flush();
        if (!renderedFramesIndex.good()) {
            failed = true;
            renderedFrameFailed = true;
            std::error_code error;
            std::filesystem::remove(framePath, error);
        } else {
            ++renderedFrames;
            renderedFrameBytes += byteCount;
        }
    }

    void Sample(const ymir::debug::VDP2RenderStateSample &s) final {
        if (!ShouldTrace(s.frame, s.y)) return;
        csv << s.frame << ',' << s.vdp2FrameSerial << ',' << s.lineCallOrdinal << ','
            << s.y << ',' << (s.alternateField ? 1 : 0) << ','
            << s.scrollH << ',' << s.scrollV << ',' << s.scrollIncH << ',' << s.scrollIncV << ','
            << s.fractionalScrollX << ',' << s.fractionalScrollY << ',' << unsigned(s.priority) << ','
            << (s.colorCalculation ? 1 : 0) << ',' << unsigned(s.colorCalculationRatio) << ','
            << (s.bitmap ? 1 : 0) << ',' << unsigned(s.colorFormat) << ','
            << (s.nbg0Enabled ? 1 : 0) << ',' << (s.layerEnabled ? 1 : 0) << ','
            << (s.rbg1Enabled ? 1 : 0) << ',' << (s.mosaicReuse ? 1 : 0) << '\n';
        if (!csv) failed = true;
        else {
            ++rows;
            if (s.alternateField) {
                ++alternateSamples;
            } else {
                ++mainSamples;
                mainCoverage.emplace(s.frame, s.y);
                mainVdp2FrameCoverage.emplace(s.vdp2FrameSerial, s.y);
                observedMainFrames.insert(s.frame);
                observedVdp2FrameSerials.insert(s.vdp2FrameSerial);
                observedMainRows.insert(s.y);
            }
        }
    }

    void Abort(std::string status) { captureStatus = std::move(status); }

    bool Finalize() {
        if (csv.is_open()) {
            csv.flush();
            failed = failed || !csv.good();
            csv.close();
            failed = failed || csv.fail();
        }
        if (renderedFramesIndex.is_open()) {
            renderedFramesIndex.flush();
            if (!renderedFramesIndex.good()) {
                failed = true;
                renderedFrameFailed = true;
            }
            renderedFramesIndex.close();
            if (renderedFramesIndex.fail()) {
                failed = true;
                renderedFrameFailed = true;
            }
        }
        const uint64 expectedMainSamples =
            static_cast<uint64>(frameEnd - frameBegin) * (rowEnd - rowBegin);
        if (captureStatus == "pending") {
            captureStatus = mainSamples == expectedMainSamples &&
                                    mainCoverage.size() == expectedMainSamples ? "complete" : "incomplete";
        }
        auto receiptPath = csvPath;
        receiptPath += ".receipt.txt";
        std::ofstream receipt(receiptPath, std::ios::trunc);
        if (!receipt) return false;
        receipt << "format=" << (renderedFramesPath.empty() ? "ymir-vdp2-render-state-trace-v2"
                                                              : "ymir-vdp2-render-state-trace-v3") << "\n"
                << "layer=NBG0\n"
                << "frame_index=VDP1 frame counter, zero-based (same convention as VDP2 memory trace)\n"
                << "frame_range_half_open=" << frameBegin << ':' << frameEnd << "\n"
                << "row_vcnt_range_half_open=" << rowBegin << ':' << rowEnd << "\n"
                << "expected_main_samples=" << expectedMainSamples << "\n"
                << "observed_main_samples=" << mainSamples << "\n"
                << "unique_main_coverage_pairs=" << mainCoverage.size() << "\n"
                << "duplicate_main_samples=" << (mainSamples > mainCoverage.size()
                                                      ? mainSamples - mainCoverage.size() : 0) << "\n"
                << "unique_vdp2_frame_row_pairs=" << mainVdp2FrameCoverage.size() << "\n"
                << "observed_vdp2_frame_serials=" << observedVdp2FrameSerials.size() << "\n"
                << "missing_main_samples=" << (expectedMainSamples > mainCoverage.size()
                                                   ? expectedMainSamples - mainCoverage.size() : 0) << "\n"
                << "observed_main_frames=" << observedMainFrames.size() << "\n"
                << "observed_main_rows=" << observedMainRows.size() << "\n"
                << "observed_alternate_field_samples=" << alternateSamples << "\n";
        if (!renderedFramesPath.empty()) {
            receipt << "rendered_frame_output_count=" << renderedFrames << "\n"
                    << "rendered_frame_output_bytes=" << renderedFrameBytes << "\n"
                    << "rendered_frame_output_byte_limit=" << kMaxRenderedFrameBytes << "\n"
                    << "rendered_frame_output_limit_exceeded=" << (renderedFrameLimitExceeded ? 1 : 0) << "\n"
                    << "rendered_frame_output_status="
                    << (!renderedFrameFailed && renderedFrames > 0 ? "complete" : "incomplete") << "\n"
                    << "rendered_frame_output_directory=" << renderedFramesPath.string() << "\n"
                    << "rendered_frame_capture_stage=composed software-renderer framebuffer after VDP1 sprite and VDP2 layer rendering\n"
                    << "rendered_frame_pixel_format=rgbx8888le (RGBX byte order from little-endian uint32 words)\n";
        }
        receipt << "observed_main_frame_ids=";
        bool first = true;
        for (uint64 frame : observedMainFrames) {
            receipt << (first ? "" : ",") << frame;
            first = false;
        }
        receipt << "\nobserved_main_row_vcnts=";
        first = true;
        for (uint32 row : observedMainRows) {
            receipt << (first ? "" : ",") << row;
            first = false;
        }
        receipt << "\nobserved_vdp2_frame_serial_ids=";
        first = true;
        for (uint64 serial : observedVdp2FrameSerials) {
            receipt << (first ? "" : ",") << serial;
            first = false;
        }
        receipt << "\ncoordinate_units=11.8 fixed point for scroll and increments\n"
                << "parameter_source=VDP2Regs BGParams[1] and current NBG0 layer state\n"
                << "sample_point=software renderer, after line preparation and before NBG0 drawing\n"
                << "interlace=alternate_field distinguishes second field sample when present\n"
                << "vdp2_frame_serial=monotonic software-renderer VDP2BeginFrame count\n"
                << "line_call_ordinal=per-VDP2-frame sequence of VDP2RenderLine calls; alternate-field samples share ordinal\n"
                << "fields=frame_index,vdp2_frame_serial,line_call_ordinal,row_vcnt,alternate_field,scroll_h_11_8,scroll_v_11_8,scroll_inc_h_11_8,scroll_inc_v_11_8,"
                   "fractional_scroll_x_11_8,fractional_scroll_y_11_8,priority,color_calculation,"
                   "color_calculation_ratio,bitmap,color_format,nbg0_enabled,layer_enabled,rbg1_enabled,mosaic_reuse\n"
                << "capture_status=" << captureStatus << "\n"
                << "csv=" << csvPath.string() << "\n"
                << "rows_written=" << rows << "\n"
                << "write_error=" << (failed ? 1 : 0) << "\n";
        receipt.flush();
        const bool receiptOk = static_cast<bool>(receipt);
        receipt.close();
        return receiptOk && !receipt.fail() && !failed;
    }
};

} // namespace app
