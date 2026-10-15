#include "../mips_place_fns.h"

#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"

namespace cerfengine {

uint8_t* PlaceMips16Jalr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitBranchRegister(cursor, d->rs, true, d->length);
    mips_emit::EmitStoreGprSextImm32(cursor, 31, (d->guest_address + 4u) | 1u);
    return cursor;
}

}
