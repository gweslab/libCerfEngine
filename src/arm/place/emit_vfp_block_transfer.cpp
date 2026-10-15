#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_vfp_memory.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {


uint8_t* EmitVfpBlockTransfer(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    /* DDI 0406C.c A8.8.332 VLDM (p. A8-922) and A8.8.412 VSTM (p. A8-1080)
       close every encoding with "if n == 15 && (wback || CurrentInstrSet() !=
       InstrSet_ARM) then UNPREDICTABLE". */
    if (d->rn == 15u && (ctx->thumb || d->w)) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const bool is_dp = (d->cp_num == 11);
    const uint32_t vd = is_dp
        ? ((d->n << 4) | (d->crd & 0xFu))
        : ((d->crd << 1) | (d->n & 0x1u));

    const int32_t  signed_off = d->offset;
    const uint32_t abs_off    = static_cast<uint32_t>(
        signed_off < 0 ? -signed_off : signed_off);
    const uint32_t imm8       = (abs_off >> 2) & 0xFFu;

    uint32_t flags = 0;
    if (d->l) flags |= ArmVfpMemory::kFlagL;
    if (d->w) flags |= ArmVfpMemory::kFlagW;
    if (d->p) flags |= ArmVfpMemory::kFlagP;
    if (is_dp) flags |= ArmVfpMemory::kFlagDp;

    EmitPush32(cursor, flags);
    EmitPush32(cursor, imm8);
    EmitPush32(cursor, vd);
    EmitPush32(cursor, d->rn);
    EmitPush32(cursor, ArmPcReadValue(d, ctx));
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->VfpMem())));
    EmitCall(cursor, reinterpret_cast<void*>(
        &ArmVfpMemory::HandleBlockTransferHelper));
    EmitAddRegImm32(cursor, kEsp, 28);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* continue_label = EmitJzLabel(cursor);
    EmitRet(cursor);
    FixupLabel(continue_label, cursor);
    return cursor;
}

}
