//===--- NativeCPUPassPlugin.cpp - pass plugin entry for SYCL Native CPU --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Exposes the SYCL Native CPU backend pass pipeline as an LLVM pass plugin.
//
// The pipeline is requested by name under opt and contains only the backend
// passes; the caller composes it with the default optimization pipelines.
//
//===----------------------------------------------------------------------===//

#include "llvm/SYCLLowerIR/NormalizeTargetAttrsSYCLNativeCPU.h"
#include "llvm/SYCLLowerIR/UtilsSYCLNativeCPU.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/ErrorHandling.h"
#include <optional>

using namespace llvm;

namespace {

constexpr StringRef PipelineName = "sycl-native-cpu";
constexpr StringRef NormalizePipelineName = "hipcpu-normalize";

std::optional<OptimizationLevel> parseOptLevel(StringRef Param) {
  if (Param == "O0")
    return OptimizationLevel::O0;
  if (Param == "O1")
    return OptimizationLevel::O1;
  if (Param == "O2")
    return OptimizationLevel::O2;
  if (Param == "O3")
    return OptimizationLevel::O3;
  return std::nullopt;
}

// Matches "sycl-native-cpu" or "sycl-native-cpu<Olevel>". Returns the requested
// level, defaulting to O2 when no parameter is given.
std::optional<OptimizationLevel> matchPipelineName(StringRef Name) {
  if (Name == PipelineName)
    return OptimizationLevel::O2;
  if (!Name.consume_front(PipelineName) || !Name.consume_front("<") ||
      !Name.consume_back(">"))
    return std::nullopt;
  return parseOptLevel(Name);
}

// addSYCLNativeCPUBackendPasses() registers its own analyses into the module
// analysis manager, which the pipeline parsing callback does not receive.
// PassBuilder runs analysis registration callbacks before building any
// pipeline, so the manager captured here is live by the time the parsing
// callback runs.
thread_local ModuleAnalysisManager *TheMAM = nullptr;

void addBackendPasses(ModulePassManager &MPM, OptimizationLevel Level) {
  if (!TheMAM)
    report_fatal_error("sycl-native-cpu: module analysis manager was not "
                       "registered before pipeline construction",
                       /*gen_crash_diag=*/false);

  sycl::utils::addSYCLNativeCPUBackendPasses(MPM, *TheMAM, Level);
}

void registerCallbacks(PassBuilder &PB) {
  PB.registerAnalysisRegistrationCallback(
      [](ModuleAnalysisManager &MAM) { TheMAM = &MAM; });

  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        auto Level = matchPipelineName(Name);
        if (!Level)
          return false;
        addBackendPasses(MPM, *Level);
        return true;
      });

  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name != NormalizePipelineName)
          return false;
        MPM.addPass(NormalizeTargetAttrsSYCLNativeCPUPass());
        return true;
      });
}

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "HIPOnSYCLNativeCPUPasses",
          LLVM_VERSION_STRING, registerCallbacks};
}
