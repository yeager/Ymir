#pragma once

#include <ymir/debug/vdp2_pixel_fetch_tracer.hpp>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>

namespace app {

class VDP2PixelFetchTrace final : public ymir::debug::IVDP2PixelFetchTracer {
public:
    VDP2PixelFetchTrace(const std::filesystem::path &csvPath, uint64_t firstFrame, uint64_t endFrame,
                        uint32_t firstRow, uint32_t endRow, uint32_t firstX, uint32_t endX,
                        uint64_t maxRows);
    ~VDP2PixelFetchTrace();

    bool IsOpen() const { return m_csv.is_open() && m_csv.good(); }
    bool ShouldTrace(uint64_t frame, uint32_t row, uint32_t screenX, uint8_t layer) const override;
    void Fetch(const ymir::debug::VDP2PixelFetchSample &sample) override;

private:
    std::filesystem::path m_csvPath;
    std::ofstream m_csv;
    const uint64_t m_firstFrame;
    const uint64_t m_endFrame;
    const uint32_t m_firstRow;
    const uint32_t m_endRow;
    const uint32_t m_firstX;
    const uint32_t m_endX;
    const uint64_t m_maxRows;
    mutable std::mutex m_mutex;
    uint64_t m_rows = 0;
    uint64_t m_droppedRows = 0;
    uint64_t m_writeErrors = 0;
    std::set<uint64_t> m_observedFrames;
    std::set<uint32_t> m_observedRows;
};

} // namespace app
