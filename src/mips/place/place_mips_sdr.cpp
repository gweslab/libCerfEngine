#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_gpr_emit.h"
#include "../mips_emit_services.h"
#include "../mips_memory_access.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceMipsSdr(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    const uint32_t sext = static_cast<uint32_t>(static_cast<int32_t>(
                              static_cast<int16_t>(d->imm)));
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitAddRegImm32(cursor, kEcx, sext);
    EmitMovRegImm32(cursor, kEdx, d->rt);
    EmitPush32(cursor, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Memory())));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsMemoryAccess::SdrHelper));
    return cursor;
}

}
