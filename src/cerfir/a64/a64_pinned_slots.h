#pragma once

#include <cstdint>
#include <vector>

#include <cerfengine/force_inline.h>
#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../cerfir_guest_model.h"

namespace cerfengine::cerfir {

inline constexpr uint32_t kA64MaxPinned = 16;

inline constexpr uint8_t kA64PinnedRegs[kA64MaxPinned] = {
    a64::kX19, a64::kX20, a64::kX21, a64::kX22, a64::kX23, a64::kX24, a64::kX25, a64::kX26,
    a64::kX3,  a64::kX4,  a64::kX5,  a64::kX6,  a64::kX7,  a64::kX8,  a64::kX14, a64::kX15};

inline constexpr uint32_t kA64CallerSavedRegs = 0x0003FFFFu;

constexpr uint32_t A64PinnedHostMask(uint32_t count) {
    uint32_t mask = 0;
    for (uint32_t i = 0; i < count && i < kA64MaxPinned; ++i) mask |= 1u << kA64PinnedRegs[i];
    return mask;
}

struct A64PinState {
    uint32_t dirty = 0;
    uint32_t stale = 0;
};

class A64PinnedSlots {
public:
    void Reset(const PinnedSlots& model);

    CERFENGINE_FORCEINLINE uint32_t HostMask() const { return host_mask_; }
    CERFENGINE_FORCEINLINE uint32_t Full() const { return full_; }
    CERFENGINE_FORCEINLINE A64PinState State() const { return state_; }

    CERFENGINE_FORCEINLINE int32_t Index(int32_t disp, uint32_t bytes) const {
        const int64_t off = int64_t{disp} - base_;
        if (off >= span_ || off + int64_t{bytes} <= 0) return -1;
        if (bytes != 4u || (off & 3) != 0) WidthFault(disp, bytes);
        return static_cast<int32_t>(off >> 2);
    }
    CERFENGINE_FORCEINLINE void CheckUnpinned(int32_t disp, uint32_t bytes) const {
        const int64_t off = int64_t{disp} - base_;
        if (off < span_ && off + int64_t{bytes} > 0) WidthFault(disp, bytes);
    }

    CERFENGINE_FORCEINLINE void Load(uint8_t*& c, uint8_t reg, int32_t disp) {
        const int32_t i = Index(disp, 4u);
        if (i < 0 || parked_) {
            a64::EmitLdrW(c, reg, a64::kStateReg, disp);
            return;
        }
        const uint8_t pin = Fresh(c, static_cast<uint32_t>(i));
        if (pin != reg) a64::EmitMovReg(c, reg, pin);
    }

    CERFENGINE_FORCEINLINE void Store(uint8_t*& c, uint8_t reg, int32_t disp) {
        const int32_t i = Index(disp, 4u);
        if (i < 0) {
            a64::EmitStrW(c, reg, a64::kStateReg, disp);
            return;
        }
        const uint8_t pin = Define(static_cast<uint32_t>(i));
        if (pin != reg) a64::EmitMovReg(c, pin, reg);
    }

    CERFENGINE_FORCEINLINE void StoreImm(uint8_t*& c, int32_t disp, uint32_t value, uint8_t scratch) {
        const int32_t i = Index(disp, 4u);
        if (i >= 0) {
            a64::EmitMovImm32(c, Define(static_cast<uint32_t>(i)), value);
            return;
        }
        if (value == 0u) {
            a64::EmitStrW(c, a64::kZr, a64::kStateReg, disp);
            return;
        }
        a64::EmitMovImm32(c, scratch, value);
        a64::EmitStrW(c, scratch, a64::kStateReg, disp);
    }

    CERFENGINE_FORCEINLINE uint8_t Source(uint8_t*& c, int32_t disp, uint8_t scratch) {
        const int32_t i = Index(disp, 4u);
        if (i < 0 || parked_) {
            a64::EmitLdrW(c, scratch, a64::kStateReg, disp);
            return scratch;
        }
        return Fresh(c, static_cast<uint32_t>(i));
    }

    CERFENGINE_FORCEINLINE uint8_t Target(int32_t disp, uint8_t scratch) {
        const int32_t i = Index(disp, 4u);
        return i < 0 ? scratch : Define(static_cast<uint32_t>(i));
    }

    CERFENGINE_FORCEINLINE void Commit(uint8_t*& c, uint8_t reg, int32_t disp) const {
        if (Index(disp, 4u) < 0) a64::EmitStrW(c, reg, a64::kStateReg, disp);
    }

    CERFENGINE_FORCEINLINE void Unstale(uint8_t*& c, int32_t disp) {
        const int32_t i = Index(disp, 4u);
        if (i >= 0 && !parked_) Fresh(c, static_cast<uint32_t>(i));
    }

    void Park(uint8_t*& c);
    void Unpark();
    void Flush(uint8_t*& c);
    void Sync(uint8_t*& c);
    void StaleCallerSaved();
    void EmitStores(uint8_t*& c, uint32_t mask) const;
    void EmitLoads(uint8_t*& c, uint32_t mask) const;

    CERFENGINE_FORCEINLINE void MarkUnreachable() { reachable_ = false; }
    void BranchToLabel(uint8_t*& c, uint32_t label);
    void BindLabel(uint8_t*& c, uint32_t label);
    void RecordGuard(const uint8_t* site);
    void BindGuard(uint8_t*& c, const uint8_t* site);

private:
    struct Record {
        uintptr_t   key;
        A64PinState state;
    };

    CERFENGINE_FORCEINLINE uint8_t Fresh(uint8_t*& c, uint32_t i) {
        const uint32_t bit = 1u << i;
        if ((state_.stale & bit) != 0u) {
            a64::EmitLdrW(c, kA64PinnedRegs[i], a64::kStateReg, base_ + static_cast<int32_t>(i * 4u));
            state_.stale &= ~bit;
        }
        return kA64PinnedRegs[i];
    }

    CERFENGINE_FORCEINLINE uint8_t Define(uint32_t i) {
        if (parked_) ParkedStoreFault();
        const uint32_t bit = 1u << i;
        state_.dirty |= bit;
        state_.stale &= ~bit;
        return kA64PinnedRegs[i];
    }

    void Reconcile(uint8_t*& c, const A64PinState& other);
    void Branch(uint8_t*& c, std::vector<Record>& records, uintptr_t key);
    void Bind(uint8_t*& c, std::vector<Record>& records, uintptr_t key);

    [[noreturn]] static void WidthFault(int32_t disp, uint32_t bytes);
    [[noreturn]] static void ParkedStoreFault();

    int32_t             base_      = 0;
    int64_t             span_      = 0;
    uint32_t            count_     = 0;
    uint32_t            full_      = 0;
    uint32_t            caller_    = 0;
    uint32_t            host_mask_ = 0;
    A64PinState         state_;
    bool                parked_    = false;
    bool                reachable_ = true;
    std::vector<Record> labels_;
    std::vector<Record> guards_;
};

}
