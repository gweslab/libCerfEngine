#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>
#include "../x86_emit_alu.h"

namespace cerfengine {


namespace mips_emit {

inline int32_t GprLoOff(uint32_t r) {
    return static_cast<int32_t>(offsetof(MipsCpuState, gpr)) +
           static_cast<int32_t>(r) * 8;
}
inline int32_t GprHiOff(uint32_t r) { return GprLoOff(r) + 4; }

inline void EmitLoadGprLo(uint8_t*& c, uint8_t reg, uint32_t r) {
    x86::EmitMovRegBaseDisp32(c, reg, x86::kStateReg, GprLoOff(r));
}

/* CDQ - 99, EDX:EAX = sign-extend of EAX (SDM Vol. 2A 3-314 CWD/CDQ/CQO). */
inline void EmitStoreGprSextEax(uint8_t*& c, uint32_t r) {
    x86::EmitCdq(c);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprLoOff(r), x86::kEax);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprHiOff(r), x86::kEdx);
}

inline void EmitStoreGprImm64(uint8_t*& c, uint32_t r, uint32_t lo, uint32_t hi) {
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, GprLoOff(r), lo);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, GprHiOff(r), hi);
}

inline void EmitStoreGprSextImm32(uint8_t*& c, uint32_t r, uint32_t v) {
    EmitStoreGprImm64(c, r, v, (v & 0x80000000u) ? 0xFFFFFFFFu : 0u);
}

inline void EmitMoveGpr64ToField(uint8_t*& c, int32_t field_off, uint32_t rs) {
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, field_off, x86::kEax);
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, field_off + 4, x86::kEax);
}

inline void EmitShiftImm32Sext(uint8_t*& c, uint32_t rd, uint32_t rt, uint32_t sa,
                               uint8_t ext) {
    if (rd == 0) {
        return;
    }
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rt));
    if (sa != 0) {
        x86::Emit8(c, 0xC1);       /* C1 /ext ib, shift r/m32, imm8
                                      (SDM Vol. 2B 4-599 SAL/SAR/SHL/SHR) */
        x86::EmitModRmReg(c, 3, x86::kEax, ext);
        x86::Emit8(c, static_cast<uint8_t>(sa));
    }
    EmitStoreGprSextEax(c, rd);
}

inline void EmitDShiftImm(uint8_t*& c, uint32_t rd, uint32_t rt, uint8_t sa,
                          bool left, uint8_t outer_ext) {
    if (rd == 0) {
        return;
    }
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rt));
    x86::EmitMovRegBaseDisp32(c, x86::kEdx, x86::kStateReg, GprHiOff(rt));
    if (sa != 0) {
        if (left) {
            x86::Emit8(c, 0x0F); x86::Emit8(c, 0xA4);   /* SHLD edx,eax,sa - 0F A4 /r ib
                                                           (SDM Vol. 2B 4-635 SHLD) */
            x86::EmitModRmReg(c, 3, x86::kEdx, x86::kEax);
            x86::Emit8(c, sa);
            x86::Emit8(c, 0xC1); x86::EmitModRmReg(c, 3, x86::kEax, outer_ext);  /* SHL eax,sa - C1 /4 ib
                                                           (SDM Vol. 2B 4-600) */
            x86::Emit8(c, sa);
        } else {
            x86::Emit8(c, 0x0F); x86::Emit8(c, 0xAC);   /* SHRD eax,edx,sa - 0F AC /r ib
                                                           (SDM Vol. 2B 4-638 SHRD) */
            x86::EmitModRmReg(c, 3, x86::kEax, x86::kEdx);
            x86::Emit8(c, sa);
            x86::Emit8(c, 0xC1); x86::EmitModRmReg(c, 3, x86::kEdx, outer_ext);  /* SHR/SAR edx,sa - C1 /5, /7 ib
                                                           (SDM Vol. 2B 4-599/4-600) */
            x86::Emit8(c, sa);
        }
    }
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprLoOff(rd), x86::kEax);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprHiOff(rd), x86::kEdx);
}

inline void EmitRtypeBitwise64(uint8_t*& c, uint32_t rd, uint32_t rs, uint32_t rt,
                               uint8_t alu_opcode) {
    if (rd == 0) {
        return;
    }
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::Emit8(c, alu_opcode);
    x86::EmitModRmReg(c, 2, x86::kStateReg, x86::kEax);
    x86::Emit32(c, static_cast<uint32_t>(GprLoOff(rt)));
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprLoOff(rd), x86::kEax);
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::Emit8(c, alu_opcode);
    x86::EmitModRmReg(c, 2, x86::kStateReg, x86::kEax);
    x86::Emit32(c, static_cast<uint32_t>(GprHiOff(rt)));
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprHiOff(rd), x86::kEax);
}

inline void EmitRtypeArith32Sext(uint8_t*& c, uint32_t rd, uint32_t rs,
                                 uint32_t rt, uint8_t alu_opcode) {
    if (rd == 0) {
        return;
    }
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::Emit8(c, alu_opcode);
    x86::EmitModRmReg(c, 2, x86::kStateReg, x86::kEax);
    x86::Emit32(c, static_cast<uint32_t>(GprLoOff(rt)));
    EmitStoreGprSextEax(c, rd);
}

inline void EmitRtypeArith64(uint8_t*& c, uint32_t rd, uint32_t rs, uint32_t rt,
                             uint8_t lo_opcode, uint8_t hi_opcode) {
    if (rd == 0) {
        return;
    }
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::Emit8(c, lo_opcode);
    x86::EmitModRmReg(c, 2, x86::kStateReg, x86::kEax);
    x86::Emit32(c, static_cast<uint32_t>(GprLoOff(rt)));
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprLoOff(rd), x86::kEax);
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::Emit8(c, hi_opcode);
    x86::EmitModRmReg(c, 2, x86::kStateReg, x86::kEax);
    x86::Emit32(c, static_cast<uint32_t>(GprHiOff(rt)));
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprHiOff(rd), x86::kEax);
}

inline void EmitSltImm64(uint8_t*& c, uint32_t rd, uint32_t rs, uint32_t imm16,
                         uint8_t setcc_op) {
    if (rd == 0) {
        return;
    }
    const uint32_t imm_lo = static_cast<uint32_t>(static_cast<int32_t>(
                                static_cast<int16_t>(imm16)));
    const uint8_t imm_hi8 = (imm16 & 0x8000u) ? 0xFFu : 0x00u;
    x86::EmitXorRegReg(c, x86::kEcx, x86::kEcx);
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    /* SUB EAX, imm32 - 2D id (SDM Vol. 2B 4-681 SUB). */
    x86::Emit8(c, 0x2D);
    x86::Emit32(c, imm_lo);
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::Emit8(c, 0x83);                       /* SBB eax, imm8 - 83 /3 ib
                                                  (SDM Vol. 2B 4-608 SBB) */
    x86::EmitModRmReg(c, 3, x86::kEax, 3);
    x86::Emit8(c, imm_hi8);
    x86::Emit8(c, 0x0F);                       /* SETcc cl - 0F 9x, Op/En M
                                                  (SDM Vol. 2B 4-618 SETcc) */
    x86::Emit8(c, setcc_op);
    x86::EmitModRmReg(c, 3, x86::kCl, 0);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprLoOff(rd), x86::kEcx);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, GprHiOff(rd), 0);
}

inline void EmitTrappingArith32Tail(uint8_t*& c, uint32_t dst, void* owner,
                                    void* overflow_helper, uint32_t guest_addr) {
    uint8_t* j_ok = x86::EmitJnoLabel(c);
    x86::EmitPush32(c, guest_addr);
    x86::EmitPush32(c, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)));
    x86::EmitCall(c, overflow_helper);
    x86::FixupLabel(j_ok, c);
    if (dst != 0) {
        EmitStoreGprSextEax(c, dst);
    }
}

inline void EmitTrappingArith64Tail(uint8_t*& c, uint32_t dst, void* owner,
                                    void* overflow_helper, uint32_t guest_addr) {
    uint8_t* j_ok = x86::EmitJnoLabel(c);
    x86::EmitPush32(c, guest_addr);
    x86::EmitPush32(c, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner)));
    x86::EmitCall(c, overflow_helper);
    x86::FixupLabel(j_ok, c);
    if (dst != 0) {
        x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprLoOff(dst), x86::kEax);
        x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, GprHiOff(dst), x86::kEdx);
    }
}

inline void EmitTrappingArith64RR(uint8_t*& c, uint32_t rd, uint32_t rs, uint32_t rt,
                                  uint8_t lo_op, uint8_t hi_op, void* owner,
                                  void* overflow_helper, uint32_t guest_addr) {
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::Emit8(c, lo_op);
    x86::EmitModRmReg(c, 2, x86::kStateReg, x86::kEax);
    x86::Emit32(c, static_cast<uint32_t>(GprLoOff(rt)));
    x86::EmitMovRegBaseDisp32(c, x86::kEdx, x86::kStateReg, GprHiOff(rs));
    x86::Emit8(c, hi_op);
    x86::EmitModRmReg(c, 2, x86::kStateReg, x86::kEdx);
    x86::Emit32(c, static_cast<uint32_t>(GprHiOff(rt)));
    EmitTrappingArith64Tail(c, rd, owner, overflow_helper, guest_addr);
}

inline int32_t BranchStateOff() { return static_cast<int32_t>(offsetof(MipsCpuState, branch_state)); }
inline int32_t BtargetOff()     { return static_cast<int32_t>(offsetof(MipsCpuState, btarget)); }
inline int32_t BcondOff()       { return static_cast<int32_t>(offsetof(MipsCpuState, bcond)); }
inline int32_t BtargetIsaOff()  { return static_cast<int32_t>(offsetof(MipsCpuState, btarget_isa)); }
inline int32_t BranchLenOff()   { return static_cast<int32_t>(offsetof(MipsCpuState, branch_len)); }

/* QEMU MIPS gen_compute_branch OPC_J/OPC_JAL/OPC_JALX (translate.c). */
inline void EmitBranchUncond(uint8_t*& c, uint32_t btarget, uint32_t btarget_isa,
                             uint32_t branch_len) {
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BtargetOff(), btarget);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BtargetIsaOff(), btarget_isa);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchLenOff(), branch_len);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchStateOff(), MipsBranch::kUncond);
}

inline void EmitBranchRegister(uint8_t*& c, uint32_t rs, bool mips16,
                               uint32_t branch_len) {
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    if (mips16) {
        x86::EmitMovRegReg(c, x86::kEcx, x86::kEax);
        x86::EmitAndRegImm32(c, x86::kEcx, 1u);
        x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, BtargetIsaOff(), x86::kEcx);
        x86::EmitAndRegImm32(c, x86::kEax, ~1u);
    } else {
        x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BtargetIsaOff(), 0u);
    }
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, BtargetOff(), x86::kEax);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchLenOff(), branch_len);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchStateOff(), MipsBranch::kRegister);
}

inline void EmitBranchCondEq(uint8_t*& c, uint32_t rs, uint32_t rt, uint32_t btarget,
                             bool take_if_equal, uint32_t branch_state,
                             uint32_t branch_len) {
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::EmitCmpRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rt));
    uint8_t* j_neq_lo = x86::EmitJnzLabel(c);
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::EmitCmpRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rt));
    uint8_t* j_neq_hi = x86::EmitJnzLabel(c);
    x86::EmitMovRegImm32(c, x86::kEax, take_if_equal ? 1u : 0u);
    uint8_t* j_done = x86::EmitJmpLabel(c);
    uint8_t* neq_label = c;
    x86::FixupLabel(j_neq_lo, neq_label);
    x86::FixupLabel(j_neq_hi, neq_label);
    x86::EmitMovRegImm32(c, x86::kEax, take_if_equal ? 0u : 1u);
    x86::FixupLabel(j_done, c);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, BcondOff(), x86::kEax);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BtargetOff(), btarget);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchLenOff(), branch_len);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchStateOff(), branch_state);
}

inline void EmitBranchCondSign(uint8_t*& c, uint32_t rs, uint32_t btarget,
                               bool take_if_neg, uint32_t branch_state,
                               uint32_t branch_len) {
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::EmitTestRegReg(c, x86::kEax, x86::kEax);
    uint8_t* j_neg = x86::EmitJsLabel32(c);
    x86::EmitMovRegImm32(c, x86::kEax, take_if_neg ? 0u : 1u);
    uint8_t* j_done = x86::EmitJmpLabel(c);
    uint8_t* neg_label = c;
    x86::FixupLabel32(j_neg, neg_label);
    x86::EmitMovRegImm32(c, x86::kEax, take_if_neg ? 1u : 0u);
    x86::FixupLabel(j_done, c);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, BcondOff(), x86::kEax);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BtargetOff(), btarget);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchLenOff(), branch_len);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchStateOff(), branch_state);
}

inline void EmitBranchCondGtz(uint8_t*& c, uint32_t rs, uint32_t btarget,
                              uint32_t branch_state, uint32_t branch_len) {
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::EmitTestRegReg(c, x86::kEax, x86::kEax);
    uint8_t* j_neg = x86::EmitJsLabel32(c);
    uint8_t* j_pos = x86::EmitJnzLabel(c);
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::EmitTestRegReg(c, x86::kEax, x86::kEax);
    uint8_t* j_lo_nz = x86::EmitJnzLabel(c);
    uint8_t* nottaken_label = c;
    x86::FixupLabel32(j_neg, nottaken_label);
    x86::EmitMovRegImm32(c, x86::kEax, 0u);
    uint8_t* j_done = x86::EmitJmpLabel(c);
    uint8_t* taken_label = c;
    x86::FixupLabel(j_pos, taken_label);
    x86::FixupLabel(j_lo_nz, taken_label);
    x86::EmitMovRegImm32(c, x86::kEax, 1u);
    x86::FixupLabel(j_done, c);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, BcondOff(), x86::kEax);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BtargetOff(), btarget);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchLenOff(), branch_len);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchStateOff(), branch_state);
}

inline void EmitBranchCondLez(uint8_t*& c, uint32_t rs, uint32_t btarget,
                              uint32_t branch_state, uint32_t branch_len) {
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprHiOff(rs));
    x86::EmitTestRegReg(c, x86::kEax, x86::kEax);
    uint8_t* j_taken_neg = x86::EmitJsLabel32(c);
    uint8_t* j_nt_hi = x86::EmitJnzLabel(c);
    x86::EmitMovRegBaseDisp32(c, x86::kEax, x86::kStateReg, GprLoOff(rs));
    x86::EmitTestRegReg(c, x86::kEax, x86::kEax);
    uint8_t* j_nt_lo = x86::EmitJnzLabel(c);
    uint8_t* taken_label = c;
    x86::FixupLabel32(j_taken_neg, taken_label);
    x86::EmitMovRegImm32(c, x86::kEax, 1u);
    uint8_t* j_done = x86::EmitJmpLabel(c);
    uint8_t* nt_label = c;
    x86::FixupLabel(j_nt_hi, nt_label);
    x86::FixupLabel(j_nt_lo, nt_label);
    x86::EmitMovRegImm32(c, x86::kEax, 0u);
    x86::FixupLabel(j_done, c);
    x86::EmitMovBaseDisp32Reg(c, x86::kStateReg, BcondOff(), x86::kEax);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BtargetOff(), btarget);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchLenOff(), branch_len);
    x86::EmitMovBaseDisp32Imm32(c, x86::kStateReg, BranchStateOff(), branch_state);
}

}

}
