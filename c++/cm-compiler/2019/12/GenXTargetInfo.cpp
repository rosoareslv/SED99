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

#include "GenXTargetMachine.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheGenXTarget32() {
  static Target TheGenXTarget32;
  return TheGenXTarget32;
}

Target &llvm::getTheGenXTarget64() {
  static Target TheGenXTarget64;
  return TheGenXTarget64;
}

extern "C" void LLVMInitializeGenXTargetInfo() {
  RegisterTarget<Triple::genx32> X(getTheGenXTarget32(), "genx32",
                                   "Intel GenX 32-bit", "genx32");
  RegisterTarget<Triple::genx64> Y(getTheGenXTarget64(), "genx64",
                                   "Intel GenX 64-bit", "genx64");
}

extern "C" void LLVMInitializeGenXTargetMC() {}