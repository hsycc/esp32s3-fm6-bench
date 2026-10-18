idf_component_register(
    SRCS "fm6_audio_runtime.c"
    INCLUDE_DIRS "."
    REQUIRES fm6_program_queue fm6_port
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
