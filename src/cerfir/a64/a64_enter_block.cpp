#include "a64_enter_block.h"

#include "a64_host_abi.h"

namespace cerfengine::cerfir {

#if CERFENGINE_BACKEND_A64_MACOS || CERFENGINE_BACKEND_A64_LINUX
static_assert(kA64FrameBytes == 240 && kA64ReturnSlot == 96,
              "the block frame in the entry stubs matches the lowering");

/* AAPCS64 2025Q4 Table 2: x0-x2 carry the first three arguments, x19-x29 are callee-saved;
   "The Frame Pointer": FP addresses a frame record of the previous FP and LR. */
__attribute__((naked)) void A64EnterBlock(void*, void*) {
    __asm__(
        "stp x29, x30, [sp, #-16]!\n\t"
        "mov x29, sp\n\t"
        "stp x19, x20, [sp, #-16]!\n\t"
        "stp x21, x22, [sp, #-16]!\n\t"
        "stp x23, x24, [sp, #-16]!\n\t"
        "stp x25, x26, [sp, #-16]!\n\t"
        "stp x27, x28, [sp, #-16]!\n\t"
        "sub sp, sp, #240\n\t"
        "mov x28, x1\n\t"
        "adr x30, 1f\n\t"
        "str x30, [sp, #96]\n\t"
        "blr x0\n\t"
        "1:\n\t"
        "add sp, sp, #240\n\t"
        "ldp x27, x28, [sp], #16\n\t"
        "ldp x25, x26, [sp], #16\n\t"
        "ldp x23, x24, [sp], #16\n\t"
        "ldp x21, x22, [sp], #16\n\t"
        "ldp x19, x20, [sp], #16\n\t"
        "ldp x29, x30, [sp], #16\n\t"
        "ret\n\t");
}

__attribute__((naked)) void A64EnterBlockWithMmu(void*, void*, void*, const void*) {
    __asm__(
        "stp x29, x30, [sp, #-16]!\n\t"
        "mov x29, sp\n\t"
        "stp x19, x20, [sp, #-16]!\n\t"
        "stp x21, x22, [sp, #-16]!\n\t"
        "stp x23, x24, [sp, #-16]!\n\t"
        "stp x25, x26, [sp, #-16]!\n\t"
        "stp x27, x28, [sp, #-16]!\n\t"
        "sub sp, sp, #240\n\t"
        "mov x28, x1\n\t"
        "mov x27, x2\n\t"
        "adr x30, 1f\n\t"
        "str x30, [sp, #96]\n\t"
        "blr x3\n\t"
        "1:\n\t"
        "add sp, sp, #240\n\t"
        "ldp x27, x28, [sp], #16\n\t"
        "ldp x25, x26, [sp], #16\n\t"
        "ldp x23, x24, [sp], #16\n\t"
        "ldp x21, x22, [sp], #16\n\t"
        "ldp x19, x20, [sp], #16\n\t"
        "ldp x29, x30, [sp], #16\n\t"
        "ret\n\t");
}
#endif

}
