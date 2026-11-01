#pragma once

#include <ymir/debug/vdp2_pnd_fetch_tracer.hpp>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>

namespace app {

class VDP2PNDFetchTrace final : public ymir::debug::IVDP2PNDFetchTracer {
public:
    VDP2PNDFetchTrace(const std::filesystem::path &csvPath, uint64_t firstFrame, uint64_t endFrame,
                      uint32_t firstRow, uint32_t endRow, uint64_t maxRows);
    ~VDP2PNDFetchTrace();

    bool IsOpen() const { return m_csv.is_open() && m_csv.good(); }
    bool ShouldTrace(uint64_t frame, uint32_t row, uint8_t layer) const override;
    void Fetch(const ymir::debug::VDP2PNDFetchSample &sample) override;

private:
    std::filesystem::path m_csvPath;
    std::ofstream m_csv;
    const uint64_t m_firstFrame;
    const uint64_t m_endFrame;
    const uint32_t m_firstRow;
    const uint32_t m_endRow;
    const uint64_t m_maxRows;
    mutable std::mutex m_mutex;
    uint64_t m_rows = 0;
    uint64_t m_droppedRows = 0;
    uint64_t m_writeErrors = 0;
    std::set<uint64_t> m_observedFrames;
    std::set<uint32_t> m_observedRows;
};

} // namespace app
