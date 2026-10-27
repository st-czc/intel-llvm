add_llvm_pass_plugin(LLVMHIPCPUFixups
  FrexpF64Devectorize.cpp

  LINK_LIBS
  LLVM

  INSTALL_WITH_TOOLCHAIN
)
