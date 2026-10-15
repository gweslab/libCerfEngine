#include "../mips_place_fns.h"

#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"

namespace cerfengine {

/* MIPS16 ADDIU rx, pc / DADDIU ry, pc (U15509EJ2V0UM Table 3-15 p74/p75):
   masked BasePC + zero-extended shifted immediate "is placed into" the
   register (base rules Table 3-12 p67). */
uint8_t* PlaceMips16Addiupc(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitStoreGprSextImm32(cursor, d->rt, d->target);
    return cursor;
}

}
