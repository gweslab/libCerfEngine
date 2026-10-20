#include "arm_unconditional_space_decoder.h"

#include <cerfengine/engine.h>
#include "arm_opcode.h"
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/decoded_insn.h>
#include "neon_unconditional_decoder.h"
#include "arm_mark_unimplemented.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmUnconditionalSpaceDecoder);

bool ArmUnconditionalSpaceDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void ArmUnconditionalSpaceDecoder::OnReady() {
    processor_config_           = &engine_.GetSetup().arm;
    neon_unconditional_decoder_ = &engine_.Get<NeonUnconditionalDecoder>();
}

bool ArmUnconditionalSpaceDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    if (processor_config_->arch_v6 &&
        (op.word & 0xFE50FFFFu) == 0xF8100A00u) {
        const uint32_t rn = (op.word >> 16) & 0xFu;
        if (rn == ArmGpr::kR15) {
            return false;
        }
        insn->kind                = ArmInsnKind::kRfe;
        insn->p                   = (op.word >> 24) & 0x1u;
        insn->u                   = (op.word >> 23) & 0x1u;
        insn->w                   = (op.word >> 21) & 0x1u;
        insn->rn                  = rn;
        insn->r15_modified        = true;
        insn->is_exception_return = 1;
        return true;
    }

    /* SRS - Store Return State. ddi0406c B9.3.16 encoding A1:
       1111 100P U1W0 1101 0000 0101 000 mode[4:0]. */
    if (processor_config_->arch_v6 &&
        (op.word & 0xFE5FFFE0u) == 0xF84D0500u) {
        insn->kind      = ArmInsnKind::kSrs;
        insn->p         = (op.word >> 24) & 0x1u;
        insn->u         = (op.word >> 23) & 0x1u;
        insn->w         = (op.word >> 21) & 0x1u;
        insn->immediate = op.word & 0x1Fu;
        return true;
    }

    /* DSB / DMB / ISB, ddi0406c A8.8.44 / A8.8.43 / A8.8.53 encoding A1:
       1111 0101 0111 (1)x4 (1)x4 (0)x4 01xx option, bits [7:4] = 0100 DSB /
       0101 DMB / 0110 ISB. */
    if (processor_config_->barrier_insn &&
        (op.word & 0xFFFFFF00u) == 0xF57FF000u) {
        const uint32_t barrier_op = (op.word >> 4) & 0xFu;
        if (barrier_op >= 0x4u && barrier_op <= 0x6u) {
            /* DDI 0406C.c Glossary, "Context synchronization operation": one
               of "Performing an ISB operation ... Taking an exception ...
               Returning from an exception". DSB and DMB are not in that set. */
            insn->context_sync = barrier_op == 0x6u;
            insn->kind         = ArmInsnKind::kNop;
            return true;
        }
    }

    const uint32_t op1 = (op.word >> 20) & 0xFFu;

    /* DDI 0406C.c Table A5-24 (p. A5-217): the Advanced SIMD element and
       structure load/store row is 100xxx0 over op1 = insn[26:20], so
       insn[20] == 1 selects the PLI and memory-hint rows. */
    if (processor_config_->neon && (op1 & 0xF1u) == 0x40u) {
        return neon_unconditional_decoder_->DecodeLoadStore(insn, op);
    }

    if (processor_config_->neon && op.neon_data_3reg.marker == 0x1u) {
        return neon_unconditional_decoder_->DecodeData3reg(insn, op);
    }

    if (processor_config_->preload &&
        ((op1 & 0xF7u) == 0x55u ||
         ((op1 & 0xF7u) == 0x75u && (op.word & 0x10u) == 0u))) {
        insn->kind = ArmInsnKind::kNop;
        return true;
    }

    if ((op1 & 0xE0u) == 0xC0u || (op1 & 0xF0u) == 0xE0u) {
        return false;
    }
    /* DDI 0406C.c Table A5-23 (p. A5-216) row 101xxxxx is "Branch with Link
       and Exchange"; A8.8.25 encoding A2 (p. A8-348) is 1111 101H imm24 with
       imm32 = SignExtend(imm24:H:'0', 32). */
    if ((op1 & 0xE0u) == 0xA0u) {
        insn->offset       = (static_cast<int32_t>(op.word << 8) >> 6) |
                             static_cast<int32_t>((op.word >> 23) & 2u);
        insn->r15_modified = true;
        insn->kind         = ArmInsnKind::kArmBlxImm;
        return true;
    }
    if ((op1 & 0x80u) == 0x00u) {
        return MarkArmUnimplemented(insn, op.word);
    }
    return false;
}

}
