#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "jit_block_index.h"

namespace cerfengine {

constexpr uint32_t kJumpCacheSize = 4096;

constexpr uint32_t kBlockUnindexed = 0xFFFFFFFFu;
struct JumpCacheEntry {
    uint32_t  folded_va;
    void*     native;
    JitBlock* blk;
    uint32_t  reserved;
};

constexpr uint32_t kJumpCacheEntryShift = 4;

struct IsaBlockSpace {
    JitBlockIndex global;
    JitBlockIndex per_asid[256];
    uint32_t      asid_populated[8] = {0};
    JumpCacheEntry jump_cache[kJumpCacheSize];

    std::vector<JitBlock*> page_heads;
    uint32_t               page_base  = 0;
    uint32_t               page_count = 0;

    void JumpCacheFlush() { std::memset(jump_cache, 0, sizeof(jump_cache)); }

    void JumpCacheClearPage(uint32_t page_va) {
        const uint32_t base = page_va & 0xFFFFF000u;
        for (uint32_t off = 0; off < 0x1000u; off += 4u) {
            JumpCacheEntry& e =
                jump_cache[((base + off) >> 2) & (kJumpCacheSize - 1u)];
            if ((e.folded_va & 0xFFFFF000u) == base) {
                e.folded_va = 0;
                e.native    = nullptr;
                e.blk       = nullptr;
            }
        }
    }

    void JumpCacheClearRange(uint32_t base_va, uint32_t span_bytes);

    void* JumpCacheLookup(uint32_t folded_va) const {
        const JumpCacheEntry& e = jump_cache[(folded_va >> 2) & (kJumpCacheSize - 1u)];
        return e.folded_va == folded_va ? e.native : nullptr;
    }

    const JumpCacheEntry* JumpCacheProbe(uint32_t folded_va) const {
        const JumpCacheEntry& e = jump_cache[(folded_va >> 2) & (kJumpCacheSize - 1u)];
        return e.folded_va == folded_va ? &e : nullptr;
    }

    void JumpCacheInsert(uint32_t folded_va, void* native,
                         JitBlock* blk = nullptr) {
        JumpCacheEntry& e = jump_cache[(folded_va >> 2) & (kJumpCacheSize - 1u)];
        e.folded_va = folded_va;
        e.native    = native;
        e.blk       = blk;
    }

    void Initialize(cerfengine::Engine& engine, uint32_t dram_page_base,
                    uint32_t dram_page_count) {
        global.Initialize(engine);
        for (auto& t : per_asid) t.Initialize(engine);
        JumpCacheFlush();
        page_base  = dram_page_base;
        page_count = dram_page_count;
        page_heads.assign(dram_page_count, nullptr);
    }
    void MarkPopulated(uint8_t asid) {
        asid_populated[asid >> 5] |= (1u << (asid & 31u));
    }
    static void ClearJcSlot(uint32_t folded_va, void* ctx) {
        auto* sp = static_cast<IsaBlockSpace*>(ctx);
        JumpCacheEntry& e = sp->jump_cache[(folded_va >> 2) & (kJumpCacheSize - 1u)];
        if (e.folded_va == folded_va) {
            e.folded_va = 0;
            e.native    = nullptr;
            e.blk       = nullptr;
        }
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
        global.Flush();
        for (uint32_t w = 0; w < 8u; ++w) {
            uint32_t bits = asid_populated[w];
            if (!bits) continue;
            for (uint32_t b = 0; b < 32u; ++b) {
                if (bits & (1u << b)) per_asid[(w << 5) + b].Flush();
            }
            asid_populated[w] = 0;
        }
        JumpCacheFlush();
        for (auto& h : page_heads) h = nullptr;
    }
};

}
