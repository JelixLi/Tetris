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

#include <string>

#include "llvm/Support/raw_ostream.h"
#include "mlir/IR/Module.h"  // from @llvm-project
#include "mlir/InitAllPasses.h"  // from @llvm-project
#include "mlir/Parser.h"  // from @llvm-project
#include "mlir/Pass/PassManager.h"  // from @llvm-project
#include "mlir/Pass/PassRegistry.h"  // from @llvm-project
#include "tensorflow/c/tf_status.h"
#include "tensorflow/c/tf_status_helper.h"
#include "tensorflow/compiler/mlir/tensorflow/dialect_registration.h"
#include "tensorflow/compiler/mlir/tensorflow/transforms/passes.h"
#include "tensorflow/compiler/mlir/tensorflow/transforms/tf_saved_model_passes.h"
#include "tensorflow/compiler/mlir/tensorflow/translate/import_model.h"
#include "tensorflow/compiler/mlir/tensorflow/utils/error_util.h"
#include "tensorflow/compiler/mlir/tensorflow/utils/import_utils.h"
#include "tensorflow/core/framework/function.h"
#include "tensorflow/core/framework/function.pb.h"
#include "tensorflow/core/framework/op.h"

namespace tensorflow {

namespace {

// Runs pass pipeline `pass_pipeline` on `module` if `pass_pipeline` is not
// empty.
std::string RunPassPipelineOnModule(mlir::ModuleOp module,
                                    const std::string &pass_pipeline,
                                    TF_Status *status) {
  if (!pass_pipeline.empty()) {
    mlir::PassManager pm(module.getContext());
    std::string error;
    llvm::raw_string_ostream error_stream(error);
    if (failed(mlir::parsePassPipeline(pass_pipeline, pm, error_stream))) {
      TF_SetStatus(status, TF_INVALID_ARGUMENT,
                   ("Invalid pass_pipeline: " + error_stream.str()).c_str());
      return "// error";
    }

    mlir::StatusScopedDiagnosticHandler statusHandler(module.getContext());
    if (failed(pm.run(module))) {
      Set_TF_Status_from_Status(status, statusHandler.ConsumeStatus());
      return "// error";
    }
  }
  return MlirModuleToString(module);
}

}  // anonymous namespace

std::string ImportGraphDef(const std::string &proto,
                           const std::string &pass_pipeline,
                           TF_Status *status) {
  GraphDef graphdef;
  auto s = tensorflow::LoadProtoFromBuffer(proto, &graphdef);
  if (!s.ok()) {
    Set_TF_Status_from_Status(status, s);
    return "// error";
  }
  GraphDebugInfo debug_info;
  GraphImportConfig specs;
  mlir::MLIRContext context;
  auto module = ConvertGraphdefToMlir(graphdef, debug_info, specs, &context);
  if (!module.ok()) {
    Set_TF_Status_from_Status(status, module.status());
    return "// error";
  }

  return RunPassPipelineOnModule(module->get(), pass_pipeline, status);
}

std::string ImportFunction(const std::string &functiondef_proto,
                           const std::string &functiondef_library_proto,
                           const std::string &pass_pipeline,
                           TF_Status *status) {
  FunctionDef functiondef;
  auto s = tensorflow::LoadProtoFromBuffer(functiondef_proto, &functiondef);
  if (!s.ok()) {
    Set_TF_Status_from_Status(status, s);
    return "// error";
  }

  FunctionDefLibrary fdef_lib;
  s = tensorflow::LoadProtoFromBuffer(functiondef_library_proto, &fdef_lib);
  if (!s.ok()) {
    Set_TF_Status_from_Status(status, s);
    return "// error";
  }

  FunctionLibraryDefinition flib_def(OpRegistry::Global(), fdef_lib);
  s = flib_def.AddFunctionDef(functiondef);
  if (!s.ok()) {
    Set_TF_Status_from_Status(status, s);
    return "// error";
  }

  const std::string &function_name = functiondef.signature().name();
  mlir::MLIRContext context;
  auto module = ConvertFunctionToMlir(function_name, flib_def, &context);
  if (!module.ok()) {
    Set_TF_Status_from_Status(status, module.status());
    return "// error";
  }

  return RunPassPipelineOnModule(module->get(), pass_pipeline, status);
}

std::string ExperimentalConvertSavedModelToMlir(
    const std::string &saved_model_path, const std::string &exported_names_str,
    bool show_debug_info, TF_Status *status) {
  // Load the saved model into a SavedModelV2Bundle.

  tensorflow::SavedModelV2Bundle bundle;
  auto load_status =
      tensorflow::SavedModelV2Bundle::Load(saved_model_path, &bundle);
  if (!load_status.ok()) {
    Set_TF_Status_from_Status(status, load_status);
    return "// error";
  }

  // Convert the SavedModelV2Bundle to an MLIR module.

  std::vector<string> exported_names =
      absl::StrSplit(exported_names_str, ',', absl::SkipEmpty());
  mlir::MLIRContext context;
  auto module_or = ConvertSavedModelToMlir(
      &bundle, &context, absl::Span<std::string>(exported_names));
  if (!module_or.status().ok()) {
    Set_TF_Status_from_Status(status, module_or.status());
    return "// error";
  }

  return MlirModuleToString(*module_or.ConsumeValueOrDie(), show_debug_info);
}

std::string ExperimentalConvertSavedModelV1ToMlir(
    const std::string &saved_model_path, const std::string &tags,
    bool lift_variables, bool upgrade_legacy, bool show_debug_info,
    TF_Status *status) {
  // Load the saved model into a SavedModelBundle.

  std::unordered_set<string> tag_set =
      absl::StrSplit(tags, ',', absl::SkipEmpty());

  tensorflow::SavedModelBundle bundle;
  auto load_status =
      tensorflow::LoadSavedModel({}, {}, saved_model_path, tag_set, &bundle);
  if (!load_status.ok()) {
    Set_TF_Status_from_Status(status, load_status);
    return "// error";
  }

  // Convert the SavedModelBundle to an MLIR module.

  mlir::MLIRContext context;
  auto module_or =
      ConvertSavedModelV1ToMlir(bundle, {}, &context, upgrade_legacy);
  if (!module_or.status().ok()) {
    Set_TF_Status_from_Status(status, module_or.status());
    return "// error";
  }

  // Run the tf standard pipeline by default and then, run passes that lift
  // variables if the flag is set on the module.
  mlir::OwningModuleRef module = module_or.ConsumeValueOrDie();
  mlir::PassManager pm(&context);
  std::string error;
  llvm::raw_string_ostream error_stream(error);

  mlir::TF::StandardPipelineOptions tf_options;
  mlir::TF::CreateTFStandardPipeline(pm, tf_options);
  if (lift_variables) {
    pm.addPass(mlir::TF::CreatePromoteVarHandlesToArgsPass());
    pm.addPass(
        mlir::tf_saved_model::CreateLiftVariablesPass(bundle.GetSession()));
  }

  mlir::StatusScopedDiagnosticHandler diagnostic_handler(&context);
  if (failed(pm.run(*module))) {
    Set_TF_Status_from_Status(status, diagnostic_handler.ConsumeStatus());
    return "// error";
  }
  return MlirModuleToString(*module, show_debug_info);
}

std::string ExperimentalRunPassPipeline(const std::string &mlir_txt,
                                        const std::string &pass_pipeline,
                                        bool show_debug_info,
                                        TF_Status *status) {
  mlir::MLIRContext context;
  mlir::RegisterAllTensorFlowDialects(context.getDialectRegistry());
  mlir::OwningModuleRef module;
  {
    mlir::StatusScopedDiagnosticHandler diagnostic_handler(&context);
    module = mlir::parseSourceString(mlir_txt, &context);
    if (!module) {
      Set_TF_Status_from_Status(status, diagnostic_handler.ConsumeStatus());
      return "// error";
    }
  }

  // Run the pass_pipeline on the module.
  mlir::PassManager pm(&context);
  std::string error;
  llvm::raw_string_ostream error_stream(error);
  mlir::registerAllPasses();
  if (failed(mlir::parsePassPipeline(pass_pipeline, pm, error_stream))) {
    TF_SetStatus(status, TF_INVALID_ARGUMENT,
                 ("Invalid pass_pipeline: " + error_stream.str()).c_str());
    return "// error";
  }

  mlir::StatusScopedDiagnosticHandler diagnostic_handler(&context);
  if (failed(pm.run(*module))) {
    Set_TF_Status_from_Status(status, diagnostic_handler.ConsumeStatus());
    return "// error";
  }
  return MlirModuleToString(*module, show_debug_info);
}

}  // namespace tensorflow
