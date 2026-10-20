#pragma once

#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/engine.h>

#include <cerfengine/force_inline.h>
#include "arm_opcode.h"

namespace cerfengine {

/* Decoder for the Table A5-15 load/store word and unsigned byte space
   (ARM DDI 0406C.c, p. A5-208). */
class ArmSingleDataSpaceDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;

    CERFENGINE_FORCEINLINE bool Decode(DecodedInsn* insn, ArmOpcode op) {
        insn->p  = (op.word >> 24) & 0x1u;
        insn->u  = (op.word >> 23) & 0x1u;
        insn->s  = (op.word >> 22) & 0x1u;
        insn->w  = (op.word >> 21) & 0x1u;
        insn->l  = (op.word >> 20) & 0x1u;
        insn->rn = (op.word >> 16) & 0xFu;
        insn->rd = (op.word >> 12) & 0xFu;
        if (((op.word >> 25) & 0x1u) != 0u) {
            insn->n   = 0u;
            insn->rm  = op.word & 0xFu;
            insn->op1 = (op.word >> 5) & 0x3u;
            insn->rs  = (op.word >> 7) & 0x1Fu;
        } else {
            insn->n = 1u;
            const uint32_t imm12 = op.word & 0xFFFu;
            insn->offset = insn->u != 0u ? static_cast<int32_t>(imm12)
                                         : -static_cast<int32_t>(imm12);
        }
        /* Table A5-15 (p. A5-208) op1 rows 0x010 / 0x011 / 0x110 / 0x111. */
        insn->unpriv = (insn->p == 0u && insn->w != 0u) ? 1u : 0u;
        insn->r15_modified =
            insn->l != 0u && insn->rd == 15u && insn->s == 0u;
        insn->kind = ArmInsnKind::kSingleDataTransfer;
        return true;
    }
};

}
