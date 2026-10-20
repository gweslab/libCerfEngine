#pragma once

#include <cstdint>

#include <cerfengine/arm/arm_system_host.h>
#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/decoded_insn.h>

namespace cerfengine {

ArmCoprocDecision QueryCoprocDecision(const DecodedInsn* d, BlockContext* ctx, ArmCoprocKind kind);

uint32_t CoprocRegisterTransferHaltInfo(const DecodedInsn* d);
uint32_t CoprocDataOperationHaltInfo(const DecodedInsn* d);
uint32_t CoprocDataTransferHaltInfo(const DecodedInsn* d);

}
