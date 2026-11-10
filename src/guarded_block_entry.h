#pragma once

#include <cstdint>

#if CERFENGINE_BACKEND_X86_WIN32
#include <excpt.h>
#endif

#include <cerfengine/engine.h>
#include <cerfengine/force_inline.h>
#include "guest_unwind.h"
#include "host_platform.h"

namespace cerfengine {

enum class BlockExit : uint8_t {
    kReturned,
    kGuestUnwind,
    kHalted,
    kHostFault,
};

class GuardedBlockEntry : public EngineService {
public:
    using EngineService::EngineService;

#if CERFENGINE_GUARDED_DISPATCH
    template <typename Fn>
    CERFENGINE_FORCEINLINE BlockExit Enter(Fn& enter, uint32_t guest_pc, platform::HostFault& fault) {
        const uint32_t code = platform::CallDispatchGuarded(
            [](void* context) { (*static_cast<Fn*>(context))(); }, &enter, fault);
        if (code == platform::kHaltUnwindCode) {
            platform::RethrowHostException();
            return BlockExit::kHalted;
        }
        if (code == 0u) return BlockExit::kReturned;
        if (code == platform::kGuestUnwindCode) return BlockExit::kGuestUnwind;
        if (code != platform::kHostFaultCode) PanicOnUnwindCode(code, guest_pc);
        return BlockExit::kHostFault;
    }
#endif

    void              LogHostFault(const char* engine_name, const platform::HostFault& fault, uint32_t guest_pc);
    [[noreturn]] void PanicOnHostFault(const char* engine_name, uint32_t guest_pc);
    [[noreturn]] void ReportHostFault(const char* engine_name, const platform::HostFault& fault, uint32_t guest_pc);
    [[noreturn]] void PanicWithoutBackend(uint32_t guest_pc);

private:
    [[noreturn]] void PanicOnUnwindCode(uint32_t code, uint32_t guest_pc);
};

}
