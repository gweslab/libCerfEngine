#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <cerfengine/engine.h>
#include <cerfengine/isa_block_space.h>
#include "../jit_code_arena.h"

namespace cerfengine {

struct MipsCpuState;

class MipsMmu;
class PhysicalMap;

class MipsTranslationCache : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    static bool ValidateDram(const std::vector<DramRegion>& dram, void* memory,
                             std::string& error);

    JitCodeArena&  Arena()            { return arena_; }
    IsaBlockSpace& Space(bool m16)    { return m16 ? blocks16_ : blocks_; }

    void Flush();

    void ContextSwitchFlush();

    /* QEMU notdirty_write (cputlb.c): a guest store onto a page that
       holds translated blocks drops the blocks its bytes overlap. */
    void InvalidateOnRamWrite(uint8_t* host, uint32_t size);

    bool     TryBlockIndexKey(uint32_t phys_start, uint32_t* key);
    uint32_t BlockIndexKey(uint32_t phys_start);

    void SetFixedWindowHost(uint32_t pa, uint32_t size);
    void AddDmaRegion(uint32_t pa, uint32_t size);

private:
    struct DramHostRegion {
        uint8_t* host_lo    = nullptr;
        uint8_t* host_hi    = nullptr;
        uint32_t index_base = 0;
    };
    static constexpr int kMaxDramRegions = 4;

    struct DmaRegionEntry { uint8_t* base = nullptr; uint32_t size = 0; };
    static constexpr int kMaxDmaRegions = 4;

    uint32_t DramIndexOffset(const uint8_t* host) const {
        for (int i = 0; i < dram_region_count_; ++i) {
            const DramHostRegion& r = dram_regions_[i];
            if (host >= r.host_lo && host < r.host_hi) {
                return r.index_base + static_cast<uint32_t>(host - r.host_lo);
            }
        }
        return UINT32_MAX;
    }

    bool InFixedWindow(const uint8_t* host) const {
        return window_host_base_ && host >= window_host_base_ &&
               host < window_host_base_ + window_size_;
    }

    bool InDmaRegion(const uint8_t* host) const {
        for (int i = 0; i < dma_region_count_; ++i)
            if (host >= dma_regions_[i].base &&
                host < dma_regions_[i].base + dma_regions_[i].size)
                return true;
        return false;
    }

    JitCodeArena  arena_;
    IsaBlockSpace blocks_;
    IsaBlockSpace blocks16_;

    DramHostRegion dram_regions_[kMaxDramRegions];
    int            dram_region_count_ = 0;
    uint32_t       dram_index_size_   = 0;

    uint8_t* window_host_base_ = nullptr;
    uint32_t window_size_      = 0;

    DmaRegionEntry dma_regions_[kMaxDmaRegions];
    int            dma_region_count_ = 0;

    PhysicalMap*  map_       = nullptr;
    MipsMmu*      mmu_       = nullptr;
    MipsCpuState* cpu_state_ = nullptr;
};

}
