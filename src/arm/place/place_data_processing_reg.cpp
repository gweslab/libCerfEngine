#include <cstddef>

#include <cerfengine/engine.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceDataProcessingReg(uint8_t*      cursor,
                                DecodedInsn*  d,
                                BlockContext* ctx) {
    using namespace x86;

    const uint32_t opcode  = d->op1;
    const uint32_t shift_t = d->n;
    const uint32_t shift_n = d->rs;
    const bool     s       = d->s != 0;
    const bool     is_test = opcode >= 8u && opcode <= 11u;
    const bool     to_pc   = !is_test && d->rd == ArmGpr::kR15;

    const bool is_arith = (opcode >= 2u && opcode <= 7u) || opcode == 10u ||
                          opcode == 11u;
    const bool reversed = opcode == 3u || opcode == 7u;
    const bool is_move  = opcode == 13u || opcode == 15u;

    if (to_pc && s) {
        /* B9.3.20 (p. B9-2013): UNPREDICTABLE in User and System mode. */
        cursor = EmitSpsrModeGuard(cursor, d, ctx);
    }

    const uint8_t shiftee = (reversed || is_move) ? kEax : kEcx;
    if (d->rm == ArmGpr::kR15) {
        EmitMovRegImm32(cursor, shiftee, ArmPcReadValue(d, ctx));
    } else {
        EmitMovRegBaseDisp32(cursor, shiftee, kStateReg, ArmGprDisp(d->rm));
    }

    /* Shift_C (A8.4.3 p. A8-293): amount == 0 leaves (value, carry_in). */
    const bool has_shifter_carry = shift_n != 0u;
    const bool capture_carry     = s && !is_arith && has_shifter_carry &&
                                   !to_pc;
    if (capture_carry) {
        EmitXorRegReg(cursor, kEdx, kEdx);
    }
    switch (shift_t) {
    case kSrLsl:
        if (shift_n != 0u) {
            EmitShlReg32Imm(cursor, shiftee, static_cast<uint8_t>(shift_n));
        }
        break;
    case kSrLsr:
        if (shift_n == 32u) {
            /* LSR_C #32 (A2.2.1 p. A2-42): result 0, carry = Rm<31>; the
               x86 count is masked mod 32 (SDM Vol. 2B 4-600). */
            EmitShrReg32Imm(cursor, shiftee, 31u);
            EmitShrReg32Imm(cursor, shiftee, 1u);
        } else {
            EmitShrReg32Imm(cursor, shiftee, static_cast<uint8_t>(shift_n));
        }
        break;
    case kSrAsr:
        if (shift_n == 32u) {
            /* ASR_C #32 (A2.2.1 p. A2-42): result and carry = Rm<31>. */
            EmitSarReg32Imm(cursor, shiftee, 31u);
            EmitSarReg32Imm(cursor, shiftee, 1u);
        } else {
            EmitSarReg32Imm(cursor, shiftee, static_cast<uint8_t>(shift_n));
        }
        break;
    case kSrRor:
        /* ROR_C (A2.2.1 p. A2-42): carry = result<31> = the x86 ROR CF
           (SDM Vol. 2B 4-533). */
        EmitRorReg32Imm(cursor, shiftee, static_cast<uint8_t>(shift_n));
        break;
    default:
        /* RRX_C (A2.2.1 p. A2-43): result = carry_in : Rm<31:1>,
           carry = Rm<0>. */
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 1u);
        EmitCmc(cursor);
        EmitRcrReg32By1(cursor, shiftee);
        break;
    }
    if (capture_carry) {
        EmitSetccReg8(cursor, kSetC, kDl);
    }

    if (!is_move) {
        const uint8_t other = reversed ? kEcx : kEax;
        if (d->rn == ArmGpr::kR15) {
            EmitMovRegImm32(cursor, other, ArmPcReadValue(d, ctx));
        } else {
            EmitMovRegBaseDisp32(cursor, other, kStateReg, ArmGprDisp(d->rn));
        }
    }

    switch (opcode) {
    case 0u:  EmitAndReg32Reg32(cursor, kEax, kEcx); break;
    case 1u:
    case 9u:  EmitXorRegReg(cursor, kEax, kEcx);     break;
    case 2u:
    case 3u:
    case 10u: EmitSubReg32Reg32(cursor, kEax, kEcx); break;
    case 4u:
    case 11u: EmitAddReg32Reg32(cursor, kEax, kEcx); break;
    case 5u:
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 1u);
        EmitCmc(cursor);
        EmitAdcReg32Reg32(cursor, kEax, kEcx);
        break;
    case 6u:
    case 7u:
        /* SBC / RSC (A8.8.162 p. A8-595 / A8.8.156 p. A8-583):
           AddWithCarry(x, NOT(y), APSR.C) subtracts NOT(C) where the x86
           SBB subtracts CF (SDM Vol. 2B 4-608). */
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 1u);
        EmitSbbReg32Reg32(cursor, kEax, kEcx);
        break;
    case 8u:  EmitTestRegReg(cursor, kEax, kEcx);    break;
    case 12u: EmitOrReg32Reg32(cursor, kEax, kEcx);  break;
    case 13u:
        if (s && !to_pc) {
            EmitTestRegReg(cursor, kEax, kEax);
        }
        break;
    case 14u:
        /* BIC (A8.8.22 p. A8-343): Rn AND NOT(shifted); NOT leaves the
           flags, the captured shifter carry included (SDM Vol. 2B
           4-170). */
        EmitNotReg32(cursor, kEcx);
        EmitAndReg32Reg32(cursor, kEax, kEcx);
        break;
    case kDpOrn:
        /* ORN (register) (A8.8.121 p. A8-515): Rn OR NOT(shifted). */
        EmitNotReg32(cursor, kEcx);
        EmitOrReg32Reg32(cursor, kEax, kEcx);
        break;
    case 15u:
        /* MVN (A8.8.116 p. A8-507): NOT(shifted). */
        EmitNotReg32(cursor, kEax);
        if (s && !to_pc) {
            EmitTestRegReg(cursor, kEax, kEax);
        }
        break;
    default:
        ctx->emit->Owner().Panic(
            "data-processing (register) opcode %u not implemented at guest pc=0x%08X",
            opcode, d->guest_address);
    }

    if (to_pc) {
        return EmitDpPcWriteTail(cursor, d, ctx);
    }
    if (!is_test) {
        EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd), kEax);
    }
    if (!s) {
        return cursor;
    }
    if (is_arith) {
        return EmitDpArithFlagTail(cursor, d);
    }

    return EmitDpLogicalFlagTail(cursor,
                                 has_shifter_carry ? DpLogicalCarry::kFromDl
                                                   : DpLogicalCarry::kUnchanged);
}

}
