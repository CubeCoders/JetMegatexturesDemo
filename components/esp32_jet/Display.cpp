#include "Display.hpp"

#include <stdio.h>
#include <string.h>

#include <esp_heap_caps.h>
#include <driver/gpio.h>

#include "JetConfig.hpp"
// For ZBUFFER_STRIDE() — keeps the depth-buffer allocation in sync with the
// rasterizer's expected stride (half-width when HALF_WIDTH_BUFFERS is on,
// per-pixel otherwise).
#include "Renderer.hpp"
#include "Material.hpp"
#include "Texture.hpp"
#include "PixelOps.hpp"
#include "BlendSpans.hpp"

#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_cache.h"
#include "Board.hpp"
#elif CONFIG_IDF_TARGET_ESP32S3
#include "Board.hpp"
#include "S3Scanout.hpp"
#endif

namespace Display {

namespace {

LGFX tft = LGFX();
#if CONFIG_IDF_TARGET_ESP32S3
S3Scanout scanout;
bool queuedScanout = false;
#endif

// Primary framebuffer (non-FIELD_BUFFERS path) or initial placeholder for
// Scene construction (FIELD_BUFFERS alternates every frame so this pointer
// is only meaningful before the first beginFrame()).
uint16_t* framebufferA = nullptr;

// Interlaced field buffers — each holds half the scanlines packed
// contiguously. Written/read alternately by CPU and DMA.
#if FIELD_BUFFERS
uint16_t* evenFieldBuffer = nullptr;
uint16_t* oddFieldBuffer  = nullptr;
#endif

// Ping-pong line buffers: fill buffer A while DMA pushes buffer B.
uint16_t* lineBufferA = nullptr;
uint16_t* lineBufferB = nullptr;

uint16_t* depthBuffer = nullptr;

#ifdef TDeck
constexpr gpio_num_t TDECK_POWERON_GPIO = GPIO_NUM_10;

bool tdeck_power_on() {
    gpio_config_t cfg = {
        .pin_bit_mask = BIT64(TDECK_POWERON_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK) return false;
    return gpio_set_level(TDECK_POWERON_GPIO, 1) == ESP_OK;
}
#endif

bool allocateBuffers() {
#if FIELD_BUFFERS
#if HALF_WIDTH_BUFFERS
    size_t fbBytes = (SCREEN_WIDTH / 2) * (SCREEN_HEIGHT / 2) * sizeof(uint16_t);
#else
    size_t fbBytes = SCREEN_WIDTH * (SCREEN_HEIGHT / 2) * sizeof(uint16_t);
#endif
    evenFieldBuffer = (uint16_t*)heap_caps_aligned_alloc(16, fbBytes, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    oddFieldBuffer  = (uint16_t*)heap_caps_aligned_alloc(16, fbBytes, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    if (!evenFieldBuffer || !oddFieldBuffer) {
        printf("Failed to allocate field buffers (wanted 2x %u bytes)\n", (unsigned)fbBytes);
        return false;
    }
    memset(evenFieldBuffer, 0, fbBytes);
    memset(oddFieldBuffer,  0, fbBytes);
    framebufferA = evenFieldBuffer;
#else
#if HALF_WIDTH_BUFFERS
    size_t fbBytes = (SCREEN_WIDTH / 2) * SCREEN_HEIGHT * sizeof(uint16_t);
    framebufferA = (uint16_t*)heap_caps_aligned_alloc(16, fbBytes, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
#else
    size_t fbBytes = SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t);
#if CONFIG_IDF_TARGET_ESP32P4
    // On P4 the display loop copies through ping-pong DMA buffers, so the
    // main framebuffer does not need to be DMA-capable — keep internal SRAM
    // free by preferring MALLOC_CAP_INTERNAL, then fall back to anywhere.
    framebufferA = (uint16_t*)heap_caps_aligned_alloc(16, fbBytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!framebufferA) {
        framebufferA = (uint16_t*)heap_caps_aligned_alloc(16, fbBytes, MALLOC_CAP_8BIT);
    }
#else
    framebufferA = (uint16_t*)heap_caps_aligned_alloc(16, fbBytes, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
#endif
#endif
    if (!framebufferA) {
        printf("Failed to allocate framebuffer.\n");
        return false;
    }
    memset(framebufferA, 0, fbBytes);
#endif

    lineBufferA = (uint16_t*)heap_caps_aligned_alloc(16, SCREEN_WIDTH * sizeof(uint16_t), MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    if (!lineBufferA) { printf("Failed to allocate lineBufferA\n"); return false; }
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S3
    lineBufferB = (uint16_t*)heap_caps_aligned_alloc(16, SCREEN_WIDTH * sizeof(uint16_t), MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    if (!lineBufferB) { printf("Failed to allocate lineBufferB\n"); return false; }
#endif
#if Z_BUFFERING
    // Stride matches the renderer's depth-buffer layout: half-width when
    // HALF_WIDTH_BUFFERS is enabled, per-pixel otherwise.
    // Depth is CPU-only, never a DMA source. Keep scarce internal SRAM for
    // the field buffers, row queue and tasks; use PSRAM when it is present.
    const size_t depthBytes = ZBUFFER_STRIDE(SCREEN_WIDTH) * SCREEN_HEIGHT * sizeof(uint16_t);
    depthBuffer = (uint16_t*)heap_caps_aligned_alloc(16, depthBytes, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (!depthBuffer)
        depthBuffer = (uint16_t*)heap_caps_aligned_alloc(16, depthBytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!depthBuffer) return false;
#endif
    return true;
}

} // namespace

bool init() {

#ifdef TDeck
    tdeck_power_on();
#endif

    tft.init();
    tft.setRotation(3);
    tft.fillScreen(TFT_BLACK);
    tft.setSwapBytes(true);
    printf("LCD initialized\n");

    if (!allocateBuffers()) return false;
    // Prime LGFX's screen-width DMA descriptor before the queued S3 ISR uses it.
    // The panel remains black until the first completed cube field is ready.
    memset(lineBufferA, 0, SCREEN_WIDTH * sizeof(uint16_t));
    tft.startWrite();
    tft.setAddrWindow(0, 0, SCREEN_WIDTH, 1);
    tft.writePixelsDMA(lineBufferA, SCREEN_WIDTH, false);
    tft.waitDMA();
    tft.endWrite();
#if CONFIG_IDF_TARGET_ESP32S3 && JET32_S3_PARALLEL_RASTER
    queuedScanout = scanout.init(&tft, lineBufferA, lineBufferB);
#endif
    return true;
}

uint16_t* initialFramebuffer() { return framebufferA; }
uint16_t* zBuffer()            { return depthBuffer; }
bool supportsRasterWorker() {
#if CONFIG_IDF_TARGET_ESP32S3 && FIELD_BUFFERS && SSR_FIELD_REFLECT
    return queuedScanout;
#else
    return false;
#endif
}

Frame beginFrame() {
    static int frameIndex = 0;
    const int n = frameIndex++;
    Frame f{};
#if FIELD_BUFFERS
    // The renderer advances one interlaced field per render() call starting
    // with even, so displayBuffer always points at the OTHER buffer — the
    // one the previous frame finished writing. CPU and DMA therefore never
    // touch the same memory region concurrently.
    const bool evenForRender = (n % 2 == 0);
    f.renderBuffer  = evenForRender ? evenFieldBuffer : oddFieldBuffer;
    f.displayBuffer = evenForRender ? oddFieldBuffer  : evenFieldBuffer;
    f.displayField  = n % RENDER_FIELDS;
#else
    // Shared-buffer fallback: display the field OPPOSITE to the one the
    // parallel render task is writing, otherwise both land on the same
    // parity and race.
    f.renderBuffer  = framebufferA;
    f.displayBuffer = framebufferA;
    f.displayField  = n % RENDER_FIELDS;
#endif
    return f;
}

#if CONFIG_IDF_TARGET_ESP32P4
extern "C" volatile bool g_lgfx_skip_data_msync;
#endif

void pushFrame(uint16_t* displayBuffer, int displayField,
               Renderer::Sprite2D* const* sprites, int spriteCount) {
    uint16_t* buffers[2] = { lineBufferA, lineBufferB };
    int curBuf = 0;

#if CONFIG_IDF_TARGET_ESP32P4
    // Ping-pong lineBuffer: fill+msync buffer A while DMA is transmitting buffer B.
    // This hides the per-row esp_cache_msync(data) cost behind the SPI transfer.
    //
    // We byte-swap pixels inline (panel wants swap565) and use writePixelsDMA
    // with swap=false, which takes the no_convert path in Panel_LCD/Bus_SPI and
    // DMAs our buffer DIRECTLY (bypassing the shared _flip_buffer that
    // pushPixelsDMA would otherwise use — which would invalidate our
    // skip_data_msync contract).
    const uint32_t rowBytes = RENDER_WIDTH * sizeof(uint16_t);
    tft.startWrite();
    g_lgfx_skip_data_msync = true;  // we pre-sync each row manually below
    for (int y = displayField; y < RENDER_HEIGHT; y += RENDER_FIELDS) {
        uint16_t* buf = buffers[curBuf];
#if FIELD_BUFFERS
        const int physRow = y >> 1;
#else
        const int physRow = y;
#endif
#if HALF_WIDTH_BUFFERS
        uint32_t* buf32 = (uint32_t*)buf;
        uint16_t* fieldBuffer = &displayBuffer[physRow * (RENDER_WIDTH / 2)];
        // Fast path: no sprites — write pairs of identical pixels.
        // Slow path (sprite rows): expand to per-output-pixel.
        // Determine whether any sprite covers this scanline once per row.
        bool anySprite = false;
        for (int si = 0; si < spriteCount && !anySprite; ++si) {
            const Renderer::Sprite2D* sp = sprites[si];
            if (!sp || !sp->enabled || !sp->material) continue;
            const int sprH = sp->sourceHeight() * sp->scale;
            if (y >= sp->y && y < sp->y + sprH) anySprite = true;
        }
        if (!anySprite) {
            for (int x = 0; x < RENDER_WIDTH / 2; x++) {
                uint16_t pixel = fieldBuffer[x];
                uint16_t swapped = (uint16_t)((pixel << 8) | (pixel >> 8));
                buf32[x] = ((uint32_t)swapped << 16) | swapped;
            }
        } else {
            // Expand to full-res: each half-width source pixel becomes two
            // adjacent output pixels, then sprites composite over the top.
            for (int xOut = 0; xOut < RENDER_WIDTH; ++xOut) {
                uint16_t pixel = fieldBuffer[xOut >> 1];
                buf[xOut] = (uint16_t)((pixel << 8) | (pixel >> 8));
            }
            Renderer::compositeSprites(buf, RENDER_WIDTH, y, sprites, spriteCount, true);
        }
#else
        uint16_t* fieldBuffer = &displayBuffer[physRow * RENDER_WIDTH];
        for (int x = 0; x < RENDER_WIDTH; x++) {
            uint16_t pixel = fieldBuffer[x];
            buf[x] = (uint16_t)((pixel << 8) | (pixel >> 8));
        }
#endif
        // Pre-flush this buffer to DRAM. Runs concurrently with DMA of the
        // other buffer.
        esp_cache_msync(buf, rowBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

        tft.setAddrWindow(RENDER_LEFT, RENDER_TOP + y, RENDER_WIDTH, 1);
        tft.writePixelsDMA(buf, RENDER_WIDTH, false);  // swap=false -> direct DMA
        curBuf ^= 1;
    }
    g_lgfx_skip_data_msync = false;
    tft.endWrite();
#else
    // S3: prepare byte-swapped rows in the DMA queue, sleeping between
    // refills so core 0 can rasterize. Retain ping-pong polling as fallback.
    // swap=false avoids LGFX's flip-buffer copy; these rows use internal SRAM.
    if (queuedScanout) scanout.begin(displayField);
    else tft.startWrite();
    for (int y = displayField; y < RENDER_HEIGHT; y += RENDER_FIELDS) {
        while (queuedScanout && !scanout.freeSlots()) { scanout.kick(); scanout.waitForSpace(); }
        uint16_t* buf = queuedScanout ? scanout.nextRow() : buffers[curBuf];
#if FIELD_BUFFERS
        const int physRow = y >> 1;
#else
        const int physRow = y;
#endif
#if HALF_WIDTH_BUFFERS
        uint16_t* fieldBuffer = &displayBuffer[physRow * (RENDER_WIDTH / 2)];
        bool anySprite = false;
        for (int si = 0; si < spriteCount && !anySprite; ++si) {
            const Renderer::Sprite2D* sp = sprites[si];
            if (!sp || !sp->enabled || !sp->material) continue;
            const int sprH = sp->sourceHeight() * sp->scale;
            if (y >= sp->y && y < sp->y + sprH) anySprite = true;
        }
        Renderer::expandSwapRGB565(buf, fieldBuffer, RENDER_WIDTH / 2);
        if (anySprite) Renderer::compositeSprites(buf, RENDER_WIDTH, y, sprites, spriteCount, true);
#else
        uint16_t* fieldBuffer = &displayBuffer[physRow * RENDER_WIDTH];
        for (int x = 0; x < RENDER_WIDTH; x++) {
            uint16_t pixel = fieldBuffer[x];
            buf[x] = (uint16_t)((pixel << 8) | (pixel >> 8));
        }
#endif
        if (queuedScanout) {
            scanout.publish();
            if (!scanout.freeSlots()) scanout.kick();
        } else {
            tft.setAddrWindow(RENDER_LEFT, RENDER_TOP + y, RENDER_WIDTH, 1);
            tft.writePixelsDMA(buf, RENDER_WIDTH, false);
            curBuf ^= 1;
        }
    }
    if (queuedScanout) { scanout.kick(); scanout.finish(); }
    else tft.endWrite();
#endif
}

} // namespace Display
