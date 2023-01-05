/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "llvm/Transforms/Utils/Cloning.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"  // from @llvm-project
#include "mlir/Target/NVVMIR.h"  // from @llvm-project
#include "mlir/Target/ROCDLIR.h"  // from @llvm-project
#include "mlir/Transforms/DialectConversion.h"  // from @llvm-project
#include "tensorflow/compiler/mlir/hlo/include/mlir-hlo/Dialect/mhlo/IR/hlo_ops.h"
#include "tensorflow/compiler/mlir/tools/kernel_gen/transforms/passes.h"
#include "tensorflow/compiler/xla/debug_options_flags.h"
#include "tensorflow/compiler/xla/service/gpu/llvm_gpu_backend/gpu_backend_lib.h"
#include "tensorflow/compiler/xla/service/gpu/stream_executor_util.h"
#include "tensorflow/compiler/xla/service/gpu/target_constants.h"
#include "tensorflow/compiler/xla/service/hlo_module_config.h"
#include "tensorflow/compiler/xla/status.h"
#include "tensorflow/compiler/xla/statusor.h"
#include "tensorflow/core/platform/cuda_libdevice_path.h"
#include "tensorflow/core/platform/logging.h"
#include "tensorflow/core/platform/path.h"

#if GOOGLE_CUDA
#include "tensorflow/stream_executor/gpu/asm_compiler.h"
#elif TENSORFLOW_USE_ROCM
#include "tensorflow/core/platform/rocm_rocdl_path.h"
#endif

namespace mlir {
namespace kernel_gen {
namespace transforms {
namespace {

#define GEN_PASS_CLASSES
#include "tensorflow/compiler/mlir/tools/kernel_gen/transforms/kernel_gen_passes.h.inc"

using xla::InternalError;

class GpuKernelToBlobPass
    : public GpuKernelToBlobPassBase<GpuKernelToBlobPass> {
 public:
  GpuKernelToBlobPass(mlir::StringRef blob_annotation,
                      llvm::ArrayRef<std::string> architectures,
                      bool generate_fatbin) {
    blob_annotation_ = blob_annotation.str();
    architectures_ = architectures;
    generate_fatbin_ = generate_fatbin;
  }

  void runOnOperation() override {
    mlir::gpu::GPUModuleOp gpu_module = getOperation();
    auto blob_or = GetGpuBinaryBlob(gpu_module);
    if (blob_or.ok()) {
      const auto& blob = blob_or.ValueOrDie();
      std::string blob_string(blob.begin(), blob.end());
      gpu_module.setAttr(blob_annotation_,
                         mlir::StringAttr::get(blob_string, &getContext()));
      return;
    }
    return signalPassFailure();
  }

  xla::StatusOr<std::vector<uint8_t>> GetGpuBinaryBlob(
      mlir::gpu::GPUModuleOp gpu_module) {
    if (architectures_.empty()) {
      return InternalError("Expected at least one GPU architecture.");
    }
    if (!generate_fatbin_ && architectures_.size() > 1) {
      return InternalError(
          "Can only generate machine code for more than one architecture as a "
          "fatbin.");
    }

    llvm::LLVMContext llvmContext;

#if TENSORFLOW_USE_ROCM
    auto llvmModule = mlir::translateModuleToROCDLIR(gpu_module, llvmContext);
    if (!llvmModule) {
      return InternalError("Could not translate MLIR module to ROCDL IR");
    }

    llvmModule->setModuleIdentifier("acme");

    xla::HloModuleConfig config;
    config.set_debug_options(xla::GetDebugOptionsFromFlags());

    // TODO(b/169066682): Support fatbin on ROCm.
    if (generate_fatbin_) {
      return InternalError("Fatbins are not yet supported for ROCm.");
    }

    // Parse ROCm architecture.
    absl::string_view consumable_arch(architectures_.front());
    if (!absl::ConsumePrefix(&consumable_arch, "gfx")) {
      return InternalError(
          "Could not parse ROCm architecture prefix (expected gfx)");
    }
    uint32_t arch;
    if (!absl::SimpleAtoi(consumable_arch, &arch)) {
      return InternalError("Could not parse ROCm architecture number");
    }

    std::string libdevice_dir = tensorflow::RocdlRoot();
    return xla::gpu::amdgpu::CompileToHsaco(llvmModule.get(), arch, config,
                                            libdevice_dir);

#elif GOOGLE_CUDA
    auto llvmModule = mlir::translateModuleToNVVMIR(gpu_module, llvmContext);
    if (!llvmModule) {
      return InternalError("Could not translate MLIR module to NVVM");
    }

    llvmModule->setModuleIdentifier("acme");
    llvmModule->setDataLayout(xla::gpu::nvptx::kDataLayout);

    xla::HloModuleConfig config;
    config.set_debug_options(xla::GetDebugOptionsFromFlags());

    auto enable_fusion = [](llvm::TargetMachine* target) {
      target->Options.AllowFPOpFusion = llvm::FPOpFusion::FPOpFusionMode::Fast;
    };

    // Compile and collect requested cubin and PTX images.
    std::vector<tensorflow::se::CubinOrPTXImage> images;
    TF_ASSIGN_OR_RETURN(std::string libdevice_dir, GetLibdeviceDir(config));
    auto gpu_asm_opts = xla::gpu::PtxOptsFromConfig(config);
    for (const std::string& arch_str : architectures_) {
      // Parse CUDA architecture.
      absl::string_view consumable_arch(arch_str);
      bool is_compute_profile;
      if (absl::ConsumePrefix(&consumable_arch, "compute_")) {
        is_compute_profile = true;
      } else if (absl::ConsumePrefix(&consumable_arch, "sm_")) {
        is_compute_profile = false;
      } else {
        return InternalError(
            "Could not parse cuda architecture prefix (expected sm_ or "
            "compute_)");
      }
      uint32_t arch;
      if (!absl::SimpleAtoi(consumable_arch, &arch)) {
        return InternalError("Could not parse cuda architecture number");
      }

      uint32_t cc_major = arch / 10;
      uint32_t cc_minor = arch % 10;
      // Module may be changed by CompileToPtx.
      auto llvm_module_copy = llvm::CloneModule(*llvmModule);
      TF_ASSIGN_OR_RETURN(
          std::string ptx,
          xla::gpu::nvptx::CompileToPtx(llvm_module_copy.get(),
                                        std::make_pair(cc_major, cc_minor),
                                        config, libdevice_dir, enable_fusion));
      VLOG(1) << ptx;
      TF_ASSIGN_OR_RETURN(std::vector<uint8_t> gpu_asm,
                          tensorflow::se::CompileGpuAsm(
                              cc_major, cc_minor, ptx.c_str(), gpu_asm_opts));

      if (!generate_fatbin_) {
        // Skip fatbin generation and return the first and only GPU machine
        // code. This is currently only used for `tf_to_gpu_binary` and will
        // eventually disappear.
        return gpu_asm;
      }

      // Collect cubin (and ptx image if requested).
      images.push_back({absl::StrCat("sm_", arch), std::move(gpu_asm)});
      if (is_compute_profile) {
        std::vector<uint8_t> ptx_bytes;
        std::copy(ptx.begin(), ptx.end(), std::back_inserter(ptx_bytes));
        images.push_back(
            {absl::StrCat("compute_", arch), std::move(ptx_bytes)});
      }
    }

    // TODO(b/169870789): Revisit the use of fatbins.
    // Bundle cubin and PTX images into a single fatbin.
    return tensorflow::se::BundleGpuAsm(images,
                                        gpu_asm_opts.preferred_cuda_dir);
#endif

    return InternalError(
        "Neither TENSORFLOW_USE_ROCM nor GOOGLE_CUDA are defined."
        " Did you specify either --config=rocm or --config=cuda ?");
  }

 private:
  xla::StatusOr<std::string> GetLibdeviceDir(
      const xla::HloModuleConfig& hlo_module_config) {
    for (const std::string& cuda_root : tensorflow::CandidateCudaRoots(
             hlo_module_config.debug_options().xla_gpu_cuda_data_dir())) {
      std::string libdevice_dir =
          tensorflow::io::JoinPath(cuda_root, "nvvm", "libdevice");
      VLOG(2) << "Looking for libdevice at " << libdevice_dir;
      if (tensorflow::Env::Default()->IsDirectory(libdevice_dir).ok()) {
        VLOG(2) << "Found libdevice dir " << libdevice_dir;
        return libdevice_dir;
      }
    }
    return InternalError(
        "Can't find libdevice directory ${CUDA_DIR}/nvvm/libdevice");
  }
};

}  // namespace

std::unique_ptr<OperationPass<gpu::GPUModuleOp>> CreateGpuKernelToBlobPass(
    mlir::StringRef blob_annotation, ArrayRef<std::string> architectures,
    bool generate_fatbin) {
  return std::make_unique<GpuKernelToBlobPass>(blob_annotation, architectures,
                                               generate_fatbin);
}

}  // namespace transforms
}  // namespace kernel_gen
}  // namespace mlir
