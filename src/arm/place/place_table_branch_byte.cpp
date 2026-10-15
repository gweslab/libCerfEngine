#include <cstddef>
#include <cstdint>

#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceTableBranchByte(uint8_t* cursor, DecodedInsn* d,
                              BlockContext* ctx) {
    using namespace x86;

    ArmMmu*        mmu     = ctx->emit->Mmu();
    const ArmSctlr sctlr   = mmu->State()->effective_control_register;
    const uint32_t mmu_imm =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mmu));
    const bool     is_tbh  = d->op1 != 0u;

    if (d->rn == 15u) {
        EmitMovRegImm32(cursor, kEcx, ArmPcReadValue(d, ctx));
    } else {
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rn));
    }

    if (is_tbh) {
        EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, ArmGprDisp(d->rm));
        EmitShlReg32Imm     (cursor, kEdx, 1u);
        EmitAddReg32Reg32   (cursor, kEcx, kEdx);
    } else {
        EmitAddRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rm));
    }

    uint8_t* align_fault_label     = nullptr;
    uint8_t* cross_label           = nullptr;
    uint8_t* unaligned_fault_label = nullptr;
    if (is_tbh) {
        EmitHalfwordAlignCheck(cursor, sctlr.bits.a != 0u,
                               &align_fault_label, &cross_label);
    }

    cursor = EmitTranslateAccess(cursor, ctx, TlbAccess::kRead, false);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* abort_label = EmitJzLabel32(cursor);

    /* MOVZX EDX, byte [EAX] - 0F B6 /r; MOVZX EDX, word [EAX] - 0F B7 /r
       (SDM Vol. 2B 4-140 MOVZX). */
    Emit8(cursor, 0x0F);
    Emit8(cursor, is_tbh ? 0xB7 : 0xB6);
    EmitModRmReg(cursor, 0, kEax, kEdx);

    if (cross_label != nullptr) {
        uint8_t* compute_label = EmitJmpLabel32(cursor);

        FixupLabel32(cross_label, cursor);
        EmitPush32 (cursor, 0u);
        EmitPushReg(cursor, kEcx);
        EmitPush32 (cursor, mmu_imm);
        EmitCall(cursor, reinterpret_cast<void*>(
            &ArmMmu::UnalignedHalfwordLoadHelper));
        EmitAddRegImm32(cursor, kEsp, 12u);
        EmitCmpRegImm32(cursor, kEax, 0xFFFFFFFFu);
        unaligned_fault_label = EmitJzLabel32(cursor);
        EmitMovRegReg(cursor, kEdx, kEax);

        FixupLabel32(compute_label, cursor);
    }

    EmitShlReg32Imm(cursor, kEdx, 1u);
    EmitAddRegImm32(cursor, kEdx, ArmPcReadValue(d, ctx));
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(15u), kEdx);
    cursor = PlaceR15ModifiedHelper(cursor, d, ctx);

    if (align_fault_label != nullptr) {
        FixupLabel32(align_fault_label, cursor);
        EmitMovRegImm32(cursor, kEdx, mmu_imm);
        EmitCall(cursor,
                 reinterpret_cast<void*>(&ArmMmu::AlignmentFaultReadHelper));
    }
    if (unaligned_fault_label != nullptr) {
        FixupLabel32(unaligned_fault_label, cursor);
    }
    FixupLabel32(abort_label, cursor);
    return EmitAbortDataTail(cursor, d, ctx);
}

}
