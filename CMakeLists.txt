cmake_minimum_required(VERSION 3.15)
project(bntech_virtual_scanner LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 17)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_DIR win64)
    set(TWAIN_DIR twain_64/bntech)
else()
    set(ARCH_DIR win32)
    set(TWAIN_DIR twain_32/bntech)
endif()
add_library(${PROJECT_NAME} SHARED
    src/capability.cpp src/ds_entry.cpp
    src/twain_data_source.cpp src/unit_convert.cpp
    src/virtual_scanner.cpp res/twain_ds.rc)
target_include_directories(${PROJECT_NAME} PRIVATE src pub/external/include)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /FIplatform.h)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -include ${CMAKE_SOURCE_DIR}/src/platform.h)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".ds")
if(MSVC)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/pub/external/lib/${ARCH_DIR}/FreeImage.lib)
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/pub/external/bin/${ARCH_DIR}/FreeImage.lib)
    endif()
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "/EXPORT:DS_Entry")
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/pub/external/bin/${ARCH_DIR}/FreeImage.a)
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wl,--kill-at")
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE shlwapi)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/pub/external/bin/${ARCH_DIR}/FreeImage.dll $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/res/TWAIN_logo.png $<TARGET_FILE_DIR:${PROJECT_NAME}>)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${TWAIN_DIR})
install(FILES ${CMAKE_SOURCE_DIR}/pub/external/bin/${ARCH_DIR}/FreeImage.dll
    ${CMAKE_SOURCE_DIR}/res/TWAIN_logo.png DESTINATION ${TWAIN_DIR})