#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "mips_decoded_insn.h"
#include "mips_gpr_emit.h"
#include "../x86_emit_alu.h"

namespace cerfengine {

namespace mips_emit {

/* TMPR39xx-um p.155-156: t <- (HI || LO) + GPR[rs] * GPR[rt]; LO <- t31..0; HI <- t63..32;
   GPR[rd] <- t31..0. */
inline void EmitMultiplyAdd(uint8_t*& cursor, const MipsDecodedInsn* d, uint8_t f7_ext) {
    using namespace x86;
    const int32_t kLoLo = static_cast<int32_t>(offsetof(MipsCpuState, lo));
    const int32_t kLoHi = kLoLo + 4;
    const int32_t kHiLo = static_cast<int32_t>(offsetof(MipsCpuState, hi));
    const int32_t kHiHi = kHiLo + 4;

    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, GprLoOff(d->rt));
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, GprLoOff(d->rs));
    Emit8(cursor, 0xF7);
    EmitModRmReg(cursor, 3, kEcx, f7_ext);
    EmitAddRegBaseDisp32(cursor, kEax, kStateReg, kLoLo);
    EmitAdcRegBaseDisp32(cursor, kEdx, kStateReg, kHiLo);
    EmitMovBaseDisp32Reg(cursor, kStateReg, kLoLo, kEax);
    EmitMovBaseDisp32Reg(cursor, kStateReg, kHiLo, kEdx);
    EmitCdq(cursor);
    EmitMovBaseDisp32Reg(cursor, kStateReg, kLoHi, kEdx);
    if (d->rd != 0u) {
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprLoOff(d->rd), kEax);
        EmitMovBaseDisp32Reg(cursor, kStateReg, GprHiOff(d->rd), kEdx);
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, kHiLo);
    EmitCdq(cursor);
    EmitMovBaseDisp32Reg(cursor, kStateReg, kHiHi, kEdx);
}

}

}
