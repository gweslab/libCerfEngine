#pragma once

#include <cstdint>

#include <cerfengine/force_inline.h>
#include "mips_decoded_insn.h"
#include "mips_opcode.h"

namespace cerfengine {

class MipsDecoder {
public:
    void Configure(bool has_fpu, bool has_llsc, bool has_mips4, bool has_vr41xx_power_modes,
                   bool has_64bit, bool has_eret, bool has_rfe, bool has_mips16,
                   bool has_tx39_multiply_add, bool has_cop1_cop2_unusable);

    CERFENGINE_FORCEINLINE bool Decode(uint32_t word, uint32_t pc, MipsDecodedInsn* d) const {
        const uint32_t op = word >> 26;
        d->guest_address  = pc;
        d->raw            = word;
        d->length         = 4u;
        d->op             = static_cast<uint8_t>(op);
        d->rs             = static_cast<uint8_t>((word >> 21) & 0x1fu);
        d->rt             = static_cast<uint8_t>((word >> 16) & 0x1fu);
        d->rd             = static_cast<uint8_t>((word >> 11) & 0x1fu);
        d->sa             = static_cast<uint8_t>((word >> 6) & 0x1fu);
        d->funct          = static_cast<uint8_t>(word & 0x3fu);
        d->imm            = word & 0xffffu;
        d->target         = word & 0x03ffffffu;
        d->ends_block     = 0;
        Entry e;
        if (op == MipsOp::kSPECIAL) {
            e = word == 0u ? kNopEntry : special_[word & 0x3fu];
        } else if (op == MipsOp::kREGIMM) {
            e = regimm_[d->rt];
        } else {
            e = primary_[op];
            if ((e.flags & kRare) != 0u) return DecodeRare(d);
        }
        d->kind      = e.kind;
        d->is_branch = static_cast<uint8_t>(e.flags & kBranch);
        d->is_likely = static_cast<uint8_t>((e.flags & kLikely) >> 1);
        return (e.flags & kRecognized) != 0u;
    }

private:
    struct Entry {
        MipsInsnKind kind  = MipsInsnKind::kUndefined;
        uint8_t      flags = 0;
    };

    static constexpr uint8_t kBranch     = 1u << 0;
    static constexpr uint8_t kLikely     = 1u << 1;
    static constexpr uint8_t kRecognized = 1u << 2;
    static constexpr uint8_t kRare       = 1u << 3;
    static constexpr Entry   kNopEntry{MipsInsnKind::kNop, kRecognized};

    Entry ClassifyPrimary(uint32_t op) const;
    Entry ClassifySpecial(uint32_t funct) const;
    Entry ClassifyRegimm(uint32_t rt) const;
    bool  DecodeRare(MipsDecodedInsn* d) const;

    Entry primary_[64]{};
    Entry special_[64]{};
    Entry regimm_[32]{};

    bool has_fpu_                = false;
    bool has_llsc_               = false;
    bool has_mips4_              = false;
    bool has_vr41xx_power_modes_ = false;
    bool has_64bit_              = false;
    bool has_eret_               = false;
    bool has_rfe_                = false;
    bool has_mips16_             = false;
    bool has_tx39_multiply_add_  = false;
    bool has_cop1_cop2_unusable_ = false;
};

}
