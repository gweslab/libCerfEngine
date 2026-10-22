#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/byte_order.h>

#include <cerfengine/force_inline.h>
#include "../cerfir_guest_model.h"
#include "x86_copy_lanes.h"

namespace cerfengine::cerfir {

class X86SoftTlb {
public:
    static constexpr size_t kMissBytes     = 256;
    static constexpr size_t kStampMaxBytes = 512;
    static constexpr size_t RegionBytes(uint32_t privileges) {
        return privileges * kTlbAccessKinds * kMissBytes;
    }

    void Build(const GuestModel& guest, uint8_t* region);

    CERFENGINE_FORCEINLINE uint8_t* EmitLookup(uint8_t* cursor, uint32_t privilege,
                                               TlbAccess access) const {
        const Stamp& s = stamps_[privilege][static_cast<size_t>(access)];
        X86CopyLanes(cursor, s.bytes, s.length);
        uint8_t* rel = cursor + s.call_rel;
        le::Put32(rel, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.target) -
                                             reinterpret_cast<uintptr_t>(rel + 4)));
        return cursor + s.length;
    }

    uint8_t* EmitUserLookup(uint8_t* cursor, TlbAccess access) const;
    uint8_t* EmitMissCall(uint8_t* cursor, TlbAccess access) const;

private:
    struct Stamp {
        uint8_t     bytes[kStampMaxBytes];
        size_t      length;
        size_t      call_rel;
        const void* target;
    };

    uint8_t* EmitDirect(uint8_t* cursor, uint32_t privilege, TlbAccess access,
                        size_t* call_rel) const;
    uint8_t* EmitMiss(uint8_t* cursor, uint32_t privilege, TlbAccess access) const;
    uint8_t* EmitFold(uint8_t* cursor) const;
    [[nodiscard]] uint8_t* EmitHelperCall(uint8_t* cursor, const void* helper) const;
    [[nodiscard]] uint8_t* EmitInlineHelperCall(uint8_t* cursor, const void* helper) const;

    SoftTlbModel model_{};
    uint32_t     set_shift_ = 0;
    uint8_t*     miss_[kMaxPrivileges][kTlbAccessKinds]{};
    Stamp        stamps_[kMaxPrivileges][kTlbAccessKinds]{};
};

}
