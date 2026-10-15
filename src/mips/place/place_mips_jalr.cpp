#include "../mips_place_fns.h"

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"
#include "../mips_block_context.h"
#include "../mips_emit_services.h"

namespace cerfengine {

uint8_t* PlaceMipsJalr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    mips_emit::EmitBranchRegister(cursor, d->rs, ctx->emit->CpuConfig()->mips16,
                                  d->length);
    if (d->rd != 0) {
        mips_emit::EmitStoreGprSextImm32(cursor, d->rd, d->guest_address + 8u);
    }
    return cursor;
}

}
