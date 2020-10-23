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
/// GenXRegionCollapsing
/// --------------------
///
/// GenX region collapsing pass is function pass that collapses nested
/// read regions or nested write regions.
///
/// Nested region accesses can occur in two ways (or a mixture of both):
///
/// 1. The front end compiler deliberately generates nested region access. The
///    CM compiler does this for a matrix select, generating a region access for
///    the rows and another one for the columns, safe in the knowledge that this
///    pass will combine them where it can.
///
/// 2. Two region accesses in different source code constructs (e.g. two select()
///    calls, either in the same or different source statements).
///
/// The combineRegions() function is what makes the decisions on whether two
/// regions can be collapsed, depending on whether they are 1D or 2D, how the
/// rows of one fit in the rows of the other, whether each is indirect, etc.
///
/// This pass makes an effort to combine two region accesses even if there are
/// multiple bitcasts (from CM format()) or up to one SExt/ZExt (from a cast) in
/// between.
///
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "GENX_RegionCollapsing"

#include "GenX.h"
#include "GenXBaling.h"
#include "GenXRegion.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/Debug.h"

using namespace llvm;
using namespace genx;

namespace {

// GenX region collapsing pass
class GenXRegionCollapsing : public FunctionPass {
  const DataLayout *DL;
  const TargetLibraryInfo *TLI;
  DominatorTree *DT;
  bool Modified;
public:
  static char ID;
  explicit GenXRegionCollapsing() : FunctionPass(ID) { }
  virtual StringRef getPassName() const { return "GenX Region Collapsing"; }
  void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.setPreservesCFG();
  }
  bool runOnFunction(Function &F);
private:
  void runOnBasicBlock(BasicBlock *BB);
  void processBitCast(BitCastInst *BC);
  void processRdRegion(Instruction *InnerRd);
  void splitReplicatingIndirectRdRegion(Instruction *Rd, Region *R);
  void processWrRegionElim(Instruction *OuterWr);
  Instruction *processWrRegionBitCast(Instruction *WrRegion);
  void processWrRegionBitCast2(Instruction *WrRegion);
  Instruction *processWrRegion(Instruction *OuterWr);
  Instruction *processWrRegionSplat(Instruction *OuterWr);
  bool normalizeElementType(Region *R1, Region *R2, bool PreferFirst = false);
  bool combineRegions(const Region *OuterR, const Region *InnerR,
                      Region *CombinedR);
  void calculateIndex(const Region *OuterR, const Region *InnerR,
                      Region *CombinedR, Value *InnerIndex, const Twine &Name,
                      Instruction *InsertBefore, DebugLoc DL);
  Value *insertOp(Instruction::BinaryOps Opcode, Value *Lhs, unsigned Rhs,
                  const Twine &Name, Instruction *InsertBefore, DebugLoc DL);
  Value *insertOp(Instruction::BinaryOps Opcode, Value *Lhs, Value *Rhs,
                  const Twine &Name, Instruction *InsertBefore, DebugLoc DL);
};

}// end namespace llvm


char GenXRegionCollapsing::ID = 0;
namespace llvm { void initializeGenXRegionCollapsingPass(PassRegistry &); }
INITIALIZE_PASS_BEGIN(GenXRegionCollapsing, "GenXRegionCollapsing",
                      "GenXRegionCollapsing", false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(GenXRegionCollapsing, "GenXRegionCollapsing",
                    "GenXRegionCollapsing", false, false)

// Publicly exposed interface to pass...
FunctionPass *llvm::createGenXRegionCollapsingPass()
{
  initializeGenXRegionCollapsingPass(*PassRegistry::getPassRegistry());
  return new GenXRegionCollapsing();
}

/***********************************************************************
 * runOnFunction : run the region collapsing pass for this Function
 */
bool GenXRegionCollapsing::runOnFunction(Function &F)
{
  DL = &F.getParent()->getDataLayout();
  TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();

  // Track if there is any modification to the function.
  bool Changed = false;

  // This does a postordered depth first traversal of the CFG, processing
  // instructions within a basic block in reverse, to ensure that we see a def
  // after its uses (ignoring phi node uses).
  for (po_iterator<BasicBlock *> i = po_begin(&F.getEntryBlock()),
                                 e = po_end(&F.getEntryBlock());
       i != e; ++i) {
    // Iterate until there is no modification.
    BasicBlock *BB = *i;
    do {
      Modified = false;
      runOnBasicBlock(BB);
      if (Modified)
        Changed = true;
    } while (Modified);
  }

  return Changed;
}

static bool lowerTrunc(TruncInst *Inst) {
  Value *InValue = Inst->getOperand(0);
  if (!isRdRegion(InValue))
    return false;

  Type *InElementTy = InValue->getType();
  Type *OutElementTy = Inst->getType();
  unsigned NumElements = 1;
  if (VectorType *VT = dyn_cast<VectorType>(InElementTy)) {
    InElementTy = VT->getElementType();
    OutElementTy = cast<VectorType>(OutElementTy)->getElementType();
    NumElements = VT->getNumElements();
  }
  assert(OutElementTy->getPrimitiveSizeInBits());
  unsigned Stride = InElementTy->getPrimitiveSizeInBits() /
                    OutElementTy->getPrimitiveSizeInBits();

  // Create the new bitcast.
  Instruction *BC =
      CastInst::Create(Instruction::BitCast, InValue,
                       VectorType::get(OutElementTy, Stride * NumElements),
                       Inst->getName(), Inst /*InsertBefore*/);
  BC->setDebugLoc(Inst->getDebugLoc());

  // Create the new rdregion.
  Region R(BC);
  R.NumElements = NumElements;
  R.Stride = Stride;
  R.Width = NumElements;
  R.VStride = R.Stride * R.Width;
  Instruction *NewInst = R.createRdRegion(
      BC, Inst->getName(), Inst /*InsertBefore*/, Inst->getDebugLoc(),
      !isa<VectorType>(Inst->getType()) /*AllowScalar*/);

  // Change uses and mark the old inst for erasing.
  Inst->replaceAllUsesWith(NewInst);
  return true;
}

void GenXRegionCollapsing::runOnBasicBlock(BasicBlock *BB) {
  // Code simplification in block first.
  for (auto BI = BB->begin(), E = --BB->end(); BI != E;) {
    assert(!BI->isTerminator());
    Instruction *Inst = &*BI++;
    if (Inst->use_empty())
      continue;

    // Turn trunc into bitcast followed by rdr. This helps region collapsing in
    // a later stage.
    if (auto TI = dyn_cast<TruncInst>(Inst)) {
      Modified |= lowerTrunc(TI);
      continue;
    }

    // Simplify
    // %1 = call <1 x i32> @rdr(...)
    // %2 = extractelement <1 x i32> %1, i32 0
    // into
    // %2 = call i32 @rdr(...)
    //
    if (auto EEI = dyn_cast<ExtractElementInst>(Inst)) {
      Value *Src = EEI->getVectorOperand();
      if (isRdRegion(Src) && Src->getType()->getVectorNumElements() == 1) {
        // Create a new region with scalar output.
        Region R(Inst);
        Instruction *NewInst =
            R.createRdRegion(Src, Inst->getName(), Inst /*InsertBefore*/,
                             Inst->getDebugLoc(), true /*AllowScalar*/);
        Inst->replaceAllUsesWith(NewInst);
        Modified = true;
        continue;
      }
    }

    if (Value *V = simplifyRegionInst(Inst, DL, TLI)) {
      Inst->replaceAllUsesWith(V);
      Modified = true;
      continue;
    }

    // sink index calculation before region collapsing. For collapsed regions,
    // it is more difficult to lift constant offsets.
    static const unsigned NOT_INDEX = 255;
    unsigned Index = NOT_INDEX;

    unsigned IID = getIntrinsicID(Inst);
    if (isRdRegion(IID))
      Index = Intrinsic::GenXRegion::RdIndexOperandNum;
    else if (isWrRegion(IID))
      Index = Intrinsic::GenXRegion::WrIndexOperandNum;
    else if (isa<InsertElementInst>(Inst))
      Index = 2;
    else if (isa<ExtractElementInst>(Inst))
      Index = 1;

    if (Index != NOT_INDEX) {
      Use *U = &Inst->getOperandUse(Index);
      Value *V = sinkAdd(*U);
      if (V != U->get()) {
        *U = V;
        Modified = true;
      }
    }
  }
  Modified |= SimplifyInstructionsInBlock(BB, TLI);

  // This loop processes instructions in reverse, tolerating an instruction
  // being removed during its processing, and not re-processing any new
  // instructions added during the processing of an instruction.
  for (Instruction *Prev = BB->getTerminator(); Prev;) {
    Instruction *Inst = Prev;
    Prev = nullptr;
    if (Inst != &BB->front())
      Prev = Inst->getPrevNode();
    switch (getIntrinsicID(Inst)) {
    case Intrinsic::genx_rdregioni:
    case Intrinsic::genx_rdregionf:
      processRdRegion(Inst);
      break;
    case Intrinsic::genx_wrregioni:
    case Intrinsic::genx_wrregionf:
      processWrRegionElim(Inst);
      if (!Inst->use_empty()) {
        if (auto NewInst = processWrRegionBitCast(Inst)) {
          Modified = true;
          Inst = NewInst;
        }
        auto NewInst1 = processWrRegionSplat(Inst);
        if (Inst != NewInst1) {
          Modified = true;
          Inst = NewInst1;
        }

        auto NewInst = processWrRegion(Inst);
        processWrRegionBitCast2(NewInst);
        if (Inst != NewInst && NewInst->use_empty()) {
          NewInst->eraseFromParent();
          Modified = true;
        }
      }
      if (Inst->use_empty()) {
        Inst->eraseFromParent();
        Modified = true;
      }
      break;
    default:
      if (auto BC = dyn_cast<BitCastInst>(Inst))
        processBitCast(BC);
      if (isa<CastInst>(Inst) && Inst->use_empty()) {
        // Remove bitcast that has become unused due to changes in this pass.
        Inst->eraseFromParent();
        Modified = true;
      }
      break;
    }
  }
}

/***********************************************************************
 * createBitCast : create a bitcast, combining bitcasts where applicable
 */
static Value *createBitCast(Value *Input, Type *Ty,
    const Twine &Name, Instruction *InsertBefore, DebugLoc DL)
{
  if (Input->getType() == Ty)
    return Input;
  if (auto BC = dyn_cast<BitCastInst>(Input))
    Input = BC->getOperand(0);
  if (Input->getType() == Ty)
    return Input;
  auto NewBC = CastInst::Create(Instruction::BitCast, Input, Ty,
      Name, InsertBefore);
  NewBC->setDebugLoc(DL);
  return NewBC;
}

/***********************************************************************
 * createBitCastToElementType : create a bitcast to a vector with the
 *    specified element type, combining bitcasts where applicable
 */
static Value *createBitCastToElementType(Value *Input, Type *ElementTy,
    const Twine &Name, Instruction *InsertBefore, DebugLoc DL)
{
  unsigned ElBytes = ElementTy->getPrimitiveSizeInBits() / 8U;
  unsigned InputBytes = Input->getType()->getPrimitiveSizeInBits() / 8U;
  assert(!(InputBytes & (ElBytes - 1)) && "non-integral number of elements");
  auto Ty = VectorType::get(ElementTy, InputBytes / ElBytes);
  return createBitCast(Input, Ty, Name, InsertBefore, DL);
}

/***********************************************************************
 * combineBitCastWithUser : if PossibleBC is a bitcast, and it has a single
 *    user that is also a bitcast, then combine them
 *
 * If combined, the two bitcast instructions are erased.
 *
 * This can happen because combining two rdregions with a bitcast between
 * them can result in the bitcast being used by another bitcast that was
 * already there.
 */
static void combineBitCastWithUser(Value *PossibleBC)
{
  if (auto BC1 = dyn_cast<BitCastInst>(PossibleBC)) {
    if (BC1->hasOneUse()) {
      if (auto BC2 = dyn_cast<BitCastInst>(BC1->use_begin()->getUser())) {
        Value *CombinedBC = BC1->getOperand(0);
        if (CombinedBC->getType() != BC2->getType())
          CombinedBC = createBitCast(BC1->getOperand(0), BC2->getType(),
              BC2->getName(), BC2, BC2->getDebugLoc());
        BC2->replaceAllUsesWith(CombinedBC);
        BC2->eraseFromParent();
        BC1->eraseFromParent();
      }
    }
  }
}

/***********************************************************************
 * processBitCast : process a bitcast whose input is rdregion
 *
 * We put the bitcast before the rdregion, in the hope that it will enable
 * the rdregion to be baled in to something later on.
 */
void GenXRegionCollapsing::processBitCast(BitCastInst *BC)
{
  if (BC->getType()->getScalarType()->isIntegerTy(1))
    return;
  auto Rd = dyn_cast<Instruction>(BC->getOperand(0));
  if (!Rd || !Rd->hasOneUse() || !isRdRegion(getIntrinsicID(Rd)))
    return;
  // We have a single use rdregion as the input to the bitcast.
  // Adjust the region parameters if possible so the element type is that of
  // the result of the bitcast, instead of the input.
  Region R(Rd, BaleInfo());
  auto ElTy = BC->getType()->getScalarType();
  if (!R.changeElementType(ElTy))
    return;
  // Create the new bitcast.
  assert(ElTy->getPrimitiveSizeInBits());
  auto Input = Rd->getOperand(Intrinsic::GenXRegion::OldValueOperandNum);
  auto NewBCTy = VectorType::get(ElTy,
      Input->getType()->getPrimitiveSizeInBits() / ElTy->getPrimitiveSizeInBits());
  auto NewBC = CastInst::Create(Instruction::BitCast, Input, NewBCTy, "", Rd);
  NewBC->takeName(BC);
  NewBC->setDebugLoc(BC->getDebugLoc());
  // Create the new rdregion.
  auto NewRd = R.createRdRegion(NewBC, "", Rd, Rd->getDebugLoc(),
      /*AllowScalar=*/!isa<VectorType>(BC->getType()));
  NewRd->takeName(Rd);
  // Replace uses.
  BC->replaceAllUsesWith(NewRd);
  // Caller removes BC.
  Modified = true;
}

/***********************************************************************
 * processRdRegion : process a rdregion
 *
 * 1. If this rdregion is unused, it probably became so in the processing
 *    of a later rdregion. Erase it.
 *
 * 2. Otherwise, see if the input to this rdregion is the result of
 *    an earlier rdregion, and if so see if they can be combined. This can
 *    work even if there are bitcasts and up to one sext/zext between the
 *    two rdregions.
 */
void GenXRegionCollapsing::processRdRegion(Instruction *InnerRd)
{
  if (InnerRd->use_empty()) {
    InnerRd->eraseFromParent();
    Modified = true;
    return;
  }

  // We use Region::getWithOffset to get a Region object for a rdregion/wrregion
  // throughout this pass, in order to ensure that, with an index that is
  // V+const, we get the V and const separately (in Region::Indirect and
  // Region::Offset). Then our index calculations can ensure that the constant
  // add remains the last thing that happens in the calculation.
  Region InnerR = Region::getWithOffset(InnerRd, /*WantParentWidth=*/true);

  // Prevent region collapsing for specific src replication pattern,
  // in order to enable swizzle optimization for Align16 instruction
  if (InnerRd->hasOneUse()) {
    if (auto UseInst = dyn_cast<Instruction>(InnerRd->use_begin()->getUser())) {
      if (UseInst->getOpcode() == Instruction::FMul) {
        auto NextInst = dyn_cast<Instruction>(UseInst->use_begin()->getUser());
        if (NextInst &&
            (NextInst->getOpcode() == Instruction::FAdd ||
             NextInst->getOpcode() == Instruction::FSub) &&
          InnerR.ElementTy->getPrimitiveSizeInBits() == 64U &&
          InnerR.Width == 2 &&
          InnerR.Stride == 0 &&
          InnerR.VStride == 2)
          return;
      }
    }
  }

  for (;;) {
    Instruction *OuterRd = dyn_cast<Instruction>(InnerRd->getOperand(0));
    // Go through any bitcasts and up to one sext/zext if necessary to find the
    // outer rdregion.
    Instruction *Extend = nullptr;
    bool HadElementTypeChange = false;
    for (;;) {
      if (!OuterRd)
        break; // input not result of earlier rdregion
      if (isRdRegion(getIntrinsicID(OuterRd)))
        break; // found the outer rdregion
      if (isa<SExtInst>(OuterRd) || isa<ZExtInst>(OuterRd)) {
        if (OuterRd->getOperand(0)->getType()->getScalarType()->isIntegerTy(1)) {
          OuterRd = nullptr;
          break; // input not result of earlier rdregion
        }
        if (Extend || HadElementTypeChange) {
          OuterRd = nullptr;
          break; // can only have one sext/zext between the rdregions, and
                 // sext/zext not allowed if it is then subject to a bitcast
                 // that changes the element type
        }
        // Remember the sext/zext instruction.
        Extend = OuterRd;
      } else if (isa<BitCastInst>(OuterRd)) {
        if (OuterRd->getType()->getScalarType()
            != OuterRd->getOperand(0)->getType()->getScalarType())
          HadElementTypeChange = true;
      } else {
        OuterRd = nullptr;
        break; // input not result of earlier rdregion
      }
      OuterRd = dyn_cast<Instruction>(OuterRd->getOperand(0));
    }
    if (!OuterRd)
      break; // no outer rdregion that we can combine with
    Region OuterR = Region::getWithOffset(OuterRd);
    // There was a sext/zext. Because we are going to put that after the
    // collapsed region, we want to modify the inner region to the
    // extend's input element type without changing the region parameters
    // (other than scaling the offset). We know that there is no element
    // type changing bitcast between the extend and the inner rdregion.
    if (Extend) {
      if (InnerR.Indirect)
        return; // cannot cope with indexed inner region and sext/zext
      InnerR.ElementTy = Extend->getOperand(0)->getType()->getScalarType();
      unsigned ExtInputElementBytes
            = InnerR.ElementTy->getPrimitiveSizeInBits() / 8U;
      InnerR.Offset = InnerR.Offset / InnerR.ElementBytes * ExtInputElementBytes;
      InnerR.ElementBytes = ExtInputElementBytes;
    }
    // See if the regions can be combined. We call normalizeElementType with
    // InnerR as the first arg so it prefers to normalize to that region's
    // element type if possible. That can avoid a bitcast being put after the
    // combined rdregion, which can help baling later on.
    DEBUG(dbgs() << "GenXRegionCollapsing::processRdRegion:\n"
        "  OuterRd (line " << OuterRd->getDebugLoc().getLine() << "): " << *OuterRd << "\n"
        "  InnerRd (line " << InnerRd->getDebugLoc().getLine() << "): " << *InnerRd << "\n");
    if (!normalizeElementType(&InnerR, &OuterR, /*PreferFirst=*/true)) {
      DEBUG(dbgs() << "Cannot normalize element type\n");
      return;
    }
    Region CombinedR;
    if (!combineRegions(&OuterR, &InnerR, &CombinedR))
      return; // cannot combine

    // If the combined region is both indirect and splat, then do not combine.
    // Otherwise, this leads to an infinite loop as later on we split such
    // region reads.
    auto isIndirectSplat = [](const Region &R) {
      if (!R.Indirect)
        return false;
      if (R.Width != R.NumElements && !R.VStride &&
          !isa<VectorType>(R.Indirect->getType()))
        return true;
      if (R.Width == 1 || R.Stride)
        return false;
      return true;
    };
    if (isIndirectSplat(CombinedR))
      return;

    // Calculate index if necessary.
    if (InnerR.Indirect) {
      calculateIndex(&OuterR, &InnerR, &CombinedR,
          InnerRd->getOperand(Intrinsic::GenXRegion::RdIndexOperandNum),
          InnerRd->getName() + ".indexcollapsed",
          InnerRd, InnerRd->getDebugLoc());
    }
    // If the element type of the combined region does not match that of the
    // outer region, we need to do a bitcast first.
    Value *Input = OuterRd->getOperand(0);
    Input = createBitCastToElementType(Input, CombinedR.ElementTy,
        Input->getName() + ".bitcast_before_collapse", OuterRd, OuterRd->getDebugLoc());
    // Create the combined rdregion.
    Instruction *CombinedRd = CombinedR.createRdRegion(Input,
        InnerRd->getName() + ".regioncollapsed", InnerRd, InnerRd->getDebugLoc(),
        !isa<VectorType>(InnerRd->getType()));
    // If we went through sext/zext, re-instate it here.
    Value *NewVal = CombinedRd;
    if (Extend) {
      auto NewCI = CastInst::Create((Instruction::CastOps)Extend->getOpcode(),
          NewVal, InnerRd->getType(), Extend->getName(), InnerRd);
      NewCI->setDebugLoc(Extend->getDebugLoc());
      NewVal = NewCI;
    }
    // If we still don't have the right type due to bitcasts in the original
    // code, add a bitcast here.
    NewVal = createBitCast(NewVal, InnerRd->getType(),
        NewVal->getName() + ".bitcast_after_collapse", InnerRd,
        InnerRd->getDebugLoc());
    // Replace the inner read with the new value, and erase the inner read.
    // any other instructions between it and the outer read (inclusive) that
    // become unused.
    InnerRd->replaceAllUsesWith(NewVal);
    InnerRd->eraseFromParent();
    Modified = true;
    // Check whether we just created a bitcast that can be combined with its
    // user. If so, combine them.
    combineBitCastWithUser(NewVal);
    InnerRd = CombinedRd;
    InnerR = Region::getWithOffset(InnerRd, /*WantParentWidth=*/true);
    // Because the loop in runOnFunction does not re-process the new rdregion,
    // loop back here to re-process it.
  }
  // InnerRd and InnerR are now the combined rdregion (or the original one if
  // no combining was done).
  // Check whether we have a rdregion that is both indirect and replicating,
  // that we want to split.
  splitReplicatingIndirectRdRegion(InnerRd, &InnerR);
}

/***********************************************************************
 * splitReplicatingIndirectRdRegion : if the rdregion is both indirect and
 *    replicating, split out the indirect part so it is read only once
 */
void GenXRegionCollapsing::splitReplicatingIndirectRdRegion(
    Instruction *Rd, Region *R)
{
  if (!R->Indirect)
    return;
  if (R->Width != R->NumElements && !R->VStride
      && !isa<VectorType>(R->Indirect->getType())) {
    // Replicating rows. We want an indirect region that just reads
    // one row
    Region IndirR = *R;
    IndirR.NumElements = IndirR.Width;
    auto Indir = IndirR.createRdRegion(Rd->getOperand(0),
        Rd->getName() + ".split_replicated_indir", Rd, Rd->getDebugLoc());
    // ... and a direct region that replicates the row.
    R->Indirect = nullptr;
    R->Offset = 0;
    R->Stride = 1;
    auto NewRd = R->createRdRegion(Indir, "", Rd, Rd->getDebugLoc());
    NewRd->takeName(Rd);
    Rd->replaceAllUsesWith(NewRd);
    Rd->eraseFromParent();
    Modified = true;
    return;
  }
  if (R->Width == 1 || R->Stride)
    return;
  // Replicating columns. We want an indirect region that just reads
  // one column
  Region IndirR = *R;
  IndirR.NumElements = IndirR.NumElements / IndirR.Width;
  IndirR.Width = 1;
  auto Indir = IndirR.createRdRegion(Rd->getOperand(0),
      Rd->getName() + ".split_replicated_indir", Rd, Rd->getDebugLoc());
  // ... and a direct region that replicates the column.
  R->Indirect = nullptr;
  R->Offset = 0;
  R->VStride = 1;
  auto NewRd = R->createRdRegion(Indir, "", Rd, Rd->getDebugLoc());
  NewRd->takeName(Rd);
  Rd->replaceAllUsesWith(NewRd);
  Rd->eraseFromParent();
}

/***********************************************************************
 * processWrRegionElim : process a wrregion and eliminate redundant writes
 *
 * This detects the following code:
 *
 *   B = wrregion(A, V1, R)
 *   C = wrregion(B, V2, R)
 *
 * (where "R" is a region that is identical in the two versions
 * this can be collapsed to
 *
 *   D = wrregion(A, V2, R)
 *
 */
void GenXRegionCollapsing::processWrRegionElim(Instruction *OuterWr)
{
  auto InnerWr = dyn_cast<Instruction>(
      OuterWr->getOperand(Intrinsic::GenXRegion::OldValueOperandNum));
  if (!isWrRegion(getIntrinsicID(InnerWr)))
    return;
  // Only perform this optimisation if the only use is with outer - otherwise
  // this seems to make the code spill more
  if (!InnerWr->hasOneUse())
    return;
  Region InnerR(InnerWr, BaleInfo(), /*WantParentWidth=*/true);
  Region OuterR(OuterWr, BaleInfo());
  if (OuterR != InnerR)
    return;
  // Create the combined wrregion.
  Instruction *CombinedWr = cast<Instruction>(OuterR.createWrRegion(
      InnerWr->getOperand(0),
      OuterWr->getOperand(Intrinsic::GenXRegion::NewValueOperandNum),
      OuterWr->getName() + ".regioncollapsed", OuterWr,
      OuterWr->getDebugLoc()));
  OuterWr->replaceAllUsesWith(CombinedWr);
  // Do not erase OuterWr here -- it gets erased by the caller.
  Modified = true;
}

/***********************************************************************
 * processWrRegionBitCast : handle a wrregion whose "new value" is a
 *      bitcast (before processing wrregion for region collapsing)
 *
 * Enter:   Inst = the wrregion
 *
 * Return:  replacement wrregion if any, else 0
 *
 * If the "new value" operand of the wrregion is a bitcast from scalar to
 * 1-vector, or vice versa, then we can replace the wrregion with one that
 * uses the input to the bitcast directly. This may enable later baling
 * that would otherwise not happen.
 *
 * The bitcast typically arises from GenXLowering lowering an insertelement.
 */
Instruction *GenXRegionCollapsing::processWrRegionBitCast(Instruction *WrRegion)
{
  assert(isWrRegion(getIntrinsicID(WrRegion)));
  if (auto BC = dyn_cast<BitCastInst>(WrRegion->getOperand(
          Intrinsic::GenXRegion::NewValueOperandNum))) {
    if (BC->getType()->getScalarType()
        == BC->getOperand(0)->getType()->getScalarType()) {
      // The bitcast is from scalar to 1-vector, or vice versa.
      Region R(WrRegion, BaleInfo());
      auto NewInst = cast<Instruction>(R.createWrRegion(WrRegion->getOperand(0),
            BC->getOperand(0), "", WrRegion, WrRegion->getDebugLoc()));
      NewInst->takeName(WrRegion);
      WrRegion->replaceAllUsesWith(NewInst);
      WrRegion->eraseFromParent();
      return NewInst;
    }
  }
  return nullptr;
}

/***********************************************************************
 * processWrRegionBitCast2 : handle a wrregion whose "new value" is a
 *      bitcast (after processing wrregion for region collapsing)
 *
 * Enter:   WrRegion = the wrregion
 *
 * This does not erase WrRegion even if it becomes unused.
 *
 *
 * If the "new value" operand of the wrregion is some other bitcast, then we
 * change the wrregion to the pre-bitcast type and add new bitcasts for the
 * "old value" input and the result. This makes it possible for the new value
 * to be baled in to the wrregion.
 */
void GenXRegionCollapsing::processWrRegionBitCast2(Instruction *WrRegion)
{
  auto BC = dyn_cast<BitCastInst>(WrRegion->getOperand(
        Intrinsic::GenXRegion::NewValueOperandNum));
  if (!BC)
    return;
  Type *BCInputElementType = BC->getOperand(0)->getType()->getScalarType();
  if (BCInputElementType->isIntegerTy(1))
    return;
  // Get the region params for the replacement wrregion, checking if that
  // fails.
  Region R(WrRegion, BaleInfo());
  if (!R.changeElementType(BCInputElementType))
    return;
  // Bitcast the "old value" input.
  Value *OldVal = createBitCastToElementType(
      WrRegion->getOperand(Intrinsic::GenXRegion::OldValueOperandNum),
      BCInputElementType, WrRegion->getName() + ".precast", WrRegion,
      WrRegion->getDebugLoc());
  // Create the replacement wrregion.
  auto NewInst = cast<Instruction>(R.createWrRegion(OldVal, BC->getOperand(0),
        "", WrRegion, WrRegion->getDebugLoc()));
  NewInst->takeName(WrRegion);
  // Cast it.
  Value *Res = createBitCast(NewInst, WrRegion->getType(),
      WrRegion->getName() + ".postcast", WrRegion, WrRegion->getDebugLoc());
  WrRegion->replaceAllUsesWith(Res);
}

static bool hasMemoryDeps(CallInst *L1, CallInst *L2, Value *Addr,
                          DominatorTree *DT) {

  auto isKill = [=](Instruction &I) {
    Instruction *Inst = &I;
    if (getIntrinsicID(Inst) == Intrinsic::genx_vstore &&
        (Addr == Inst->getOperand(1) ||
         Addr == getUnderlyingGlobalVariable(Inst->getOperand(1))))
      return true;
    // OK.
    return false;
  };

  // vloads from the same block.
  if (L1->getParent() == L2->getParent()) {
    BasicBlock::iterator I = L1->getParent()->begin();
    for (; &*I != L1 && &*I != L2; ++I)
      /*empty*/;
    assert(&*I == L1 || &*I == L2);
    auto IEnd = (&*I == L1) ? L2->getIterator() : L1->getIterator();
    return std::any_of(I->getIterator(), IEnd, isKill);
  }

  // vloads are from different blocks.
  //
  //       BB1 (L1)
  //      /   \
  //   BB3    BB2 (L2)
  //     \     /
  //       BB4
  //
  auto BB1 = L1->getParent();
  auto BB2 = L2->getParent();
  if (!DT->properlyDominates(BB1, BB2)) {
    std::swap(BB1, BB2);
    std::swap(L1, L2);
  }
  if (DT->properlyDominates(BB1, BB2)) {
    // As BB1 dominates BB2, we can recursively check BB2's predecessors, until
    // reaching BB1.
    //
    // check BB1 && BB2
    if (std::any_of(BB2->begin(), L2->getIterator(), isKill))
      return true;
    if (std::any_of(L1->getIterator(), BB1->end(), isKill))
      return true;
    std::set<BasicBlock *> Visited{BB1, BB2};
    std::vector<BasicBlock *> BBs;
    for (auto I = pred_begin(BB2), E = pred_end(BB2); I != E; ++I) {
      BasicBlock *BB = *I;
      if (!Visited.count(BB))
        BBs.push_back(BB);
    }

    // This visits the subgraph dominated by BB1, originated from BB2.
    while (!BBs.empty()) {
      BasicBlock *BB = BBs.back();
      BBs.pop_back();
      Visited.insert(BB);

      // check if there is any store kill in this block.
      if (std::any_of(BB->begin(), BB->end(), isKill))
        return true;

      // Populate not visited predecessors.
      for (auto I = pred_begin(BB), E = pred_end(BB); I != E; ++I)
        if (!Visited.count(*I))
          BBs.push_back(*I);
    }

    // no mem deps.
    return false;
  }

  return true;
}

// Check whether two values are bitwise identical.
static bool isBitwiseIdentical(Value *V1, Value *V2, DominatorTree *DT) {
  assert(V1 && V2 && "null value");
  if (V1 == V2)
    return true;
  if (BitCastInst *BI = dyn_cast<BitCastInst>(V1))
    V1 = BI->getOperand(0);
  if (BitCastInst *BI = dyn_cast<BitCastInst>(V2))
    V2 = BI->getOperand(0);

  // Special case arises from vload/vstore.
  if (getIntrinsicID(V1) == Intrinsic::genx_vload &&
      getIntrinsicID(V2) == Intrinsic::genx_vload) {
    auto L1 = cast<CallInst>(V1);
    auto L2 = cast<CallInst>(V2);

    // Loads from global variables.
    auto GV1 = getUnderlyingGlobalVariable(L1->getOperand(0));
    auto GV2 = getUnderlyingGlobalVariable(L2->getOperand(0));
    Value *Addr = L1->getOperand(0);
    if (GV1 && GV1 == GV2)
      // OK.
      Addr = GV1;
    else if (L1->getOperand(0) != L2->getOperand(0))
      // Check if loading from the same location.
      return false;
    else if (!isa<AllocaInst>(Addr))
      // Check if this pointer is local and only used in vload/vstore.
      return false;

    // Check if there is no store to the same location in between.
    return !hasMemoryDeps(L1, L2, Addr, DT);
  }

  // Cannot prove.
  return false;
}

/***********************************************************************
 * processWrRegion : process a wrregion
 *
 * Enter:   OuterWr = the wrregion instruction that we will attempt to use as
 *                    the outer wrregion and collapse with inner ones
 *
 * Return:  the replacement wrregion if any, otherwise OuterWr
 *
 * This detects the following code:
 *
 *   B = rdregion(A, OuterR)
 *   C = wrregion(B, V, InnerR)
 *   D = wrregion(A, C, OuterR)
 *
 * (where "InnerR" and "OuterR" are the region parameters). This code can
 * be collapsed to
 *
 *   D = wrregion(A, V, CombinedR)
 *
 * We want to do innermost wrregion combining first, but this pass visits
 * instructions in the wrong order for that. So, when we see a wrregion
 * here, we use recursion to scan back to find the innermost one and then work
 * forwards to where we started.
 */
Instruction *GenXRegionCollapsing::processWrRegion(Instruction *OuterWr)
{
  // Find the inner wrregion, skipping bitcasts.
  auto InnerWr = dyn_cast<Instruction>(
      OuterWr->getOperand(Intrinsic::GenXRegion::NewValueOperandNum));
  while (InnerWr && isa<BitCastInst>(InnerWr))
    InnerWr = dyn_cast<Instruction>(InnerWr->getOperand(0));
  if (!isWrRegion(getIntrinsicID(InnerWr)))
    return OuterWr;
  // Process inner wrregions first, recursively.
  InnerWr = processWrRegion(InnerWr);
  // Now process this one.
  // Find the associated rdregion of the outer region, skipping bitcasts,
  // and check it has the right region parameters.
  auto OuterRd = dyn_cast<Instruction>(InnerWr->getOperand(0));
  while (OuterRd && isa<BitCastInst>(OuterRd))
    OuterRd = dyn_cast<Instruction>(OuterRd->getOperand(0));
  if (!isRdRegion(getIntrinsicID(OuterRd)))
    return OuterWr;
  if (!isBitwiseIdentical(OuterRd->getOperand(0), OuterWr->getOperand(0), DT))
    return OuterWr;
  Region InnerR = Region::getWithOffset(InnerWr, /*WantParentWidth=*/true);
  Region OuterR = Region::getWithOffset(OuterWr);
  if (OuterR != Region::getWithOffset(OuterRd))
    return OuterWr;
  // See if the regions can be combined.
  DEBUG(dbgs() << "GenXRegionCollapsing::processWrRegion:\n"
      "  OuterWr (line " << OuterWr->getDebugLoc().getLine() << "): " << *OuterWr << "\n"
      "  InnerWr (line " << InnerWr->getDebugLoc().getLine() << "): " << *InnerWr << "\n");
  if (!normalizeElementType(&OuterR, &InnerR)) {
    DEBUG(dbgs() << "Cannot normalize element type\n");
    return OuterWr;
  }
  Region CombinedR;
  if (!combineRegions(&OuterR, &InnerR, &CombinedR))
    return OuterWr; // cannot combine
  // Calculate index if necessary.
  if (InnerR.Indirect) {
    calculateIndex(&OuterR, &InnerR, &CombinedR,
        InnerWr->getOperand(Intrinsic::GenXRegion::WrIndexOperandNum),
        InnerWr->getName() + ".indexcollapsed", OuterWr, InnerWr->getDebugLoc());
  }
  // Bitcast inputs if necessary.
  Value *OldValInput = OuterRd->getOperand(0);
  OldValInput = createBitCastToElementType(OldValInput, CombinedR.ElementTy,
      OldValInput->getName() + ".bitcast_before_collapse", OuterWr, OuterWr->getDebugLoc());
  Value *NewValInput = InnerWr->getOperand(1);
  NewValInput = createBitCastToElementType(NewValInput, CombinedR.ElementTy,
      NewValInput->getName() + ".bitcast_before_collapse", OuterWr, OuterWr->getDebugLoc());
  // Create the combined wrregion.
  Instruction *CombinedWr = cast<Instruction>(CombinedR.createWrRegion(
      OldValInput,
      NewValInput,
      InnerWr->getName() + ".regioncollapsed", OuterWr,
      InnerWr->getDebugLoc()));
  // Bitcast to the original type if necessary.
  Value *Res = createBitCast(CombinedWr, OuterWr->getType(),
      CombinedWr->getName() + ".cast", OuterWr,
      InnerWr->getDebugLoc());
  // Replace all uses.
  OuterWr->replaceAllUsesWith(Res);
  // Do not erase OuterWr here, as (if this function recursed to process an
  // inner wrregion first) OuterWr might be the same as Prev in the loop in
  // runOnFunction(). For a recursive call of processWrRegion, it will
  // eventually get visited and then erased as it has no uses.  For an outer
  // call of processWrRegion, OuterWr is erased by the caller.
  Modified = true;
  return CombinedWr;
}

/***********************************************************************
 * processWrRegionSplat : process a wrregion
 *
 * Enter:   OuterWr = the wrregion instruction that we will attempt to use as
 *                    the outer wrregion and collapse with inner ones
 *
 * Return:  the replacement wrregion if any, otherwise OuterWr
 *
 * This detects the following code:
 *
 *   C = wrregion(undef, V, InnerR)
 *   D = wrregion(undef, C, OuterR)
 *
 * (where "InnerR" and "OuterR" are the region parameters). This code can
 * be collapsed to
 *
 *   D = wrregion(undef, V, CombinedR)
 *
 * We want to do innermost wrregion combining first, but this pass visits
 * instructions in the wrong order for that. So, when we see a wrregion
 * here, we use recursion to scan back to find the innermost one and then work
 * forwards to where we started.
 */
Instruction *GenXRegionCollapsing::processWrRegionSplat(Instruction *OuterWr)
{
  // Find the inner wrregion, skipping bitcasts.
  auto InnerWr = dyn_cast<Instruction>(
      OuterWr->getOperand(Intrinsic::GenXRegion::NewValueOperandNum));
  while (InnerWr && isa<BitCastInst>(InnerWr))
    InnerWr = dyn_cast<Instruction>(InnerWr->getOperand(0));
  if (!isWrRegion(getIntrinsicID(InnerWr)))
    return OuterWr;
  // Process inner wrregions first, recursively.
  InnerWr = processWrRegionSplat(InnerWr);

  // Now process this one.
  auto InnerSrc = dyn_cast<Constant>(InnerWr->getOperand(0));
  if (!InnerSrc)
    return OuterWr;
  // Ensure that the combined region is well-defined.
  if (InnerSrc->getType()->getScalarSizeInBits() !=
      OuterWr->getType()->getScalarSizeInBits())
    return OuterWr;

  auto OuterSrc = dyn_cast<Constant>(OuterWr->getOperand(0));
  if (!OuterSrc)
   return OuterWr;
  if (isa<UndefValue>(InnerSrc)) {
    // OK.
  } else {
    auto InnerSplat = InnerSrc->getSplatValue();
    auto OuterSplat = OuterSrc->getSplatValue();
    if (!InnerSplat || !OuterSplat || InnerSplat != OuterSplat)
      return OuterWr;
  }

  Region InnerR = Region::getWithOffset(InnerWr, /*WantParentWidth=*/true);
  Region OuterR = Region::getWithOffset(OuterWr);
  Region CombinedR;
  if (!combineRegions(&OuterR, &InnerR, &CombinedR))
    return OuterWr; // cannot combine
  // Calculate index if necessary.
  if (InnerR.Indirect) {
    calculateIndex(&OuterR, &InnerR, &CombinedR,
        InnerWr->getOperand(Intrinsic::GenXRegion::WrIndexOperandNum),
        InnerWr->getName() + ".indexcollapsed", OuterWr, InnerWr->getDebugLoc());
  }
  // Bitcast inputs if necessary.
  Value *OldValInput = OuterSrc;
  Value *NewValInput = InnerWr->getOperand(1);
  NewValInput = createBitCastToElementType(NewValInput, CombinedR.ElementTy,
      NewValInput->getName() + ".bitcast_before_collapse", OuterWr, OuterWr->getDebugLoc());
  // Create the combined wrregion.
  Instruction *CombinedWr = cast<Instruction>(CombinedR.createWrRegion(
      OldValInput,
      NewValInput,
      InnerWr->getName() + ".regioncollapsed", OuterWr,
      InnerWr->getDebugLoc()));
  // Bitcast to the original type if necessary.
  Value *Res = createBitCast(CombinedWr, OuterWr->getType(),
      CombinedWr->getName() + ".cast", OuterWr,
      InnerWr->getDebugLoc());
  // Replace all uses.
  OuterWr->replaceAllUsesWith(Res);
  // Do not erase OuterWr here, as (if this function recursed to process an
  // inner wrregion first) OuterWr might be the same as Prev in the loop in
  // runOnFunction(). For a recursive call of processWrRegionSplat, it will
  // eventually get visited and then erased as it has no uses.  For an outer
  // call of processWrRegionSplat, OuterWr is erased by the caller.
  Modified = true;
  return CombinedWr;
}

/***********************************************************************
 * normalizeElementType : where two regions have different element size,
 *      make them the same if possible
 *
 * Enter:   R1 = first region
 *          R2 = second region
 *          PreferFirst = true to prefer the first region's element type
 *
 * Return:  false if failed
 *
 * If PreferFirst is false, this uses the larger element size if everything is
 * suitably aligned and the region with the smaller element size can be
 * converted to the larger element size.
 *
 * Otherwise, it uses the smaller element size if the region with the
 * larger element size can be converted to the smaller element size.
 */
bool GenXRegionCollapsing::normalizeElementType(Region *R1, Region *R2,
      bool PreferFirst)
{
  if (R1->ElementBytes == R2->ElementBytes)
    return true; // nothing to do
  DEBUG(dbgs() << "Before normalizeElementType:\n"
        "  R1: " << *R1 << "\n"
        "  R2: " << *R2 << "\n");
  // Set BigR to the region with the bigger element size, and SmallR to the
  // region with the smaller element size.
  bool PreferSmall = false;
  Region *BigR = nullptr, *SmallR = nullptr;
  if (R1->ElementBytes > R2->ElementBytes) {
    BigR = R1;
    SmallR = R2;
  } else {
    BigR = R2;
    SmallR = R1;
    PreferSmall = PreferFirst;
  }
  // Try the smaller element size first if it is preferred by the caller.
  if (PreferSmall)
    if (!BigR->Indirect) // big region not indirect
      if (BigR->changeElementType(SmallR->ElementTy))
        return true;
  // Then try the bigger element size.
  if (!SmallR->Indirect) // small region not indirect
    if (SmallR->changeElementType(BigR->ElementTy))
      return true;
  // Then try the smaller element size.
  if (!PreferSmall)
    if (!BigR->Indirect) // big region not indirect
      if (BigR->changeElementType(SmallR->ElementTy))
        return true;
  return false;
}

/***********************************************************************
 * combineRegions : combine two regions if possible
 *
 * Enter:   OuterR = Region struct for outer region
 *          InnerR = Region struct for inner region
 *          CombinedR = Region struct to write combined region into
 *
 * Return:  true if combining is possible
 *
 * If combining is possible, this function sets up CombinedR. However,
 * CombinedR->Offset and CombinedR->Indirect are set assuming that the
 * inner region is direct.
 */
bool GenXRegionCollapsing::combineRegions(const Region *OuterR,
    const Region *InnerR, Region *CombinedR)
{
  DEBUG(dbgs() << "GenXRegionCollapsing::combineRegions\n"
      "  OuterR: " << *OuterR << "\n"
      "  InnerR: " << *InnerR << "\n");
  if (InnerR->Indirect && isa<VectorType>(InnerR->Indirect->getType()))
    return false; // multi indirect not supported
  if (OuterR->Indirect && isa<VectorType>(OuterR->Indirect->getType()))
    return false; // multi indirect not supported
  if (OuterR->Mask)
    return false; // outer region predicated, cannot combine
  *CombinedR = *InnerR;
  CombinedR->Indirect = OuterR->Indirect;
  CombinedR->Stride *= OuterR->Stride;
  CombinedR->VStride *= OuterR->Stride;
  unsigned ElOffset = InnerR->Offset / InnerR->ElementBytes;
  if (OuterR->is2D()) {
    // Outer region is 2D: create the combined offset. For outer 2D
    // and inner indirect, what CombinedR->Offset is set to here is
    // ignored and overwritten by calculateIndex(), so it does not matter
    // that it is incorrect in that case.
    ElOffset = ElOffset / OuterR->Width * OuterR->VStride
        + ElOffset % OuterR->Width * OuterR->Stride;
  } else {
    // Outer region is 1D: create the combined offset. For the benefit
    // of inner indirect, where InnerR->Offset is just an offset from
    // InnerR->Indirect, we cope with InnerR->Offset being apparently
    // out of range (negative or too big).
    ElOffset *= OuterR->Stride;
  }
  CombinedR->Offset = OuterR->Offset + ElOffset * InnerR->ElementBytes;
  if (!OuterR->is2D()) {
    DEBUG(dbgs() << "outer 1D: CombinedR: " << *CombinedR << "\n");
    return true; // outer region is 1D, can always combine
  }
  if (InnerR->isScalar()) {
    DEBUG(dbgs() << "inner scalar/splat: CombinedR: " << *CombinedR << "\n");
    return true; // inner region is scalar/splat, can always combine
  }
  if (InnerR->Indirect) {
    // Indirect inner region. Can combine as long as inner vstride is a
    // multiple of outer width, and it in turn is a multiple of inner parent
    // width.
    if (InnerR->ParentWidth && !(InnerR->VStride % (int)OuterR->Width)
        && !(OuterR->Width % InnerR->ParentWidth)) {
      CombinedR->VStride = InnerR->VStride / OuterR->Width * OuterR->VStride;
      DEBUG(dbgs() << "inner indirect: CombinedR: " << *CombinedR << "\n");
      return true;
    }
    DEBUG(dbgs() << "inner indirect: failed\n");
    return false;
  }
  // Inner region is not indirect.
  unsigned StartEl = InnerR->Offset / InnerR->ElementBytes;
  unsigned StartRow = StartEl / OuterR->Width;
  if (!InnerR->is2D()) {
    // Inner region is 1D but outer region is 2D.
    unsigned EndEl = StartEl + (InnerR->NumElements - 1) * InnerR->Stride;
    unsigned EndRow = EndEl / OuterR->Width;
    if (StartRow == EndRow) {
      // The whole 1D inner region fits in a row of the outer region.
      DEBUG(dbgs() << "inner 1D outer 2D, fits in row: CombinedR: " << *CombinedR << "\n");
      return true;
    }
    if (EndRow == StartRow + 1 && !(InnerR->NumElements % 2)) {
      unsigned MidEl = StartEl + InnerR->NumElements / 2 * InnerR->Stride;
      if (InnerR->Stride > 0 && (unsigned)(MidEl - (EndRow * OuterR->Width))
            < (unsigned)InnerR->Stride) {
        // The 1D inner region is evenly split between two adjacent rows of
        // the outer region.
        CombinedR->VStride = (MidEl % OuterR->Width - StartEl % OuterR->Width)
            * OuterR->Stride + OuterR->VStride;
        CombinedR->Width = InnerR->NumElements / 2;
        DEBUG(dbgs() << "inner 1D outer 2D, split between two rows: CombinedR: " << *CombinedR << "\n");
        return true;
      }
    }
    unsigned BeyondEndEl = EndEl + InnerR->Stride;
    if (BeyondEndEl % OuterR->Width == StartEl % OuterR->Width
        && !(OuterR->Width % InnerR->Stride)) {
      // The 1D inner region is evenly split between N adjacent rows of the
      // outer region, starting in the same column for each row.
      CombinedR->Width = OuterR->Width / InnerR->Stride;
      CombinedR->VStride = OuterR->VStride;
      DEBUG(dbgs() << "inner 1D outer 2D, split between N rows: CombinedR: " << *CombinedR << "\n");
      return true;
    }
    DEBUG(dbgs() << "inner 1D outer 2D, fail\n");
    return false; // All other 1D inner region cases fail.
  }
  if (!(InnerR->VStride % (int)OuterR->Width)) {
    // Inner vstride is a whole number of outer rows.
    CombinedR->VStride = OuterR->VStride * InnerR->VStride / (int)OuterR->Width;
    if (!InnerR->Indirect) {
      // For a direct inner region, calculate whether we can combine.
      unsigned StartEl = InnerR->Offset / InnerR->ElementBytes;
      unsigned StartRow = StartEl / OuterR->Width;
      unsigned EndRowOfFirstRow = (StartEl + (InnerR->Width - 1) * InnerR->Stride)
            / OuterR->Width;
      if (StartRow == EndRowOfFirstRow) {
        // Each row of inner region is within a row of outer region, starting
        // at the same column.
        DEBUG(dbgs() << "row within row: CombinedR: " << *CombinedR << "\n");
        return true;
      }
    } else {
      // For an indirect inner region, use parent width to tell whether we can
      // combine.
      if (InnerR->ParentWidth && !(OuterR->Width % InnerR->ParentWidth)) {
        DEBUG(dbgs() << "inner indirect, parentwidth ok: CombinedR: " << *CombinedR << "\n");
        return true;
      }
    }
  }
  // We could handle other cases like:
  //  - each row of inner region enclosed in a row of outer region
  //    but with a different column offset
  DEBUG(dbgs() << "failed\n");
  return false;
}

/***********************************************************************
 * calculateIndex : calculate index in the case that the inner region is
 *      indirect
 *
 * Enter:   OuterR, InnerR = outer and inner regions
 *          CombinedR = combined region set up by combineRegions()
 *          InnerIndex = variable index for inner region, including the
 *              constant offset add that was extracted by the Region
 *              constructor into InnerR->Offset
 *          Name = name for new instruction(s)
 *          InsertBefore = insert before this instruction
 *          DL = debug loc for new instruction(s)
 *
 * This sets up CombinedR->Indirect and CombinedR->Offset.
 *
 * A Region has the offset set up as follows:
 *
 *  - For a direct region, R.Offset is the constant offset in bytes and
 *    R.Indirect is 0.
 *
 *  - Normally, for an indirect region, R.Offset is 0 and R.Indirect is the
 *    Value used for the offset (in bytes).
 *
 *  - But if the Value used for the offset is an add constant, then R.Offset
 *    is the constant offset and R.Indirect is the other operand of the add.
 *
 * In some code paths, this function needs the actual index of the inner region,
 * rather than the R.Offset and R.Indirect parts separated out by the Region
 * constructor. Thus it is passed InnerIndex, which is that actual index value.
 */
void GenXRegionCollapsing::calculateIndex(const Region *OuterR,
    const Region *InnerR, Region *CombinedR, Value *InnerIndex,
    const Twine &Name, Instruction *InsertBefore, DebugLoc DL)
{
  if (!OuterR->is2D()) {
    // Outer region is 1D. We can leave CombinedR->Offset as
    // set by combineRegions, but we need to add the indices together, scaling
    // the inner one by the outer region's stride.
    Value *Idx = InnerR->Indirect;
    if (OuterR->Stride != 1) {
      Idx = insertOp(Instruction::Mul, Idx, OuterR->Stride, Name,
          InsertBefore, DL);
      DEBUG(dbgs() << " calculateIndex: " << *Idx << "\n");
    }
    if (OuterR->Indirect) {
      Idx = insertOp(Instruction::Add, Idx, OuterR->Indirect, Name,
          InsertBefore, DL);
      DEBUG(dbgs() << " calculateIndex: " << *Idx << "\n");
    }
    CombinedR->Indirect = Idx;
    DEBUG(dbgs() << " calculateIndex result(1d): CombinedR: " << *CombinedR << "\n");
    return;
  }
  // Outer region is 2D. We need to split the inner region's index into row
  // and column of the outer region, then recombine. We are using InnerIndex,
  // which includes any constant offset add, so we need to adjust
  // CombinedR->Offset so it does not include InnerR->Offset.
  CombinedR->Offset = OuterR->Offset;
  DEBUG(dbgs() << " calculateIndex: Offset now " << CombinedR->Offset << "\n");
  Value *Col = insertOp(Instruction::URem, InnerIndex,
      OuterR->Width * OuterR->ElementBytes,
      Name, InsertBefore, DL);
  DEBUG(dbgs() << " calculateIndex: " << *Col << "\n");
  Value *Row = insertOp(Instruction::UDiv, InnerIndex,
      OuterR->Width * OuterR->ElementBytes,
      Name, InsertBefore, DL);
  DEBUG(dbgs() << " calculateIndex: " << *Row << "\n");
  Value *Idx = nullptr;
  if (!(OuterR->VStride % OuterR->Stride)) {
    // We need to multply Row by VStride and Col by Stride. However, Stride
    // divides VStride evenly, so we can common up the multiply by Stride.
    Idx = insertOp(Instruction::Mul, Row,
        OuterR->VStride * OuterR->ElementBytes / OuterR->Stride,
        Name, InsertBefore, DL);
    DEBUG(dbgs() << " calculateIndex: " << *Idx << "\n");
    Idx = insertOp(Instruction::Add, Idx, Col, Name, InsertBefore, DL);
    DEBUG(dbgs() << " calculateIndex: " << *Idx << "\n");
    Idx = insertOp(Instruction::Mul, Idx, OuterR->Stride, Name, InsertBefore, DL);
    DEBUG(dbgs() << " calculateIndex: " << *Idx << "\n");
  } else {
    // Need to do Row*VStride and Col*Stride separately.
    Idx = insertOp(Instruction::Mul, Row,
        OuterR->VStride * OuterR->ElementBytes, Name, InsertBefore, DL);
    DEBUG(dbgs() << " calculateIndex: " << *Idx << "\n");
    Col = insertOp(Instruction::Mul, Col, OuterR->Stride, Name, InsertBefore, DL);
    DEBUG(dbgs() << " calculateIndex: " << *Col << "\n");
    Idx = insertOp(Instruction::Add, Idx, Col, Name, InsertBefore, DL);
    DEBUG(dbgs() << " calculateIndex: " << *Idx << "\n");
  }
  if (OuterR->Indirect) {
    Idx = insertOp(Instruction::Add, Idx, OuterR->Indirect,
        Name, InsertBefore, DL);
    DEBUG(dbgs() << " calculateIndex: " << *Idx << "\n");
  }
  CombinedR->Indirect = Idx;
  DEBUG(dbgs() << " calculateIndex result(2d): CombinedR: " << *CombinedR << "\n");
}

/***********************************************************************
 * insertOp : insert a binary op
 */
Value *GenXRegionCollapsing::insertOp(Instruction::BinaryOps Opcode, Value *Lhs,
    unsigned Rhs, const Twine &Name, Instruction *InsertBefore, DebugLoc DL)
{
  auto I16Ty = Type::getInt16Ty(InsertBefore->getContext());
  return insertOp(Opcode, Lhs,
      Constant::getIntegerValue(I16Ty, APInt(16, Rhs)),
      Name, InsertBefore, DL);
}

Value *GenXRegionCollapsing::insertOp(Instruction::BinaryOps Opcode, Value *Lhs,
    Value *Rhs, const Twine &Name, Instruction *InsertBefore, DebugLoc DL)
{
  if (auto C = dyn_cast<ConstantInt>(Rhs)) {
    int RhsVal = C->getZExtValue();
    int LogVal = llvm::exactLog2(RhsVal);
    if (LogVal >= 0) {
      switch (Opcode) {
        case Instruction::Mul:
          // multiply by power of 2 -> shl
          if (!LogVal)
            return Lhs;
          Rhs = Constant::getIntegerValue(C->getType(), APInt(16, LogVal));
          Opcode = Instruction::Shl;
          break;
        case Instruction::UDiv:
          // divide by power of 2 -> lshr
          if (!LogVal)
            return Lhs;
          Rhs = Constant::getIntegerValue(C->getType(), APInt(16, LogVal));
          Opcode = Instruction::LShr;
          break;
        case Instruction::URem:
          // remainder by power of 2 -> and
          Rhs = Constant::getIntegerValue(C->getType(), APInt(16, RhsVal - 1));
          Opcode = Instruction::And;
          break;
        default:
          break;
      }
    }
  }
  auto Inst = BinaryOperator::Create(Opcode, Lhs, Rhs, Name, InsertBefore);
  Inst->setDebugLoc(DL);
  return Inst;
}

