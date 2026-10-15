#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* DSRA32 rd, rt, sa : rd = (int64)gpr[rt] >> (sa + 32), arithmetic -> rd.lo =
   (int32)rt.hi >> sa, rd.hi = sign-extension. (QEMU translate.c gen_shift_imm
   OPC_DSRA32 sari uimm+32.) rd==0 NOP. */
uint8_t* PlaceMipsDsra32(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rd == 0) {
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rt));
    if (d->sa != 0) {
        EmitSarReg32Imm(cursor, kEax, static_cast<uint8_t>(d->sa));
    }
    mips_emit::EmitStoreGprSextEax(cursor, d->rd);
    return cursor;
}

}
