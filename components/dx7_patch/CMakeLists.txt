idf_component_register(
    SRCS
        "dx7_patch.c"
        "dx7_import.c"
    INCLUDE_DIRS "."
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
