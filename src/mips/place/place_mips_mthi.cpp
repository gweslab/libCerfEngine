#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"

namespace cerfengine {

/* QEMU MIPS gen_HILO OPC_MTHI (translate.c). */
uint8_t* PlaceMipsMthi(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitMoveGpr64ToField(
        cursor, static_cast<int32_t>(offsetof(MipsCpuState, hi)), d->rs);
    return cursor;
}

}
