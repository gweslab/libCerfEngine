#pragma once

#include <cstdint>

#include <cerfengine/engine.h>
#include <cerfengine/isa_block_space.h>
#include "../dram_host_regions.h"
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

    JitCodeArena&  Arena()            { return arena_; }
    IsaBlockSpace& Space(bool m16)    { return m16 ? blocks16_ : blocks_; }

    void Flush();

    void ContextSwitchFlush();

    void InvalidateOnRamWrite(uint8_t* host, uint32_t size);

    uint32_t BlockIndexKey(uint32_t phys_start);

    void SetFixedWindowHost(uint32_t pa, uint32_t size);
    void AddDmaRegion(uint32_t pa, uint32_t size);

private:
    struct DmaRegionEntry { uint8_t* base = nullptr; uint32_t size = 0; };
    static constexpr int kMaxDmaRegions = 4;

    bool TryBlockIndexKey(uint32_t phys_start, uint32_t* key);

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

    uint8_t* window_host_base_ = nullptr;
    uint32_t window_size_      = 0;

    DmaRegionEntry dma_regions_[kMaxDmaRegions];
    int            dma_region_count_ = 0;

    DramHostTable dram_;
    PhysicalMap*  map_       = nullptr;
    MipsMmu*      mmu_       = nullptr;
    MipsCpuState* cpu_state_ = nullptr;
};

}
