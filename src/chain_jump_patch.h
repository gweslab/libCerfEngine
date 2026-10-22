#pragma once

#include <cstdint>

#include <cerfengine/byte_order.h>
#include <cerfengine/force_inline.h>

#include "host_platform.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)

namespace cerfengine {

CERFENGINE_FORCEINLINE void PatchChainJump(uint8_t* site, const uint8_t* target) {
    le::Put32(site, static_cast<uint32_t>(target - (site + 4)));
}

}

#elif defined(__aarch64__) || defined(_M_ARM64)

#include "a64_emit.h"

namespace cerfengine {

CERFENGINE_FORCEINLINE void PatchChainJump(uint8_t* site, const uint8_t* target) {
    const platform::CodeWriteScope write;
    le::Put32(site, 0x14000000u | a64::Imm26(site, target));
    platform::FlushInstructionCache(site, 4u);
}

}

#else
#error "chain_jump_patch.h has no chain jump for this host"
#endif
