//===- AggregateExternSharedSYCLNativeCPU.h - extern __shared__ aggregate -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Aggregates the extern __shared__ globals of a HIP kernel into a single
// representative variable, so the local memory lowering sees one aliased
// dynamic region instead of one slot per declaration.
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class AggregateExternSharedSYCLNativeCPUPass
    : public OptionalPassInfoMixin<AggregateExternSharedSYCLNativeCPUPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm
