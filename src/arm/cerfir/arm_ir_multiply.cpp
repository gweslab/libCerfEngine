#include <cerfengine/arm/cpu_state.h>

#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::FlagSet;
using cerfir::Op;
using cerfir::Ref;
using cerfir::kNoRef;

void WriteNz(ArmIrContext& c, const FlagSet& f) {
    if (c.d->s == 0u) return;
    ArmIrWriteFlag(c, ArmIrFlag::kN, f.n);
    ArmIrWriteFlag(c, ArmIrFlag::kZ, f.z);
}

void WriteLong(ArmIrContext& c, Ref result) {
    cerfir::Builder& b = *c.b;
    ArmIrWriteGpr(c, c.d->rs, b.Op1(Op::kLo32, result));
    ArmIrWriteGpr(c, c.d->rd, b.Op1(Op::kHi32, result));
}

}

/* ARM DDI 0406C.c operations: MUL A8-503, MLA A8-481, MLS A8-483, UMAAL A8-775,
   UMULL A8-779, UMLAL A8-777, SMULL A8-647, SMLAL A8-625. */
void ArmIrMultiply(ArmIrContext& c) {
    const DecodedInsn& d    = *c.d;
    cerfir::Builder&   b    = *c.b;
    const Ref          n    = ArmIrReadGpr(c, d.rn);
    const Ref          m    = ArmIrReadGpr(c, d.rm);
    const uint8_t      want = d.s != 0u ? static_cast<uint8_t>(cerfir::kWantN | cerfir::kWantZ)
                                        : uint8_t{0};
    FlagSet f;
    switch (d.op1) {
    case 0u:
        ArmIrWriteGpr(c, d.rd, b.Compute(Op::kMul, n, m, kNoRef, want, &f));
        WriteNz(c, f);
        return;
    case 1u: {
        const Ref product = b.Op2(Op::kMul, n, m);
        const Ref addend  = ArmIrReadGpr(c, d.rs);
        ArmIrWriteGpr(c, d.rd, b.Compute(Op::kAdd, product, addend, kNoRef, want, &f));
        WriteNz(c, f);
        return;
    }
    case 2u: {
        const Ref zero = b.Const32(0u);
        Ref       sum  = b.Op2(Op::kMulU64, n, m);
        sum = b.Op2(Op::kAdd, sum, b.Op2(Op::kPair, ArmIrReadGpr(c, d.rs), zero));
        sum = b.Op2(Op::kAdd, sum, b.Op2(Op::kPair, ArmIrReadGpr(c, d.rd), zero));
        WriteLong(c, sum);
        return;
    }
    case 3u:
        ArmIrWriteGpr(c, d.rd, b.Op2(Op::kSub, ArmIrReadGpr(c, d.rs), b.Op2(Op::kMul, n, m)));
        return;
    default: {
        const Op  product_op = d.op1 >= 6u ? Op::kMulS64 : Op::kMulU64;
        const bool accumulate = d.op1 == 5u || d.op1 == 7u;
        Ref result = kNoRef;
        if (accumulate) {
            const Ref acc = b.Op2(Op::kPair, ArmIrReadGpr(c, d.rs), ArmIrReadGpr(c, d.rd));
            result = b.Compute(Op::kAdd, b.Op2(product_op, n, m), acc, kNoRef, want, &f);
        } else {
            result = b.Compute(product_op, n, m, kNoRef, want, &f);
        }
        WriteLong(c, result);
        WriteNz(c, f);
        return;
    }
    }
}

}
