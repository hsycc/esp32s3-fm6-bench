idf_component_register(
    SRCS
        "app_main.c"
        "bench_stats.c"
        "fm6_bench.c"
        "fm6_feature_tests.c"
        "fm6_event_stress.c"
        "fm6_program_queue_tests.c"
        "fm6_program_queue_dualcore_tests.c"
        "fm6_program_queue_wrap_tests.c"
        "fm6_audio_runtime_tests.c"
        "fm6_note_runtime_tests.c"
        "fm6_control_runtime_tests.c"
        "fm6_control_runtime_dualcore_tests.c"
        "fm6_midi_router_tests.c"
        "fm6_performance_tests.c"
        "fm6_pcm_output_tests.c"
        "dx7_patch_tests.c"
        "dx7_import_tests.c"
        "dx7_import_stress_tests.c"
        "dx7_layout_tests.c"
    INCLUDE_DIRS "."
    REQUIRES fm6_port fm6_program_queue fm6_audio_runtime fm6_note_runtime fm6_control_runtime fm6_program_bank fm6_midi_router fm6_pcm_output dx7_patch esp_hw_support
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2)

if(DEFINED ENV{FM6_QEMU_CI})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FM6_QEMU_CI=1)
endif()
