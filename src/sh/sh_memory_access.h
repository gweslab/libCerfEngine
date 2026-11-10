#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>
#include <cerfengine/mmio_bus_host.h>
#include <cerfengine/sh/sh_mmu.h>

namespace cerfengine {

class ClockHost;
class MmioBus;
class PhysicalMap;
class ShMemoryAccess;
class ShTranslationCache;
struct ShCpuState;

struct ShMemoryHelpers {
    using LoadFn  = uint32_t (CERFENGINE_FASTCALL*)(uint32_t va, ShMemoryAccess* mem);
    using StoreFn = void (CERFENGINE_FASTCALL*)(uint32_t va, uint32_t value, ShMemoryAccess* mem);

    LoadFn  load[3];
    StoreFn store[3];
};

enum class ShMemoryMode : uint8_t { kPrivileged, kUser, kRestored };

constexpr uint32_t kShMemoryModes = 3u;

class ShMemoryAccess : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kSh; }
    void OnReady() override;

    static const ShMemoryHelpers& HelpersFor(ShMemoryMode mode) { return kHelpers[static_cast<uint32_t>(mode)]; }

    void DeliverDueClockEvents();
    void EndMmioAccess();

private:
    static const ShMemoryHelpers kHelpers[kShMemoryModes];

    template <ShMemoryMode kMode>
    static constexpr ShMemoryHelpers MakeHelpers();

    template <uint32_t kBytes, MmioWidth kWidth, ShMemoryMode kMode>
    static uint32_t CERFENGINE_FASTCALL LoadHelper(uint32_t va, ShMemoryAccess* mem);

    template <uint32_t kBytes, MmioWidth kWidth, ShMemoryMode kMode>
    static void CERFENGINE_FASTCALL StoreHelper(uint32_t va, uint32_t value, ShMemoryAccess* mem);

    template <ShMemoryMode kMode>
    void RequireAccessible(uint32_t va, ShAccess access);

    static const char* AccessName(ShAccess access);

    void     RequireAligned(uint32_t va, uint32_t bytes, ShAccess access);
    uint32_t TranslateLoad(uint32_t va, uint32_t bytes);
    bool     TranslateStore(uint32_t va, uint32_t bytes, uint32_t value, uint32_t* pa);
    uint32_t TranslateMapped(uint32_t va, uint32_t bytes, ShAccess access);
    void     WriteCacheAddressArray(uint32_t va, uint32_t value);
    void     EnterMmio(uint32_t va, uint32_t bytes, const char* access);

    ShCpuState*         cpu_state_ = nullptr;
    PhysicalMap*        map_       = nullptr;
    MmioBus*            bus_       = nullptr;
    ClockHost*          clock_     = nullptr;
    ShMmu*              mmu_       = nullptr;
    ShTranslationCache* cache_     = nullptr;
};

}
