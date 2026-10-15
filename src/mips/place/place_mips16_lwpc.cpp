#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"
#include "../mips_emit_services.h"
#include "../mips_memory_access.h"
#include "../../x86_emit.h"

namespace cerfengine {

uint8_t* PlaceMips16Lwpc(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx, d->target);
    EmitMovRegImm32(cursor, kEdx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Memory())));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsMemoryAccess::LoadWordHelper));
    mips_emit::EmitStoreGprSextEax(cursor, d->rt);
    return cursor;
}

}
