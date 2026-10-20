#pragma once

#include <cstddef>
#include <cstdint>

#include "cerfir_block.h"

namespace cerfengine {
class JitBlockIndex;
struct JitBlock;
}

namespace cerfengine::cerfir {

enum class TlbAccess : uint8_t { kRead, kWrite, kReadWrite };

constexpr uint32_t kTlbAccessKinds = 3;
constexpr uint32_t kMaxPrivileges  = 3;

struct ExitModel {
    bool     chain           = false;
    int32_t  chain_exit_slot = 0;
    int32_t  cycle_slot      = 0;
    int32_t  deadline_slot   = 0;
    uint32_t fold_mask       = 0;
    int32_t  fold_aux        = 0;
    int32_t  mode_slot       = 0;
    uint32_t mode_mask       = 0;
};

struct BlockLink {
    JitBlockIndex* index               = nullptr;
    JitBlock*      self                = nullptr;
    uint32_t       key_start           = 0;
    uint32_t       phys_start          = 0;
    uint8_t*       native_start        = nullptr;
    uint32_t       granule_bytes       = 0;
    uint32_t       fold_value          = 0;
    bool           mode_set            = false;
    const void*    jump_cache          = nullptr;
    int32_t        jump_cache_key_disp = 0;
    uint32_t       privilege           = 0;
    bool           translation_off     = false;
};

struct SoftTlbModel {
    uint32_t    privileges                      = 0;
    uint32_t    fold_mask                       = 0;
    int32_t     fold_aux                        = 0;
    uint32_t    page_shift                      = 12;
    uint32_t    set_mask                        = 0;
    uint32_t    set_bytes                       = 0;
    int32_t     unit_aux[kMaxPrivileges]        = {};
    int32_t     entry_tag                       = 0;
    int32_t     entry_addend                    = 0;
    int32_t     entry_pa                        = 0;
    int32_t     entry_context                   = 0;
    int32_t     entry_global                    = 0;
    int32_t     entry_writable                  = 0;
    int32_t     context_aux                     = 0;
    bool        watch_writes                    = false;
    int32_t     watch_base_aux                  = 0;
    int32_t     watch_top_aux                   = 0;
    int32_t     watch_words_aux                 = 0;
    int32_t     watch_dirty_aux                 = 0;
    uint32_t    io_tag_bit                      = 0;
    uint32_t*   io_pending_address              = nullptr;
    uint32_t*   io_pending_valid                = nullptr;
    const void* miss_helper[kTlbAccessKinds]    = {};
    const void* user_helper[2]                  = {};
    const void* context                         = nullptr;
};

struct HelperMemoryModel {
    const void* load[kMaxPrivileges][3]  = {};
    const void* store[kMaxPrivileges][3] = {};
    const void* context                  = nullptr;
    bool        store_pc                 = false;
};

struct SlowEntries {
    const void* abort = nullptr;
    const void* align = nullptr;
};

struct MemorySlowDesc {
    const void* context;
    uint32_t    guest_pc;
    uint32_t    next_pc;
    uint32_t    raw;
    int32_t     result_slot;
    int32_t     writeback_slot;
    uint32_t    wb_add;
    AlignPolicy align;
    bool        unpriv;
    bool        writeback_on_fault;
    bool        back_out;
};

constexpr uint32_t kSlowShapes = 68;

constexpr uint32_t SlowShapeIndex(bool complete, bool store, uint32_t width_bytes, bool sign,
                                  bool has_wb, bool stop) {
    if (complete) return 64u + (store ? 2u : 0u) + (has_wb ? 1u : 0u);
    return (static_cast<uint32_t>(store) << 5) | ((width_bytes >> 1) << 3) |
           (static_cast<uint32_t>(sign && !store) << 2) | (static_cast<uint32_t>(has_wb) << 1) |
           static_cast<uint32_t>(stop);
}

struct MemorySlowModel {
    const void*        context = nullptr;
    const SlowEntries* entries = nullptr;
};

enum class MemoryModelKind : uint8_t { kNone, kSoftTlb, kHelper };

struct GuestModel {
    int32_t           cycle_slot = 0;
    ExitModel         exits;
    MemoryModelKind   memory = MemoryModelKind::kNone;
    SoftTlbModel      tlb;
    HelperMemoryModel helper;
    MemorySlowModel   slow;
};

constexpr uint32_t HelperWidthIndex(uint32_t width_bytes) {
    return width_bytes == 4u ? 2u : width_bytes - 1u;
}

}
