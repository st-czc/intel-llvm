# HIP on SYCL Native CPU passes

An out-of-tree LLVM pass plugin that lowers device code for the HIP CPU
offload path on top of the SYCL Native CPU backend. The linked device bitcode
is lowered to host code and compiled into the CPU device image.

The plugin is consumed by `opt` through `-load-pass-plugin`. It must be built
against the same LLVM it runs in.

## Build

Requires an LLVM build with `LLVM_ENABLE_PLUGINS=ON`.

### Standalone

```sh
cmake -B build -DLLVM_DIR=<llvm-install>/lib/cmake/llvm
cmake --build build
cmake --install build --prefix <install-prefix>
```

The plugin is installed to the LLVM library directory under the install
prefix; the module is `LLVMHIPOnSYCLNativeCPUPasses.so`.

### As an LLVM external project

```sh
cmake -S <llvm-src> -B build \
    -DLLVM_EXTERNAL_PROJECTS=NativeCPUPasses \
    -DLLVM_EXTERNAL_NATIVECPUPASSES_SOURCE_DIR=<path-to-this-repo>
```

In this mode the plugin is built and installed as part of LLVM.

## Bitcode

The plugin installs `libsycl-nativecpu_utils.bc` to `spvcpu/bitcode` under the
install prefix. The file is downloaded from
[`hipcpu-artifacts`](https://github.com/st-czc/hipcpu-artifacts) at configure
time, pinned by `NATIVECPU_BITCODE_TAG` in `CMakeLists.txt` (an intel/llvm tag).

- `NATIVECPU_BITCODE_DOWNLOAD` (`ON` by default) — set to `OFF` to skip both the
  download and the install.
- `NATIVECPU_BITCODE_FILE` (empty by default) — path to a local bitcode file;
  takes precedence over the download.

## Use

The plugin runs on device IR or bitcode produced for the SYCL Native CPU
backend. It only does work when the `sycl-native-cpu` pipeline is requested by
name.

### opt

The pipeline name is `sycl-native-cpu`; `sycl-native-cpu<O0|O1|O2|O3>` selects
the optimization level, defaulting to `O2`. The pipeline contains only the
backend passes, so the default optimization pipelines are composed explicitly:

```sh
opt -load-pass-plugin=<path>/LLVMHIPOnSYCLNativeCPUPasses.so \
    -passes='default<O2>,sycl-native-cpu<O2>,default<O2>' \
    input.bc -o output.bc
```

If `input.bc` is already optimized, the leading `default<O2>` can be omitted.

## Pipeline

The `sycl-native-cpu` pipeline runs, in order:

1. `EmitHIPNoteSYCLNativeCPUPass` — emits the AMDGPU HSA metadata note the HIP
   runtime reads from the device image (kernel names, argument layout, segment
   sizes). It runs first, while the kernel name, signature and group segment
   are still in the form the runtime expects.
2. `SpecConstantsPass` — emulates specialization constants.
3. `ConvertToMuxBuiltinsSYCLNativeCPUPass` — rewrites device builtins.
4. oneAPI Construction Kit passes: barrier preparation, kernel metadata
   transfer, vectorization, work-item loops, module-scope variable
   replacement, inlining.
5. `PrepareSYCLNativeCPUPass` — kernel calling conventions and attributes.
6. `DefineMuxBuiltinsPass` — defines the mux builtin implementations.
7. `RenameKernelSYCLNativeCPUPass` — exports one subhandler per kernel and
   reproduces the kernel under its original name.

The effective sequence is a default optimization pipeline, the backend
pipeline above, and another default optimization pipeline. The caller supplies
both default pipelines around the named `sycl-native-cpu` pipeline.

## Layout

- `llvm/include/llvm/SYCLLowerIR/` — pass declarations.
- `llvm/lib/SYCLNativeCPUUtils/` — plugin entry, pipeline, and the Native CPU
  passes.
- `llvm/lib/SYCLNativeCPUUtils/compiler_passes/` — the oneAPI Construction Kit
  passes and vectorizer used when `NATIVECPU_USE_OCK` is defined (it is defined
  for the plugin target).
- `llvm/lib/Transforms/Utils/UnifyFunctionExitNodes.cpp` — vendored copy used
  by the vecz pass pipeline.

## Origin

This project is derived from `intel/llvm` at tag `nightly-2026-10-16`. The
retained files remain under the Apache License v2.0 with LLVM Exceptions; see
`llvm/LICENSE.TXT`.
