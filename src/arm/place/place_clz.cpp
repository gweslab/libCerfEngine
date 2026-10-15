#include <cstddef>

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceClz(uint8_t*      cursor,
                  DecodedInsn*  d,
                  BlockContext*) {
    using namespace x86;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
        ArmGprDisp(d->rm));

    EmitMovRegImm32(cursor, kEcx, 32);

    EmitTestRegReg(cursor, kEax, kEax);

    uint8_t* jz_done = EmitJzLabel(cursor);

    /* BSR EDX, EAX - 0F BD /r, RM: reg=dest, r/m=src
       (SDM Vol. 2A 3-127 BSR). */
    Emit8(cursor, 0x0F);
    Emit8(cursor, 0xBD);
    EmitModRmReg(cursor, 3, kEax, kEdx);

    EmitMovRegImm32(cursor, kEcx, 31);

    EmitSubReg32Reg32(cursor, kEcx, kEdx);

    FixupLabel(jz_done, cursor);

    EmitMovBaseDisp32Reg(cursor, kStateReg,
        ArmGprDisp(d->rd),
        kEcx);

    return cursor;
}

}
