#include "fm6_bench.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
        0);
}
