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
/// GenXTidyControlFlow
/// -------------------
///
/// This pass tidies the control flow in the following ways:
///
/// 1. It removes empty blocks (a block is empty if all it contains is an
///    unconditional branch), and thus reduces branch chains in the generated
///    code.  It is needed because often a block inserted by critical edge
///    splitting is not needed for any phi copies.
///
/// 2. It reorders blocks to increase fallthrough generally, and specifically
///    to ensure that SIMD CF goto and join have the required structure: the
///    "false" successor must be fallthrough and the "true" successor must be
///    forward. (The '"true" successor must be forward' requirement is a vISA
///    requirement, because vISA goto/join does not specify JIP, and the
///    finalizer reconstructs it on this assumption.)
///
/// 3. fixGotoOverBranch: The pass spots where there is a SIMD CF goto over an
///    unconditional branch, and turns the combination into a backwards goto.
/// 
///    After reordering blocks, we know that any simd goto has its "false" successor as
///    the following block. If all of the following are true:
/// 
///    a. its "true" successor just branches over that same block;
/// 
///    b. that block contains only an unconditional branch;
/// 
///    c. the UIP of the goto (the join whose RM it updates) is the same as the
///       "true" successor;
/// 
///    d. the goto condition is not constant 0 (this condition is because we
///       cannot represent a backwards simd goto with this, and it is too late to
///       allocate it a register);
/// 
///    then we have the end of a simd do..while loop, and we can optimize to a
///    backwards simd goto.
/// 
///    We represent a backwards simd goto in the IR by having the "true"
///    successor as the following block. GenXVisaFuncWriter can then spot that it
///    is a backwards simd goto, and it needs its condition inverting.
///
/// 4. Ensure that there is a single return block and it is the last block.
///    These are required by the vISA's structurizer.
///
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "GENX_TIDYCONTROLFLOW"

#include "GenX.h"
#include "GenXBaling.h"
#include "GenXGotoJoin.h"
#include "GenXLiveness.h"
#include "GenXModule.h"
#include "GenXNumbering.h"
#include "GenXSubtarget.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Analysis/LoopInfo.h"

using namespace llvm;
using namespace genx;

/***********************************************************************
 * GenXTidyControlFlow pass declaration
 */
namespace {
  class GenXTidyControlFlow : public FunctionPass {
    const GenXSubtarget *ST;
    bool Modified;
  public:
    static char ID;
    explicit GenXTidyControlFlow() : FunctionPass(ID), Modified(false) {}
    virtual StringRef getPassName() const { return "GenX tidy control flow"; }

    void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addPreserved<GenXModule>();
      AU.addPreserved<GenXGroupBaling>();
      AU.addPreserved<GenXLiveness>();
      AU.addPreserved<GenXNumbering>();
      AU.addPreserved<FunctionGroupAnalysis>();
      AU.addRequired<LoopInfoWrapperPass>();
    }

    bool runOnFunction(Function &F);
    // createPrinterPass : get a pass to print the IR, together with the GenX
    // specific analyses
    virtual Pass *createPrinterPass(raw_ostream &O, const std::string &Banner) const
    { return createGenXPrinterPass(O, Banner); }
  private:
    void removeEmptyBlocks(Function *F);
    void reorderBlocks(Function *F);
    void fixGotoOverBranch(Function *F);
    void fixReturns(Function *F);
  };
} // end anonymous namespace.

char GenXTidyControlFlow::ID = 0;

FunctionPass *llvm::createGenXTidyControlFlowPass() {
  return new GenXTidyControlFlow;
}

/***********************************************************************
 * GenXTidyControlFlow::runOnFunction : process a function
 */
bool GenXTidyControlFlow::runOnFunction(Function &F)
{
  auto P = getAnalysisIfAvailable<GenXSubtargetPass>();
  ST = P ? P->getSubtarget() : nullptr;
  Modified = false;
  removeEmptyBlocks(&F);
  reorderBlocks(&F);
  fixGotoOverBranch(&F);
  fixReturns(&F);
  return Modified;
}

/***********************************************************************
 * removeEmptyBlocks
 */
void GenXTidyControlFlow::removeEmptyBlocks(Function *F)
{
  Function::iterator fi = F->begin(), fe = F->end();
  // Don't consider the entry block.
  for (++fi; fi != fe; ) {
    BasicBlock *BB = &*fi;
    // Increment iterator here as we may be removing this block.
    ++fi;
    // FIXME: By claiming preserving liveness, we cannot remove phi(s) in empty
    // blocks. Need to adjust the pass order if such phi(s) really need
    // eliminating.
    BranchInst *BI = dyn_cast<BranchInst>(&BB->front());
    if (!BI || !BI->isUnconditional())
      continue;
    // Do not remove BB if it has more than one predecessor.
    if (!BB->hasOneUse())
      continue;
    // Check if this is a critical edge splitting block whose predecessor is
    // the "false" leg of a goto/join. In that case we do not remove the
    // block, as reorderBlocks below may rely on it to ensure that the "false"
    // successor of a goto/join can be made fallthrough.
    if (BB->hasOneUse()
        && BB->use_begin()->getOperandNo() == 1 /*false successor*/
        && GotoJoin::isBranchingGotoJoinBlock(cast<TerminatorInst>(
            BB->use_begin()->getUser())->getParent())) {
      DEBUG(dbgs() << "removeEmptyBlocks: not removing " << BB->getName() << "\n");
      continue;
    }
    // We are removing this block. First adjust phi nodes in the successor.
    auto Succ = BI->getSuccessor(0);
    adjustPhiNodesForBlockRemoval(Succ, BB);
    // Change all of BB's uses to use its successor instead.
    assert(BB->getSinglePredecessor() != BB && "self loop");
    BB->replaceAllUsesWith(BI->getSuccessor(0));
    BI->eraseFromParent();
    BB->eraseFromParent();
    Modified = true;
  }
}

/***********************************************************************
 * reorderBlocks : reorder blocks to increase fallthrough, and specifically
 *    to satisfy the requirements of SIMD control flow
 */
void GenXTidyControlFlow::reorderBlocks(Function *F)
{
  LoopInfo& LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  if (LI.empty())
    LayoutBlocks(*F);
  else
    LayoutBlocks(*F, LI);
  Modified = true;
}

/***********************************************************************
 * fixGotoOverBranch : fix a (simd) goto over a branch into a backwards goto
 *
 * See the comment at the top of the file.
 */
void GenXTidyControlFlow::fixGotoOverBranch(Function *F)
{
  for (auto fi = F->begin(), fe = F->end(); fi != fe; ++fi) {
    BasicBlock *BB = &*fi;
    auto Goto = GotoJoin::isGotoBlock(BB);
    if (!Goto)
      continue;
    auto Br = cast<BranchInst>(BB->getTerminator());
    if (!Br->isConditional())
      continue;
    // We have a block ending with a conditional branch that is a goto.
    // Now check whether it branches over an unconditional branch.
    auto Succ = BB->getNextNode();
    if (!Succ || !Succ->hasOneUse())
      continue;
    if (Br->getSuccessor(0)->getPrevNode() != Succ)
      continue;
    auto SuccBr = dyn_cast<BranchInst>(Succ->getFirstNonPHIOrDbg());
    if (!SuccBr || SuccBr->isConditional())
      continue;
    // The goto branches over just an unconditional branch.
    // Check whether its UIP is the same as the branch target.
    auto Join = GotoJoin::findJoin(Goto);
    if (!Join || Join->getParent() != Br->getSuccessor(0))
      continue;
    // Check that the goto condition is not constant.
    if (isa<Constant>(Goto->getOperand(2)))
      continue;
    // Change the goto's "false" successor to the target of the unconditional
    // branch, and remove Succ so the goto's "true" successor becomes
    // fallthrough. This then represents a backward goto.
    adjustPhiNodesForBlockRemoval(SuccBr->getSuccessor(0), Succ);
    Br->setSuccessor(1, SuccBr->getSuccessor(0));
    Succ->eraseFromParent();
    Modified = true;
  }
}

/******************************************************************************
 * fixReturns : only keep a single return block and ensure it is the last block
 * of a function.
 */
void GenXTidyControlFlow::fixReturns(Function *F) {
  // This fixup is only when jmpi is disabled.
  // TODO: Cmc should properly layout blocks.
  if (!ST->disableJmpi())
    return;

  // Loop over all of the blocks in a function, tracking all of the blocks
  // that return.
  SmallVector<BasicBlock *, 16> ReturningBlocks;
  for (Function::iterator I = F->begin(), E = F->end(); I != E; ++I)
    if (isa<ReturnInst>(I->getTerminator()))
      ReturningBlocks.push_back(&*I);

  // We need to insert a new basic block into the function,
  // add a PHI nodes (if the function returns values), and convert
  // all of the return instructions into unconditional branches.
  //
  if (ReturningBlocks.size() == 1) {
    BasicBlock *RetBlock = ReturningBlocks.front();
    BasicBlock *LastBlock = &F->back();
    if (LastBlock != RetBlock) {
      RetBlock->moveAfter(LastBlock);
      Modified = true;
    }
  } else if (ReturningBlocks.size() > 1) {
    BasicBlock *NewRetBlock =
        BasicBlock::Create(F->getContext(), "UnifiedReturnBlock", F);
    PHINode *PN = nullptr;
    if (F->getReturnType()->isVoidTy())
      ReturnInst::Create(F->getContext(), nullptr, NewRetBlock);
    else {
      // If the function doesn't return void, add a PHI node to the block.
      PN = PHINode::Create(F->getReturnType(), ReturningBlocks.size(),
                           "UnifiedRetVal");
      NewRetBlock->getInstList().push_back(PN);
      ReturnInst::Create(F->getContext(), PN, NewRetBlock);
    }

    // Loop over all of the blocks, replacing the return instruction with an
    // unconditional branch.
    for (auto BB : ReturningBlocks) {
      // Add an incoming element to the PHI node for every return instruction
      // that is merging into this new block.
      if (PN)
        PN->addIncoming(BB->getTerminator()->getOperand(0), BB);

      BB->getInstList().pop_back(); // Remove the return inst.
      BranchInst::Create(NewRetBlock, BB);
    }
    Modified = true;
  }
}

