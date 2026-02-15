cmake_minimum_required(VERSION 3.21)

project(INTEGRATEDBOW VERSION 0.0.1 LANGUAGES CXX)

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()

find_package(CommonLibSSE CONFIG REQUIRED)
add_commonlibsse_plugin(${PROJECT_NAME}
  SOURCES
    src/plugin.cpp
    src/Hooks.cpp
    src/bow_input/InputState.cpp
    src/bow_input/InputGate.cpp
    src/bow_input/HotkeyDetector.cpp
    src/bow_input/BowModeController.cpp
    src/bow_input/BowInputHandler.cpp
    src/Detours/detours.cpp
    src/Detours/disasm.cpp
    src/Detours/disolx64.cpp
    src/Detours/image.cpp
    src/Detours/modules.cpp
    src/config/BowConfig.cpp
    src/menu/BowStrings.cpp
    src/menu/UI_IntegratedBow.cpp
    src/patchs/UnMapBlock.cpp
    src/patchs/HiddenItemsPatch.cpp
    src/BowState.cpp
    src/config/SaveBowDB.cpp
    src/patchs/SkipEquipController.cpp
)

target_include_directories(${PROJECT_NAME}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_precompile_headers(${PROJECT_NAME} PRIVATE
  src/PCH.h
  src/BowState.h
  src/Hooks.h
  src/bow_input/BowInputTiming.h
  src/bow_input/InputState.h
  src/bow_input/InputGate.h
  src/bow_input/HotkeyDetector.h
  src/bow_input/BowModeController.h
  src/bow_input/BowInputHandler.h
  src/config/BowConfig.h
  src/config/BoWConfigPath.h
  src/menu/BowStrings.h
  src/menu/UI_IntegratedBow.h
  src/patchs/UnMapBlock.h
  src/patchs/HiddenItemsPatch.h
  src/config/SaveBowDB.h
  src/patchs/SkipEquipController.h
)

set_source_files_properties(
  src/Detours/detours.cpp
  src/Detours/disasm.cpp
  src/Detours/disolx64.cpp
  src/Detours/image.cpp
  src/Detours/modules.cpp
  PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON       
    COMPILE_DEFINITIONS "DETOURS_INTERNAL"  
)

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
