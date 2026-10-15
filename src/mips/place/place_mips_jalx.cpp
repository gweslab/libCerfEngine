#include "../mips_place_fns.h"

#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"

namespace cerfengine {

/* JALX target : gpr[31] = sext64(PC + 8), bit 0 = the pre-jump ISA mode (0 in
   32-bit mode, U15509EJ2V0UM 3.4.1); btarget = ((PC+4) & 0xF0000000) |
   (target26 << 2), target ISA inverted (QEMU translate.c OPC_JALX). */
uint8_t* PlaceMipsJalx(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    const uint32_t tgt = ((d->guest_address + 4u) & 0xF0000000u) | (d->target << 2);
    mips_emit::EmitStoreGprSextImm32(cursor, 31, d->guest_address + 8u);
    mips_emit::EmitBranchUncond(cursor, tgt, 1u, d->length);
    return cursor;
}

}
