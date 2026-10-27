//===-- FrexpF64Devectorize.cpp - Devectorize frexp on doubles ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The X86 backend cannot select vector forms of llvm.frexp over doubles
// (llvm.frexp.v2f64.v2i32 and wider): the widened multi-result aggregate made
// by the SLP vectorizer crashes the type legalizer and instruction selection.
//
// This pass rewrites those calls into one scalar llvm.frexp per lane and
// reassembles the two result vectors. It runs at the OptimizerLast extension
// point, after SLP has run, so the plugin needs to be loaded into clang with
// -fpass-plugin.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

namespace {

// Only fixed-width vectors are rewritten. Scalable vectors have no lane count
// known at compile time, so they cannot be split lane by lane and are left to
// the backend.
bool isFixedVectorOfDouble(Value *V) {
  auto *VecTy = dyn_cast<VectorType>(V->getType());
  return VecTy && !VecTy->isScalableTy() &&
         VecTy->getElementType()->isDoubleTy();
}

// Replaces one vector llvm.frexp call with per-lane scalar calls.
void devectorize(CallInst *CI) {
  auto *VecTy = cast<FixedVectorType>(CI->getArgOperand(0)->getType());
  const unsigned NumLanes = VecTy->getNumElements();
  Type *ElemTy = VecTy->getElementType();
  Type *IntTy = Type::getInt32Ty(CI->getContext());

  Function *Scalar = Intrinsic::getOrInsertDeclaration(
      CI->getModule(), Intrinsic::frexp, {ElemTy, IntTy});

  IRBuilder<> Builder(CI);
  ElementCount Width = ElementCount::getFixed(NumLanes);
  Value *Mantissa = PoisonValue::get(VectorType::get(ElemTy, Width));
  Value *Exponent = PoisonValue::get(VectorType::get(IntTy, Width));
  for (unsigned Lane = 0; Lane < NumLanes; ++Lane) {
    Value *Input = Builder.CreateExtractElement(CI->getArgOperand(0), Lane);
    CallInst *Result = Builder.CreateCall(Scalar, Input);
    Result->copyIRFlags(CI);
    Result->copyMetadata(*CI);
    Mantissa = Builder.CreateInsertElement(
        Mantissa, Builder.CreateExtractValue(Result, 0), Lane);
    Exponent = Builder.CreateInsertElement(
        Exponent, Builder.CreateExtractValue(Result, 1), Lane);
  }

  Value *Aggregate = PoisonValue::get(CI->getType());
  Aggregate = Builder.CreateInsertValue(Aggregate, Mantissa, 0);
  Aggregate = Builder.CreateInsertValue(Aggregate, Exponent, 1);
  Aggregate->setName(CI->getName());

  CI->replaceAllUsesWith(Aggregate);
  CI->eraseFromParent();
}

class FrexpF64DevectorizePass
    : public OptionalPassInfoMixin<FrexpF64DevectorizePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    SmallVector<CallInst *, 4> VectorFrexpCalls;
    for (BasicBlock &BB : F)
      for (Instruction &I : BB)
        if (auto *CI = dyn_cast<CallInst>(&I))
          if (CI->getCalledFunction() &&
              CI->getCalledFunction()->getIntrinsicID() == Intrinsic::frexp &&
              isFixedVectorOfDouble(CI->getArgOperand(0)))
            VectorFrexpCalls.push_back(CI);

    if (VectorFrexpCalls.empty())
      return PreservedAnalyses::all();

    for (CallInst *CI : VectorFrexpCalls)
      devectorize(CI);

    return PreservedAnalyses::none();
  }
};

} // namespace

void registerFrexpF64Devectorize(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, FunctionPassManager &FPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != "frexp-devectorize")
          return false;
        FPM.addPass(FrexpF64DevectorizePass());
        return true;
      });

  PB.registerOptimizerLastEPCallback([](ModulePassManager &MPM,
                                        OptimizationLevel, ThinOrFullLTOPhase) {
    MPM.addPass(createModuleToFunctionPassAdaptor(FrexpF64DevectorizePass()));
  });
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "HIPCPUFixups", LLVM_VERSION_STRING,
          registerFrexpF64Devectorize};
}
