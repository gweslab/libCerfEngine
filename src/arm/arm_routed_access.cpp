#include "arm_routed_access.h"

#include <cerfengine/byte_order.h>
#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_translator.h>
#include <cerfengine/arm/cpu_state.h>
#include "../mmio_bus.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmRoutedAccess);

bool ArmRoutedAccess::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void ArmRoutedAccess::OnReady() {
    mmu_        = &engine_.Get<ArmMmu>();
    translator_ = &engine_.Get<ArmTranslator>();
    bus_        = &engine_.Get<MmioBus>();
    cpu_state_  = engine_.Get<ArmCpu>().State();
    engine_.AttachClock(clock_);
}

void ArmRoutedAccess::DeliverDueClockEvents() {
    if (ArmCycleDeadlineReached(*cpu_state_)) host::ClockDispatch(clock_);
}

void ArmRoutedAccess::HaltUnalignedRouted(uint32_t guest_pc, uint32_t va,
                                          uint32_t bytes, uint32_t pa,
                                          const char* kind) {
    engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                 "ArmRoutedAccess: guest PC 0x%08X routes an unaligned %u-byte "
                 "%s at VA 0x%08X to peripheral PA 0x%08X",
                 guest_pc, bytes, kind, va, pa);
}

void ArmRoutedAccess::PanicRoutedWidth(uint32_t guest_pc, uint32_t va,
                                       uint32_t bytes, uint32_t pa,
                                       const char* kind) {
    engine_.Panic("ArmRoutedAccess: guest PC 0x%08X routes a %u-byte %s at "
                  "VA 0x%08X to peripheral PA 0x%08X with no modeled width",
                  guest_pc, bytes, kind, va, pa);
}

template <MmioWidth kWidth>
uint32_t ArmRoutedAccess::ReadRouted(uint32_t pa) {
    DeliverDueClockEvents();
    return bus_->Read(pa, kWidth);
}

template <MmioWidth kWidth>
void ArmRoutedAccess::WriteRouted(uint32_t pa, uint32_t value) {
    DeliverDueClockEvents();
    bus_->Write(pa, value, kWidth);
}

uint32_t ArmRoutedAccess::DispatchRead(uint32_t pa, uint32_t bytes,
                                       uint32_t guest_pc, uint32_t va) {
    switch (bytes) {
    case 1u: return ReadRouted<MmioWidth::kByte>(pa);
    case 2u: return ReadRouted<MmioWidth::kHalf>(pa);
    case 4u: return ReadRouted<MmioWidth::kWord>(pa);
    }
    PanicRoutedWidth(guest_pc, va, bytes, pa, "load");
}

void ArmRoutedAccess::DispatchWrite(uint32_t pa, uint32_t bytes, uint32_t value,
                                    uint32_t guest_pc, uint32_t va) {
    switch (bytes) {
    case 1u: WriteRouted<MmioWidth::kByte>(pa, value); return;
    case 2u: WriteRouted<MmioWidth::kHalf>(pa, value); return;
    case 4u: WriteRouted<MmioWidth::kWord>(pa, value); return;
    }
    PanicRoutedWidth(guest_pc, va, bytes, pa, "store");
}

template uint32_t ArmRoutedAccess::ReadRouted<MmioWidth::kByte>(uint32_t);
template uint32_t ArmRoutedAccess::ReadRouted<MmioWidth::kHalf>(uint32_t);
template uint32_t ArmRoutedAccess::ReadRouted<MmioWidth::kWord>(uint32_t);
template void ArmRoutedAccess::WriteRouted<MmioWidth::kByte>(uint32_t, uint32_t);
template void ArmRoutedAccess::WriteRouted<MmioWidth::kHalf>(uint32_t, uint32_t);
template void ArmRoutedAccess::WriteRouted<MmioWidth::kWord>(uint32_t, uint32_t);

bool ArmRoutedAccess::Load(uint32_t guest_pc, uint32_t va, uint32_t bytes, uint32_t* out,
                           bool unpriv) {
    if ((va & (bytes - 1u)) != 0u) {
        uint8_t b[4] = {};
        if (mmu_->AccessPaged(va, b, bytes, true, unpriv)) {
            *out = le::U32(b);
            return true;
        }
        const uint32_t split_pa = mmu_->io_pending_address();
        if (!mmu_->io_pending()) {
            return false;
        }
        HaltUnalignedRouted(guest_pc, va, bytes, split_pa, "load");
    }

    uint8_t* host = unpriv ? translator_->TranslateUserRead(va)
                           : translator_->TranslateRead(va);
    if (host != nullptr) {
        *out = static_cast<uint32_t>(le::UN(host, bytes));
        return true;
    }
    const uint32_t pa = mmu_->io_pending_address();
    if (!mmu_->io_pending()) {
        return false;
    }
    *out = DispatchRead(pa, bytes, guest_pc, va);
    return true;
}

/* DDI 0406C.c A8.8.332 VLDM Operation, p. A8-923; A8.8.333 VLDR Operation,
   p. A8-925. */
bool ArmRoutedAccess::WideAccess(uint32_t guest_pc, uint32_t va, uint32_t bytes,
                                 uint8_t* buf, bool is_load) {
    uint32_t chunk = 0;
    switch (bytes) {
    case 1u:
    case 2u:
    case 4u: chunk = bytes; break;
    case 8u: chunk = 4u;    break;
    default: PanicRoutedWidth(guest_pc, va, bytes, mmu_->io_pending_address(),
                              is_load ? "load" : "store");
    }

    for (uint32_t done = 0; done < bytes; done += chunk) {
        uint32_t word = 0;
        if (is_load) {
            if (!Load(guest_pc, va + done, chunk, &word, false)) {
                return false;
            }
            le::PutN(buf + done, word, chunk);
        } else {
            word = static_cast<uint32_t>(le::UN(buf + done, chunk));
            if (!Store(guest_pc, va + done, chunk, word, false)) {
                return false;
            }
        }
    }
    mmu_->ClearIoPending();
    return true;
}

bool ArmRoutedAccess::Store(uint32_t guest_pc, uint32_t va, uint32_t bytes, uint32_t value,
                            bool unpriv) {
    if ((va & (bytes - 1u)) != 0u) {
        uint8_t b[4];
        le::Put32(b, value);
        if (mmu_->AccessPaged(va, b, bytes, false, unpriv)) {
            return true;
        }
        const uint32_t split_pa = mmu_->io_pending_address();
        if (!mmu_->io_pending()) {
            return false;
        }
        HaltUnalignedRouted(guest_pc, va, bytes, split_pa, "store");
    }

    uint8_t* host = unpriv ? translator_->TranslateUserWrite(va)
                           : translator_->TranslateWrite(va);
    if (host != nullptr) {
        le::PutN(host, value, bytes);
        return true;
    }
    const uint32_t pa = mmu_->io_pending_address();
    if (!mmu_->io_pending()) {
        return false;
    }
    DispatchWrite(pa, bytes, value, guest_pc, va);
    return true;
}

}
