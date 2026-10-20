#pragma once

#include <cstdint>

#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/engine_setup.h>

namespace cerfengine {

/* ARM DDI 0406C.c B9.3.8 MRS (p. B9-1991): "CPSR is read with execution state bits
   other than E masked out", AND '11111000 11111111 00000011 11011111'. */
inline constexpr uint32_t kArmV7MrsCpsrReadMask = 0xF8FF03DFu;

bool ArmMrsUnpredictable(const DecodedInsn& d);
bool ArmMsrRegisterUnpredictable(const DecodedInsn& d, const ArmFeatures& f);
bool ArmMsrImmediateUnpredictable(const DecodedInsn& d, const BlockContext* ctx);

}
