#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_multiply_add_emit.h"

namespace cerfengine {

/* MADD rd, rs, rt (TMPR39xx-um p.155): t <- (HI || LO) + GPR[rs]*GPR[rt], both operands two's
   complement; LO <- t31..0; HI <- t63..32; GPR[rd] <- t31..0. */
uint8_t* PlaceMipsMadd(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitMultiplyAdd(cursor, d, 5);
    return cursor;
}

}
