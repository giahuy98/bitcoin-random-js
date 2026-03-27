{
  "targets": [
    {
      "target_name": "bitcoin_random_js",
      "sources": [
        "addon.cpp",
        "random.cpp"
      ],
      "cflags_cc": [
        "-std=c++2a"
      ],
      "msvs_settings": {
        "VCCLCompilerTool": {
          "AdditionalOptions": [
            "/std:c++20"
          ]
        }
      },
      "xcode_settings": {
        "CLANG_CXX_LANGUAGE_STANDARD": "c++20"
      }
    }
  ]
}
