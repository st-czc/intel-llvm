//===--- NormalizeTargetAttrsSYCLNativeCPU.cpp - Normalize target attrs ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The device compilation and the prebuilt support bitcode annotate functions
// with the target attributes of the machine they were compiled for, and those
// attributes override the target options of the host compile. Functions of the
// same module then disagree on the subtarget, which changes the calling
// convention of vector arguments at the ABI level. Dropping the attributes
// leaves the host compile as the single source of the target features.
//
//===----------------------------------------------------------------------===//

#include "llvm/SYCLLowerIR/NormalizeTargetAttrsSYCLNativeCPU.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;

PreservedAnalyses
NormalizeTargetAttrsSYCLNativeCPUPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : M)
    for (StringRef Attr : {"target-cpu", "target-features", "tune-cpu"})
      if (F.hasFnAttribute(Attr)) {
        F.removeFnAttr(Attr);
        Changed = true;
      }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
