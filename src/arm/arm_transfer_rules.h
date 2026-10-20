#pragma once

#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/engine_setup.h>

#include "../cerfir/cerfir_block.h"
#include <cerfengine/force_inline.h>
#include "arm_emit_services.h"

namespace cerfengine {

CERFENGINE_FORCEINLINE bool ArmTransferAlignmentChecked(const BlockContext* ctx) {
    return ctx->alignment_checked;
}

CERFENGINE_FORCEINLINE bool ArmTransferUnalignedFaults(const BlockContext* ctx) {
    return ctx->unaligned_faults;
}

CERFENGINE_FORCEINLINE bool ArmExtensionTransferIsDual(const DecodedInsn& d) {
    return d.l == 0u && (d.op1 == 2u || d.op1 == 3u);
}

/* LDRHT/STRHT/LDRSBT/LDRSHT are ARMv6T2 encodings (DDI 0406C.c A8.8.83
   p. A8-448); before v6T2, DDI 0100I A5.3 (p. A5-34): "P == 0 The W bit
   must be 0 or the instruction is UNPREDICTABLE". */
CERFENGINE_FORCEINLINE bool ArmExtensionUnprivilegedUndefined(const DecodedInsn& d,
                                                              const ArmFeatures& f) {
    return d.unpriv != 0u && !f.movw_movt;
}

/* Dual transfers are ARMv5TE (Table A5-10, p. A5-204); UNPREDICTABLE
   may be implemented as UNDEFINED (p. Glossary-2737). */
CERFENGINE_FORCEINLINE bool ArmExtensionDualUndefined(const DecodedInsn& d,
                                                      const ArmFeatures& f) {
    return ArmExtensionTransferIsDual(d) && !f.load_store_double;
}

/* ARM DDI 0406C.c A8.8.80/A8.8.82 LDRH (pp. A8-442/A8-446), A8.8.72/A8.8.74 LDRD
   (pp. A8-426/A8-430), A8.8.210/A8.8.211 STRD (pp. A8-686/A8-688): the
   UNPREDICTABLE cases of the encodings. */
CERFENGINE_FORCEINLINE bool ArmExtensionUnpredictable(const DecodedInsn& d, const ArmFeatures& f,
                                                      bool thumb) {
    const bool imm_form = d.n != 0u;
    const bool wback    = d.p == 0u || d.w != 0u;
    bool       u        = false;
    if (ArmExtensionTransferIsDual(d)) {
        if (!thumb) {
            u = u || (d.rd & 1u) != 0u || d.rd == ArmGpr::kR14;
            u = u || (d.p == 0u && d.w != 0u);
        }
        if (wback) u = u || d.rn == ArmGpr::kR15 || d.rn == d.rd || d.rn == d.rd2;
        if (!imm_form) {
            u = u || d.rm == ArmGpr::kR15;
            if (d.op1 == 2u) u = u || d.rm == d.rd || d.rm == d.rd2;
        }
    } else {
        u = u || d.rd == ArmGpr::kR15;
        if (wback) u = u || d.rn == ArmGpr::kR15 || d.rn == d.rd;
        if (!imm_form) u = u || d.rm == ArmGpr::kR15;
    }
    if (!imm_form && wback && !f.arch_v6 && !f.arch_v7) u = u || d.rm == d.rn;
    return u;
}

/* ARM DDI 0406C.c A8.8.63/A8.8.64/A8.8.66 LDR, A8.8.68/A8.8.70 LDRB, A8.8.71
   LDRBT, A8.8.92 LDRT, A8.8.204/A8.8.205 STR, A8.8.207/A8.8.208 STRB, A8.8.209
   STRBT, A8.8.220 STRT: the UNPREDICTABLE cases of the ARM encodings. */
CERFENGINE_FORCEINLINE bool ArmSingleTransferUnpredictable(const DecodedInsn& d,
                                                           const ArmFeatures& f) {
    const bool load  = d.l != 0u;
    const bool wback = d.p == 0u || d.w != 0u;
    if (d.unpriv != 0u && load && d.rd == ArmGpr::kR15) return true;
    if (d.s != 0u && d.rd == ArmGpr::kR15) return true;
    if (wback && (d.rn == ArmGpr::kR15 || d.rn == d.rd)) return true;
    if (d.n == 0u) {
        if (d.rm == ArmGpr::kR15) return true;
        if (wback && !f.arch_v6 && !f.arch_v7 && d.rm == d.rn) return true;
    }
    return false;
}

CERFENGINE_FORCEINLINE bool ArmExtensionTransferUndefined(const DecodedInsn& d,
                                                          const ArmFeatures& f, bool thumb) {
    return ArmExtensionUnprivilegedUndefined(d, f) || ArmExtensionDualUndefined(d, f) ||
           ArmExtensionUnpredictable(d, f, thumb);
}

/* A8.8.229: t == 15 || t2 == 15 || n == 15 || n == t || n == t2 is
   UNPREDICTABLE -> UNDEFINED (p. Glossary-2737). */
CERFENGINE_FORCEINLINE bool ArmSwapUnpredictable(const DecodedInsn& d) {
    return d.rd == ArmGpr::kR15 || d.rm == ArmGpr::kR15 || d.rn == ArmGpr::kR15 ||
           d.rn == d.rd || d.rn == d.rm;
}

/* ARM DDI 0406C.c A8.8.58 LDM (p. A8-398): "if n == 15 || BitCount(registers)
   < 1 then UNPREDICTABLE; if wback && registers<n> == '1' && ArchVersion() >=
   7 then UNPREDICTABLE"; B9.3.6/B9.3.17 (pp. B9-1988/B9-2008): W is (0). */
CERFENGINE_FORCEINLINE bool ArmBlockTransferUndefined(const DecodedInsn& d, const ArmFeatures& f) {
    const uint32_t list       = d.register_list;
    const bool     load       = d.l != 0u;
    const bool     wback      = d.w != 0u;
    const bool     pc_in_list = (list & 0x8000u) != 0u;
    const bool     user_regs  = d.s != 0u && !(load && pc_in_list);
    const bool     rn_in_list = ((list >> d.rn) & 1u) != 0u;
    return d.rn == ArmGpr::kR15 || list == 0u || (load && wback && rn_in_list && f.arch_v7) ||
           (user_regs && wback);
}

/* D15.5.2 (p. D15-2604) Base Restored Abort Model: the base register of a load/store
   that "causes a memory system abort is always restored to the value it had
   immediately before that instruction". */
CERFENGINE_FORCEINLINE bool ArmBaseUpdatedOnAbort(bool wback, const ArmFeatures& f) {
    return wback && !f.base_restored_abort_model;
}

CERFENGINE_FORCEINLINE cerfir::AlignPolicy ArmSingleTransferAlign(const DecodedInsn& d,
                                                                  const BlockContext* ctx) {
    using cerfir::AlignPolicy;
    if (d.s != 0u) return AlignPolicy::kNone;
    if (ArmTransferAlignmentChecked(ctx)) return AlignPolicy::kFault;
    if (d.l != 0u && d.rd == ArmGpr::kR15) return AlignPolicy::kUndefinedIfUnaligned;
    if (!ArmTransferUnalignedFaults(ctx)) return AlignPolicy::kLegacyRotate;
    return AlignPolicy::kCrossCheck;
}

CERFENGINE_FORCEINLINE cerfir::AlignPolicy ArmExtensionTransferAlign(const DecodedInsn& d,
                                                                     const BlockContext* ctx) {
    using cerfir::AlignPolicy;
    if (ArmExtensionTransferIsDual(d)) return AlignPolicy::kFault;
    const bool halfword = d.op1 == 1u || (d.op1 == 3u && d.l != 0u);
    if (!halfword) return AlignPolicy::kNone;
    return ArmTransferAlignmentChecked(ctx) ? AlignPolicy::kFault : AlignPolicy::kCrossCheck;
}

/* A8.8.236 TBB, TBH Operation (p. A8-737): "MemU[R[n]+LSL(R[m],1), 2]" for TBH,
   "MemU[R[n]+R[m], 1]" for TBB. */
CERFENGINE_FORCEINLINE cerfir::AlignPolicy ArmTableBranchAlign(const DecodedInsn& d,
                                                               const BlockContext* ctx) {
    using cerfir::AlignPolicy;
    if (d.op1 == 0u) return AlignPolicy::kNone;
    return ArmTransferAlignmentChecked(ctx) ? AlignPolicy::kFault : AlignPolicy::kCrossCheck;
}

/* Unaligned SWP faults when U == 1 (Table A3-1, p. A3-108; D12.3.1,
   p. D12-2506) or A == 1 (D15.3.1, p. D15-2592); U by architecture
   version per Table D12-1 (p. D12-2506). */
CERFENGINE_FORCEINLINE cerfir::AlignPolicy ArmSwapAlign(const DecodedInsn& d,
                                                        const BlockContext* ctx) {
    using cerfir::AlignPolicy;
    if (d.n != 0u) return AlignPolicy::kNone;
    return ArmTransferUnalignedFaults(ctx) || ArmTransferAlignmentChecked(ctx)
               ? AlignPolicy::kFault
               : AlignPolicy::kLegacyRotate;
}

/* Table A3-1 (p. A3-108) + D12.3.1 (p. D12-2506): multi-word accesses
   are word-aligned or abort on ARMv7 and ARMv6 U == 1; D15.3.1
   (p. D15-2592): otherwise SCTLR.A == 0 ignores address[1:0]. */
CERFENGINE_FORCEINLINE cerfir::AlignPolicy ArmBlockTransferAlign(const BlockContext* ctx) {
    using cerfir::AlignPolicy;
    return ArmTransferUnalignedFaults(ctx) || ArmTransferAlignmentChecked(ctx)
               ? AlignPolicy::kFault
               : AlignPolicy::kIgnoreLowBits;
}

CERFENGINE_FORCEINLINE void ArmTransferSlowChecks(cerfir::MemAttr& m) {
    using cerfir::AlignPolicy;
    switch (m.align) {
    case AlignPolicy::kFault:
        m.slow_mask = m.width_bytes - 1u;
        return;
    case AlignPolicy::kLegacyRotate:
    case AlignPolicy::kUndefinedIfUnaligned:
        m.slow_mask = 3u;
        return;
    case AlignPolicy::kCrossCheck:
        m.cross_granule = static_cast<uint16_t>(kArmBlockPageBytes);
        return;
    case AlignPolicy::kNone:
    case AlignPolicy::kIgnoreLowBits:
        return;
    }
}

}
