#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_gpr_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceMipsDsrl32(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    if (d->rs & 1u) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    if (d->rd == 0) {
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprHiOff(d->rt));
    if (d->sa != 0) {
        EmitShrReg32Imm(cursor, kEax, static_cast<uint8_t>(d->sa));
    }
    EmitMovBaseDisp32Reg(cursor, kStateReg, mips_emit::GprLoOff(d->rd), kEax);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, mips_emit::GprHiOff(d->rd), 0);
    return cursor;
}

}
