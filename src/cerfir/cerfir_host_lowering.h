#pragma once

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <cerfengine/force_inline.h>

#include "x86/x86_direct_ops.h"
#include "x86/x86_enter_block.h"
#include "x86/x86_soft_tlb.h"

namespace cerfengine::cerfir {

using HostLowering    = X86Lowering;
using HostSoftTlb     = X86SoftTlb;
using HostGuestTarget = X86GuestTarget;
using HostArenaFull   = X86ArenaFull;

CERFENGINE_FORCEINLINE void HostEnterBlock(void* native, void* state) {
    X86EnterBlock(native, state);
}

CERFENGINE_FORCEINLINE void HostEnterBlock(void* native, void* state, void* mmu_state) {
    X86EnterBlockWithMmu(native, state, mmu_state);
}

}
#else
#error "cerfir_host_lowering.h has no host lowering for this host"
#endif
