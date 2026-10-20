#pragma once

#include <cstdint>

#include "../calling_convention.h"

namespace cerfengine {

struct MipsCpuState;

enum class MipsTlbResult {
    kMatch,
    kNoMatch,
    kInvalid,
    kModified,
    kAddressError,
};

enum class MipsAccess { kFetch, kRead, kWrite };

class MipsTlbContext;

using MipsMapAddressFn  = MipsTlbResult (CERFENGINE_FASTCALL*)(void* context, MipsCpuState* st,
                                                               uint32_t va, MipsAccess acc,
                                                               uint32_t* pa);
using MipsPageGlobalFn  = bool (CERFENGINE_FASTCALL*)(void* context, MipsCpuState* st,
                                                      uint32_t va);
using MipsTlbOpFn       = void (CERFENGINE_FASTCALL*)(void* context, MipsTlbContext& tlb);

struct MipsTlbHooks {
    void*            context       = nullptr;
    MipsMapAddressFn map_address   = nullptr;
    MipsPageGlobalFn page_global   = nullptr;
    MipsTlbOpFn      write_indexed = nullptr;
    MipsTlbOpFn      write_random  = nullptr;
    MipsTlbOpFn      probe         = nullptr;
    MipsTlbOpFn      read          = nullptr;
};

using MipsEnterExceptionFn    = void (CERFENGINE_FASTCALL*)(void* context, MipsCpuState* st,
                                                             uint32_t cause,
                                                             bool refill_eligible);
using MipsMmuFaultRegsFn      = void (CERFENGINE_FASTCALL*)(void* context, MipsCpuState* st,
                                                           uint32_t va);
using MipsInterruptsEnabledFn = bool (CERFENGINE_FASTCALL*)(void* context,
                                                           const MipsCpuState& st);
using MipsStateHook           = void (CERFENGINE_FASTCALL*)(void* context, MipsCpuState* st);

struct MipsExceptionHooks {
    void*                   context            = nullptr;
    MipsEnterExceptionFn    enter              = nullptr;
    MipsMmuFaultRegsFn      set_mmu_fault_regs = nullptr;
    MipsInterruptsEnabledFn interrupts_enabled = nullptr;
    MipsStateHook           eret               = nullptr;
    MipsStateHook           rfe                = nullptr;
};

using MipsCp0Read  = uint32_t (CERFENGINE_FASTCALL*)(void* context);
using MipsCp0Write = void (CERFENGINE_FASTCALL*)(void* context, uint32_t value);

struct MipsCp0Decision {
    bool         present       = false;
    int32_t      offset        = -1;
    bool         writable      = false;
    MipsCp0Read  read          = nullptr;
    void*        read_context  = nullptr;
    MipsCp0Write write         = nullptr;
    void*        write_context = nullptr;
};

class MipsSystemHost {
public:
    virtual ~MipsSystemHost() = default;

    virtual MipsCp0Decision MipsCp0(uint32_t rd)        = 0;
    virtual void            MipsReset(MipsCpuState* st) = 0;
};

}
