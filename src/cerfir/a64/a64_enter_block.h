#pragma once

namespace cerfengine::cerfir {

void A64EnterBlock(void* native, void* state);
void A64EnterBlockWithMmu(void* native, void* state, void* mmu_state, const void* entry_stub);

}
