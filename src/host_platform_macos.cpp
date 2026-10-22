#include "host_platform.h"

#include <cerrno>
#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <libkern/OSCacheControl.h>
#include <mutex>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include <cerfengine/wake_event.h>

#if !defined(__APPLE__) || !defined(__arm64__)
#error "host_platform_macos.cpp has no execution backend for this host"
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
constexpr int    kGeneralRegs      = 29;

struct FaultCapture {
    int         signal;
    const void* data_address;
    uint64_t    x[kGeneralRegs];
    uint64_t    fp;
    uint64_t    lr;
    uint64_t    sp;
    uint64_t    pc;
    uint32_t    cpsr;
};

thread_local Guard*       t_guard = nullptr;
thread_local FaultCapture t_fault{};
thread_local uint32_t     t_code_write_depth = 0;
thread_local bool         t_write_protect_set = false;

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
        const auto& ss       = static_cast<const ucontext_t*>(context)->uc_mcontext->__ss;
        t_fault.signal       = sig;
        t_fault.data_address = info->si_addr;
        for (int i = 0; i < kGeneralRegs; ++i) t_fault.x[i] = ss.__x[i];
        t_fault.fp   = __darwin_arm_thread_state64_get_fp(ss);
        t_fault.lr   = __darwin_arm_thread_state64_get_lr(ss);
        t_fault.sp   = __darwin_arm_thread_state64_get_sp(ss);
        t_fault.pc   = __darwin_arm_thread_state64_get_pc(ss);
        t_fault.cpsr = ss.__cpsr;
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
    const FaultCapture& f = t_fault;
    fault.code            = static_cast<uint32_t>(f.signal);
    fault.address         = reinterpret_cast<const void*>(static_cast<uintptr_t>(f.pc));
    int used = std::snprintf(fault.registers, sizeof(fault.registers), "signal %d (%s) data=%p",
                             f.signal, strsignal(f.signal), f.data_address);
    for (int i = 0; i < kGeneralRegs && used > 0 && static_cast<size_t>(used) < sizeof(fault.registers);
         ++i) {
        used += std::snprintf(fault.registers + used, sizeof(fault.registers) - static_cast<size_t>(used),
                              " x%d=%llX", i, static_cast<unsigned long long>(f.x[i]));
    }
    if (used > 0 && static_cast<size_t>(used) < sizeof(fault.registers)) {
        std::snprintf(fault.registers + used, sizeof(fault.registers) - static_cast<size_t>(used),
                      " fp=%llX lr=%llX sp=%llX pc=%llX cpsr=%X",
                      static_cast<unsigned long long>(f.fp), static_cast<unsigned long long>(f.lr),
                      static_cast<unsigned long long>(f.sp), static_cast<unsigned long long>(f.pc),
                      f.cpsr);
    }
}

void LeaveCodeWrite() {
    if (t_code_write_depth == 0u) return;
    t_code_write_depth  = 0u;
    pthread_jit_write_protect_np(1);
    t_write_protect_set = true;
}

void ProtectCodeOnFirstDispatch() {
    if (t_write_protect_set) return;
    t_write_protect_set = true;
    pthread_jit_write_protect_np(1);
}

void* WakeEventHandle(int read_fd, int write_fd) {
    const uint64_t packed = (static_cast<uint64_t>(static_cast<uint32_t>(write_fd)) << 32) |
                            static_cast<uint32_t>(read_fd);
    return reinterpret_cast<void*>(static_cast<uintptr_t>(packed + 1u));
}

int WriteDescriptor(void* handle) {
    return static_cast<int>((reinterpret_cast<uintptr_t>(handle) - 1u) >> 32);
}

int SignalWake(void* handle) {
    const uint8_t one = 1u;
    ssize_t       written;
    do {
        written = ::write(WriteDescriptor(handle), &one, sizeof(one));
    } while (written < 0 && errno == EINTR);
    if (written == static_cast<ssize_t>(sizeof(one))) return 1;
    return written < 0 && errno == EAGAIN ? 1 : 0;
}

bool MakeNonBlockingCloseOnExec(int fd) {
    const int flags = ::fcntl(fd, F_GETFL);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 &&
           ::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

}

void* AllocateExecutable(size_t bytes) {
    void* const base = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE | PROT_EXEC,
                              MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    return base == MAP_FAILED ? nullptr : base;
}

void FreeExecutable(void* base, size_t bytes) {
    ::munmap(base, bytes);
}

void BeginCodeWrite() {
    if (t_code_write_depth++ == 0u) pthread_jit_write_protect_np(0);
}

void EndCodeWrite() {
    if (--t_code_write_depth != 0u) return;
    pthread_jit_write_protect_np(1);
    t_write_protect_set = true;
}

void FlushInstructionCache(const void* start, size_t bytes) {
    sys_icache_invalidate(const_cast<void*>(start), bytes);
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
    Guard    guard;
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
    ProtectCodeOnFirstDispatch();
    Guard      guard;
    GuardScope scope(guard, true);
    const int  code = sigsetjmp(guard.env, 0);
    if (code == 0) {
        fn(context);
        return 0u;
    }
    LeaveCodeWrite();
    if (static_cast<uint32_t>(code) == kHostFaultCode) FormatFault(fault);
    return static_cast<uint32_t>(code);
}

WakeEvent CreateWakeEvent() {
    WakeEvent event;
    int       fds[2];
    if (::pipe(fds) != 0) return event;
    if (!MakeNonBlockingCloseOnExec(fds[0]) || !MakeNonBlockingCloseOnExec(fds[1])) {
        ::close(fds[0]);
        ::close(fds[1]);
        return event;
    }
    event.handle = WakeEventHandle(fds[0], fds[1]);
    event.signal = &SignalWake;
    return event;
}

void CloseWakeEvent(const WakeEvent& event) {
    if (event.handle == nullptr) return;
    ::close(WakeEventDescriptor(event.handle));
    ::close(WriteDescriptor(event.handle));
}

uint32_t LastErrorCode() {
    return static_cast<uint32_t>(errno);
}

}

namespace cerfengine {

int WakeEventDescriptor(void* handle) {
    return static_cast<int>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handle) - 1u));
}

void ConsumeWakeEvent(void* handle) {
    uint8_t drain[64];
    for (;;) {
        const ssize_t read_bytes = ::read(WakeEventDescriptor(handle), drain, sizeof(drain));
        if (read_bytes > 0) continue;
        if (read_bytes < 0 && errno == EINTR) continue;
        return;
    }
}

}
