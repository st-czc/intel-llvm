//===-- EmitHIPNoteSYCLNativeCPU.cpp - Emit the HIP device note -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emits the AMDGPU HSA metadata note that the HIP runtime reads out of a device
// image. See EmitHIPNoteSYCLNativeCPU.h for why this cannot be left to the
// target backend.
//
// The note is an ELF note: a header, the owner string and the description. The
// description is the msgpack document that llvm would otherwise produce from a
// MachineFunction in AMDGPUHSAMetadataStreamer.cpp. The field layout below
// follows that streamer; each place where the host backend cannot supply what
// the AMDGPU backend would have is marked with a TODO.
//===----------------------------------------------------------------------===//

#include "llvm/SYCLLowerIR/EmitHIPNoteSYCLNativeCPU.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MsgPackDocument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/AMDGPUMetadata.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

namespace {

// The note owner and type the runtime looks for. These match
// AMDGPU::ElfNote::NoteNameV3 and ELF::NT_AMDGPU_METADATA, and the code object
// version matches VersionMajorV5/VersionMinorV5, i.e. the version the HIP CPU
// device is prepared to read.
constexpr char NoteOwner[] = "AMDGPU";
constexpr uint32_t NoteType = ELF::NT_AMDGPU_METADATA;

// The owner string is emitted NUL-terminated and the note body is padded to a
// 4-byte boundary, as ELF requires for both the name and the description.
constexpr unsigned NoteNameSize = sizeof(NoteOwner); // includes the NUL
constexpr unsigned NoteAlign = 4;

// The HIP CPU device does not run AMDGPU kernels, so the AMDGPU-specific
// per-kernel code properties have no value here. The runtime reads them into
// its work group info but does not act on them for this device, so the values
// below only need to be well-formed.
//
// TODO: these are placeholders, not measurements. Replace them with something
// derived from the CPU kernel, or drop them once the runtime stops reading
// them.
constexpr uint32_t PlaceholderSGPRCount = 0;
constexpr uint32_t PlaceholderVGPRCount = 0;
constexpr uint32_t PlaceholderWavefrontSize = 1;

// The hidden arguments the AMDGPU ABI appends after the explicit ones. Their
// offsets, sizes and value kinds are the ones AMDGPUHSAMetadataStreamer emits
// for a V5 code object, so the produced note is shaped like the AMDGPU one.
//
// TODO: these describe arguments a CPU kernel does not have and never receives.
// They are emitted because the runtime accounts for them when it builds the
// kernel signature. Revisit once the CPU device stops needing them.
struct HiddenArg {
  uint32_t Size;
  uint32_t Align;
  const char *ValueKind;
};

constexpr HiddenArg HiddenArgsHead[] = {
    {4, 4, "hidden_block_count_x"}, {4, 4, "hidden_block_count_y"},
    {4, 4, "hidden_block_count_z"}, {2, 2, "hidden_group_size_x"},
    {2, 2, "hidden_group_size_y"},  {2, 2, "hidden_group_size_z"},
    {2, 2, "hidden_remainder_x"},   {2, 2, "hidden_remainder_y"},
    {2, 2, "hidden_remainder_z"},
};

constexpr HiddenArg HiddenArgsTail[] = {
    {8, 8, "hidden_global_offset_x"},
    {8, 8, "hidden_global_offset_y"},
    {8, 8, "hidden_global_offset_z"},
    {2, 2, "hidden_grid_dims"},
};

// The offset of the implicit argument pointer, which is the alignment the
// hidden block starts at.
constexpr uint32_t ImplicitArgPtrAlign = 8;

// AMDGPUHSAMetadataStreamer leaves a gap between the two groups of hidden
// arguments that no emitted argument covers: 8 bytes for the tool correlation
// id and 8 more that are reserved. Skip it so the offsets line up with what the
// AMDGPU backend produces.
constexpr uint32_t HiddenArgsReservedGap = 16;

// Under code object V5 the implicit argument block is 256 bytes, the size the
// AMDGPU ABI reserves for the hidden arguments, the dispatch pointer and the
// reserved tails. The CPU device has no such block, but the runtime sizes the
// kernel argument buffer from it.
//
// TODO: this is a placeholder, see HiddenArgs above.
constexpr uint32_t ImplicitArgNumBytes = 256;

StringRef getValueKind(Type *Ty) {
  if (isa<PointerType>(Ty))
    return "global_buffer";
  return "by_value";
}

StringRef getAddressSpaceQualifier(Type *Ty) {
  // A device image for this flow only ever carries pointers into the global
  // segment: the local segment lives in address space 3 and never reaches a
  // kernel parameter.
  if (isa<PointerType>(Ty))
    return "global";
  return "";
}

Align getArgumentAlign(const Argument &Arg, const DataLayout &DL) {
  Type *Ty = Arg.getType();
  MaybeAlign ArgAlign;
  if (Arg.hasByRefAttr()) {
    Ty = Arg.getParamByRefType();
    ArgAlign = Arg.getParamAlign();
  }
  if (!ArgAlign)
    ArgAlign = DL.getABITypeAlign(Ty);
  return *ArgAlign;
}

Type *getArgumentType(const Argument &Arg) {
  if (Arg.hasByRefAttr())
    return Arg.getParamByRefType();
  return Arg.getType();
}

// Emits one entry of the ".args" array. Mirrors
// MetadataStreamerMsgPackV4::emitKernelArg, minus the fields that come from
// frontend metadata this flow does not produce.
void emitKernelArg(msgpack::Document &Doc, msgpack::ArrayDocNode Args,
                   const DataLayout &DL, Type *Ty, Align Alignment,
                   StringRef ValueKind, uint64_t &Offset) {
  msgpack::MapDocNode Arg = Doc.getMapNode();

  uint64_t Size = DL.getTypeAllocSize(Ty);
  Offset = alignTo(Offset, Alignment);
  Arg[".offset"] = Doc.getNode(Offset);
  Arg[".size"] = Doc.getNode(Size);
  Offset += Size;

  Arg[".value_kind"] = Doc.getNode(ValueKind, /*Copy=*/true);
  if (StringRef Qualifier = getAddressSpaceQualifier(Ty); !Qualifier.empty())
    Arg[".address_space"] = Doc.getNode(Qualifier, /*Copy=*/true);

  Args.push_back(Arg);
}

// The size of the group segment is the sum of the module scope variables in
// the local address space, which are the "__shared__" objects.
uint64_t getGroupSegmentFixedSize(const Module &M) {
  const DataLayout &DL = M.getDataLayout();
  uint64_t Size = 0;
  for (const GlobalVariable &GV : M.globals()) {
    if (GV.getAddressSpace() != 3 || GV.isDeclaration())
      continue;
    Size += DL.getTypeAllocSize(GV.getValueType());
  }
  return Size;
}

std::string getTargetID(const Module &M) {
  // The note carries the identifier of the device the image was built for. The
  // image here is built for the host, so the identifier names the host rather
  // than an accelerator.
  //
  // TODO: this is not an AMDGPU target string, so a consumer that parses the
  // target id and expects an "amdgcn" triple will not find one. It is not read
  // on this path today.
  return M.getTargetTriple().str();
}

// Mirrors AMDGPUSubtarget::getKernArgSegmentSize: the explicit arguments, the
// padding before the implicit pointer, and the implicit block. Under AMDHSA
// the explicit arguments start at offset 0.
uint64_t getKernargSegmentSize(const Function &F, const DataLayout &DL,
                               Align &MaxAlign) {
  uint64_t ExplicitSize = 0;
  MaxAlign = Align(1);
  for (const Argument &Arg : F.args()) {
    if (Arg.hasAttribute("amdgpu-hidden-argument"))
      continue;
    Align ArgAlign = getArgumentAlign(Arg, DL);
    MaxAlign = std::max(MaxAlign, ArgAlign);
    ExplicitSize = alignTo(ExplicitSize, ArgAlign) +
                   DL.getTypeAllocSize(getArgumentType(Arg));
  }

  const Align ImplicitAlign(8);
  uint64_t Size = alignTo(ExplicitSize, ImplicitAlign) + ImplicitArgNumBytes;
  MaxAlign = std::max(MaxAlign, ImplicitAlign);

  return alignTo(Size, 4);
}

// Returns the maximum flat work group size from __launch_bounds__.
// If the attribute is absent, returns the device maximum.
uint32_t getMaxFlatWorkGroupSize(const Function &F) {
  constexpr uint32_t DeviceMaxWorkGroupSize = 1024;
  Attribute Attr = F.getFnAttribute("amdgpu-flat-work-group-size");
  if (!Attr.isStringAttribute())
    return DeviceMaxWorkGroupSize;

  StringRef Max = Attr.getValueAsString().split(',').second;
  uint32_t MaxVal = 0;
  if (Max.getAsInteger(10, MaxVal))
    return DeviceMaxWorkGroupSize;
  return MaxVal;
}

msgpack::MapDocNode emitKernel(const Function &F, LLVMContext &Ctx,
                               msgpack::Document &Doc, const Module &M,
                               StringRef Name) {
  const DataLayout &DL = M.getDataLayout();
  msgpack::MapDocNode Kern = Doc.getMapNode();

  Kern[".name"] = Doc.getNode(Name, /*Copy=*/true);
  // The AMDGPU backend names the kernel descriptor symbol after the kernel.
  // Nothing in this image defines one, but the runtime reads the field.
  Kern[".symbol"] = Doc.getNode(Twine(Name, ".kd").str(), /*Copy=*/true);

  Align MaxAlign;
  Kern[".kernarg_segment_size"] =
      Doc.getNode(getKernargSegmentSize(F, DL, MaxAlign));
  Kern[".kernarg_segment_align"] =
      Doc.getNode(std::max(Align(4), MaxAlign).value());
  Kern[".group_segment_fixed_size"] = Doc.getNode(getGroupSegmentFixedSize(M));

  // TODO: placeholders, see the constants above.
  Kern[".private_segment_fixed_size"] = Doc.getNode(uint64_t(0));
  Kern[".uses_dynamic_stack"] = Doc.getNode(false);
  Kern[".sgpr_count"] = Doc.getNode(PlaceholderSGPRCount);
  Kern[".vgpr_count"] = Doc.getNode(PlaceholderVGPRCount);
  Kern[".wavefront_size"] = Doc.getNode(PlaceholderWavefrontSize);
  Kern[".sgpr_spill_count"] = Doc.getNode(uint32_t(0));
  Kern[".vgpr_spill_count"] = Doc.getNode(uint32_t(0));

  // The maximum flat work group size the kernel was built for.
  Kern[".max_flat_workgroup_size"] = Doc.getNode(getMaxFlatWorkGroupSize(F));
  if (F.hasFnAttribute("uniform-work-group-size")) {
    if (F.getFnAttribute("uniform-work-group-size").getValueAsBool())
      Kern[".uniform_work_group_size"] = Doc.getNode(uint32_t(1));
  }

  uint64_t Offset = 0;
  msgpack::ArrayDocNode Args = Doc.getArrayNode();
  for (const Argument &Arg : F.args()) {
    if (Arg.hasAttribute("amdgpu-hidden-argument"))
      continue;
    emitKernelArg(Doc, Args, DL, getArgumentType(Arg),
                  getArgumentAlign(Arg, DL), getValueKind(getArgumentType(Arg)),
                  Offset);
  }

  Offset = alignTo(Offset, Align(ImplicitArgPtrAlign));
  for (const HiddenArg &H : HiddenArgsHead)
    emitKernelArg(Doc, Args, DL, Type::getIntNTy(Ctx, H.Size * 8),
                  Align(H.Align), H.ValueKind, Offset);

  Offset += HiddenArgsReservedGap;

  for (const HiddenArg &H : HiddenArgsTail)
    emitKernelArg(Doc, Args, DL, Type::getIntNTy(Ctx, H.Size * 8),
                  Align(H.Align), H.ValueKind, Offset);

  Kern[".args"] = Args;
  return Kern;
}

} // namespace

PreservedAnalyses
EmitHIPNoteSYCLNativeCPUPass::run(Module &M, ModuleAnalysisManager &MAM) {
  SmallVector<Function *> Kernels;
  for (Function &F : M) {
    if (F.getCallingConv() == CallingConv::SPIR_KERNEL && !F.isDeclaration())
      Kernels.push_back(&F);
  }
  if (Kernels.empty())
    return PreservedAnalyses::all();

  msgpack::Document Doc;

  msgpack::ArrayDocNode Version = Doc.getArrayNode();
  Version.push_back(Doc.getNode(AMDGPU::HSAMD::VersionMajorV5));
  Version.push_back(Doc.getNode(AMDGPU::HSAMD::VersionMinorV5));
  Doc.getRoot().getMap(/*Convert=*/true)["amdhsa.version"] = Version;
  Doc.getRoot().getMap(/*Convert=*/true)["amdhsa.target"] =
      Doc.getNode(getTargetID(M), /*Copy=*/true);

  msgpack::ArrayDocNode KernelArray = Doc.getArrayNode();
  for (Function *F : Kernels) {
    if (F->hasFnAttribute("sycl-module-id")) {
      // Kernels reach this pass under their original name; the device image
      // exports a subhandler named after the kernel, and the runtime looks the
      // subhandler up from this field. See RenameKernelSYCLNativeCPU, which
      // adds the suffix the runtime appends here.
      KernelArray.push_back(
          emitKernel(*F, M.getContext(), Doc, M, F->getName()));
    }
  }
  if (KernelArray.empty())
    return PreservedAnalyses::all();
  Doc.getRoot().getMap(/*Convert=*/true)["amdhsa.kernels"] = KernelArray;

  std::string Desc;
  Doc.writeToBlob(Desc);

  SmallVector<uint8_t> Note;
  auto Append32 = [&Note](uint32_t V) {
    for (unsigned I = 0; I < 4; ++I)
      Note.push_back(static_cast<uint8_t>(V >> (8 * I)));
  };
  Append32(NoteNameSize);
  Append32(Desc.size());
  Append32(NoteType);
  Note.append(NoteOwner, NoteOwner + NoteNameSize);
  while (Note.size() % NoteAlign != 0)
    Note.push_back(0);
  Note.append(Desc.begin(), Desc.end());
  while (Note.size() % NoteAlign != 0)
    Note.push_back(0);

  LLVMContext &Ctx = M.getContext();
  Constant *Init = ConstantDataArray::get(Ctx, Note);
  auto *NoteGV = new GlobalVariable(M, Init->getType(), /*isConstant=*/true,
                                    GlobalValue::PrivateLinkage, Init,
                                    "amdgpu_metadata_note");
  // A section whose name starts with ".note" is given SHT_NOTE by the object
  // file implementation, independently of the target.
  NoteGV->setSection(".note.AMDGPU");
  NoteGV->setAlignment(Align(NoteAlign));
  appendToCompilerUsed(M, NoteGV);

  return PreservedAnalyses::none();
}
