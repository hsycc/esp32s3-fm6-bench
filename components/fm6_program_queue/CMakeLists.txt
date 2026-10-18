idf_component_register(
    SRCS "fm6_program_queue.c"
    INCLUDE_DIRS "."
    REQUIRES fm6_port dx7_patch
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
