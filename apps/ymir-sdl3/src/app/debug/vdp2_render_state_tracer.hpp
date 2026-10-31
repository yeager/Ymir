#pragma once

#include <ymir/debug/vdp2_render_state_tracer.hpp>

#include <array>
#include <cstring>
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
    static constexpr uint64 kMaxLayerOutputBytes = 64 * 1024 * 1024;

    std::filesystem::path csvPath;
    std::filesystem::path renderedFramesPath;
    std::filesystem::path renderedFramesIndexPath;
    std::filesystem::path layerOutputsPath;
    unsigned frameBegin = 0, frameEnd = 0, rowBegin = 0, rowEnd = 0;
    std::ofstream csv;
    std::ofstream renderedFramesIndex;
    std::ofstream layerOutputs;
    uint64 rows = 0;
    uint64 renderedFrames = 0;
    uint64 renderedFrameBytes = 0;
    uint64 layerOutputRecords = 0;
    uint64 layerOutputBytes = 0;
    bool renderedFrameFailed = false;
    bool renderedFrameLimitExceeded = false;
    bool layerOutputFailed = false;
    bool layerOutputLimitExceeded = false;
    bool failed = false;
    uint64 mainSamples = 0;
    uint64 alternateSamples = 0;
    std::set<std::pair<uint64, uint32>> mainCoverage;
    std::set<std::pair<uint64, uint32>> mainVdp2FrameCoverage;
    std::set<uint64> observedMainFrames;
    std::set<uint64> observedVdp2FrameSerials;
    std::set<uint32> observedMainRows;
    std::string captureStatus = "pending";

    bool Configure(const std::filesystem::path &path, unsigned fb, unsigned fe, unsigned rb, unsigned re,
                   const std::filesystem::path &framesPath = {},
                   const std::filesystem::path &layerPath = {}) {
        csvPath = path;
        renderedFramesPath = framesPath;
        layerOutputsPath = layerPath;
        frameBegin = fb; frameEnd = fe; rowBegin = rb; rowEnd = re;
        rows = 0; renderedFrames = 0; renderedFrameBytes = 0; renderedFrameFailed = false;
        renderedFrameLimitExceeded = false;
        layerOutputRecords = 0; layerOutputBytes = 0; layerOutputFailed = false; layerOutputLimitExceeded = false;
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

        if (!layerOutputsPath.empty()) {
            layerOutputs.open(layerOutputsPath, std::ios::binary | std::ios::trunc);
            if (!layerOutputs) return false;
            layerOutputs.write("YMLYR001", 8);
            if (!layerOutputs) { layerOutputs.close(); return false; }
            layerOutputBytes = 8;
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

    bool ShouldCaptureLayerOutput(uint64 frame, uint32 y) const final {
        return layerOutputs.is_open() && !layerOutputFailed && frame >= frameBegin && frame < frameEnd &&
               y >= rowBegin && y < rowEnd && layerOutputBytes < kMaxLayerOutputBytes;
    }

    static void AppendLE(unsigned char *output, size_t offset, uint64 value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) {
            output[offset + i] = static_cast<unsigned char>((value >> (i * 8)) & 0xFF);
        }
    }

    void LayerOutput(uint64 frame, uint64 serial, uint32 ordinal, uint32 y, bool alternateField, uint32 width,
                     const ymir::debug::VDP2LayerOutputView *layers, uint32 layerCount) final {
        if (!ShouldCaptureLayerOutput(frame, y)) return;
        constexpr uint64 kRecordHeaderBytes = 28;
        constexpr uint64 kBytesPerPixel = 6;
        if (layers == nullptr || layerCount != 7 || width == 0 || width > 1024) {
            layerOutputFailed = failed = true;
            return;
        }
        const uint64 recordBytes = kRecordHeaderBytes + static_cast<uint64>(width) * kBytesPerPixel;
        const uint64 requiredBytes = recordBytes * layerCount;
        if (requiredBytes > kMaxLayerOutputBytes - layerOutputBytes) {
            layerOutputFailed = layerOutputLimitExceeded = failed = true;
            return;
        }
        std::array<unsigned char, 28> recordHeader{};
        std::array<unsigned char, 4096> colorPlane{};
        std::array<unsigned char, 1024> specialColorPlane{};
        for (uint32 i = 0; i < layerCount; ++i) {
            const auto &layer = layers[i];
            if (layer.color == nullptr || layer.priority == nullptr || layer.specialColorCalc == nullptr) {
                layerOutputFailed = failed = true;
                return;
            }
            AppendLE(recordHeader.data(), 0, frame, 8);
            AppendLE(recordHeader.data(), 8, serial, 8);
            AppendLE(recordHeader.data(), 16, ordinal, 4);
            AppendLE(recordHeader.data(), 20, y, 4);
            AppendLE(recordHeader.data(), 24, alternateField ? 1 : 0, 1);
            AppendLE(recordHeader.data(), 25, layer.layer, 1);
            AppendLE(recordHeader.data(), 26, width, 2);
            const auto *colors = static_cast<const unsigned char *>(layer.color);
            for (uint32 x = 0; x < width; ++x) {
                uint32 color = 0;
                std::memcpy(&color, colors + static_cast<size_t>(x) * sizeof(color), sizeof(color));
                AppendLE(colorPlane.data(), static_cast<size_t>(x) * 4, color, 4);
                specialColorPlane[x] = static_cast<unsigned char>(layer.specialColorCalc[x] ? 1 : 0);
            }
            layerOutputs.write(reinterpret_cast<const char *>(recordHeader.data()),
                               static_cast<std::streamsize>(recordHeader.size()));
            layerOutputs.write(reinterpret_cast<const char *>(colorPlane.data()),
                               static_cast<std::streamsize>(static_cast<size_t>(width) * 4));
            layerOutputs.write(reinterpret_cast<const char *>(layer.priority), static_cast<std::streamsize>(width));
            layerOutputs.write(reinterpret_cast<const char *>(specialColorPlane.data()),
                               static_cast<std::streamsize>(width));
            if (!layerOutputs) {
                layerOutputFailed = failed = true;
                return;
            }
            ++layerOutputRecords;
            layerOutputBytes += recordBytes;
        }
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
        if (layerOutputs.is_open()) {
            layerOutputs.flush();
            if (!layerOutputs.good()) { failed = true; layerOutputFailed = true; }
            layerOutputs.close();
            if (layerOutputs.fail()) { failed = true; layerOutputFailed = true; }
            auto layerReceiptPath = layerOutputsPath;
            layerReceiptPath += ".receipt.txt";
            std::ofstream layerReceipt(layerReceiptPath, std::ios::trunc);
            if (!layerReceipt) return false;
            layerReceipt << "format=ymir-vdp2-layer-output-trace-v1\n"
                         << "binary=" << layerOutputsPath.string() << "\n"
                         << "magic=YMLYR001\nrecord_header_bytes=28\n"
                         << "record_header=frame_index:u64le,vdp2_frame_serial:u64le,line_call_ordinal:u32le,row_vcnt:u32le,alternate_field:u8,layer_id:u8,width:u16le\n"
                         << "pixel_planes=color_u32le[width],priority_u8[width],special_color_calc_u8[width]\n"
                         << "layer_ids=0:sprite,1:rbg0,2:nbg0_or_rbg1,3:nbg1_or_exbg,4:nbg2,5:nbg3,6:transparent_mesh\n"
                         << "records_written=" << layerOutputRecords << "\nbytes_written=" << layerOutputBytes << "\n"
                         << "byte_limit=" << kMaxLayerOutputBytes << "\nlimit_exceeded="
                         << (layerOutputLimitExceeded ? 1 : 0) << "\nwrite_error=" << (layerOutputFailed ? 1 : 0) << "\n";
            layerReceipt.flush();
            if (!layerReceipt.good()) return false;
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
