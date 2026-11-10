#include "sh_ir_fns.h"

#include <cstddef>

namespace cerfengine {

using cerfir::Op;
using cerfir::Ref;

namespace {

constexpr Op kLogicOp[] = {Op::kAnd, Op::kOr, Op::kXor, Op::kAnd};

Ref Combine(cerfir::Builder& b, ShLogic logic, Ref a, Ref v) {
    return b.Op2(kLogicOp[static_cast<size_t>(logic)], a, v);
}

void TestZero(cerfir::Builder& b, Ref value) {
    ShIrSetT(b, b.Compare(Op::kCmpEq, value, b.Const32(0u)));
}

void Apply(ShIrContext& c, uint32_t dest_slot, Ref a, Ref v) {
    cerfir::Builder& b      = *c.b;
    const Ref        result = Combine(b, c.d->logic, a, v);
    if (c.d->logic == ShLogic::kTest) {
        TestZero(b, result);
    } else {
        b.SetSlot(dest_slot, result);
    }
}

}

void ShIrTranslateLogic(ShIrContext& c) {
    cerfir::Builder&     b = *c.b;
    const ShDecodedInsn& d = *c.d;
    Apply(c, ShGprDisp(d.n), b.ReadSlot(ShGprDisp(d.n)), b.ReadSlot(ShGprDisp(d.m)));
}

void ShIrTranslateLogicImm(ShIrContext& c) {
    cerfir::Builder& b = *c.b;
    Apply(c, ShGprDisp(0u), b.ReadSlot(ShGprDisp(0u)), b.Const32(c.d->disp));
}

void ShIrTranslateLogicGbr(ShIrContext& c) {
    cerfir::Builder&     b       = *c.b;
    const ShDecodedInsn& d       = *c.d;
    const Ref            address = b.Op2(Op::kAdd, b.ReadSlot(ShGprDisp(0u)), b.ReadSlot(ShGbrDisp()));
    if (d.logic == ShLogic::kTest) {
        TestZero(b, Combine(b, d.logic, ShIrLoadAt(c, address), b.Const32(d.disp)));
        return;
    }
    b.Dup(address);
    ShIrStoreAt(c, address, Combine(b, d.logic, ShIrLoadAt(c, address), b.Const32(d.disp)));
}

void ShIrTranslateNot(ShIrContext& c) {
    cerfir::Builder&     b = *c.b;
    const ShDecodedInsn& d = *c.d;
    b.SetSlot(ShGprDisp(d.n), b.Op1(Op::kNot, b.ReadSlot(ShGprDisp(d.m))));
}

void ShIrTranslateTas(ShIrContext& c) {
    cerfir::Builder& b       = *c.b;
    const Ref        address = b.ReadSlot(ShGprDisp(c.d->n));
    const Ref        value   = ShIrLoadAt(c, address);
    b.Dup(value);
    ShIrStoreAt(c, address, b.Op2(Op::kOr, value, b.Const32(0x80u)));
    TestZero(b, value);
}

}
