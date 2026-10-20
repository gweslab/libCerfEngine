#pragma once

#include <cstdint>

#include <cerfengine/arm/decoded_insn.h>

namespace cerfengine {

inline bool MarkArmUnimplemented(DecodedInsn* insn, uint32_t word) {
    insn->immediate = word;
    insn->kind      = ArmInsnKind::kArmUnimplemented;
    return true;
}

}
