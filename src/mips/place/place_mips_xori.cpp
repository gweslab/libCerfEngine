#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceMipsXori(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rt == 0) {
        return cursor;
    }
    mips_emit::EmitLoadGprLo(cursor, kEax, d->rs);
    EmitMovRegImm32(cursor, kEcx, d->imm);
    EmitXorRegReg(cursor, kEax, kEcx);
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rt), kEax);
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rs));
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprHiOff(d->rt), kEax);
    return cursor;
}

}
