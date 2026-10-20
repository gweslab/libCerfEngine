#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "../cerfir/cerfir_block.h"

namespace cerfengine {

struct BlockContext;
struct DecodedInsn;

constexpr uint32_t kArmCallPlanMaxArgs = 9;

struct ArmCallPlan {
    const void*     fn                        = nullptr;
    cerfir::CallArg args[kArmCallPlanMaxArgs] = {};
    uint8_t         arg_count                 = 0;
    bool            undefined                 = false;
    bool            exit_if_nonzero           = false;
};

using ArmCallPlanFn = ArmCallPlan (*)(const DecodedInsn& d, const BlockContext& ctx);

inline ArmCallPlan ArmUndefinedCallPlan() {
    ArmCallPlan plan;
    plan.undefined = true;
    return plan;
}

[[noreturn]] void ArmCallPlanArgumentFault(const void* fn, size_t count);

inline ArmCallPlan ArmPlannedCall(const void* fn, std::initializer_list<cerfir::CallArg> args,
                                  bool exit_if_nonzero = false) {
    if (args.size() > kArmCallPlanMaxArgs) ArmCallPlanArgumentFault(fn, args.size());
    ArmCallPlan plan;
    plan.fn              = fn;
    plan.exit_if_nonzero = exit_if_nonzero;
    for (const cerfir::CallArg& value : args) plan.args[plan.arg_count++] = value;
    return plan;
}

inline cerfir::CallArg ArmCallPlanPointer(const void* p) { return cerfir::CallHostAddress(p); }

}
