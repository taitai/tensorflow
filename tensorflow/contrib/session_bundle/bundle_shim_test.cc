/* Copyright 2016 The TensorFlow Authors. All Rights Reserved.

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

#include "tensorflow/contrib/session_bundle/bundle_shim.h"

#include "tensorflow/contrib/session_bundle/bundle_shim_constants.h"
#include "tensorflow/contrib/session_bundle/test_util.h"
#include "tensorflow/core/framework/tensor_testutil.h"
#include "tensorflow/core/lib/core/status_test_util.h"
#include "tensorflow/core/protobuf/meta_graph.pb.h"

namespace tensorflow {
namespace serving {
namespace internal {
namespace {

constexpr char kSessionBundlePath[] =
    "session_bundle/example/half_plus_two/00000123";
constexpr char kSessionBundleMetaGraphFilename[] = "export.meta";
constexpr char kSessionBundleVariablesFilename[] = "export-00000-of-00001";

void ValidateHalfPlusTwo(const SavedModelBundle& saved_model_bundle,
                         const string& input_tensor_name,
                         const string& output_tensor_name) {
  // Validate the half plus two behavior.
  Tensor input = test::AsTensor<float>({0, 1, 2, 3}, TensorShape({4, 1}));
  std::vector<Tensor> outputs;
  TF_ASSERT_OK(saved_model_bundle.session->Run(
      {{input_tensor_name, input}}, {output_tensor_name}, {}, &outputs));
  ASSERT_EQ(outputs.size(), 1);
  test::ExpectTensorEqual<float>(
      outputs[0], test::AsTensor<float>({2, 2.5, 3, 3.5}, TensorShape({4, 1})));
}

// Checks that the input map in a signature def is populated correctly.
TEST(BundleShimTest, AddInputToSignatureDef) {
  SignatureDef signature_def;
  const string tensor_name = "foo_tensor";
  const string map_key = "foo_key";
  AddInputToSignatureDef(tensor_name, map_key, &signature_def);
  EXPECT_EQ(1, signature_def.inputs_size());
  EXPECT_EQ(tensor_name, signature_def.inputs().find(map_key)->second.name());
}

// Checks that the output map in a signature def is populated correctly.
TEST(BundleShimTest, AddOutputToSignatureDef) {
  SignatureDef signature_def;
  const string tensor_name = "foo_tensor";
  const string map_key = "foo_key";
  AddOutputToSignatureDef(tensor_name, map_key, &signature_def);
  EXPECT_EQ(1, signature_def.outputs_size());
  EXPECT_EQ(tensor_name, signature_def.outputs().find(map_key)->second.name());
}

// Checks that no signature defs are added if the default signature is missing.
TEST(BundleShimTest, DefaultSignatureMissing) {
  MetaGraphDef meta_graph_def;
  Signatures signatures;
  ConvertDefaultSignatureToSignatureDef(signatures, &meta_graph_def);
  EXPECT_EQ(0, meta_graph_def.signature_def_size());
}

// Checks that no signature defs are added if the default signature is empty.
TEST(BundleShimTest, DefaultSignatureEmpty) {
  Signatures signatures;
  signatures.mutable_default_signature();

  MetaGraphDef meta_graph_def;
  (*meta_graph_def.mutable_collection_def())[kSignaturesKey]
      .mutable_any_list()
      ->add_value()
      ->PackFrom(signatures);
  ConvertDefaultSignatureToSignatureDef(signatures, &meta_graph_def);
  EXPECT_EQ(0, meta_graph_def.signature_def_size());
}

// Checks the conversion to signature def for a regression default signature.
TEST(BundleShimTest, DefaultSignatureRegression) {
  Signatures signatures;
  RegressionSignature* regression_signature =
      signatures.mutable_default_signature()->mutable_regression_signature();
  regression_signature->mutable_input()->set_tensor_name("foo-input");
  regression_signature->mutable_output()->set_tensor_name("foo-output");
  MetaGraphDef meta_graph_def;
  (*meta_graph_def.mutable_collection_def())[kSignaturesKey]
      .mutable_any_list()
      ->add_value()
      ->PackFrom(signatures);
  ConvertDefaultSignatureToSignatureDef(signatures, &meta_graph_def);
  EXPECT_EQ(1, meta_graph_def.signature_def_size());
  const auto actual_signature_def =
      meta_graph_def.signature_def().find(kDefaultSignatureDefKey);
  EXPECT_EQ("foo-input", actual_signature_def->second.inputs()
                             .find(kSignatureInputs)
                             ->second.name());
  EXPECT_EQ("foo-output", actual_signature_def->second.outputs()
                              .find(kSignatureOutputs)
                              ->second.name());
  EXPECT_EQ(kRegressMethodName, actual_signature_def->second.method_name());
}

// Checks the conversion to signature def for a classification default
// signature.
TEST(BundleShimTest, DefaultSignatureClassification) {
  Signatures signatures;
  ClassificationSignature* classification_signature =
      signatures.mutable_default_signature()
          ->mutable_classification_signature();
  classification_signature->mutable_input()->set_tensor_name("foo-input");
  classification_signature->mutable_classes()->set_tensor_name("foo-classes");
  classification_signature->mutable_scores()->set_tensor_name("foo-scores");
  MetaGraphDef meta_graph_def;
  (*meta_graph_def.mutable_collection_def())[kSignaturesKey]
      .mutable_any_list()
      ->add_value()
      ->PackFrom(signatures);
  ConvertDefaultSignatureToSignatureDef(signatures, &meta_graph_def);
  EXPECT_EQ(1, meta_graph_def.signature_def_size());
  const auto actual_signature_def =
      meta_graph_def.signature_def().find(kDefaultSignatureDefKey);
  EXPECT_EQ("foo-input", actual_signature_def->second.inputs()
                             .find(kSignatureInputs)
                             ->second.name());
  EXPECT_EQ("foo-classes", actual_signature_def->second.outputs()
                               .find(kClassifyOutputClasses)
                               ->second.name());
  EXPECT_EQ("foo-scores", actual_signature_def->second.outputs()
                              .find(kClassifyOutputScores)
                              ->second.name());
  EXPECT_EQ(kClassifyMethodName, actual_signature_def->second.method_name());
}

// Checks that generic default signatures are not up converted.
TEST(BundleShimTest, DefaultSignatureGeneric) {
  TensorBinding input_binding;
  input_binding.set_tensor_name("foo-input");

  TensorBinding output_binding;
  output_binding.set_tensor_name("foo-output");

  Signatures signatures;
  GenericSignature* generic_signature =
      signatures.mutable_default_signature()->mutable_generic_signature();
  generic_signature->mutable_map()->insert({kSignatureInputs, input_binding});
  generic_signature->mutable_map()->insert({kSignatureOutputs, output_binding});

  MetaGraphDef meta_graph_def;
  (*meta_graph_def.mutable_collection_def())[kSignaturesKey]
      .mutable_any_list()
      ->add_value()
      ->PackFrom(signatures);
  EXPECT_EQ(0, meta_graph_def.signature_def_size());
}

// Checks that a named signature of type other than generic is not up converted.
TEST(BundleShimTest, NamedSignatureWrongType) {
  Signatures signatures;

  RegressionSignature* inputs_regression_signature =
      (*signatures.mutable_named_signatures())[kSignatureInputs]
          .mutable_regression_signature();
  inputs_regression_signature->mutable_input()->set_tensor_name("foo-input");

  RegressionSignature* outputs_regression_signature =
      (*signatures.mutable_named_signatures())[kSignatureOutputs]
          .mutable_regression_signature();
  outputs_regression_signature->mutable_output()->set_tensor_name("foo-output");

  MetaGraphDef meta_graph_def;
  (*meta_graph_def.mutable_collection_def())[kSignaturesKey]
      .mutable_any_list()
      ->add_value()
      ->PackFrom(signatures);
  ConvertNamedSignaturesToSignatureDef(signatures, &meta_graph_def);
  EXPECT_EQ(0, meta_graph_def.signature_def_size());
}

// Checks the signature def created when the named signatures have `inputs` and
// `outputs`.
TEST(BundleShimTest, NamedSignatureGenericInputsAndOutputs) {
  TensorBinding input_binding;
  input_binding.set_tensor_name("foo-input");

  TensorBinding output_binding;
  output_binding.set_tensor_name("foo-output");

  Signatures signatures;
  GenericSignature* input_generic_signature =
      (*signatures.mutable_named_signatures())[kSignatureInputs]
          .mutable_generic_signature();
  input_generic_signature->mutable_map()->insert({"foo-input", input_binding});

  GenericSignature* output_generic_signature =
      (*signatures.mutable_named_signatures())[kSignatureOutputs]
          .mutable_generic_signature();
  output_generic_signature->mutable_map()->insert(
      {"foo-output", output_binding});

  MetaGraphDef meta_graph_def;
  (*meta_graph_def.mutable_collection_def())[kSignaturesKey]
      .mutable_any_list()
      ->add_value()
      ->PackFrom(signatures);
  ConvertNamedSignaturesToSignatureDef(signatures, &meta_graph_def);
  EXPECT_EQ(1, meta_graph_def.signature_def_size());
  const auto actual_signature_def =
      meta_graph_def.signature_def().find(kDefaultSignatureDefKey);
  EXPECT_EQ(
      "foo-input",
      actual_signature_def->second.inputs().find("foo-input")->second.name());
  EXPECT_EQ(
      "foo-output",
      actual_signature_def->second.outputs().find("foo-output")->second.name());
  EXPECT_EQ(kPredictMethodName, actual_signature_def->second.method_name());
}

// Checks that a signature def is only added if the named signatures have
// `inputs` and `outputs`.
TEST(BundleShimTest, NamedSignatureGenericNoInputsOrOutputs) {
  TensorBinding input_binding;
  input_binding.set_tensor_name("foo-input");

  TensorBinding output_binding;
  output_binding.set_tensor_name("foo-output");

  Signatures signatures;
  GenericSignature* generic_signature =
      (*signatures.mutable_named_signatures())["unknown"]
          .mutable_generic_signature();
  generic_signature->mutable_map()->insert({kSignatureInputs, input_binding});
  generic_signature->mutable_map()->insert({kSignatureOutputs, output_binding});

  MetaGraphDef meta_graph_def;
  (*meta_graph_def.mutable_collection_def())[kSignaturesKey]
      .mutable_any_list()
      ->add_value()
      ->PackFrom(signatures);
  ConvertNamedSignaturesToSignatureDef(signatures, &meta_graph_def);
  EXPECT_EQ(0, meta_graph_def.signature_def_size());
}

// Checks that a signature def is not added when the named signatures have only
// one of `inputs` and `outputs`.
TEST(BundleShimTest, NamedSignatureGenericOnlyInput) {
  TensorBinding input_binding;
  input_binding.set_tensor_name("foo-input");

  Signatures signatures;
  GenericSignature* input_generic_signature =
      (*signatures.mutable_named_signatures())[kSignatureInputs]
          .mutable_generic_signature();
  input_generic_signature->mutable_map()->insert({"foo-input", input_binding});

  MetaGraphDef meta_graph_def;
  (*meta_graph_def.mutable_collection_def())[kSignaturesKey]
      .mutable_any_list()
      ->add_value()
      ->PackFrom(signatures);
  ConvertNamedSignaturesToSignatureDef(signatures, &meta_graph_def);
  EXPECT_EQ(0, meta_graph_def.signature_def_size());
}

// Checks a basic up conversion for half plus two.
TEST(BundleShimTest, BasicExport) {
  const string session_bundle_export_dir =
      test_util::TestSrcDirPath(kSessionBundlePath);
  SessionOptions session_options;
  RunOptions run_options;
  SavedModelBundle saved_model_bundle;
  TF_ASSERT_OK(LoadSavedModelFromLegacySessionBundlePath(
      session_options, run_options, session_bundle_export_dir,
      &saved_model_bundle));
  const MetaGraphDef meta_graph_def = saved_model_bundle.meta_graph_def;
  const auto& signature_def_map = meta_graph_def.signature_def();
  EXPECT_EQ(1, signature_def_map.size());

  const auto& regression_entry =
      signature_def_map.find(kDefaultSignatureDefKey);
  SignatureDef regression_signature_def = regression_entry->second;
  EXPECT_EQ(1, regression_signature_def.inputs_size());
  TensorInfo input_tensor_info =
      regression_signature_def.inputs().find(kSignatureInputs)->second;
  EXPECT_EQ(1, regression_signature_def.outputs_size());
  TensorInfo output_tensor_info =
      regression_signature_def.outputs().find(kSignatureOutputs)->second;
  ValidateHalfPlusTwo(saved_model_bundle, input_tensor_info.name(),
                      output_tensor_info.name());
}

}  // namespace
}  // namespace internal
}  // namespace serving
}  // namespace tensorflow
