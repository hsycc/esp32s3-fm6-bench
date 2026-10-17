#include "fm6_bench.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef FM6_QEMU_CI
#include "dx7_patch_tests.h"
#define FM6_BENCH_CORE 0
#else
#define FM6_BENCH_CORE 1
#endif

static void bench_task(void *arg)
{
    (void)arg;
#ifdef FM6_QEMU_CI
    const unsigned dx7_failures = dx7_patch_tests_run();
    printf("QEMU_DX7_PATCH_GATE: %s failures=%u\n",
           dx7_failures == 0u ? "PASS" : "FAIL",
           dx7_failures);
#endif
    fm6_bench_run();
    vTaskDelete(NULL);
}

void app_main(void)
{
    xTaskCreatePinnedToCore(
        bench_task,
        "fm6_bench",
        8192,
        NULL,
        20,
        NULL,
        FM6_BENCH_CORE);
}
