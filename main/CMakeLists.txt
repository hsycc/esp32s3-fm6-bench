idf_component_register(
    SRCS
        "app_main.c"
        "bench_stats.c"
        "fm6_bench.c"
    INCLUDE_DIRS "."
    REQUIRES fm6_port esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
