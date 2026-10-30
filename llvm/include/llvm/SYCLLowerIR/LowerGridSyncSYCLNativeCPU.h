//===- LowerGridSyncSYCLNativeCPU.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A transformation pass that lowers __ockl_grid_sync calls to a work-group
// barrier, a call into the runtime, and a second work-group barrier.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class LowerGridSyncSYCLNativeCPUPass
    : public OptionalPassInfoMixin<LowerGridSyncSYCLNativeCPUPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm
