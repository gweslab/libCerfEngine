#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>
#include <cerfengine/mmio_bus_host.h>

namespace cerfengine {

class ArmMmu;
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
    static uint32_t CERFENGINE_CDECL IoLoadHelper(ArmRoutedAccess* self, uint32_t guest_pc,
                                                  uint32_t va);
    template <MmioWidth kWidth>
    static void CERFENGINE_CDECL IoStoreHelper(ArmRoutedAccess* self, uint32_t guest_pc,
                                               uint32_t va, uint32_t value);

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
