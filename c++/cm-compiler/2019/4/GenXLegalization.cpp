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
/// GenXLegalization
/// ----------------
///
/// GenXLegalization is a function pass that splits vector instructions
/// up to make execution widths legal, and to ensure that the GRF crossing rules
/// are satisfied.
///
/// This pass makes the LLVM IR closer to legal vISA by
/// splitting up any instruction that has an illegal vector width (too big or non
/// power of two) or an illegal region (illegal vstride/width/stride or illegal GRF
/// crossing).
///
/// **IR restriction**: After this pass, LLVM IR represents vISA instructions
/// with legal execution width and region parameters, and with any particular
/// instruction's region restrictions adhered to.
///
/// The pass uses the instruction baling information to tell which
/// regions an instruction has. Splitting an instruction and its regions needs to
/// be done with reference to all the regions at the same time, as they may need
/// splitting at different points.
///
/// For general values, an illegal width instruction is split by
/// creating narrower instructions, each of which uses a rdregion to extract the
/// subregion for each source operand, and then uses a wrregion to insert the
/// resulting subregion into the original destination value. The original illegal
/// width values survive, and that is OK because a vISA register can have any
/// vector width.
/// 
/// The pass uses the hasIndirectGRFCrossing feature from GenXSubtarget when
/// calculating whether a region is legal, or how a region needs to be split, in
/// the case that the region is indirect.
///
/// The legalization pass considers a bale of instructions as a separate
/// entity which can be split without reference to other bales. This works because
/// the overhead of splitting, which is an extra rdregion per operand and an extra
/// wrregion on the result, is pretty much free in that these extra region accesses
/// are baled in to the split instruction.
///
/// There are some cases where we decide we need to unbale an instruction, i.e.
/// remove it (or rather the subtree of instructions in the bale rooted at it)
/// from the bale, and then re-start the analysis for the bale. This happens
/// when there are two conflicting requirements in the bale, for example a main
/// instruction that needs at least simd4 but a rdregion that can only manage
/// simd2.
///
/// The pass scans backwards through the code, which makes this unbaling a bit
/// easier. An unbaled instruction will be encountered again a bit later, and
/// be processed as its own bale.
///
/// If a source operand being split is already an rdregion, then that rdregion is
/// split, so the new split rdregions read from the original rdregion's input.
///
/// Similarly, if the bale is already headed by an wrregion, it is replaced by
/// the new split wrregions used to join the splits back together.
///
/// BitCast is not split in this pass. A non-category-converting BitCast is
/// always coalesced in GenXCoalescing, so never generates actual code. Thus it
/// does not matter if it has an illegal size.
///
/// Predicate legalization
/// ^^^^^^^^^^^^^^^^^^^^^^
///
/// Predicates (vector of i1) are more complex. A general vISA value can be any
/// vector width, but a predicate can only be a power of two up to 32. Thus the
/// actual predicate values need to be split, not just the reads from and writes
/// to the values.
///
/// Furthermore, although it is possible to read and write a region within a
/// predicate, using H1/H2/Q1..Q4 flags, there are restrictions: the start
/// offset must be 8 aligned (4 aligned for a select or cmp with 64-bit
/// operands), and the size must be no more than the misalignment of the start
/// offset (e.g. for a start offset of 8, the size can be 8 but not 16).
///
/// So this pass splits an arbitrary size predicate value (including predicate phi
/// nodes) into as many as possible 32 bit parts, then descending power of two parts.
/// For example, a predicate of size 37 is split into 32,4,1.
///
/// Then, within each part, a read or write of the predicate can be further split
/// as long as it fits the restrictions above, e.g. a 32 bit part can be read/written
/// in 8 or 16 bit subregions.
///
/// This is achieved in two steps:
///
/// 1. Predicates take part in the main code of GenXLegalization. When deciding how
///    to split a read or write of a predicate, we determine how the predicate value
///    will be split into parts (e.g. the 37 split into 32,4,1 example above), then
///    decides how a part could be subregioned if necessary (e.g. the 32 could have
///    a 16 aligned 16 bit region, or an 8 aligned 8 bit region). As well as a
///    maximum, this usually gives a minimum size region. If the rest of the bale
///    cannot achieve that minimum size, then we unbale to avoid the problem and
///    restart the analysis of the bale.
///
/// 2. Then, fixIllegalPredicates() actually divides the illegally sized
///    predicate values, including phi nodes. The splitting in the main part of
///    GenXLegalization ensures that no read or write of a predicate value
///    crosses a part boundary, so it is straightforward to split the values into
///    those parts.
///
/// This is complicated by the case that the IR before legalization has an
/// rdpredregion. This typically happens when a CM select has odd size operands
/// but an i32 mask. Clang codegen bitcasts the i32 mask to v32i1, then does a
/// shufflevector to extract the correct size predicate. GenXLowering turns the
/// shufflevector into rdpredregion. The main code in GenXLegalization splits the
/// rdpredregion into several rdpredregions.
///
/// In that case, we cannot guarantee that fixIllegalPredicates will find legal
/// rdpredregions. For example, suppose the original rdpredregion has a v32i1 as
/// input, and v13i1 as result. It is determined that the 13 bit predicate will
/// be split into 8,4,1 parts. The main GenXLegalization code will generate
/// an rdpredregion from the 32 bit predicate for each part of the 13 bit
/// predicate. However, the rdpredregion for the 1 bit part is illegal, because
/// its start offset is not 8 aligned.
///
/// We currently do not cope with that (it will probably assert somewhere). If we
/// do find a need to cope with it, then the illegal rdpredregion will need to be
/// lowered to bit twiddling code.
///
/// Other tasks of GenXLegalization
/// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
///
/// An additional task of this pass is to lower an any/all intrinsic that is
/// used anywhere other than as the predicate of a scalar wrregion by inserting
/// such a scalar wrregion with a byte 0/1 result and then a compare of that
/// to give an i1.
///
/// A further task of this pass is to lower any predicated wrregion where the
/// value to write is a vector wider than 1 but the predicate is a scalar i1 (other
/// than the value 1, which means unpredicated). It inserts code to splat the
/// scalar i1 predicate to v16i1 or v32i1. This is really part of lowering, but
/// we need to do it here because in GenXLowering the value to write might be
/// wider than 32.
///
/// An extra optimization performed in this pass is to transform a move (that
/// is, a lone wrregion or lone rdregion or a rdregion+wrregion baled together)
/// with a byte element type into the equivalent short or int move. This saves
/// the jitter having to split the byte move into even and odd halves. This
/// optimization needs to be done when baling info is available, so legalization
/// is a handy place to put it.
///
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "GENX_LEGALIZATION"

#include "GenX.h"
#include "GenXAlignmentInfo.h"
#include "GenXBaling.h"
#include "GenXIntrinsics.h"
#include "GenXRegion.h"
#include "GenXSubtarget.h"
#include "KillAnalysis.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include <set>

using namespace llvm;
using namespace genx;

namespace {

// Information on a part of a predicate.
struct PredPart {
  unsigned Offset;
  unsigned Size;
  unsigned PartNum;
};

// min and max legal size for a predicate split
struct LegalPredSize {
  unsigned Min;
  unsigned Max;
};

// GenXLegalization : legalize execution widths and GRF crossing
class GenXLegalization : public FunctionPass {
  enum { DETERMINEWIDTH_UNBALE = 0, DETERMINEWIDTH_NO_SPLIT = 256 };
  GenXBaling *Baling;
  const GenXSubtarget *ST;
  ScalarEvolution *SE;
  // Work variables when in the process of splitting a bale.
  // The Bale being split. (Also info on whether it has FIXED4 and TWICEWIDTH operands.)
  Bale B;
  Use *Fixed4;
  Use *TwiceWidth;
  // Map from the original instruction to the split one for the current index.
  std::map<Instruction *, Value *> SplitMap;

  // Consider reading from and writing to the same region in this bale,
  // bale {
  //   W1 = rdr(V0, R)
  //   W2 = op(W1, ...)
  //   V1 = wrd(V0, W2, R)
  // }
  // if splitting the above bale into two bales
  // bale {
  //    W1.0 = rdr(V0, R.0)
  //    W2.0 = op(W1.0, ...)
  //    V1.0 = wrr(V0, W2.0, R.0)
  // }
  // bale {
  //    W1.1 = rdr(V0, R.1)
  //    W2.1 = op(W1.1, ...)
  //    V1.1 = wrr(V1.0, W2.1, R1)
  // }
  // V1.0 and V0 are live at the same time. This makes copy-coalescing
  // fail and also increases rp by the size of V0.
  //
  // If we can prove that
  // (*) rdr(V0, R.1) == rdr(V1.0, R.1) = rdr(wrr(V0, W2.0, R.0), R.1)
  // then we could split the bale slightly differently:
  // bale {
  //    W1.0 = rdr(V0, R.0)
  //    W2.0 = op(W1.0, ...)
  //    V1.0 = wrr(V0, W2.0, R.0)
  // }
  // bale {
  //    W1.1 = rdr(V1.0, R.1)
  //    W2.1 = op(W1.1, ...)
  //    V1.1 = wrr(V1.0, W2.1, R1)
  // }
  // If V0 is killed after this bale, then V1.0, V1.1 and V0
  // could be coalesced into a single variable. This is the pattern
  // for in-place operations.
  //
  // To satisfy equation (*), it suffices to prove there is no overlap for any
  // two neighbor subregions. This holds for the following two cases:
  //  (1) 1D direct regions or indirect regions with single offset
  //  (2) 2D direct regions with VStride >= Width, or indirect regions with
  //      single offset.
  //
  enum SplitKind {
    SplitKind_Normal,     // split bales without propagation.
    SplitKind_Propagation // split bales with propagation
  };
  SplitKind CurSplitKind;
  // Current instruction in loop in runOnFunction, which gets adjusted if that
  // instruction is erased.
  Instruction *CurrentInst;
  // Illegally sized predicate values that need splitting at the end of
  // processing the function.
  SetVector<Instruction *> IllegalPredicates;
public:
  static char ID;
  explicit GenXLegalization() : FunctionPass(ID) { clearBale(); }
  virtual StringRef getPassName() const {
    return "GenX execution width and GRF crossing legalization";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const;
  bool runOnFunction(Function &F);
  // createPrinterPass : get a pass to print the IR, together with the GenX
  // specific analyses
  virtual Pass *createPrinterPass(raw_ostream &O, const std::string &Banner) const
  { return createGenXPrinterPass(O, Banner); }
private:
  void clearBale() { B.clear(); Fixed4 = nullptr; TwiceWidth = nullptr; }
  unsigned getExecSizeAllowedBits(Instruction *Inst);
  bool processInst(Instruction *Inst);
  bool processBale(Instruction *InsertBefore);
  bool noSplitProcessing();
  bool processAllAny(Instruction *Inst, Instruction *InsertBefore);
  bool processBitCastFromPredicate(Instruction *Inst, Instruction *InsertBefore);
  bool processBitCastToPredicate(Instruction *Inst, Instruction *InsertBefore);
  unsigned getExecutionWidth();
  unsigned determineWidth(unsigned WholeWidth, unsigned StartIdx);
  unsigned determineNonRegionWidth(Instruction *Inst, unsigned StartIdx);
  LegalPredSize getLegalPredSize(Value *Pred, Type *ElementTy, unsigned StartIdx, unsigned RemainingSize = 0);
  PredPart getPredPart(Value *V, unsigned Offset);
  Value *splitBale(Value *Last, unsigned StartIdx, unsigned Width, Instruction *InsertBefore);
  Value *splitInst(Value *Last, BaleInst BInst, unsigned StartIdx,
                   unsigned Width, Instruction *InsertBefore,
                   const DebugLoc &DL);
  Value *getSplitOperand(Instruction *Inst, unsigned OperandNum,
                         unsigned StartIdx, unsigned Size,
                         Instruction *InsertBefore, const DebugLoc &DL);
  Instruction *convertToMultiIndirect(Instruction *Inst, Value *LastJoinVal,
                                      Region *R);
  Instruction *transformByteMove(Bale *B);
  Value *splatPredicateIfNecessary(Value *V, Type *ValueToWriteTy, Instruction *InsertBefore, DebugLoc DL);
  Value *splatPredicateIfNecessary(Value *V, unsigned Width, Instruction *InsertBefore, DebugLoc DL);
  void eraseInst(Instruction *Inst);
  void removingInst(Instruction *Inst);
  void fixIllegalPredicates(Function *F);
  void fixIntrinsicCalls(Function *F);
  SplitKind checkBaleSplittingKind();
};

static const unsigned MaxPredSize = 32;

} // end anonymous namespace


char GenXLegalization::ID = 0;
namespace llvm { void initializeGenXLegalizationPass(PassRegistry &); }
INITIALIZE_PASS_BEGIN(GenXLegalization, "GenXLegalization", "GenXLegalization", false, false)
INITIALIZE_PASS_DEPENDENCY(GenXFuncBaling)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_END(GenXLegalization, "GenXLegalization", "GenXLegalization", false, false)

FunctionPass *llvm::createGenXLegalizationPass()
{
  initializeGenXLegalizationPass(*PassRegistry::getPassRegistry());
  return new GenXLegalization;
}

void GenXLegalization::getAnalysisUsage(AnalysisUsage &AU) const
{
  AU.addRequired<GenXFuncBaling>();
  AU.addRequired<ScalarEvolutionWrapperPass>();
  AU.addPreserved<GenXModule>();
}

/***********************************************************************
 * GenXLegalization::runOnFunction : process one function to
 *    legalize execution width and GRF crossing
 */
bool GenXLegalization::runOnFunction(Function &F)
{
  Baling = &getAnalysis<GenXFuncBaling>();
  SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  auto P = getAnalysisIfAvailable<GenXSubtargetPass>();
  ST = P ? P->getSubtarget() : nullptr;
  // Check args for illegal predicates.
  for (Function::arg_iterator fi = F.arg_begin(), fe = F.arg_end();
      fi != fe; ++fi) {
    Argument *Arg = &*fi;
    if (auto VT = dyn_cast<VectorType>(Arg->getType()))
      if (VT->getElementType()->isIntegerTy(1))
        assert(getPredPart(Arg, 0).Size == VT->getNumElements() && "function arg not allowed to be illegally sized predicate");
  }
  // Legalize instructions. This does a postordered depth first traversal of the
  // CFG, and scans backwards in each basic block, to ensure that, if we unbale
  // anything, it then gets processed subsequently.
  for (po_iterator<BasicBlock *> i = po_begin(&F.getEntryBlock()),
      e = po_end(&F.getEntryBlock()); i != e; ++i) {
    BasicBlock *BB = *i;
    // The effect of this loop is that we process the instructions in reverse
    // order, and we re-process anything inserted before the instruction
    // being processed. CurrentInst is a field in the GenXLegalization object,
    // which gets updated if a
    for (CurrentInst = BB->getTerminator(); CurrentInst;) {
      // If processInst returns true, re-process the same instruction. This is
      // used when unbaling.
      while (processInst(CurrentInst))
        DEBUG(dbgs() << "reprocessing\n");
      CurrentInst = CurrentInst == &BB->front()
          ? nullptr : CurrentInst->getPrevNode();
    }
  }
  fixIntrinsicCalls(&F);
  fixIllegalPredicates(&F);
  IllegalPredicates.clear();
  return true;
}

/***********************************************************************
 * getExecSizeAllowedBits : get bitmap of allowed execution sizes
 *
 * Enter:   Inst = main instruction of bale
 *
 * Return:  bit N set if execution size 1<<N is allowed
 *
 * Most instructions have a minimum width of 1. But some instructions,
 * such as dp4 and lrp, have a minimum width of 4, and legalization cannot
 * allow such an instruction to be split to a smaller width.
 *
 * This also sets up fields in GenXLegalization: Fixed4 is set to a use
 * that is a FIXED4 operand, and TwiceWidth is set to a use that is a
 * TWICEWIDTH operand.
 */
unsigned GenXLegalization::getExecSizeAllowedBits(Instruction *Inst)
{
  // HW does not support simd16/32 integer div/rem. Here it allows
  // simd16 but not simdD32, as jitter will split it. This emits simd16
  // moves not simd8 ones.
  switch (Inst->getOpcode()) {
  default:
    break;
  case BinaryOperator::SDiv:
  case BinaryOperator::UDiv:
  case BinaryOperator::SRem:
  case BinaryOperator::URem:
    return 0x1f;
  }

  unsigned ID = getIntrinsicID(Inst);
  switch (ID) {
  case Intrinsic::fma:
  case Intrinsic::genx_ssmad:
  case Intrinsic::genx_sumad:
  case Intrinsic::genx_usmad:
  case Intrinsic::genx_uumad:
  case Intrinsic::genx_ssmad_sat:
  case Intrinsic::genx_sumad_sat:
  case Intrinsic::genx_usmad_sat:
  case Intrinsic::genx_uumad_sat:
    // Do not emit simd32 mad for pre-CNL.
    return ST->isCNLplus() ? 0x3f : 0x1f;
  default:
    break;
  }

  if (CallInst *CI = dyn_cast<CallInst>(Inst)) {
    // We have a call instruction, so we can assume it is an intrinsic since
    // otherwise processInst would not have got as far as calling us as
    // a non-intrinsic call forces isSplittable() to be false.
    GenXIntrinsicInfo II(CI->getCalledFunction()->getIntrinsicID());
    // While we have the intrinsic info, we also spot whether we have a FIXED4
    // operand and/or a TWICEWIDTH operand.
    for (auto i = II.begin(), e = II.end(); i != e; ++i) {
      auto ArgInfo = *i;
      if (ArgInfo.isArgOrRet()) {
        switch (ArgInfo.getRestriction()) {
          case GenXIntrinsicInfo::FIXED4:
            Fixed4 = &CI->getOperandUse(ArgInfo.getArgIdx());
            break;
          case GenXIntrinsicInfo::TWICEWIDTH:
            TwiceWidth = &CI->getOperandUse(ArgInfo.getArgIdx());
            break;
        }
      }
    }
    return II.getExecSizeAllowedBits();
  }
  return 0x3f;
}

/***********************************************************************
 * processInst : process one instruction to legalize execution width and GRF
 *    crossing
 *
 * Return:  true to re-process same instruction (typically after unbaling
 *          something from it)
 */
bool GenXLegalization::processInst(Instruction *Inst)
{
  DEBUG(dbgs() << "processInst: " << *Inst << "\n");
  if (isa<TerminatorInst>(Inst))
    return false; // ignore terminator
  // Prepare to insert split code after current instruction.
  auto InsertBefore = Inst->getNextNode();
  if (isa<PHINode>(Inst))
    return false; // ignore phi node
  // Sanity check for illegal operand type
  if ((Inst->getType()->getScalarType()->getPrimitiveSizeInBits() == 64) &&
      !(ST->hasLongLong()))
    report_fatal_error("'double' and 'long long' type are not supported by this target");
  if (!isa<VectorType>(Inst->getType())) {
    if (Inst->getOpcode() == Instruction::BitCast
        && Inst->getOperand(0)->getType()->getScalarType()->isIntegerTy(1)) {
      // Special processing for bitcast from predicate to scalar int.
      return processBitCastFromPredicate(Inst, InsertBefore);
    }
    switch (getIntrinsicID(Inst)) {
      case Intrinsic::genx_all:
      case Intrinsic::genx_any:
        return processAllAny(Inst, InsertBefore); // Special processing for all/any
      default:
        break;
    }
    return false; // no splitting needed for other scalar op.
  }
  if (isa<ExtractValueInst>(Inst))
    return false;
  if (isa<BitCastInst>(Inst)) {
    if (Inst->getType()->getScalarType()->isIntegerTy(1)) {
      // Special processing for bitcast from scalar int to predicate.
      return processBitCastToPredicate(Inst, InsertBefore);
    }
    // Ignore any other bitcast.
    return false;
  }

  if (Baling->isBaled(Inst)) {
    DEBUG(dbgs() << "is baled\n");
    return false; // not head of bale, ignore
  }
  // No need to split an llvm.genx.constant with an undef value.
  switch (getIntrinsicID(Inst)) {
    case Intrinsic::genx_constanti:
    case Intrinsic::genx_constantf:
      if (isa<UndefValue>(Inst->getOperand(0)))
        return false;
      break;
  }
  clearBale();
  Baling->buildBale(Inst, &B);
  // Get the main inst from the bale and decide whether it is something we do not split.
  // If there is no main inst, the bale is splittable.
  if (auto MainInst = B.getMainInst()) {
    if (isa<CallInst>(MainInst->Inst)) {
      unsigned IntrinID = getIntrinsicID(MainInst->Inst);
      switch (IntrinID) {
        case Intrinsic::not_intrinsic:
          return false; // non-intrinsic call, ignore
        case Intrinsic::genx_constantpred:
          break; // these intrinsics can be split
        default:
          if (GenXIntrinsicInfo(IntrinID).getRetInfo().getCategory()
              != GenXIntrinsicInfo::GENERAL) {
            // This is not an ALU intrinsic (e.g. cm_add).
            // We have a non-splittable intrinsic. Such an intrinsic can
            // have a scalar arg with a baled in rdregion, which does not
            // need legalizing. It never has a vector arg with a baled in
            // rdregion. So no legalization needed.
            return false;
          }
          break;
      }
    } else if (isa<BitCastInst>(MainInst->Inst)) {
      // BitCast is not splittable in here. A non-category-converting BitCast
      // is always coalesced in GenXCoalescing, so never generates actual
      // code. Thus it does not matter if it has an illegal size.
      return false;
    }
    // Any other instruction: split.
  }
  // Check if it is a byte move that we want to transform into a short/int move.
  if (transformByteMove(&B)) {
    // Successfully transformed. Run legalization on the new instruction (which
    // got inserted before the existing one, so will be processed next).
    DEBUG(dbgs() << "done transformByteMove\n");
    return false;
  }
  // Normal instruction splitting.
  DEBUG(
    dbgs() << "processBale: ";
    B.print(dbgs())
  );
  return processBale(InsertBefore);
}

/***********************************************************************
 * processBale : process one bale to legalize execution width and GRF crossing
 *
 * Return:  true to re-process same head of bale
 */
bool GenXLegalization::processBale(Instruction *InsertBefore)
{
  // Get the current execution width.
  unsigned WholeWidth = getExecutionWidth();
  if (WholeWidth == 1)
    return false; // No splitting of scalar or 1-vector
  Value *Joined = nullptr;
  // We will be generating a chain of joining wrregions. The initial "old
  // value" input is undef. If the bale is headed by a wrregion or
  // wrpredpredregion that is being split, code inside splitInst uses the
  // original operand 0 for split 0 instead.
  Joined = UndefValue::get(B.getHead()->Inst->getType());
  // Check the bale split kind if do splitting.
  CurSplitKind = checkBaleSplittingKind();

  // Do the splits.
  for (unsigned StartIdx = 0; StartIdx != WholeWidth;) {
    // Determine the width of the next split.
    unsigned Width = determineWidth(WholeWidth, StartIdx);
    if (Width == DETERMINEWIDTH_UNBALE) {
      // determineWidth wants us to re-start processing from the head of the
      // bale, because it did some unbaling. First erase any newly added
      // instructions.
      for (;;) {
        Instruction *Erase = InsertBefore->getPrevNode();
        if (Erase == B.getHead()->Inst)
          break;
        eraseInst(Erase);
      }
      return true; // ask to re-start processing
    }
    if (Width == DETERMINEWIDTH_NO_SPLIT)
      return noSplitProcessing(); // no splitting required
    // Some splitting is required. This includes the case that there will be
    // only one split (i.e. no splitting really required), but:
    //  * it includes an indirect rdregion that is converted to multi indirect;
    // Create the next split.
    Joined = splitBale(Joined, StartIdx, Width, InsertBefore);
    StartIdx += Width;
  }
  B.getHead()->Inst->replaceAllUsesWith(Joined);
  // Erase the original bale.  We erase in reverse order so erasing each one
  // removes the uses of earlier ones. However we do not erase an instruction
  // that still has uses; that happens for a FIXED4 operand.
  InsertBefore = B.getHead()->Inst->getNextNode();
  for (auto bi = B.rbegin(), be = B.rend(); bi != be; ++bi) {
    if (bi->Inst->use_empty())
      eraseInst(bi->Inst);
    else {
      // Do not erase this one as it still has a use; it must be a FIXED4
      // operand so it is used by the new split bales. Instead move it so it
      // does not get re-processed by the main loop of this pass.
      removingInst(bi->Inst);
      bi->Inst->removeFromParent();
      bi->Inst->insertBefore(InsertBefore);
      InsertBefore = bi->Inst;
    }
  }
  return false;
}

/***********************************************************************
 * noSplitProcessing : processing of a splttable bale in the case
 *    that it is not split
 *
 * Return:  true to re-process same head of bale
 */
bool GenXLegalization::noSplitProcessing()
{
  if (auto SI = dyn_cast<SelectInst>(B.getHead()->Inst)) {
    // Handle the case that a vector select has a scalar condition.
    SI->setOperand(0, splatPredicateIfNecessary(SI->getCondition(),
          SI->getType(), SI, SI->getDebugLoc()));
  }
  return false;
}

/***********************************************************************
 * processAllAny : legalize all/any
 *
 * Return:  true to re-process same head of bale
 */
bool GenXLegalization::processAllAny(Instruction *Inst,
    Instruction *InsertBefore)
{
  // See if the all/any is already legally sized.
  Value *Pred = Inst->getOperand(0);
  unsigned WholeSize = Pred->getType()->getVectorNumElements();
  if (getPredPart(Pred, 0).Size == WholeSize) {
    // Already legally sized. We need to check whether it is used just in a
    // branch or select, possibly via a not; if not we need to convert the
    // result to a non-predicate then back to a predicate with a cmp, as there
    // is no way of expressing a non-baled-in all/any in the generated code.
    if (Inst->hasOneUse()) {
      auto User = cast<Instruction>(Inst->use_begin()->getUser());
      if (isNot(User)) {
        if (!User->hasOneUse())
          User = nullptr;
        else
          User = cast<Instruction>(User->use_begin()->getUser());
      }
      if (User && (isa<SelectInst>(User) || isa<BranchInst>(User)))
        return false;
    }
    // Do that conversion.
    DebugLoc DL = Inst->getDebugLoc();
    auto I16Ty = Type::getInt16Ty(Inst->getContext());
    auto V1I16Ty = VectorType::get(I16Ty, 1);
    Region R(V1I16Ty);
    R.Mask = Inst;
    auto NewWr = cast<Instruction>(R.createWrRegion(
          Constant::getNullValue(V1I16Ty), ConstantInt::get(I16Ty, 1),
          Inst->getName() + ".allany_lowered", InsertBefore, DL));
    auto NewBC = CastInst::Create(Instruction::BitCast, NewWr, I16Ty,
        NewWr->getName(), InsertBefore);
    auto NewPred = CmpInst::Create(Instruction::ICmp, CmpInst::ICMP_NE, NewBC,
        Constant::getNullValue(I16Ty), NewBC->getName(), InsertBefore);
    NewPred->setDebugLoc(DL);
    NewWr->setOperand(Intrinsic::GenXRegion::PredicateOperandNum,
        UndefValue::get(Inst->getType()));
    Inst->replaceAllUsesWith(NewPred);
    NewWr->setOperand(Intrinsic::GenXRegion::PredicateOperandNum, Inst);
    return false;
  }
  // It needs to be split. For each part, we have an all/any on that part, and
  // use it to do a select on a scalar that keeps track of whether all/any set
  // bits have been found.
  unsigned IID = getIntrinsicID(Inst);
  Type *I16Ty = Type::getInt16Ty(Inst->getContext());
  Value *Zero = Constant::getNullValue(I16Ty);
  Value *One = ConstantInt::get(I16Ty, 1);
  Value *Result = IID == Intrinsic::genx_all ? One : Zero;
  DebugLoc DL = Inst->getDebugLoc();
  for (unsigned StartIdx = 0; StartIdx != WholeSize; ) {
    auto PP = getPredPart(Pred, StartIdx);
    auto Part = Region::createRdPredRegionOrConst(Pred, StartIdx, PP.Size,
        Pred->getName() + ".split" + Twine(StartIdx), InsertBefore, DL);
    Module *M = InsertBefore->getParent()->getParent()->getParent();
    Function *Decl = Intrinsic::getDeclaration(M, (Intrinsic::ID)IID,
        Part->getType());
    Instruction *NewAllAny = nullptr;
    if (PP.Size != 1)
      NewAllAny = CallInst::Create(Decl, Part,
          Inst->getName() + ".split" + Twine(StartIdx), InsertBefore);
    else {
      // Part is v1i1. All we need to do is bitcast it to i1, which does not
      // generate any code.
      NewAllAny = CastInst::Create(Instruction::BitCast, Part,
          Part->getType()->getScalarType(),
          Inst->getName() + ".split" + Twine(StartIdx), InsertBefore);
    }
    NewAllAny->setDebugLoc(DL);
    SelectInst *Sel = nullptr;
    if (IID == Intrinsic::genx_all)
      Sel = SelectInst::Create(NewAllAny, Result, Zero,
          Inst->getName() + ".join" + Twine(StartIdx), InsertBefore);
    else
      Sel = SelectInst::Create(NewAllAny, One, Result,
          Inst->getName() + ".join" + Twine(StartIdx), InsertBefore);
    Sel->setDebugLoc(DL);
    Result = Sel;
    StartIdx += PP.Size;
  }
  // Add a scalar comparison to get the final scalar bool result.
  auto Cmp = CmpInst::Create(Instruction::ICmp, CmpInst::ICMP_NE, Result, Zero,
      Inst->getName() + ".joincmp", InsertBefore);
  // Replace and erase the old all/any.
  Inst->replaceAllUsesWith(Cmp);
  eraseInst(Inst);
  return false;
}

/***********************************************************************
 * processBitCastFromPredicate : legalize bitcast from predicate (vector of
 *    i1) to scalar int
 */
bool GenXLegalization::processBitCastFromPredicate(Instruction *Inst,
    Instruction *InsertBefore)
{
  Value *Pred = Inst->getOperand(0);
  unsigned SplitWidth = getPredPart(Pred, 0).Size;
  if (SplitWidth == 0)
    return false;
#if _DEBUG
  unsigned WholeWidth = Pred->getType()->getVectorNumElements();
  assert(!(WholeWidth % SplitWidth) && "does not handle odd predicate sizes");
#endif
  // Bitcast each split predicate into an element of an int vector.
  // For example, if the split size is 16, then the result is a vector
  // of i16. Then bitcast that to the original result type.
  Type *IntTy = Type::getIntNTy(Inst->getContext(), SplitWidth);
  unsigned NumSplits = Inst->getType()->getPrimitiveSizeInBits() / SplitWidth;
  if (NumSplits == 1)
    return false;
  DebugLoc DL = Inst->getDebugLoc();
  Type *IntVecTy = VectorType::get(IntTy, NumSplits);
  Value *Result = UndefValue::get(IntVecTy);
  // For each split...
  for (unsigned i = 0; i != NumSplits; ++i) {
    // Bitcast that split of the predicate.
    auto *NewBitCast = CastInst::Create(Instruction::BitCast,
        getSplitOperand(Inst, /*OperandNum=*/ 0, i * SplitWidth,
          SplitWidth, InsertBefore, DL),
        IntTy, Inst->getName() + ".split", InsertBefore);
    NewBitCast->setDebugLoc(DL);
    // Write it into the element of the vector.
    Region R(Result);
    R.getSubregion(i, 1);
    Result = R.createWrRegion(Result, NewBitCast,
        Inst->getName() + ".join" + Twine(i * SplitWidth), InsertBefore, DL);
  }
  // Bitcast the vector to the original type.
  auto *NewBitCast = CastInst::Create(Instruction::BitCast, Result,
      Inst->getType(), Inst->getName() + ".cast", InsertBefore);
  NewBitCast->setDebugLoc(DL);
  // Change uses and erase original.
  Inst->replaceAllUsesWith(NewBitCast);
  eraseInst(Inst);
  return false;
}

/***********************************************************************
 * processBitCastToPredicate : legalize bitcast to predicate (vector of
 *    i1) from scalar int
 */
bool GenXLegalization::processBitCastToPredicate(Instruction *Inst,
    Instruction *InsertBefore)
{
  unsigned WholeWidth = Inst->getType()->getVectorNumElements();
  unsigned SplitWidth = getPredPart(Inst, 0).Size;
  assert(!(WholeWidth % SplitWidth) && "does not handle odd predicate sizes");
  unsigned NumSplits = WholeWidth / SplitWidth;
  if (NumSplits == 1)
    return false;
  // Bitcast the scalar int input to a vector of ints each with a number of
  // bits matching the predicate split size.
  DebugLoc DL = Inst->getDebugLoc();
  auto IVTy = VectorType::get(Type::getIntNTy(Inst->getContext(), SplitWidth),
      WholeWidth / SplitWidth);
  auto IntVec = CastInst::Create(Instruction::BitCast, Inst->getOperand(0),
      IVTy, Inst->getName() + ".cast", InsertBefore);
  IntVec->setDebugLoc(DL);
  Value *Result = UndefValue::get(Inst->getType());
  Type *SplitPredTy = VectorType::get(
      Inst->getType()->getScalarType(), SplitWidth);
  // For each predicate split...
  for (unsigned i = 0; i != NumSplits; ++i) {
    // Get the element of the vector using rdregion.
    Region R(IntVec);
    R.getSubregion(i, 1);
    auto NewRd = R.createRdRegion(IntVec,
        Inst->getName() + ".rdsplit" + Twine(i), InsertBefore, DL);
    // Bitcast that element of the int vector to a predicate.
    auto NewPred = CastInst::Create(Instruction::BitCast, NewRd, SplitPredTy,
        Inst->getName() + ".split" + Twine(i), InsertBefore);
    NewPred->setDebugLoc(DL);
    // Join into the overall result using wrpredregion.
    auto NewWr = Region::createWrPredRegion(Result, NewPred, i * SplitWidth,
        Inst->getName() + ".join" + Twine(i), InsertBefore, DL);
    // If this is the first wrpredregion, add it to IllegalPredicates so it gets
    // processed later in fixIllegalPredicates.
    if (!i)
      IllegalPredicates.insert(NewWr);
    Result = NewWr;
  }
  // Change uses and erase original.
  Inst->replaceAllUsesWith(Result);
  eraseInst(Inst);
  return false;
}

/***********************************************************************
 * getExecutionWidth : get the execution width of the bale
 *
 * If there is no wrregion at the head of the bale, then the execution width is
 * the width of the head. If there is a wrregion or wrpredpredregion, then the
 * execution width is the width of the subregion input to the wrregion.
 */
unsigned GenXLegalization::getExecutionWidth()
{
  BaleInst *Head = B.getHead();
  Value *Dest = Head->Inst;
  if (Head->Info.Type == BaleInfo::WRREGION
      || Head->Info.Type == BaleInfo::WRPREDREGION
      || Head->Info.Type == BaleInfo::WRPREDPREDREGION)
    Dest = Head->Inst->getOperand(1);
  VectorType *VT = dyn_cast<VectorType>(Dest->getType());
  if (!VT)
    return 1;
  return VT->getNumElements();
}

/***********************************************************************
 * determineWidth : determine width of the next split
 *
 * Enter:   WholeWidth = whole execution width of the bale before splitting
 *          StartIdx = start index of this split
 *
 * Return:  width of next split, DETERMINEWIDTH_UNBALE if unbaling occurred,
 *          DETERMINEWIDTH_NO_SPLIT if no split required
 *
 * If this function returns WholeWidth rather than DETERMINEWIDTH_NO_SPLIT, it
 * means that there is an indirect rdregion that needs to be converted to multi
 * indirect. This is different to the condition of not needing a split at all,
 * which causes this function to return DETERMINEWIDTH_NO_SPLIT.
 */
unsigned GenXLegalization::determineWidth(unsigned WholeWidth,
    unsigned StartIdx)
{
  // Prepare to keep track of whether an instruction with a minimum width
  // (e.g. dp4) would be split too small, and whether we need to unbale.
  unsigned ExecSizeAllowedBits = 0x3f;
  if (auto Main = B.getMainInst())
    ExecSizeAllowedBits = getExecSizeAllowedBits(Main->Inst);
  unsigned MainInstMinWidth = 1 << countTrailingZeros(ExecSizeAllowedBits,
        ZB_Undefined);
  // Determine the vector width that we need to split into.
  bool IsReadSameVector = false;
  unsigned Width = WholeWidth - StartIdx;
  unsigned PredMinWidth = 1;
  Value *WrRegionInput = nullptr;
  auto Head = B.getHead();
  if (Head->Info.Type == BaleInfo::WRREGION)
    WrRegionInput = Head->Inst->getOperand(
        Intrinsic::GenXRegion::OldValueOperandNum);
  bool MustSplit = false;
  for (Bale::iterator i = B.begin(), InstWithMinWidth = i, e = B.end();
      i != e; ++i) {
    unsigned ThisWidth = Width;
    // Determine the width we need for this instruction.
    switch (i->Info.Type) {
      case BaleInfo::WRREGION: {
        bool Unbale = false;
        Region R(i->Inst, i->Info);
        if (R.Mask && !i->Info.isOperandBaled(
              Intrinsic::GenXRegion::PredicateOperandNum)) {
          // We have a predicate, and it is not a baled in rdpredregion. (A
          // baled in rdpredregion is handled when this loop reaches that
          // instruction.) Get the min and max legal predicate size.
          auto PredWidths = getLegalPredSize(R.Mask, R.ElementTy, StartIdx);
          ThisWidth = std::min(ThisWidth, PredWidths.Max);
          PredMinWidth = PredWidths.Min;
        }
        if (PredMinWidth > Width) {
          // The min predicate size is bigger than the legal size for the rest
          // of the bale other than the wrregion. Unbale the main instruction.
          Unbale = true;
        }
        // Get the max legal size for the wrregion.
        ThisWidth = std::min(ThisWidth, R.getLegalSize(StartIdx, false/*Allow2D*/,
            i->Inst->getOperand(0)->getType()->getVectorNumElements(), ST, &(Baling->AlignInfo)));
        if (!Unbale && R.Mask && PredMinWidth > ThisWidth) {
          // The min predicate size (from this wrregion) is bigger than the
          // legal size for this wrregion. We have to rewrite the wrregion as:
          //    rdregion of the region out of the old value
          //    predicated wrregion, which now has a contiguous region
          //    wrregion (the original wrregion but with no predicate)
          // then set DETERMINEWIDTH_UNBALE to restart.
          auto DL = i->Inst->getDebugLoc();
          auto NewRd = R.createRdRegion(
              i->Inst->getOperand(Intrinsic::GenXRegion::OldValueOperandNum),
              i->Inst->getName() + ".separatepred.rd", i->Inst, DL, false);
          Baling->setBaleInfo(NewRd, BaleInfo(BaleInfo::RDREGION));
          Region R2(NewRd);
          R2.Mask = R.Mask;
          auto NewWr = cast<Instruction>(R2.createWrRegion(NewRd,
              i->Inst->getOperand(Intrinsic::GenXRegion::NewValueOperandNum),
              i->Inst->getName() + ".separatepred.wr", i->Inst, DL));
          auto NewBI = i->Info;
          NewBI.clearOperandBaled(Intrinsic::GenXRegion::WrIndexOperandNum);
          Baling->setBaleInfo(NewWr, NewBI);
          i->Inst->setOperand(Intrinsic::GenXRegion::NewValueOperandNum,
              NewWr);
          i->Inst->setOperand(Intrinsic::GenXRegion::PredicateOperandNum,
              Constant::getAllOnesValue(R.Mask->getType()));
          i->Info.clearOperandBaled(Intrinsic::GenXRegion::PredicateOperandNum);
          Baling->setBaleInfo(i->Inst, i->Info);
          ThisWidth = DETERMINEWIDTH_UNBALE;
          break;
        }
        if (PredMinWidth > ThisWidth) {
          // The min predicate size (from a select baled into this wrregion) is
          // bigger than the legal size for this wrregion. Unbale the select.
          Unbale = true;
        }
        if (ThisWidth < MainInstMinWidth) {
          // The wrregion is split too small for the main instruction. Unbale
          // the main instruction.
          Unbale = true;
        }
        if (Unbale) {
          i->Info.clearOperandBaled(Intrinsic::GenXRegion::NewValueOperandNum);
          Baling->setBaleInfo(i->Inst, i->Info);
          ThisWidth = DETERMINEWIDTH_UNBALE;
        }
        break;
      }
      case BaleInfo::RDREGION: {
        if (i->Inst->getOperand(Intrinsic::GenXRegion::OldValueOperandNum)
            == WrRegionInput)
          IsReadSameVector = true; // See use of this flag below.
        // Determine the max region width. If this rdregion is baled into a
        // TWICEWIDTH operand, double the start index and half the resulting
        // size.
        Region R(i->Inst, i->Info);
        unsigned Doubling = TwiceWidth && i->Inst == *TwiceWidth;
        unsigned ModifiedStartIdx = StartIdx << Doubling;
        if (Fixed4 && i->Inst == *Fixed4)
          ModifiedStartIdx = 0;
        ThisWidth = R.getLegalSize(ModifiedStartIdx, true/*Allow2D*/,
            i->Inst->getOperand(0)->getType()->getVectorNumElements(), ST, &(Baling->AlignInfo));
        if (ThisWidth == 1 && R.Indirect && !isa<VectorType>(R.Indirect->getType())) {
          // This is a single indirect rdregion where we failed to make the
          // valid size any more than one. If possible, increase the valid size
          // to 4 or 8 on the assumption that we are going to convert it to a
          // multi indirect.
          ThisWidth = 1 << llvm::log2(R.Width - StartIdx % R.Width);
          if (ThisWidth >= 4) {
            ThisWidth = std::min(ThisWidth, 8U);
            MustSplit = true;
          } else
            ThisWidth = 1;
        }
        ThisWidth >>= Doubling;
        if (ThisWidth < MainInstMinWidth) {
          // The rdregion is split too small for the main instruction.
          // Unbale the rdregion from its user (must be exactly one user as
          // it is baled). Note that the user is not necessarily the main
          // inst, it might be a modifier baled in to the main inst.
          Value::use_iterator UI = i->Inst->use_begin();
          Instruction *User = cast<Instruction>(UI->getUser());
          BaleInfo BI = Baling->getBaleInfo(User);
          BI.clearOperandBaled(UI->getOperandNo());
          Baling->setBaleInfo(User, BI);
          ThisWidth = DETERMINEWIDTH_UNBALE;
        }
        break;
      }
      case BaleInfo::NOTP:
        // Only process notp
        // - if predicate is a vector and
        // - if it does not have rdpredregion baled in.
        if (!i->Info.isOperandBaled(0) && i->Inst->getType()->isVectorTy()) {
          // Get the min and max legal predicate size. First get the element type from the
          // wrregion or select that the notp is baled into.
          Type *ElementTy = nullptr;
          auto Head = B.getHead()->Inst;
          if (Head != i->Inst)
            ElementTy = Head->getOperand(1)->getType()->getScalarType();
          auto PredWidths = getLegalPredSize(i->Inst->getOperand(0),
              ElementTy, StartIdx);
          // If the min legal predicate size is more than the remaining size in
          // the predicate that the rdpredregion extracts, ignore it. This results
          // in an illegal rdpredregion from splitInst, which then has to be
          // lowered to less efficient code by fixIllegalPredicates. This situation
          // arises when the original unsplit bale has an odd size rdpredregion
          // out of a v32i1, from a CM select() where the mask is an i32.
          if (PredWidths.Min <= WholeWidth - StartIdx)
            PredMinWidth = PredWidths.Min;
          ThisWidth = std::min(ThisWidth, PredWidths.Max);
        }
        break;
      case BaleInfo::RDPREDREGION: {
        unsigned RdPredStart = cast<ConstantInt>(i->Inst->getOperand(1))
            ->getZExtValue();
        // Get the min and max legal predicate size.
        auto PredWidths = getLegalPredSize(
            i->Inst->getOperand(0), // the input predicate
            cast<Instruction>(i->Inst->use_begin()->getUser())->getOperand(1)
                ->getType()->getScalarType(), // the wrregion/select element type
            RdPredStart + StartIdx);
        // If the min legal predicate size is more than the remaining size in
        // the predicate that the rdpredregion extracts, ignore it. This results
        // in an illegal rdpredregion from splitInst, which then has to be
        // lowered to less efficient code by fixIllegalPredicates. This situation
        // arises when the original unsplit bale has an odd size rdpredregion
        // out of a v32i1, from a CM select() where the mask is an i32.
        if (PredWidths.Min <= WholeWidth - StartIdx)
          PredMinWidth = PredWidths.Min;
        ThisWidth = std::min(ThisWidth, PredWidths.Max);
        break;
      }
      case BaleInfo::ADDRADD:
        break;
      default: {
        ThisWidth = determineNonRegionWidth(i->Inst, StartIdx);
        Value *Pred = nullptr;
        if (auto SI = dyn_cast<SelectInst>(i->Inst)) {
          Pred = SI->getCondition();
          if (!isa<VectorType>(Pred->getType())) {
            // For a select with a scalar predicate, the predicate will be splatted by splatPredicateIfNecessary.
            // We need to limit the legal width to the max predicate width.
            ThisWidth = std::min(ThisWidth, MaxPredSize);
            Pred = nullptr;
          }
        } else if (isa<CmpInst>(i->Inst))
          Pred = i->Inst;
        if (Pred && isa<VectorType>(Pred->getType())) {
          // For a select (with a vector predicate) or cmp, we need to take the
          // predicate into account. Get the min and max legal predicate size.
          auto PredWidths = getLegalPredSize(Pred,
              i->Inst->getOperand(1)->getType()->getVectorElementType(),
              StartIdx);
          // If the min legal predicate size is more than the remaining size in
          // the predicate that the rdpredregion extracts, ignore it. This results
          // in an illegal rdpredregion from splitInst, which then has to be
          // lowered to less efficient code by fixIllegalPredicates. This situation
          // arises when the original unsplit bale has an odd size rdpredregion
          // out of a v32i1, from a CM select() where the mask is an i32.
          if (PredWidths.Min <= WholeWidth - StartIdx)
            PredMinWidth = PredWidths.Min;
          if (PredMinWidth > Width) {
            // The min predicate size is bigger than the legal size for the
            // rest of the bale so far. There must be a rdregion that needs to
            // be split too much. Unbale it.
            assert(InstWithMinWidth->Info.Type == BaleInfo::RDREGION);
            Instruction *RdToUnbale = InstWithMinWidth->Inst;
            Use *U = &*RdToUnbale->use_begin();
            auto User = cast<Instruction>(U->getUser());
            BaleInfo BI = Baling->getBaleInfo(User);
            BI.clearOperandBaled(U->getOperandNo());
            Baling->setBaleInfo(User, BI);
            ThisWidth = DETERMINEWIDTH_UNBALE;
          }
          ThisWidth = std::min(ThisWidth, PredWidths.Max);
        }
        break;
      }
    }
    if (ThisWidth < Width) {
      InstWithMinWidth = i;
      Width = ThisWidth;
    }
    if (Width == DETERMINEWIDTH_UNBALE)
      return DETERMINEWIDTH_UNBALE;
  }
  while (!(ExecSizeAllowedBits & Width)) {
    // This width is disallowed by the main instruction. We have already
    // dealt with the case where there is a minimum width above; the
    // code here is for when there is a particular disallowed width
    // (e.g. bfi disallows width 2 but allows 1). Try a smaller width.
    assert(Width != 1);
    Width >>= 1;
  }
  if (Width != WholeWidth && IsReadSameVector &&
      CurSplitKind == SplitKind_Normal) {
    // Splitting required, and the bale contains a rdregion from the same
    // vector as the wrregion's old value input, and we're not already
    // unbaling. Splitting that would result
    // in the original value of the vector and a new value being live at the
    // same time, so we avoid it by unbaling the wrregion.  The resulting
    // code will use an intermediate smaller register for the result of the
    // main inst before writing that back in to a region of the vector.
    //
    // Note that this unbaling is necessary despite pretty much the same
    // thing being done in second baling in GenXBaling::unbaleBadOverlaps.
    // Not doing the unbaling here results in code where the split rdregions
    // and wrregions are interleaved, so the unbaling in
    // GenXBaling::unbaleBadOverlaps does not actually stop the bad live range
    // overlap. (This might change if we had a pass to schedule to reduce
    // register pressure.)
    auto Head = B.getHead();
    Head->Info.clearOperandBaled(Intrinsic::GenXRegion::NewValueOperandNum);
    Baling->setBaleInfo(Head->Inst, Head->Info);
    DEBUG(dbgs() << "GenXLegalization unbaling when rdr and wrr use same vector\n");
    return DETERMINEWIDTH_UNBALE;
  }
  if (Width == WholeWidth && !MustSplit) {
    // No split required, so return that to the caller, which then just
    // returns.  However we do not do that if MustSplit is set, because there
    // is some reason we need to go through splitting code anyway, one of:
    // 1. there is an rdregion that needs to be converted to multi indirect;
    // 2. there is an rdpredregion.
    return DETERMINEWIDTH_NO_SPLIT;
  }

  // If join is generated after splitting, need to check destination region rule
  {
    auto Head = B.getHead();
    if (Head->Info.Type != BaleInfo::WRREGION
      && Head->Info.Type != BaleInfo::WRPREDPREDREGION) {
      auto VT = cast<VectorType>(Head->Inst->getType());
      unsigned VecSize = VT->getNumElements();
      if (VecSize != Width) {
        if (!VT->getElementType()->isIntegerTy(1)) {
          Region R(VT);
          auto ThisWidth = R.getLegalSize(StartIdx,
            false /*no 2d for dst*/,
            VecSize, ST, &(Baling->AlignInfo));
          if (ThisWidth < Width) {
            Width = ThisWidth;
          }
        }
      }
    }
  }

  return Width;
}

/***********************************************************************
 * determineNonRegionWidth : determine max valid width of non-region instruction
 *
 * Enter:   Inst = the instruction
 *          StartIdx = start index
 *
 * Return:  max valid width
 */
unsigned GenXLegalization::determineNonRegionWidth(Instruction *Inst, unsigned StartIdx)
{
  VectorType *VT = dyn_cast<VectorType>(Inst->getType());
  if (!VT)
    return 1;
  unsigned Width = VT->getNumElements() - StartIdx;
  unsigned BytesPerElement = VT->getElementType()->getPrimitiveSizeInBits() / 8;
  // Check whether the operand element size is bigger than the result operand
  // size. Normally we just check operand 0. This won't work on a select, and
  // we don't need to do the check on a select anyway as its operand and result
  // type are the same.
  if (!isa<SelectInst>(Inst)) {
    unsigned NumOperands = Inst->getNumOperands();
    if (CallInst *CI = dyn_cast<CallInst>(Inst))
      NumOperands = CI->getNumArgOperands();
    if (NumOperands) {
      assert(isa<VectorType>(Inst->getOperand(0)->getType()) &&
             "instruction not supported");
      unsigned InBytesPerElement = cast<VectorType>(Inst->getOperand(0)->getType())
          ->getElementType()->getPrimitiveSizeInBits() / 8;
      if (InBytesPerElement > BytesPerElement)
        BytesPerElement = InBytesPerElement;
    }
  }
  if (BytesPerElement) {
    // Non-predicate result.
    if (Width * BytesPerElement > 64)
      Width = 64 / BytesPerElement;
    Width = 1 << llvm::log2(Width);
  } else {
    // Predicate result. This is to handle and/or/xor/not of predicates; cmp's
    // def of a predicate is handled separately where this function is called
    // in determineWidth().
    Width = getPredPart(Inst, StartIdx).Size;
  }
  return Width;
}

/***********************************************************************
 * getLegalPredSize : get legal predicate size
 *
 * Enter:   Pred = predicate value
 *          ElementTy = element type, 0 to assume not 64 bit
 *          StartIdx = start index in that predicate
 *          RemainingSize = remaining size from StartIdx in whole vector
 *                          operation being split, or 0 to imply from the
 *                          number of elements in the type of Pred
 *
 * Return:  Min = min legal size
 *          Max = max legal size
 */
LegalPredSize GenXLegalization::getLegalPredSize(Value *Pred, Type *ElementTy,
    unsigned StartIdx, unsigned RemainingSize)
{
  // Get details of the part containing StartIdx.
  auto PP = getPredPart(Pred, StartIdx);
  // Set Min to 8, or 4 if the element type of the operation using the
  // intrinsic is 64 bit. Doing this ensures that the next split in the same
  // part is on a legal offset. The offset of a split within a part must be 8
  // aligned, or 4 aligned if the element type is 64 bit.
  LegalPredSize Ret;
  Ret.Min = !ElementTy ? 8 : ElementTy->getPrimitiveSizeInBits() != 64 ? 8 : 4;
  // Set Max to the remaining size left in this part, rounded down to a power
  // of two.
  unsigned LogMax = Log2_32(PP.Size - StartIdx + PP.Offset);
  // However, Max cannot be any bigger than the misalignment of the offset into
  // the part. For example. if the offset is 4 or 12, the size must be 4, not 8
  // or 16.
  LogMax = std::min(LogMax, findFirstSet(StartIdx - PP.Offset));
  Ret.Max = 1 << LogMax;
  // If Min>Max, then we're at the end of that part and we don't need to ensure
  // that the next split in the same part is legally aligned.
  Ret.Min = std::min(Ret.Min, Ret.Max);
  return Ret;
}

/***********************************************************************
 * getPredPart : get info on which part of a predicate an index is in
 *
 * Enter:   V = a value of predicate type
 *          Offset = offset to get info on
 *
 * Return:  PredPart struct with
 *            Offset = start offset of the part 
 *            Size = size of the part
 *            PartNum = part number
 *
 * On entry, Offset is allowed to be equal to the total size of V, in which
 * case the function returns PartNum = the number of parts and Size = 0.
 *
 * This function is what determines how an illegally sized predicate is divided
 * into parts. It is constrained by vISA only allowing a power of two size for
 * each part. Therefore it divides into zero or more 32 bit parts (currently 16
 * bit), then descending powers of two to fill up any odd size end.
 *
 * These parts correspond to how predicate values in the IR are divided up, not
 * just how instructions that use or define them get legalized. Thus a
 * predicate of size 13 actually gets divided into parts of 8,4,1 as vISA
 * predicate registers P1,P2,P3 (for example).
 */
PredPart GenXLegalization::getPredPart(Value *V, unsigned Offset)
{
  unsigned WholeSize = V->getType()->getVectorNumElements();
  PredPart Ret;
  if (Offset == WholeSize && !(WholeSize & (MaxPredSize - 1))) {
    Ret.Offset = Offset;
    Ret.Size = 0;
    Ret.PartNum = Offset / MaxPredSize;
    return Ret;
  }
  if ((Offset ^ WholeSize) & -MaxPredSize) {
    // This is in one of the 32 bit parts.
    Ret.Offset = Offset & -MaxPredSize;
    Ret.Size = MaxPredSize;
    Ret.PartNum = Offset / MaxPredSize;
    return Ret;
  }
  // This is in the odd less-than-32 section at the end.
  Ret.Offset = WholeSize & -MaxPredSize;
  Ret.PartNum = WholeSize / MaxPredSize;
  for (unsigned Pwr2 = MaxPredSize / 2U; ; Pwr2 >>= 1) {
    if (Pwr2 <= Offset - Ret.Offset) {
      Ret.Offset += Pwr2;
      ++Ret.PartNum;
      if (Offset == WholeSize && Ret.Offset == Offset) {
        Ret.Size = 0;
        break;
      }
    } 
    if (Pwr2 <= WholeSize - Ret.Offset && Pwr2 > Offset - Ret.Offset) {
      Ret.Size = Pwr2;
      break;
    }
  }
  return Ret;
}

/***********************************************************************
 * splitBale : do one split of the bale
 *
 * Enter:   Last = result of previous split, undef if this is the first one
 *          StartIdx = start index of split
 *          Width = width of split
 *          InsertBefore = instruction to insert before
 *
 * Return:  result of this split
 */
Value *GenXLegalization::splitBale(Value *Last, unsigned StartIdx,
    unsigned Width, Instruction *InsertBefore)
{
  // For each instruction in the bale:
  Value *NewLast = nullptr;
  for (Bale::iterator i = B.begin(), e = B.end(); i != e; ++i) {
    BaleInst BI = *i;
    // Split the instruction.
    SplitMap[BI.Inst] = NewLast = splitInst(Last, BI, StartIdx, Width,
        InsertBefore, BI.Inst->getDebugLoc());
  }
  auto Head = B.getHead();
  if (Head->Info.Type != BaleInfo::WRREGION
      && Head->Info.Type != BaleInfo::WRPREDPREDREGION) {
    // Need to join this result into the overall result with a wrregion or
    // wrpredregion. Do not generate the join if it is a write into the whole
    // of the overall result, which can happen when going through the split
    // code even when no split is required other than conversion to multi
    // indirect.
    auto VT = cast<VectorType>(Head->Inst->getType());
    if (VT->getNumElements() != Width) {
      if (!VT->getElementType()->isIntegerTy(1)) {
        Region R(VT);
        R.Width = R.NumElements = Width;
        R.Offset = StartIdx * R.ElementBytes;
		assert(NewLast);
        NewLast = R.createWrRegion(Last, NewLast,
            NewLast->getName() + ".join" + Twine(StartIdx), InsertBefore,
            Head->Inst->getDebugLoc());
      } else {
        assert(NewLast);
        auto NewWr = Region::createWrPredRegion(Last, NewLast, StartIdx, 
            NewLast->getName() + ".join" + Twine(StartIdx), InsertBefore,
            Head->Inst->getDebugLoc());
        NewLast = NewWr;
        // If this is the first wrpredregion into an illegally sized predicate,
        // save it for processing later. (Only the first one could possibly be
        // the root of a tree of wrpredregions, and only the roots of
        // wrpredregion trees need to be in IllegalPredicates.)
        if (!StartIdx) {
          auto PredSize = getLegalPredSize(NewWr, nullptr, 0);
          if (PredSize.Max != NewWr->getType()->getVectorNumElements())
            IllegalPredicates.insert(NewWr);
        }
      }
    }
  }
  SplitMap.clear();
  return NewLast;
}

/***********************************************************************
 * splitInst : split an instruction in the bale
 *
 * Enter:   Last = result of previous split, undef if this is the first one
 *                 (only used when splitting a wrregion)
 *          BInst = the BaleInst for this instruction
 *          StartIdx = element start index for this split
 *          Width = number of elements in this split
 *          InsertBefore = insert new inst before this point
 *          DL = debug location to give new instruction(s)
 *
 * Return:  the new split value (which is not necessarily a new instruction
 *          if it would have been a wrregion with 0 mask)
 */
Value *GenXLegalization::splitInst(Value *Last, BaleInst BInst,
    unsigned StartIdx, unsigned Width, Instruction *InsertBefore,
    const DebugLoc &DL)
{
  switch (BInst.Info.Type) {
    case BaleInfo::WRREGION:
      {
        Region R(BInst.Inst, BInst.Info);
        R.getSubregion(StartIdx, Width);
        if (R.Mask && isa<VectorType>(R.Mask->getType()))
          R.Mask = getSplitOperand(BInst.Inst,
              Intrinsic::GenXRegion::PredicateOperandNum, StartIdx, Width,
              InsertBefore, DL);
        // For SplitIdx==0, the old vector value comes from the original
        // wrregion. Otherwise it comes from the split wrregion created
        // last time round.
        Value *In = !StartIdx ? BInst.Inst->getOperand(0) : Last;
        Value *MaybeNewWrRegion = R.createWrRegion(In,
            getSplitOperand(BInst.Inst, 1, StartIdx, Width, InsertBefore, DL),
            BInst.Inst->getName() + ".join" + Twine(StartIdx), InsertBefore, DL);
        return MaybeNewWrRegion;
      }
    case BaleInfo::RDREGION:
      {
        // Allow for this being a rdregion baled in to a TWICEWIDTH operand.
        // If it is, double the start index and width.
        unsigned Doubling = TwiceWidth && BInst.Inst == *TwiceWidth;
        StartIdx <<= Doubling;
        Width <<= Doubling;
        // Get the subregion.
        Region R(BInst.Inst, BInst.Info);
        // Check whether this is an indirect operand that was allowed only
        // because we assumed that we are going to convert it to a multi
        // indirect.
        bool ConvertToMulti = R.Indirect && Width != 1
            && R.getLegalSize(StartIdx, true/*Allow2D*/,
              BInst.Inst->getOperand(0)->getType()->getVectorNumElements(),
              ST, &(Baling->AlignInfo)) == 1;
        R.getSubregion(StartIdx, Width);
        // The region to read from. This is normally from the input region baled
        // in. If this is reading from and writing to the same region and
        // split progapation is on, then just reading from the last joined value
        // (but not the initial undef).
        //
        Value *OldVal = BInst.Inst->getOperand(0);
        if (!isa<UndefValue>(Last) && CurSplitKind == SplitKind_Propagation) {
          auto Head = B.getHead();
          if (Head->Info.Type == BaleInfo::WRREGION) {
            Value *WrRegionInput = Head->Inst->getOperand(0);
            if (OldVal == WrRegionInput)
              OldVal = Last;
          }
        }
        if (!ConvertToMulti) {
          // Not converting to multi indirect.
          return R.createRdRegion(OldVal, BInst.Inst->getName() + ".split" +
                                              Twine(StartIdx),
                                  InsertBefore, DL);
        }
        // Converting to multi indirect.
        return convertToMultiIndirect(BInst.Inst, OldVal, &R);
      }
    case BaleInfo::WRPREDPREDREGION:
      {
        unsigned WrPredStart = cast<ConstantInt>(BInst.Inst->getOperand(2))
            ->getZExtValue();
        Value *WrPredNewVal = getSplitOperand(BInst.Inst, 1, StartIdx, Width,
            InsertBefore, DL);
        // For SplitIdx==0, the old vector value comes from the original
        // wrregion. Otherwise it comes from the split wrregion created
        // last time round.
        Value *In = !StartIdx ? BInst.Inst->getOperand(0) : Last;
        // Create the split wrpredpredregion. Note that the mask is passed in
        // its original unsplit form; the spec of wrpredpredregion is that the
        // mask is the same size as the result, and the index is used to slice
        // the mask as well as to determine the slice where the value is written
        // in the result.
        return Region::createWrPredPredRegion(In, WrPredNewVal,
            StartIdx + WrPredStart, BInst.Inst->getOperand(3),
            BInst.Inst->getName() + ".split" + Twine(StartIdx),
            InsertBefore, DL);
      }
    case BaleInfo::RDPREDREGION:
      {
        unsigned RdPredStart = cast<ConstantInt>(BInst.Inst->getOperand(1))
            ->getZExtValue();
        Value *RdPredInput = BInst.Inst->getOperand(0);
        return Region::createRdPredRegionOrConst(RdPredInput,
            RdPredStart + StartIdx,
            Width, BInst.Inst->getName() + ".split" + Twine(StartIdx),
            InsertBefore, DL);
      }
  }
  // Splitting non-region instruction.
  assert(!isa<PHINode>(BInst.Inst) && "not expecting to split phi node");
  if (CastInst *CI = dyn_cast<CastInst>(BInst.Inst)) {
    Type *CastToTy = VectorType::get(
        cast<VectorType>(CI->getType())->getElementType(), Width);
    Instruction *NewInst = CastInst::Create(CI->getOpcode(),
        getSplitOperand(CI, 0, StartIdx, Width, InsertBefore, DL),
        CastToTy, CI->getName() + ".split" + Twine(StartIdx),
        InsertBefore);
    NewInst->setDebugLoc(DL);
    return NewInst;
  }
  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(BInst.Inst)) {
    Instruction *NewInst = BinaryOperator::Create(BO->getOpcode(),
        getSplitOperand(BO, 0, StartIdx, Width, InsertBefore, DL),
        getSplitOperand(BO, 1, StartIdx, Width, InsertBefore, DL),
        BO->getName() + ".split" + Twine(StartIdx), InsertBefore);
    NewInst->setDebugLoc(DL);
    return NewInst;
  }
  if (CmpInst *CI = dyn_cast<CmpInst>(BInst.Inst)) {
    Instruction *NewInst = CmpInst::Create(CI->getOpcode(), CI->getPredicate(),
        getSplitOperand(CI, 0, StartIdx, Width, InsertBefore, DL),
        getSplitOperand(CI, 1, StartIdx, Width, InsertBefore, DL),
        CI->getName() + ".split" + Twine(StartIdx), InsertBefore);
    NewInst->setDebugLoc(DL);
    return NewInst;
  }
  if (auto SI = dyn_cast<SelectInst>(BInst.Inst)) {
    Value *Selector = getSplitOperand(SI, 0, StartIdx, Width, InsertBefore, DL);
    Selector = splatPredicateIfNecessary(Selector, Width, InsertBefore, DL);
    auto Split1 = getSplitOperand(SI, 1, StartIdx, Width, InsertBefore, DL);
    auto Split2 = getSplitOperand(SI, 2, StartIdx, Width, InsertBefore, DL);
    auto NewInst = SelectInst::Create(Selector, Split1, Split2,
        SI->getName() + ".split" + Twine(StartIdx), InsertBefore);
    NewInst->setDebugLoc(DL);
    return NewInst;
  }
  // Must be a splittable intrinsic.
  CallInst *CI = dyn_cast<CallInst>(BInst.Inst);
  assert(CI);
  unsigned IntrinID = CI->getCalledFunction()->getIntrinsicID();
  assert(IntrinID != Intrinsic::not_intrinsic);
  if (IntrinID == Intrinsic::genx_constanti
      || IntrinID == Intrinsic::genx_constantf) {
    // This is the constant loading intrinsic.
    // We don't need to load the split constants, since a constant value-to-
    // write operand is valid in the wrregions that will be used to link
    // the values back together.
    return getSplitOperand(BInst.Inst, 0, StartIdx, Width, InsertBefore, DL);
  }
  // Some other splittable intrinsic.
  GenXIntrinsicInfo II(IntrinID);
  unsigned NotFixed4Operand = 0;
  SmallVector<Value *, 2> Args;
  for (unsigned i = 0, e = CI->getNumArgOperands(); i != e; ++i) {
    Use *U = &CI->getOperandUse(i);
    if (U == Fixed4) {
      // FIXED4: operand is fixed size 4-vector that is not split.
      if (i == NotFixed4Operand)
        ++NotFixed4Operand;
      Args.push_back(CI->getArgOperand(i));
    } else if (U == TwiceWidth) {
      // TWICEWIDTH: operand is twice the width of other operand and result
      Args.push_back(getSplitOperand(BInst.Inst, i, StartIdx * 2, Width * 2,
            InsertBefore, DL));
    } else
      Args.push_back(
          getSplitOperand(BInst.Inst, i, StartIdx, Width, InsertBefore, DL));
  }
  // Assume overloaded and resolved by ret type and and the type of the first
  // arg that is not FIXED4 (usually arg0, except for line and pln).
  Type *RetTy = VectorType::get(cast<VectorType>(BInst.Inst->getType())->getElementType(), Width);
  Type *OverloadedTypes[] = { RetTy, Args[NotFixed4Operand]->getType() };
  Module *M = InsertBefore->getParent()->getParent()->getParent();
  Function *Decl = nullptr;
  switch (IntrinID) {
    case Intrinsic::fma:
    case Intrinsic::genx_absf:
    case Intrinsic::genx_absi:
    case Intrinsic::genx_sbfe:
    case Intrinsic::genx_ubfe:
    case Intrinsic::genx_bfi:
    case Intrinsic::genx_bfrev:
    case Intrinsic::genx_cos:
    case Intrinsic::genx_dp2:
    case Intrinsic::genx_dp3:
    case Intrinsic::genx_dp4:
    case Intrinsic::genx_dph:
    case Intrinsic::genx_exp:
    case Intrinsic::genx_sfbh:
    case Intrinsic::genx_ufbh:
    case Intrinsic::genx_fbl:
    case Intrinsic::genx_frc:
    case Intrinsic::genx_inv:
    case Intrinsic::genx_line:
    case Intrinsic::genx_log:
    case Intrinsic::genx_lrp:
    case Intrinsic::genx_lzd:
    case Intrinsic::genx_pow:
    case Intrinsic::genx_rndd:
    case Intrinsic::genx_rnde:
    case Intrinsic::genx_rndu:
    case Intrinsic::genx_rndz:
    case Intrinsic::genx_rsqrt:
    case Intrinsic::genx_sat:
    case Intrinsic::genx_sin:
    case Intrinsic::genx_sqrt:
    case Intrinsic::genx_ieee_sqrt:
    case Intrinsic::genx_ieee_div:
      // These intrinsics only overload the return type; the arg type must be
      // the same.
      Decl = Intrinsic::getDeclaration(M, (Intrinsic::ID)IntrinID,
          OverloadedTypes[0]);
      break;
    default:
      // Other alu intrinsics overload both the return type and the arg type.
      Decl = Intrinsic::getDeclaration(M, (Intrinsic::ID)IntrinID,
          OverloadedTypes);
      break;
  }
  Instruction *NewInst = CallInst::Create(Decl, Args,
      CI->getName() + ".split" + Twine(StartIdx), InsertBefore);
  NewInst->setDebugLoc(DL);
  return NewInst;
}

/***********************************************************************
 * getSplitOperand : get a possibly split operand
 *
 * Enter:   Inst = original non-split instruction
 *          OperandNum = operand number we want
 *          StartIdx = element start index for this split
 *          Size = number of elements in this split
 *          InsertBefore = where to insert any added rdregion
 *          DL = debug location to give new instruction(s)
 *
 * If the requested operand is a constant, it splits the constant.
 * Otherwise it creates an rdregion from the original operand.
 */
Value *GenXLegalization::getSplitOperand(Instruction *Inst, unsigned OperandNum,
    unsigned StartIdx, unsigned Size, Instruction *InsertBefore,
    const DebugLoc &DL)
{
  Value *V = Inst->getOperand(OperandNum);
  if (!isa<VectorType>(V->getType()))
    return V; // operand not vector, e.g. variable index in region
  if (auto C = dyn_cast<Constant>(V))
    return getConstantSubvector(C, StartIdx, Size);
  // Split a non-constant vector.
  if (Instruction *OperandInst = dyn_cast<Instruction>(V)) {
    auto i = SplitMap.find(OperandInst);
    if (i != SplitMap.end()) {
      // Operand is another instruction in the bale being split.
      return i->second;
    }
  }
  // Non-constant operand not baled in.
  // Create an rdregion for the operand.
  if (!V->getType()->getScalarType()->isIntegerTy(1)) {
    Region R(V);
    R.getSubregion(StartIdx, Size);
    return R.createRdRegion(V,
        V->getName() + ".split" + Twine(StartIdx), InsertBefore, DL);
  }
  // Predicate version.
  return Region::createRdPredRegion(V, StartIdx, Size,
      V->getName() + ".split" + Twine(StartIdx), InsertBefore, DL);
}

/***********************************************************************
 * convertToMultiIndirect : convert a rdregion into multi-indirect
 *
 * Enter:   Inst = original rdregion
 *          LastJoinVal = the acutal region to read from
 *          R = region for it, already subregioned if applicable
 *
 * Return:  new rdregion instruction (old one has not been erased)
 */
Instruction *GenXLegalization::convertToMultiIndirect(Instruction *Inst,
                                                      Value *LastJoinVal,
                                                      Region *R) {
  assert(!R->is2D() && (R->NumElements == 4 || R->NumElements == 8));
  Value *Indirect = R->Indirect;
  assert(Indirect);
  Instruction *InsertBefore = Inst;
  DebugLoc DL = Inst->getDebugLoc();

  // scalar indirect index
  if (R->Stride == 1 && !R->is2D() && !isa<VectorType>(Indirect->getType())
      && ST->hasIndirectGRFCrossing()) {
    Instruction *NewInst =
        R->createRdRegion(LastJoinVal, Inst->getName(), InsertBefore, DL);
    return NewInst;
  }

  // 1. Splat the address. (We will get multiple copies of this
  // instruction, one per split, but they will be CSEd away.)
  Instruction *SplattedIndirect = CastInst::Create(Instruction::BitCast,
      Indirect, VectorType::get(Indirect->getType(), 1),
      Twine(Indirect->getName()) + ".splat", InsertBefore);
  SplattedIndirect->setDebugLoc(DL);
  Region AddrR(SplattedIndirect);
  AddrR.Stride = 0;
  AddrR.Width = AddrR.NumElements = R->NumElements;
  SplattedIndirect = AddrR.createRdRegion(SplattedIndirect,
      SplattedIndirect->getName(), InsertBefore, DL);
  // 2. Add the constant vector <0,1,2,3,4,5,6,7> to it (adjusted
  // for stride in bytes).
  uint16_t OffsetValues[8];
  for (unsigned i = 0; i != 8; ++i)
    OffsetValues[i] = i * (R->Stride * R->ElementBytes);
  Constant *Offsets = ConstantDataVector::get(
      InsertBefore->getContext(),
      ArrayRef<uint16_t>(OffsetValues).slice(0, R->NumElements));
  SplattedIndirect = BinaryOperator::Create(Instruction::Add,
      SplattedIndirect, Offsets, SplattedIndirect->getName(),
      InsertBefore);
  SplattedIndirect->setDebugLoc(DL);
  // 3. Create the multi indirect subregion.
  R->Indirect = SplattedIndirect;
  R->VStride = R->Stride;
  R->Stride = 1;
  R->Width = 1;
  Instruction *NewInst =
      R->createRdRegion(LastJoinVal, Inst->getName(), InsertBefore, DL);
  return NewInst;
}

/***********************************************************************
 * transformByteMove : transform a byte move into short or int move
 *
 * Enter:   B = bale (not necessarily a byte move)
 *
 * Return:  0 if nothing changed, else the new head of bale (ignoring the
 *          bitcasts inserted either side)
 *
 * If the bale is a byte move (a lone wrregion or lone rdregion or
 * rdregion+wrregion where the element type is byte), and the region parameters
 * are suitably aligned, we turn it into a short or int move. This saves the
 * jitter having to split the byte move into an even half and an odd half.
 *
 * If the code is modified, it updates bale info.
 *
 * This optimization needs to be done when baling info is available, so
 * legalization is a handy place to put it.
 */
Instruction *GenXLegalization::transformByteMove(Bale *B)
{
  auto HeadBI = B->getHead();
  Instruction *Head = HeadBI->Inst;
  if (!Head->getType()->getScalarType()->isIntegerTy(8))
    return nullptr;
  Instruction *Wr = nullptr, *Rd = nullptr;
  if (HeadBI->Info.Type == BaleInfo::WRREGION) {
    Wr = Head;
    if (HeadBI->Info.isOperandBaled(Intrinsic::GenXRegion::NewValueOperandNum)) {
      Rd = dyn_cast<Instruction>(
        Wr->getOperand(Intrinsic::GenXRegion::NewValueOperandNum));
      if (!isRdRegion(getIntrinsicID(Rd)))
        return nullptr;
    }
  }
  else {
    if (HeadBI->Info.Type != BaleInfo::RDREGION)
      return nullptr;
    Rd = Head;
  }
  // Now Rd is the rdregion and Wr is the wrregion, and one of them might be 0.
  if (Rd && !isa<VectorType>(Rd->getType()))
    return nullptr;
  if (Wr && !isa<VectorType>(Wr->getOperand(1)->getType()))
    return nullptr;
  assert(Rd || Wr);
  Value *In = Rd ? Rd->getOperand(0) : Wr->getOperand(1);
  Region WrR;
  if (Wr) {
    WrR = Region(Wr, BaleInfo());
    if (WrR.Stride != 1 || WrR.Indirect || WrR.Mask)
      return nullptr;
  } else
    WrR = Region(Rd); // representing just the result of the rd, not the region
  Region RdR;
  if (Rd) {
    RdR = Region(Rd, BaleInfo());
    if (RdR.Stride != 1 || RdR.Indirect)
      return nullptr;
  } else
    RdR = Region(Wr->getOperand(0)); // representing just the value being
                                     // written in to the region
  unsigned InNumElements = In->getType()->getVectorNumElements();
  assert(Wr || Rd);
  unsigned OutNumElements = (Wr ? Wr : Rd)->getType()->getVectorNumElements();
  unsigned Misalignment = InNumElements | OutNumElements | RdR.NumElements
      | RdR.Width | RdR.VStride | RdR.Offset | WrR.NumElements | WrR.Width
      | WrR.VStride | WrR.Offset;
  if (Misalignment & 1)
    return nullptr;
  unsigned LogAlignment = Misalignment & 2 ? 1 : 2;
  auto InTy = VectorType::get(Type::getIntNTy(Head->getContext(),
        8 << LogAlignment), InNumElements >> LogAlignment);
  // Create the bitcast of the input if necessary. (We do that even if the input is constant,
  // on the basis that EarlyCSE will simplify it.)
  Value *BCIn = nullptr;
  if (BitCastInst *InCast = dyn_cast<BitCastInst>(In)) {
    if (InCast->getSrcTy() == InTy )
      BCIn = InCast->getOperand(0);
  }
  if (BCIn == nullptr) {
    BCIn = CastInst::Create(Instruction::BitCast, In, InTy,
      "bytemov", Head);
    cast<CastInst>(BCIn)->setDebugLoc(Head->getDebugLoc());
  }
  Value *Val = BCIn;
  if (Rd) {
    // Create the new rdregion.
    RdR.NumElements >>= LogAlignment;
    RdR.VStride >>= LogAlignment;
    RdR.Width >>= LogAlignment;
    auto NewRd = RdR.createRdRegion(Val, "", Head, Rd->getDebugLoc(),
        /*AllowScalar=*/false);
    NewRd->takeName(Rd);
    Baling->setBaleInfo(NewRd, BaleInfo(BaleInfo::RDREGION));
    Val = NewRd;
  }
  if (Wr) {
    // Create the bitcast of the old value of the vector. (Or just reuse
    // the first bitcast if it is of the same value -- I saw this in
    // Boxfilter.)
    Value *BCOld = BCIn;
    if (In != Wr->getOperand(0)) {
      Value *OV = Wr->getOperand(0);
      BCOld = nullptr;
      auto ResTy = VectorType::get(Type::getIntNTy(Head->getContext(),
        8 << LogAlignment), OutNumElements >> LogAlignment);
      if (BitCastInst *OVCast = dyn_cast<BitCastInst>(OV)) {
        if (OVCast->getSrcTy() == ResTy)
          BCOld = OVCast->getOperand(0);
      }
      if (BCOld == nullptr) {
        BCOld = CastInst::Create(Instruction::BitCast, OV, ResTy,
          "bytemov", Head);
        cast<CastInst>(BCOld)->setDebugLoc(Wr->getDebugLoc());
      }
    }
    // Create the new wrregion.
    WrR.NumElements >>= LogAlignment;
    WrR.VStride >>= LogAlignment;
    WrR.Width >>= LogAlignment;
    auto NewWr = cast<Instruction>(WrR.createWrRegion(BCOld, Val, "",
          Head, Wr->getDebugLoc()));
    NewWr->takeName(Wr);
    BaleInfo BI(BaleInfo::WRREGION);
    if (Rd)
      BI.setOperandBaled(Intrinsic::GenXRegion::NewValueOperandNum);
    Baling->setBaleInfo(NewWr, BI);
    Val = NewWr;
  }

  bool NeedBC = true;
  if (Head->hasOneUse()) {
    auto U = Head->use_begin()->getUser();
    if (BitCastInst *UBC = dyn_cast<BitCastInst>(U)) {
      if (UBC->getDestTy() == Val->getType()) {
        UBC->replaceAllUsesWith(Val);
        eraseInst(UBC);
        NeedBC = false;
      }
    }
  }
  if (NeedBC) {
    // Create the bitcast back to the original type.
    auto BCOut = CastInst::Create(Instruction::BitCast, Val,
      Head->getType(), "bytemov", Head);
    BCOut->setDebugLoc(Head->getDebugLoc());
    // Replace and erase the original rdregion and wrregion. We do not need
    // to do anything with their baling info as that is a ValueMap and they get
    // removed automatically.
    Head->replaceAllUsesWith(BCOut);
  }
  if (Wr)
    eraseInst(Wr);
  if (Rd)
    eraseInst(Rd);
  // Return the new wrregion if any, else the new rdregion. Do not return
  // BCOut as it is not part of the bale for the move.
  assert(dyn_cast<Instruction>(Val));
  return cast<Instruction>(Val);
}

/***********************************************************************
 * splatPredicateIfNecessary : splat a wrregion/select predicate if necessary
 *
 * Enter:   V = the predicate
 *          Width = width it needs to be splatted to
 *          InsertBefore = where to insert new instructions
 *          DL = debug loc for new instructions
 *
 * Return:  the predicate, possibly a new instruction
 *
 * From GenXLegalization onwards, the predicate (mask) in a wrregion must
 * either be scalar constant 1, or have the same vector width as the value
 * being written by the wrregion. Similarly for the selector in a vector
 * select, except that is not allowed to be scalar constant 1.
 *
 * It might make more sense to do this in GenXLowering, except that the
 * predicate might be wider than 32 at that point. So we have to do it here.
 */
Value *GenXLegalization::splatPredicateIfNecessary(Value *V,
    Type *ValueToWriteTy, Instruction *InsertBefore, DebugLoc DL)
{
  if (auto VT = dyn_cast<VectorType>(ValueToWriteTy))
    return splatPredicateIfNecessary(V, VT->getNumElements(), InsertBefore, DL);
  return V;
}

Value *GenXLegalization::splatPredicateIfNecessary(Value *V, unsigned Width,
    Instruction *InsertBefore, DebugLoc DL)
{
  if (Width == 1)
    return V;
  if (auto C = dyn_cast<Constant>(V))
    if (C->isAllOnesValue())
      return V;
  if (isa<VectorType>(V->getType()))
    return V;
  // Round Width up to 16 or 32. (No point in using up a 32 bit predicate
  // register if we only need 16.)
  unsigned RoundedWidth = Width > 16 ? 32 : 16;
  // Use a select to turn the predicate into 0 or -1.
  auto ITy = Type::getIntNTy(InsertBefore->getContext(), RoundedWidth);
  auto Sel = SelectInst::Create(V, Constant::getAllOnesValue(ITy),
      Constant::getNullValue(ITy), InsertBefore->getName() + ".splatpredicate",
      InsertBefore);
  Sel->setDebugLoc(DL);
  // Bitcast that to v16i1 or v32i1 predicate (which becomes a setp instruction).
  Instruction *Res = CastInst::Create(Instruction::BitCast, Sel,
      VectorType::get(Type::getInt1Ty(InsertBefore->getContext()), RoundedWidth),
      InsertBefore->getName() + ".splatpredicate", InsertBefore);
  Res->setDebugLoc(DL);
  // If the required size is smaller, do an rdpredregion.
  if (Width == RoundedWidth)
    return Res;
  return Region::createRdPredRegionOrConst(Res, 0, Width,
        Res->getName() + ".rdpredregion", InsertBefore, DL);
}

/***********************************************************************
 * eraseInst : erase instruction, updating CurrentInst if we're erasing that
 */
void GenXLegalization::eraseInst(Instruction *Inst)
{
  removingInst(Inst);
  // If the result is a predicate, ensure it is removed from IllegalPredicates,
  // just in case it is a wrpredregion that was in IllegalPredicates.
  if (auto VT = dyn_cast<VectorType>(Inst->getType()))
    if (VT->getElementType()->isIntegerTy(1))
      IllegalPredicates.remove(Inst);
  Inst->eraseFromParent();
}

void GenXLegalization::removingInst(Instruction *Inst)
{
  if (Inst == CurrentInst)
    CurrentInst = Inst->getNextNode();
}

/***********************************************************************
 * fixIllegalPredicates : fix illegally sized predicate values
 */
struct StackEntry {
  Instruction *Wr; // the wrpredregion this stack entry is for
  Instruction *Parent; // its parent wrpredregion in the tree
  SmallVector<Value *, 4> Parts;
  // Constructor given wrpredregion and parent.
  StackEntry(Instruction *Wr, Instruction *Parent) : Wr(Wr), Parent(Parent) {}
};

void GenXLegalization::fixIllegalPredicates(Function *F)
{
  // First fix illegal size predicate phi nodes, replacing each with multiple
  // phi nodes with rdpredregion on the incomings and wrpredregion on the
  // result. These rdpredregions and wrpredregions then get removed with other
  // illegal size predicates in the code below.
  SmallVector<PHINode *, 4> PhisToErase;
  for (auto fi = F->begin(), fe = F->end(); fi != fe; ++fi) {
    auto BB = &*fi;
    Instruction *FirstNonPhi = BB->getFirstNonPHI();
    for (auto Phi = dyn_cast<PHINode>(BB->begin()); Phi;
        Phi = dyn_cast<PHINode>(Phi->getNextNode())) {
      if (!Phi->getType()->getScalarType()->isIntegerTy(1))
        continue;
      // We have a predicate phi. Get the first part of it, which might show that
      // we do not need to split it at all.
      auto VT = dyn_cast<VectorType>(Phi->getType());
      if (!VT)
        continue;
      unsigned WholeSize = VT->getNumElements();
      auto PP = getPredPart(Phi, 0);
      if (PP.Size == WholeSize)
        continue;
      // We do need to split.
      Value *Joined = UndefValue::get(Phi->getType());
      unsigned NumIncoming = Phi->getNumIncomingValues();
      for (unsigned StartIdx = 0; StartIdx != WholeSize; ) {
        // Create a split phi node.
        PP = getPredPart(Phi, StartIdx);
        auto NewPhi = PHINode::Create(
            VectorType::get(Phi->getType()->getScalarType(), PP.Size),
            NumIncoming, Phi->getName() + ".split" + Twine(StartIdx), Phi);
        // Do a rdpredregion for each incoming.
        for (unsigned ii = 0; ii != NumIncoming; ++ii) {
          BasicBlock *IncomingBlock = Phi->getIncomingBlock(ii);
          Value *Incoming = Phi->getIncomingValue(ii);
          auto NewRd = Region::createRdPredRegionOrConst(Incoming, StartIdx,
              PP.Size, Incoming->getName() + ".split" + Twine(StartIdx),
              IncomingBlock->getTerminator(), DebugLoc());
          NewPhi->addIncoming(NewRd, IncomingBlock);
        }
        // Join with previous new phis for this original phi.
        Joined = Region::createWrPredRegion(Joined, NewPhi, StartIdx,
            Phi->getName() + ".join" + Twine(StartIdx), FirstNonPhi,
            DebugLoc());
        // If that was the first join, add it to the IllegalPredicates list for
        // processing its tree of wrpredregions below.
        if (!StartIdx)
          IllegalPredicates.insert(cast<Instruction>(Joined));
        StartIdx += PP.Size;
      }
      // Replace the original phi and mark it for erasing. Also undef out its
      // incomings so it doesn't matter what order we do the erases in.
      auto Undef = UndefValue::get(Phi->getType());
      for (unsigned ii = 0; ii != NumIncoming; ++ii)
        Phi->setIncomingValue(ii, Undef);
      Phi->replaceAllUsesWith(Joined);
      PhisToErase.push_back(Phi);
    }
  }
  for (auto i = PhisToErase.begin(), e = PhisToErase.end(); i != e; ++i)
    (*i)->eraseFromParent();
  // For each entry in IllegalPredicates that is the root of a tree of
  // wrpredregions...
  SmallVector<Instruction *, 4> ToErase;
  for (auto ipi = IllegalPredicates.begin(), ipe = IllegalPredicates.end();
      ipi != ipe; ++ipi) {
    std::vector<StackEntry> Stack;
    auto Root = *ipi;
    if (getIntrinsicID(Root->getOperand(0)) == Intrinsic::genx_wrpredregion)
      continue; // not root of tree
    assert(isa<UndefValue>(Root->getOperand(0)) && "expecting undef input to root of tree");
    // See if it really is illegally sized.
    if (getPredPart(Root, 0).Size == Root->getType()->getVectorNumElements())
      continue;
    // For traversing the tree, create a stack where each entry represents a
    // value in the tree, and contains the values of the parts.  Create an
    // initial entry for the root of the tree.
    Stack.push_back(StackEntry(Root, nullptr));
    // Process stack entries.
    while (!Stack.empty()) {
      auto Entry = &Stack.back();
      if (!Entry->Parts.empty()) {
        // This stack entry has already been processed; we are on the way back
        // down having processed its children. Just pop the stack entry, and
        // mark the wrpredregion for erasing. We do not erase it now because it
        // might be yet to visit in the IllegalPredicates vector.
        ToErase.push_back(Entry->Wr);
        Stack.pop_back();
        continue;
      }
      // Populate Parts with the value of each part from the parent.
      if (!Entry->Parent) {
        // No parent. All parts are undef.
        auto Ty = Entry->Wr->getType();
        unsigned WholeSize = Ty->getVectorNumElements();
        for (unsigned Offset = 0; Offset != WholeSize; ) {
          auto PP = getPredPart(Entry->Wr, Offset);
          Entry->Parts.push_back(
              UndefValue::get(VectorType::get(Ty->getScalarType(), PP.Size)));
          Offset += PP.Size;
        }
      } else {
        // Inherit from parent.
        for (auto i = (Entry - 1)->Parts.begin(),
            e = (Entry - 1)->Parts.end(); i != e; ++i)
          Entry->Parts.push_back(*i);
      }
      // For this wrpredregion, determine the part that it writes to, and see
      // if it is the whole part. (It cannot overlap more than one part,
      // because getLegalPredSize ensured that all splits were within parts.)
      unsigned WrOffset = cast<ConstantInt>(Entry->Wr->getOperand(2))
          ->getZExtValue();
      unsigned WrSize = Entry->Wr->getOperand(1)->getType()
          ->getVectorNumElements();
      auto PP = getPredPart(Entry->Wr, WrOffset);
      assert(WrOffset + WrSize <= PP.Offset + PP.Size && "overlaps multiple parts");
      Value *Part = Entry->Parts[PP.PartNum];
      if (WrSize != PP.Size) {
        // Not the whole part. We need to write into the previous value of this
        // part.
        auto NewWr = Region::createWrPredRegion(Part, Entry->Wr->getOperand(1),
            WrOffset - PP.Offset, "", Entry->Wr, Entry->Wr->getDebugLoc());
        NewWr->takeName(Entry->Wr);
        Part = NewWr;
      } else
        Part = Entry->Wr->getOperand(1);
      // Store the new value of this part.
      Entry->Parts[PP.PartNum] = Part;
      // Gather uses in rdpredregion.
      SmallVector<Instruction *, 4> Rds;
      for (auto ui = Entry->Wr->use_begin(), ue = Entry->Wr->use_end();
          ui != ue; ++ui) {
        auto User = cast<Instruction>(ui->getUser());
        if (getIntrinsicID(User) == Intrinsic::genx_rdpredregion)
          Rds.push_back(User);
      }
      // For each rdpredregion, turn it into a read from the appropriate
      // part.
      for (auto ri = Rds.begin(), re = Rds.end(); ri != re; ++ri) {
        Instruction *Rd = *ri;
        unsigned RdOffset = cast<ConstantInt>(Rd->getOperand(1))->getZExtValue();
        unsigned RdSize = Rd->getType()->getVectorNumElements();
        auto PP = getPredPart(Entry->Wr, RdOffset);
        assert(RdOffset + RdSize <= PP.Offset + PP.Size && "overlaps multiple parts");
        Value *Part = Entry->Parts[PP.PartNum];
        if (RdSize != PP.Size) {
          // Only reading a subregion of a part.
          // Assert that the rdpredregion is legal. In fact we will probably
          // have to cope with an illegal one, by generating code to bitcast
          // the predicate to a scalar int (or finding code where it is already
          // bitcast from a scalar int), using bit twiddling to get the
          // required subregion, and bitcasting back.  I think this situation
          // will arise where the input to legalization had an odd size
          // rdpredregion in a wrregion where the input predicate is a v32i1
          // from an odd size CM select using an i32 as the mask.
#if _DEBUG
          if (RdOffset) {
            unsigned RdMisalignment = 1U << findFirstSet(RdOffset);
            assert((RdMisalignment >= 8
                || (RdMisalignment == 4 && Rd->hasOneUse()
                  && cast<Instruction>(Rd->use_begin()->getUser())->getOperand(1)
                    ->getType()->getScalarType()->getPrimitiveSizeInBits() == 64))
              && !((RdOffset - PP.Offset) % RdSize)
              && "illegal rdpredregion");
          }
#endif
          // Create a new rdpredregion.
          auto NewRd = Region::createRdPredRegion(Part, RdOffset - PP.Offset,
              RdSize, "", Rd, Rd->getDebugLoc());
          NewRd->takeName(Rd);
          Part = NewRd;
        }
        // Replace the original rdpredregion with the value of the part.
        Rd->replaceAllUsesWith(Part);
        Rd->eraseFromParent();
      }
      // All remaining uses must be wrpredregion. Push them onto the stack.
      for (auto ui = Entry->Wr->use_begin(), ue = Entry->Wr->use_end();
          ui != ue; ++ui) {
        auto User = cast<Instruction>(ui->getUser());
        assert(getIntrinsicID(User) == Intrinsic::genx_wrpredregion && !ui->getOperandNo() && "expecting only wrpredregion uses");
        Stack.push_back(StackEntry(User, Entry->Wr));
      }
    }
  }
  // Erase the old wrpredregions.
  for (auto i = ToErase.begin(), e = ToErase.end(); i != e; ++i)
    (*i)->eraseFromParent();
}

GenXLegalization::SplitKind GenXLegalization::checkBaleSplittingKind() {
  auto Head = B.getHead();
  SplitKind Kind = SplitKind::SplitKind_Normal;

  if (Head->Info.Type == BaleInfo::WRREGION) {
    Value *WrRegionInput = Head->Inst->getOperand(0);
    Region R1(Head->Inst, Head->Info);
    for (auto &I : B) {
      if (I.Info.Type != BaleInfo::RDREGION)
        continue;
      if (I.Inst->getOperand(0) != WrRegionInput)
        continue;
      Region R2(I.Inst, I.Info);
      if (R1 != R2) {
        // Check if R1 overlaps with R2. Create a new region for R1 as we are
        // rewriting region offsets if their difference is a constant.
        Region R(Head->Inst, Head->Info);

        // Analyze dynamic offset difference, but only for a scalar offset.
        if (R1.Indirect && R2.Indirect) {
          if (R1.Indirect->getType()->isVectorTy() ||
              R2.Indirect->getType()->isVectorTy())
            return SplitKind::SplitKind_Normal;

          // Strip truncation from bitcast followed by a region read.
          auto stripConv = [](Value *Val) {
            if (isRdRegion(Val)) {
              CallInst *CI = cast<CallInst>(Val);
              Region R(CI, BaleInfo());
              if (R.Offset == 0 && R.Width == 1)
                Val = CI->getOperand(0);
              if (auto BI = dyn_cast<BitCastInst>(Val))
                Val = BI->getOperand(0);
            }
            return Val;
          };

          Value *Offset1 = stripConv(R.Indirect);
          Value *Offset2 = stripConv(R2.Indirect);
          if (Offset1->getType() == Offset2->getType()) {
            auto S1 = SE->getSCEV(Offset1);
            auto S2 = SE->getSCEV(Offset2);
            auto Diff = SE->getMinusSCEV(S1, S2);
            Diff = SE->getTruncateOrNoop(Diff, R.Indirect->getType());
            if (auto SCC = dyn_cast<SCEVConstant>(Diff)) {
              ConstantInt *CI = SCC->getValue();
              int OffsetDiff = std::abs(static_cast<int>(CI->getSExtValue()));
              R.Offset = 0;
              R.Indirect = nullptr;
              R2.Offset = OffsetDiff;
              R2.Indirect = nullptr;
            }
          }
        }

        // Ignore the mask and adjust both offsets by a common dynamic
        // value if exists. If the resulting regions do not overlap, then two
        // original regions do not overlap.
        R.Mask = nullptr;
        R2.Mask = nullptr;

        // As both R and R2 have constant offsets, the overlap function
        // should check their footprints accurately.
        if (R.overlap(R2))
          return SplitKind::SplitKind_Normal;
        Kind = SplitKind::SplitKind_Propagation;
        continue;
      }

      // (1) 1D direct regions or indirect regions with single offset.
      // (2) 2D direct regions with VStride >= Width, or indirect regions with
      //     single offset.
      bool IsMultiAddr = R1.Indirect && R1.Indirect->getType()->isVectorTy();
      if (!R1.is2D()) {
        if (IsMultiAddr)
          return SplitKind::SplitKind_Normal;
        Kind = SplitKind::SplitKind_Propagation;
      } else {
        if (R1.VStride < (int)R1.Width || IsMultiAddr)
          return SplitKind::SplitKind_Normal;
        Kind = SplitKind::SplitKind_Propagation;
      }
    }
  }

  return Kind;
}

// This function deals with intrinsic calls with special restrictions.
// - Certain intrinsic calls should be placed in the entry blocks:
//     llvm.genx.predifined.surface
//
void GenXLegalization::fixIntrinsicCalls(Function *F) {
  auto PF = F->getParent()->getFunction("llvm.genx.predefined.surface");
  if (!PF)
    return;

  // Collect all calls to PF in this function.
  std::map<int64_t, std::vector<Instruction *>> Calls;
  for (auto U : PF->users()) {
    if (auto UI = dyn_cast<CallInst>(U)) {
      BasicBlock *BB = UI->getParent();
      if (BB->getParent() != F)
        continue;
      if (auto CI = dyn_cast<ConstantInt>(UI->getOperand(0))) {
        int64_t Arg = CI->getSExtValue();
        Calls[Arg].push_back(UI);
      }
    }
  }

  BasicBlock *EntryBB = &F->getEntryBlock();
  Instruction *InsertPos = &*EntryBB->getFirstInsertionPt();

  for (auto I = Calls.begin(), E = Calls.end(); I != E; ++I) {
    Instruction *EntryDef = nullptr;
    for (auto Inst : I->second) {
      if (Inst->getParent() == EntryBB) {
        EntryDef = Inst;
        break;
      }
    }

    // No entry definition found, then clone one.
    if (EntryDef == nullptr) {
      EntryDef = I->second.front()->clone();
      EntryDef->insertBefore(InsertPos);
    } else
      EntryDef->moveBefore(InsertPos);

    // Now replace all uses with this new definition.
    for (auto Inst : I->second) {
      std::vector<Instruction *> WorkList{Inst};
      while (!WorkList.empty()) {
        Instruction *CurI = WorkList.back();
        WorkList.pop_back();

        for (auto UI = CurI->use_begin(); UI != CurI->use_end();) {
          Use &U = *UI++;
          // Skip if this use just comes from EntryDef.
          if (EntryDef == U.get())
            continue;
          // All uses of this PHI will be replaced as well.
          if (auto PHI = dyn_cast<PHINode>(U.getUser()))
            WorkList.push_back(PHI);
          U.set(EntryDef);
        }
        if (CurI->use_empty())
          CurI->eraseFromParent();
      }
    }
  }
}