#include <cstddef>
#include <cstdint>

#include <cerfengine/arm/arm_cpu.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* EmitVfpSingleMoveIdx(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx,
                              uint32_t      sn) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const int32_t sn_disp =
        static_cast<int32_t>(offsetof(ArmCpuState, vfp_d) + sn * 4u);
    const int32_t rt_disp =
        ArmGprDisp(d->rd);

    if (d->l) {
        if (d->rd == 15) {
            EmitPushBaseDisp32(cursor, kStateReg, sn_disp);
            EmitPush32(cursor,
                static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(emit->Cpu())));
            EmitCall(cursor, reinterpret_cast<void*>(
                &ArmCpu::UpdateNzcvOnlyHelper));
            EmitAddRegImm32(cursor, kEsp, 8);
            return cursor;
        }
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, sn_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg, rt_disp, kEax);
        return cursor;
    }

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rt_disp);
    EmitMovBaseDisp32Reg(cursor, kStateReg, sn_disp, kEax);
    return cursor;
}

/* VMOV Sn <-> Rt (DDI 0406C.c A8.8.343, p. A8-944): "n = UInt(Vn:N)". */
uint8_t* EmitVfpSingleMove(uint8_t*      cursor,
                           DecodedInsn*  d,
                           BlockContext* ctx) {
    const uint32_t sn = ((d->crn & 0xFu) << 1) | ((d->cp >> 2) & 1u);
    return EmitVfpSingleMoveIdx(cursor, d, ctx, sn);
}

}
