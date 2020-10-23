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
// Utility functions relating to SIMD CF goto/join.
//
//===----------------------------------------------------------------------===//
#include "GenXGotoJoin.h"
#include "GenX.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/ADT/SetVector.h"

using namespace llvm;
using namespace genx;

/***********************************************************************
 * isEMValue : detect whether a value is an EM (execution mask)
 *
 * It is an EM value if it is an extractvalue instruction extracting element
 * 0 from the struct returned by goto/join.
 */
bool GotoJoin::isEMValue(Value *V)
{
  if (auto EI = dyn_cast<ExtractValueInst>(V)) {
    if (EI->getIndices()[0] == 0/* element number of EM in goto/join struct */) {
      switch (getIntrinsicID(EI->getAggregateOperand())) {
        case Intrinsic::genx_simdcf_goto:
        case Intrinsic::genx_simdcf_join:
          return true;
      }
    }
  }
  return false;
}

/***********************************************************************
 * findJoin : given a goto, find the join whose RM it modifies
 *
 * Return:    the join instruction, 0 if join not found
 */
CallInst *GotoJoin::findJoin(CallInst *Goto)
{
  // Find the RM value from the goto. We know that the only
  // uses of the goto are extracts.
  ExtractValueInst *RM = nullptr;
  for (auto ui = Goto->use_begin(), ue = Goto->use_end(); ui != ue; ++ui) {
    auto Extract = dyn_cast<ExtractValueInst>(ui->getUser());
    if (Extract && Extract->getIndices()[0] == 1/* RM index in struct */) {
      RM = Extract;
      break;
    }
  }
  if (!RM)
    return nullptr;
  // Find the single use of the RM in a join, possibly via phi nodes and
  // other goto instructions.
  CallInst *Join = nullptr;
  SetVector<Instruction *> RMVals;
  RMVals.insert(RM);
  for (unsigned ri = 0; !Join && ri != RMVals.size(); ++ri) {
    auto RM = RMVals[ri];
    for (auto ui = RM->use_begin(), ue = RM->use_end();
        !Join && ui != ue; ++ui) {
      auto User = cast<Instruction>(ui->getUser());
      if (isa<PHINode>(User))
        RMVals.insert(User);
      else switch (getIntrinsicID(User)) {
        case Intrinsic::genx_simdcf_join:
          // We have found the join the RM is for.
          Join = cast<CallInst>(User);
          break;
        case Intrinsic::genx_simdcf_goto: {
          // This is another goto that modifies the same RM. Find the
          // extractvalue for the updated RM value.
          ExtractValueInst *Extract = nullptr;
          for (auto gui = User->use_begin(), gue = User->use_end();
              gui != gue; ++gui) {
            auto ThisExtract = dyn_cast<ExtractValueInst>(gui->getUser());
            if (ThisExtract
                && ThisExtract->getIndices()[0] == 1/*RM index in struct*/) {
              Extract = ThisExtract;
              break;
            }
          }
          if (Extract)
            RMVals.insert(Extract);
          break;
        }
        default:
          return nullptr; // unexpected use of RM
      }
    }
  }
  return Join;
}

/***********************************************************************
 * isValidJoin : check that a join is valid
 *
 * In a block that is a join label (the "true" successor of a goto/join), there
 * must be a join at the start of the block, ignoring phi nodes and bitcasts
 * (which generate no code).
 *
 */
bool GotoJoin::isValidJoin(CallInst *Join)
{
  assert(getIntrinsicID(Join) == Intrinsic::genx_simdcf_join);
  auto BB = Join->getParent();
  // If this block has a goto/join predecessor of which it is "true" successor,
  // check that this block starts with a join -- not necessarily the join we
  // were given.
  if (!isJoinLabel(BB))
    return true;
  auto Inst = BB->getFirstNonPHIOrDbg();
  while (isa<BitCastInst>(Inst))
    Inst = Inst->getNextNode();
  if (getIntrinsicID(Inst) == Intrinsic::genx_simdcf_join)
    return true;
  return false;
}

/***********************************************************************
 * isBranchingJoinLabelBlock : check whether a block has a single join and
 *    is both a join label and a branching join
 *
 * This only works after GenXLateSimdCFConformance.
 *
 * For a block for which this returns true, a pass must not insert code.
 */
bool GotoJoin::isBranchingJoinLabelBlock(BasicBlock *BB)
{
  auto Join = isBranchingJoinBlock(BB);
  if (!Join || Join != BB->getFirstNonPHIOrDbg())
    return false;
  return isJoinLabel(BB);
}

/***********************************************************************
 * isJoinLabel : check whether this block needs to be a join label, because
 *    it is the "true" successor of at least one goto/join branch
 *
 * Enter:   BB = the basic block
 *          SkipCriticalEdgeSplitter = if true, skip a critical edge splitter
 *                block when trying to find a branching goto/join
 *
 * SkipCriticalEdgeSplitter only needs to be set when used from inside
 * GenXSimdCFConformance, before it has removed critical edge splitter blocks
 * that separate a branching goto/join and the join label.
 *
 * A basic block that is a join label needs to start with a join. This
 * function does not test that.
 */
bool GotoJoin::isJoinLabel(BasicBlock *BB, bool SkipCriticalEdgeSplitter)
{
  for (auto ui = BB->use_begin(), ue = BB->use_end(); ui != ue; ++ui) {
    auto PredBr = dyn_cast<BranchInst>(ui->getUser());
    if (!PredBr || ui->getOperandNo() != PredBr->getNumOperands() - 1)
      continue;
    // PredBr is a branch that has BB as its "true" successor. First skip a
    // critical edge splitter.
    auto PredBB = PredBr->getParent();
    if (SkipCriticalEdgeSplitter && PredBr->getNumSuccessors() == 1
        && PredBr == PredBB->getFirstNonPHIOrDbg() && PredBB->hasOneUse()) {
      auto ui2 = PredBB->use_begin();
      PredBr = dyn_cast<BranchInst>(ui2->getUser());
      if (!PredBr || ui2->getOperandNo() != PredBr->getNumOperands() - 1)
        continue;
      PredBB = PredBr->getParent();
    }
    // Check to see if it is a goto/join.
    if (isBranchingGotoJoinBlock(PredBB))
      return true;
  }
  return false;
}

/***********************************************************************
 * isGotoBlock : see if a basic block is a goto block (hence branching),
 *    returning the goto if so
 *
 * See the comment at the top of isBranchingGotoJoinBlock regarding the case
 * of a goto with an unconditional branch.
 */
CallInst *GotoJoin::isGotoBlock(BasicBlock *BB)
{
  auto Goto = isBranchingGotoJoinBlock(BB);
  if (getIntrinsicID(Goto) != Intrinsic::genx_simdcf_goto)
    Goto = nullptr;
  return Goto;
}

/***********************************************************************
 * isBranchingJoinBlock : see if a basic block is a branching
 *    join block, returning the join if so
 */
CallInst *GotoJoin::isBranchingJoinBlock(BasicBlock *BB)
{
  auto Join = isBranchingGotoJoinBlock(BB);
  if (getIntrinsicID(Join) != Intrinsic::genx_simdcf_join)
    Join = nullptr;
  return Join;
}

/***********************************************************************
 * isBranchingGotoJoinBlock : see if a basic block is a branching
 *    goto/join block, returning the goto/join if so
 *
 * This includes the case of a goto with an unconditional branch, as long as
 * this is after GenXLateSimdCFConformance (or during GenX*SimdCFConformance
 * after it has run moveCodeInGotoBlocks), because it relies on
 * moveCodeInGotoBlocks having sunk the goto and its extracts to the end of the
 * block.
 */
CallInst *GotoJoin::isBranchingGotoJoinBlock(BasicBlock *BB)
{
  auto Br = dyn_cast<BranchInst>(BB->getTerminator());
  if (!Br)
    return nullptr;
  if (!Br->isConditional()) {
    // Unconditional branch. Check for the block ending with a goto or an
    // extract from a goto.
    if (Br == &BB->front())
      return nullptr;
    Value *LastInst = Br->getPrevNode();
    if (auto EV = dyn_cast<ExtractValueInst>(LastInst))
      LastInst = EV->getOperand(0);
    if (getIntrinsicID(LastInst) == Intrinsic::genx_simdcf_goto)
      return cast<CallInst>(LastInst);
    return nullptr;
  }
  // Conditional branch. Check for the condition being an extractvalue from a
  // goto/join.
  auto EV = dyn_cast<ExtractValueInst>(Br->getCondition());
  if (!EV)
    return nullptr;
  auto GotoJoin = dyn_cast<CallInst>(EV->getOperand(0));
  if (!GotoJoin || GotoJoin->getParent() != BB)
    return nullptr;
  switch (getIntrinsicID(GotoJoin)) {
    case Intrinsic::genx_simdcf_goto:
    case Intrinsic::genx_simdcf_join:
      return GotoJoin;
  }
  return nullptr;
}

/***********************************************************************
 * getLegalInsertionPoint : ensure an insertion point is legal in the presence
 *    of SIMD CF
 *
 * This is used by a pass that inserts or moves code after
 * GenXLateSimdCFConformance.
 *
 * A branching join label block is not allowed any other code. If the insertion
 * point is in one of those, move up to its immediate dominator.
 *
 * A goto or branching join is not allowed code after the goto/join. If the
 * insertion point is there, move to just before the goto/join.
 */
Instruction *GotoJoin::getLegalInsertionPoint(Instruction *InsertBefore,
    DominatorTree *DomTree)
{
  auto *InsertBB = InsertBefore->getParent();
  while (isBranchingJoinLabelBlock(InsertBB)) {
    InsertBB = DomTree->getNode(InsertBB)->getIDom()->getBlock();
    InsertBefore = InsertBB->getTerminator();
  }
  if (auto GotoJoin = isBranchingGotoJoinBlock(InsertBB))
    InsertBefore = GotoJoin;
  return InsertBefore;
}

