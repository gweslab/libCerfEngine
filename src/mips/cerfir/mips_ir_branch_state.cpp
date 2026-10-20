#include "mips_ir_branch_state.h"

#include "../../emit_fault.h"

namespace cerfengine {

using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

void MipsIrBranchUncond(MipsIrContext& c, uint32_t target, uint32_t isa) {
    cerfir::Builder& b = *c.b;
    c.branch           = MipsIrBranch{MipsBranch::kUncond, target, isa, false, false, c.lean_branch};
    if (c.lean_branch) return;
    b.SetSlotConst(MipsBtargetDisp(), target);
    b.SetSlotConst(MipsBtargetIsaDisp(), isa);
    b.SetSlotConst(MipsBranchLenDisp(), c.d->length);
    b.SetSlotConst(MipsBranchStateDisp(), MipsBranch::kUncond);
}

void MipsIrBranchRegister(MipsIrContext& c, uint32_t rs, bool mips16) {
    cerfir::Builder& b = *c.b;
    c.branch           = MipsIrBranch{MipsBranch::kRegister, 0u, 0u, false, false, c.lean_branch};
    if (c.lean_branch) {
        if (mips16) {
            const Ref value = b.Snapshot(MipsIrReadLo(c, rs));
            b.Dup(value);
            b.SetSlot(MipsIsaModeDisp(), b.Op2(Op::kAnd, value, b.Const32(1u)));
            b.SetSlot(MipsPcDisp(), b.Op2(Op::kAnd, value, b.Const32(~1u)));
        } else {
            b.SetSlot(MipsPcDisp(), MipsIrReadLo(c, rs));
            b.SetSlotConst(MipsIsaModeDisp(), 0u);
        }
        return;
    }
    if (mips16) {
        b.SetSlot(MipsBtargetIsaDisp(), b.Op2(Op::kAnd, MipsIrReadLo(c, rs), b.Const32(1u)));
        b.SetSlot(MipsBtargetDisp(), b.Op2(Op::kAnd, MipsIrReadLo(c, rs), b.Const32(~1u)));
    } else {
        b.SetSlotConst(MipsBtargetIsaDisp(), 0u);
        b.SetSlot(MipsBtargetDisp(), MipsIrReadLo(c, rs));
    }
    b.SetSlotConst(MipsBranchLenDisp(), c.d->length);
    b.SetSlotConst(MipsBranchStateDisp(), MipsBranch::kRegister);
}

void MipsIrBranchCond(MipsIrContext& c, Ref cond, uint32_t target, bool likely) {
    cerfir::Builder& b     = *c.b;
    const uint32_t   kind  = likely ? MipsBranch::kCondLikely : MipsBranch::kCond;
    const bool       known = cerfir::IsConstRef(cond);
    c.branch = MipsIrBranch{kind, target, 0u, known,
                            known && b.GetBlock().ConstValue(cond) != 0u, c.lean_branch};
    if (c.lean_branch) {
        c.branch.cond = cond;
        return;
    }
    b.SetSlot(MipsBcondDisp(), b.Convert(Op::kZeroExtend, Type::kI32, cond));
    b.SetSlotConst(MipsBtargetDisp(), target);
    b.SetSlotConst(MipsBranchLenDisp(), c.d->length);
    b.SetSlotConst(MipsBranchStateDisp(), kind);
}

void MipsIrBranchCondTest(MipsIrContext& c, const cerfir::SlotTest& test, uint32_t target,
                          bool likely) {
    if (c.lean_branch) {
        throw EmitFault("CerfIR MIPS frontend: a stored slot-test branch inside a lean branch");
    }
    cerfir::Builder& b    = *c.b;
    const uint32_t   kind = likely ? MipsBranch::kCondLikely : MipsBranch::kCond;
    c.branch              = MipsIrBranch{kind, target, 0u, false, false, false};
    b.SetSlotTest(test, MipsBcondDisp());
    b.SetSlotConst(MipsBtargetDisp(), target);
    b.SetSlotConst(MipsBranchLenDisp(), c.d->length);
    b.SetSlotConst(MipsBranchStateDisp(), kind);
}

void MipsIrBranchDeferred(MipsIrContext& c, const cerfir::SlotTest& test, uint32_t target,
                          bool likely) {
    if (!c.lean_branch) {
        throw EmitFault("CerfIR MIPS frontend: a slot-test branch outside a lean branch");
    }
    const uint32_t kind = likely ? MipsBranch::kCondLikely : MipsBranch::kCond;
    c.branch            = MipsIrBranch{kind, target, 0u, false, false, true};
    c.branch.deferred   = true;
    c.branch.test       = test;
}

}
