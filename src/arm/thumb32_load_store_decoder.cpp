#include "thumb32_load_store_decoder.h"

#include <cerfengine/engine.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>

namespace cerfengine {

CERFENGINE_REGISTER(Thumb32LoadStoreDecoder);

bool Thumb32LoadStoreDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

/* DDI 0406C.c A8.8.64 LDR (literal) encoding T2 (p. A8-410): U = bit[23],
   Rt = hw2[15:12], imm12 = hw2[11:0], "imm32 = ZeroExtend(imm12, 32);
   add = (U == '1')". */
bool Thumb32LoadStoreDecoder::DecodeLoadLiteral(DecodedInsn* insn,
                                                uint32_t op) {
    const uint32_t add   = (op >> 23) & 0x1u;
    const uint32_t rt    = (op >> 12) & 0xFu;
    const uint32_t imm12 =  op        & 0xFFFu;

    insn->n            = 1u;
    insn->s            = 0u;
    insn->l            = 1u;
    insn->p            = 1u;
    insn->u            = add;
    insn->w            = 0u;
    insn->unpriv       = 0u;
    insn->rn           = 15u;
    insn->rd           = rt;
    insn->offset       = add != 0u ? static_cast<int32_t>(imm12)
                                   : -static_cast<int32_t>(imm12);
    insn->r15_modified = rt == 15u;
    insn->place_fn     = &PlaceSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A8.8.62 LDR (immediate, Thumb) encoding T3 (p. A8-406):
   "imm32 = ZeroExtend(imm12, 32); index = TRUE; add = TRUE;
   wback = FALSE". */
bool Thumb32LoadStoreDecoder::DecodeLoadImmediate12(DecodedInsn* insn,
                                                    uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;

    insn->n            = 1u;
    insn->s            = 0u;
    insn->l            = 1u;
    insn->p            = 1u;
    insn->u            = 1u;
    insn->w            = 0u;
    insn->unpriv       = 0u;
    insn->rn           = (op >> 16) & 0xFu;
    insn->rd           = rt;
    insn->offset       = static_cast<int32_t>(op & 0xFFFu);
    insn->r15_modified = rt == 15u;
    insn->place_fn     = &PlaceSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A8.8.62 LDR (immediate, Thumb) encoding T4 (p. A8-406):
   hw2[10:8] = P:U:W, imm8 = hw2[7:0], "imm32 = ZeroExtend(imm8, 32);
   index = (P == '1'); add = (U == '1'); wback = (W == '1')". */
bool Thumb32LoadStoreDecoder::DecodeLoadImmediate8(DecodedInsn* insn,
                                                   uint32_t op) {
    const uint32_t add  = (op >> 9) & 0x1u;
    const uint32_t rt   = (op >> 12) & 0xFu;
    const uint32_t imm8 =  op        & 0xFFu;

    insn->n            = 1u;
    insn->s            = 0u;
    insn->l            = 1u;
    insn->p            = (op >> 10) & 0x1u;
    insn->u            = add;
    insn->w            = (op >>  8) & 0x1u;
    insn->unpriv       = 0u;
    insn->rn           = (op >> 16) & 0xFu;
    insn->rd           = rt;
    insn->offset       = add != 0u ? static_cast<int32_t>(imm8)
                                   : -static_cast<int32_t>(imm8);
    insn->r15_modified = rt == 15u;
    insn->place_fn     = &PlaceSingleDataTransfer;
    return true;
}

bool Thumb32LoadStoreDecoder::DecodeLoadRegister(DecodedInsn* insn,
                                                 uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    const uint32_t rm =  op        & 0xFu;
    if (rm == 13u || rm == 0xFu) {
        return false;
    }

    insn->n            = 0u;
    insn->s            = 0u;
    insn->l            = 1u;
    insn->p            = 1u;
    insn->u            = 1u;
    insn->w            = 0u;
    insn->unpriv       = 0u;
    insn->rn           = (op >> 16) & 0xFu;
    insn->rd           = rt;
    insn->rm           = rm;
    insn->op1          = kSrLsl;
    insn->rs           = (op >> 4) & 0x3u;
    insn->r15_modified = rt == 15u;
    insn->place_fn     = &PlaceSingleDataTransfer;
    return true;
}

bool Thumb32LoadStoreDecoder::DecodeLoadUnprivileged(DecodedInsn* insn,
                                                     uint32_t op) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u || rt == 0xFu) {
        return false;
    }

    insn->n            = 1u;
    insn->s            = 0u;
    insn->l            = 1u;
    insn->p            = 1u;
    insn->u            = 1u;
    insn->w            = 0u;
    insn->unpriv       = 1u;
    insn->rn           = (op >> 16) & 0xFu;
    insn->rd           = rt;
    insn->offset       = static_cast<int32_t>(op & 0xFFu);
    insn->r15_modified = false;
    insn->place_fn     = &PlaceSingleDataTransfer;
    return true;
}

/* DDI 0406C.c A6.3.7 and Table A6-18 p. A6-239: op1 = bits[24:23],
   op2 = hw2[11:6], Rn = bits[19:16]; other encodings in this space are
   UNDEFINED. The SEE redirects are A8-406/410/412/466 encoding line 1. */
bool Thumb32LoadStoreDecoder::DecodeLoadWord(DecodedInsn* insn, uint32_t op) {
    const uint32_t op1 = (op >> 23) & 0x3u;
    if (op1 > 0x1u) {
        return false;
    }
    if (((op >> 16) & 0xFu) == 0xFu) {
        return DecodeLoadLiteral(insn, op);
    }
    if (op1 == 0x1u) {
        return DecodeLoadImmediate12(insn, op);
    }

    const uint32_t op2 = (op >> 6) & 0x3Fu;
    if (op2 == 0x00u) {
        return DecodeLoadRegister(insn, op);
    }
    if ((op2 & 0x20u) == 0u) {
        return false;
    }

    const uint32_t p = (op >> 10) & 0x1u;
    const uint32_t u = (op >>  9) & 0x1u;
    const uint32_t w = (op >>  8) & 0x1u;
    if (p != 0u && u != 0u && w == 0u) {
        return DecodeLoadUnprivileged(insn, op);
    }
    /* A8.8.62 encoding T4 (p. A8-406): "if P == '0' && W == '0' then
       UNDEFINED". */
    if (p == 0u && w == 0u) {
        return false;
    }
    return DecodeLoadImmediate8(insn, op);
}

void Thumb32LoadStoreDecoder::SetStoreTransfer(DecodedInsn* insn,
                                               StoreSize size) {
    if (size == StoreSize::kHalfword) {
        insn->op1      = 1u;
        insn->place_fn = &PlaceLoadStoreExtension;
        return;
    }
    insn->s        = size == StoreSize::kByte ? 1u : 0u;
    insn->place_fn = &PlaceSingleDataTransfer;
}

bool Thumb32LoadStoreDecoder::DecodeStoreImmediate12(DecodedInsn* insn,
                                                     uint32_t op,
                                                     StoreSize size) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 0xFu || (size != StoreSize::kWord && rt == 13u)) {
        return false;
    }

    insn->n      = 1u;
    insn->l      = 0u;
    insn->p      = 1u;
    insn->u      = 1u;
    insn->w      = 0u;
    insn->unpriv = 0u;
    insn->rn     = (op >> 16) & 0xFu;
    insn->rd     = rt;
    insn->offset = static_cast<int32_t>(op & 0xFFFu);
    SetStoreTransfer(insn, size);
    return true;
}

bool Thumb32LoadStoreDecoder::DecodeStoreImmediate8(DecodedInsn* insn,
                                                    uint32_t op,
                                                    StoreSize size) {
    const uint32_t add  = (op >>  9) & 0x1u;
    const uint32_t w    = (op >>  8) & 0x1u;
    const uint32_t rt   = (op >> 12) & 0xFu;
    const uint32_t rn   = (op >> 16) & 0xFu;
    const uint32_t imm8 =  op        & 0xFFu;
    if (rt == 0xFu || (size != StoreSize::kWord && rt == 13u) ||
        (w != 0u && rn == rt)) {
        return false;
    }

    insn->n      = 1u;
    insn->l      = 0u;
    insn->p      = (op >> 10) & 0x1u;
    insn->u      = add;
    insn->w      = w;
    insn->unpriv = 0u;
    insn->rn     = rn;
    insn->rd     = rt;
    insn->offset = add != 0u ? static_cast<int32_t>(imm8)
                             : -static_cast<int32_t>(imm8);
    SetStoreTransfer(insn, size);
    return true;
}

bool Thumb32LoadStoreDecoder::DecodeStoreRegister(DecodedInsn* insn,
                                                  uint32_t op, StoreSize size) {
    const uint32_t rt = (op >> 12) & 0xFu;
    const uint32_t rm =  op        & 0xFu;
    if (rt == 0xFu || (size != StoreSize::kWord && rt == 13u) || rm == 13u ||
        rm == 0xFu) {
        return false;
    }

    insn->n      = 0u;
    insn->l      = 0u;
    insn->p      = 1u;
    insn->u      = 1u;
    insn->w      = 0u;
    insn->unpriv = 0u;
    insn->rn     = (op >> 16) & 0xFu;
    insn->rd     = rt;
    insn->rm     = rm;
    insn->op1    = kSrLsl;
    insn->rs     = (op >> 4) & 0x3u;
    SetStoreTransfer(insn, size);
    return true;
}

bool Thumb32LoadStoreDecoder::DecodeStoreUnprivileged(DecodedInsn* insn,
                                                      uint32_t op,
                                                      StoreSize size) {
    const uint32_t rt = (op >> 12) & 0xFu;
    if (rt == 13u || rt == 0xFu) {
        return false;
    }

    insn->n      = 1u;
    insn->l      = 0u;
    insn->p      = 1u;
    insn->u      = 1u;
    insn->w      = 0u;
    insn->unpriv = 1u;
    insn->rn     = (op >> 16) & 0xFu;
    insn->rd     = rt;
    insn->offset = static_cast<int32_t>(op & 0xFFu);
    SetStoreTransfer(insn, size);
    return true;
}

bool Thumb32LoadStoreDecoder::DecodeStoreSingleDataItem(DecodedInsn* insn,
                                                        uint32_t op) {
    const uint32_t op1 = (op >> 21) & 0x7u;
    if ((op1 & 0x3u) == 0x3u) {
        return false;
    }
    const StoreSize size = (op1 & 0x3u) == 0x0u   ? StoreSize::kByte
                           : (op1 & 0x3u) == 0x1u ? StoreSize::kHalfword
                                                  : StoreSize::kWord;
    if (((op >> 16) & 0xFu) == 0xFu) {
        return false;
    }
    if ((op1 & 0x4u) != 0u) {
        return DecodeStoreImmediate12(insn, op, size);
    }

    const uint32_t op2 = (op >> 6) & 0x3Fu;
    if (op2 == 0x00u) {
        /* A8.8.218 STRH (register) T2 (p. A8-702): "(shift_t, shift_n) =
           (SRType_LSL, UInt(imm2))". */
        return DecodeStoreRegister(insn, op, size);
    }
    if ((op2 & 0x20u) == 0u) {
        return false;
    }

    const uint32_t p = (op >> 10) & 0x1u;
    const uint32_t u = (op >>  9) & 0x1u;
    const uint32_t w = (op >>  8) & 0x1u;
    if (p != 0u && u != 0u && w == 0u) {
        return DecodeStoreUnprivileged(insn, op, size);
    }
    /* A8.8.203 encoding T4 (p. A8-672): "if Rn == '1111' || (P == '0' &&
       W == '0') then UNDEFINED". */
    if (p == 0u && w == 0u) {
        return false;
    }
    return DecodeStoreImmediate8(insn, op, size);
}

}
