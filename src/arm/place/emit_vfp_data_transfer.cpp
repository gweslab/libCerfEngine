#include <cstdint>

#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>

namespace cerfengine {

uint8_t* EmitVfpDataTransfer(uint8_t*      cursor,
                             DecodedInsn*  d,
                             BlockContext* ctx) {
    /* DDI 0406C.c A8.8.332 VLDM (p. A8-922) and A8.8.412 VSTM (p. A8-1080)
       carry "if P == U && W == '1' then UNDEFINED" in every encoding. */
    if (d->p == d->u && d->w == 1) {
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
    if (d->p == 1 && d->w == 0) {
        return EmitVfpSingleTransfer(cursor, d, ctx);
    }
    return EmitVfpBlockTransfer(cursor, d, ctx);
}

}
