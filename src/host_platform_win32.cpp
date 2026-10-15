#include "host_platform.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdlib>

namespace cerfengine::platform {

void* AllocateExecutable(size_t bytes) {
    return VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

void FreeExecutable(void* base) {
    VirtualFree(base, 0, MEM_RELEASE);
}

void RaiseUnwind(uint32_t code) {
    RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    std::abort();
}

bool CallHaltGuarded(void (*fn)(void* context), void* context) {
    __try {
        fn(context);
    } __except (GetExceptionCode() == kHaltUnwindCode ? EXCEPTION_EXECUTE_HANDLER
                                                       : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
    return true;
}

WakeEvent CreateWakeEvent() {
    WakeEvent event;
    event.handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    event.signal = &SetEvent;
    return event;
}

void CloseWakeEvent(const WakeEvent& event) {
    if (event.handle != nullptr) CloseHandle(event.handle);
}

uint32_t LastErrorCode() {
    return GetLastError();
}

int CaptureFault(void* exception_pointers, HostFault& fault) {
    const EXCEPTION_POINTERS* ep  = static_cast<const EXCEPTION_POINTERS*>(exception_pointers);
    const EXCEPTION_RECORD*   rec = ep->ExceptionRecord;
    const CONTEXT*            ctx = ep->ContextRecord;
    fault.code    = rec->ExceptionCode;
    fault.address = rec->ExceptionAddress;
    fault.eax     = ctx->Eax;
    fault.ecx     = ctx->Ecx;
    fault.edx     = ctx->Edx;
    fault.ebx     = ctx->Ebx;
    fault.esp     = ctx->Esp;
    fault.ebp     = ctx->Ebp;
    fault.esi     = ctx->Esi;
    fault.edi     = ctx->Edi;
    fault.eip     = ctx->Eip;
    fault.eflags  = ctx->EFlags;
    return EXCEPTION_EXECUTE_HANDLER;
}

}
