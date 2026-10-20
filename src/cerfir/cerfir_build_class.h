#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "cerfir_op.h"

namespace cerfengine::cerfir {

constexpr uint8_t kBuildEndsCycleRun   = 1u << 0;
constexpr uint8_t kBuildWritesAllSlots = 1u << 5;
constexpr uint8_t kBuildIsValueOp      = 1u << 6;
constexpr uint8_t kBuildTerminal       = 1u << 7;

constexpr std::array<uint8_t, static_cast<size_t>(Op::kCount)> BuildClasses() {
    std::array<uint8_t, static_cast<size_t>(Op::kCount)> classes{};
    for (size_t i = 0; i < classes.size(); ++i) {
        const Op       op    = static_cast<Op>(i);
        const uint32_t flags = kOpInfoTable[i].flags;
        uint8_t        cls   = 0u;
        if ((flags & (kOpTerminal | kOpControl | kOpMayFault)) != 0u || op == Op::kCallHelper) {
            cls |= kBuildEndsCycleRun;
        }
        if ((flags & kOpPure) != 0u || op == Op::kGetSlot) cls |= kBuildIsValueOp;
        if ((flags & kOpTerminal) != 0u) cls |= kBuildTerminal;
        if (op == Op::kCallHelper) cls |= kBuildWritesAllSlots;
        classes[i] = cls;
    }
    return classes;
}

inline constexpr std::array<uint8_t, static_cast<size_t>(Op::kCount)> kBuildClass = BuildClasses();

enum FlagWant : uint8_t {
    kWantN = 1u << 0,
    kWantZ = 1u << 1,
    kWantC = 1u << 2,
    kWantV = 1u << 3,
};

inline constexpr uint8_t kFlagCount[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

constexpr uint8_t kFastBinaryOp       = 1u << 4;
constexpr uint8_t kFastIdentityZero   = 1u << 5;
constexpr uint8_t kFastIdentityAllOne = 1u << 6;
constexpr uint8_t kFastFusable        = 1u << 7;

constexpr std::array<uint8_t, static_cast<size_t>(Op::kCount)> FastBinaryClasses() {
    std::array<uint8_t, static_cast<size_t>(Op::kCount)> classes{};
    const uint8_t arith = kFastBinaryOp | kWantN | kWantZ | kWantC | kWantV;
    const uint8_t logic = kFastBinaryOp | kWantN | kWantZ;
    classes[static_cast<size_t>(Op::kAdd)]    = arith | kFastIdentityZero | kFastFusable;
    classes[static_cast<size_t>(Op::kSub)]    = arith | kFastIdentityZero | kFastFusable;
    classes[static_cast<size_t>(Op::kAnd)]    = logic | kFastIdentityAllOne | kFastFusable;
    classes[static_cast<size_t>(Op::kOr)]     = logic | kFastIdentityZero | kFastFusable;
    classes[static_cast<size_t>(Op::kXor)]    = logic | kFastIdentityZero | kFastFusable;
    classes[static_cast<size_t>(Op::kAndNot)] = logic | kFastIdentityZero;
    classes[static_cast<size_t>(Op::kOrNot)]  = logic;
    classes[static_cast<size_t>(Op::kShl)]    = kFastBinaryOp | kFastIdentityZero;
    classes[static_cast<size_t>(Op::kLsr)]    = kFastBinaryOp | kFastIdentityZero;
    classes[static_cast<size_t>(Op::kAsr)]    = kFastBinaryOp | kFastIdentityZero;
    classes[static_cast<size_t>(Op::kRor)]    = kFastBinaryOp | kFastIdentityZero;
    return classes;
}

inline constexpr std::array<uint8_t, static_cast<size_t>(Op::kCount)> kFastBinary = FastBinaryClasses();

}
