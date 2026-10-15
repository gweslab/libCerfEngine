#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_vfp_memory.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {


uint8_t* EmitVfpSingleTransfer(uint8_t*      cursor,
                               DecodedInsn*  d,
                               BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    /* A8.8.413 VSTR (p. A8-1082) closes both T1/A1 and T2/A2 with "if n == 15
       && CurrentInstrSet() != InstrSet_ARM then UNPREDICTABLE"; A8.8.333 VLDR
       (p. A8-924) carries no such clause. */
    if (d->rn == 15u && d->l == 0u && ctx->thumb) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const bool is_dp = (d->cp_num == 11);
    const uint32_t vd = is_dp
        ? ((d->n << 4) | (d->crd & 0xFu))
        : ((d->crd << 1) | (d->n & 0x1u));

    uint32_t flags = 0;
    if (d->l)  flags |= ArmVfpMemory::kFlagL;
    if (is_dp) flags |= ArmVfpMemory::kFlagDp;

    EmitPush32(cursor, flags);
    EmitPush32(cursor, static_cast<uint32_t>(d->offset));
    EmitPush32(cursor, vd);
    EmitPush32(cursor, d->rn);
    EmitPush32(cursor, ArmPcReadValue(d, ctx));
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->VfpMem())));
    EmitCall(cursor, reinterpret_cast<void*>(
        &ArmVfpMemory::HandleSingleTransferHelper));
    EmitAddRegImm32(cursor, kEsp, 28);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* continue_label = EmitJzLabel(cursor);
    EmitRet(cursor);
    FixupLabel(continue_label, cursor);
    return cursor;
}

}
