#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceMipsSlt(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rd == 0) {
        return cursor;
    }
    EmitXorRegReg(cursor, kEcx, kEcx);
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitSubRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rs));
    EmitSbbRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rt));
    Emit8(cursor, 0x0F);                /* SETL cl - 0F 9C, SF != OF
                                           (SDM Vol. 2B 4-618 SETcc) */
    Emit8(cursor, 0x9C);
    EmitModRmReg(cursor, 3, kCl, 0);
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rd), kEcx);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, mips_emit::GprHiOff(d->rd), 0);
    return cursor;
}

}
