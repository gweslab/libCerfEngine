#include "../mips_place_fns.h"

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"
#include "../mips_block_context.h"
#include "../mips_emit_services.h"

namespace cerfengine {

uint8_t* PlaceMipsJr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    mips_emit::EmitBranchRegister(cursor, d->rs, ctx->emit->CpuConfig()->mips16,
                                  d->length);
    return cursor;
}

}
