# litert/samples/blazeface_app/BUILD

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "blazeface_app",
    srcs = ["main.cc"],
    # Linker options are still required for the final linking stage.
    linkopts = [
        "-lX11",
        "-lXext",
        "-lXfixes",
    ],
    deps = [
        "//litert/cc:litert_api_with_dynamic_runtime",
        # Add a dependency on the X11 headers target we defined in WORKSPACE.
        "@x11//:headers",
    ],
)
