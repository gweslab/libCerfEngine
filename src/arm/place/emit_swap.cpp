#include <cstddef>
#include <cstdint>

#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* SWP / SWPB - ARM ARM DDI 0406C.c A8.8.229 (pp. A8-722/A8-723). */
uint8_t* EmitSwap(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;

    /* A8.8.229: t == 15 || t2 == 15 || n == 15 || n == t || n == t2 is
       UNPREDICTABLE -> UNDEFINED (p. Glossary-2737). */
    if (d->rd == 15 || d->rm == 15 || d->rn == 15 ||
        d->rn == d->rd || d->rn == d->rm) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    ArmMmu*                   mmu    = ctx->emit->Mmu();
    const ArmSctlr            sctlr  = mmu->State()->effective_control_register;
    /* Unaligned SWP faults when U == 1 (Table A3-1, p. A3-108; D12.3.1,
       p. D12-2506) or A == 1 (D15.3.1, p. D15-2592); U by architecture
       version per Table D12-1 (p. D12-2506). */
    const bool u1 = mmu->UnalignedAccessesFault();
    const bool align_fault_check = !d->n && (u1 || sctlr.bits.a);

    uint8_t* align_fault_label = nullptr;

    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rn));
    if (align_fault_check) {
        EmitTestRegImm32(cursor, kEcx, 3u);
        align_fault_label = EmitJnzLabel32(cursor);
    } else if (!d->n) {
        EmitAndRegImm32(cursor, kEcx, 0xFFFFFFFCu);
    }
    cursor = EmitTlbFastPath(cursor, ctx, TlbAccess::kReadWrite);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* abort_label = EmitJzLabel32(cursor);

    if (d->n) {
        /* MOVZX EDX, byte [EAX] - 0F B6 /r (SDM Vol. 2B 4-140 MOVZX). */
        Emit8(cursor, 0x0F);
        Emit8(cursor, 0xB6);
        EmitModRmReg(cursor, 0, kEax, kEdx);

        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rm));
        /* MOV [EAX], CL - 88 /r (SDM Vol. 2B 4-35 MOV). */
        Emit8(cursor, 0x88);
        EmitModRmReg(cursor, 0, kEax, kCl);
    } else {
        /* MOV EDX, [EAX] - 8B /r (SDM Vol. 2B 4-35 MOV). */
        Emit8(cursor, 0x8B);
        EmitModRmReg(cursor, 0, kEax, kEdx);

        if (!align_fault_check) {
            /* CL = 8 * address<1:0> (D15.3.1, p. D15-2592). */
            EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rn));
            EmitAndRegImm32(cursor, kEcx, 3u);
            EmitShlReg32Imm(cursor, kEcx, 3);
            /* ROR EDX, CL - D3 /1 (SDM Vol. 2B 4-533 RCL/RCR/ROL/ROR). */
            Emit8(cursor, 0xD3);
            EmitModRmReg(cursor, 3, kEdx, 1);
        }

        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rm));
        /* MOV [EAX], ECX - 89 /r (SDM Vol. 2B 4-35 MOV). */
        Emit8(cursor, 0x89);
        EmitModRmReg(cursor, 0, kEax, kEcx);
    }

    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdx);
    uint8_t* done_label = EmitJmpLabel32(cursor);

    /* A8.8.229 (p. A8-723): "data = ZeroExtend(MemA[R[n], size],32)" precedes the store. */
    if (align_fault_label != nullptr) {
        FixupLabel32(align_fault_label, cursor);
        EmitMovRegImm32(cursor, kEdx,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mmu)));
        EmitCall(cursor,
                 reinterpret_cast<void*>(&ArmMmu::AlignmentFaultReadHelper));
    }

    FixupLabel32(abort_label, cursor);
    cursor = EmitIoIrqPreciseBackoutIfIo(cursor, d, ctx);
    cursor = EmitAbortDataTail(cursor, d, ctx);

    FixupLabel32(done_label, cursor);
    return cursor;
}

}
