#pragma once
#if CONFIG_IDF_TARGET_ESP32S3
#include <atomic>
#include <esp_intr_alloc.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/gdma_reg.h>
#include <soc/interrupts.h>

// Eight prepared rows keep the SPI peripheral fed while core 0 does other work.
// The panel/bus is exclusively owned from begin() until finish(). Init and
// submission must run on the same core as the completion interrupt.
class S3Scanout {
public:
    static constexpr unsigned Capacity = 8;
    static constexpr unsigned Width = Display::RENDER_WIDTH;
    static constexpr unsigned Height = Display::RENDER_HEIGHT;
    static constexpr unsigned Fields = Display::RENDER_FIELDS;
    uint16_t* rows[Capacity] = {};
    unsigned underruns = 0;
    bool init(LGFX* display, uint16_t* a, uint16_t* b) {
        tft = display; rows[0] = a; rows[1] = b;
        ownerCore = xPortGetCoreID();
        const int channel = lgfx::search_dma_out_ch(SOC_GDMA_TRIG_PERIPH_SPI2);
        if (channel < 0) return false;
        const unsigned stride = GDMA_OUT_INT_ENA_CH1_REG - GDMA_OUT_INT_ENA_CH0_REG;
        enable = (volatile uint32_t*)(GDMA_OUT_INT_ENA_CH0_REG + stride * channel);
        clear = (volatile uint32_t*)(GDMA_OUT_INT_CLR_CH0_REG + stride * channel);
        if (*enable != 0) return false;
        for (unsigned i = 2; i < Capacity; ++i) {
            rows[i] = (uint16_t*)heap_caps_aligned_alloc(16, Width * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
            if (!rows[i]) { releaseRows(); return false; }
        }
        const esp_err_t error = esp_intr_alloc_intrstatus(ETS_DMA_OUT_CH0_INTR_SOURCE + channel,
            ESP_INTR_FLAG_LEVEL1, GDMA_OUT_INT_ST_CH0_REG + stride * channel,
            GDMA_OUT_TOTAL_EOF_CH0_INT_ST, interrupt, this, &handle);
        printf("Queued scanout: DMA channel %d, %u rows, interrupt status %d\n",
               channel, Capacity, (int)error);
        if (error != ESP_OK) releaseRows();
        return error == ESP_OK;
    }
    void begin(int field) {
        configASSERT(xPortGetCoreID() == ownerCore);
        producer = xTaskGetCurrentTaskHandle();
        ulTaskNotifyTake(pdTRUE, 0);
        produced.store(0); consumed.store(0);
        parity = field; active = false; underruns = 0;
        rowCount = (Height - field + Fields - 1) / Fields;
        tft->startWrite();
        *enable = 0; *clear = UINT32_MAX;
    }
    unsigned freeSlots() const { return Capacity - (produced.load() - consumed.load()); }
    uint16_t* nextRow() { return rows[produced.load() % Capacity]; }
    void publish() { produced.fetch_add(1, std::memory_order_release); }
    void kick() {
        // Only the producer task and this core's ISR touch the panel.
        portENTER_CRITICAL(&lock);
        if (!active && consumed.load() < produced.load()) {
            active = true;
            launch();
        }
        portEXIT_CRITICAL(&lock);
    }
    void waitForSpace() {
        if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000))) {
            printf("Scanout timeout: produced=%u consumed=%u\n", produced.load(), consumed.load());
            abort();
        }
    }
    void finish() {
        while (consumed.load(std::memory_order_acquire) != rowCount) waitForSpace();
        *enable = 0;
        tft->endWrite();
    }
private:
    void releaseRows() {
        for (unsigned i = 2; i < Capacity; ++i) { heap_caps_free(rows[i]); rows[i] = nullptr; }
    }
    LGFX* tft = nullptr;
    intr_handle_t handle = nullptr;
    TaskHandle_t producer = nullptr;
    std::atomic<unsigned> produced{0}, consumed{0};
    volatile uint32_t* enable = nullptr;
    volatile uint32_t* clear = nullptr;
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    volatile bool active = false;
    int parity = 0;
    int ownerCore = 0;
    unsigned rowCount = 0;
    void launch() {
        // init() runs after the black startup row has allocated LGFX's DMA descriptor
        // for a screen-width row. These fixed-size transfers never resize it.
        // The interrupt deliberately lacks ESP_INTR_FLAG_IRAM: LGFX panel
        // commands reside in flash, so IDF masks it while flash cache is off.
        const unsigned row = consumed.load(std::memory_order_relaxed);
        tft->setAddrWindow(Display::RENDER_LEFT, Display::RENDER_TOP + parity + row * Fields, Width, 1);
        *clear = GDMA_OUT_TOTAL_EOF_CH0_INT_CLR;
        *enable = GDMA_OUT_TOTAL_EOF_CH0_INT_ENA;
        tft->writePixelsDMA(rows[row % Capacity], Width, false);
    }
    static void interrupt(void* arg) { static_cast<S3Scanout*>(arg)->completed(); }
    void completed() {
        *enable = 0; *clear = GDMA_OUT_TOTAL_EOF_CH0_INT_CLR;
        // GDMA EOF precedes the last SPI FIFO bytes leaving the wire.
        tft->waitDMA();
        const unsigned done = consumed.fetch_add(1, std::memory_order_release) + 1;
        if (done < produced.load(std::memory_order_acquire)) launch();
        else { active = false; if (done != rowCount) ++underruns; }
        if (done % 4 == 0 || !active) {
            BaseType_t woke = pdFALSE;
            vTaskNotifyGiveFromISR(producer, &woke);
            if (woke) portYIELD_FROM_ISR();
        }
    }
};
#endif
