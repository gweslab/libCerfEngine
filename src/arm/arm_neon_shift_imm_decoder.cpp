#include "arm_neon_shift_imm_decoder.h"

#include <cerfengine/engine.h>
#include "arm_neon_sat.h"
#include "arm_neon_shift_imm.h"
#include "arm_opcode.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/block_context.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeonShiftImmDecoder);

bool ArmNeonShiftImmDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    const uint32_t L_   = (op.word >> 7) & 1u;
    const uint32_t imm6 = (op.word >> 16) & 0x3Fu;
    const uint32_t L_imm6 = (L_ << 6) | imm6;
    if ((L_imm6 >> 3) == 0u) {
        /* L:imm6 == 0000xxx → 1-reg-modified-immediate (A7.4.6). */
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->kind      = ArmInsnKind::kNeonOneRegImm;
        return true;
    }

    const uint32_t opc = op.neon_data_3reg.opc;
    if (opc == 0x0u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeonShiftImm::kSiShrU
                              : ArmNeonShiftImm::kSiShrS;
        insn->kind  = ArmInsnKind::kNeonShiftImm;
        return true;
    }
    if (opc == 0x2u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeonShiftImm::kSiRshrU
                              : ArmNeonShiftImm::kSiRshrS;
        insn->kind  = ArmInsnKind::kNeonShiftImm;
        return true;
    }
    /* VSRA: opc=0001, U->S/U (A8.8.402). */
    if (opc == 0x1u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeonShiftImm::kSiSraU
                              : ArmNeonShiftImm::kSiSraS;
        insn->kind  = ArmInsnKind::kNeonShiftImm;
        return true;
    }
    /* VRSRA: opc=0011, U->S/U (A8.8.393). */
    if (opc == 0x3u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeonShiftImm::kSiRsraU
                              : ArmNeonShiftImm::kSiRsraS;
        insn->kind  = ArmInsnKind::kNeonShiftImm;
        return true;
    }
    if (opc == 0x5u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeonShiftImm::kSiSli
                              : ArmNeonShiftImm::kSiShl;
        insn->kind  = ArmInsnKind::kNeonShiftImm;
        return true;
    }
    /* VSRI: opc=0x4, U fixed to 1 (A8.8.403). */
    if (opc == 0x4u && op.neon_data_3reg.u == 1u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeonShiftImm::kSiSri;
        insn->kind      = ArmInsnKind::kNeonShiftImm;
        return true;
    }
    /* VQSHL imm: opc=0x7 (op-bit=1). U=0 -> VQSHL.S, U=1 -> VQSHL.U
       (A8.8.380). */
    if (opc == 0x7u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeonSat::kSatShlImmU
                              : ArmNeonSat::kSatShlImmS;
        insn->kind  = ArmInsnKind::kNeonShiftImmSat;
        return true;
    }
    /* ARM DDI 0406C.c A8.8.380 (p. A8-1016): "if U == '0' && op == '0' then
       UNDEFINED". */
    if (opc == 0x6u && op.neon_data_3reg.u == 1u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeonSat::kSatShlImmSU;
        insn->kind      = ArmInsnKind::kNeonShiftImmSat;
        return true;
    }
    /* VSHRN (A8.8.399) / VRSHRN (A8.8.390): opc=0x8, u=0.
       bit6: 0 truncating, 1 rounding. */
    if (opc == 0x8u && op.neon_data_3reg.u == 0u) {
        const uint32_t bit6 = (op.word >> 6) & 1u;
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = bit6 ? ArmNeonShiftImm::kSiRshrn
                               : ArmNeonShiftImm::kSiShrn;
        insn->kind  = ArmInsnKind::kNeonShiftImmNarrow;
        return true;
    }
    /* VQSHRUN (A8.8.381) / VQRSHRUN (A8.8.378): opc=0x8, u=1. Signed src,
       unsigned out. bit6 discriminates truncating vs rounding. */
    if (opc == 0x8u && op.neon_data_3reg.u == 1u) {
        const uint32_t bit6 = (op.word >> 6) & 1u;
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = bit6 ? ArmNeonSat::kSatRShrnSU
                               : ArmNeonSat::kSatShrnSU;
        insn->kind  = ArmInsnKind::kNeonShiftImmNarrowSat;
        return true;
    }
    /* VQSHRN (A8.8.381) / VQRSHRN (A8.8.378): opc=0x9. bit6: 0 truncating,
       1 rounding. U=0 signed, U=1 unsigned. */
    if (opc == 0x9u) {
        const uint32_t bit6 = (op.word >> 6) & 1u;
        uint32_t op_sel;
        if (op.neon_data_3reg.u == 0u) {
            op_sel = bit6 ? ArmNeonSat::kSatRShrnS
                          : ArmNeonSat::kSatShrnS;
        } else {
            op_sel = bit6 ? ArmNeonSat::kSatRShrnU
                          : ArmNeonSat::kSatShrnU;
        }
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op_sel;
        insn->kind      = ArmInsnKind::kNeonShiftImmNarrowSat;
        return true;
    }
    /* VSHLL T1/A1 (A8.8.397): opc=0xA, L=0. U=0 signed, U=1 unsigned. */
    if (opc == 0xAu) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeonShiftImm::kSiShllU
                              : ArmNeonShiftImm::kSiShllS;
        insn->kind  = ArmInsnKind::kNeonShiftImmWiden;
        return true;
    }
    /* Table A7-12 (p. A7-266): A = 111x, L = 0 is VCVT (between floating-point and
       fixed-point, Advanced SIMD) on page A8-872. */
    if ((opc & 0xEu) == 0xEu && L_ == 0u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ((opc & 1u) << 1) | op.neon_data_3reg.u;
        insn->kind      = ArmInsnKind::kNeonData2RegCvtIntFp;
        return true;
    }
    /* ARM DDI 0406C.c A7.4.4 (p. A7-266): "Other encodings in this space are
       UNDEFINED." */
    insn->cond      = 14;
    insn->immediate = op.word;
    insn->kind      = ArmInsnKind::kUndefined;
    return true;
}

}
