#include "mips_ir_fns.h"

#include <cerfengine/mips/mips_exception_delivery.h>

#include "../mips_emit_services.h"

namespace cerfengine {

namespace {

using cerfir::FlagSet;
using cerfir::kNoRef;
using cerfir::kWantC;
using cerfir::kWantV;
using cerfir::Op;
using cerfir::Ref;

uint64_t SignedImm64(const MipsDecodedInsn& d) {
    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(d.imm)));
}

void RaiseOverflow(MipsIrContext& c) {
    const bool stored = c.pc_stored;
    MipsIrCdeclConst(c, reinterpret_cast<const void*>(&MipsExceptionDelivery::ArithOverflowHelper),
                     2u, cerfir::CallHostAddress(c.block->emit->Exceptions()), c.d->guest_address,
                     0u);
    c.pc_stored = stored;
}

Ref Rhs32(MipsIrContext& c, bool imm) {
    return imm ? c.b->Const32(MipsSignedImm(*c.d)) : MipsIrReadLo(c, c.d->rt);
}

void Trapping32(MipsIrContext& c, Op op, uint32_t dest, bool imm) {
    const MipsDecodedInsn& d = *c.d;
    cerfir::Builder&       b = *c.b;
    if (imm && MipsSignedImm(d) == 0u) {
        MipsIrWriteSext(c, dest, MipsIrReadLo(c, d.rs));
        return;
    }
    FlagSet f;
    b.ComputeOnlyFlags(op, MipsIrReadLo(c, d.rs), Rhs32(c, imm), kNoRef, kWantV, &f);
    const uint32_t ok = b.NewLabel();
    b.Branch(f.v, ok, false);
    RaiseOverflow(c);
    b.Label(ok);
    if (dest == 0u) return;
    b.ComputeIntoSext(op, MipsIrReadLo(c, d.rs), Rhs32(c, imm), MipsGprLoDisp(dest));
}

Ref Rhs64(MipsIrContext& c, bool imm) {
    return imm ? MipsIrConst64(c, SignedImm64(*c.d)) : MipsIrRead64(c, c.d->rt);
}

void Wide(MipsIrContext& c, Op op, uint32_t dest, Ref rhs) {
    if (dest == 0u) return;
    cerfir::Builder& b = *c.b;
    const Ref        r = b.Op2(op, MipsIrRead64(c, c.d->rs), rhs);
    MipsIrWrite64(c, dest, b.Lo32(r), b.Hi32(r));
}

void Trapping64(MipsIrContext& c, bool add, uint32_t dest, bool imm) {
    const MipsDecodedInsn& d = *c.d;
    cerfir::Builder&       b = *c.b;
    const uint64_t         k = SignedImm64(d);
    const Ref rhs_lo = imm ? b.Const32(static_cast<uint32_t>(k)) : MipsIrReadLo(c, d.rt);
    const Ref rhs_hi = imm ? b.Const32(static_cast<uint32_t>(k >> 32)) : MipsIrReadHi(c, d.rt);
    FlagSet   low;
    b.ComputeOnlyFlags(add ? Op::kAdd : Op::kSub, MipsIrReadLo(c, d.rs), rhs_lo, kNoRef, kWantC,
                       &low);
    FlagSet high;
    b.ComputeOnlyFlags(add ? Op::kAdc : Op::kSbc, MipsIrReadHi(c, d.rs), rhs_hi, low.c, kWantV,
                       &high);
    const uint32_t ok = b.NewLabel();
    b.Branch(high.v, ok, false);
    RaiseOverflow(c);
    b.Label(ok);
    Wide(c, add ? Op::kAdd : Op::kSub, dest, Rhs64(c, imm));
}

}

void MipsIrAdd(MipsIrContext& c) { Trapping32(c, Op::kAdd, c.d->rd, false); }
void MipsIrSub(MipsIrContext& c) { Trapping32(c, Op::kSub, c.d->rd, false); }
void MipsIrAddi(MipsIrContext& c) { Trapping32(c, Op::kAdd, c.d->rt, true); }

void MipsIrDadd(MipsIrContext& c) { Trapping64(c, true, c.d->rd, false); }
void MipsIrDsub(MipsIrContext& c) { Trapping64(c, false, c.d->rd, false); }
void MipsIrDaddi(MipsIrContext& c) { Trapping64(c, true, c.d->rt, true); }

void MipsIrDaddu(MipsIrContext& c) { Wide(c, Op::kAdd, c.d->rd, MipsIrRead64(c, c.d->rt)); }
void MipsIrDsubu(MipsIrContext& c) { Wide(c, Op::kSub, c.d->rd, MipsIrRead64(c, c.d->rt)); }

void MipsIrDaddiu(MipsIrContext& c) {
    const MipsDecodedInsn& d = *c.d;
    if (d.rt == 0u) return;
    if ((d.imm & 0xFFFFu) == 0u) {
        MipsIrCopy64(c, d.rt, d.rs);
        return;
    }
    Wide(c, Op::kAdd, d.rt, MipsIrConst64(c, SignedImm64(d)));
}

}
