#include "vdp2_pixel_fetch_trace.hpp"

#include <fmt/format.h>

namespace app {

VDP2PixelFetchTrace::VDP2PixelFetchTrace(const std::filesystem::path &csvPath, uint64_t firstFrame,
                                         uint64_t endFrame, uint32_t firstRow, uint32_t endRow, uint32_t firstX,
                                         uint32_t endX, uint64_t maxRows)
    : m_csvPath(csvPath)
    , m_csv(csvPath, std::ios::out | std::ios::trunc)
    , m_firstFrame(firstFrame)
    , m_endFrame(endFrame)
    , m_firstRow(firstRow)
    , m_endRow(endRow)
    , m_firstX(firstX)
    , m_endX(endX)
    , m_maxRows(maxRows) {
    if (m_csv) {
        m_csv << "# ymir-vdp2-pixel-fetch-csv-v1\n"
                 "frame,vdp2_frame_serial,field,row,layer,screen_x,scroll_x,scroll_y,window_blocked,mosaic_reused,"
                 "mosaic_source_x,pnd_source_valid,pnd_access_allowed,pnd_address,pnd_width_bytes,raw_pnd,char_num,"
                 "char_address,char_data_cache_hit,char_pattern_access_allowed,char_data_valid,dot_address,dot_value,"
                 "dot_value_valid,priority\n";
    }
}

VDP2PixelFetchTrace::~VDP2PixelFetchTrace() {
    std::lock_guard lock(m_mutex);
    if (m_csv.is_open()) {
        m_csv.flush();
        m_csv.close();
    }
    std::ofstream receipt(m_csvPath.string() + ".receipt.json", std::ios::out | std::ios::trunc);
    if (receipt) {
        receipt << fmt::format(
            "{{\n  \"format\": \"ymir-vdp2-pixel-fetch-receipt-v1\",\n  \"csv\": \"{}\",\n"
            "  \"frame_range\": [ {}, {} ],\n  \"row_range\": [ {}, {} ],\n"
            "  \"x_range\": [ {}, {} ],\n  \"pixel_limit\": {},\n  \"rows_written\": {},\n"
            "  \"rows_dropped\": {},\n  \"write_errors\": {},\n  \"frames_observed\": {},\n"
            "  \"rows_observed\": {},\n  \"complete\": {}\n}}\n",
            m_csvPath.filename().string(), m_firstFrame, m_endFrame, m_firstRow, m_endRow, m_firstX, m_endX,
            m_maxRows, m_rows, m_droppedRows, m_writeErrors, m_observedFrames.size(), m_observedRows.size(),
            (m_droppedRows == 0 && m_writeErrors == 0 && m_csv.good()) ? "true" : "false");
    }
}

bool VDP2PixelFetchTrace::ShouldTrace(uint64_t frame, uint32_t row, uint32_t screenX, uint8_t layer) const {
    return layer == 1 && frame >= m_firstFrame && frame < m_endFrame && row >= m_firstRow && row < m_endRow &&
           screenX >= m_firstX && screenX < m_endX;
}

void VDP2PixelFetchTrace::Fetch(const ymir::debug::VDP2PixelFetchSample &sample) {
    std::lock_guard lock(m_mutex);
    if (!m_csv.good() || m_rows >= m_maxRows) {
        ++m_droppedRows;
        return;
    }
    constexpr char comma = 44;
    m_csv << sample.frame << comma << sample.vdp2FrameSerial << comma << (sample.alternateField ? 1 : 0) << comma
          << sample.row << comma << unsigned(sample.layer) << comma << sample.screenX << comma << sample.scrollX << comma
          << sample.scrollY << comma << (sample.windowBlocked ? 1 : 0) << comma << (sample.mosaicReused ? 1 : 0)
          << comma << sample.mosaicSourceX << comma << (sample.pndSourceValid ? 1 : 0) << comma
          << (sample.pndAccessAllowed ? 1 : 0) << comma << "0x" << fmt::format("{:05X}", sample.pndAddress) << comma
          << unsigned(sample.pndWidthBytes) << comma << "0x" << fmt::format("{:08X}", sample.rawPND) << comma
          << sample.charNum << comma << "0x" << fmt::format("{:05X}", sample.charAddress) << comma
          << (sample.charDataCacheHit ? 1 : 0) << comma << (sample.charPatternAccessAllowed ? 1 : 0) << comma
          << (sample.charDataValid ? 1 : 0) << comma << "0x" << fmt::format("{:05X}", sample.dotAddress) << comma
          << unsigned(sample.dotValue) << comma << (sample.dotValueValid ? 1 : 0) << comma
          << unsigned(sample.priority) << static_cast<char>(10);
    if (!m_csv.good()) {
        ++m_writeErrors;
        return;
    }
    ++m_rows;
    m_observedFrames.insert(sample.frame);
    m_observedRows.insert(sample.row);
}

} // namespace app
