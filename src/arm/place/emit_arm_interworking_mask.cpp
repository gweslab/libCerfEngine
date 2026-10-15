#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* EmitArmInterworkingMaskEax(uint8_t* cursor) {
    using namespace x86;

    EmitTestRegImm32(cursor, kEax, 1);
    uint8_t* jz_to_rejoin = EmitJzLabel(cursor);

    EmitOrBaseDisp32Imm32(cursor, kStateReg, ArmCpsrDisp(), kArmCpsrT);
    EmitAndRegImm32(cursor, kEax, 0xFFFFFFFEu);

    FixupLabel(jz_to_rejoin, cursor);
    return cursor;
}

uint8_t* EmitArmInterworkingFullEax(uint8_t* cursor) {
    using namespace x86;

    /* BXWritePC (DDI 0406C A2.3.2, p. A2-47): bit0==1 -> Thumb at
       address<31:1>:'0'; else bit1==0 -> ARM; else UNPREDICTABLE. */
    EmitTestRegImm32(cursor, kEax, 1);
    uint8_t* jz_to_arm = EmitJzLabel(cursor);

    EmitOrBaseDisp32Imm32(cursor, kStateReg, ArmCpsrDisp(), kArmCpsrT);
    EmitAndRegImm32(cursor, kEax, 0xFFFFFFFEu);
    uint8_t* jmp_done = EmitJmpLabel(cursor);

    FixupLabel(jz_to_arm, cursor);
    EmitAndBaseDisp32Imm32(cursor, kStateReg, ArmCpsrDisp(), ~kArmCpsrT);
    EmitAndRegImm32(cursor, kEax, 0xFFFFFFFCu);

    FixupLabel(jmp_done, cursor);
    return cursor;
}

uint8_t* EmitArmInterworkingPcImm32(uint8_t* cursor, uint32_t address) {
    using namespace x86;

    const bool thumb = (address & 1u) != 0u;

    if (thumb) {
        EmitOrBaseDisp32Imm32(cursor, kStateReg, ArmCpsrDisp(), kArmCpsrT);
    } else {
        EmitAndBaseDisp32Imm32(cursor, kStateReg, ArmCpsrDisp(), ~kArmCpsrT);
    }
    EmitMovBaseDisp32Imm32(cursor, kStateReg,
        ArmGprDisp(ArmGpr::kR15),
        address & (thumb ? 0xFFFFFFFEu : 0xFFFFFFFCu));
    return cursor;
}

}
