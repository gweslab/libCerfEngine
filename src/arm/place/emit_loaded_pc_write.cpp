#include "../arm_emit_services.h"
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* LoadWritePC (DDI 0406C.c A2.3.2, p. A2-48): BXWritePC from ARMv5 on,
   else BranchWritePC. */
uint8_t* EmitLoadedPcWrite(uint8_t* cursor, DecodedInsn* d,
                           BlockContext* ctx) {
    using namespace x86;
    if (ctx->emit->ProcessorConfig()->load_to_pc_interworking) {
        cursor = EmitArmInterworkingFullEax(cursor);
    } else {
        /* BranchWritePC (A2.3.2, p. A2-47): <31:2>:'00' in ARM state,
           <31:1>:'0' in Thumb state. */
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmCpsrDisp());
        EmitTestRegImm32(cursor, kEcx, kArmCpsrT);
        uint8_t* thumb_l = EmitJnzLabel32(cursor);
        EmitAndRegImm32(cursor, kEax, 0xFFFFFFFCu);
        uint8_t* mask_done = EmitJmpLabel32(cursor);
        FixupLabel32(thumb_l, cursor);
        EmitAndRegImm32(cursor, kEax, 0xFFFFFFFEu);
        FixupLabel32(mask_done, cursor);
    }
    EmitMovBaseDisp32Reg(cursor, kStateReg,
        ArmGprDisp(15u), kEax);
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}

}
