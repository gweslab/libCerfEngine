#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>
#include <cerfengine/isa_block_space.h>
#include "../jit_code_arena.h"
#include <cerfengine/arm/cpu_state.h>
#include "arm_tlb_ops.h"

namespace cerfengine {

class ArmMmu;

class ArmTranslationCache : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kArm;
    }

    JitCodeArena&  Arena()               { return arena_; }
    IsaBlockSpace& Space(bool thumb)     { return thumb ? blocks_thumb_ : blocks_arm_; }

    void* Lookup(bool thumb, uint32_t privilege, uint32_t folded_pc);

    void Flush();
    void PendFlush();

    /* QEMU tlb_flush -> tcg_flush_jmp_cache (cputlb.c). */
    void InvalidateVaCachesAll() {
        blocks_arm_.JumpCacheFlush();
        blocks_thumb_.JumpCacheFlush();
    }

    void InvalidateVaCachesRange(uint32_t folded_va, uint32_t span_bytes);

    void ContextSwitchFlush();
    void InvalidateDirtyCodePages();

    static void CERFENGINE_FASTCALL ContextSwitchFlushHelper(ArmTranslationCache* tc);
    static void CERFENGINE_FASTCALL InvalidateDirtyCodePagesHelper(ArmTranslationCache* tc);

    static void CERFENGINE_FASTCALL ItlbInvalidateAllHelper(ArmTranslationCache* tc);
    static void CERFENGINE_FASTCALL DtlbInvalidateAllHelper(ArmTranslationCache* tc);
    static void CERFENGINE_FASTCALL UtlbInvalidateAllHelper(ArmTranslationCache* tc);
    static void CERFENGINE_FASTCALL ItlbInvalidateMvaHelper(uint32_t mva, ArmTranslationCache* tc);
    static void CERFENGINE_FASTCALL DtlbInvalidateMvaHelper(uint32_t mva, ArmTranslationCache* tc);
    static void CERFENGINE_FASTCALL UtlbInvalidateMvaHelper(uint32_t mva, ArmTranslationCache* tc);

private:
    ArmTlbInvalidation InvalidateTlbsByVa(ArmTlbUnit (&units)[kArmTlbPrivilegeCount], uint32_t mva);

    JitCodeArena  arena_;
    IsaBlockSpace blocks_arm_;
    IsaBlockSpace blocks_thumb_;
    bool          flush_pending_ = false;

    ArmMmu*      mmu_       = nullptr;
    ArmCpuState* cpu_state_ = nullptr;
};

}
