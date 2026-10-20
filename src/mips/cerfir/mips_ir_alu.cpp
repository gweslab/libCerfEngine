#include "mips_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

void CopySext(MipsIrContext& c, uint32_t dest, Ref src) {
    if (cerfir::IsSlotRef(src)) {
        c.b->SlotCopy32(MipsGprLoDisp(dest), static_cast<uint32_t>(cerfir::SlotRefDisp(src)), true);
    } else {
        c.b->SetSlotSext(MipsGprLoDisp(dest), src);
    }
}

void Arith32(MipsIrContext& c, Op op, uint32_t dest, Ref b) {
    if (dest == 0u) return;
    if (c.d->rs == 0u && op == Op::kAdd) {
        CopySext(c, dest, b);
        return;
    }
    if (cerfir::IsConstRef(b) && c.b->GetBlock().ConstValue(b) == 0u) {
        CopySext(c, dest, MipsIrReadLo(c, c.d->rs));
        return;
    }
    if (c.d->rs == 0u) {
        c.b->ComputeIntoSext(op, c.b->Const32(0u), b, MipsGprLoDisp(dest));
        return;
    }
    c.b->ComputeSlotInto<true>(op, MipsGprLoDisp(c.d->rs), b, 0u, MipsGprLoDisp(dest));
}

void Copy64(MipsIrContext& c, uint32_t dest, uint32_t src) {
    if (dest == src) return;
    MipsIrCopy64(c, dest, src);
}

void Bitwise64(MipsIrContext& c, Op op) {
    const MipsDecodedInsn& d = *c.d;
    if (d.rd == 0u) return;
    if (op != Op::kAnd && (d.rs == 0u || d.rt == 0u)) {
        Copy64(c, d.rd, d.rs == 0u ? d.rt : d.rs);
        return;
    }
    cerfir::Builder& b = *c.b;
    if (d.rs != 0u && d.rt != 0u) {
        b.SlotSlotInto(op, MipsGprLoDisp(d.rs), MipsGprLoDisp(d.rt), MipsGprLoDisp(d.rd), false);
        b.SlotSlotInto(op, MipsGprHiDisp(d.rs), MipsGprHiDisp(d.rt), MipsGprHiDisp(d.rd), false);
        return;
    }
    b.ComputeInto(op, MipsIrReadLo(c, d.rs), MipsIrReadLo(c, d.rt), cerfir::kNoRef, 0u,
                  MipsGprLoDisp(d.rd));
    b.ComputeInto(op, MipsIrReadHi(c, d.rs), MipsIrReadHi(c, d.rt), cerfir::kNoRef, 0u,
                  MipsGprHiDisp(d.rd));
}

void ArithReg32(MipsIrContext& c, Op op) {
    const MipsDecodedInsn& d = *c.d;
    if (d.rd == 0u || d.rs == 0u || d.rt == 0u) {
        Arith32(c, op, d.rd, MipsIrReadLo(c, d.rt));
        return;
    }
    c.b->SlotSlotInto(op, MipsGprLoDisp(d.rs), MipsGprLoDisp(d.rt), MipsGprLoDisp(d.rd), true);
}

void Immediate64(MipsIrContext& c, Op op, bool keep_hi) {
    const MipsDecodedInsn& d = *c.d;
    if (d.rt == 0u) return;
    cerfir::Builder& b = *c.b;
    if (op != Op::kAnd && d.imm == 0u) {
        Copy64(c, d.rt, d.rs);
        return;
    }
    if (d.rs == 0u) {
        b.SetSlotConst(MipsGprLoDisp(d.rt), op == Op::kAnd ? 0u : d.imm);
        b.SetSlotConst(MipsGprHiDisp(d.rt), 0u);
        return;
    }
    b.SlotImmInto(op, MipsGprLoDisp(d.rs), d.imm, MipsGprLoDisp(d.rt), false);
    if (!keep_hi) {
        b.SetSlotConst(MipsGprHiDisp(d.rt), 0u);
    } else if (d.rt != d.rs) {
        b.SetSlot(MipsGprHiDisp(d.rt), MipsIrReadHi(c, d.rs));
    }
}

void SetLessThan(MipsIrContext& c, Op op, uint32_t dest, Ref rhs) {
    if (dest == 0u) return;
    cerfir::Builder& b    = *c.b;
    const Ref        less = b.Compare(op, MipsIrReadFull(c, c.d->rs), rhs);
    MipsIrWriteZext(c, dest, b.Convert(Op::kZeroExtend, Type::kI32, less));
}

void ConditionalMove(MipsIrContext& c, Op skip_if) {
    const MipsDecodedInsn& d = *c.d;
    if (d.rd == 0u) return;
    cerfir::Builder& b    = *c.b;
    const uint32_t   skip = b.NewLabel();
    b.Branch(b.Compare(skip_if, MipsIrRead64(c, d.rt), MipsIrConst64(c, 0u)), skip, true);
    MipsIrWrite64(c, d.rd, MipsIrReadLo(c, d.rs), MipsIrReadHi(c, d.rs));
    b.Label(skip);
}

}

void MipsIrNop(MipsIrContext&) {}

void MipsIrAddu(MipsIrContext& c) { ArithReg32(c, Op::kAdd); }
void MipsIrSubu(MipsIrContext& c) { ArithReg32(c, Op::kSub); }

void MipsIrAddiu(MipsIrContext& c) {
    const MipsDecodedInsn& d = *c.d;
    if (d.rs == 0u) {
        MipsIrWriteSextConst(c, d.rt, MipsSignedImm(d));
        return;
    }
    if (d.rt == 0u || MipsSignedImm(d) == 0u) {
        Arith32(c, Op::kAdd, d.rt, c.b->Const32(MipsSignedImm(d)));
        return;
    }
    c.b->SlotImmInto(Op::kAdd, MipsGprLoDisp(d.rs), MipsSignedImm(d), MipsGprLoDisp(d.rt), true);
}

void MipsIrLui(MipsIrContext& c) {
    MipsIrWriteSextConst(c, c.d->rt, c.d->imm << 16);
}

void MipsIrOri(MipsIrContext& c) { Immediate64(c, Op::kOr, true); }
void MipsIrXori(MipsIrContext& c) { Immediate64(c, Op::kXor, true); }
void MipsIrAndi(MipsIrContext& c) { Immediate64(c, Op::kAnd, false); }

void MipsIrAnd(MipsIrContext& c) { Bitwise64(c, Op::kAnd); }
void MipsIrOr(MipsIrContext& c) { Bitwise64(c, Op::kOr); }
void MipsIrXor(MipsIrContext& c) { Bitwise64(c, Op::kXor); }

void MipsIrNor(MipsIrContext& c) {
    const MipsDecodedInsn& d = *c.d;
    if (d.rd == 0u) return;
    cerfir::Builder& b  = *c.b;
    const Ref lo = b.Op1(Op::kNot, b.Op2(Op::kOr, MipsIrReadLo(c, d.rs), MipsIrReadLo(c, d.rt)));
    const Ref hi = b.Op1(Op::kNot, b.Op2(Op::kOr, MipsIrReadHi(c, d.rs), MipsIrReadHi(c, d.rt)));
    MipsIrWrite64(c, d.rd, lo, hi);
}

void MipsIrSlt(MipsIrContext& c) {
    SetLessThan(c, Op::kCmpLtS, c.d->rd, MipsIrReadFull(c, c.d->rt));
}

void MipsIrSltu(MipsIrContext& c) {
    SetLessThan(c, Op::kCmpLtU, c.d->rd, MipsIrReadFull(c, c.d->rt));
}

void MipsIrSlti(MipsIrContext& c) {
    const uint64_t imm = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(c.d->imm)));
    SetLessThan(c, Op::kCmpLtS, c.d->rt, MipsIrFullConst(c, imm));
}

void MipsIrSltiu(MipsIrContext& c) {
    const uint64_t imm = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(c.d->imm)));
    SetLessThan(c, Op::kCmpLtU, c.d->rt, MipsIrFullConst(c, imm));
}

void MipsIrMovz(MipsIrContext& c) { ConditionalMove(c, Op::kCmpNe); }
void MipsIrMovn(MipsIrContext& c) { ConditionalMove(c, Op::kCmpEq); }

}
