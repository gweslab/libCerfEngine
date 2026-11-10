#include "sh_ir_fns.h"

namespace cerfengine {

using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

namespace {

Ref Halfword(cerfir::Builder& b, Op extend, uint32_t r) {
    return ShIrExtend(b, extend, Type::kI16, b.ReadSlot(ShGprDisp(r)));
}

void DoubleLength(ShIrContext& c, Op product) {
    cerfir::Builder& b = *c.b;
    const Ref        p = b.Wide64(product, b.ReadSlot(ShGprDisp(c.d->n)), b.ReadSlot(ShGprDisp(c.d->m)));
    b.SetSlot(ShMaclDisp(), b.Lo32(p));
    b.SetSlot(ShMachDisp(), b.Hi32(p));
}

}

void ShIrTranslateMultiply(ShIrContext& c) {
    cerfir::Builder&     b = *c.b;
    const ShDecodedInsn& d = *c.d;
    switch (d.multiply) {
    case ShMultiply::kMulL:
        b.SetSlot(ShMaclDisp(), b.Op2(Op::kMul, b.ReadSlot(ShGprDisp(d.n)), b.ReadSlot(ShGprDisp(d.m))));
        return;
    case ShMultiply::kMulsW:
        b.SetSlot(ShMaclDisp(), b.Op2(Op::kMul, Halfword(b, Op::kSignExtend, d.n), Halfword(b, Op::kSignExtend, d.m)));
        return;
    case ShMultiply::kMuluW:
        b.SetSlot(ShMaclDisp(), b.Op2(Op::kMul, Halfword(b, Op::kZeroExtend, d.n), Halfword(b, Op::kZeroExtend, d.m)));
        return;
    case ShMultiply::kDmulsL:
        DoubleLength(c, Op::kMulS64);
        return;
    case ShMultiply::kDmuluL:
        DoubleLength(c, Op::kMulU64);
        return;
    }
}

void ShIrTranslateClrmac(ShIrContext& c) {
    c.b->SetSlotConst(ShMachDisp(), 0u);
    c.b->SetSlotConst(ShMaclDisp(), 0u);
}

}
