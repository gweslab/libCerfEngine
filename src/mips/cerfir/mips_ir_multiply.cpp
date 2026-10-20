#include "mips_ir_fns.h"

#include "../mips_emit_services.h"
#include "../mips_wide_arithmetic.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

void Wide(MipsIrContext& c, Op op) {
    cerfir::Builder& b = *c.b;
    const Ref p = b.Wide64(op, MipsIrReadLo(c, c.d->rs), MipsIrReadLo(c, c.d->rt));
    MipsIrWriteHiLo(c, MipsLoDisp(), b.Lo32(p));
    MipsIrWriteHiLo(c, MipsHiDisp(), b.Hi32(p));
}

/* TMPR39xx-um p.155-156: t <- (HI || LO) + GPR[rs] * GPR[rt]; LO <- t31..0;
   HI <- t63..32; GPR[rd] <- t31..0. */
void MultiplyAdd(MipsIrContext& c, Op op) {
    cerfir::Builder& b   = *c.b;
    const Ref        p   = b.Wide64(op, MipsIrReadLo(c, c.d->rs), MipsIrReadLo(c, c.d->rt));
    const Ref        acc = b.Pair(b.ReadSlot(MipsLoDisp()), b.ReadSlot(MipsHiDisp()));
    const Ref        t   = b.Op2(Op::kAdd, p, acc);
    const Ref        lo  = b.Lo32(t);
    if (c.d->rd != 0u) b.Dup(lo);
    MipsIrWriteHiLo(c, MipsLoDisp(), lo);
    MipsIrWriteSext(c, c.d->rd, lo);
    MipsIrWriteHiLo(c, MipsHiDisp(), b.Hi32(t));
}

void WideHelper(MipsIrContext& c, const void* helper) {
    MipsIrFastcallConst(c, helper, 3u, c.d->rs, c.d->rt,
                        cerfir::CallHostAddress(c.block->emit->WideArithmetic()));
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
