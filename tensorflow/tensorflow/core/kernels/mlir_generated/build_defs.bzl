"""Generates cubin headers for TF dialect ops."""

load("@local_config_cuda//cuda:build_defs.bzl", "cuda_gpu_architectures")
load(
    "@local_config_rocm//rocm:build_defs.bzl",
    "rocm_gpu_architectures",
    "rocm_is_configured",
)
load(
    "//tensorflow/core/platform/default:cuda_build_defs.bzl",
    "if_cuda_is_configured",
)
load(
    "//tensorflow/stream_executor:build_defs.bzl",
    "if_gpu_is_configured",
)

def if_mlir_generated_gpu_kernels_enabled(if_true, if_false = []):
    return select({
        "//tensorflow/core/kernels/mlir_generated:mlir_generated_gpu_kernels_disabled": if_false,
        "//conditions:default": if_true,
    })

def _lookup_file(filegroup, path):
    """Extracts file at (relative) path in filegroup."""
    for file in filegroup.files.to_list():
        if file.path.endswith(path) or file.path.endswith(path + ".exe"):
            return file
    return None

GpuBinaryInfo = provider(
    "GPU binaries in either cubin format or hsaco format",
    fields = ["gpu_bins"],
)

def _gen_kernel_gpu_bin_impl(ctx):
    name = ctx.attr.name
    tile_sizes = ctx.attr.tile_size.replace("x", ",")
    cmd_args = []
    if ctx.attr.same_shape:
        cmd_args.append("--same_shape=%s" % ctx.attr.same_shape)
    if ctx.attr.unroll_factors:
        cmd_args.append("--unroll_factors=%s" % ctx.attr.unroll_factors)

    if ctx.attr.extra_args:
        cmd_args.extend(ctx.attr.extra_args)

    gpu_bins = []
    for arch in ctx.attr.gpu_archs:
        # TODO(b/170283783): 'compute_' should generate both SASS and PTX.
        arch = arch.replace("compute_", "sm_")
        filename = "%s.%s.bin" % (name, arch)
        gpu_bin = ctx.actions.declare_file(filename)
        ctx.actions.run(
            inputs = [ctx.file.mlir_op, ctx.file._tfso],
            outputs = [gpu_bin],
            executable = ctx.executable._tool,
            arguments = cmd_args + [
                "--tile_sizes=%s" % tile_sizes,
                "--arch=%s" % arch,
                "--input=%s" % ctx.file.mlir_op.path,
                "--output=%s" % gpu_bin.path,
            ],
            mnemonic = "compile",
        )
        gpu_bins.append(gpu_bin)
    return [GpuBinaryInfo(gpu_bins = gpu_bins)]

_gen_kernel_gpu_bin_rule = rule(
    attrs = {
        "mlir_op": attr.label(mandatory = True, allow_single_file = True),
        "tile_size": attr.string(mandatory = True),
        "same_shape": attr.string(),
        "unroll_factors": attr.string(),
        "gpu_archs": attr.string_list(mandatory = True),
        "extra_args": attr.string_list(),
        "_tfso": attr.label(
            default = Label("//tensorflow:libtensorflow_framework.so.2"),
            cfg = "host",
            allow_single_file = True,
        ),
        "_tool": attr.label(
            executable = True,
            default = Label("//tensorflow/compiler/mlir/tools/kernel_gen:tf_to_gpu_binary"),
            cfg = "host",
        ),
    },
    output_to_genfiles = True,
    implementation = _gen_kernel_gpu_bin_impl,
)

def _gen_kernel_image_hdr_impl_cuda(ctx):
    images = []
    for cubin in ctx.attr.input[GpuBinaryInfo].gpu_bins:
        arch = cubin.path.split(".")[-2]
        images.append("--image=profile=%s,file=%s" % (arch, cubin.path))

    # Generate fatbin file from all cubins.
    fatbin = ctx.actions.declare_file("%s.fatbin" % ctx.attr.name)
    ctx.actions.run(
        outputs = [fatbin],
        inputs = ctx.attr.input[GpuBinaryInfo].gpu_bins,
        executable = _lookup_file(ctx.attr._gpu_root, "bin/fatbinary"),
        arguments = [
            "--64",
            "--cmdline=--compile-only",
            "--link",
            "--compress-all",
            "--create=%s" % fatbin.path,
        ] + images,
        mnemonic = "fatbinary",
    )

    bin2c = _lookup_file(ctx.attr._gpu_root, "bin/bin2c")
    ctx.actions.run_shell(
        outputs = [ctx.outputs.out],
        inputs = [fatbin],
        tools = [bin2c],
        command = "%s --static --const --type=char --name=%s %s 1> %s" %
                  (bin2c.path, ctx.attr.symbol, fatbin.path, ctx.outputs.out.path),
        mnemonic = "bin2c",
    )

def _gen_kernel_image_hdr_impl_rocm(ctx):
    hsaco_files = []
    hsaco_targets = []

    # Add a dummy host target triple...clang-offload-bundler requires 1 and only 1 host target triple
    hsaco_files.append("/dev/null")
    hsaco_targets.append("host-x86_64-unknown-linux")

    hsacos = ctx.attr.input[GpuBinaryInfo].gpu_bins
    for hsaco in hsacos:
        gfx_arch = hsaco.path.split(".")[-2]
        hsaco_files.append(hsaco.path)
        hsaco_targets.append("hip-amdgcn-amd-amdhsa-%s" % gfx_arch)

    # Generate fatbin file from all hsacos.
    fatbin = ctx.actions.declare_file("%s.fatbin" % ctx.attr.name)
    ctx.actions.run(
        outputs = [fatbin],
        inputs = hsacos,
        executable = _lookup_file(ctx.attr._gpu_root, "bin/clang-offload-bundler"),
        arguments = [
            "--inputs=%s" % ",".join(hsaco_files),
            "--targets=%s" % ",".join(hsaco_targets),
            "--type=o",
            "--outputs=%s" % fatbin.path,
        ],
        mnemonic = "fatbinary",
    )

    ctx.actions.run_shell(
        outputs = [ctx.outputs.out],
        inputs = [fatbin],
        command = (
            ("hex=`hexdump -v -e \'/1 \"0x%%02x, \"\' %s` && " +
             "len=`echo $hex | wc -c` && " +
             "echo 'static const unsigned char %s['$len' + 1] = {' > %s && " +
             "echo $hex | cat >> %s && " +
             "echo '};' >> %s") % (
                fatbin.path,
                ctx.attr.symbol,
                ctx.outputs.out.path,
                ctx.outputs.out.path,
                ctx.outputs.out.path,
            )
        ),
    )

_gen_kernel_image_hdr_rule = rule(
    implementation = _gen_kernel_image_hdr_impl_rocm if rocm_is_configured() else _gen_kernel_image_hdr_impl_cuda,
    output_to_genfiles = True,
    attrs = {
        "input": attr.label(mandatory = True, providers = [GpuBinaryInfo]),
        "out": attr.output(mandatory = True),
        "symbol": attr.string(mandatory = True),
        "_gpu_root": attr.label(
            default = Label("@local_config_rocm//rocm:rocm_root") if rocm_is_configured() else Label("@local_config_cuda//cuda:cuda_root"),
        ),
    },
)

def _gen_kernel_image_hdr(name, mlir_op, gpu_archs, tile_size, same_shape = None, unroll_factors = None, extra_args = []):
    """Generates a C header with fatbin data from a Tensorflow op."""
    _gen_kernel_gpu_bin_rule(
        name = name + "_cubin",
        mlir_op = mlir_op,
        tile_size = tile_size,
        same_shape = same_shape,
        unroll_factors = unroll_factors,
        gpu_archs = gpu_archs,
        extra_args = extra_args,
    )
    _gen_kernel_image_hdr_rule(
        name = name,
        input = ":" + name + "_cubin",
        out = "%s.h" % name,
        symbol = "k%s" % name.replace("_", " ").title().replace(" ", ""),
    )

def _gen_mlir_op_impl(ctx):
    # In order to generate a ranked kernel we change *xelem_type to ?xelem_type
    # and remove element type from the entry function name.
    convert_to_ranked = ""
    if ctx.attr.unranked == False:
        convert_to_ranked = "sed s/*x/?x/g | sed s/_elem_type//g |"
    cmd = ctx.actions.run_shell(
        inputs = [ctx.file.template],
        outputs = [ctx.outputs.out],
        command = (
            ("cat %s | %s sed s/elem_type/%s/g | sed 's/c64/complex<f32>/g'" +
             " | sed 's/c128/complex<f64>/g' > %s") % (
                ctx.file.template.path,
                convert_to_ranked,
                ctx.attr.type,
                ctx.outputs.out.path,
            )
        ),
    )

_gen_mlir_op_rule = rule(
    implementation = _gen_mlir_op_impl,
    output_to_genfiles = True,
    attrs = {
        "template": attr.label(mandatory = True, allow_single_file = True),
        "type": attr.string(mandatory = True),
        "out": attr.output(mandatory = True),
        "unranked": attr.bool(mandatory = True),
    },
)

def _gen_mlir_op(name, type, unranked):
    tmpl_name = name.replace("_unranked", "") if unranked else name
    _gen_mlir_op_rule(
        name = "generate_{name}_{type}_mlir".format(name = name, type = type),
        template = "op_definitions/{name}.mlir.tmpl".format(name = tmpl_name),
        type = type,
        out = "{name}_{type}.mlir".format(name = name, type = type),
        unranked = unranked,
    )

def gen_ranked_kernel_library(name, types, tile_size, tags = [], same_shape = None, unroll_factors = None, extra_args = []):
    """ Generate a library with kernels for a specific tensorflow op.

    Args:
      name: The name of the tensorflow op.
      types: The types ("f16", "f32", "f64") for which a kernel should be generated.
      tile_size: The tiling specification, e.g. "16x16".
      unroll_factors: The unrolling specification, e.g. "4,4"
      tags: The tags which should be added to the library.
      same_shape: The information about which shapes are the same, e.g. "0,1".
      extra_args: Extra arguments to pass to the generator tool.
    """

    if cuda_gpu_architectures() or rocm_gpu_architectures():
        for type in types:
            _gen_mlir_op(
                name = name,
                type = type,
                unranked = False,
            )
            _gen_kernel_image_hdr(
                name = "{name}_{type}_kernel".format(name = name, type = type),
                mlir_op = "{name}_{type}.mlir".format(name = name, type = type),
                gpu_archs = rocm_gpu_architectures() if rocm_is_configured() else cuda_gpu_architectures(),
                tile_size = tile_size,
                same_shape = same_shape,
                unroll_factors = unroll_factors,
                extra_args = extra_args,
            )

    native.cc_library(
        name = name + "_kernels",
        hdrs = if_gpu_is_configured([":{name}_{type}_kernel".format(name = name, type = type) for type in types]),
        tags = tags,
    )

################################################################################
# Unranked kernels build rules.
################################################################################

def if_mlir_unranked_kernels_enabled(if_true, if_false = []):
    return select({
        "//tensorflow/core/kernels/mlir_generated:mlir_use_unranked_kernels": if_true,
        "//conditions:default": if_false,
    })

def _gen_unranked_kernel_fatbin_impl(ctx):
    name = ctx.attr.name
    cmd_args = []
    if ctx.attr.unroll_factors:
        cmd_args.append("--unroll_factors=%s" % ctx.attr.unroll_factors)
    if ctx.attr.extra_args:
        cmd_args.extend(ctx.attr.extra_args)
    tile_sizes = ctx.attr.tile_size.replace("x", ",")
    arch_flag = ",".join(ctx.attr.gpu_archs)
    gpu_bin = ctx.outputs.output
    ctx.actions.run(
        inputs = [ctx.file.mlir_op],
        outputs = [gpu_bin],
        executable = ctx.executable._tool,
        arguments = cmd_args + [
            "--tile_sizes=%s" % tile_sizes,
            "--arch=%s" % arch_flag,
            "--input=%s" % ctx.file.mlir_op.path,
            "--output=%s" % gpu_bin.path,
        ],
        mnemonic = "compile",
    )

_gen_unranked_kernel_fatbin_rule = rule(
    attrs = {
        "mlir_op": attr.label(mandatory = True, allow_single_file = True),
        "output": attr.output(mandatory = True, doc = "The generated file"),
        "tile_size": attr.string(mandatory = True),
        "unroll_factors": attr.string(),
        "gpu_archs": attr.string_list(mandatory = True),
        "extra_args": attr.string_list(),
        "_tool": attr.label(
            executable = True,
            default = Label("//tensorflow/compiler/mlir/tools/kernel_gen:tf_to_kernel"),
            cfg = "host",
        ),
    },
    output_to_genfiles = True,
    implementation = _gen_unranked_kernel_fatbin_impl,
)

def gen_unranked_kernel_library(name, types, tile_size, tags = [], unroll_factors = None, extra_args = []):
    """ Generate a library with unranked kernels for a specific tensorflow op.

    Args:
      name: The name of the tensorflow op.
      types: The types ("f16", "f32", "f64") for which a kernel should be generated.
      tile_size: The tiling specification, e.g. "16x16".
      unroll_factors: The unrolling specification, e.g. "4,4"
      tags: The tags which should be added to the library.
      extra_args: Extra arguments to pass to the generator tool.
    """

    if cuda_gpu_architectures():
        for type in types:
            _gen_mlir_op(
                name = name,
                type = type,
                unranked = True,
            )
            _gen_unranked_kernel_fatbin_rule(
                name = "{name}_{type}_kernel_generator".format(name = name, type = type),
                mlir_op = "{name}_{type}.mlir".format(name = name, type = type),
                output = "{name}_{type}.a".format(name = name, type = type),
                gpu_archs = cuda_gpu_architectures(),
                tile_size = tile_size,
                unroll_factors = unroll_factors,
                extra_args = extra_args,
            )
            native.cc_import(
                name = "{name}_{type}_kernel".format(name = name, type = type),
                static_library = "{name}_{type}.a".format(name = name, type = type),
            )

    native.cc_library(
        name = name + "_kernels",
        deps = if_cuda_is_configured([":{name}_{type}_kernel".format(name = name, type = type) for type in types]),
        linkstatic = 1,
        tags = tags,
    )

def gen_kernel_library(name, types, tile_size, tags = [], same_shape = None, unroll_factors = None, extra_args = [], generate_ranked = True, generate_unranked = False):
    if (generate_ranked):
        gen_ranked_kernel_library(
            name = name,
            types = types,
            tile_size = tile_size,
            tags = tags,
            same_shape = same_shape,
            unroll_factors = unroll_factors,
            extra_args = extra_args,
        )
    if (generate_unranked):
        gen_unranked_kernel_library(
            name = name + "_unranked",
            types = types,
            tile_size = tile_size,
            tags = tags,
            unroll_factors = unroll_factors,
            extra_args = extra_args,
        )
