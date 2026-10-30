//===- LowerGridSyncSYCLNativeCPU.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/SYCLLowerIR/LowerGridSyncSYCLNativeCPU.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

namespace {

constexpr char GridSyncName[] = "__ockl_grid_sync";
constexpr char WorkGroupBarrierName[] = "__mux_work_group_barrier";
constexpr char GetLocalIdName[] = "__mux_get_local_id";
constexpr char GridSyncHandleName[] = "runtime_grid_sync_handle";

} // namespace

PreservedAnalyses
LowerGridSyncSYCLNativeCPUPass::run(Module &M, ModuleAnalysisManager &MAM) {
  SmallVector<CallInst *, 4> Calls;
  for (Function &F : M)
    if (F.getName() == GridSyncName)
      for (User *U : F.users())
        if (auto *Call = dyn_cast<CallInst>(U))
          Calls.push_back(Call);
  if (Calls.empty())
    return PreservedAnalyses::all();

  LLVMContext &Ctx = M.getContext();
  Type *I32 = Type::getInt32Ty(Ctx);
  Type *I64 = Type::getInt64Ty(Ctx);
  Type *Void = Type::getVoidTy(Ctx);
  PointerType *Ptr = PointerType::getUnqual(Ctx);

  FunctionCallee Barrier = M.getOrInsertFunction(
      WorkGroupBarrierName, FunctionType::get(Void, {I32, I32, I32}, false));
  FunctionCallee GetLocalId = M.getOrInsertFunction(
      GetLocalIdName, FunctionType::get(I64, {I32}, false));

  auto *Handle =
      new GlobalVariable(M, Ptr, false, GlobalValue::ExternalLinkage,
                         ConstantPointerNull::get(Ptr), GridSyncHandleName);
  appendToUsed(M, Handle);

  for (CallInst *Call : Calls) {
    BasicBlock *Head = Call->getParent();

    IRBuilder<> Builder(Call);
    Builder.CreateCall(Barrier,
                       {Builder.getInt32(0), Builder.getInt32(2), Builder.getInt32(528)});
    Value *X = Builder.CreateCall(GetLocalId, {Builder.getInt32(0)});
    Value *Y = Builder.CreateCall(GetLocalId, {Builder.getInt32(1)});
    Value *Z = Builder.CreateCall(GetLocalId, {Builder.getInt32(2)});
    Value *IsRepresentative =
        Builder.CreateICmpEQ(Builder.CreateOr(Builder.CreateOr(X, Y), Z),
                             Builder.getInt64(0));

    BasicBlock *Cont = Head->splitBasicBlock(Call->getIterator(), "grid.sync.cont");
    BasicBlock *Rep =
        BasicBlock::Create(Ctx, "grid.sync.rep", Head->getParent(), Cont);

    Instruction *OldTerm = Head->getTerminator();
    IRBuilder<> HeadBuilder(OldTerm);
    HeadBuilder.CreateCondBr(IsRepresentative, Rep, Cont);
    OldTerm->eraseFromParent();

    IRBuilder<> RepBuilder(Rep);
    Value *H = RepBuilder.CreateLoad(Ptr, Handle);
    RepBuilder.CreateCall(FunctionType::get(Void, false), H);
    RepBuilder.CreateBr(Cont);

    IRBuilder<> ContBuilder(Call);
    ContBuilder.CreateCall(Barrier,
                           {ContBuilder.getInt32(0), ContBuilder.getInt32(2),
                            ContBuilder.getInt32(528)});
    Call->eraseFromParent();
  }

  return PreservedAnalyses::none();
}
