"""Custom rule to run SWIG hermetically for pylibfdt."""

load("//build/kernel/kleaf:hermetic_tools.bzl", "hermetic_toolchain")

def _pylibfdt_swig_impl(ctx):
    interface_file = ctx.file.src
    include_dirs = ctx.files.include_dirs
    libfdt_sources = ctx.files.libfdt_sources

    # Resolve include paths statically in Starlark from the File objects
    include_flags = ["-I" + d.path for d in include_dirs]

    py_out = ctx.actions.declare_file("scripts/dtc/pylibfdt/libfdt.py")
    c_out = ctx.actions.declare_file("scripts/dtc/pylibfdt/libfdt_wrap.c")

    hermetic_tools = hermetic_toolchain.get(ctx)

    cmd = """
        {setup}

        swig -python \
            {include_args} \
            -o {c_out} \
            -outdir {py_out_dir} \
            {src}
    """.format(
        setup = hermetic_tools.setup,
        include_args = " ".join(include_flags),
        c_out = c_out.path,
        py_out_dir = py_out.dirname,
        src = interface_file.path,
    )

    # NOTE: We do NOT include include_dirs in inputs to avoid sandbox conflicts.
    # All necessary source files are already mapped via libfdt_sources.
    ctx.actions.run_shell(
        inputs = depset([interface_file] + libfdt_sources),
        tools = hermetic_tools.deps,
        outputs = [py_out, c_out],
        command = cmd,
        mnemonic = "PylibfdtSwig",
        progress_message = "Running SWIG on %{label}",
    )

    return [
        DefaultInfo(files = depset([py_out])),
        OutputGroupInfo(wrapper = depset([c_out])),
    ]

pylibfdt_swig = rule(
    implementation = _pylibfdt_swig_impl,
    doc = "Runs SWIG hermetically to generate pylibfdt C/Python wrappers.",
    attrs = {
        "src": attr.label(
            mandatory = True,
            allow_single_file = True,
            doc = "The SWIG .i interface file",
        ),
        "include_dirs": attr.label_list(
            mandatory = True,
            allow_files = True,
            doc = "List of filegroups/directories containing include paths for SWIG",
        ),
        "libfdt_sources": attr.label_list(
            mandatory = True,
            allow_files = True,
            doc = "The full list of libfdt C/H sources to map into the action sandbox",
        ),
    },
    toolchains = [hermetic_toolchain.type],
)
