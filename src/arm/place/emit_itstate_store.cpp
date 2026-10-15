#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* DDI 0406C.c B1.3.3 (p. B1-1148): "IT[7:0], bits[15:10, 26:25]". */
uint8_t* EmitItStateStore(uint8_t* cursor, uint32_t itstate) {
    using namespace x86;
    const uint32_t bits = ArmItToCpsrBits(itstate);
    EmitAndBaseDisp32Imm32(cursor, kStateReg, ArmCpsrDisp(), ~kArmCpsrItMask);
    if (bits != 0u) {
        EmitOrBaseDisp32Imm32(cursor, kStateReg, ArmCpsrDisp(), bits);
    }
    return cursor;
}

}
