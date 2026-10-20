#include "thumb32_load_halfword_decoder.h"

#include <cerfengine/engine.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/block_context.h>

namespace cerfengine {

CERFENGINE_REGISTER(Thumb32LoadHalfwordDecoder);

bool Thumb32LoadHalfwordDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

/* DDI 0406C.c A8.8.81 LDRH (literal) encoding T1 (p. A8-444) and A8.8.89 LDRSH
   (literal) encoding T1 (p. A8-460): U = bit[23], "imm32 = ZeroExtend(imm12,
   32); add = (U == '1')", "if t == 13 then UNPREDICTABLE". */
bool Thumb32LoadHalfwordDecoder::DecodeLiteral(DecodedInsn* insn, uint32_t op,
                                               uint32_t ext_op) {
    const uint32_t add   = (op >> 23) & 0x1u;
    const uint32_t rt    = (op >> 12) & 0xFu;
    const uint32_t imm12 =  op        & 0xFFFu;
    if (rt == 13u) {
        return false;
    }

    insn->n        = 1u;
    insn->l        = 1u;
    insn->op1      = ext_op;
    insn->p        = 1u;
    insn->u        = add;
    insn->w        = 0u;
    insn->unpriv   = 0u;
    insn->rn       = 15u;
    insn->rd       = rt;
    insn->offset   = add != 0u ? static_cast<int32_t>(imm12)
                               : -static_cast<int32_t>(imm12);
    insn->kind = ArmInsnKind::kLoadStoreExtension;
    return true;
}

bool Thumb32LoadHalfwordDecoder::DecodeImmediate12(DecodedInsn* insn,
                                                   uint32_t op,
                                                   uint32_t ext_op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u) {
        return false;
    }

    insn->n        = 1u;
    insn->l        = 1u;
    insn->op1      = ext_op;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->w        = 0u;
    insn->unpriv   = 0u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rd       = rt;
    insn->offset   = static_cast<int32_t>(op & 0xFFFu);
    insn->kind     = ArmInsnKind::kLoadStoreExtension;
    return true;
}

bool Thumb32LoadHalfwordDecoder::DecodeImmediate8(DecodedInsn* insn,
                                                  uint32_t op,
                                                  uint32_t ext_op) {
    const uint32_t add  = (op >>  9) & 0x1u;
    const uint32_t w    = (op >>  8) & 0x1u;
    const uint32_t rt   = (op >> 12) & 0xFu;
    const uint32_t rn   = (op >> 16) & 0xFu;
    const uint32_t imm8 =  op        & 0xFFu;
    if (rt == 13u || (rt == 0xFu && w != 0u) || (w != 0u && rn == rt)) {
        return false;
    }

    insn->n        = 1u;
    insn->l        = 1u;
    insn->op1      = ext_op;
    insn->p        = (op >> 10) & 0x1u;
    insn->u        = add;
    insn->w        = w;
    insn->unpriv   = 0u;
    insn->rn       = rn;
    insn->rd       = rt;
    insn->offset   = add != 0u ? static_cast<int32_t>(imm8)
                               : -static_cast<int32_t>(imm8);
    insn->kind = ArmInsnKind::kLoadStoreExtension;
    return true;
}

bool Thumb32LoadHalfwordDecoder::DecodeRegister(DecodedInsn* insn, uint32_t op,
                                                uint32_t ext_op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    const uint32_t rm =  op        & 0xFu;
    if (rt == 13u || rm == 13u || rm == 0xFu) {
        return false;
    }

    insn->n        = 0u;
    insn->l        = 1u;
    insn->op1      = ext_op;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->w        = 0u;
    insn->unpriv   = 0u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rd       = rt;
    insn->rm       = rm;
    insn->rs       = (op >> 4) & 0x3u;
    insn->kind     = ArmInsnKind::kLoadStoreExtension;
    return true;
}

bool Thumb32LoadHalfwordDecoder::DecodeUnprivileged(DecodedInsn* insn,
                                                    uint32_t op,
                                                    uint32_t ext_op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u || rt == 0xFu) {
        return false;
    }

    insn->n        = 1u;
    insn->l        = 1u;
    insn->op1      = ext_op;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->w        = 0u;
    insn->unpriv   = 1u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rd       = rt;
    insn->offset   = static_cast<int32_t>(op & 0xFFu);
    insn->kind     = ArmInsnKind::kLoadStoreExtension;
    return true;
}

bool Thumb32LoadHalfwordDecoder::DecodeLoadHalfwordMemoryHints(
    DecodedInsn* insn, uint32_t op) {
    const uint32_t op1 = (op >> 23) & 0x3u;
    const uint32_t rn  = (op >> 16) & 0xFu;
    const uint32_t rt  = (op >> 12) & 0xFu;
    const uint32_t op2 = (op >>  6) & 0x3Fu;
    const uint32_t ext_op = op1 >= 0x2u ? 3u : 1u;

    if (rt == 0xFu &&
        (rn == 0xFu || op1 == 0x1u || op1 == 0x3u || op2 == 0x00u ||
         (op2 & 0x3Cu) == 0x30u)) {
        insn->kind = ArmInsnKind::kNop;
        return true;
    }

    if (rn == 0xFu) {
        return DecodeLiteral(insn, op, ext_op);
    }
    if (op1 == 0x1u || op1 == 0x3u) {
        return DecodeImmediate12(insn, op, ext_op);
    }
    if (op2 == 0x00u) {
        return DecodeRegister(insn, op, ext_op);
    }
    if ((op2 & 0x24u) == 0x24u || (op2 & 0x3Cu) == 0x30u) {
        return DecodeImmediate8(insn, op, ext_op);
    }
    if ((op2 & 0x3Cu) == 0x38u) {
        return DecodeUnprivileged(insn, op, ext_op);
    }
    return false;
}

}
