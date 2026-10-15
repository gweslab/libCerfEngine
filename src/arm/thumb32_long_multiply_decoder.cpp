#include "thumb32_long_multiply_decoder.h"

#include <cerfengine/engine.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "thumb32_fatal.h"

namespace cerfengine {

CERFENGINE_REGISTER(Thumb32LongMultiplyDecoder);

bool Thumb32LongMultiplyDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void Thumb32LongMultiplyDecoder::OnReady() {
    fatal_              = &engine_.Get<Thumb32Fatal>();
    has_integer_divide_ = engine_.GetSetup().arm.integer_divide;
}

bool Thumb32LongMultiplyDecoder::RegistersValid(uint32_t op) const {
    const uint32_t rn   = (op >> 16) & 0xFu;
    const uint32_t rdlo = (op >> 12) & 0xFu;
    const uint32_t rdhi = (op >>  8) & 0xFu;
    const uint32_t rm   =  op        & 0xFu;

    if (rn == 13u || rn == 0xFu || rm == 13u || rm == 0xFu) {
        return false;
    }
    if (rdlo == 13u || rdlo == 0xFu || rdhi == 13u || rdhi == 0xFu) {
        return false;
    }
    return rdhi != rdlo;
}

/* DDI 0406C.c Table A5-7 (p. A5-202) numbers the 64-bit rows 0100 UMAAL,
   100x UMULL, 101x UMLAL, 110x SMULL, 111x SMLAL, i.e. 2/4/5/6/7 at
   bits[23:21]. Every Table A6-29 T1 encoding gives "setflags = FALSE". */
bool Thumb32LongMultiplyDecoder::DecodeLongMultiply(DecodedInsn* insn,
                                                    uint32_t op, uint32_t row) {
    if (!RegistersValid(op)) {
        return false;
    }

    insn->op1      = row;
    insn->s        = 0u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rs       = (op >> 12) & 0xFu;
    insn->rd       = (op >>  8) & 0xFu;
    insn->rm       =  op        & 0xFu;
    insn->place_fn = &PlaceMultiply;
    return true;
}

/* DDI 0406C.c A8.8.179 SMLALBB, SMLALBT, SMLALTB, SMLALTT T1 (p. A8-626):
   "n_high = (N == '1'); m_high = (M == '1')" with N at hw2[5] and M at
   hw2[4]. Table A5-9 (p. A5-203) gives this row op = 0b10 at bits[22:21]. */
bool Thumb32LongMultiplyDecoder::DecodeHalfwordLongMultiply(DecodedInsn* insn,
                                                            uint32_t op) {
    if (!RegistersValid(op)) {
        return false;
    }

    insn->op1      = 2u;
    insn->s        = 0u;
    insn->n        = (op >> 5) & 0x1u;
    insn->u        = (op >> 4) & 0x1u;
    insn->rn       = (op >> 16) & 0xFu;
    insn->rs       = (op >> 12) & 0xFu;
    insn->rd       = (op >>  8) & 0xFu;
    insn->rm       =  op        & 0xFu;
    insn->place_fn = &PlaceHalfwordMultiply;
    return true;
}

bool Thumb32LongMultiplyDecoder::DecodeDivide(DecodedInsn* insn, uint32_t op,
                                              bool is_signed) {
    if (!has_integer_divide_) {
        return false;
    }
    fatal_->Unimplemented(is_signed ? "signed divide (A6-250)"
                                    : "unsigned divide (A6-250)", insn, op);
}

/* DDI 0406C.c A6.3.17 and Table A6-29 (p. A6-250): op1 = bits[22:20],
   Rn = bits[19:16], RdLo = bits[15:12], RdHi = bits[11:8], op2 = bits[7:4],
   Rm = bits[3:0]; "Other encodings in this space are UNDEFINED". */
bool Thumb32LongMultiplyDecoder::DecodeLongMultiplyDivide(DecodedInsn* insn,
                                                          uint32_t op) {
    const uint32_t op1 = (op >> 20) & 0x7u;
    const uint32_t op2 = (op >>  4) & 0xFu;

    switch (op1) {
    case 0x0u:
        return op2 == 0x0u && DecodeLongMultiply(insn, op, 6u);
    case 0x1u:
        return op2 == 0xFu && DecodeDivide(insn, op, true);
    case 0x2u:
        return op2 == 0x0u && DecodeLongMultiply(insn, op, 4u);
    case 0x3u:
        return op2 == 0xFu && DecodeDivide(insn, op, false);
    case 0x4u:
        if (op2 == 0x0u) return DecodeLongMultiply(insn, op, 7u);
        if ((op2 & 0xCu) == 0x8u) {
            return DecodeHalfwordLongMultiply(insn, op);
        }
        if ((op2 & 0xEu) == 0xCu) {
            fatal_->Unimplemented(
                "signed multiply accumulate long dual (A6-250)", insn, op);
        }
        return false;
    case 0x5u:
        if ((op2 & 0xEu) == 0xCu) {
            fatal_->Unimplemented(
                "signed multiply subtract long dual (A6-250)", insn, op);
        }
        return false;
    case 0x6u:
        if (op2 == 0x0u) return DecodeLongMultiply(insn, op, 5u);
        if (op2 == 0x6u) return DecodeLongMultiply(insn, op, 2u);
        return false;
    default:
        return false;
    }
}

}
