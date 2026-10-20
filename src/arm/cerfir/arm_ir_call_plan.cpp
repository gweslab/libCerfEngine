#include "arm_ir_fns.h"

namespace cerfengine {

void ArmIrCallPlan(ArmIrContext& c, const ArmCallPlan& plan) {
    if (plan.undefined) {
        ArmIrUndefined(c);
        return;
    }
    static_assert(kArmCallPlanMaxArgs <= cerfir::kMaxCallExtraArgs);
    cerfir::Builder& b = *c.b;
    cerfir::CallDesc call{plan.fn, cerfir::CallConv::kCdecl, 0u};
    call.num_extra = plan.arg_count;
    for (uint32_t i = 0; i < plan.arg_count; ++i) call.extra[i] = plan.args[i];
    if (!plan.exit_if_nonzero) {
        b.Call(call, cerfir::Type::kVoid, cerfir::kNoRef, cerfir::kNoRef, cerfir::kNoRef);
        return;
    }
    const cerfir::Ref result =
        b.Call(call, cerfir::Type::kI32, cerfir::kNoRef, cerfir::kNoRef, cerfir::kNoRef);
    const uint32_t proceed = b.NewLabel();
    b.Branch(b.Op2(cerfir::Op::kCmpEq, result, b.Const32(0u)), proceed, true);
    b.ExitDispatcher();
    b.Label(proceed);
}

}
