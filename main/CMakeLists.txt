idf_component_register(
    SRCS
        "app_main.c"
        "bench_stats.c"
        "fm6_bench.c"
    INCLUDE_DIRS "."
    REQUIRES fm6_port esp_hw_support
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2)

if(DEFINED ENV{FM6_QEMU_CI})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FM6_QEMU_CI=1)
endif()
