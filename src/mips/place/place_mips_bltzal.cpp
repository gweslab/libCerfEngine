#include "../mips_place_fns.h"

#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsBltzal(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    const uint32_t soff = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    const uint32_t btgt = d->guest_address + 4u + (soff << 2);
    mips_emit::EmitBranchCondSign(cursor, d->rs, btgt, true,
                                  MipsBranch::kCond, d->length);
    mips_emit::EmitStoreGprSextImm32(cursor, 31, d->guest_address + 8u);
    return cursor;
}

}
