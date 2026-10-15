#include "thumb32_decoder.h"

#include <cerfengine/engine.h>
#include "arm_coproc_space_decoder.h"
#include "arm_opcode.h"
#include <cerfengine/arm/decoded_insn.h>
#include "neon_unconditional_decoder.h"
#include <cerfengine/arm/place_fns.h>
#include "thumb32_branch_system_decoder.h"
#include "thumb32_data_proc_decoder.h"
#include "thumb32_fatal.h"
#include "thumb32_load_byte_decoder.h"
#include "thumb32_load_halfword_decoder.h"
#include "thumb32_load_store_decoder.h"
#include "thumb32_load_store_dual_decoder.h"
#include "thumb32_load_store_multiple_decoder.h"
#include "thumb32_long_multiply_decoder.h"
#include "thumb32_multiply_decoder.h"
#include "thumb32_plain_imm_decoder.h"

namespace cerfengine {

CERFENGINE_REGISTER(Thumb32Decoder);

bool Thumb32Decoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void Thumb32Decoder::OnReady() {
    has_thumb2_     = engine_.GetSetup().arm.thumb2;
    has_neon_       = engine_.GetSetup().arm.neon;
    coproc_decoder_ = &engine_.Get<ArmCoprocSpaceDecoder>();
    neon_decoder_   = &engine_.Get<NeonUnconditionalDecoder>();
    branch_system_  = &engine_.Get<Thumb32BranchSystemDecoder>();
    data_proc_      = &engine_.Get<Thumb32DataProcDecoder>();
    fatal_          = &engine_.Get<Thumb32Fatal>();
    load_byte_      = &engine_.Get<Thumb32LoadByteDecoder>();
    load_halfword_  = &engine_.Get<Thumb32LoadHalfwordDecoder>();
    load_store_     = &engine_.Get<Thumb32LoadStoreDecoder>();
    long_multiply_  = &engine_.Get<Thumb32LongMultiplyDecoder>();
    multiply_       = &engine_.Get<Thumb32MultiplyDecoder>();
    plain_imm_      = &engine_.Get<Thumb32PlainImmDecoder>();

    load_store_dual_     = &engine_.Get<Thumb32LoadStoreDualDecoder>();
    load_store_multiple_ = &engine_.Get<Thumb32LoadStoreMultipleDecoder>();
}

bool Thumb32Decoder::DecodeLoadStoreMultiple(DecodedInsn* insn, uint32_t op) {
    return load_store_multiple_->Decode(insn, op);
}

bool Thumb32Decoder::DecodeLoadStoreDualExclusiveTableBranch(DecodedInsn* insn,
                                                             uint32_t op) {
    return load_store_dual_->Decode(insn, op);
}

/* DDI 0406C.c Table A6-30 (A6.3.18) p. A6-251, Table A5-22 (A5.6) p. A5-215;
   the U bit position per A7.4 p. A7-261. */
bool Thumb32Decoder::DecodeCoprocessorSimdFp(DecodedInsn* insn, uint32_t op) {
    const uint32_t op1 = (op >> 20) & 0x3Fu;
    ArmOpcode      arm{};
    if ((op1 & 0x30u) == 0x30u) {
        if (!has_neon_) return false;
        arm.word = 0xF2000000u | (((op >> 28) & 0x1u) << 24) |
                   (op & 0x00FFFFFFu);
        neon_decoder_->DecodeData3reg(insn, arm);
        if (insn->place_fn == &PlaceNeonUnimplemented) insn->immediate = op;
        return true;
    }
    /* T is hw1[12]. A7.5 p. A7-272, A7.6 p. A7-274, A7.8 p. A7-278, A7.9
       p. A7-279, B3.15.2 p. B3-1446, A2.9 p. A2-94, B1.9.2 p. B1-1206. */
    if (((op >> 28) & 0x1u) != 0u) {
        return false;
    }
    /* A8.8.98 MCR p. A8-476, A8.8.107 MRC p. A8-492, A8.8.99 MCRR p. A8-478,
       A8.8.108 MRRC p. A8-494, A8.8.314 VDUP p. A8-886, A8.8.341-345 VMOV
       pp. A8-940/942/944/946/948; Rt at bits[15:12], Rt2 at bits[19:16]. */
    if ((op1 & 0x3Eu) == 0x04u) {
        if (((op >> 12) & 0xFu) == 13u || ((op >> 16) & 0xFu) == 13u) {
            return false;
        }
    } else if ((op1 & 0x30u) == 0x20u && ((op >> 4) & 0x1u) != 0u &&
               ((op >> 12) & 0xFu) == 13u) {
        return false;
    }

    arm.word = op;
    return coproc_decoder_->Decode(insn, arm);
}

bool Thumb32Decoder::DecodeStoreSingleDataItem(DecodedInsn* insn, uint32_t op) {
    return load_store_->DecodeStoreSingleDataItem(insn, op);
}

bool Thumb32Decoder::DecodeLoadByteMemoryHints(DecodedInsn* insn, uint32_t op) {
    return load_byte_->DecodeLoadByteMemoryHints(insn, op);
}

bool Thumb32Decoder::DecodeLoadHalfwordMemoryHints(DecodedInsn* insn,
                                                   uint32_t op) {
    return load_halfword_->DecodeLoadHalfwordMemoryHints(insn, op);
}

bool Thumb32Decoder::DecodeLoadWord(DecodedInsn* insn, uint32_t op) {
    return load_store_->DecodeLoadWord(insn, op);
}

bool Thumb32Decoder::DecodeSimdElementOrStructure(DecodedInsn* insn,
                                                  uint32_t op) {
    if (!has_neon_) {
        return false;
    }
    ArmOpcode arm{};
    arm.word = 0xF4000000u | (op & 0x00FFFFFFu);
    if (!neon_decoder_->DecodeLoadStore(insn, arm)) {
        return false;
    }
    if (insn->place_fn == &PlaceNeonUnimplemented) insn->immediate = op;
    return true;
}

bool Thumb32Decoder::DecodeMultiplyAbsoluteDifference(DecodedInsn* insn,
                                                      uint32_t op) {
    return multiply_->DecodeMultiplyAbsoluteDifference(insn, op);
}

bool Thumb32Decoder::DecodeLongMultiplyDivide(DecodedInsn* insn, uint32_t op) {
    return long_multiply_->DecodeLongMultiplyDivide(insn, op);
}

/* ARM DDI 0406C.c Table A6-9 (A6.3, p. A6-230), keyed on op1 = bits[12:11] of
   the first halfword, op2 = bits[10:4] of the first halfword and op = bit[15]
   of the second halfword. */
bool Thumb32Decoder::DecodeThumb32(DecodedInsn* insn, uint32_t op) {
    insn->cond = 14u;

    const uint32_t op1 = (op >> 27) & 0x3u;
    const uint32_t op2 = (op >> 20) & 0x7Fu;
    const uint32_t o   = (op >> 15) & 0x1u;

    switch (op1) {
    case 0x1u:
        if ((op2 & 0x40u) != 0u) return DecodeCoprocessorSimdFp(insn, op);
        if ((op2 & 0x20u) != 0u) {
            return data_proc_->DecodeDataProcessingShiftedRegister(insn, op);
        }
        if ((op2 & 0x04u) != 0u) {
            return DecodeLoadStoreDualExclusiveTableBranch(insn, op);
        }
        return DecodeLoadStoreMultiple(insn, op);
    case 0x2u:
        if (o != 0u) return branch_system_->Decode(insn, op);
        if ((op2 & 0x20u) != 0u) {
            return plain_imm_->Decode(insn, op);
        }
        return data_proc_->DecodeDataProcessingModifiedImmediate(insn, op);
    case 0x3u:
        if ((op2 & 0x40u) != 0u) return DecodeCoprocessorSimdFp(insn, op);
        if ((op2 & 0x20u) != 0u) {
            if ((op2 & 0x10u) == 0u) {
                return data_proc_->DecodeDataProcessingRegister(insn, op);
            }
            return (op2 & 0x08u) != 0u
                       ? DecodeLongMultiplyDivide(insn, op)
                       : DecodeMultiplyAbsoluteDifference(insn, op);
        }
        if ((op2 & 0x01u) == 0u) {
            return (op2 & 0x10u) != 0u
                       ? DecodeSimdElementOrStructure(insn, op)
                       : DecodeStoreSingleDataItem(insn, op);
        }
        switch (op2 & 0x07u) {
        case 0x1u: return DecodeLoadByteMemoryHints(insn, op);
        case 0x3u: return DecodeLoadHalfwordMemoryHints(insn, op);
        case 0x5u: return DecodeLoadWord(insn, op);
        default:   return false;
        }
    default:
        fatal_->Unimplemented("op1 == 0b00 is a 16-bit encoding", insn, op);
    }
}

}
