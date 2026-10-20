#include "arm_branch_block_space_decoder.h"

#include <cerfengine/engine.h>
#include "arm_opcode.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/block_context.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmBranchBlockSpaceDecoder);

bool ArmBranchBlockSpaceDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

bool ArmBranchBlockSpaceDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    if (((op.word >> 25) & 0x1u) != 0u) {
        /* A8.8.18 B / A8.8.25 BL encoding A1 (pp. A8-334/A8-348):
           imm32 = SignExtend(imm24:'00', 32), L = insn[24]. */
        insn->l            = (op.word >> 24) & 0x1u;
        insn->offset       = static_cast<int32_t>(op.word << 8) >> 6;
        insn->r15_modified = true;
        insn->kind         = ArmInsnKind::kBranch;
        return true;
    }
    insn->p             = (op.word >> 24) & 0x1u;
    insn->u             = (op.word >> 23) & 0x1u;
    insn->s             = (op.word >> 22) & 0x1u;
    insn->w             = (op.word >> 21) & 0x1u;
    insn->l             = (op.word >> 20) & 0x1u;
    insn->rn            = (op.word >> 16) & 0xFu;
    insn->register_list = static_cast<uint16_t>(op.word & 0xFFFFu);
    const bool pc_load  = insn->l != 0u &&
                          (insn->register_list & 0x8000u) != 0u;
    insn->r15_modified        = pc_load;
    insn->is_exception_return = pc_load && insn->s != 0u;
    insn->kind                = ArmInsnKind::kBlockDataTransfer;
    return true;
}

}
