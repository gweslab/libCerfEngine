#pragma once

#include <cstdint>

#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/engine_setup.h>

namespace cerfengine {

enum class ArmVfpRegisterForm : uint8_t {
    kUndefined,
    kNeonVdupCore,
    kNeonCoreToScalar,
    kNeonScalarToCore,
    kSystemRegister,
    kSingleMove,
};

struct ArmVfpRegisterTransfer {
    ArmVfpRegisterForm form   = ArmVfpRegisterForm::kUndefined;
    uint32_t           single = 0;
};

ArmVfpRegisterTransfer ArmVfpRegisterTransferOf(const DecodedInsn& d, const ArmFeatures& features);

}
