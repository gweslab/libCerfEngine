#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_emit_services.h"
#include "../../x86_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsRfe(uint8_t* cursor, MipsDecodedInsn*, MipsBlockContext* ctx) {
    using namespace x86;
    const MipsExceptionHooks& hooks = ctx->emit->Owner().GetSetup().mips_exception;
    EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hooks.context)));
    EmitMovRegReg(cursor, kEdx, kStateReg);
    EmitCall(cursor, reinterpret_cast<void*>(hooks.rfe));
    return cursor;
}

}
