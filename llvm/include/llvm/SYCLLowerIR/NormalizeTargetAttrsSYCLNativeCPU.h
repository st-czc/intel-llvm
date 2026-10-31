//===----- NormalizeTargetAttrsSYCLNativeCPU.h - Normalize target attrs ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Removes the per-function target attributes left by the device compilation
// and by the prebuilt support bitcode, so that the code generated for the host
// follows the target options given to opt and clang.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class NormalizeTargetAttrsSYCLNativeCPUPass
    : public OptionalPassInfoMixin<NormalizeTargetAttrsSYCLNativeCPUPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm
