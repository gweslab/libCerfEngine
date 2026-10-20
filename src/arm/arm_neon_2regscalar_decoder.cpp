#include "arm_neon_2regscalar_decoder.h"

#include <cerfengine/engine.h>
#include "arm_neon_2reg_scalar_mul.h"
#include "arm_neon_2regscalar.h"
#include "arm_opcode.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/block_context.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeon2RegScalarDecoder);

bool ArmNeon2RegScalarDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    const uint32_t a = (op.word >> 8) & 0xFu;
    /* A=000F: VMLA by scalar T1/A1 (A8.8.338, op=0). F=bit[8] selects int/.F32. */
    if (a == 0x0u || a == 0x1u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon2RegScalarMul::kMla;
        insn->kind      = ArmInsnKind::kNeonData2RegScalarMul;
        return true;
    }
    /* A=010F: VMLS by scalar T1/A1 (A8.8.338, op=1). */
    if (a == 0x4u || a == 0x5u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon2RegScalarMul::kMls;
        insn->kind      = ArmInsnKind::kNeonData2RegScalarMul;
        return true;
    }
    /* A=100F: VMUL by scalar T1/A1 (A8.8.352). */
    if (a == 0x8u || a == 0x9u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon2RegScalarMul::kMul;
        insn->kind      = ArmInsnKind::kNeonData2RegScalarMul;
        return true;
    }
    /* A=0010: VMLAL by scalar (A8.8.338 T2/A2, op=0). */
    if (a == 0x2u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon2RegScalar::kS2sMlal;
        insn->kind      = ArmInsnKind::kNeonData2RegScalarLong;
        return true;
    }
    /* A=0110: VMLSL by scalar (A8.8.338 T2/A2, op=1). */
    if (a == 0x6u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon2RegScalar::kS2sMlsl;
        insn->kind      = ArmInsnKind::kNeonData2RegScalarLong;
        return true;
    }
    /* A=1010: VMULL by scalar (A8.8.352 T2/A2). Long form, U selects signedness. */
    if (a == 0xAu) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon2RegScalar::kS2sMull;
        insn->kind      = ArmInsnKind::kNeonData2RegScalarLong;
        return true;
    }
    /* A=0011: VQDMLAL by scalar (A8.8.371 T2/A2, op=0). Signed only. */
    if (a == 0x3u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon2RegScalar::kS2sVqdmlal;
        insn->kind      = ArmInsnKind::kNeonData2RegScalarMulSat;
        return true;
    }
    /* A=0111: VQDMLSL by scalar (A8.8.371 T2/A2, op=1). Signed only. */
    if (a == 0x7u) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon2RegScalar::kS2sVqdmlsl;
        insn->kind      = ArmInsnKind::kNeonData2RegScalarMulSat;
        return true;
    }
    /* A=1011: VQDMULL by scalar (A8.8.373 T2/A2). Signed only, no accum. */
    if (a == 0xBu) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon2RegScalar::kS2sVqdmull;
        insn->kind      = ArmInsnKind::kNeonData2RegScalarMulSat;
        return true;
    }
    /* A=1100: VQDMULH by scalar (A8.8.372 T2/A2). Signed only. */
    if (a == 0xCu) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon2RegScalar::kS2sVqdmulh;
        insn->kind      = ArmInsnKind::kNeonData2RegScalarMulhSat;
        return true;
    }
    /* A=1101: VQRDMULH by scalar (A8.8.376 T2/A2). Signed only, rounding. */
    if (a == 0xDu) {
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->op1       = ArmNeon2RegScalar::kS2sVqrdmulh;
        insn->kind      = ArmInsnKind::kNeonData2RegScalarMulhSat;
        return true;
    }
    /* ARM DDI 0406C.c Table A7-11 (p. A7-265) allocates A = 0x0x, 0x10, 0x11,
       100x, 1010, 1011, 1100 and 1101 only. A7.4.3 (p. A7-265): "Other
       encodings in this space are UNDEFINED." */
    insn->cond      = 14;
    insn->immediate = op.word;
    insn->kind      = ArmInsnKind::kUndefined;
    return true;
}

}
