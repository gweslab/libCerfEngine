#pragma once

#include <cstdint>

#include "../../emit_fault.h"
#include "../../x86_emit_encoding.h"

namespace cerfengine::cerfir {

/* System V AMD64 psABI 3.2.3 (p. 26): INTEGER arguments take %rdi, %rsi, %rdx,
   %rcx, %r8 and %r9 in order; the rest go on the stack right to left (p. 28). */
inline constexpr uint8_t  kX86ArgRegs[6]  = {x86::kEdi, x86::kEsi, x86::kEdx,
                                             x86::kEcx, x86::kR8,  x86::kR9};
inline constexpr uint32_t kX86ArgRegCount = 6;

#if defined(_WIN64)
void X86RequireKnownCallAbi();
#else
inline void X86RequireKnownCallAbi() {}
#endif

}
