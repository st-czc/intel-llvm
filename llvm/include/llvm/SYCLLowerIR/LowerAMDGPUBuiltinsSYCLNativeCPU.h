//===-- LowerAMDGPUBuiltinsSYCLNativeCPU.h - Lower AMDGPU builtins --------===//
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

#pragma once

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class LowerAMDGPUBuiltinsSYCLNativeCPUPass
    : public OptionalPassInfoMixin<LowerAMDGPUBuiltinsSYCLNativeCPUPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm
