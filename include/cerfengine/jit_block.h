#pragma once

#include <cstdint>

namespace cerfengine {

class JitBlockIndex;

struct JitBlock {
    uint32_t guest_start  = 0;
    uint32_t guest_end    = 0;
    uint32_t phys_start   = 0;

    uint8_t* native_start = nullptr;

    JitBlockIndex* owner  = nullptr;

    JitBlock* page_next[2] = {nullptr, nullptr};
    uint32_t  index_start  = 0;
    uint32_t  index_split  = 0;
    uint32_t  index_start2 = 0;

    /* QEMU TranslationBlock jmp_list_next / jmp_dest (translation-block.h), and
       tb_jmp_unlink() (tb-maint.c). */
    uint32_t  chain_pending_va[2] = {0, 0};
    uint8_t*  chain_site[2]       = {nullptr, nullptr};
    uint8_t*  chain_fallback[2]   = {nullptr, nullptr};
    JitBlock* chain_target[2]     = {nullptr, nullptr};

    JitBlock* chain_src_next[2]      = {nullptr, nullptr};
    uint8_t   chain_src_next_slot[2] = {0, 0};
    JitBlock* chain_src_head         = nullptr;
    uint8_t   chain_src_head_slot    = 0;
};

}
