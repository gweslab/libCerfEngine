#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceMipsAndi(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rt == 0) {
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitAndRegImm32(cursor, kEax, d->imm);
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rt), kEax);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, mips_emit::GprHiOff(d->rt), 0);
    return cursor;
}

}
