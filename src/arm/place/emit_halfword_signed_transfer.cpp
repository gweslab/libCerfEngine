#include <cstddef>
#include <cstdint>

#include <cerfengine/engine.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include "../arm_routed_access.h"
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

namespace {

/* A8.8.82 LDRH (register) encoding T2 (p. A8-446): "(shift_t, shift_n) =
   (SRType_LSL, UInt(imm2))"; T1 and A1 give "(SRType_LSL, 0)". */
void EmitShiftedRmInto(uint8_t*& cursor, DecodedInsn* d, uint8_t reg) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, reg, kStateReg, ArmGprDisp(d->rm));
    if (d->rs != 0u) {
        EmitShlReg32Imm(cursor, reg, static_cast<uint8_t>(d->rs));
    }
}

/* offset_addr = R[n] +/- imm/R[m] (A8.8.80 encoding-specific operations). */
void EmitOffsetAddr(uint8_t*& cursor, DecodedInsn* d) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rn));
    if (d->n) {
        if (d->offset != 0) {
            EmitAddRegImm32(cursor, kEcx, static_cast<uint32_t>(d->offset));
        }
    } else {
        EmitShiftedRmInto(cursor, d, kEax);
        if (d->u) {
            EmitAddReg32Reg32(cursor, kEcx, kEax);
        } else {
            EmitSubReg32Reg32(cursor, kEcx, kEax);
        }
    }
}

void EmitWritebackRecomputed(uint8_t*& cursor, DecodedInsn* d) {
    using namespace x86;
    EmitOffsetAddr(cursor, d);
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rn), kEcx);
}

/* R[n] = offset_addr (A8.8.80/A8.8.217 operation order). */
void EmitWritebackTail(uint8_t*& cursor, DecodedInsn* d) {
    using namespace x86;
    if (d->p) {
        EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rn), kEcx);
        return;
    }
    EmitWritebackRecomputed(cursor, d);
}

}

uint8_t* EmitHalfwordSignedTransfer(uint8_t*      cursor,
                                    DecodedInsn*  d,
                                    BlockContext* ctx) {
    using namespace x86;
    const cerfengine::ArmFeatures* config = ctx->emit->ProcessorConfig();
    ArmMmu*                   mmu    = ctx->emit->Mmu();
    const ArmSctlr            sctlr  = mmu->State()->effective_control_register;
    const uint32_t            mmu_imm =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mmu));

    const bool imm_form = d->n != 0;
    const bool wback    = !d->p || d->w;
    const bool is_dual  = !d->l && (d->op1 == 2 || d->op1 == 3);

    /* LDRHT/STRHT/LDRSBT/LDRSHT are ARMv6T2 encodings (DDI 0406C.c A8.8.83
       p. A8-448); before v6T2, DDI 0100I A5.3 (p. A5-34): "P == 0 The W bit
       must be 0 or the instruction is UNPREDICTABLE". */
    const bool unpriv = d->unpriv != 0;
    if (unpriv && !config->movw_movt) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    /* Dual transfers are ARMv5TE (Table A5-10, p. A5-204); UNPREDICTABLE
       may be implemented as UNDEFINED (p. Glossary-2737). */
    if (is_dual && !config->load_store_double) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    bool unpredictable = false;
    if (is_dual) {
        /* A8.8.72 / A8.8.210 A1 (pp. A8-426 / A8-686): "if Rt<0> == '1' then
           UNPREDICTABLE", "if t2 == 15 then UNPREDICTABLE", "if P == '0' &&
           W == '1' then UNPREDICTABLE"; T1 gives "t2 = UInt(Rt2)" instead. */
        if (!ctx->thumb) {
            unpredictable |= (d->rd & 1u) != 0 || d->rd == 14;
            unpredictable |= !d->p && d->w;
        }
        if (wback) {
            unpredictable |= d->rn == 15 || d->rn == d->rd ||
                             d->rn == d->rd2;
        }
        if (!imm_form) {
            unpredictable |= d->rm == 15 || d->rm == d->rd ||
                             d->rm == d->rd2;
        }
    } else {
        /* A8.8.80/82/84/86/88/90/217/218: t == 15, register form m == 15,
           wback && (n == 15 || n == t). */
        unpredictable |= d->rd == 15;
        if (wback) {
            unpredictable |= d->rn == 15 || d->rn == d->rd;
        }
        if (!imm_form) {
            unpredictable |= d->rm == 15;
        }
    }
    if (!imm_form && wback && !config->arch_v6 && !config->arch_v7) {
        unpredictable |= d->rm == d->rn;
    }
    if (unpredictable) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    if (d->p) {
        if (imm_form) {
            if (d->rn == 15) {
                /* A8.8.81 LDRH (literal) Operation (p. A8-445): "base =
                   Align(PC,4); address = if add then (base + imm32) else
                   (base - imm32)". */
                EmitMovRegImm32(cursor, kEcx,
                    (ArmPcReadValue(d, ctx) & ~3u) +
                        static_cast<uint32_t>(d->offset));
            } else {
                EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rn));
                if (d->offset != 0) {
                    EmitAddRegImm32(cursor, kEcx,
                                    static_cast<uint32_t>(d->offset));
                }
            }
        } else {
            if (d->rn == 15) {
                EmitMovRegImm32(cursor, kEcx, ArmPcReadValue(d, ctx));
            } else {
                EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rn));
            }
            EmitShiftedRmInto(cursor, d, kEdx);
            if (d->u) {
                EmitAddReg32Reg32(cursor, kEcx, kEdx);
            } else {
                EmitSubReg32Reg32(cursor, kEcx, kEdx);
            }
        }
    } else {
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rn));
    }

    const bool is_store    = !d->l && (d->op1 == 1 || d->op1 == 3);
    const bool is_halfword = d->op1 == 1 || (d->op1 == 3 && d->l);
    const TlbAccess access = is_store ? TlbAccess::kWrite : TlbAccess::kRead;
    const bool base_updated_abort =
        wback && !config->base_restored_abort_model;

    uint8_t* align_fault_label = nullptr;
    uint8_t* cross_label       = nullptr;

    if (is_halfword) {
        EmitHalfwordAlignCheck(cursor, sctlr.bits.a != 0u,
                               &align_fault_label, &cross_label);
    } else if (is_dual) {
        const uint32_t mask = mmu->DoublewordAlignMask();
        EmitTestRegImm32(cursor, kEcx, mask);
        align_fault_label = EmitJnzLabel32(cursor);
    }

    uint8_t* abort_labels[3];
    int      n_abort = 0;
    uint8_t* fault_labels[3];
    int      n_fault = 0;
    uint8_t* io_fault_labels[3];
    int      n_io_fault = 0;

    cursor = EmitTranslateAccess(cursor, ctx, access, unpriv);
    EmitTestRegReg(cursor, kEax, kEax);
    if (is_dual) {
        io_fault_labels[n_io_fault++] = EmitJzLabel32(cursor);
    } else {
        abort_labels[n_abort++] = EmitJzLabel32(cursor);
    }

    switch ((d->op1 << 1) | (d->l ? 1u : 0u)) {
        case (1u << 1) | 1u:
            /* MOVZX EDX, word [EAX] - 0F B7 /r (SDM Vol. 2B 4-140 MOVZX). */
            Emit8(cursor, 0x0F);
            Emit8(cursor, 0xB7);
            EmitModRmReg(cursor, 0, kEax, kEdx);
            if (wback) EmitWritebackTail(cursor, d);
            EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdx);
            break;

        case (2u << 1) | 1u:
            /* MOVSX EDX, byte [EAX] - 0F BE /r (SDM Vol. 2B 4-130 MOVSX). */
            Emit8(cursor, 0x0F);
            Emit8(cursor, 0xBE);
            EmitModRmReg(cursor, 0, kEax, kEdx);
            if (wback) EmitWritebackTail(cursor, d);
            EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdx);
            break;

        case (3u << 1) | 1u:
            /* MOVSX EDX, word [EAX] - 0F BF /r (SDM Vol. 2B 4-130 MOVSX). */
            Emit8(cursor, 0x0F);
            Emit8(cursor, 0xBF);
            EmitModRmReg(cursor, 0, kEax, kEdx);
            if (wback) EmitWritebackTail(cursor, d);
            EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdx);
            break;

        case (1u << 1) | 0u:
            EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, ArmGprDisp(d->rd));
            /* MOV [EAX], DX - 66 89 /r: MOV r/m16, r16 (SDM Vol. 2B 4-35
               MOV) with the operand-size override prefix (SDM Vol. 2A
               §2.1.1, p. 2-2). */
            Emit8(cursor, 0x66);
            Emit8(cursor, 0x89);
            EmitModRmReg(cursor, 0, kEax, kEdx);
            if (wback) EmitWritebackTail(cursor, d);
            break;

        case (2u << 1) | 0u:
            /* MOV EDI, [EAX] - 8B /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x8B);
            EmitModRmReg(cursor, 0, kEax, kEdi);
            EmitAddRegImm32(cursor, kEcx, 4u);
            cursor = EmitTlbFastPath(cursor, ctx, TlbAccess::kRead);
            EmitTestRegReg(cursor, kEax, kEax);
            io_fault_labels[n_io_fault++] = EmitJzLabel32(cursor);
            Emit8(cursor, 0x8B);
            EmitModRmReg(cursor, 0, kEax, kEdx);
            EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdi);
            EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd2), kEdx);
            if (wback) {
                if (d->p) {
                    EmitAddRegImm32(cursor, kEcx, static_cast<uint32_t>(-4));
                }
                EmitWritebackTail(cursor, d);
            }
            break;

        case (3u << 1) | 0u:
            EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, ArmGprDisp(d->rd));
            /* MOV [EAX], EDX - 89 /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x89);
            EmitModRmReg(cursor, 0, kEax, kEdx);
            EmitAddRegImm32(cursor, kEcx, 4u);
            cursor = EmitTlbFastPath(cursor, ctx, TlbAccess::kWrite);
            EmitTestRegReg(cursor, kEax, kEax);
            fault_labels[n_fault++] = EmitJzLabel32(cursor);
            EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, ArmGprDisp(d->rd2));
            Emit8(cursor, 0x89);
            EmitModRmReg(cursor, 0, kEax, kEdx);
            if (wback) {
                if (d->p) {
                    EmitAddRegImm32(cursor, kEcx, static_cast<uint32_t>(-4));
                }
                EmitWritebackTail(cursor, d);
            }
            break;

        default:
            ctx->emit->Owner().Panic(
                "EmitHalfwordSignedTransfer got op1=%u L=%u outside the A5.2.8 op2 "
                "space at guest pc=0x%08X", d->op1, d->l, d->guest_address);
    }

    uint8_t* done_labels[3];
    int      n_done = 0;
    done_labels[n_done++] = EmitJmpLabel32(cursor);

    if (cross_label != nullptr) {
        FixupLabel32(cross_label, cursor);
        if (is_store) {
            EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, ArmGprDisp(d->rd));
            EmitPush32 (cursor, unpriv ? 1u : 0u);
            EmitPushReg(cursor, kEdx);
            EmitPushReg(cursor, kEcx);
            EmitPush32 (cursor, mmu_imm);
            EmitCall(cursor, reinterpret_cast<void*>(
                &ArmMmu::UnalignedHalfwordStoreHelper));
            EmitAddRegImm32(cursor, kEsp, 16u);
        } else {
            EmitPush32 (cursor, unpriv ? 1u : 0u);
            EmitPushReg(cursor, kEcx);
            EmitPush32 (cursor, mmu_imm);
            EmitCall(cursor, reinterpret_cast<void*>(
                &ArmMmu::UnalignedHalfwordLoadHelper));
            EmitAddRegImm32(cursor, kEsp, 12u);
        }
        EmitCmpRegImm32(cursor, kEax, 0xFFFFFFFFu);
        uint8_t* unaligned_fault = EmitJzLabel32(cursor);
        if (is_store) fault_labels[n_fault++]       = unaligned_fault;
        else          io_fault_labels[n_io_fault++] = unaligned_fault;
        if (!is_store) {
            if (d->op1 == 3) {
                EmitMovsxReg32Reg16(cursor, kEdx, kEax);
            } else {
                EmitMovRegReg(cursor, kEdx, kEax);
            }
        }
        if (wback) EmitWritebackRecomputed(cursor, d);
        if (!is_store) {
            EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdx);
        }
        done_labels[n_done++] = EmitJmpLabel32(cursor);
    }

    uint8_t* align_done_label = nullptr;
    if (align_fault_label != nullptr) {
        FixupLabel32(align_fault_label, cursor);
        EmitMovRegImm32(cursor, kEdx, mmu_imm);
        EmitCall(cursor, is_store
            ? reinterpret_cast<void*>(&ArmMmu::AlignmentFaultWriteHelper)
            : reinterpret_cast<void*>(&ArmMmu::AlignmentFaultReadHelper));
        if (base_updated_abort) {
            EmitWritebackRecomputed(cursor, d);
            align_done_label = EmitJmpLabel32(cursor);
        }
    }

    for (int i = 0; i < n_abort; ++i) {
        FixupLabel32(abort_labels[i], cursor);
    }
    if (n_abort != 0) {
        /* 8B /r mod=00 disp32 (SDM Vol. 2B 4-35 MOV). */
        Emit8(cursor, 0x8B);
        EmitModRmDisp32(cursor, kEax);
        Emit32(cursor, static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(mmu->IoPendingValidPtr())));
        EmitTestRegReg(cursor, kEax, kEax);
        uint8_t* real_fault_label = EmitJzLabel32(cursor);

        cursor = EmitIoIrqPreciseBackout(cursor, d, ctx);

        const uint32_t routed_imm = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(ctx->emit->RoutedAccess()));
        const bool io_byte = d->op1 == 2u;
        if (is_store) {
            EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, ArmGprDisp(d->rd));
            EmitPushReg(cursor, kEdx);
            EmitPushReg(cursor, kEcx);
            EmitPush32 (cursor, d->guest_address);
            EmitPush32 (cursor, routed_imm);
            EmitCall(cursor, io_byte
                ? reinterpret_cast<void*>(&ArmRoutedAccess::IoStoreHelper<MmioWidth::kByte>)
                : reinterpret_cast<void*>(&ArmRoutedAccess::IoStoreHelper<MmioWidth::kHalf>));
            EmitAddRegImm32(cursor, kEsp, 16u);
        } else {
            EmitPushReg(cursor, kEcx);
            EmitPush32 (cursor, d->guest_address);
            EmitPush32 (cursor, routed_imm);
            EmitCall(cursor, io_byte
                ? reinterpret_cast<void*>(&ArmRoutedAccess::IoLoadHelper<MmioWidth::kByte>)
                : reinterpret_cast<void*>(&ArmRoutedAccess::IoLoadHelper<MmioWidth::kHalf>));
            EmitAddRegImm32(cursor, kEsp, 12u);
            /* A8.8.84 LDRSB (immediate) (p. A8-451): R[t] =
               SignExtend(MemU[address,1], 32). A8.8.88 LDRSH (immediate)
               (p. A8-459): R[t] = SignExtend(data, 32). */
            if (d->op1 == 2u) {
                EmitMovsxReg32Reg8(cursor, kEdx, kEax);
            } else if (d->op1 == 3u) {
                EmitMovsxReg32Reg16(cursor, kEdx, kEax);
            } else {
                EmitMovRegReg(cursor, kEdx, kEax);
            }
        }
        if (wback) {
            EmitWritebackRecomputed(cursor, d);
        }
        if (!is_store) {
            EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdx);
        }
        cursor = EmitStopAfterIoAccess(cursor, d, ctx);
        done_labels[n_done++] = EmitJmpLabel32(cursor);

        FixupLabel32(real_fault_label, cursor);
    }
    if (n_io_fault > 0) {
        for (int i = 0; i < n_io_fault; ++i) {
            FixupLabel32(io_fault_labels[i], cursor);
        }
        cursor = EmitIoIrqPreciseBackoutIfIo(cursor, d, ctx);
    }

    for (int i = 0; i < n_fault; ++i) {
        FixupLabel32(fault_labels[i], cursor);
    }
    if (base_updated_abort) {
        /* ARM DDI 0406C.c D15.5.2 (p. D15-2604) Base Updated Abort Model. */
        /* 8B /r mod=00 disp32 (SDM Vol. 2B 4-35 MOV). */
        Emit8(cursor, 0x8B);
        EmitModRmDisp32(cursor, kEax);
        Emit32(cursor, static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(mmu->IoPendingValidPtr())));
        EmitTestRegReg(cursor, kEax, kEax);
        uint8_t* io_skip = EmitJnzLabel32(cursor);
        EmitWritebackRecomputed(cursor, d);
        FixupLabel32(io_skip, cursor);
    }
    if (align_done_label != nullptr) {
        FixupLabel32(align_done_label, cursor);
    }
    cursor = EmitAbortDataTail(cursor, d, ctx);

    for (int i = 0; i < n_done; ++i) {
        FixupLabel32(done_labels[i], cursor);
    }
    return cursor;
}

}
