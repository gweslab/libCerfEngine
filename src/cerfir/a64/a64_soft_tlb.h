#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <cerfengine/byte_order.h>
#include <cerfengine/force_inline.h>

#include "../../a64_emit.h"
#include "../cerfir_guest_model.h"
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

    CERFENGINE_FORCEINLINE uint8_t* EmitLookup(uint8_t* cursor, uint32_t privilege,
                                               TlbAccess access, uint8_t** call_site) const {
        const Stamp& s = stamps_[privilege][static_cast<size_t>(access)];
        std::memcpy(cursor, s.bytes, s.length);
        uint8_t* const call = cursor + s.call_offset;
        le::Put32(call, 0x94000000u | a64::Imm26(call, s.target));
        *call_site = call;
        return cursor + s.length;
    }

    const void* MissTarget(uint32_t privilege, TlbAccess access) const {
        return stamps_[privilege][static_cast<size_t>(access)].target;
    }

    uint8_t* EmitUserLookup(uint8_t* cursor, TlbAccess access) const;
    uint8_t* EmitMissCall(uint8_t* cursor, TlbAccess access) const;

private:
    struct Stamp {
        uint8_t     bytes[kStampMaxBytes];
        size_t      length;
        size_t      call_offset;
        const void* target;
    };

    uint8_t* EmitDirect(uint8_t* cursor, uint32_t privilege, TlbAccess access,
                        size_t* call_offset) const;
    uint8_t* EmitMiss(uint8_t* cursor, uint32_t privilege, TlbAccess access) const;
    uint8_t* EmitFold(uint8_t* cursor) const;
    uint8_t* EmitEntryChecks(uint8_t* cursor, uint32_t privilege, TlbAccess access, uint32_t tag_add,
                             uint8_t** misses, int* count) const;
    uint8_t* EmitWatchWrite(uint8_t* cursor, uint32_t privilege) const;
    [[nodiscard]] uint8_t* EmitHelperCall(uint8_t* cursor, const void* helper) const;
    [[nodiscard]] uint8_t* EmitInlineHelperCall(uint8_t* cursor, const void* helper) const;

    SoftTlbModel model_{};
    uint8_t      saved_[kA64MaxPinned]{};
    uint32_t     saved_count_ = 0;
    uint32_t     set_shift_ = 0;
    uint8_t*     miss_[kMaxPrivileges][kTlbAccessKinds]{};
    Stamp        stamps_[kMaxPrivileges][kTlbAccessKinds]{};
};

}
