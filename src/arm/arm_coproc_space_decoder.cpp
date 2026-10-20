#include "arm_coproc_space_decoder.h"

#include <cerfengine/engine.h>
#include "arm_opcode.h"
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/block_context.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmCoprocSpaceDecoder);

bool ArmCoprocSpaceDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void ArmCoprocSpaceDecoder::OnReady() {
    processor_config_ = &engine_.GetSetup().arm;
}

bool ArmCoprocSpaceDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    const uint32_t word   = op.word;
    const uint32_t op1    = (word >> 20) & 0x3Fu;
    const uint32_t cp_num = (word >> 8) & 0xFu;

    if ((op1 & 0x30u) == 0x30u) {
        insn->r15_modified = true;
        insn->kind         = ArmInsnKind::kSvc;
        return true;
    }
    if ((op1 & 0x3Eu) == 0x00u) {
        return false;
    }
    if ((op1 & 0x3Eu) == 0x04u) {
        if (!processor_config_->dsp) {
            return false;
        }
        const uint32_t l   = op1 & 0x1u;
        const uint32_t rt  = (word >> 12) & 0xFu;
        const uint32_t rt2 = (word >> 16) & 0xFu;
        if (rt == ArmGpr::kR15 || rt2 == ArmGpr::kR15) {
            return false;
        }
        if (l != 0u && rt == rt2) {
            return false;
        }
        insn->l        = l;
        insn->crd      = rt;
        insn->rn       = rt2;
        insn->cp_num   = cp_num;
        insn->offset   = static_cast<int32_t>(word & 0xFFu);
        insn->kind     = ArmInsnKind::kCoprocExtension;
        return true;
    }
    if ((op1 & 0x20u) == 0u) {
        const uint32_t rn = (word >> 16) & 0xFu;
        const uint32_t w  = (word >> 21) & 0x1u;
        if (rn == ArmGpr::kR15 && w != 0u) {
            return false;
        }
        const int32_t imm32 = static_cast<int32_t>((word & 0xFFu) << 2);
        insn->l        = op1 & 0x1u;
        insn->p        = (word >> 24) & 0x1u;
        insn->u        = (word >> 23) & 0x1u;
        insn->n        = (word >> 22) & 0x1u;
        insn->w        = w;
        insn->rn       = rn;
        insn->crd      = (word >> 12) & 0xFu;
        insn->cp_num   = cp_num;
        insn->offset   = insn->u != 0u ? imm32 : -imm32;
        insn->kind     = ArmInsnKind::kCoprocDataTransfer;
        return true;
    }
    insn->cp_num = cp_num;
    insn->crn    = (word >> 16) & 0xFu;
    insn->crm    = word & 0xFu;
    insn->cp     = (word >> 5) & 0x7u;
    if (((word >> 4) & 0x1u) == 0u) {
        insn->cp_opc   = (word >> 20) & 0xFu;
        insn->crd      = (word >> 12) & 0xFu;
        insn->kind     = ArmInsnKind::kCoprocDataOperation;
        return true;
    }
    const uint32_t l  = op1 & 0x1u;
    const uint32_t rt = (word >> 12) & 0xFu;
    if (l == 0u && rt == ArmGpr::kR15) {
        return false;
    }
    if (l != 0u && rt == ArmGpr::kR15) {
        if (cp_num == 15u) {
            return false;
        }
        const bool dbgdscr_int = ((word >> 21) & 0x7u) == 0u &&
                                 ((word >> 16) & 0xFu) == 0u &&
                                 (word & 0xFu) == 1u &&
                                 ((word >> 5) & 0x7u) == 0u;
        if (cp_num == 14u && !dbgdscr_int) {
            return false;
        }
    }
    insn->l        = l;
    insn->cp_opc   = (word >> 21) & 0x7u;
    insn->rd       = rt;
    /* DDI 0406C.c B4.2.6 (p. B4-1753): "The deprecated CP15 c7 encoding for an
       Instruction Synchronization Barrier is an MCR instruction with <opc1> set
       to 0, <CRn> set to c7, <CRm> set to c5, and <opc2> set to 4." */
    insn->context_sync = l == 0u && cp_num == 15u && insn->cp_opc == 0u &&
                         insn->crn == 7u && insn->crm == 5u && insn->cp == 4u;
    insn->kind = ArmInsnKind::kCoprocRegisterTransfer;
    return true;
}

}
