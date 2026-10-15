#pragma once

#include <cstdint>
#include <optional>

#include "engine.h"

namespace cerfengine {

class GuestEngine : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    enum class RunStatus : uint32_t { kRunning, kHalted };

    virtual RunStatus Run()                = 0;
    virtual bool      DeepSleep()    const = 0;
    virtual bool      ResetPending() const = 0;
    virtual uint32_t  Pc()           const = 0;

    virtual void      PrintFatalDump()     = 0;

    virtual bool GuestIrqMasked() const = 0;

    virtual std::optional<uint8_t*> PeekGuestVa(uint32_t va) = 0;

    virtual void FlushTranslationCache() = 0;

    virtual void SetResetPending(bool is_resume) = 0;

    virtual void EnterDeepSleep() = 0;

    virtual void ExitDeepSleep() = 0;

    virtual void EnterIdleWait() = 0;

    virtual void SetCoreClockStopped(bool stopped) = 0;

    virtual void SetDmaRegion(uint32_t pa, uint32_t size) = 0;

    virtual uint8_t* ResolveGuestVaToHost(uint32_t va) = 0;

    virtual bool ResolveGuestVaToPa(uint32_t va, uint32_t* pa) = 0;

    virtual uint32_t PhysAddrMask() const { return 0xFFFFFFFFu; }

    /* QEMU cpu_exit() (cpu-common.c). */
    virtual void SetHostChainExit(bool) {}
};

}
