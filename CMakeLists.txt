cmake_minimum_required(VERSION 3.21)

project(INTEGRATEDBOW VERSION 1.5 LANGUAGES CXX)

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()

set(BUILD_TESTS OFF CACHE BOOL "Build unit tests for CommonLibVR." FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "Disable Skyrim VR in CommonLibVR-ng" FORCE)

add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibVR-ng
    ${CMAKE_BINARY_DIR}/_deps/CommonLibVR-ng-build
)

get_target_property(COMMONLIB_SRC_DIR CommonLibSSE SOURCE_DIR)
include(${COMMONLIB_SRC_DIR}/cmake/CommonLibSSE.cmake)

include(cmake/headerlist.cmake)
include(cmake/sourcelist.cmake)

add_commonlibsse_plugin(${PROJECT_NAME} SOURCES ${headers} ${sources})

target_include_directories(${PROJECT_NAME}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

find_package(imgui CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui xinput.lib)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

include(cmake/detours.cmake)

if(DEFINED OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")

    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
            VERBATIM
        )
    endif()
endif()
