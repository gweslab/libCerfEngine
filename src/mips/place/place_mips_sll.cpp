#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsSll(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitShiftImm32Sext(cursor, d->rd, d->rt, d->sa, 4);
    return cursor;
}

}
