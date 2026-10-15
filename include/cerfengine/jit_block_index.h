#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "jit_block.h"

namespace cerfengine {

class Engine;

class JitBlockIndex {
public:
    using ClearJumpCacheFn = void (*)(uint32_t folded_va, void* ctx);

    void Initialize(cerfengine::Engine& engine);
    void Flush();

    static size_t OuterEntrySize() {
        return (sizeof(JitBlock) + 15u) & ~static_cast<size_t>(15u);
    }

    JitBlock* PlaceOuterAt(uint8_t* slab, const JitBlock& block);

    JitBlock* FindExact(uint32_t guest_start);

    void RemoveNode(JitBlock* block, ClearJumpCacheFn clear_jc, void* ctx);

    void LinkChain(JitBlock* src, uint32_t slot, JitBlock* dest, uint8_t* site,
                   uint8_t* fallback);

private:
    struct Slot {
        uint32_t  key = 0;
        JitBlock* blk = nullptr;
    };

    static constexpr size_t kInitialSlots = 256;

    size_t Bucket(uint32_t key) const {
        return static_cast<size_t>(key * 2654435761u) >> shift_;
    }

    void SetCapacity(size_t slots);
    void Grow();
    void EraseAt(size_t idx);

    void RestoreFallbackJump(JitBlock* src, uint32_t slot);
    void UnlinkChains(JitBlock* block);
    void DetachFromDest(JitBlock* block, uint32_t slot);

    cerfengine::Engine* engine_ = nullptr;
    std::vector<Slot>   table_;
    size_t            mask_  = 0;
    size_t            shift_ = 32;
    size_t            count_ = 0;
};

}
