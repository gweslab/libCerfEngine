#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* MOVZ rd, rs, rt : if gpr[rt] == 0 then rd = rs, full 64-bit (no
   sign-extension); rd==0 is a NOP. (QEMU translate.c gen_cond_move OPC_MOVZ.) */
uint8_t* PlaceMipsMovz(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rd == 0) {
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprHiOff(d->rt));
    EmitOrReg32Reg32(cursor, kEax, kEcx);
    uint8_t* j_skip = EmitJnzLabel(cursor);
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rd), kEax);
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rs));
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprHiOff(d->rd), kEax);
    FixupLabel(j_skip, cursor);
    return cursor;
}

}
