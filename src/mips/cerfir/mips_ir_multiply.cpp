#include "mips_ir_fns.h"

#include "../mips_emit_services.h"
#include "../mips_wide_arithmetic.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

constexpr uint32_t kAccumulateUnsigned = 1u << 0;
constexpr uint32_t kAccumulateHi       = 1u << 3;
constexpr uint32_t kAccumulateSaturate = 1u << 4;
constexpr uint32_t kWordSignShift      = 31u;
constexpr uint32_t kAllOnes            = 0xFFFFFFFFu;
constexpr uint32_t kSignedWordMax      = 0x7FFFFFFFu;
constexpr uint32_t kSignedWordMin      = 0x80000000u;

struct WordPair {
    Ref low;
    Ref high;
};

void Wide(MipsIrContext& c, Op op) {
    cerfir::Builder& b = *c.b;
    const Ref p = b.Wide64(op, MipsIrReadLo(c, c.d->rs), MipsIrReadLo(c, c.d->rt));
    MipsIrWriteHiLo(c, MipsLoDisp(), b.Lo32(p));
    MipsIrWriteHiLo(c, MipsHiDisp(), b.Hi32(p));
}

Ref AccumulatedSum(MipsIrContext& c, Op multiply, uint32_t accumulator_high_disp) {
    cerfir::Builder& b   = *c.b;
    const Ref        p   = b.Wide64(multiply, MipsIrReadLo(c, c.d->rs), MipsIrReadLo(c, c.d->rt));
    const Ref        acc = b.Pair(b.ReadSlot(MipsLoDisp()), b.ReadSlot(accumulator_high_disp));
    return b.Op2(Op::kAdd, p, acc);
}

WordPair Split(cerfir::Builder& b, Ref sum) {
    const Ref low  = b.Lo32(sum);
    const Ref high = b.Hi32(sum);
    return {low, high};
}

WordPair SaturateToWord(cerfir::Builder& b, Ref sum, bool unsigned_values) {
    const WordPair exact = Split(b, sum);
    if (unsigned_values) {
        const Ref over = b.Compare(Op::kCmpNe, exact.high, b.Const32(0u));
        b.Dup(over);
        const Ref low  = b.Op3(Op::kSelect, over, b.Const32(kAllOnes), exact.low);
        const Ref high = b.Op3(Op::kSelect, over, b.Const32(kAllOnes), b.Const32(0u));
        return {low, high};
    }
    b.Dup(exact.low);
    b.Dup(exact.high);
    const Ref fits =
        b.Compare(Op::kCmpEq, exact.high, b.Op2(Op::kAsr, exact.low, b.Const32(kWordSignShift)));
    const Ref negative = b.Compare(Op::kCmpLtS, exact.high, b.Const32(0u));
    const Ref bound =
        b.Op3(Op::kSelect, negative, b.Const32(kSignedWordMin), b.Const32(kSignedWordMax));
    const Ref low = b.Op3(Op::kSelect, fits, exact.low, bound);
    b.Dup(low);
    const Ref high = b.Op2(Op::kAsr, low, b.Const32(kWordSignShift));
    return {low, high};
}

void WriteAccumulator(MipsIrContext& c, const WordPair& words, bool rd_from_hi) {
    const Ref result = rd_from_hi ? words.high : words.low;
    if (c.d->rd != 0u) c.b->Dup(result);
    MipsIrWriteHiLo(c, MipsLoDisp(), words.low);
    MipsIrWriteHiLo(c, MipsHiDisp(), words.high);
    MipsIrWriteSext(c, c.d->rd, result);
}

void StoreLo64(MipsIrContext& c, const WordPair& words) {
    c.b->SetSlot(MipsLoDisp(), words.low);
    c.b->SetSlot(MipsLoDisp() + 4u, words.high);
}

void MultiplyAdd(MipsIrContext& c, Op op) {
    WriteAccumulator(c, Split(*c.b, AccumulatedSum(c, op, MipsHiDisp())), false);
}

void WideHelper(MipsIrContext& c, const void* helper) {
    MipsIrFastcallConst(c, helper, 3u, c.d->rs, c.d->rt,
                        cerfir::CallHostAddress(c.block->emit->WideArithmetic()));
}

bool HasOption(const MipsIrContext& c, uint32_t option) { return (c.d->sa & option) != 0u; }

Op AccumulateMultiply(const MipsIrContext& c) {
    return HasOption(c, kAccumulateUnsigned) ? Op::kMulU64 : Op::kMulS64;
}

WordPair AccumulateWords(MipsIrContext& c, uint32_t accumulator_high_disp) {
    const Ref sum = AccumulatedSum(c, AccumulateMultiply(c), accumulator_high_disp);
    if (!HasOption(c, kAccumulateSaturate)) return Split(*c.b, sum);
    return SaturateToWord(*c.b, sum, HasOption(c, kAccumulateUnsigned));
}

void MoveFrom(MipsIrContext& c, uint32_t disp) {
    const uint32_t rd = c.d->rd;
    if (rd == 0u) return;
    c.b->SlotCopy64(MipsGprLoDisp(rd), disp);
}

void MoveTo(MipsIrContext& c, uint32_t disp) {
    const uint32_t rs = c.d->rs;
    c.b->SetSlot(disp, MipsIrReadLo(c, rs));
    c.b->SetSlot(disp + 4u, MipsIrReadHi(c, rs));
}

}

void MipsIrMult(MipsIrContext& c) { Wide(c, Op::kMulS64); }
void MipsIrMultu(MipsIrContext& c) { Wide(c, Op::kMulU64); }
void MipsIrMadd(MipsIrContext& c) { MultiplyAdd(c, Op::kMulS64); }
void MipsIrMaddu(MipsIrContext& c) { MultiplyAdd(c, Op::kMulU64); }

void MipsIrMacc(MipsIrContext& c) {
    WriteAccumulator(c, AccumulateWords(c, MipsHiDisp()), HasOption(c, kAccumulateHi));
}

void MipsIrDmacc(MipsIrContext& c) {
    StoreLo64(c, AccumulateWords(c, MipsLoDisp() + 4u));
    MoveFrom(c, HasOption(c, kAccumulateHi) ? MipsHiDisp() : MipsLoDisp());
}

void MipsIrMadd16(MipsIrContext& c) {
    cerfir::Builder& b = *c.b;
    const Ref p = b.Wide64(Op::kMulS64, MipsIrReadLo(c, c.d->rs), MipsIrReadLo(c, c.d->rt));
    b.Dup(p);
    const Ref acc         = b.Pair(b.ReadSlot(MipsLoDisp()), b.ReadSlot(MipsHiDisp()));
    const Ref t           = b.Op2(Op::kAdd, p, acc);
    const Ref product_low = b.Lo32(p);
    b.DropHalf(p, true);
    b.SetSlot(MipsLoDisp(), b.Lo32(t));
    b.SetSlot(MipsLoDisp() + 4u, b.Op2(Op::kAsr, product_low, b.Const32(kWordSignShift)));
    MipsIrWriteHiLo(c, MipsHiDisp(), b.Hi32(t));
}

void MipsIrDmadd16(MipsIrContext& c) {
    StoreLo64(c, Split(*c.b, AccumulatedSum(c, Op::kMulS64, MipsLoDisp() + 4u)));
}

void MipsIrDiv(MipsIrContext& c) { Wide(c, Op::kDivRemS32); }
void MipsIrDivu(MipsIrContext& c) { Wide(c, Op::kDivRemU32); }

void MipsIrDmult(MipsIrContext& c) {
    WideHelper(c, reinterpret_cast<const void*>(&MipsWideArithmetic::DmultHelper));
}

void MipsIrDmultu(MipsIrContext& c) {
    WideHelper(c, reinterpret_cast<const void*>(&MipsWideArithmetic::DmultuHelper));
}

void MipsIrDdiv(MipsIrContext& c) {
    WideHelper(c, reinterpret_cast<const void*>(&MipsWideArithmetic::DdivHelper));
}

void MipsIrDdivu(MipsIrContext& c) {
    WideHelper(c, reinterpret_cast<const void*>(&MipsWideArithmetic::DdivuHelper));
}

void MipsIrMfhi(MipsIrContext& c) { MoveFrom(c, MipsHiDisp()); }
void MipsIrMflo(MipsIrContext& c) { MoveFrom(c, MipsLoDisp()); }
void MipsIrMthi(MipsIrContext& c) { MoveTo(c, MipsHiDisp()); }
void MipsIrMtlo(MipsIrContext& c) { MoveTo(c, MipsLoDisp()); }

}
