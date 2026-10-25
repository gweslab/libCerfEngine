#include "host_platform.h"

#include "host_fault_text.h"

#include <cerrno>
#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include <cerfengine/wake_event.h>

#if !defined(__linux__) || !(defined(__x86_64__) || defined(__aarch64__))
#error "host_platform_linux.cpp has no execution backend for this host"
#endif

namespace cerfengine::platform {

namespace {

struct Guard {
    sigjmp_buf env;
    bool       dispatch = false;
    Guard*     previous = nullptr;
};

struct GuardUnwind {
    uint32_t code;
};

constexpr int    kFaultSignals[]   = {SIGSEGV, SIGBUS, SIGILL, SIGFPE};
constexpr size_t kFaultSignalCount = sizeof(kFaultSignals) / sizeof(kFaultSignals[0]);

#if defined(__x86_64__)
constexpr int kCapturedRegs = REG_EFL + 1;

struct FaultCapture {
    int         signal;
    const void* data_address;
    greg_t      regs[kCapturedRegs];
};
#else
constexpr int kGeneralRegs = 31;

struct FaultCapture {
    int         signal;
    const void* data_address;
    uint64_t    x[kGeneralRegs];
    uint64_t    sp;
    uint64_t    pc;
    uint64_t    pstate;
};
#endif

thread_local Guard*       t_guard = nullptr;
thread_local FaultCapture t_fault{};

struct sigaction g_previous[kFaultSignalCount];
std::once_flag   g_install;

class GuardScope {
public:
    GuardScope(Guard& guard, bool dispatch) : guard_(guard) {
        guard_.dispatch = dispatch;
        guard_.previous = t_guard;
        t_guard         = &guard_;
    }
    ~GuardScope() { t_guard = guard_.previous; }
    GuardScope(const GuardScope&)            = delete;
    GuardScope& operator=(const GuardScope&) = delete;

private:
    Guard& guard_;
};

void CaptureRegisters(const ucontext_t& context) {
#if defined(__x86_64__)
    const greg_t* regs = context.uc_mcontext.gregs;
    for (int i = 0; i < kCapturedRegs; ++i) t_fault.regs[i] = regs[i];
#else
    const mcontext_t& m = context.uc_mcontext;
    for (int i = 0; i < kGeneralRegs; ++i) t_fault.x[i] = m.regs[i];
    t_fault.sp     = m.sp;
    t_fault.pc     = m.pc;
    t_fault.pstate = m.pstate;
#endif
}

void FaultHandler(int sig, siginfo_t* info, void* context) {
    Guard* const guard = t_guard;
    if (guard != nullptr && guard->dispatch) {
        t_fault.signal       = sig;
        t_fault.data_address = info->si_addr;
        CaptureRegisters(*static_cast<const ucontext_t*>(context));
        sigset_t unblock;
        sigemptyset(&unblock);
        sigaddset(&unblock, sig);
        pthread_sigmask(SIG_UNBLOCK, &unblock, nullptr);
        siglongjmp(guard->env, static_cast<int>(kHostFaultCode));
    }
    for (size_t i = 0; i < kFaultSignalCount; ++i) {
        if (kFaultSignals[i] == sig) sigaction(sig, &g_previous[i], nullptr);
    }
}

void InstallFaultHandlers() {
    struct sigaction action {};
    action.sa_sigaction = &FaultHandler;
    action.sa_flags     = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < kFaultSignalCount; ++i) {
        sigaction(kFaultSignals[i], &action, &g_previous[i]);
    }
}

#if defined(__x86_64__)
constexpr struct {
    const char* name;
    int         index;
} kFaultRegs[] = {{"rax", REG_RAX}, {"rcx", REG_RCX}, {"rdx", REG_RDX}, {"rbx", REG_RBX}, {"rsp", REG_RSP},
                  {"rbp", REG_RBP}, {"rsi", REG_RSI}, {"rdi", REG_RDI}, {"r8", REG_R8},   {"r9", REG_R9},
                  {"r10", REG_R10}, {"r11", REG_R11}, {"r12", REG_R12}, {"r13", REG_R13}, {"r14", REG_R14},
                  {"r15", REG_R15}, {"rip", REG_RIP}, {"eflags", REG_EFL}};

void FormatFault(HostFault& fault) {
    const greg_t* r = t_fault.regs;
    fault.code      = static_cast<uint32_t>(t_fault.signal);
    fault.address   = reinterpret_cast<const void*>(r[REG_RIP]);
    FaultText text(fault.registers, sizeof(fault.registers), t_fault.signal, t_fault.data_address);
    for (const auto& reg : kFaultRegs) text.Register(reg.name, static_cast<uint64_t>(r[reg.index]));
}
#else
void FormatFault(HostFault& fault) {
    const FaultCapture& f = t_fault;
    fault.code            = static_cast<uint32_t>(f.signal);
    fault.address         = reinterpret_cast<const void*>(static_cast<uintptr_t>(f.pc));
    FaultText text(fault.registers, sizeof(fault.registers), f.signal, f.data_address);
    text.Registers("x", f.x, kGeneralRegs);
    text.Register("sp", f.sp);
    text.Register("pc", f.pc);
    text.Register("pstate", f.pstate);
}
#endif

void* WakeEventHandle(int descriptor) {
    return reinterpret_cast<void*>(static_cast<intptr_t>(descriptor) + 1);
}

int SignalWake(void* handle) {
    const uint64_t one = 1u;
    ssize_t        written;
    do {
        written = ::write(WakeEventDescriptor(handle), &one, sizeof(one));
    } while (written < 0 && errno == EINTR);
    return written == static_cast<ssize_t>(sizeof(one)) ? 1 : 0;
}

void* ExecutableHint() {
#if defined(__x86_64__)
    const uintptr_t code = reinterpret_cast<uintptr_t>(&AllocateExecutable);
    const uintptr_t near = code > (uintptr_t{1} << 31) ? code - (uintptr_t{1} << 30)
                                                       : code + (uintptr_t{1} << 30);
    return reinterpret_cast<void*>(near & ~((uintptr_t{1} << 21) - 1u));
#else
    return nullptr;
#endif
}

}

void* AllocateExecutable(size_t bytes) {
    void* const base = ::mmap(ExecutableHint(), bytes, PROT_READ | PROT_WRITE | PROT_EXEC,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return base == MAP_FAILED ? nullptr : base;
}

void FreeExecutable(void* base, size_t bytes) {
    ::munmap(base, bytes);
}

#if CERFENGINE_BACKEND_A64_LINUX
void FlushInstructionCache(const void* start, size_t bytes) {
    char* const begin = static_cast<char*>(const_cast<void*>(start));
    __builtin___clear_cache(begin, begin + bytes);
}
#endif

void RaiseUnwind(uint32_t code) {
    Guard* const guard = t_guard;
    if (guard == nullptr) {
        std::fprintf(stderr, "cerfengine: unwind 0x%08X with no guard on this thread\n", code);
        std::fflush(stderr);
        std::abort();
    }
    if (!guard->dispatch) throw GuardUnwind{code};
    siglongjmp(guard->env, static_cast<int>(code));
}

bool CallHaltGuarded(void (*fn)(void* context), void* context) {
    Guard guard;
    uint32_t unwound = 0u;
    {
        GuardScope scope(guard, false);
        try {
            fn(context);
        } catch (const GuardUnwind& unwind) {
            unwound = unwind.code;
        }
    }
    if (unwound == 0u) return true;
    if (unwound == kHaltUnwindCode) return false;
    RaiseUnwind(unwound);
}

uint32_t CallDispatchGuarded(void (*fn)(void* context), void* context, HostFault& fault) {
    std::call_once(g_install, &InstallFaultHandlers);
    Guard      guard;
    GuardScope scope(guard, true);
    const int  code = sigsetjmp(guard.env, 0);
    if (code == 0) {
        fn(context);
        return 0u;
    }
    if (static_cast<uint32_t>(code) == kHostFaultCode) FormatFault(fault);
    return static_cast<uint32_t>(code);
}

WakeEvent CreateWakeEvent() {
    WakeEvent  event;
    const int descriptor = ::eventfd(0u, EFD_CLOEXEC);
    if (descriptor < 0) return event;
    event.handle = WakeEventHandle(descriptor);
    event.signal = &SignalWake;
    return event;
}

void CloseWakeEvent(const WakeEvent& event) {
    if (event.handle != nullptr) ::close(WakeEventDescriptor(event.handle));
}

uint32_t LastErrorCode() {
    return static_cast<uint32_t>(errno);
}

}

namespace cerfengine {

int WakeEventDescriptor(void* handle) {
    return static_cast<int>(reinterpret_cast<intptr_t>(handle) - 1);
}

void ConsumeWakeEvent(void* handle) {
    uint64_t count = 0;
    ssize_t  read_bytes;
    do {
        read_bytes = ::read(WakeEventDescriptor(handle), &count, sizeof(count));
    } while (read_bytes < 0 && errno == EINTR);
}

}
