//===- AggregateExternSharedSYCLNativeCPU.cpp - extern __shared__ aggregate
//===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// In HIP every extern __shared__ declaration in a kernel aliases the same
// dynamically sized local memory region, but the frontend emits each one as a
// separate external, zero-sized array in address space 3. The SYCL Native CPU
// local memory lowering would otherwise give each its own struct slot. This
// pass collapses them into a single representative variable whose alignment is
// the maximum of all of them, mirroring AMDGPU's AMDGPULowerModuleLDSPass
// (buildRepresentativeDynamicLDSInstance).
//===----------------------------------------------------------------------===//

#include "llvm/SYCLLowerIR/AggregateExternSharedSYCLNativeCPU.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Alignment.h"

#include <algorithm>

using namespace llvm;

PreservedAnalyses
AggregateExternSharedSYCLNativeCPUPass::run(Module &M,
                                            ModuleAnalysisManager &) {
  SmallVector<GlobalVariable *, 4> ExternShared;
  // 8-byte floor keeps scalar accesses aligned for any declared type.
  Align MaxAlign(8);

  for (GlobalVariable &GV : M.globals()) {
    if (GV.getAddressSpace() != 3 || !GV.isDeclaration())
      continue;
    if (GV.use_empty())
      continue;
    ExternShared.push_back(&GV);
    MaxAlign = std::max(MaxAlign, GV.getAlign().valueOrOne());
  }

  // Nothing to aggregate when the kernel does not use extern __shared__.
  if (ExternShared.size() == 0)
    return PreservedAnalyses::all();

  auto *EmptyCharArray = ArrayType::get(Type::getInt8Ty(M.getContext()), 0);
  auto *Representative = new GlobalVariable(
      M, EmptyCharArray, /*isConstant=*/false, GlobalValue::ExternalLinkage,
      /*Initializer=*/nullptr, "llvm.hipcpu.dynshared",
      /*InsertBefore=*/nullptr, GlobalValue::NotThreadLocal, /*AddressSpace=*/3,
      /*isExternallyInitialized=*/false);
  Representative->setAlignment(MaxAlign);
  Representative->setVisibility(ExternShared.front()->getVisibility());

  for (GlobalVariable *GV : ExternShared) {
    GV->replaceAllUsesWith(Representative);
    GV->eraseFromParent();
  }

  return PreservedAnalyses::none();
}
