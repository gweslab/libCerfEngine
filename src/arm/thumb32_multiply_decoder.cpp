#include "thumb32_multiply_decoder.h"

#include <cerfengine/engine.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "thumb32_fatal.h"

namespace cerfengine {

CERFENGINE_REGISTER(Thumb32MultiplyDecoder);

bool Thumb32MultiplyDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void Thumb32MultiplyDecoder::OnReady() {
    fatal_ = &engine_.Get<Thumb32Fatal>();
}

bool Thumb32MultiplyDecoder::DecodeMultiplyAccumulate(DecodedInsn* insn,
                                                      uint32_t op) {
    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t ra = (op >> 12) & 0xFu;
    const uint32_t rd = (op >>  8) & 0xFu;
    const uint32_t rm =  op        & 0xFu;

    if (rd == 13u || rd == 0xFu || rn == 13u || rn == 0xFu ||
        rm == 13u || rm == 0xFu || ra == 13u) {
        return false;
    }

    insn->op1      = ra == 0xFu ? 0u : 1u;
    insn->s        = 0u;
    insn->rd       = rd;
    insn->rn       = rn;
    insn->rm       = rm;
    insn->rs       = ra == 0xFu ? 0u : ra;
    insn->place_fn = &PlaceMultiply;
    return true;
}

/* DDI 0406C.c A8.8.101 MLS encoding T1 (p. A8-482): "if d IN {13,15} ||
   n IN {13,15} || m IN {13,15} || a IN {13,15} then UNPREDICTABLE". */
bool Thumb32MultiplyDecoder::DecodeMultiplySubtract(DecodedInsn* insn,
                                                    uint32_t op) {
    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t ra = (op >> 12) & 0xFu;
    const uint32_t rd = (op >>  8) & 0xFu;
    const uint32_t rm =  op        & 0xFu;

    if (rd == 13u || rd == 0xFu || rn == 13u || rn == 0xFu ||
        rm == 13u || rm == 0xFu || ra == 13u || ra == 0xFu) {
        return false;
    }

    insn->op1      = 3u;
    insn->s        = 0u;
    insn->rd       = rd;
    insn->rn       = rn;
    insn->rm       = rm;
    insn->rs       = ra;
    insn->place_fn = &PlaceMultiply;
    return true;
}

bool Thumb32MultiplyDecoder::DecodeHalfwordMultiply(DecodedInsn* insn,
                                                    uint32_t op) {
    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t ra = (op >> 12) & 0xFu;
    const uint32_t rd = (op >>  8) & 0xFu;
    const uint32_t rm =  op        & 0xFu;

    if (rd == 13u || rd == 0xFu || rn == 13u || rn == 0xFu ||
        rm == 13u || rm == 0xFu || ra == 13u) {
        return false;
    }

    insn->op1      = ra == 0xFu ? 3u : 0u;
    insn->n        = (op >> 5) & 0x1u;
    insn->u        = (op >> 4) & 0x1u;
    insn->rd       = rd;
    insn->rn       = rn;
    insn->rm       = rm;
    insn->rs       = ra == 0xFu ? 0u : ra;
    insn->place_fn = &PlaceHalfwordMultiply;
    return true;
}

bool Thumb32MultiplyDecoder::DecodeWordByHalfwordMultiply(DecodedInsn* insn,
                                                          uint32_t op) {
    const uint32_t rn = (op >> 16) & 0xFu;
    const uint32_t ra = (op >> 12) & 0xFu;
    const uint32_t rd = (op >>  8) & 0xFu;
    const uint32_t rm =  op        & 0xFu;

    if (rd == 13u || rd == 0xFu || rn == 13u || rn == 0xFu ||
        rm == 13u || rm == 0xFu || ra == 13u) {
        return false;
    }

    insn->op1      = 1u;
    insn->s        = ra == 0xFu ? 1u : 0u;
    insn->u        = (op >> 4) & 0x1u;
    insn->rd       = rd;
    insn->rn       = rn;
    insn->rm       = rm;
    insn->rs       = ra == 0xFu ? 0u : ra;
    insn->place_fn = &PlaceHalfwordMultiply;
    return true;
}

bool Thumb32MultiplyDecoder::DecodeMultiplyAbsoluteDifference(DecodedInsn* insn,
                                                              uint32_t op) {
    if (((op >> 6) & 0x3u) != 0u) {
        return false;
    }

    const uint32_t op1 = (op >> 20) & 0x7u;
    const uint32_t op2 = (op >>  4) & 0x3u;

    switch (op1) {
    case 0x0u:
        if (op2 == 0x0u) return DecodeMultiplyAccumulate(insn, op);
        if (op2 == 0x1u) return DecodeMultiplySubtract(insn, op);
        return false;
    case 0x1u:
        return DecodeHalfwordMultiply(insn, op);
    case 0x3u:
        if ((op2 & 0x2u) != 0u) return false;
        return DecodeWordByHalfwordMultiply(insn, op);
    case 0x2u:
        if ((op2 & 0x2u) != 0u) return false;
        fatal_->Unimplemented("signed multiply accumulate dual (A6-249)",
                              insn, op);
    case 0x4u:
        if ((op2 & 0x2u) != 0u) return false;
        fatal_->Unimplemented("signed multiply subtract dual (A6-249)",
                              insn, op);
    case 0x5u:
        if ((op2 & 0x2u) != 0u) return false;
        fatal_->Unimplemented("signed most significant word multiply "
                              "accumulate (A6-249)", insn, op);
    case 0x6u:
        if ((op2 & 0x2u) != 0u) return false;
        fatal_->Unimplemented("signed most significant word multiply "
                              "subtract (A6-249)", insn, op);
    default:
        if (op2 != 0x0u) return false;
        fatal_->Unimplemented("unsigned sum of absolute differences (A6-249)",
                              insn, op);
    }
}

}
