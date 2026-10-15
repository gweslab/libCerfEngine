#include "../mips_place_fns.h"

#include "../mips_gpr_emit.h"

namespace cerfengine {

/* SRA rd, rt, sa : rd = sext32(rt[31:0] >> sa), arithmetic (sign-filling).
   QEMU translate.c gen_shift_imm OPC_SRA (sari). */
uint8_t* PlaceMipsSra(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    mips_emit::EmitShiftImm32Sext(cursor, d->rd, d->rt, d->sa, 7);
    return cursor;
}

}
