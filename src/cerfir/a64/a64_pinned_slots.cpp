#include "a64_pinned_slots.h"

#include <cstdio>

#include "../../emit_fault.h"

namespace cerfengine::cerfir {

void A64PinnedSlots::Reset(const PinnedSlots& model) {
    if (model.count > kA64MaxPinned) {
        throw EmitFault("CerfIR A64 lowering: the guest pins more slots than the host has pinned registers");
    }
    base_      = model.base;
    count_     = model.count;
    span_      = int64_t{count_} * 4;
    full_      = count_ == 0u ? 0u : (0xFFFFFFFFu >> (32u - count_));
    host_mask_ = A64PinnedHostMask(count_);
    caller_    = 0u;
    for (uint32_t i = 0; i < count_; ++i) {
        if ((kA64CallerSavedRegs & (1u << kA64PinnedRegs[i])) != 0u) caller_ |= 1u << i;
    }
    state_     = A64PinState{};
    parked_    = false;
    reachable_ = true;
    labels_.clear();
    guards_.clear();
}

void A64PinnedSlots::EmitStores(uint8_t*& c, uint32_t mask) const {
    for (uint32_t i = 0; i < count_;) {
        const uint32_t bit  = 1u << i;
        const int32_t  disp = base_ + static_cast<int32_t>(i * 4u);
        if ((mask & bit) == 0u) {
            ++i;
            continue;
        }
        if (i + 1u < count_ && (mask & (bit << 1)) != 0u && a64::PairWOffsetFits(disp)) {
            a64::EmitStpWOffset(c, kA64PinnedRegs[i], kA64PinnedRegs[i + 1u], a64::kStateReg, disp);
            i += 2u;
            continue;
        }
        a64::EmitStrW(c, kA64PinnedRegs[i], a64::kStateReg, disp);
        ++i;
    }
}

void A64PinnedSlots::EmitLoads(uint8_t*& c, uint32_t mask) const {
    for (uint32_t i = 0; i < count_;) {
        const uint32_t bit  = 1u << i;
        const int32_t  disp = base_ + static_cast<int32_t>(i * 4u);
        if ((mask & bit) == 0u) {
            ++i;
            continue;
        }
        if (i + 1u < count_ && (mask & (bit << 1)) != 0u && a64::PairWOffsetFits(disp)) {
            a64::EmitLdpWOffset(c, kA64PinnedRegs[i], kA64PinnedRegs[i + 1u], a64::kStateReg, disp);
            i += 2u;
            continue;
        }
        a64::EmitLdrW(c, kA64PinnedRegs[i], a64::kStateReg, disp);
        ++i;
    }
}

void A64PinnedSlots::Park(uint8_t*& c) {
    if (parked_) throw EmitFault("CerfIR A64 lowering: a call inside the flush window of another call");
    EmitStores(c, state_.dirty);
    state_.dirty = 0u;
    parked_      = true;
}

void A64PinnedSlots::Unpark() {
    if (!parked_) throw EmitFault("CerfIR A64 lowering: a call return without its flush");
    parked_      = false;
    state_.stale = full_;
}

void A64PinnedSlots::Flush(uint8_t*& c) {
    if (parked_) throw EmitFault("CerfIR A64 lowering: a pinned-register flush inside the flush window of a call");
    EmitStores(c, state_.dirty);
    state_.dirty = 0u;
}

void A64PinnedSlots::Sync(uint8_t*& c) {
    Flush(c);
    EmitLoads(c, state_.stale);
    state_.stale = 0u;
}

void A64PinnedSlots::StaleCallerSaved() {
    if ((state_.dirty & caller_) != 0u) {
        throw EmitFault("CerfIR A64 lowering: an inline helper call clobbers a modified pinned register");
    }
    state_.stale |= caller_;
}

void A64PinnedSlots::Reconcile(uint8_t*& c, const A64PinState& other) {
    const uint32_t load  = other.dirty & state_.stale;
    const uint32_t store = other.stale & state_.dirty;
    if (load != 0u) {
        EmitLoads(c, load);
        state_.stale &= ~load;
    }
    if (store != 0u) {
        EmitStores(c, store);
        state_.dirty &= ~store;
    }
}

void A64PinnedSlots::Branch(uint8_t*& c, std::vector<Record>& records, uintptr_t key) {
    if (parked_) throw EmitFault("CerfIR A64 lowering: a branch inside the flush window of a call");
    if (!reachable_) return;
    for (Record& r : records) {
        if (r.key != key) continue;
        Reconcile(c, r.state);
        r.state = A64PinState{r.state.dirty | state_.dirty, r.state.stale | state_.stale};
        return;
    }
    records.push_back(Record{key, state_});
}

void A64PinnedSlots::Bind(uint8_t*& c, std::vector<Record>& records, uintptr_t key) {
    if (parked_) throw EmitFault("CerfIR A64 lowering: a label inside the flush window of a call");
    for (size_t n = 0; n < records.size(); ++n) {
        if (records[n].key != key) continue;
        const A64PinState other = records[n].state;
        records[n]              = records.back();
        records.pop_back();
        if (!reachable_) {
            state_     = other;
            reachable_ = true;
            return;
        }
        Reconcile(c, other);
        state_ = A64PinState{state_.dirty | other.dirty, state_.stale | other.stale};
        return;
    }
}

void A64PinnedSlots::BranchToLabel(uint8_t*& c, uint32_t label) {
    Branch(c, labels_, label);
}

void A64PinnedSlots::BindLabel(uint8_t*& c, uint32_t label) {
    Bind(c, labels_, label);
}

void A64PinnedSlots::RecordGuard(const uint8_t* site) {
    if (parked_) throw EmitFault("CerfIR A64 lowering: a guard inside the flush window of a call");
    if (!reachable_) return;
    const uintptr_t key = reinterpret_cast<uintptr_t>(site);
    for (const Record& r : guards_) {
        if (r.key == key) throw EmitFault("CerfIR A64 lowering: a guard site is recorded twice");
    }
    guards_.push_back(Record{key, state_});
}

void A64PinnedSlots::BindGuard(uint8_t*& c, const uint8_t* site) {
    Bind(c, guards_, reinterpret_cast<uintptr_t>(site));
}

void A64PinnedSlots::WidthFault(int32_t disp, uint32_t bytes) {
    char text[128];
    std::snprintf(text, sizeof(text),
                  "CerfIR A64 lowering: a %u-byte slot access at +%d overlaps a pinned slot",
                  static_cast<unsigned>(bytes), static_cast<int>(disp));
    throw EmitFault(text);
}

void A64PinnedSlots::ParkedStoreFault() {
    throw EmitFault("CerfIR A64 lowering: a pinned slot store inside the flush window of a call");
}

}
