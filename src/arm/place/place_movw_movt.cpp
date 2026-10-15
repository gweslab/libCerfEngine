#include <cstddef>

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceMovw(uint8_t*      cursor,
                   DecodedInsn*  d,
                   BlockContext*) {
    using namespace x86;
    EmitMovBaseDisp32Imm32(cursor, kStateReg,
        ArmGprDisp(d->rd),
        d->immediate);
    return cursor;
}

uint8_t* PlaceMovt(uint8_t*      cursor,
                   DecodedInsn*  d,
                   BlockContext*) {
    using namespace x86;
    const int32_t rd_disp =
        ArmGprDisp(d->rd);

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
    EmitAndRegImm32     (cursor, kEax, 0x0000FFFFu);
    EmitMovRegImm32     (cursor, kEcx, d->immediate << 16);
    EmitOrReg32Reg32    (cursor, kEax, kEcx);
    EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
    return cursor;
}

}
