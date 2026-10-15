#pragma once

#include <cstddef>
#include <cstdint>

#include "../cycle_deadline.h"

namespace cerfengine {

/* ARM ARM DDI 0406C.c Table B1-1, p. B1-1139: CPSR.M[4:0] mode encodings. */
namespace ArmMode {
constexpr uint32_t kUser       = 0x10;
constexpr uint32_t kFiq        = 0x11;
constexpr uint32_t kIrq        = 0x12;
constexpr uint32_t kSupervisor = 0x13;
constexpr uint32_t kMonitor    = 0x16;
constexpr uint32_t kAbort      = 0x17;
constexpr uint32_t kHyp        = 0x1A;
constexpr uint32_t kUndefined  = 0x1B;
constexpr uint32_t kSystem     = 0x1F;
}

/* ARM ARM DDI 0406C.c A2.3, p. A2-45: SP, LR and PC are R13, R14 and R15. */
namespace ArmGpr {
constexpr uint32_t kR13 = 13;
constexpr uint32_t kR14 = 14;
constexpr uint32_t kR15 = 15;
}

union ArmPsrFull {
    uint32_t word;
    struct {
        uint32_t mode                 : 5;
        uint32_t thumb_mode           : 1;
        uint32_t fiq_disable          : 1;
        uint32_t irq_disable          : 1;
        uint32_t async_abort_disable  : 1;
        uint32_t endian               : 1;
        uint32_t it_high              : 6;
        uint32_t ge                   : 4;
        uint32_t reserved             : 4;
        uint32_t jazelle              : 1;
        uint32_t it_low               : 2;
        uint32_t saturation           : 1;
        uint32_t overflow             : 1;
        uint32_t carry                : 1;
        uint32_t zero                 : 1;
        uint32_t negative             : 1;
    } bits;
};

enum class ArmBank : uint32_t {
    kUsr, kFiq, kIrq, kSvc, kAbt, kUnd, kCount
};

/* ARM ARM DDI 0406C.c B1.3.3, p. B1-1152: SPSR[] resolves only FIQ, IRQ,
   Supervisor, Monitor, Abort, Hyp and Undefined; every other CPSR.M is
   "otherwise UNPREDICTABLE", so User and System have no SPSR. */
enum class ArmSpsrBank : uint32_t {
    kFiq, kIrq, kSvc, kAbt, kUnd, kCount
};

struct ArmCpuState {
    uint32_t    gprs[16];
    ArmPsrFull  cpsr{ArmMode::kSupervisor};

    /* QEMU ARM CPUARMState "cpsr flag cache for faster execution" (cpu.h),
       packed into the architectural word only in cpsr_read (helper.c). */
    uint8_t     nf;
    uint8_t     zf;
    uint8_t     cf;
    uint8_t     vf;

    uint32_t    sp_bank[static_cast<uint32_t>(ArmBank::kCount)];
    uint32_t    lr_bank[static_cast<uint32_t>(ArmBank::kCount)];
    ArmPsrFull  spsr_bank[static_cast<uint32_t>(ArmSpsrBank::kCount)];

    /* ARM ARM DDI 0406C.c Figure B1-2, p. B1-1144: "In addition FIQ mode has
       Banked copies of the ARM core registers R8 to R12". */
    uint32_t    r8_r12_fiq[5];

    uint64_t    vfp_d[32];
    uint32_t    fpscr;
    uint32_t    fpexc;

    uint32_t    ldrex_monitor_addr;
    uint32_t    ldrex_monitor_armed;

    uint32_t    guest_cycle_counter;
    uint32_t    guest_cycle_deadline;
    uint32_t    guest_cycle_hi;
    uint32_t    guest_cycle_folded;
    uint32_t    irq_interrupt_pending;
    uint32_t    reset_pending;
    uint32_t    deep_sleep;
    uint32_t    idle_wait;

    /* QEMU cpu_exit() (cpu-common.c) and CPUState::exit_request (cpu.h). */
    uint32_t    chain_exit_request;
    uint32_t    pad;
};

constexpr uint32_t kChainExitIrq   = 1u << 0;
constexpr uint32_t kChainExitReset = 1u << 1;
constexpr uint32_t kChainExitHost  = 1u << 2;
constexpr uint32_t kChainExitFlush = 1u << 3;
constexpr uint32_t kChainExitClock = 1u << 4;
constexpr uint32_t kChainExitIdle  = 1u << 5;

/* ARM ARM DDI 0406C.c B1.3.3, p. B1-1148: "Condition flags, bits[31:28] ...
   N, bit[31] ... Z, bit[30] ... C, bit[29] ... V, bit[28]". */
constexpr uint32_t kCpsrNzcvMask = 0xF0000000u;

inline uint32_t ArmPackCpsr(const ArmCpuState& state) {
    return (state.cpsr.word & ~kCpsrNzcvMask) |
           (static_cast<uint32_t>(state.nf) << 31) |
           (static_cast<uint32_t>(state.zf) << 30) |
           (static_cast<uint32_t>(state.cf) << 29) |
           (static_cast<uint32_t>(state.vf) << 28);
}

inline void ArmApplyCpsr(ArmCpuState& state, uint32_t word) {
    state.cpsr.word = word;
    state.nf = static_cast<uint8_t>((word >> 31) & 1u);
    state.zf = static_cast<uint8_t>((word >> 30) & 1u);
    state.cf = static_cast<uint8_t>((word >> 29) & 1u);
    state.vf = static_cast<uint8_t>((word >> 28) & 1u);
}

constexpr uint32_t kArmCpsrItMask   = 0x0600FC00u;
constexpr uint32_t kArmCpsrQ        = 1u << 27;
constexpr uint32_t kArmCpsrT        = 1u << 5;
constexpr uint32_t kArmCpsrModeMask = 0x1Fu;

inline uint32_t ArmItAdvance(uint32_t it) {
    if ((it & 0x7u) == 0u) {
        return 0u;
    }
    return (it & 0xE0u) | ((it << 1) & 0x1Fu);
}

inline bool ArmItInBlock(uint32_t it) {
    return (it & 0xFu) != 0u;
}

inline uint32_t ArmItFromCpsr(const ArmCpuState& state) {
    return (state.cpsr.bits.it_high << 2) | state.cpsr.bits.it_low;
}

inline uint32_t ArmItToCpsrBits(uint32_t it) {
    return ((it >> 2) << 10) | ((it & 0x3u) << 25);
}

inline void ArmItStoreToCpsr(ArmCpuState& state, uint32_t it) {
    state.cpsr.bits.it_high = it >> 2;
    state.cpsr.bits.it_low  = it & 0x3u;
}

inline bool ArmCycleDeadlineReached(const ArmCpuState& state) {
    return CycleDeadlineReached(state.guest_cycle_counter, state.guest_cycle_deadline);
}

constexpr int32_t ArmNfDisp() {
    return static_cast<int32_t>(offsetof(ArmCpuState, nf));
}

constexpr int32_t ArmZfDisp() {
    return static_cast<int32_t>(offsetof(ArmCpuState, zf));
}

constexpr int32_t ArmCfDisp() {
    return static_cast<int32_t>(offsetof(ArmCpuState, cf));
}

constexpr int32_t ArmVfDisp() {
    return static_cast<int32_t>(offsetof(ArmCpuState, vf));
}

constexpr int32_t ArmGprDisp(uint32_t n) {
    return static_cast<int32_t>(offsetof(ArmCpuState, gprs) + n * 4u);
}

constexpr int32_t ArmCpsrDisp() {
    return static_cast<int32_t>(offsetof(ArmCpuState, cpsr));
}

static_assert(offsetof(ArmCpuState, nf) % 4u == 0u,
              "nf must be 4-aligned: emitted code loads the flag quad as one "
              "dword");
static_assert(offsetof(ArmCpuState, zf) == offsetof(ArmCpuState, nf) + 1u &&
                  offsetof(ArmCpuState, cf) == offsetof(ArmCpuState, nf) + 2u &&
                  offsetof(ArmCpuState, vf) == offsetof(ArmCpuState, nf) + 3u,
              "nf/zf/cf/vf must stay consecutive: emitted code loads them as "
              "one dword and gathers them with kNzcvGatherMultiplier");

constexpr uint32_t kNzcvGatherMultiplier =
    (1u << 31) | (1u << 22) | (1u << 13) | (1u << 4);

constexpr bool ArmNzcvGatherIsExact() {
    for (uint32_t i = 0; i < 16u; ++i) {
        const uint32_t nf = (i >> 3) & 1u;
        const uint32_t zf = (i >> 2) & 1u;
        const uint32_t cf = (i >> 1) & 1u;
        const uint32_t vf = i & 1u;
        const uint32_t quad = nf | (zf << 8) | (cf << 16) | (vf << 24);
        const uint32_t want =
            (nf << 31) | (zf << 30) | (cf << 29) | (vf << 28);
        if (((quad * kNzcvGatherMultiplier) & kCpsrNzcvMask) != want) {
            return false;
        }
    }
    return true;
}

static_assert(ArmNzcvGatherIsExact(),
              "kNzcvGatherMultiplier must map the little-endian nf/zf/cf/vf "
              "quad onto CPSR bits 31/30/29/28 for all 16 flag combinations");

}
