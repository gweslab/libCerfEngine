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
#endif

}
