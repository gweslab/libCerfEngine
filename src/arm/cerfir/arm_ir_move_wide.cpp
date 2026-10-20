#include "arm_ir_fns.h"

namespace cerfengine {

void ArmIrMovw(ArmIrContext& c) {
    ArmIrWriteGprConst(c, c.d->rd, c.d->immediate);
}

void ArmIrMovt(ArmIrContext& c) {
    cerfir::Builder& b   = *c.b;
    const cerfir::Ref low = b.Op2(cerfir::Op::kAnd, ArmIrReadGpr(c, c.d->rd), b.Const32(0xFFFFu));
    ArmIrWriteGpr(c, c.d->rd, b.Op2(cerfir::Op::kOr, low, b.Const32(c.d->immediate << 16)));
}

}
