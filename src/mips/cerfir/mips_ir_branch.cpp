#include "mips_ir_fns.h"

#include "../mips_emit_services.h"
#include "mips_ir_branch_state.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

/* QEMU MIPS gen_compute_branch (translate.c): btgt = PC + 4 + (offset << 2) for the
   PC-relative branches, ((PC + 4) & 0xF0000000) | (target << 2) for J/JAL/JALX. */
uint32_t PcRelTarget(const MipsDecodedInsn& d) {
    return d.guest_address + 4u + (MipsSignedImm(d) << 2);
}

uint32_t JumpTarget(const MipsDecodedInsn& d) {
    return ((d.guest_address + 4u) & 0xF0000000u) | (d.target << 2);
}

Ref Equality(MipsIrContext& c, Op op) {
    if (c.d->rs == c.d->rt) return c.b->Const1(op == Op::kCmpEq);
    return c.b->Compare(op, MipsIrReadFull(c, c.d->rs), MipsIrReadFull(c, c.d->rt));
}

cerfir::SlotTest EqualityTest(MipsIrContext& c, bool equal) {
    const MipsDecodedInsn& d = *c.d;
    cerfir::SlotTest       t{};
    t.if_true = equal;
    if (d.rs == 0u || d.rt == 0u) {
        t.pred    = c.wide_gprs ? cerfir::SlotPredicate::kZero64 : cerfir::SlotPredicate::kZero32;
        t.disp[0] = static_cast<int32_t>(MipsGprLoDisp(d.rs != 0u ? d.rs : d.rt));
        return t;
    }
    t.pred    = c.wide_gprs ? cerfir::SlotPredicate::kEqual64 : cerfir::SlotPredicate::kEqual32;
    t.disp[0] = static_cast<int32_t>(MipsGprLoDisp(d.rs));
    t.disp[1] = static_cast<int32_t>(MipsGprLoDisp(d.rt));
    return t;
}

Ref Top(MipsIrContext& c) {
    return c.wide_gprs ? MipsIrReadHi(c, c.d->rs) : MipsIrReadLo(c, c.d->rs);
}

Ref Negative(MipsIrContext& c) {
    return c.b->Compare(Op::kCmpLtS, Top(c), c.b->Const32(0u));
}

Ref NotNegative(MipsIrContext& c) {
    return c.b->Compare(Op::kCmpLtS, c.b->Const32(0xFFFFFFFFu), Top(c));
}

Ref Positive(MipsIrContext& c) {
    return c.b->Compare(Op::kCmpLtS, MipsIrFullConst(c, 0u), MipsIrReadFull(c, c.d->rs));
}

Ref NotPositive(MipsIrContext& c) {
    return c.b->Compare(Op::kCmpLeS, MipsIrReadFull(c, c.d->rs), MipsIrFullConst(c, 0u));
}

void Conditional(MipsIrContext& c, Ref taken, bool likely) {
    MipsIrBranchCond(c, taken, PcRelTarget(*c.d), likely);
}

void EqualityBranch(MipsIrContext& c, bool equal, bool likely) {
    const MipsDecodedInsn& d     = *c.d;
    const uint32_t         reads = (1u << d.rs) | (1u << d.rt);
    if (d.rs != d.rt && !c.lean_branch) {
        MipsIrBranchCondTest(c, EqualityTest(c, equal), PcRelTarget(d), likely);
        return;
    }
    if (c.lean_branch && d.rs != d.rt && (likely || (c.slot_writes & reads & ~1u) == 0u)) {
        MipsIrBranchDeferred(c, EqualityTest(c, equal), PcRelTarget(d), likely);
        return;
    }
    Conditional(c, Equality(c, equal ? Op::kCmpEq : Op::kCmpNe), likely);
}

void Link(MipsIrContext& c, uint32_t r) {
    MipsIrWriteSextConst(c, r, c.d->guest_address + 8u);
}

bool Mips16(MipsIrContext& c) { return c.block->emit->CpuConfig()->mips16; }

}

void MipsIrJ(MipsIrContext& c) { MipsIrBranchUncond(c, JumpTarget(*c.d), 0u); }

void MipsIrJal(MipsIrContext& c) {
    Link(c, 31u);
    MipsIrBranchUncond(c, JumpTarget(*c.d), 0u);
}

void MipsIrJalx(MipsIrContext& c) {
    Link(c, 31u);
    MipsIrBranchUncond(c, JumpTarget(*c.d), 1u);
}

void MipsIrJr(MipsIrContext& c) { MipsIrBranchRegister(c, c.d->rs, Mips16(c)); }

void MipsIrJalr(MipsIrContext& c) {
    MipsIrBranchRegister(c, c.d->rs, Mips16(c));
    Link(c, c.d->rd);
}

void MipsIrBeq(MipsIrContext& c) { EqualityBranch(c, true, false); }
void MipsIrBne(MipsIrContext& c) { EqualityBranch(c, false, false); }
void MipsIrBgtz(MipsIrContext& c) { Conditional(c, Positive(c), false); }
void MipsIrBlez(MipsIrContext& c) { Conditional(c, NotPositive(c), false); }
void MipsIrBltz(MipsIrContext& c) { Conditional(c, Negative(c), false); }
void MipsIrBgez(MipsIrContext& c) { Conditional(c, NotNegative(c), false); }
void MipsIrBeql(MipsIrContext& c) { EqualityBranch(c, true, true); }
void MipsIrBnel(MipsIrContext& c) { EqualityBranch(c, false, true); }
void MipsIrBgtzl(MipsIrContext& c) { Conditional(c, Positive(c), true); }
void MipsIrBlezl(MipsIrContext& c) { Conditional(c, NotPositive(c), true); }
void MipsIrBltzl(MipsIrContext& c) { Conditional(c, Negative(c), true); }
void MipsIrBgezl(MipsIrContext& c) { Conditional(c, NotNegative(c), true); }

void MipsIrBltzal(MipsIrContext& c) {
    Conditional(c, Negative(c), false);
    Link(c, 31u);
}

void MipsIrBgezal(MipsIrContext& c) {
    Conditional(c, NotNegative(c), false);
    Link(c, 31u);
}

}
