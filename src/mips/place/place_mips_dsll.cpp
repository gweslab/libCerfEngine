#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsDsll(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitDShiftImm(cursor, d->rd, d->rt, static_cast<uint8_t>(d->sa),
                             true, 4);
    return cursor;
}

}
