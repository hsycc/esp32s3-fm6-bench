idf_component_register(
    SRCS "fm6_midi_router.c"
    INCLUDE_DIRS "."
    REQUIRES fm6_control_runtime fm6_program_bank
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
