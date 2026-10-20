#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/cpu_state.h>

#include "../arm_emit_services.h"
#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::kNoRef;
using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

Ref Address(ArmIrContext& c) {
    const DecodedInsn& d    = *c.d;
    const Ref          base = ArmIrReadGpr(c, d.rn);
    if (d.offset == 0) return c.b->Snapshot(base);
    return c.b->Op2(Op::kAdd, base, c.b->Const32(static_cast<uint32_t>(d.offset)));
}

uint32_t AlignMask(uint32_t bytes) {
    return bytes < 2u ? 0u : bytes - 1u;
}

void Access(ArmIrContext& c, uint32_t* attr, uint32_t width, uint32_t slow_mask) {
    cerfir::MemAttr& m = c.b->NewMemAttr(attr);
    m.width_bytes      = static_cast<uint8_t>(width);
    m.complete         = true;
    m.back_out         = true;
    m.align            = cerfir::AlignPolicy::kFault;
    m.slow_mask        = slow_mask;
    ArmIrMemOrigin(c, m);
}

/* DDI 0406C.c Table A3-3 (p. A3-116): StoreExcl(!t) and an Open Access
   StoreExcl(x) both leave the local monitor in Open Access. */
void Status(ArmIrContext& c, uint32_t status) {
    c.b->SetSlot(static_cast<uint32_t>(ArmGprDisp(c.d->rd)), c.b->Const32(status));
    c.b->SetSlot(static_cast<uint32_t>(ArmMonitorArmedDisp()), c.b->Const32(0u));
}

}

/* A8.8.75 LDREX (p. A8-433): "SetExclusiveMonitors(address,4); R[t] =
   MemA[address,4];". */
void ArmIrLdrex(ArmIrContext& c) {
    const DecodedInsn& d       = *c.d;
    cerfir::Builder&   b       = *c.b;
    const uint32_t     bytes   = d.op1;
    const Ref          address = Address(c);
    b.Share(address, bytes == 8u ? 3u : 2u);
    uint32_t           attr_lo = 0;
    Access(c, &attr_lo, bytes == 8u ? 4u : bytes, AlignMask(bytes));
    const Ref lo = b.Load(Type::kI32, address, kNoRef, attr_lo);
    if (bytes == 8u) {
        uint32_t attr_hi = 0;
        Access(c, &attr_hi, 4u, 0u);
        const Ref hi = b.Load(Type::kI32, b.Op2(Op::kAdd, address, b.Const32(4u)), kNoRef, attr_hi);
        ArmIrWriteGpr(c, d.rd, lo);
        ArmIrWriteGpr(c, d.rd2, hi);
    } else {
        ArmIrWriteGpr(c, d.rd, lo);
    }
    b.SetSlot(static_cast<uint32_t>(ArmMonitorAddrDisp()), address);
    b.SetSlot(static_cast<uint32_t>(ArmMonitorArmedDisp()), b.Const32(1u));
}

/* A8.8.212 STREX (p. A8-691): "if ExclusiveMonitorsPass(address,4) then
   MemA[address,4] = R[t]; R[d] = ZeroExtend('0', 32); else R[d] = ZeroExtend('1',
   32);". */
void ArmIrStrex(ArmIrContext& c) {
    const DecodedInsn& d     = *c.d;
    cerfir::Builder&   b     = *c.b;
    const uint32_t     bytes = d.op1;
    const uint32_t     mask  = AlignMask(bytes);
    const uint32_t     fail  = b.NewLabel();
    const uint32_t     done  = b.NewLabel();
    const uint32_t     fault = mask != 0u ? b.NewLabel() : kArmIrNoLabel;

    const Ref address = Address(c);
    b.Share(address, 2u + (fault != kArmIrNoLabel ? 1u : 0u) + (bytes == 8u ? 1u : 0u));
    if (fault != kArmIrNoLabel) {
        b.Branch(b.Compare(Op::kCmpNe, b.Op2(Op::kAnd, address, b.Const32(mask)), b.Const32(0u)),
                 fault, true);
    }
    const Ref armed = b.GetSlot(Type::kI32, static_cast<uint32_t>(ArmMonitorArmedDisp()));
    b.Branch(b.Compare(Op::kCmpEq, armed, b.Const32(0u)), fail, true);
    const Ref monitor = b.GetSlot(Type::kI32, static_cast<uint32_t>(ArmMonitorAddrDisp()));
    b.Branch(b.Compare(Op::kCmpNe, monitor, address), fail, true);
    uint32_t attr_lo = 0;
    Access(c, &attr_lo, bytes == 8u ? 4u : bytes, 0u);
    b.Store(address, ArmIrReadGpr(c, d.rm), kNoRef, attr_lo);
    if (bytes == 8u) {
        uint32_t attr_hi = 0;
        Access(c, &attr_hi, 4u, 0u);
        b.Store(b.Op2(Op::kAdd, address, b.Const32(4u)), ArmIrReadGpr(c, d.rd2), kNoRef, attr_hi);
    }
    Status(c, 0u);
    b.Branch(b.Const1(true), done, true);

    b.Label(fail);
    Status(c, 1u);
    if (fault != kArmIrNoLabel) {
        b.Branch(b.Const1(true), done, true);
        b.Label(fault);
        const cerfir::CallDesc align_fault{
            reinterpret_cast<const void*>(&ArmMmu::AlignmentFaultWriteHelper),
            cerfir::CallConv::kFastcall, 2u};
        b.Call(align_fault, Type::kVoid, Address(c), b.ConstPtr(c.block->emit->Mmu()), kNoRef);
        ArmIrRaiseDataAbort(c);
    }
    b.Label(done);
}

/* ARM DDI 0406C.c A8.8.32 CLREX Operation (p. A8-361):
   "ClearExclusiveLocal(ProcessorID());". */
void ArmIrClrex(ArmIrContext& c) {
    c.b->SetSlot(static_cast<uint32_t>(ArmMonitorArmedDisp()), c.b->Const32(0u));
}

}
