#include "arm_neon_loadstore_decoder.h"

#include <cerfengine/engine.h>
#include "arm_opcode.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/block_context.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeonLoadStoreDecoder);

bool ArmNeonLoadStoreDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    if (op.neon_load_store.a == 0u) {
        const uint32_t type = op.neon_load_store.type;
        ArmInsnKind kind;
        if (type == 0x2u || type == 0x6u || type == 0x7u || type == 0xAu) {
            kind = ArmInsnKind::kNeonLoadStoreMultiple;
        } else if (type == 0x0u || type == 0x1u || type == 0x3u ||
                   type == 0x4u || type == 0x5u || type == 0x8u ||
                   type == 0x9u) {
            kind = ArmInsnKind::kNeonLoadStoreInterleaved;
        } else {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->kind      = ArmInsnKind::kUndefined;
            return true;
        }
        insn->kind     = kind;
        insn->cond     = 14;
        insn->rn       = op.neon_load_store.rn;
        insn->rm       = op.neon_load_store.rm;
        insn->crn      = op.neon_load_store.vd;
        insn->n        = op.neon_load_store.d;
        insn->l        = op.neon_load_store.l;
        insn->op1      = type;
        insn->cp       = op.neon_load_store.size;
        insn->crm      = op.neon_load_store.align;
        return true;
    }
    if (op.neon_load_store_single.size != 3u) {
        insn->kind     = ArmInsnKind::kNeonLoadStoreSingleLane;
        insn->cond     = 14;
        insn->rn       = op.neon_load_store_single.rn;
        insn->rm       = op.neon_load_store_single.rm;
        insn->crn      = op.neon_load_store_single.vd;
        insn->n        = op.neon_load_store_single.d;
        insn->l        = op.neon_load_store_single.l;
        insn->cp       = op.neon_load_store_single.size;
        insn->op1      = op.neon_load_store_single.n_minus1;
        insn->crm      = op.neon_load_store_single.index_align;
        return true;
    }
    insn->cond      = 14;
    insn->immediate = op.word;
    insn->kind      = op.neon_load_store_single.l == 0u ? ArmInsnKind::kUndefined
        : ArmInsnKind::kNeonLoadAllLanes;
    return true;
}

}
