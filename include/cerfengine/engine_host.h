#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cerfengine {

struct MipsCpuState;

struct DramRegion {
    uint32_t pa_base;
    uint32_t size;
};

enum class LogChannel : uint32_t { kJit, kCaution };

struct BlockCompileStats {
    uint32_t guest_insns;
    uint32_t host_bytes;
    uint64_t compile_ns;
};

class EngineHost {
public:
    virtual ~EngineHost() = default;

    virtual std::vector<DramRegion> CachedDramRegions() = 0;

    virtual void OnResetRequested(bool is_resume) = 0;
    virtual void OnRebootRequested()              = 0;
    virtual void OnResetDelivered()               = 0;
    virtual void OnPowerDownRequest()             = 0;
    virtual void OnBlockCompiled(const BlockCompileStats& stats) = 0;

    virtual bool HasPcTrace(uint32_t pc)                                          = 0;
    virtual bool HasPcTraceIn(uint32_t first_pc, uint32_t last_pc)                = 0;
    virtual void OnArmPcTrace(uint32_t pc, const uint32_t* gprs, uint32_t cpsr)   = 0;
    virtual void OnMipsPcTrace(uint32_t pc, const MipsCpuState* state)            = 0;

    [[noreturn]] virtual void Panic(const char* message)                    = 0;
    virtual void Log(LogChannel channel, const char* text)                  = 0;
    virtual bool Symbolize(const void* address, char* out, size_t out_size) = 0;
};

}
