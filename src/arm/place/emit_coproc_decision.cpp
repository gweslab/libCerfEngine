#include <cstddef>
#include <cstdint>

#include <cerfengine/arm/arm_cpu.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/arm_system_host.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

cerfengine::ArmCoprocDecision QueryCoprocDecision(DecodedInsn*              d,
                                                  BlockContext*             ctx,
                                                  cerfengine::ArmCoprocKind kind) {
    cerfengine::ArmCoprocAccess access;
    access.kind   = kind;
    access.cp_num = d->cp_num;
    access.read   = d->l != 0u;
    switch (kind) {
    case cerfengine::ArmCoprocKind::kRegisterTransfer:
        access.opc1 = d->cp_opc;
        access.crn  = d->crn;
        access.crm  = d->crm;
        access.opc2 = d->cp;
        access.rt   = d->rd;
        break;
    case cerfengine::ArmCoprocKind::kRegisterTransferDouble:
        access.opc1 = (static_cast<uint32_t>(d->offset) >> 4) & 0xFu;
        access.crm  = static_cast<uint32_t>(d->offset) & 0xFu;
        access.rt   = d->crd;
        access.rt2  = d->rn;
        break;
    case cerfengine::ArmCoprocKind::kDataTransfer:
        access.crd = d->crd;
        break;
    case cerfengine::ArmCoprocKind::kDataOperation:
        access.opc1 = d->cp_opc;
        access.crn  = d->crn;
        access.crm  = d->crm;
        access.opc2 = d->cp;
        access.crd  = d->crd;
        break;
    }
    return ctx->emit->System()->ArmCoprocessor(access);
}

uint8_t* EmitCoprocGate(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx,
                        uint32_t gate_mask) {
    using namespace x86;
    if (gate_mask == 0u) {
        return cursor;
    }
    EmitTestDwordBaseDisp32Imm32(cursor, kMmuReg,
        static_cast<int32_t>(offsetof(ArmMmuState, coprocessor_access)), gate_mask);
    uint8_t* enabled = EmitJnzLabel(cursor);
    cursor = EmitRaiseUndAndReturn(cursor, d, ctx);
    FixupLabel(enabled, cursor);
    return cursor;
}

/* ARM DDI 0406C.c A8.8.107 MRC Operation (p. A8-493): "if t != 15 then
   R[t] = value; else APSR.N = value<31>; APSR.Z = value<30>; APSR.C =
   value<29>; APSR.V = value<28>;". */
uint8_t* EmitCoprocReadResult(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    if (d->rd != ArmGpr::kR15) {
        EmitMovBaseDisp32Reg(cursor, kStateReg,
            ArmGprDisp(d->rd), kEax);
        return cursor;
    }
    EmitPushReg(cursor, kEax);
    EmitPush32(cursor, static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(ctx->emit->Cpu())));
    EmitCall(cursor, reinterpret_cast<void*>(&ArmCpu::UpdateNzcvOnlyHelper));
    EmitAddRegImm32(cursor, kEsp, 8);
    return cursor;
}

}
