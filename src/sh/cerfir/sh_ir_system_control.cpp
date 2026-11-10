#include "sh_ir_fns.h"

#include <cstddef>

#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_mmu.h>

namespace cerfengine {

using cerfir::kNoRef;
using cerfir::Type;

namespace {

using MmuHelper = void(CERFENGINE_CDECL*)(ShMmu* mmu);

void CallMmu(ShIrContext& c, MmuHelper helper) {
    cerfir::Builder& b = *c.b;
    cerfir::CallDesc call{reinterpret_cast<const void*>(helper), cerfir::CallConv::kCdecl, 1u};
    call.store_pc = true;
    call.pc       = c.d->guest_address;
    b.Call(call, Type::kVoid, b.ConstPtr(c.mmu), kNoRef, kNoRef);
}

}

void ShIrTranslateLdtlb(ShIrContext& c) {
    CallMmu(c, &ShMmu::LoadTlbHelper);
    c.b->SetSlotConst(ShStateDisp(offsetof(ShCpuState, ldtlb_shadow)), 1u);
}

void ShIrTranslateSleep(ShIrContext& c) {
    cerfir::CallDesc call{reinterpret_cast<const void*>(&ShCpu::EnterSleepHelper), cerfir::CallConv::kCdecl, 1u};
    call.store_pc = true;
    call.pc       = c.d->guest_address;
    c.b->Call(call, Type::kVoid, c.b->ConstPtr(c.cpu), kNoRef, kNoRef);
}

void ShIrTranslateClearSrBits(ShIrContext& c) {
    c.b->SlotImmInto(cerfir::Op::kAnd, ShSrDisp(), ~c.d->disp, ShSrDisp(), false);
}

void ShIrTranslateSetSrBits(ShIrContext& c) {
    c.b->SlotImmInto(cerfir::Op::kOr, ShSrDisp(), c.d->disp, ShSrDisp(), false);
}

void ShIrTranslateRte(ShIrContext& c) {
    cerfir::Builder& b = *c.b;
    if (c.in_ldtlb_shadow) CallMmu(c, &ShMmu::ReturnAfterLdtlbHelper);
    c.branch_target = b.Snapshot(b.ReadSlot(ShStateDisp(offsetof(ShCpuState, spc))));
    cerfir::CallDesc write{reinterpret_cast<const void*>(&ShCpu::WriteSrHelper), cerfir::CallConv::kCdecl, 2u};
    write.store_pc = true;
    write.pc       = c.d->guest_address;
    b.Call(write, Type::kVoid, b.ConstPtr(c.cpu), b.ReadSlot(ShStateDisp(offsetof(ShCpuState, ssr))), kNoRef);
    b.SetSlotConst(ShStateDisp(offsetof(ShCpuState, rte_slot)), 1u);
}

}
