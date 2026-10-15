#pragma once

#include <cstdint>

#include "../isa_block_space.h"

namespace cerfengine {

struct MipsCpuState;

class MipsTlbContext {
public:
    void Bind(MipsCpuState* state, IsaBlockSpace* blocks32, IsaBlockSpace* blocks16) {
        state_    = state;
        blocks32_ = blocks32;
        blocks16_ = blocks16;
    }

    MipsCpuState* State() const { return state_; }

    void InvalidatePage(uint32_t page_va) {
        blocks32_->JumpCacheClearPage(page_va);
        blocks16_->JumpCacheClearPage(page_va);
    }

    void InvalidateAddressSpace() {
        blocks32_->JumpCacheFlush();
        blocks16_->JumpCacheFlush();
    }

private:
    MipsCpuState*  state_    = nullptr;
    IsaBlockSpace* blocks32_ = nullptr;
    IsaBlockSpace* blocks16_ = nullptr;
};

}
