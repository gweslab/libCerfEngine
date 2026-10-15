#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>
#include <cerfengine/mips/mips_system_host.h>
#include <cerfengine/mmio_bus_host.h>

namespace cerfengine {

struct MipsCpuState;

class MipsExceptionDelivery;
class MipsMmu;
class MipsTranslationCache;
class MmioBus;
class PhysicalMap;

class MipsMemoryAccess : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    static void CERFENGINE_FASTCALL StoreWordHelper(uint32_t va, uint32_t value,
                                                    MipsMemoryAccess* mem);
    static void CERFENGINE_FASTCALL StoreHalfHelper(uint32_t va, uint32_t value,
                                                    MipsMemoryAccess* mem);
    static void CERFENGINE_FASTCALL StoreByteHelper(uint32_t va, uint32_t value,
                                                    MipsMemoryAccess* mem);
    static void CERFENGINE_FASTCALL StoreDwordHelper(uint32_t va, uint32_t rt,
                                                     MipsMemoryAccess* mem);

    static uint32_t CERFENGINE_FASTCALL LoadWordHelper(uint32_t va, MipsMemoryAccess* mem);
    static uint32_t CERFENGINE_FASTCALL LoadByteHelper(uint32_t va, MipsMemoryAccess* mem);
    static uint32_t CERFENGINE_FASTCALL LoadHalfHelper(uint32_t va, MipsMemoryAccess* mem);
    static uint64_t CERFENGINE_FASTCALL LoadDwordHelper(uint32_t va, MipsMemoryAccess* mem);

    static void CERFENGINE_FASTCALL LwlHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);
    static void CERFENGINE_FASTCALL LwrHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);
    static void CERFENGINE_FASTCALL LdlHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);
    static void CERFENGINE_FASTCALL LdrHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);

    static void CERFENGINE_FASTCALL SwlHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);
    static void CERFENGINE_FASTCALL SwrHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);
    static void CERFENGINE_FASTCALL SdlHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);
    static void CERFENGINE_FASTCALL SdrHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);

    void DeliverDueClockEvents();

private:
    static void StoreByteXlate(MipsMemoryAccess* mem, uint32_t va, uint8_t value,
                               const char* who);

    void     LeaveBlockIfCpuStopped();
    template <MmioWidth kWidth>
    uint64_t MmioRead(uint32_t va, uint32_t pa, const char* who);
    template <MmioWidth kWidth>
    void     MmioWrite(uint32_t va, uint32_t pa, uint64_t value, const char* who);
    [[noreturn]] void HaltUnmappedRead(uint32_t va, uint32_t pa, const char* who);
    [[noreturn]] void HaltUnmappedWrite(uint32_t va, uint32_t pa, uint64_t value,
                                        const char* who);

    MipsCpuState*          cpu_state_  = nullptr;
    MipsMmu*               mmu_        = nullptr;
    PhysicalMap*           map_        = nullptr;
    MmioBus*               bus_        = nullptr;
    void*                  bus_host_   = nullptr;
    MipsExceptionDelivery* exceptions_ = nullptr;
    MipsTranslationCache*  cache_      = nullptr;
    ClockHost*             clock_      = nullptr;
};

}
