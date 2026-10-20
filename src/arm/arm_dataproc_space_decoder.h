#pragma once

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/engine.h>

#include <cerfengine/force_inline.h>
#include "arm_expand_imm.h"
#include "arm_multiply_space_decoder.h"
#include "arm_opcode.h"

namespace cerfengine {

/* Decoder for the Table A5-2 data-processing and miscellaneous space
   (ARM DDI 0406C.c, p. A5-196), including its Table A5-12 synchronization
   and Table A5-10/A5-11 extra load/store sub-spaces. */
class ArmDataprocSpaceDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    /* DDI 0406C.c Table A5-2 (p. A5-196): op = insn[25], op1 = insn[24:20],
       op2 = insn[7:4]. */
    CERFENGINE_FORCEINLINE bool Decode(DecodedInsn* insn, ArmOpcode op) {
        const uint32_t op1      = (op.word >> 20) & 0x1Fu;
        const uint32_t op2      = (op.word >>  4) & 0xFu;
        const bool     misc_row = (op1 & 0x19u) == 0x10u;

        if (((op.word >> 25) & 0x1u) == 0u) {
            if (op2 == 0x9u) {
                if ((op1 & 0x10u) == 0u) {
                    return multiply_decoder_->Decode(insn, op);
                }
                return DecodeSyncSpace(insn, op);
            }
            if ((op2 & 0x9u) == 0x9u) {
                return DecodeExtraLoadStore(insn, op);
            }
            if (misc_row) {
                if ((op2 & 0x8u) == 0u) {
                    return DecodeMiscSpace(insn, op);
                }
                return multiply_decoder_->DecodeHalfword(insn, op);
            }
            /* DDI 0406C.c Table A5-2 (p. A5-196): op2 xxx0 selects
               Data-processing (register), op2 0xx1 register-shifted
               register. */
            if ((op2 & 0x1u) == 0u) {
                return DecodeDataProcessingReg(insn, op);
            }
            return DecodeDataProcessingShiftedReg(insn, op);
        }

        if (!misc_row) {
            return DecodeDataProcessingImm(insn, op);
        }
        if (op1 == 0x10u || op1 == 0x14u) {
            return DecodeMovwMovt(insn, op, op1);
        }
        return DecodeMsrImmHints(insn, op);
    }

private:
    bool DecodeMovwMovt(DecodedInsn* insn, ArmOpcode op, uint32_t op1);

    CERFENGINE_FORCEINLINE bool DecodeDataProcessingImm(DecodedInsn* insn, ArmOpcode op) {
        const uint32_t opcode = (op.word >> 21) & 0xFu;
        const uint32_t s      = (op.word >> 20) & 0x1u;
        const uint32_t rn     = (op.word >> 16) & 0xFu;
        const uint32_t rd     = (op.word >> 12) & 0xFu;
        const uint32_t rot2   = ((op.word >> 8) & 0xFu) * 2u;
        const uint32_t imm32  = ArmExpandImm(op.word & 0xFFFu);

        const bool is_test = opcode >= 8u && opcode <= 11u;
        const bool is_move = opcode == 13u || opcode == 15u;
        if (is_move && rn != 0u) {
            return false;
        }
        if (is_test && rd != 0u) {
            return false;
        }

        insn->op1       = opcode;
        insn->s         = s;
        insn->rn        = rn;
        insn->rd        = rd;
        insn->rs        = rot2;
        insn->immediate = imm32;
        if (rd == ArmGpr::kR15 && !is_test) {
            insn->r15_modified = true;
            if (s != 0u) {
                insn->is_exception_return = true;
            }
        }
        insn->kind = ArmInsnKind::kDataProcessing;
        return true;
    }

    CERFENGINE_FORCEINLINE bool DecodeDataProcessingReg(DecodedInsn* insn, ArmOpcode op) {
        const uint32_t opcode = (op.word >> 21) & 0xFu;
        const uint32_t s      = (op.word >> 20) & 0x1u;
        const uint32_t rn     = (op.word >> 16) & 0xFu;
        const uint32_t rd     = (op.word >> 12) & 0xFu;
        const uint32_t imm5   = (op.word >>  7) & 0x1Fu;
        const uint32_t type   = (op.word >>  5) & 0x3u;
        const uint32_t rm     =  op.word        & 0xFu;

        const bool is_test = opcode >= 8u && opcode <= 11u;
        const bool is_move = opcode == 13u || opcode == 15u;
        if (is_move && rn != 0u) {
            return false;
        }
        if (is_test && rd != 0u) {
            return false;
        }

        uint32_t shift_t = 0u;
        uint32_t shift_n = 0u;
        DecodeImmShift(type, imm5, &shift_t, &shift_n);

        insn->op1 = opcode;
        insn->s   = s;
        insn->rn  = rn;
        insn->rd  = rd;
        insn->rm  = rm;
        insn->n   = shift_t;
        insn->rs  = shift_n;
        if (rd == ArmGpr::kR15 && !is_test) {
            insn->r15_modified = true;
            if (s != 0u) {
                insn->is_exception_return = true;
            }
        }
        insn->kind = ArmInsnKind::kDataProcessingReg;
        return true;
    }

    bool DecodeDataProcessingShiftedReg(DecodedInsn* insn, ArmOpcode op);
    bool DecodeMiscSpace(DecodedInsn* insn, ArmOpcode op);
    bool DecodeSyncSpace(DecodedInsn* insn, ArmOpcode op);
    bool DecodeLdrexStrex(DecodedInsn* insn, ArmOpcode op);
    bool DecodeExtraLoadStore(DecodedInsn* insn, ArmOpcode op);
    bool DecodeMsrImmHints(DecodedInsn* insn, ArmOpcode op);

    ArmMultiplySpaceDecoder* multiply_decoder_ = nullptr;
    const cerfengine::ArmFeatures*      processor_config_ = nullptr;
};

}
