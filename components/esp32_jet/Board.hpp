#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ST7796 SPI reference wiring. Adapt these pins for your board. SPI2 is
// required by the S3 queued scanout; the bus must be dedicated to the LCD.
namespace Board {
#if CONFIG_IDF_TARGET_ESP32S3
constexpr int clock = 46, mosi = 3, dc = 8, cs = 17, reset = 18, backlight = 9;
constexpr int spiMode = 1;
#elif CONFIG_IDF_TARGET_ESP32P4
constexpr int clock = 20, mosi = 5, dc = 23, cs = 7, reset = 8, backlight = 21;
constexpr int spiMode = 0;
#else
#error "Select esp32s3 or esp32p4"
#endif
}

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796 panel;
    lgfx::Light_PWM light;
    lgfx::Bus_SPI bus;
public:
    LGFX() {
        auto b = bus.config();
        b.spi_host = SPI2_HOST;
        b.spi_mode = Board::spiMode;
        b.freq_write = 80000000;
        b.freq_read = 16000000;
        b.spi_3wire = false;
        b.use_lock = true;
        b.dma_channel = SPI_DMA_CH_AUTO;
        b.pin_sclk = Board::clock;
        b.pin_mosi = Board::mosi;
        b.pin_miso = -1;
        b.pin_dc = Board::dc;
        bus.config(b);
        panel.setBus(&bus);
        auto p = panel.config();
        p.pin_cs = Board::cs;
        p.pin_rst = Board::reset;
        p.pin_busy = -1;
        p.panel_width = 320;
        p.panel_height = 480;
        p.offset_x = p.offset_y = p.offset_rotation = 0;
        p.dummy_read_pixel = 8;
        p.dummy_read_bits = 1;
        p.readable = false;
        p.invert = true;
        p.rgb_order = false;
        p.dlen_16bit = false;
        p.bus_shared = false;
        panel.config(p);
        auto l = light.config();
        l.pin_bl = Board::backlight;
        l.invert = false;
        l.freq = 44100;
        l.pwm_channel = 7;
        light.config(l);
        panel.setLight(&light);
        setPanel(&panel);
    }
};
