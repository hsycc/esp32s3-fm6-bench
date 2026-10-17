idf_component_register(
    SRCS "fm6_port.c"
    INCLUDE_DIRS "."
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
