#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>
#include <cerfengine/isa_block_space.h>
#include <cerfengine/mips/mips_system_host.h>
#include <cerfengine/mmio_bus_host.h>

namespace cerfengine {

struct MipsCpuState;

class MipsExceptionDelivery;
class MipsInterruptChannel;
class MipsMemoryAccess;
class MipsMmu;
class MipsTranslationCache;
class MmioBus;
class PhysicalMap;

struct MipsMemoryHelpers {
    using LoadFn  = uint32_t (CERFENGINE_FASTCALL*)(uint32_t va, MipsMemoryAccess* mem);
    using StoreFn = void (CERFENGINE_FASTCALL*)(uint32_t va, uint32_t value, MipsMemoryAccess* mem);
    using DwordFn = uint64_t (CERFENGINE_FASTCALL*)(uint32_t va, MipsMemoryAccess* mem);
    using IndexFn = void (CERFENGINE_FASTCALL*)(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);

    LoadFn  load[3];
    StoreFn store[3];
    DwordFn load_dword;
    IndexFn store_dword;
    IndexFn lwl;
    IndexFn lwr;
    IndexFn ldl;
    IndexFn ldr;
    IndexFn swl;
    IndexFn swr;
    IndexFn sdl;
    IndexFn sdr;
};

class MipsMemoryAccess : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    static const MipsMemoryHelpers& HelpersFor(uint32_t privilege) { return kHelpers[privilege]; }

    void DeliverDueClockEvents();

private:
    static const MipsMemoryHelpers kHelpers[kBlockPrivilegeCount];

    template <uint32_t kPrivilege>
    static void CERFENGINE_FASTCALL StoreWordHelper(uint32_t va, uint32_t value,
                                                    MipsMemoryAccess* mem);
    template <uint32_t kPrivilege>
    static void CERFENGINE_FASTCALL StoreHalfHelper(uint32_t va, uint32_t value,
                                                    MipsMemoryAccess* mem);
    template <uint32_t kPrivilege>
    static void CERFENGINE_FASTCALL StoreByteHelper(uint32_t va, uint32_t value,
                                                    MipsMemoryAccess* mem);
    template <uint32_t kPrivilege>
    static void CERFENGINE_FASTCALL StoreDwordHelper(uint32_t va, uint32_t rt,
                                                     MipsMemoryAccess* mem);

    template <uint32_t kPrivilege>
    static uint32_t CERFENGINE_FASTCALL LoadWordHelper(uint32_t va, MipsMemoryAccess* mem);
    template <uint32_t kPrivilege>
    static uint32_t CERFENGINE_FASTCALL LoadByteHelper(uint32_t va, MipsMemoryAccess* mem);
    template <uint32_t kPrivilege>
    static uint32_t CERFENGINE_FASTCALL LoadHalfHelper(uint32_t va, MipsMemoryAccess* mem);
    template <uint32_t kPrivilege>
    static uint64_t CERFENGINE_FASTCALL LoadDwordHelper(uint32_t va, MipsMemoryAccess* mem);

    template <uint32_t kPrivilege>
    static void CERFENGINE_FASTCALL LwlHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);
    template <uint32_t kPrivilege>
    static void CERFENGINE_FASTCALL LwrHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);
    template <uint32_t kPrivilege>
    static void CERFENGINE_FASTCALL LdlHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);
    template <uint32_t kPrivilege>
    static void CERFENGINE_FASTCALL LdrHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);

    template <uint32_t kPrivilege>
    static void CERFENGINE_FASTCALL SwlHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);
    template <uint32_t kPrivilege>
    static void CERFENGINE_FASTCALL SwrHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);
    template <uint32_t kPrivilege>
    static void CERFENGINE_FASTCALL SdlHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);
    template <uint32_t kPrivilege>
    static void CERFENGINE_FASTCALL SdrHelper(uint32_t va, uint32_t rt, MipsMemoryAccess* mem);

    template <uint32_t kPrivilege>
    static constexpr MipsMemoryHelpers MakeHelpers();

    template <uint32_t kPrivilege>
    static void StoreByteXlate(MipsMemoryAccess* mem, uint32_t va, uint8_t value,
                               const char* who);

    void     DeliverDueClockEventsInBlock();
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
    MipsInterruptChannel*  channel_    = nullptr;
    MipsTranslationCache*  cache_      = nullptr;
    ClockHost*             clock_      = nullptr;
};

}
