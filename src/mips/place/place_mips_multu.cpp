#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* QEMU MIPS gen_muldiv OPC_MULTU (translate.c). */
uint8_t* PlaceMipsMultu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    const int32_t kLoLo = static_cast<int32_t>(offsetof(MipsCpuState, lo));
    const int32_t kLoHi = kLoLo + 4;
    const int32_t kHiLo = static_cast<int32_t>(offsetof(MipsCpuState, hi));
    const int32_t kHiHi = kHiLo + 4;

    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rt));
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    Emit8(cursor, 0xF7);                            /* MUL ecx - F7 /4, EDX:EAX = EAX*ECX
                                                       (SDM Vol. 2B 4-150 MUL) */
    EmitModRmReg(cursor, 3, kEcx, 4);
    EmitMovBaseDisp32Reg(cursor, kStateReg, kLoLo, kEax);
    EmitMovBaseDisp32Reg(cursor, kStateReg, kHiLo, kEdx);
    EmitCdq(cursor);
    EmitMovBaseDisp32Reg(cursor, kStateReg, kLoHi, kEdx);
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, kHiLo);
    EmitCdq(cursor);
    EmitMovBaseDisp32Reg(cursor, kStateReg, kHiHi, kEdx);
    return cursor;
}

}
