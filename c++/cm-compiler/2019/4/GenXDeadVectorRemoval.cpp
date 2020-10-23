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
/// GenXDeadVectorRemoval
/// ---------------------
///
/// GenXDeadVectorRemoval is an aggressive dead code removal pass that analyzes
/// individual elements of a vector rather than whole values.
///
/// As a result of this analysis, the pass can then make the two following
/// modifications to the code:
///
/// 1. If all vector elements of an instruction result turn out to be unused, the
///    instruction is removed. In fact, this pass just sets all its uses to
///    undef, relying on the subsequent dead code removal pass to actually
///    remove it.
///
/// 2. If all vector elements of the "old value" input (even a constant) of a
///    wrregion turn out to be unused, then that input is set to undef. This
///    covers further cases over (1) above:
///
///    a. the "old value" input is constant, and we want to turn it into undef
///       to save a useless constant load;
///
///    b. the "old value" input is an instruction that does have elements used
///       elsewhere, and we want to turn it into undef to detach the two webs
///       of defs and uses from each other to reduce register pressure in
///       between.
///
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "GENX_DEAD_VECTOR_REMOVAL"

#include "GenX.h"
#include "GenXBaling.h"
#include "GenXRegion.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#include <queue>
#include <set>

using namespace llvm;
using namespace genx;
using namespace Intrinsic::GenXRegion;

static cl::opt<unsigned> LimitGenXDeadVectorRemoval("limit-genx-dead-vector-removal", cl::init(UINT_MAX), cl::Hidden,
                                      cl::desc("Limit GenX dead element removal."));

namespace {

// LiveBitsStorage : encapsulate how live bits for a vector value are stored
// For 31/63 elements or fewer, the bitmap is inside the LiveBitsStorage
// object. For 32/64 elements or more, the bitmap is separately allocated.
class LiveBitsStorage {
  uintptr_t V;
public:
  LiveBitsStorage() : V(0) {}
  ~LiveBitsStorage() {
    if (auto P = getExternal())
      delete[] P;
    V = 0;
  }
private:
  // getExternal : get the external pointer, 0 if none
  // Whether we have an external pointer is encoded in the top bit.
  // The pointer itself is shifted down one and stored in the other bits.
  uintptr_t *getExternal() {
    if ((intptr_t)V >= 0)
      return nullptr; // top bit not set, not external
    return (uintptr_t *)(V * 2);
  }
  // setExternal : set the external pointer
  void setExternal(uintptr_t *P) {
    assert(!getExternal());
    V = (uintptr_t)P >> 1 | (uintptr_t)1U << (sizeof(uintptr_t) * 8 - 1);
  }
public:
  // setNumElements : set the number of elements to be stored in this
  // LiveBitsStorage. Allocate external storage if necessary.
  void setNumElements(unsigned NumElements) {
    if (NumElements >= sizeof(uintptr_t) * 8 - 1) {
      unsigned Size = NumElements + sizeof(uintptr_t) * 8 - 1
            / (sizeof(uintptr_t) * 8);
      setExternal(new uintptr_t[Size]);
      memset(getExternal(), 0, Size * sizeof(uintptr_t));
    }
  }
  // get : get the pointer to the bitmap
  uintptr_t *get() {
    if (auto P = getExternal())
      return P;
    return &V;
  }
};

// LiveBits : encapsulate a pointer to a bitmap of element liveness and its size
class LiveBits {
  uintptr_t *P;
  unsigned NumElements;
public:
  static const unsigned BitsPerWord = sizeof(uintptr_t) * 8;
  LiveBits() : P(nullptr), NumElements(0) {}
  LiveBits(LiveBitsStorage *LBS, unsigned NumElements)
    : P(LBS->get()), NumElements(NumElements) {}
  // getNumElements : get the number of elements in this bitmap
  unsigned getNumElements() const { return NumElements; }
  // get : get a bit value
  bool get(unsigned Idx) const {
    assert(Idx < NumElements);
    return P[Idx / BitsPerWord] >> (Idx % BitsPerWord) & 1;
  }
  // isAllZero : return true if all bits zero
  bool isAllZero() const;
  // set : set a bit value
  // Returns true if value changed
  bool set(unsigned Idx, bool Val = true);
  // copy : copy all bits from another LiveBits
  // Returns true if value changed
  bool copy(LiveBits Src);
  // orBits : or all bits from another LiveBits into this one
  // Returns true if value changed
  bool orBits(LiveBits Src);
  // setRange : set range of bits, returning true if any changed
  bool setRange(unsigned Start, unsigned Len);
  // debug print
  void print(raw_ostream &OS) const;
};

#ifndef NDEBUG
static raw_ostream &operator<<(raw_ostream &OS, const LiveBits &LB) {
  LB.print(OS);
  return OS;
}
#endif

// GenXDeadVectorRemoval : dead vector element removal pass
class GenXDeadVectorRemoval : public FunctionPass {
  std::map<Instruction *, LiveBitsStorage> InstMap;
  std::set<Instruction *> WorkListSet;
  std::queue<Instruction *> WorkList;
  std::set<Instruction *> WrRegionsWithUsedOldInput;
  bool WorkListPhase;
public:
  static char ID;
  explicit GenXDeadVectorRemoval() : FunctionPass(ID) { }
  virtual StringRef getPassName() const { return "GenX dead vector element removal pass"; }
  void getAnalysisUsage(AnalysisUsage &AU) const;
  bool runOnFunction(Function &F);
private:
  void clear() {
    InstMap.clear();
    WorkListSet.clear();
    assert(WorkList.empty());
    WrRegionsWithUsedOldInput.clear();
  }
  bool nullOutInstructions(Function *F);
  void processInst(Instruction *Inst);
  void processRdRegion(Instruction *Inst, LiveBits LB);
  void processWrRegion(Instruction *Inst, LiveBits LB);
  void processBitCast(Instruction *Inst, LiveBits LB);
  void processElementwise(Instruction *Inst, LiveBits LB);
  void markWhollyLive(Value *V);
  void addToWorkList(Instruction *Inst);
  LiveBits getLiveBits(Instruction *Inst, bool Create = false);
};

} // end anonymous namespace


char GenXDeadVectorRemoval::ID = 0;
namespace llvm { void initializeGenXDeadVectorRemovalPass(PassRegistry &); }
INITIALIZE_PASS_BEGIN(GenXDeadVectorRemoval, "GenXDeadVectorRemoval", "GenXDeadVectorRemoval", false, false)
INITIALIZE_PASS_END(GenXDeadVectorRemoval, "GenXDeadVectorRemoval", "GenXDeadVectorRemoval", false, false)

FunctionPass *llvm::createGenXDeadVectorRemovalPass()
{
  initializeGenXDeadVectorRemovalPass(*PassRegistry::getPassRegistry());
  return new GenXDeadVectorRemoval();
}

void GenXDeadVectorRemoval::getAnalysisUsage(AnalysisUsage &AU) const
{
  AU.setPreservesCFG();
}

/***********************************************************************
 * isRootInst : check if this is a "root" instruction, one that we want to
 *    keep even if unused
 */
static bool isRootInst(Instruction *Inst)
{
  if (isa<ReturnInst>(Inst) || isa<BranchInst>(Inst) || isa<TerminatorInst>(Inst))
    return true;
  if (auto CI = dyn_cast<CallInst>(Inst))
    return !CI->onlyReadsMemory();
  return false;
}

/***********************************************************************
 * GenXDeadVectorRemoval::runOnFunction : process one function
 */
bool GenXDeadVectorRemoval::runOnFunction(Function &F)
{
  // First scan all the code to compute the initial live set
  WorkListPhase = false;
  for (po_iterator<BasicBlock *> i = po_begin(&F.getEntryBlock()),
    e = po_end(&F.getEntryBlock()); i != e; ++i) {
    BasicBlock *BB = *i;
    for (Instruction *Inst = BB->getTerminator(); Inst;) {
      if (isRootInst(Inst))
        processInst(Inst);
      else if (WorkListSet.count(Inst)) {
        if (!isa<PHINode>(Inst))
          WorkListSet.erase(Inst);
        processInst(Inst);
      }
      Inst = (Inst == &BB->front()) ? nullptr : Inst->getPrevNode();
    }
  }

  WorkListPhase = true;
  // initialize the worklist
  for (auto Inst : WorkListSet) {
    WorkList.push(Inst);
  }
  // process until the work list is empty.
  DEBUG(dbgs() << "GenXDeadVectorRemoval: process work list\n");
  while (!WorkList.empty()) {
    Instruction *Inst = WorkList.front();
    WorkList.pop();
    WorkListSet.erase(Inst);
    processInst(Inst);
  }
  // Null out unused instructions so the subsequent dead code removal pass
  // removes them.
  DEBUG(dbgs() << "GenXDeadVectorRemoval: null out instructions\n");
  bool Modified = nullOutInstructions(&F);
  clear();
  return Modified;
}

/***********************************************************************
 * nullOutInstructions : null out unused instructions so the subsequent dead
 * code removal pass removes them
 *
 * For wrregion, there are two special cases:
 * - when no elements in the "new value" input of a wrregion are use,
 *   then bypass the wrregion with the "old value".
 * - when no elements in the "old value" input of a wrregion are used, 
 *   then changes the input to undef.
 */
bool GenXDeadVectorRemoval::nullOutInstructions(Function *F)
{
  static unsigned Count = 0;
  bool Modified = false;
  for (auto fi = F->begin(), fe = F->end(); fi != fe; ++fi) {
    for (auto bi = fi->begin(), be = fi->end(); bi != be; ++bi) {
      Instruction *Inst = &*bi;
      // Ignore "root" instructions.
      if (isRootInst(Inst))
        continue;
      // See if the instruction has no used elements. If so, null out its uses.
      auto LB = getLiveBits(Inst);
      if (LB.isAllZero()) {
        if (++Count > LimitGenXDeadVectorRemoval)
          return Modified;
        if (LimitGenXDeadVectorRemoval != UINT_MAX)
          dbgs() << "-limit-genx-dead-vector-removal " << Count << "\n";
        DEBUG(if (!Inst->use_empty())
          dbgs() << "nulled out uses of " << *Inst << "\n");
        while (!Inst->use_empty()) {
          Use *U = &*Inst->use_begin();
          *U = UndefValue::get((*U)->getType());
        }
        Modified = true;
      } else if (isWrRegion(getIntrinsicID(Inst))) {
        // Otherwise, for a wrregion, check if it is in the old input used set.
        // If not, then no element of the "old value" input is used by this
        // instruction (even if it has bits set from other uses), and we can
        // undef out the input.
        Use *U = &Inst->getOperandUse(Intrinsic::GenXRegion::OldValueOperandNum);
        if (WrRegionsWithUsedOldInput.find(Inst)
          == WrRegionsWithUsedOldInput.end()) {
          if (!isa<UndefValue>(*U)) {
            if (++Count > LimitGenXDeadVectorRemoval)
              return Modified;
            if (LimitGenXDeadVectorRemoval != UINT_MAX)
              dbgs() << "-limit-genx-dead-vector-removal " << Count << "\n";
            *U = UndefValue::get((*U)->getType());
            DEBUG(dbgs() << "null out old value input in " << *Inst << "\n");
            Modified = true;
          }
        }
        // when no elements in the "new value" input of a wrregion are use,
        // then bypass the wrregion with the "old value".
        bool bypass = true;
        Region R(Inst, BaleInfo());
        if (R.Mask || R.Indirect)
          bypass = false;
        else {
          for (unsigned RowIdx = R.Offset / R.ElementBytes, Row = 0,
            NumRows = R.NumElements / R.Width; Row != NumRows && bypass;
            RowIdx += R.VStride, ++Row) {
            for (unsigned Idx = RowIdx, Col = 0; Col != R.Width && bypass;
              Idx += R.Stride, ++Col) {
              if (Idx < LB.getNumElements() && LB.get(Idx))
                bypass = false;
            }
          }
        }
        if (bypass) {
          Inst->replaceAllUsesWith(Inst->getOperandUse(Intrinsic::GenXRegion::OldValueOperandNum));
          Modified = true;
        }
      }
    }
  }
  return Modified;
}

/***********************************************************************
 * processInst : process an instruction in the dead element removal pass
 */
void GenXDeadVectorRemoval::processInst(Instruction *Inst)
{
  DEBUG(dbgs() << "  " << *Inst << "\n       has bits " << getLiveBits(Inst) << "\n");
  if (isRootInst(Inst)) {
    // This is a "root" instruction. Mark its inputs as wholly live.
    for (unsigned oi = 0, oe = Inst->getNumOperands(); oi != oe; ++oi)
      markWhollyLive(Inst->getOperand(oi));
    return;
  }
  // Check for the result of the instruction not being used at all.
  auto LB = getLiveBits(Inst);
  if (!LB.getNumElements())
    return;
  // Handle phi node.
  if (auto Phi = dyn_cast<PHINode>(Inst)) {
    processElementwise(Phi, LB);
    return;
  }
  // Special case for bitcast.
  if (auto BC = dyn_cast<BitCastInst>(Inst)) {
    processBitCast(BC, LB);
    return;
  }
  // Check for element-wise instructions.
  if (isa<BinaryOperator>(Inst) || isa<CastInst>(Inst)
      || isa<SelectInst>(Inst) || isa<CmpInst>(Inst)) {
    processElementwise(Inst, LB);
    return;
  }
  // Check for rdregion and wrregion.
  switch (getIntrinsicID(Inst)) {
    case Intrinsic::genx_rdregionf:
    case Intrinsic::genx_rdregioni:
    case Intrinsic::genx_rdpredregion:
      processRdRegion(Inst, LB);
      return;
    case Intrinsic::genx_wrregionf:
    case Intrinsic::genx_wrregioni:
    case Intrinsic::genx_wrconstregion:
    case Intrinsic::genx_wrpredregion:
      processWrRegion(Inst, LB);
      return;
  }
  // For any other instruction, just mark all operands as wholly live.
  for (unsigned oi = 0, oe = Inst->getNumOperands(); oi != oe; ++oi)
    markWhollyLive(Inst->getOperand(oi));
}

/***********************************************************************
 * processRdRegion : process a rdregion instruction for element liveness
 */
void GenXDeadVectorRemoval::processRdRegion(Instruction *Inst, LiveBits LB)
{
  auto InInst = dyn_cast<Instruction>(
      Inst->getOperand(Intrinsic::GenXRegion::OldValueOperandNum));
  Region R(Inst, BaleInfo());
  if (R.Indirect) {
    markWhollyLive(InInst);
    markWhollyLive(Inst->getOperand(Intrinsic::GenXRegion::RdIndexOperandNum));
    return;
  }
  if (!InInst)
    return;
  // Set bits in InLB (InInst's livebits) for live elements read by the
  // rdregion.
  bool Modified = false;
  LiveBits InLB = getLiveBits(InInst, /*Create=*/true);
  for (unsigned RowIdx = R.Offset / R.ElementBytes, Row = 0,
      NumRows = R.NumElements / R.Width; Row != NumRows;
      RowIdx += R.VStride, ++Row)
    for (unsigned Idx = RowIdx, Col = 0; Col != R.Width; Idx += R.Stride, ++Col)
      if (LB.get(Row * R.Width + Col))
        if (Idx < InLB.getNumElements())
          Modified |= InLB.set(Idx);
  if (Modified)
    addToWorkList(InInst);
}

/***********************************************************************
 * processWrRegion : process a wrregion instruction for element liveness
 */
void GenXDeadVectorRemoval::processWrRegion(Instruction *Inst, LiveBits LB)
{
  Region R(Inst, BaleInfo());
  if (R.Mask)
    markWhollyLive(Inst->getOperand(Intrinsic::GenXRegion::PredicateOperandNum));
  auto NewInInst = dyn_cast<Instruction>(
        Inst->getOperand(Intrinsic::GenXRegion::NewValueOperandNum));
  if (R.Indirect) {
    markWhollyLive(NewInInst);
    markWhollyLive(Inst->getOperand(Intrinsic::GenXRegion::WrIndexOperandNum));
  } else if (NewInInst) {
    // Set bits in NewInLB (NewInInst's livebits) for live elements read by
    // the wrregion in the "new value" input.
    bool Modified = false;
    LiveBits NewInLB = getLiveBits(NewInInst, /*Create=*/true);
    for (unsigned RowIdx = R.Offset / R.ElementBytes, Row = 0,
        NumRows = R.NumElements / R.Width; Row != NumRows;
        RowIdx += R.VStride, ++Row)
      for (unsigned Idx = RowIdx, Col = 0; Col != R.Width;
          Idx += R.Stride, ++Col)
        if (Idx < LB.getNumElements() && LB.get(Idx))
          Modified |= NewInLB.set(Row * R.Width + Col);
    if (Modified)
      addToWorkList(NewInInst);
  }
  // For the "old value" input, we want to see if any elements are used even if
  // the input is a constant, since we want to be able to turn it into undef
  // later on if it is not used. In the non-instruction case, OldInLB is left
  // in a state where it contains no bits and OldInLB.getNumElements() is 0.
  LiveBits OldInLB;
  auto OldInInst = dyn_cast<Instruction>(
        Inst->getOperand(Intrinsic::GenXRegion::OldValueOperandNum));
  if (OldInInst)
    OldInLB = getLiveBits(OldInInst, /*Create=*/true);
  bool Modified = false;
  bool UsedOldInput = false;
  if (R.Indirect) {
    if (OldInLB.getNumElements())
      Modified = OldInLB.orBits(LB);
    UsedOldInput = true;
  } else {
    // Set bits in OldLB (OldInInst's livebits) for live elements read by the
    // wrregion in the "old value" input, excluding ones that come from the
    // "new value" input.
    unsigned NextRow = 0, NextCol = 0, NextIdx = R.Offset / R.ElementBytes,
             NextRowIdx = NextIdx, NumRows = R.NumElements / R.Width;
    for (unsigned Idx = 0, End = LB.getNumElements(); Idx != End; ++Idx) {
      if (Idx == NextIdx) {
        // This element comes from the "new value" input, unless the wrregion
        // is predicated in which case it could come from either.
        if (R.Mask && LB.get(Idx)) {
          UsedOldInput = true;
          if (OldInLB.getNumElements())
            Modified |= OldInLB.set(Idx);
        }
        if (++NextCol == R.Width) {
          if (++NextRow == NumRows)
            NextIdx = End;
          else
            NextIdx = NextRowIdx += R.VStride;
          NextCol = 0;
        } else
          NextIdx += R.Stride;
      } else {
        // This element comes from the "old value" input.
        if (LB.get(Idx)) {
          UsedOldInput = true;
          if (OldInLB.getNumElements())
            Modified |= OldInLB.set(Idx);
        }
      }
    }
  }
  if (Modified)
    addToWorkList(OldInInst);
  if (UsedOldInput) {
    // We know that at least one element of the "old value" input is used,
    // so add the wrregion to the used old input set.
    WrRegionsWithUsedOldInput.insert(Inst);
  }
}

/***********************************************************************
 * processBitCast : process a bitcast instruction for element liveness
 */
void GenXDeadVectorRemoval::processBitCast(Instruction *Inst, LiveBits LB)
{
  auto InInst = dyn_cast<Instruction>(Inst->getOperand(0));
  if (!InInst)
    return;
  LiveBits InLB = getLiveBits(InInst, /*Create=*/true);
  bool Modified = false;
  if (InLB.getNumElements() == LB.getNumElements())
    Modified = InLB.orBits(LB);
  else if (InLB.getNumElements() > LB.getNumElements()) {
    assert((InLB.getNumElements() % LB.getNumElements()) == 0);
    int Scale = InLB.getNumElements() / LB.getNumElements();
    // Input element is smaller than result element.
    for (unsigned Idx = 0, End = LB.getNumElements(); Idx != End; ++Idx)
      if (LB.get(Idx))
        Modified |= InLB.setRange(Idx * Scale, Scale);
  } else {
    assert((LB.getNumElements() % InLB.getNumElements()) == 0);
    int Scale = LB.getNumElements() / InLB.getNumElements();
    // Input element is bigger than result element.
    for (unsigned Idx = 0, End = InLB.getNumElements(); Idx != End; ++Idx) {
      bool IsSet = false;
      for (unsigned Idx2 = 0; Idx2 != Scale; ++Idx2)
        IsSet |= LB.get(Idx*Scale | Idx2);
      if (IsSet)
        Modified |= InLB.set(Idx);
    }
  }
  if (Modified)
    addToWorkList(InInst);
}

/***********************************************************************
 * processElementwise : process an element-wise instruction such as add or
 *      a phi node
 */
void GenXDeadVectorRemoval::processElementwise(Instruction *Inst, LiveBits LB)
{
  for (unsigned oi = 0, oe = Inst->getNumOperands(); oi != oe; ++oi) {
    auto OpndInst = dyn_cast<Instruction>(Inst->getOperand(oi));
    if (!OpndInst)
      continue;
    auto OpndLB = getLiveBits(OpndInst, /*Create=*/true);
    if (OpndLB.orBits(LB))
      addToWorkList(OpndInst);
  }
}

/***********************************************************************
 * markWhollyLive : mark a value as wholly live (all elements live)
 */
void GenXDeadVectorRemoval::markWhollyLive(Value *V)
{
  auto Inst = dyn_cast_or_null<Instruction>(V);
  if (!Inst)
    return;
  auto LB = getLiveBits(Inst, /*Create=*/true);
  if (LB.setRange(0, LB.getNumElements()))
    addToWorkList(Inst);
}

/***********************************************************************
 * addToWorkList : add instruction to work list if not already there
 *
 * Enter:   Inst = the instruction
 *
 * This does not actually add to the work list in the initial scan through
 * the whole code.
 */
void GenXDeadVectorRemoval::addToWorkList(Instruction *Inst)
{
  DEBUG(dbgs() << "    " << Inst->getName() << " now " << getLiveBits(Inst) << "\n");
  if (WorkListSet.insert(Inst).second && WorkListPhase) {
    DEBUG(dbgs() << "    adding " << Inst->getName() << " to work list\n");
    WorkList.push(Inst);
  }
}

/***********************************************************************
 * getLiveBits : get the bitmap of live elements for the given instruction
 *
 * Return:  LiveBits object, which contains a pointer to the bitmap for
 *          this instruction, and a size which is set to 0 if there is no
 *          bitmap allocated yet for this instruction and Create is false
 */
LiveBits GenXDeadVectorRemoval::getLiveBits(Instruction *Inst, bool Create)
{
  unsigned NumElements = 1;
  if (auto VT = dyn_cast<VectorType>(Inst->getType()))
    NumElements = VT->getNumElements();
  LiveBitsStorage *LBS = nullptr;
  if (!Create) {
    auto i = InstMap.find(Inst);
    if (i == InstMap.end())
      return LiveBits();
    LBS = &i->second;
  } else {
    auto Ret = InstMap.insert(std::map<Instruction *,
          LiveBitsStorage>::value_type(Inst, LiveBitsStorage()));
    LBS = &Ret.first->second;
    if (Ret.second) {
      // New entry. Set its number of elements.
      LBS->setNumElements(NumElements);
    }
  }
  return LiveBits(LBS, NumElements);
}

/***********************************************************************
 * LiveBits::isAllZero : return true if all bits zero
 */
bool LiveBits::isAllZero() const
{
  for (unsigned Idx = 0, End = (NumElements + BitsPerWord - 1) / BitsPerWord;
      Idx != End; ++Idx)
    if (P[Idx])
      return false;
  return true;
}

/***********************************************************************
 * LiveBits::set : set (or clear) bit
 *
 * Enter:   Idx = element number
 *          Val = true to set, false to clear, default true
 *
 * Return:  true if the bitmap changed
 */
bool LiveBits::set(unsigned Idx, bool Val)
{
  assert(Idx < NumElements);
  uintptr_t *Ptr = P + Idx / BitsPerWord;
  uintptr_t Bit = 1ULL << (Idx % BitsPerWord);
  uintptr_t Entry = *Ptr;
  if (Val)
    Entry |= Bit;
  else
    Entry &= ~Bit;
  bool Ret = Entry != *Ptr;
  *Ptr = Entry;
  return Ret;
}

/***********************************************************************
 * LiveBits::copy : copy all bits from another LiveBits
 */
bool LiveBits::copy(LiveBits Src)
{
  assert(NumElements == Src.NumElements);
  bool Modified = false;
  for (unsigned Idx = 0, End = (NumElements + BitsPerWord - 1) / BitsPerWord;
      Idx != End; ++Idx) {
    Modified |= P[Idx] != Src.P[Idx];
    P[Idx] = Src.P[Idx];
  }
  return Modified;
}

/***********************************************************************
 * LiveBits::orBits : or all bits from another LiveBits into this one
 */
bool LiveBits::orBits(LiveBits Src)
{
  assert(NumElements == Src.NumElements);
  bool Modified = false;
  for (unsigned Idx = 0, End = (NumElements + BitsPerWord - 1) / BitsPerWord;
      Idx != End; ++Idx) {
    uintptr_t Word = P[Idx] | Src.P[Idx];
    Modified |= P[Idx] != Word;
    P[Idx] = Word;
  }
  return Modified;
}

/***********************************************************************
 * LiveBits::setRange : set range of bits, returning true if any changed
 */
bool LiveBits::setRange(unsigned Start, unsigned Len)
{
  bool Modified = false;
  unsigned End = Start + Len;
  assert(End <= NumElements);
  while (Start != End) {
    unsigned ThisLen = BitsPerWord - (Start & (BitsPerWord - 1));
    if (ThisLen > End - Start)
      ThisLen = End - Start;
    uintptr_t *Entry = P + (Start / BitsPerWord);
    uintptr_t Updated = *Entry
          | ((uintptr_t)-1LL >> (BitsPerWord - ThisLen))
              << (Start & (BitsPerWord - 1));
    if (Updated != *Entry) {
      Modified = true;
      *Entry = Updated;
    }
    Start += ThisLen;
  }
  return Modified;
}

/***********************************************************************
 * LiveBits::print : debug print
 */
void LiveBits::print(raw_ostream &OS) const
{
  for (unsigned Idx = 0, End = getNumElements(); Idx != End; ++Idx)
    OS << get(Idx);
}

