#include <cstddef>
#include <cstdint>

#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include "../arm_routed_access.h"
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

namespace {

/* Shift(R[m], shift_t, shift_n, APSR.C) with DecodeImmShift (DDI 0406C.c
   A8.4, p. A8-291: LSR/ASR imm5 == 0 encode a shift by 32, ROR imm5 == 0
   encodes RRX). */
void EmitShiftedOffsetIntoEax(uint8_t*& cursor, DecodedInsn* d) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rm));
    switch (d->op1) {
    case 0u:
        if (d->rs != 0u) {
            EmitShlReg32Imm(cursor, kEax, static_cast<uint8_t>(d->rs));
        }
        break;
    case 1u:
        if (d->rs != 0u) {
            EmitShrReg32Imm(cursor, kEax, static_cast<uint8_t>(d->rs));
        } else {
            EmitMovRegImm32(cursor, kEax, 0u);
        }
        break;
    case 2u:
        EmitSarReg32Imm(cursor, kEax,
                        static_cast<uint8_t>(d->rs != 0u ? d->rs : 31u));
        break;
    default:
        if (d->rs != 0u) {
            EmitRorReg32Imm(cursor, kEax, static_cast<uint8_t>(d->rs));
        } else {
            /* RRX_C (DDI 0406C.c A2.2.1, p. A2-43): result = carry_in :
               x<31:1>. */
            EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 1u);
            EmitCmc(cursor);
            EmitRcrReg32By1(cursor, kEax);
        }
        break;
    }
}

/* offset_addr = R[n] +/- offset (A8.8.63 / A8.8.66 encoding-specific
   operations). */
void EmitOffsetAddrIntoEcx(uint8_t*& cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    if (d->n) {
        if (d->rn == 15u) {
            /* A8.8.64 LDR (literal) Operation (p. A8-411): "base =
               Align(PC,4); address = if add then (base + imm32) else (base -
               imm32)". */
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
        return;
    }
    EmitShiftedOffsetIntoEax(cursor, d);
    if (d->rn == 15u) {
        EmitMovRegImm32(cursor, kEcx, ArmPcReadValue(d, ctx));
    } else {
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rn));
    }
    if (d->u) {
        EmitAddReg32Reg32(cursor, kEcx, kEax);
    } else {
        EmitSubReg32Reg32(cursor, kEcx, kEax);
    }
}

void EmitWritebackRecomputed(uint8_t*& cursor, DecodedInsn* d,
                             BlockContext* ctx) {
    using namespace x86;
    EmitOffsetAddrIntoEcx(cursor, d, ctx);
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rn), kEcx);
}

/* R[n] = offset_addr (A8.8.63 operation order: after the access, before
   the Rt write). */
void EmitWritebackTail(uint8_t*& cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    if (d->p) {
        EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rn), kEcx);
        return;
    }
    EmitWritebackRecomputed(cursor, d, ctx);
}

}

uint8_t* PlaceSingleDataTransfer(uint8_t*      cursor,
                                 DecodedInsn*  d,
                                 BlockContext* ctx) {
    using namespace x86;
    const cerfengine::ArmFeatures* config = ctx->emit->ProcessorConfig();
    ArmMmu*                   mmu    = ctx->emit->Mmu();
    const ArmSctlr            sctlr  = mmu->State()->effective_control_register;
    const uint32_t            mmu_imm =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mmu));

    const bool imm_form = d->n != 0;
    const bool is_byte  = d->s != 0;
    const bool load     = d->l != 0;
    const bool wback    = !d->p || d->w;

    const bool unpriv = d->unpriv != 0;

    bool unpredictable = false;
    if (unpriv && load && d->rd == 15u) {
        unpredictable = true;
    }
    if (is_byte && d->rd == 15u) {
        unpredictable = true;
    }
    if (wback && (d->rn == 15u || d->rn == d->rd)) {
        unpredictable = true;
    }
    if (!imm_form) {
        if (d->rm == 15u) {
            unpredictable = true;
        }
        if (wback && !config->arch_v6 && !config->arch_v7 &&
            d->rm == d->rn) {
            unpredictable = true;
        }
    }
    if (unpredictable) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    if (d->p) {
        EmitOffsetAddrIntoEcx(cursor, d, ctx);
    } else {
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rn));
    }

    const bool legacy = !mmu->UnalignedAccessesFault();

    uint8_t* align_fault_label = nullptr;
    uint8_t* legacy_label      = nullptr;
    uint8_t* cross_label       = nullptr;
    uint8_t* pc_unaligned_label = nullptr;
    if (!is_byte) {
        if (sctlr.bits.a) {
            EmitTestRegImm32(cursor, kEcx, 3u);
            align_fault_label = EmitJnzLabel32(cursor);
        } else if (legacy) {
            EmitTestRegImm32(cursor, kEcx, 3u);
            legacy_label = EmitJnzLabel32(cursor);
        } else if (load && d->rd == 15u) {
            EmitTestRegImm32(cursor, kEcx, 3u);
            pc_unaligned_label = EmitJnzLabel32(cursor);
        } else {
            /* An unaligned word can span two mappings at a 1 KB boundary -
               Tiny pages map 1 KB (Table D15-10, p. D15-2609; A3.2.3,
               p. A3-109). */
            EmitMovRegReg  (cursor, kEdx, kEcx);
            EmitAndRegImm32(cursor, kEdx, 0x3FFu);
            EmitAddRegImm32(cursor, kEdx, 3u);
            EmitTestRegImm32(cursor, kEdx, 0x400u);
            cross_label = EmitJnzLabel32(cursor);
        }
    }

    uint8_t* abort_labels[3];
    int      n_abort = 0;
    uint8_t* fault_labels[2];
    int      n_fault = 0;
    uint8_t* io_fault_labels[2];
    int      n_io_fault = 0;
    uint8_t* done_labels[3];
    int      n_done = 0;

    cursor = EmitTranslateAccess(cursor, ctx,
                                 load ? TlbAccess::kRead : TlbAccess::kWrite,
                                 unpriv);
    EmitTestRegReg(cursor, kEax, kEax);
    abort_labels[n_abort++] = EmitJzLabel32(cursor);

    if (load) {
        if (is_byte) {
            /* MOVZX EDX, byte [EAX] - 0F B6 /r (SDM Vol. 2B 4-140 MOVZX). */
            Emit8(cursor, 0x0F);
            Emit8(cursor, 0xB6);
            EmitModRmReg(cursor, 0, kEax, kEdx);
        } else {
            /* MOV EDX, [EAX] - 8B /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x8B);
            EmitModRmReg(cursor, 0, kEax, kEdx);
        }
        if (wback) {
            EmitWritebackTail(cursor, d, ctx);
        }
        if (d->rd == 15u) {
            EmitMovRegReg(cursor, kEax, kEdx);
            cursor = EmitLoadedPcWrite(cursor, d, ctx);
        } else {
            EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdx);
            done_labels[n_done++] = EmitJmpLabel32(cursor);
        }
    } else {
        if (d->rd == 15u) {
            /* PCStoreValue (A2.3.2, p. A2-47): the stored value is the
               instruction address plus 8 or plus 12 per the core. */
            EmitMovRegImm32(cursor, kEdx,
                d->guest_address + config->pc_store_offset);
        } else {
            EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, ArmGprDisp(d->rd));
        }
        if (is_byte) {
            /* MOV [EAX], DL - 88 /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x88);
            EmitModRmReg(cursor, 0, kEax, kEdx);
        } else {
            /* MOV [EAX], EDX - 89 /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x89);
            EmitModRmReg(cursor, 0, kEax, kEdx);
        }
        if (wback) {
            EmitWritebackTail(cursor, d, ctx);
        }
        done_labels[n_done++] = EmitJmpLabel32(cursor);
    }

    /* D15.3.1 (p. D15-2592). */
    if (legacy_label != nullptr) {
        FixupLabel32(legacy_label, cursor);
        if (load && d->rd == 15u) {
            cursor = EmitRaiseUndTail(cursor, d, ctx);
        } else if (load) {
            EmitMovRegReg  (cursor, kEdx, kEcx);
            EmitAndRegImm32(cursor, kEcx, 0xFFFFFFFCu);
            EmitPushReg(cursor, kEdx);
            cursor = EmitTranslateAccess(cursor, ctx, TlbAccess::kRead, unpriv);
            EmitPopReg(cursor, kEdx);
            EmitTestRegReg(cursor, kEax, kEax);
            io_fault_labels[n_io_fault++] = EmitJzLabel32(cursor);
            /* MOV EAX, [EAX] - 8B /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x8B);
            EmitModRmReg(cursor, 0, kEax, kEax);
            EmitMovRegReg  (cursor, kEcx, kEdx);
            EmitAndRegImm32(cursor, kEcx, 3u);
            EmitShlReg32Imm(cursor, kEcx, 3u);
            /* ROR EAX, CL - D3 /1 (SDM Vol. 2B 4-533 RCL/RCR/ROL/ROR). */
            EmitRorReg32Cl(cursor, kEax);
            EmitMovRegReg(cursor, kEdx, kEax);
            if (wback) {
                EmitWritebackRecomputed(cursor, d, ctx);
            }
            EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdx);
            done_labels[n_done++] = EmitJmpLabel32(cursor);
        } else {
            EmitAndRegImm32(cursor, kEcx, 0xFFFFFFFCu);
            cursor = EmitTranslateAccess(cursor, ctx, TlbAccess::kWrite, unpriv);
            EmitTestRegReg(cursor, kEax, kEax);
            abort_labels[n_abort++] = EmitJzLabel32(cursor);
            if (d->rd == 15u) {
                EmitMovRegImm32(cursor, kEdx,
                    d->guest_address + config->pc_store_offset);
            } else {
                EmitMovRegBaseDisp32(cursor, kEdx, kStateReg,
                                     ArmGprDisp(d->rd));
            }
            /* MOV [EAX], EDX - 89 /r (SDM Vol. 2B 4-35 MOV). */
            Emit8(cursor, 0x89);
            EmitModRmReg(cursor, 0, kEax, kEdx);
            if (wback) {
                EmitWritebackRecomputed(cursor, d, ctx);
            }
            done_labels[n_done++] = EmitJmpLabel32(cursor);
        }
    }

    if (pc_unaligned_label != nullptr) {
        FixupLabel32(pc_unaligned_label, cursor);
        cursor = EmitRaiseUndTail(cursor, d, ctx);
    }

    if (cross_label != nullptr) {
        FixupLabel32(cross_label, cursor);
        if (load) {
            EmitPush32 (cursor, unpriv ? 1u : 0u);
            EmitPushReg(cursor, kEcx);
            EmitPush32 (cursor, mmu_imm);
            EmitCall(cursor, reinterpret_cast<void*>(
                &ArmMmu::UnalignedWordLoadHelper));
            EmitAddRegImm32(cursor, kEsp, 12u);
            EmitTestRegReg(cursor, kEdx, kEdx);
            io_fault_labels[n_io_fault++] = EmitJzLabel32(cursor);
            EmitMovRegReg(cursor, kEdx, kEax);
            if (wback) {
                EmitWritebackRecomputed(cursor, d, ctx);
            }
            EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdx);
            done_labels[n_done++] = EmitJmpLabel32(cursor);
        } else {
            if (d->rd == 15u) {
                EmitMovRegImm32(cursor, kEdx,
                    d->guest_address + config->pc_store_offset);
            } else {
                EmitMovRegBaseDisp32(cursor, kEdx, kStateReg,
                                     ArmGprDisp(d->rd));
            }
            EmitPush32 (cursor, unpriv ? 1u : 0u);
            EmitPushReg(cursor, kEdx);
            EmitPushReg(cursor, kEcx);
            EmitPush32 (cursor, mmu_imm);
            EmitCall(cursor, reinterpret_cast<void*>(
                &ArmMmu::UnalignedWordStoreHelper));
            EmitAddRegImm32(cursor, kEsp, 16u);
            EmitCmpRegImm32(cursor, kEax, 0xFFFFFFFFu);
            fault_labels[n_fault++] = EmitJzLabel32(cursor);
            if (wback) {
                EmitWritebackRecomputed(cursor, d, ctx);
            }
            done_labels[n_done++] = EmitJmpLabel32(cursor);
        }
    }

    const bool base_updated_abort =
        wback && !config->base_restored_abort_model;
    uint8_t* align_done_label = nullptr;
    if (align_fault_label != nullptr) {
        FixupLabel32(align_fault_label, cursor);
        EmitMovRegImm32(cursor, kEdx, mmu_imm);
        EmitCall(cursor, load
            ? reinterpret_cast<void*>(&ArmMmu::AlignmentFaultReadHelper)
            : reinterpret_cast<void*>(&ArmMmu::AlignmentFaultWriteHelper));
        if (base_updated_abort) {
            EmitWritebackRecomputed(cursor, d, ctx);
            align_done_label = EmitJmpLabel32(cursor);
        }
    }

    for (int i = 0; i < n_abort; ++i) {
        FixupLabel32(abort_labels[i], cursor);
    }
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
    if (load) {
        EmitPushReg(cursor, kEcx);
        EmitPush32 (cursor, d->guest_address);
        EmitPush32 (cursor, routed_imm);
        EmitCall(cursor, is_byte
            ? reinterpret_cast<void*>(&ArmRoutedAccess::IoLoadHelper<MmioWidth::kByte>)
            : reinterpret_cast<void*>(&ArmRoutedAccess::IoLoadHelper<MmioWidth::kWord>));
        EmitAddRegImm32(cursor, kEsp, 12u);
        EmitMovRegReg(cursor, kEdx, kEax);
        if (wback) {
            EmitWritebackRecomputed(cursor, d, ctx);
        }
        if (d->rd == 15u) {
            EmitMovRegReg(cursor, kEax, kEdx);
            cursor = EmitLoadedPcWrite(cursor, d, ctx);
        } else {
            EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEdx);
            cursor = EmitStopAfterIoAccess(cursor, d, ctx);
            done_labels[n_done++] = EmitJmpLabel32(cursor);
        }
    } else {
        if (d->rd == 15u) {
            EmitMovRegImm32(cursor, kEdx,
                d->guest_address + config->pc_store_offset);
        } else {
            EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, ArmGprDisp(d->rd));
        }
        EmitPushReg(cursor, kEdx);
        EmitPushReg(cursor, kEcx);
        EmitPush32 (cursor, d->guest_address);
        EmitPush32 (cursor, routed_imm);
        EmitCall(cursor, is_byte
            ? reinterpret_cast<void*>(&ArmRoutedAccess::IoStoreHelper<MmioWidth::kByte>)
            : reinterpret_cast<void*>(&ArmRoutedAccess::IoStoreHelper<MmioWidth::kWord>));
        EmitAddRegImm32(cursor, kEsp, 16u);
        if (wback) {
            EmitWritebackRecomputed(cursor, d, ctx);
        }
        cursor = EmitStopAfterIoAccess(cursor, d, ctx);
        done_labels[n_done++] = EmitJmpLabel32(cursor);
    }

    if (n_io_fault > 0) {
        for (int i = 0; i < n_io_fault; ++i) {
            FixupLabel32(io_fault_labels[i], cursor);
        }
        cursor = EmitIoIrqPreciseBackoutIfIo(cursor, d, ctx);
    }

    FixupLabel32(real_fault_label, cursor);
    for (int i = 0; i < n_fault; ++i) {
        FixupLabel32(fault_labels[i], cursor);
    }
    if (base_updated_abort) {
        /* ARM DDI 0406C.c D15.5.2 (p. D15-2604) Base Updated Abort Model
           applies to an instruction "that causes a memory system abort". */
        /* 8B /r mod=00 disp32 (SDM Vol. 2B 4-35 MOV). */
        Emit8(cursor, 0x8B);
        EmitModRmDisp32(cursor, kEax);
        Emit32(cursor, static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(mmu->IoPendingValidPtr())));
        EmitTestRegReg(cursor, kEax, kEax);
        uint8_t* io_skip = EmitJnzLabel32(cursor);
        EmitWritebackRecomputed(cursor, d, ctx);
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
