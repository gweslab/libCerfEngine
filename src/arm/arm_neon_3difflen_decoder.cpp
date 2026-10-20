#include "arm_neon_3difflen_decoder.h"

#include <cerfengine/engine.h>
#include "arm_neon_3difflen.h"
#include "arm_opcode.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/block_context.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeon3DiffLenDecoder);

bool ArmNeon3DiffLenDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    const uint32_t a_high = (op.word >> 9) & 0x7u;
    const uint32_t op_bit = (op.word >> 8) & 1u;

    /* A=000op: VADDL (op=0) / VADDW (op=1) (A8.8.285). */
    if (a_high == 0u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op_bit ? ArmNeon3DiffLen::kDlAddWide
                                 : ArmNeon3DiffLen::kDlAddLong;
        insn->kind  = ArmInsnKind::kNeonData3DiffLen;
        return true;
    }
    /* A=001op: VSUBL (op=0) / VSUBW (op=1) (A8.8.417). */
    if (a_high == 1u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op_bit ? ArmNeon3DiffLen::kDlSubWide
                                 : ArmNeon3DiffLen::kDlSubLong;
        insn->kind  = ArmInsnKind::kNeonData3DiffLen;
        return true;
    }
    /* A=0100: VADDHN (U=0, A8.8.284) / VRADDHN (U=1, A8.8.383).
       op_bit must be 0; A=0101 with op_bit==1 is VABAL (A8.8.277). */
    if (a_high == 2u && op_bit == 0u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeon3DiffLen::kDlRaddhn
                              : ArmNeon3DiffLen::kDlAddhn;
        insn->kind  = ArmInsnKind::kNeonData3DiffLenHn;
        return true;
    }
    /* A=0110: VSUBHN (U=0, A8.8.416) / VRSUBHN (U=1, A8.8.394).
       op_bit must be 0; A=0111 with op_bit==1 is VABDL (A8.8.278). */
    if (a_high == 3u && op_bit == 0u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeon3DiffLen::kDlRsubhn
                              : ArmNeon3DiffLen::kDlSubhn;
        insn->kind  = ArmInsnKind::kNeonData3DiffLenHn;
        return true;
    }
    /* A=0101: VABAL (A8.8.277 T2/A2). a_high==2, op_bit==1. */
    if (a_high == 2u && op_bit == 1u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeon3DiffLen::kDlAbalU
                              : ArmNeon3DiffLen::kDlAbalS;
        insn->kind  = ArmInsnKind::kNeonData3DiffLenAbs;
        return true;
    }
    /* A=0111: VABDL (A8.8.278 T2/A2). a_high==3, op_bit==1. */
    if (a_high == 3u && op_bit == 1u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeon3DiffLen::kDlAbdlU
                              : ArmNeon3DiffLen::kDlAbdlS;
        insn->kind  = ArmInsnKind::kNeonData3DiffLenAbs;
        return true;
    }
    /* A=1000: VMLAL (A8.8.336 T2/A2). a_high==4, op_bit==0. */
    if (a_high == 4u && op_bit == 0u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeon3DiffLen::kDlMlalU
                              : ArmNeon3DiffLen::kDlMlalS;
        insn->kind  = ArmInsnKind::kNeonData3DiffLenMul;
        return true;
    }
    /* A=1010: VMLSL (A8.8.336 T2/A2). a_high==5, op_bit==0. */
    if (a_high == 5u && op_bit == 0u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeon3DiffLen::kDlMlslU
                              : ArmNeon3DiffLen::kDlMlslS;
        insn->kind  = ArmInsnKind::kNeonData3DiffLenMul;
        return true;
    }
    /* A=1100: VMULL integer (A8.8.350 T2/A2). a_high==6, op_bit==0. */
    if (a_high == 6u && op_bit == 0u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = op.neon_data_3reg.u
                              ? ArmNeon3DiffLen::kDlMullIntU
                              : ArmNeon3DiffLen::kDlMullIntS;
        insn->kind  = ArmInsnKind::kNeonData3DiffLenMul;
        return true;
    }
    /* A=1110: VMULL polynomial (A8.8.350 T2/A2). a_high==7, op_bit==0.
       ARM DDI 0406C.c A8.8.350 (p. A8-958): "if op == '1' && (U != '0' ||
       size != '00') then UNDEFINED". */
    if (a_high == 7u && op_bit == 0u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon3DiffLen::kDlMullPoly;
        insn->kind      = ArmInsnKind::kNeonData3DiffLenMul;
        return true;
    }
    /* A=1001: VQDMLAL (A8.8.371 T1/A1). a_high==4, op_bit==1. */
    if (a_high == 4u && op_bit == 1u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon3DiffLen::kDlVqdmlal;
        insn->kind      = ArmInsnKind::kNeonData3DiffLenMulSat;
        return true;
    }
    /* A=1011: VQDMLSL (A8.8.371 T1/A1). a_high==5, op_bit==1. */
    if (a_high == 5u && op_bit == 1u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon3DiffLen::kDlVqdmlsl;
        insn->kind      = ArmInsnKind::kNeonData3DiffLenMulSat;
        return true;
    }
    /* A=1101: VQDMULL (A8.8.373 T1/A1). a_high==6, op_bit==1. */
    if (a_high == 6u && op_bit == 1u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon3DiffLen::kDlVqdmull;
        insn->kind      = ArmInsnKind::kNeonData3DiffLenMulSat;
        return true;
    }
    /* ARM DDI 0406C.c Table A7-10 (p. A7-264) allocates A = 000x through 1110
       only. A7.4.2 (p. A7-264): "Other encodings in this space are
       UNDEFINED." */
    insn->cond      = 14;
    insn->immediate = op.word;
    insn->kind      = ArmInsnKind::kUndefined;
    return true;
}

}
