#include "mips_memory_access.h"
#include <cerfengine/calling_convention.h>

#include <cerfengine/byte_order.h>
#include <cerfengine/cycle_deadline.h>
#include <cerfengine/engine.h>
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mips/mips_exception_delivery.h>
#include <cerfengine/mips/mips_interrupt_channel.h>
#include <cerfengine/mips/mips_mmu.h>
#include <cerfengine/physical_map.h>
#include "../mmio_bus.h"
#include "mips_translation_cache.h"

namespace cerfengine {

CERFENGINE_REGISTER(MipsMemoryAccess);

void MipsMemoryAccess::OnReady() {
    cpu_state_  = engine_.Get<MipsCpu>().State();
    mmu_        = &engine_.Get<MipsMmu>();
    map_        = &engine_.Get<PhysicalMap>();
    bus_        = &engine_.Get<MmioBus>();
    bus_host_   = engine_.GetSetup().mmio.context;
    exceptions_ = &engine_.Get<MipsExceptionDelivery>();
    channel_    = &engine_.Get<MipsInterruptChannel>();
    cache_      = &engine_.Get<MipsTranslationCache>();
    engine_.AttachClock(clock_);
}

void MipsMemoryAccess::DeliverDueClockEvents() {
    if (CycleDeadlineReached(cpu_state_->guest_cycle_counter,
                             cpu_state_->guest_cycle_deadline)) {
        host::ClockDispatch(clock_);
    }
}

void MipsMemoryAccess::DeliverDueClockEventsInBlock() {
    if (CycleDeadlineReached(cpu_state_->guest_cycle_counter,
                             cpu_state_->guest_cycle_deadline)) {
        host::ClockDispatch(clock_);
        channel_->RequestChainExit();
    }
}

void MipsMemoryAccess::HaltUnmappedRead(uint32_t va, uint32_t pa, const char* who) {
    engine_.Halt(cerfengine::HaltReason::kUnmappedPhysical,
                 "%s: unmapped MMIO read va=0x%08X pa=0x%08X pc=0x%08X", who, va, pa,
                 cpu_state_->pc);
}

void MipsMemoryAccess::HaltUnmappedWrite(uint32_t va, uint32_t pa, uint64_t value,
                                         const char* who) {
    engine_.Halt(cerfengine::HaltReason::kUnmappedPhysical,
                 "%s: unmapped MMIO write va=0x%08X pa=0x%08X val=0x%016llX pc=0x%08X", who,
                 va, pa, static_cast<unsigned long long>(value), cpu_state_->pc);
}

template <MmioWidth kWidth>
uint64_t MipsMemoryAccess::MmioRead(uint32_t va, uint32_t pa, const char* who) {
    if (bus_->Route(pa) == nullptr) HaltUnmappedRead(va, pa, who);
    DeliverDueClockEventsInBlock();
    if constexpr (kWidth == MmioWidth::kDword) {
        return host::MmioReadDword(bus_host_, pa);
    } else {
        const MmioRoute* route = bus_->Route(pa);
        if (route == nullptr) HaltUnmappedRead(va, pa, who);
        return route->Read(pa, kWidth);
    }
}

void MipsMemoryAccess::LeaveBlockIfCpuStopped() {
    if (cpu_state_->idle_wait != MipsIdle::kNone) exceptions_->LeaveBlock();
}

template <MmioWidth kWidth>
void MipsMemoryAccess::MmioWrite(uint32_t va, uint32_t pa, uint64_t value, const char* who) {
    if (bus_->Route(pa) == nullptr) HaltUnmappedWrite(va, pa, value, who);
    DeliverDueClockEventsInBlock();
    if constexpr (kWidth == MmioWidth::kDword) {
        host::MmioWriteDword(bus_host_, pa, value);
    } else {
        const MmioRoute* route = bus_->Route(pa);
        if (route == nullptr) HaltUnmappedWrite(va, pa, value, who);
        route->Write(pa, static_cast<uint32_t>(value), kWidth);
    }
    LeaveBlockIfCpuStopped();
}

template <uint32_t kPrivilege>
void CERFENGINE_FASTCALL MipsMemoryAccess::StoreWordHelper(uint32_t va, uint32_t value, MipsMemoryAccess* mem) {
    if (va & 3u) {
        mem->exceptions_->RaiseAddressError(va, MipsAccess::kWrite);
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        mem->mmu_->Translate<kPrivilege>(mem->cpu_state_, va, MipsAccess::kWrite, &pa);
    if (r != MipsTlbResult::kMatch) {
        mem->exceptions_->RaiseTlbException(va, MipsAccess::kWrite, r);
    }
    if (uint8_t* host = mem->map_->Write(pa)) {
        mem->cache_->InvalidateOnRamWrite(host, 4u);
        le::Put32(host, value);
        return;
    }
    mem->MmioWrite<MmioWidth::kWord>(va, pa, value, "MipsMemoryAccess::StoreWordHelper");
}

template <uint32_t kPrivilege>
void CERFENGINE_FASTCALL MipsMemoryAccess::StoreHalfHelper(uint32_t va, uint32_t value, MipsMemoryAccess* mem) {
    if (va & 1u) {
        mem->exceptions_->RaiseAddressError(va, MipsAccess::kWrite);
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        mem->mmu_->Translate<kPrivilege>(mem->cpu_state_, va, MipsAccess::kWrite, &pa);
    if (r != MipsTlbResult::kMatch) {
        mem->exceptions_->RaiseTlbException(va, MipsAccess::kWrite, r);
    }
    if (uint8_t* host = mem->map_->Write(pa)) {
        mem->cache_->InvalidateOnRamWrite(host, 2u);
        le::Put16(host, static_cast<uint16_t>(value));
        return;
    }
    mem->MmioWrite<MmioWidth::kHalf>(va, pa, static_cast<uint16_t>(value),
                                     "MipsMemoryAccess::StoreHalfHelper");
}

template <uint32_t kPrivilege>
void CERFENGINE_FASTCALL MipsMemoryAccess::StoreByteHelper(uint32_t va, uint32_t value, MipsMemoryAccess* mem) {
    StoreByteXlate<kPrivilege>(mem, va, static_cast<uint8_t>(value),
                               "MipsMemoryAccess::StoreByteHelper");
}

template <uint32_t kPrivilege>
uint32_t CERFENGINE_FASTCALL MipsMemoryAccess::LoadWordHelper(uint32_t va, MipsMemoryAccess* mem) {
    if (va & 3u) {
        mem->exceptions_->RaiseAddressError(va, MipsAccess::kRead);
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        mem->mmu_->Translate<kPrivilege>(mem->cpu_state_, va, MipsAccess::kRead, &pa);
    if (r != MipsTlbResult::kMatch) {
        mem->exceptions_->RaiseTlbException(va, MipsAccess::kRead, r);
    }
    if (const uint8_t* host = mem->map_->Read(pa)) {
        return le::U32(host);
    }
    return static_cast<uint32_t>(
        mem->MmioRead<MmioWidth::kWord>(va, pa, "MipsMemoryAccess::LoadWordHelper"));
}

template <uint32_t kPrivilege>
uint32_t CERFENGINE_FASTCALL MipsMemoryAccess::LoadByteHelper(uint32_t va, MipsMemoryAccess* mem) {
    uint32_t pa = 0;
    const MipsTlbResult r =
        mem->mmu_->Translate<kPrivilege>(mem->cpu_state_, va, MipsAccess::kRead, &pa);
    if (r != MipsTlbResult::kMatch) {
        mem->exceptions_->RaiseTlbException(va, MipsAccess::kRead, r);
    }
    if (const uint8_t* host = mem->map_->Read(pa)) {
        return *host;
    }
    return static_cast<uint8_t>(
        mem->MmioRead<MmioWidth::kByte>(va, pa, "MipsMemoryAccess::LoadByteHelper"));
}

template <uint32_t kPrivilege>
uint32_t CERFENGINE_FASTCALL MipsMemoryAccess::LoadHalfHelper(uint32_t va, MipsMemoryAccess* mem) {
    if (va & 1u) {
        mem->exceptions_->RaiseAddressError(va, MipsAccess::kRead);
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        mem->mmu_->Translate<kPrivilege>(mem->cpu_state_, va, MipsAccess::kRead, &pa);
    if (r != MipsTlbResult::kMatch) {
        mem->exceptions_->RaiseTlbException(va, MipsAccess::kRead, r);
    }
    if (const uint8_t* host = mem->map_->Read(pa)) {
        return le::U16(host);
    }
    return static_cast<uint16_t>(
        mem->MmioRead<MmioWidth::kHalf>(va, pa, "MipsMemoryAccess::LoadHalfHelper"));
}

template <uint32_t kPrivilege>
uint64_t CERFENGINE_FASTCALL MipsMemoryAccess::LoadDwordHelper(uint32_t va, MipsMemoryAccess* mem) {
    if (va & 7u) {
        mem->exceptions_->RaiseAddressError(va, MipsAccess::kRead);
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        mem->mmu_->Translate<kPrivilege>(mem->cpu_state_, va, MipsAccess::kRead, &pa);
    if (r != MipsTlbResult::kMatch) {
        mem->exceptions_->RaiseTlbException(va, MipsAccess::kRead, r);
    }
    if (const uint8_t* host = mem->map_->Read(pa)) {
        return le::U64(host);
    }
    return mem->MmioRead<MmioWidth::kDword>(va, pa, "MipsMemoryAccess::LoadDwordHelper");
}

template <uint32_t kPrivilege>
void MipsMemoryAccess::StoreByteXlate(MipsMemoryAccess* mem, uint32_t va, uint8_t value,
                            const char* who) {
    uint32_t pa = 0;
    const MipsTlbResult r =
        mem->mmu_->Translate<kPrivilege>(mem->cpu_state_, va, MipsAccess::kWrite, &pa);
    if (r != MipsTlbResult::kMatch) {
        mem->exceptions_->RaiseTlbException(va, MipsAccess::kWrite, r);
    }
    uint8_t* host = mem->map_->Write(pa);
    if (!host) {
        mem->MmioWrite<MmioWidth::kByte>(va, pa, value, who);
        return;
    }
    mem->cache_->InvalidateOnRamWrite(host, 1u);
    *host = value;
}

template <uint32_t kPrivilege>
void CERFENGINE_FASTCALL MipsMemoryAccess::SdrHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem) {
    /* Little-endian SDR: store the low ((va&7)^7)+1 bytes of gpr[rt], byte n ->
       mem[va+n] = rt>>(n*8). (QEMU MIPS ldst_helper.c helper_sdr + get_lmask.) */
    const uint64_t val   = mem->cpu_state_->gpr[rt];
    const uint32_t count = ((va & 7u) ^ 7u) + 1u;
    for (uint32_t n = 0; n < count; ++n) {
        StoreByteXlate<kPrivilege>(mem, va + n, static_cast<uint8_t>(val >> (n * 8u)),
                                   "MipsMemoryAccess::SdrHelper");
    }
}

template <uint32_t kPrivilege>
void CERFENGINE_FASTCALL MipsMemoryAccess::SdlHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem) {
    /* Little-endian SDL: store the high (va&7)+1 bytes of gpr[rt], byte n ->
       mem[va-n] = rt>>((7-n)*8). (QEMU MIPS ldst_helper.c helper_sdl + get_lmask.) */
    const uint64_t val   = mem->cpu_state_->gpr[rt];
    const uint32_t count = (va & 7u) + 1u;
    for (uint32_t n = 0; n < count; ++n) {
        StoreByteXlate<kPrivilege>(mem, va - n, static_cast<uint8_t>(val >> ((7u - n) * 8u)),
                                   "MipsMemoryAccess::SdlHelper");
    }
}

template <uint32_t kPrivilege>
void CERFENGINE_FASTCALL MipsMemoryAccess::LwlHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem) {
    /* LE: load the aligned word, shift it into the high bytes by ((va&3)^3)*8,
       OR in the kept low bytes of rt, sign-extend the 32-bit result. (QEMU
       translate.c gen_lxl + ext32s, OPC_LWL.) */
    const uint32_t shift  = ((va & 3u) ^ 3u) * 8u;
    const uint32_t w      = LoadWordHelper<kPrivilege>(va & ~3u, mem);
    const uint32_t old    = static_cast<uint32_t>(mem->cpu_state_->gpr[rt]);
    const uint32_t mask   = 0xFFFFFFFFu << shift;
    const uint32_t merged = (w << shift) | (old & ~mask);
    if (rt != 0) {
        mem->cpu_state_->gpr[rt] =
            static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(merged)));
    }
}

template <uint32_t kPrivilege>
void CERFENGINE_FASTCALL MipsMemoryAccess::LwrHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem) {
    const uint32_t s         = (va & 3u) * 8u;
    const uint32_t w         = LoadWordHelper<kPrivilege>(va & ~3u, mem);
    const uint32_t loaded    = w >> s;
    const uint32_t keep_mask = 0xFFFFFFFEu << (s ^ 31u);
    const uint32_t old       = static_cast<uint32_t>(mem->cpu_state_->gpr[rt]);
    const uint32_t merged    = loaded | (old & keep_mask);
    if (rt != 0) {
        mem->cpu_state_->gpr[rt] =
            static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(merged)));
    }
}

template <uint32_t kPrivilege>
void CERFENGINE_FASTCALL MipsMemoryAccess::LdlHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem) {
    /* LE: load the aligned doubleword, shift it left by shift=((va&7)^7)*8 so the
       left bytes land in rt's high end, keep rt's low bytes via ~((~0)<<shift), OR.
       Full 64-bit (no sext). (QEMU translate.c gen_lxl, OPC_LDL, MO_UQ.) */
    const uint32_t shift  = ((va & 7u) ^ 7u) * 8u;
    const uint64_t w      = LoadDwordHelper<kPrivilege>(va & ~7u, mem);
    const uint64_t old    = mem->cpu_state_->gpr[rt];
    const uint64_t mask   = (~0ull) << shift;
    const uint64_t merged = (w << shift) | (old & ~mask);
    if (rt != 0) {
        mem->cpu_state_->gpr[rt] = merged;
    }
}

template <uint32_t kPrivilege>
void CERFENGINE_FASTCALL MipsMemoryAccess::LdrHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem) {
    /* LE: load the aligned dword, shift it right by s=(va&7)*8 so the right bytes
       land in rt's low end, keep rt's high bytes via (~1)<<(s^63), OR. Full 64-bit
       (no sext). (QEMU translate.c gen_lxr, OPC_LDR, MO_UQ.) */
    const uint32_t s         = (va & 7u) * 8u;
    const uint64_t w         = LoadDwordHelper<kPrivilege>(va & ~7u, mem);
    const uint64_t loaded    = w >> s;
    const uint64_t keep_mask = (~1ull) << (s ^ 63u);
    const uint64_t old       = mem->cpu_state_->gpr[rt];
    const uint64_t merged    = loaded | (old & keep_mask);
    if (rt != 0) {
        mem->cpu_state_->gpr[rt] = merged;
    }
}

template <uint32_t kPrivilege>
void CERFENGINE_FASTCALL MipsMemoryAccess::SwrHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem) {
    /* Little-endian SWR: store the low ((va&3)^3)+1 bytes of gpr[rt][31:0],
       byte n -> mem[va+n] = rt>>(n*8). (QEMU MIPS ldst_helper.c helper_swr.) */
    const uint32_t val   = static_cast<uint32_t>(mem->cpu_state_->gpr[rt]);
    const uint32_t count = ((va & 3u) ^ 3u) + 1u;
    for (uint32_t n = 0; n < count; ++n) {
        StoreByteXlate<kPrivilege>(mem, va + n, static_cast<uint8_t>(val >> (n * 8u)),
                                   "MipsMemoryAccess::SwrHelper");
    }
}

template <uint32_t kPrivilege>
void CERFENGINE_FASTCALL MipsMemoryAccess::SwlHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem) {
    /* Little-endian SWL: store the high (va&3)+1 bytes of gpr[rt][31:0], byte n
       -> mem[va-n] = rt>>((3-n)*8). (QEMU MIPS ldst_helper.c helper_swl.) */
    const uint32_t val   = static_cast<uint32_t>(mem->cpu_state_->gpr[rt]);
    const uint32_t count = (va & 3u) + 1u;
    for (uint32_t n = 0; n < count; ++n) {
        StoreByteXlate<kPrivilege>(mem, va - n, static_cast<uint8_t>(val >> ((3u - n) * 8u)),
                                   "MipsMemoryAccess::SwlHelper");
    }
}

template <uint32_t kPrivilege>
void CERFENGINE_FASTCALL MipsMemoryAccess::StoreDwordHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem) {
    if (va & 7u) {
        mem->exceptions_->RaiseAddressError(va, MipsAccess::kWrite);
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        mem->mmu_->Translate<kPrivilege>(mem->cpu_state_, va, MipsAccess::kWrite, &pa);
    if (r != MipsTlbResult::kMatch) {
        mem->exceptions_->RaiseTlbException(va, MipsAccess::kWrite, r);
    }
    if (uint8_t* host = mem->map_->Write(pa)) {
        mem->cache_->InvalidateOnRamWrite(host, 8u);
        le::Put64(host, mem->cpu_state_->gpr[rt]);
        return;
    }
    mem->MmioWrite<MmioWidth::kDword>(va, pa, mem->cpu_state_->gpr[rt],
                                      "MipsMemoryAccess::StoreDwordHelper");
}

template <uint32_t kPrivilege>
constexpr MipsMemoryHelpers MipsMemoryAccess::MakeHelpers() {
    return MipsMemoryHelpers{
        {&LoadByteHelper<kPrivilege>, &LoadHalfHelper<kPrivilege>, &LoadWordHelper<kPrivilege>},
        {&StoreByteHelper<kPrivilege>, &StoreHalfHelper<kPrivilege>,
         &StoreWordHelper<kPrivilege>},
        &LoadDwordHelper<kPrivilege>,
        &StoreDwordHelper<kPrivilege>,
        &LwlHelper<kPrivilege>,
        &LwrHelper<kPrivilege>,
        &LdlHelper<kPrivilege>,
        &LdrHelper<kPrivilege>,
        &SwlHelper<kPrivilege>,
        &SwrHelper<kPrivilege>,
        &SdlHelper<kPrivilege>,
        &SdrHelper<kPrivilege>,
    };
}

const MipsMemoryHelpers MipsMemoryAccess::kHelpers[kBlockPrivilegeCount] = {
    MakeHelpers<kBlockPrivileged>(),
    MakeHelpers<kBlockUser>(),
    MakeHelpers<kBlockSupervisor>(),
};

}
