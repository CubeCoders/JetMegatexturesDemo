#include "Runtime.hpp"
#include "Display.hpp"
#include "FramePacer.hpp"
#include "PerformanceOverlay.hpp"
#include <esp_heap_caps.h>
#include <cstdio>
#include <algorithm>
#include <vector>
#if CONFIG_IDF_TARGET_ESP32S3
#include "S3RasterWorker.hpp"
#endif

// A shared framebuffer would race with concurrent scanout. Deliberately reject
// unsupported layouts rather than silently corrupting output after a config edit.
static_assert(FIELD_BUFFERS && !CHECKERBOARD_MODE,
              "This runtime requires interlaced field buffers, without checkerboarding");
static_assert(Display::RENDER_FIELDS == 2 && Display::RENDER_HEIGHT % 2 == 0
              && Display::RENDER_WIDTH % 2 == 0, "Field layout requires even dimensions");

namespace Esp32Jet {
namespace {
Renderer::Scene* scene = nullptr;
Init initScene = nullptr;
Update updateScene = nullptr;
Update afterRenderScene = nullptr;
RenderEffects renderEffectsScene = nullptr;
Renderer::Scene::RasterExecutor executor = nullptr;
TaskHandle_t renderTask = nullptr;
SemaphoreHandle_t renderDone = nullptr;
uint16_t* renderBuffer = nullptr;
float elapsed = 1.0f / 60.0f;
int64_t renderUs = 0, rasterUs = 0;
void measuredRaster(Renderer::Scene& target) {
    const int64_t start = esp_timer_get_time();
    if (executor) executor(target);
    else target.rasterizeBand(0, Display::RENDER_HEIGHT);
    rasterUs = esp_timer_get_time() - start;
}

// Copy mutable sprite and material state before waking the render task.
// Textures/pixel data are borrowed and must not be modified during scanout.
struct Overlays {
    std::vector<Renderer::Sprite2D> sprites;
    std::vector<Renderer::Material> materials;
    std::vector<Renderer::Sprite2D*> pointers;
    void capture() {
        const auto& source = scene->getSprites();
        sprites.resize(source.size());
        materials.resize(source.size());
        pointers.clear();
        for (size_t i = 0; i < source.size(); ++i) {
            if (!source[i]) continue;
            sprites[i] = *source[i];
            if (source[i]->material) {
                materials[i] = *source[i]->material;
                sprites[i].material = &materials[i];
            }
            pointers.push_back(&sprites[i]);
        }
        std::stable_sort(pointers.begin(), pointers.end(), [](auto* a, auto* b) {
            return a->zOrder < b->zOrder;
        });
    }
};

void render(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (updateScene) updateScene(elapsed);
        const int64_t start = esp_timer_get_time();
        scene->setFramebuffer(renderBuffer);
        scene->render(measuredRaster);
        if (renderEffectsScene)
            scene->lastFrameRasterizedTriangles += renderEffectsScene(*scene);
        renderUs = esp_timer_get_time() - start;
        if (afterRenderScene) afterRenderScene(elapsed);
        xSemaphoreGive(renderDone);
    }
}

void run(void*) {
    // Init and submission share core 0 with the S3 completion interrupt.
    if (!Display::init()) {
        std::printf("Display/buffer initialization failed; rendering stopped.\n");
        vTaskDelete(nullptr);
        return;
    }
    scene = new Renderer::Scene(Display::initialFramebuffer(), Display::zBuffer(),
                                Display::RENDER_WIDTH, Display::RENDER_HEIGHT);
    scene->getRenderer()->interlacedMode = true;
    initScene(*scene);
    configASSERT(scene->getCamera());
    static PerformanceOverlay stats;
    stats.attach(*scene, Display::RENDER_WIDTH);
    renderDone = xSemaphoreCreateBinary();
    configASSERT(renderDone);
#if CONFIG_IDF_TARGET_ESP32S3
    if (Display::supportsRasterWorker() && S3RasterWorker::init(Display::RENDER_HEIGHT))
        executor = S3RasterWorker::execute;
#endif
    const BaseType_t created = xTaskCreatePinnedToCore(render, "JetRender", 8192,
                                                       nullptr, 1, &renderTask, 1);
    configASSERT(created == pdPASS);
    std::printf("Jet ready: 480x320, 60 fields/s, raster cores %s, internal free %u, PSRAM free %u\n",
        executor ? "1+0" : "1", unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
    FramePacer pacer;
    pacer.init();
    Overlays overlays;
    int64_t previousStart = 0, intervalSum = 0, renderSum = 0, scanoutSum = 0, rasterSum = 0;
    unsigned intervals = 0, samples = 0;
    for (;;) {
        const int64_t start = esp_timer_get_time();
        if (previousStart) {
            elapsed = float(start - previousStart) / 1000000.0f;
            intervalSum += start - previousStart;
            ++intervals;
        }
        previousStart = start;
        stats.tick(start, unsigned(scene->lastFrameRasterizedTriangles), renderUs);
        const auto frame = Display::beginFrame();
        renderBuffer = frame.renderBuffer;
        // Previous-field reads are immutable even while both cores rasterize.
        // This also preserves the safe path for future WATER_REFLECT examples.
        scene->getRenderer()->reflectBuffer = frame.displayBuffer;
        overlays.capture();
        xTaskNotifyGive(renderTask);
        const int64_t scanoutStart = esp_timer_get_time();
        Display::pushFrame(frame.displayBuffer, frame.displayField,
                           overlays.pointers.data(), int(overlays.pointers.size()));
        scanoutSum += esp_timer_get_time() - scanoutStart;
        xSemaphoreTake(renderDone, portMAX_DELAY);
        renderSum += renderUs;
        rasterSum += rasterUs;
        if (++samples == 60) {
            std::printf("Cadence %.2f fields/s; render %.2f ms (setup %.2f, raster %.2f), scanout %.2f ms; %u tris, %u render tris/s; idle recovery %u\n",
                intervals * 1000000.0 / intervalSum,
                renderSum / 60000.0, (renderSum-rasterSum) / 60000.0, rasterSum / 60000.0, scanoutSum / 60000.0, stats.triangles(), stats.trianglesPerSecond(), pacer.idleRecoveryYields);
            intervalSum = renderSum = scanoutSum = rasterSum = 0;
            intervals = samples = 0;
        }
        pacer.waitNext();
    }
}
}

void start(Init init, Update update, Update afterRender, RenderEffects renderEffects) {
    configASSERT(init && !initScene);
    initScene = init;
    updateScene = update;
    afterRenderScene = afterRender;
    renderEffectsScene = renderEffects;
    const BaseType_t created = xTaskCreatePinnedToCore(run, "JetFrame", 8192,
                                                       nullptr, 2, nullptr, 0);
    configASSERT(created == pdPASS);
}
}
