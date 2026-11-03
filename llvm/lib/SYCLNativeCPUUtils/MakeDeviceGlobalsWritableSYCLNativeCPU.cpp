//===- MakeDeviceGlobalsWritableSYCLNativeCPU.cpp - writable device globals -==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// HIP places __constant__ and __device__ const in the constant address space,
// and the frontend marks them constant in the IR. Constant globals are emitted
// to a read-only section, so the host cannot write them. On a GPU the host
// writes through the command processor and the memory stays read-only; the
// native CPU device shares the storage with the host, so it has to be writable.
//
// Only the constant attribute is dropped. The address space and the linkage are
// left alone, and the initializer is preserved because the frontend emits these
// globals as externally initialized.
//
//===----------------------------------------------------------------------===//

#include "llvm/SYCLLowerIR/MakeDeviceGlobalsWritableSYCLNativeCPU.h"

#include "llvm/IR/GlobalVariable.h"

using namespace llvm;

PreservedAnalyses MakeDeviceGlobalsWritableSYCLNativeCPUPass::run(
    Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  for (GlobalVariable &GV : M.globals()) {
    // Address space 4 is the constant address space. Internal globals are not
    // reachable from the host, which registers symbols by external name.
    if (GV.getAddressSpace() != 4 || !GV.isConstant() || GV.isDeclaration() ||
        GV.hasInternalLinkage())
      continue;
    GV.setConstant(false);
    Changed = true;
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
