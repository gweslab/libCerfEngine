#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceMipsNor(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    mips_emit::EmitRtypeBitwise64(cursor, d->rd, d->rs, d->rt, 0x0B);
    if (d->rd != 0) {
        EmitNotBaseDisp32(cursor, kStateReg, mips_emit::GprLoOff(d->rd));
        EmitNotBaseDisp32(cursor, kStateReg, mips_emit::GprHiOff(d->rd));
    }
    return cursor;
}

}
