#pragma once

#include <cstdint>

#include "arm/arm_system_host.h"
#include "calling_convention.h"
#include "isa.h"
#include "mips/mips_system_host.h"
#include "mmio_bus_host.h"
#include "physical_memory_host.h"

namespace cerfengine {

struct MipsCpuState;

class EngineHost;
class ArmInstructionTiming;

struct ArmFeatures {
    bool arch_v6                      = false;
    bool arch_v7                      = false;
    bool thumb                        = false;
    bool thumb2                       = false;
    bool blx_reg                      = false;
    bool dsp                          = false;
    bool neon                         = false;
    bool vfp                          = false;
    bool integer_divide               = false;
    bool bit_field                    = false;
    bool rev                          = false;
    bool extend_rotate                = false;
    bool movw_movt                    = false;
    bool clz                          = false;
    bool security_extensions          = false;
    bool ldrex_strex                  = false;
    bool ldrex_strex_v6k              = false;
    bool armv5_unconditional_space    = false;
    bool mls                          = false;
    bool barrier_insn                 = false;
    bool preload                      = false;
    bool load_store_double            = false;
    bool load_to_pc_interworking      = false;
    bool data_proc_to_pc_interworking = false;
    bool accesses_spsr_in_system_mode = false;
    bool base_restored_abort_model    = false;
    uint32_t pc_store_offset          = 8;
    uint32_t fpsid                    = 0;
    uint32_t mvfr0                    = 0;
    uint32_t mvfr1                    = 0;
};

enum class MipsIsaLevel : uint32_t { kMips1, kMips3, kMips4 };

struct MipsFeatures {
    MipsIsaLevel isa_level  = MipsIsaLevel::kMips1;
    bool fpu                = false;
    bool llsc               = false;
    bool mips16             = false;
    bool tx39_multiply_add  = false;
    bool vr41xx_power_modes = false;
    bool cop1_cop2_unusable = false;
    uint32_t device_ip_mask = 0;
    uint32_t phys_addr_mask = 0xFFFFFFFFu;
};

struct Setup {
    Isa                   isa             = Isa::kArm;
    ArmFeatures           arm;
    MipsFeatures          mips;
    EngineHost*           host            = nullptr;
    void*                 memory          = nullptr;
    MmioBusBinding        mmio;
    bool                  report_block_compiles = false;
    bool                  verify_ir       = false;
    void*                 arm_translation = nullptr;
    void*                 arm_alignment_fault = nullptr;
    ArmSystemHost*        arm_system      = nullptr;
    const ArmInstructionTiming* arm_timing = nullptr;
    MipsTlbHooks          mips_tlb;
    MipsExceptionHooks    mips_exception;
    MipsSystemHost*       mips_system     = nullptr;
};

}
