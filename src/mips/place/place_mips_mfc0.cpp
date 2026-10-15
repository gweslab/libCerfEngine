#include "../mips_place_fns.h"

#include "../mips_block_context.h"
#include "../mips_cp0_emitter.h"
#include "../mips_decoded_insn.h"
#include "../mips_emit_services.h"

namespace cerfengine {

uint8_t* PlaceMipsMfc0(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    return ctx->emit->Cp0Emitter()->EmitMfc0(cursor, d, ctx);
}

}
