#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/calling_convention.h>

namespace cerfengine::platform {

inline constexpr uint32_t kGuestUnwindCode = 0xE0000001u;
inline constexpr uint32_t kHaltUnwindCode  = 0xE0000002u;
inline constexpr uint32_t kHostFaultCode   = 0xE0000003u;

void* AllocateExecutable(size_t bytes);
void  FreeExecutable(void* base, size_t bytes);

[[noreturn]] void RaiseUnwind(uint32_t code);

bool CallHaltGuarded(void (*fn)(void* context), void* context);

template <typename Fn>
bool CallHaltGuarded(Fn& fn) {
    return CallHaltGuarded([](void* context) { (*static_cast<Fn*>(context))(); }, &fn);
}

using WakeSignalFn = int (CERFENGINE_STDCALL*)(void* handle);

struct WakeEvent {
    void*        handle = nullptr;
    WakeSignalFn signal = nullptr;
};

WakeEvent CreateWakeEvent();
void      CloseWakeEvent(const WakeEvent& event);
uint32_t  LastErrorCode();

struct HostFault {
    uint32_t    code;
    const void* address;
    char        registers[448];
};

#if CERFENGINE_BACKEND_X86_WIN32
int CaptureFault(void* exception_pointers, HostFault& fault);
#endif

#if CERFENGINE_BACKEND_X86_64_SYSV
uint32_t CallDispatchGuarded(void (*fn)(void* context), void* context, HostFault& fault);
#endif

}
