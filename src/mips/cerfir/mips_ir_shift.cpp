#include "mips_ir_fns.h"

#include "../mips_emit_services.h"
#include "../mips_wide_arithmetic.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

void ShiftImm32(MipsIrContext& c, Op op) {
    const MipsDecodedInsn& d = *c.d;
    if (d.rd == 0u) return;
    if (d.sa == 0u) {
        c.b->SetSlotSext(MipsGprLoDisp(d.rd), MipsIrReadLo(c, d.rt));
        return;
    }
    if (d.rt != 0u) {
        c.b->SlotImmInto(op, MipsGprLoDisp(d.rt), d.sa, MipsGprLoDisp(d.rd), true);
        return;
    }
    c.b->ComputeIntoSext(op, MipsIrReadLo(c, d.rt), c.b->Const32(d.sa), MipsGprLoDisp(d.rd));
}

void ShiftVar32(MipsIrContext& c, Op op) {
    const MipsDecodedInsn& d = *c.d;
    if (d.rd == 0u) return;
    c.b->ComputeIntoSext(op, MipsIrReadLo(c, d.rt), MipsIrReadLo(c, d.rs), MipsGprLoDisp(d.rd));
}

void ShiftImm64(MipsIrContext& c, Op op) {
    const MipsDecodedInsn& d = *c.d;
    if (d.rd == 0u) return;
    if (d.sa == 0u) {
        MipsIrCopy64(c, d.rd, d.rt);
        return;
    }
    cerfir::Builder& b  = *c.b;
    const Ref        lo = MipsIrReadLo(c, d.rt);
    const Ref        hi = MipsIrReadHi(c, d.rt);
    const Ref amount = b.Const32(d.sa);
    const Ref back   = b.Const32(32u - d.sa);
    if (op == Op::kShl) {
        const Ref new_lo = b.Op2(Op::kShl, lo, amount);
        const Ref new_hi = b.Op2(Op::kOr, b.Op2(Op::kShl, hi, amount), b.Op2(Op::kLsr, lo, back));
        MipsIrWrite64(c, d.rd, new_lo, new_hi);
        return;
    }
    const Ref new_lo = b.Op2(Op::kOr, b.Op2(Op::kLsr, lo, amount), b.Op2(Op::kShl, hi, back));
    const Ref new_hi = b.Op2(op, hi, amount);
    MipsIrWrite64(c, d.rd, new_lo, new_hi);
}

void ShiftVar64(MipsIrContext& c, const void* helper) {
    const MipsDecodedInsn& d = *c.d;
    MipsIrFastcallConst(c, helper, 4u, d.rd, d.rt, d.rs,
                        cerfir::CallHostAddress(c.block->emit->WideArithmetic()));
}

}

void MipsIrSll(MipsIrContext& c) { ShiftImm32(c, Op::kShl); }

void MipsIrSrl(MipsIrContext& c) {
    if ((c.d->rs & 1u) != 0u) {
        MipsIrUndefined(c);
        return;
    }
    ShiftImm32(c, Op::kLsr);
}

void MipsIrSra(MipsIrContext& c) { ShiftImm32(c, Op::kAsr); }
void MipsIrSllv(MipsIrContext& c) { ShiftVar32(c, Op::kShl); }
void MipsIrSrlv(MipsIrContext& c) { ShiftVar32(c, Op::kLsr); }
void MipsIrSrav(MipsIrContext& c) { ShiftVar32(c, Op::kAsr); }

void MipsIrDsll(MipsIrContext& c) { ShiftImm64(c, Op::kShl); }
void MipsIrDsrl(MipsIrContext& c) { ShiftImm64(c, Op::kLsr); }
void MipsIrDsra(MipsIrContext& c) { ShiftImm64(c, Op::kAsr); }

void MipsIrDsll32(MipsIrContext& c) {
    const MipsDecodedInsn& d = *c.d;
    if (d.rd == 0u) return;
    const Ref hi = c.b->Op2(Op::kShl, MipsIrReadLo(c, d.rt), c.b->Const32(d.sa));
    MipsIrWrite64(c, d.rd, c.b->Const32(0u), hi);
}

void MipsIrDsrl32(MipsIrContext& c) {
    const MipsDecodedInsn& d = *c.d;
    if ((d.rs & 1u) != 0u) {
        MipsIrUndefined(c);
        return;
    }
    if (d.rd == 0u) return;
    const Ref lo = c.b->Op2(Op::kLsr, MipsIrReadHi(c, d.rt), c.b->Const32(d.sa));
    MipsIrWrite64(c, d.rd, lo, c.b->Const32(0u));
}

void MipsIrDsra32(MipsIrContext& c) {
    const MipsDecodedInsn& d = *c.d;
    if (d.rd == 0u) return;
    if (d.sa == 0u) {
        c.b->SetSlotSext(MipsGprLoDisp(d.rd), MipsIrReadHi(c, d.rt));
        return;
    }
    if (d.rt != 0u) {
        c.b->SlotImmInto(Op::kAsr, MipsGprHiDisp(d.rt), d.sa, MipsGprLoDisp(d.rd), true);
        return;
    }
    c.b->ComputeIntoSext(Op::kAsr, MipsIrReadHi(c, d.rt), c.b->Const32(d.sa), MipsGprLoDisp(d.rd));
}

void MipsIrDsllv(MipsIrContext& c) {
    ShiftVar64(c, reinterpret_cast<const void*>(&MipsWideArithmetic::DsllvHelper));
}

void MipsIrDsrlv(MipsIrContext& c) {
    ShiftVar64(c, reinterpret_cast<const void*>(&MipsWideArithmetic::DsrlvHelper));
}

void MipsIrDsrav(MipsIrContext& c) {
    ShiftVar64(c, reinterpret_cast<const void*>(&MipsWideArithmetic::DsravHelper));
}

}
