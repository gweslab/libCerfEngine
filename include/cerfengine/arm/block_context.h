#pragma once

#include <cstdint>

#include "decoded_insn.h"

namespace cerfengine {

class ArmEmitServices;

/* ARM DDI 0406C.c Table D15-10 (p. D15-2609): "In a Fine page table, each
   entry provides translation information for 1KB of memory." */
constexpr uint32_t kArmBlockPageBytes  = 1024;
constexpr uint32_t kMaxArmInsnPerBlock = kArmBlockPageBytes / 4u;

struct BlockContext {
    ArmEmitServices* emit;

    bool thumb;
    bool alignment_checked;
    bool unaligned_faults;

    DecodedInsn insns[kMaxArmInsnPerBlock];
    uint32_t    num_insns;
};

/* ARM DDI 0100I A7.1, p. A7-3. */
inline uint32_t ArmPcReadValue(const DecodedInsn* d, const BlockContext* ctx) {
    return d->guest_address + (ctx->thumb ? 4u : 8u);
}

}
