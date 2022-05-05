"""External versions of build rules that differ outside of Google."""

load(
    "//tensorflow:tensorflow.bzl",
    "clean_dep",
)

def tflite_portable_test_suite(**kwargs):
    """This is a no-op outside of Google."""
    _ignore = [kwargs]
    pass

def tflite_portable_test_suite_combined(**kwargs):
    """This is a no-op outside of Google."""
    _ignore = [kwargs]
    pass

def tflite_ios_per_kernel_test(**kwargs):
    """This is a no-op outside of Google."""
    _ignore = [kwargs]
    pass

def ios_visibility_whitelist():
    """This is a no-op outside of Google."""
    pass

def tflite_extra_gles_deps():
    """This is a no-op outside of Google."""
    return []

def tflite_ios_lab_runner(version):
    """This is a no-op outside of Google."""

    # Can switch back to None when https://github.com/bazelbuild/rules_apple/pull/757 is fixed
    return "@build_bazel_rules_apple//apple/testing/default_runner:ios_default_runner"

def if_nnapi(supported, not_supported = [], supported_android = None):
    if supported_android == None:
        supported_android = supported

    # We use a blacklist rather than a whitelist for known unsupported platforms.
    return select({
        clean_dep("//tensorflow:emscripten"): not_supported,
        clean_dep("//tensorflow:ios"): not_supported,
        clean_dep("//tensorflow:macos"): not_supported,
        clean_dep("//tensorflow:windows"): not_supported,
        clean_dep("//tensorflow:android"): supported_android,
        "//conditions:default": supported,
    })

def tflite_hexagon_mobile_test(name):
    """This is a no-op outside of Google."""
    pass

def tflite_hexagon_nn_skel_libraries():
    """This is a no-op outside of Google due to license agreement process.

    Developers who want to use hexagon nn skel libraries can download
    and install the libraries as the guided in
    https://www.tensorflow.org/lite/performance/hexagon_delegate#step_2_add_hexagon_libraries_to_your_android_app.
    For example, if you installed the libraries at third_party/hexagon_nn_skel
    and created third_party/hexagon_nn_skel/BUILD with a build target,
    filegroup(
        name = "libhexagon_nn_skel",
        srcs = glob(["*.so"]),
    )
    you need to modify this macro to specifiy the build target.
    return ["//third_party/hexagon_nn_skel:libhexagon_nn_skel"]
    """
    return []

def tflite_schema_utils_friends():
    """This is a no-op outside of Google.

    Return the package group declaration to which targets for Flatbuffer schema utilities."""

    # Its usage should be rare, and is often abused by tools that are doing
    # Flatbuffer creation/manipulation in unofficially supported ways."
    return ["//..."]

def flex_portable_tensorflow_deps():
    """Returns dependencies for building portable tensorflow in Flex delegate."""

    return [
        "//third_party/fft2d:fft2d_headers",
        "//third_party/eigen3",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/strings:str_format",
        "@gemmlowp",
        "@icu//:common",
        "//third_party/icu/data:conversion_data",
    ]
