# cmake/SetupIcons.cmake

function(setup_app_icons TARGET_NAME ICON_DIR ICON_BASENAME TEMPLATE_DIR)
    set(PNG_FILE "${ICON_DIR}/${ICON_BASENAME}_256.png")

    if(NOT DEFINED PROJECT_VERSION_MAJOR)
        set(PROJECT_VERSION_MAJOR 0)
        set(PROJECT_VERSION_MINOR 1)
        set(PROJECT_VERSION_PATCH 0)
    endif()

    set(VERSION_COMMA "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
    set(VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

    # Pass the version string to C++ code as a macro (APP_VERSION)
    target_compile_definitions(${TARGET_NAME} PRIVATE APP_VERSION="${VERSION_STRING}")

    # 1. Generate Qt Resource File (.qrc) for runtime window icon
    if(EXISTS "${PNG_FILE}")
        set(QRC_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_resources.qrc")
        file(WRITE "${QRC_FILE}"
            "<!DOCTYPE RCC><RCC version=\"1.0\">\n"
            "<qresource prefix=\"/\">\n"
            "    <file alias=\"app_icon.png\">${PNG_FILE}</file>\n"
            "</qresource>\n"
            "</RCC>\n"
        )
        target_sources(${TARGET_NAME} PRIVATE "${QRC_FILE}")
    endif()

    # 2. Platform-specific external icons and metadata
    if(WIN32)
        set(RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_icon.rc")

        # Write icon and version metadata for Windows executable properties
        file(WRITE "${RC_FILE}"
            "IDI_ICON1 ICON \"${ICON_DIR}/${ICON_BASENAME}.ico\"\n"
            "1 VERSIONINFO\n"
            "FILEVERSION ${VERSION_COMMA}\n"
            "PRODUCTVERSION ${VERSION_COMMA}\n"
            "BEGIN\n"
            "  BLOCK \"StringFileInfo\"\n"
            "  BEGIN\n"
            "    BLOCK \"040904E4\"\n"
            "    BEGIN\n"
            "      VALUE \"FileDescription\", \"${TARGET_NAME} Application\"\n"
            "      VALUE \"FileVersion\", \"${VERSION_STRING}\"\n"
            "      VALUE \"ProductName\", \"${TARGET_NAME}\"\n"
            "      VALUE \"ProductVersion\", \"${VERSION_STRING}\"\n"
            "    END\n"
            "  END\n"
            "  BLOCK \"VarFileInfo\"\n"
            "  BEGIN\n"
            "    VALUE \"Translation\", 0x409, 1252\n"
            "  END\n"
            "END\n"
        )
        target_sources(${TARGET_NAME} PRIVATE "${RC_FILE}")

    elseif(UNIX AND NOT APPLE)
        set(DESKTOP_TEMPLATE "${TEMPLATE_DIR}/linux_shortcut.desktop.in")
        set(GENERATED_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.desktop")

        if(EXISTS "${DESKTOP_TEMPLATE}")
            configure_file("${DESKTOP_TEMPLATE}" "${GENERATED_DESKTOP}" @ONLY)
        endif()

        if(EXISTS "${PNG_FILE}")
            install(FILES "${PNG_FILE}"
                    DESTINATION "share/icons/hicolor/256x256/apps"
                    RENAME "${TARGET_NAME}.png")
        endif()

        if(EXISTS "${GENERATED_DESKTOP}")
            install(FILES "${GENERATED_DESKTOP}"
                    DESTINATION "share/applications")
        endif()
    endif()
endfunction()
