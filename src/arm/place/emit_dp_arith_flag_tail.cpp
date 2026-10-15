#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* ARM DDI 0406C.c: flag writeback for the AddWithCarry data-processing
   class (A8.8.7 Operation, p. A8-313). */
uint8_t* EmitDpArithFlagTail(uint8_t* cursor, DecodedInsn* d) {
    using namespace x86;

    const uint32_t opcode      = d->op1;
    const bool     borrow_form = opcode == 2u || opcode == 3u ||
                                 opcode == 6u || opcode == 7u ||
                                 opcode == 10u;

    /* SETcc (SDM Vol. 2B 4-620): "Flags Affected: None". */
    EmitSetccBaseDisp32(cursor, kSetS, kStateReg, ArmNfDisp());
    EmitSetccBaseDisp32(cursor, kSetZ, kStateReg, ArmZfDisp());
    EmitSetccBaseDisp32(cursor, borrow_form ? kSetNc : kSetC, kStateReg,
                        ArmCfDisp());
    EmitSetccBaseDisp32(cursor, kSetO, kStateReg, ArmVfDisp());
    return cursor;
}

}
