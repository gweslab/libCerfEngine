#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_emit_services.h"
#include <cerfengine/mips/mips_exception_delivery.h>
#include "../../x86_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsBreak(uint8_t* cursor, MipsDecodedInsn*, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Exceptions())));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsExceptionDelivery::BreakHelper));
    return cursor;
}

}
