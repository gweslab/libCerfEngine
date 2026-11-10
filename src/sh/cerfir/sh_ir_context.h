#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/sh/sh_cpu_state.h>
#include "../../cerfir/cerfir_builder.h"
#include "../sh_decoded_insn.h"

namespace cerfengine {

class ShCpu;
class ShMmu;

constexpr uint32_t ShStateDisp(size_t offset) { return static_cast<uint32_t>(offset); }
constexpr uint32_t ShGprDisp(uint32_t r) { return ShStateDisp(offsetof(ShCpuState, gpr)) + r * 4u; }
constexpr uint32_t ShPcDisp() { return ShStateDisp(offsetof(ShCpuState, pc)); }
constexpr uint32_t ShSrDisp() { return ShStateDisp(offsetof(ShCpuState, sr)); }
constexpr uint32_t ShGbrDisp() { return ShStateDisp(offsetof(ShCpuState, gbr)); }
constexpr uint32_t ShPrDisp() { return ShStateDisp(offsetof(ShCpuState, pr)); }
constexpr uint32_t ShMachDisp() { return ShStateDisp(offsetof(ShCpuState, mach)); }
constexpr uint32_t ShMaclDisp() { return ShStateDisp(offsetof(ShCpuState, macl)); }

struct ShIrContext {
    cerfir::Builder*     b;
    const ShDecodedInsn* d;
    ShCpu*               cpu;
    ShMmu*               mmu;
    bool                 in_delay_slot;
    cerfir::Ref          branch_target;
    bool                 branch_target_static;
    uint32_t             static_target;
    cerfir::Ref          t_clear;
    bool                 taken_if_t_clear;
    bool                 in_ldtlb_shadow;
};

}
