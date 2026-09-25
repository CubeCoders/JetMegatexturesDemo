idf_component_register(SRCS "Display.cpp" "Runtime.cpp"
    INCLUDE_DIRS "." "${JET_CONFIG_INCLUDE_DIR}"
    REQUIRES Jet LovyanGFX esp_timer esp_mm esp_driver_gpio freertos)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -fno-math-errno)
idf_component_get_property(jet_lib Jet COMPONENT_LIB)
target_compile_options(${jet_lib} PRIVATE -fno-math-errno)
if(NOT IDF_TARGET STREQUAL "esp32s3" AND NOT IDF_TARGET STREQUAL "esp32p4")
    message(FATAL_ERROR "These display paths support ESP32-S3 and ESP32-P4 only")
endif()
if(CONFIG_FREERTOS_UNICORE)
    message(FATAL_ERROR "The ESP32 Jet runtime requires both cores")
endif()
