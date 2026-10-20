#pragma once

#include <cerfengine/calling_convention.h>

namespace cerfengine::cerfir {

void CERFENGINE_CDECL X86EnterBlock(void* native, void* state);
void CERFENGINE_CDECL X86EnterBlockWithMmu(void* native, void* state, void* mmu_state);

}
