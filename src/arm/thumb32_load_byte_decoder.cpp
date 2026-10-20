#include "thumb32_load_byte_decoder.h"

#include <cerfengine/engine.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/block_context.h>

namespace cerfengine {

CERFENGINE_REGISTER(Thumb32LoadByteDecoder);

bool Thumb32LoadByteDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

bool Thumb32LoadByteDecoder::DecodePreloadRegister(DecodedInsn* insn,
                                                   uint32_t op) {
    const uint32_t rm = op & 0xFu;
    if (rm == 13u || rm == 0xFu) {
        return false;
    }
    insn->kind = ArmInsnKind::kNop;
    return true;
}

/* DDI 0406C.c A8.8.69 LDRB (literal) encoding T1 (p. A8-420): U = bit[23],
   "imm32 = ZeroExtend(imm12, 32); add = (U == '1')", "if t == 13 then
   UNPREDICTABLE"; Rt == '1111' is "SEE PLD". */
bool Thumb32LoadByteDecoder::DecodeByteLiteral(DecodedInsn* insn,
                                               uint32_t op) {
    const uint32_t add   = (op >> 23) & 0x1u;
    const uint32_t rt    = (op >> 12) & 0xFu;
    const uint32_t imm12 =  op        & 0xFFFu;
    if (rt == 13u) {
        return false;
    }

    insn->n        = 1u;
    insn->s        = 1u;
    insn->l        = 1u;
    insn->p        = 1u;
    insn->u        = add;
    insn->w        = 0u;
    insn->unpriv   = 0u;
    insn->rn       = 15u;
    insn->rd       = rt;
    insn->offset   = add != 0u ? static_cast<int32_t>(imm12)
                               : -static_cast<int32_t>(imm12);
    insn->kind = ArmInsnKind::kSingleDataTransfer;
    return true;
}

bool Thumb32LoadByteDecoder::DecodeByteImmediate12(DecodedInsn* insn,
                                                   uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u) {
        return false;
    }

    insn->n        = 1u;
    insn->s        = 1u;
    insn->l        = 1u;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->w        = 0u;
    insn->unpriv   = 0u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rd       = rt;
    insn->offset   = static_cast<int32_t>(op & 0xFFFu);
    insn->kind     = ArmInsnKind::kSingleDataTransfer;
    return true;
}

bool Thumb32LoadByteDecoder::DecodeByteImmediate8(DecodedInsn* insn,
                                                  uint32_t op) {
    const uint32_t add  = (op >>  9) & 0x1u;
    const uint32_t w    = (op >>  8) & 0x1u;
    const uint32_t rt   = (op >> 12) & 0xFu;
    const uint32_t rn   = (op >> 16) & 0xFu;
    const uint32_t imm8 =  op        & 0xFFu;
    if (rt == 13u || (rt == 0xFu && w != 0u) || (w != 0u && rn == rt)) {
        return false;
    }

    insn->n        = 1u;
    insn->s        = 1u;
    insn->l        = 1u;
    insn->p        = (op >> 10) & 0x1u;
    insn->u        = add;
    insn->w        = w;
    insn->unpriv   = 0u;
    insn->rn       = rn;
    insn->rd       = rt;
    insn->offset   = add != 0u ? static_cast<int32_t>(imm8)
                               : -static_cast<int32_t>(imm8);
    insn->kind = ArmInsnKind::kSingleDataTransfer;
    return true;
}

bool Thumb32LoadByteDecoder::DecodeByteRegister(DecodedInsn* insn,
                                                uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    const uint32_t rm =  op        & 0xFu;
    if (rt == 13u || rm == 13u || rm == 0xFu) {
        return false;
    }

    insn->n        = 0u;
    insn->s        = 1u;
    insn->l        = 1u;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->w        = 0u;
    insn->unpriv   = 0u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rd       = rt;
    insn->rm       = rm;
    insn->op1      = kSrLsl;
    insn->rs       = (op >> 4) & 0x3u;
    insn->kind     = ArmInsnKind::kSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A8.8.71 LDRBT encoding T1 (p. A8-424): "postindex = FALSE;
   add = TRUE", "if t IN {13,15} then UNPREDICTABLE"; the Thumb form "uses an
   offset addressing mode ... and leaves the base register unchanged". */
bool Thumb32LoadByteDecoder::DecodeByteUnprivileged(DecodedInsn* insn,
                                                    uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u || rt == 0xFu) {
        return false;
    }

    insn->n        = 1u;
    insn->s        = 1u;
    insn->l        = 1u;
    insn->p        = 1u;
    insn->u        = 1u;
    insn->w        = 0u;
    insn->unpriv   = 1u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rd       = rt;
    insn->offset   = static_cast<int32_t>(op & 0xFFu);
    insn->kind     = ArmInsnKind::kSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A8.8.85 LDRSB (literal) encoding T1 (p. A8-452): U = bit[23],
   "imm32 = ZeroExtend(imm12, 32); add = (U == '1')", "if t == 13 then
   UNPREDICTABLE"; Rt == '1111' is "SEE PLI". */
bool Thumb32LoadByteDecoder::DecodeSignedByteLiteral(DecodedInsn* insn,
                                                     uint32_t op) {
    const uint32_t add   = (op >> 23) & 0x1u;
    const uint32_t rt    = (op >> 12) & 0xFu;
    const uint32_t imm12 =  op        & 0xFFFu;
    if (rt == 13u) {
        return false;
    }

    insn->n        = 1u;
    insn->l        = 1u;
    insn->op1      = 2u;
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

bool Thumb32LoadByteDecoder::DecodeSignedByteRegister(DecodedInsn* insn,
                                                      uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    const uint32_t rm =  op        & 0xFu;
    if (rt == 13u || rm == 13u || rm == 0xFu) {
        return false;
    }

    insn->n        = 0u;
    insn->l        = 1u;
    insn->op1      = 2u;
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

bool Thumb32LoadByteDecoder::DecodeSignedByteImmediate12(DecodedInsn* insn,
                                                         uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u) {
        return false;
    }

    insn->n        = 1u;
    insn->l        = 1u;
    insn->op1      = 2u;
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

bool Thumb32LoadByteDecoder::DecodeSignedByteImmediate8(DecodedInsn* insn,
                                                        uint32_t op) {
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
    insn->op1      = 2u;
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

/* DDI 0406C.c A8.8.87 LDRSBT encoding T1 (p. A8-456): "postindex = FALSE;
   add = TRUE", "if t IN {13,15} then UNPREDICTABLE"; the Thumb form "uses an
   offset addressing mode ... and leaves the base register unchanged". */
bool Thumb32LoadByteDecoder::DecodeSignedByteUnprivileged(DecodedInsn* insn,
                                                          uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u || rt == 0xFu) {
        return false;
    }

    insn->n        = 1u;
    insn->l        = 1u;
    insn->op1      = 2u;
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

bool Thumb32LoadByteDecoder::DecodeLoadByteMemoryHints(DecodedInsn* insn,
                                                       uint32_t op) {
    const uint32_t op1 = (op >> 23) & 0x3u;
    const uint32_t rn  = (op >> 16) & 0xFu;
    const uint32_t rt  = (op >> 12) & 0xFu;
    const uint32_t op2 = (op >>  6) & 0x3Fu;
    const bool     signed_byte = op1 >= 0x2u;

    if (rn == 0xFu) {
        if (rt == 0xFu) {
            insn->kind = ArmInsnKind::kNop;
            return true;
        }
        return signed_byte ? DecodeSignedByteLiteral(insn, op)
                           : DecodeByteLiteral(insn, op);
    }

    if (op1 == 0x1u || op1 == 0x3u) {
        if (rt == 0xFu) {
            insn->kind = ArmInsnKind::kNop;
            return true;
        }
        return signed_byte ? DecodeSignedByteImmediate12(insn, op)
                           : DecodeByteImmediate12(insn, op);
    }

    if (op2 == 0x00u) {
        if (rt == 0xFu) {
            return DecodePreloadRegister(insn, op);
        }
        return signed_byte ? DecodeSignedByteRegister(insn, op)
                           : DecodeByteRegister(insn, op);
    }
    if ((op2 & 0x24u) == 0x24u || (op2 & 0x3Cu) == 0x30u) {
        if (rt == 0xFu && (op2 & 0x3Cu) == 0x30u) {
            insn->kind = ArmInsnKind::kNop;
            return true;
        }
        return signed_byte ? DecodeSignedByteImmediate8(insn, op)
                           : DecodeByteImmediate8(insn, op);
    }
    if ((op2 & 0x3Cu) == 0x38u) {
        return signed_byte ? DecodeSignedByteUnprivileged(insn, op)
                           : DecodeByteUnprivileged(insn, op);
    }
    return false;
}

}
