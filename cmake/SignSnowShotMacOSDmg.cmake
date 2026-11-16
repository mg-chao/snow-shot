# CPack runs this before computing checksums and copying the final packages.
foreach(_package IN LISTS CPACK_PACKAGE_FILES)
    if(_package MATCHES "\\.dmg$")
        execute_process(COMMAND /usr/bin/codesign --force --sign - "${_package}"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND /usr/bin/codesign --verify --strict "${_package}"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND /usr/bin/hdiutil verify "${_package}"
            COMMAND_ERROR_IS_FATAL ANY)
        # Audit the app *inside* the finished DMG, including its actual previous
        # release DR. Signing the container alone does not preserve TCC identity.
        set(_previous_dmg "$ENV{SNOW_MACOS_PREVIOUS_FULL_DMG}")
        if(CPACK_PACKAGE_NAME STREQUAL "snow-shot-mini")
            set(_previous_dmg "$ENV{SNOW_MACOS_PREVIOUS_MINI_DMG}")
        endif()
        set(_previous_arguments)
        if(NOT _previous_dmg STREQUAL "")
            list(APPEND _previous_arguments --previous-dmg "${_previous_dmg}")
        endif()
        execute_process(COMMAND "${CPACK_SNOW_SHOT_SIGNING_PYTHON}"
            "${CPACK_SNOW_SHOT_SIGNING_TOOL}" audit-release "${_package}"
            --product "${CPACK_PACKAGE_NAME}" --version "${CPACK_SNOW_SHOT_RELEASE_VERSION}"
            --architecture "${CPACK_SNOW_SHOT_RELEASE_ARCHITECTURE}" ${_previous_arguments}
            --report "${CPACK_PACKAGE_DIRECTORY}/${CPACK_PACKAGE_FILE_NAME}.signing.json"
            COMMAND_ERROR_IS_FATAL ANY)
    endif()
endforeach()
