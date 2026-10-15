#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceMipsAddiu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    if (d->rt == 0) {
        return cursor;
    }
    const uint32_t sext =
        static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(d->imm)));
    mips_emit::EmitLoadGprLo(cursor, kEax, d->rs);
    EmitAddRegImm32(cursor, kEax, sext);
    mips_emit::EmitStoreGprSextEax(cursor, d->rt);
    return cursor;
}

}
