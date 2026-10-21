add_subdirectory(compiler_passes)

add_llvm_pass_plugin(LLVMHIPOnSYCLNativeCPUPasses
  NativeCPUPassPlugin.cpp
  PipelineSYCLNativeCPU.cpp
  PrepareSYCLNativeCPU.cpp
  RenameKernelSYCLNativeCPU.cpp
  ConvertToMuxBuiltinsSYCLNativeCPU.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../SYCLLowerIR/SpecConstants.cpp

  LINK_LIBS
  LLVM
  NativeCPUPipeline
  NativeCPUVecz

  INSTALL_WITH_TOOLCHAIN
)

target_compile_definitions(LLVMHIPOnSYCLNativeCPUPasses
  PRIVATE NATIVECPU_USE_OCK)

target_include_directories(LLVMHIPOnSYCLNativeCPUPasses PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/compiler_passes/compiler_pipeline/include
  ${CMAKE_CURRENT_SOURCE_DIR}/compiler_passes/vecz/include
)
