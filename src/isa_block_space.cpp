#include <cerfengine/isa_block_space.h>

namespace cerfengine {

void IsaBlockSpace::JumpCacheClearRange(uint32_t base_va,
                                        uint32_t span_bytes) {
    if (span_bytes == 0x1000u) {
        JumpCacheClearPage(base_va);
        return;
    }
    for (uint32_t index = 0; index < kJumpCacheSize; ++index) {
        const uint32_t page = JumpCacheLiveKey(jump_cache[index]) & 0xFFFFF000u;
        if (page - base_va < span_bytes) JumpCacheClearEntry(index);
    }
}

}
