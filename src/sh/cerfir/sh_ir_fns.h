#pragma once

#include "sh_ir_context.h"

namespace cerfengine {

cerfir::Ref ShIrAlignedPcBase(ShIrContext& c);
cerfir::Ref ShIrLoadValue(ShIrContext& c, uint32_t base_slot);
cerfir::Ref ShIrLoadAt(ShIrContext& c, cerfir::Ref address);
void        ShIrStoreAt(ShIrContext& c, cerfir::Ref address, cerfir::Ref value);
void        ShIrStorePreDecrement(ShIrContext& c, uint32_t source_slot);
void        ShIrLoadPostIncrement(ShIrContext& c, uint32_t dest_slot);
void        ShIrIncrementBase(ShIrContext& c);
void        ShIrSetT(cerfir::Builder& b, cerfir::Ref condition);
void        ShIrSetNotT(cerfir::Builder& b, cerfir::Ref condition);
cerfir::Ref ShIrTClear(cerfir::Builder& b);
cerfir::Ref ShIrTAsCarry(cerfir::Builder& b);
cerfir::Ref ShIrExtend(cerfir::Builder& b, cerfir::Op op, cerfir::Type from, cerfir::Ref value);
void        ShIrLinkProcedure(ShIrContext& c);

void ShIrTranslateMovPcRel(ShIrContext& c);
void ShIrTranslateMov(ShIrContext& c);
void ShIrTranslateMovImm(ShIrContext& c);
void ShIrTranslateMovStore(ShIrContext& c);
void ShIrTranslateMovLoad(ShIrContext& c);
void ShIrTranslateMovStorePreDec(ShIrContext& c);
void ShIrTranslateMovLoadPostInc(ShIrContext& c);
void ShIrTranslateMovStoreIndexed(ShIrContext& c);
void ShIrTranslateMovLoadIndexed(ShIrContext& c);
void ShIrTranslateMovStoreDisp(ShIrContext& c);
void ShIrTranslateMovLoadDisp(ShIrContext& c);
void ShIrTranslateMovStoreGbr(ShIrContext& c);
void ShIrTranslateMovLoadGbr(ShIrContext& c);
void ShIrTranslateMova(ShIrContext& c);
void ShIrTranslateMovt(ShIrContext& c);
void ShIrTranslateSwap(ShIrContext& c);
void ShIrTranslateXtrct(ShIrContext& c);

void ShIrTranslateJump(ShIrContext& c);
void ShIrTranslateBranchAlways(ShIrContext& c);
void ShIrTranslateConditionalBranch(ShIrContext& c);

void ShIrTranslateCmp(ShIrContext& c);

void ShIrTranslateSystemLoad(ShIrContext& c);
void ShIrTranslateSystemLoadPostInc(ShIrContext& c);
void ShIrTranslateSystemStore(ShIrContext& c);
void ShIrTranslateSystemStorePreDec(ShIrContext& c);

void ShIrTranslateControlLoad(ShIrContext& c);
void ShIrTranslateControlLoadPostInc(ShIrContext& c);
void ShIrTranslateControlStore(ShIrContext& c);
void ShIrTranslateControlStorePreDec(ShIrContext& c);

void ShIrTranslateLogic(ShIrContext& c);
void ShIrTranslateLogicImm(ShIrContext& c);
void ShIrTranslateLogicGbr(ShIrContext& c);
void ShIrTranslateNot(ShIrContext& c);
void ShIrTranslateTas(ShIrContext& c);

void ShIrTranslateArith(ShIrContext& c);
void ShIrTranslateAddImm(ShIrContext& c);

void ShIrTranslateShift(ShIrContext& c);

void ShIrTranslateMultiply(ShIrContext& c);
void ShIrTranslateClrmac(ShIrContext& c);
void ShIrTranslateLdtlb(ShIrContext& c);
void ShIrTranslateRte(ShIrContext& c);
void ShIrTranslateClearSrBits(ShIrContext& c);
void ShIrTranslateSetSrBits(ShIrContext& c);
void ShIrTranslateSleep(ShIrContext& c);

}
