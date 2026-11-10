#include "sh_ir_fns.h"

namespace cerfengine {

using cerfir::kNoRef;
using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

namespace {

constexpr uint32_t kDelaySlotOffset = 2u;

uint32_t AccessPc(const ShIrContext& c) {
    return c.in_delay_slot ? c.d->guest_address - kDelaySlotOffset : c.d->guest_address;
}

cerfir::MemAttr& NewAccessAttr(ShIrContext& c, uint32_t* index) {
    cerfir::MemAttr& m = c.b->NewMemAttr(index);
    m.width_bytes      = c.d->width;
    m.guest_pc         = AccessPc(c);
    return m;
}

cerfir::MemAttr& NewLoadAttr(ShIrContext& c, uint32_t* index) {
    cerfir::MemAttr& m = NewAccessAttr(c, index);
    m.sign             = c.d->width < 4u;
    return m;
}

Ref IndexedAddress(ShIrContext& c, uint32_t r) {
    cerfir::Builder& b = *c.b;
    return b.Op2(Op::kAdd, b.ReadSlot(ShGprDisp(0u)), b.ReadSlot(ShGprDisp(r)));
}

Ref PcRelativeBase(ShIrContext& c) {
    cerfir::Builder& b = *c.b;
    if (!c.in_delay_slot) return b.Const32(ShPcValue(c.d->guest_address));
    b.Dup(c.branch_target);
    return b.Op2(Op::kAdd, c.branch_target, b.Const32(2u));
}

void LoadInto(ShIrContext& c, Ref address, uint32_t base_slot, uint32_t offset, uint32_t dest_slot) {
    cerfir::Builder&     b = *c.b;
    const ShDecodedInsn& d = *c.d;
    if (address == kNoRef && b.SimpleLoad(d.width, d.width < 4u, base_slot, offset, dest_slot,
                                          static_cast<uint32_t>(cerfir::kNoSlot), AccessPc(c))) {
        return;
    }
    uint32_t         index = 0;
    cerfir::MemAttr& m     = NewLoadAttr(c, &index);
    m.result_slot          = static_cast<int32_t>(dest_slot);
    if (address == kNoRef) {
        m.base_slot = static_cast<int32_t>(base_slot);
        m.offset    = static_cast<int32_t>(offset);
        b.LoadBased(Type::kI32, index, m);
    } else {
        b.Load(Type::kI32, address, kNoRef, index);
    }
}

void StoreFrom(ShIrContext& c, Ref address, uint32_t base_slot, uint32_t offset, uint32_t source_slot) {
    cerfir::Builder&     b = *c.b;
    const ShDecodedInsn& d = *c.d;
    if (address != kNoRef) {
        ShIrStoreAt(c, address, b.ReadSlot(source_slot));
        return;
    }
    if (b.SimpleStore(d.width, base_slot, offset, b.ReadSlot(source_slot), AccessPc(c))) return;
    uint32_t         index = 0;
    cerfir::MemAttr& m     = NewAccessAttr(c, &index);
    m.base_slot            = static_cast<int32_t>(base_slot);
    m.offset               = static_cast<int32_t>(offset);
    b.StoreBased(b.ReadSlot(source_slot), index, m);
}

}

Ref ShIrAlignedPcBase(ShIrContext& c) {
    return c.b->Op2(Op::kAnd, PcRelativeBase(c), c.b->Const32(~3u));
}

Ref ShIrLoadValue(ShIrContext& c, uint32_t base_slot) {
    uint32_t         index = 0;
    cerfir::MemAttr& m     = NewLoadAttr(c, &index);
    m.base_slot            = static_cast<int32_t>(base_slot);
    return c.b->LoadBased(Type::kI32, index, m);
}

Ref ShIrLoadAt(ShIrContext& c, Ref address) {
    uint32_t index = 0;
    NewAccessAttr(c, &index);
    return c.b->Load(Type::kI32, address, kNoRef, index);
}

void ShIrStoreAt(ShIrContext& c, Ref address, Ref value) {
    uint32_t index = 0;
    NewAccessAttr(c, &index);
    c.b->Store(address, value, kNoRef, index);
}

void ShIrStorePreDecrement(ShIrContext& c, uint32_t source_slot) {
    cerfir::Builder&     b       = *c.b;
    const ShDecodedInsn& d       = *c.d;
    const Ref            address = b.Op2(Op::kSub, b.ReadSlot(ShGprDisp(d.n)), b.Const32(d.width));
    b.Dup(address);
    StoreFrom(c, address, 0u, 0u, source_slot);
    b.SetSlot(ShGprDisp(d.n), address);
}

void ShIrIncrementBase(ShIrContext& c) {
    cerfir::Builder& b    = *c.b;
    const uint32_t   base = ShGprDisp(c.d->m);
    b.SetSlot(base, b.Op2(Op::kAdd, b.ReadSlot(base), b.Const32(c.d->width)));
}

void ShIrLoadPostIncrement(ShIrContext& c, uint32_t dest_slot) {
    const uint32_t base = ShGprDisp(c.d->m);
    LoadInto(c, kNoRef, base, 0u, dest_slot);
    if (dest_slot != base) ShIrIncrementBase(c);
}

void ShIrTranslateMovPcRel(ShIrContext& c) {
    cerfir::Builder&     b    = *c.b;
    const ShDecodedInsn& d    = *c.d;
    const Ref            base = d.width == 4u ? ShIrAlignedPcBase(c) : PcRelativeBase(c);
    LoadInto(c, b.Op2(Op::kAdd, base, b.Const32(d.disp)), 0u, 0u, ShGprDisp(d.n));
}

void ShIrTranslateMovStore(ShIrContext& c) {
    StoreFrom(c, kNoRef, ShGprDisp(c.d->n), 0u, ShGprDisp(c.d->m));
}

void ShIrTranslateMovLoad(ShIrContext& c) {
    LoadInto(c, kNoRef, ShGprDisp(c.d->m), 0u, ShGprDisp(c.d->n));
}

void ShIrTranslateMovStoreDisp(ShIrContext& c) {
    StoreFrom(c, kNoRef, ShGprDisp(c.d->n), c.d->disp, ShGprDisp(c.d->m));
}

void ShIrTranslateMovLoadDisp(ShIrContext& c) {
    LoadInto(c, kNoRef, ShGprDisp(c.d->m), c.d->disp, ShGprDisp(c.d->n));
}

void ShIrTranslateMovStoreGbr(ShIrContext& c) {
    StoreFrom(c, kNoRef, ShGbrDisp(), c.d->disp, ShGprDisp(0u));
}

void ShIrTranslateMovLoadGbr(ShIrContext& c) {
    LoadInto(c, kNoRef, ShGbrDisp(), c.d->disp, ShGprDisp(0u));
}

void ShIrTranslateMovStorePreDec(ShIrContext& c) {
    ShIrStorePreDecrement(c, ShGprDisp(c.d->m));
}

void ShIrTranslateMovLoadPostInc(ShIrContext& c) {
    ShIrLoadPostIncrement(c, ShGprDisp(c.d->n));
}

void ShIrTranslateMovStoreIndexed(ShIrContext& c) {
    StoreFrom(c, IndexedAddress(c, c.d->n), 0u, 0u, ShGprDisp(c.d->m));
}

void ShIrTranslateMovLoadIndexed(ShIrContext& c) {
    LoadInto(c, IndexedAddress(c, c.d->m), 0u, 0u, ShGprDisp(c.d->n));
}

}
