#include "arm_multiply_space_decoder.h"

#include <cerfengine/engine.h>
#include "arm_opcode.h"
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmMultiplySpaceDecoder);

bool ArmMultiplySpaceDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void ArmMultiplySpaceDecoder::OnReady() {
    processor_config_ = &engine_.GetSetup().arm;
}

bool ArmMultiplySpaceDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    const uint32_t row = (op.word >> 21) & 0x7u;
    const uint32_t s   = (op.word >> 20) & 0x1u;
    const uint32_t rd  = (op.word >> 16) & 0xFu;
    const uint32_t ra  = (op.word >> 12) & 0xFu;
    const uint32_t rm  = (op.word >>  8) & 0xFu;
    const uint32_t rn  =  op.word        & 0xFu;

    if (row == 2u && (s != 0u || !processor_config_->arch_v6)) {
        return false;
    }
    if (row == 3u && (s != 0u || !processor_config_->mls)) {
        return false;
    }
    if (rd == ArmGpr::kR15 || rm == ArmGpr::kR15 || rn == ArmGpr::kR15) {
        return false;
    }
    if (row == 0u) {
        if (ra != 0u) {
            return false;
        }
    } else if (ra == ArmGpr::kR15) {
        return false;
    }
    const bool is_long = row == 2u || row >= 4u;
    if (is_long && rd == ra) {
        return false;
    }

    insn->op1      = row;
    insn->s        = s;
    insn->rd       = rd;
    insn->rs       = ra;
    insn->rm       = rm;
    insn->rn       = rn;
    insn->place_fn = &PlaceMultiply;
    return true;
}

/* DDI 0406C.c Table A5-9 (p. A5-203), A5.2.7; A1 encodings SMLA<x><y>
   p. A8-620, SMLAW<y> A8-630, SMULW<y> A8-648, SMLAL<x><y> A8-626,
   SMUL<x><y> A8-644. DDI 0100I SMLAL<x><y> p. A4-149. */
bool ArmMultiplySpaceDecoder::DecodeHalfword(DecodedInsn* insn, ArmOpcode op) {
    if (!processor_config_->dsp) {
        return false;
    }
    const uint32_t row = (op.word >> 21) & 0x3u;
    const uint32_t rd  = (op.word >> 16) & 0xFu;
    const uint32_t ra  = (op.word >> 12) & 0xFu;
    const uint32_t rm  = (op.word >>  8) & 0xFu;
    const uint32_t rn  =  op.word        & 0xFu;
    const uint32_t m   = (op.word >>  6) & 0x1u;
    const uint32_t n   = (op.word >>  5) & 0x1u;

    if (rd == ArmGpr::kR15 || rm == ArmGpr::kR15 || rn == ArmGpr::kR15) {
        return false;
    }
    const bool reads_ra = row == 0u || row == 2u || (row == 1u && n == 0u);
    if (reads_ra) {
        if (ra == ArmGpr::kR15) {
            return false;
        }
        if (row == 2u && rd == ra) {
            return false;
        }
    } else if (ra != 0u) {
        return false;
    }

    insn->op1      = row;
    insn->s        = (row == 1u) ? n : 0u;
    insn->n        = (row == 1u) ? 0u : n;
    insn->u        = m;
    insn->rd       = rd;
    insn->rs       = ra;
    insn->rm       = rm;
    insn->rn       = rn;
    insn->place_fn = &PlaceHalfwordMultiply;
    return true;
}

}
