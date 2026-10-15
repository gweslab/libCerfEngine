#include "../mips_place_fns.h"

#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"

namespace cerfengine {

uint8_t* PlaceMips16Jal(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitStoreGprSextImm32(cursor, 31, (d->guest_address + 6u) | 1u);
    mips_emit::EmitBranchUncond(cursor, d->target, 1u, d->length);
    return cursor;
}

}
