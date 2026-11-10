#pragma once

#include <cstdint>

#include <cerfengine/sh/sh_insn_kind.h>

namespace cerfengine {

enum class ShCmp : uint8_t { kEq, kGe, kGt, kHi, kHs, kPl, kPz, kStr, kEqImm };

enum class ShJumpTarget : uint8_t { kRegister, kPcPlusRegister, kProcedureRegister };

enum class ShLogic : uint8_t { kAnd, kOr, kXor, kTest };

enum class ShArith : uint8_t {
    kAdd,
    kAddc,
    kAddv,
    kSub,
    kSubc,
    kSubv,
    kNeg,
    kNegc,
    kDt,
    kDiv0s,
    kDiv0u,
    kDiv1,
    kExtsB,
    kExtsW,
    kExtuB,
    kExtuW,
};

enum class ShShift : uint8_t {
    kShll,
    kShlr,
    kShal,
    kShar,
    kShll2,
    kShlr2,
    kShll8,
    kShlr8,
    kShll16,
    kShlr16,
    kRotl,
    kRotr,
    kRotcl,
    kRotcr,
    kShad,
    kShld,
};

enum class ShMultiply : uint8_t { kMulL, kMulsW, kMuluW, kDmulsL, kDmuluL };

enum class ShSystemRegister : uint8_t {
    kMach = 0x0,
    kMacl = 0x1,
    kPr   = 0x2,
    kDsr  = 0x6,
    kA0   = 0x7,
    kX0   = 0x8,
    kX1   = 0x9,
    kY0   = 0xA,
    kY1   = 0xB,
};

enum class ShControlRegister : uint8_t {
    kSr    = 0x0,
    kGbr   = 0x1,
    kVbr   = 0x2,
    kSsr   = 0x3,
    kSpc   = 0x4,
    kMod   = 0x5,
    kRs    = 0x6,
    kRe    = 0x7,
    kBank0 = 0x8,
};

constexpr uint32_t ShPcValue(uint32_t guest_address) { return guest_address + 4u; }

constexpr uint32_t kLdtlbShadowInsns = 2u;

struct ShDecodedInsn {
    ShInsnKind       kind;
    uint8_t          n;
    uint8_t          m;
    uint8_t          width;
    uint8_t          cycles;
    uint8_t          delayed_branch;
    uint8_t          slot_illegal;
    uint8_t          ends_block;
    uint8_t          branch_if_t;
    uint8_t          link;
    uint8_t          dsp_mode;
    uint8_t          privileged;
    ShCmp            cmp;
    ShLogic          logic;
    ShArith          arith;
    ShShift          shift;
    ShMultiply       multiply;
    ShJumpTarget     jump_target;
    ShSystemRegister system_register;
    uint8_t          control_register;
    uint16_t         raw;
    uint32_t         guest_address;
    uint32_t         disp;
};

}
