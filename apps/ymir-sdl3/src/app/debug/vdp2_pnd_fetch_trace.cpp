#include "vdp2_pnd_fetch_trace.hpp"

#include <fmt/format.h>

namespace app {

VDP2PNDFetchTrace::VDP2PNDFetchTrace(const std::filesystem::path &csvPath, uint64_t firstFrame, uint64_t endFrame,
                                     uint32_t firstRow, uint32_t endRow, uint64_t maxRows)
    : m_csvPath(csvPath)
    , m_csv(csvPath, std::ios::out | std::ios::trunc)
    , m_firstFrame(firstFrame)
    , m_endFrame(endFrame)
    , m_firstRow(firstRow)
    , m_endRow(endRow)
    , m_maxRows(maxRows) {
    if (m_csv) {
        m_csv << "# ymir-vdp2-pnd-fetch-csv-v1\n"
                 "frame,field,row,layer,screen_x,scroll_x,scroll_y,pnd_address,pnd_width_bytes,raw_pnd\n";
    }
}

VDP2PNDFetchTrace::~VDP2PNDFetchTrace() {
    std::lock_guard lock(m_mutex);
    if (m_csv.is_open()) {
        m_csv.flush();
        m_csv.close();
    }
    std::ofstream receipt(m_csvPath.string() + ".receipt.json", std::ios::out | std::ios::trunc);
    if (receipt) {
        receipt << fmt::format(
            "{{\n  \"format\": \"ymir-vdp2-pnd-fetch-receipt-v1\",\n  \"csv\": \"{}\",\n"
            "  \"frame_range\": [ {}, {} ],\n  \"row_range\": [ {}, {} ],\n"
            "  \"row_limit\": {},\n  \"rows_written\": {},\n  \"rows_dropped\": {},\n"
            "  \"write_errors\": {},\n  \"frames_observed\": {},\n  \"rows_observed\": {},\n"
            "  \"complete\": {}\n}}\n",
            m_csvPath.filename().string(), m_firstFrame, m_endFrame, m_firstRow, m_endRow, m_maxRows, m_rows,
            m_droppedRows, m_writeErrors, m_observedFrames.size(), m_observedRows.size(),
            (m_droppedRows == 0 && m_writeErrors == 0 && m_csv.good()) ? "true" : "false");
    }
}

bool VDP2PNDFetchTrace::ShouldTrace(uint64_t frame, uint32_t row, uint8_t layer) const {
    return (layer == 1 || layer == 2) && frame >= m_firstFrame && frame < m_endFrame && row >= m_firstRow &&
           row < m_endRow;
}

void VDP2PNDFetchTrace::Fetch(const ymir::debug::VDP2PNDFetchSample &sample) {
    std::lock_guard lock(m_mutex);
    if (!m_csv.good() || m_rows >= m_maxRows) {
        ++m_droppedRows;
        return;
    }
    const auto &ctx = sample.context;
    constexpr char comma = 44;
    m_csv << ctx.frame << comma << (ctx.alternateField ? 1 : 0) << comma << ctx.row << comma << unsigned(ctx.layer)
          << comma << ctx.screenX << comma << ctx.scrollX << comma << ctx.scrollY << comma << "0x"
          << fmt::format("{:05X}", sample.pndAddress) << comma << unsigned(sample.pndWidthBytes) << comma << "0x"
          << fmt::format("{:08X}", sample.rawPND) << static_cast<char>(10);
    if (!m_csv.good()) {
        ++m_writeErrors;
        return;
    }
    ++m_rows;
    m_observedFrames.insert(ctx.frame);
    m_observedRows.insert(ctx.row);
}

} // namespace app
