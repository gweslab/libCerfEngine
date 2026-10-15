#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit.h"

namespace cerfengine {

/* QEMU MIPS gen_shift OPC_SLLV (translate.c). */
uint8_t* PlaceMipsSllv(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rd == 0) {
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    Emit8(cursor, 0xD3);                          /* SHL r/m32, CL - D3 /4
                                                     (SDM Vol. 2B 4-600 SAL/SAR/SHL/SHR) */
    EmitModRmReg(cursor, 3, kEax, 4);
    mips_emit::EmitStoreGprSextEax(cursor, d->rd);
    return cursor;
}

}
