#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_emit_services.h"
#include "../mips_wide_arithmetic.h"
#include "../../x86_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsDsrav(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    EmitMovRegImm32(cursor, kEcx, d->rd);
    EmitMovRegImm32(cursor, kEdx, d->rt);
    EmitPush32(cursor, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->WideArithmetic())));
    EmitPush32(cursor, d->rs);
    EmitCall(cursor, reinterpret_cast<void*>(&MipsWideArithmetic::DsravHelper));
    return cursor;
}

}
