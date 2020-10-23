/*
 * Copyright (c) 2019, Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

//===----------------------------------------------------------------------===//
//
// GenX instruction baling is analyzed by this pass. See GenXBaling.h for more
// detailed comment.
//
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "GENX_INSTRUCTION_BALING"

#include "GenXBaling.h"
#include "GenXIntrinsics.h"
#include "GenXLiveness.h"
#include "GenXRegion.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/Local.h"

// Part of the bodge to allow abs to bale in to sext/zext. This needs to be set
// to some arbitrary value that does not clash with any
// GenXIntrinsicInfo::MODIFIER_* value.
enum { MODIFIER_ABSONLY = 9000 };

using namespace llvm;
using namespace genx;
using namespace Intrinsic::GenXRegion;

//----------------------------------------------------------------------
// Administrivia for GenXFuncBaling pass
//
char GenXFuncBaling::ID = 0;
INITIALIZE_PASS(GenXFuncBaling, "GenXFuncBaling", "GenXFuncBaling", false, false)

FunctionPass *llvm::createGenXFuncBalingPass(BalingKind Kind, GenXSubtarget *ST)
{
  initializeGenXFuncBalingPass(*PassRegistry::getPassRegistry());
  return new GenXFuncBaling(Kind, ST);
}

void GenXFuncBaling::getAnalysisUsage(AnalysisUsage &AU) const
{
  FunctionPass::getAnalysisUsage(AU);
  AU.setPreservesCFG();
}

//----------------------------------------------------------------------
// Administrivia for GenXGroupBaling pass
//
char GenXGroupBaling::ID = 0;
INITIALIZE_PASS_BEGIN(GenXGroupBaling, "GenXGroupBaling", "GenXGroupBaling", false, false)
INITIALIZE_PASS_DEPENDENCY(GenXLiveness)
INITIALIZE_PASS_END(GenXGroupBaling, "GenXGroupBaling", "GenXGroupBaling", false, false)

FunctionGroupPass *llvm::createGenXGroupBalingPass(BalingKind Kind, GenXSubtarget *ST)
{
  initializeGenXGroupBalingPass(*PassRegistry::getPassRegistry());
  return new GenXGroupBaling(Kind, ST);
}

void GenXGroupBaling::getAnalysisUsage(AnalysisUsage &AU) const
{
  FunctionGroupPass::getAnalysisUsage(AU);
  AU.addRequired<GenXLiveness>();
  AU.setPreservesCFG();
  AU.addPreserved<GenXModule>();
  AU.addPreserved<GenXLiveness>();
}

/***********************************************************************
 * GenXGroupBaling::runOnFunctionGroup : run second baling pass on function
 *    group
 */
bool GenXGroupBaling::runOnFunctionGroup(FunctionGroup &FG)
{
  clear();
  Liveness = &getAnalysis<GenXLiveness>();
  return processFunctionGroup(&FG);
}

/***********************************************************************
 * processFunctionGroup : run instruction baling analysis on one
 *  function group
 */
bool GenXBaling::processFunctionGroup(FunctionGroup *FG)
{
  bool Modified = false;
  for (auto i = FG->begin(), e = FG->end(); i != e; ++i) {
    Modified |= processFunction(*i);
  }
  return Modified;
}

/***********************************************************************
 * processFunction : run instruction baling analysis on one function
 *
 * This does a preordered depth first traversal of the CFG to
 * ensure that we see a def before its uses (ignoring phi node uses).
 * This is required when we see a constant add/sub used as a region or
 * element variable index; if the add/sub has already been marked as
 * baling in a modifier or rdregion then we cannot bale it in to the
 * variable index region.
 *
 * This pass also clones any instruction that can be baled in but has
 * multiple uses. A baled in instruction must have exactly one use.
 */
bool GenXBaling::processFunction(Function *F)
{
  bool Changed = prologue(F);

  for (df_iterator<BasicBlock *> i = df_begin(&F->getEntryBlock()),
      e = df_end(&F->getEntryBlock()); i != e; ++i) {
    for (BasicBlock::iterator bi = i->begin(), be = i->end(); bi != be; ) {
      Instruction *Inst = &*bi;
      ++bi; // increment here as Inst may be erased
      processInst(Inst);
    }
  }
  // Process any two addr sends we found.
  for (auto i = TwoAddrSends.begin(), e = TwoAddrSends.end(); i != e; ++i)
    processTwoAddrSend(*i);
  TwoAddrSends.clear();
  // Clone any instructions that we found in the pass that want to be baled in
  // but have more than one use.
  if (NeedCloneStack.size()) {
    doClones();
    Changed = true;
  }
  return Changed;
}

/***********************************************************************
 * processInst : calculate baling for an instruction
 *
 * Usually this is called from runOnFunction above. However another pass
 * can call this to recalculate the baling for an instruction, particularly
 * for a new instruction it has just added. GenXLegalization does this.
 */
void GenXBaling::processInst(Instruction *Inst)
{
  unsigned IntrinID = getIntrinsicID(Inst);
  if (isWrRegion(IntrinID))
    processWrRegion(Inst);
  else if (IntrinID == Intrinsic::genx_wrpredregion)
    processWrPredRegion(Inst);
  else if (IntrinID == Intrinsic::genx_wrpredpredregion)
    processWrPredPredRegion(Inst);
  else if (IntrinID == Intrinsic::genx_sat || isIntegerSat(IntrinID))
    processSat(Inst);
  else if (isRdRegion(IntrinID))
    processRdRegion(Inst);
  else if (BranchInst *Branch = dyn_cast<BranchInst>(Inst))
    processBranch(Branch);
  else if (auto SI = dyn_cast<StoreInst>(Inst))
    processStore(SI);
  else {
    // Try to bale a select into cmp's dst. If failed, continue to process
    // select as a main instruction.
    bool BaledSelect = processSelect(Inst);
    if (!BaledSelect)
      processMainInst(Inst, IntrinID);
  }
}

/***********************************************************************
 * isRegionOKForIntrinsic : check whether region is OK for an intrinsic arg
 *
 * Enter:   AI = the ArgInfo for the intrinsic arg (or return value)
 *          RegionInst = the rdregion or wrregion instruction
 *          SecondPass = whether in second baling pass
 *
 * This checks that the arg is general (rather than raw) and does not have
 * any stride restrictions that are incompatible with the region.
 *
 * In the legalization pass of baling, we always return true when the main 
 * instruction can be splitted. Otherwise, a region that would be OK after
 * being split by legalization might here appear not OK, and that would stop
 * legalization considering splitting it. However, if the main instruction
 * cannot be splitted, then we need to check the full restriction
 * otherwise, if the region is considered baled and skip legalization, 
 * we may have illegal standalone read-region.
 */
bool GenXBaling::isRegionOKForIntrinsic(unsigned ArgInfoBits,
  Instruction *RegionInst, bool CanSplitBale)
{
  GenXIntrinsicInfo::ArgInfo AI(ArgInfoBits);
  if (!AI.isGeneral())
    return false;
  if (Kind == BalingKind::BK_Legalization) {
    if (CanSplitBale)
      return true;
  }
  unsigned Restriction = AI.getRestriction();
  if (!Restriction)
    return true;
  unsigned GRFWidth = ST ? ST->getGRFWidth() : 32;
  Region R(RegionInst, BaleInfo());
  unsigned ElementsPerGrf = GRFWidth/R.ElementBytes;
  unsigned GRFLogAlign = Log2_32(GRFWidth);
  if (AI.Info & GenXIntrinsicInfo::GRFALIGNED) {
    if (R.Indirect) {
      // Instructions that cannot be splitted also cannot allow indirect 
      if (!CanSplitBale)
        return false;
      Alignment AL = AlignInfo.get(R.Indirect);
      if (AL.getLogAlign() < GRFLogAlign || AL.getExtraBits() != 0)
        return false;
    }
    else if (R.Offset & (GRFWidth-1))
      return false;
    if (R.is2D() && (R.VStride & (ElementsPerGrf - 1)))
      return false;
  }
  if (AI.Info & GenXIntrinsicInfo::OWALIGNED) {
    // Instructions that cannot be splitted also cannot allow indirect 
    if (R.Indirect) {
      if (!CanSplitBale)
        return false;
      Alignment AL = AlignInfo.get(R.Indirect);
      if (AL.getLogAlign() < 4 || AL.getExtraBits() != 0)
        return false;
    }
    if (R.Offset & 15)
      return false;
    if (R.is2D() && (R.VStride & ((ElementsPerGrf >> 1) - 1)))
      return false;
  }
  switch (Restriction) {
    case GenXIntrinsicInfo::SCALARORCONTIGUOUS:
      if (!R.Stride && R.Width == R.NumElements)
        break;
      // fall through...
    case GenXIntrinsicInfo::FIXED4:
    case GenXIntrinsicInfo::CONTIGUOUS:
      if (R.Stride != 1 || R.Width != R.NumElements)
        return false;
      break;
    case GenXIntrinsicInfo::STRIDE1:
      // For the dot product instructions, the vISA spec just says that the
      // horizontal stride must be 1. It doesn't say anything about the
      // width or the vertical stride. I am assuming that the width must also
      // be at least 4, since the operation works on groups of 4 channels.
      if (R.Stride != 1 || R.Width < 4)
        return false;
      break;
    default:
      break;
  }
  return true;
}

/***********************************************************************
 * isRegionOKForRaw : check if region is OK for baling in to raw operand
 *
 * Enter:   V = value that is possibly rdregion/wrregion
 *          IsWrite = true if caller wants to see wrregion, false for rdregion
 *
 * The region must be constant indexed, contiguous, and start on a GRF
 * boundary.
 */
bool GenXBaling::isRegionOKForRaw(Value *V, bool IsWrite)
{
  switch (getIntrinsicID(V)) {
    case Intrinsic::genx_rdregioni:
    case Intrinsic::genx_rdregionf:
      if (IsWrite)
        return false;
      break;
    case Intrinsic::genx_wrregioni:
    case Intrinsic::genx_wrregionf:
      if (!IsWrite)
        return false;
      break;
    default:
      return false;
  }
  Region R(cast<Instruction>(V), BaleInfo());
  if (R.Mask)
    return false;
  unsigned GRFWidth = ST ? ST->getGRFWidth() : 32;
  if (R.Indirect)
    return false;
  else if (R.Offset & (GRFWidth-1)) // GRF boundary check
    return false;
  if (R.Width != R.NumElements)
    return false;
  if (R.Stride != 1)
    return false;
  return true;
}

/***********************************************************************
 * checkModifier : check whether instruction is a source modifier
 *
 * Enter:   Inst = instruction to check
 *
 * Return:  ABSMOD, NEGMOD, NOTMOD, ZEXT, SEXT or MAININST (0) if not modifier
 */
static int checkModifier(Instruction *Inst)
{
  switch (Inst->getOpcode()) {
    case Instruction::Sub:
    case Instruction::FSub:
      // Negate is represented in LLVM IR by subtract from 0.
      if (Constant *Lhs = dyn_cast<Constant>(Inst->getOperand(0))) {
        // Canonicalize splats as well
        if (isa<VectorType>(Lhs->getType()))
          if (auto splat = Lhs->getSplatValue())
            Lhs = splat;

        if (Lhs->isZeroValue())
          return BaleInfo::NEGMOD;
      }
      break;
    case Instruction::Xor:
      if (isIntNot(Inst))
        return BaleInfo::NOTMOD;
      break;
    case Instruction::ZExt:
      if (!Inst->getOperand(0)->getType()->getScalarType()->isIntegerTy(1))
        return BaleInfo::ZEXT;
      break;
    case Instruction::SExt:
      if (!Inst->getOperand(0)->getType()->getScalarType()->isIntegerTy(1))
        return BaleInfo::SEXT;
      break;
    default:
      switch (getIntrinsicID(Inst)) {
        case Intrinsic::genx_absi:
        case Intrinsic::genx_absf:
          return BaleInfo::ABSMOD;
        default:
          break;
      }
      break;
  }
  return BaleInfo::MAININST;
}

/***********************************************************************
 * operandIsBaled : check if a main inst is baled
 *
 * Enter:   Inst = the main inst
 *          OperandNum = operand number to look at
 *          ModType = what type of modifier (arith/logic/extonly/none) this
 *                    operand accepts
 *          AI = GenXIntrinsicInfo::ArgInfo, so we can see any stride
 *               restrictions, omitted if Inst is not an intrinsic
 */
bool
GenXBaling::operandIsBaled(Instruction *Inst,
               unsigned OperandNum, int ModType,
               unsigned ArgInfoBits = GenXIntrinsicInfo::GENERAL) {
  GenXIntrinsicInfo::ArgInfo AI(ArgInfoBits);
  Instruction *Opnd = dyn_cast<Instruction>(Inst->getOperand(OperandNum));
  if (!Opnd)
    return false;
  // Check for source operand modifier.
  if (ModType != GenXIntrinsicInfo::MODIFIER_DEFAULT) {
    int Mod = checkModifier(Opnd);
    switch (Mod) {
      case BaleInfo::MAININST:
        break;
      case BaleInfo::ZEXT:
      case BaleInfo::SEXT:
        if (ModType != GenXIntrinsicInfo::MODIFIER_DEFAULT)
          return true;
        break;
      case BaleInfo::NOTMOD:
        if (ModType == GenXIntrinsicInfo::MODIFIER_LOGIC)
          return true;
        break;
      case BaleInfo::ABSMOD:
        // Part of the bodge to allow abs to be baled in to zext/sext.
        if (ModType == MODIFIER_ABSONLY)
          return true;
        // fall through...
      default:
        if (ModType == GenXIntrinsicInfo::MODIFIER_ARITH)
          return true;
        break;
    }
  }
  if (isRdRegion(getIntrinsicID(Opnd))) {
    // The operand is a rdregion. Check any restrictions.
    // (Note we call isRegionOKForIntrinsic even when Inst is not an
    // intrinsic, since in that case AI is initialized to a state
    // where there are no region restrictions.)
    bool CanSplitBale = true;
    if (!isRegionOKForIntrinsic(AI.Info, Opnd, CanSplitBale))
      return false;

    // Do not bale in a region read with multiple uses if
    // - any use is bitcast, or
    // - it is indirect.
    // as bitcast will not bale its operands and indirect multiple-use region
    // reads often lead to narrow simd width after legalization.
    if (Opnd->getNumUses() > 1 && (Kind == BalingKind::BK_Legalization ||
                                   Kind == BalingKind::BK_Analysis)) {
      for (auto U : Opnd->users())
        if (isa<BitCastInst>(U))
          return false;
      Region R(cast<CallInst>(Opnd), BaleInfo());
      if (R.Indirect)
        return false;
    }
    return true;
  }

  return false;
}

/***********************************************************************
 * processWrPredRegion : set up baling info for wrpredregion
 *
 * The input to wrpredregion may be the following:
 * 1) icmp or fcmp, in which case it is always baled.
 * 2) constant, which may resulted from region simplification.
 */
void GenXBaling::processWrPredRegion(Instruction *Inst)
{
  Value *V = Inst->getOperand(Intrinsic::GenXRegion::NewValueOperandNum);
  assert(isa<CmpInst>(V) || isa<Constant>(V));
  BaleInfo BI(BaleInfo::WRPREDREGION);
  if (isa<CmpInst>(V)) {
    setOperandBaled(Inst, Intrinsic::GenXRegion::NewValueOperandNum, &BI);
  }
  setBaleInfo(Inst, BI);
}

/***********************************************************************
 * processWrPredPredRegion : set up baling info for wrpredpredregion
 *
 * The "new value" input to wrpredregion must be icmp or fcmp, and it is always
 * baled.
 *
 * The condition input is assumed to be EM. But it might be an rdpredregion
 * out of EM, in which case the rdpredregion is baled. The rdpredregion must
 * have offset 0.
 */
void GenXBaling::processWrPredPredRegion(Instruction *Inst)
{
  assert(isa<CmpInst>(Inst->getOperand(Intrinsic::GenXRegion::NewValueOperandNum)));
  BaleInfo BI(BaleInfo::WRPREDPREDREGION);
  setOperandBaled(Inst, Intrinsic::GenXRegion::NewValueOperandNum, &BI);
  Value *Cond = Inst->getOperand(3);
  if (getIntrinsicID(Cond) == Intrinsic::genx_rdpredregion) {
    assert(cast<Constant>(cast<CallInst>(Cond)->getOperand(1))->isNullValue());
    setOperandBaled(Inst, 3, &BI);
  }
  setBaleInfo(Inst, BI);
}

/***********************************************************************
 * processWrRegion : set up baling info for wrregion
 */
void GenXBaling::processWrRegion(Instruction *Inst)
{
  BaleInfo BI(BaleInfo::WRREGION);
  // Get the instruction (if any) that creates the element/subregion to write.
  unsigned OperandNum = 1;
  Instruction *V = dyn_cast<Instruction>(Inst->getOperand(OperandNum));
  if (V && !V->hasOneUse()) {
    // The instruction has multiple uses.
    // We don't want to bale in the following cases, as they seem to make the
    // code worse, unless this is load from a global variable.
    if (V->getParent() != Inst->getParent()) {
      auto isRegionFromGlobalLoad = [](Value *V) {
        if (!isRdRegion(V))
          return false;
        auto LI = dyn_cast<LoadInst>(cast<CallInst>(V)->getArgOperand(0));
        return LI && getUnderlyingGlobalVariable(LI->getPointerOperand());
      };
      // 0. It is in a different basic block to the wrregion.
      if (!isRegionFromGlobalLoad(V))
        V = nullptr;
    } else {
      // 1. The maininst is a select.
      Bale B;
      buildBale(V, &B);
      if (auto MainInst = B.getMainInst()) {
        if (isa<SelectInst>(MainInst->Inst) ||
            isHighCostBaling(BaleInfo::WRREGION, MainInst->Inst))
          V = nullptr;
      }
      // 2. There is an indirect rdregion with a constant offset (probably due to
      // the risk of the jitter doing unfolding; this check may be unnecessary
      // after HSW).
      for (auto i = B.begin(), e = B.end(); i != e; ++i) {
        if (i->Info.Type != BaleInfo::RDREGION)
          continue;
        if (!isa<Constant>(i->Inst->getOperand(
                Intrinsic::GenXRegion::RdIndexOperandNum))) {
          V = nullptr;
          break;
        }
      }
    }
    // FIXME: Baling on WRREGION is not the right way to reduce the overhead
    // from `wrregion`. Instead, register coalescing should be applied to
    // enable direct defining of the WRREGION and minimize the value
    // duplication.
  }
  if (V) {
    // It is an instruction. We can bale it in, if it is a suitable instruction.
    unsigned ValIntrinID = getIntrinsicID(V);
    if (ValIntrinID == Intrinsic::genx_sat || isRdRegion(ValIntrinID))
      setOperandBaled(Inst, OperandNum, &BI);
    else if (ValIntrinID == Intrinsic::not_intrinsic) {
      if (isa<BinaryOperator>(V) || (isa<CastInst>(V) && !isa<BitCastInst>(V)))
        setOperandBaled(Inst, OperandNum, &BI);
      else if (isMaskPacking(V))
        setOperandBaled(Inst, OperandNum, &BI);
      else if (isa<SelectInst>(V) && !Region(Inst, BaleInfo()).Mask) {
        // Can bale in a select as long as the wrregion is unpredicated.
        setOperandBaled(Inst, OperandNum, &BI);
      }
    } else if (!isWrRegion(ValIntrinID)) {
      // V is an intrinsic other than rdregion/wrregion. If this is a
      // predicated wrregion, only permit baling in if the intrinsic
      // supports a predicate mask.

      Region R(Inst, BaleInfo());
      GenXIntrinsicInfo II(ValIntrinID);

      if (R.Mask == 0 || II.getPredAllowed()) {
        // Check that its return value is suitable for baling.
        GenXIntrinsicInfo::ArgInfo AI = II.getRetInfo();
        switch (AI.getCategory()) {
          case GenXIntrinsicInfo::GENERAL: {
            bool CanSplitBale = true;
            if (isRegionOKForIntrinsic(AI.Info, Inst, CanSplitBale))
              setOperandBaled(Inst, OperandNum, &BI);
            }
            break;
          case GenXIntrinsicInfo::RAW:
            // Intrinsic with raw result can be baled in to wrregion as long as
            // it is unstrided and starts on a GRF boundary, and there is no
            // non-undef TWOADDR operand.  Ensure the wrregion's result has an
            // alignment of 32.
            if (isRegionOKForRaw(Inst, /*IsWrite=*/true)) {
              if (Liveness)
                Liveness->getOrCreateLiveRange(Inst)->LogAlignment = 5;
              unsigned FinalCallArgIdx = V->getNumOperands() - 2;
              if (isa<UndefValue>(V->getOperand(FinalCallArgIdx)))
                setOperandBaled(Inst, OperandNum, &BI);
              else {
                GenXIntrinsicInfo::ArgInfo AI2 = II.getArgInfo(FinalCallArgIdx);
                if (AI2.getCategory() != GenXIntrinsicInfo::TWOADDR)
                  setOperandBaled(Inst, OperandNum, &BI);
              }
            }
            break;
        }
      }
    }
  }
  // Now see if there is a variable index with an add/sub with an in range
  // offset that we can bale in, such that the add/sub does not already
  // bale in other instructions.
  OperandNum = 5;
  if (isBalableIndexAdd(Inst->getOperand(OperandNum))) {
    setOperandBaled(Inst, OperandNum, &BI);
    // We always set up InstMap for an address add, even though it does not
    // bale in any operands.
    setBaleInfo(cast<Instruction>(Inst->getOperand(OperandNum)), BaleInfo(BaleInfo::ADDRADD, 0));
  }
  // See if there is any baling in to the predicate (mask) operand.
  if (processPredicate(Inst, Intrinsic::GenXRegion::PredicateOperandNum))
    setOperandBaled(Inst, Intrinsic::GenXRegion::PredicateOperandNum, &BI);
  // We always set up InstMap for a wrregion, even if it does not bale in any
  // operands.
  setBaleInfo(Inst, BI);
}

// Process a select instruction. Return true if it can be baled into a cmp
// instruction, false otherwise.
bool GenXBaling::processSelect(Instruction *Inst) {
  auto SI = dyn_cast<SelectInst>(Inst);
  if (!SI || !SI->getType()->isVectorTy())
    return false;

  // Only bale into a cmp instruction.
  Value *Cond = SI->getCondition();
  if (!isa<CmpInst>(Cond) || !Cond->getType()->isVectorTy() ||
      !Cond->hasOneUse())
    return false;

  // Only bale "select cond, -1, 0"
  Constant *Src0 = dyn_cast<Constant>(SI->getTrueValue());
  Constant *Src1 = dyn_cast<Constant>(SI->getFalseValue());
  if (Src0 && Src0->isAllOnesValue() && Src1 && Src1->isNullValue()) {
    BaleInfo BI(BaleInfo::CMPDST);
    unsigned OperandNum = 0;
    setOperandBaled(Inst, OperandNum, &BI);
    setBaleInfo(Inst, BI);
  }

  // No baling.
  return false;
}

// Process a store instruction.
void GenXBaling::processStore(StoreInst *Inst) {
  BaleInfo BI(BaleInfo::GSTORE);
  unsigned OperandNum = 0;
  Instruction *V = dyn_cast<Instruction>(Inst->getOperand(OperandNum));
  if (isWrRegion(V))
    setOperandBaled(Inst, OperandNum, &BI);
  setBaleInfo(Inst, BI);
}

/***********************************************************************
 * processPredicate : process predicate operand (to wrregion or branch)
 *
 * Enter:   Inst = instruction with predicate operand
 *          OperandNum = operand number in Inst
 *
 * Return:  whether operand can be baled in
 *
 * If the function returns true, the caller needs to call
 * setOperandBaled(Inst, OperandNum, &BI) to actually bale it in.
 *
 * Unlike most baling, which proceeds in code order building a tree of baled in
 * instructions, this function recurses, scanning backward through the code,
 * because we only want to bale predicate operations all/any/not/rdpredregion
 * once we know that the resulting predicate is used in wrregion or branch (as
 * opposed to say a bitcast to int).
 *
 * So this function decides whether OperandNum in Inst is an instruction that
 * is to be baled in, and additionally performs any further baling in to that
 * instruction.
 */
bool GenXBaling::processPredicate(Instruction *Inst, unsigned OperandNum)
{
  Instruction *Mask = dyn_cast<Instruction>(Inst->getOperand(OperandNum));
  if (Mask && Kind == BalingKind::BK_CodeGen && !isa<VectorType>(Mask->getType())) {
    if (auto Extract = dyn_cast<ExtractValueInst>(Mask)) {
      auto *GotoJoin = cast<Instruction>(Extract->getAggregateOperand());
      unsigned IID = getIntrinsicID(GotoJoin);
      if (IID == Intrinsic::genx_simdcf_goto
          || IID == Intrinsic::genx_simdcf_join) {
        // Second pass: Mask is the extractvalue of the !any(EM) result out of
        // the result of goto/join. We mark both the use of the extract in the
        // branch and the use of the goto/join in the extract as baled. The
        // former is done by the caller when we return true.
        BaleInfo BI;
        setOperandBaled(Mask, /*OperandNum=*/0, &BI);
        setBaleInfo(Mask, BI);
        return true;
      }
    }
  }
  switch (getIntrinsicID(Mask)) {
    case Intrinsic::genx_rdpredregion: {
      if (Kind == BalingKind::BK_CodeGen) {
#if _DEBUG
        // Sanity check the offset and number of elements being accessed.
        unsigned MinSize = Inst->getType()->getScalarType()->getPrimitiveSizeInBits() == 64 ? 4 : 8;
        unsigned NElems = Mask->getType()->getVectorNumElements();
        unsigned Offset = dyn_cast<ConstantInt>(Mask->getOperand(1))->getZExtValue();
        assert(llvm::exactLog2(NElems) >= 0 && (Offset & (std::min(NElems, MinSize) - 1)) == 0 &&
               "illegal offset and/or width in rdpredregion");
#endif
      }
      // We always set up InstMap for an rdpredregion, even though it does not
      // bale in any operands.
      setBaleInfo(Mask, BaleInfo(BaleInfo::RDPREDREGION, 0));
      return true;
    }
    case Intrinsic::genx_all:
    case Intrinsic::genx_any: {
        if (Kind != BalingKind::BK_CodeGen)
          return false; // only bale all/any for CodeGen
        // The mask is the result of an all/any. Bale that in.
        // Also see if its operand can be baled in.
        BaleInfo BI(BaleInfo::ALLANY);
        if (processPredicate(Mask, /*OperandNum=*/0))
          setOperandBaled(Mask, /*OperandNum=*/0, &BI);
        setBaleInfo(Mask, BI);
        return true;
      }
    default:
      if (Mask && isNot(Mask)) {
        // The mask is the result of a notp. Bale that in.
        // Also see if its operand can be baled in.
        BaleInfo BI(BaleInfo::NOTP);
        if (processPredicate(Mask, /*OperandNum=*/0))
          setOperandBaled(Mask, /*OperandNum=*/0, &BI);
        setBaleInfo(Mask, BI);
        return true;
      }
      break;
  }
  return false;
}

/***********************************************************************
 * processSat : set up baling info fp saturate
 */
void GenXBaling::processSat(Instruction *Inst)
{
  BaleInfo BI(BaleInfo::SATURATE);
  // Get the instruction (if any) that creates value to saturate.
  unsigned OperandNum = 0;
  Instruction *V = dyn_cast<Instruction>(Inst->getOperand(OperandNum));
  if (V && V->hasOneUse()) {
    // It is an instruction where we are the only use. We can bale it in, if
    // it is a suitable instruction.
    unsigned ValIntrinID = getIntrinsicID(V);
    if (isRdRegion(ValIntrinID))
      setOperandBaled(Inst, OperandNum, &BI);
    else if (ValIntrinID == Intrinsic::not_intrinsic) {
      if (isa<BinaryOperator>(V) || (isa<CastInst>(V) && !isa<BitCastInst>(V)))
        setOperandBaled(Inst, OperandNum, &BI);
    } else if (!isWrRegion(ValIntrinID)) {
      // V is an intrinsic other than rdregion/wrregion. Check that its return
      // value is suitable for baling.
      GenXIntrinsicInfo II(ValIntrinID);
      if (II.getRetInfo().getSaturation()
          == GenXIntrinsicInfo::SATURATION_DEFAULT)
        setOperandBaled(Inst, OperandNum, &BI);
    }
  }
  // We always set up InstMap for a saturate, even if it does not bale in any
  // operands.
  setBaleInfo(Inst, BI);
}

/***********************************************************************
 * processRdRegion : set up baling info for rdregion
 */
void GenXBaling::processRdRegion(Instruction *Inst)
{
  // See if there is a variable index with an add/sub with an in range
  // offset that we can bale in, such that the add/sub does not already
  // bale in other instructions.
  const unsigned OperandNum = 4; // operand number of index in rdregion
  BaleInfo BI(BaleInfo::RDREGION);
  if (isBalableIndexAdd(Inst->getOperand(OperandNum))) {
    setOperandBaled(Inst, OperandNum, &BI);
    // We always set up InstMap for an address add, even though it does not
    // bale in any operands.
    setBaleInfo(cast<Instruction>(Inst->getOperand(OperandNum)), BaleInfo(BaleInfo::ADDRADD, 0));
  }
  // We always set up InstMap for a rdregion, even if it does not bale in any
  // operands.
  setBaleInfo(Inst, BI);
}

/***********************************************************************
 * static getIndexAdd : test whether the specified value is
 *        a constant add/sub that could be baled in as a variable index offset,
 *        but without checking that the index is in range
 *
 * Enter:   V = the value that might be a constant add/sub
 *          Offset = where to store the offset of the constant add/sub
 *
 * Return:  true if a constant add/sub was detected
 *
 * For the second run of GenXBaling, which is after GenXCategoryConversion,
 * we are looking for an llvm.genx.add.addr rather than a real add/sub.
 */
bool GenXBaling::getIndexAdd(Value *V, int *Offset)
{
  if (Instruction *Inst = dyn_cast<Instruction>(V)) {
    int IsConstAdd = 0;
    switch (Inst->getOpcode()) {
      case Instruction::Add:
        IsConstAdd = 1;
        break;
      case Instruction::Sub:
        IsConstAdd = -1;
        break;
      default:
        if (getIntrinsicID(Inst) == Intrinsic::genx_add_addr)
          IsConstAdd = 1;
        break;
    }
    if (IsConstAdd) {
      if (Constant *C = dyn_cast<Constant>(Inst->getOperand(1))) {
        if (isa<VectorType>(C->getType()))
          C = C->getSplatValue();
        if (C) {
          if (C->isNullValue()) {
            *Offset = 0;
            return true;
          }
          if (ConstantInt *CI = dyn_cast<ConstantInt>(C)) {
            // It is a constant add/sub.
            *Offset = CI->getSExtValue() * IsConstAdd;
            return true;
          }
        }
      }
    }
  }
  return false;
}

/***********************************************************************
 * static isBalableIndexAdd : test whether the specified value is
 *        a constant add/sub that could be baled in as a variable index offset
 *
 * For the second run of GenXBaling, which is after GenXCategoryConversion,
 * we are looking for an llvm.genx.add.addr rather than a real add/sub.
 */
bool GenXBaling::isBalableIndexAdd(Value *V)
{
  int Offset;
  if (!getIndexAdd(V, &Offset))
    return false;
  // It is a constant add/sub. Check the constant is in range.
  // vISA allows [-512,511].
  return (unsigned)Offset + 512U <= 512U + 511U;
}

bool GenXBaling::isHighCostBaling(uint16_t Type, Instruction *Inst) {
  switch (Type) {
  case BaleInfo::WRREGION:
    switch (getIntrinsicID(Inst)) {
    case Intrinsic::genx_dword_atomic_add:
    case Intrinsic::genx_dword_atomic_sub:
    case Intrinsic::genx_dword_atomic_min:
    case Intrinsic::genx_dword_atomic_max:
    case Intrinsic::genx_dword_atomic_xchg:
    case Intrinsic::genx_dword_atomic_or:
    case Intrinsic::genx_dword_atomic_xor:
    case Intrinsic::genx_dword_atomic_imin:
    case Intrinsic::genx_dword_atomic_imax:
    case Intrinsic::genx_dword_atomic_fmin:
    case Intrinsic::genx_dword_atomic_fmax:
    case Intrinsic::genx_dword_atomic_inc:
    case Intrinsic::genx_dword_atomic_dec:
    case Intrinsic::genx_dword_atomic_cmpxchg:
    case Intrinsic::genx_dword_atomic_fcmpwr:
    case Intrinsic::genx_typed_atomic_add:
    case Intrinsic::genx_typed_atomic_sub:
    case Intrinsic::genx_typed_atomic_min:
    case Intrinsic::genx_typed_atomic_max:
    case Intrinsic::genx_typed_atomic_xchg:
    case Intrinsic::genx_typed_atomic_and:
    case Intrinsic::genx_typed_atomic_or:
    case Intrinsic::genx_typed_atomic_xor:
    case Intrinsic::genx_typed_atomic_imin:
    case Intrinsic::genx_typed_atomic_imax:
    case Intrinsic::genx_typed_atomic_fmin:
    case Intrinsic::genx_typed_atomic_fmax:
    case Intrinsic::genx_typed_atomic_inc:
    case Intrinsic::genx_typed_atomic_dec:
    case Intrinsic::genx_typed_atomic_cmpxchg:
    case Intrinsic::genx_typed_atomic_fcmpwr:
    case Intrinsic::genx_gather_scaled:
    case Intrinsic::genx_gather4_scaled:
    case Intrinsic::genx_gather4_typed:
    case Intrinsic::genx_media_ld:
    case Intrinsic::genx_oword_ld:
    case Intrinsic::genx_oword_ld_unaligned:
    case Intrinsic::genx_svm_block_ld:
    case Intrinsic::genx_svm_block_ld_unaligned:
    case Intrinsic::genx_svm_gather:
    case Intrinsic::genx_svm_gather4_scaled:
    case Intrinsic::genx_svm_atomic_add:
    case Intrinsic::genx_svm_atomic_sub:
    case Intrinsic::genx_svm_atomic_min:
    case Intrinsic::genx_svm_atomic_max:
    case Intrinsic::genx_svm_atomic_xchg:
    case Intrinsic::genx_svm_atomic_and:
    case Intrinsic::genx_svm_atomic_or:
    case Intrinsic::genx_svm_atomic_xor:
    case Intrinsic::genx_svm_atomic_imin:
    case Intrinsic::genx_svm_atomic_imax:
    case Intrinsic::genx_svm_atomic_inc:
    case Intrinsic::genx_svm_atomic_dec:
    case Intrinsic::genx_svm_atomic_cmpxchg:
    case Intrinsic::genx_load:
    case Intrinsic::genx_sample:
    case Intrinsic::genx_sample_unorm:
    case Intrinsic::genx_3d_sample:
    case Intrinsic::genx_3d_load:
    case Intrinsic::genx_avs:
    case Intrinsic::genx_raw_send:
    case Intrinsic::genx_raw_sends:
    case Intrinsic::genx_va_convolve2d:
    case Intrinsic::genx_va_hdc_convolve2d:
    case Intrinsic::genx_va_erode:
    case Intrinsic::genx_va_hdc_erode:
    case Intrinsic::genx_va_dilate:
    case Intrinsic::genx_va_hdc_dilate:
    case Intrinsic::genx_va_minmax:
    case Intrinsic::genx_va_minmax_filter:
    case Intrinsic::genx_va_hdc_minmax_filter:
    case Intrinsic::genx_va_bool_centroid:
    case Intrinsic::genx_va_centroid:
    case Intrinsic::genx_va_1d_convolve_horizontal:
    case Intrinsic::genx_va_hdc_1d_convolve_horizontal:
    case Intrinsic::genx_va_1d_convolve_vertical:
    case Intrinsic::genx_va_hdc_1d_convolve_vertical:
    case Intrinsic::genx_va_1pixel_convolve:
    case Intrinsic::genx_va_hdc_1pixel_convolve:
    case Intrinsic::genx_va_1pixel_convolve_1x1mode:
    case Intrinsic::genx_va_lbp_creation:
    case Intrinsic::genx_va_hdc_lbp_creation:
    case Intrinsic::genx_va_lbp_correlation:
    case Intrinsic::genx_va_hdc_lbp_correlation:
    case Intrinsic::genx_va_correlation_search:
    case Intrinsic::genx_va_flood_fill:
      return true;
    }
    break;
  }
  return false;
}

/***********************************************************************
 * processMainInst : set up baling info for potential main instruction
 */
void GenXBaling::processMainInst(Instruction *Inst, int IntrinID)
{
  BaleInfo BI(BaleInfo::MAININST);
  if (IntrinID == Intrinsic::dbg_value)
    return;
  if (IntrinID == Intrinsic::not_intrinsic) {
    if (!isa<BinaryOperator>(Inst) && !isa<CmpInst>(Inst)
        && !isa<CastInst>(Inst) && !isa<SelectInst>(Inst))
      return;
    if (isa<BitCastInst>(Inst))
      return;
    BI.Type = checkModifier(Inst);
    // Work out whether the instruction accepts arithmetic, logic or no
    // modifier.
    int ModType = GenXIntrinsicInfo::MODIFIER_ARITH;
    switch (BI.Type) {
      case BaleInfo::NOTMOD:
        // a "not" can only merge with a logic modifier (another "not")
        ModType = GenXIntrinsicInfo::MODIFIER_LOGIC;
        break;
      case BaleInfo::ZEXT:
      case BaleInfo::SEXT:
        // an extend cannot bale in any other modifier.
        // But as a bodge we allow abs to be baled in to zext/sext. This is a
        // workaround for not having worked out how to set the computation type
        // in cm_abs. Currently cm_abs does a genx.absi in the source type, then
        // converts it to destination type. This does not allow for the result
        // of an abs needing one more bit than its input.
        ModType = MODIFIER_ABSONLY;
        break;
      case BaleInfo::MAININST:
        switch (Inst->getOpcode()) {
          case Instruction::And:
          case Instruction::Or:
          case Instruction::Xor:
            // These instructions take a logic modifier.
            ModType = GenXIntrinsicInfo::MODIFIER_LOGIC;
            break;
          case Instruction::LShr:
          case Instruction::AShr:
          case Instruction::Shl:
            // Do not allow source modifier on integer shift operations,
            // because of extra precision introduced.
            ModType = GenXIntrinsicInfo::MODIFIER_DEFAULT;
            break;
          default:
            // All other (non-intrinsic) instructions take an arith modifier.
            break;
        }
        break;
      default:
        // Anything else is an arith modifier, so it can only merge with
        // another arith modifier.
        break;
    }
    unsigned i = 0;
    if (isa<SelectInst>(Inst)) {
      // Deal specially with operand 0, the selector, of a select.
      const unsigned OperandNum = 0;
      if (processPredicate(Inst, OperandNum))
        setOperandBaled(Inst, OperandNum, &BI);
      ++i;
    }
    // See which operands we can bale in.
    for (unsigned e = Inst->getNumOperands(); i != e; ++i)
      if (operandIsBaled(Inst, i, ModType))
        setOperandBaled(Inst, i, &BI);
  } else if (IntrinID == Intrinsic::genx_convert
      || IntrinID == Intrinsic::genx_convert_addr) {
    // llvm.genx.convert can bale, and has exactly one arg
    if (operandIsBaled(Inst, 0, GenXIntrinsicInfo::MODIFIER_ARITH))
      setOperandBaled(Inst, 0, &BI);
  } else if (isAbs(IntrinID)) {
    BI.Type = BaleInfo::ABSMOD;
    if (operandIsBaled(Inst, 0, GenXIntrinsicInfo::MODIFIER_ARITH))
      setOperandBaled(Inst, 0, &BI);
  } else {
    // For an intrinsic, check the arg info of each arg to see if we can
    // bale into it.
    GenXIntrinsicInfo Info(IntrinID);
    for (const auto *p = Info.getInstDesc(); *p; ++p) {
      GenXIntrinsicInfo::ArgInfo AI(*p);
      if (AI.isArgOrRet() && !AI.isRet()) {
        unsigned ArgIdx = AI.getArgIdx();
        switch (AI.getCategory()) {
          case GenXIntrinsicInfo::GENERAL:
            // This source operand of the intrinsic is general.
            if (operandIsBaled(Inst, ArgIdx, AI.getModifier(), AI.Info))
              setOperandBaled(Inst, ArgIdx, &BI);
            break;
          case GenXIntrinsicInfo::RAW:
            // Rdregion can be baled in to a raw operand as long as it is
            // unstrided and starts on a GRF boundary. Ensure that the input to
            // the rdregion is 32 aligned.
            if (isRegionOKForRaw(Inst->getOperand(ArgIdx), /*IsWrite=*/false)) {
              setOperandBaled(Inst, ArgIdx, &BI);
              if (Liveness) {
                Value *Opnd = Inst->getOperand(ArgIdx);
                Opnd = cast<Instruction>(Opnd)->getOperand(0);
                Liveness->getOrCreateLiveRange(Opnd)->LogAlignment = 5;
              }
            }
            break;
          case GenXIntrinsicInfo::TWOADDR:
            if (Kind == BalingKind::BK_CodeGen) {
              // Record this as a two address send for processing later.
              TwoAddrSends.push_back(cast<CallInst>(Inst));
            }
            break;
          case GenXIntrinsicInfo::PREDICATION:
            // See if there is any baling in to the predicate (mask) operand.
            if (processPredicate(Inst, ArgIdx))
              setOperandBaled(Inst, ArgIdx, &BI);
            break;
        }
      }
    }
  }

  // If this instruction is a modifier, we attempt to simplify it here
  // (i.e. fold constants), to avoid confusion later in GenXVisaFuncWriter
  // if a modifier has a constant operand. Because this pass scans code
  // forwards, a constant will propagate through a chain of modifiers.
  if (BI.Type != BaleInfo::MAININST) {
    Value *Simplified = nullptr;
    if (BI.Type != BaleInfo::ABSMOD) {
      const DataLayout &DL = Inst->getModule()->getDataLayout();
      Simplified = SimplifyInstruction(Inst, SimplifyQuery(DL));
    } else {
      // SimplifyInstruction does not work on abs, so we roll our own for now.
      if (auto C = dyn_cast<Constant>(Inst->getOperand(0))) {
        if (C->getType()->isIntOrIntVectorTy()) {
          if (!ConstantExpr::getICmp(CmpInst::ICMP_SLT, C,
                Constant::getNullValue(C->getType()))->isNullValue())
            C = ConstantExpr::getNeg(C);
        } else {
          if (!ConstantExpr::getFCmp(CmpInst::FCMP_OLT, C,
                Constant::getNullValue(C->getType()))->isNullValue())
            C = ConstantExpr::getFNeg(C);
        }
        Simplified = C;
      }
    }
    if (Simplified) {
      assert(isa<Constant>(Simplified) && "expecting a constant when simplifying a modifier");
      Inst->replaceAllUsesWith(Simplified);
      Inst->eraseFromParent();
      return;
    }
  }

  // Only give an instruction an entry in the map if (a) it is not a main
  // instruction or (b) it bales something in.
  if (BI.Type || BI.Bits)
    setBaleInfo(Inst, BI);
}

/***********************************************************************
 * processBranch : process a branch instruction
 *
 * If the branch is conditional, bale in all/any/not
 */
void GenXBaling::processBranch(BranchInst *Branch)
{
  if (Branch->isConditional()) {
    BaleInfo BI(BaleInfo::MAININST);
    if (processPredicate(Branch, 0/*OperandNum of predicate*/)) {
      setOperandBaled(Branch, 0/*OperandNum*/, &BI);
      setBaleInfo(Branch, BI);
    }
  }
}

/***********************************************************************
 * processTwoAddrSend : process a two-address send
 *
 * A "two-address send" is a send (or an intrinsic that becomes a send in the
 * finalizer) with a potentially partial write, so it has a TWOADDR operand to
 * represent the value of the destination before the operation, and that
 * TWOADDR operand is not undef.
 *
 * This only gets called in the second baling pass.
 *
 * We can bale a rdregion into the TWOADDR operand and bale the send into a
 * wrregion, but only if the two have the same region and "old value" input.
 *
 * We used to allow such baling in first baling, such that legalization would
 * then not split the rdregion and wrregion. In bug 4607, we ran into a problem
 * where code changed due to vector decomposition, and the same baling did not
 * happen in second baling, leaving an illegally wide rdregion or wrregion.
 *
 * So now we only do this special kind of baling in the second baling pass.
 * That means that we have to detect where the rdregion and wrregion have been
 * split by legalization. We use the RdWrRegionSequence class to do that.
 */
void GenXBaling::processTwoAddrSend(CallInst *CI)
{
  unsigned TwoAddrOperandNum = CI->getNumArgOperands() - 1;
  assert(GenXIntrinsicInfo(getIntrinsicID(CI)).getArgInfo(TwoAddrOperandNum)
      .getCategory() == GenXIntrinsicInfo::TWOADDR);
  assert(GenXIntrinsicInfo(getIntrinsicID(CI)).getRetInfo()
      .getCategory() == GenXIntrinsicInfo::RAW);
  // First check the case where legalization did not need to split the rdregion
  // and wrregion.
  auto TwoAddrOperand = dyn_cast<Instruction>(CI->getArgOperand(TwoAddrOperandNum));
  if (!TwoAddrOperand)
    return;
  if (isRdRegion(getIntrinsicID(TwoAddrOperand))) {
    if (!CI->hasOneUse())
      return;
    auto Rd = cast<Instruction>(TwoAddrOperand);
    auto Wr = cast<Instruction>(CI->use_begin()->getUser());
    if (!isWrRegion(getIntrinsicID(Wr)))
      return;
    if (CI->use_begin()->getOperandNo()
        != Intrinsic::GenXRegion::NewValueOperandNum)
      return;
    Region RdR(Rd, BaleInfo());
    Region WrR(Wr, BaleInfo());
    if (RdR != WrR || RdR.Indirect || WrR.Mask)
      return;
    if (!isRegionOKForRaw(Wr, /*IsWrite=*/true))
      return;
    // Everything else is in place for a rd-send-wr baling. We just need to check
    // that the input to the read sequence is the same as the old value input to
    // the write sequence.  We need to allow for some bitcasts in the way. Having
    // different bitcasts on the two inputs is ok, as long as the original value
    // is the same, because bitcasts are always copy coalesced so will be in the
    // same register.
    Value *RdIn = Rd->getOperand(Intrinsic::GenXRegion::OldValueOperandNum);
    Value *WrIn = Wr->getOperand(Intrinsic::GenXRegion::OldValueOperandNum);
    while (auto BC = dyn_cast<BitCastInst>(RdIn))
      RdIn = BC->getOperand(0);
    while (auto BC = dyn_cast<BitCastInst>(WrIn))
      WrIn = BC->getOperand(0);
    if (RdIn != WrIn)
      return;
    // We can do the baling.
    auto BI = getBaleInfo(CI);
    setOperandBaled(CI, TwoAddrOperandNum, &BI);
    setBaleInfo(CI, BI);
    BI = getBaleInfo(Wr);
    setOperandBaled(Wr, Intrinsic::GenXRegion::NewValueOperandNum, &BI);
    setBaleInfo(Wr, BI);
    return;
  }
  // Second, check the case where legalization has split the rdregion and
  // wrregion.
  if (CI->use_empty())
      return;
  if (!isWrRegion(getIntrinsicID(TwoAddrOperand)))
    return;
  RdWrRegionSequence RdSeq;
  if (!RdSeq.buildFromWr(TwoAddrOperand, this))
    return;
  RdWrRegionSequence WrSeq;
  auto Rd = cast<Instruction>(CI->use_begin()->getUser());
  if (!isRdRegion(getIntrinsicID(Rd)))
    return;
  if (!WrSeq.buildFromRd(Rd, this))
    return;
  if (!RdSeq.WrR.isWhole(CI->getType()))
    return;
  if (!WrSeq.RdR.isWhole(CI->getType()))
    return;
  if (RdSeq.RdR.Indirect || WrSeq.WrR.Indirect)
    return;
  if (RdSeq.RdR != WrSeq.WrR)
    return;
  // Everything else is in place for a rd-send-wr baling. We just need to check
  // that the input to the read sequence is the same as the old value input to
  // the write sequence.  We need to allow for some bitcasts in the way. Having
  // different bitcasts on the two inputs is ok, as long as the original value
  // is the same, because bitcasts are always copy coalesced so will be in the
  // same register.
  Value *RdIn = RdSeq.Input;
  Value *WrIn = WrSeq.OldVal;
  while (auto BC = dyn_cast<BitCastInst>(RdIn))
    RdIn = BC->getOperand(0);
  while (auto BC = dyn_cast<BitCastInst>(WrIn))
    WrIn = BC->getOperand(0);
  if (RdIn != WrIn)
    return;
  // Check that there are no uses of CI other than in WrSeq. We can do that by
  // counting the uses.
  unsigned NumUses = 0, Size = WrSeq.size();
  for (auto ui = CI->use_begin(), ue = CI->use_end(); ui != ue; ++ui)
    if (++NumUses > Size)
      return;
  // We can bale, but we need to unlegalize back to a single rdregion and
  // single wrregion.
  auto NewRd = RdSeq.RdR.createRdRegion(RdSeq.Input, RdSeq.StartWr->getName(),
      RdSeq.StartWr, RdSeq.StartWr->getDebugLoc());
  CI->setOperand(TwoAddrOperandNum, NewRd);
  auto NewWr = cast<Instruction>(WrSeq.WrR.createWrRegion(WrSeq.OldVal, CI,
      WrSeq.StartWr->getName(), WrSeq.StartWr, WrSeq.StartWr->getDebugLoc()));
  WrSeq.EndWr->replaceAllUsesWith(NewWr);
  // Set baling info for new instructions. The BI for NewWr is just a copy of
  // the first wrregion in the sequence being replaced.
  setBaleInfo(NewWr, getBaleInfo(WrSeq.StartWr));
  auto BI = getBaleInfo(CI);
  setOperandBaled(CI, TwoAddrOperandNum, &BI);
  setBaleInfo(CI, BI);
  // Remove original sequences if now unused.
  for (Instruction *End = RdSeq.EndWr;;) {
    for (Instruction *Wr = End; Wr && Wr->use_empty(); ) {
      if (!Wr->use_empty())
        break;
      if (Wr->getNumOperands() < 2)
        break;
      auto Rd = dyn_cast<Instruction>(Wr->getOperand(1));
      auto NextWr = dyn_cast<Instruction>(Wr->getOperand(0));
      Liveness->eraseLiveRange(Wr);
      Wr->eraseFromParent();
      assert(Rd);
      if (Rd->use_empty()) {
        Liveness->eraseLiveRange(Rd);
        Rd->eraseFromParent();
      }
      Wr = NextWr;
    }
    if (End == WrSeq.EndWr)
      break;
    End = WrSeq.EndWr;
  }
}

/***********************************************************************
 * setBaleInfo : set BaleInfo for an instruction
 */
void GenXBaling::setBaleInfo(const Instruction *Inst, genx::BaleInfo BI)
{
  assert(BI.Bits < 1 << Inst->getNumOperands());
  InstMap[(const llvm::Value *)Inst] = BI;
}

/***********************************************************************
 * setOperandBaled : set flag to say that an operand is baled in
 *
 * Enter:   Inst = instruction to bale into
 *          OperandNum = operand number in that instruction
 *          BI = BaleInfo to set flag in
 *
 * If the operand value has multiple uses, this also flags that we will need
 * to do some cloning afterwards to ensure that a baled in operand has a
 * single use.
 *
 * Note that a main instruction baled into a saturate modifier or into
 * a wrregion, or a saturate modifier baled into a wrregion, never has
 * multiple uses. So the multiple use thing only covers source operands
 * of the main inst, plus a possible addradd in the wrregion.
 */
void GenXBaling::setOperandBaled(Instruction *Inst, unsigned OperandNum,
    BaleInfo *BI)
{
  // Set the bit.
  BI->Bits |= 1 << OperandNum;
  // Check whether the operand has more than one use.
  Instruction *BaledInst = cast<Instruction>(Inst->getOperand(OperandNum));
  if (!BaledInst->hasOneUse()) {
    // Multiple uses. Add to the NeedClone stack. But not if it is a goto/join;
    // we allow a goto/join to be baled into the extract of its !any(EM) result
    // even though it has uses in other extracts.
    unsigned IID = getIntrinsicID(BaledInst);
    if (IID != Intrinsic::genx_simdcf_goto &&
        IID != Intrinsic::genx_simdcf_join)
      NeedCloneStack.push_back(NeedClone(Inst, OperandNum));
  }
}

/***********************************************************************
 * doClones : do any cloning required to make baled in instructions
 *            single use
 *
 * NeedCloneStack is a stack of operands (instruction and operand number
 * pairs) that are baled in and have more than one use, so need cloning.
 * They were pushed in forward order, so if A is baled into B is baled
 * into C then the use of A in B was pushed before the use of B in C.
 *
 * We now pop off the stack in reverse order. We see the use of B in C,
 * and clone B to single use B'. Then we see that B bales in A, so we
 * add the use of A in B' onto the stack, causing A to be cloned later.
 * In this way we handle nested baling correctly.
 */
void GenXBaling::doClones()
{
  while (NeedCloneStack.size()) {
    // Pop a NeedClone off the stack.
    NeedClone NC = NeedCloneStack.back();
    NeedCloneStack.pop_back();
    // See if it is still multiple use (earlier cloning may have caused this
    // one to become single use).
    Instruction *Opnd = cast<Instruction>(NC.Inst->getOperand(NC.OperandNum));
    if (Opnd->hasOneUse())
      continue;
    // See if it is still baled. But continue with cloning even if not baled in
    // these cases:
    // 1. An extend (zext or sext), because it tends to result in better gen
    //    code, probably because a zext or sext can be baled in to its user by
    //    the finalizer in a case where we cannot because of the vISA
    //    restriction that both operands need the same extend. This case arises
    //    only if we were going to bale the extend in, but then decided not to
    //    because the two operands did not have the same extend.
    // 2. An address generating instruction, because, at this point in the flow
    //    (between GenXCategory and GenXAddressCommoning), an address
    //    generating instruction must have a single use.
    bool IsBaled = getBaleInfo(NC.Inst).isOperandBaled(NC.OperandNum);
    if (!IsBaled && !isa<CastInst>(Opnd)
        && getAddrOperandNum(getIntrinsicID(NC.Inst)) != (int)NC.OperandNum)
      continue;
    // Clone it.
    assert(!isa<PHINode>(Opnd));
    Instruction *Cloned = Opnd->clone();
    Cloned->setName(Opnd->getName());
    // Change the use.
    NC.Inst->setOperand(NC.OperandNum, Cloned);
    if (IsBaled) {
      // Normally, insert the cloned instruction just after the original.
      Cloned->insertAfter(Opnd);
    } else {
      // In the special case that we are cloning something even when not baled:
      // Ensure the cloned instruction has the same category as the original
      // one.
      if (Liveness)
        Liveness->getOrCreateLiveRange(Cloned)->setCategory(
            Liveness->getOrCreateLiveRange(Opnd)->getCategory());
      // Insert the clone just before its single use.
      Cloned->insertBefore(NC.Inst);
      // If the instruction that we cloned is now single use, not in a phi
      // node, move it to just before its use.
      if (Opnd->hasOneUse()) {
        auto User = Opnd->use_begin()->getUser();
        if (!isa<PHINode>(User)) {
          Opnd->removeFromParent();
          Opnd->insertBefore(cast<Instruction>(User));
        }
      }
    }
    // Copy the bale info.
    BaleInfo BI = getBaleInfo(Opnd);
    setBaleInfo(Cloned, BI);
    // Stack any operands of the cloned instruction that are baled. (They
    // must be multiple use because we have just cloned the instruction
    // using them.) Also any address calculation, for the reason given in the
    // comment above.
    int AON = getAddrOperandNum(getIntrinsicID(Cloned));
    for (unsigned i = 0, e = Cloned->getNumOperands(); i != e; ++i)
      if (BI.isOperandBaled(i) ||
          (Kind == BalingKind::BK_CodeGen && AON == (int)i &&
           isa<Instruction>(Cloned->getOperand(i))))
        NeedCloneStack.push_back(NeedClone(Cloned, i));
  }
}

/***********************************************************************
 * getOrUnbaleExtend : get or unbale the extend instruction (if any) in
 *                     this operand
 *
 * Enter:   Inst = instruction containing operand
 *          BI = BaleInfo for Inst
 *          OperandNum = operand number to look at
 *          Unbale = true to unbale the extend
 *
 * Return:  0 if no extend found, else the extend (ZExt or SExt), and, if
 *          Unbale is true, then *BI has been modified _and_ written back
 *          into Inst's map entry in GenXBaling.
 *
 * BI is a pointer to handle two slightly different cases of unbaling the ext:
 * 1. If this is the top level call to getOrUnBaleExtend from processMainInst,
 *    then we want to modify the caller's BaleInfo pointed to by BI, which the
 *    caller is in the middle of setting up and will write back into the map.
 * 2. If this is a recursive call from getOrUnbaleExtend, then we want to
 *    use setBaleInfo to write the BaleInfo back into the map.
 * We don't check which case we have, and we just do both things, as the
 * unneeded one is harmless.
 */
Instruction *GenXBaling::getOrUnbaleExtend(Instruction *Inst, BaleInfo *BI,
    unsigned OperandNum, bool Unbale)
{
  if (!BI->isOperandBaled(OperandNum))
    return nullptr;
  auto Opnd = cast<Instruction>(Inst->getOperand(OperandNum));
  if (isa<ZExtInst>(Opnd) || isa<SExtInst>(Opnd)) {
    // Found an extend. Unbale it if requested. But do not remove it from the
    // NeedClone stack; we still clone an extend that is not being baled in on
    // the basis that the jitter will be able to bale it in because gen allows
    // mismatched integer operand types.
    if (Unbale) {
      BI->clearOperandBaled(OperandNum);
      setBaleInfo(Inst, *BI);
    }
    return Opnd;
  }
  BaleInfo ThisBI = getBaleInfo(Opnd);
  if (ThisBI.isOperandBaled(0))
    return getOrUnbaleExtend(Opnd, &ThisBI, 0, Unbale);
  if (ThisBI.isOperandBaled(1))
    return getOrUnbaleExtend(Opnd, &ThisBI, 1, Unbale);
  return nullptr;
}

/***********************************************************************
 * dump, print : dump the result of the GenXBaling analysis
 */
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void GenXBaling::dump()
{
  print(errs());
}
#endif

void GenXBaling::print(raw_ostream &OS)
{
  for (InstMap_t::iterator i = InstMap.begin(), e = InstMap.end(); i != e; ++i) {
    const Instruction *Inst = cast<const Instruction>(i->first);
    BaleInfo *BI = &i->second;
    OS << Inst->getName() << ": ";
    switch (BI->Type) {
      case BaleInfo::WRREGION: OS << "WRREGION"; break;
      case BaleInfo::SATURATE: OS << "SATURATE"; break;
      case BaleInfo::MAININST: OS << "MAININST"; break;
      case BaleInfo::ABSMOD: OS << "ABSMOD"; break;
      case BaleInfo::NEGMOD: OS << "NEGMOD"; break;
      case BaleInfo::NOTMOD: OS << "NOTMOD"; break;
      case BaleInfo::RDREGION: OS << "RDREGION"; break;
      default: OS << "??"; break;
    }
    for (unsigned OperandNum = 0, e = Inst->getNumOperands();
        OperandNum != e; ++OperandNum)
      if (BI->isOperandBaled(OperandNum))
        OS << " " << OperandNum;
    OS << "\n";
  }
}

/***********************************************************************
 * getBaleParent : return the instruction baled into, 0 if none
 */
Instruction *GenXBaling::getBaleParent(Instruction *Inst)
{
  // We can rely on the fact that a baled in instruction always has exactly
  // one use. The exception is llvm.genx.simdcf.goto/join, which is baled in
  // to the extractvalue that extracts the !any(EM) value. Rather than check
  // the intrinsic ID, we check whether the return type is struct.
  auto use = Inst->use_begin();
  if (!Inst->hasOneUse()) {
    if (!isa<StructType>(Inst->getType()))
      return nullptr;
    // For an llvm.genx.simdcf.goto/join, the use we want is the extractvalue
    // that extracts the !any(EM) value from the result struct.
    for (auto ue = Inst->use_end();; ++use) {
      if (use == ue)
        return nullptr;
      if (!isa<ExtractValueInst>(use->getUser()))
        return nullptr;
      if (use->getUser()->getType()->isIntegerTy(1))
        break;
    }
  }
  Instruction *user = cast<Instruction>(use->getUser());
  BaleInfo BI = getBaleInfo(user);
  if (!BI.isOperandBaled(use->getOperandNo()))
    return nullptr;
  return cast<Instruction>(use->getUser());
}

/***********************************************************************
 * unbale : unbale an instruction from its bale parent
 */
void GenXBaling::unbale(Instruction *Inst)
{
  if (!Inst->hasOneUse())
    return;
  Value::use_iterator use = Inst->use_begin();
  Instruction *user = cast<Instruction>(use->getUser());
  BaleInfo BI = getBaleInfo(user);
  unsigned OperandNum = use->getOperandNo();
  if (!BI.isOperandBaled(OperandNum))
    return;
  BI.clearOperandBaled(OperandNum);
  setBaleInfo(user, BI);
}

/***********************************************************************
 * getBaleHead : return the head of the bale containing Inst
 */
Instruction *GenXBaling::getBaleHead(Instruction *Inst)
{
  for (;;) {
    Instruction *Parent = getBaleParent(Inst);
    if (!Parent)
      break;
    Inst = Parent;
  }
  return Inst;
}

/***********************************************************************
 * buildBale : populate a Bale from the head instruction
 *
 * Enter:   Inst = the head instruction
 *          B = Bale struct, assumed empty
 *          IncludeAddr = default false, true to include address calculations
 *                        even when not baled in
 *
 * IncludeAddr is used by GenXUnbaling to include the address calculation of
 * a rdregion in the bale, so it can be considered together when deciding
 * whether to unbale and move. This works because an address calculation has
 * exactly one use, until GenXAddressCommoning commons them up later.
 */
void GenXBaling::buildBale(Instruction *Inst, Bale *B, bool IncludeAddr)
{
  assert(!B->size());
  buildBaleSub(Inst, B, IncludeAddr);
}

void GenXBaling::buildBaleSub(Instruction *Inst, Bale *B, bool IncludeAddr)
{
  BaleInfo BI = getBaleInfo(Inst);
  B->push_front(BaleInst(Inst, BI));

  if (isa<PHINode>(Inst)
      || (isa<CallInst>(Inst) && getIntrinsicID(Inst) == Intrinsic::not_intrinsic))
    return;
  if (IncludeAddr) {
    int AddrOperandNum = getAddrOperandNum(getIntrinsicID(Inst));
    if (AddrOperandNum >= 0) {
      // IncludeAddr: pretend that the address calculation is baled in, as long
      // as it is an instruction.
      if (auto OpndInst = dyn_cast<Instruction>(Inst->getOperand(AddrOperandNum))) {
        assert(OpndInst->hasOneUse()); (void)OpndInst;
        BI.setOperandBaled(AddrOperandNum);
        B->front().Info = BI;
      }
    }
  }

  assert(BI.Bits < (1 << Inst->getNumOperands()) || Inst->getNumOperands() > 16);

  while (BI.Bits) {
    unsigned Idx = llvm::log2(BI.Bits);
    BI.Bits &= ~(1 << Idx);
    if (Instruction *Op = dyn_cast<Instruction>(Inst->getOperand(Idx)))
      buildBaleSub(Op, B, IncludeAddr);
  }
}

/***********************************************************************
 * getAddrOperandNum : given an intrinsic ID, get the address operand number
 *
 * For rdregion/wrregion, it returns the operand number of the index operand.
 *
 * For genx_add_addr, it returns 0 (the only operand number)
 *
 * In any other case, it returns -1.
 *
 * This is used both in buildBale when IncludeAddr is true, and in doClones,
 * to find the address operand of an instruction.
 */
int GenXBaling::getAddrOperandNum(unsigned IID)
{
  switch (IID) {
    case Intrinsic::genx_rdregioni:
    case Intrinsic::genx_rdregionf:
      return Intrinsic::GenXRegion::RdIndexOperandNum;
    case Intrinsic::genx_wrregioni:
    case Intrinsic::genx_wrregionf:
      return Intrinsic::GenXRegion::WrIndexOperandNum;
    case Intrinsic::genx_add_addr:
      return 0;
    default:
      return -1;
  }
}

/***********************************************************************
 * store : store updated BaleInfo for instruction
 *
 * Enter:   BI = BaleInst struct
 *
 * This function stores BI.Info as the new BaleInfo for BI.Inst
 *
 * It is used by GenXLegalization to unbale.
 */
void GenXBaling::store(BaleInst BI)
{
  assert(BI.Info.Bits < 1<< BI.Inst->getNumOperands());
  InstMap[BI.Inst] = BI.Info;
}

static bool skipTransform(Instruction *DefI, Instruction *UseI) {
  SmallPtrSet<Instruction *, 8> DInsts;
  BasicBlock *BB = UseI->getParent();

  // Special case for extracting out of subroutine call.
  if (isa<ExtractValueInst>(DefI))
   return true;

  // This is a local optimization only.
  for (auto U : DefI->users()) {
    auto UI = dyn_cast<Instruction>(U);
    if (UI == nullptr || UI->getParent() != BB)
      return true;
    if (UI != UseI)
      DInsts.insert(UI);
  }

  // If a use is crossing the next region write,
  // then two regions are live at the same time.
  // Very likely this increases register pressure
  // and/or results region copies.
  //
  // Scan forward starting from Region write,
  // check if this hits a write to this region
  // before some use.
  //
  SmallPtrSet<Instruction *, 8> UInsts;
  bool IsLocal = !UseI->isUsedOutsideOfBlock(BB);
  if (IsLocal) {
    for (auto U : UseI->users()) {
      auto UI = dyn_cast<Instruction>(U);
      if (UI != nullptr)
        UInsts.insert(UI);
    }
  }

  for (auto I = UseI; I; I = I->getNextNode()) {
    if (I == &BB->back())
      break;
    if (DInsts.empty())
      break;

    // UInst is local and it is dead now.
    if (IsLocal && UInsts.empty())
      break;

    // There is a region write before some use.
    if (isWrRegion(I) &&
        I->getOperand(Intrinsic::GenXRegion::OldValueOperandNum) == UseI)
      return true;

    if (DInsts.count(I))
      DInsts.erase(I);
    if (UInsts.count(I))
      UInsts.erase(I);
  }

  // Not all users are checked which means UseI does not
  // dominate them, or UseI is local and dead before some uses.
  return !DInsts.empty();
}

// Cleanup and optimization before do baling on a function.
bool GenXBaling::prologue(Function *F) {
  bool Changed = false;
  auto nextInst = [](BasicBlock &BB, Instruction *I) -> Instruction * {
    // This looks like an llvm bug. We cannot call getPrevNode
    // on the first instruction...
    if (isa<PHINode>(I) || I == &BB.front())
      return nullptr;
    return I->getPrevNode();
  };

  for (auto &BB : F->getBasicBlockList()) {
    // scan the block backwards.
    for (auto Inst = &BB.back(); Inst; Inst = nextInst(BB, Inst)) {
      //
      // Rewrite
      // A = B op C
      // V = wrr(A, R)
      // E = A op D
      // into
      //
      // A = B op C
      // V = wrr(A, R)
      // A' = rrd(V, R)
      // E = A' op D
      //
      if (isWrRegion(Inst)) {
        Instruction *V = dyn_cast<Instruction>(
            Inst->getOperand(Intrinsic::GenXRegion::NewValueOperandNum));

        // Only process the case with multiple uses.
        if (!V || V->hasOneUse())
          continue;

        // Skip if this region write is indirect as
        // this would result an indirect read.
        Region R(Inst, BaleInfo());
        if (R.Indirect)
          continue;

        // Aggressively apply this transform may increase register pressure.
        // We detect if there is other region write in between, so that two
        // outer regions will not be live at the same time.
        if (skipTransform(V, Inst))
          continue;

        // Do this transformation.
        // - Insert a region read right after Inst
        // - Replace all uses other than Inst with this region read
        //
        auto NewV = R.createRdRegion(Inst, "split", Inst, Inst->getDebugLoc(),
                                     /*AllowScalar*/ !V->getType()->isVectorTy());
        assert(NewV->getType() == V->getType());
        Inst->moveBefore(NewV);
        for (auto UI = V->use_begin(); UI != V->use_end(); /*Empty*/) {
          Use &U = *UI++;
          if (U.getUser() != Inst)
            U.set(NewV);
        }
        Changed = true;
      }
    }
  }

  // fold bitcast into store/load if any. This allows to bale a g_store instruction
  // crossing a bitcast.
  for (auto &BB : F->getBasicBlockList()) {
    for (auto I = BB.begin(); I != BB.end(); /*empty*/) {
      Instruction *Inst = &*I++;
      if (isa<LoadInst>(Inst) || isa<StoreInst>(Inst))
        Changed |= foldBitCastInst(Inst) != nullptr;

      // Delete Trivially dead store instructions.
      if (auto ST = dyn_cast<StoreInst>(Inst)) {
        Value *Val = ST->getValueOperand();
        if (auto LI = dyn_cast<LoadInst>(Val)) {
          Value *Ptr = ST->getPointerOperand();
          auto GV1 = getUnderlyingGlobalVariable(Ptr);
          auto GV2 = getUnderlyingGlobalVariable(LI->getPointerOperand());
          if (GV1 && GV1 == GV2) {
            ST->eraseFromParent();
            Changed = true;
          }
        }
      }
    }
    for (auto I = BB.rbegin(); I != BB.rend(); /*empty*/) {
      Instruction *Inst = &*I++;
      if (isInstructionTriviallyDead(Inst))
        Inst->eraseFromParent();
    }
  }

  // Make sure do not store global variables with constants.
  for (auto &BB : F->getBasicBlockList()) {
    for (auto &Inst : BB.getInstList()) {
      if (auto ST = dyn_cast<StoreInst>(&Inst)) {
        // Make sure not to write a constant to global variable directly.
        Constant *C = dyn_cast<Constant>(ST->getValueOperand());
        if (C && getUnderlyingGlobalVariable(ST->getPointerOperand())) {
          loadGlobalStoreConstant(ST);
          Changed = true;
        }
        // Make sure a write region is used to store value. Otherwise, create a
        // copy.
        Value *Val = ST->getValueOperand();
        if (!isWrRegion(Val)) {
          Region R(Val->getType());
          Val = R.createWrRegion(UndefValue::get(Val->getType()), Val, ".copy",
                                 &Inst, Inst.getDebugLoc());
          ST->setOperand(0, Val);
          Changed = true;
        }
      }
    }
  }

  return Changed;
}


/***********************************************************************
 * Bale::getMainInst : get the main instruction from the bale, 0 if none
 */
BaleInst *Bale::getMainInst()
{
  // From the last instruction (the bale head) backwards, find the first
  // one that is not wrregion or saturate or addradd. If the head is
  // wrregion, then skip anything before we reach its value operand.
  // If the first one we find is rdregion, that does not count as a main
  // instruction.
  Value *PossibleMainInst = nullptr;
  for (reverse_iterator i = rbegin(), e = rend(); i != e; ++i) {
    if (PossibleMainInst && PossibleMainInst != i->Inst)
      continue;
    PossibleMainInst = nullptr;
    switch (i->Info.Type) {
      case BaleInfo::WRREGION:
        PossibleMainInst = i->Inst->getOperand(1);
        break;
      case BaleInfo::GSTORE:
        PossibleMainInst = i->Inst->getOperand(0);
        break;
      case BaleInfo::SATURATE:
      case BaleInfo::ADDRADD:
        break;
      case BaleInfo::MAININST:
        return &*i;
      default:
        return nullptr;
    }
  }
  return nullptr;
}

/***********************************************************************
 * eraseFromParent : do eraseFromParent on all instructions in the bale
 */
void Bale::eraseFromParent()
{
  // Iterate in reverse as each instruction becomes unused only when its
  // user in the bale is erased.
  for (reverse_iterator ri = rbegin(), re = rend(); ri != re; ++ri)
    ri->Inst->eraseFromParent();
}

/***********************************************************************
 * Bale::compare : compare this Bale with another one
 *
 * Return:  0 if equivalent
 *          < 0 if less
 *          > 0 if more
 *
 * Two Bales are equivalent if they compute the same value, that is, they
 * have the same opcodes in the instructions, the instructions are
 * baled together in the same way, and the operands coming in from outside
 * the bale are the same.
 *
 * Both bales must have had hash() called on them since being built or
 * modified in any other way.
 */
int Bale::compare(const Bale &Other) const
{
  assert(Hash && Other.Hash);
  if (Hash != Other.Hash)
    return Hash < Other.Hash ? -1 : 1;
  if (size() != Other.size())
    return size() < Other.size() ? -1 : 1;
  for (unsigned i = 0, e = size(); i != e; ++i) {
    if (Insts[i].Info.Bits != Other.Insts[i].Info.Bits)
      return Insts[i].Info.Bits < Other.Insts[i].Info.Bits ? -1 : 1;
    Instruction *Inst = Insts[i].Inst, *OtherInst = Other.Insts[i].Inst;
    if (Inst->getOpcode() != OtherInst->getOpcode())
      return Inst->getOpcode() < OtherInst->getOpcode() ? -1 : 1;
    unsigned NumOperands = Inst->getNumOperands();
    if (NumOperands != OtherInst->getNumOperands())
      return NumOperands < OtherInst->getNumOperands() ? -1 : 1;
    for (unsigned j = 0; j != NumOperands; ++j) {
      Value *Opnd = Inst->getOperand(j);
      if (!Insts[i].Info.isOperandBaled(j)) {
        if (Opnd != OtherInst->getOperand(j))
          return Opnd < OtherInst->getOperand(j) ? -1 : 1;
      } else {
        // Baled operand. Find which baled instruction it is, and check that
        // the other bale has its corresponding instruction used in its
        // corresponding operand.
        // (We could use a map to find the baled instruction
        // in an algorithmically less complex way, but there is not likely
        // to be more than 3 or 4 instructions in the bale so I didn't
        // bother.)
        unsigned BaledInst;
        for (BaledInst = 0; Insts[BaledInst].Inst != Opnd; ++BaledInst) {
          assert(BaledInst != size());
        }
        if (Other.Insts[BaledInst].Inst != OtherInst->getOperand(j))
          return Other.Insts[BaledInst].Inst < OtherInst->getOperand(j) ? -1 : 1;
      }
    }
  }
  return 0;
}

/***********************************************************************
 * hash_value : get a hash_code for a Bale
 *
 * If two Bales are equivalent, they have the same hash_value.
 *
 * If two Bales are not equivalent, it is unlikely but possible that
 * they have the same hash_value.
 */
void Bale::hash()
{
  Hash = 0;
  for (auto i = begin(), e = end(); i != e; ++i) {
    BaleInst BI = *i;
    Hash = hash_combine(Hash, BI.Info.Bits);
    Hash = hash_combine(Hash, BI.Inst->getOpcode());
    for (unsigned j = 0, je = BI.Inst->getNumOperands(); j != je; ++j) {
      Value *Opnd = BI.Inst->getOperand(j);
      if (!BI.Info.isOperandBaled(j)) {
        // Non-baled operand. Hash the operand itself.
        Hash = hash_combine(Hash, Opnd);
      } else {
        // Baled operand. Find which baled instruction it is, and use that
        // index in the hash. (We could use a map to find the baled instruction
        // in an algorithmically less complex way, but there is not likely
        // to be more than 3 or 4 instructions in the bale so I didn't
        // bother.)
        Bale::iterator BaledInst;
        for (BaledInst = begin(); BaledInst->Inst != Opnd; ++BaledInst) {
          assert(BaledInst != i);
        }
        Hash = hash_combine(Hash, BaledInst - begin());
      }
    }
  }
}

/***********************************************************************
 * Bale debug dump/print
 */
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void Bale::dump() const
{
  print(errs());
}
#endif

void Bale::print(raw_ostream &OS) const
{
  OS << "bale {\n";
  for (const_iterator i = begin(), e = end(); i != e; ++i) {
    i->Inst->print(OS);
    OS << " // {" << i->Info.getTypeString() << "}\n";
  }
  OS << "}\n";
}

const char *BaleInfo::getTypeString() const
{
  switch (Type) {
    case BaleInfo::MAININST: return "maininst";
    case BaleInfo::WRREGION: return "wrregion";
    case BaleInfo::SATURATE: return "saturate";
    case BaleInfo::NOTMOD: return "notmod";
    case BaleInfo::NEGMOD: return "negmod";
    case BaleInfo::ABSMOD: return "absmod";
    case BaleInfo::RDREGION: return "rdregion";
    case BaleInfo::ADDRADD: return "addradd";
    case BaleInfo::RDPREDREGION: return "rdpredregion";
    case BaleInfo::ALLANY: return "allany";
    case BaleInfo::NOTP: return "notp";
    case BaleInfo::ZEXT: return "zext";
    case BaleInfo::SEXT: return "sext";
    case BaleInfo::WRPREDREGION: return "wrpreregion";
    case BaleInfo::CMPDST: return "cmpdst";
    case BaleInfo::GSTORE: return "g_store";
    default: return "???";
  }
}

