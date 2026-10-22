#include "mips_translation_cache.h"

#include <cstdio>

#include <cerfengine/engine.h>
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mips/mips_mmu.h>
#include <cerfengine/physical_map.h>
#include <cerfengine/physical_memory_host.h>

#include "../host_platform.h"

namespace cerfengine {

CERFENGINE_REGISTER(MipsTranslationCache);

bool MipsTranslationCache::ValidateDram(const std::vector<DramRegion>& dram, void* memory,
                                        std::string& error) {
    char text[256];
    if (static_cast<int>(dram.size()) > kMaxDramRegions) {
        std::snprintf(text, sizeof(text),
                      "the host declares %zu cached DRAM regions; the MIPS "
                      "self-modifying-code index supports 1..%d",
                      dram.size(), kMaxDramRegions);
        error = text;
        return false;
    }
    for (const DramRegion& r : dram) {
        PhysicalPage page;
        if (host::PhysicalTranslate(memory, r.pa_base, false, page) == nullptr ||
            (r.size & 0xFFFu) != 0u) {
            std::snprintf(text, sizeof(text),
                          "DRAM pa=0x%08X size=0x%X is not a page-multiple "
                          "backed region", r.pa_base, r.size);
            error = text;
            return false;
        }
    }
    return true;
}

void MipsTranslationCache::OnReady() {
    map_       = &engine_.Get<PhysicalMap>();
    cpu_state_ = engine_.Get<MipsCpu>().State();

    arena_.Initialize(engine_);

    uint32_t index_base = 0;
    for (const cerfengine::DramRegion& r : engine_.Host().CachedDramRegions()) {
        uint8_t* host = map_->PeekRead(r.pa_base);
        DramHostRegion& e = dram_regions_[dram_region_count_++];
        e.host_lo    = host;
        e.host_hi    = host + r.size;
        e.index_base = index_base;
        index_base  += r.size;
    }
    dram_index_size_ = index_base;
    blocks_.Initialize(engine_, 0, dram_index_size_ >> 12);
    blocks16_.Initialize(engine_, 0, dram_index_size_ >> 12);

    mmu_ = &engine_.Get<MipsMmu>();
    mmu_->Bind(&blocks_, &blocks16_);

    CERFENGINE_LOG(engine_, kJit,
                   "MipsTranslationCache::OnReady: %d DRAM bank(s), %u SMC-indexed pages\n",
                   dram_region_count_, dram_index_size_ >> 12);
}

void MipsTranslationCache::Flush() {
    arena_.Flush();
    blocks_.FlushAll();
    blocks16_.FlushAll();
}

void MipsTranslationCache::ContextSwitchFlush() {
    blocks_.JumpCacheFlush();
    blocks16_.JumpCacheFlush();
}

bool MipsTranslationCache::TryBlockIndexKey(uint32_t phys_start, uint32_t* key) {
    uint8_t* host = map_->Write(phys_start);
    if (!host) {
        *key = kBlockUnindexed;
        return true;
    }

    const uint32_t off = DramIndexOffset(host);
    if (off != UINT32_MAX) {
        *key = off;
        return true;
    }
    if (InFixedWindow(host)) {
        *key = kBlockUnindexed;
        return true;
    }
    return false;
}

uint32_t MipsTranslationCache::BlockIndexKey(uint32_t phys_start) {
    uint32_t key = 0;
    if (TryBlockIndexKey(phys_start, &key)) return key;
    engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                 "MipsTranslationCache::BlockIndexKey: block at pa=0x%08X pc=0x%08X is in "
                 "a writable region that is neither DRAM nor the fixed window; a store "
                 "into it would not invalidate the block",
                 phys_start, cpu_state_->pc);
}

void MipsTranslationCache::InvalidateOnRamWrite(uint8_t* host, uint32_t size) {
    const uint32_t off = DramIndexOffset(host);
    if (off == UINT32_MAX) {
        if (InFixedWindow(host)) return;
        if (InDmaRegion(host)) return;
        engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                     "MipsTranslationCache::InvalidateOnRamWrite: store of %u byte(s) at "
                     "host %p pc=0x%08X lands in a writable region that is neither DRAM "
                     "nor the fixed window; self-modifying code there is unmodeled",
                     size, static_cast<void*>(host), cpu_state_->pc);
    }
    if (blocks_.PageHasBlocks(off)) {
        const platform::CodeWriteScope write;
        blocks_.RemoveRange(off, off + size - 1u);
    }
    if (blocks16_.PageHasBlocks(off)) {
        const platform::CodeWriteScope write;
        blocks16_.RemoveRange(off, off + size - 1u);
    }
}

void MipsTranslationCache::SetFixedWindowHost(uint32_t pa, uint32_t size) {
    window_host_base_ = map_->PeekWrite(pa);
    window_size_      = window_host_base_ ? size : 0u;
}

void MipsTranslationCache::AddDmaRegion(uint32_t pa, uint32_t size) {
    uint8_t* base = map_->PeekWrite(pa);
    if (!base) return;
    if (dma_region_count_ >= kMaxDmaRegions) {
        engine_.Panic("MipsTranslationCache::AddDmaRegion: more than %d DMA regions "
                      "declared", kMaxDmaRegions);
    }
    dma_regions_[dma_region_count_].base = base;
    dma_regions_[dma_region_count_].size = size;
    ++dma_region_count_;
}

}
