#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/sh/sh_cpu_state.h>

namespace cerfengine {

enum class ShDspReg : uint8_t {
    kA1  = 0x5,
    kA0  = 0x7,
    kX0  = 0x8,
    kX1  = 0x9,
    kY0  = 0xA,
    kY1  = 0xB,
    kM0  = 0xC,
    kA1G = 0xD,
    kM1  = 0xE,
    kA0G = 0xF,
};

constexpr size_t kShDspRegOffset[16] = {
    0u,
    0u,
    0u,
    0u,
    0u,
    offsetof(ShCpuState, a1),
    0u,
    offsetof(ShCpuState, a0),
    offsetof(ShCpuState, x0),
    offsetof(ShCpuState, x1),
    offsetof(ShCpuState, y0),
    offsetof(ShCpuState, y1),
    offsetof(ShCpuState, m0),
    offsetof(ShCpuState, a1g),
    offsetof(ShCpuState, m1),
    offsetof(ShCpuState, a0g),
};

constexpr size_t ShDspRegOffset(ShDspReg reg) { return kShDspRegOffset[static_cast<uint32_t>(reg)]; }

constexpr bool ShDspHasGuard(ShDspReg reg) { return reg == ShDspReg::kA0 || reg == ShDspReg::kA1; }

constexpr ShDspReg ShDspGuardOf(ShDspReg reg) { return reg == ShDspReg::kA0 ? ShDspReg::kA0G : ShDspReg::kA1G; }

constexpr bool ShDspIsGuard(ShDspReg reg) { return reg == ShDspReg::kA0G || reg == ShDspReg::kA1G; }

constexpr ShDspReg kShDspSx[4] = {ShDspReg::kX0, ShDspReg::kX1, ShDspReg::kA0, ShDspReg::kA1};
constexpr ShDspReg kShDspSy[4] = {ShDspReg::kY0, ShDspReg::kY1, ShDspReg::kM0, ShDspReg::kM1};
constexpr ShDspReg kShDspSe[4] = {ShDspReg::kX0, ShDspReg::kX1, ShDspReg::kY0, ShDspReg::kA1};
constexpr ShDspReg kShDspSf[4] = {ShDspReg::kY0, ShDspReg::kY1, ShDspReg::kX0, ShDspReg::kA1};
constexpr ShDspReg kShDspDg[4] = {ShDspReg::kM0, ShDspReg::kM1, ShDspReg::kA0, ShDspReg::kA1};
constexpr ShDspReg kShDspDu[4] = {ShDspReg::kX0, ShDspReg::kY0, ShDspReg::kA0, ShDspReg::kA1};

constexpr bool ShDspRegCode(uint32_t z) {
    return z == 0x5u || z == 0x7u || (z >= 0x8u && z <= 0xCu) || z == 0xEu;
}

enum class ShDspOp : uint8_t {
    kReserved,
    kPshl,
    kPcmp,
    kPabs,
    kPdec,
    kPclr,
    kPsha,
    kPand,
    kPrnd,
    kPinc,
    kPdmsb,
    kPsubc,
    kPsub,
    kPxor,
    kPaddc,
    kPadd,
    kPor,
    kPneg,
    kPstsMach,
    kPstsMacl,
    kPcopy,
    kPldsMach,
    kPldsMacl,
    kPshlImm,
    kPshaImm,
    kPmuls,
    kPsubPmuls,
    kPaddPmuls,
    kPwsb,
    kPwad,
};

enum class ShDspForm : uint8_t { kSxSyDz, kSxSy, kSxDz, kSyDz, kDz, kImmDz, kMultiply, kMultiplyAlu };

enum class ShDspCondition : uint8_t { kAlways, kIfDcSet, kIfDcClear };

struct ShDspFieldB {
    ShDspOp        op;
    ShDspForm      form;
    ShDspCondition condition;
};

constexpr ShDspFieldB kShDspReservedB{ShDspOp::kReserved, ShDspForm::kDz, ShDspCondition::kAlways};

constexpr ShDspCondition ShDspConditionOf(uint32_t cc) {
    return cc == 2u ? ShDspCondition::kIfDcSet : cc == 3u ? ShDspCondition::kIfDcClear : ShDspCondition::kAlways;
}

constexpr ShDspFieldB ShDspConditional(uint32_t cc, ShDspOp op, ShDspForm form) {
    return cc == 0u ? kShDspReservedB : ShDspFieldB{op, form, ShDspConditionOf(cc)};
}

constexpr ShDspFieldB ShDspPaired(uint32_t cc, ShDspOp always, ShDspOp conditional, ShDspForm form) {
    return cc == 0u ? ShDspFieldB{always, form, ShDspCondition::kAlways} : ShDspConditional(cc, conditional, form);
}

constexpr ShDspFieldB ShDspDecodeOperation(uint32_t b) {
    const uint32_t hi = b >> 8;
    const uint32_t cc = hi & 3u;
    if (hi < 0x40u) {
        if ((hi & 0xF8u) == 0x00u) return {ShDspOp::kPshlImm, ShDspForm::kImmDz, ShDspCondition::kAlways};
        if ((hi & 0xF8u) == 0x10u) return {ShDspOp::kPshaImm, ShDspForm::kImmDz, ShDspCondition::kAlways};
        return kShDspReservedB;
    }
    if (hi < 0x80u) {
        switch (hi >> 4) {
        case 0x4u: return {ShDspOp::kPmuls, ShDspForm::kMultiply, ShDspCondition::kAlways};
        case 0x6u: return {ShDspOp::kPsubPmuls, ShDspForm::kMultiplyAlu, ShDspCondition::kAlways};
        case 0x7u: return {ShDspOp::kPaddPmuls, ShDspForm::kMultiplyAlu, ShDspCondition::kAlways};
        default: return kShDspReservedB;
        }
    }
    switch (hi & 0xFCu) {
    case 0x80u: return ShDspConditional(cc, ShDspOp::kPshl, ShDspForm::kSxSyDz);
    case 0x84u: return cc == 0u ? ShDspFieldB{ShDspOp::kPcmp, ShDspForm::kSxSy, ShDspCondition::kAlways} : kShDspReservedB;
    case 0x88u: return ShDspPaired(cc, ShDspOp::kPabs, ShDspOp::kPdec, ShDspForm::kSxDz);
    case 0x8Cu: return ShDspConditional(cc, ShDspOp::kPclr, ShDspForm::kDz);
    case 0x90u: return ShDspConditional(cc, ShDspOp::kPsha, ShDspForm::kSxSyDz);
    case 0x94u: return ShDspConditional(cc, ShDspOp::kPand, ShDspForm::kSxSyDz);
    case 0x98u: return ShDspPaired(cc, ShDspOp::kPrnd, ShDspOp::kPinc, ShDspForm::kSxDz);
    case 0x9Cu: return ShDspConditional(cc, ShDspOp::kPdmsb, ShDspForm::kSxDz);
    case 0xA0u: return ShDspPaired(cc, ShDspOp::kPsubc, ShDspOp::kPsub, ShDspForm::kSxSyDz);
    case 0xA4u: return ShDspPaired(cc, ShDspOp::kPwsb, ShDspOp::kPxor, ShDspForm::kSxSyDz);
    case 0xA8u: return ShDspPaired(cc, ShDspOp::kPabs, ShDspOp::kPdec, ShDspForm::kSyDz);
    case 0xB0u: return ShDspPaired(cc, ShDspOp::kPaddc, ShDspOp::kPadd, ShDspForm::kSxSyDz);
    case 0xB4u: return ShDspPaired(cc, ShDspOp::kPwad, ShDspOp::kPor, ShDspForm::kSxSyDz);
    case 0xB8u: return ShDspPaired(cc, ShDspOp::kPrnd, ShDspOp::kPinc, ShDspForm::kSyDz);
    case 0xBCu: return ShDspConditional(cc, ShDspOp::kPdmsb, ShDspForm::kSyDz);
    case 0xC8u: return ShDspConditional(cc, ShDspOp::kPneg, ShDspForm::kSxDz);
    case 0xCCu: return ShDspConditional(cc, ShDspOp::kPstsMach, ShDspForm::kDz);
    case 0xD8u: return ShDspConditional(cc, ShDspOp::kPcopy, ShDspForm::kSxDz);
    case 0xDCu: return ShDspConditional(cc, ShDspOp::kPstsMacl, ShDspForm::kDz);
    case 0xE8u: return ShDspConditional(cc, ShDspOp::kPneg, ShDspForm::kSyDz);
    case 0xECu: return ShDspConditional(cc, ShDspOp::kPldsMach, ShDspForm::kDz);
    case 0xF8u: return ShDspConditional(cc, ShDspOp::kPcopy, ShDspForm::kSyDz);
    case 0xFCu: return ShDspConditional(cc, ShDspOp::kPldsMacl, ShDspForm::kDz);
    default: return kShDspReservedB;
    }
}

constexpr bool ShDspOperationUnspecified(uint32_t b) {
    const ShDspOp op = ShDspDecodeOperation(b).op;
    return op == ShDspOp::kPwsb || op == ShDspOp::kPwad;
}

constexpr bool ShDspOperationDefined(uint32_t b) {
    const ShDspFieldB f = ShDspDecodeOperation(b);
    if (f.op == ShDspOp::kReserved) return false;
    switch (f.form) {
    case ShDspForm::kSxSyDz:
    case ShDspForm::kImmDz: return ShDspRegCode(b & 0xFu);
    case ShDspForm::kSxSy: return (b & 0x000Fu) == 0u;
    case ShDspForm::kSxDz: return (b & 0x0030u) == 0u && ShDspRegCode(b & 0xFu);
    case ShDspForm::kSyDz: return (b & 0x00C0u) == 0u && ShDspRegCode(b & 0xFu);
    case ShDspForm::kDz: return (b & 0x00F0u) == 0u && ShDspRegCode(b & 0xFu);
    case ShDspForm::kMultiply: return (b & 0x00F3u) == 0u;
    case ShDspForm::kMultiplyAlu: return true;
    }
    return false;
}

constexpr uint32_t ShDspPackOperation(ShDspFieldB f) {
    return static_cast<uint32_t>(f.op) | (static_cast<uint32_t>(f.form) << 8) |
           (static_cast<uint32_t>(f.condition) << 16);
}

constexpr ShDspFieldB ShDspUnpackOperation(uint32_t packed) {
    return {static_cast<ShDspOp>(packed & 0xFFu), static_cast<ShDspForm>((packed >> 8) & 0xFFu),
            static_cast<ShDspCondition>((packed >> 16) & 0xFFu)};
}

constexpr uint32_t kShDspAxR5       = 1u << 9;
constexpr uint32_t kShDspAyR7       = 1u << 8;
constexpr uint32_t kShDspXSecond    = 1u << 7;
constexpr uint32_t kShDspYSecond    = 1u << 6;
constexpr uint32_t kShDspXStore     = 1u << 5;
constexpr uint32_t kShDspYStore     = 1u << 4;
constexpr uint32_t kShDspXModeShift = 2u;
constexpr uint32_t kShDspModeMask   = 3u;
constexpr uint32_t kShDspXNopBits   = kShDspAxR5 | kShDspXSecond | kShDspXStore;
constexpr uint32_t kShDspYNopBits   = kShDspAyR7 | kShDspYSecond | kShDspYStore;

enum class ShDspAddressing : uint8_t { kNone, kIndirect, kIncrement, kIndex };

constexpr ShDspAddressing ShDspXMode(uint32_t a) {
    return static_cast<ShDspAddressing>((a >> kShDspXModeShift) & kShDspModeMask);
}
constexpr ShDspAddressing ShDspYMode(uint32_t a) { return static_cast<ShDspAddressing>(a & kShDspModeMask); }
constexpr ShDspAddressing ShDspBusMode(uint32_t a, bool x) { return x ? ShDspXMode(a) : ShDspYMode(a); }

constexpr bool ShDspUpdatesPointer(ShDspAddressing mode) {
    return mode == ShDspAddressing::kIncrement || mode == ShDspAddressing::kIndex;
}

constexpr bool ShDspTransferStore(uint32_t a, bool x) { return (a & (x ? kShDspXStore : kShDspYStore)) != 0u; }

constexpr ShDspReg ShDspTransferReg(uint32_t a, bool x) {
    const bool second = (a & (x ? kShDspXSecond : kShDspYSecond)) != 0u;
    if (ShDspTransferStore(a, x)) return second ? ShDspReg::kA1 : ShDspReg::kA0;
    if (x) return second ? ShDspReg::kX1 : ShDspReg::kX0;
    return second ? ShDspReg::kY1 : ShDspReg::kY0;
}

constexpr uint32_t kShDspAxGpr = 4u;
constexpr uint32_t kShDspAyGpr = 6u;
constexpr uint32_t kShDspIxGpr = 8u;
constexpr uint32_t kShDspIyGpr = 9u;

constexpr uint32_t ShDspPointerGpr(uint32_t a, bool x) {
    if (x) return kShDspAxGpr + ((a & kShDspAxR5) != 0u ? 1u : 0u);
    return kShDspAyGpr + ((a & kShDspAyR7) != 0u ? 1u : 0u);
}

constexpr bool ShDspTransferDefined(uint32_t a) {
    const bool x = ShDspXMode(a) != ShDspAddressing::kNone || (a & kShDspXNopBits) == 0u;
    const bool y = ShDspYMode(a) != ShDspAddressing::kNone || (a & kShDspYNopBits) == 0u;
    return x && y;
}

enum class ShDspSingleMode : uint8_t { kPreDecrement, kIndirect, kIncrement, kIndex };

constexpr uint32_t kShDspAsGpr[4] = {4u, 5u, 2u, 3u};

constexpr bool            ShDspSingleTransferPrefix(uint16_t raw) { return (raw & 0xFC00u) == 0xF400u; }
constexpr ShDspSingleMode ShDspSingleModeOf(uint16_t raw) { return static_cast<ShDspSingleMode>((raw >> 2) & 3u); }
constexpr bool            ShDspSingleLong(uint16_t raw) { return (raw & 2u) != 0u; }
constexpr bool            ShDspSingleStore(uint16_t raw) { return (raw & 1u) != 0u; }
constexpr uint32_t        ShDspSingleAsGpr(uint16_t raw) { return kShDspAsGpr[(raw >> 8) & 3u]; }
constexpr ShDspReg        ShDspSingleDs(uint16_t raw) { return static_cast<ShDspReg>((raw >> 4) & 0xFu); }

constexpr bool ShDspSingleTransferDefined(uint16_t raw) {
    const uint32_t ds = (raw >> 4) & 0xFu;
    return ds == 0x5u || ds >= 0x7u;
}

constexpr bool     ShDspParallelPrefix(uint16_t raw) { return (raw & 0xFC00u) == 0xF800u; }
constexpr bool     ShDspDoubleTransferPrefix(uint16_t raw) { return (raw & 0xFC00u) == 0xF000u; }
constexpr bool     ShDspReservedPrefix(uint16_t raw) { return (raw & 0xFC00u) == 0xFC00u; }
constexpr uint32_t ShDspFieldA(uint16_t raw) { return raw & 0x03FFu; }

}
