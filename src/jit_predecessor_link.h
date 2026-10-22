#pragma once

#include <cstdint>

#include <cerfengine/jit_block.h>
#include <cerfengine/jit_block_index.h>

#include <cerfengine/force_inline.h>

#include "chain_jump_patch.h"

namespace cerfengine {

CERFENGINE_FORCEINLINE void LinkPredecessor(JitBlockIndex& index, uint32_t predecessor_start,
                                            JitBlock* dest) {
    if (dest->index_split != 0u) return;
    JitBlock* const src = index.FindExact(predecessor_start);
    if (src == nullptr || src == dest ||
        dest->phys_start != src->phys_start + (dest->guest_start - src->guest_start)) {
        return;
    }
    for (uint32_t s = 0; s < 2u; ++s) {
        if (src->chain_target[s] != nullptr || src->chain_site[s] == nullptr ||
            src->chain_pending_va[s] != dest->guest_start) {
            continue;
        }
        PatchChainJump(src->chain_site[s], dest->native_start);
        index.LinkChain(src, s, dest, src->chain_site[s], src->chain_fallback[s]);
    }
}

}
