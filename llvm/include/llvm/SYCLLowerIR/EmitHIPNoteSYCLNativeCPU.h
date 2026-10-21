//===- EmitHIPNoteSYCLNativeCPU.h - Emit the HIP device note -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A transformation pass that emits the AMDGPU HSA metadata note that the HIP
// runtime reads out of a device image.
//
// On an AMDGPU target that note is produced by the AMDGPU backend from the
// MachineFunction, so it never exists in IR. The device image for the HIP CPU
// device is built by the host backend instead, and the runtime that consumes
// the image still looks for the same note. This pass therefore builds the note
// in IR, from the kernel signature that is still intact, and hands it to the
// host backend as a section-carried global.
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class EmitHIPNoteSYCLNativeCPUPass
    : public OptionalPassInfoMixin<EmitHIPNoteSYCLNativeCPUPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm
