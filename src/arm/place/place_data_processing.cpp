#include <cstddef>

#include <cerfengine/engine.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceDataProcessing(uint8_t*      cursor,
                             DecodedInsn*  d,
                             BlockContext* ctx) {
    using namespace x86;

    const uint32_t opcode  = d->op1;
    const uint32_t imm32   = d->immediate;
    const bool     s       = d->s != 0;
    const bool     is_test = opcode >= 8u && opcode <= 11u;
    const bool     to_pc   = !is_test && d->rd == ArmGpr::kR15;

    const bool is_arith = (opcode >= 2u && opcode <= 7u) || opcode == 10u ||
                          opcode == 11u;
    const bool reversed = opcode == 3u || opcode == 7u;
    const bool is_move  = opcode == 13u || opcode == 15u;
    const uint32_t move_result = (opcode == 15u) ? ~imm32 : imm32;

    if (to_pc && s) {
        /* B9.3.20 (p. B9-2013): UNPREDICTABLE in User and System mode. */
        cursor = EmitSpsrModeGuard(cursor, d, ctx);
    }

    if (is_move && !to_pc) {
        EmitMovBaseDisp32Imm32(cursor, kStateReg, ArmGprDisp(d->rd), move_result);
        if (s) {
            EmitMovByteBaseDisp32Imm8(
                cursor, kStateReg, ArmNfDisp(),
                (move_result & 0x80000000u) != 0u ? 1u : 0u);
            EmitMovByteBaseDisp32Imm8(cursor, kStateReg, ArmZfDisp(),
                                      move_result == 0u ? 1u : 0u);
            if (d->rs != 0u) {
                EmitMovByteBaseDisp32Imm8(
                    cursor, kStateReg, ArmCfDisp(),
                    (imm32 & 0x80000000u) != 0u ? 1u : 0u);
            }
        }
        return cursor;
    }

    if (is_move) {
        EmitMovRegImm32(cursor, kEax, move_result);
    } else if (reversed) {
        if (d->rn == ArmGpr::kR15) {
            EmitMovRegImm32(cursor, kEcx, ArmPcReadValue(d, ctx));
        } else {
            EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rn));
        }
        EmitMovRegImm32(cursor, kEax, imm32);
        if (opcode == 3u) {
            EmitSubReg32Reg32(cursor, kEax, kEcx);
        } else {
            EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 1u);
            EmitSbbReg32Reg32(cursor, kEax, kEcx);
        }
    } else {
        if (d->rn == ArmGpr::kR15) {
            EmitMovRegImm32(cursor, kEax, ArmPcReadValue(d, ctx));
        } else {
            EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rn));
        }
        switch (opcode) {
        case 0u:  EmitAndRegImm32(cursor, kEax, imm32);  break;
        case 1u:  EmitXorRegImm32(cursor, kEax, imm32);  break;
        case 2u:  EmitSubRegImm32(cursor, kEax, imm32);  break;
        case 4u:  EmitAddRegImm32(cursor, kEax, imm32);  break;
        case 5u:
            EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 1u);
            EmitCmc(cursor);
            EmitAdcRegImm32(cursor, kEax, imm32);
            break;
        case 6u:
            EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 1u);
            EmitSbbRegImm32(cursor, kEax, imm32);
            break;
        case 8u:  EmitTestRegImm32(cursor, kEax, imm32); break;
        case 9u:  EmitXorRegImm32(cursor, kEax, imm32);  break;
        case 10u: EmitCmpRegImm32(cursor, kEax, imm32);  break;
        case 11u: EmitAddRegImm32(cursor, kEax, imm32);  break;
        case 12u: EmitOrRegImm32(cursor, kEax, imm32);   break;
        case 14u: EmitAndRegImm32(cursor, kEax, ~imm32); break;
        case kDpOrn: EmitOrRegImm32(cursor, kEax, ~imm32); break;
        default:
            ctx->emit->Owner().Panic("PlaceDataProcessing: opcode %u has no emit path "
                                     "(pc=0x%08X)", opcode, d->guest_address);
        }
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

    DpLogicalCarry carry = DpLogicalCarry::kUnchanged;
    if (d->rs != 0u) {
        carry = (imm32 & 0x80000000u) ? DpLogicalCarry::kSetImm
                                      : DpLogicalCarry::kClearImm;
    }
    return EmitDpLogicalFlagTail(cursor, carry);
}

}
