#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsSlti(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitSltImm64(cursor, d->rt, d->rs, d->imm, 0x9C);
    return cursor;
}

}
