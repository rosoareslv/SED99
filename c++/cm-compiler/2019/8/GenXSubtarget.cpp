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
// This file implements the GenX specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "GenXSubtarget.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

#define DEBUG_TYPE "subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#define GET_SUBTARGETINFO_MC_DESC
#include "GenXGenSubtargetInfo.inc"

void GenXSubtarget::resetSubtargetFeatures(StringRef CPU, StringRef FS) {

  DumpRegAlloc = false;
  SvmptrIs64Bit = false;
  HasLongLong = false;
  DisableJmpi = false;
  DisableVectorDecomposition = false;
  WarnCallable = false;

  GenXVariant = llvm::StringSwitch<GenXTag>(CPU)
    .Case("HSW", GENX_HSW)
    .Case("BDW", GENX_BDW)
    .Case("CHV", GENX_CHV)
    .Case("SKL", GENX_SKL)
    .Case("BXT", GENX_BXT)
    .Case("KBL", GENX_KBL)
    .Case("GLK", GENX_GLK)
    .Case("CNL", GENX_CNL)
    .Case("ICL", GENX_ICL)
    .Case("ICLLP", GENX_ICLLP)
    .Default(GENX_SKL);

  std::string CPUName = CPU;
  if (CPUName.empty())
    CPUName = "generic";

  ParseSubtargetFeatures(CPUName, FS);
}

GenXSubtarget::GenXSubtarget(const Triple &TT, const std::string &CPU,
                             const std::string &FS)
    : GenXGenSubtargetInfo(TT, CPU, FS), TargetTriple(TT) {

  resetSubtargetFeatures(CPU, FS);
}

StringRef GenXSubtarget::getEmulateFunction(const Instruction *Inst) const {
  StringRef EmuFnName;
  return EmuFnName;
}

GenXSubtargetPass::GenXSubtargetPass() : ImmutablePass(ID), ST(nullptr) {}
GenXSubtargetPass::GenXSubtargetPass(const GenXSubtarget &ST)
    : ImmutablePass(ID), ST(&ST) {}
GenXSubtargetPass::~GenXSubtargetPass() {}

char GenXSubtargetPass::ID = 0;

namespace llvm {

void initializeGenXSubtargetPassPass(PassRegistry &);

ImmutablePass *createGenXSubtargetPass(const GenXSubtarget &ST) {
  initializeGenXSubtargetPassPass(*PassRegistry::getPassRegistry());
  return new GenXSubtargetPass(ST);
}

} // namespace llvm

INITIALIZE_PASS_BEGIN(GenXSubtargetPass, "GenXSubtargetPass", "GenXSubtargetPass", false, true)
INITIALIZE_PASS_END(GenXSubtargetPass, "GenXSubtargetPass", "GenXSubtargetPass", false, true)
