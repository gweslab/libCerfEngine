#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../../x86_emit.h"

namespace cerfengine {

uint8_t* PlaceMips16B(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    x86::EmitMovBaseDisp32Imm32(cursor, x86::kStateReg,
                                static_cast<int32_t>(offsetof(MipsCpuState, pc)),
                                d->target);
    return cursor;
}

}
