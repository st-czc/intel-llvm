//===- MakeDeviceGlobalsWritableSYCLNativeCPU.h - writable device globals -==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Drops the constant attribute from __constant__ and __device__ const globals so
// that they are placed in a writable section of the device image. The host
// writes these variables through the symbol APIs, and the native CPU device
// shares their storage with the host instead of copying through a command
// processor.
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class MakeDeviceGlobalsWritableSYCLNativeCPUPass
    : public OptionalPassInfoMixin<MakeDeviceGlobalsWritableSYCLNativeCPUPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm
