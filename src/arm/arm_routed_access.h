#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/engine.h>
#include <cerfengine/mmio_bus_host.h>

#include <cerfengine/force_inline.h>

namespace cerfengine {

class ArmTranslator;
class MmioBus;
struct ArmCpuState;

class ArmRoutedAccess : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Load(uint32_t guest_pc, uint32_t va, uint32_t bytes, uint32_t* out, bool unpriv);
    bool Store(uint32_t guest_pc, uint32_t va, uint32_t bytes, uint32_t value, bool unpriv);

    bool WideAccess(uint32_t guest_pc, uint32_t va, uint32_t bytes, uint8_t* buf,
                    bool is_load);

    template <MmioWidth kWidth>
    CERFENGINE_FORCEINLINE static uint32_t CERFENGINE_CDECL IoLoadHelper(ArmRoutedAccess* self,
                                                                         uint32_t guest_pc,
                                                                         uint32_t va) {
        constexpr uint32_t kBytes = static_cast<uint32_t>(kWidth);
        const uint32_t pa = self->mmu_->io_pending_address();
        self->mmu_->ClearIoPending();
        if constexpr (kBytes > 1u) {
            if ((pa & (kBytes - 1u)) != 0u) {
                self->HaltUnalignedRouted(guest_pc, va, kBytes, pa, "load");
            }
        }
        return self->ReadRouted<kWidth>(pa);
    }
    template <MmioWidth kWidth>
    CERFENGINE_FORCEINLINE static void CERFENGINE_CDECL IoStoreHelper(ArmRoutedAccess* self,
                                                                      uint32_t guest_pc,
                                                                      uint32_t va, uint32_t value) {
        constexpr uint32_t kBytes = static_cast<uint32_t>(kWidth);
        const uint32_t pa = self->mmu_->io_pending_address();
        self->mmu_->ClearIoPending();
        if constexpr (kBytes > 1u) {
            if ((pa & (kBytes - 1u)) != 0u) {
                self->HaltUnalignedRouted(guest_pc, va, kBytes, pa, "store");
            }
        }
        self->WriteRouted<kWidth>(pa, value);
    }

private:
    uint32_t DispatchRead(uint32_t pa, uint32_t bytes, uint32_t guest_pc,
                          uint32_t va);
    void     DispatchWrite(uint32_t pa, uint32_t bytes, uint32_t value,
                           uint32_t guest_pc, uint32_t va);
    template <MmioWidth kWidth>
    uint32_t ReadRouted(uint32_t pa);
    template <MmioWidth kWidth>
    void     WriteRouted(uint32_t pa, uint32_t value);
    void     DeliverDueClockEvents();

    [[noreturn]] void HaltUnalignedRouted(uint32_t guest_pc, uint32_t va,
                                          uint32_t bytes, uint32_t pa,
                                          const char* kind);
    [[noreturn]] void PanicRoutedWidth(uint32_t guest_pc, uint32_t va,
                                       uint32_t bytes, uint32_t pa,
                                       const char* kind);

    ArmMmu*        mmu_        = nullptr;
    ArmTranslator* translator_ = nullptr;
    MmioBus*       bus_        = nullptr;
    ArmCpuState*   cpu_state_  = nullptr;
    ClockHost*     clock_      = nullptr;
};

}
