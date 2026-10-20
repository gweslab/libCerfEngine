#include <bit>

#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/cpu_state.h>

#include "../../lowest_set_bit.h"
#include "../arm_emit_services.h"
#include "../arm_transfer_rules.h"
#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::kNoRef;
using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

struct TransferShape {
    cerfir::AlignPolicy policy;
    bool                writeback_on_fault;
    Ref                 fault_wb = kNoRef;
    Ref                 cpu      = kNoRef;
    bool                user_regs;
    bool                first;
};

struct TransferPlan {
    uint32_t rn_disp;
    uint32_t delta;
    uint32_t base_off;
    bool     defer_rn;
    bool     final_wb;
    bool     ignore_low;
};

cerfir::MemAttr& NewAccess(ArmIrContext& c, TransferShape& s, uint32_t* attr) {
    cerfir::MemAttr& m   = c.b->NewMemAttr(attr);
    m.width_bytes        = 4u;
    m.complete           = true;
    m.back_out           = s.first;
    m.align              = s.policy;
    m.writeback_on_fault = s.writeback_on_fault;
    m.writeback_slot     = s.writeback_on_fault ? ArmGprDisp(c.d->rn) : cerfir::kNoSlot;
    ArmIrMemOrigin(c, m);
    s.first              = false;
    return m;
}

void LoadRegister(ArmIrContext& c, TransferShape& s, uint32_t reg, Ref address) {
    cerfir::Builder& b    = *c.b;
    uint32_t         attr = 0;
    cerfir::MemAttr& m    = NewAccess(c, s, &attr);
    if (reg == ArmGpr::kR15) {
        m.result_slot = ArmGprDisp(reg);
        b.Load(Type::kI32, address, s.fault_wb, attr);
        return;
    }
    const Ref              value = b.Load(Type::kI32, address, s.fault_wb, attr);
    const cerfir::CallDesc write{reinterpret_cast<const void*>(&ArmCpu::WriteUserRegHelper),
                                 cerfir::CallConv::kCdecl, 3u};
    b.Call(write, Type::kVoid, s.cpu, b.Const32(reg), value);
}

void StoreRegister(ArmIrContext& c, TransferShape& s, uint32_t reg, Ref address) {
    cerfir::Builder&   b = *c.b;
    const DecodedInsn& d = *c.d;
    Ref                value;
    if (reg == ArmGpr::kR15) {
        value = b.Const32(d.guest_address + c.block->emit->ProcessorConfig()->pc_store_offset);
    } else {
        const cerfir::CallDesc read{reinterpret_cast<const void*>(&ArmCpu::ReadUserRegHelper),
                                    cerfir::CallConv::kCdecl, 2u};
        value = b.Call(read, Type::kI32, s.cpu, b.Const32(reg), kNoRef);
    }
    uint32_t attr = 0;
    NewAccess(c, s, &attr);
    b.Store(address, value, s.fault_wb, attr);
}

void TransferUserRegisters(ArmIrContext& c, TransferShape& s, const TransferPlan& p) {
    const DecodedInsn& d    = *c.d;
    cerfir::Builder&   b    = *c.b;
    const uint32_t     list = d.register_list;
    const bool         load = d.l != 0u;
    s.cpu                   = b.ConstPtr(c.block->emit->Cpu());

    const uint32_t direct = static_cast<uint32_t>(std::popcount(list)) - (p.defer_rn ? 1u : 0u);
    const Ref      base   = b.GetSlot(Type::kI32, p.rn_disp);
    b.Share(base, 1u + (s.writeback_on_fault ? 1u : 0u) + (p.defer_rn ? 1u : 0u) +
                      (p.final_wb ? 1u : 0u));
    Ref      start       = p.base_off != 0u ? b.Op2(Op::kAdd, base, b.Const32(p.base_off)) : base;
    uint32_t align_fault = kArmIrNoLabel;
    if (p.ignore_low) {
        start = b.Op2(Op::kAnd, start, b.Const32(0xFFFFFFFCu));
        b.Share(start, direct);
    } else {
        b.Share(start, direct + 1u);
        align_fault = b.NewLabel();
        b.Branch(b.Compare(Op::kCmpNe, b.Op2(Op::kAnd, start, b.Const32(3u)), b.Const32(0u)),
                 align_fault, true);
    }
    if (s.writeback_on_fault) {
        s.fault_wb = b.Op2(Op::kAdd, base, b.Const32(p.delta));
        b.Share(s.fault_wb, static_cast<uint32_t>(std::popcount(list)));
    }

    uint32_t offset      = 0u;
    uint32_t rn_word_off = 0u;
    for (uint32_t i = 0; i < 16u; ++i) {
        if (((list >> i) & 1u) == 0u) continue;
        if (p.defer_rn && i == d.rn) {
            rn_word_off = p.base_off + offset;
            offset += 4u;
            continue;
        }
        const Ref address = offset != 0u ? b.Op2(Op::kAdd, start, b.Const32(offset)) : start;
        if (load) {
            LoadRegister(c, s, i, address);
        } else {
            StoreRegister(c, s, i, address);
        }
        offset += 4u;
    }
    if (p.defer_rn) {
        Ref address = rn_word_off != 0u ? b.Op2(Op::kAdd, base, b.Const32(rn_word_off)) : base;
        if (p.ignore_low) address = b.Op2(Op::kAnd, address, b.Const32(0xFFFFFFFCu));
        LoadRegister(c, s, d.rn, address);
    }
    if (p.final_wb) b.SetSlot(p.rn_disp, b.Op2(Op::kAdd, base, b.Const32(p.delta)));

    if (align_fault == kArmIrNoLabel) return;
    const uint32_t done = b.NewLabel();
    b.Branch(b.Const1(true), done, true);
    b.Label(align_fault);
    const Ref fault_base = b.GetSlot(Type::kI32, p.rn_disp);
    const Ref fault_va =
        p.base_off != 0u ? b.Op2(Op::kAdd, fault_base, b.Const32(p.base_off)) : fault_base;
    const cerfir::CallDesc fault{
        load ? reinterpret_cast<const void*>(&ArmMmu::AlignmentFaultReadHelper)
             : reinterpret_cast<const void*>(&ArmMmu::AlignmentFaultWriteHelper),
        cerfir::CallConv::kFastcall, 2u};
    b.Call(fault, Type::kVoid, fault_va, b.ConstPtr(c.block->emit->Mmu()), kNoRef);
    if (s.writeback_on_fault) {
        b.SetSlot(p.rn_disp, b.Op2(Op::kAdd, b.GetSlot(Type::kI32, p.rn_disp), b.Const32(p.delta)));
    }
    ArmIrRaiseDataAbort(c);
    b.Label(done);
}

void AddWord(cerfir::WordList& w, int32_t offset, int32_t slot, uint32_t imm) {
    const uint32_t k = w.count++;
    w.offset[k]      = offset;
    w.slot[k]        = slot;
    w.imm[k]         = imm;
}

void TransferWordList(ArmIrContext& c, const TransferShape& s, const TransferPlan& p) {
    const DecodedInsn& d    = *c.d;
    cerfir::Builder&   b    = *c.b;
    const bool         load = d.l != 0u;
    uint32_t           attr = 0;
    cerfir::MemAttr&   m    = b.NewMemAttr(&attr);
    m.width_bytes           = 4u;
    m.complete              = true;
    m.back_out              = true;
    m.align                 = s.policy;
    m.writeback_on_fault    = s.writeback_on_fault;
    m.writeback_slot        = s.writeback_on_fault ? static_cast<int32_t>(p.rn_disp) : cerfir::kNoSlot;
    m.base_slot             = static_cast<int32_t>(p.rn_disp);
    m.offset                = static_cast<int32_t>(p.base_off);
    m.slow_mask             = p.ignore_low ? 0u : 3u;
    ArmIrMemOrigin(c, m);

    uint32_t          list_index = 0;
    cerfir::WordList& w          = b.NewWordList(&list_index);
    w.store                      = !load;
    w.wb_add                     = p.delta;
    const uint32_t pc_value = d.guest_address + c.block->emit->ProcessorConfig()->pc_store_offset;
    int32_t        offset   = 0;
    int32_t        rn_off   = 0;
    for (uint32_t rest = d.register_list & 0xFFFFu; rest != 0u; rest &= rest - 1u) {
        const uint32_t i = LowestSetBit(rest);
        if (p.defer_rn && i == d.rn) {
            rn_off = offset;
        } else if (!load && i == ArmGpr::kR15) {
            AddWord(w, offset, cerfir::kNoSlot, pc_value);
        } else {
            AddWord(w, offset, ArmGprDisp(i), 0u);
        }
        offset += 4;
    }
    if (p.defer_rn) AddWord(w, rn_off, ArmGprDisp(d.rn), 0u);
    b.TransferWords(attr, list_index);
    if (p.final_wb) b.ComputeSlotInto(Op::kAdd, p.rn_disp, b.Const32(p.delta), 0u, p.rn_disp);
}

}

void ArmIrBlockDataTransfer(ArmIrContext& c) {
    const DecodedInsn& d          = *c.d;
    cerfir::Builder&   b          = *c.b;
    const ArmFeatures& f          = *c.block->emit->ProcessorConfig();
    const uint32_t     list       = d.register_list;
    const bool         load       = d.l != 0u;
    const bool         wback      = d.w != 0u;
    const bool         pc_in_list = (list & 0x8000u) != 0u;
    const bool         exc_return = d.s != 0u && load && pc_in_list;
    const bool         rn_in_list = ((list >> d.rn) & 1u) != 0u;
    if (ArmBlockTransferUndefined(d, f)) {
        ArmIrUndefined(c);
        return;
    }
    if (d.s != 0u) ArmIrSpsrModeGuard(c);

    const uint32_t bytes = 4u * static_cast<uint32_t>(std::popcount(list));
    TransferPlan   p{};
    p.rn_disp  = static_cast<uint32_t>(ArmGprDisp(d.rn));
    p.delta    = d.u != 0u ? bytes : 0u - bytes;
    p.base_off = d.u != 0u ? 0u : 0u - bytes;
    if (d.p == d.u) p.base_off += 4u;
    p.defer_rn = load && rn_in_list;
    p.final_wb = wback && !(load && rn_in_list);

    TransferShape s{};
    s.policy             = ArmBlockTransferAlign(c.block);
    s.writeback_on_fault = ArmBaseUpdatedOnAbort(p.final_wb, f);
    s.user_regs          = d.s != 0u && !(load && pc_in_list);
    s.first              = true;
    p.ignore_low         = s.policy == cerfir::AlignPolicy::kIgnoreLowBits;

    if (s.user_regs) {
        TransferUserRegisters(c, s, p);
    } else {
        TransferWordList(c, s, p);
    }

    if (!load || !pc_in_list) return;
    const Ref pc = b.GetSlot(Type::kI32, static_cast<uint32_t>(ArmGprDisp(ArmGpr::kR15)));
    if (exc_return) {
        ArmIrExceptionReturn(c, pc);
    } else {
        ArmIrLoadWritePc(c, pc);
    }
}

}
