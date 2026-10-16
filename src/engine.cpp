#include <cerfengine/engine.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "host_platform.h"

#if CERFENGINE_BACKEND_X86_WIN32
#include "mips/mips_translation_cache.h"
#endif

namespace cerfengine {

namespace {

constexpr size_t kMessageBytes = 2048;

}

Engine::Engine(const Setup& setup) : setup_(setup) {}

void Engine::BindClock(ClockHost& clock) {
    clock_ = &clock;
    for (ClockHost** slot : clock_slots_) *slot = clock_;
}

void Engine::AttachClock(ClockHost*& slot) {
    clock_slots_.push_back(&slot);
    slot = clock_;
}

Engine::~Engine() {
    TearDown();
}

#if CERFENGINE_BACKEND_X86_WIN32
bool Engine::ValidateSetup(std::string& error) {
    if (setup_.host == nullptr || setup_.mmio.generation == nullptr) {
        error = "the setup needs a host and the MMIO bus generation counter";
        return false;
    }
    if (setup_.isa == Isa::kArm && setup_.arm_system == nullptr) {
        error = "an ARM engine needs an ARM system host";
        return false;
    }
    const MipsTlbHooks&       tlb       = setup_.mips_tlb;
    const MipsExceptionHooks& exception = setup_.mips_exception;
    if (setup_.isa == Isa::kMips &&
        (tlb.map_address == nullptr || tlb.page_global == nullptr ||
         tlb.write_indexed == nullptr || tlb.write_random == nullptr ||
         tlb.probe == nullptr || tlb.read == nullptr || exception.enter == nullptr ||
         exception.set_mmu_fault_regs == nullptr || exception.interrupts_enabled == nullptr ||
         exception.eret == nullptr || exception.rfe == nullptr ||
         setup_.mips_system == nullptr)) {
        error = "a MIPS engine needs the MIPS TLB hooks, the MIPS exception hooks and a "
                "MIPS system host";
        return false;
    }

    const std::vector<DramRegion> dram = setup_.host->CachedDramRegions();
    if (dram.empty()) {
        error = "the host declares no cached DRAM region";
        return false;
    }
    return setup_.isa != Isa::kMips ||
           MipsTranslationCache::ValidateDram(dram, setup_.memory, error);
}
#endif

bool Engine::Start(std::string& error) {
#if CERFENGINE_BACKEND_X86_WIN32
    if (!ValidateSetup(error)) return false;
    Bootstrap();
    return true;
#else
    error = "this build of libCerfEngine has no execution backend for the host";
    return false;
#endif
}

void Engine::Log(LogChannel channel, const char* fmt, ...) {
    char text[kMessageBytes];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    Host().Log(channel, text);
}

void Engine::Panic(const char* fmt, ...) {
    char text[kMessageBytes];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    Host().Panic(text);
    std::abort();
}

#if CERFENGINE_X86_TRANSLATOR
void Engine::Halt(HaltReason reason, const char* fmt, ...) {
    halt_.reason = reason;
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(halt_.detail, sizeof(halt_.detail), fmt, args);
    va_end(args);
    platform::RaiseUnwind(platform::kHaltUnwindCode);
}
#endif

void Engine::OnLocatorError(const char* message) {
    if (setup_.host != nullptr) setup_.host->Panic(message);
    std::fprintf(stderr, "cerfengine: %s\n", message);
    std::fflush(stderr);
    std::abort();
}

}
