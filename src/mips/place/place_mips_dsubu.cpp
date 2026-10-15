#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsDsubu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitRtypeArith64(cursor, d->rd, d->rs, d->rt, 0x2B, 0x1B);
    return cursor;
}

}
