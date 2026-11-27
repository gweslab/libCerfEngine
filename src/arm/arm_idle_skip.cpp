#include "arm_idle_skip.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/clock_host.h>
#include <cerfengine/jit_block.h>
#include "../mmio_bus.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmIdleSkip);

void ArmIdleSkip::OnReady() {
    ArmCpu& cpu = engine_.Get<ArmCpu>();
    cpu_state_  = cpu.State();
    mmu_state_  = engine_.Get<ArmMmu>().State();
    bus_        = &engine_.Get<MmioBus>();
    engine_.AttachClock(clock_);
    cpu.BindIdleSkip(this);
}

void ArmIdleSkip::BeforeBlock(const JitBlock& block) {
    const uint32_t cycle = cpu_state_->guest_cycle_counter;
    if (mmu_state_->effective_control_register.bits.m == 0u) {
        Fail(cycle);
        return;
    }
    if (!scan_exit_) {
        watch_start_ = cycle;
        SetScanExit(true);
    } else if (cycle - watch_start_ > kMaxWatchCycles) {
        Fail(cycle);
        return;
    }
    if (block.idle_barrier != 0u) {
        Retarget(cycle);
        return;
    }
    if (phase_ == Phase::kOff) {
        Begin(cycle);
        return;
    }
    if (cpu_state_->gprs[ArmGpr::kR15] != head_pc_) {
        if (cycle - head_cycle_ > kMaxIterationCycles) Retarget(cycle);
        return;
    }
    const bool same = std::memcmp(snapshot_, cpu_state_, kArchBytes) == 0;
    if (phase_ == Phase::kProbe) {
        if (same) StartRecord(cycle);
        else      Mismatch(cycle);
        return;
    }
    if (same && !broken_ && StoresUnchanged() && Skip(cycle)) {
        StartRecord(cpu_state_->guest_cycle_counter);
        return;
    }
    Mismatch(cycle);
}

void ArmIdleSkip::Restart() {
    if (phase_ == Phase::kOff) return;
    EndRecord();
    phase_     = Phase::kOff;
    next_scan_ = cpu_state_->guest_cycle_counter;
}

void ArmIdleSkip::Reset() {
    EndRecord();
    phase_      = Phase::kOff;
    backoff_    = kMinBackoffCycles;
    next_scan_  = cpu_state_->guest_cycle_counter + kMinBackoffCycles;
    SetScanExit(false);
}

void ArmIdleSkip::NoteStore(const uint8_t* host, uint32_t va) {
    const uint32_t offset = va & (kPageBytes - 1u) & ~3u;
    const uint8_t* word   = host - (va & 3u);
    LogWord(word);
    if (kPageBytes - offset > 4u) LogWord(word + 4);
}

void ArmIdleSkip::NoteStoreSpan(const uint8_t* host, uint32_t va, uint32_t bytes) {
    const uint8_t* word  = host - (va & 3u);
    const uint32_t words = ((va & 3u) + bytes + 3u) >> 2;
    for (uint32_t i = 0; i < words; ++i) LogWord(word + 4u * i);
}

void ArmIdleSkip::NoteDeviceRead(uint32_t pa, MmioWidth width) {
    if (phase_ != Phase::kRecord) return;
    const uint64_t until = bus_->ReadStableUntil(pa, width);
    if (until <= clock_->Cycles()) {
        broken_ = true;
        return;
    }
    stable_until_ = std::min(stable_until_, until);
}

void ArmIdleSkip::NoteDeviceWrite() {
    if (phase_ == Phase::kRecord) broken_ = true;
}

void ArmIdleSkip::Begin(uint32_t cycle) {
    phase_      = Phase::kProbe;
    head_pc_    = cpu_state_->gprs[ArmGpr::kR15];
    mismatches_ = 0;
    Snapshot(cycle);
}

void ArmIdleSkip::Retarget(uint32_t cycle) {
    EndRecord();
    phase_     = Phase::kOff;
    next_scan_ = cycle;
}

void ArmIdleSkip::Snapshot(uint32_t cycle) {
    std::memcpy(snapshot_, cpu_state_, kArchBytes);
    head_cycle_ = cycle;
}

void ArmIdleSkip::StartRecord(uint32_t cycle) {
    phase_        = Phase::kRecord;
    broken_       = false;
    stable_until_ = kUnbounded;
    logged_count_ = 0;
    if (++stamp_ == 0u) {
        for (LoggedWord& slot : slots_) slot.stamp = 0;
        stamp_ = 1u;
    }
    Snapshot(cycle);
    if (!writable_cleared_) ClearWritable();
}

void ArmIdleSkip::Mismatch(uint32_t cycle) {
    if (++mismatches_ > kMaxMismatches) {
        Retarget(cycle);
        return;
    }
    if (phase_ == Phase::kRecord) StartRecord(cycle);
    else                          Snapshot(cycle);
}

void ArmIdleSkip::Fail(uint32_t cycle) {
    EndRecord();
    SetScanExit(false);
    phase_     = Phase::kOff;
    next_scan_ = cycle + backoff_;
    backoff_   = std::min(backoff_ * 2u, kMaxBackoffCycles);
}

bool ArmIdleSkip::Skip(uint32_t cycle) {
    const uint32_t period = cycle - head_cycle_;
    const int32_t  ahead  = static_cast<int32_t>(cpu_state_->guest_cycle_deadline - cycle);
    if (period == 0u || ahead <= 1) return false;
    uint64_t room = static_cast<uint64_t>(ahead) - 1u;
    if (stable_until_ != kUnbounded) {
        const uint64_t now = clock_->Cycles();
        if (stable_until_ <= now) return false;
        room = std::min(room, stable_until_ - now);
    }
    const uint64_t iterations = room / period;
    if (iterations == 0u) return false;
    cpu_state_->guest_cycle_counter = cycle + static_cast<uint32_t>(iterations * period);
    mismatches_   = 0;
    backoff_      = kMinBackoffCycles;
    watch_start_  = cpu_state_->guest_cycle_counter;
    return true;
}

void ArmIdleSkip::SetScanExit(bool on) {
    scan_exit_ = on;
    std::atomic_ref<uint32_t> word(cpu_state_->chain_exit_request);
    if (on) word.fetch_or(kChainExitScan, std::memory_order_acq_rel);
    else    word.fetch_and(~kChainExitScan, std::memory_order_acq_rel);
}

void ArmIdleSkip::ClearWritable() {
    cleared_count_ = 0;
    for (ArmTlbUnit& unit : mmu_state_->data_tlb) {
        for (ArmTlbEntry& entry : unit.entries) {
            const bool subpage_writes =
                ArmTlbIsSubpageEntry(entry) && (entry.tag & kArmTlbSubpageWriteBits) != 0u;
            if (entry.writable == 0u && !subpage_writes) continue;
            cleared_entries_[cleared_count_++] =
                ClearedEntry{&entry, entry.va_addend, entry.tag, entry.asid, entry.global};
            entry.writable = 0u;
            entry.tag &= subpage_writes ? ~kArmTlbSubpageWriteBits : ~0u;
        }
    }
    writable_cleared_ = true;
}

void ArmIdleSkip::RestoreWritable() {
    for (uint32_t i = 0; i < cleared_count_; ++i) {
        const ClearedEntry& saved = cleared_entries_[i];
        ArmTlbEntry&        entry = *saved.entry;
        const bool subpage = saved.tag != kArmTlbInvalidTag && (saved.tag & kArmTlbSubpageTagBit) != 0u;
        const uint32_t cleared_tag = subpage ? saved.tag & ~kArmTlbSubpageWriteBits : saved.tag;
        if (entry.tag == cleared_tag && entry.va_addend == saved.addend &&
            entry.asid == saved.asid && entry.global == saved.global) {
            if (subpage) entry.tag = saved.tag;
            else         entry.writable = 1u;
        }
    }
    cleared_count_    = 0;
    writable_cleared_ = false;
}

void ArmIdleSkip::EndRecord() {
    if (writable_cleared_) RestoreWritable();
}

void ArmIdleSkip::LogWord(const uint8_t* word) {
    uint32_t slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(word) >> 2) & (kLogSlots - 1u);
    for (;;) {
        LoggedWord& s = slots_[slot];
        if (s.stamp != stamp_) {
            if (logged_count_ == kMaxLoggedWords) {
                broken_ = true;
                return;
            }
            s.word  = word;
            s.stamp = stamp_;
            std::memcpy(&s.old, word, sizeof(s.old));
            logged_[logged_count_++] = static_cast<uint16_t>(slot);
            return;
        }
        if (s.word == word) return;
        slot = (slot + 1u) & (kLogSlots - 1u);
    }
}

bool ArmIdleSkip::StoresUnchanged() const {
    for (uint32_t i = 0; i < logged_count_; ++i) {
        const LoggedWord& s = slots_[logged_[i]];
        uint32_t now = 0;
        std::memcpy(&now, s.word, sizeof(now));
        if (now != s.old) return false;
    }
    return true;
}

}
