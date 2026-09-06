if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "SNOW_IMAGE_BUILD_FUZZERS requires Clang with libFuzzer.")
endif()

add_executable(snow_image_decode_fuzzer decode_fuzzer.cpp)
target_link_libraries(snow_image_decode_fuzzer PRIVATE snow_image::snow_image)
target_compile_features(snow_image_decode_fuzzer PRIVATE cxx_std_20)
target_compile_options(snow_image_decode_fuzzer PRIVATE
    -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
)
target_link_options(snow_image_decode_fuzzer PRIVATE
    -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
)
