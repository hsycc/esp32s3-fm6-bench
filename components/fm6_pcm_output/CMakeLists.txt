idf_component_register(
    SRCS "fm6_pcm_output.c"
    INCLUDE_DIRS "."
    REQUIRES fm6_control_runtime
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
