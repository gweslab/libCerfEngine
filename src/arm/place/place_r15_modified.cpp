#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"

namespace cerfengine {

uint8_t* PlaceR15ModifiedHelper(uint8_t* cursor, DecodedInsn* d,
                                BlockContext* ctx) {
    if (d->itstate_after_valid != 0u && d->is_exception_return == 0u) {
        cursor = EmitItStateStore(cursor, d->itstate_after);
    }
    /* QEMU ARM translate.c gen_rfe and the trans_LDM exc_return
       tail: "Must exit loop to check un-masked IRQs" - DISAS_EXIT, never
       gen_goto_ptr. */
    if (d->is_exception_return == 0u) {
        cursor = EmitJumpCacheProbe(cursor, ctx);
    }
    x86::EmitRet(cursor);
    return cursor;
}

}
