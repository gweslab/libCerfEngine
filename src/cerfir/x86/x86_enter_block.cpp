#include "x86_enter_block.h"

namespace cerfengine::cerfir {

#if CERFENGINE_BACKEND_X86_WIN32
__declspec(naked) void CERFENGINE_CDECL X86EnterBlock(void*, void*) {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov  ecx, [esp + 20]
        mov  esi, [esp + 24]
        call ecx
        pop  edi
        pop  esi
        pop  ebx
        pop  ebp
        ret
    }
}

__declspec(naked) void CERFENGINE_CDECL X86EnterBlockWithMmu(void*, void*, void*) {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov  ecx, [esp + 20]
        mov  esi, [esp + 24]
        mov  ebx, [esp + 28]
        call ecx
        pop  edi
        pop  esi
        pop  ebx
        pop  ebp
        ret
    }
}
#elif CERFENGINE_BACKEND_X86_64_SYSV
/* System V AMD64 psABI Figure 3.4 (p. 27): %rdi, %rsi, %rdx carry the first three
   arguments; %rbx, %rbp, %r12 and %r13 are callee-saved. */
__attribute__((naked)) void X86EnterBlock(void*, void*) {
    __asm__(
        "push %rbp\n\t"
        "push %rbx\n\t"
        "push %r12\n\t"
        "push %r13\n\t"
        "mov  %rsi, %rbx\n\t"
        "call *%rdi\n\t"
        "pop  %r13\n\t"
        "pop  %r12\n\t"
        "pop  %rbx\n\t"
        "pop  %rbp\n\t"
        "ret\n\t");
}

__attribute__((naked)) void X86EnterBlockWithMmu(void*, void*, void*) {
    __asm__(
        "push %rbp\n\t"
        "push %rbx\n\t"
        "push %r12\n\t"
        "push %r13\n\t"
        "mov  %rsi, %rbx\n\t"
        "mov  %rdx, %rbp\n\t"
        "call *%rdi\n\t"
        "pop  %r13\n\t"
        "pop  %r12\n\t"
        "pop  %rbx\n\t"
        "pop  %rbp\n\t"
        "ret\n\t");
}
#endif

}
