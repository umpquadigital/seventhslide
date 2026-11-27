# Project Nayuki's QR Code generator, vendored unmodified (README.md). Built
# without the project's warning flags, and included as SYSTEM so its headers
# raise none in ours.
add_library(seventhslide_qrcodegen STATIC qrcodegen.cpp)
target_include_directories(seventhslide_qrcodegen SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(seventhslide_qrcodegen PUBLIC cxx_std_20)
