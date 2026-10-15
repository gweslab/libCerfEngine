#include "../mips_place_fns.h"

#include <cstddef>
#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_gpr_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* QEMU MIPS gen_muldiv OPC_DIVU (translate.c). */
uint8_t* PlaceMipsDivu(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext*) {
    using namespace x86;
    const int32_t kLoLo = static_cast<int32_t>(offsetof(MipsCpuState, lo));
    const int32_t kLoHi = kLoLo + 4;
    const int32_t kHiLo = static_cast<int32_t>(offsetof(MipsCpuState, hi));
    const int32_t kHiHi = kHiLo + 4;

    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, mips_emit::GprLoOff(d->rt));
    EmitTestRegReg(cursor, kEcx, kEcx);
    uint8_t* j_nz = EmitJnzLabel(cursor);
    EmitMovRegImm32(cursor, kEcx, 1);
    FixupLabel(j_nz, cursor);
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rs));
    EmitXorRegReg(cursor, kEdx, kEdx);
    Emit8(cursor, 0xF7);                           /* DIV ecx - F7 /6, EAX=quot,
                                                      EDX=rem (SDM Vol. 2A 3-321 DIV) */
    EmitModRmReg(cursor, 3, kEcx, 6);
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
