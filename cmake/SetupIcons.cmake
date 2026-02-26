# cmake/SetupIcons.cmake

function(setup_app_icons TARGET_NAME ICON_DIR ICON_BASENAME TEMPLATE_DIR)
    if(WIN32)
        # Generate RC file for Windows build
        set(RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_icon.rc")
        file(WRITE "${RC_FILE}" "IDI_ICON1 ICON \"${ICON_DIR}/${ICON_BASENAME}.ico\"\n")
        
        # Attach the generated RC file to the target
        target_sources(${TARGET_NAME} PRIVATE "${RC_FILE}")
        
    elseif(UNIX AND NOT APPLE)
        # Setup paths for Linux desktop integration
        set(PNG_FILE "${ICON_DIR}/${ICON_BASENAME}.png")
        set(DESKTOP_TEMPLATE "${TEMPLATE_DIR}/linux_shortcut.desktop.in")
        set(GENERATED_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.desktop")
        
        # Generate the specific .desktop file for this project
        if(EXISTS "${DESKTOP_TEMPLATE}")
            configure_file("${DESKTOP_TEMPLATE}" "${GENERATED_DESKTOP}" @ONLY)
        endif()
        
        # Install the PNG icon with a target-specific name to prevent collisions
        if(EXISTS "${PNG_FILE}")
            install(FILES "${PNG_FILE}" 
                    DESTINATION "share/icons/hicolor/256x256/apps" 
                    RENAME "${TARGET_NAME}.png") # <-- Use TARGET_NAME here
        endif()
        
        # Install the generated .desktop entry
        if(EXISTS "${GENERATED_DESKTOP}")
            install(FILES "${GENERATED_DESKTOP}" 
                    DESTINATION "share/applications")
        endif()
    endif()
endfunction()