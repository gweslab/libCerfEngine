#include "host_platform.h"

#include <cstdio>
#include <cstdlib>

#include <cerfengine/wake_event.h>

namespace cerfengine::platform {

namespace {

[[noreturn]] void NoBackend(const char* function) {
    std::fprintf(stderr, "cerfengine: %s is not available in a build without an execution "
                         "backend\n", function);
    std::fflush(stderr);
    std::abort();
}

}

void* AllocateExecutable(size_t) { NoBackend("AllocateExecutable"); }

void FreeExecutable(void*, size_t) { NoBackend("FreeExecutable"); }

void RaiseUnwind(uint32_t) { NoBackend("RaiseUnwind"); }

bool CallHaltGuarded(void (*)(void* context), void*) { NoBackend("CallHaltGuarded"); }

WakeEvent CreateWakeEvent() { NoBackend("CreateWakeEvent"); }

void CloseWakeEvent(const WakeEvent&) { NoBackend("CloseWakeEvent"); }

uint32_t LastErrorCode() { NoBackend("LastErrorCode"); }

}

namespace cerfengine {

int WakeEventDescriptor(void*) { platform::NoBackend("WakeEventDescriptor"); }

void ConsumeWakeEvent(void*) { platform::NoBackend("ConsumeWakeEvent"); }

}
