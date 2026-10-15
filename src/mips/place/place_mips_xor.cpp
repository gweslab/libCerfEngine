#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsXor(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitRtypeBitwise64(cursor, d->rd, d->rs, d->rt, 0x33);
    return cursor;
}

}
