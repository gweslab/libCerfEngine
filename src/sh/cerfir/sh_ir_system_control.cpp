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
    b.Call(ShIrHostCall(reinterpret_cast<const void*>(helper), 1u), Type::kVoid, b.ConstPtr(c.mmu), kNoRef,
           kNoRef);
}

}

void ShIrTranslateLdtlb(ShIrContext& c) {
    CallMmu(c, &ShMmu::LoadTlbHelper);
    c.b->SetSlotConst(ShStateDisp(offsetof(ShCpuState, ldtlb_shadow)), 1u);
}

void ShIrTranslateSleep(ShIrContext& c) {
    c.b->Call(ShIrHostCall(reinterpret_cast<const void*>(&ShCpu::EnterSleepHelper), 1u), Type::kVoid,
              c.b->ConstPtr(c.cpu), kNoRef, kNoRef);
}

void ShIrTranslateTrapa(ShIrContext& c) {
    const ShDecodedInsn& d = *c.d;
    c.b->CallConst(ShIrHostCallConst(reinterpret_cast<const void*>(&ShCpu::TrapHelper)), 3u,
                   cerfir::CallHostAddress(c.cpu), d.guest_address + d.length, d.disp, 0u);
}

void ShIrTranslateClearSrBits(ShIrContext& c) {
    c.b->SlotImmInto(cerfir::Op::kAnd, ShSrDisp(), ~c.d->disp, ShSrDisp(), false);
}

void ShIrTranslateSetSrBits(ShIrContext& c) {
    c.b->SlotImmInto(cerfir::Op::kOr, ShSrDisp(), c.d->disp, ShSrDisp(), false);
}

void ShIrRestoreSrFromSsr(ShIrContext& c) {
    cerfir::Builder& b = *c.b;
    b.Call(ShIrHostCall(reinterpret_cast<const void*>(&ShCpu::ReturnFromExceptionHelper), 2u), Type::kVoid,
           b.ConstPtr(c.cpu), b.ReadSlot(ShStateDisp(offsetof(ShCpuState, ssr))), kNoRef);
    b.SetSlotConst(ShStateDisp(offsetof(ShCpuState, rte_slot)), 1u);
}

void ShIrTranslateRte(ShIrContext& c) {
    cerfir::Builder& b = *c.b;
    if (c.in_ldtlb_shadow) CallMmu(c, &ShMmu::ReturnAfterLdtlbHelper);
    c.branch_target = b.Snapshot(b.ReadSlot(ShStateDisp(offsetof(ShCpuState, spc))));
    ShIrRestoreSrFromSsr(c);
}

}
