//===-- LowerAMDGPUBuiltinsSYCLNativeCPU.cpp - Lower AMDGPU builtins ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Lowers AMDGPU builtins that the SPVCPU target borrows, such as
// llvm.amdgcn.s.sleep, to host instructions for the SYCL Native CPU backend.
//
//===----------------------------------------------------------------------===//

#include "llvm/SYCLLowerIR/LowerAMDGPUBuiltinsSYCLNativeCPU.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsRISCV.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/Support/Casting.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace {

// Return the host's pause hint, or Intrinsic::not_intrinsic.
static Intrinsic::ID getHostPauseIntrinsic(const Triple &T) {
  switch (T.getArch()) {
  case Triple::x86:
  case Triple::x86_64:
    return Intrinsic::x86_sse2_pause;
  case Triple::riscv32:
  case Triple::riscv64:
    return Intrinsic::riscv_pause;
  default:
    return Intrinsic::not_intrinsic;
  }
}

// Lower a llvm.amdgcn.s.sleep call to the host's pause hint.
static void lowerSSleep(Module &M, CallInst *CI) {
  Intrinsic::ID PauseID = getHostPauseIntrinsic(Triple(M.getTargetTriple()));
  if (PauseID != Intrinsic::not_intrinsic) {
    // The argument encodes "pause n times". We do not emulate it. s_sleep is
    // only a hint.
    Function *Pause = Intrinsic::getOrInsertDeclaration(&M, PauseID);
    IRBuilder<> Builder(CI);
    Builder.CreateCall(Pause);
  }
  CI->eraseFromParent();
}

} // namespace

PreservedAnalyses
LowerAMDGPUBuiltinsSYCLNativeCPUPass::run(Module &M, ModuleAnalysisManager &) {
  SmallVector<CallInst *, 4> SleepCalls;
  for (Function &F : M)
    for (BasicBlock &BB : F)
      for (Instruction &I : BB)
        if (auto *CI = dyn_cast<CallInst>(&I))
          if (CI->getIntrinsicID() == Intrinsic::amdgcn_s_sleep)
            SleepCalls.push_back(CI);

  if (SleepCalls.empty())
    return PreservedAnalyses::all();

  for (CallInst *CI : SleepCalls)
    lowerSSleep(M, CI);

  return PreservedAnalyses::none();
}
