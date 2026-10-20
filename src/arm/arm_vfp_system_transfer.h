#pragma once

#include <cstdint>

#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/engine_setup.h>

namespace cerfengine {

enum class ArmVfpSystemAction : uint8_t {
    kUndefined,
    kReadConstant,
    kReadState,
    kReadStateToFlags,
    kWriteState,
    kIgnore,
};

struct ArmVfpSystemTransfer {
    bool               undefined_in_user = false;
    ArmVfpSystemAction action            = ArmVfpSystemAction::kUndefined;
    uint32_t           constant          = 0;
    int32_t            state_disp        = 0;
    uint32_t           write_mask        = 0xFFFFFFFFu;
};

ArmVfpSystemTransfer ArmVfpSystemTransferOf(const DecodedInsn& d, const ArmFeatures& features);

}
