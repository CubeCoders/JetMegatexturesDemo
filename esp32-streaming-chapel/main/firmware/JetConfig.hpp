#pragma once
// Shared by every translation unit in this example. Keep FIELD_BUFFERS=1:
// the runtime overlaps rendering with scanout of the previous field.
// HALF_WIDTH_BUFFERS=1 uses two 76,800-byte buffers at 480x320 output.
// See Jet/src/JetConfig.example.hpp for descriptions of renderer options.
#define RENDER_TILE_BUFFER 0
#define JET32_S3_PARALLEL_RASTER 1
#define TILE_WIDTH 32
#define TILE_HEIGHT 32
#define FAST_Z 1
#define LAZY_Z 0
#define SCREEN_DOOR_ALPHA 0
#define SKIP_ZERO_AREA_TRIANGLES 1
#define NOISE_ALPHA 0
#define Z_BUFFERING 0
#define SORT_TRIANGLES 0
#define SORT_SCENE_OBJECTS 0
#define SORT_SCENE_REVERSE 0
#define DEPTH_ALPHA_BLEND 0
#define TEXTURE_MAPPING 1
#define PERSPECTIVE_CORRECT_TEXTURES 1
#define BILINEAR_FILTER 1
#define LIGHTING 0
#define Z_BRIGHTNESS 0
#define FLOAT_CAMERA_ANGLES 1
#define FLOAT_SIN_CACHE_SCALE 10
#define FLOAT_TAN_CACHE_SCALE 1
#define POSTFX_CRT 0         // CRT scanline effect
#define POSTFX_CELLSHADING 0 // Cell shading effect
#define POSTFX_ANTIALIASING 0 // FXAA anti-aliasing
#define POSTFX_BLOOM 0       // Bloom effect (requires additional buffer)
#define POSTFX_MOTION_BLUR 0 // Motion blur effect (requires additional buffer)
#define POSTFX_CHROMATIC 0   // Chromatic aberration
#define POSTFX_PIXELATE 0    // Pixelation effect
#define DEBUG_OVERDRAW 0     // Visualize overdraw using orange blending
#define CRT_SCANLINE_INTENSITY 48    // Intensity of CRT scanlines (0-255)
#define MOTION_BLUR_STRENGTH 50      // Strength of motion blur (0-100)
#define CHROMATIC_OFFSET 2           // Pixel offset for chromatic aberration
#define PIXELATE_SIZE 4              // Size of pixelation blocks
#define CELLSHADING_CELL_BITS 4      // Reduce precision of lighting to create a cell shading effect (0-8)
#define zBrightScale 48
#define HALF_WIDTH_BUFFERS 1
#define FIELD_BUFFERS 1
#define SSR_FIELD_REFLECT 1
#define CHECKERBOARD_MODE 0
#define CHECKERBOARD_RECONSTRUCTION 0
#define MAX_PICK_QUERIES 0
#define zBrightFar 1600
#define zBrightNear 200
#define depthFogFar 8192
#define depthFogNear 6144
#if defined(ESP_PLATFORM)
#define ESP32
#endif

#define JET_MINIMAL_PERFORMANCE_OVERLAY 1
