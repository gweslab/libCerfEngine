#pragma once

#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/decoded_insn.h>

namespace cerfengine {

void LogUndefinedEncoding(const DecodedInsn* d, const BlockContext* ctx);

}
