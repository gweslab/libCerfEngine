#include "../mips_place_fns.h"

#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"

namespace cerfengine {

/* JAL target : gpr[31] = sext64(PC + 8); branch_state = uncond, btarget =
   ((PC+4) & 0xF0000000) | (target26 << 2). QEMU translate.c gen_compute_branch
   OPC_JAL. */
uint8_t* PlaceMipsJal(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    const uint32_t tgt = ((d->guest_address + 4u) & 0xF0000000u) | (d->target << 2);
    mips_emit::EmitStoreGprSextImm32(cursor, 31, d->guest_address + 8u);
    mips_emit::EmitBranchUncond(cursor, tgt, 0u, d->length);
    return cursor;
}

}
