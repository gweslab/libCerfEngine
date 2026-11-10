#include "sh_ir_fns.h"

namespace cerfengine {

using cerfir::kNoRef;
using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

namespace {

void WriteT(cerfir::Builder& b, Ref condition, bool inverted) {
    const Ref t  = b.Convert(Op::kZeroExtend, Type::kI32, condition);
    const Ref sr = inverted ? b.Op2(Op::kOr, b.ReadSlot(ShSrDisp()), b.Const32(kShSrT))
                            : b.Op2(Op::kAnd, b.ReadSlot(ShSrDisp()), b.Const32(~kShSrT));
    b.SetSlot(ShSrDisp(), b.Op2(inverted ? Op::kXor : Op::kOr, sr, t));
}

}

void ShIrSetT(cerfir::Builder& b, Ref condition) { WriteT(b, condition, false); }

void ShIrSetNotT(cerfir::Builder& b, Ref condition) { WriteT(b, condition, true); }

Ref ShIrTAsCarry(cerfir::Builder& b) {
    cerfir::FlagSet flags;
    b.ComputeOnlyFlags(Op::kLsrC, b.ReadSlot(ShSrDisp()), b.Const32(1u), b.Const1(false), cerfir::kWantC, &flags);
    return flags.c;
}

Ref ShIrTClear(cerfir::Builder& b) {
    cerfir::FlagSet flags;
    b.ComputeOnlyFlags(Op::kAnd, b.ReadSlot(ShSrDisp()), b.Const32(kShSrT), kNoRef, cerfir::kWantZ, &flags);
    return flags.z;
}

void ShIrTranslateCmp(ShIrContext& c) {
    cerfir::Builder&     b = *c.b;
    const ShDecodedInsn& d = *c.d;
    switch (d.cmp) {
    case ShCmp::kEq:
        ShIrSetT(b, b.Compare(Op::kCmpEq, b.ReadSlot(ShGprDisp(d.n)), b.ReadSlot(ShGprDisp(d.m))));
        return;
    case ShCmp::kGe:
        ShIrSetT(b, b.Compare(Op::kCmpLeS, b.ReadSlot(ShGprDisp(d.m)), b.ReadSlot(ShGprDisp(d.n))));
        return;
    case ShCmp::kGt:
        ShIrSetT(b, b.Compare(Op::kCmpLtS, b.ReadSlot(ShGprDisp(d.m)), b.ReadSlot(ShGprDisp(d.n))));
        return;
    case ShCmp::kHi:
        ShIrSetT(b, b.Compare(Op::kCmpLtU, b.ReadSlot(ShGprDisp(d.m)), b.ReadSlot(ShGprDisp(d.n))));
        return;
    case ShCmp::kHs: {
        cerfir::FlagSet flags;
        b.ComputeOnlyFlags(Op::kSub, b.ReadSlot(ShGprDisp(d.n)), b.ReadSlot(ShGprDisp(d.m)), kNoRef, cerfir::kWantC,
                           &flags);
        ShIrSetT(b, flags.c);
        return;
    }
    case ShCmp::kPl:
        ShIrSetT(b, b.Compare(Op::kCmpLtS, b.Const32(0u), b.ReadSlot(ShGprDisp(d.n))));
        return;
    case ShCmp::kPz:
        ShIrSetT(b, b.Compare(Op::kCmpLeS, b.Const32(0u), b.ReadSlot(ShGprDisp(d.n))));
        return;
    case ShCmp::kStr: {
        const Ref diff = b.Op2(Op::kXor, b.ReadSlot(ShGprDisp(d.n)), b.ReadSlot(ShGprDisp(d.m)));
        b.Dup(diff);
        const Ref borrow = b.Op2(Op::kSub, diff, b.Const32(0x01010101u));
        const Ref high   = b.Op2(Op::kAnd, b.Op2(Op::kAndNot, borrow, diff), b.Const32(0x80808080u));
        ShIrSetT(b, b.Compare(Op::kCmpNe, high, b.Const32(0u)));
        return;
    }
    case ShCmp::kEqImm:
        ShIrSetT(b, b.Compare(Op::kCmpEq, b.ReadSlot(ShGprDisp(0u)), b.Const32(d.disp)));
        return;
    }
}

}
