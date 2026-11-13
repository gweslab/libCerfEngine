#pragma once

#include <cstdint>

namespace cerfengine {

constexpr uint32_t kShNumGpr  = 16;
constexpr uint32_t kShNumBank = 8;

constexpr uint32_t kShChainExitHost        = 1u << 0;
constexpr uint32_t kShChainExitInterrupt   = 1u << 1;
constexpr uint32_t kShChainExitSleep       = 1u << 2;
constexpr uint32_t kShChainExitHostRequest = 1u << 3;
constexpr uint32_t kShChainExitReset       = 1u << 4;

enum class ShResetKind : uint32_t { kPowerOn, kManual };

constexpr uint32_t kShSrMd           = 1u << 30;
constexpr uint32_t kShSrRb           = 1u << 29;
constexpr uint32_t kShSrBl           = 1u << 28;
constexpr uint32_t kShSrRc           = 0x0FFF0000u;
constexpr uint32_t kShSrImask        = 0x000000F0u;
constexpr uint32_t kShSrImaskShift   = 4u;
constexpr uint32_t kShSrDsp          = 1u << 12;
constexpr uint32_t kShSrM            = 1u << 9;
constexpr uint32_t kShSrQ            = 1u << 8;
constexpr uint32_t kShSrQShift       = 8u;
constexpr uint32_t kShSrS            = 1u << 1;
constexpr uint32_t kShSrT            = 1u << 0;
constexpr uint32_t kShSrWritableSh3  = 0x700003F3u;
constexpr uint32_t kShSrWritableDsp  = 0x7FFF1FFFu;

constexpr uint32_t kShVectorGeneral   = 0x100u;
constexpr uint32_t kShVectorTlbMiss   = 0x400u;
constexpr uint32_t kShVectorInterrupt = 0x600u;

constexpr uint32_t kShSleepNone      = 0u;
constexpr uint32_t kShSleepRequested = 1u;
constexpr uint32_t kShSleepMode      = 2u;
constexpr uint32_t kShSleepStandby   = 3u;

constexpr bool ShSrAcceptsInterrupt(uint32_t sr, uint32_t level) {
    return (sr & kShSrBl) == 0u && level > ((sr & kShSrImask) >> kShSrImaskShift);
}

struct ShCpuState {
    uint32_t gpr[kShNumGpr];
    uint32_t bank[kShNumBank];
    uint32_t sr;
    uint32_t gbr;
    uint32_t vbr;
    uint32_t ssr;
    uint32_t spc;
    uint32_t mach;
    uint32_t macl;
    uint32_t pr;
    uint32_t pc;
    uint32_t tra;
    uint32_t expevt;
    uint32_t intevt;
    uint32_t rs;
    uint32_t re;
    uint32_t mod;
    uint32_t dsr;
    uint32_t a0;
    uint32_t a0g;
    uint32_t a1;
    uint32_t a1g;
    uint32_t m0;
    uint32_t m1;
    uint32_t x0;
    uint32_t x1;
    uint32_t y0;
    uint32_t y1;
    uint32_t guest_cycle_counter;
    uint32_t guest_cycle_deadline;
    uint32_t guest_cycle_hi;
    uint32_t guest_cycle_folded;
    uint32_t chain_exit_request;
    uint32_t ldtlb_shadow;
    uint32_t rte_slot;
    uint32_t sleeping;
    uint32_t deep_sleep;
    uint32_t mmio_access;
};

}
