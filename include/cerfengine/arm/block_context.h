#pragma once

#include <cstdint>

#include "decoded_insn.h"

namespace cerfengine {

class ArmEmitServices;
class JitBlockIndex;
struct JitBlock;

/* ARM DDI 0406C.c Table D15-10 (p. D15-2609): "In a Fine page table, each
   entry provides translation information for 1KB of memory." */
constexpr uint32_t kArmBlockPageBytes  = 1024;
constexpr uint32_t kMaxArmInsnPerBlock = kArmBlockPageBytes / 4u;

struct BlockContext {
    ArmEmitServices* emit;
    const void* sctlr_write_target;
    const void* raise_abort_data_helper_target;

    uint8_t* native_start;

    JitBlockIndex* index;
    JitBlock*      self;
    uint32_t       guest_start;
    uint32_t       phys_start;
    uint32_t       fcse_pid;
    const void*    jump_cache;
    bool           thumb;

    DecodedInsn insns[kMaxArmInsnPerBlock];
    uint32_t    num_insns;
};

}
