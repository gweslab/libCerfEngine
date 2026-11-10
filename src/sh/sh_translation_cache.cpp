#include "sh_translation_cache.h"

#include <cerfengine/physical_map.h>
#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_cpu_state.h>
#include <cerfengine/sh/sh_mmu.h>
#include "../host_platform.h"

namespace cerfengine {

CERFENGINE_REGISTER(ShTranslationCache);

void ShTranslationCache::OnReady() {
    map_       = &engine_.Get<PhysicalMap>();
    cpu_state_ = engine_.Get<ShCpu>().State();
    const DramHostRegions& regions = engine_.Get<DramHostRegions>();
    dram_                          = regions.Table();
    arena_.Initialize(engine_);
    const DramSpan span = regions.PaSpan();
    blocks_.Initialize(engine_, span.low >> 12, (span.high - span.low) >> 12);
}

void ShTranslationCache::Flush() {
    arena_.Flush();
    blocks_.FlushAll();
}

bool ShTranslationCache::TryBlockIndexKey(uint32_t pa, uint32_t* key) {
    const uint8_t* host = map_->Write(pa);
    if (host == nullptr) {
        *key = pa;
        return true;
    }
    *key = dram_.CanonicalPa(host);
    return *key != DramHostTable::kNotDram;
}

uint32_t ShTranslationCache::BlockIndexKey(uint32_t phys_start, uint32_t guest_pc) {
    uint32_t key = 0u;
    if (TryBlockIndexKey(phys_start, &key)) return key;
    engine_.Halt(HaltReason::kUnimplemented,
                 "ShTranslationCache: block at PA 0x%08X PC 0x%08X lies in writable memory outside DRAM", phys_start,
                 guest_pc);
}

void ShTranslationCache::InvalidateOnRamWrite(const uint8_t* host, uint32_t bytes) { RemoveWritten(host, bytes, true); }

void ShTranslationCache::InvalidateBusMasterWrite(const uint8_t* host, uint32_t bytes, bool block_executing) {
    RemoveWritten(host, bytes, block_executing);
}

void ShTranslationCache::RemoveWritten(const uint8_t* host, uint32_t bytes, bool block_executing) {
    const uint32_t lo = dram_.CanonicalPa(host);
    if (lo == DramHostTable::kNotDram || !blocks_.PageHasBlocks(lo)) return;
    const uint32_t hi = lo + bytes - 1u;
    if (!blocks_.RangeHitsBlock(lo, hi, lo, hi)) return;
    ShTranslation writer{};
    uint32_t      writer_key = 0u;
    const bool    own_block  = block_executing && engine_.Get<ShMmu>().ProbeFetch(cpu_state_->pc, &writer) &&
                           TryBlockIndexKey(writer.pa, &writer_key) &&
                           blocks_.RangeHitsBlock(lo, hi, writer_key, writer_key + 1u);
    if (own_block) {
        engine_.Halt(HaltReason::kUnimplemented,
                     "ShTranslationCache: %u-byte write at PC 0x%08X rewrites the translated block that holds it",
                     bytes, cpu_state_->pc);
    }
    const platform::CodeWriteScope write;
    blocks_.RemoveRange(lo, hi);
}

}
