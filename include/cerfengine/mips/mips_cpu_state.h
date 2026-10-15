#pragma once

#include <cstddef>
#include <cstdint>

namespace cerfengine {


namespace MipsCp0 {
    constexpr uint32_t kIndex    = 0;
    constexpr uint32_t kRandom   = 1;
    constexpr uint32_t kEntryLo0 = 2;
    constexpr uint32_t kEntryLo1 = 3;
    constexpr uint32_t kContext  = 4;
    constexpr uint32_t kPageMask = 5;
    constexpr uint32_t kWired    = 6;
    constexpr uint32_t kBadVAddr = 8;
    constexpr uint32_t kCount    = 9;
    constexpr uint32_t kEntryHi  = 10;
    constexpr uint32_t kCompare  = 11;
    constexpr uint32_t kStatus   = 12;
    constexpr uint32_t kCause    = 13;
    constexpr uint32_t kEPC      = 14;
    constexpr uint32_t kPRId     = 15;
    constexpr uint32_t kConfig   = 16;
    constexpr uint32_t kLLAddr   = 17;
    constexpr uint32_t kWatchLo  = 18;
    constexpr uint32_t kWatchHi  = 19;
    constexpr uint32_t kXContext = 20;
    constexpr uint32_t kECC      = 26;
    constexpr uint32_t kTagLo    = 28;
    constexpr uint32_t kTagHi    = 29;
    constexpr uint32_t kErrorEPC = 30;
}

/* QEMU MIPS CP0St_* (cpu.h). */
namespace MipsStatusBit {
    constexpr uint32_t kCU1 = 29;
    constexpr uint32_t kCU0 = 28;
    constexpr uint32_t kFR  = 26;
    constexpr uint32_t kBEV = 22;
    constexpr uint32_t kIM  = 8;
    constexpr uint32_t kKSU = 3;
    constexpr uint32_t kERL = 2;
    constexpr uint32_t kEXL = 1;
    constexpr uint32_t kIE  = 0;
}

/* QEMU MIPS CP0Ca_* (cpu.h). */
namespace MipsCauseBit {
    constexpr uint32_t kBD      = 31;
    constexpr uint32_t kIV      = 23;
    constexpr uint32_t kIP      = 8;
    constexpr uint32_t kExcCode = 2;
}

constexpr uint32_t kMipsCauseTimerIp = 1u << (MipsCauseBit::kIP + 7);
constexpr uint32_t kMipsCauseIpMask  = 0xFFu << MipsCauseBit::kIP;

namespace MipsIdle {
    constexpr uint32_t kNone    = 0;
    constexpr uint32_t kStandby = 1;
    constexpr uint32_t kSuspend = 2;
    constexpr uint32_t kStopCpu = 3;
    constexpr uint32_t kHalt    = 4;
}

/* CP0 Cause.ExcCode values (MIPS Vol III Table; QEMU tlb_helper.c do_interrupt
   `cause` assignments). */
namespace MipsExcCode {
    constexpr uint32_t kInt = 0;
    constexpr uint32_t kMod = 1;
    constexpr uint32_t kTLBL = 2;
    constexpr uint32_t kTLBS = 3;
    constexpr uint32_t kAdEL = 4;
    constexpr uint32_t kAdES = 5;
    constexpr uint32_t kSys = 8;
    constexpr uint32_t kBp  = 9;
    constexpr uint32_t kRI  = 10;
    constexpr uint32_t kOv  = 12;
}

/* QEMU mips_cpu_do_interrupt (tlb_helper.c): PC = (BEV ? exception_base + 0x200 : EBase)
   + offset; offset 0x000 for a 32-bit-address NOMATCH refill with EXL clear, else 0x180. */
namespace MipsExcVector {
    constexpr uint32_t kBaseNormal = 0x80000000u;
    constexpr uint32_t kBaseBev    = 0xBFC00200u;
    constexpr uint32_t kOffRefill  = 0x000u;
    constexpr uint32_t kOffGeneral = 0x180u;
}

constexpr uint32_t kMipsNumGpr     = 32;

/* QEMU hflags branch field (cpu.h MIPS_HFLAG_B/BC/BL/BR). */
namespace MipsBranch {
    constexpr uint32_t kNone       = 0;
    constexpr uint32_t kUncond     = 1;
    constexpr uint32_t kCond       = 2;
    constexpr uint32_t kRegister   = 3;
    constexpr uint32_t kCondLikely = 4;
}

/* QEMU MIPS r4k_tlb_t (internal.h); r4k_fill_tlb (tlb_helper.c) stores each PFN
   as "(get_tlb_pfn_from_entrylo(...) & ~mask) << 12". */
struct MipsTlbEntry {
    uint32_t vpn;
    uint32_t page_mask;
    uint16_t asid;
    uint8_t  g;
    uint8_t  v0, d0, c0;
    uint8_t  v1, d1, c1;
    uint8_t  pad[7];
    uint64_t pfn[2];
};

/* QEMU MIPS_TLB_MAX (mips-defs.h). */
constexpr uint32_t kMipsTlbMax = 128;

struct MipsCpuState {
    uint64_t gpr[kMipsNumGpr];
    uint64_t hi;
    uint64_t lo;
    uint32_t pc;

    /* QEMU translate.c gen_compute_branch, gen_branch, save_cpu_state. */
    uint32_t branch_state;
    uint32_t btarget;
    uint32_t bcond;

    uint32_t cp0_index;
    uint32_t cp0_random;
    uint32_t cp0_entrylo0;
    uint32_t cp0_entrylo1;
    uint32_t cp0_context;
    uint32_t cp0_pagemask;
    uint32_t cp0_wired;
    uint32_t cp0_badvaddr;
    uint32_t count_save;
    uint32_t cp0_entryhi;
    uint32_t cp0_compare;
    uint32_t cp0_status;
    uint32_t cp0_cause;
    uint32_t cp0_epc;
    uint32_t cp0_prid;
    uint32_t cp0_config;
    uint32_t cp0_watchlo;
    uint32_t cp0_watchhi;
    uint32_t cp0_taglo;
    uint32_t cp0_taghi;
    uint32_t cp0_errorepc;
    uint32_t cp0_lladdr;
    uint32_t cp0_xcontext;
    uint32_t cp0_ecc;

    MipsTlbEntry tlb[kMipsTlbMax];
    uint32_t     nb_tlb;
    uint32_t     tlb_in_use;   /* QEMU MIPS cpu.c reset: "tlb_in_use = env->tlb->nb_tlb" */

    uint32_t llbit;
    uint32_t ll_addr;

    uint32_t reset_pending;
    uint32_t deep_sleep;
    uint32_t guest_cycle_counter;
    uint32_t guest_cycle_deadline;
    uint64_t count_save_phase;
    uint32_t guest_cycle_hi;
    uint32_t guest_cycle_folded;
    uint32_t count_save_phase_den;

    uint32_t min_page_shift;

    uint32_t phys_addr_mask;

    uint32_t isa_mode;

    uint32_t btarget_isa;

    /* QEMU MIPS exception_resume_pc (exception.c): a delay-slot EPC is the slot PC
       minus "MIPS_HFLAG_B16 ? 2 : 4". */
    uint32_t branch_len;

    uint32_t idle_wait;
    uint32_t idle_pad;
};

static_assert(offsetof(MipsTlbEntry, pfn) == 24, "MipsTlbEntry layout");
static_assert(sizeof(MipsTlbEntry) == 40, "MipsTlbEntry layout");
static_assert(sizeof(MipsCpuState) == 5584, "MipsCpuState layout");

inline void MipsResolvePendingBranch(MipsCpuState& s, uint32_t fallthrough) {
    if (s.branch_state == MipsBranch::kCond) {
        s.pc = s.bcond ? s.btarget : fallthrough;
    } else if (s.branch_state == MipsBranch::kCondLikely) {
        s.pc = s.btarget;
    } else {
        s.pc = s.btarget;
        s.isa_mode = s.btarget_isa;
    }
    s.branch_state = MipsBranch::kNone;
}

inline uint32_t MipsPairMinMask(uint32_t s) { return (1u << (s + 1u)) - 1u; }
inline uint32_t MipsVpn2Mask(uint32_t s)    { return ~MipsPairMinMask(s); }

}
