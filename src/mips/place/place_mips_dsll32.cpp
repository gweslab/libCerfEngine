#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* DSLL32 rd, rt, sa : rd = rt << (sa + 32), full 64-bit -> rd.hi = rt.lo << sa,
   rd.lo = 0. (QEMU translate.c gen_shift_imm OPC_DSLL32.) */
uint8_t* PlaceMipsDsll32(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rd == 0) {
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    if (d->sa != 0) {
        EmitShlReg32Imm(cursor, kEax, static_cast<uint8_t>(d->sa));
    }
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprHiOff(d->rd), kEax);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, mips_emit::GprLoOff(d->rd), 0);
    return cursor;
}

}
