#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/calling_convention.h>

namespace cerfengine::platform {

inline constexpr uint32_t kGuestUnwindCode = 0xE0000001u;
inline constexpr uint32_t kHaltUnwindCode  = 0xE0000002u;

void* AllocateExecutable(size_t bytes);
void  FreeExecutable(void* base);

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
    uint32_t    eax, ecx, edx, ebx, esp, ebp, esi, edi, eip, eflags;
};

int CaptureFault(void* exception_pointers, HostFault& fault);

}
