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
/// GenXPostLegalization
/// --------------------
///
/// GenXPostLegalization is a function pass run after legalization with the
/// following purposes:
///
/// 1. It inserts a constant load for most constants that are not representable
///    as a constant operand in GenX code. See the GenXConstants section below.
//     (in the file GenXConstants.cpp)
///
/// 2. It calls GenXVectorDecomposer to perform vector decomposition. See the
///    GenXVectorDecomposer section below.
//     (in the file GenXVectorDecomposer.h)
///
/// Both of these things are done here because the results of them (constant
/// loads and decomposed vector operations) may benefit from CSE run after
/// this pass.
///
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "GENX_POST_LEGALIZATION"

#include "GenX.h"
#include "GenXBaling.h"
#include "GenXRegion.h"
#include "GenXSubtarget.h"
#include "GenXVectorDecomposer.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include <set>

using namespace llvm;
using namespace genx;
using namespace Intrinsic::GenXRegion;

namespace {

// GenXPostLegalization : post-legalization pass
class GenXPostLegalization : public FunctionPass {
  DominatorTree *DT;
  VectorDecomposer VD;
  const DataLayout *DL;
  const GenXSubtarget *ST;
  const TargetLibraryInfo *TLI;
public:
  static char ID;
  explicit GenXPostLegalization() : FunctionPass(ID) { }
  virtual StringRef getPassName() const { return "GenX post-legalization pass"; }
  void getAnalysisUsage(AnalysisUsage &AU) const;
  bool runOnFunction(Function &F);
};

} // end namespace llvm


char GenXPostLegalization::ID = 0;
namespace llvm { void initializeGenXPostLegalizationPass(PassRegistry &); }
INITIALIZE_PASS_BEGIN(GenXPostLegalization, "GenXPostLegalization", "GenXPostLegalization", false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(GenXPostLegalization, "GenXPostLegalization", "GenXPostLegalization", false, false)

FunctionPass *llvm::createGenXPostLegalizationPass()
{
  initializeGenXPostLegalizationPass(*PassRegistry::getPassRegistry());
  return new GenXPostLegalization;
}

void GenXPostLegalization::getAnalysisUsage(AnalysisUsage &AU) const
{
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  AU.setPreservesCFG();
}

/***********************************************************************
 * GenXPostLegalization::runOnFunction : process one function
 */
bool GenXPostLegalization::runOnFunction(Function &F)
{
  DL = &F.getParent()->getDataLayout();
  auto P = getAnalysisIfAvailable<GenXSubtargetPass>();
  if (P)
    ST = P->getSubtarget();
  else
    return false;
  TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();

  bool Modified = false;
  Modified |= breakConstantExprs(&F);

  for (Function::iterator fi = F.begin(), fe = F.end(); fi != fe; ++fi) {
    BasicBlock *BB = &*fi;
    for (BasicBlock::iterator bi = BB->begin(), be = BB->end(); bi != be; ++bi) {
      Instruction *Inst = &*bi;
      switch (getIntrinsicID(Inst)) {
      default:
        // Lower non-simple constant operands.
        Modified |= loadNonSimpleConstants(Inst);
        break;
      case Intrinsic::fma:
        Modified |= loadConstants(Inst);
        break;
      }

      // If this is a wrregion with constant input, or phi node input, give it
      // to the vector decomposer. (We could just give it all wrregions, but we
      // are trying to minimize the amount of work it has to do.)
      if (!ST->disableVectorDecomposition()) {
        if (isWrRegion(getIntrinsicID(Inst))) {
          if (isa<Constant>(Inst->getOperand(0)))
            VD.addStartWrRegion(Inst);
          else if (isa<PHINode>(Inst->getOperand(0)))
            VD.addStartWrRegion(Inst);
        }
      }
    }
  }
  // Run the vector decomposer for this function.
  Modified |= VD.run(DT);
  // Cleanup region reads and writes.
  Modified |= simplifyRegionInsts(&F, DL, TLI);
  // Cleanup redundant global loads.
  Modified |= cleanupLoads(&F);
  // Legalize constants in return.
  for (auto FI = F.begin(), FE = F.end(); FI != FE; ++FI) {
    BasicBlock *BB = &*FI;
    for (auto BI = BB->begin(), BE = BB->end(); BI != BE; ++BI) {
      Instruction *Inst = &*BI;
      if (isa<ReturnInst>(Inst)) {
        Modified |= loadNonSimpleConstants(Inst);
        Modified |= loadConstants(Inst);
      }
    }
  }

  return Modified;
}

