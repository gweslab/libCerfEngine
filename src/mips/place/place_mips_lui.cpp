#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsLui(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    if (d->rt == 0) {
        return cursor;
    }
    mips_emit::EmitStoreGprSextImm32(cursor, d->rt, d->imm << 16);
    return cursor;
}

}
