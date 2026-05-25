cmake_minimum_required(VERSION 3.15)
project(bntech_virtual_scanner LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 17)

# Default build type if not specified.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_DIR win64)
    set(TWAIN_DIR twain_64/bntech)
else()
    set(ARCH_DIR win32)
    set(TWAIN_DIR twain_32/bntech)
endif()
add_library(${PROJECT_NAME} SHARED
    src/capability.cpp src/ds_entry.cpp
    src/localization.cpp src/settings_server.cpp src/twain_data_source.cpp
    src/unit_convert.cpp src/virtual_scanner.cpp
    res/twain_ds.rc)
target_include_directories(${PROJECT_NAME} PRIVATE src pub/external/include pub/external/FreeImage/${ARCH_DIR})
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /FIplatform.h /utf-8)
    # Debug configuration: enable debug logging and extra warnings.
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:DS_DEBUG_LOG>)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:/W4>
        $<$<CONFIG:Release>:/W3>)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -include ${CMAKE_SOURCE_DIR}/src/platform.h)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:DS_DEBUG_LOG>)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".ds")
if(MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/pub/external/FreeImage/${ARCH_DIR}/FreeImage.lib)
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/EXPORT:DS_Entry")
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/pub/external/FreeImage/${ARCH_DIR}/FreeImage.a)
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wl,--kill-at")
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE shlwapi ws2_32)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/pub/external/FreeImage/${ARCH_DIR}/FreeImage.dll $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/res/TWAIN_logo.png $<TARGET_FILE_DIR:${PROJECT_NAME}>)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${TWAIN_DIR})
install(FILES ${CMAKE_SOURCE_DIR}/pub/external/FreeImage/${ARCH_DIR}/FreeImage.dll
    ${CMAKE_SOURCE_DIR}/res/TWAIN_logo.png DESTINATION ${TWAIN_DIR})

# WiX 4 MSI package targets.
#
# A single multi-language MSI is produced per architecture. The en-us
# and zh-cn packages are built separately, then merged into one MSI
# using embedded transforms (MsiTran.exe / WiSubStg.vbs / WiLangId.vbs
# from the Windows SDK). Windows Installer auto-selects the matching
# UI language at install time.
#
# Usage after configuring the project:
#   cmake --build . --target msi64
#   cmake --build . --target msi32
#
# The current architecture target depends on bntech_virtual_scanner so the .ds,
# FreeImage.dll, and TWAIN_logo.png files are up to date before WiX runs.  The
# opposite architecture target packages the conventional build output directory
# (build/win32 or build/win64), so build that architecture first when invoking it
# from a build tree of the other architecture.
set(WIX_EXECUTABLE "wix" CACHE FILEPATH "Path to WiX Toolset 4 wix executable")
set(WIX_UI_EXTENSION "WixToolset.UI.wixext" CACHE STRING "WiX UI extension name or full path")
set(WIX_SOURCE_FILE ${CMAKE_SOURCE_DIR}/installer/bntech_virtual_scanner.wxs)
set(WIX_INSTALLER_ROOT ${CMAKE_SOURCE_DIR}/build/installer)

# Windows SDK tools needed for multi-language MSI transform embedding.
# These ship with the Windows SDK (typically under
# C:\Program Files (x86)\Windows Kits\10\bin\<version>\x86).
set(WINSDK_BIN "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x86"
    CACHE PATH "Windows SDK Tools bin directory (contains MsiTran.exe, WiSubStg.vbs, WiLangId.vbs)")
set(MSITRAN "${WINSDK_BIN}/MsiTran.exe")
set(WISUBSTG "${WINSDK_BIN}/WiSubStg.vbs")
set(WILANGID "${WINSDK_BIN}/WiLangId.vbs")

if(ARCH_DIR STREQUAL "win64")
    set(MSI64_SOURCE_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    set(MSI64_DEPENDS ${PROJECT_NAME})
else()
    set(MSI64_SOURCE_DIR ${CMAKE_SOURCE_DIR}/build/win64)
    set(MSI64_DEPENDS)
endif()

if(ARCH_DIR STREQUAL "win32")
    set(MSI32_SOURCE_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    set(MSI32_DEPENDS ${PROJECT_NAME})
else()
    set(MSI32_SOURCE_DIR ${CMAKE_SOURCE_DIR}/build/win32)
    set(MSI32_DEPENDS)
endif()

# Helper function: add a custom target that builds the en-us and zh-cn
# MSI packages, then merges them into one multi-language MSI using
# Windows SDK MsiTran + WiSubStg + WiLangId.
function(add_multilang_msi TARGET ARCH PLATFORM SOURCE_DIR DEPENDS_EXPR)
    set(OUTDIR ${WIX_INSTALLER_ROOT}/${PLATFORM})
    set(MSI_EN  ${OUTDIR}/bntech_virtual_scanner_${PLATFORM}_en_US.msi)
    set(MSI_ZH  ${OUTDIR}/bntech_virtual_scanner_${PLATFORM}_zh_CN.msi)
    set(MSI_MST ${OUTDIR}/zh_CN.mst)
    set(MSI_OUT ${OUTDIR}/bntech_virtual_scanner_${PLATFORM}.msi)

    add_custom_target(${TARGET}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTDIR}
        # Build English MSI.
        COMMAND ${WIX_EXECUTABLE} build ${WIX_SOURCE_FILE}
            -arch ${ARCH}
            -d Platform=${PLATFORM}
            -d SourceDir=${SOURCE_DIR}
            -d ProjectDir=${CMAKE_SOURCE_DIR}/
            -d AppLanguage=en_US
            -culture en-us
            -ext ${WIX_UI_EXTENSION}
            -o ${MSI_EN}
        # Build Simplified Chinese MSI.
        COMMAND ${WIX_EXECUTABLE} build ${WIX_SOURCE_FILE}
            -arch ${ARCH}
            -d Platform=${PLATFORM}
            -d SourceDir=${SOURCE_DIR}
            -d ProjectDir=${CMAKE_SOURCE_DIR}/
            -d AppLanguage=zh_CN
            -culture zh-cn
            -ext ${WIX_UI_EXTENSION}
            -o ${MSI_ZH}
        # Generate language transform from the two MSI files.
        COMMAND "${MSITRAN}" -g "${MSI_EN}" "${MSI_ZH}" "${MSI_MST}"
        # Use the English MSI as base (copy to final name).
        COMMAND ${CMAKE_COMMAND} -E copy "${MSI_EN}" "${MSI_OUT}"
        # Embed the zh-CN transform (LCID 2052) into the final MSI.
        COMMAND cscript //Nologo "${WISUBSTG}" "${MSI_OUT}" "${MSI_MST}" 2052
        # Update MSI summary to declare support for both languages (1033,2052).
        COMMAND cscript //Nologo "${WILANGID}" "${MSI_OUT}" Package 1033,2052
        # Clean up intermediate single-language MSIs and transform file.
        COMMAND ${CMAKE_COMMAND} -E rm -f "${MSI_EN}" "${MSI_ZH}" "${MSI_MST}"
        DEPENDS ${DEPENDS_EXPR} ${WIX_SOURCE_FILE}
        COMMENT "Building multi-language ${ARCH} MSI with WiX (en-us + zh-cn embedded transforms)"
        VERBATIM
    )
endfunction()

add_multilang_msi(msi64 x64 x64 "${MSI64_SOURCE_DIR}" "${MSI64_DEPENDS}")
add_multilang_msi(msi32 x86 win32 "${MSI32_SOURCE_DIR}" "${MSI32_DEPENDS}")