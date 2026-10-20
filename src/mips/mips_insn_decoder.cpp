#include "mips_insn_decoder.h"

#include <cerfengine/byte_order.h>
#include <cerfengine/engine.h>
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mips/mips_mmu.h>
#include <cerfengine/physical_map.h>
#include <cerfengine/force_inline.h>
#include "../zero_fill.h"

namespace cerfengine {

CERFENGINE_REGISTER(MipsInsnDecoder);

void MipsInsnDecoder::OnReady() {
    cpu_state_ = engine_.Get<MipsCpu>().State();
    mmu_       = &engine_.Get<MipsMmu>();
    map_       = &engine_.Get<PhysicalMap>();

    auto& cfg = engine_.GetSetup().mips;
    const bool has_64bit = cfg.isa_level != cerfengine::MipsIsaLevel::kMips1;
    const bool has_eret  = cfg.isa_level != cerfengine::MipsIsaLevel::kMips1;
    const bool has_rfe   = cfg.isa_level == cerfengine::MipsIsaLevel::kMips1;
    decoder_.Configure(cfg.fpu, cfg.llsc,
                       cfg.isa_level == cerfengine::MipsIsaLevel::kMips4,
                       cfg.vr41xx_power_modes,
                       has_64bit, has_eret, has_rfe, cfg.mips16,
                       cfg.tx39_multiply_add, cfg.cop1_cop2_unusable);
    m16_decoder_.Configure(has_64bit);
}

CERFENGINE_NOINLINE bool MipsInsnDecoder::FetchPageOf(MipsBlockContext* ctx, uint32_t va,
                                                      uint32_t page_mask, bool record_fault,
                                                      FetchPage& page) {
    const uint32_t      offset = va & page_mask;
    uint32_t            frame  = 0;
    const MipsTlbResult res =
        mmu_->TranslateAs(ctx->privilege, cpu_state_, va, MipsAccess::kFetch, &frame);
    if (res != MipsTlbResult::kMatch) {
        if (record_fault) {
            ctx->fetch_fault_pending = 1;
            ctx->fetch_fault_va      = va;
            ctx->fetch_fault_res     = res;
        }
        return false;
    }
    uint8_t* const mapped = map_->Read(frame);
    if (mapped == nullptr) {
        ctx->fetch_unbacked_va = va;
        ctx->fetch_unbacked_pa = frame;
        return false;
    }
    page.va   = va & ~page_mask;
    page.pa   = frame - offset;
    page.host = mapped - offset;
    return true;
}

CERFENGINE_FORCEINLINE bool MipsInsnDecoder::Fetch(MipsBlockContext* ctx, uint32_t va,
                                                   uint32_t page_mask, bool record_fault,
                                                   FetchPage& page, const uint8_t** host,
                                                   uint32_t* pa) {
    if ((va & ~page_mask) != page.va && !FetchPageOf(ctx, va, page_mask, record_fault, page)) {
        return false;
    }
    const uint32_t offset = va & page_mask;
    *host                 = page.host + offset;
    *pa                   = page.pa + offset;
    return true;
}

void MipsInsnDecoder::Decode16(MipsBlockContext* ctx, uint32_t guest_pc) {
    guest_pc &= ~0x1u;
    const uint32_t block_start   = guest_pc;
    const uint32_t page_off_mask = (1u << cpu_state_->min_page_shift) - 1u;
    const uint32_t page_end      = (guest_pc & ~page_off_mask) + page_off_mask + 1u;

    uint32_t  i = 0;
    bool      delay_pending = false;
    bool      slot_decoded  = false;
    uint32_t  jump_pc       = 0;
    FetchPage page;
    for (; i < kMaxMipsInsnPerBlock && (guest_pc < page_end || delay_pending); ++i) {
        MipsDecodedInsn& insn = ctx->insns[i];
        ZeroFill(insn);

        uint16_t       hw0 = 0, hw1 = 0;
        uint32_t       pa0 = 0, pa1 = 0;
        const uint8_t* host = nullptr;
        if (!Fetch(ctx, guest_pc, page_off_mask, true, page, &host, &pa0)) {
            break;
        }
        hw0 = le::U16(host);
        if (guest_pc >= page_end && ctx->tail_split == 0u) {
            ctx->tail_split   = guest_pc - block_start;
            ctx->tail_page_pa = pa0;
        }
        const bool wide = Mips16Decoder::Needs4Bytes(hw0);
        if (wide) {
            const uint32_t va1 = guest_pc + 2u;
            if (!Fetch(ctx, va1, page_off_mask, true, page, &host, &pa1)) {
                break;
            }
            hw1 = le::U16(host);
            if ((va1 & ~page_off_mask) != (guest_pc & ~page_off_mask)) {
                ctx->tail_split   = va1 - block_start;
                ctx->tail_page_pa = pa1;
            }
        }

        /* U15509EJ2V0UM Table 3-12 p67: base PC = the insn's own PC, the
           EXTEND's PC, or - in a jump delay slot - the owning JR/JALR/JAL's PC. */
        const uint32_t base_pc = delay_pending ? jump_pc : guest_pc;

        uint32_t synth = 0;
        switch (m16_decoder_.Decode(hw0, hw1, guest_pc, base_pc, &insn, &synth)) {
            case Mips16DecodeKind::kSynth:
                if (!decoder_.Decode(synth, guest_pc, &insn)) {
                    engine_.Panic("MipsInsnDecoder::Decode16: synthesized word 0x%08X "
                                  "for halfword 0x%04X at 0x%08X is not decodable",
                                  synth, hw0, guest_pc);
                }
                break;
            case Mips16DecodeKind::kDirect:
                insn.guest_address = guest_pc;
                break;
            case Mips16DecodeKind::kReserved:
                insn.guest_address = guest_pc;
                insn.kind          = MipsInsnKind::kUndefined;
                break;
        }
        insn.length = wide ? 4u : 2u;
        insn.raw    = (static_cast<uint32_t>(hw1) << 16) | hw0;

        if (delay_pending && (wide || insn.is_branch || insn.ends_block)) {
            engine_.Halt(cerfengine::HaltReason::kUnpredictable,
                         "MipsInsnDecoder::Decode16: illegal jump-delay-slot insn 0x%08X at "
                         "0x%08X (wide=%d branch=%d ends=%d)",
                         insn.raw, guest_pc, wide ? 1 : 0, insn.is_branch, insn.ends_block);
        }

        if (delay_pending) {
            slot_decoded = true;
            ++i;
            break;
        }
        if (insn.ends_block) {
            ++i;
            break;
        }
        if (insn.is_branch) {
            delay_pending = true;
            jump_pc       = guest_pc;
        }
        guest_pc += insn.length;
    }

    ctx->num_insns = CloseDelaySlot(ctx, i, delay_pending, slot_decoded);
}

uint32_t MipsInsnDecoder::CloseDelaySlot(MipsBlockContext* ctx, uint32_t count,
                                         bool delay_pending, bool slot_decoded) {
    if (!delay_pending || slot_decoded) return count;
    if (count == kMaxMipsInsnPerBlock) return count - 1u;
    ctx->uncacheable = 1;
    return count;
}

void MipsInsnDecoder::Decode(MipsBlockContext* ctx, uint32_t guest_pc) {
    guest_pc &= ~0x3u;
    const uint32_t page_off_mask = (1u << cpu_state_->min_page_shift) - 1u;
    const uint32_t page_end = (guest_pc & ~page_off_mask) + page_off_mask + 1u;

    const uint32_t block_start = guest_pc;
    uint32_t  i = 0;
    bool      delay_pending = false;
    bool      slot_decoded  = false;
    FetchPage page;
    for (; i < kMaxMipsInsnPerBlock && (guest_pc < page_end || delay_pending);
         ++i, guest_pc += 4) {
        MipsDecodedInsn& insn = ctx->insns[i];

        uint32_t       pa   = 0;
        const uint8_t* host = nullptr;
        if (!Fetch(ctx, guest_pc, page_off_mask, false, page, &host, &pa)) {
            break;
        }
        if (guest_pc >= page_end) {
            ctx->tail_split   = guest_pc - block_start;
            ctx->tail_page_pa = pa;
        }

        decoder_.Decode(le::U32(host), guest_pc, &insn);
        if (insn.kind == MipsInsnKind::kUndefined) insn.ends_block = 1;

        if (delay_pending) {
            slot_decoded = true;
            ++i;
            break;
        }
        if (insn.ends_block) {
            ++i;
            break;
        }
        if (insn.is_branch) {
            delay_pending = true;
        }
    }

    ctx->num_insns = CloseDelaySlot(ctx, i, delay_pending, slot_decoded);
}

}
