#include "sh_ir_fns.h"

#include "../sh_address_space.h"
#include "../sh_dsp_encoding.h"
#include "../sh_dsp_unit.h"

namespace cerfengine {

using cerfir::kNoRef;
using cerfir::Op;
using cerfir::Ref;

namespace {

constexpr uint32_t kXyAddressMask  = 0xFFFEu;
constexpr uint32_t kTopWordShift   = 16u;
constexpr uint32_t kGuardBits      = 0xFFu;
constexpr uint32_t kSignToGuardAsr = 31u;
constexpr uint32_t kGuardSignShift = 24u;
constexpr uint8_t  kWordBytes      = 2u;

struct XyLoad {
    uint32_t dest_slot = 0u;
    Ref      value     = kNoRef;
};

uint32_t RegSlot(ShDspReg reg) { return ShStateDisp(ShDspRegOffset(reg)); }

XyLoad Transfer(ShIrContext& c, uint32_t field_a, bool x) {
    cerfir::Builder& b = *c.b;
    if (ShDspBusMode(field_a, x) == ShDspAddressing::kNone) return {};
    const uint32_t bus     = x ? ShAddressSpace::kUxyBase : ShAddressSpace::kUxyBase | ShAddressSpace::kXyBusYHalf;
    const Ref      pointer = b.ReadSlot(ShGprDisp(ShDspPointerGpr(field_a, x)));
    const Ref      address = b.Op2(Op::kOr, b.Op2(Op::kAnd, pointer, b.Const32(kXyAddressMask)), b.Const32(bus));
    const uint32_t slot    = RegSlot(ShDspTransferReg(field_a, x));
    if (ShDspTransferStore(field_a, x)) {
        ShIrStoreAt(c, address, b.Op2(Op::kLsr, b.ReadSlot(slot), b.Const32(kTopWordShift)));
        return {};
    }
    return {slot, ShIrLoadAt(c, address)};
}

void WriteLoaded(ShIrContext& c, const XyLoad& load) {
    if (load.value == kNoRef) return;
    c.b->SetSlot(load.dest_slot, c.b->Op2(Op::kShl, load.value, c.b->Const32(kTopWordShift)));
}

void CallParallelHelper(ShIrContext& c, uint32_t field_a) {
    c.b->CallConst(ShIrHostCallConst(reinterpret_cast<const void*>(&ShDspUnit::ParallelHelper), c.d->guest_address),
                   3u, cerfir::CallHostAddress(c.dsp), (field_a << 16) | c.d->raw_b,
                   ShDspPackOperation(ShDspDecodeOperation(c.d->raw_b)), 0u);
}

Ref SingleStoreData(ShIrContext& c, ShDspReg ds) {
    cerfir::Builder& b     = *c.b;
    const Ref        value = b.ReadSlot(RegSlot(ds));
    if (ShDspIsGuard(ds)) {
        return b.Op2(Op::kAsr, b.Op2(Op::kShl, value, b.Const32(kGuardSignShift)), b.Const32(kGuardSignShift));
    }
    if (c.d->width == kWordBytes) return b.Op2(Op::kLsr, value, b.Const32(kTopWordShift));
    return value;
}

void SingleLoadInto(ShIrContext& c, ShDspReg ds, Ref loaded) {
    cerfir::Builder& b = *c.b;
    if (ShDspIsGuard(ds)) {
        b.SetSlot(RegSlot(ds), b.Op2(Op::kAnd, loaded, b.Const32(kGuardBits)));
        return;
    }
    const Ref value = c.d->width == kWordBytes ? b.Op2(Op::kShl, loaded, b.Const32(kTopWordShift)) : loaded;
    if (!ShDspHasGuard(ds)) {
        b.SetSlot(RegSlot(ds), value);
        return;
    }
    b.Dup(value);
    b.SetSlot(RegSlot(ds), value);
    ShIrSetGuardFromSign(c, RegSlot(ShDspGuardOf(ds)), value);
}

}

void ShIrSetGuardFromSign(ShIrContext& c, uint32_t guard_slot, Ref value) {
    cerfir::Builder& b    = *c.b;
    const Ref        sign = b.Op2(Op::kAsr, value, b.Const32(kSignToGuardAsr));
    b.SetSlot(guard_slot, b.Op2(Op::kAnd, sign, b.Const32(kGuardBits)));
}

void ShIrTranslateDspTransfer(ShIrContext& c) {
    const uint32_t field_a = ShDspFieldA(c.d->raw);
    const XyLoad   x       = Transfer(c, field_a, true);
    const XyLoad   y       = Transfer(c, field_a, false);
    if (ShDspUpdatesPointer(ShDspXMode(field_a)) || ShDspUpdatesPointer(ShDspYMode(field_a))) {
        c.b->CallConst(ShIrHostCallConst(reinterpret_cast<const void*>(&ShDspUnit::TransferHelper),
                                         c.d->guest_address),
                       2u, cerfir::CallHostAddress(c.dsp), field_a, 0u, 0u);
    }
    WriteLoaded(c, x);
    WriteLoaded(c, y);
}

void ShIrTranslateDspParallel(ShIrContext& c) {
    const uint32_t field_a = ShDspFieldA(c.d->raw);
    if (ShDspOperationUnspecified(c.d->raw_b)) {
        CallParallelHelper(c, field_a);
        return;
    }
    const XyLoad x = Transfer(c, field_a, true);
    const XyLoad y = Transfer(c, field_a, false);
    CallParallelHelper(c, field_a);
    WriteLoaded(c, x);
    WriteLoaded(c, y);
}

void ShIrTranslateDspSingleTransfer(ShIrContext& c) {
    cerfir::Builder&      b       = *c.b;
    const uint16_t        raw     = c.d->raw;
    const uint32_t        as      = ShDspSingleAsGpr(raw);
    const uint32_t        as_slot = ShGprDisp(as);
    const ShDspSingleMode mode    = ShDspSingleModeOf(raw);
    const ShDspReg        ds      = ShDspSingleDs(raw);
    const Ref address = mode == ShDspSingleMode::kPreDecrement ? ShIrDecrementedGpr(c, as) : b.ReadSlot(as_slot);
    if (mode == ShDspSingleMode::kPreDecrement || mode == ShDspSingleMode::kIndex) b.Dup(address);
    if (ShDspSingleStore(raw)) {
        ShIrStoreAt(c, address, SingleStoreData(c, ds));
    } else {
        SingleLoadInto(c, ds, ShIrLoadAt(c, address));
    }
    switch (mode) {
    case ShDspSingleMode::kPreDecrement: b.SetSlot(as_slot, address); break;
    case ShDspSingleMode::kIncrement: ShIrIncrementGpr(c, as); break;
    case ShDspSingleMode::kIndex:
        b.SetSlot(as_slot, b.Op2(Op::kAdd, address, b.ReadSlot(ShGprDisp(kShDspIxGpr))));
        break;
    case ShDspSingleMode::kIndirect: break;
    }
}

}
