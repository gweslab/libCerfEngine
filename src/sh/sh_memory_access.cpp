#include "sh_memory_access.h"

#include <cerfengine/byte_order.h>
#include <cerfengine/clock_host.h>
#include <cerfengine/cycle_deadline.h>
#include <cerfengine/physical_map.h>
#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_cpu_state.h>
#include "../host_platform.h"
#include "../mmio_bus.h"
#include "sh_address_space.h"
#include "sh_translation_cache.h"

namespace cerfengine {

CERFENGINE_REGISTER(ShMemoryAccess);

template <ShMemoryMode kMode>
constexpr ShMemoryHelpers ShMemoryAccess::MakeHelpers() {
    return ShMemoryHelpers{
        {&LoadHelper<1u, MmioWidth::kByte, kMode>, &LoadHelper<2u, MmioWidth::kHalf, kMode>,
         &LoadHelper<4u, MmioWidth::kWord, kMode>},
        {&StoreHelper<1u, MmioWidth::kByte, kMode>, &StoreHelper<2u, MmioWidth::kHalf, kMode>,
         &StoreHelper<4u, MmioWidth::kWord, kMode>},
    };
}

const ShMemoryHelpers ShMemoryAccess::kHelpers[kShMemoryModes] = {
    MakeHelpers<ShMemoryMode::kPrivileged>(),
    MakeHelpers<ShMemoryMode::kUser>(),
    MakeHelpers<ShMemoryMode::kRestored>(),
};

void ShMemoryAccess::OnReady() {
    cpu_state_ = engine_.Get<ShCpu>().State();
    map_       = &engine_.Get<PhysicalMap>();
    bus_       = &engine_.Get<MmioBus>();
    mmu_       = &engine_.Get<ShMmu>();
    cache_     = &engine_.Get<ShTranslationCache>();
    engine_.AttachClock(clock_);
}

void ShMemoryAccess::DeliverDueClockEvents() {
    if (CycleDeadlineReached(cpu_state_->guest_cycle_counter, cpu_state_->guest_cycle_deadline)) {
        host::ClockDispatch(clock_);
    }
}

const char* ShMemoryAccess::AccessName(ShAccess access) { return access == ShAccess::kWrite ? "store" : "load"; }

void ShMemoryAccess::RequireAligned(uint32_t va, uint32_t bytes, ShAccess access) {
    if ((va & (bytes - 1u)) == 0u) return;
    mmu_->RaiseAddressError(va, access, cpu_state_->pc);
    platform::RaiseUnwind(platform::kGuestUnwindCode);
}

template <ShMemoryMode kMode>
void ShMemoryAccess::RequireAccessible(uint32_t va, ShAccess access) {
    if constexpr (kMode != ShMemoryMode::kPrivileged) {
        if (!ShAddressSpace::PrivilegedOnly(va)) return;
        if constexpr (kMode == ShMemoryMode::kRestored) {
            if ((cpu_state_->sr & kShSrMd) != 0u) return;
        }
        mmu_->RaiseAddressError(va, access, cpu_state_->pc);
        platform::RaiseUnwind(platform::kGuestUnwindCode);
    }
}

uint32_t ShMemoryAccess::TranslateLoad(uint32_t va, uint32_t bytes) {
    RequireAligned(va, bytes, ShAccess::kRead);
    if (ShAddressSpace::DirectlyMapped(va)) return ShAddressSpace::Pa(va);
    return TranslateMapped(va, bytes, ShAccess::kRead);
}

bool ShMemoryAccess::TranslateStore(uint32_t va, uint32_t bytes, uint32_t value, uint32_t* pa) {
    RequireAligned(va, bytes, ShAccess::kWrite);
    if (ShAddressSpace::DirectlyMapped(va)) {
        *pa = ShAddressSpace::Pa(va);
        return true;
    }
    if (bytes == 4u && ShAddressSpace::CacheAddressArray(va)) {
        WriteCacheAddressArray(va, value);
        return false;
    }
    *pa = TranslateMapped(va, bytes, ShAccess::kWrite);
    return true;
}

uint32_t ShMemoryAccess::TranslateMapped(uint32_t va, uint32_t bytes, ShAccess access) {
    uint32_t pa = ShAddressSpace::Pa(va);
    if (mmu_->Translates(va)) {
        if (cpu_state_->ldtlb_shadow != 0u) {
            engine_.Halt(HaltReason::kUnimplemented,
                         "ShMemoryAccess: %u-byte %s at VA 0x%08X within two instructions after LDTLB, PC 0x%08X",
                         bytes, AccessName(access), va, cpu_state_->pc);
        }
        ShTranslation t{};
        if (!mmu_->Translate(va, access, cpu_state_->pc, &t)) platform::RaiseUnwind(platform::kGuestUnwindCode);
        pa = t.pa;
    } else if (!ShAddressSpace::Paged(va)) {
        engine_.Halt(HaltReason::kUnimplemented,
                     "ShMemoryAccess: %u-byte %s at VA 0x%08X outside P0-P3 and the P4 on-chip I/O space, PC 0x%08X",
                     bytes, AccessName(access), va, cpu_state_->pc);
    }
    if (pa >= ShAddressSpace::kArea7PaBase) {
        engine_.Halt(HaltReason::kUnimplemented,
                     "ShMemoryAccess: %u-byte %s at VA 0x%08X reaches the reserved area 7 PA 0x%08X, PC 0x%08X", bytes,
                     AccessName(access), va, pa, cpu_state_->pc);
    }
    return pa;
}

void ShMemoryAccess::WriteCacheAddressArray(uint32_t va, uint32_t value) {
    if ((value & (ShAddressSpace::kCacheEntryValidDirty | ShAddressSpace::kCacheTagShadow)) != 0u) {
        engine_.Halt(HaltReason::kUnimplemented,
                     "ShMemoryAccess: cache address array write of 0x%08X to VA 0x%08X sets V, U or tag bits 31-29, "
                     "PC 0x%08X",
                     value, va, cpu_state_->pc);
    }
    if ((va & ShAddressSpace::kCacheAssociative) != 0u && mmu_->AddressTranslation()) {
        engine_.Halt(HaltReason::kUnimplemented,
                     "ShMemoryAccess: associative cache address array write of 0x%08X to VA 0x%08X with MMUCR.AT set, "
                     "PC 0x%08X",
                     value, va, cpu_state_->pc);
    }
}

void ShMemoryAccess::EnterMmio(uint32_t va, uint32_t bytes, const char* access) {
    if (ShAddressSpace::ReservedArea7(va)) {
        engine_.Halt(HaltReason::kUnimplemented,
                     "ShMemoryAccess: %u-byte %s at VA 0x%08X reaches the reserved area 7 through P1/P2, PC 0x%08X",
                     bytes, access, va, cpu_state_->pc);
    }
    DeliverDueClockEvents();
}

template <uint32_t kBytes, MmioWidth kWidth, ShMemoryMode kMode>
uint32_t CERFENGINE_FASTCALL ShMemoryAccess::LoadHelper(uint32_t va, ShMemoryAccess* mem) {
    mem->RequireAccessible<kMode>(va, ShAccess::kRead);
    const uint32_t pa = mem->TranslateLoad(va, kBytes);
    if (const uint8_t* host = mem->map_->Read(pa)) {
        if constexpr (kBytes == 4u) return le::U32(host);
        if constexpr (kBytes == 2u) return le::U16(host);
        if constexpr (kBytes == 1u) return host[0];
    }
    mem->cpu_state_->mmio_access = 1u;
    mem->EnterMmio(va, kBytes, "load");
    const MmioRoute* route = mem->bus_->Route(pa);
    if (route == nullptr) {
        mem->engine_.Halt(HaltReason::kUnmappedPhysical, "ShMemoryAccess: unmapped %u-byte load VA 0x%08X PA 0x%08X PC 0x%08X",
                          kBytes, va, pa, mem->cpu_state_->pc);
    }
    const uint32_t loaded        = route->Read(pa, kWidth);
    mem->cpu_state_->mmio_access = 0u;
    return loaded;
}

template <uint32_t kBytes, MmioWidth kWidth, ShMemoryMode kMode>
void CERFENGINE_FASTCALL ShMemoryAccess::StoreHelper(uint32_t va, uint32_t value, ShMemoryAccess* mem) {
    mem->RequireAccessible<kMode>(va, ShAccess::kWrite);
    uint32_t pa = 0u;
    if (!mem->TranslateStore(va, kBytes, value, &pa)) return;
    if (uint8_t* host = mem->map_->Write(pa)) {
        mem->cache_->InvalidateOnRamWrite(host, kBytes);
        if constexpr (kBytes == 4u) le::Put32(host, value);
        if constexpr (kBytes == 2u) le::Put16(host, static_cast<uint16_t>(value));
        if constexpr (kBytes == 1u) host[0] = static_cast<uint8_t>(value);
        return;
    }
    mem->cpu_state_->mmio_access = 1u;
    mem->EnterMmio(va, kBytes, "store");
    const MmioRoute* route = mem->bus_->Route(pa);
    if (route == nullptr) {
        mem->engine_.Halt(HaltReason::kUnmappedPhysical,
                          "ShMemoryAccess: unmapped %u-byte store of 0x%08X to VA 0x%08X PA 0x%08X PC 0x%08X",
                          kBytes, value, va, pa, mem->cpu_state_->pc);
    }
    if constexpr (kBytes == 4u) route->Write(pa, value, kWidth);
    if constexpr (kBytes == 2u) route->Write(pa, static_cast<uint16_t>(value), kWidth);
    if constexpr (kBytes == 1u) route->Write(pa, static_cast<uint8_t>(value), kWidth);
    mem->cpu_state_->mmio_access = 0u;
}

void ShMemoryAccess::EndMmioAccess() { cpu_state_->mmio_access = 0u; }

}
