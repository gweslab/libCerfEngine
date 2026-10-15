#include <cstdint>

#include "../arm_emit_services.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>

namespace cerfengine {

uint8_t* EmitVfpRegisterTransfer(uint8_t*      cursor,
                                 DecodedInsn*  d,
                                 BlockContext* ctx) {
    /* ARM DDI 0406C.c Table A7-22 (p. A7-278): L=0, C=1, A=1xx, B=0x is
       VDUP (ARM core register). */
    if (ctx->emit->ProcessorConfig()->neon &&
        d->cp_num == 11 && d->l == 0 &&
        (d->cp_opc & 4u) && (d->cp & 2u) == 0u) {
        return EmitNeonVdup(cursor, d, ctx);
    }

    if (ctx->emit->ProcessorConfig()->neon && d->cp_num == 11) {
        const bool esize_8  = (d->cp_opc & 2u) != 0u;
        const bool esize_16 = ((d->cp_opc & 2u) == 0u) && ((d->cp & 1u) != 0u);
        if (esize_8 || esize_16) {
            if (d->l == 0u) return EmitNeonCoreToScalar(cursor, d, ctx);
            return EmitNeonScalarToCore(cursor, d, ctx);
        }
    }

    /* B9.3.21 VMRS (p. B9-2014) and B9.3.22 VMSR (p. B9-2016), encoding
       T1/A1: bits[27:20] = 1110 111L, coproc = 1010, opc2 = (0)(0)(0),
       CRm = (0)(0)(0)(0). */
    if (d->cp_num == 10 && d->cp_opc == 7 && d->crm == 0 && d->cp == 0) {
        return EmitVfpSystemRegTransfer(cursor, d, ctx);
    }

    if (d->cp_num == 11 && (d->cp_opc & 6u) == 0u && d->crm == 0 &&
        (d->cp & 3u) == 0u) {
        const uint32_t dn = (((d->cp >> 2) & 1u) << 4) | (d->crn & 0xFu);
        const uint32_t sx = (dn << 1) | (d->cp_opc & 1u);
        return EmitVfpSingleMoveIdx(cursor, d, ctx, sx);
    }

    if (d->cp_opc == 0 && d->crm == 0 && (d->cp & 3u) == 0u) {
        return EmitVfpSingleMove(cursor, d, ctx);
    }

    return EmitRaiseUndAndReturn(cursor, d, ctx);
}

}
