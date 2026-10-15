#include <cstddef>

#include <cerfengine/arm/arm_cpu.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* ALUWritePC (DDI 0406C.c A2.3.2, pp. A2-47/A2-48) and the B9.3.20
   SUBS PC, LR return (p. B9-2013). */
uint8_t* EmitDpPcWriteTail(uint8_t* cursor, DecodedInsn* d,
                           BlockContext* ctx) {
    using namespace x86;

    if (d->s != 0u) {
        /* B9.3.20 (p. B9-2013): CPSRWriteByInstr(SPSR[], '1111', TRUE),
           then BranchWritePC(result); the flag outputs are discarded. */
        EmitPushReg(cursor, kEax);
        EmitPush32(cursor, static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(ctx->emit->Cpu())));
        EmitCall(cursor,
            reinterpret_cast<void*>(&ArmCpu::ExceptionReturnHelper));
        EmitAddRegImm32(cursor, kEsp, 8);
    } else if (ctx->thumb) {
        /* ARM DDI 0406C.c A2.3.1, p. A2-46. */
        EmitAndRegImm32(cursor, kEax, 0xFFFFFFFEu);
    } else if (ctx->emit->ProcessorConfig()->data_proc_to_pc_interworking) {
        /* ALUWritePC (A2.3.2 p. A2-48): BXWritePC from ARMv7 on. */
        cursor = EmitArmInterworkingFullEax(cursor);
    } else {
        /* ALUWritePC pre-v7: BranchWritePC, ARM state masks <31:2>
           (A2.3.2 p. A2-47). */
        EmitAndRegImm32(cursor, kEax, 0xFFFFFFFCu);
    }
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(15u), kEax);
    return PlaceR15ModifiedHelper(cursor, d, ctx);
}

}
