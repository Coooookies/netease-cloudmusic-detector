{
    "targets": [
        {
            "target_name": "smtc",
            "cflags!": ["-fno-exceptions"],
            "cflags_cc!": ["-fno-exceptions"],
            "sources": ["addons/smtc.cc"],
            "include_dirs": ["<!@(node -p \"require('node-addon-api').include\")"],
            "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"],
            "libraries": ["runtimeobject.lib"],
            "msvs_settings": {
                "VCCLCompilerTool": {"AdditionalOptions": ["/std:c++17", "/EHsc"]}
            },
        }
    ]
}
