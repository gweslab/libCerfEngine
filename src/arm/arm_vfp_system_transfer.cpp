#include "arm_vfp_system_transfer.h"

#include <cstddef>

#include <cerfengine/arm/cpu_state.h>

#include "arm_vfp.h"

namespace cerfengine {

namespace {

/* QEMU ARM ARM_VFP_* constants (cpu.h). */
constexpr uint32_t kFpsid  = 0;
constexpr uint32_t kFpscr  = 1;
constexpr uint32_t kMvfr1  = 6;
constexpr uint32_t kMvfr0  = 7;
constexpr uint32_t kFpexc  = 8;

constexpr int32_t kFpscrDisp = static_cast<int32_t>(offsetof(ArmCpuState, fpscr));
constexpr int32_t kFpexcDisp = static_cast<int32_t>(offsetof(ArmCpuState, fpexc));

ArmVfpSystemTransfer Action(ArmVfpSystemTransfer t, ArmVfpSystemAction action,
                            uint32_t constant = 0u, int32_t state_disp = 0) {
    t.action     = action;
    t.constant   = constant;
    t.state_disp = state_disp;
    return t;
}

}

ArmVfpSystemTransfer ArmVfpSystemTransferOf(const DecodedInsn& d, const ArmFeatures& features) {
    ArmVfpSystemTransfer t;
    const uint32_t fpsp     = (features.mvfr0 >> 4) & 0xFu;
    const bool     any_mode = d.crn == kFpscr ||
                              (fpsp < 2u && (d.crn == kFpsid || d.crn == kMvfr0 || d.crn == kMvfr1));
    t.undefined_in_user = !any_mode;
    const bool to_pc = d.rd == 15u;

    if (d.l) {
        switch (d.crn) {
        case kFpsid:
            if (to_pc) return Action(t, ArmVfpSystemAction::kUndefined);
            return Action(t, ArmVfpSystemAction::kReadConstant, features.fpsid);
        case kFpscr:
            return Action(t, to_pc ? ArmVfpSystemAction::kReadStateToFlags
                                   : ArmVfpSystemAction::kReadState,
                          0u, kFpscrDisp);
        case kMvfr1:
            if (to_pc) return Action(t, ArmVfpSystemAction::kUndefined);
            return Action(t, ArmVfpSystemAction::kReadConstant, features.mvfr1);
        case kMvfr0:
            if (to_pc) return Action(t, ArmVfpSystemAction::kUndefined);
            return Action(t, ArmVfpSystemAction::kReadConstant, features.mvfr0);
        case kFpexc:
            if (to_pc) return Action(t, ArmVfpSystemAction::kUndefined);
            return Action(t, ArmVfpSystemAction::kReadState, 0u, kFpexcDisp);
        default:
            return Action(t, ArmVfpSystemAction::kUndefined);
        }
    }

    switch (d.crn) {
    case kFpsid:
    case kMvfr1:
    case kMvfr0:
        /* QEMU trans_VMSR_VMRS (translate-vfp.c). */
        return Action(t, ArmVfpSystemAction::kIgnore);
    case kFpscr:
        t = Action(t, ArmVfpSystemAction::kWriteState, 0u, kFpscrDisp);
        if (((features.mvfr0 >> 12) & 0xFu) == 0u) t.write_mask = ~ArmVfp::kFpscrTrapEnableMask;
        return t;
    case kFpexc:
        return Action(t, ArmVfpSystemAction::kWriteState, 0u, kFpexcDisp);
    default:
        return Action(t, ArmVfpSystemAction::kUndefined);
    }
}

}
