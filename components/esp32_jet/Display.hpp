#pragma once

#include <stdint.h>
#include <stddef.h>
#include "Sprite2D.hpp"

// Display: panel init, framebuffer ownership, and per-frame buffer swapping
// plus push-to-panel. Keeps all LGFX / DMA / ping-pong plumbing out of main.
namespace Display {

constexpr int SCREEN_WIDTH  = 480;
constexpr int SCREEN_HEIGHT = 320;
constexpr int EDGE_INSET_X  = 0;
constexpr int EDGE_INSET_Y  = 0;
constexpr int RENDER_WIDTH  = SCREEN_WIDTH  - EDGE_INSET_X;
constexpr int RENDER_HEIGHT = SCREEN_HEIGHT - EDGE_INSET_Y;
constexpr int RENDER_TOP    = (SCREEN_HEIGHT - RENDER_HEIGHT) / 2;
constexpr int RENDER_LEFT   = (SCREEN_WIDTH  - RENDER_WIDTH ) / 2;
constexpr int RENDER_FIELDS = 2;

// Power up the panel (board-specific), initialise LGFX, prime DMA,
// and allocate all framebuffers / line buffers / z-buffer. Returns false if
// any allocation fails.
bool init();
// True when scanout sleeps between DMA refills, leaving core 0 available.
bool supportsRasterWorker();

// Initial framebuffer pointer handed to Scene at construction time. The
// renderer's actual write target is reassigned each frame via beginFrame().
uint16_t* initialFramebuffer();

// Optional depth buffer (nullptr when Z_BUFFERING is disabled).
uint16_t* zBuffer();

struct Frame {
    uint16_t* renderBuffer;   // Where the render task should write this frame.
    uint16_t* displayBuffer;  // The buffer the display pass should push now.
    int       displayField;   // Parity of the field held in displayBuffer.
};

// Pick render/display buffers for the next frame. With FIELD_BUFFERS enabled
// the even/odd half-height buffers alternate every frame so CPU and DMA never
// touch the same memory. Otherwise both pointers reference the single
// framebuffer and only displayField flips. Call once per loop iteration.
Frame beginFrame();

// Push displayBuffer (of the given field parity) to the panel using the
// SoC-appropriate byte-swap + DMA path (queued rows on S3 when enabled).
//
// On HALF_WIDTH_BUFFERS builds, sprites are composited at full output
// resolution during scanout rather than pre-written into the half-width
// framebuffer. Pass the scene's sprite list here (scene->getSprites().data(),
// scene->getSprites().size()) so HUD elements are pixel-perfect.
// Omit (or pass nullptr/0) for backward compatibility.
void pushFrame(uint16_t* displayBuffer, int displayField,
               Renderer::Sprite2D* const* sprites = nullptr,
               int spriteCount = 0);

} // namespace Display
