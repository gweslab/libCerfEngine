#include <cstddef>

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* ARM DDI 0406C.c A8.8.29 CBNZ, CBZ Operation (p. A8-357): "if nonzero !=
   IsZero(R[n]) then BranchWritePC(PC + imm32);", with "nonzero = (op == '1')"
   from encoding T1 (p. A8-356). */
uint8_t* PlaceCbz(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rn));
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* not_taken = d->op1 != 0u ? EmitJzLabel32(cursor)
                                      : EmitJnzLabel32(cursor);
    cursor = PlaceBranch(cursor, d, ctx);
    FixupLabel32(not_taken, cursor);
    return cursor;
}

}
