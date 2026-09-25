idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." "firmware" REQUIRES esp32_jet esp_driver_gpio EMBED_FILES "assets.bin")
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
option(CHAPEL_BENCHMARK "Run cached/flash ABBA measurements before the chapel tour" OFF)
option(CHAPEL_USE_CACHE "Allocate and use the experimental DRAM hot-tile cache during normal playback" OFF)
option(CHAPEL_PSRAM_BENCH "Compare direct flash versus PSRAM texture backing" OFF)
option(CHAPEL_PSRAM_BACKING "Copy the complete indexed texture pack to PSRAM at startup" ON)
target_compile_definitions(${COMPONENT_LIB} PRIVATE CHAPEL_BENCHMARK=$<BOOL:${CHAPEL_BENCHMARK}>)
target_compile_definitions(${COMPONENT_LIB} PRIVATE CHAPEL_USE_CACHE=$<BOOL:${CHAPEL_USE_CACHE}>)
target_compile_definitions(${COMPONENT_LIB} PRIVATE CHAPEL_PSRAM_BENCH=$<BOOL:${CHAPEL_PSRAM_BENCH}> CHAPEL_PSRAM_BACKING=$<BOOL:${CHAPEL_PSRAM_BACKING}>)

option(CHAPEL_FILTER_BENCH "Compare nearest/bilinear/three-point on the same PSRAM tiled path" OFF)
target_compile_definitions(${COMPONENT_LIB} PRIVATE CHAPEL_FILTER_BENCH=$<BOOL:${CHAPEL_FILTER_BENCH}>)

option(CHAPEL_PREFILTER_BENCH "Measure an exact 2 MiB prefiltered top-mip window lookup" OFF)
target_compile_definitions(${COMPONENT_LIB} PRIVATE CHAPEL_PREFILTER_BENCH=$<BOOL:${CHAPEL_PREFILTER_BENCH}>)

option(CHAPEL_HOT_FILTER "Nearest fallback with an incremental hot cache of bilinear UV tiles" ON)
target_compile_definitions(${COMPONENT_LIB} PRIVATE CHAPEL_HOT_FILTER=$<BOOL:${CHAPEL_HOT_FILTER}>)

option(CHAPEL_BUTTONS "Enable the reference board's shift-register buttons" OFF)
target_compile_definitions(${COMPONENT_LIB} PRIVATE CHAPEL_BUTTONS=$<BOOL:${CHAPEL_BUTTONS}>)
