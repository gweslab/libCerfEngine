#include "thumb32_fatal.h"

#include <cerfengine/engine.h>
#include <cerfengine/arm/decoded_insn.h>

namespace cerfengine {

CERFENGINE_REGISTER(Thumb32Fatal);

bool Thumb32Fatal::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void Thumb32Fatal::Unimplemented(const char* what, const DecodedInsn* insn,
                                 uint32_t op) {
    engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                 "Thumb32Decoder: %s not implemented, op=0x%08X at guest PC 0x%08X",
                 what, op, insn->guest_address);
}

}
