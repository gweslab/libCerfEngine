#include "sh_ir_fns.h"

#include <array>

#include <cerfengine/sh/sh_cpu.h>

namespace cerfengine {

using cerfir::kNoRef;
using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

namespace {

constexpr uint32_t kBank0 = static_cast<uint32_t>(ShControlRegister::kBank0);

constexpr std::array<uint32_t, kBank0> ControlRegisterDisps() {
    std::array<uint32_t, kBank0> t{};
    t[static_cast<size_t>(ShControlRegister::kSr)]  = ShSrDisp();
    t[static_cast<size_t>(ShControlRegister::kGbr)] = ShGbrDisp();
    t[static_cast<size_t>(ShControlRegister::kVbr)] = ShStateDisp(offsetof(ShCpuState, vbr));
    t[static_cast<size_t>(ShControlRegister::kSsr)] = ShStateDisp(offsetof(ShCpuState, ssr));
    t[static_cast<size_t>(ShControlRegister::kSpc)] = ShStateDisp(offsetof(ShCpuState, spc));
    t[static_cast<size_t>(ShControlRegister::kMod)] = ShStateDisp(offsetof(ShCpuState, mod));
    t[static_cast<size_t>(ShControlRegister::kRs)]  = ShStateDisp(offsetof(ShCpuState, rs));
    t[static_cast<size_t>(ShControlRegister::kRe)]  = ShStateDisp(offsetof(ShCpuState, re));
    return t;
}

constexpr std::array<uint32_t, kBank0> kControlDisp = ControlRegisterDisps();

constexpr uint32_t ControlDisp(uint32_t which) {
    return which >= kBank0 ? ShStateDisp(offsetof(ShCpuState, bank)) + (which - kBank0) * 4u : kControlDisp[which];
}

void WriteControl(ShIrContext& c, Ref value) {
    cerfir::Builder& b     = *c.b;
    const uint32_t   which = c.d->control_register;
    if (which == static_cast<uint32_t>(ShControlRegister::kSr)) {
        b.Call(ShIrHostCall(reinterpret_cast<const void*>(&ShCpu::WriteSrHelper), 2u), Type::kVoid,
               b.ConstPtr(c.cpu), value, kNoRef);
    } else if (which == static_cast<uint32_t>(ShControlRegister::kSsr)) {
        b.SetSlot(ControlDisp(which), b.Op2(Op::kAnd, value, b.Const32(c.cpu->SrWritable())));
    } else {
        b.SetSlot(ControlDisp(which), value);
    }
}

}

void ShIrTranslateControlLoad(ShIrContext& c) {
    WriteControl(c, c.b->ReadSlot(ShGprDisp(c.d->m)));
}

void ShIrTranslateControlLoadPostInc(ShIrContext& c) {
    const Ref value = ShIrLoadValue(c, ShGprDisp(c.d->m));
    ShIrIncrementGpr(c, c.d->m);
    WriteControl(c, value);
}

void ShIrTranslateControlStore(ShIrContext& c) {
    c.b->SetSlot(ShGprDisp(c.d->n), c.b->ReadSlot(ControlDisp(c.d->control_register)));
}

void ShIrTranslateControlStorePreDec(ShIrContext& c) {
    ShIrStorePreDecrement(c, ControlDisp(c.d->control_register));
}

}
