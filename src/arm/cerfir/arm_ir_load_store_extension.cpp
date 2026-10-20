#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/block_context.h>

#include "../arm_emit_services.h"
#include "../arm_transfer_rules.h"
#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::kNoRef;
using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

Ref OffsetAddress(ArmIrContext& c, Ref base) {
    const DecodedInsn& d = *c.d;
    cerfir::Builder&   b = *c.b;
    if (d.n != 0u) return b.Op2(Op::kAdd, base, b.Const32(static_cast<uint32_t>(d.offset)));
    const Ref offset = b.Op2(Op::kShl, ArmIrReadGpr(c, d.rm), b.Const32(d.rs));
    return b.Op2(d.u != 0u ? Op::kAdd : Op::kSub, base, offset);
}

/* A8.8.81 LDRH (literal) (p. A8-445): "base = Align(PC,4)"; the register forms
   read R[n] (A8.8.82, p. A8-447). */
Ref Base(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    if (d.rn != ArmGpr::kR15) return ArmIrReadGpr(c, d.rn);
    const uint32_t pc = ArmPcReadValue(&d, c.block);
    return c.b->Const32(d.n != 0u ? (pc & ~3u) : pc);
}

/* ARM DDI 0406C.c A8.8.229 SWP, SWPB (p. A8-723): "data = ZeroExtend(MemA[R[n],
   size],32); MemA[R[n], size] = R[t2]<8*size-1:0>"; SWP returns
   "ROR(data, 8*UInt(R[n]<1:0>))". */
void Swap(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    cerfir::Builder&   b = *c.b;
    if (ArmSwapUnpredictable(d)) {
        ArmIrUndefined(c);
        return;
    }
    const cerfir::AlignPolicy policy  = ArmSwapAlign(d, c.block);
    const bool                rotate  = policy == cerfir::AlignPolicy::kLegacyRotate;
    const Ref                 address = b.Snapshot(ArmIrReadGpr(c, d.rn));
    const Ref                 value   = ArmIrReadGpr(c, d.rm);
    uint32_t                  attr;
    cerfir::MemAttr&          m = b.NewMemAttr(&attr);
    m.width_bytes = d.n != 0u ? 1u : 4u;
    m.align       = policy;
    m.complete    = true;
    m.back_out    = true;
    m.slow_mask   = policy == cerfir::AlignPolicy::kFault ? 3u : 0u;
    ArmIrMemOrigin(c, m);
    if (rotate) b.Dup(address);
    const Ref word_address = rotate ? b.Op2(Op::kAnd, address, b.Const32(0xFFFFFFFCu)) : address;
    const Ref data         = b.Swap(Type::kI32, word_address, value, attr);
    if (!rotate) {
        ArmIrWriteGpr(c, d.rd, data);
        return;
    }
    const Ref amount = b.Op2(Op::kShl, b.Op2(Op::kAnd, address, b.Const32(3u)), b.Const32(3u));
    ArmIrWriteGpr(c, d.rd, b.Op2(Op::kRor, data, amount));
}

cerfir::MemAttr& DualWord(ArmIrContext& c, uint32_t* attr, bool first, bool back_out,
                          bool writeback_on_fault, bool wback) {
    const DecodedInsn& d = *c.d;
    cerfir::MemAttr&   m = c.b->NewMemAttr(attr);
    m.width_bytes        = 4u;
    m.complete           = true;
    m.back_out           = back_out;
    m.align              = cerfir::AlignPolicy::kFault;
    m.slow_mask          = first ? c.block->emit->Mmu()->DoublewordAlignMask() : 0u;
    m.writeback_on_fault = writeback_on_fault;
    m.writeback_slot     = wback ? ArmGprDisp(d.rn) : cerfir::kNoSlot;
    ArmIrMemOrigin(c, m);
    return m;
}

/* ARM DDI 0406C.c A8.8.72 LDRD (p. A8-427): "R[t] = MemA[address,4];
   R[t2] = MemA[address+4,4]; if wback then R[n] = offset_addr"; A8.8.210 STRD
   (p. A8-687): "MemA[address,4] = R[t]; MemA[address+4,4] = R[t2]". */
void Dual(ArmIrContext& c, Ref address, Ref writeback, bool wback, bool writeback_on_fault) {
    const DecodedInsn& d       = *c.d;
    cerfir::Builder&   b       = *c.b;
    const Ref          first   = b.Snapshot(address);
    b.Dup(first);
    const Ref          second  = b.Op2(Op::kAdd, first, b.Const32(4u));
    uint32_t           attr_lo = 0;
    uint32_t           attr_hi = 0;
    if (wback) {
        b.Dup(writeback);
        b.Dup(writeback);
    }
    if (d.op1 == 2u) {
        DualWord(c, &attr_lo, true, true, writeback_on_fault, wback);
        const Ref lo = b.Load(Type::kI32, first, writeback, attr_lo);
        DualWord(c, &attr_hi, false, true, writeback_on_fault, wback);
        const Ref hi = b.Load(Type::kI32, second, writeback, attr_hi);
        ArmIrWriteGpr(c, d.rd, lo);
        ArmIrWriteGpr(c, d.rd2, hi);
    } else {
        DualWord(c, &attr_lo, true, true, writeback_on_fault, wback);
        b.Store(first, ArmIrReadGpr(c, d.rd), writeback, attr_lo);
        DualWord(c, &attr_hi, false, false, writeback_on_fault, wback);
        b.Store(second, ArmIrReadGpr(c, d.rd2), writeback, attr_hi);
    }
    if (wback) ArmIrWriteGpr(c, d.rn, writeback);
}

}

void ArmIrLoadStoreExtension(ArmIrContext& c) {
    const DecodedInsn& d    = *c.d;
    cerfir::Builder&   b    = *c.b;
    ArmEmitServices*   emit = c.block->emit;
    const ArmFeatures& f    = *emit->ProcessorConfig();
    if (d.op1 == 0u) {
        Swap(c);
        return;
    }
    if (ArmExtensionTransferUndefined(d, f, c.block->thumb)) {
        ArmIrUndefined(c);
        return;
    }
    const bool wback       = d.p == 0u || d.w != 0u;
    const Ref  offset_addr = (d.p != 0u || wback) ? OffsetAddress(c, Base(c)) : kNoRef;
    const Ref  address     = d.p != 0u ? offset_addr : ArmIrReadGpr(c, d.rn);
    const Ref  writeback   = wback ? offset_addr : kNoRef;
    if (d.p != 0u && wback) b.Dup(offset_addr);
    if (ArmExtensionTransferIsDual(d)) {
        Dual(c, address, wback ? b.Snapshot(writeback) : kNoRef, wback,
             ArmBaseUpdatedOnAbort(wback, f));
        return;
    }
    const bool store    = d.l == 0u && (d.op1 == 1u || d.op1 == 3u);
    const bool halfword = d.op1 == 1u || (d.op1 == 3u && d.l != 0u);

    uint32_t         attr;
    cerfir::MemAttr& m = b.NewMemAttr(&attr);
    m.width_bytes        = halfword ? 2u : 1u;
    m.sign               = d.l != 0u && d.op1 != 1u;
    m.unpriv             = d.unpriv != 0u;
    m.writeback_on_fault = ArmBaseUpdatedOnAbort(wback, f);
    m.writeback_slot     = wback ? ArmGprDisp(d.rn) : cerfir::kNoSlot;
    ArmIrMemOrigin(c, m);
    m.align              = ArmExtensionTransferAlign(d, c.block);
    ArmTransferSlowChecks(m);
    m.stop_after_io = true;

    if (store) {
        b.Store(address, ArmIrReadGpr(c, d.rd), writeback, attr);
        return;
    }
    m.result_slot = ArmGprDisp(d.rd);
    b.Load(cerfir::Type::kI32, address, writeback, attr);
}

}
