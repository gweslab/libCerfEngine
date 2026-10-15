#pragma once

#include "clock_host.h"
#include "engine_host.h"
#include "engine_setup.h"

#include <servicelocator/service_locator.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace cerfengine {

enum class HaltReason : uint32_t {
    kNone,
    kUnimplemented,
    kUnpredictable,
    kUnmappedPhysical,
};

struct HaltRecord {
    HaltReason reason       = HaltReason::kNone;
    char       detail[1024] = {};
};

class Engine : public servicelocator::ServiceLocator<Engine, 512> {
public:
    explicit Engine(const Setup& setup);
    ~Engine();

    bool Start(std::string& error);

    const Setup& GetSetup() const { return setup_; }
    EngineHost&  Host() const { return *setup_.host; }

    void BindClock(ClockHost& clock);
    void AttachClock(ClockHost*& slot);

    void SetLogMask(uint32_t mask) { log_mask_.store(mask, std::memory_order_relaxed); }
    bool LogEnabled(LogChannel channel) const {
        return (log_mask_.load(std::memory_order_relaxed) >> static_cast<uint32_t>(channel)) & 1u;
    }
    void Log(LogChannel channel, const char* fmt, ...);
    [[noreturn]] void Panic(const char* fmt, ...);
    [[noreturn]] void Halt(HaltReason reason, const char* fmt, ...);
    const HaltRecord& LastHalt() const { return halt_; }

    [[noreturn]] void OnLocatorError(const char* message);

private:
    bool ValidateSetup(std::string& error);

    Setup                    setup_;
    ClockHost*               clock_ = nullptr;
    std::vector<ClockHost**> clock_slots_;
    HaltRecord               halt_;
    std::atomic<uint32_t>    log_mask_{0};
};

class EngineService : public servicelocator::Service {
public:
    explicit EngineService(Engine& engine) : engine_(engine) {}

    Engine& Owner() const { return engine_; }

protected:
    Engine& engine_;
};

}

#define CERFENGINE_LOG(engine, channel, ...)                                       \
    do {                                                                           \
        if ((engine).LogEnabled(cerfengine::LogChannel::channel))                  \
            (engine).Log(cerfengine::LogChannel::channel, __VA_ARGS__);            \
    } while (0)

#define CERFENGINE_REGISTER(Type) SERVICELOCATOR_REGISTER(cerfengine::Engine, Type)
#define CERFENGINE_REGISTER_AS(Type, Base) \
    SERVICELOCATOR_REGISTER_AS(cerfengine::Engine, Type, Base)
#define CERFENGINE_REGISTER_AS_FALLBACK(Type, Base) \
    SERVICELOCATOR_REGISTER_AS_FALLBACK(cerfengine::Engine, Type, Base)
