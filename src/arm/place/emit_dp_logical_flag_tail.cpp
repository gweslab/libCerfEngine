#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* ARM DDI 0406C.c: flag writeback for the logical data-processing class -
   APSR.C = the Shift_C carry, V unchanged (A8.8.14 p. A8-327, A8.8.241
   p. A8-747); Shift_C(amount == 0) leaves carry_in (A8.4.3 p. A8-293). */
uint8_t* EmitDpLogicalFlagTail(uint8_t* cursor, DpLogicalCarry carry) {
    using namespace x86;

    /* SETcc (SDM Vol. 2B 4-620): "Flags Affected: None". */
    EmitSetccBaseDisp32(cursor, kSetS, kStateReg, ArmNfDisp());
    EmitSetccBaseDisp32(cursor, kSetZ, kStateReg, ArmZfDisp());

    switch (carry) {
    case DpLogicalCarry::kUnchanged:
        break;
    case DpLogicalCarry::kClearImm:
        EmitMovByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 0u);
        break;
    case DpLogicalCarry::kSetImm:
        EmitMovByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 1u);
        break;
    case DpLogicalCarry::kFromDl:
        EmitMovBaseDisp32Byte(cursor, kStateReg, ArmCfDisp(), kDl);
        break;
    }
    return cursor;
}

}
