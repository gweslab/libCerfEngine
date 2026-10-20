#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "jit_block_index.h"

namespace cerfengine {

constexpr uint32_t kJumpCacheSize = 4096;

constexpr uint32_t kBlockUnindexed = 0xFFFFFFFFu;

constexpr uint32_t kBlockPrivileged     = 0;
constexpr uint32_t kBlockUser           = 1;
constexpr uint32_t kBlockSupervisor     = 2;
constexpr uint32_t kBlockPrivilegeCount = 3;

constexpr uint32_t kJumpCacheNoKey = 0xFFFFFFFFu;

struct JumpCacheEntry {
    uint32_t key[kBlockPrivilegeCount];
    void*    native;
};

constexpr int32_t JumpCacheKeyDisp(uint32_t privilege) {
    return static_cast<int32_t>(offsetof(JumpCacheEntry, key) + privilege * sizeof(uint32_t));
}

struct BlockIndexSet {
    JitBlockIndex global;
    JitBlockIndex per_asid[256];
    uint32_t      asid_populated[8] = {};

    JitBlockIndex& Index(bool global_page, uint8_t asid) {
        return global_page ? global : per_asid[asid];
    }

    void MarkPopulated(uint8_t asid) {
        asid_populated[asid >> 5] |= (1u << (asid & 31u));
    }

    void Initialize(cerfengine::Engine& engine) {
        global.Initialize(engine);
        for (auto& t : per_asid) t.Initialize(engine);
    }

    void Flush() {
        global.Flush();
        for (uint32_t w = 0; w < 8u; ++w) {
            uint32_t bits = asid_populated[w];
            if (!bits) continue;
            for (uint32_t b = 0; b < 32u; ++b) {
                if (bits & (1u << b)) per_asid[(w << 5) + b].Flush();
            }
            asid_populated[w] = 0;
        }
    }
};

struct IsaBlockSpace {
    BlockIndexSet  indices[kBlockPrivilegeCount];
    JumpCacheEntry jump_cache[kJumpCacheSize];
    JitBlock*      jump_cache_blk[kJumpCacheSize];

    std::vector<JitBlock*> page_heads;
    uint32_t               page_base  = 0;
    uint32_t               page_count = 0;

    static uint32_t JumpCacheIndex(uint32_t folded_va) {
        return (folded_va >> 2) & (kJumpCacheSize - 1u);
    }

    static uint32_t JumpCacheLiveKey(const JumpCacheEntry& e) {
        for (uint32_t key : e.key) {
            if (key != kJumpCacheNoKey) return key;
        }
        return kJumpCacheNoKey;
    }

    void JumpCacheClearEntry(uint32_t index) {
        JumpCacheEntry& e = jump_cache[index];
        for (uint32_t& key : e.key) key = 0;
        e.native              = nullptr;
        jump_cache_blk[index] = nullptr;
    }

    void JumpCacheFlush() {
        std::memset(jump_cache, 0, sizeof(jump_cache));
        std::memset(jump_cache_blk, 0, sizeof(jump_cache_blk));
    }

    void JumpCacheClearPage(uint32_t page_va) {
        const uint32_t base = page_va & 0xFFFFF000u;
        for (uint32_t off = 0; off < 0x1000u; off += 4u) {
            const uint32_t index = JumpCacheIndex(base + off);
            if ((JumpCacheLiveKey(jump_cache[index]) & 0xFFFFF000u) == base) {
                JumpCacheClearEntry(index);
            }
        }
    }

    void JumpCacheClearRange(uint32_t base_va, uint32_t span_bytes);

    void* JumpCacheLookup(uint32_t privilege, uint32_t folded_va) const {
        const JumpCacheEntry& e = jump_cache[JumpCacheIndex(folded_va)];
        return e.key[privilege] == folded_va ? e.native : nullptr;
    }

    const JumpCacheEntry* JumpCacheProbe(uint32_t privilege, uint32_t folded_va) const {
        const JumpCacheEntry& e = jump_cache[JumpCacheIndex(folded_va)];
        return e.key[privilege] == folded_va ? &e : nullptr;
    }

    JitBlock* JumpCacheBlock(const JumpCacheEntry* e) const {
        return jump_cache_blk[e - jump_cache];
    }

    void JumpCacheInsert(uint32_t privilege, uint32_t folded_va, void* native, JitBlock* blk) {
        const uint32_t  index = JumpCacheIndex(folded_va);
        static_assert(kBlockPrivilegeCount == 3u);
        JumpCacheEntry& e     = jump_cache[index];
        e.key[0]              = kJumpCacheNoKey;
        e.key[1]              = kJumpCacheNoKey;
        e.key[2]              = kJumpCacheNoKey;
        e.key[privilege]      = folded_va;
        e.native              = native;
        jump_cache_blk[index] = blk;
    }

    void Initialize(cerfengine::Engine& engine, uint32_t dram_page_base,
                    uint32_t dram_page_count) {
        for (BlockIndexSet& set : indices) set.Initialize(engine);
        JumpCacheFlush();
        page_base  = dram_page_base;
        page_count = dram_page_count;
        page_heads.assign(dram_page_count, nullptr);
    }
    static void ClearJcSlot(uint32_t folded_va, void* ctx) {
        auto* const    sp    = static_cast<IsaBlockSpace*>(ctx);
        const uint32_t index = JumpCacheIndex(folded_va);
        if (JumpCacheLiveKey(sp->jump_cache[index]) == folded_va) sp->JumpCacheClearEntry(index);
    }

    void IndexInsert(JitBlock* outer, JitBlockIndex* owner, uint32_t index_start,
                     uint32_t index_split = 0,
                     uint32_t index_start2 = kBlockUnindexed) {
        outer->owner        = owner;
        outer->page_next[0] = nullptr;
        outer->page_next[1] = nullptr;
        outer->index_start  = index_start;
        outer->index_split  = index_split;
        outer->index_start2 = index_start2;
        LinkIntoPage(outer, 0, index_start >> 12);
        if (index_split != 0 && (index_start2 >> 12) != (index_start >> 12)) {
            LinkIntoPage(outer, 1, index_start2 >> 12);
        }
    }

    void LinkIntoPage(JitBlock* outer, int slot, uint32_t pg) {
        if (pg >= page_base && pg < page_base + page_count) {
            JitBlock*& head = page_heads[pg - page_base];
            outer->page_next[slot] = head;
            head = outer;
        }
    }

    static int LinkSlot(const JitBlock* blk, uint32_t pg) {
        return (blk->index_start >> 12) == pg ? 0 : 1;
    }

    static bool RangesOverlap(uint32_t a_lo, uint32_t a_hi, uint32_t b_lo,
                              uint32_t b_hi) {
        return a_hi >= b_lo && a_lo <= b_hi;
    }

    static bool IntersectsBlock(const JitBlock* blk, uint32_t lo, uint32_t hi) {
        const uint32_t span = blk->guest_end - blk->guest_start;
        if (blk->index_split == 0) {
            return RangesOverlap(blk->index_start, blk->index_start + span, lo, hi);
        }
        return RangesOverlap(blk->index_start,
                             blk->index_start + blk->index_split - 1u, lo, hi) ||
               RangesOverlap(blk->index_start2,
                             blk->index_start2 + (span - blk->index_split), lo, hi);
    }

    bool PageHasBlocks(uint32_t index_addr) const {
        const uint32_t pg = index_addr >> 12;
        if (pg < page_base || pg >= page_base + page_count) return false;
        return page_heads[pg - page_base] != nullptr;
    }

    void UnlinkFromPage(JitBlock* outer, int slot) {
        if (slot == 1 &&
            (outer->index_split == 0 ||
             (outer->index_start2 >> 12) == (outer->index_start >> 12))) {
            return;
        }
        const uint32_t pg =
            (slot == 0 ? outer->index_start : outer->index_start2) >> 12;
        if (pg < page_base || pg >= page_base + page_count) return;
        JitBlock** pp = &page_heads[pg - page_base];
        while (JitBlock* b = *pp) {
            if (b == outer) { *pp = b->page_next[slot]; return; }
            pp = &b->page_next[LinkSlot(b, pg)];
        }
    }
    void UnlinkPage(JitBlock* outer) {
        UnlinkFromPage(outer, 0);
        UnlinkFromPage(outer, 1);
    }
    uint32_t RemoveRange(uint32_t lo, uint32_t hi) {
        uint32_t removed = 0;
        const uint32_t pg_lo = lo >> 12;
        const uint32_t pg_hi = hi >> 12;
        for (uint32_t pg = pg_lo; pg <= pg_hi; ++pg) {
            if (pg < page_base || pg >= page_base + page_count) continue;
            JitBlock** pp = &page_heads[pg - page_base];
            while (JitBlock* blk = *pp) {
                const int n = LinkSlot(blk, pg);
                if (!IntersectsBlock(blk, lo, hi)) {
                    pp = &blk->page_next[n];
                } else {
                    *pp = blk->page_next[n];
                    UnlinkFromPage(blk, 1 - n);
                    blk->owner->RemoveNode(blk, &ClearJcSlot, this);
                    ++removed;
                }
            }
        }
        return removed;
    }

    void RemoveBlock(JitBlock* outer) {
        UnlinkPage(outer);
        outer->owner->RemoveNode(outer, &ClearJcSlot, this);
    }
    void FlushAll() {
        for (BlockIndexSet& set : indices) set.Flush();
        JumpCacheFlush();
        for (auto& h : page_heads) h = nullptr;
    }
};

}
