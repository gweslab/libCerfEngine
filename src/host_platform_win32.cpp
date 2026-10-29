#include "host_platform.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <ehdata_values.h>

#include <cstdio>
#include <cstdlib>

namespace cerfengine::platform {

void* AllocateExecutable(size_t bytes) {
    return VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

void FreeExecutable(void* base, size_t) {
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

namespace {

thread_local HostFault t_dispatch_fault;

}

const HostFault& LastDispatchFault() {
    return t_dispatch_fault;
}

int FilterDispatchException(void* exception_pointers) {
    HostFault&                fault = t_dispatch_fault;
    const EXCEPTION_POINTERS* ep  = static_cast<const EXCEPTION_POINTERS*>(exception_pointers);
    const EXCEPTION_RECORD*   rec = ep->ExceptionRecord;
    if (rec->ExceptionCode == EH_EXCEPTION_NUMBER) return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT*            ctx = ep->ContextRecord;
    fault.code    = rec->ExceptionCode;
    fault.address = rec->ExceptionAddress;
    std::snprintf(fault.registers, sizeof(fault.registers),
                  "eax=%08lX ecx=%08lX edx=%08lX ebx=%08lX esp=%08lX ebp=%08lX esi=%08lX "
                  "edi=%08lX eip=%08lX eflags=%08lX",
                  ctx->Eax, ctx->Ecx, ctx->Edx, ctx->Ebx, ctx->Esp, ctx->Ebp, ctx->Esi,
                  ctx->Edi, ctx->Eip, ctx->EFlags);
    return EXCEPTION_EXECUTE_HANDLER;
}

}
