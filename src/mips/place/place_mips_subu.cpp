#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsSubu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitRtypeArith32Sext(cursor, d->rd, d->rs, d->rt, 0x2B);
    return cursor;
}

}
