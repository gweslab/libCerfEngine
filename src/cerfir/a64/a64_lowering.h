#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../../a64_emit.h"
#include "../cerfir_block.h"
#include "../cerfir_guest_model.h"
#include "a64_memory_tails.h"
#include "a64_pinned_slots.h"
#include "a64_reg_alloc.h"

namespace cerfengine::cerfir {

class A64SoftTlb;

struct A64GuestTarget {
    const GuestModel* model      = nullptr;
    const BlockLink*  link       = nullptr;
    const A64SoftTlb* tlb        = nullptr;
    const void*       entry_stub = nullptr;
};

struct A64WideHalf {
    Ref      ref;
    bool     imm;
    uint32_t value;
};

enum class A64FlagKind : uint8_t { kNone, kNzcv, kReg };

struct A64FlagSource {
    Ref         producer = kNoRef;
    A64FlagKind kind[4]  = {};
    uint8_t     where[4] = {};
};

constexpr uint32_t kA64FlagIndexN = 0;
constexpr uint32_t kA64FlagIndexZ = 1;
constexpr uint32_t kA64FlagIndexC = 2;
constexpr uint32_t kA64FlagIndexV = 3;

struct A64ArenaFull {};

class A64Lowering {
public:
    static constexpr ptrdiff_t kOpReserveBytes = 4096;
    static constexpr uint32_t  kCycleAddLimit  = 0x1000u;
    static constexpr size_t    kEntryStubBytes = 64;

    A64Lowering() : regs_(cursor_, pins_) {}

    static const void* BuildEntryStub(const PinnedSlots& pinned, uint8_t* region);
    static uint8_t     PinnedHostRegister(uint32_t index) {
        return index < kA64MaxPinned ? kA64PinnedRegs[index] : 0xFFu;
    }
    A64Lowering(const A64Lowering&)            = delete;
    A64Lowering& operator=(const A64Lowering&) = delete;

    void        Begin(Block& block, uint8_t* code, uint8_t* code_end, const A64GuestTarget& target);
    void        Lower(Ref ref);
    uint8_t*    EmitCycleAdd(uint32_t cycles);
    void        Drop(Ref r);

    /* ARM DDI 0487 M.d C6.2.5 ADD (immediate): imm12 occupies bits 21:10. */
    static CERFENGINE_FORCEINLINE void PatchCycleAdd(uint8_t* site, uint32_t total) {
        const uint32_t insn = le::U32(site);
        le::Put32(site, (insn & ~(0xFFFu << 10)) | ((total & 0xFFFu) << 10));
    }
    void        DropHalf(Ref r, bool hi);
    size_t      Finish();

    CERFENGINE_FORCEINLINE void Undefine(Ref r) { regs_.Undefine(r); }

    void     EmitSetSlot(Ref value, uint32_t disp, bool sext);
    void     EmitSetSlotConst(uint32_t disp, uint32_t value, bool sext);
    void     EmitLabel(uint32_t label);
    void     EmitBranch(Ref cond, uint32_t label, bool if_true);
    void     EmitBranchSlots(const SlotTest& test, uint32_t label);
    uint8_t* EmitGuardBranch(const SlotTest& test);
    void     EmitSetSlotTest(const SlotTest& test, uint32_t dest);
    void     EmitGuardBind(uint8_t* site);
    void     EmitExitDirect(uint32_t target, uint16_t flags);
    void     EmitExitIndirect();
    void     EmitExitDispatcher();
    void     EmitFusedAlu(Op op, Ref a, Ref b, uint32_t mask, uint32_t dest, bool sext);
    bool     EmitSlotImm(Op op, uint32_t a_disp, uint32_t imm, uint32_t dest, bool sext);
    void     EmitSlotSlot(Op op, uint32_t a_disp, uint32_t b_disp, uint32_t dest, bool sext);
    void     EmitSlotImmFlags(Op op, uint32_t a_disp, uint32_t imm, uint32_t mask, uint32_t dest);
    void     EmitSlotSlotFlags(Op op, uint32_t a_disp, uint32_t b_disp, uint32_t mask, uint32_t dest);
    bool     EmitShiftedSlotAlu(Op op, uint32_t a_disp, uint32_t b_disp, Op shift, uint32_t amount,
                                uint32_t mask, uint32_t dest);
    void     EmitLoad(Ref address, Ref writeback, uint32_t attr);
    void     EmitStore(Ref address, Ref value, Ref writeback, uint32_t attr);
    void     EmitTransferWords(uint32_t attr, uint32_t list);
    void     EmitCallDirect(const void* fn, CallConv conv, uint32_t num_args, Ref a, Ref b, Ref c,
                            const CallArg* extra, uint32_t num_extra, bool store_pc, uint32_t pc);
    void     EmitCallConst(const CallSpec& spec, uint32_t num_args, CallArg a0, CallArg a1,
                           CallArg a2, CallArg a3);
    CERFENGINE_NOINLINE void EmitCallConstBusy(const CallSpec& spec, uint32_t num_args, CallArg a0,
                                               CallArg a1, CallArg a2, CallArg a3);
    void     EmitSlotCopy32(uint32_t dest, uint32_t src, bool sext);
    void     EmitSlotCopy64(uint32_t dest, uint32_t src);
    bool     EmitLeanLoad(const MemAttr& m);
    bool     EmitLeanStore(const MemAttr& m, Ref value);
    bool     EmitSimpleLoad(uint32_t width, bool sign, int32_t base_slot, int32_t offset,
                            int32_t result_slot, int32_t result_slot2, uint32_t guest_pc);
    bool     EmitSimpleStore(uint32_t width, int32_t base_slot, int32_t offset, Ref value,
                             uint32_t guest_pc);

    const Block&          GetBlock() const { return *block_; }
    uint8_t*&             Cursor() { return cursor_; }
    bool                  HasRoom() const { return code_end_ - cursor_ >= kOpReserveBytes; }
    bool                  HasRoomFor(ptrdiff_t bytes) const { return code_end_ - cursor_ >= bytes; }
    A64RegAlloc&          Regs() { return regs_; }
    A64PinnedSlots&       Pins() { return pins_; }
    A64MemoryTails&       Tails() { return tails_; }
    const A64GuestTarget& Target() const { return *target_; }
    A64FlagSource&        FlagSource() { return flag_source_; }

    CERFENGINE_FORCEINLINE uint8_t WantedFlags(Ref producer) const {
        const uint32_t aux16 = block_->At(producer).aux16;
        return static_cast<uint8_t>((aux16 | (aux16 >> 4)) & 0x0Fu);
    }

    void ClearFlagSource() {
        flag_source_.producer = kNoRef;
        std::memset(flag_source_.kind, 0, sizeof(flag_source_.kind));
    }

    void SetFlag(Ref producer, uint32_t index, A64FlagKind kind, uint8_t where) {
        if (flag_source_.producer != producer) {
            ClearFlagSource();
            flag_source_.producer = producer;
        }
        flag_source_.kind[index]  = kind;
        flag_source_.where[index] = where;
    }

    CERFENGINE_FORCEINLINE void AddLabelFixup(uint32_t label, uint8_t* site) {
        fixups_.push_back(Fixup{label, site});
    }
    CERFENGINE_FORCEINLINE void BindLabel(uint32_t label) {
        for (size_t i = fixups_.size(); i-- > 0u;) {
            if (fixups_[i].label != label) continue;
            a64::FixupLabel(fixups_[i].site, cursor_);
            fixups_[i] = fixups_.back();
            fixups_.pop_back();
        }
    }
    static Ref  HiHalf(Ref value) { return Block::HiHalf(value); }
    A64WideHalf Half(Ref value, bool hi) const;

private:
    struct Fixup {
        uint32_t label;
        uint8_t* site;
    };

    void LowerValueOp(uint32_t i);
    void StoreDest(Ref value, int32_t disp);
    void SkipDead(const Inst& inst);
    void ConsumeArg(Ref r);
    void EmitSlotAluFast(Op op, uint32_t a_disp, const A64RegAlloc::Operand& b, uint32_t dest,
                         bool sext);
    void EmitSlotAluFlags(Op op, uint32_t a_disp, const A64RegAlloc::Operand& b, uint32_t mask,
                          uint32_t dest);

    uint8_t*              cursor_     = nullptr;
    uint8_t*              code_start_ = nullptr;
    uint8_t*              code_end_   = nullptr;
    A64PinnedSlots        pins_;
    A64RegAlloc           regs_;
    Block*                block_  = nullptr;
    ValueInfo*            info_   = nullptr;
    const A64GuestTarget* target_ = nullptr;
    A64MemoryTails        tails_;
    std::vector<Fixup>    fixups_;
    A64FlagSource         flag_source_;
};

}
