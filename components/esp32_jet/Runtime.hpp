#pragma once
#include "Scene.hpp"

namespace Esp32Jet {
// Call once from app_main. The runtime owns the Scene and framebuffers.
// init runs on core 0 after display setup; update runs on core 1 before each
// render, with actual elapsed seconds. Keep borrowed objects/camera alive.
// Optional afterRender runs on core 1 after rendering/picking, before publishing
// the completed field. It may update sprite state; borrowed texture pixels must
// stay immutable during scanout. Its work is excluded from render timing.
// The callbacks must not access the panel or start their own render passes.
using Init = void (*)(Renderer::Scene&);
using Update = void (*)(float seconds);
// Optional renderEffects appends geometry (for example ParticleSystem::render)
// to the completed scene field, on core 1 after raster workers have joined.
// Return the additional accepted triangle count. Its cost is included in MS and
// TRI/S. Do not clear, swap or advance the scene, or mutate pixels used by scanout.
using RenderEffects = unsigned (*)(Renderer::Scene& scene);
void start(Init init, Update update, Update afterRender = nullptr,
           RenderEffects renderEffects = nullptr);
}
