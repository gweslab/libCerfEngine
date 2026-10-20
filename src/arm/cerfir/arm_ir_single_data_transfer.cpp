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

/* A8.4.1 (p. A8-291): LSR and ASR with immediate 0 shift by 32, ROR with
   immediate 0 is RRX. */
Ref ShiftedOffset(ArmIrContext& c) {
    const DecodedInsn& d  = *c.d;
    cerfir::Builder&   b  = *c.b;
    const Ref          rm = ArmIrReadGpr(c, d.rm);
    switch (d.op1) {
    case 0u:
        return b.Op2(Op::kShl, rm, b.Const32(d.rs));
    case 1u:
        return d.rs != 0u ? b.Op2(Op::kLsr, rm, b.Const32(d.rs)) : b.Const32(0u);
    case 2u:
        return b.Op2(Op::kAsr, rm, b.Const32(d.rs != 0u ? d.rs : 31u));
    default:
        return d.rs != 0u ? b.Op2(Op::kRor, rm, b.Const32(d.rs))
                          : b.Op2(Op::kRrx, rm, ArmIrReadFlag(c, ArmIrFlag::kC));
    }
}

/* A8.8.64 (p. A8-411): the literal form addresses from Align(PC, 4). */
Ref OffsetAddress(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    cerfir::Builder&   b = *c.b;
    if (d.n != 0u) {
        const uint32_t offset = static_cast<uint32_t>(d.offset);
        if (d.rn == ArmGpr::kR15) {
            return b.Const32((ArmPcReadValue(&d, c.block) & ~3u) + offset);
        }
        return b.Op2(Op::kAdd, ArmIrReadGpr(c, d.rn), b.Const32(offset));
    }
    const Ref offset = ShiftedOffset(c);
    return b.Op2(d.u != 0u ? Op::kAdd : Op::kSub, ArmIrReadGpr(c, d.rn), offset);
}

}

void ArmIrSingleDataTransfer(ArmIrContext& c) {
    const DecodedInsn& d    = *c.d;
    cerfir::Builder&   b    = *c.b;
    ArmEmitServices*   emit = c.block->emit;
    const ArmFeatures& f    = *emit->ProcessorConfig();
    if (ArmSingleTransferUnpredictable(d, f)) {
        ArmIrUndefined(c);
        return;
    }
    const bool wback = d.p == 0u || d.w != 0u;
    const bool based = d.n != 0u && d.rn != ArmGpr::kR15;
    Ref        address   = kNoRef;
    Ref        writeback = kNoRef;
    if (!based) {
        const Ref offset_addr = OffsetAddress(c);
        address               = d.p != 0u ? offset_addr : ArmIrReadGpr(c, d.rn);
        writeback             = wback ? offset_addr : kNoRef;
        if (d.p != 0u && wback) b.Dup(offset_addr);
    }

    cerfir::MemAttr m{
        .width_bytes        = static_cast<uint8_t>(d.s != 0u ? 1u : 4u),
        .unpriv             = d.unpriv != 0u,
        .align              = ArmSingleTransferAlign(d, c.block),
        .writeback_on_fault = ArmBaseUpdatedOnAbort(wback, f),
        .writeback_slot     = wback ? ArmGprDisp(d.rn) : cerfir::kNoSlot,
        .base_slot          = based ? ArmGprDisp(d.rn) : cerfir::kNoSlot,
        .offset             = based ? d.offset : 0,
        .pre_index          = !based || d.p != 0u,
    };
    ArmTransferSlowChecks(m);
    ArmIrMemOrigin(c, m);

    if (d.l != 0u) {
        const bool to_pc = d.rd == ArmGpr::kR15;
        m.result_slot    = to_pc ? cerfir::kNoSlot : ArmGprDisp(d.rd);
        m.stop_after_io  = !to_pc;
        if (based && !to_pc && b.LeanLoad(m)) return;
        uint32_t attr = 0;
        b.NewMemAttr(&attr) = m;
        const Ref value = based ? b.LoadBased(cerfir::Type::kI32, attr, b.GetBlock().MemAttrAt(attr))
                                : b.Load(cerfir::Type::kI32, address, writeback, attr);
        if (to_pc) ArmIrLoadWritePc(c, value);
        return;
    }
    const Ref value = d.rd == ArmGpr::kR15 ? b.Const32(d.guest_address + f.pc_store_offset)
                                           : ArmIrReadGpr(c, d.rd);
    m.stop_after_io = true;
    if (based && b.LeanStore(m, value)) return;
    uint32_t attr = 0;
    b.NewMemAttr(&attr) = m;
    if (based) {
        b.StoreBased(value, attr, b.GetBlock().MemAttrAt(attr));
    } else {
        b.Store(address, value, writeback, attr);
    }
}

}
