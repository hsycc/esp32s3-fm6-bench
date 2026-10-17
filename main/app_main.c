#include "fm6_bench.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef FM6_QEMU_CI
#define FM6_BENCH_CORE 0
#else
#define FM6_BENCH_CORE 1
#endif

static void bench_task(void *arg)
{
    (void)arg;
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
