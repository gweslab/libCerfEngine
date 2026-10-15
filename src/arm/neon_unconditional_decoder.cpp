#include "neon_unconditional_decoder.h"

#include <cerfengine/engine.h>
#include "arm_neon_2reg_unary_decoder.h"
#include "arm_neon_2regscalar_decoder.h"
#include "arm_neon_3difflen_decoder.h"
#include "arm_neon_3regsame_decoder.h"
#include "arm_neon_loadstore_decoder.h"
#include "arm_neon_shift_imm_decoder.h"
#include "arm_opcode.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>

namespace cerfengine {

CERFENGINE_REGISTER(NeonUnconditionalDecoder);

bool NeonUnconditionalDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void NeonUnconditionalDecoder::OnReady() {
    loadstore_decoder_     = &engine_.Get<ArmNeonLoadStoreDecoder>();
    three_regsame_decoder_ = &engine_.Get<ArmNeon3RegSameDecoder>();
    shift_imm_decoder_     = &engine_.Get<ArmNeonShiftImmDecoder>();
    three_difflen_decoder_ = &engine_.Get<ArmNeon3DiffLenDecoder>();
    two_regscalar_decoder_ = &engine_.Get<ArmNeon2RegScalarDecoder>();
    two_reg_unary_decoder_ = &engine_.Get<ArmNeon2RegUnaryDecoder>();
}

bool NeonUnconditionalDecoder::DecodeLoadStore(DecodedInsn* insn, ArmOpcode op) {
    return loadstore_decoder_->Decode(insn, op);
}

bool NeonUnconditionalDecoder::DecodeData3reg(DecodedInsn* insn, ArmOpcode op) {
    if (op.neon_data_3reg.bit23 == 0u) {
        if (three_regsame_decoder_->Decode(insn, op)) {
            return true;
        }
    } else if (op.neon_data_3reg.c == 1u) {
        if (shift_imm_decoder_->Decode(insn, op)) {
            return true;
        }
    } else {
        const uint32_t size_bits = (op.word >> 20) & 0x3u;
        if (size_bits == 0x3u) {
            if (op.neon_data_3reg.u == 0u) {
                /* VEXT (A8.8.316). */
                insn->cond      = 14;
                insn->immediate = op.word;
                insn->place_fn  = &PlaceNeonDataVext;
                return true;
            }
            if ((op.neon_data_3reg.opc & 0xCu) == 0x8u) {
                insn->cond      = 14;
                insn->immediate = op.word;
                insn->place_fn  = &PlaceNeonDataVtbl;
                return true;
            }
            if ((op.word & 0x00000F90u) == 0x00000C00u) {
                insn->cond = 14;
                insn->immediate = op.word;
                insn->place_fn = &EmitNeonVdupScalar;
                return true;
            }
            if (two_reg_unary_decoder_->Decode(insn, op)) {
                return true;
            }
        } else {
            const uint32_t bit6 = (op.word >> 6) & 1u;
            if (bit6 == 0u) {
                if (three_difflen_decoder_->Decode(insn, op)) {
                    return true;
                }
            } else {
                if (two_regscalar_decoder_->Decode(insn, op)) {
                    return true;
                }
            }
        }
    }
    insn->cond      = 14;
    insn->immediate = op.word;
    insn->place_fn  = &PlaceNeonUnimplemented;
    return true;
}

}
