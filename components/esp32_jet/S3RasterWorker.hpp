#pragma once
#include "Scene.hpp"
#include <esp_timer.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <vector>
#include <algorithm>

// The display producer runs at priority 2; this worker uses core 0 only
// while that producer is asleep waiting for scanline DMA completions.
namespace S3RasterWorker {
inline TaskHandle_t task = nullptr;
inline SemaphoreHandle_t done = nullptr;
inline Renderer::Scene* job = nullptr;
inline std::vector<uint8_t> upperFlags, lowerFlags;
inline int split = 160;
inline int height = 320;
inline int64_t lowerTime = 0;

inline void execute(Renderer::Scene& scene) {
    // Current-frame water reflections may read another band's pixels.
    // Frame setup/material updates are serial; only these row-disjoint
    // raster passes overlap, including their full-height depth rows. FreeRTOS saves S3 cop_ai (CP3) SIMD state when
    // the display producer preempts this worker.
    if (!task || !scene.getRenderer()->reflectBuffer || !scene.lastFrameDrawnTriangles) {
        scene.rasterizeBand(0, height);
        return;
    }
    upperFlags.assign(scene.lastFrameDrawnTriangles, 0);
    lowerFlags.assign(scene.lastFrameDrawnTriangles, 0);
    job = &scene;
    int64_t start = esp_timer_get_time();
    xTaskNotifyGive(task);
    scene.rasterizeBand(0, split, upperFlags.data());
    int64_t upperTime = esp_timer_get_time() - start;
    xSemaphoreTake(done, portMAX_DELAY);
    static uint32_t frames = 0;
    // Move a few rows toward the busier core; display preemption is included
    // in the helper's elapsed time. Keep the boundary on a field-row pair.
    if (++frames % 4 == 0) {
        if (lowerTime > upperTime + 250) split = std::min(height - 32, split + 4);
        else if (upperTime > lowerTime + 250) split = std::max(32, split - 4);
    }
    int count = 0;
    for (size_t i = 0; i < upperFlags.size(); ++i) count += (upperFlags[i] | lowerFlags[i]) != 0;
    scene.lastFrameRasterizedTriangles = count;
}

inline bool init(int renderHeight) {
    height = renderHeight;
    split = (height / 2) & ~1;
    done = xSemaphoreCreateBinary();
    if (!done) return false;
    if (xTaskCreatePinnedToCore([](void*) {
        for (;;) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            int64_t start = esp_timer_get_time();
            job->rasterizeBand(split, height, lowerFlags.data());
            lowerTime = esp_timer_get_time() - start;
            xSemaphoreGive(done);
        }
    }, "RasterWorker", 8192, nullptr, 1, &task, 0) != pdPASS) {
        vSemaphoreDelete(done); done = nullptr; return false;
    }
    return true;
}
}
