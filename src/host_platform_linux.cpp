#include "host_platform.h"

#include <cerrno>
#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include <cerfengine/wake_event.h>

#if !defined(__linux__) || !defined(__x86_64__)
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

constexpr int    kFaultSignals[]  = {SIGSEGV, SIGBUS, SIGILL, SIGFPE};
constexpr size_t kFaultSignalCount = sizeof(kFaultSignals) / sizeof(kFaultSignals[0]);
constexpr int    kCapturedRegs     = REG_EFL + 1;

struct FaultCapture {
    int         signal;
    const void* data_address;
    greg_t      regs[kCapturedRegs];
};

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

void FaultHandler(int sig, siginfo_t* info, void* context) {
    Guard* const guard = t_guard;
    if (guard != nullptr && guard->dispatch) {
        const greg_t* regs = static_cast<const ucontext_t*>(context)->uc_mcontext.gregs;
        t_fault.signal       = sig;
        t_fault.data_address = info->si_addr;
        for (int i = 0; i < kCapturedRegs; ++i) t_fault.regs[i] = regs[i];
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

void FormatFault(HostFault& fault) {
    const greg_t* r = t_fault.regs;
    fault.code      = static_cast<uint32_t>(t_fault.signal);
    fault.address   = reinterpret_cast<const void*>(r[REG_RIP]);
    std::snprintf(fault.registers, sizeof(fault.registers),
                  "signal %d (%s) data=%p rax=%llX rcx=%llX rdx=%llX rbx=%llX rsp=%llX "
                  "rbp=%llX rsi=%llX rdi=%llX r8=%llX r9=%llX r10=%llX r11=%llX r12=%llX "
                  "r13=%llX r14=%llX r15=%llX rip=%llX eflags=%llX",
                  t_fault.signal, strsignal(t_fault.signal), t_fault.data_address,
                  static_cast<unsigned long long>(r[REG_RAX]),
                  static_cast<unsigned long long>(r[REG_RCX]),
                  static_cast<unsigned long long>(r[REG_RDX]),
                  static_cast<unsigned long long>(r[REG_RBX]),
                  static_cast<unsigned long long>(r[REG_RSP]),
                  static_cast<unsigned long long>(r[REG_RBP]),
                  static_cast<unsigned long long>(r[REG_RSI]),
                  static_cast<unsigned long long>(r[REG_RDI]),
                  static_cast<unsigned long long>(r[REG_R8]),
                  static_cast<unsigned long long>(r[REG_R9]),
                  static_cast<unsigned long long>(r[REG_R10]),
                  static_cast<unsigned long long>(r[REG_R11]),
                  static_cast<unsigned long long>(r[REG_R12]),
                  static_cast<unsigned long long>(r[REG_R13]),
                  static_cast<unsigned long long>(r[REG_R14]),
                  static_cast<unsigned long long>(r[REG_R15]),
                  static_cast<unsigned long long>(r[REG_RIP]),
                  static_cast<unsigned long long>(r[REG_EFL]));
}

int SignalWake(void* handle) {
    const uint64_t one = 1u;
    ssize_t        written;
    do {
        written = ::write(WakeEventDescriptor(handle), &one, sizeof(one));
    } while (written < 0 && errno == EINTR);
    return written == static_cast<ssize_t>(sizeof(one)) ? 1 : 0;
}

}

void* AllocateExecutable(size_t bytes) {
    const uintptr_t code = reinterpret_cast<uintptr_t>(&AllocateExecutable);
    const uintptr_t near = code > (uintptr_t{1} << 31) ? code - (uintptr_t{1} << 30)
                                                       : code + (uintptr_t{1} << 30);
    void* const     hint = reinterpret_cast<void*>(near & ~((uintptr_t{1} << 21) - 1u));
    void* const     base = ::mmap(hint, bytes, PROT_READ | PROT_WRITE | PROT_EXEC,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return base == MAP_FAILED ? nullptr : base;
}

void FreeExecutable(void* base, size_t bytes) {
    ::munmap(base, bytes);
}

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
