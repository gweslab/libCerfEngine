#include "arm_call_plan.h"

#include <cstdio>

#include "../emit_fault.h"

namespace cerfengine {

void ArmCallPlanArgumentFault(const void* fn, size_t count) {
    char text[96];
    std::snprintf(text, sizeof(text), "ARM call plan: %zu arguments for helper %p exceed %u", count,
                  fn, kArmCallPlanMaxArgs);
    throw EmitFault(text);
}

}
