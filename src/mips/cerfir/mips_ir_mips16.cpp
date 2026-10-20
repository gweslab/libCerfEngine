#include "mips_ir_fns.h"

#include "mips_ir_branch_state.h"

namespace cerfengine {

namespace {

using cerfir::Op;

void CompactBranch(MipsIrContext& c, Op taken_if) {
    const MipsDecodedInsn& d     = *c.d;
    cerfir::Builder&       b     = *c.b;
    const uint32_t         taken = b.NewLabel();
    b.Branch(b.Compare(taken_if, MipsIrReadFull(c, d.rs), MipsIrFullConst(c, 0u)), taken, true);
    b.ExitDirect(d.guest_address + d.length, 1u, true);
    b.Label(taken);
    b.ExitDirect(d.target, 0u, true);
}

void LinkMips16(MipsIrContext& c, uint32_t offset) {
    MipsIrWriteSextConst(c, 31u, (c.d->guest_address + offset) | 1u);
}

}

void MipsIrMips16B(MipsIrContext& c) { c.b->ExitDirect(c.d->target, 0u, true); }
void MipsIrMips16Beqz(MipsIrContext& c) { CompactBranch(c, Op::kCmpEq); }
void MipsIrMips16Bnez(MipsIrContext& c) { CompactBranch(c, Op::kCmpNe); }

void MipsIrMips16Jal(MipsIrContext& c) {
    LinkMips16(c, 6u);
    MipsIrBranchUncond(c, c.d->target, 1u);
}

void MipsIrMips16Jalx(MipsIrContext& c) {
    LinkMips16(c, 6u);
    MipsIrBranchUncond(c, c.d->target, 0u);
}

void MipsIrMips16Jalr(MipsIrContext& c) {
    MipsIrBranchRegister(c, c.d->rs, true);
    LinkMips16(c, 4u);
}

void MipsIrMips16Addiupc(MipsIrContext& c) {
    MipsIrWriteSextConst(c, c.d->rt, c.d->target);
}

}
