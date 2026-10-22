#include "fm6_bench.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef FM6_QEMU_CI
#include "dx7_import_stress_tests.h"
#include "dx7_import_tests.h"
#include "dx7_layout_tests.h"
#include "dx7_patch_tests.h"
#include "fm6_audio_runtime_tests.h"
#include "fm6_control_runtime_dualcore_tests.h"
#include "fm6_control_runtime_tests.h"
#include "fm6_midi_router_tests.h"
#include "fm6_performance_tests.h"
#include "fm6_note_runtime_tests.h"
#include "fm6_program_queue_dualcore_tests.h"
#include "fm6_program_queue_tests.h"
#include "fm6_program_queue_wrap_tests.h"
#define FM6_BENCH_CORE 0
#else
#define FM6_BENCH_CORE 1
#endif

static void bench_task(void *arg)
{
    (void)arg;
#ifdef FM6_QEMU_CI
    const unsigned dx7_layout_failures = dx7_layout_tests_run();
    printf("QEMU_DX7_LAYOUT_GATE: %s failures=%u\n",
           dx7_layout_failures == 0u ? "PASS" : "FAIL", dx7_layout_failures);
    const unsigned dx7_patch_failures = dx7_patch_tests_run();
    printf("QEMU_DX7_PATCH_GATE: %s failures=%u\n",
           dx7_patch_failures == 0u ? "PASS" : "FAIL", dx7_patch_failures);
    const unsigned dx7_import_failures = dx7_import_tests_run();
    printf("QEMU_DX7_IMPORT_GATE: %s failures=%u\n",
           dx7_import_failures == 0u ? "PASS" : "FAIL", dx7_import_failures);
    const unsigned dx7_import_stress_failures = dx7_import_stress_tests_run();
    printf("QEMU_DX7_IMPORT_STRESS_GATE: %s failures=%u\n",
           dx7_import_stress_failures == 0u ? "PASS" : "FAIL", dx7_import_stress_failures);
    const unsigned fm6_program_queue_failures = fm6_program_queue_tests_run();
    printf("QEMU_FM6_PROGRAM_QUEUE_GATE: %s failures=%u\n",
           fm6_program_queue_failures == 0u ? "PASS" : "FAIL", fm6_program_queue_failures);
    const unsigned fm6_program_queue_dualcore_failures = fm6_program_queue_dualcore_tests_run();
    printf("QEMU_FM6_PROGRAM_QUEUE_DUALCORE_GATE: %s failures=%u\n",
           fm6_program_queue_dualcore_failures == 0u ? "PASS" : "FAIL", fm6_program_queue_dualcore_failures);
    const unsigned fm6_program_queue_wrap_failures = fm6_program_queue_wrap_tests_run();
    printf("QEMU_FM6_PROGRAM_QUEUE_WRAP_GATE: %s failures=%u\n",
           fm6_program_queue_wrap_failures == 0u ? "PASS" : "FAIL", fm6_program_queue_wrap_failures);
    const unsigned fm6_audio_runtime_failures = fm6_audio_runtime_tests_run();
    printf("QEMU_FM6_AUDIO_RUNTIME_GATE: %s failures=%u\n",
           fm6_audio_runtime_failures == 0u ? "PASS" : "FAIL", fm6_audio_runtime_failures);
    const unsigned fm6_note_runtime_failures = fm6_note_runtime_tests_run();
    printf("QEMU_FM6_NOTE_RUNTIME_GATE: %s failures=%u\n",
           fm6_note_runtime_failures == 0u ? "PASS" : "FAIL", fm6_note_runtime_failures);
    const unsigned fm6_control_runtime_failures = fm6_control_runtime_tests_run();
    printf("QEMU_FM6_CONTROL_RUNTIME_GATE: %s failures=%u\n",
           fm6_control_runtime_failures == 0u ? "PASS" : "FAIL", fm6_control_runtime_failures);
    const unsigned fm6_control_runtime_dualcore_failures = fm6_control_runtime_dualcore_tests_run();
    printf("QEMU_FM6_CONTROL_RUNTIME_DUALCORE_GATE: %s failures=%u\n",
           fm6_control_runtime_dualcore_failures == 0u ? "PASS" : "FAIL", fm6_control_runtime_dualcore_failures);
    const unsigned fm6_midi_router_failures = fm6_midi_router_tests_run();
    printf("QEMU_FM6_MIDI_ROUTER_GATE: %s failures=%u\n",
           fm6_midi_router_failures == 0u ? "PASS" : "FAIL", fm6_midi_router_failures);
    const unsigned fm6_performance_failures = fm6_performance_tests_run();
    printf("QEMU_FM6_PERFORMANCE_GATE: %s failures=%u\n",
           fm6_performance_failures == 0u ? "PASS" : "FAIL", fm6_performance_failures);
#endif
    fm6_bench_run();
    vTaskDelete(NULL);
}

void app_main(void)
{
    xTaskCreatePinnedToCore(bench_task, "fm6_bench", 8192, NULL, 20, NULL, FM6_BENCH_CORE);
}
