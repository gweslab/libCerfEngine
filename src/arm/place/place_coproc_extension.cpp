#include <cstddef>
#include <cstdint>

#include <cerfengine/engine.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_system_host.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

namespace {

using cerfengine::ArmCoprocAction;
using cerfengine::ArmCoprocDecision;

uint8_t* EmitCoprocDouble(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    const ArmCoprocDecision dec = QueryCoprocDecision(
        d, ctx, cerfengine::ArmCoprocKind::kRegisterTransferDouble);
    cursor = EmitCoprocGate(cursor, d, ctx, dec.gate_mask);

    const int32_t rt_disp  = ArmGprDisp(d->crd);
    const int32_t rt2_disp = ArmGprDisp(d->rn);

    switch (dec.action) {
    case ArmCoprocAction::kUndefined:
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    case ArmCoprocAction::kRegister40: {
        if (dec.reg == nullptr) break;
        const uint8_t* const high = reinterpret_cast<const uint8_t*>(dec.reg) + 4;
        if (d->l != 0u) {
            EmitMovRegDwordPtr(cursor, kEax, dec.reg);
            EmitMovBaseDisp32Reg(cursor, kStateReg, rt_disp, kEax);
            EmitMovsxRegBytePtr(cursor, kEax, high);
            EmitMovBaseDisp32Reg(cursor, kStateReg, rt2_disp, kEax);
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rt_disp);
            EmitMovDwordPtrReg(cursor, dec.reg, kEax);
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rt2_disp);
            EmitMovBytePtrReg8(cursor, high, kAl);
        }
        return cursor;
    }
    default:
        break;
    }
    ctx->emit->Owner().Panic(
        "coprocessor decision for %s p%u at guest pc=0x%08X has no two-word shape",
        d->l != 0u ? "MRRC" : "MCRR", d->cp_num, d->guest_address);
}

}

uint8_t* PlaceCoprocExtension(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx) {
    using namespace x86;

    bool unconditional_und = false;
    cursor = PlaceCoprocessorPermissionCheck(cursor, d, ctx, false, &unconditional_und);
    if (unconditional_und) return cursor;

    if (d->cp_num != 10u && d->cp_num != 11u) {
        return EmitCoprocDouble(cursor, d, ctx);
    }

    const uint32_t opc1 = (d->offset >> 4) & 0xFu;
    if ((opc1 & 0xDu) != 0x1u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    const uint32_t M      = (opc1 >> 1) & 1u;
    const uint32_t Vm     = d->offset & 0xFu;
    const bool     single = d->cp_num == 10u;
    const uint32_t m      = single ? ((Vm << 1) | M) : ((M << 4) | Vm);
    if (single && m == 31u) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    const uint32_t Rt  = d->crd;
    const uint32_t Rt2 = d->rn;
    const bool to_arm  = d->l != 0u;

    const int32_t rt_disp  = ArmGprDisp(Rt);
    const int32_t rt2_disp = ArmGprDisp(Rt2);
    const int32_t lo_disp = static_cast<int32_t>(
        offsetof(ArmCpuState, vfp_d) + (single ? m * 4u : m * 8u));
    const int32_t hi_disp = lo_disp + 4;

    if (to_arm) {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, lo_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg, rt_disp,  kEax);
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, hi_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg, rt2_disp, kEax);
    } else {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rt_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg, lo_disp, kEax);
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rt2_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg, hi_disp, kEax);
    }
    return cursor;
}

}
