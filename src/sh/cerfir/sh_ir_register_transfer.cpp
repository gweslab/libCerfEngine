#include "sh_ir_fns.h"

namespace cerfengine {

using cerfir::Op;
using cerfir::Ref;

void ShIrTranslateMov(ShIrContext& c) {
    const ShDecodedInsn& d = *c.d;
    if (d.n != d.m) c.b->SetSlot(ShGprDisp(d.n), c.b->ReadSlot(ShGprDisp(d.m)));
}

void ShIrTranslateMovImm(ShIrContext& c) {
    c.b->SetSlotConst(ShGprDisp(c.d->n), c.d->disp);
}

void ShIrTranslateMova(ShIrContext& c) {
    cerfir::Builder& b = *c.b;
    b.SetSlot(ShGprDisp(0u), b.Op2(Op::kAdd, ShIrAlignedPcBase(c), b.Const32(c.d->disp)));
}

void ShIrTranslateMovt(ShIrContext& c) {
    cerfir::Builder& b = *c.b;
    b.SetSlot(ShGprDisp(c.d->n), b.Op2(Op::kAnd, b.ReadSlot(ShSrDisp()), b.Const32(kShSrT)));
}

void ShIrTranslateSwap(ShIrContext& c) {
    cerfir::Builder&     b      = *c.b;
    const ShDecodedInsn& d      = *c.d;
    const Ref            source = b.ReadSlot(ShGprDisp(d.m));
    if (d.width == 2u) {
        b.SetSlot(ShGprDisp(d.n), b.Op2(Op::kRor, source, b.Const32(16u)));
        return;
    }
    b.Dup(source);
    const Ref upper = b.Op2(Op::kAnd, source, b.Const32(0xFFFF0000u));
    const Ref lower = b.Op2(Op::kLsr, b.Op1(Op::kByteSwap, source), b.Const32(16u));
    b.SetSlot(ShGprDisp(d.n), b.Op2(Op::kOr, upper, lower));
}

void ShIrTranslateXtrct(ShIrContext& c) {
    cerfir::Builder&     b    = *c.b;
    const ShDecodedInsn& d    = *c.d;
    const Ref            high = b.Op2(Op::kShl, b.ReadSlot(ShGprDisp(d.m)), b.Const32(16u));
    const Ref            low  = b.Op2(Op::kLsr, b.ReadSlot(ShGprDisp(d.n)), b.Const32(16u));
    b.SetSlot(ShGprDisp(d.n), b.Op2(Op::kOr, high, low));
}

}
