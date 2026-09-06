vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO zlib-ng/zlib-ng
    REF "${VERSION}"
    SHA512 e2057c764f1d5aaee738edee7e977182c5b097e3c95489dcd8de813f237d92a05daaa86d68d44b331d9fec5d1802586a8f6cfb658ba849874aaa14e72a8107f5
    HEAD_REF develop
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DZLIB_ENABLE_TESTS=OFF
        -DZLIB_COMPAT=ON
        -DWITH_GZFILEOP=ON
        -DWITH_NEW_STRATEGIES=ON
        -DWITH_NATIVE_INSTRUCTIONS=OFF
    OPTIONS_RELEASE
        -DWITH_OPTIM=ON
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()

# zlib-ng follows zlib's established Windows output names in compatibility mode,
# except that static MSVC builds use zlibstatic[ d ]. Keep pkg-config in sync.
if(VCPKG_TARGET_IS_WINDOWS AND
   NOT (VCPKG_LIBRARY_LINKAGE STREQUAL static AND VCPKG_TARGET_IS_MINGW))
    if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
        set(_zlib_output_name "zlib")
    else()
        set(_zlib_output_name "zlibstatic")
    endif()
    if(NOT DEFINED VCPKG_BUILD_TYPE OR VCPKG_BUILD_TYPE STREQUAL "release")
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/lib/pkgconfig/zlib.pc"
                             " -lz" " -l${_zlib_output_name}")
    endif()
    if(NOT DEFINED VCPKG_BUILD_TYPE OR VCPKG_BUILD_TYPE STREQUAL "debug")
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/zlib.pc"
                             " -lz" " -l${_zlib_output_name}d")
    endif()
endif()

vcpkg_fixup_pkgconfig()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/ZLIB)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share"
                    "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
