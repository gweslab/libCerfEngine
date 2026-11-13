#include "sh_ir_fns.h"

namespace cerfengine {

using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

namespace {

constexpr uint8_t  kMacLongBytes        = 4u;
constexpr uint32_t kMacLongMaxHigh      = 0x00007FFFu;
constexpr uint32_t kMacLongMinHigh      = 0xFFFF8000u;
constexpr uint32_t kMacLongMaxLow       = 0xFFFFFFFFu;
constexpr uint32_t kMacLongMinLow       = 0x00000000u;
constexpr uint32_t kMacLongHighShift    = 16u;
constexpr uint32_t kMacWordOverflowMach = 0x00000001u;

Ref Halfword(cerfir::Builder& b, Op extend, uint32_t r) {
    return ShIrExtend(b, extend, Type::kI16, b.ReadSlot(ShGprDisp(r)));
}

void DoubleLength(ShIrContext& c, Op product) {
    cerfir::Builder& b = *c.b;
    const Ref        p = b.Wide64(product, b.ReadSlot(ShGprDisp(c.d->n)), b.ReadSlot(ShGprDisp(c.d->m)));
    b.SetSlot(ShMaclDisp(), b.Lo32(p));
    b.SetSlot(ShMachDisp(), b.Hi32(p));
}

Ref MacPlus(cerfir::Builder& b, Ref product, Ref accumulator_high) {
    return b.Op2(Op::kAdd, product, b.Op2(Op::kPair, b.ReadSlot(ShMaclDisp()), accumulator_high));
}

void StoreMac(cerfir::Builder& b, Ref saturating_high, Ref wrapping_high, Ref saturating_low, Ref wrapping_low) {
    const Ref saturate =
        b.Compare(Op::kCmpNe, b.Op2(Op::kAnd, b.ReadSlot(ShSrDisp()), b.Const32(kShSrS)), b.Const32(0u));
    b.Dup(saturate);
    b.SetSlot(ShMachDisp(), b.Op3(Op::kSelect, saturate, saturating_high, wrapping_high));
    b.SetSlot(ShMaclDisp(), b.Op3(Op::kSelect, saturate, saturating_low, wrapping_low));
}

void AccumulateLong(cerfir::Builder& b, Ref rn_operand, Ref rm_operand) {
    const Ref product = b.Wide64(Op::kMulS64, rn_operand, rm_operand);
    b.Dup(product);
    const Ref wrapping = MacPlus(b, product, b.ReadSlot(ShMachDisp()));
    const Ref accumulator_high =
        b.Op2(Op::kAsr, b.Op2(Op::kShl, b.ReadSlot(ShMachDisp()), b.Const32(kMacLongHighShift)),
              b.Const32(kMacLongHighShift));
    const Ref exact      = MacPlus(b, product, accumulator_high);
    const Ref exact_high = b.Hi32(exact);
    const Ref exact_low  = b.Lo32(exact);
    b.Dup(exact_high);
    b.Dup(exact_high);
    const Ref above = b.Compare(Op::kCmpLtS, b.Const32(kMacLongMaxHigh), exact_high);
    const Ref below = b.Compare(Op::kCmpLtS, exact_high, b.Const32(kMacLongMinHigh));
    b.Dup(above);
    b.Dup(below);
    const Ref saturating_high = b.Op3(Op::kSelect, above, b.Const32(kMacLongMaxHigh),
                                      b.Op3(Op::kSelect, below, b.Const32(kMacLongMinHigh), exact_high));
    const Ref saturating_low  = b.Op3(Op::kSelect, above, b.Const32(kMacLongMaxLow),
                                      b.Op3(Op::kSelect, below, b.Const32(kMacLongMinLow), exact_low));
    const Ref wrapping_high   = b.Hi32(wrapping);
    const Ref wrapping_low    = b.Lo32(wrapping);
    StoreMac(b, saturating_high, wrapping_high, saturating_low, wrapping_low);
}

void AccumulateWord(cerfir::Builder& b, Ref rn_operand, Ref rm_operand) {
    const Ref product = b.Wide64(Op::kMulS64, ShIrExtend(b, Op::kSignExtend, Type::kI16, rn_operand),
                                 ShIrExtend(b, Op::kSignExtend, Type::kI16, rm_operand));
    b.Dup(product);
    const Ref wrapping    = MacPlus(b, product, b.ReadSlot(ShMachDisp()));
    const Ref product_low = b.Lo32(product);
    b.DropHalf(product, true);
    const Ref saturating_low =
        b.Saturate(Op::kSatAdd, b.ReadSlot(ShMaclDisp()), product_low, ShMachDisp(), kMacWordOverflowMach);
    const Ref saturating_high = b.ReadSlot(ShMachDisp());
    const Ref wrapping_high   = b.Hi32(wrapping);
    const Ref wrapping_low    = b.Lo32(wrapping);
    StoreMac(b, saturating_high, wrapping_high, saturating_low, wrapping_low);
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

void ShIrTranslateMac(ShIrContext& c) {
    cerfir::Builder&     b          = *c.b;
    const ShDecodedInsn& d          = *c.d;
    const Ref            rn_operand = ShIrLoadAt(c, b.ReadSlot(ShGprDisp(d.n)));
    const Ref            rm_operand =
        ShIrLoadAt(c, d.n == d.m ? ShIrIncrementedGpr(c, d.n) : b.ReadSlot(ShGprDisp(d.m)));
    ShIrIncrementGpr(c, d.n);
    ShIrIncrementGpr(c, d.m);
    if (d.width == kMacLongBytes) {
        AccumulateLong(b, rn_operand, rm_operand);
    } else {
        AccumulateWord(b, rn_operand, rm_operand);
    }
}

}
