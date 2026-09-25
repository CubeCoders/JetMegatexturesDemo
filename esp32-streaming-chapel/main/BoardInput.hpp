#pragma once
#include "CameraControls.hpp"
#ifndef CHAPEL_BUTTONS
#define CHAPEL_BUTTONS 0
#endif
#if CHAPEL_BUTTONS && CONFIG_IDF_TARGET_ESP32S3
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#endif

namespace ChapelInput {
// Same one-hot shift-register scan and button bits as jet32/main/main.cpp.
inline void init() {
#if CHAPEL_BUTTONS && CONFIG_IDF_TARGET_ESP32S3
    for (auto pin : {GPIO_NUM_20,GPIO_NUM_19,GPIO_NUM_47}) {
        gpio_reset_pin(pin); gpio_set_direction(pin,GPIO_MODE_OUTPUT); gpio_set_level(pin,0);
    }
    gpio_reset_pin(GPIO_NUM_21); gpio_set_direction(GPIO_NUM_21,GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_21,GPIO_PULLDOWN_ONLY);
#endif
}
inline uint8_t read() {
    uint8_t buttons=0;
#if CHAPEL_BUTTONS && CONFIG_IDF_TARGET_ESP32S3
    for (unsigned b=0;b<8;++b) {
        gpio_set_level(GPIO_NUM_20,0);
        const uint8_t mask=uint8_t(1u<<b);
        for (int i=7;i>=0;--i) {
            gpio_set_level(GPIO_NUM_19,0);
            gpio_set_level(GPIO_NUM_47,(mask>>i)&1);
            gpio_set_level(GPIO_NUM_19,1);
        }
        gpio_set_level(GPIO_NUM_20,1); ets_delay_us(5);
        buttons |= uint8_t(gpio_get_level(GPIO_NUM_21)<<b);
    }
#endif
    static ChapelView::Debouncer debounce;
    return debounce.read(buttons);
}
}
