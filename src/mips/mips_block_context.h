#pragma once

#include <cstdint>

#include "mips_decoded_insn.h"

namespace cerfengine {

class MipsBlockCompiler;
class MipsEmitServices;
enum class MipsTlbResult;

constexpr uint32_t kMaxMipsInsnPerBlock = 100;

struct MipsBlockContext {
    MipsEmitServices*  emit;
    MipsBlockCompiler* compiler;

    MipsDecodedInsn insns[kMaxMipsInsnPerBlock];
    uint32_t        num_insns;
    uint32_t        privilege;

    uint32_t        block_phys_page_base;

    uint32_t        tail_split;
    uint32_t        tail_page_pa;

    uint32_t        fetch_fault_pending;
    uint32_t        fetch_fault_va;
    MipsTlbResult   fetch_fault_res;

    uint32_t        fetch_unbacked_va;
    uint32_t        fetch_unbacked_pa;

    uint32_t        uncacheable;
};

}
