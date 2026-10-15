#include "../mips_place_fns.h"

#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"

namespace cerfengine {

/* MIPS16 JALX (U15509EJ2V0UM Table 3-19 p82): link as JAL ("ra bit 0 will
   reflect the ISA Mode bit before execution of the Jump" = 1); "The ISA Mode
   bit is inverted with a delay of one instruction" -> target ISA 0. */
uint8_t* PlaceMips16Jalx(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitStoreGprSextImm32(cursor, 31, (d->guest_address + 6u) | 1u);
    mips_emit::EmitBranchUncond(cursor, d->target, 0u, d->length);
    return cursor;
}

}
