#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsSrl(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    if (d->rs & 1u) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    mips_emit::EmitShiftImm32Sext(cursor, d->rd, d->rt, d->sa, 5);
    return cursor;
}

}
