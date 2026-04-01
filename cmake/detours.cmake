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