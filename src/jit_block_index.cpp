#include <cerfengine/jit_block_index.h>

#include <algorithm>
#include <new>

#include <cerfengine/engine.h>

#include "chain_jump_patch.h"

namespace cerfengine {

void JitBlockIndex::Initialize(cerfengine::Engine& engine) {
    engine_ = &engine;
    table_.clear();
    count_ = 0;
}

void JitBlockIndex::SetCapacity(size_t slots) {
    table_.assign(slots, Slot{});
    mask_  = slots - 1u;
    shift_ = 32u;
    for (size_t s = slots; s > 1u; s >>= 1) --shift_;
}

void JitBlockIndex::Flush() {
    std::fill(table_.begin(), table_.end(), Slot{});
    count_ = 0;
}

void JitBlockIndex::Grow() {
    std::vector<Slot> old;
    old.swap(table_);
    SetCapacity(old.empty() ? kInitialSlots : old.size() * 2u);
    for (const Slot& s : old) {
        if (s.blk == nullptr) continue;
        size_t i = Bucket(s.key);
        while (table_[i].blk != nullptr) i = (i + 1u) & mask_;
        table_[i] = s;
    }
}

JitBlock* JitBlockIndex::PlaceOuterAt(uint8_t* slab, uint32_t guest_start, uint32_t guest_end,
                                      uint32_t phys_start, uint8_t* native_start) {
    JitBlock* stored = new (slab) JitBlock{.guest_start  = guest_start,
                                           .guest_end    = guest_end,
                                           .phys_start   = phys_start,
                                           .native_start = native_start};

    if ((count_ + 1u) * 4u > table_.size() * 3u) Grow();

    size_t i = Bucket(stored->guest_start);
    while (table_[i].blk != nullptr) {
        if (table_[i].key == stored->guest_start) {
            engine_->Panic("JitBlockIndex::PlaceOuterAt: duplicate guest_start "
                           "0x%08X (existing block was not evicted)",
                           stored->guest_start);
        }
        i = (i + 1u) & mask_;
    }
    table_[i].key = stored->guest_start;
    table_[i].blk = stored;
    ++count_;
    return stored;
}

JitBlock* JitBlockIndex::FindExact(uint32_t guest_start) {
    if (table_.empty()) return nullptr;
    size_t i = Bucket(guest_start);
    while (table_[i].blk != nullptr) {
        if (table_[i].key == guest_start) return table_[i].blk;
        i = (i + 1u) & mask_;
    }
    return nullptr;
}

void JitBlockIndex::EraseAt(size_t idx) {
    table_[idx] = Slot{};
    size_t i = idx;
    size_t j = idx;
    for (;;) {
        j = (j + 1u) & mask_;
        if (table_[j].blk == nullptr) break;
        const size_t k = Bucket(table_[j].key);
        if (i <= j) {
            if (i < k && k <= j) continue;
        } else {
            if (i < k || k <= j) continue;
        }
        table_[i] = table_[j];
        table_[j] = Slot{};
        i = j;
    }
    --count_;
}

void JitBlockIndex::LinkChain(JitBlock* src, uint32_t slot, JitBlock* dest,
                              uint8_t* site, uint8_t* fallback) {
    src->chain_site[slot]          = site;
    src->chain_fallback[slot]      = fallback;
    src->chain_target[slot]        = dest;
    src->chain_src_next[slot]      = dest->chain_src_head;
    src->chain_src_next_slot[slot] = dest->chain_src_head_slot;
    dest->chain_src_head           = src;
    dest->chain_src_head_slot      = static_cast<uint8_t>(slot);
}

void JitBlockIndex::RestoreFallbackJump(JitBlock* src, uint32_t slot) {
    PatchChainJump(src->chain_site[slot], src->chain_fallback[slot]);
}

void JitBlockIndex::DetachFromDest(JitBlock* block, uint32_t slot) {
    JitBlock* const dest = block->chain_target[slot];
    if (dest == nullptr) return;

    RestoreFallbackJump(block, slot);

    JitBlock* cur      = dest->chain_src_head;
    uint8_t   cur_slot = dest->chain_src_head_slot;
    if (cur == block && cur_slot == slot) {
        dest->chain_src_head      = block->chain_src_next[slot];
        dest->chain_src_head_slot = block->chain_src_next_slot[slot];
    } else {
        while (cur != nullptr) {
            JitBlock* const nxt      = cur->chain_src_next[cur_slot];
            const uint8_t   nxt_slot = cur->chain_src_next_slot[cur_slot];
            if (nxt == block && nxt_slot == slot) {
                cur->chain_src_next[cur_slot]      = block->chain_src_next[slot];
                cur->chain_src_next_slot[cur_slot] = block->chain_src_next_slot[slot];
                break;
            }
            cur      = nxt;
            cur_slot = nxt_slot;
        }
    }
    block->chain_target[slot]   = nullptr;
    block->chain_src_next[slot] = nullptr;
}

void JitBlockIndex::UnlinkChains(JitBlock* block) {
    DetachFromDest(block, 0u);
    DetachFromDest(block, 1u);

    JitBlock* src      = block->chain_src_head;
    uint8_t   src_slot = block->chain_src_head_slot;
    while (src != nullptr) {
        JitBlock* const nxt      = src->chain_src_next[src_slot];
        const uint8_t   nxt_slot = src->chain_src_next_slot[src_slot];
        RestoreFallbackJump(src, src_slot);
        src->chain_target[src_slot]   = nullptr;
        src->chain_src_next[src_slot] = nullptr;
        src      = nxt;
        src_slot = nxt_slot;
    }
    block->chain_src_head = nullptr;
}

void JitBlockIndex::RemoveNode(JitBlock* block, ClearJumpCacheFn clear_jc,
                               void* ctx) {
    size_t idx   = 0;
    bool   found = false;
    if (!table_.empty()) {
        idx = Bucket(block->guest_start);
        while (table_[idx].blk != nullptr) {
            if (table_[idx].key == block->guest_start) {
                found = true;
                break;
            }
            idx = (idx + 1u) & mask_;
        }
    }
    if (!found || table_[idx].blk != block) {
        engine_->Panic("JitBlockIndex::RemoveNode: block 0x%08X..0x%08X is not "
                       "the indexed record for its guest_start",
                       block->guest_start, block->guest_end);
    }
    UnlinkChains(block);
    clear_jc(block->guest_start, ctx);
    EraseAt(idx);
}

}
