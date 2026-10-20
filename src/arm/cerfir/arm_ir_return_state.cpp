#include <cerfengine/arm/cpu_state.h>

#include "../arm_emit_services.h"
#include "../arm_exception_frame.h"
#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

uint32_t FrameBits(const DecodedInsn& d, uint32_t low5) {
    return (static_cast<uint32_t>(d.p) << 7) | (static_cast<uint32_t>(d.u) << 6) |
           (static_cast<uint32_t>(d.w) << 5) | (low5 & 0x1Fu);
}

}

void ArmIrRfe(ArmIrContext& c) {
    const DecodedInsn&     d = *c.d;
    cerfir::Builder&       b = *c.b;
    const cerfir::CallDesc rfe{reinterpret_cast<const void*>(&ArmExceptionFrame::RfeHelper),
                               cerfir::CallConv::kFastcall, 3u};
    const cerfir::Ref base    = ArmIrReadGpr(c, d.rn);
    const cerfir::Ref failed  = b.Call(rfe, cerfir::Type::kI1, base, b.Const32(FrameBits(d, d.rn)),
                                       b.ConstPtr(c.block->emit->ExceptionFrame()));
    const uint32_t    aborted = b.NewLabel();
    b.Branch(failed, aborted, true);
    ArmIrFinishPcWrite(c);
    b.Label(aborted);
    ArmIrRaiseDataAbort(c);
}

void ArmIrSrs(ArmIrContext& c) {
    const DecodedInsn&     d = *c.d;
    cerfir::Builder&       b = *c.b;
    const cerfir::CallDesc srs{reinterpret_cast<const void*>(&ArmExceptionFrame::SrsHelper),
                               cerfir::CallConv::kFastcall, 3u};
    const cerfir::Ref failed = b.Call(srs, cerfir::Type::kI1, b.Const32(FrameBits(d, d.immediate)),
                                      b.ConstPtr(c.block->emit->ExceptionFrame()),
                                      b.Const32(d.guest_address));
    const uint32_t    stored = b.NewLabel();
    b.Branch(failed, stored, false);
    ArmIrRaiseDataAbort(c);
    b.Label(stored);
}

}
