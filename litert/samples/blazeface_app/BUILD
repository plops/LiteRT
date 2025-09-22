# litert/samples/blazeface_app/BUILD

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "blazeface_app",
    srcs = ["main.cc"],
    deps = [
        "//litert/cc:litert_api_with_dynamic_runtime",
        # Dependency for X11 headers
        "@x11//:headers",
        # The C++ API uses absl::Span for buffer operations
        "@com_google_absl//absl/types:span",
    ],
    linkopts = [
        "-lX11",
        "-lXext",
        "-lXfixes",
    ],
)