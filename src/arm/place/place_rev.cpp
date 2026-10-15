#include <cstddef>

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

namespace {

void EmitSwapAdjacentGroups(uint8_t*& cursor, uint32_t mask, uint8_t width) {
    using namespace x86;
    EmitMovRegReg   (cursor, kEdx, kEax);
    EmitShrReg32Imm (cursor, kEax, width);
    EmitAndRegImm32 (cursor, kEax, mask);
    EmitAndRegImm32 (cursor, kEdx, mask);
    EmitShlReg32Imm (cursor, kEdx, width);
    EmitOrReg32Reg32(cursor, kEax, kEdx);
}

}

uint8_t* PlaceRev(uint8_t*      cursor,
                  DecodedInsn*  d,
                  BlockContext*) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rm));
    EmitBswapReg32      (cursor, kEax);
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
    return cursor;
}

uint8_t* PlaceRev16(uint8_t*      cursor,
                    DecodedInsn*  d,
                    BlockContext*) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rm));
    EmitBswapReg32      (cursor, kEax);
    EmitRorReg32Imm     (cursor, kEax, 16);
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
    return cursor;
}

uint8_t* PlaceRevsh(uint8_t*      cursor,
                    DecodedInsn*  d,
                    BlockContext*) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rm));
    EmitBswapReg32      (cursor, kEax);
    EmitSarReg32Imm     (cursor, kEax, 16);
    EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
    return cursor;
}

/* DDI 0406C.c A8.8.144 RBIT Operation (p. A8-561): "bits(32) result; for i = 0
   to 31 result<31-i> = R[m]<i>; R[d] = result". Exceptions: None. */
uint8_t* PlaceRbit(uint8_t*      cursor,
                   DecodedInsn*  d,
                   BlockContext*) {
    using namespace x86;
    EmitMovRegBaseDisp32  (cursor, kEax, kStateReg, ArmGprDisp(d->rm));
    EmitSwapAdjacentGroups(cursor, 0x55555555u, 1);
    EmitSwapAdjacentGroups(cursor, 0x33333333u, 2);
    EmitSwapAdjacentGroups(cursor, 0x0F0F0F0Fu, 4);
    EmitBswapReg32        (cursor, kEax);
    EmitMovBaseDisp32Reg  (cursor, kStateReg, ArmGprDisp(d->rd), kEax);
    return cursor;
}

}
