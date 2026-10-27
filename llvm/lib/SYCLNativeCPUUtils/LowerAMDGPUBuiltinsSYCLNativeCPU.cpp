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

// Return the LLVM intrinsic that computes the same value as the given AMDGPU
// intrinsic, or Intrinsic::not_intrinsic.
static Intrinsic::ID getEquivalentIntrinsic(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::amdgcn_sqrt:
    return Intrinsic::sqrt;
  case Intrinsic::amdgcn_exp2:
    return Intrinsic::exp2;
  case Intrinsic::amdgcn_log:
    return Intrinsic::log2;
  case Intrinsic::amdgcn_sin:
    return Intrinsic::sin;
  case Intrinsic::amdgcn_cos:
    return Intrinsic::cos;
  default:
    return Intrinsic::not_intrinsic;
  }
}

// Return true if the AMDGPU intrinsic is a reciprocal of the given argument.
// The reciprocal itself is emitted as an fdiv by lowerReciprocal.
static bool isReciprocal(Intrinsic::ID ID) {
  return ID == Intrinsic::amdgcn_rcp || ID == Intrinsic::amdgcn_rsq;
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

// Lower a llvm.amdgcn.rcp or llvm.amdgcn.rsq call to a division.
static void lowerReciprocal(Module &M, CallInst *CI, Intrinsic::ID ID) {
  Value *Arg = CI->getArgOperand(0);
  // rcp(x) is 1/x and rsq(x) is 1/sqrt(x), the same forms that the SYCL Native
  // CPU device library uses for rsqrt.
  Value *Denominator = Arg;
  if (ID == Intrinsic::amdgcn_rsq) {
    Function *Sqrt = Intrinsic::getOrInsertDeclaration(&M, Intrinsic::sqrt,
                                                       {Arg->getType()});
    IRBuilder<> Builder(CI);
    CallInst *NewCI = Builder.CreateCall(Sqrt, Arg);
    NewCI->copyIRFlags(CI);
    NewCI->copyMetadata(*CI);
    Denominator = NewCI;
  }
  IRBuilder<> Builder(CI);
  Instruction *Result = cast<Instruction>(Builder.CreateFDiv(
      ConstantFP::get(Arg->getType(), 1.0), Denominator, CI->getName()));
  Result->copyIRFlags(CI);
  Result->setHasAllowReciprocal(true);
  Result->copyMetadata(*CI);
  CI->replaceAllUsesWith(Result);
  CI->eraseFromParent();
}

// Lower a call to an AMDGPU intrinsic that has an LLVM equivalent.
static void lowerEquivalent(Module &M, CallInst *CI, Intrinsic::ID ID) {
  Value *Arg = CI->getArgOperand(0);
  Function *Fn = Intrinsic::getOrInsertDeclaration(&M, ID, {Arg->getType()});
  IRBuilder<> Builder(CI);
  CallInst *NewCI = Builder.CreateCall(Fn, Arg);
  NewCI->copyIRFlags(CI);
  NewCI->copyMetadata(*CI);
  CI->replaceAllUsesWith(NewCI);
  CI->eraseFromParent();
}

} // namespace

PreservedAnalyses
LowerAMDGPUBuiltinsSYCLNativeCPUPass::run(Module &M, ModuleAnalysisManager &) {
  SmallVector<CallInst *, 4> SleepCalls;
  SmallVector<std::pair<CallInst *, Intrinsic::ID>, 4> EquivalentCalls;
  SmallVector<std::pair<CallInst *, Intrinsic::ID>, 4> ReciprocalCalls;
  for (Function &F : M)
    for (BasicBlock &BB : F)
      for (Instruction &I : BB)
        if (auto *CI = dyn_cast<CallInst>(&I)) {
          const Intrinsic::ID ID = CI->getIntrinsicID();
          if (ID == Intrinsic::amdgcn_s_sleep)
            SleepCalls.push_back(CI);
          else if (isReciprocal(ID))
            ReciprocalCalls.push_back({CI, ID});
          else if (const Intrinsic::ID Equivalent = getEquivalentIntrinsic(ID);
                   Equivalent != Intrinsic::not_intrinsic)
            EquivalentCalls.push_back({CI, Equivalent});
        }

  if (SleepCalls.empty() && EquivalentCalls.empty() && ReciprocalCalls.empty())
    return PreservedAnalyses::all();

  for (CallInst *CI : SleepCalls)
    lowerSSleep(M, CI);

  for (auto &[CI, ID] : EquivalentCalls)
    lowerEquivalent(M, CI, ID);

  for (auto &[CI, ID] : ReciprocalCalls)
    lowerReciprocal(M, CI, ID);

  return PreservedAnalyses::none();
}
