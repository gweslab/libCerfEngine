#include "sh_ir_fns.h"

#include <array>

namespace cerfengine {

using cerfir::Op;
using cerfir::Ref;

namespace {

constexpr std::array<uint32_t, 12> SystemRegisterDisps() {
    std::array<uint32_t, 12> t{};
    t[static_cast<size_t>(ShSystemRegister::kMach)] = ShMachDisp();
    t[static_cast<size_t>(ShSystemRegister::kMacl)] = ShMaclDisp();
    t[static_cast<size_t>(ShSystemRegister::kPr)]   = ShPrDisp();
    t[static_cast<size_t>(ShSystemRegister::kDsr)]  = ShStateDisp(offsetof(ShCpuState, dsr));
    t[static_cast<size_t>(ShSystemRegister::kA0)]   = ShStateDisp(offsetof(ShCpuState, a0));
    t[static_cast<size_t>(ShSystemRegister::kX0)]   = ShStateDisp(offsetof(ShCpuState, x0));
    t[static_cast<size_t>(ShSystemRegister::kX1)]   = ShStateDisp(offsetof(ShCpuState, x1));
    t[static_cast<size_t>(ShSystemRegister::kY0)]   = ShStateDisp(offsetof(ShCpuState, y0));
    t[static_cast<size_t>(ShSystemRegister::kY1)]   = ShStateDisp(offsetof(ShCpuState, y1));
    return t;
}

constexpr std::array<uint32_t, 12> kShSystemDisp = SystemRegisterDisps();

constexpr uint32_t ShSystemDisp(ShSystemRegister r) { return kShSystemDisp[static_cast<size_t>(r)]; }

constexpr uint32_t kShDsrBits        = 0x0000000Fu;
constexpr uint32_t kShA0GuardBits    = 0x000000FFu;
constexpr uint32_t kShSignToGuardAsr = 31u;

void CompleteSystemLoad(ShIrContext& c) {
    cerfir::Builder&       b = *c.b;
    const ShSystemRegister r = c.d->system_register;
    if (r == ShSystemRegister::kDsr) {
        const uint32_t dsr = ShSystemDisp(r);
        b.SetSlot(dsr, b.Op2(Op::kAnd, b.ReadSlot(dsr), b.Const32(kShDsrBits)));
    } else if (r == ShSystemRegister::kA0) {
        const Ref sign = b.Op2(Op::kAsr, b.ReadSlot(ShSystemDisp(r)), b.Const32(kShSignToGuardAsr));
        b.SetSlot(ShStateDisp(offsetof(ShCpuState, a0g)), b.Op2(Op::kAnd, sign, b.Const32(kShA0GuardBits)));
    }
}

}

void ShIrTranslateSystemLoad(ShIrContext& c) {
    c.b->SetSlot(ShSystemDisp(c.d->system_register), c.b->ReadSlot(ShGprDisp(c.d->m)));
    CompleteSystemLoad(c);
}

void ShIrTranslateSystemLoadPostInc(ShIrContext& c) {
    ShIrLoadPostIncrement(c, ShSystemDisp(c.d->system_register));
    CompleteSystemLoad(c);
}

void ShIrTranslateSystemStore(ShIrContext& c) {
    c.b->SetSlot(ShGprDisp(c.d->n), c.b->ReadSlot(ShSystemDisp(c.d->system_register)));
}

void ShIrTranslateSystemStorePreDec(ShIrContext& c) {
    ShIrStorePreDecrement(c, ShSystemDisp(c.d->system_register));
}

}
