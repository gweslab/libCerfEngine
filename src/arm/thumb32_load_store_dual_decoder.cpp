#include "thumb32_load_store_dual_decoder.h"

#include <cerfengine/engine.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/block_context.h>

namespace cerfengine {

CERFENGINE_REGISTER(Thumb32LoadStoreDualDecoder);

bool Thumb32LoadStoreDualDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void Thumb32LoadStoreDualDecoder::OnReady() {
    has_cp15_v7_ = engine_.GetSetup().arm.arch_v7;
}

bool Thumb32LoadStoreDualDecoder::DecodeTableBranchByte(DecodedInsn* insn,
                                                        uint32_t op) {
    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t rm =  op        & 0xFu;

    if (((op >> 12) & 0xFu) != 0xFu || ((op >> 8) & 0xFu) != 0u) {
        return false;
    }
    if (rn == 13u || rm == 13u || rm == 0xFu) {
        return false;
    }

    insn->rn           = rn;
    insn->rm           = rm;
    /* "is_tbh = (H == '1')", H at hw2[4] of the A8.8.236 T1 diagram. */
    insn->op1          = (op >> 4) & 0x1u;
    insn->r15_modified = true;
    insn->kind         = ArmInsnKind::kTableBranchByte;
    return true;
}

bool Thumb32LoadStoreDualDecoder::DecodeLoadExclusive(DecodedInsn* insn,
                                                      uint32_t op,
                                                      uint32_t bytes) {
    /* A8.8.76 LDREXB (p. A8-434), A8.8.77 LDREXD (p. A8-436) and A8.8.78
       LDREXH (p. A8-438) give "Encoding T1 ARMv7"; A8.8.75 LDREX (p. A8-432)
       gives "Encoding T1 ARMv6T2, ARMv7". */
    if (bytes != 4u && !has_cp15_v7_) {
        return false;
    }

    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u || rt == 15u || rn == 15u) {
        return false;
    }
    if (bytes == 8u) {
        const uint32_t rt2 = (op >> 8) & 0xFu;
        if (rt2 == 13u || rt2 == 15u || rt2 == rt || (op & 0xFu) != 0xFu) {
            return false;
        }
        insn->rd2 = rt2;
    } else if (bytes == 4u) {
        if (((op >> 8) & 0xFu) != 0xFu) {
            return false;
        }
        insn->offset = static_cast<int32_t>((op & 0xFFu) * 4u);
    } else if (((op >> 8) & 0xFu) != 0xFu || (op & 0xFu) != 0xFu) {
        return false;
    }
    insn->op1      = bytes;
    insn->rd       = rt;
    insn->rn       = rn;
    insn->kind     = ArmInsnKind::kLdrex;
    return true;
}

bool Thumb32LoadStoreDualDecoder::DecodeStoreExclusive(DecodedInsn* insn,
                                                       uint32_t op,
                                                       uint32_t bytes) {
    /* A8.8.213 STREXB (p. A8-692), A8.8.214 STREXD (p. A8-694) and A8.8.215
       STREXH (p. A8-696) give "Encoding T1 ARMv7"; A8.8.212 STREX (p. A8-690)
       gives "Encoding T1 ARMv6T2, ARMv7". */
    if (bytes != 4u && !has_cp15_v7_) {
        return false;
    }

    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t rt = (op >> 12) & 0xFu;
    uint32_t       rd = 0;
    if (bytes == 4u) {
        rd           = (op >> 8) & 0xFu;
        insn->offset = static_cast<int32_t>((op & 0xFFu) * 4u);
    } else {
        rd = op & 0xFu;
        if (bytes != 8u && ((op >> 8) & 0xFu) != 0xFu) {
            return false;
        }
    }
    if (rd == 13u || rd == 15u || rt == 13u || rt == 15u || rn == 15u) {
        return false;
    }
    if (rd == rn || rd == rt) {
        return false;
    }
    if (bytes == 8u) {
        const uint32_t rt2 = (op >> 8) & 0xFu;
        if (rt2 == 13u || rt2 == 15u || rd == rt2) {
            return false;
        }
        insn->rd2 = rt2;
    }
    insn->op1      = bytes;
    insn->rd       = rd;
    insn->rn       = rn;
    insn->rm       = rt;
    insn->kind     = ArmInsnKind::kStrex;
    return true;
}

/* DDI 0406C.c Table A6-17 (p. A6-238), the op1 = 01 rows keyed on
   op3 = hw2[7:4]: 0100 byte, 0101 halfword, 0111 doubleword. */
bool Thumb32LoadStoreDualDecoder::DecodeExclusiveOrTableBranch(
    DecodedInsn* insn, uint32_t op) {
    const uint32_t op2 = (op >> 20) & 0x3u;
    const uint32_t op3 = (op >>  4) & 0xFu;

    if (op2 == 0x0u) {
        switch (op3) {
        case 0x4u: return DecodeStoreExclusive(insn, op, 1u);
        case 0x5u: return DecodeStoreExclusive(insn, op, 2u);
        case 0x7u: return DecodeStoreExclusive(insn, op, 8u);
        default:   return false;
        }
    }

    switch (op3) {
    case 0x0u:
    case 0x1u:
        return DecodeTableBranchByte(insn, op);
    case 0x4u: return DecodeLoadExclusive(insn, op, 1u);
    case 0x5u: return DecodeLoadExclusive(insn, op, 2u);
    case 0x7u: return DecodeLoadExclusive(insn, op, 8u);
    default:   return false;
    }
}

bool Thumb32LoadStoreDualDecoder::DecodeDual(DecodedInsn* insn, uint32_t op) {
    const uint32_t p    = (op >> 24) & 0x1u;
    const uint32_t u    = (op >> 23) & 0x1u;
    const uint32_t w    = (op >> 21) & 0x1u;
    const bool     load = ((op >> 20) & 0x1u) != 0u;
    const uint32_t rn   = (op >> 16) & 0xFu;
    const uint32_t rt   = (op >> 12) & 0xFu;
    const uint32_t rt2  = (op >>  8) & 0xFu;

    if (rt == 13u || rt == 15u || rt2 == 13u || rt2 == 15u) {
        return false;
    }
    if (load) {
        if (rt == rt2 || (rn == 15u && w != 0u)) {
            return false;
        }
    } else if (rn == 15u) {
        return false;
    }
    if (w != 0u && (rn == rt || rn == rt2)) {
        return false;
    }

    const int32_t imm32 = static_cast<int32_t>((op & 0xFFu) * 4u);
    insn->op1      = load ? 2u : 3u;
    insn->l        = 0u;
    insn->n        = 1u;
    insn->p        = p;
    insn->u        = u;
    insn->w        = w;
    insn->rn       = rn;
    insn->rd       = rt;
    insn->rd2      = rt2;
    insn->offset   = u != 0u ? imm32 : -imm32;
    insn->kind     = ArmInsnKind::kLoadStoreExtension;
    return true;
}

/* DDI 0406C.c A6.3.6 and Table A6-17 (p. A6-238): op1 = bits[24:23],
   op2 = bits[21:20], op3 = hw2[7:4], Rn = bits[19:16]; "Other encodings in
   this space are UNDEFINED". */
bool Thumb32LoadStoreDualDecoder::Decode(DecodedInsn* insn, uint32_t op) {
    const uint32_t op1 = (op >> 23) & 0x3u;
    const uint32_t op2 = (op >> 20) & 0x3u;

    if (op1 == 0x0u && op2 == 0x0u) {
        return DecodeStoreExclusive(insn, op, 4u);
    }
    if (op1 == 0x0u && op2 == 0x1u) {
        return DecodeLoadExclusive(insn, op, 4u);
    }
    if (op1 == 0x1u && op2 <= 0x1u) {
        return DecodeExclusiveOrTableBranch(insn, op);
    }
    return DecodeDual(insn, op);
}

}
