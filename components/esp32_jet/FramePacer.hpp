#pragma once
#include "FrameSchedule.hpp"
#include <atomic>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_freertos_hooks.h>

// Owned by the frame task for its lifetime. DMA uses that task's default
// notification slot, so the timer has a separate semaphore.
class FramePacer {
public:
    void init() {
        wake = xSemaphoreCreateBinary();
        configASSERT(wake);
        esp_timer_create_args_t args{};
        args.callback = [](void* arg) {
            xSemaphoreGive(static_cast<FramePacer*>(arg)->wake);
        };
        args.arg = this;
        args.name = "frame_pacing";
        ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
        const int64_t now = esp_timer_get_time();
        for (unsigned i = 0; i < portNUM_PROCESSORS; ++i) {
            ESP_ERROR_CHECK(esp_register_freertos_idle_hook_for_cpu(idleHook, i));
            lastIdle[i] = now;
        }
        schedule.reset(now);
    }

    void waitNext() {
        int64_t now = esp_timer_get_time();
        bool idleStarved = false;
        for (unsigned i = 0; i < portNUM_PROCESSORS; ++i) {
            if (idleSeen[i].exchange(false, std::memory_order_relaxed)) lastIdle[i] = now;
            idleStarved |= now - lastIdle[i] >= 500000;
        }
        // Normally scanout and the deadline wait already allow idle to run.
        // Sustained overruns/polling fallback must still service both idle
        // tasks, which are watched by the task watchdog. Keep it enabled.
        if (idleStarved) {
            ++idleRecoveryYields;
            vTaskDelay(1);
            now = esp_timer_get_time();
        }
        const int64_t target = schedule.nextFrame(now);
        const int64_t remaining = target - esp_timer_get_time();
        if (remaining > 0) {
            ESP_ERROR_CHECK(esp_timer_start_once(timer, remaining));
            xSemaphoreTake(wake, portMAX_DELAY);
        }
    }

    unsigned idleRecoveryYields = 0;
private:
    static inline std::atomic<bool> idleSeen[portNUM_PROCESSORS]{};
    static bool idleHook() {
        idleSeen[xPortGetCoreID()].store(true, std::memory_order_relaxed);
        return true;
    }
    int64_t lastIdle[portNUM_PROCESSORS]{};
    FrameSchedule schedule;
    esp_timer_handle_t timer = nullptr;
    SemaphoreHandle_t wake = nullptr;
};
