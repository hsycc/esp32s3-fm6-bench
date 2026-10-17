idf_component_register(
    SRCS "dx7_patch.c"
    INCLUDE_DIRS "."
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
