#include <cstddef>

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

namespace {

uint8_t* EmitShiftByClCapture(uint8_t* cursor, uint32_t type) {
    using namespace x86;

    EmitTestRegReg(cursor, kEcx, kEcx);
    uint8_t* zero_l = EmitJzLabel32(cursor);
    uint8_t* done_a = nullptr;
    uint8_t* done_b = nullptr;
    switch (type) {
    case 0u:
    case 1u: {
        EmitCmpRegImm8(cursor, kEcx, 32);
        uint8_t* small_l = EmitJbLabel32(cursor);
        uint8_t* eq_l    = EmitJzLabel32(cursor);
        EmitXorRegReg(cursor, kEax, kEax);
        done_a = EmitJmpLabel32(cursor);
        FixupLabel32(eq_l, cursor);
        EmitBtRegImm8(cursor, kEax, type == 0u ? 0u : 31u);
        EmitMovRegImm32(cursor, kEax, 0u);
        done_b = EmitJmpLabel32(cursor);
        FixupLabel32(small_l, cursor);
        if (type == 0u) {
            EmitShlReg32Cl(cursor, kEax);
        } else {
            EmitShrReg32Cl(cursor, kEax);
        }
        break;
    }
    case 2u: {
        EmitCmpRegImm8(cursor, kEcx, 32);
        uint8_t* small_l = EmitJbLabel32(cursor);
        EmitSarReg32Imm(cursor, kEax, 31u);
        EmitSarReg32Imm(cursor, kEax, 1u);
        done_a = EmitJmpLabel32(cursor);
        FixupLabel32(small_l, cursor);
        EmitSarReg32Cl(cursor, kEax);
        break;
    }
    default: {
        EmitTestRegImm32(cursor, kEcx, 31u);
        uint8_t* ror_l = EmitJnzLabel32(cursor);
        EmitBtRegImm8(cursor, kEax, 31u);
        done_a = EmitJmpLabel32(cursor);
        FixupLabel32(ror_l, cursor);
        EmitRorReg32Cl(cursor, kEax);
        break;
    }
    }
    uint8_t* done_c = EmitJmpLabel32(cursor);
    FixupLabel32(zero_l, cursor);
    EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 1u);
    EmitCmc(cursor);
    FixupLabel32(done_a, cursor);
    if (done_b != nullptr) FixupLabel32(done_b, cursor);
    FixupLabel32(done_c, cursor);
    EmitSetccReg8(cursor, kSetC, kDl);
    return cursor;
}

uint8_t* EmitShiftByCl(uint8_t* cursor, uint32_t type) {
    using namespace x86;

    switch (type) {
    case 0u:
    case 1u: {
        EmitCmpRegImm8(cursor, kEcx, 32);
        uint8_t* small_l = EmitJbLabel32(cursor);
        EmitXorRegReg(cursor, kEax, kEax);
        uint8_t* done_l = EmitJmpLabel32(cursor);
        FixupLabel32(small_l, cursor);
        if (type == 0u) {
            EmitShlReg32Cl(cursor, kEax);
        } else {
            EmitShrReg32Cl(cursor, kEax);
        }
        FixupLabel32(done_l, cursor);
        break;
    }
    case 2u: {
        EmitCmpRegImm8(cursor, kEcx, 32);
        uint8_t* small_l = EmitJbLabel32(cursor);
        EmitMovRegImm32(cursor, kEcx, 31u);
        FixupLabel32(small_l, cursor);
        EmitSarReg32Cl(cursor, kEax);
        break;
    }
    default:
        EmitRorReg32Cl(cursor, kEax);
        break;
    }
    return cursor;
}

}

uint8_t* PlaceDataProcessingShiftedReg(uint8_t*      cursor,
                                       DecodedInsn*  d,
                                       BlockContext*) {
    using namespace x86;

    const uint32_t opcode  = d->op1;
    const uint32_t type    = d->n;
    const bool     s       = d->s != 0;
    const bool     is_test = opcode >= 8u && opcode <= 11u;

    const bool is_arith = (opcode >= 2u && opcode <= 7u) || opcode == 10u ||
                          opcode == 11u;
    const bool is_move = opcode == 13u || opcode == 15u;
    const bool capture = s && !is_arith;

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmGprDisp(d->rm));
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rs));
    EmitAndRegImm32(cursor, kEcx, 0xFFu);
    if (capture) {
        EmitXorRegReg(cursor, kEdx, kEdx);
        cursor = EmitShiftByClCapture(cursor, type);
    } else {
        cursor = EmitShiftByCl(cursor, type);
    }

    if (!is_move) {
        EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rn));
    }

    switch (opcode) {
    case 0u:  EmitAndReg32Reg32(cursor, kEax, kEcx); break;
    case 1u:
    case 9u:  EmitXorRegReg(cursor, kEax, kEcx);     break;
    case 2u:
        EmitSubReg32Reg32(cursor, kEcx, kEax);
        EmitMovRegReg(cursor, kEax, kEcx);
        break;
    case 10u: EmitSubReg32Reg32(cursor, kEcx, kEax); break;
    case 3u:  EmitSubReg32Reg32(cursor, kEax, kEcx); break;
    case 4u:
    case 11u: EmitAddReg32Reg32(cursor, kEax, kEcx); break;
    case 5u:
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 1u);
        EmitCmc(cursor);
        EmitAdcReg32Reg32(cursor, kEax, kEcx);
        break;
    case 6u:
        /* SBC (A8.8.163 p. A8-597): AddWithCarry(Rn, NOT(shifted),
           APSR.C) subtracts NOT(C) where the x86 SBB subtracts CF
           (SDM Vol. 2B 4-608). */
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 1u);
        EmitSbbReg32Reg32(cursor, kEcx, kEax);
        EmitMovRegReg(cursor, kEax, kEcx);
        break;
    case 7u:
        /* RSC (A8.8.157 p. A8-585): AddWithCarry(NOT(Rn), shifted,
           APSR.C). */
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 1u);
        EmitSbbReg32Reg32(cursor, kEax, kEcx);
        break;
    case 8u:  EmitTestRegReg(cursor, kEax, kEcx);    break;
    case 12u: EmitOrReg32Reg32(cursor, kEax, kEcx);  break;
    case 13u:
        if (s) {
            EmitTestRegReg(cursor, kEax, kEax);
        }
        break;
    case 14u:
        /* BIC (A8.8.23 Operation, p. A8-345): Rn AND NOT(shifted); NOT
           leaves the flags (SDM Vol. 2B 4-170). */
        EmitNotReg32(cursor, kEax);
        EmitAndReg32Reg32(cursor, kEax, kEcx);
        break;
    default:
        /* MVN (A8.8.117 p. A8-509): NOT(shifted). */
        EmitNotReg32(cursor, kEax);
        if (s) {
            EmitTestRegReg(cursor, kEax, kEax);
        }
        break;
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
    return EmitDpLogicalFlagTail(cursor, DpLogicalCarry::kFromDl);
}

}
