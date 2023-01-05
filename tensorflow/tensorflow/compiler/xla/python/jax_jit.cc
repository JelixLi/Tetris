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

// This files implements the `jax.jit` dispatch and just-in-time feature.
//
// In a nutshell, `Jit(f)` returns a callable that will dispatch (i.e. forward
// based on passed arguments dtypes/shapes/identity) the execution to a
// just-in-time compiled XLA Executable. All of that is done in C++ for
// performance reasons.
//
// This file contains the utilities to:
// (a) inspect arguments and describe their structure, dtype/shapes, etc.
// (b) keep a mapping from function signatures to compiled XLA Executables.

#include "tensorflow/compiler/xla/python/jax_jit.h"

#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/synchronization/notification.h"
#include "absl/types/optional.h"
#include "pybind11/cast.h"
#include "pybind11/numpy.h"
#include "pybind11/pybind11.h"
#include "pybind11/pytypes.h"
#include "tensorflow/compiler/xla/pjrt/pjrt_client.h"
#include "tensorflow/compiler/xla/python/py_buffer.h"
#include "tensorflow/compiler/xla/python/py_executable.h"
#include "tensorflow/compiler/xla/python/pytree.h"
#include "tensorflow/compiler/xla/python/types.h"
#include "tensorflow/compiler/xla/shape_util.h"
#include "tensorflow/compiler/xla/statusor.h"
#include "tensorflow/compiler/xla/util.h"
#include "tensorflow/compiler/xla/xla_data.pb.h"
#include "tensorflow/core/platform/status.h"

namespace xla {

namespace py = pybind11;

// TODO(phawkins): Add support for Tracers.
// TODO(jblespiau): Use absl Status.

namespace {

thread_local bool disable_jit;
void SetDisableJit(bool disable_jit_) { disable_jit = disable_jit_; }
bool GetDisableJit() { return disable_jit; }

// Describes the abstract shape and dtype of an argument.
struct ArgSignature {
  // This is the XLA dtype of the object.
  xla::PrimitiveType dtype;
  // JAX arguments can be of weak type, if and only if they are Python scalars
  // or `DeviceArray` values such that `aval.weak_type` is true.
  bool weak_type;
  absl::InlinedVector<int64, 4> shape;
  bool operator==(const ArgSignature& other) const {
    return std::tie(dtype, weak_type, shape) ==
           std::tie(other.dtype, other.weak_type, other.shape);
  }
  bool operator!=(const ArgSignature& other) const { return !(*this == other); }

  std::string DebugString() const {
    std::string result = "";
    if (weak_type) {
      absl::StrAppend(&result, "weak_");
    }
    absl::StrAppend(&result, xla::PrimitiveType_Name(dtype));
    absl::StrAppend(&result, "[", absl::StrJoin(shape, ","), "]");
    return result;
  }
};

template <typename H>
H AbslHashValue(H h, const ArgSignature& s) {
  h = H::combine(std::move(h), s.dtype);
  h = H::combine_contiguous(std::move(h), s.shape.data(), s.shape.size());
  return h;
}

// The signature of Python jitted function call, partitioned into:
// - dynamic positional arguments (i.e. positional args which are not static)
// - static positional arguments (i.e. the args associated to static_argnums)
// - keyword arguments
// The CallSignature should unambiguously identify a function call, thus,
// equality is based on:
// (a) Same PyTree for all dynamic positional arguments and keyword arguments
// (a) equality of the arguments and keyword arguments ArgSignature
// (a) equality (delegated to Python) of the static arguments.
struct CallSignature {
  struct KwargEntry {
    // To avoid comparing strings, we intern the kwargs strings.
    // The compilation cache holds a reference to all the keys.
    py::handle key;
    PyTreeDef value_treedef;
    bool operator==(const KwargEntry& other) const {
      return key.ptr() == other.key.ptr() &&
             value_treedef == other.value_treedef;
    }
    bool operator!=(const KwargEntry& other) const { return !(*this == other); }
  };

  // Only contains the arguments associated to `static_argnums`, sorted in the
  // order of their argnum index.
  std::vector<py::object> static_args;
  // A PyTreeDef for each positional dynamic (i.e. not static) argument.
  std::vector<PyTreeDef> dynamic_positional_args_treedef;
  // Keyword arguments. Sorted by the keyword name.
  std::vector<KwargEntry> keyword_args;
  // Shape and dtype for both the dynamic positional arguments and the keyword
  // arguments (sorted by keyword name).
  std::vector<ArgSignature> dynamic_args_signatures;
  PjRtDevice* device;

  bool operator==(const CallSignature& other) const {
    return std::tie(dynamic_positional_args_treedef, keyword_args,
                    dynamic_args_signatures, device) ==
               std::tie(other.dynamic_positional_args_treedef,
                        other.keyword_args, other.dynamic_args_signatures,
                        other.device) &&
           // `==` on py:objects is the Python `is`. We need equal.
           std::equal(static_args.begin(), static_args.end(),
                      other.static_args.begin(), other.static_args.end(),
                      [](const py::object& a, const py::object& b) {
                        return a.equal(b);
                      });
  }
  bool operator!=(const CallSignature& other) const {
    return !(*this == other);
  }

  // To be used when we want to keep ownership of Python values referenced by
  // the `CallSignature` (i.e. when we insert an entry).
  void IncRef() const;
  // The destructor of the cache should call this on all entries.
  void DecRef() const;

  std::string DebugString() const;
};

void CallSignature::IncRef() const {
  for (const auto& kw : keyword_args) {
    kw.key.inc_ref();
  }
}

void CallSignature::DecRef() const {
  for (const auto& kw : keyword_args) {
    kw.key.dec_ref();
  }
}

template <typename H>
H AbslHashValue(H h, const CallSignature::KwargEntry& kw) {
  h = H::combine(std::move(h), kw.key.ptr(), kw.value_treedef);
  return h;
}

template <typename H>
H AbslHashValue(H h, const CallSignature& s) {
  // /!\ important: We cannot include static arguments to the hash, because
  // the py::object must be hashable for absl. We can try delegating to the
  // Python __hash__, but there are many non-hashable Python types such as
  // np.ndarray.
  // TODO(jblespiau): We should either ban non-hashable objects from jit or we
  // should hash them by object identity.
  h = H::combine_contiguous(std::move(h),
                            s.dynamic_positional_args_treedef.data(),
                            s.dynamic_positional_args_treedef.size());
  h = H::combine_contiguous(std::move(h), s.keyword_args.data(),
                            s.keyword_args.size());
  h = H::combine_contiguous(std::move(h), s.dynamic_args_signatures.data(),
                            s.dynamic_args_signatures.size());
  h = H::combine(std::move(h), s.device);
  return h;
}

std::string CallSignature::DebugString() const {
  std::vector<std::string> static_args_str;
  static_args_str.reserve(static_args.size());
  for (auto& static_arg : static_args) {
    static_args_str.emplace_back(py::cast<std::string>(py::str(static_arg)));
  }

  std::vector<std::string> signature_str;
  signature_str.reserve(dynamic_args_signatures.size());

  for (auto& arg_signature : dynamic_args_signatures) {
    signature_str.emplace_back(arg_signature.DebugString());
  }
  std::vector<std::string> tree_def_str;
  signature_str.reserve(dynamic_positional_args_treedef.size());
  for (auto& tree_def : dynamic_positional_args_treedef) {
    tree_def_str.emplace_back(tree_def.ToString());
  }
  std::vector<std::string> keyword_names;
  keyword_names.reserve(keyword_args.size());
  for (auto& kwarg_entry : keyword_args) {
    keyword_names.emplace_back(py::cast<std::string>(kwarg_entry.key));
    tree_def_str.emplace_back(kwarg_entry.value_treedef.ToString());
  }
  return absl::StrCat(
      static_args.size(), " static_args: ", absl::StrJoin(static_args_str, ","),
      "\n",  // new line
      keyword_args.size(), " keyword args:", absl::StrJoin(keyword_names, ","),
      "\n",  // new-line
      dynamic_positional_args_treedef.size(), " positional args.\n",
      dynamic_args_signatures.size(),
      " dynamic args (positional+keyword):\n   - ",
      absl::StrJoin(signature_str, ", "), "\n   - ",
      absl::StrJoin(tree_def_str, " | "));
}

struct CacheEntry {
  std::shared_ptr<xla::PyExecutable> executable;
  PyTreeDef out_pytree_def;
  // Callables (one for each output) to call on each output to get the Python
  // object (usually a DeviceArray) that we should return.
  // TODO(jblespiau): The goal of the C++ codepath being to be fast, thus, we
  // should not call into Python. It will be trivial to fix this when
  // omnistaging is the only option & when DeviceArray and PyBuffer are merged).
  std::vector<py::function> handlers;

  // Ensures a single thread performs the compilation for a given executable.
  //
  // The first thread (holding the GIL) will create the CacheEntry associated to
  // a signature and if the object has been insterted already, other threads
  // will wait for the notification.
  absl::Notification compilation_complete;
  absl::optional<Status> compilation_error = absl::nullopt;
  // Trivial computation will fallback to Python.
  // Running a jax(pmap) will also fallback to Python.
  bool fall_back_to_python = false;
};

// A `CompiledFunction` is associated to a `jax.jit(f)` and takes care of the
// bookkeeping of the different signatures used and the dispatch of calls to
// the correct underlying `PyExecutable`. This class is thread-safe.
class CompiledFunction {
 public:
  CompiledFunction(py::function fun, py::function cache_miss,
                   py::function get_device, py::function get_jax_enable_x64,
                   py::function get_jax_disable_jit,
                   std::vector<int> static_argnums);
  ~CompiledFunction();

  // This function will:
  // (a) flatten the inputs using pytree
  // (b) get buffer objects from the arguments
  // (c) call the executable
  // (d) construct `DeviceArray` objects from the outputs
  // (e) reconstruct the `PyTree`.
  py::object Call(py::args args, py::kwargs kwargs);

  // This allows `inspect.signature(cpp_jitted_f)` from Python.
  py::object __signature__() {
    static const auto* inspect = new py::module(py::module::import("inspect"));
    return inspect->attr("signature")(fun_);
  }

  int cache_size() const { return executables_.size(); }

 private:
  // Returns nullptr if not present in the cache.
  CacheEntry* GetCacheEntryIfPresent(const CallSignature& signature);
  // Should never return nullptr.
  CacheEntry* AddCacheEntry(const py::args& args, const py::kwargs& kwargs,
                            const CallSignature& signature,
                            py::object out_and_fastpath_data);
  bool JitIsDisabled() { return GetDisableJit() || jax_disable_jit_.value(); }

  bool always_fallback_to_python_ = false;

  const py::function fun_;  // The Python function to jit.
  // See JAX _cpp_jit in api.py for documentation.
  const py::function cache_miss_;

  // We need to know the static arguments to remove them from the arguments
  // passed to the underlying PyExecutable. In sorted order.
  std::vector<int> static_argnums_;
  // We need a `unique_ptr` here to ensure value pointer stability.
  absl::flat_hash_map<CallSignature, std::unique_ptr<CacheEntry>> executables_;

  // As top-level functions are decorated with `jax.jit`, when
  // `CompiledFunction` is being instantiated from Python, the clients are not
  // yet available (done after GoogleInit). They will be during the first call
  // to `Call`.
  // A function taking no arguments and returning the default device and whether
  // jax.jit has been committed to it.
  const py::function get_jax_enable_x64_;
  const py::function get_jax_disable_jit_;
  const py::function get_device_;

  // The writing of the following is protected by the mutex.
  absl::Mutex mu_;
  // The value of the Python flag. The value will be computed only during the
  // first object call, because GoogleInit must have been executed.
  absl::optional<bool> jax_enable_x64_ = absl::nullopt;
  absl::optional<bool> jax_disable_jit_ = absl::nullopt;

  // The logic if the following:
  // - if `device` or `backend` are not specified to `jax.jit`, we will use
  //   the input sticky buffer device, or `default_device_` if there is no
  //   such sticky buffer.
  // - When one of `device` or `backend` is specified, this will determine
  //   the `default_device_` which will be used as the targeted device. In
  //   which case, we will always copy input buffers to this device.
  std::shared_ptr<xla::PyClient> default_pyclient_ = nullptr;
  xla::ClientAndPtr<PjRtDevice> default_pydevice_;
  xla::PjRtDevice* default_device_ = nullptr;
  bool is_committed_;
};

CompiledFunction::CompiledFunction(py::function fun, py::function cache_miss,
                                   py::function get_device,
                                   py::function get_jax_enable_x64,
                                   py::function get_jax_disable_jit,
                                   std::vector<int> static_argnums)
    : fun_(std::move(fun)),
      cache_miss_(std::move(cache_miss)),
      static_argnums_(std::move(static_argnums)),
      get_jax_enable_x64_(get_jax_enable_x64),
      get_jax_disable_jit_(get_jax_disable_jit),
      get_device_(std::move(get_device)) {
  std::sort(static_argnums_.begin(), static_argnums_.end());
}

CompiledFunction::~CompiledFunction() {
  for (const auto& entry : executables_) {
    entry.first.DecRef();
  }
}

namespace {

// The equivalent of the Python jax/lazy.py::is_trivial:
// return (type(lexpr.input) is ArrayVar and
//         lexpr.dims == tuple(range(len(lexpr.shape))))
//
// Expects *only* instances of `DeviceArray`.
bool HasTrivialLazyExpr(py::handle device_array) {
  static const auto* lazy_module =
      new py::module(py::module::import("jax.lazy"));

  auto lexpr = py::getattr(device_array, "_lazy_expr");
  auto input = py::getattr(lexpr, "input");
  if (!input.get_type().is(lazy_module->attr("ArrayVar"))) {
    return false;
  }
  py::tuple dims = py::cast<py::tuple>(lexpr.attr("dims"));
  py::tuple shape = py::cast<py::tuple>(lexpr.attr("shape"));

  for (int i = 0; i < shape.size(); ++i) {
    if (dims[i].is_none()) {
      return false;
    }
    if (py::cast<int>(dims[i]) != i) {
      return false;
    }
  }
  return true;
}

// The resulting information of the parsing and conversion of the arguments.
struct ParsedArgumentsAsBuffers {
  // The call signature will be filled during 2 steps:
  // - `FlattenArguments` will fill the static arguments and the pytree
  //    structures
  // - the shapes and dtypes are filled later, by `ParseAndTransferArguments`.
  CallSignature signature;
  // The concatenation of the dynamic positional arguments and the sorted
  // keyword arguments. We do not need ownership, thus the py::handle.
  // TODO(jblespiau): We do not need py::object here and py::handle suffice and
  // will prevent any counter increment.
  std::vector<py::object> flat_dynamic_args;
  std::vector<py::object> keep_alive_objects;

  // The following is only valid if the parsing succeeds.
  std::vector<xla::PjRtBuffer*> arg_buffers;
  // We may need to keep some objects around, because:
  // (a) we need to extend the lifetime of objects created within
  //    `ConvertArgsToBuffers`
  // (b) `arg_buffers` do not maintain ownership
  std::vector<absl::variant<std::unique_ptr<xla::PyBuffer>,
                            std::unique_ptr<xla::PjRtBuffer>>>
      keep_alive;
};

// Filter out static arguments, flatten and concatenate other arguments (i.e.
// dynamic positional and keyword arguments), filling `arguments` in place.
void FlattenArguments(const py::args& args, const py::kwargs& py_kwargs,
                      absl::Span<int const> static_argnums,
                      ParsedArgumentsAsBuffers& arguments) {
  arguments.flat_dynamic_args.reserve(args.size() + py_kwargs.size() -
                                      static_argnums.size());
  arguments.signature.dynamic_positional_args_treedef.reserve(
      args.size() - static_argnums.size());

  // Positional arguments.
  for (size_t i = 0; i < args.size(); ++i) {
    if (std::find(static_argnums.begin(), static_argnums.end(), i) ==
        static_argnums.end()) {
      PyTreeDef pytree_def;
      pytree_def.FlattenInto(args[i], arguments.flat_dynamic_args);
      arguments.signature.dynamic_positional_args_treedef.push_back(pytree_def);
    } else {
      arguments.signature.static_args.emplace_back(
          // borrow is mandatory here.
          py::reinterpret_borrow<py::object>(args[i]));
    }
  }

  // Keyword arguments.
  std::vector<std::pair<py::handle, py::handle>> kwargs(py_kwargs.begin(),
                                                        py_kwargs.end());
  // We first intern the keys, then sort them (by name, as in the Python path)
  // (see also PyTreeDef::Flatten) and then create the signatures.
  // TODO(jblespiau): We should be able to sort the keys by interned-key
  // pointers, but this requires the Python compilation to do the same.
  arguments.signature.keyword_args.resize(kwargs.size());
  for (size_t i = 0; i < kwargs.size(); ++i) {
    // Intern the key if not already interned.
    if (!PyUnicode_CHECK_INTERNED(kwargs[i].first.ptr())) {
      PyObject* key = kwargs[i].first.ptr();
      kwargs[i].first.inc_ref();
      PyUnicode_InternInPlace(&key);
      arguments.keep_alive_objects.push_back(
          py::reinterpret_steal<py::object>(key));
      kwargs[i].first = py::handle(key);
    }
  }

  std::sort(kwargs.begin(), kwargs.end(),
            [](const std::pair<py::handle, py::handle>& a,
               const std::pair<py::handle, py::handle>& b) {
              return a.first < b.first;
            });
  for (size_t i = 0; i < kwargs.size(); ++i) {
    arguments.signature.keyword_args[i].key = kwargs[i].first;
    arguments.signature.keyword_args[i].value_treedef.FlattenInto(
        kwargs[i].second, arguments.flat_dynamic_args);
  }
}

template <typename CppType, typename Pybind11Type>
std::unique_ptr<xla::PjRtBuffer> ConvertToScalarBuffer(
    const py::handle& scalar, xla::PjRtClient* client,
    xla::PjRtDevice* device) {
  CppType data = py::cast<Pybind11Type>(scalar);
  xla::Shape shape = xla::ShapeUtil::MakeShapeWithType<CppType>({});
  return ValueOrThrow(client->BufferFromHostBuffer(
      &data, shape,
      xla::PjRtClient::HostBufferSemantics::kImmutableOnlyDuringCall, nullptr,
      device));
}

// Convert a scalar to the associated PjRtBuffer or raises an error if it is
// not convertible (thus, this must be called after other checks).
StatusOr<std::unique_ptr<xla::PjRtBuffer>> ScalarToBuffer(
    py::handle scalar, bool jax_enable_x64, xla::PjRtClient* client,
    xla::PjRtDevice* device) {
  // Important: In Python, isinstance(True, int) returns True. Thus, we have
  // to check for bool before int.
  if (py::isinstance<py::bool_>(scalar)) {
    return ConvertToScalarBuffer<bool, py::bool_>(scalar, client, device);
  } else if (py::isinstance<py::int_>(scalar)) {
    if (jax_enable_x64) {
      return ConvertToScalarBuffer<int64, py::int_>(scalar, client, device);
    } else {
      return ConvertToScalarBuffer<int, py::int_>(scalar, client, device);
    }
  } else if (py::isinstance<py::float_>(scalar)) {
    if (jax_enable_x64) {
      return ConvertToScalarBuffer<double, py::float_>(scalar, client, device);

    } else {
      return ConvertToScalarBuffer<float, py::float_>(scalar, client, device);
    }
  } else if (PyComplex_Check(scalar.ptr())) {
    Py_complex result = PyComplex_AsCComplex(scalar.ptr());
    if (result.real == -1.0 && PyErr_Occurred()) {
      PyErr_Clear();
      throw std::runtime_error("Could not convert the complex number");
    }
    if (jax_enable_x64) {
      xla::complex128 data(result.real, result.imag);
      xla::Shape shape = xla::ShapeUtil::MakeShapeWithType<xla::complex128>({});
      return ValueOrThrow(client->BufferFromHostBuffer(
          &data, shape,
          xla::PjRtClient::HostBufferSemantics::kImmutableOnlyDuringCall,
          nullptr, device));
    } else {
      xla::complex64 data(result.real, result.imag);
      xla::Shape shape = xla::ShapeUtil::MakeShapeWithType<xla::complex64>({});
      return ValueOrThrow(client->BufferFromHostBuffer(
          &data, shape,
          xla::PjRtClient::HostBufferSemantics::kImmutableOnlyDuringCall,
          nullptr, device));
    }
  }
  return InvalidArgument(
      "%s", absl::StrCat(
                "Not supported: The C++ jax jit execution path, only accepts "
                "DeviceArray, Numpy arrays, or Python scalars. Got type ",
                py::cast<std::string>(py::str(scalar.get_type()))));
}

const py::dtype* DtypeTo32BitDtype(const py::dtype& dtype) {
  static const auto* int64_dt = new py::dtype("int64");
  static const auto* int32_dt = new py::dtype("int32");
  static const auto* uint64_dt = new py::dtype("uint64");
  static const auto* uint32_dt = new py::dtype("uint32");
  static const auto* float64_dt = new py::dtype("float64");
  static const auto* float32_dt = new py::dtype("float32");
  static const auto* complex64_dt = new py::dtype("complex64");
  static const auto* complex128_dt = new py::dtype("complex128");

  if (dtype.equal(*int64_dt)) {
    return int32_dt;
  }
  if (dtype.equal(*float64_dt)) {
    return float32_dt;
  }
  if (dtype.equal(*uint64_dt)) {
    return uint32_dt;
  }
  if (dtype.equal(*complex128_dt)) {
    return complex64_dt;
  }

  return nullptr;
}

bool IsFloat0(py::array arg) {
  static const auto* dtypes_module =
      new py::module(py::module::import("jax.dtypes"));
  static const auto* float0_dtype =
      new py::handle(dtypes_module->attr("float0"));
  return float0_dtype->is(arg.attr("dtype"));
}

// Converts flattened arguments contained in ParsedArgumentsAsBuffers in
// place. If arguments are `DeviceArray`, they must all be on the same `Device`.
//
// Returns `OkStatus()` on success. Returning an error should lead to calling
// the Python fallback.
Status ConvertArgsToBuffers(bool jax_enable_x64, xla::PyClient& pyclient,
                            xla::PjRtDevice* default_device, bool is_committed,
                            ParsedArgumentsAsBuffers& arguments) {
  std::vector<xla::PjRtBuffer*>& arg_buffers = arguments.arg_buffers;
  auto& keep_alive = arguments.keep_alive;

  int num_flat_dynamic_args = arguments.flat_dynamic_args.size();
  arg_buffers.reserve(num_flat_dynamic_args);
  arguments.signature.dynamic_args_signatures.reserve(num_flat_dynamic_args);

  static const auto* xla_module =
      new py::module(py::module::import("jax.interpreters.xla"));
  const auto& device_array = xla_module->attr("DeviceArray");

  static const auto* numpy_module = new py::module(py::module::import("numpy"));
  const auto& np_array = numpy_module->attr("array");

  // When the jitted function is not committed, we first check whether any
  // sticky `DeviceArray` is present and on which device they live. See also:
  // https://github.com/google/jax/pull/1884
  // https://github.com/google/jax/pull/1916 for the rationale why the
  // computation follows the data locality.
  // It's also similar to PyTorch's behavior.
  xla::PjRtDevice* data_device = nullptr;
  if (is_committed) {
    data_device = default_device;
  } else {
    for (py::handle arg : arguments.flat_dynamic_args) {
      // We specically only deal with DeviceArray (not ShardedDeviceArray).
      // (Can happen in jit(pmap), e.g. "test_jit_nested_donate_ignored").
      if (arg.get_type().is(device_array)) {
        xla::PyBuffer* buffer;
        if (arg.attr("_device").is_none()) {  // Skip non-sticky devices.
          continue;
        }
        try {
          // This can fail, e.g. when device_buffer is a `DeviceConstant`.
          buffer = py::cast<xla::PyBuffer*>(arg.attr("device_buffer"));
        } catch (const py::cast_error& e) {
          return InvalidArgument(
              "%s",
              absl::StrCat("[jaxjit] Unsupported subclass of `DeviceArray`: "
                           "`device_buffer` field is of type ",
                           py::cast<std::string>(
                               arg.attr("device_buffer").get_type().str()),
                           " while a `PyBuffer` was expected."

                           ));
        }
        xla::PjRtDevice* device = buffer->buffer()->device();
        if (data_device && (device != data_device)) {
          throw std::invalid_argument(absl::StrCat(
              "primitive arguments must be colocated on the same device ("
              "C++ jax.jit). Arguments are on devices: ",
              device->DebugString(), " and ", data_device->DebugString()));
        } else {
          data_device = device;
        }
      }
    }
  }
  if (!data_device) {
    // No `DeviceArray` were found default to `default_device`.
    data_device = default_device;
  }
  CHECK(data_device);
  arguments.signature.device = data_device;
  xla::PjRtClient* pjrt_client = data_device->client();

  for (py::handle arg : arguments.flat_dynamic_args) {
    if (arg.get_type().is(device_array)) {
      if (!HasTrivialLazyExpr(arg)) {
        return InvalidArgument(
            "Non-trivial lazy expression not supported in C++. "
            "Falling back to Python.");
      }

      PyBuffer* buffer = py::cast<xla::PyBuffer*>(arg.attr("device_buffer"));
      if (buffer->device().contents == data_device) {
        arg_buffers.push_back(buffer->buffer());
      } else {
        // source and target platforms are the same, but different device.
        // Perform a device-to-device copy.
        // buffers from different XLA backends are passed through the host.
        std::unique_ptr<PjRtBuffer> copied_buffer =
            ValueOrThrow(buffer->buffer()->CopyToDevice(data_device));
        arg_buffers.push_back(copied_buffer.get());
        keep_alive.emplace_back(std::move(copied_buffer));
      }

      ArgSignature sig;
      sig.dtype = buffer->shape().element_type();
      sig.shape.assign(buffer->shape().dimensions().begin(),
                       buffer->shape().dimensions().end());
      sig.weak_type = py::cast<py::bool_>(arg.attr("aval").attr("weak_type"));
      arguments.signature.dynamic_args_signatures.push_back(std::move(sig));
    } else if (py::isinstance<py::array>(arg)) {
      // TODO(jblespiau): Can we improve this call? Do we need the underlying
      // GlobalPyRefManager() and co?
      py::array numpy_array = py::cast<py::array>(arg);
      if (IsFloat0(numpy_array)) {
        return InvalidArgument(
            "float0 numpy arrays not supported in C++. "
            "It will fallback to Python.");
      }
      // If jax_enable_x64 is not set, we need to coerce 32 bits types.
      // Note that this is calling back to Python!
      if (!jax_enable_x64) {
        const py::dtype* to_dtype = DtypeTo32BitDtype(numpy_array.dtype());
        if (to_dtype) {
          numpy_array = np_array(numpy_array, *to_dtype);
        }
      }
      std::unique_ptr<xla::PyBuffer> buffer =
          ValueOrThrow(pyclient.BufferFromPyval(
              numpy_array, data_device,
              /*force_copy=*/false, /*host_buffer_semantics=*/
              xla::PjRtClient::HostBufferSemantics::kZeroCopy));
      arg_buffers.push_back(buffer->buffer());

      ArgSignature sig;
      sig.dtype = buffer->shape().element_type();
      sig.weak_type = false;
      sig.shape.assign(buffer->shape().dimensions().begin(),
                       buffer->shape().dimensions().end());
      arguments.signature.dynamic_args_signatures.push_back(sig);

      keep_alive.emplace_back(std::move(buffer));
    } else {
      StatusOr<std::unique_ptr<xla::PjRtBuffer>> buffer =
          ScalarToBuffer(arg, jax_enable_x64, pjrt_client, data_device);
      if (!buffer.ok()) {
        return buffer.status();
      }
      arg_buffers.push_back(buffer.ValueOrDie().get());
      ArgSignature sig;
      sig.dtype = buffer.ValueOrDie()->on_host_shape().element_type();
      sig.weak_type = true;
      arguments.signature.dynamic_args_signatures.push_back(sig);

      keep_alive.emplace_back(std::move(buffer).ValueOrDie());
    }
  }
  return Status::OK();
}

}  // namespace

CacheEntry* CompiledFunction::GetCacheEntryIfPresent(
    const CallSignature& signature) {
  auto found_iterator = executables_.find(signature);
  if (found_iterator != executables_.end()) {  // Cache hit!
    if (!found_iterator->second->compilation_complete.HasBeenNotified()) {
      py::gil_scoped_release gil_release;
      found_iterator->second->compilation_complete.WaitForNotification();
    }
    if (found_iterator->second->compilation_error) {
      throw std::invalid_argument(
          found_iterator->second->compilation_error.value().error_message());
    }
    return found_iterator->second.get();
  }
  return nullptr;
}

CacheEntry* CompiledFunction::AddCacheEntry(const py::args& args,
                                            const py::kwargs& kwargs,
                                            const CallSignature& signature,
                                            py::object out_and_fastpath_data) {
  // We need to insert the element.
  auto result = executables_.emplace(signature, std::make_unique<CacheEntry>());
  auto it = result.first;
  CacheEntry* cache_entry = it->second.get();
  // CallSignatures in the cache own their keyword argument reference.
  result.first->first.IncRef();

  py::tuple tuple = py::cast<py::tuple>(out_and_fastpath_data);
  CHECK_EQ(tuple.size(), 2);
  if (tuple[1].is_none()) {
    cache_entry->fall_back_to_python = true;
    cache_entry->compilation_complete.Notify();
    return cache_entry;
  }

  py::tuple executable_handlers_out_tree = py::cast<py::tuple>(tuple[1]);
  CHECK_EQ(executable_handlers_out_tree.size(), 3);

  auto executable = py::cast<std::shared_ptr<xla::PyExecutable>>(
      executable_handlers_out_tree[0]);
  std::vector<py::function> handlers;
  for (const auto& handler :
       py::cast<py::list>(executable_handlers_out_tree[1])) {
    handlers.push_back(py::cast<py::function>(handler));
  }
  auto out_tree = py::cast<PyTreeDef>(executable_handlers_out_tree[2]);

  cache_entry->executable = std::move(executable);
  int num_devices =
      cache_entry->executable->pjrt_executable().local_devices().size();
  // The presence of jit(pmap) is detected from Python.
  CHECK_EQ(num_devices, 1);

  cache_entry->handlers = std::move(handlers);
  cache_entry->out_pytree_def = std::move(out_tree);

  cache_entry->compilation_complete.Notify();
  return cache_entry;
}

py::object CompiledFunction::Call(py::args args, py::kwargs kwargs) {
  if (always_fallback_to_python_) {
    return py::cast<py::tuple>(cache_miss_(*args, **kwargs))[0];
  }
  // Delayed values are retrieved on the first call to `Call`.
  if (!default_device_) {
    // As we are calling Python code, that may release the GIL, we first hold
    // mu_ before holding the GIL.
    py::gil_scoped_release gil_release;
    {
      absl::MutexLock lock1(&mu_);
      py::gil_scoped_acquire gil_aquire;

      jax_enable_x64_ = py::cast<bool>(get_jax_enable_x64_());
      jax_disable_jit_ = py::cast<bool>(get_jax_disable_jit_());
      if (!default_device_) {
        py::object device_and_is_committed = get_device_();
        try {
          default_pydevice_ = py::cast<ClientAndPtr<PjRtDevice>>(
              device_and_is_committed.attr("default_device"));
        } catch (const py::cast_error& e) {
          // Pathways and Cloud TPU 2VM runtime.
          always_fallback_to_python_ = true;
          return py::cast<py::tuple>(cache_miss_(*args, **kwargs))[0];
        }
        default_pyclient_ = default_pydevice_.client;
        default_device_ = default_pydevice_.contents;
        if (!default_device_) {  // UPTC
          always_fallback_to_python_ = true;
          return py::cast<py::tuple>(cache_miss_(*args, **kwargs))[0];
        }
        is_committed_ =
            py::cast<bool>(device_and_is_committed.attr("committed_to_device"));
      }
    }
  }
  CHECK(default_device_);
  if (JitIsDisabled()) {
    return fun_(*args, **kwargs);
  }
  ParsedArgumentsAsBuffers arguments;
  FlattenArguments(args, kwargs, static_argnums_, arguments);

  // The C++ jit do not support Tracers arguments inputs yet. The Python-based
  // jit function will be called if any of the dynamic arguments is unsupported.
  if (!ConvertArgsToBuffers(jax_enable_x64_.value(), *default_pyclient_,
                            default_device_, is_committed_, arguments)
           .ok()) {
    return py::cast<py::tuple>(cache_miss_(*args, **kwargs))[0];
  }

  CacheEntry* cache_entry = GetCacheEntryIfPresent(arguments.signature);

  if (!cache_entry) {
    py::object out_and_fastpath_data = cache_miss_(*args, **kwargs);
    cache_entry = GetCacheEntryIfPresent(arguments.signature);
    if (!cache_entry) {
      cache_entry = AddCacheEntry(args, kwargs, arguments.signature,
                                  out_and_fastpath_data);
    }
    CHECK(cache_entry);
    if (cache_entry->fall_back_to_python) {
      return py::cast<py::tuple>(out_and_fastpath_data)[0];
    }
    // As we have already computed the results, we can return it.
    // It's even *required* e.g. if there are donated arguments, because
    // otherwise the buffer which has been donated already will be invalid.
    return py::cast<py::tuple>(out_and_fastpath_data)[0];
  }
  CHECK(cache_entry);
  if (cache_entry->fall_back_to_python) {
    return py::cast<py::tuple>(cache_miss_(*args, **kwargs))[0];
  }
  std::vector<std::unique_ptr<xla::PyBuffer>> outputs =
      ValueOrThrow(cache_entry->executable->PjRtExecute(arguments.arg_buffers));

  const std::vector<py::function>& handlers = cache_entry->handlers;
  py::list flat_device_arrays;
  for (int i = 0; i < outputs.size(); ++i) {
    flat_device_arrays.append(handlers[i](std::move(outputs[i])));
  }
  return cache_entry->out_pytree_def.Unflatten(flat_device_arrays);
}

}  // namespace

void BuildJaxjitSubmodule(pybind11::module& m) {
  py::module jitlib = m.def_submodule("jax_jit", "Jax C++ jit library");

  py::class_<CompiledFunction, std::unique_ptr<CompiledFunction>> cfun(
      jitlib, "CompiledFunction");
  cfun.def("__call__", &CompiledFunction::Call);
  cfun.def_property_readonly("__signature__", &CompiledFunction::__signature__);

  jitlib.def("set_disable_jit", &SetDisableJit);
  jitlib.def("get_disable_jit", &GetDisableJit);
  jitlib.def(
      "jit",
      [](py::function fun, py::function cache_miss, py::function get_device,
         py::function get_jax_enable_x64, py::function get_jax_disable_jit,
         std::vector<int> static_argnums) -> std::unique_ptr<CompiledFunction> {
        return std::make_unique<CompiledFunction>(
            std::move(fun), std::move(cache_miss), std::move(get_device),
            std::move(get_jax_enable_x64), std::move(get_jax_disable_jit),
            std::move(static_argnums));
      });

  // Only for testing purposes
  cfun.def("_cache_size", &CompiledFunction::cache_size);
  jitlib.def("_DtypeTo32BitDtype", [](const py::object obj) -> py::object {
    py::dtype dtype = py::dtype::from_args(obj);
    const py::dtype* res = DtypeTo32BitDtype(dtype);
    if (res) {
      return *res;
    } else {
      return py::none();
    }
  });
  jitlib.def("_is_float0", &IsFloat0);
  jitlib.def("_is_trivial", &HasTrivialLazyExpr);
  jitlib.def("_ScalarToBuffer", [](py::handle scalar, bool jax_enable_x64,
                                   std::shared_ptr<xla::PyClient> client) {
    xla::PjRtClient* pjrt_client = client->pjrt_client();

    return std::make_unique<xla::PyBuffer>(
        client,
        ScalarToBuffer(scalar, jax_enable_x64, pjrt_client,
                       pjrt_client->local_devices()[0])
            .ValueOrDie(),
        nullptr);
  });
}

}  // namespace xla
