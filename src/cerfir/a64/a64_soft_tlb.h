#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <cerfengine/force_inline.h>

#include "../../a64_emit.h"
#include "../cerfir_guest_model.h"
#include "../soft_tlb_miss_sites.h"
#include "a64_pinned_slots.h"

namespace cerfengine::cerfir {

class A64SoftTlb {
public:
    static constexpr size_t kMissBytes     = 512;
    static constexpr size_t kStampMaxBytes = 512;
    static constexpr size_t RegionBytes(uint32_t privileges) {
        return privileges * kTlbAccessKinds * kMissBytes;
    }

    void Build(const GuestModel& guest, uint8_t* region);

    static constexpr uint8_t kPcReg      = a64::kX12;
    static constexpr uint8_t kSavedPcReg = a64::kX13;

    CERFENGINE_FORCEINLINE uint8_t* EmitLookup(uint8_t* cursor, uint32_t privilege,
                                               TlbAccess access, SoftTlbMissSites* misses) const {
        const Stamp& s = stamps_[privilege][static_cast<size_t>(access)];
        std::memcpy(cursor, s.bytes, s.length);
        misses->count = s.miss_count;
        for (uint32_t i = 0; i < s.miss_count; ++i) misses->site[i] = cursor + s.miss_offset[i];
        return cursor + s.length;
    }

    const void* MissTarget(uint32_t privilege, TlbAccess access) const {
        return miss_[privilege][static_cast<size_t>(access)];
    }
    bool KeepsPc() const { return model_.pc_slot != kNoSlot; }
    uint32_t PageBytes() const { return 1u << model_.page_shift; }

    uint8_t* EmitUserLookup(uint8_t* cursor, TlbAccess access, uint32_t guest_pc) const;
    uint8_t* EmitMissCall(uint8_t* cursor, TlbAccess access, uint32_t guest_pc) const;
    uint8_t* EmitWatchWords(uint8_t* cursor, const int32_t* word_delta, uint32_t count) const;

private:
    struct Stamp {
        uint8_t  bytes[kStampMaxBytes];
        size_t   length;
        size_t   miss_offset[SoftTlbMissSites::kMax];
        uint32_t miss_count;
    };

    uint8_t* EmitDirect(uint8_t* cursor, uint32_t privilege, TlbAccess access,
                        size_t* miss_offset, uint32_t* miss_count) const;
    uint8_t* EmitMiss(uint8_t* cursor, uint32_t privilege, TlbAccess access) const;
    uint8_t* EmitFold(uint8_t* cursor) const;
    uint8_t* EmitEntryChecks(uint8_t* cursor, uint32_t privilege, TlbAccess access, uint32_t tag_add,
                             uint8_t** misses, int* count) const;
    uint8_t* EmitWatchWrite(uint8_t* cursor, uint32_t privilege) const;
    [[nodiscard]] uint8_t* EmitSavePc(uint8_t* cursor) const;
    [[nodiscard]] uint8_t* EmitRestorePc(uint8_t* cursor) const;
    [[nodiscard]] uint8_t* EmitHelperCall(uint8_t* cursor, const void* helper) const;
    [[nodiscard]] uint8_t* EmitInlineHelperCall(uint8_t* cursor, const void* helper,
                                                uint32_t guest_pc) const;

    SoftTlbModel model_{};
    uint8_t      saved_[kA64MaxPinned]{};
    uint32_t     saved_count_ = 0;
    uint32_t     set_shift_ = 0;
    uint8_t*     miss_[kMaxPrivileges][kTlbAccessKinds]{};
    Stamp        stamps_[kMaxPrivileges][kTlbAccessKinds]{};
};

}
