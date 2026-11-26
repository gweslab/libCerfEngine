#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/engine.h>
#include <cerfengine/mmio_bus_host.h>

namespace cerfengine {

class ClockHost;
class MmioBus;
struct JitBlock;

class ArmIdleSkip : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kArm; }
    void OnReady() override;

    bool Watching(uint32_t cycle) const {
        return phase_ != Phase::kOff || static_cast<int32_t>(cycle - next_scan_) >= 0;
    }
    bool Recording() const { return phase_ == Phase::kRecord; }

    void BeforeBlock(const JitBlock& block);
    void Restart();
    void Reset();

    void NoteStore(const uint8_t* host, uint32_t va);
    void NoteStoreSpan(const uint8_t* host, uint32_t va, uint32_t bytes);
    void NoteDeviceRead(uint32_t pa, MmioWidth width);
    void NoteDeviceWrite();

private:
    enum class Phase : uint8_t { kOff, kProbe, kRecord };

    struct LoggedWord {
        const uint8_t* word  = nullptr;
        uint32_t       old   = 0;
        uint32_t       stamp = 0;
    };

    struct ClearedEntry {
        ArmTlbEntry* entry  = nullptr;
        uintptr_t    addend = 0;
        uint32_t     tag    = 0;
        uint8_t      asid   = 0;
        uint8_t      global = 0;
    };

    static constexpr size_t   kArchBytes          = offsetof(ArmCpuState, guest_cycle_counter);
    static constexpr uint32_t kMaxIterationCycles = 1u << 12;
    static constexpr uint32_t kMaxWatchCycles     = 1u << 14;
    static constexpr uint32_t kMinBackoffCycles   = 1u << 15;
    static constexpr uint32_t kMaxBackoffCycles   = 1u << 24;
    static constexpr uint32_t kMaxMismatches      = 2u;
    static constexpr uint32_t kLogSlots           = 1024u;
    static constexpr uint32_t kMaxLoggedWords     = kLogSlots / 2u;
    static constexpr uint32_t kPageBytes          = 4096u;
    static constexpr uint64_t kUnbounded          = std::numeric_limits<uint64_t>::max();

    void Begin(uint32_t cycle);
    void Retarget(uint32_t cycle);
    void Snapshot(uint32_t cycle);
    void StartRecord(uint32_t cycle);
    void Mismatch(uint32_t cycle);
    void Fail(uint32_t cycle);
    bool Skip(uint32_t cycle);
    void SetScanExit(bool on);
    void ClearWritable();
    void RestoreWritable();
    void EndRecord();
    void LogWord(const uint8_t* word);
    bool StoresUnchanged() const;

    ArmCpuState* cpu_state_ = nullptr;
    ArmMmuState* mmu_state_ = nullptr;
    MmioBus*     bus_       = nullptr;
    ClockHost*   clock_     = nullptr;

    Phase    phase_        = Phase::kOff;
    bool     scan_exit_    = false;
    bool     broken_       = false;
    uint32_t watch_start_  = 0;
    uint32_t head_pc_      = 0;
    uint32_t head_cycle_   = 0;
    uint32_t next_scan_    = 0;
    uint32_t backoff_      = kMinBackoffCycles;
    uint32_t mismatches_   = 0;
    uint64_t stable_until_ = kUnbounded;

    uint8_t    snapshot_[kArchBytes] = {};
    LoggedWord slots_[kLogSlots]     = {};
    uint16_t   logged_[kMaxLoggedWords] = {};
    uint32_t   logged_count_ = 0;
    uint32_t   stamp_        = 0;

    ClearedEntry cleared_entries_[kArmTlbPrivilegeCount * kArmTlbEntryCount] = {};
    uint32_t     cleared_count_    = 0;
    bool         writable_cleared_ = false;
};

}
