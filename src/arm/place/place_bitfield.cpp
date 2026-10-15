#include <cstddef>

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* BFI (DDI 0406C A8.8.20): Rd<lsb+width-1:lsb> = Rn<width-1:0>, other Rd
   bits unchanged. */
uint8_t* PlaceBfi(uint8_t*      cursor,
                  DecodedInsn*  d,
                  BlockContext*) {
    using namespace x86;
    const uint32_t lsb      = d->op1;
    const uint32_t width    = d->rs;
    const uint32_t src_mask = (width == 32u) ? 0xFFFFFFFFu : ((1u << width) - 1u);

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rn));
    EmitAndRegImm32     (cursor, kEax, src_mask);
    if (lsb != 0u) {
        EmitShlReg32Imm(cursor, kEax, static_cast<uint8_t>(lsb));
    }
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rd));
    EmitAndRegImm32     (cursor, kEcx, ~d->immediate);
    EmitOrReg32Reg32    (cursor, kEcx, kEax);
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEcx);
    return cursor;
}

/* BFC (DDI 0406C A8.8.19): Rd<lsb+width-1:lsb> = 0, other Rd bits
   unchanged. */
uint8_t* PlaceBfc(uint8_t*      cursor,
                  DecodedInsn*  d,
                  BlockContext*) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rd));
    EmitAndRegImm32     (cursor, kEax, ~d->immediate);
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
    return cursor;
}

/* UBFX (DDI 0406C A8.8.246): Rd = ZeroExtend(Rn<lsb+width-1:lsb>). */
uint8_t* PlaceUbfx(uint8_t*      cursor,
                   DecodedInsn*  d,
                   BlockContext*) {
    using namespace x86;
    const uint32_t lsb   = d->op1;
    const uint32_t width = d->rs;
    const uint32_t mask  = (width == 32u) ? 0xFFFFFFFFu : ((1u << width) - 1u);

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rn));
    if (lsb != 0u) {
        EmitShrReg32Imm(cursor, kEax, static_cast<uint8_t>(lsb));
    }
    EmitAndRegImm32     (cursor, kEax, mask);
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
    return cursor;
}

/* SBFX (DDI 0406C A8.8.164): Rd = SignExtend(Rn<lsb+width-1:lsb>). */
uint8_t* PlaceSbfx(uint8_t*      cursor,
                   DecodedInsn*  d,
                   BlockContext*) {
    using namespace x86;
    const uint32_t lsb    = d->op1;
    const uint32_t width  = d->rs;
    const uint32_t lshift = 32u - lsb - width;
    const uint32_t rshift = 32u - width;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rn));
    if (lshift != 0u) {
        EmitShlReg32Imm(cursor, kEax, static_cast<uint8_t>(lshift));
    }
    if (rshift != 0u) {
        EmitSarReg32Imm(cursor, kEax, static_cast<uint8_t>(rshift));
    }
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
    return cursor;
}

}
