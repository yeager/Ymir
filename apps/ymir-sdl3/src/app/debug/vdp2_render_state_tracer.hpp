#pragma once

#include <ymir/debug/vdp2_render_state_tracer.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>

namespace app {

struct VDP2RenderStateTracer final : ymir::debug::IVDP2RenderStateTracer {
    std::filesystem::path csvPath;
    unsigned frameBegin = 0, frameEnd = 0, rowBegin = 0, rowEnd = 0;
    std::ofstream csv;
    uint64 rows = 0;
    bool failed = false;
    uint64 mainSamples = 0;
    uint64 alternateSamples = 0;
    std::set<std::pair<uint64, uint32>> mainCoverage;
    std::set<std::pair<uint64, uint32>> mainVdp2FrameCoverage;
    std::set<uint64> observedMainFrames;
    std::set<uint64> observedVdp2FrameSerials;
    std::set<uint32> observedMainRows;
    std::string captureStatus = "pending";

    bool Configure(const std::filesystem::path &path, unsigned fb, unsigned fe, unsigned rb, unsigned re) {
        csvPath = path;
        frameBegin = fb; frameEnd = fe; rowBegin = rb; rowEnd = re;
        rows = 0; failed = false; mainSamples = 0; alternateSamples = 0; captureStatus = "pending";
        mainCoverage.clear(); mainVdp2FrameCoverage.clear(); observedMainFrames.clear();
        observedVdp2FrameSerials.clear(); observedMainRows.clear();
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
        const uint64 expectedMainSamples =
            static_cast<uint64>(frameEnd - frameBegin) * (rowEnd - rowBegin);
        if (captureStatus == "pending") {
            captureStatus = !failed && mainSamples == expectedMainSamples &&
                                    mainCoverage.size() == expectedMainSamples ? "complete" : "incomplete";
        }
        auto receiptPath = csvPath;
        receiptPath += ".receipt.txt";
        std::ofstream receipt(receiptPath, std::ios::trunc);
        if (!receipt) return false;
        receipt << "format=ymir-vdp2-render-state-trace-v2\n"
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
                << "observed_alternate_field_samples=" << alternateSamples << "\n"
                << "observed_main_frame_ids=";
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
