idf_component_register(
    SRCS "fm6_note_runtime.c"
    INCLUDE_DIRS "."
    REQUIRES fm6_port
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
