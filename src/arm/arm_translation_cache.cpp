#include "arm_translation_cache.h"
#include <cerfengine/calling_convention.h>

#include <atomic>
#include <cstring>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include "../host_platform.h"
#include "arm_tlb_ops.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmTranslationCache);

void ArmTranslationCache::PendFlush() {
    flush_pending_ = true;
    std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
        .fetch_or(kChainExitFlush, std::memory_order_acq_rel);
}

void ArmTranslationCache::OnReady() {
    mmu_       = &engine_.Get<ArmMmu>();
    cpu_state_ = engine_.Get<ArmCpu>().State();
    mmu_->BindTranslationCache(this);

    arena_.Initialize(engine_);

    const ArmMmuState* st = mmu_->State();
    if (st->code_word_top <= st->code_word_base) {
        engine_.Panic("ArmTranslationCache: the host declares no cached-DRAM extent; "
                      "the JIT block index has no page window");
    }
    const uint32_t page_base  = st->code_word_base >> 12;
    const uint32_t page_count = (st->code_word_top - st->code_word_base) >> 12;
    blocks_arm_.Initialize(engine_, page_base, page_count);
    blocks_thumb_.Initialize(engine_, page_base, page_count);

    CERFENGINE_LOG(engine_, kJit, "ArmTranslationCache::OnReady: arena + %u-page block "
                   "window at PA 0x%08X\n", page_count, st->code_word_base);
}

void* ArmTranslationCache::Lookup(bool thumb, uint32_t privilege, uint32_t folded_pc) {
    if (flush_pending_) {
        flush_pending_ = false;
        std::atomic_ref<uint32_t>(cpu_state_->chain_exit_request)
            .fetch_and(~kChainExitFlush, std::memory_order_acq_rel);
        Flush();
    }
    return Space(thumb).JumpCacheLookup(privilege, folded_pc);
}

void ArmTranslationCache::Flush() {
    arena_.Flush();
    blocks_arm_.FlushAll();
    blocks_thumb_.FlushAll();
}

void ArmTranslationCache::ContextSwitchFlush() {
    InvalidateVaCachesAll();
}

void ArmTranslationCache::InvalidateVaCachesRange(uint32_t folded_va,
                                                   uint32_t span_bytes) {
    blocks_arm_.JumpCacheClearRange(folded_va, span_bytes);
    blocks_thumb_.JumpCacheClearRange(folded_va, span_bytes);
    IsaBlockSpace::ClearJcSlot(folded_va - 2u, &blocks_thumb_);
}

void CERFENGINE_FASTCALL ArmTranslationCache::ContextSwitchFlushHelper(
    ArmTranslationCache* tc) {
    tc->ContextSwitchFlush();
}

void ArmTranslationCache::InvalidateDirtyCodePages() {
    ArmMmuState* st    = mmu_->State();
    uint8_t*     dirty = st->code_page_dirty;
    if (!dirty) return;
    const uint32_t base  = st->code_word_base;
    const uint32_t bytes = st->code_page_dirty_bytes;
    for (uint32_t i = 0; i < bytes; ++i) {
        uint8_t b8 = dirty[i];
        if (!b8) continue;
        dirty[i] = 0;
        for (uint32_t b = 0; b < 8u; ++b) {
            if (!(b8 & (1u << b))) continue;
            const uint32_t page_pa = base + (((i << 3) + b) << 12);
            const platform::CodeWriteScope write;
            blocks_arm_.RemoveRange(page_pa, page_pa + 0x0FFFu);
            blocks_thumb_.RemoveRange(page_pa, page_pa + 0x0FFFu);
            const uint32_t word_byte = (page_pa - base) >> 5;
            if (word_byte + 128u <= st->code_word_bitmap_bytes) {
                std::memset(st->code_xlat_bitmap + word_byte, 0, 128u);
            }
        }
    }
}

void CERFENGINE_FASTCALL ArmTranslationCache::InvalidateDirtyCodePagesHelper(
    ArmTranslationCache* tc) {
    tc->InvalidateDirtyCodePages();
}

void CERFENGINE_FASTCALL ArmTranslationCache::ItlbInvalidateAllHelper(
    ArmTranslationCache* tc) {
    tc->mmu_->InvalidateInstructionTlbs();
    tc->InvalidateVaCachesAll();
}

void CERFENGINE_FASTCALL ArmTranslationCache::DtlbInvalidateAllHelper(
    ArmTranslationCache* tc) {
    tc->mmu_->InvalidateDataTlbs();
}

ArmTlbInvalidation ArmTranslationCache::InvalidateTlbsByVa(ArmTlbUnit (&units)[kArmTlbPrivilegeCount],
                                                          uint32_t mva) {
    const uint32_t     process_id = mmu_->State()->process_id;
    ArmTlbInvalidation widest     = ArmTlbInvalidateByVa(&units[0], process_id, mva);
    for (uint32_t p = 1; p < kArmTlbPrivilegeCount; ++p) {
        const ArmTlbInvalidation inv = ArmTlbInvalidateByVa(&units[p], process_id, mva);
        if (inv.span_bytes > widest.span_bytes) widest = inv;
    }
    return widest;
}

void CERFENGINE_FASTCALL ArmTranslationCache::UtlbInvalidateAllHelper(
    ArmTranslationCache* tc) {
    tc->mmu_->InvalidateAllTlbs();
    tc->InvalidateVaCachesAll();
}

void CERFENGINE_FASTCALL ArmTranslationCache::ItlbInvalidateMvaHelper(
    uint32_t mva, ArmTranslationCache* tc) {
    const ArmTlbInvalidation invalidation =
        tc->InvalidateTlbsByVa(tc->mmu_->State()->instruction_tlb, mva);
    tc->InvalidateVaCachesRange(invalidation.base, invalidation.span_bytes);
}

void CERFENGINE_FASTCALL ArmTranslationCache::DtlbInvalidateMvaHelper(
    uint32_t mva, ArmTranslationCache* tc) {
    tc->InvalidateTlbsByVa(tc->mmu_->State()->data_tlb, mva);
}

void CERFENGINE_FASTCALL ArmTranslationCache::UtlbInvalidateMvaHelper(
    uint32_t mva, ArmTranslationCache* tc) {
    ArmMmuState* st = tc->mmu_->State();
    tc->InvalidateTlbsByVa(st->data_tlb, mva);
    const ArmTlbInvalidation invalidation = tc->InvalidateTlbsByVa(st->instruction_tlb, mva);
    tc->InvalidateVaCachesRange(invalidation.base, invalidation.span_bytes);
}

}
