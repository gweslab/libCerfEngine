#include <cstddef>

#include <cerfengine/engine.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* EmitTlbFastPath(uint8_t* cursor, BlockContext* ctx, TlbAccess access) {
    using namespace x86;
    const bool is_write = (access != TlbAccess::kRead);

    const int32_t tlb     = static_cast<int32_t>(offsetof(ArmMmuState, data_tlb));
    const int32_t pid     = static_cast<int32_t>(offsetof(ArmMmuState, fcse_fold_id));
    const int32_t ctxid   = static_cast<int32_t>(offsetof(ArmMmuState, contextidr));
    const int32_t cwbase  = static_cast<int32_t>(offsetof(ArmMmuState, code_word_base));
    const int32_t cwtop   = static_cast<int32_t>(offsetof(ArmMmuState, code_word_top));
    const int32_t xlat    = static_cast<int32_t>(offsetof(ArmMmuState, code_xlat_bitmap));
    const int32_t dirty   = static_cast<int32_t>(offsetof(ArmMmuState, code_page_dirty));
    const int32_t e_tag   = tlb + static_cast<int32_t>(offsetof(ArmTlbEntry, tag));
    const int32_t e_add   = tlb + static_cast<int32_t>(offsetof(ArmTlbEntry, va_addend));
    const int32_t e_pa    = tlb + static_cast<int32_t>(offsetof(ArmTlbEntry, pa_page));
    const int32_t e_asid  = tlb + static_cast<int32_t>(offsetof(ArmTlbEntry, asid));
    const int32_t e_glob  = tlb + static_cast<int32_t>(offsetof(ArmTlbEntry, global));
    const int32_t e_wr    = tlb + static_cast<int32_t>(offsetof(ArmTlbEntry, writable));

    uint8_t* miss[3];
    int      nmiss = 0;

    EmitMovRegReg     (cursor, kEax, kEcx);
    EmitTestRegImm32  (cursor, kEax, 0xFE000000u);
    uint8_t* nf1 = EmitJnzLabel(cursor);
    EmitOrRegBaseDisp32(cursor, kEax, kMmuReg, pid);
    FixupLabel(nf1, cursor);

    EmitMovRegReg   (cursor, kEdx, kEax);
    EmitShrReg32Imm (cursor, kEdx, 12);
    EmitAndRegImm32 (cursor, kEdx, kArmTlbSetMask);
    EmitShlReg32Imm (cursor, kEdx, kArmTlbSetShift);
    EmitAddReg32Reg32(cursor, kEdx, kMmuReg);

    EmitAndRegImm32     (cursor, kEax, 0xFFFFF000u);
    EmitCmpRegBaseDisp32(cursor, kEax, kEdx, e_tag);
    miss[nmiss++] = EmitJnzLabel32(cursor);

    EmitMovRegBaseDisp32      (cursor, kEax, kMmuReg, ctxid);
    EmitTestByteBaseDisp32Imm8(cursor, kEdx, e_glob, 0xFF);
    uint8_t* ctx_ok = EmitJnzLabel(cursor);
    EmitCmpReg8BaseDisp32     (cursor, kAl, kEdx, e_asid);
    miss[nmiss++] = EmitJnzLabel32(cursor);
    FixupLabel(ctx_ok, cursor);

    if (is_write) {
        EmitTestByteBaseDisp32Imm8(cursor, kEdx, e_wr, 0xFF);
        miss[nmiss++] = EmitJzLabel32(cursor);

        EmitMovRegReg      (cursor, kEax, kEcx);
        EmitAndRegImm32    (cursor, kEax, 0xFFFu);
        EmitOrRegBaseDisp32(cursor, kEax, kEdx, e_pa);

        EmitCmpRegBaseDisp32(cursor, kEax, kMmuReg, cwbase);
        uint8_t* smc_done_a = EmitJbLabel32(cursor);
        EmitCmpRegBaseDisp32(cursor, kEax, kMmuReg, cwtop);
        uint8_t* smc_done_b = EmitJaeLabel32(cursor);

        EmitSubRegBaseDisp32(cursor, kEax, kMmuReg, cwbase);
        EmitPushReg         (cursor, kEcx);
        EmitMovRegBaseDisp32(cursor, kEcx, kMmuReg, xlat);
        EmitShrReg32Imm     (cursor, kEax, 2);
        EmitBtMemReg        (cursor, kEcx, kEax);
        uint8_t* smc_restore = EmitJncLabel32(cursor);
        EmitShrReg32Imm     (cursor, kEax, 10);
        EmitMovRegBaseDisp32(cursor, kEcx, kMmuReg, dirty);
        EmitBtsMemReg       (cursor, kEcx, kEax);
        FixupLabel32(smc_restore, cursor);
        EmitPopReg          (cursor, kEcx);
        FixupLabel32(smc_done_a, cursor);
        FixupLabel32(smc_done_b, cursor);
    }

    EmitMovRegReg      (cursor, kEax, kEcx);
    EmitTestRegImm32   (cursor, kEax, 0xFE000000u);
    uint8_t* nf2 = EmitJnzLabel(cursor);
    EmitOrRegBaseDisp32(cursor, kEax, kMmuReg, pid);
    FixupLabel(nf2, cursor);
    EmitAddRegBaseDisp32(cursor, kEax, kEdx, e_add);
    uint8_t* hit_done = EmitJmpLabel32(cursor);

    for (int i = 0; i < nmiss; ++i) FixupLabel32(miss[i], cursor);

    uint8_t* io_miss[3];
    int      n_io_miss = 0;

    EmitMovRegReg      (cursor, kEax, kEcx);
    EmitTestRegImm32   (cursor, kEax, 0xFE000000u);
    uint8_t* io_nf = EmitJnzLabel(cursor);
    EmitOrRegBaseDisp32(cursor, kEax, kMmuReg, pid);
    FixupLabel(io_nf, cursor);
    EmitAndRegImm32    (cursor, kEax, 0xFFFFF000u);
    EmitAddRegImm32    (cursor, kEax, kArmTlbIoTagBit);
    EmitCmpRegBaseDisp32(cursor, kEax, kEdx, e_tag);
    io_miss[n_io_miss++] = EmitJnzLabel32(cursor);

    EmitMovRegBaseDisp32      (cursor, kEax, kMmuReg, ctxid);
    EmitTestByteBaseDisp32Imm8(cursor, kEdx, e_glob, 0xFF);
    uint8_t* io_ctx_ok = EmitJnzLabel(cursor);
    EmitCmpReg8BaseDisp32     (cursor, kAl, kEdx, e_asid);
    io_miss[n_io_miss++] = EmitJnzLabel32(cursor);
    FixupLabel(io_ctx_ok, cursor);

    if (is_write) {
        EmitTestByteBaseDisp32Imm8(cursor, kEdx, e_wr, 0xFF);
        io_miss[n_io_miss++] = EmitJzLabel32(cursor);
    }

    EmitMovRegBaseDisp32(cursor, kEax, kEdx, e_pa);
    EmitMovRegReg       (cursor, kEdx, kEcx);
    EmitAndRegImm32     (cursor, kEdx, 0x00000FFFu);
    EmitOrReg32Reg32    (cursor, kEax, kEdx);
    EmitMovDwordPtrReg  (cursor, ctx->emit->Mmu()->IoPendingAddressPtr(), kEax);
    EmitMovDwordPtrImm32(cursor, ctx->emit->Mmu()->IoPendingValidPtr(), 1u);
    EmitXorRegReg       (cursor, kEax, kEax);
    uint8_t* io_done = EmitJmpLabel32(cursor);

    for (int i = 0; i < n_io_miss; ++i) FixupLabel32(io_miss[i], cursor);
    EmitPushReg(cursor, kEcx);
    EmitMovRegImm32(cursor, kEdx,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Mmu())));
    void* helper =
        access == TlbAccess::kReadWrite
            ? reinterpret_cast<void*>(&ArmMmu::TranslateReadWriteHelper)
        : access == TlbAccess::kWrite
            ? reinterpret_cast<void*>(&ArmMmu::TranslateWriteHelper)
            : reinterpret_cast<void*>(&ArmMmu::TranslateReadHelper);
    EmitCall(cursor, helper);
    EmitPopReg(cursor, kEcx);

    FixupLabel32(hit_done, cursor);
    FixupLabel32(io_done, cursor);
    return cursor;
}

uint8_t* EmitTranslateAccess(uint8_t* cursor, BlockContext* ctx,
                             TlbAccess access, bool unpriv) {
    using namespace x86;
    if (!unpriv) {
        return EmitTlbFastPath(cursor, ctx, access);
    }
    if (access == TlbAccess::kReadWrite) {
        ctx->emit->Owner().Panic(
            "EmitTranslateAccess: no unprivileged read-write transfer form exists");
    }
    EmitPushReg(cursor, kEcx);
    EmitMovRegImm32(cursor, kEdx, static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(ctx->emit->Mmu())));
    EmitCall(cursor, access == TlbAccess::kRead
        ? reinterpret_cast<void*>(&ArmMmu::TranslateUserReadHelper)
        : reinterpret_cast<void*>(&ArmMmu::TranslateUserWriteHelper));
    EmitPopReg(cursor, kEcx);
    return cursor;
}

}
