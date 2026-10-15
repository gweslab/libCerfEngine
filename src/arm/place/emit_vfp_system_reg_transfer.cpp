#include <cstddef>
#include <cstdint>

#include <cerfengine/arm/arm_cpu.h>
#include "../arm_emit_services.h"
#include "../arm_vfp.h"
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {


uint8_t* EmitVfpSystemRegTransfer(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;
    const int32_t rd_disp =
        ArmGprDisp(d->rd);

    /* QEMU ARM ARM_VFP_* constants (cpu.h). */
    constexpr uint32_t kFpsid  = 0;
    constexpr uint32_t kFpscr  = 1;
    constexpr uint32_t kMvfr1  = 6;
    constexpr uint32_t kMvfr0  = 7;
    constexpr uint32_t kFpexc  = 8;

    const uint32_t fpsp = (emit->ProcessorConfig()->mvfr0 >> 4) & 0xFu;
    const bool any_mode = d->crn == kFpscr
                       || (fpsp < 2u && (d->crn == kFpsid ||
                                         d->crn == kMvfr0 || d->crn == kMvfr1));
    if (!any_mode) {
        cursor = EmitRaiseUndIfUserMode(cursor, d, ctx);
    }

    if (d->l) {
        switch (d->crn) {
        case kFpsid:
            if (d->rd == 15) {
                return EmitRaiseUndAndReturn(cursor, d, ctx);
            }
            EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp,
                emit->ProcessorConfig()->fpsid);
            return cursor;

        case kFpscr:
            if (d->rd == 15) {
                EmitPushBaseDisp32(cursor, kStateReg,
                    static_cast<int32_t>(offsetof(ArmCpuState, fpscr)));
                EmitPush32(cursor,
                    static_cast<uint32_t>(
                        reinterpret_cast<uintptr_t>(emit->Cpu())));
                EmitCall(cursor, reinterpret_cast<void*>(
                    &ArmCpu::UpdateNzcvOnlyHelper));
                EmitAddRegImm32(cursor, kEsp, 8);
                return cursor;
            }
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, fpscr)));
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            return cursor;

        case kMvfr1:
            if (d->rd == 15) {
                return EmitRaiseUndAndReturn(cursor, d, ctx);
            }
            EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp,
                emit->ProcessorConfig()->mvfr1);
            return cursor;

        case kMvfr0:
            if (d->rd == 15) {
                return EmitRaiseUndAndReturn(cursor, d, ctx);
            }
            EmitMovBaseDisp32Imm32(cursor, kStateReg, rd_disp,
                emit->ProcessorConfig()->mvfr0);
            return cursor;

        case kFpexc:
            if (d->rd == 15) {
                return EmitRaiseUndAndReturn(cursor, d, ctx);
            }
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg,
                static_cast<int32_t>(offsetof(ArmCpuState, fpexc)));
            EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
            return cursor;
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    switch (d->crn) {
    case kFpsid:
    case kMvfr1:
    case kMvfr0:
        /* QEMU trans_VMSR_VMRS (translate-vfp.c). */
        return cursor;

    case kFpscr:
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
        if (((emit->ProcessorConfig()->mvfr0 >> 12) & 0xFu) == 0u) {
            EmitAndRegImm32(cursor, kEax, ~ArmVfp::kFpscrTrapEnableMask);
        }
        EmitMovBaseDisp32Reg(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, fpscr)), kEax);
        return cursor;

    case kFpexc:
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg,
            static_cast<int32_t>(offsetof(ArmCpuState, fpexc)), kEax);
        return cursor;
    }
    return EmitRaiseUndAndReturn(cursor, d, ctx);
}

}
