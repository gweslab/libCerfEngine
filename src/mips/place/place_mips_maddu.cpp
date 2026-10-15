#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_multiply_add_emit.h"

namespace cerfengine {

/* MADDU rd, rs, rt (TMPR39xx-um p.156): t <- (HI || LO) + (0 || GPR[rs])*(0 || GPR[rt]);
   LO <- t31..0; HI <- t63..32; GPR[rd] <- t31..0. */
uint8_t* PlaceMipsMaddu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitMultiplyAdd(cursor, d, 4);
    return cursor;
}

}
